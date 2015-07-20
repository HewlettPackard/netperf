char   netcpu_none_id[]="\
@(#)netcpu_none.c (c) Copyright 2005-2012 Hewlett-Packard Company, Version 2.6.0";

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

#include "netsh.h"
#include "netlib.h"

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
  return CPU_UNKNOWN;
}

float
calibrate_idle_rate(int iterations, int interval)
{
  return 0.0;
}

float
calc_cpu_util_internal(float elapsed_time)
{
  return -1.0;
}

void
cpu_start_internal(void)
{
  return;
}

void
cpu_stop_internal(void)
{
  return;
}
