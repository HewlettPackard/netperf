
/*
#  Copyright 2021 Hewlett Packard Enterprise Development LP
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


char   netcpu_kstat10_id[]="\
@(#)netcpu_kstat10.c (c) Copyright 2005-2012, Hewlett-Packard Company Version 2.6.0";

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

#include <errno.h>

#include <kstat.h>
#include <sys/sysinfo.h>

#include "netsh.h"
#include "netlib.h"

static kstat_ctl_t *kc = NULL;
static kid_t kcid = 0;

typedef struct cpu_time_counters {
  uint64_t idle;
  uint64_t user;
  uint64_t kernel;
  uint64_t interrupt;
} cpu_time_counters_t;

static cpu_time_counters_t starting_cpu_counters[MAXCPUS];
static cpu_time_counters_t ending_cpu_counters[MAXCPUS];
static cpu_time_counters_t delta_cpu_counters[MAXCPUS];
static cpu_time_counters_t corrected_cpu_counters[MAXCPUS];

static void
print_cpu_time_counters(char *name, int instance, cpu_time_counters_t *counters)
{
  fprintf(where,
	  "%s[%d]:\n"
	  "\t idle %llu\n"
	  "\t user %llu\n"
	  "\t kernel %llu\n"
	  "\t interrupt %llu\n",
	  name,instance,
	  counters[instance].idle,
	  counters[instance].user,
	  counters[instance].kernel,
	  counters[instance].interrupt);
}

void
cpu_util_init(void)
{
  kstat_t   *ksp;
  int i;
  kc = kstat_open();

  if (kc == NULL) {
    fprintf(where,
	    "cpu_util_init: kstat_open: errno %d %s\n",
	    errno,
	    strerror(errno));
    fflush(where);
    exit(-1);
  }

  /* lets flesh-out a CPU instance number map since it seems that some
     systems, not even those which are partitioned, can have
     non-contiguous CPU numbers.  discovered "the hard way" on a
     T5220. raj 20080804 */
  i = 0;
  for (ksp = kc->kc_chain, i = 0;
       (ksp != NULL) && (i < MAXCPUS);
       ksp = ksp->ks_next) {
    if ((strcmp(ksp->ks_module,"cpu") == 0) &&
	(strcmp(ksp->ks_name,"sys") == 0)) {
      if (debug) {
	fprintf(where,"Mapping CPU instance %d to entry %d\n",
		ksp->ks_instance,i);
	fflush(where);
      }
      lib_cpu_map[i++] = ksp->ks_instance;
    }
  }

  if (MAXCPUS == i) {
    fprintf(where,
            "Sorry, this system has more CPUs (%d) than netperf can handle (%d).\n"
            "Please alter MAXCPUS in netlib.h and recompile.\n",
            i,
            MAXCPUS);
    fflush(where);
    exit(1);
  }

  return;
}

void
cpu_util_terminate(void)
{
  kstat_close(kc);
  return;
}

int
get_cpu_method(void)
{
  return KSTAT_10;
}

static void
print_unexpected_statistic_warning(char *who, char *what, char *why)
{
  if (why) {
    fprintf(where,
	    "WARNING! WARNING! WARNING! WARNING!\n"
	    "%s found an unexpected %s statistic %.16s\n",
	    who,
	    why,
	    what);
  }
  else {
    fprintf(where,
	    "%s is ignoring statistic %.16s\n",
	    who,
	    what);
  }
}

