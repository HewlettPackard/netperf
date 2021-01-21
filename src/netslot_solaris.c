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
#endif

#include <stdlib.h>
#include <net/if.h>
#include <devid.h>
#include <libdevinfo.h>

char *
find_interface_slot(char *interface_name) {
  return strdup("Not Implemented");
}

static char interface_match[IFNAMSIZ];
static int  found_vendor = 0;
static int  found_device = 0;
static int  found_subvendor = 0;
static int  found_subdevice = 0;

static char *
set_interface_match(char *interface_name) {

  int i;
  char *nukeit;

  strncpy(interface_match,interface_name,IFNAMSIZ);
  interface_match[IFNAMSIZ-1] = 0;


  /* strip away the logical interface information if present we "know"
     thanks to the above that we will find a null character to get us
     out of the loop */
  for (nukeit = strchr(interface_match,':');
       (NULL != nukeit) && (*nukeit != 0);
       nukeit++) {
    *nukeit = 0;
  }

  if (strlen(interface_match) == 0)
    return NULL;
  else
    return interface_match;

}

/* take the binding name for our found node and try to break it up
   into pci ids. return the number of IDs we found */

static int
parse_binding_name(char *binding_name) {

  char *my_copy;
  char *vend;
  char *dev;
  char *subvend;
  char *subdev;
  int  count;
  int  i;

  /* we cannot handle "class" :) */
  if (NULL != strstr(binding_name,"class"))
    return 0;

  my_copy = strdup(binding_name);
  if (NULL == my_copy)
    return 0;

  /* we assume something of the form:

     pci14e4,164c or perhaps
     pci14e4,164c.103c.7038.12 or
     pciex8086,105e.108e.105e.6 or

     where we ass-u-me that the first four hex digits before the comma
     are the vendor ID, the next four after the comma are the device
     id, the next four after the period are the subvendor id and the
     next four after the next dot are the subdevice id. we have
     absolutely no idea what the digits after a third dot might be.

     of course these:

     pciex108e,abcd.108e.0.1
     pci14e4,164c.12

     are somewhat perplexing also.  Can we ass-u-me that the id's will
     always be presented as four character hex? Until we learn to the
     contrary, that is what will be ass-u-me-d here and so we will
     naturally ignore those things, which might be revision numbers
     raj 2008-03-20 */

  vend = strtok(my_copy,",");
  if (NULL == vend) {
    count = 0;
  }
  else {
    /* take only the last four characters */
    if (strlen(vend) < 5) {
      count = 0;
    }
    else {
      /* OK, we could just update vend I suppose, but for some reason
         I felt the need to blank-out the leading cruft... */
      for (i = 0; i < strlen(vend) - 4; i++)
	vend[i] = ' ';
      found_vendor = strtol(vend,NULL,16);
      /* ok, now check for device */
      dev = strtok(NULL,".");
      if ((NULL == dev) || (strlen(dev) != 4)) {
	/* we give-up after vendor */
	count = 1;
      }
      else {
	found_device = strtol(dev,NULL,16);
	/* ok, now check for subvendor */
	subvend = strtok(NULL,".");
	if ((NULL == subvend) || (strlen(subvend) != 4)) {
	  /* give-up after device */
	  count = 2;
	}
	else {
	  found_subvendor = strtol(subvend,NULL,16);
	  /* ok, now check for subdevice */
	  subdev = strtok(NULL,".");
	  if ((NULL == subdev) || (strlen(subdev) != 4)) {
	    /* give-up after subvendor */
	    count = 3;
	  }
	  else {
	    found_subdevice = strtol(subdev,NULL,16);
	    count = 4;
	  }
	}
      }
    }
  }
  return count;
}

static int
check_node(di_node_t node, void *arg) {

  char *nodename;
  char *minorname;
  char *propname;
  char *bindingname;
  di_minor_t minor;
  di_prop_t  prop;
  int  *ints;

#ifdef NETPERF_STANDALONE_DEBUG
  nodename = di_devfs_path(node);
  /* printf("Checking node named %s\n",nodename); */
  di_devfs_path_free(nodename);
#endif

  minor = DI_MINOR_NIL;
  while ((minor = di_minor_next(node,minor)) != DI_MINOR_NIL) {
    /* check for a match with the interface_match */
    minorname = di_minor_name(minor);
#ifdef NETPERF_STANDALONE_DEBUG
    /* printf("\tminor name %s\n",minorname); */
#endif
    /* do they match? */
    if (strcmp(minorname,interface_match) == 0) {
      /* found a match */
      bindingname = di_binding_name(node);
#ifdef NETPERF_STANDALONE_DEBUG
      printf("FOUND A MATCH ON %s under node %s with binding name %s\n",interface_match,
	     nodename,
	     bindingname);
#endif

      if (parse_binding_name(bindingname) == 4) {
	/* we are done */
	return DI_WALK_TERMINATE;
      }

      /* ok, getting here means we didn't find all the names we seek,
         so try taking a look at the properties of the node.  we know
         that at least one driver is kind enough to set them in
         there... and if we find it, we will allow that to override
         anything we may have already found */
      prop = DI_PROP_NIL;
      while ((prop = di_prop_next(node,prop)) != DI_PROP_NIL) {
	propname = di_prop_name(prop);
#ifdef NETPERF_STANDALONE_DEBUG
	printf("\t\tproperty name %s\n",propname);
#endif
	/* only bother checking the name if the type is what we expect
           and we can get the ints */
	if ((di_prop_type(prop) == DI_PROP_TYPE_INT) &&
	    (di_prop_ints(prop,&ints) > 0)) {
	  if (strcmp(propname,"subsystem-vendor-id") == 0)
	    found_subvendor = ints[0];
	  else if (strcmp(propname,"subsystem-id") == 0)
	    found_subdevice = ints[0];
	  else if (strcmp(propname,"vendor-id") == 0)
	    found_vendor = ints[0];
	  else if (strcmp(propname,"device-id") == 0)
	    found_device = ints[0];
	}
      }
      /* since we found a match on the name, we are done now */
      return DI_WALK_TERMINATE;
    }
  }
  return DI_WALK_CONTINUE;

}
void
find_interface_ids(char *interface_name, int *vendor, int *device, int *sub_vend, int *sub_dev) {

  di_node_t root;
  char *interface_match;

  /* so we have "failure values" ready if need be */
  *vendor = 0;
  *device = 0;
  *sub_vend = 0;
  *sub_dev = 0;

  interface_match = set_interface_match(interface_name);
  if (NULL == interface_match)
    return;

  /* get the root of all devices, and hope they aren't evil */
  root = di_init("/", DINFOCPYALL);

  if (DI_NODE_NIL == root)
    return;

  /* now we start trapsing merrily around the tree */
  di_walk_node(root, DI_WALK_CLDFIRST,NULL,check_node);

  di_fini(root);
  *vendor = found_vendor;
  *device = found_device;
  *sub_vend = found_subvendor;
  *sub_dev  = found_subdevice;
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
