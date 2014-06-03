char   netcpu_kstat_id[]="\
@(#)netcpu_kstat.c  Version 2.6.0";

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

#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#if HAVE_STRINGS_H
# include <strings.h>
#endif
#if STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif

#include <kstat.h>
#include <sys/sysinfo.h>

#include "netsh.h"
#include "netlib.h"

/* the lib_start_count and lib_end_count arrays hold the starting
   and ending values of whatever is counting when the system is
   idle. The rate at which this increments during a test is compared
   with a previous calibrarion to arrive at a CPU utilization
   percentage. raj 2005-01-26 */
static uint64_t  lib_start_count[MAXCPUS];
static uint64_t  lib_end_count[MAXCPUS];

static  kstat_t *cpu_ks[MAXCPUS]; /* the addresses that kstat will
                                     need to pull the cpu info from
                                     the kstat interface.  at least I
                                     think that is what this is :) raj
                                     8/2000 */

#define UPDKCID(nk,ok) \
if (nk == -1) { \
  perror("kstat_read "); \
  exit(1); \
} \
if (nk != ok)\
  goto kcid_changed;

static kstat_ctl_t *kc = NULL;
static kid_t kcid = 0;

/* do the initial open of the kstat interface, get the chain id's all
   straightened-out and set-up the addresses for get_kstat_idle to do
   its thing.  liberally borrowed from the sources to TOP. raj 8/2000 */

static int
open_kstat()
{
  kstat_t *ks;
  kid_t nkcid;
  int i;
  int changed = 0;
  static int ncpu = 0;

  kstat_named_t *kn;

  if (debug) {
    fprintf(where,"open_kstat: enter\n");
    fflush(where);
  }

  /*
   * 0. kstat_open
   */

  if (!kc)
    {
      kc = kstat_open();
      if (!kc)
        {
          perror("kstat_open ");
          exit(1);
        }
      changed = 1;
      kcid = kc->kc_chain_id;
    }
#ifdef rickwasstupid
  else {
    fprintf(where,"open_kstat double open!\n");
    fflush(where);
    exit(1);
  }
#endif

  /* keep doing it until no more changes */
 kcid_changed:

  if (debug) {
    fprintf(where,"passing kcid_changed\n");
    fflush(where);
  }

  /*
   * 1.  kstat_chain_update
   */
  nkcid = kstat_chain_update(kc);
  if (nkcid)
    {
      /* UPDKCID will abort if nkcid is -1, so no need to check */
      changed = 1;
      kcid = nkcid;
    }
  UPDKCID(nkcid,0);

  if (debug) {
    fprintf(where,"kstat_lookup for unix/system_misc\n");
    fflush(where);
  }

  ks = kstat_lookup(kc, "unix", 0, "system_misc");
  if (kstat_read(kc, ks, 0) == -1) {
    perror("kstat_read");
    exit(1);
  }


  if (changed) {

    /*
     * 2. get data addresses
     */

    ncpu = 0;

    kn = kstat_data_lookup(ks, "ncpus");
    if (kn && kn->value.ui32 > lib_num_loc_cpus) {
      fprintf(stderr,"number of CPU's mismatch!");
      exit(1);
    }

    for (ks = kc->kc_chain; ks;
         ks = ks->ks_next)
      {
        if (strncmp(ks->ks_name, "cpu_stat", 8) == 0)
          {
            nkcid = kstat_read(kc, ks, NULL);
            /* if kcid changed, pointer might be invalid. we'll deal
               wtih changes at this stage, but will not accept them
               when we are actually in the middle of reading
               values. hopefully this is not going to be a big
               issue. raj 8/2000 */
            UPDKCID(nkcid, kcid);

            if (debug) {
              fprintf(where,"cpu_ks[%d] getting %p\n",ncpu,ks);
              fflush(where);
            }

            cpu_ks[ncpu] = ks;
            ncpu++;
            if (ncpu > lib_num_loc_cpus)
              {
                /* with the check above, would we ever hit this? */
                fprintf(stderr,
                        "kstat finds too many cpus %d: should be %d\n",
                        ncpu,lib_num_loc_cpus);
                exit(1);
              }
          }
      }
    /* note that ncpu could be less than ncpus, but that's okay */
    changed = 0;
  }
}

/* return the value of the idle tick counter for the specified CPU */
static long
get_kstat_idle(cpu)
     int cpu;
{
  cpu_stat_t cpu_stat;
  kid_t nkcid;

  if (debug) {
    fprintf(where,
            "get_kstat_idle reading with kc %x and ks %p\n",
            kc,
            cpu_ks[cpu]);
  }

  nkcid = kstat_read(kc, cpu_ks[cpu], &cpu_stat);
  /* if kcid changed, pointer might be invalid, fail the test */
  UPDKCID(nkcid, kcid);

  return(cpu_stat.cpu_sysinfo.cpu[CPU_IDLE]);

 kcid_changed:
  perror("kcid changed midstream and I cannot deal with that!");
  exit(1);
}

void
cpu_util_init(void)
{
  open_kstat();
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
  return KSTAT;
}

static void
get_cpu_idle(uint64_t *res)
{

  int i;

  /* this open may be redundant */
  open_kstat();

  for (i = 0; i < lib_num_loc_cpus; i++){
    res[i] = get_kstat_idle(i);
  }
  return;
}

float
calibrate_idle_rate(int iterations, int interval)
{

  long
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

  if (debug) {
    fprintf(where,"calling open_kstat from calibrate_kstat\n");
    fflush(where);
  }

  open_kstat();

  if (iterations > MAXTIMES) {
    iterations = MAXTIMES;
  }

  local_maxrate = (float)-1.0;

  for(i = 0; i < iterations; i++) {
    rate[i] = (float)0.0;
    for (j = 0; j < lib_num_loc_cpus; j++) {
      firstcnt[j] = get_kstat_idle(j);
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
      fprintf(where, "Calibration for kstat counter run: %d\n",i);
      fprintf(where,"\tsec = %ld usec = %ld\n",sec,usec);
      fprintf(where,"\telapsed time = %g\n",elapsed);
    }

    for (j = 0; j < lib_num_loc_cpus; j++) {
      secondcnt[j] = get_kstat_idle(j);
      if(debug) {
        /* I know that there are situations where compilers know about */
        /* long long, but the library functions do not... raj 4/95 */
        fprintf(where,
                "\tfirstcnt[%d] = 0x%8.8lx%8.8lx secondcnt[%d] = 0x%8.8lx%8.8lx\n",
                j,
                firstcnt[j],
                firstcnt[j],
                j,
                secondcnt[j],
                secondcnt[j]);
      }
      /* we assume that it would wrap no more than once. we also */
      /* assume that the result of subtracting will "fit" raj 4/95 */
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
  return local_maxrate;
}

float
calc_cpu_util_internal(float elapsed_time)
{
  int i;
  float correction_factor;
  float actual_rate;

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
    if (debug) {
      fprintf(where,
              "calc_cpu_util: actual_rate on processor %d is %f start %lx end %lx\n",
              i,
              actual_rate,
              lib_start_count[i],
              lib_end_count[i]);
    }
    lib_local_per_cpu_util[i] = (lib_local_maxrate - actual_rate) /
      lib_local_maxrate * 100;
    lib_local_per_cpu_util[i] *= correction_factor;
    lib_local_cpu_stats.cpu_util += lib_local_per_cpu_util[i];
  }
  /* we want the average across all n processors */
  lib_local_cpu_stats.cpu_util /= (float)lib_num_loc_cpus;

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
