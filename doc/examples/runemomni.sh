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

# a script to run a set of single-instance netperf tests
# between two machines

# the length in seconds of each test iteration. the actual
# run time will then be somewhere between that times min
# and max iteration for confidence intervals
length=30

# unlike the aggregate script, we do not have to worry
# about everyone all running at the same time, so we can
# save some time with a lower minimum iteration count
confidence="-i 30,3"

# the CPUs to which the netperf process will be bound
# via the -T global option
netperf_CPUs="0 1 2"

# the CPUs to which the netserver process will be bound
# via the -T global option
netserver_CPUs="0 1 2"

# the host/IP to use for the control connection
control_host=192.168.0.26

# the list of host/IP addresses to actually measure over
remote_hosts="192.168.0.26 fe80::207:43ff:fe05:590a%eth0"

#reqs="128 256 512 1024"
reqs="64 128 256 512 1024 1460 2048 4096 8192"

# the burst size for bursted RR tests
bursts="0 1 2 4 8 16 32 64 128 256"

HDR="-P 1"
# -O means "human" -o means "csv" -k means "keyval"
# "all" means emit everything.  otherwise, specify a
# list of output selectors directly or a filename with
# them therein.  no parameter means a default set will
# be emitted based on the test type
CSV="-o all"
#CSV="-O all"

# which of the tests should we do?
DO_TCP_STREAM_AUTO=1
DO_TCP_STREAM_SOPT=1
DO_TCP_BIDIR=1
DO_TCP_RR=1
DO_TCP_CC=0
DO_TCP_BIDIR_REQ=0
DO_UDP_BIDIR_REQ=0
DO_UDP_STREAM=1

# when this is set to 0 then netperf and netserver are bound
# to the same CPU number and we do not walk through all the
# combinations.  one can do this when the two systems are
# otherwise identical.  when they are not, then running
# through the full matrix may be indicated
FULL_MATRIX=0

# here you should echo some things about the test and its
# environment and in particular those things not automagically
# determined by netperf
echo I NEED TO EDIT THE SCRIPT
echo interrupts CPU 0 with CPU 1 other socket CPU 2 same socket
echo ad386a in dl380 g5 2x 5160@3GHz to same

# and away we go
for data in $remote_hosts
do

  if [ $DO_TCP_STREAM_AUTO -eq 1 ]
  then
    echo TCP_STREAM to $data autotuning
    for i in $netperf_CPUs
    do
      if [ $FULL_MATRIX -eq 1 ]
      then
        set=$netserver_CPUs
      else
        set=$i
      fi
      for j in $set
      do
        netperf $HDR -T $i,$j -t omni -c -C -H $control_host -l $length $confidence -- $CSV -H $data -m 64K;HDR="-P 0";
      done
    done
  fi

  if [ $DO_TCP_STREAM_SOPT -eq 1 ]
  then
    echo TCP_STREAM to $data
    HDR="-P 1"
    for i in $netperf_CPUs
    do
      if [ $FULL_MATRIX -eq 1 ]
      then
        set=$netserver_CPUs
      else
        set=$i
      fi
      for j in $set
      do
        netperf $HDR -T $i,$j -t omni -c -C -H $control_host -l $length $confidence -- $CSV -H $data -s 1M -S 1M -m 64K;HDR="-P 0";
      done
    done
  fi

  if [ $DO_TCP_BIDIR -eq 1 ]
  then
    echo bidir TCP_RR MEGABITS to $data
    HDR="-P 1"
    for i in $netperf_CPUs
    do
      if [ $FULL_MATRIX -eq 1 ]
      then
        set=$netserver_CPUs
      else
        set=$i
      fi
      for j in $set
      do
        netperf $HDR -T $i,$j -t omni -f m -c -C -H $control_host -l $length $confidence -- $CSV  -H $data -s 1M -S 1M -r 64K -b 12; HDR="-P 0";
      done
    done
  fi

  if [ $DO_TCP_RR -eq 1 ]
  then
    echo TCP_RR to $data
    HDR="-P 1"
    for i in $netperf_CPUs
    do
      if [ $FULL_MATRIX -eq 1 ]
      then
        set=$netserver_CPUs
      else
        set=$i
      fi
      for j in $set
      do
        netperf $HDR -T $i,$j -t omni -c -C -H $control_host -l $length $confidence -- $CSV -H $data -r 1; HDR="-P 0";
      done
    done
  fi

  if [ $DO_TCP_BIDIR_REQ -eq 1 ]
  then
    echo bidir TCP_RR MEGABITS to $data altering req/rsp size and burst
    HDR="-P 1"
    for i in $netperf_CPUs
    do
      for req in $reqs; do
        for burst in $bursts; do
          netperf $HDR -T $i -t omni -f m -c -C -H $control_host -l $length $confidence -- $CSV -H $data -s 1M -S 1M -r $req -b $burst -D;HDR=-"P 0";
        done
      done
    done
  fi

  if [ $DO_UDP_BIDIR_REQ -eq 1 ]
  then
    echo bidir UDP_RR  MEGABITS to $data altering req/rsp size and burst
    HDR="-P 1"
    for i in $netperf_CPUs
    do
       for req in $reqs; do
         for burst in $bursts; do
           netperf $HDR -T $i -t omni -f m -c -C -H $control_host -l $length $confidence -- $CSV -H $data -s 1M -S 1M -r $req -b $burst -T udp;HDR=-"P 0";
         done
       done
    done
  fi

  if [ $DO_UDP_STREAM -eq 1 ]
  then
    echo UDP_STREAM  MEGABITS to $data altering send size, no confidence intvls
    confidence=" "
    echo CPUs $netperf_CPUs reqs $reqs
    HDR="-P 1"
    for i in $netperf_CPUs
    do
      for req in $reqs; do
        netperf $HDR -T $i -t omni -f m -c -C -H $control_host -l $length $confidence -- $CSV -H $data -s 1M -S 1M -m $req -T udp;HDR=-"P 0";
      done
    done
  fi

done

cat /proc/meminfo
cat /proc/cpuinfo
