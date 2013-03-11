#!/usr/bin/python -u

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

class TestNetperf() :
    """
    Will attempt to launch some instances and then run netperf in them
    Leverages heavily from scripts pointed-at by Gavin Brebner
    """

    def __init__(self) :
        self.verbose = 1
        self.sshopts = " -o StrictHostKeyChecking=no -o HashKnownHosts=no -o ConnectTimeout=30 "
        self.nova_username = ""
        self.nova_url = ""
        self.nova_password = ""
        self.nova_project_id = ""
        self.nova_region_name = ""
        self.os = None
        self.ip_pool = []
        self.vm_pool = []
        self.suitable_images = []
        self.chosen_image = None
        self.keypair = None
        self.start = time()
        self.this_run_started = strftime("%Y-%m-%d-%H-%M",gmtime()) + "/"
        self.instance_username = "ubuntu"

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
        try:
            iplist = vm.networks['hpcloud']
        except Exception as e :
            try :
                iplist = vm.networks['private']
            except Exception as e2 :
                logging.warning("Unable to find a public IP for %s (%s:%s)",
                                vm.name, e, e2)
                raise RuntimeError

        return iplist[1]

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

    def allocate_server_pool(self, count, flavor, image, keyname) :
        """
        Allocate the number of requested servers of the specified 
        flavor and image, and with the specified key name
        """

        logging.info("Allocating %d servers of flavor '%s' with image name '%s' and key %s",
                     count, flavor.name, image.name, keyname)

        for i in xrange(count) :
            thisname = "netperftest" + str(i)
            try:
                newvm = self.os.servers.create(name = thisname,
                                               image = image,
                                               flavor = flavor,
                                               key_name = keyname,
                                               security_groups = ["netperftesting"])
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
        now = time()
        sleeptime = 1

        while ((len(waiters) > 0) and
               ((now - waiting_started) < time_limit)) :
            waiter = waiters.pop()
            waiter = self.os.servers.get(waiter.id)
            if (waiter.status == "ACTIVE") :
                ip_address = self.extract_public_ip(waiter)
                self.known_hosts_ip_remove(ip_address)
                actives.append(waiter)
                sleeptime = 1
                continue
            if (re.search(r"ERROR",waiter.status)) :
                logging.warning("Instance id %d name %s encountered an error during instantiation of %s",
                                waiter.id, waiter.name, waiter.status)
                raise RuntimeError

            waiters.append(waiter)
            sleep(sleeptime)
            sleeptime *= 2
            now = time()

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

        for group in self.os.security_groups.list() :
            if (group.name == "netperftesting") :
                logging.info("Deleting vestigial netperftesting security_group")
                for rule in group.rules :
                    logging.info("Deleting vestigial netperftesting security_group rule %s" + str(rule['id']))
                    self.os.security_group_rules.delete(rule['id'])
                self.os.security_groups.delete(group.id)

        self.security_group = self.os.security_groups.create("netperftesting","A rather open security group for netperf testing")

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

    def setup_parser(self) :
        parser = argparse.ArgumentParser()
        parser.add_argument("-f", "--flavor",
                            help="Specify the flavor to test")
        parser.add_argument("--image",
                            help="Specify the image type to test")
        parser.add_argument("--region",
                            help="Specify the region in which to test")
        parser.add_argument("--username",
                            help="The name of the user to use")
        parser.add_argument("--password",
                            help="The password for the Nova user")
        parser.add_argument("--url",
                            help="The URL for Nova")
        parser.add_argument("--project",
                            help="The ID for the Nova project")

        parser.add_argument("--archivepath",
                            help="The path to where the archive is to be kept")
        parser.add_argument("--collectdsockname",
                            help="The name of the collectd socket.")
        parser.add_argument("--instanceuser",
                            help="The name of the user on the instance(s)")

        return parser

    def get_instance_username(self):
        if self.args.instanceuser:
            self.instance_username = self.args.instanceuser

        try:
            self.instance_username = environ['INSTANCE_USERNAME']
        except:
            pass

    def get_nova_project_id(self):
        if self.args.project :
            self.nova_project_id = self.args.project
            return

        try:
            self.nova_project_id = environ['NOVA_PROJECT_ID']
        except:
            self.fail("A Nova project ID must be specifed either via --project or a NOVA_PROJECT_ID environment variable.")

    def get_nova_region_name(self):
        if self.args.region:
            self.nova_region_name = self.args.region
            return

        try:
            self.nova_region_name = environ['NOVA_REGION_NAME']
        except:
            self.fail("A Nova region name must be specified via either --region or a NOVA_REGION_NAME environment variable.")

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
                                         self.nova_region_name,
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

    def get_nova_password(self):

        if self.args.password :
            self.nova_password = self.args.password
            return

        try:
            self.nova_password = environ['NOVA_PASSWORD']
        except:
            self.fail("A password for Nova access must be specified either via --password or the NOVA_PASSWORD environment variable.")

    def get_nova_url(self) :

        if self.args.url :
            self.nova_url = self.args.url
            return

        try:
            self.nova_url = environ['NOVA_URL']
        except:
            self.fail("A URL for Nova access must be specified either via --url or the NOVA_URL envionment variable")

    def get_nova_username(self) :
        if self.args.username :
            self.nova_username = self.args.username
            return

        try:
            self.nova_username = environ['NOVA_USERNAME']
        except:
            self.fail("A username for Nova access must be specified eigher via --username or the NOVA_USERNAME environment variable")

    def initialize(self) :
        self.flavor_name = None
        self.flavor_id = None
        self.archive_path = None
        self.collectd_sock_name = None

        self.nova_region_name = None
        self.nova_url = None
        self.nova_username = None
        self.nova_project_id = None
        
        parser = self.setup_parser()

        self.args = parser.parse_args()

        self.get_nova_url()
        self.get_nova_username()
        self.get_instance_username()
        self.get_nova_project_id()
        self.get_nova_region_name()
        self.get_nova_password()
        self.get_flavor()
        self.get_collectd_sock()
        self.get_archive_path()

        self.os = Client(self.nova_username,
                         self.nova_password,
                         self.nova_project_id,
                         self.nova_url,
                         region_name = self.nova_region_name)

        if (self.os == None) :
            self.fail("OpenStack API connection setup unsuccessful!")

        self.clean_vestigial_keypairs()

        self.allocate_key()

        self.create_security_group()

        self.flavor_list = self.os.flavors.list()

