#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

char *
find_egress_interface(struct sockaddr *source, struct sockaddr *dest) {
  return strdup("InterfaceUnavailable");

}
