char   netcpu_sysctl_id[]="\
@(#)netcpu_sysctl.c  Version 2.4.0";

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

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#if HAVE_LIMITS_H
# include <limits.h>
# ifndef LONG_LONG_MAX
#  define LONG_LONG_MAX LLONG_MAX
# endif /* LONG_LONG_MAX */
#endif


#include <errno.h>

/* need to have some sort of check for sys/sysctl.h versus sysctl.h */
#include <sys/sysctl.h>


/* this has been liberally cut and pasted from <sys/resource.h> on
   FreeBSD. in general, this would be a bad idea, but I don't want to
   have to do a _KERNEL define to get these and that is what
   sys/resource.h seems to want. raj 2002-03-03 */
#define CP_USER         0
#define CP_NICE         1
#define CP_SYS          2
#define CP_INTR         3
#define CP_IDLE         4
#define CPUSTATES       5


#include "netsh.h"
#include "netlib.h"

static uint64_t lib_start_count[MAXCPUS];
static uint64_t lib_end_count[MAXCPUS];

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
  return SYSCTL;
}

void
get_cpu_idle(uint64_t *res)
{
  int i;
  long cp_time[CPUSTATES];
  size_t cp_time_len = sizeof(cp_time);
  if (sysctlbyname("kern.cp_time",cp_time,&cp_time_len,NULL,0) != -1) {
    for (i = 0; i < lib_num_loc_cpus; i++){
      res[i] = cp_time[CP_IDLE];
    }
  }
  return;
}

/* calibrate_sysctl  - perform the idle rate calculation using the
   sysctl call - typically on BSD */

float
calibrate_idle_rate(int iterations, int interval)
{
  long 
    firstcnt[MAXCPUS],
    secondcnt[MAXCPUS];

  long cp_time[CPUSTATES];
  size_t cp_time_len = sizeof(cp_time);

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

  if (iterations > MAXTIMES) {
    iterations = MAXTIMES;
  }
  
  local_maxrate = -1.0;


  for(i = 0; i < iterations; i++) {
    rate[i] = 0.0;
    /* get the idle counter for each processor */
    if (sysctlbyname("kern.cp_time",cp_time,&cp_time_len,NULL,0) != -1) {
      for (j = 0; j < lib_num_loc_cpus; j++) {
        firstcnt[j] = cp_time[CP_IDLE];
      }
    }
    else {
      fprintf(where,"sysctl failure errno %d\n",errno);
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

    if (sysctlbyname("kern.cp_time",cp_time,&cp_time_len,NULL,0) != -1) {
      for (j = 0; j < lib_num_loc_cpus; j++) {
        secondcnt[j] = cp_time[CP_IDLE];
        if(debug) {
          /* I know that there are situations where compilers know about */
          /* long long, but the library fucntions do not... raj 4/95 */
          fprintf(where,
                  "\tfirstcnt[%d] = 0x%8.8lx secondcnt[%d] = 0x%8.8lx\n",
                  j,
                  firstcnt[j],
                  j,
                  secondcnt[j]);
        }
        temp_rate = (secondcnt[j] >= firstcnt[j]) ? 
          (float)(secondcnt[j] - firstcnt[j] )/elapsed : 
            (float)(secondcnt[j] - firstcnt[j] + LONG_LONG_MAX)/elapsed;
        if (temp_rate > rate[i]) rate[i] = temp_rate;
        if (debug) {
          fprintf(where,"\trate[%d] = %g\n",i,rate[i]);
          fflush(where);
        }
        if (local_maxrate < rate[i]) local_maxrate = rate[i];
      }
    }
    else {
      fprintf(where,"sysctl failure; errno %d\n",errno);
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
    lib_local_cpu_util += lib_local_per_cpu_util[i];
    if (debug) {
      fprintf(where,
	      "calc_cpu_util: actual_rate on cpu %d is %g max_rate %g cpu %6.2f\n, startcount %ll endcount %ll diff %ll",
	      i,
	      actual_rate,
	      lib_local_maxrate,
	      lib_local_per_cpu_util[i],
	      lib_start_count[i],
	      lib_end_count[i],
	      diff);
    }
  }
  
  /* we want the average across all n processors */
  lib_local_cpu_util /= (float)lib_num_loc_cpus;
  
  if (debug) {
    fprintf(where,
	    "calc_cpu_util: average across CPUs is %g\n",lib_local_cpu_util);
  }

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
  get_cpu_idle(lib_start_count);
  return;
}

void
cpu_stop_internal(void)
{
  get_cpu_idle(lib_end_count);
}