#        self.ip_count = 3
#        self.allocate_ip_pool(self.ip_count)
#        self.print_ip_pool()

        self.find_suitable_images()
        # for now just take the first one

        if self.suitable_images:
            self.chosen_image = self.suitable_images.pop()
            logging.info("The chosen image is %s",
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
        for node in self.vm_pool :
            publicip = self.extract_public_ip(node)
            cmd = "scp %s -i %s netperf netserver runemomniaggdemo.sh find_max_burst.sh remote_hosts %s@%s:" % (self.sshopts, self.keyfilename, self.instance_username, publicip)
#            cmd = "scp " + self.sshopts + " -i " + self.keyfilename + " netperf netserver runemomniaggdemo.sh find_max_burst.sh remote_hosts ubuntu@" + str(publicip) +":"
            try :
                self.do_in_shell(cmd,False)
            except Exception as e :
#                self.clean_up_instances()
                logging.warning("The copying of files to %s via command '%s' failed.",
                                node.name, cmd, e)
                raise RuntimeError

    def start_netservers(self) :
        """
        Start netservers on every node.
        """

        for node in self.vm_pool :
            publicip = self.extract_public_ip(node)
            cmd = "ssh %s -i %s %s@%s ./netserver -p 12865" % (self.sshopts, self.keyfilename, self.instance_username, publicip)
            try :
                self.do_in_shell(cmd,False)
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
            cmd = "ssh %s -i %s %s@%s 'export PATH=$PATH:. ; ./runemomniaggdemo.sh | tee overall.log' " % (self.sshopts, self.keyfilename, self.instance_username, publicip)
            self.do_in_shell(cmd,False)
        except Exception as e:
            logging.warning("Could not run the netperf script on %s at IP %s",
                            self.vm_pool[0].name, publicip)
            raise RuntimeError

    def copy_back_results(self, destination) :
        try :
            publicip = self.extract_public_ip(self.vm_pool[0])
            cmd = "scp %s -i %s %s@%s:*.{log,out} %s" % (self.sshopts, self.keyfilename, self.instance_username, publicip, destination)
            logging.info("Scping results with command " + cmd)
            self.do_in_shell(cmd,False)
        except Exception as e:
           logging.exception("Could not scp results from %s at IP %s",
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
        sleeptime = 1

        while (len(sshers) > 0) :
            ssher = sshers.pop()
            publicip = self.extract_public_ip(ssher)
            sudo = "sudo"
            if self.instance_username == 'root':
                sudo = ""

            cmd = "ssh %s -i %s %s@%s 'hostname; uname -a; date; %s sysctl -w net.core.rmem_max=1048576 net.core.wmem_max=1048576'" % (self.sshopts, os.path.abspath(self.keyfilename), self.instance_username, publicip, sudo)

            try:
                self.do_in_shell(cmd,False)
                sleeptime = 1
            except Exception as e :
                now = time()
                if ((now - starttime) < time_limit) :
                    sleep(sleeptime)
                    sleeptime *= 2
                    sshers.append(ssher)
                else :
                    logging.warning("The ssh check to %s using command '%s' failed after the %d second limit",
                                    ssher.name, cmd, time_limit)
                    raise RuntimeError

    def extract_min_avg_max(self, flavor, name, postresults) :

        avg = min = max = end = start ="0"
        units = "unknown"

        logging.info("Post-processing script output")
        for line in postresults.splitlines() :
            if (re.match("Average of peak interval is",line)) :
                toks = line.split(" ",11)
                avg = toks[5]
                start = toks[8]
                end= toks[10]
                logging.debug("Found Average %f from %s to %s",
                              avg, start, end)

            if (re.match("Minimum of peak interval is",line)) :
                toks = line.split(" ",11)
                min = toks[5]
                logging.debug("Found Minimum of peak interval %f ", min)
            if (re.match("Maximum of peak interval is",line)) :
                toks = line.split(" ",11)
                max = toks[5]
                logging.debug("Found Maximum of peak interval %f", max)
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
                self.collectd_socket.send("PUTVAL \"netperf-"+self.nova_region_name+"/unixsock-"+flavor.name+"-"+testtype+"/min\" interval=900 "+end+":"+min+"\n")
                self.collectd_socket.send("PUTVAL \"netperf-"+self.nova_region_name+"/unixsock-"+flavor.name+"-"+testtype+"/avg\" interval=900 "+end+":"+avg+"\n")
                self.collectd_socket.send("PUTVAL \"netperf-"+self.nova_region_name+"/unixsock-"+flavor.name+"-"+testtype+"/max\" interval=900 "+end+":"+max+"\n")
                logging.info("Wrote results for "+self.nova_region_name+" "+flavor.name+" "+testtype+" to collectd")


            prefix = self.nova_region_name + ":" + self.chosen_image.name + ":" + flavor.name + ":" + testtype
            suffix = ":" + units + ":" + end
            logging.info(prefix + ":min:" + min + suffix)
            logging.info(prefix + ":avg:" + avg + suffix)
            logging.info(prefix + ":max:" + max + suffix)

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
                    cmd = "PATH=$PATH:. post_proc.py " + fullname
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

        logging.info("Testing flavor: %s in region %s",
                     flavor.name, self.nova_region_name)
        archive_location = os.path.join(self.archive_path,
                                        str(flavor.name),
                                        "")
        os.makedirs(archive_location)

        self.allocate_server_pool(3,
                                  flavor,
                                  self.chosen_image,
                                  self.keypair.name)

        self.await_server_pool_ready(300)
#        self.associate_public_ips()
        logging.info("Server pool allocated. Verifying up and running via ssh")
        self.ssh_check()
        logging.info("ssh_check complete")
        self.create_remote_hosts()
        logging.info("remote_hosts file creation complete")
        self.scp_binaries()
        logging.info("scp of binaries complete.")
        self.start_netservers()
        logging.info("netservers started. about to start the script")
        self.run_netperf()
        logging.info("netperf run complete. copying back results")
        self.copy_back_results(archive_location)
        logging.info("cleaning up vms")
#       self.disassociate_public_ips()
        self.deallocate_server_pool()
        self.await_server_pool_gone(300)
        logging.info("Server pool deallocated")



    def test_flavors(self) :
        """
        Iterate through all the available flavors and test them if
        there is a match on either the name or id, or if neither have
        been specified
        """
        for flavor in self.flavor_list :
            try:
                if ((not self.flavor_name and not self.flavor_id)
                    or
                    (self.flavor_name and re.search(self.flavor_name,flavor.name))
                    or
                    (self.flavor_id and (self.flavor_id == flavor.id))) :

                    self.test_flavor(flavor)
                    # we could move the post-processing inside
                    # test_flavor to overlap it with the deletion of
                    # the VMs but for the time being we will leave it
                    # here
                    self.post_proc_flavor(flavor)
            except:
                self.clean_up_instances()
                logging.warning("Test of flavor '%s' (%d) unsuccessful",
                                flavor.name,
                                flavor.id)


    def clean_up_instances(self) :

        logging.info("Asked to clean-up instances with pool len %d",
                     len(self.vm_pool))
        if (self.vm_pool != []):
            self.deallocate_server_pool()
            self.await_server_pool_gone(300)

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
    print "Hello World!"

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    tn = TestNetperf()
    try:
        tn.initialize()
    except Exception as e:
        logging.exception("Initialization failed:")
        tn.clean_up_overall()
        tn.fail("Terminating in reponse to initialization failure")

    tn.test_flavors()
    tn.clean_up_overall()
