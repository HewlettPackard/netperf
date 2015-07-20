#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef NETPERF_STANDALONE_DEBUG
#include <errno.h>
#endif

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void
find_cpu_model(char **cpu_model) {
  char linebuf[256];
  char *cret;

  FILE *proccpu;

  proccpu = fopen("/proc/cpuinfo","r");

  if (NULL == proccpu) {
    *cpu_model = strdup("fopen");
    return;
  }

  do {
    cret = fgets(linebuf,256,proccpu);
    if (NULL != cret) {
      char *target;
      /* OK, so does it start with "model name" ? */
      if (strstr(linebuf,"model name") != NULL) {
	/* one for the money "model name" */
	target = strtok(linebuf,":");
	/* two for the show (the actual model name) */
	target = strtok(NULL,":");
	/* three to get ready - strip the eol */
	target[strlen(target)-1] = 0;
	/* and four to go! */
	*cpu_model = strdup(target+1);
	fclose(proccpu);
	return;
      }
    }
  } while (!feof(proccpu));
  *cpu_model = strdup("model_name");
  fclose(proccpu);
}

static int
find_cpu_freq() {
  char linebuf[256];
  char *cret;

  FILE *proccpu;

  proccpu = fopen("/proc/cpuinfo","r");

  if (NULL == proccpu) {
    return -1;
  }

  do {
    cret = fgets(linebuf,256,proccpu);
    if (NULL != cret) {
      char *target;
      /* OK, so does it start with "model name" ? */
      if (strstr(linebuf,"cpu MHz") != NULL) {
	target = strtok(linebuf,":");
	target = strtok(NULL,":");
	fclose(proccpu);
	return rint(strtod(target+1,NULL));
      }
    }
  } while (!feof(proccpu));
  fclose(proccpu);
  return -1;
}

static void
find_system_model(char **system_model) {
#if defined(HAVE_LIBSMBIOS)
#if defined(HAVE_SMBIOS_SYSTEMINFO_H)
#include <smbios/SystemInfo.h>
#else
  /* take our best shot - the interface seems simple and stable enough
     that we don't have to require the -dev package be installed */
  extern const char *SMBIOSGetSystemName();
#endif

  char *temp_model;

  /* SMBIOSGetSystemModel allocated */
  temp_model = (char *) SMBIOSGetSystemName();
  if (temp_model)
    *system_model = temp_model;
  else
    *system_model = strdup("SMBIOSGetSystemModel");

#else
  /* we do not even have the library so there isn't much to do here
     unless someone wants to teach netperf how to find and parse
     SMBIOS all by its lonesome. raj 2008-03-13 */
  *system_model = strdup("Teach Me SMBIOS");
#endif
  return;
}

void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {

  find_system_model(system_model);
  find_cpu_model(cpu_model);
  *cpu_frequency = find_cpu_freq();

}

#ifdef NETPERF_STANDALONE_DEBUG
int
main(int argc, char *argv[]) {

  char *system_model;
  char *cpu_model;
  int   frequency;

  find_system_info(&system_model,&cpu_model,&frequency);
  printf("system_model %s, cpu_model %s, frequency %d\n",
	 system_model,
	 cpu_model,
	 frequency);

  return 0;

}

#endif
