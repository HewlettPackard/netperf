#include <string.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/pstat.h>

/* tusc can be a very useful thing... */

#ifndef _SI_MACHINE_MODEL
#define _SI_MACHINE_MODEL 5
#endif

extern int sysinfo(int info, char *buffer, ssize_t len);


void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {
  char model_str[64];
  int  ret;
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
#ifdef PSP_MAX_CACHE_LEVELS
    /* we can get it "directly" but to help make things reconcile with
       what other tools/platforms support, we shouldn't do a simple
       integer divide - instead, we should do our division in floating
       point and then round */
    *cpu_frequency = rint((double)processor_info.psp_cpu_frequency / 
			  1000000.0);
#else
    /* older OSes were "known" to be on CPUs where the itick was
       1to1 here */
    *cpu_frequency = rint(((double)processor_info.psp_iticksperclktick * 
			   (double)sysconf(_SC_CLK_TCK)) / 1000000.0);
#endif 
  }
  else
    *cpu_frequency = -1;

  *cpu_model = strdup("Unknown CPU Model");
}
  
