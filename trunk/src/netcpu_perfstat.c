char   netcpu_perfstat_id[]="\
@(#)netcpu_perfstat.c Version 2.6.0";

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

#include "netsh.h"
#include "netlib.h"

/* the lib_start_count and lib_end_count arrays hold the starting
   and ending values of whatever is counting when the system is
   idle. The rate at which this increments during a test is compared
   with a previous calibration to arrive at a CPU utilization
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
  return PERFSTAT;
}

void
get_cpu_idle(uint64_t *res)
{
  perfstat_cpu_t *perfstat_buffer;
  perfstat_cpu_t *per_cpu_pointer;
  perfstat_id_t  name;
  int i,ret;

  /* a name of "" will cause us to start from the beginning */
  strcpy(name.name,"");
  perfstat_buffer = (perfstat_cpu_t *)malloc(lib_num_loc_cpus *
					     sizeof(perfstat_cpu_t));
  if (perfstat_buffer == NULL) {
    fprintf(where,
	    "cpu_start: malloc failed errno %d\n",
	    errno);
    fflush(where);
    exit(-1);
  }

  /* happiness and joy, keep going */
  ret = perfstat_cpu(&name,
		     perfstat_buffer,
		     sizeof(perfstat_cpu_t),
		     lib_num_loc_cpus);

  if ((ret == -1) ||
      (ret != lib_num_loc_cpus)) {
    fprintf(where,
	    "cpu_start: perfstat_cpu failed/count off; errno %d cpus %d count %d\n",
	    errno,
	    lib_num_loc_cpus,
	    ret);
    fflush(where);
    exit(-1);
  }

  per_cpu_pointer = perfstat_buffer;
  for (i = 0; i < lib_num_loc_cpus; i++){
    res[i] = per_cpu_pointer->idle;
    per_cpu_pointer++;
  }
  free(perfstat_buffer);

  return;
}

float
calibrate_idle_rate(int iterations, int interval)
{
  unsigned long long
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

  struct  timeval time1, time2 ;
  struct  timezone tz;

  perfstat_cpu_t  *perfstat_buffer;
  perfstat_cpu_t  *per_cpu_pointer;
  perfstat_id_t   name;
  int ret;

  if (debug) {
    fprintf(where,"enter calibrate_perfstat\n");
    fflush(where);
  }

  if (iterations > MAXTIMES) {
    iterations = MAXTIMES;
  }

  local_maxrate = (float)-1.0;

  perfstat_buffer = (perfstat_cpu_t *)malloc(lib_num_loc_cpus *
                                             sizeof(perfstat_cpu_t));
  if (perfstat_buffer == NULL) {
    fprintf(where,
            "calibrate_perfstat: malloc failed errno %d\n",
            errno);
    fflush(where);
    exit(-1);
  }

  for(i = 0; i < iterations; i++) {
    rate[i] = (float)0.0;
    /* a name of "" will cause us to start from the beginning */
    strcpy(name.name,"");

    /* happiness and joy, keep going */
    ret = perfstat_cpu(&name,
                       perfstat_buffer,
                       sizeof(perfstat_cpu_t),
                       lib_num_loc_cpus);

    if ((ret == -1) ||
        (ret != lib_num_loc_cpus)) {
      fprintf(where,
              "calibrate_perfstat: perfstat_cpu failed/count off; errno %d cpus %d count %d\n",
              errno,
              lib_num_loc_cpus,
              ret);
      fflush(where);
      exit(-1);
    }

    per_cpu_pointer = perfstat_buffer;
    for (j = 0; j < lib_num_loc_cpus; j++) {
      firstcnt[j] = per_cpu_pointer->idle;
      per_cpu_pointer++;
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

    /* happiness and joy, keep going */
    ret = perfstat_cpu(&name,
                       perfstat_buffer,
                       sizeof(perfstat_cpu_t),
                       lib_num_loc_cpus);

    if ((ret == -1) ||
        (ret != lib_num_loc_cpus)) {
      fprintf(where,
              "calibrate_perfstat: perfstat_cpu failed/count off; errno %d cpus %d count %d\n",
              errno,
              lib_num_loc_cpus,
              ret);
      fflush(where);
      exit(-1);
    }

    per_cpu_pointer = perfstat_buffer;

    if(debug) {
      fprintf(where,
	      "Calibration for perfstat counter run: %d\n"
	      "\tsec = %ld usec = %ld\n"
	      "\telapsed time = %g\n",
	      i,
	      sec,usec,
	      elapsed);
    }

    for (j = 0; j < lib_num_loc_cpus; j++) {
      secondcnt[j] = per_cpu_pointer->idle;
      per_cpu_pointer++;
      if(debug) {
        /* I know that there are situations where compilers know about
           long long, but the library functions do not... raj 4/95 */
        fprintf(where,
                "\tfirstcnt[%d] = 0x%8.8lx%8.8lx secondcnt[%d] = 0x%8.8lx%8.8lx\n",
                j,
                firstcnt[j],
                firstcnt[j],
                j,
                secondcnt[j],
                secondcnt[j]);
      }
      /* we assume that it would wrap no more than once. we also
	 assume that the result of subtracting will "fit" raj 4/95 */
      temp_rate = (secondcnt[j] >= firstcnt[j]) ?
        (float)(secondcnt[j] - firstcnt[j])/elapsed :
          (float)(secondcnt[j]-firstcnt[j]+MAXLONG)/elapsed;
      if (temp_rate > rate[i]) rate[i] = temp_rate;
      if(debug) {
        fprintf(where,"\trate[%d] = %g\n",i,rate[i]);
        fflush(where);
      }
      if (local_maxrate < rate[i]) local_maxrate = rate[i];
    }
  }
  if(debug) {
    fprintf(where,"\tlocal maxrate = %g per sec. \n",local_maxrate);
    fflush(where);
  }
  free(perfstat_buffer);
  return local_maxrate;
}

float
calc_cpu_util_internal(float elapsed_time)
{
  int i;

  float actual_rate;
  float correction_factor;

  lib_local_cpu_util = (float)0.0;
  /* It is possible that the library measured a time other than the
     one that the user want for the cpu utilization calculations - for
     example, tests that were ended by watchdog timers such as the udp
     stream test. We let these tests tell up what the elapsed time
     should be. */

  if (elapsed_time != 0.0) {
    correction_factor = (float) 1.0 +
      ((lib_elapsed - elapsed_time) / elapsed_time);
  }
  else {
    correction_factor = (float) 1.0;
  }

  /* this looks just like the looper case. at least I think it should
     :) raj 4/95 */
  for (i = 0; i < lib_num_loc_cpus; i++) {

    /* we assume that the two are not more than a long apart. I know
       that this is bad, but trying to go from long longs to a float
       (perhaps a double) is boggling my mind right now.  raj 4/95 */

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
              "calc_cpu_util: actual_rate on cpu %d is %g max_rate %g cpu %6.2f\n",
              i,
              actual_rate,
              lib_local_maxrate,
              lib_local_per_cpu_util[i]);
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

