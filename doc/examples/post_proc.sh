#!/bin/bash

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

#echo "MAX_INTERVAL $MAX_INTERVAL MIN_TIMESTAMP $MIN_TIMESTAMP MAX_TIMESTAMP $MAX_TIMESTAMP"
    LENGTH=`expr $MAX_TIMESTAMP - $MIN_TIMESTAMP`
    SIZE="-w $LENGTH -h 400"


    # ooh, rick learns how to strip a suffix
    basename=${i%\.out}
#    echo "Post-processing ${basename}"

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
    DS:mbps:GAUGE:1:U:U RRA:AVERAGE:0.5:1:$LENGTH

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

# find the interval with the highest AVERAGE.  we can use the
# timestamps in the vrules file to check this.  while we are doing so,
# might as well find the average, minimum and maximum for each
# interval and we will chart the interval averages with some
# reasonable transparancy.  if someone wants to chart the interval
# mins and maxes hopefully it will be fairly clear what to do

rrdtool create ${prefix}_intervals.rrd --step 1 \
    --start `expr $MIN_TIMESTAMP - 1` \
    DS:avg:GAUGE:1:U:U RRA:AVERAGE:0.5:1:$LENGTH \
    DS:min:GAUGE:1:U:U RRA:AVERAGE:0.5:1:$LENGTH \
    DS:max:GAUGE:1:U:U RRA:AVERAGE:0.5:1:$LENGTH

i=0
AVG=0
end=`expr $NUM_VRULES - 1`
while [ $i -lt $end ]
do
    start=`expr ${VRULE_TIME[$i]} + 1`
    j=`expr $i + 1`
    endtime=`expr ${VRULE_TIME[$j]} - 1`
    avgminmax=`rrdtool graph /dev/null --start $start --end $endtime \
	DEF:foo=${prefix}_overall.rrd:mbps:AVERAGE \
	VDEF:avg=foo,AVERAGE \
	VDEF:min=foo,MINIMUM \
	VDEF:max=foo,MAXIMUM \
	PRINT:avg:"%6.2lf" \
	PRINT:min:"%6.2lf" \
	PRINT:max:"%6.2lf" | sed 1d `
    # there is probably some clever way to do this without spawning
    # processes but I guess I'm just a fan of stone knives and
    # bearskins
    avg=`echo $avgminmax | awk '{print int($1)}'`
    min=`echo $avgminmax | awk '{print int($2)}'`
    max=`echo $avgminmax | awk '{print int($3)}'`
#    echo "Updating intervals from $start to $endtime with $avg $min $max"
    for k in `seq $start $endtime`
    do
	rrdtool update ${prefix}_intervals.rrd $k:$avg:$min:$max
    done
    if [ $avg -gt $AVG ]
    then
	peakintvid=`expr $i / 2`
	peakintvid=`expr $peakintvid + 1`
	maxstart=$start
	maxend=$endtime
	AVG=$avg
	MIN=$min
	MAX=$max
    fi
    i=`expr $i + 2`
done

# multiply it by the MULTIPLIER
AVG=`expr $AVG \* $MULTIPLIER`
MIN=`expr $MIN \* $MULTIPLIER`
MAX=`expr $MAX \* $MULTIPLIER`

# now graph it.  if you want the min and max on the graph then add
#    HRULE:${MIN}#0F0F0F:"Minimum of peak interval is $MIN" \
#    HRULE:${MAX}#0000FF:"Maximum of peak interval is $MAX" \
#    HRULE:${AVG}#0000FF80:"Average of peak interval (${peakintvid}) is $AVG" \
# to the rrdtool command though it can make the chart rather busy

rrdtool graph ${prefix}_overall.png \
    --start $MIN_TIMESTAMP --end $MAX_TIMESTAMP \
    $SIZE \
    --imgformat PNG \
    --font DEFAULT:0:Helvetica \
    -t "Overall ${1%.log}" \
    -v "$DIRECTION $UNITS" \
    DEF:foo=${prefix}_overall.rrd:mbps:AVERAGE \
    CDEF:bits=foo,$MULTIPLIER,\* \
    $VRULES \
    LINE2:bits#00FF0080:"$UNITS" > /dev/null \
    DEF:bar=${prefix}_intervals.rrd:avg:AVERAGE \
    CDEF:intvl=bar,$MULTIPLIER,\* \
    LINE2:intvl#0F0F0F40:"Interval average. Peak of $AVG during interval ${peakintvid}."

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
	LINE2:bits#00FF0080:"$UNITS" > /dev/null

done
echo "Average of peak interval is $AVG $UNITS from $maxstart to $maxend"
echo "Minimum of peak interval is $MIN $UNITS from $maxstart to $maxend"
echo "Maximum of peak interval is $MAX $UNITS from $maxstart to $maxend"
