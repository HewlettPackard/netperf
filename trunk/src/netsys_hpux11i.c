#include <string.h>
#include <unistd.h>
#include <string.h>
#include <sys/pstat.h>

/* tusc can be a very useful thing... */

#ifndef _SI_MACHINE_MODEL
#define _SI_MACHINE_MODEL 5
#endif

extern int sysinfo(int info, char *buffer, int len);


void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {
  char model_str[64];
  int  ret;
  printf("calling find_system_info\n");
  struct pst_processor processor_info;

  /* first the system model name */
  ret = sysinfo(_SI_MACHINE_MODEL,model_str,64);
  model_str[63] = 0;
  *system_model = strdup(model_str);

  /* now lets try to find processor frequency. we will for now
     ass-u-me that an index of zero will always get us something,
     which may not actually be the case but lets see how long it takes
     to be noticed :) raj 2008-03-07 */
  ret = pstat_getprocessor(&processor_info,
			   sizeof(processor_info),
			   1, /* one processor, one processor only please */
			   0);

  if (ret > 0) {
#ifdef PAP_MAX_CACHE_LEVELS
    /* we can get it "directly" */
    *cpu_frequency = processor_info.psp_cpu_frequency / 1000000;
#else
    /* older OSes were "known" to be on CPUs where the itick was
       1to1 here */
    *cpu_frequency = 
      (processor_info.psp_iticksperclktick * sysconf(_SC_CLK_TCK)) / 1000000;
#endif 
  }
  else
    *cpu_frequency = -1;

  *cpu_model = strdup("Unknown CPU Model");
}
  