static void
get_cpu_counters(int cpu_num, cpu_time_counters_t *counters)
{

  kstat_t *ksp;
  int found=0;
  kid_t nkcid;
  kstat_named_t *knp;
  int i;

  ksp = kstat_lookup(kc, "cpu", lib_cpu_map[cpu_num], "sys");
  if ((ksp) && (ksp->ks_type == KSTAT_TYPE_NAMED)) {
    /* happiness and joy, keep going */
    nkcid = kstat_read(kc, ksp, NULL);
    if (nkcid != -1) {
      /* happiness and joy, keep going. we could consider adding a
	 "found < 3" to the end conditions, but then we wouldn't
	 search to the end and find that Sun added some nsec. we
	 probably want to see if they add an nsec. raj 2005-01-28 */
      for (i = ksp->ks_ndata, knp = ksp->ks_data;
	   i > 0;
	   knp++,i--) {
	/* we would be hosed if the same name could appear twice */
	if (!strcmp("cpu_nsec_idle",knp->name)) {
	  found++;
	  counters[cpu_num].idle = knp->value.ui64;
	}
	else if (!strcmp("cpu_nsec_user",knp->name)) {
	  found++;
	  counters[cpu_num].user = knp->value.ui64;
	}
	else if (!strcmp("cpu_nsec_kernel",knp->name)) {
	  found++;
	  counters[cpu_num].kernel = knp->value.ui64;
	}
	else if (!strcmp("cpu_nsec_intr",knp->name)) {
	  if (debug >= 2) {
	    fprintf(where,
		    "Found a cpu_nsec_intr but it doesn't do what we want\n");
	    fflush(where);
	  }
	}
	else if (strstr(knp->name,"nsec")) {
	  /* finding another nsec here means Sun have changed
	     something and we need to warn the user. raj 2005-01-28 */
	  print_unexpected_statistic_warning("get_cpu_counters",
					     knp->name,
					     "nsec");
	}
	else if (debug >=2) {

	  /* might want to tell people about what we are skipping.
	     however, only display other names debug >=2. raj
	     2005-01-28  */

	  print_unexpected_statistic_warning("get_cpu_counters",
					     knp->name,
					     NULL);
	}
      }
      if (3 == found) {
	/* happiness and joy */
	return;
      }
      else {
	fprintf(where,
		"get_cpu_counters could not find one or more of the expected counters!\n");
	fflush(where);
	exit(-1);
      }
    }
    else {
      /* the kstat_read returned an error or the chain changed */
      fprintf(where,
	      "get_cpu_counters: kstat_read failed or chain id changed %d %s\n",
	      errno,
	      strerror(errno));
      fflush(where);
      exit(-1);
    }
  }
  else {
    /* the lookup failed or found the wrong type */
    fprintf(where,
	    "get_cpu_counters: kstat_lookup failed for module 'cpu' number %d instance %d name 'sys' and KSTAT_TYPE_NAMED: errno %d %s\n",
	    cpu_num,
	    lib_cpu_map[cpu_num],
	    errno,
	    strerror(errno));
    fflush(where);
    exit(-1);
  }
}

static void
get_interrupt_counters(int cpu_num, cpu_time_counters_t *counters)
{
  kstat_t *ksp;
  int found=0;
  kid_t nkcid;
  kstat_named_t *knp;
  int i;

  ksp = kstat_lookup(kc, "cpu", lib_cpu_map[cpu_num], "intrstat");

  counters[cpu_num].interrupt = 0;
  if ((ksp) && (ksp->ks_type == KSTAT_TYPE_NAMED)) {
    /* happiness and joy, keep going */
    nkcid = kstat_read(kc, ksp, NULL);
    if (nkcid != -1) {
      /* happiness and joy, keep going. we could consider adding a
	 "found < 15" to the end conditions, but then we wouldn't
	 search to the end and find that Sun added some "time." we
	 probably want to see if they add a "nsec." raj 2005-01-28 */
      for (i = ksp->ks_ndata, knp = ksp->ks_data;
	   i > 0;
	   knp++,i--) {
	if (strstr(knp->name,"time")) {
	  found++;
	  counters[cpu_num].interrupt += knp->value.ui64;
	}
	else if (debug >=2) {

	  /* might want to tell people about what we are skipping.
	     however, only display other names debug >=2. raj
	     2005-01-28
	  */

	  print_unexpected_statistic_warning("get_cpu_counters",
					     knp->name,
					     NULL);
	}
      }
      if (15 == found) {
	/* happiness and joy */
	return;
      }
      else {
	fprintf(where,
		"get_cpu_counters could not find one or more of the expected counters!\n");
	fflush(where);
	exit(-1);
      }
    }
    else {
      /* the kstat_read returned an error or the chain changed */
      fprintf(where,
	      "get_cpu_counters: kstat_read failed or chain id changed %d %s\n",
	      errno,
	      strerror(errno));
      fflush(where);
      exit(-1);
    }
  }
  else {
    /* the lookup failed or found the wrong type */
    fprintf(where,
	    "get_cpu_counters: kstat_lookup failed for module 'cpu' %d instance %d class 'intrstat' and KSTAT_TYPE_NAMED: errno %d %s\n",
	    cpu_num,
	    lib_cpu_map[cpu_num],
	    errno,
	    strerror(errno));
    fflush(where);
    exit(-1);
  }

}

