length=30
confidence="-i 30,3"
netperf_CPUs="0 1 2"
netserver_CPUs="0 1 2"
control_host=192.168.0.26
remote_hosts="192.168.0.26 fe80::207:43ff:fe05:590a%eth0"
#reqs="128 256 512 1024"
reqs="64 128 256 512 1024 1460 2048 4096 8192"
bursts="0 1 2 4 8 16 32 64 128 256"
HDR="-P 1"
# -O means "human" -o means "csv"
CSV="-o"
#CSV="-O"

DO_TCP_STREAM_AUTO=1
DO_TCP_STREAM_SOPT=1
DO_TCP_BIDIR=1
DO_TCP_RR=1
DO_TCP_CC=0
DO_TCP_BIDIR_REQ=0
DO_UDP_BIDIR_REQ=0
DO_UDP_STREAM=1


FULL_MATRIX=0

echo interrupts CPU 0 with CPU 1 other socket CPU 2 same socket
echo ad386a in dl380 g5 2x 5160@3GHz to same
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
