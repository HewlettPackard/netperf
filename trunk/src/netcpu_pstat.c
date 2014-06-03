char   netcpu_pstat_id[]="\
@(#)netcpu_pstat.c (c) Copyright 2005-2012, Hewlett-Packard Company, Version 2.6.0";

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

#ifndef PSTAT_IPCINFO
# error Sorry, pstat() CPU utilization on 10.0 and later only
#endif

#include "netsh.h"
#include "netlib.h"

/* the lib_start_count and lib_end_count arrays hold the starting
   and ending values of whatever is counting when the system is
   idle. The rate at which this increments during a test is compared
   with a previous calibrarion to arrive at a CPU utilization
   percentage. raj 2005-01-26 */
static uint64_t  lib_start_count[MAXCPUS];
static uint64_t  lib_end_count[MAXCPUS];

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

static void
get_cpu_idle(uint64_t *res)
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
          overlay = (union overlay_u *)&(res[i]);
          overlay->word[0] = psp[i].psp_idlecycles.psc_hi;
          overlay->word[1] = psp[i].psp_idlecycles.psc_lo;
          if(debug) {
            fprintf(where,
                    "\tres[%d] = 0x%8.8x%8.8x\n",
                    i,
                    hi_32(&res[i]),
                    lo_32(&res[i]));
            fflush(where);
          }
        }
        free(psp);
      }
}

/* calibrate_pstat
   Loop a number of iterations, sleeping wait_time seconds each and
   count how high the idle counter gets each time. Return  the measured
   cpu rate to the calling routine.  */

float
calibrate_idle_rate(int iterations, int interval)
{

  uint64_t
    firstcnt[MAXCPUS],
    secondcnt[MAXCPUS];

  float
    elapsed,
    temp_rate,
    rate[MAXTIMES],
    local_maxrate;

  long
    sec,
    usec;

  int
    i,
    j;

  long  count;

  struct  timeval time1, time2;
  struct  timezone tz;

  struct pst_processor *psp;

  if (iterations > MAXTIMES) {
    iterations = MAXTIMES;
  }

  local_maxrate = -1.0;

  psp = (struct pst_processor *)malloc(lib_num_loc_cpus * sizeof(*psp));
  if (psp == NULL) {
    printf("malloc(%d) failed!\n", lib_num_loc_cpus * sizeof(*psp));
    exit(1);
  }

  for(i = 0; i < iterations; i++) {
    rate[i] = 0.0;
    /* get the idle sycle counter for each processor */
    if (pstat_getprocessor(psp, sizeof(*psp), lib_num_loc_cpus, 0) != -1) {
      for (j = 0; j < lib_num_loc_cpus; j++) {
        union overlay_u {
          long long full;
          long      word[2];
        } *overlay;
        overlay = (union overlay_u *)&(firstcnt[j]);
        overlay->word[0] = psp[j].psp_idlecycles.psc_hi;
        overlay->word[1] = psp[j].psp_idlecycles.psc_lo;
      }
    }
    else {
      fprintf(where,"pstat_getprocessor failure errno %d\n",errno);
      fflush(where);
      exit(1);
    }

    gettimeofday (&time1, &tz);
    sleep(interval);
    gettimeofday (&time2, &tz);

    if (time2.tv_usec < time1.tv_usec)
      {
        time2.tv_usec += 1000000;
        time2.tv_sec -=1;
      }
    sec = time2.tv_sec - time1.tv_sec;
    usec = time2.tv_usec - time1.tv_usec;
    elapsed = (float)sec + ((float)usec/(float)1000000.0);

    if(debug) {
      fprintf(where, "Calibration for counter run: %d\n",i);
      fprintf(where,"\tsec = %ld usec = %ld\n",sec,usec);
      fprintf(where,"\telapsed time = %g\n",elapsed);
    }

    if (pstat_getprocessor(psp, sizeof(*psp), lib_num_loc_cpus, 0) != -1) {
      for (j = 0; j < lib_num_loc_cpus; j++) {
        union overlay_u {
          long long full;
          long      word[2];
        } *overlay;
        overlay = (union overlay_u *)&(secondcnt[j]);
        overlay->word[0] = psp[j].psp_idlecycles.psc_hi;
        overlay->word[1] = psp[j].psp_idlecycles.psc_lo;
        if(debug) {
          /* I know that there are situations where compilers know about */
          /* long long, but the library fucntions do not... raj 4/95 */
          fprintf(where,
                  "\tfirstcnt[%d] = 0x%8.8x%8.8x secondcnt[%d] = 0x%8.8x%8.8x\n",
                  j,
                  hi_32(&firstcnt[j]),
                  lo_32(&firstcnt[j]),
                  j,
                  hi_32(&secondcnt[j]),
                  lo_32(&secondcnt[j]));
        }
        temp_rate = (secondcnt[j] >= firstcnt[j]) ?
          (float)(secondcnt[j] - firstcnt[j] )/elapsed :
            (float)(secondcnt[j] - firstcnt[j] + LONG_LONG_MAX)/elapsed;
        if (temp_rate > rate[i]) rate[i] = temp_rate;
        if(debug) {
          fprintf(where,"\trate[%d] = %g\n",i,rate[i]);
          fflush(where);
        }
        if (local_maxrate < rate[i]) local_maxrate = rate[i];
      }
    }
    else {
      fprintf(where,"pstat failure; errno %d\n",errno);
      fflush(where);
      exit(1);
    }
  }
  if(debug) {
    fprintf(where,"\tlocal maxrate = %g per sec. \n",local_maxrate);
    fflush(where);
  }
  return local_maxrate;

}

float
calc_cpu_util_internal(float elapsed_time)
{
  int i;

  float actual_rate;
  float correction_factor;

  memset(&lib_local_cpu_stats, 0, sizeof(lib_local_cpu_stats));

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

    /* we assume that the two are not more than a long apart. I */
    /* know that this is bad, but trying to go from long longs to */
    /* a float (perhaps a double) is boggling my mind right now. */
    /* raj 4/95 */

    long long
      diff;

    if (lib_end_count[i] >= lib_start_count[i]) {
      diff = lib_end_count[i] - lib_start_count[i];
    }
    else {
      diff = lib_end_count[i] - lib_start_count[i] + LONG_LONG_MAX;
    }
    actual_rate = (float) diff / lib_elapsed;
    lib_local_per_cpu_util[i] = (lib_local_maxrate - actual_rate) /
      lib_local_maxrate * 100;
    lib_local_per_cpu_util[i] *= correction_factor;
    lib_local_cpu_stats.cpu_util += lib_local_per_cpu_util[i];
    if (debug) {
      fprintf(where,
	      "calc_cpu_util: actual_rate on cpu %d is %g max_rate %g cpu %6.2f cf %f\n",
	      i,
	      actual_rate,
	      lib_local_maxrate,
	      lib_local_per_cpu_util[i],
	      correction_factor);
    }
  }

  /* we want the average across all n processors */
  lib_local_cpu_stats.cpu_util /= (float)lib_num_loc_cpus;

  if (debug) {
    fprintf(where,
	    "calc_cpu_util: average across CPUs is %g\n",
            lib_local_cpu_stats.cpu_util);
  }

  return lib_local_cpu_stats.cpu_util;

}
void
cpu_start_internal(void)
{
  get_cpu_idle(lib_start_count);
  return;
}

void
cpu_stop_internal(void)
{
  get_cpu_idle(lib_end_count);
}
