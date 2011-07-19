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
