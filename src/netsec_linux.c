#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#if defined(HAVE_STRING_H)
#include <string.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dlfcn.h>

void *messiah;  /* Handel's... */

/* for the NSEC_mumble defines */
#include "netlib.h"


void
find_security_info_selinux(int *enabled, int *type, char **specific){

  int ret;
  int enforcing;

  /* at some point we should probably get these from
     selinux/selinux.h? */
  int (*getenforce)(int *);
  int (*getpolicy)(char **);

  *enabled = NSEC_UNKNOWN;
  *type    = NSEC_TYPE_SELINUX;

  getenforce = dlsym(messiah, "selinux_getenforcemode");
  if (NULL == getenforce) {
    dlclose(messiah);
    *specific = strdup("no getenforcemode");
    return;
  }

  ret = (*getenforce)(&enforcing);
#if defined(NETPERF_STANDALONE_DEBUG)
  printf("after selinux_getenforcemode() ret is %d\n",ret);
#endif

  switch(enforcing) {
  case -1: 
    *enabled = NSEC_DISABLED;
    break;
  case 0:
    *enabled = NSEC_PERMISSIVE;
    break;
  case 1:
    *enabled = NSEC_ENFORCING;
    break;
  default:
    *enabled = NSEC_UNKNOWN;
  }
    
  getpolicy = dlsym(messiah, "selinux_getpolicytype");
  if (NULL == getpolicy) {
    dlclose(messiah);
    *specific = strdup("no getpolicytype");
    return;
  }

  ret = (*getpolicy)(specific);
#if defined(NETPERF_STANDALONE_DEBUG)
  printf("after selinux_getpolicytype ret is %d\n",ret);
#endif

  return;
}

/* presently we only know about SELinux or nothing. at some point we
   probably need to learn about AppArmor and the like.  raj
   20081020 */

void
find_security_info(int *enabled, int *type, char **specific) {

  /* first, might it be selinux? */
  messiah = dlopen("libselinux.so", RTLD_LAZY);
  if (NULL != messiah) {
    dlerror();
    return find_security_info_selinux(enabled, type, specific);
  }
  else {
    *enabled = NSEC_UNKNOWN;
    *type = NSEC_TYPE_UNKNOWN;
    *specific = "unknown";
    return;
  }
}

#if defined(NETPERF_STANDALONE_DEBUG)

/* these are normally found in src/netlib.c but we put copies here for
   the nefaious popoise of standalone debugging */

char *
nsec_enabled_to_str(int enabled) {
  switch (enabled) {
  case NSEC_UNKNOWN:
    return("Unknown");
  case NSEC_DISABLED:
    return("Disabled");
  case NSEC_PERMISSIVE:
    return("Permissive");
  case NSEC_ENFORCING:
    return("Enforcing");
  default:
    return("UNKNOWN MODE");
  }
}

char * nsec_type_to_str(int type) {
  switch (type) {
  case NSEC_TYPE_UNKNOWN:
    return("Unknown");
  case NSEC_TYPE_SELINUX:
    return("SELinux");
  default:
    return("UNKNOWN TYPE");
  }
}

int
main(int argc, char *argv[]) {

  char *specific;
  int enabled;
  int type;

  find_security_info(&enabled, &type, &specific);

  printf("Security info: enabled %s (%d) type %s (0x%x) specific %s\n",
	 nsec_enabled_to_str(enabled),
	 enabled,
	 nsec_type_to_str(type),
	 type,
	 specific);

  return 0;
}
#endif
