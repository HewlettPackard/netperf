#!/usr/bin/python -u

import os
import subprocess
import re
import stat
from os import environ
from novaclient.v1_1 import Client
from time import sleep, time, strftime, localtime, gmtime

class TestNetperf() :
    """
    Will attempt to launch some instances and then run netperf in them
    Leverages heavily from scripts pointed-at by Gavin Brebnet
    """

    def __init__(self) :
        self.verbose = 1
        self.sshopts = " -o StrictHostKeyChecking=no -o HashKnownHosts=no"
        self.nova_username = ""
        self.nova_url = ""
        self.nova_password = ""
        self.nova_project_id = ""
        self.nova_region_name = ""
        self.os = None
        self.start = gmtime()
        self.archive_base = "Archive/" + self.nova_region_name + "/"
        self.this_run_started = strftime("%Y-%m-%d-%H-%M",gmtime()) + "/"

    def fail(self,message) :
        """
        print out a final utterance and die
        """
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
            raise 

        return dout

    def find_suitable_image(self, suitable) :
        """ 
        Try to find a suitable image to use for the testing
        """
        for image in self.os.images.list() :
            if (re.search(str(suitable), str(image.name))) :
                return image

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
                self.clean_up_overall()
                self.fail("Unable to find a public IP for " +
                              str(vm.name) + " " + str(e) + " " + str(e2))
        return iplist[1]

    def deallocate_server_pool(self) :
        """
        Issue delete requests onany and all existing servers
        """
        for server in self.vm_pool :
            try:
                public_ip = self.extract_public_ip(server)
                self.known_hosts_ip_remove(public_ip)
                self.os.servers.delete(server)
            except Exception as e:
                print "Unable to delete server " + str(server.name) + ". Error: "+ str(e)

    def allocate_server_pool(self, count, flavor, image, keyname) :
        """
        Allocate the number of requested servers of the specified 
        flavor and image, and with the specified key name
        """
        print "Allocating " + str(count) + " servers of flavor " + str(flavor.name) + " with image name '" + str(image.name) + "' and key " + str(keyname)

        for i in xrange(count) :
            thisname = "netperftest" + str(i)
            try:
                newvm = self.os.servers.create(name = thisname,
                                               image = image,
                                               flavor = flavor,
                                               key_name = keyname,
                                               security_groups = ["netperftesting"])
            except Exception as e :
                self.clean_up_overall()
                self.fail("Error creating instance: " + str(e))

            self.vm_pool.append(newvm)


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
                self.clean_up_overall()
                self.fail("Instance id " + str(waiter.id) + " name " + str(waiter.name) + " errored during instantiation " + str(waiter.status))

            waiters.append(waiter)
            sleep(sleeptime)
            sleeptime *= 2
            now = time()

        if (len(waiters) > 0) :
            self.clean_up_overall()
            self.fail(str(len(waiters)) + " failed to achieve ACTIVE status within " + str(time_limit) + " seconds")

        # I wonder what the right way to do this is
        self.vm_pool = []
        self.vm_pool = list(actives)

    def print_ip_pool(self) :
        """
        Display the current contents of the IP pool
        """
        print "Contents of IP pool:"
        for ip in self.ip_pool :
            print "IP:" + str(ip.ip) + " ID: " + str(ip.id)

    def known_hosts_ip_remove(self, ip_address) :
        """
        Remove an IP address from the known_hosts file
        """
        cmd = "ssh-keygen -f ~/.ssh/known_hosts -R " + str(ip_address) + " > /dev/null"
        #print "Removing " + str(ip_address) + " from ~/.ssh/known_hosts using command '" + cmd + "'"
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
                self.fail("Unable to deallocate IP:" + str(ip.ip) +
                          " ID: " + str(ip.id) + " " + str(e))

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
                self.fail("Error while allocating " + str(count) + " IPs " +
                          str(e))
            self.known_hosts_ip_remove(str(ip.ip))

    def clean_vestigial_keypairs(self) :
        """
        Seek-out and delete vestigial keypairs matching our magic
        regular expression
        """

        for keypair in self.os.keypairs.list() :
            if (re.match("net[0-9]+perf[0-9]+dot[0-9]+", str(keypair.name))) :
                print "Deleting vestigial keypair named " + str(keypair.name)
                try :
                    self.os.keypairs.delete(keypair)
                    os.remove(str(keypair.name) + ".pem")
                except Exception as e:
                    print "Exception while purging keypair " + str(keypair.name) + " " + str(e)
                    continue

    def create_security_group(self):

        for group in self.os.security_groups.list() :
            if (group.name == "netperftesting") :
                print "Deleting vestigial netperftesting security_group"
                for rule in group.rules :
                    print "Deleting vestigial netperftesting security_group rule" + str(rule['id'])
                    self.os.security_group_rules.delete(rule['id'])
                self.os.security_groups.delete(group.id)

        self.security_group = self.os.security_groups.create("netperftesting","A rather open security group for netperf testing")

        self.os.security_group_rules.create(parent_group_id = self.security_group.id,
                                            ip_protocol = 'tcp',
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

    def initialize(self) :
        if (not ('NOVA_URL' in environ) or
            not ('NOVA_USERNAME' in environ) or
            not ('NOVA_PASSWORD' in environ) or
            not ('NOVA_PROJECT_ID' in environ) or
            not ('NOVA_REGION_NAME' in environ)) :
            self.fail("One or more of NOVA_URL, NOVA_USERNAME, " + 
                      "NOVA_PASSWORD, NOVA_PROJECT_ID and NOVA_REGION_NAME " +
                      "not set in the environment.")

        self.nova_url = environ['NOVA_URL']
        self.nova_username = environ['NOVA_USERNAME']
        self.nova_password = environ['NOVA_PASSWORD']
        self.nova_project_id = environ['NOVA_PROJECT_ID']
        self.nova_region_name = environ['NOVA_REGION_NAME']

        self.os = Client(self.nova_username,
                         self.nova_password,
                         self.nova_project_id,
                         self.nova_url,
                         self.nova_region_name)

        if (self.os == None) :
            self.fail("OpenStack API connection setup unsuccessful!")

        self.clean_vestigial_keypairs()

        self.allocate_key()

        self.create_security_group()

        self.flavor_list = self.os.flavors.list()

        os.makedirs(self.archive_base + self.this_run_started)

        self.ip_pool = []

        self.ip_count = 3
#        self.allocate_ip_pool(self.ip_count)
#        self.print_ip_pool()

        self.chosen_image = None
        self.chosen_image = self.find_suitable_image("Precise")

        print "The chosen image is " + str(self.chosen_image.name)

        self.vm_pool = []

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
            self.clean_up_overall()
            self.fail("Unable to create/write remote_hosts file. " +str(e))
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
            cmd = "scp " + self.sshopts + " -i " + self.keyfilename + " netperf netserver runemomniaggdemo.sh find_max_burst.sh remote_hosts ubuntu@" + str(publicip) +":"
            try :
                self.do_in_shell(cmd,False)
            except Exception as e :
                self.clean_up_overall()
                self.fail("The copying of files to " + str(node.name) +
                          " via the command '" + cmd + "' failed. " + str(e))

    def start_netservers(self) :
        """
        Start netservers on every node.
        """

        for node in self.vm_pool :
            publicip = self.extract_public_ip(node)
            cmd = "ssh " + self.sshopts + " -i " + self.keyfilename + " ubuntu@" + str(publicip) + " ./netserver -p 12865"
            try :
                self.do_in_shell(cmd,False)
            except Exception as e:
                self.clean_up_overall()
                self.fail("Starting netserver processes was unsuccessful on "
                          + str(node.name) + " at IP " + str(publicip))

    def run_netperf(self) :
        """
        Actually run the runemomniaggdemo.sh script on the first node
        """
        try :
            publicip = self.extract_public_ip(self.vm_pool[0])
            cmd = "ssh " + self.sshopts + " -i " + self.keyfilename + " ubuntu@" + str(publicip) + " 'export PATH=$PATH:. ; ./runemomniaggdemo.sh | tee overall.log' "
            self.do_in_shell(cmd,False)
        except Exception as e:
           self.clean_up_overall()
           self.fail("Could not run the netperf script on " +
                     str(self.vm_pool[0].name) + " at IP " + str(publicip))

    def copy_back_results(self, destination) :
        try :
            publicip = self.extract_public_ip(self.vm_pool[0])
            cmd = "scp " + self.sshopts + " -i " + self.keyfilename + " ubuntu@" + str(publicip) + ":*.{log,out} " + str(destination)
            print "Scping results with command " + cmd
            print self.do_in_shell(cmd,False)
        except Exception as e:
           self.clean_up_overall()
           self.fail("Could not scp results from " +
                     str(self.vm_pool[0].name) + " at IP " + str(publicip))

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
            cmd = "ssh " + self.sshopts + " -i " + self.keyfilename + " ubuntu@" + str(publicip) + " 'hostname; uname -a; date; sudo sysctl -w net.core.rmem_max=1048576 net.core.wmem_max=1048576'"
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
                    self.clean_up_overall()
                    self.fail("The ssh check to " + str(ssher.name) +
                              " failed after the " + str(time_limit) +
                              " second limit. " + str(e) )

    def extract_min_avg_max(self, flavor, root, name) :
        vrule_times = []
        vrule_texts = []

        vrules = open("vrules","r")
        contents = vrules.read()
        for line in contents.splitlines() :
            key, value = line.split("=",1)
            if (re.match("VRULE_TIME",key)) :
                vrule_times.append(value)
            if (re.match("VRULE_TEXT",key)) :
                vrule_texts.append(value)
        interval_end = vrule_times[-1]
        interval_begin = vrule_times[-2]
        print "For " + name + " interval of interest starts at " + interval_begin + " and ends at " + interval_end

    def post_proc_flavor(self,flavor) :
        """
        Post process the results of a given flavor
        """
        print "Post-processing flavor: " + flavor.name
        archive_location = self.archive_base + self.this_run_started + str(flavor.name) + "/"
        vrule_times = []

        for root, dirs, files in os.walk(archive_location, topdown=False):
            for name in files:
                if (re.match("netperf_.*\.log",name)) :
                    fullname = os.path.join(root,name)
                    cmd = "post_proc.sh " + fullname
                    print "post processing via command '" + cmd + "'"
                    try :
                        print self.do_in_shell(cmd,True)
                        # so, look at vrules to get the times
                        self.extract_min_avg_max(flavor, root, name)
                    except Exception as e:
                        print "Unable to post-process flavor " + flavor.name + " " + fullname + " results." + str(e)


    def test_flavor(self, flavor) :
        """
        Start the right number of instances of the specified flavor
        """

        print "Testing flavor: " + flavor.name
        archive_location = self.archive_base + self.this_run_started + str(flavor.name) + "/"
        os.makedirs(archive_location)

        self.allocate_server_pool(3,
                                  flavor,
                                  self.chosen_image,
                                  self.keypair.name)

        self.await_server_pool_ready(300)
#        self.associate_public_ips()
        print "Server pool allocated. Verifying up and running via ssh"
        self.ssh_check()
        print "ssh_check complete"
        self.create_remote_hosts()
        print "remote_hosts file creation complete"
        self.scp_binaries()
        print "scp of binaries complete."
        self.start_netservers()
        print "netservers started. about to start the script"
        self.run_netperf()
        print "netperf run complete. copying back results"
        self.copy_back_results(archive_location)
        print "cleaning up vms"
#       self.disassociate_public_ips()
        self.deallocate_server_pool()
        self.await_server_pool_gone(300)
        print "Server pool deallocated"



    def test_flavors(self) :
        """
        Iterate through all the available flavors and test them
        """
        for flavor in self.flavor_list :
                self.test_flavor(flavor)
                # we could move the post-processing inside test_flavor
                # to overlap it with the deletion of the VMs but for
                # the time being we will leave it here
                self.post_proc_flavor(flavor)

    def clean_up_overall(self) :
        """
        Clean some of the things which were created before and not
        flavor-specific
        """

        print "Asked to clean-up everything left"

        if (self.keypair != None) :
            os.remove(self.keypair.name + ".pem")
            self.os.keypairs.delete(self.keypair)


        if (self.ip_pool != []) :
            self.deallocate_ip_pool()
                          
        if (self.vm_pool != []) :
            self.deallocate_server_pool()

        for rule in self.security_group.rules :
            self.os.security_group_rules.delete(rule['id'])

        self.os.security_groups.delete(self.security_group.id)

if __name__ == '__main__' :
    print "Hello World!"

    tn = TestNetperf()
    try:
        tn.initialize()
    except Exception as e:
        tn.clean_up_overall()
        tn.fail("Initialization failed: " + str(e))

    tn.test_flavors()
    tn.clean_up_overall()
