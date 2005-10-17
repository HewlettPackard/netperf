char   netcpu_pstatnew_id[]="\
@(#)netcpu_pstatnew.c (c) Copyright 2005, Hewlett-Packard Company, Version 2.4.0";

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#endif 

#include <sys/dk.h>
#include <sys/pstat.h>

/* HP-UX 11.23 seems to have added three other cycle counters to the
   original psp_idlecycles - one for user, one for kernel and one for
   interrupt. so, we can now use those to calculate CPU utilization
   without requiring any calibration phase.  raj 2005-02-16 */ 

#ifndef PSTAT_IPCINFO
# error Sorry, pstat() CPU utilization on 10.0 and later only
#endif

typedef struct cpu_time_counters {
  uint64_t idle;
  uint64_t user;
  uint64_t kernel;
  uint64_t interrupt;
} cpu_time_counters_t;

#include "netsh.h"
#include "netlib.h"

/* the lib_start_count and lib_end_count arrays hold the starting
   and ending values of whatever is counting when the system is
   idle. The rate at which this increments during a test is compared
   with a previous calibrarion to arrive at a CPU utilization
   percentage. raj 2005-01-26 */

static cpu_time_counters_t  starting_cpu_counters[MAXCPUS];
static cpu_time_counters_t  ending_cpu_counters[MAXCPUS];
static cpu_time_counters_t  delta_cpu_counters[MAXCPUS];

void
cpu_util_init(void) 
{
  return;
}

void
cpu_util_terminate(void)
{
  return;
}

int
get_cpu_method(void)
{
  return HP_IDLE_COUNTER;
}

void
get_cpu_counters(cpu_time_counters_t *res)
{
      /* get the idle sycle counter for each processor */
      struct pst_processor *psp;
      union overlay_u {
        long long full;
        long      word[2];
      } *overlay;
      
      psp = (struct pst_processor *)malloc(lib_num_loc_cpus * sizeof(*psp));
      if (psp == NULL) {
        printf("malloc(%d) failed!\n", lib_num_loc_cpus * sizeof(*psp));
        exit(1);
	  }
      if (pstat_getprocessor(psp, sizeof(*psp), lib_num_loc_cpus, 0) != -1) {
        int i;
        for (i = 0; i < lib_num_loc_cpus; i++) {
          overlay = (union overlay_u *)&(res[i].idle);
          overlay->word[0] = psp[i].psp_idlecycles.psc_hi;
          overlay->word[1] = psp[i].psp_idlecycles.psc_lo;
          if(debug) {
            fprintf(where,
                    "\tidle[%d] = 0x%8.8x%8.8x ",
                    i,
                    hi_32(&res[i]),
                    lo_32(&res[i]));
            fflush(where);
          }
          overlay = (union overlay_u *)&(res[i].user);
          overlay->word[0] = psp[i].psp_usercycles.psc_hi;
          overlay->word[1] = psp[i].psp_usercycles.psc_lo;
          if(debug) {
            fprintf(where,
                    "user[%d] = 0x%8.8x%8.8x ",
                    i,
                    hi_32(&res[i]),
                    lo_32(&res[i]));
            fflush(where);
          }
          overlay = (union overlay_u *)&(res[i].kernel);
          overlay->word[0] = psp[i].psp_systemcycles.psc_hi;
          overlay->word[1] = psp[i].psp_systemcycles.psc_lo;
          if(debug) {
            fprintf(where,
                    "kern[%d] = 0x%8.8x%8.8x ",
                    i,
                    hi_32(&res[i]),
                    lo_32(&res[i]));
            fflush(where);
          }
          overlay = (union overlay_u *)&(res[i].interrupt);
          overlay->word[0] = psp[i].psp_interruptcycles.psc_hi;
          overlay->word[1] = psp[i].psp_interruptcycles.psc_lo;
          if(debug) {
            fprintf(where,
                    "intr[%d] = 0x%8.8x%8.8x\n",
                    i,
                    hi_32(&res[i]),
                    lo_32(&res[i]));
            fflush(where);
          }
        }
        free(psp);
      }
}

/* calibrate_pstatnew
   there really isn't anything much to do here since we have all the
   counters and use their ratios for CPU util measurement. raj
   2005-02-16 */

float
calibrate_idle_rate(int iterations, int interval)
{
  return 0.0;
}

static void
print_cpu_time_counters(char *name, int instance, cpu_time_counters_t *counters) 
{
  fprintf(where,"%s[%d]:\n",name,instance);
  fprintf(where,
	  "\t idle %llu\n",counters[instance].idle);
  fprintf(where,
	  "\t user %llu\n",counters[instance].user);
  fprintf(where,
	  "\t kernel %llu\n",counters[instance].kernel);
  fprintf(where,
	  "\t interrupt %llu\n",counters[instance].interrupt);
}

