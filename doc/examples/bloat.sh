# this is a quick and dirty script to run a netperf TCP_RR and
# TCP_STREAM test concurrently to allow one to see the effect of
# buffer bloat on latency.  We assume that netperf has been compiled
# with demo-mode enabled via ./configure --enable-demo

NETPERF=`which netperf`
if [ $? -ne 0 ]
then
    echo "Unable to find a netperf binary."
    exit -1
fi

# first, start the TCP_RR test
RR_START=`date +%s`
echo "Starting netperf TCP_RR at $RR_START"
netperf -H $1 -l 7200 -t TCP_RR -D 1 -v 2 -- -r 1 2>&1 > netperf_rr.out &

# sleep 30 seconds
sleep 30

# now run the TCP_STREAM test

STREAM_START=`date +%s`
echo "Starting netperf TCP_STREAM test at $STREAM_START"
netperf -H $1 -l 30 -t TCP_STREAM -D 1 -v 2 -- -m 64K 2>&1 > netperf_stream.out
STREAM_STOP=`date +%s`
echo "Netperf TCP_STREAM test stopped at $STREAM_STOP"

# sleep another 30 seconds
sleep 30

pkill -ALRM netperf
RR_STOP=`date +%s`
echo "Netperf TCP_RR test stopped at $RR_STOP"

RRDTOOL=`which rrdtool`
if [ $? -ne 0 ]
then
    echo "Unable to find rrdtool.  You will have to post-process the results by hand"
    exit 0
fi

MIN_TIMESTAMP=`grep Interim netperf_rr.out | head -1 | awk '{print int($10)}'`
MAX_TIMESTAMP=`grep Interim netperf_rr.out | tail -1 | awk '{print int($10)}'`
MAX_INTERVAL=`grep Interim netperf_rr.out | awk 'BEGIN{max=0.0} ($6 > max) {max = $6}END{print int(max) + 1}'`
LENGTH=`expr $MAX_TIMESTAMP - $MIN_TIMESTAMP`

$RRDTOOL create netperf_rr.rrd --step 1 --start $MIN_TIMESTAMP \
    DS:tps:GAUGE:$MAX_INTERVAL:U:U RRA:AVERAGE:0.5:1:$LENGTH

# now fill it
awk -v rrdtool=$RRDTOOL '($1 == "Interim"){printf("%s update netperf_rr.rrd %.3f:%f\n",rrdtool,$10,$3)}' netperf_rr.out | sh

# now graph it.  if you change the runtimes you should probably change
# the width of the chart via the -w option
$RRDTOOL graph bloat.png --imgformat PNG \
    -w 800 -h 400 \
    --lower-limit 0 \
    --start $MIN_TIMESTAMP --end $MAX_TIMESTAMP \
    -t "Effect of bulk transfer on latency" \
    -v "Seconds" \
    DEF:trans=netperf_rr.rrd:tps:AVERAGE \
    CDEF:latency=1.0,trans,/ \
    LINE2:latency#00FF0080:Latency
