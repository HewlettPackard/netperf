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


# set -x
SAMPLE_TIMES="2 5 10 20 30 45 60"
SAMPLE_TIMES="60 45 30 20 10 5 2"
FLOW_RATES="0 200 2000 20000 200000"
NETPERF="/home/raj/netperf2_trunk/src/netperf"

# these will be switch-specific
CPMIB="SFLOW-MIB::sFlowCpInterval.11.1.3.6.1.2.1.2.2.1.1.27.1"
FSMIB="SFLOW-MIB::sFlowFsPacketSamplingRate.11.1.3.6.1.2.1.2.2.1.1.27.1"
SFLOW_RRD="/tmp/counters/192.168.1.1/1/27/generic.rrd"

for sample in $SAMPLE_TIMES
do

  LENGTH="300"
  XGRID="--x-grid SECOND:5:SECOND:30:SECOND:30:0:%X"
  # we want at least 10 sFlow samples
  SAMPLES=`expr $LENGTH / $sample`
  if [ $SAMPLES -lt 10 ]; then
    LENGTH=`expr 10 \* $sample`
    XGRID="--x-grid SECOND:10:SECOND:60:SECOND:60:0:%X"
  fi

  # configure the switch for the counter sampling interval
  snmpset 192.168.1.1 $CPMIB i $sample
  TWOXSAMPLE=`expr $sample \* 2`
  for flow in $FLOW_RATES
  do
    # configure the switch for the flow sampling rate which is one per
    # N on average, with some randomization if adhering to the sFlow
    # spec.
    snmpset 192.168.1.1 $FSMIB i $flow

    # setup some variables
    FILE_BASE="sample_${sample}_flow_${flow}"
    NETPERF_BASE="raw_netperf_${FILE_BASE}"
    NETPERF_RAW="${NETPERF_BASE}.out"
    NETPERF_RRD="${NETPERF_BASE}.rrd"

    # provide an indication of forward progress and status
    echo "sample rate $sample flow rate 1 in $flow"

    # start top
    top -b -i > "top_${FILE_BASE}.out" &
    TOP_PID=$!

    # run our netperf test long enough to make sure we have as many
    # sFlow samples as we wish even after we later skip the first two.
    $NETPERF -t UDP_STREAM -H 192.168.1.3 -l `expr $LENGTH + $TWOXSAMPLE`\
      -D 1 -c -C -- -m 1472 -s 64K -S 64K > $NETPERF_RAW

    # stop top
    kill -HUP $TOP_PID

    # prepare to shove the interim results into an RRD
    grep Interim $NETPERF_RAW > interims.tmp

    # over what time interval do we have netperf interim results?
    START=`head -1 interims.tmp | awk '{printf("%d",$10)}'`
    END=`tail -1 interims.tmp | awk '{printf("%d",$10)}'`

    # create an rrd for netperf starting at the beginning of that interval
    rrdtool create netperf.rrd --step 1 --start $START \
      DS:mbps:GAUGE:2:U:U RRA:AVERAGE:0.5:1:${LENGTH}

    # shove those interim results into the netperf rrd
    awk '{printf("rrdtool update netperf.rrd %f:%f\n",$10,$3)}' \
      interims.tmp | sh
    mv netperf.rrd $NETPERF_RRD

    # extract the data from the sflow rrd to save it for posterity
    rrdtool fetch $SFLOW_RRD AVERAGE --start $START --end $END \
      > sflow_${FILE_BASE}.fetch

    # now make the graph, trim-off the first two sFlow counter samples
    # for better automagic scaling. the magic multiplier is how to go
    # from mbps from netperf, which counts only payload, to octets on
    # the wire assuming 8 bytes of UDP header 20 bytes of IP, 14 bytes
    # of ethernet, 4 bytes of FCS and 1472 bytes of user payload
    START=`expr $START + $TWOXSAMPLE`

    # we don't like rrdtool's autoscaling
    YMIN=`rrdtool graph /dev/null --start $START --end $END \
          DEF:foo=${SFLOW_RRD}:ifOutOctets:AVERAGE \
	  CDEF:bar=foo,0.98,\* \
	  VDEF:bing=bar,MINIMUM \
	  PRINT:bing:"%6.2lf" | sed 1d`

    YMAX=`rrdtool graph /dev/null --start $START --end $END \
          DEF:foo=${SFLOW_RRD}:ifOutOctets:AVERAGE \
	  CDEF:bar=foo,1.02,\* \
	  VDEF:bing=bar,MAXIMUM \
	  PRINT:bing:"%6.2lf" | sed 1d`

    # also consider the neterf results
    NMIN=`rrdtool graph /dev/null --start $START --end $END \
          DEF:foo=${NETPERF_RRD}:mbps:AVERAGE \
	  CDEF:baz=foo,128906.25,\* \
	  CDEF:bar=baz,0.98,\* \
	  VDEF:bing=bar,MINIMUM \
	  PRINT:bing:"%6.2lf" | sed 1d`

    NMAX=`rrdtool graph /dev/null --start $START --end $END \
          DEF:foo=${NETPERF_RRD}:mbps:AVERAGE \
	  CDEF:baz=foo,128906.25,\* \
	  CDEF:bar=baz,1.02,\* \
	  VDEF:bing=bar,MAXIMUM \
	  PRINT:bing:"%6.2lf" | sed 1d`

    # I am certain someone will say dude use perl when they see this
    INMIN=`echo $NMIN | awk '{printf("%d",$1)}'`
    INMAX=`echo $NMAX | awk '{printf("%d",$1)}'`
    IYMIN=`echo $YMIN | awk '{printf("%d",$1)}'`
    IYMAX=`echo $YMAX | awk '{printf("%d",$1)}'`

    # we wont sweat the fractional part
    if [ $INMIN -lt $IYMIN ]; then
	YMIN=$NMIN
    fi

    if [ $INMAX -gt $IYMAX ]; then
        YMAX=$NMAX
    fi

    GRAPH="graph_${FILE_BASE}.svg"
    SIZE="-w 1000 -h 400"
    rrdtool graph $GRAPH --start $START --end $END \
      --imgformat SVG \
      -t "sFlow counter accuracy, average flow sample rate 1 in ${flow}" \
      -v "Octets/s" \
      $XGRID $SIZE --full-size-mode \
      --upper-limit $YMAX \
      --lower-limit $YMIN \
      --rigid \
      --alt-y-grid \
      DEF:foo=${NETPERF_RRD}:mbps:AVERAGE \
      DEF:bar=${SFLOW_RRD}:ifOutOctets:AVERAGE \
      CDEF:baz=foo,128906.25,\* \
      HRULE:124019607.84#000000:"Theoretical link-rate" \
      LINE2:baz#FF0000:"netperf+headers at 1s intvl" \
      LINE2:bar#00FF0080:"sFlow counters at ${sample}s intvl"
  done
done

