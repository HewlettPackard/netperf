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

CHUNK=60

# first, start the TCP_RR test
RR_START=`date +%s`
echo "Starting netperf TCP_RR at $RR_START" | tee bloat.log
# a negative value for the demo interval (-D) will cause netperf to
# make gettimeofday() calls after every transaction. this will result
# in more accurate demo intervals once the STREAM test kicks-in, but a
# somewhat lower transaction rate.  not unlike enabling histogram
# mode.
netperf -H $1 -l 7200 -t TCP_RR -D -0.5 -v 2 -- -r 1 2>&1 > netperf_rr.out &

# sleep CHUNK seconds
sleep $CHUNK

# now run the TCP_STREAM test

STREAM_START=`date +%s`
echo "Starting netperf TCP_STREAM test at $STREAM_START" | tee -a bloat.log
netperf -H $1 -l `expr $CHUNK \* 2` -t TCP_STREAM -D 0.25 -v 2 -- -m 1K 2>&1 > netperf_stream.out
STREAM_STOP=`date +%s`
echo "Netperf TCP_STREAM test stopped at $STREAM_STOP" | tee -a bloat.log

# sleep another CHUNK seconds
sleep $CHUNK

pkill -ALRM netperf
RR_STOP=`date +%s`
echo "Netperf TCP_RR test stopped at $RR_STOP" | tee -a bloat.log

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

# now post process the tcp_stream test. we could use STREAM_START and
# STREAM_STOP but these will be just a bit more accurate
STREAM_MIN_TIMESTAMP=`grep Interim netperf_stream.out | head -1 | awk '{print int($10)}'`
STREAM_MAX_TIMESTAMP=`grep Interim netperf_stream.out | tail -1 | awk '{print int($10)}'`
STREAM_MAX_INTERVAL=`grep Interim netperf_stream.out | awk 'BEGIN{max=0.0} ($6 > max) {max = $6}END{print int(max) + 1}'`
STREAM_LENGTH=`expr $STREAM_MAX_TIMESTAMP - $STREAM_MIN_TIMESTAMP`

$RRDTOOL create netperf_stream.rrd --step 1 --start $STREAM_MIN_TIMESTAMP \
    DS:mbps:GAUGE:$STREAM_MAX_INTERVAL:U:U RRA:AVERAGE:0.5:1:$STREAM_LENGTH

# now fill it
awk -v rrdtool=$RRDTOOL '($1 == "Interim"){printf("%s update netperf_stream.rrd %.3f:%f\n",rrdtool,$10,$3)}' netperf_stream.out | sh


# now graph it. we want to make sure the chart is at least 800 pixels
# wide, and has enough pixels for every data point

WIDTH=$LENGTH
if [ $WIDTH -lt 800 ]
then
    WIDTH=800
fi

SIZE="-w $WIDTH -h 400"

# we want to find the scaling factor for the throughput, with the goal
# being that latency can go to the top of the charts and throughput
# will go half-way up

MAXLATMAXBPS=`$RRDTOOL graph /dev/null \
    --start $MIN_TIMESTAMP --end $MAX_TIMESTAMP \
    DEF:trans=netperf_rr.rrd:tps:AVERAGE \
    CDEF:latency=1.0,trans,/ \
    VDEF:maxlatency=latency,MAXIMUM \
    DEF:mbps=netperf_stream.rrd:mbps:AVERAGE \
    CDEF:bps=mbps,2000000,\* \
    VDEF:maxbps=bps,MAXIMUM \
    PRINT:maxlatency:"%.20lf" \
    PRINT:maxbps:"%.20lf" | sed 1d`

# should I check the completion status of the previous command?
# probably :)

SCALE=`echo $MAXLATMAXBPS | awk '{print $2/$1}'`

$RRDTOOL graph bloat.svg --imgformat SVG \
    $SIZE \
    --lower-limit 0 \
    --start $MIN_TIMESTAMP --end $MAX_TIMESTAMP \
    -t "Effect of bulk transfer on latency to $1" \
    -v "Seconds" \
    --right-axis $SCALE:0 \
    --right-axis-label "Bits per Second" \
    DEF:trans=netperf_rr.rrd:tps:AVERAGE \
    CDEF:latency=1.0,trans,/ \
    LINE2:latency#00FF0080:"TCP_RR Latency" \
    DEF:mbps=netperf_stream.rrd:mbps:AVERAGE \
    CDEF:bps=mbps,1000000,\* \
    CDEF:sbps=bps,$SCALE,/ \
    LINE2:sbps#0000FFF0:"TCP_STREAM Throughput" \
    VRULE:${STREAM_START}#FF000080:"TCP_STREAM start" \
    VRULE:${STREAM_STOP}#00000080:"TCP_STREAM stop" \
    --x-grid SECOND:10:SECOND:60:SECOND:60:0:%X
