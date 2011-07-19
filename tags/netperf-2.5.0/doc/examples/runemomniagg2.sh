#set -x
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
num_cli=16
length=30
confidence="-i 30,30"
concurrent_sessions="1 4 8 16 32 64"
socket_sizes=" -s 1M -S 1M"
#burst_sizes="0 1 4 16 64 256 1024"
burst_sizes="0 1 4 16"
HDR="-P 1"
# -O means "human" -o means "csv"
CSV="-o"
#CSV="-O"

DO_STREAM=
DO_MAERTS=
DO_BIDIR=1
DO_RRAGG=1

#echo "throughput, command_line" > tmpfoo
#CSV="-o tmpfoo"

echo interrupts spread wherever irqbalanced put them
echo 4xAD386A in DL785 G5 SLES11B6, HP/vendor drivers
echo four dl585 G5 clients rh5.2, each with two AD386A

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
