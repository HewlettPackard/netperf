#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#if defined(HAVE_STRING_H)
#include <string.h>
#endif

#if defined(NETPERF_STANDALONE_DEBUG)
#include <stdio.h>
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
