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

char   netcpu_none_id[]="\
@(#)netcpu_none.c (c) Copyright 2005-2012 Hewlett-Packard Co, 2021 Hewlett Packard Enterprise Development LP. Version 2.6.0";

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