static void
get_cpu_time_counters(cpu_time_counters_t *counters)
{

  int i;

  for (i = 0; i < lib_num_loc_cpus; i++){
    get_cpu_counters(i, counters);
    get_interrupt_counters(i, counters);
  }

  return;
}

/* the kstat10 mechanism, since it is based on actual nanosecond
   counters is not going to use a comparison to an idle rate. so, the
   calibrate_idle_rate routine will be rather simple :) raj 2005-01-28
   */

float
calibrate_idle_rate(int iterations, int interval)
{
  return 0.0;
}

float
calc_cpu_util_internal(float elapsed_time)
{
  int i;
  float correction_factor;
  float actual_rate;

  uint64_t total_cpu_nsec;

  /* multiply by 100 and divide by total and you get whole
     percentages. multiply by 1000 and divide by total and you get
     tenths of percentages.  multiply by 10000 and divide by total and
     you get hundredths of percentages. etc etc etc raj 2005-01-28 */

#define CALC_PERCENT 100
#define CALC_TENTH_PERCENT 1000
#define CALC_HUNDREDTH_PERCENT 10000
#define CALC_THOUSANDTH_PERCENT 100000
#define CALC_ACCURACY CALC_THOUSANDTH_PERCENT

  uint64_t fraction_idle;
  uint64_t fraction_user;
  uint64_t fraction_kernel;
  uint64_t fraction_interrupt;

  uint64_t interrupt_idle;
  uint64_t interrupt_user;
  uint64_t interrupt_kernel;

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

  for (i = 0; i < lib_num_loc_cpus; i++) {

    /* this is now the fun part.  we have the nanoseconds _allegedly_
       spent in user, idle and kernel.  We also have nanoseconds spent
       servicing interrupts.  Sadly, in the developer's finite wisdom,
       the interrupt time accounting is in parallel with the other
       accounting. this means that time accounted in user, kernel or
       idle will also include time spent in interrupt.  for netperf's
       porpoises we do not really care about that for user and kernel,
       but we certainly do care for idle.  the $64B question becomes -
       how to "correct" for this?

       we could just subtract interrupt time from idle.  that has the
       virtue of simplicity and also "punishes" Sun for doing
       something that seems to be so stupid.  however, we probably
       have to be "fair" even to the allegedly stupid so the other
       mechanism, suggested by a Sun engineer is to subtract interrupt
       time from each of user, kernel and idle in proportion to their
       numbers.  then we sum the corrected user, kernel and idle along
       with the interrupt time and use that to calculate a new idle
       percentage and thus a CPU util percentage.

       that is what we will attempt to do here.  raj 2005-01-28

       of course, we also have to wonder what we should do if there is
       more interrupt time than the sum of user, kernel and idle.
       that is a theoretical possibility I suppose, but for the
       time-being, one that we will blythly ignore, except perhaps for
       a quick check. raj 2005-01-31
    */

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

    /* for this summation, we do not include interrupt time */
    total_cpu_nsec =
      delta_cpu_counters[i].idle +
      delta_cpu_counters[i].user +
      delta_cpu_counters[i].kernel;

    if (debug) {
      fprintf(where,"total_cpu_nsec %llu\n",total_cpu_nsec);
    }

    if (delta_cpu_counters[i].interrupt > total_cpu_nsec) {
      /* we are not in Kansas any more Toto, and I am not quite sure
	 the best way to get our tails out of here so let us just
	 punt. raj 2005-01-31 */
      fprintf(where,
	      "WARNING! WARNING! WARNING! WARNING! WARNING! \n"
	      "calc_cpu_util_internal: more interrupt time than others combined!\n"
	      "\tso CPU util cannot be estimated\n"
	      "\t delta[%d].interrupt %llu\n"
	      "\t delta[%d].idle %llu\n"
	      "\t delta[%d].user %llu\n"
	      "\t delta[%d].kernel %llu\n",
	      i,delta_cpu_counters[i].interrupt,
	      i,delta_cpu_counters[i].idle,
	      i,delta_cpu_counters[i].user,
	      i,delta_cpu_counters[i].kernel);
      fflush(where);

      lib_local_cpu_stats.cpu_util = -1.0;
      lib_local_per_cpu_util[i] = -1.0;
      return -1.0;
    }

    /* and now some fun with integer math.  i initially tried to
       promote things to long doubled but that didn't seem to result
       in happiness and joy. raj 2005-01-28 */

    fraction_idle =
      (delta_cpu_counters[i].idle * CALC_ACCURACY) / total_cpu_nsec;

    fraction_user =
      (delta_cpu_counters[i].user * CALC_ACCURACY) / total_cpu_nsec;

    fraction_kernel =
      (delta_cpu_counters[i].kernel * CALC_ACCURACY) / total_cpu_nsec;

    /* ok, we have our fractions, now we want to take that fraction of
       the interrupt time and subtract that from the bucket. */

    interrupt_idle =  ((delta_cpu_counters[i].interrupt * fraction_idle) /
		       CALC_ACCURACY);

    interrupt_user = ((delta_cpu_counters[i].interrupt * fraction_user) /
		      CALC_ACCURACY);

    interrupt_kernel = ((delta_cpu_counters[i].interrupt * fraction_kernel) /
			CALC_ACCURACY);

    if (debug) {
      fprintf(where,
	      "\tfraction_idle %llu interrupt_idle %llu\n"
	      "\tfraction_user %llu interrupt_user %llu\n"
	      "\tfraction_kernel %llu interrupt_kernel %llu\n",
	      fraction_idle,
	      interrupt_idle,
	      fraction_user,
	      interrupt_user,
	      fraction_kernel,
	      interrupt_kernel);
    }

    corrected_cpu_counters[i].idle = delta_cpu_counters[i].idle -
      interrupt_idle;

    corrected_cpu_counters[i].user = delta_cpu_counters[i].user -
      interrupt_user;

    corrected_cpu_counters[i].kernel = delta_cpu_counters[i].kernel -
      interrupt_kernel;

    corrected_cpu_counters[i].interrupt = delta_cpu_counters[i].interrupt;

    if (debug) {
      print_cpu_time_counters("corrected_cpu_counters",
			      i,
			      corrected_cpu_counters);
    }

    /* I was going to check for going less than zero, but since all
       the calculations are in unsigned quantities that would seem to
       be a triffle silly... raj 2005-01-28 */

    /* ok, now we sum the numbers again, this time including interrupt
       */

    total_cpu_nsec =
      corrected_cpu_counters[i].idle +
      corrected_cpu_counters[i].user +
      corrected_cpu_counters[i].kernel +
      corrected_cpu_counters[i].interrupt;

    /* and recalculate our fractions we are really only going to use
       fraction_idle, but lets calculate the rest just for the heck of
       it. one day we may want to display them. raj 2005-01-28 */

    /* multiply by 100 and divide by total and you get whole
       percentages. multiply by 1000 and divide by total and you get
       tenths of percentages.  multiply by 10000 and divide by total
       and you get hundredths of percentages. etc etc etc raj
       2005-01-28 */
    fraction_idle =
      (corrected_cpu_counters[i].idle * CALC_ACCURACY) / total_cpu_nsec;

    fraction_user =
      (corrected_cpu_counters[i].user * CALC_ACCURACY) / total_cpu_nsec;

    fraction_kernel =
      (corrected_cpu_counters[i].kernel * CALC_ACCURACY) / total_cpu_nsec;

    fraction_interrupt =
      (corrected_cpu_counters[i].interrupt * CALC_ACCURACY) / total_cpu_nsec;

    if (debug) {
      fprintf(where,"\tfraction_idle %lu\n",fraction_idle);
      fprintf(where,"\tfraction_user %lu\n",fraction_user);
      fprintf(where,"\tfraction_kernel %lu\n",fraction_kernel);
      fprintf(where,"\tfraction_interrupt %lu\n",fraction_interrupt);
    }

    /* and finally, what is our CPU utilization? */
    lib_local_per_cpu_util[i] = 100.0 - (((float)fraction_idle /
					  (float)CALC_ACCURACY) * 100.0);
    lib_local_per_cpu_util[i] *= correction_factor;
    if (debug) {
      fprintf(where,
	      "lib_local_per_cpu_util[%d] %g cf %f\n",
	      i,
	      lib_local_per_cpu_util[i],
	      correction_factor);
    }
    lib_local_cpu_stats.cpu_util += lib_local_per_cpu_util[i];
  }
  /* we want the average across all n processors */
  lib_local_cpu_stats.cpu_util /= (float)lib_num_loc_cpus;

  return lib_local_cpu_stats.cpu_util;
}

void
cpu_start_internal(void)
{
  get_cpu_time_counters(starting_cpu_counters);
  return;
}

void
cpu_stop_internal(void)
{
  get_cpu_time_counters(ending_cpu_counters);
}
