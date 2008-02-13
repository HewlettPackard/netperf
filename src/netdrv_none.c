#include <string.h>

void
find_driver_info(char *ifname, char *driver, char *version, char *firmware, char *bus, int len) {

    strncpy(driver,"Unavailable",len);
    strncpy(version,"Unavailable",len);
    strncpy(firmware,"Unavailable",len);
    strncpy(bus,"Unavailable",len);
    driver[len-1] = 0;
    version[len-1] = 0;
    firmware[len-1] = 0;
    bus[len-1] = 0;
    return;
}
