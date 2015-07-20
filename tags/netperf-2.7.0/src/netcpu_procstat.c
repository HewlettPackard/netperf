char   netcpu_procstat_id[]="\
@(#)netcpu_procstat.c (c) Copyright 2005-2012 Version 2.6.0";

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

#define IDLE_IDX 4
#define CPU_STATES 10

typedef struct cpu_states
{
  uint64_t     	user;
  uint64_t     	nice;
  uint64_t     	sys;
  uint64_t     	idle;
  uint64_t     	iowait;
  uint64_t     	hard_irq;
  uint64_t     	soft_irq;
  uint64_t     	steal;
  uint64_t     	guest;
  uint64_t     	guest_nice;
} cpu_states_t;

static cpu_states_t  lib_start_count[MAXCPUS];
static cpu_states_t  lib_end_count[MAXCPUS];


/* The max. length of one line of /proc/stat cpu output */
#define CPU_LINE_LENGTH (int)((CPU_STATES * sizeof (long) / 3 + 1) * 4 + 8)
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

static void
get_cpu (cpu_states_t *res)
{
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

  for (i = 0; i < n; i++) {
    memset(&res[i], 0, sizeof (res[i]));
    p = strchr (p, ' ');
    sscanf(p, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
	   (unsigned long long *)&res[i].user,
	   (unsigned long long *)&res[i].nice,
	   (unsigned long long *)&res[i].sys,
	   (unsigned long long *)&res[i].idle,
	   (unsigned long long *)&res[i].iowait,
	   (unsigned long long *)&res[i].hard_irq,
	   (unsigned long long *)&res[i].soft_irq,
	   (unsigned long long *)&res[i].steal,
	   (unsigned long long *)&res[i].guest,
           (unsigned long long *)&res[i].guest_nice);
    if (debug) {
      fprintf(where,
	      "res[%d] is %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
	      i,
	      (unsigned long long)res[i].user,
	      (unsigned long long)res[i].nice,
	      (unsigned long long)res[i].sys,
	      (unsigned long long)res[i].idle,
	      (unsigned long long)res[i].iowait,
	      (unsigned long long)res[i].hard_irq,
	      (unsigned long long)res[i].soft_irq,
	      (unsigned long long)res[i].steal,
	      (unsigned long long)res[i].guest,
              (unsigned long long)res[i].guest_nice);
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
  get_cpu(lib_start_count);
}

/* collect final CPU utilization raw data */
void
measure_cpu_stop()
{
  get_cpu(lib_end_count);
}

static uint64_t
tick_subtract(uint64_t start, uint64_t end)
{
  if (end >= start || (start & 0xffffffff00000000ULL))
    return (end - start);

  /*
   *  We wrapped, and it is likely that the kernel is suppling 32-bit
   *  counters, because "start" is less than 32-bits wide.  If that's
   *  the case, then handle the wrap by subtracting off everything but
   *  the lower 32-bits so as to get back to unsigned 32-bit
   *  arithmetic.
   */
  return (end - start +  0xffffffff00000000ULL);
}

float
calc_cpu_util_internal(float elapsed_time)
{
  int i;

  float correction_factor;
  cpu_states_t diff;
  uint64_t total_ticks;

  memset(&lib_local_cpu_stats, 0, sizeof(lib_local_cpu_stats));

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

  if (debug) {
    fprintf(where,
	    "lib_local_maxrate = %f\n", lib_local_maxrate);
  }
  for (i = 0; i < lib_num_loc_cpus; i++) {

    /* Find the difference in all CPU stat fields */
    diff.user =
      tick_subtract(lib_start_count[i].user, lib_end_count[i].user);
    diff.nice =
      tick_subtract(lib_start_count[i].nice, lib_end_count[i].nice);
    diff.sys =
      tick_subtract(lib_start_count[i].sys, lib_end_count[i].sys);
    diff.idle =
      tick_subtract(lib_start_count[i].idle, lib_end_count[i].idle);
    diff.iowait =
      tick_subtract(lib_start_count[i].iowait, lib_end_count[i].iowait);
    diff.hard_irq =
      tick_subtract(lib_start_count[i].hard_irq, lib_end_count[i].hard_irq);
    diff.soft_irq =
      tick_subtract(lib_start_count[i].soft_irq, lib_end_count[i].soft_irq);
    diff.steal =
      tick_subtract(lib_start_count[i].steal, lib_end_count[i].steal);
    diff.guest =
      tick_subtract(lib_start_count[i].guest, lib_end_count[i].guest);
    diff.guest_nice =
      tick_subtract(lib_start_count[i].guest_nice, lib_end_count[i].guest_nice);
    total_ticks = diff.user + diff.nice + diff.sys + diff.idle + diff.iowait
      + diff.hard_irq + diff.soft_irq + diff.steal
      + diff.guest + diff.guest_nice;

    /* calculate idle time as a percentage of all CPU states */
    if (total_ticks == 0) {
      if (debug) {
	fprintf(where, "Total ticks 0 on CPU %d, charging nothing!\n", i);
      }
      lib_local_per_cpu_util[i] = 0.0;
    } else {
#define CPU_STAT_PERCENTIZE(x) (100. * (((float)(x)) / ((float)(total_ticks))))
      /* utilization = 100% - %idle */
      lib_local_per_cpu_util[i] = 100. - CPU_STAT_PERCENTIZE(diff.idle);
      lib_local_cpu_stats.cpu_util += lib_local_per_cpu_util[i];
      lib_local_cpu_stats.cpu_user += CPU_STAT_PERCENTIZE(diff.user);
      lib_local_cpu_stats.cpu_system += CPU_STAT_PERCENTIZE(diff.sys);
      lib_local_cpu_stats.cpu_iowait += CPU_STAT_PERCENTIZE(diff.iowait);
      lib_local_cpu_stats.cpu_irq += CPU_STAT_PERCENTIZE(diff.hard_irq);
      lib_local_cpu_stats.cpu_swintr += CPU_STAT_PERCENTIZE(diff.soft_irq);
    }
    /* apply correction factor */
    lib_local_per_cpu_util[i] *= correction_factor;
    if (debug) {
      fprintf(where,
              "calc_cpu_util: util on processor %d, diff = %llu %llu %llu "
	      "%llu %llu %llu %llu %llu %llu util %f cf %f\n",
              i,
	      (unsigned long long)diff.user,
	      (unsigned long long)diff.nice,
	      (unsigned long long)diff.sys,
	      (unsigned long long)diff.idle,
	      (unsigned long long)diff.iowait,
	      (unsigned long long)diff.hard_irq,
	      (unsigned long long)diff.soft_irq,
	      (unsigned long long)diff.steal,
	      (unsigned long long)diff.guest,
	      lib_local_per_cpu_util[i],
	      correction_factor);
    }
  }

  /* we want to apply correction factor and average across all n processors */
#define CPU_STAT_FIXUP(fldname)                                         \
  lib_local_cpu_stats.fldname = ((correction_factor                     \
                                  * lib_local_cpu_stats.fldname)        \
                                 / ((float)lib_num_loc_cpus))

  CPU_STAT_FIXUP(cpu_util);
  CPU_STAT_FIXUP(cpu_user);
  CPU_STAT_FIXUP(cpu_system);
  CPU_STAT_FIXUP(cpu_iowait);
  CPU_STAT_FIXUP(cpu_irq);
  CPU_STAT_FIXUP(cpu_swintr);

  return lib_local_cpu_stats.cpu_util;
}

void
cpu_start_internal(void)
{
  get_cpu(lib_start_count);
  return;
}

void
cpu_stop_internal(void)
{
  get_cpu(lib_end_count);
}
