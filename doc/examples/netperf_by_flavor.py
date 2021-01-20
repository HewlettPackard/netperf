#!/usr/bin/python -u

#  Copyright 2021 Hewlett Packard Enterprise Development LP
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.


import os
import subprocess
import re
import stat
import logging
import socket
import argparse
from os import environ
from novaclient.v1_1 import Client
from time import sleep, time, strftime, localtime, gmtime
import paramiko

class TestNetperf() :
    """
    Will attempt to launch some instances and then run netperf in them
    Leverages heavily from scripts pointed-at by Gavin Brebner
    """

    def __init__(self) :
        self.verbose = 1
        self.sshopts = " -o StrictHostKeyChecking=no -o HashKnownHosts=no -o ConnectTimeout=30 "

        self.os = None
        self.ip_pool = []
        self.vm_pool = []
        self.adminPasses = dict()
        self.suitable_images = []
        self.chosen_image = None
        self.keypair = None
        self.start = time()
        self.this_run_started = strftime("%Y-%m-%d-%H-%M",gmtime()) + "/"

    def fail(self,message) :
        """
        print out a final utterance and die
        """
        logging.critical(message)
        print strftime("%s, %d, %b %Y %H:%M:%S", localtime())+" FAILURE "+message
        os._exit(1)


    def do_in_shell(self, command, may_fail = False) :
        """
        do a task in the shell. by default it must succeed
        """
        p=subprocess.Popen(command,
                           shell=True,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        (dout, eout)=p.communicate()
        if (eout == None) :
            eout=""

        if (p.returncode != 0) and (may_fail == False):
            logging.debug("Shell command '%s' failed with eout '%s' and dout '%s'",
                          command, eout, dout)
            raise RuntimeError

        return dout

    # we will need to handle IDs that are UUIDs at some point
    def find_suitable_images(self) :
        """
        Try to find a suitable image to use for the testing
        """
        image_regex = None
        image_id = None
        if self.args.image :
            img = self.args.image
        else:
            try:
                img = environ['IMAGE']
            except:
                img = "netperf"

        try:
            image_id = int(img)
        except:
            image_regex =  img

        for image in self.os.images.list() :
            if ((image_regex and re.search(image_regex,image.name))
                or
                (image_id and (image_id == int(image.id)))) :
                self.suitable_images.append(image)


    def await_server_pool_gone(self,time_limit) :
        """
        Keep checking the status of the server pool until it is gone or
        our patience has run-out.  Alas, novaclient.v1_1 is not sophisticated
        enough to return "ENOEND" or somesuch as a status when the specified
        server is well and truly gone.  So we get status until there is an
        exception raise, which we interpret as "server gone"
        """
        waiting_started = time()
        now = time()
        sleeptime = 1

        while ((len(self.vm_pool) > 0) and
               ((now - waiting_started) < time_limit)) :
            waiter = self.vm_pool.pop()
            try:
                waiter = self.os.servers.get(waiter.id)

                if (re.search(r"ERROR",waiter.status)) :
                    self.clean_up_overall()
                    self.fail("Instance id " + str(waiter.id) + " name " + str(waiter.name) + " errored during deletion " + str(waiter.status))

                self.vm_pool.append(waiter)
                sleep(sleeptime)
                sleeptime *= 2
                now = time()
            except :
                sleeptime = 1
                continue

        if (len(self.vm_pool) > 0) :
            self.clean_up_overall()
            self.fail(str(len(self.vm_pool)) + " failed to achieve 'deleted' status within " + str(time_limit) + " seconds")

    def associate_public_ips(self) :
        """
        Associate our public IPs with instances
        """
        for (vm, ip) in zip(self.vm_pool, self.ip_pool) :
            vm.add_floating_ip(ip.ip)


    def disassociate_public_ips(self) :
        """
        Break the association between an instance and its floating IP
        """
        for (vm, ip) in zip(self.vm_pool, self.ip_pool) :
            vm.remove_floating_ip(ip.ip)

    def extract_public_ip(self, vm) :
        """
        Find the public IP of a VM
        """
        if "rackspace" in self.env('OS_AUTH_SYSTEM',default=[]):
            ip = vm.accessIPv4
        else:
            try:
                #print "trying networks['hpcloud']"
                iplist = vm.networks['hpcloud']
            except Exception as e2 :
                #print "exception 2"
                try :
                    #print "trying networks['private']"
                    iplist = vm.networks['private']
                except Exception as e3 :
                    #print "exception 3"
                    logging.warning("Unable to find a public IP for %s (%s:%s:$s)",
                                    vm.name, e, e2, e3)
                    raise RuntimeError
            ip = iplist[1]

        #print "returning %s" % ip
        return ip

    def deallocate_server_pool(self) :
        """
        Issue delete requests on any and all existing servers
        """
#        print "Length of server pool %d" %len(self.vm_pool)

        for server in self.vm_pool :
            try:
                try:
                    # try to remove the public ip for this server from
                    # the known hosts file but don't get too hung up
                    # about it if we cannot.  If we happen to
                    # encounter this IP again in another test we will
                    # rely on its being removed before first use to
                    # cover things
                    public_ip = self.extract_public_ip(server)
                    self.known_hosts_ip_remove(public_ip)
                except:
                    pass
                self.os.servers.delete(server)
            except Exception as e:
                logging.warning("Unable to delete server %s. Error: %s",
                                server.name, e)

    def allocate_server_pool(self, count, flavor, image) :
        """
        Allocate the number of requested servers of the specified
        flavor and image, and with the specified key name
        """

        #print "before server_pool opening warning"
        keyname = None
        secgroup = None
        if self.keypair:
            keyname = self.keypair.name
        if self.security_group:
            secgroup = [self.security_group.name]

        logging.warning("Allocating %d servers of flavor '%s' with image name '%s'",
                        count, flavor.name, image.name)
        #print "after same"

        # instance number (filled-in later) time flavorname imagename
        basename = "netperftest%s_%s_%s_%s" % ("%d",str(self.start),flavor.name,image.name)
        for i in xrange(count) :
            thisname = basename % i
            try:
                newvm = self.os.servers.create(name = thisname,
                                               image = image,
                                               flavor = flavor,
                                               key_name = keyname,
                                               security_groups = secgroup)
                #print "adminPass is %s" % newvm.adminPass
                self.adminPasses[newvm.id]=newvm.adminPass
                self.vm_pool.append(newvm)

            except Exception as e :
                logging.warning("Error allocating instance: %s" , e)
                raise RuntimeError



    def await_server_pool_ready(self, time_limit) :
        """
        Wait until all the servers in the server pool are in an "ACTIVE" state.
        If they happen to come-up in an "ERROR" state we are toast.  If it
        takes too long for them to transition to the "ACTIVE" state we are
        similarly toast
        """
        waiters = list(self.vm_pool)
        actives = []
        waiting_started = time()
        deadline = waiting_started + time_limit
        sleeptime = 1

        #print "awaiting server pool active"
        while (len(waiters) > 0) :

            waiter = waiters.pop()
            waiter = self.os.servers.get(waiter.id)
            print "elapsed %d server %s state %s" % ((time() - waiting_started), waiter.name, waiter.status)
            if (waiter.status == "ACTIVE") :
                #print "Extracting public IP"
                ip_address = self.extract_public_ip(waiter)
                #print "got IP address from extract"
                #print "IP address is %s" % ip_address
                self.known_hosts_ip_remove(ip_address)
                actives.append(waiter)
                sleeptime = 1
                continue
            if (re.search(r"ERROR",waiter.status)) :
                logging.warning("Instance id %s name %s encountered an error during instantiation of %s",
                                waiter.id, waiter.name, waiter.status)
                raise RuntimeError

            if (time() >= deadline):
                break

            waiters.append(waiter)
            sleep(sleeptime)
            sleeptime *= 2

            sleeptime = min(120, deadline - time(), sleeptime)


        if (len(waiters) > 0) :
            logging.warning("%d waiters failed to acheive ACTIVE status within %d seconds",
                            len(waiters), time_liit)
            raise RuntimeError

        # I wonder what the right way to do this is
        self.vm_pool = []
        self.vm_pool = list(actives)

    def print_ip_pool(self) :
        """
        Display the current contents of the IP pool
        """
        logging.debug("Contents of IP pool:")
        for ip in self.ip_pool :
            logging.debug("IP: %s  ID: %s",
                          ip.ip, ip.id)

    def known_hosts_ip_remove(self, ip_address) :
        """
        Remove an IP address from the known_hosts file
        """
        cmd = "ssh-keygen -f ~/.ssh/known_hosts -R " + str(ip_address) + " > /dev/null"
        logging.debug("Removing %s from ~/.ssh/known_hosts using command '%s'",
                      ip_address, cmd)
        self.do_in_shell(cmd,True)


    def deallocate_ip_pool(self) :
        """
        Return the contents of self.ip_pool to the sea
        """
        while (len(self.ip_pool) > 0) :
            ip = self.ip_pool.pop()
            try:
                self.os.floating_ips.delete(ip.id)
            except Exception as e:
                logging.warning("Unable to deallocate IP: %s  ID: %s  Error: %s",
                                ip.ip, ip.id, e)

    def allocate_ip_pool(self, count) :
        """
        Allocate a pool of public IPs to be used when perf testing
        """

        for i in xrange(count) :
            try :
                ip = self.os.floating_ips.create()
                self.ip_pool.append(ip)
            except Exception as e:
                self.deallocate_ip_pool()
                self.fail("Error while allocating %d IPs '%s'" %
                          (count, str(e)))
            self.known_hosts_ip_remove(str(ip.ip))

    def clean_vestigial_keypairs(self) :
        """
        Seek-out and delete vestigial keypairs matching our magic
        regular expression
        """

        for keypair in self.os.keypairs.list() :
            if (re.match("net[0-9]+perf[0-9]+dot[0-9]+", str(keypair.name))) :
                logging.info("Deleting vestigial keypair named %s",
                             keypair.name)
                try :
                    self.os.keypairs.delete(keypair)
                    os.remove(str(keypair.name) + ".pem")
                except Exception as e:
                    logging.warning("Exception while purging keypair %s '%s'",
                                    keypair.name,e)
                    continue

    def create_security_group(self):

        #print "Creating security group"

        self.security_group = self.os.security_groups.create("netperftesting " + str(self.start),"A rather open security group for netperf testing debugging")

        self.os.security_group_rules.create(parent_group_id = self.security_group.id,
                                            ip_protocol = 'tcp',
                                            from_port = 1,
                                            to_port = 65535)

        self.os.security_group_rules.create(parent_group_id = self.security_group.id,
                                            ip_protocol = 'udp',
                                            from_port = 1,
                                            to_port = 65535)

        self.os.security_group_rules.create(parent_group_id = self.security_group.id,
                                            ip_protocol = 'icmp',
                                            from_port = -1,
                                            to_port = -1)

    def allocate_key(self) :
        """
        Allocate the private key to be used with instances for this
        session of testing, and write it to a file
        """

        pid = self.do_in_shell("echo $$")

        self.uniquekeyname = re.sub(r"\n", "", "net" + str(pid) +
                                    "perf" + str(self.start))
        self.uniquekeyname = re.sub(r"\.", "dot", self.uniquekeyname)

        #print "Allocating key %s" % self.uniquekeyname

        self.keypair = self.os.keypairs.create(self.uniquekeyname)

        self.keyfilename = self.keypair.name + ".pem"

        keyfile = None
        try :
            keyfile = open(self.keyfilename, 'w')
        except Exception as e:
            if (keyfile != None) :
                keyfile.close()
            self.fail("Error opening " + self.keyfilename +
                      "for writing: " + str(e))

        try :
            keyfile.write(self.keypair.private_key)
        except Exception as e:
            self.fail("Error writing to " + self.keyfilename + str(e))
        finally:
            keyfile.close()

        os.chmod(self.keyfilename, stat.S_IRWXU)

    # lifted quite liberally from python-novaclient/novaclient/utils.py
    def env(self, *args, **kwargs):
        """
        returns the first environment variable set
        if none are non-empty, defaults to '' or keyword arg default
        """
        for arg in args:
            value = os.environ.get(arg, None)
            if value:
                return value

        if kwargs.get('failure',None):
            print "found failure"
            self.fail(kwargs.get('failure'))

        return kwargs.get('default', '')

    def setup_parser(self) :
        parser = argparse.ArgumentParser()
        parser.add_argument("-f", "--flavor",
                            help="Specify the flavor to test")
        parser.add_argument("--image",
                            help="Specify the image type to test")
        parser.add_argument("--region",
                            default=self.env('OS_REGION_NAME',
                                             'NOVA_REGION_NAME',
                                             failure="A Nova region name must be specified via either --region, OS_REGION_NAME or NOVA_REGION_NAME"),
                            help="Specify the region in which to test")
        parser.add_argument("--username",
                            default=self.env('OS_USERNAME',
                                             'NOVA_USERNAME',
                                             failure="A username for Nova access must be specified either via --username, OS_USERNAME, or NOVA_USERNAME"),
                            help="The name of the user to use")
        parser.add_argument("--password",
                            default=self.env('OS_PASSWORD',
                                             "NOVA_PASSWORD",
                                             failure="A password for Nova access must be specified via either --password, OS_PASSWORD or NOVA_PASSWORD"),
                            help="The password for the Nova user")
        parser.add_argument("--url",
                            default=self.env('OS_AUTH_URL',
                                             'NOVA_URL',
                                             failure="A URL for authorization must be provided via either --url, OS_AUTH_URL or NOVA_URL"),
                            help="The URL for Nova")
        parser.add_argument("--project",
                            default=self.env('OS_TENANT_NAME',
                                             'NOVA_PROJECT_ID',
                                             failure="You must provide a project via --project, OS_TENANT_NAME or NOVA_PROJECT_ID"),
                            help="The ID for the Nova project")

        parser.add_argument("--archivepath",
                            help="The path to where the archive is to be kept")
        parser.add_argument("--collectdsockname",
                            help="The name of the collectd socket.")
        parser.add_argument("--instanceuser",
                            default=self.env('INSTANCE_USERNAME', default='ubuntppppu'),
                            help="The name of the user on the instance(s)")

        return parser

    def get_flavor(self) :
        flav = None
        if self.args.flavor :
            flav = self.args.flavor
        else:
            try:
                self.flavor_name = environ['FLAVOR']
            except:
                return

        try:
            self.flavor_id = int(flav)
        except:
            self.flavor_name = flav
      
    def get_collectd_sock(self) :
        self.collectd_socket = None
        if self.args.collectdsockname :
            self.collectd_socket_name = self.args.collectdsockname

        try:
            self.collectd_socket_name = environ['COLLECTD_SOCK_NAME']
        except:
#            self.collectd_socket_name = "/opt/collectd/collectd-socket"
            return

        if self.collectd_socket_name:
            try:
                self.collectd_socket = socket.socket(socket.AF_UNIX,
                                                     socket.SOCK_STREAM)
                self.collectd_socket.connect(self.collectd_socket_name)
            except Exception as e:
                if self.collectd_socket:
                    self.collectd_socket.close()
                self.collectd_socket = None
                logging.warning("Unable to open %s error %s ",
                                self.collectd_socket_name, e)


    def get_archive_path(self) :
        if self.args.archivepath :
            self.archive_path = self.args.archivepath
        else:
            try:
                self.archive_path = environ['ARCHIVE_PATH']
            except:
                self.archive_path = "Archive"

        self.archive_path = os.path.join(self.archive_path,
                                         self.args.region,
                                         self.this_run_started)
        logging.debug("Asking to make %s", self.archive_path)
        try:
            os.makedirs(self.archive_path)
        except Exception as e:
            if os.path.isdir(self.archive_path):
                # already there
                pass
            else:
                logging.warning("Archive path %s could not be created %s",
                                self.archive_path,e)
                raise RuntimeError

        return

    def initialize(self) :
        self.flavor_name = None
        self.flavor_id = None
        self.archive_path = None
        self.collectd_sock_name = None

        parser = self.setup_parser()

        self.args = parser.parse_args()

        self.get_flavor()
        self.get_collectd_sock()
        self.get_archive_path()

        self.os = Client(self.args.username,
                         self.args.password,
                         self.args.project,
                         self.args.url,
                         region_name = self.args.region)

        if (self.os == None) :
            self.fail("OpenStack API connection setup unsuccessful!")

#        self.clean_vestigial_keypairs()

        if not "rackspace" in self.env('OS_AUTH_SYSTEM',default=[]):
            self.allocate_key()
            self.create_security_group()
        else:
            self.security_group = None
            self.keypair = None

        self.flavor_list = self.os.flavors.list()

#        self.ip_count = 3
#        self.allocate_ip_pool(self.ip_count)
#        self.print_ip_pool()

        self.find_suitable_images()
        # for now just take the first one

        if self.suitable_images:
            self.chosen_image = self.suitable_images.pop()
            logging.warning("The chosen image is %s",
                         self.chosen_image.name)
        else:
            self.fail("Unable to find a suitable image to test")

    def create_remote_hosts(self) :
        """
        Create the remote_hosts file that will be used by the
        runemomniaggdemo.sh script.  if the script is ever enhanced to
        make sure it does not run a loopback test, we can just dump
        all the IP addresses in there.
        """
        first = True
        index = 0

        try :
            fl = open("remote_hosts", "w")
            for server in self.vm_pool :
                if not first :
                    public_ip = self.extract_public_ip(server)
                    fl.write("REMOTE_HOSTS["+str(index)+"]="+str(public_ip)+"\n")
                    index += 1
                else :
                    first = False
            fl.write("NUM_REMOTE_HOSTS="+str(index)+"\n")
        except Exception as e:
#            self.clean_up_instances()
            logging.warning("Unable to create/write remote_hosts file. %s ",
                            e)
            raise RuntimeError
        finally:
            fl.close()

    def scp_binaries(self) :
        """
        Scp all the binaries to the nodes.  Unlke the ssh_check, we
        will bail on any failures.  in the name of simplicity/laziness
        we will copy everything to everyone.
        """
        upload_files = ("netperf", "netserver", "runemomniaggdemo.sh", "find_max_burst.sh", "remote_hosts")
        for node in self.vm_pool :
            publicip = self.extract_public_ip(node)
            try :
                transport = paramiko.Transport((publicip,22))
                if self.keypair:
                    mykey = paramiko.RSAKey.from_private_key_file(os.path.abspath(self.keyfilename))
                    transport.connect(username=self.args.instanceuser,
                                      pkey = mykey)
                else:
                    transport.connect(username=self.args.instanceuser,
                                      password=self.adminPasses[node.id])

                sftp = paramiko.SFTPClient.from_transport(transport)
                for file in upload_files:
                    sftp.put(file,file)
                    mode = sftp.stat(file).st_mode
                    # I would really rather not have to do this, but
                    # SFTP, unlike scp, does not seem to preserve file
                    # permissions.
                    sftp.chmod(file, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
                sftp.close()
                transport.close()
            except Exception as e :
                logging.warning("The copying of files to %s failed.",
                                node.name, e)
                raise RuntimeError

    def start_netservers(self) :
        """
        Start netservers on every node.
        """

        for node in self.vm_pool :
            publicip = self.extract_public_ip(node)
            sudo = "sudo"
            if self.args.instanceuser == 'root':
                sudo = ""

            cmd = "%s ./netserver -p 12865" % sudo

            try :
                ssh = paramiko.SSHClient()
                ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
                if self.keypair:
                    ssh.connect(hostname=publicip,
                                username=self.args.instanceuser,
                                key_filename=os.path.abspath(self.keyfilename),
                                timeout=30.0)
                else:
                    ssh.connect(hostname=publicip,
                                username=self.args.instanceuser,
                                password=self.adminPasses[node.id],
                                timeout=30.0)

                stdin, stdout, stderr = ssh.exec_command(cmd)
#                print stdout.readlines()
#                print stderr.readlines()
                ssh.close()

            except Exception as e:
                logging.warning("Starting netserver processes was unsuccessful on %s at IP %s",
                                node.name, publicip)
                raise RuntimeError

    def run_netperf(self) :
        """
        Actually run the runemomniaggdemo.sh script on the first node
        """
        try :
            publicip = self.extract_public_ip(self.vm_pool[0])
            cmd = "export PATH=$PATH:. ; ./runemomniaggdemo.sh | tee overall.log "
            ssh = paramiko.SSHClient()
            ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            if self.keypair:
                ssh.connect(hostname=publicip,
                            username=self.args.instanceuser,
                            key_filename=os.path.abspath(self.keyfilename),
                            timeout=30.0)
            else:
                ssh.connect(hostname=publicip,
                            username=self.args.instanceuser,
                            password=self.adminPasses[self.vm_pool[0].id],
                            timeout=30.0)

            stdin, stdout, stderr = ssh.exec_command(cmd)
            # if would seem that unless one does something with the
            # output the exec_command will complete rather quickly,
            # which is not what we want right here so we will await
            # the exit status of the underlying channel, whatever that
            # is :) but it is what I found in a web search :)
            status = stdout.channel.recv_exit_status()
            ssh.close()
        except Exception as e:
            logging.warning("Could not run the netperf script on %s at IP %s %s",
                            self.vm_pool[0].name, publicip,str(e))
            raise RuntimeError

    def copy_back_results(self, destination) :
        try :
            publicip = self.extract_public_ip(self.vm_pool[0])
            logging.warning("Copying-back results")
            transport = paramiko.Transport((publicip,22))
            if self.keypair:
                mykey = paramiko.RSAKey.from_private_key_file(os.path.abspath(self.keyfilename))
                transport.connect(username=self.args.instanceuser,
                                  pkey = mykey)
            else:
                transport.connect(username=self.args.instanceuser,
                                  password=self.adminPasses[self.vm_pool[0].id])
  
            sftp = paramiko.SFTPClient.from_transport(transport)
            for file in sftp.listdir():
                # one of these days I should read-up on how to make
                # that one regular expression
                if (re.match(r".*\.log",file) or
                    re.match(r".*\.out",file) or
                    re.match(r".*\.txt",file)):
                    sftp.get(file,destination+file)
            sftp.close()
        except Exception as e:
           logging.exception("Could not retrieve results from %s at IP %s",
                             self.vm_pool[0].name, publicip)
           raise RuntimeError

    def ssh_check(self, time_limit=300) :
        """
        SSH into the VMs and report on progress. we ass-u-me that the
        VMs are already in a happy, running state, or will be within
        the time_limit.  We give-up only if there is a problem after
        our time_limit.  So long as the ssh's remain error-free we
        will keep going as long as it takes to get through the list.
        """
        sshers = list(self.vm_pool)
        starttime = time()
        deadline = starttime + time_limit
        sleeptime = 1

        while (len(sshers) > 0) :
            ssher = sshers.pop()
            publicip = self.extract_public_ip(ssher)
            sudo = "sudo"
            if self.args.instanceuser == 'root':
                sudo = ""

            cmd = "hostname; uname -a; date; %s sysctl -w net.core.rmem_max=1048576 net.core.wmem_max=1048576" % sudo

            try:
                ssh = paramiko.SSHClient()
                ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
                # paper or plastic?
                if self.keypair:
                    ssh.connect(hostname=publicip,
                                username=self.args.instanceuser,
                                key_filename=os.path.abspath(self.keyfilename),
                                timeout=30.0)
                else:
                    #print "ssh.connect to %s user %s pass %s" % (publicip,self.args.instanceuser,self.adminPasses[ssher.id])
                    ssh.connect(hostname=publicip,
                                username=self.args.instanceuser,
                                password=self.adminPasses[ssher.id],
                                timeout=30.0)

                stdin, stdout, stderr = ssh.exec_command(cmd)
#                print stdout.readlines()
#                print stderr.readlines()
                ssh.close()
                sleeptime = 1
            except Exception as e :
                now = time()
                #print "Attempt to ssh failed with %s" % str(e)
                if (now < deadline) :
                    sleep(sleeptime)
                    sleeptime *= 2
                    sleeptime = min(sleeptime, 120, deadline - now)
                    sshers.append(ssher)
                else :
                    logging.warning("The ssh check to %s (IP:%s ID:%s) using command '%s' failed after the %d second limit (%s)",
                                    ssher.name, publicip, ssher.id, cmd, time_limit,str(e))
                    raise RuntimeError

    def extract_min_avg_max(self, flavor, name, postresults) :

        avg = min = max = end = start ="0"
        units = "unknown"

        logging.warning("Post-processing script output")

        for line in postresults.splitlines() :
            if (re.match("Average of peak interval is",line)) :
                toks = line.split(" ",11)
                avg = toks[5]
                start = toks[8]
                end= toks[10]
                logging.debug("Found Average %s from %s to %s",
                              avg, start, end)

            if (re.match("Minimum of peak interval is",line)) :
                toks = line.split(" ",11)
                min = toks[5]
                logging.debug("Found Minimum of peak interval %s ", min)
            if (re.match("Maximum of peak interval is",line)) :
                toks = line.split(" ",11)
                max = toks[5]
                logging.debug("Found Maximum of peak interval %s", max)
        try:

            # I suppose one could split name on '_' to find the testtype?
            testtype="unknown"
            if (re.search("tps",name)) :
                testtype="tps"
                units="TPS"
            if (re.search("sync",name)):
                testtype="synctps"
                units="TPS"
            if (re.search("inbound",name)) :
                testtype="inbound"
                units="BPS"
            if (re.search("outbound",name)):
                testtype="outbound"
                units="BPS"
            if (re.search("bidirectional",name)):
                testtype="bidirectional"
                units="BPS"

            if (self.collectd_socket != None):
                self.collectd_socket.send("PUTVAL \"netperf-"+self.args.region+"/unixsock-"+flavor.name+"-"+testtype+"/min\" interval=900 "+end+":"+min+"\n")
                self.collectd_socket.send("PUTVAL \"netperf-"+self.args.region+"/unixsock-"+flavor.name+"-"+testtype+"/avg\" interval=900 "+end+":"+avg+"\n")
                self.collectd_socket.send("PUTVAL \"netperf-"+self.args.region+"/unixsock-"+flavor.name+"-"+testtype+"/max\" interval=900 "+end+":"+max+"\n")
                logging.warning("Wrote results for "+self.args.region+" "+flavor.name+" "+testtype+" to collectd")


            prefix = self.args.region + ":" + self.chosen_image.name + ":" + flavor.name + ":" + testtype
            suffix = ":" + units + ":" + end
            logging.warning(prefix + ":min:" + min + suffix)
            logging.warning(prefix + ":avg:" + avg + suffix)
            logging.warning(prefix + ":max:" + max + suffix)

        except Exception as e:
            logging.warning("Post process fubar %s",e)
            raise RuntimeError

    def post_proc_flavor(self,flavor) :
        """
        Post process the results of a given flavor
        """
        logging.info("Post-processing flavor: %s ", flavor.name)
        archive_location = os.path.join(self.archive_path,
                                        str(flavor.name),
                                        "")

        for root, dirs, files in os.walk(archive_location, topdown=False):
            for name in files:
                if (re.match("netperf_.*\.log",name)) :
                    fullname = os.path.join(root,name)
                    cmd = 'PATH=$PATH:. post_proc.py "%s"' % fullname
                    logging.debug("post processing via command '%s'", cmd)
                    try :
                        postresults = self.do_in_shell(cmd,True)
                        # so, look at vrules to get the times
                        self.extract_min_avg_max(flavor, name, postresults)
                    except Exception as e:
                        logging.warning("Unable to post-process flavor %s %s results",
                                        flavor.name, fullname)
                        raise RuntimeError


    def test_flavor(self, flavor) :
        """
        Start the right number of instances of the specified flavor
        """

        logging.warning("Testing flavor: %s in region %s",
                     flavor.name, self.args.region)
        archive_location = os.path.join(self.archive_path,
                                        str(flavor.name),
                                        "")
        os.makedirs(archive_location)

        #print "Calling for server pool allocation"
        self.allocate_server_pool(5,
                                  flavor,
                                  self.chosen_image)

        logging.warning("Awaiting server pool active")
        self.await_server_pool_ready(600)
#        self.associate_public_ips()
        logging.warning("Server pool active. Verifying up and running via ssh")
        self.ssh_check()
        logging.warning("ssh_check complete")
        self.create_remote_hosts()
        logging.warning("remote_hosts file creation complete")
        self.scp_binaries()
        logging.warning("transfer of binaries complete.")
        self.start_netservers()
        logging.warning("netservers started. about to start the script")
        self.run_netperf()
        logging.warning("netperf run complete. copying back results")
        self.copy_back_results(archive_location)
        logging.warning("cleaning up vms")
#       self.disassociate_public_ips()
        self.deallocate_server_pool()
        self.await_server_pool_gone(600)
        logging.warning("Server pool deallocated")



    def test_flavors(self) :
        """
        Iterate through all the available flavors and test them if
        there is a match on either the name or id, or if neither have
        been specified
        """
        for flavor in self.flavor_list :
            #print "Checking flavor name %s id %s" % (flavor.name, flavor.id)
            try:
                if ((not self.flavor_name and not self.flavor_id)
                    or
                    (self.flavor_name and re.search(self.flavor_name,flavor.name))
                    or
                    (self.flavor_id and (self.flavor_id == int(flavor.id)))) :

                    self.test_flavor(flavor)
                    # we could move the post-processing inside
                    # test_flavor to overlap it with the deletion of
                    # the VMs but for the time being we will leave it
                    # here
                    self.post_proc_flavor(flavor)
            except:
                self.clean_up_instances()
                logging.warning("Test of flavor '%s' (%s) unsuccessful",
                                flavor.name,
                                flavor.id)


    def clean_up_instances(self) :

        import inspect

        curframe = inspect.currentframe()
        calframe = inspect.getouterframes(curframe, 2)
        logging.warning("Asked by %s to clean-up instances with pool len %d",
                        calframe[1][3],
                        len(self.vm_pool))
        if (self.vm_pool != []):
            self.deallocate_server_pool()
            self.await_server_pool_gone(600)

    def clean_up_overall(self) :
        """
        Clean some of the things which were created before and not
        flavor-specific
        """

        logging.info("Asked to clean-up everything left")

        if (self.keypair != None) :
            os.remove(self.keypair.name + ".pem")
            self.os.keypairs.delete(self.keypair)
            self.keypair = None


        if (self.ip_pool != []) :
            self.deallocate_ip_pool()
            
        self.clean_up_instances()

        if self.security_group :
            for rule in self.security_group.rules :
                self.os.security_group_rules.delete(rule['id'])

            self.os.security_groups.delete(self.security_group.id)
            self.security_group = None

if __name__ == '__main__' :
    print "Hello World! Let us run some netperf shall we?"

    logging.basicConfig(level=logging.WARNING, format="%(asctime)s %(message)s")
    tn = TestNetperf()
    try:
        tn.initialize()
    except Exception as e:
        logging.exception("Initialization failed:")
        tn.clean_up_overall()
        tn.fail("Terminating in reponse to initialization failure")

    print "testing flavors"
    tn.test_flavors()
    tn.clean_up_overall()
