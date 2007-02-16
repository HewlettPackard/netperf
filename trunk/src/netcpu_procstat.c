char   netcpu_procstat_id[]="\
@(#)netcpu_procstat.c (c) Copyright 2005-2007 Version 2.4.3";

/* netcpu_procstat.c
  
   Implement the /proc/stat specific portions of netperf CPU
   utilization measurements. These are broken-out into a separate file
   to make life much nicer over in netlib.c which had become a maze of
   twisty, CPU-util-related, #ifdefs, all different.  raj 2005-01-26
   */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#if STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif

#include <string.h>

#include "netsh.h"
#include "netlib.h"

/* the lib_start_count and lib_end_count arrays hold the starting
   and ending values of whatever is counting when the system is
   idle. The rate at which this increments during a test is compared
   with a previous calibrarion to arrive at a CPU utilization
   percentage. raj 2005-01-26 */
static uint64_t  lib_start_count[MAXCPUS];
static uint64_t  lib_end_count[MAXCPUS];


/* The max. length of one line of /proc/stat cpu output */
#define CPU_LINE_LENGTH ((8 * sizeof (long) / 3 + 1) * 4 + 8)
#define PROC_STAT_FILE_NAME "/proc/stat"
#define N_CPU_LINES(nr) (nr == 1 ? 1 : 1 + nr)

static int proc_stat_fd = -1;
static char *proc_stat_buf = NULL;
static int proc_stat_buflen = 0;

void
cpu_util_init(void) 
{

  if (debug) {
    fprintf(where,
	    "cpu_util_init enter, proc_stat_fd %d proc_stat_buf %p\n",
	    proc_stat_fd,
	    proc_stat_buf);
    fflush(where);
  }
  if (proc_stat_fd < 0) {
    proc_stat_fd = open (PROC_STAT_FILE_NAME, O_RDONLY, NULL);
    if (proc_stat_fd < 0) {
      fprintf (stderr, "Cannot open %s!\n", PROC_STAT_FILE_NAME);
      exit (1);
    };
  };

  if (!proc_stat_buf) {
    proc_stat_buflen = N_CPU_LINES (lib_num_loc_cpus) * CPU_LINE_LENGTH;
    if (debug) {
      fprintf(where,
	      "lib_num_loc_cpus %d lines %d CPU_LINE_LENGTH %d proc_stat_buflen %d\n",
	      lib_num_loc_cpus,
	      N_CPU_LINES(lib_num_loc_cpus),
	      CPU_LINE_LENGTH,
	      proc_stat_buflen);
      fflush(where);
    }
    proc_stat_buf = (char *)malloc (proc_stat_buflen);
    if (!proc_stat_buf) {
      fprintf (stderr, "Cannot allocate buffer memory!\n");
      exit (1);
    }
  }
  return;
}

void
cpu_util_terminate(void)
{
  close(proc_stat_fd);
  proc_stat_fd = -1;
  free(proc_stat_buf);
  proc_stat_buf = NULL;
  return;
}

int
get_cpu_method()
{
  return PROC_STAT;
}

float
calibrate_idle_rate (int iterations, int interval)
{
  if (proc_stat_fd < 0) {
    proc_stat_fd = open (PROC_STAT_FILE_NAME, O_RDONLY, NULL);
    if (proc_stat_fd < 0) {
      fprintf (stderr, "Cannot open %s!\n", PROC_STAT_FILE_NAME);
      exit (1);
    };
  };

  if (!proc_stat_buf) {
    proc_stat_buflen = N_CPU_LINES (lib_num_loc_cpus) * CPU_LINE_LENGTH;
    if (debug) {
      fprintf(where,
	      "calibrate: lib_num_loc_cpus %d lines %d CPU_LINE_LENGTH %d proc_stat_buflen %d\n",
	      lib_num_loc_cpus,
	      N_CPU_LINES(lib_num_loc_cpus),
	      CPU_LINE_LENGTH,
	      proc_stat_buflen);
      fflush(where);
    }
    proc_stat_buf = (char *)malloc (proc_stat_buflen);
    if (!proc_stat_buf) {
      fprintf (stderr, "Cannot allocate buffer memory!\n");
      exit (1);
    };
  };

  return sysconf (_SC_CLK_TCK);
}

void
get_cpu_idle (uint64_t *res)
{
  int space;
  int i;
  int n = lib_num_loc_cpus;
  char *p = proc_stat_buf;

  lseek (proc_stat_fd, 0, SEEK_SET);
  read (proc_stat_fd, p, proc_stat_buflen);

  if (debug) {
    fprintf(where,"proc_stat_buf '%.*s'\n",proc_stat_buflen,p);
    fflush(where);
  }
  /* Skip first line (total) on SMP */
  if (n > 1) p = strchr (p, '\n');

  /* Idle time is the 4th space-separated token */
  for (i = 0; i < n; i++) {
    for (space = 0; space < 4; space ++) {
      p = strchr (p, ' ');
      while (*++p == ' ');
    };
    res[i] = strtoul (p, &p, 10);
    if (debug) {
      fprintf(where,"res[%d] is %llu\n",i,res[i]);
      fflush(where);
    }
    p = strchr (p, '\n');
  };

}

/* take the initial timestamp and start collecting CPU utilization if
   requested */

void
measure_cpu_start()
{
  cpu_method = PROC_STAT;
  get_cpu_idle(lib_start_count);
}

/* collect final CPU utilization raw data */
void
measure_cpu_stop()
{
  get_cpu_idle(lib_end_count);
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

  for (i = 0; i < lib_num_loc_cpus; i++) {

    /* it would appear that on some systems, in loopback, nice is
     *very* effective, causing the looper process to stop dead in its
     tracks. if this happens, we need to ensure that the calculation
     does not go south. raj 6/95 and if we run completely out of idle,
     the same thing could in theory happen to the USE_KSTAT path. raj
     8/2000 */ 
    
    if (lib_end_count[i] == lib_start_count[i]) {
      lib_end_count[i]++;
    }
    
    actual_rate = (lib_end_count[i] > lib_start_count[i]) ?
      (float)(lib_end_count[i] - lib_start_count[i])/lib_elapsed :
      (float)(lib_end_count[i] - lib_start_count[i] +
	      MAXLONG)/ lib_elapsed;
    lib_local_per_cpu_util[i] = (lib_local_maxrate - actual_rate) /
      lib_local_maxrate * 100;
    if (debug) {
      fprintf(where,
              "calc_cpu_util: actual_rate on processor %d is %f start %llx end %llx util %f\n",
              i,
              actual_rate,
              lib_start_count[i],
              lib_end_count[i],
	      lib_local_per_cpu_util[i]);
    }
    lib_local_cpu_util += lib_local_per_cpu_util[i];
  }
  /* we want the average across all n processors */
  lib_local_cpu_util /= (float)lib_num_loc_cpus;
  
  lib_local_cpu_util *= correction_factor;
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
