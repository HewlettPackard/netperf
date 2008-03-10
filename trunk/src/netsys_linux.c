#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void
find_cpu_model(char **cpu_model) {
  char linebuf[256];
  char *cret;
  int  ret;
  int  c;

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
	return;
      }
    }
  } while (!feof(proccpu));
  *cpu_model = strdup("model_name");
}

static int
find_cpu_freq() {
  char linebuf[256];
  char *cret;
  int  ret;
  int  c;

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
	return rint(strtod(target+1,NULL));
      }
    }
  } while (!feof(proccpu));
  return -1;
}

void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {
  int ret;

  *system_model = strdup("Teach Me DMI");
  find_cpu_model(cpu_model);
  *cpu_frequency = find_cpu_freq();

}