float
calc_cpu_util_internal(float elapsed_time)
{
  int i;


#define CALC_PERCENT 100
#define CALC_TENTH_PERCENT 1000
#define CALC_HUNDREDTH_PERCENT 10000
#define CALC_THOUSANDTH_PERCENT 100000
#define CALC_ACCURACY CALC_THOUSANDTH_PERCENT

  uint64_t total_cpu_nsec;
  uint64_t fraction_idle;
  uint64_t fraction_user;
  uint64_t fraction_kernel;
  uint64_t fraction_interrupt;

  float actual_rate;
  float correction_factor;

  lib_local_cpu_util = (float)0.0;

  /* It is possible that the library measured a time other than */
  /* the one that the user want for the cpu utilization */
  /* calculations - for example, tests that were ended by */
  /* watchdog timers such as the udp stream test. We let these */
  /* tests tell up what the elapsed time should be. */
  
  if (elapsed_time != 0.0) {
    correction_factor = (float) 1.0 + 
      ((lib_elapsed - elapsed_time) / elapsed_time);
  }
  else {
    correction_factor = (float) 1.0;
  }

  /* this looks just like the looper case. at least I think it */
  /* should :) raj 4/95 */
  for (i = 0; i < lib_num_loc_cpus; i++) {
    
    /* we ass-u-me that these counters will never wrap during a
       netperf run.  this may not be a particularly safe thing to
       do. raj 2005-01-28 */
    delta_cpu_counters[i].idle = ending_cpu_counters[i].idle -
      starting_cpu_counters[i].idle;
    delta_cpu_counters[i].user = ending_cpu_counters[i].user -
      starting_cpu_counters[i].user;
    delta_cpu_counters[i].kernel = ending_cpu_counters[i].kernel -
      starting_cpu_counters[i].kernel;
    delta_cpu_counters[i].interrupt = ending_cpu_counters[i].interrupt -
      starting_cpu_counters[i].interrupt;
    
    if (debug) {
      print_cpu_time_counters("delta_cpu_counters",i,delta_cpu_counters);
    }

    /* now get the sum, which we ass-u-me does not overflow a 64-bit
       counter. raj 2005-02-16 */
    total_cpu_nsec = 
      delta_cpu_counters[i].idle +
      delta_cpu_counters[i].user +
      delta_cpu_counters[i].kernel +
      delta_cpu_counters[i].interrupt;

    if (debug) {
      fprintf(where,"total_cpu_nsec %llu\n",total_cpu_nsec);
    }

    /* since HP-UX 11.23 does the _RIGHT_ thing and idle/user/kernel
       does _NOT_ overlap with interrupt, we do not have to apply any
       correction kludge. raj 2005-02-16 */

    /* and now some fun with integer math.  i initially tried to
       promote things to long doubled but that didn't seem to result
       in happiness and joy. raj 2005-01-28 */

    /* multiply by 100 and divide by total and you get whole
       percentages. multiply by 1000 and divide by total and you get
       tenths of percentages.  multiply by 10000 and divide by total
       and you get hundredths of percentages. etc etc etc raj
       2005-01-28 */
    fraction_idle = 
      (delta_cpu_counters[i].idle * CALC_ACCURACY) / total_cpu_nsec;

    fraction_user = 
      (delta_cpu_counters[i].user * CALC_ACCURACY) / total_cpu_nsec;

    fraction_kernel = 
      (delta_cpu_counters[i].kernel * CALC_ACCURACY) / total_cpu_nsec;

    fraction_interrupt = 
      (delta_cpu_counters[i].interrupt * CALC_ACCURACY) / total_cpu_nsec;

    if (debug) {
      fprintf(where,"\tfraction_idle %lu\n",fraction_idle);
      fprintf(where,"\tfraction_user %lu\n",fraction_user);
      fprintf(where,"\tfraction_kernel %lu\n",fraction_kernel);
      fprintf(where,"\tfraction_interrupt %lu\n",fraction_interrupt);
    }

    /* and finally, what is our CPU utilization? */
    lib_local_per_cpu_util[i] = 100.0 - (((float)fraction_idle / 
					  (float)CALC_ACCURACY) * 100.0);
    if (debug) {
      fprintf(where,
	      "lib_local_per_cpu_util[%d] %g\n",
	      i,
	      lib_local_per_cpu_util[i]);
    }
    lib_local_cpu_util += lib_local_per_cpu_util[i];
  }
  /* we want the average across all n processors */
  lib_local_cpu_util /= (float)lib_num_loc_cpus;

  lib_local_cpu_util *= correction_factor;

  if (debug) {
    fprintf(where,
	    "calc_cpu_util: returning %g\n",lib_local_cpu_util);
  }

  return lib_local_cpu_util;

}
void
cpu_start_internal(void)
{
  get_cpu_counters(starting_cpu_counters);
}

void
cpu_stop_internal(void)
{
  get_cpu_counters(ending_cpu_counters);
}
