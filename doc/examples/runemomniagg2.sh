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

#set -x
# edit and add to this array as necessary
# the hosts you will use should be contiguous
# starting at index zero
remote_hosts[0]=192.168.2.3
remote_hosts[1]=192.168.3.5
remote_hosts[2]=192.168.4.6
remote_hosts[3]=192.168.5.7
remote_hosts[4]=192.168.2.5
remote_hosts[5]=192.168.3.3
remote_hosts[6]=192.168.4.7
remote_hosts[7]=192.168.5.6
remote_hosts[8]=192.168.2.6
remote_hosts[9]=192.168.3.7
remote_hosts[10]=192.168.4.3
remote_hosts[11]=192.168.5.5
remote_hosts[12]=192.168.2.7
remote_hosts[13]=192.168.3.6
remote_hosts[14]=192.168.4.5
remote_hosts[15]=192.168.5.3

# this should always be less than or equal to the
# number of valid hosts in the array above
num_cli=16

# this will be the length of each individual test
# iteration
length=30

# this will be the settings for confidence intervals
# you can use a smaller number of iterations but
# to ensure that everyone is running at the same time
# the min and max iterations MUST be the same
confidence="-i 30,30"

# the different number of concurrent sessions to be run
# if you do not start with one concurrent session the
# test headers may not be where one wants them and you
# may need to edit the output to hoist the test header
# up above the first result
concurrent_sessions="1 4 8 16 32 64"

# the socket buffer sizes - you may need to tweak
# some system settings to allow 1M socket buffers
socket_sizes=" -s 1M -S 1M"

# the burst sizes in the aggregate request/response tests
#burst_sizes="0 1 4 16 64 256 1024"
burst_sizes="0 1 4 16"

# this ensures the test header of at least one instance
# is displayed
HDR="-P 1"

# -O means "human" -o means "csv" and -k means "keyval"
# "all" means emit everything it knows to emit. omit "all"
# and what is emitted will depend on the test. can customize
# with direct output selection or specifying a file with
# output selectors in it
CSV="-o all"

# should tests outbound relative to this system be run?
DO_STREAM=0

# should tests inbound relative to this system be run?
DO_MAERTS=0

# should same connection bidirectional tests be run?
DO_BIDIR=1

# should aggreagte single-byte request/response be run?
# this can be used to try to get a maximum PPS figure
DO_RRAGG=1

# here you should echo-out some things about the test
# particularly those things that will not be automagically
# captured by netperf.
echo interrupts spread wherever irqbalanced put them
echo 4xAD386A in DL785 G5 SLES11B6, HP/vendor drivers
echo four dl585 G5 clients rh5.2, each with two AD386A

# and here we go
  if [ $DO_STREAM -eq 1 ]; then
  echo TCP_STREAM
  for i in $concurrent_sessions; do
    j=0;
    NETUUID=`netperf -t uuid`;
    echo $i concurrent streams id $NETUUID;
    while [ $j -lt $i ]; do
      client=`expr $j % $num_cli` ;
      netperf $HDR -t omni -c -C -H ${remote_hosts[$client]} -l $length $confidence -- $CSV -H ${remote_hosts[$client]} $socket_sizes -m 64K -u $NETUUID & HDR="-P 0";
      j=`expr $j + 1`;
    done;
    wait;
  done
  fi
#
  if [ $DO_MAERTS -eq 1 ]; then
  echo TCP_MAERTS
  for i in $concurrent_sessions; do
    j=0;
    NETUUID=`netperf -t uuid`;
    echo $i concurrent streams id $NETUUID;
    while [ $j -lt $i ]; do
      client=`expr $j % $num_cli` ;
      netperf $HDR -t omni -c -C -H ${remote_hosts[$client]} -l $length $confidence -- $CSV -H ${remote_hosts[$client]} $socket_sizes -M ,64K -u $NETUUID & HDR="-P 0";
      j=`expr $j + 1`;
    done;
    wait;
  done
  fi

  if [ $DO_BIDIR -eq 1 ]; then
  echo bidir TCP_RR MEGABITS
  HDR="-P 1"
  for i in $concurrent_sessions;
    do j=0;
    NETUUID=`netperf -t uuid`;
    echo $i concurrent streams id $NETUUID;
    while [ $j -lt $i ]; do
      client=`expr $j % $num_cli` ;
      netperf $HDR  -t omni -f m -c -C -H ${remote_hosts[$client]} -l $length $confidence -- $CSV  -H ${remote_hosts[$client]} -s 1M -S 1M -r 64K -b 12 -u $NETUUID & HDR="-P 0";
      j=`expr $j + 1`;
    done;
    wait;
  done
  fi

  if [ $DO_RRAGG -eq 1 ]; then
  echo TCP_RR aggregates
  HDR="-P 1"
  for i in $concurrent_sessions; do
    NETUUID=`netperf -t uuid`;
    echo $i concurrent streams id $NETUUID;
    for b in $burst_sizes; do
      echo burst of $b;
      j=0;
      while [ $j -lt $i ]; do
        client=`expr $j % $num_cli` ;
        netperf $HDR  -t omni -f x -c -C -H ${remote_hosts[$client]} -l $length $confidence -- $CSV  -H ${remote_hosts[$client]} -r 1 -b $b -D -u $NETUUID & HDR="-P 0";
        j=`expr $j + 1`;
      done;
      wait;
    done;
  done
  fi

cat /proc/meminfo
