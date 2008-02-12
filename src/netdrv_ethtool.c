#include <sys/types.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <linux/sockios.h>

/* alas, direct inclusion of ethtool.h depends on some types not
   normally found in nature, which we must provide or things will be
   quite unhappy. */

typedef __uint64_t __u64;
typedef __uint32_t __u32;
typedef __uint16_t __u16;
typedef __uint8_t  __u8;

#include <linux/ethtool.h>

void
find_driver_info(char *ifname, char *driver, char *version, char *firmware, char *bus, int len) {

  int s;
  int ret;
  struct ifreq ifr;
  struct ethtool_drvinfo drvinfo;

  if (len < 32) return;

  if (!strcmp(ifname,"lo")) {
    /* special case loopback */
    strncpy(driver,"loopback",len);
    strncpy(version,"system",len);
    strncpy(firmware,"N/A",len);
    strncpy(bus,"N/A",len);
    driver[len-1] = 0;
    version[len-1] = 0;
    firmware[len-1] = 0;
    bus[len-1] = 0;
    return;
  }

  s = socket(AF_INET,SOCK_DGRAM,0);

  if (s < 0) {
    strncpy(driver,"SocketFailure",len);
    strncpy(version,"SocketFailure",len);
    strncpy(firmware,"SocketFailure",len);
    strncpy(bus,"SocketFailure",len);
    driver[len-1] = 0;
    version[len-1] = 0;
    firmware[len-1] = 0;
    bus[len-1] = 0;
    return;
  }

  memset(&ifr, 0, sizeof(ifr));
  drvinfo.cmd = ETHTOOL_GDRVINFO;
  strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)-1);
  ifr.ifr_data = (caddr_t)&drvinfo;

  ret = ioctl(s, SIOCETHTOOL, &ifr);

  printf("ret is %d\n",ret);
  if (ret == -1) {
    strncpy(driver,"IoctlFailure",len);
    strncpy(version,"IoctlFailure",len);
    strncpy(firmware,"IoctlFailure",len);
    strncpy(bus,"IoctlFailure",len);
    driver[len-1] = 0;
    version[len-1] = 0;
    firmware[len-1] = 0;
    bus[len-1] = 0;
    return;
  }
  strncpy(driver,drvinfo.driver,len);
  strncpy(version,drvinfo.version,len);
  strncpy(firmware,drvinfo.fw_version,len);
  strncpy(bus,drvinfo.bus_info,len);
  driver[len-1] = 0;
  version[len-1] = 0;
  firmware[len-1] = 0;
  bus[len-1] = 0;
  
  return;
}
