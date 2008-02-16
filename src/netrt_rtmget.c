#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>

char     *
find_egress_interface(struct sockaddr *source, struct sockaddr *dest) {
  return strdup("NotImplemented");
}
