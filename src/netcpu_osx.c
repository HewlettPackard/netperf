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

char   netcpu_sysctl_id[]="\
@(#)netcpu_osx.c  Version 2.6.0";

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

#include <mach/host_info.h>
#include <mach/mach_types.h>
/* it would seem that on 10.3.9 mach_msg_type_number_t is in
   <mach/message.h> so we'll see about including that one too.
   hopefully it still exists in 10.4. if not, we will need to add some
   .h file checks in configure so we can use "HAVE_mumble" ifdefs
   here */
#include <mach/message.h>

/* some of this is to make Tiger (10.4), Leopard (10.5) and
   SnowLeopard (10.6) happy, we hope it does not anger previous
   versions */
#include <mach/mach_host.h>
/* #include <mach/mach_port.h> */

#include "netsh.h"
#include "netlib.h"

#define UNSIGNED_DIFFERENCE(x,y) (x >= y ? x - y : (0 - y) + x )

static host_cpu_load_info_data_t lib_start_ticks;
static host_cpu_load_info_data_t lib_end_ticks;

static mach_port_t lib_host_port;

void
cpu_util_init(void)
{
  lib_host_port = mach_host_self();
  return;
}

void
cpu_util_terminate(void)
{
  mach_port_deallocate(lib_host_port);
  return;
}

int
get_cpu_method(void)
{
  return OSX;
}

void
get_cpu_idle(uint64_t *res)
{
    return;
}

void
get_host_ticks(host_cpu_load_info_t info)
{
  mach_msg_type_number_t count;

  count = HOST_CPU_LOAD_INFO_COUNT;
  host_statistics(lib_host_port, HOST_CPU_LOAD_INFO, (host_info_t)info, &count);
  return;
}

/* calibrate_sysctl  - perform the idle rate calculation using the
   sysctl call - typically on BSD */

float
calibrate_idle_rate(int iterations, int interval)
{
    return (float)0.0;
}

float
calc_cpu_util_internal(float elapsed_time)
{
  float correction_factor;
  natural_t	userticks, systicks, idleticks, totalticks;

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
    fprintf(where, "correction factor: %f\n", correction_factor);
  }

  userticks = UNSIGNED_DIFFERENCE((lib_end_ticks.cpu_ticks[CPU_STATE_USER] + lib_end_ticks.cpu_ticks[CPU_STATE_NICE]),
				  (lib_start_ticks.cpu_ticks[CPU_STATE_USER] + lib_start_ticks.cpu_ticks[CPU_STATE_NICE]));
  systicks = UNSIGNED_DIFFERENCE(lib_end_ticks.cpu_ticks[CPU_STATE_SYSTEM], lib_start_ticks.cpu_ticks[CPU_STATE_SYSTEM]);
  idleticks = UNSIGNED_DIFFERENCE(lib_end_ticks.cpu_ticks[CPU_STATE_IDLE], lib_start_ticks.cpu_ticks[CPU_STATE_IDLE]);
  totalticks = userticks + systicks + idleticks;

  lib_local_cpu_stats.cpu_util = ((float)userticks
                                  + (float)systicks)/(float)totalticks * 100.0f;
  lib_local_cpu_stats.cpu_util *= correction_factor;

  return lib_local_cpu_stats.cpu_util;

}
void
cpu_start_internal(void)
{
    get_host_ticks(&lib_start_ticks);
}

void
cpu_stop_internal(void)
{
    get_host_ticks(&lib_end_ticks);
}
