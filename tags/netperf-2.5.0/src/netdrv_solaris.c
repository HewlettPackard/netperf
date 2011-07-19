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
