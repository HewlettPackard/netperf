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
from neutronclient.v2_0 import client as neutron_client
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
        self.port_pool = []
        self.vm_pool = []
        self.dead_pool = []
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
            try :
                if ((image_regex and re.search(image_regex,image.name))
                    or
                    (image_id and (image_id == int(image.id)))) :
                    self.suitable_images.append(image)
            except :
                pass

    def await_server_pool_gone(self,time_limit) :
        """
        Keep checking the status of the server pool until it is gone or
        our patience has run-out.  Alas, novaclient.v1_1 is not sophisticated
        enough to return "ENOEND" or somesuch as a status when the specified
        server is well and truly gone.  So we get status until there is an
        exception raised, which we interpret as "server gone"
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
#                    self.clean_up_overall()
                    logging.warning("Instance id " + str(waiter.id) + " name " + str(waiter.name) + " errored during deletion " + str(waiter.status))
                    continue

                self.vm_pool.append(waiter)
                sleep(sleeptime)
                sleeptime *= 2
                now = time()
            except Exception as e:
                sleeptime = 1
                # at some point I should check the exception and only
                # log something only for things other than "not found"
                #logging.warning("Attempt to get status of instance id %s name %s failed with %s", str(waiter.id), str(waiter.name), str(e))
                continue

        instances_remaining = len(self.vm_pool)
        if (instances_remaining > 0) :
#            self.clean_up_overall()
            plural = ""
            if (instances_remaining > 1):
                plural = "s"
            logging.warning("%d instance%s failed to achieve deleted status within %d seconds",
                            instances_remaining, plural, time_limit)
            # we need to clear the self.vm_pool otherwise it will
            # mess-up the testing of other flavors during this
            # run. still we might not want to lose track of them
            # entirely, so let us put them into a "dead pool" as it
            # were
            self.dead_pool += self.vm_pool
            self.vm_pool = []

    def associate_public_ips(self) :
        """
        Associate our public IPs with instances
        """
        for (vm, ip) in zip(self.vm_pool, self.ip_pool) :
            # belt and suspenders - remove the IP from the known_hosts
            # file since we will be re-using it during the run
            self.known_hosts_ip_remove(ip['floating_ip_address'])
            vm.add_floating_ip(ip['floating_ip_address'])

    def disassociate_public_ips(self) :
        """
        Break the association between an instance and its floating IP
        """
        for (vm, ip) in zip(self.vm_pool, self.ip_pool) :
            vm.remove_floating_ip(ip['floating_ip_address'])

    def deallocate_server_pool(self) :
        """
        Issue delete requests on any and all existing servers
        """
#        print "Length of server pool %d" %len(self.vm_pool)

        for server in self.vm_pool :
            for retries in xrange(1,4) :
                try:
                    self.os.servers.delete(server)
                    break
                except Exception as e :
                    logging.warning("Unable to delete server %s try %d. Error: %s",
                                    server.name, retries, e)

    def allocate_server_pool(self, count, flavor, image) :
        """
        Allocate the number of requested servers of the specified
        flavor and image, and with the specified key name
        """

        #print "before server_pool opening warning"
        keyname = None
        secgroup = None
        az = None
        if self.keypair:
            keyname = self.keypair.name
        if self.security_group:
            secgroup = [self.security_group['name']]
        if self.args.availability_zone:
            az = self.args.availability_zone

        logging.warning("Allocating %d servers of flavor '%s' with image name '%s' and az %s",
                        count, flavor.name, image.name, az)
        #print "after same"

        # instance number (filled-in later) time flavorname imagename
        basename = "netperftest%s_%s_%s_%s" % ("%d",str(self.start),flavor.name,image.name.replace(" ","_"))
        for (i, port) in zip(xrange(count), self.port_pool) :
            thisname = basename % i
            nics_options = [ {'port-id': port['id']} ]

            try:
                newvm = self.os.servers.create(name = thisname,
                                               image = image,
                                               flavor = flavor,
                                               key_name = keyname,
                                               security_groups = secgroup,
                                               nics = nics_options,
                                               availability_zone = az)
                #print "adminPass is %s" % newvm.adminPass
                self.adminPasses[newvm.id]=newvm.adminPass
                self.vm_pool.append(newvm)

            except Exception as e :
                logging.warning("Error allocating instance: %s" , e)
                raise RuntimeError



    def await_server_pool_ready(self, time_limit) :
        """
        Wait until all the servers in the server pool are in an
        "ACTIVE" state.  If they happen to come-up in an "ERROR" state
        we are toast.  If it takes too long for them to transition to
        the "ACTIVE" state we are similarly toast
        """
        actives = []
        waiting_started = time()
        deadline = waiting_started + time_limit
        sleeptime = 1

        for vm in self.vm_pool :
            this_vm_not_active = True
            while this_vm_not_active :

                waiter = self.os.servers.get(vm.id)

                logging.warning("elapsed %d server %s state %s id %s hostId %s" % ((time() - waiting_started), waiter.name, waiter.status, waiter.id, waiter.hostId))
                if (waiter.status == "ACTIVE") :
                    actives.append(waiter)
                    sleeptime = 1
                    this_vm_not_active = False
                    continue
                if (re.search(r"ERROR",waiter.status)) :
                    logging.warning("Instance id %s name %s encountered an error during instantiation of %s with fault %s",
                                    waiter.id, waiter.name, waiter.status, waiter.fault)
                    raise RuntimeError

                if (time() >= deadline):
                    logging.warning("Instance id %s name %s failed to achieve ACTIVE status within %d seconds",
                                    waiter.id, waiter.name, time_limit)
                    raise RuntimeError


                sleep(sleeptime)
                sleeptime *= 2
                sleeptime = min(120, deadline - time(), sleeptime)


        # I wonder what the right way to do this is
        self.vm_pool = []
        self.vm_pool = list(actives)

    def find_singleton_server(self) :
        """
        Look for a server which is not on the same hostId as any other
        instance we have launched and return its position in the
        original list of servers.  shirley there are better ways to go
        about doing this.
        """

        # sort the list of servers by hostId
        my_pool = sorted(self.vm_pool,key=lambda vm: vm.hostId)

        still_looking = True

        # check the first against the second
        if (my_pool[0].hostId != my_pool[1].hostId):
            # the first one is by itself
            still_looking = False
            lonely_uuid = my_pool[0].id

        # check the against the middle
        i = 1
        while (still_looking and i < (len(my_pool)-1)) :
            if ((my_pool[i].hostId == my_pool[i-1].hostId) or
                (my_pool[i].hostId == my_pool[i+1].hostId)) :
                still_looking = True
                i += 1
                continue
            else :
                still_looking = False
                lonely_uuid = my_pool[i].id
                break

        # check the last one if we need to
        if (still_looking and (my_pool[i].hostId !=
                               my_pool[i+1].hostId)):
            lonely_uuid = my_pool[i+1].id
            still_looking = False

        if (still_looking) :
            # we didn't find one by itself
            return -1

        for (i, vm) in enumerate(self.vm_pool,start=0):
            if vm.id == lonely_uuid :
                return i



    def print_ip_pool(self) :
        """
        Display the current contents of the IP pool
        """
        logging.debug("Contents of IP pool:")
        for ip in self.ip_pool :
            logging.debug("IP: %s  ID: %s",
                          ip['floating_ip_address'], ip['id'])

    def known_hosts_ip_remove(self, ip_address) :
        """
        Remove an IP address from the known_hosts file
        """
        cmd = "ssh-keygen -f ~/.ssh/known_hosts -R " + str(ip_address) + " > /dev/null"
        logging.debug("Removing %s from ~/.ssh/known_hosts using command '%s'",
                      ip_address, cmd)
        self.do_in_shell(cmd,True)

    def create_network(self, name):
        body = dict(
            network = dict(
                name = name,
            ),
        )
        logging.debug("create_network body=%s", body)
        result = self.qc.create_network(body = body)
        return result['network']

    def delete_network(self, network) :
        self.qc.delete_network(network['id'])

    def get_network(self, net_name) :
        for network in self.qc.list_networks()['networks']:
            if network['name'] == net_name:
                return network
        logging.warning("Could not find a network named %s",net_name)
        raise RuntimeError

    def create_subnet(self, network, cidr, name):
        body = dict(
            subnet=dict(
                ip_version = 4,
                network_id = network['id'],
                cidr = cidr,
                name = name,
                ),
            )
        logging.debug('create_subnet body=%s', body)
        result = self.qc.create_subnet(body = body)
        return result['subnet']

    def create_floatingip(self, network):
        body = { 'floatingip' : { 'floating_network_id' : network['id'] } }
        logging.debug('create_floatingip(body=%s)', body)
        result = self.qc.create_floatingip(body=body)
        return result['floatingip']

    def create_port(self, network, subnet, name, security_groups):
        body = { 'port' :
                    {
                     'network_id' : network['id'],
                     'name' : name,
                    }
                }
        if subnet != None :
            body['port']['fixed_ips'] = [ { "subnet_id": subnet['id'] } ]
        if security_groups != None :
            sec_group_ids = []
            for sec_group in security_groups :
                sec_group_ids.append(sec_group['id'])
            body['port']['security_groups'] = sec_group_ids

        logging.debug('create_port(body=%s)', body)
        result = self.qc.create_port(body=body)
        return result['port']

    def delete_port(self, port):
        logging.debug('port(id=%s, network_id=%s, fixed_ips=%s, status=%s) : ', port['id'], port['network_id'], port['fixed_ips'], port['status'])
        # logging.debug('delete_port(%s)', port['id'])
        self.qc.delete_port(port['id'])

    def update_floatingip(self, floating_ip, port):
        body = { 'floatingip' : { 'port_id' : port['id'] } }
        logging.debug('update_floatingip(%s, body=%s)', floating_ip['id'], body)
        result = self.qc.update_floatingip(floating_ip['id'], body=body)
        return result['floatingip']


    def create_router(self, name):
        body = { 'router' : { 'name' : name } }
        logging.debug('create_router body=%s', body)
        result = self.qc.create_router(body = body)
        return result['router']

    def add_gateway_router(self, router, network):
        body = { 'network_id' : network['id'] }
        logging.debug('add_gateway_router body=%s', body)
        self.qc.add_gateway_router(router['id'], body = body)

    def add_interface_router(self, router, subnet) :
        body = { "subnet_id" : subnet['id'] }
        logging.debug('add_interface_router body=%s', body)
        self.qc.add_interface_router(router['id'], body = body)

    def remove_interface_router(self, router, subnet) :
        body = { "subnet_id" : subnet['id'] }
        logging.debug('remove_gateway_router body=%s', body)
        self.qc.remove_interface_router(router['id'], body = body)

    def delete_router(self, router):
        self.qc.delete_router(router['id'])

    def deallocate_ip_pool(self) :
        """
        Return the contents of self.ip_pool to the sea
        """
        while (len(self.ip_pool) > 0) :
            ip = self.ip_pool.pop()
            try:
                self.qc.delete_floatingip(ip['id'])
            except Exception as e:
                logging.warning("Unable to deallocate IP: %s  ID: %s  Error: %s",
                                ip.ip, ip.id, e)

    def allocate_ip_pool(self, count, network) :
        """
        Allocate a pool of public IPs to be used when perf testing
        """

        body = { 'floatingip' : { 'floating_network_id' : network['id'] } }
        logging.debug("Creating floating IPs using body=%s",body)
        for i in xrange(count) :
            try :
                ip = self.qc.create_floatingip(body = body)['floatingip']
                self.ip_pool.append(ip)
            except Exception as e:
                self.deallocate_ip_pool()
                self.fail("Error while allocating %d IPs '%s'" %
                          (count, str(e)))
            self.known_hosts_ip_remove(str(ip['floating_ip_address']))

    def deallocate_port_pool(self) :
        for port in self.port_pool :
            try :
                self.delete_port(port)
            except:
                pass
        self.port_pool = []

    def allocate_port_pool(self, count, network, subnet) :
        """
        Allocate a pool of ports to use when perf testing
        """
        for i in xrange(count) :
            try :
                name = "%s%s_port%.4d" % ("netperftesting",
                                          str(self.start),
                                          i)
                port =  self.create_port(network, subnet, name, [self.security_group])
                self.port_pool.append(port)
            except Exception as e:
                self.deallocate_port_pool()
                self.fail("Error while allocating %d ports '%s'" %
                          (count, str(e)))

    def associate_ips_with_ports(self, ips, ports) :
        try :
            for (ip, port) in zip(ips, ports):
                self.update_floatingip(ip, port)
        except Exception as e:
            self.clean_up_overall()
            self.fail("Error associating ports and IPs '%s'" %
                      str(e))

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

    def delete_security_group(self, sec_group):
        logging.debug('delete_security_group(%s)', sec_group['id'])
        self.qc.delete_security_group(sec_group['id'])

    def create_security_group(self, name) :
        body = { 'security_group' : { 'name' : name } }
        logging.debug('create_security_group(body=%s)', body)
        result = self.qc.create_security_group(body=body)
        return result['security_group']

    def create_security_group_rule(self, sec_group, protocol='tcp', direction='ingress', min_port=None, max_port=None) :
        body = dict(
            security_group_rule=dict(
            security_group_id=sec_group['id'],
            protocol=protocol,
            direction=direction,
            port_range_min=min_port,
            port_range_max=max_port,
            ),
        )
        logging.debug('create_security_group_rule(body=%s)', body)
        result = self.qc.create_security_group_rule(body=body)
        return result['security_group_rule']

    def create_self_security_group(self):

        #print "Creating security group"

        self.security_group = self.create_security_group("netperftesting" + str(self.start))

        self.create_security_group_rule(self.security_group,
                                        protocol = 'tcp',
                                        min_port = 1,
                                        max_port = 65535)

        self.create_security_group_rule(self.security_group,
                                        protocol = 'udp',
                                        min_port = 1,
                                        max_port = 65535)

        self.create_security_group_rule(self.security_group,
                                        protocol = 'icmp')

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
        parser.add_argument("--availability-zone",
                            help="Select a specific availability zone when launching instances")
        parser.add_argument("--region",
                            default=self.env('OS_REGION_NAME',
                                             'NOVA_REGION_NAME',
                                             help="Specify the region in which to test"))
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
                            default=self.env('INSTANCE_USERNAME', default='ubuntu'),
                            help="The name of the user on the instance(s)")
        parser.add_argument("--use-private-ips",
                            help="Test against instance private IPs rather than public",
                            action="store_true")

        parser.add_argument("--singleton",
                            help="Attempt to find a VM not on the same hostId as any other to use as the Instance Under Test",
                            action="store_true")
        parser.add_argument("--external-network",
                            help="Name of the external network. Used for the plumbing of the router.",
                            default=self.env('EXTERNAL_NETWORK', default='Ext-Net'))

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
        self.test_network = None
        self.test_subnet = None
        self.test_router = None
        self.external_network = None

        self.result_logger = logging.getLogger('results')
        fh = logging.FileHandler('results.log')
        fh.setLevel(logging.INFO)
        self.result_logger.addHandler(fh)

        parser = self.setup_parser()

        self.args = parser.parse_args()

        self.get_flavor()
        self.get_collectd_sock()
        self.get_archive_path()

        self.os = Client(self.args.username,
                         self.args.password,
                         self.args.project,
                         self.args.url,
                         region_name = self.args.region,
                         cacert = environ.get('OS_CACERT', None))

        if (self.os == None) :
            self.fail("OpenStack API connection setup unsuccessful!")

        # Nova wanted cacert, but Neutron wants ca_cert.  It is always
        # nice to be reminded that OpenStack is not burdened by the
        # ravages of consistency
        self.qc = neutron_client.Client(username=self.args.username,
                                        password=self.args.password,
                                        tenant_name=self.args.project,
                                        auth_url=self.args.url,
                                        region_name=self.args.region,
                                        ca_cert = environ.get('OS_CACERT', None));

        if (self.qc == None):
            self.fail("OpenStack Neutron API connection setup unsuccessful!")

#        self.clean_vestigial_keypairs()

        if not "rackspace" in self.env('OS_AUTH_SYSTEM',default=[]):
            self.allocate_key()
            self.create_self_security_group()
        else:
            self.security_group = None
            self.keypair = None

        self.flavor_list = self.os.flavors.list()

        logging.warning("Setting-up test network plumbing")

        self.external_network = self.get_network(self.args.external_network)

        self.test_network = self.create_network("netperftesting"+str(self.start))
        self.test_subnet = self.create_subnet(self.test_network,
                                              "192.168.255.0/24",
                                              "netperftesting"+str(self.start))
        self.test_router = self.create_router("netperftesting"+str(self.start))

        self.add_interface_router(self.test_router, self.test_subnet);
        self.add_gateway_router(self.test_router, self.external_network)

        self.ip_count = 5
        self.allocate_ip_pool(self.ip_count, self.external_network)
        self.print_ip_pool()

        self.find_suitable_images()
        # for now just take the first one

        if self.suitable_images:
            self.chosen_image = self.suitable_images.pop()
            logging.warning("The chosen image is %s",
                         self.chosen_image.name)
        else:
            self.clean_up_overall()
            self.fail("Unable to find a suitable image to test")

    def create_remote_hosts(self,sut_index) :
        """
        Create the remote_hosts file that will be used by the
        runemomniaggdemo.sh script.  if the script is ever enhanced to
        make sure it does not run a loopback test, we can just dump
        all the IP addresses in there.
        """
        try :
            fl = open("remote_hosts", "w")
            i=0
            for j,ip in enumerate(self.ip_pool,start=0) :
                if j != sut_index :
                    if not self.args.use_private_ips :
                        dstip = ip['floating_ip_address']
                    else:
                        # we could have I suppose looked-up the mappings
                        # when we did the associate/update of the floating
                        # IPs, but since we only really need to know the
                        # private IPs when we are going to use the private
                        # IPs, and we only need to know them here, there
                        # isn't much point doing it elsewhere.  of course,
                        # since OpenStack Neutron is not going to fall
                        # into the trap of foolish consistency, the "ip" a
                        # show_floatingip will return is going to be ever
                        # so slightly different from the "ip" a create
                        # will have returned...
                        ip2 = self.qc.show_floatingip(ip['id'])
                        dstip = ip2['floatingip']['fixed_ip_address']

                    fl.write("REMOTE_HOSTS[%d]=%s\n"%(i,dstip))
                    i += 1

            fl.write("NUM_REMOTE_HOSTS=%d\n" % (i))

        except Exception as e:
            logging.warning("Unable to create/write remote_hosts file. %s ",
                            str(e))
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
        for (node, ip) in zip(self.vm_pool, self.ip_pool) :
            publicip = ip['floating_ip_address']
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
                logging.warning("The copying of files to %s failed. (%s)",
                                node.name, e)
                raise RuntimeError

    def start_netservers(self) :
        """
        Start netservers on every node.
        """

        for (node, ip) in zip(self.vm_pool, self.ip_pool) :
            publicip = ip['floating_ip_address']
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
                #print stdout.readlines()
                #print stderr.readlines()
                status = stdout.channel.recv_exit_status()
                ssh.close()

            except Exception as e:
                logging.warning("Starting netserver processes was unsuccessful on %s at IP %s",
                                node.name, publicip)
                raise RuntimeError

    def run_netperf(self,sut_index) :
        """
        Actually run the runemomniaggdemo.sh script on the sut node
        """
        try :
            publicip = self.ip_pool[sut_index]['floating_ip_address']
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
                            password=self.adminPasses[self.vm_pool[sut_index].id],
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
                            self.vm_pool[sut_index].name, publicip, str(e))
            raise RuntimeError

    def copy_back_results(self, destination, sut_index) :
        try :
            publicip = self.ip_pool[sut_index]['floating_ip_address']
            logging.warning("Copying-back results")
            transport = paramiko.Transport((publicip,22))
            if self.keypair:
                mykey = paramiko.RSAKey.from_private_key_file(os.path.abspath(self.keyfilename))
                transport.connect(username=self.args.instanceuser,
                                  pkey = mykey)
            else:
                transport.connect(username=self.args.instanceuser,
                                  password=self.adminPasses[self.vm_pool[sut_index].id])
  
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
                             self.vm_pool[sut_index].name, publicip)
           raise RuntimeError

    def ssh_check(self, time_limit=300) :
        """
        SSH into the VMs and report on progress. we ass-u-me that the
        VMs are already in a happy, running state, or will be within
        the time_limit.  We give-up only if there is a problem after
        our time_limit.  So long as the ssh's remain error-free we
        will keep going as long as it takes to get through the list.
        """
        starttime = time()
        deadline = starttime + time_limit
        sleeptime = 1

        sudo = "sudo"
        if self.args.instanceuser == 'root':
            sudo = ""

        cmd = "hostname; uname -a; date; %s sysctl -w net.core.rmem_max=1048576 net.core.wmem_max=1048576" % sudo


        for (vm, ip) in zip(self.vm_pool, self.ip_pool) :

            publicip = ip['floating_ip_address']
            #print "Sshing to %s" % publicip
            not_connected = True
            while (not_connected) :
                try:
                    ssh = paramiko.SSHClient()
                    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
                    # paper or plastic?
                    if self.keypair:
                        #print "IP %s user %s key filename %s" % (publicip, self.args.instanceuser,os.path.abspath(self.keyfilename))
                        ssh.connect(hostname=publicip,
                                    username=self.args.instanceuser,
                                    key_filename=os.path.abspath(self.keyfilename),
                                    timeout=30.0)
                    else:
                    #print "ssh.connect to %s user %s pass %s" % (publicip,self.args.instanceuser,self.adminPasses[vm.id])
                        ssh.connect(hostname=publicip,
                                    username=self.args.instanceuser,
                                    password=self.adminPasses[vm.id],
                                    timeout=30.0)

                    stdin, stdout, stderr = ssh.exec_command(cmd)
                    #print stdout.readlines()
                    #print stderr.readlines()
                    status = stdout.channel.recv_exit_status()
                    ssh.close()
                    not_connected = False
                    sleeptime = 1
                except Exception as e :
                    now = time()
                    #print "Attempt to ssh failed with %s" % str(e)
                    if (now < deadline) :
                        sleep(sleeptime)
                        sleeptime *= 2
                        sleeptime = min(sleeptime, 120, deadline - now)

                    else :
                        logging.warning("The ssh check to %s (IP:%s ID:%s) using command '%s' failed after the %d second limit (%s)",
                                        vm.name, publicip, vm.id, cmd, time_limit,str(e))
                        #self.fail("That's all folks")
                        raise RuntimeError


    def extract_min_avg_max(self, flavor, name, postresults) :

        avg = minimum = maximum = end = start ="0"
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
                minimum = toks[5]
                logging.debug("Found Minimum of peak interval %s ", minimum)
            if (re.match("Maximum of peak interval is",line)) :
                toks = line.split(" ",11)
                maximum = toks[5]
                logging.debug("Found Maximum of peak interval %s", maximum)
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
                self.collectd_socket.send("PUTVAL \"netperf-"+self.args.region+"/unixsock-"+flavor.name+"-"+testtype+"/min\" interval=900 "+end+":"+minimum+"\n")
                self.collectd_socket.send("PUTVAL \"netperf-"+self.args.region+"/unixsock-"+flavor.name+"-"+testtype+"/avg\" interval=900 "+end+":"+avg+"\n")
                self.collectd_socket.send("PUTVAL \"netperf-"+self.args.region+"/unixsock-"+flavor.name+"-"+testtype+"/max\" interval=900 "+end+":"+maximum+"\n")
                logging.warning("Wrote results for "+self.args.region+" "+flavor.name+" "+testtype+" to collectd")

            ip = "PrivateIPs" if self.args.use_private_ips else "PublicIPs"
            prefix = "%s.%s.%s" % (ip, self.args.availability_zone, self.args.region)
            prefix = prefix + ":" + self.chosen_image.name + ":" + flavor.name + ":" + testtype
            suffix = ":" + units + ":" + end
            self.result_logger.warning(prefix + ":min:" + minimum + suffix)
            self.result_logger.warning(prefix + ":avg:" + avg + suffix)
            self.result_logger.warning(prefix + ":max:" + maximum + suffix)

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

        az = ""
        if self.args.availability_zone:
            az = "%s of " % self.args.availability_zone

        logging.warning("Testing flavor: %s in %sregion %s",
                        flavor.name, az, self.args.region)
        archive_location = os.path.join(self.archive_path,
                                        str(flavor.name),
                                        "")
        os.makedirs(archive_location)

        logging.warning("Creating test ports.")
        self.allocate_port_pool(self.ip_count,
                                self.test_network,
                                self.test_subnet)
        logging.warning("Associating floating IPs with test ports.")
        self.associate_ips_with_ports(self.ip_pool,
                                      self.port_pool)

        self.allocate_server_pool(5,
                                  flavor,
                                  self.chosen_image)

        logging.warning("Awaiting server pool active.")
        self.await_server_pool_ready(600)
        singleton_iut = 0
        if self.args.singleton:
            singleton_iut = self.find_singleton_server()
            if singleton_iut < 0:
                logging.warning("Unable to find an instance not sharing a hostId.")
                singleton_iut = 0
            else:
                logging.warning("The singleton VM is %s on %s index %d" % (self.vm_pool[singleton_iut].name, self.vm_pool[singleton_iut].hostId, singleton_iut))

        logging.warning("Verifying up and running via ssh.")
        self.ssh_check()
        logging.warning("Completed ssh check.")
        self.create_remote_hosts(singleton_iut)
        logging.warning("Creation of remote_hosts file complete with use of private IPs %s." % self.args.use_private_ips)
        self.scp_binaries()
        logging.warning("Transfer of binaries complete.")
        self.start_netservers()
        logging.warning("Netservers started. about to start the script.")
        self.run_netperf(singleton_iut)
        logging.warning("Netperf run complete. Copying back results.")
        self.copy_back_results(archive_location,singleton_iut)
        logging.warning("Cleaning up vms.")
        self.deallocate_server_pool()
        # there is some ongoing confusion as to whether deleting a
        # server will always delete the port.
        self.deallocate_port_pool()
        self.await_server_pool_gone(180)

        logging.warning("Server pool deallocated.")



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
            except Exception as e:
                logging.warning("Test of flavor '%s' (%s) unsuccessful %s",
                                flavor.name,
                                flavor.id,
                                str(e))
                try:
                    self.clean_up_instances()
                    self.deallocate_port_pool()
                except Exception as e2:
                    logging.warning("Cleanup of flavor '%s' (%s) unsuccessful %s",
                                    flavor.name,
                                    flavor.id,
                                    str(e2))


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
        import inspect
        curframe = inspect.currentframe()
        calframe = inspect.getouterframes(curframe, 2)
        logging.info("Asked by %s to clean-up everything left",
                     calframe[1][3])

        if (self.keypair != None) :
            os.remove(self.keypair.name + ".pem")
            self.os.keypairs.delete(self.keypair)
            self.keypair = None

        if (self.ip_pool != []) :
            self.deallocate_ip_pool()
            
        self.clean_up_instances()

        if self.security_group :
            self.delete_security_group(self.security_group)
            self.security_group = None

        if self.test_router and self.test_subnet:
            self.remove_interface_router(self.test_router,self.test_subnet)

        # do I need to worry about clearing the external network gateway?

        if self.test_router :
            self.delete_router(self.test_router)

        if self.port_pool != [] :
            self.deallocate_port_pool()

        # do I need to worry about deleting the subnet?
        if self.test_network :
            for retries in xrange(1,4):
                try :
                    self.delete_network(self.test_network)
                    break
                except Exception as e:
                    logging.warning("Unable to delete the test network on try %d. (%s)", retries, str(e))
                    sleep(1)

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

    tn.test_flavors()
    tn.clean_up_overall()
