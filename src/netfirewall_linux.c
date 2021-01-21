/*
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
*/

/* Really simplistic, bordering on brain-dead way to go about allowing
   netperf to run successfully on a system with firewalls enabled, and
   only the netperf control port open in the firewall.  We expect code
   called by netserver to call into this with the port number to open.
   We will then open the port in the system-local firewall, and
   store-off if that port was already open.  Then at the end of the
   test, code called by netserver will call in again and if the port
   was not enabled before, we will disable it.  If it was enabled
   before, we do nothing.  We assume there is only ever one port
   number being manipulated in this way per netserver process. raj
   20130211*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>

static int port_was_enabled = 0;
static int enabled_port = -1;
static int enabled_protocol = -1;

static char *protocol_to_ufw(int protocol) {
  switch (protocol) {
  case IPPROTO_TCP:
    return("tcp");
    break;
  case IPPROTO_UDP:
    return("udp");
    break;
#if defined(IPPROTO_SCTP)
  case IPPROTO_SCTP:
    return("sctp");
    break;
#endif
#if defined(IPPROTO_DCCP)
  case IPPROTO_DCCP:
    return "dccp";
    break;
#endif
#if defined(IPPROTO_UDPLITE)
  case IPPROTO_UDPLITE:
    return "udplite";
    break;
#endif
  default:
    return("UNKNOWN");
  }
}

void
enable_port(int port, int protocol) {
  char command[128];

  if ((port < 0) || (port > 65535))
    return;

  /* one of these days we will have to learn the proper way to see if
     a port is already open under Linux... */
  sprintf(command,
	  "ufw allow %d/%s 2>&1 > /dev/null",
	  port,
	  protocol_to_ufw(protocol));
  if (system(command) < 0) {
    /* if the command failed outright, don't bother at the back-end */
    port_was_enabled = 0;
  }
  else {
    port_was_enabled = 1;
    enabled_port = port;
    enabled_protocol = protocol;
  }
  return;
}

void
done_with_port(int port, int protocol) {
  char command[128];

  if (port_was_enabled) {
    sprintf(command,
	    "ufw delete allow %d/%s 2>&1 > /dev/null",
	    enabled_port,
	    protocol_to_ufw(enabled_protocol));
    system(command);
  }
  return;
}
