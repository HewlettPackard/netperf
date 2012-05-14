#!/bin/bash
# find where to put the vertical lines
awk -f vrules.awk $1 > vrules
. ./vrules

i=0
VRULES=""
while [ $i -lt $NUM_VRULES ]
do
  VRULES="$VRULES VRULE:${VRULE_TIME[$i]}#${VRULE_COLR[$i]}:${VRULE_TEXT[$i]}"
  i=`expr $i + 1`
done
# echo $VRULES

if [ -f kitsink ]
then
  rm kitsink
fi

prefix=${1%.log}
echo "Prefix is $prefix"


for i in ${prefix}*.out
do

# find some boundaries for this .out file
    awk -F "," -f mins_maxes.awk $i > minsn

    . ./minsn

# echo "MAX_INTERVAL $MAX_INTERVAL MIN_TIMESTAMP $MIN_TIMESTAMP MAX_TIMESTAMP $MAX_TIMESTAMP"
    LENGTH=`expr $MAX_TIMESTAMP - $MIN_TIMESTAMP`
    SIZE="-w $LENGTH -h 400"


    # ooh, rick learns how to strip a suffix
    basename=${i%\.out}
    echo "Post-processing ${basename}"

    rrdtool create ${basename}.rrd --step 1 --start $MIN_TIMESTAMP \
	DS:mbps:GAUGE:$MAX_INTERVAL:U:U RRA:AVERAGE:0.5:1:$LENGTH

    # keep in mind that rrd only likes timestamps to milliseconds
    # at some point it would be nice to do more than one data point
    # at a time
    awk -v rrdfile=${basename}.rrd -F "," '(NF == 4){printf("rrdtool update %s %.3f:%f\n",rrdfile,$4,$1)}' \
	$i | sh

    # this and the way things are handled for overall.rrd is massively
    # kludgey and I would love to know a better way to do this
    rrdtool fetch ${basename}.rrd AVERAGE \
	--start $MIN_TIMESTAMP --end $MAX_TIMESTAMP | \
	awk -F ":" '{printf("%d %f\n",$1,$2)}' | grep -v -e "nan" -e "^0" >> kitsink

done

echo Performing overall summary computations

# find some overall boundaries.  at some point we should build this up
# based on what we were doing one file at a time above
    awk -F "," -f mins_maxes.awk ${prefix}*.out > minsn

    . ./minsn

# echo "MAX_INTERVAL $MAX_INTERVAL MIN_TIMESTAMP $MIN_TIMESTAMP MAX_TIMESTAMP $MAX_TIMESTAMP"
    LENGTH=`expr $MAX_TIMESTAMP - $MIN_TIMESTAMP`
    WIDTH=$LENGTH
    if [ $WIDTH -lt 800 ]
    then
	WIDTH=800
    fi
    SIZE="-w $WIDTH -h 400"


# ok time for the overall results
# by now all the large intervals have been dealt with so we do not
# have to use MAX_INTERVAL

rrdtool create ${prefix}_overall.rrd --step 1 --start `expr $MIN_TIMESTAMP - 1` \
    DS:mbps:GAUGE:2:U:U RRA:AVERAGE:0.5:1:$LENGTH

for i in `seq $MIN_TIMESTAMP $MAX_TIMESTAMP`
do
    SUM=`grep $i kitsink | awk '{sum += $2}END{print sum}'`
    rrdtool update ${prefix}_overall.rrd ${i}:$SUM
done

# get out labels set correctly
UNITS="bits/s"
MULTIPLIER="1000000"
DIRECTION="Bidirectional"
case $prefix in
    *pps* | *tps* ) 
	UNITS="Trans/s"
	MULTIPLIER="1"
	;;
    *inbound* )
	DIRECTION="Inbound"
	;;
    *outbound* )
	DIRECTION="Outbound"
	;;
esac


# now graph it
rrdtool graph ${prefix}_overall.png --start $MIN_TIMESTAMP --end $MAX_TIMESTAMP \
    $SIZE \
    --imgformat PNG \
    --font DEFAULT:0:Helvetica \
    -t "Overall ${1%.log}" \
    -v "$DIRECTION $UNITS" \
    DEF:foo=${prefix}_overall.rrd:mbps:AVERAGE \
    CDEF:bits=foo,$MULTIPLIER,\* \
    $VRULES \
    LINE2:bits#00FF0080:"$UNITS"

# now we can do the individual run graphs using the same x axis limits
# as the overall graph
for i in ${prefix}*.out
do
    basename=${i%\.out}
    rrdtool graph ${basename}.png --start $MIN_TIMESTAMP --end $MAX_TIMESTAMP \
	$SIZE \
	--imgformat PNG \
	--font DEFAULT:0:Helvetica \
	-t "$basename ${1%.log}" \
	-v "$DIRECTION $UNITS" \
	DEF:foo=${basename}.rrd:mbps:AVERAGE \
        CDEF:bits=foo,$MULTIPLIER,\* \
	$VRULES \
	LINE2:bits#00FF0080:"$UNITS"

done