# a crude script to try to find the burst size which results in
# something approximating peak transactions per second.  once we
# pass the peak, we may want to refine further but for now, this
# should suffice

LAST_TPS=0
BURST=1
LAST_BURST=0
NETPERF=${NETPERF:="netperf"}

TMP_TPS=`$NETPERF -t UDP_RR -P 0 -H $1 -- -b $BURST -D -o throughput -e 1 -s 1M -S 1M`

if [ $? -ne 0 ]
then
    echo -1
    exit -1
fi

CUR_TPS=`echo $TMP_TPS | awk '{print int($1)}'`

while [ $CUR_TPS -gt $LAST_TPS ]
do
    LAST_BURST=$BURST
    BURST=`expr $BURST \* 2`
    LAST_TPS=$CUR_TPS

    TMP_TPS=`$NETPERF -t UDP_RR -P 0 -H $1 -- -b $BURST -D -o throughput -e 1 -s 1M -S 1M`

    if [ $? -ne 0 ]
    then
	echo -1
	exit -1
    fi

    CUR_TPS=`echo $TMP_TPS | awk '{print int($1)}'`
done

# if we were going to further refine the estimate we would probably do
# a binary search at this point but for now, just emit the value of
# LAST_BURST and be done with it.

echo $LAST_BURST
