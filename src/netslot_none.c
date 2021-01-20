
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

#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#if defined(HAVE_STRING_H)
#include <string.h>
#endif

#if defined(NETPERF_STANDALONE_DEBUG)
#include <stdio.h>
#endif

#ifdef WIN32
#define strdup _strdup
#endif

char *
find_interface_slot(char *interface_name) {
  return strdup("Not Implemented");
}

void
find_interface_ids(char *interface_name, int *vendor, int *device, int *sub_vend, int *sub_dev) {
  *vendor = 0;
  *device = 0;
  *sub_vend = 0;
  *sub_dev = 0;
  return;
}

#if defined(NETPERF_STANDALONE_DEBUG)
int
main(int argc, char *argv[]) {

  char *slot;
  int vendor;
  int device;
  int subvendor;
  int subdevice;

  if (argc != 2) {
    fprintf(stderr,"%s <interface>\n",argv[0]);
    return -1;
  }

  slot = find_interface_slot(argv[1]);

  find_interface_ids(argv[1], &vendor, &device, &subvendor, &subdevice);

  printf("%s in in slot %s: vendor %4x device %4x subvendor %4x subdevice %4x\n",
	 argv[1],
	 slot,
	 vendor,
	 device,
	 subvendor,
	 subdevice);

  return 0;
}
#endif
