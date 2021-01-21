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

#include <string.h>
#include <ctype.h>

#if defined(NETPERF_STANDALONE_DEBUG)
#include <stdio.h>
#include <stdlib.h>
#endif

void
find_driver_info(char *ifname, char *driver, char *version, char *firmware, char *bus, int len) {

  /* until something better comes along, we will use the expedient
     that the interface name, up to but not including the instance
     number is the driver name. raj 2008-03-19 */
  int i;

  strncpy(driver,ifname,len);
  driver[len-1] = 0;

  /* work backwards nuking numbers and punctuation */
  for (i = strlen(driver) - 1; ((isdigit(driver[i])) ||
				(ispunct(driver[i]))) && (i >= 0); i--) {
    driver[i] = 0;
  }

  /* on the off chance we managed to toast the entire string, we
     should probably mention that somehow. raj 2008-03-19 */
  if (strlen(driver) == 0)
    strncpy(driver,"NoAlpha",len);

  strncpy(version,"Unavailable",len);
  strncpy(firmware,"Unavailable",len);
  strncpy(bus,"Unavailable",len);
  version[len-1] = 0;
  firmware[len-1] = 0;
  bus[len-1] = 0;
  return;
}

#if defined(NETPERF_STANDALONE_DEBUG)
int
main(int argc, char *argv[]) {

#define MYLEN 32
  char driver[MYLEN];
  char version[MYLEN];
  char firmware[MYLEN];
  char bus[MYLEN];

  if (argc != 2) {
    fprintf(stderr,"%s <interfacename>\n",argv[0]);
    exit(-1);
  }

  find_driver_info(argv[1],driver, version, firmware, bus, MYLEN);

  printf("Interface %s driver %s version %s firmware %s bus %s\n",
	 argv[1], driver, version, firmware, bus);

  return 0;

}
#endif
