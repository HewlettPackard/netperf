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
#include "config.h"
#endif

#if defined(HAVE_STRING_H)
#include <string.h>
#endif

#include <stdio.h>

#include "netlib.h"

void
find_security_info(int *enabled, int *type, char **specific){
  *enabled = NSEC_UNKNOWN;
  *type    = NSEC_TYPE_UNKNOWN;
  *specific = strdup("N/A");
  return;
}

#if defined(NETPERF_STANDALONE_DEBUG)
int
main(int argc, char *argv[]) {

  char *specific;
  int enabled;
  int type;

  find_security_info(&enabled, &type, &specific);

  printf("Security info: enabled %d type 0x%x specific %s\n",
	 enabled,
	 type,
	 specific);

  return 0;
}
#endif
