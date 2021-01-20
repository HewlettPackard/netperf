/* Copyright 2010, Hewlett-Packard Company
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

#include <sys/libIO.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <strings.h>
#include <netsh.h>
#include <netlib.h>

/*
 * No OLAR headerfile on target system! ;-( Yes, this means that these
 * interfaces are NOT DOCUMENTED FOR PUBLIC USE, which means you don't
 * really see what you are reading here, and if you attempt to use it
 * elsewhere, no one will profess any knowledge of what you are doing.
 * You will be utterly and completely on your own.
 */
typedef uint64_t olar_io_slot_t;

typedef struct olar_error_info {
        int  oe_err;
        char oe_hwpath[MAX_HW_PATH_STR];
} olar_err_t;

#define MAX_SLOT_ID_LEN 30

char * get_hw_path_from_if(char *if_name);

char *
find_interface_slot(char *interface_name) {

  char *hw_str;
  olar_io_slot_t slot_id;
  olar_err_t oe;
  char slot_str[MAX_SLOT_ID_LEN];

  /* Open dev_config for libIO communication */
  if ( io_init(O_RDONLY) == IO_ERROR ) {
    return strdup("io_init");
  }

  hw_str = get_hw_path_from_if(interface_name);

  /* close dev_config */
  io_end();

  slot_id = 0;
  if ( olar_path_to_id(hw_str,&slot_id,&oe) == -1 ) {
    /* since the call failed, lets give them the HW path as a
       consolation prize. we will ass-u-me that the caller will be
       freeing the string we give him anyway. */
    if (debug) {
      fprintf(where,
	      "%s olar_path_to_id hw_str %s oe_err %d path %s\n",
	      __func__,
	      hw_str,
	      oe.oe_err,
	      oe.oe_hwpath);
      fflush(where);
    }
    return hw_str;
  }

  if ( olar_slot_id_to_string(slot_id,slot_str,30) == -1 ) {
    /* do the same thing here, give them the hw path if this call
       fails */
    if (debug) {
      fprintf(where,
	      "%s olar_slot_id_to_string slot_id %" PRId64 "\n",
	      slot_id);
      fflush(where);
    }
    return hw_str;
  }

  /* we can give them the honest to goodness slot id as a string now,
     so let us free that which we should free */
  free(hw_str);

  return strdup(slot_str);
}

/*
 * Returns the H/W path string corresponding to the lan if name
 *
 * Assumption: if_name is of type lan%ppid
 *             Not known to work with Logical,Vlan etc..
 *
 */
char * get_hw_path_from_if(char *if_name)
{
  int instance;
  io_token_t tok;
  hw_path_t hw;
  char *hwstr;

  sscanf(if_name,"lan%d",&instance);

  if ( (tok = io_search(NULL,S_IOTREE_EXT,0,"class", "lan",
                        "instance", &instance, NULL)) == NULL ) {
    /* we don't want extraneous output on netserver side - at some
       point we can teach it about "where" */
    /* io_error("Could not find H/w path"); */
    return(NULL);
  }

  if ( io_node_to_hw_path(tok, &hw) != IO_SUCCESS ) {
    /* io_error("io_node_to_hw_path failed"); */
    return NULL;
  }

  hwstr = (char *) malloc (MAX_HW_PATH_STR * sizeof(char));
  if (hwstr == NULL ) {
    /* perror("malloc failed in get_hw_path_from_if"); */
    return NULL;
  }

  if  (io_hw_path_to_str(hwstr,&hw) == IO_ERROR ) {
    /* io_error("io_hw_path_to_str failed"); */
    free(hwstr);
    return NULL;
  }

  return hwstr;
}

void
find_interface_ids(char *interface_name, int *vendor, int *device, int *sub_vend, int *sub_dev) {
  *vendor = 0;
  *device = 0;
  *sub_vend = 0;
  *sub_dev = 0;
  return;
}
