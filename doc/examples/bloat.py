#!/usr/bin/python -u

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


import os
import glob
import time
import shlex
import signal
import subprocess
import argparse
import rrdtool
import math

netperf_maertss = []
netperf_rrs = []
destinations = []

def launch_netperf(output_file,
                   error_file=None,
                   destination=None,
                   test=None,
                   length=None,
                   frequency=None,
                   units=None,
                   test_specific=None):

    arguments = ['netperf']
    if test:
        arguments.append('-t')
        arguments.append('%s' % test)
    if destination:
        arguments.append('-H')
        arguments.append('%s' % destination)
    if not length == None:
        arguments.append('-l')
        arguments.append('%d' % length)
    if frequency:
        arguments.append('-D')
        arguments.append('-%f' % frequency)
    else:
        arguments.append('-D')
        arguments.append('-0.25')
    if units:
        arguments.append('-f')
        arguments.append('%s' % units)

    # we will want some test-specific options
    arguments.append('--')

    if test_specific:
        arguments += shlex.split(test_specific)

    print "going to start netperf with '%s'" % arguments
    netperf = subprocess.Popen(arguments,
                               stdout=output_file)
    return netperf

def launch_rr(destination=None,
              length=None,
              frequency = None):
    rrs = []
    instance = 0
    output_file = open('netperf_rr_%.5d.out' % instance, 'w')
    error_file = open('netperf_rr_%.5d.err' % instance, 'w')

    rrs.append((launch_netperf(output_file,
                               error_file,
                               destination=destination,
                               length=length,
                               frequency=frequency,
                               test="%s_rr" % args.rr_protocol,
                               test_specific=args.rr_arguments),False))


    return rrs

def launch_streams(count,
                   destination=None,
                   length=None,
                   frequency=None):
    streams = []
    for instance in xrange(0,count):
        output_file = open('netperf_stream_%.5d.out' % instance, 'w')
        error_file = open('netperf_stream_%.5d.err' % instance, 'w')
        streams.append((launch_netperf(output_file,
                                       error_file=error_file,
                                       length=length,
                                       frequency=frequency,
                                       destination=destination,
                                       test="%s_stream" % args.stream_protocol,
                                       test_specific=args.stream_arguments),False))
    return streams

def launch_maerts(count,
                  destination=None,
                  length=None,
                  frequency=None):
    maerts = []
    for instance in xrange(0,count):
        output_file = open('netperf_maerts_%.5d.out' % instance, 'w')
        error_file = open('netperf_maerts_%.5d.err' % instance, 'w')
        maerts.append((launch_netperf(output_file,
                                      error_file=error_file,
                                      length=length,
                                      frequency=frequency,
                                      destination=destination,
                                      test="tcp_maerts",
                                      test_specific="-s 1M -S 1M -m 64K,64K -M 64K,64K"),True))

    return maerts

def await_netperfs_termination(netperfs):
    for netperf in netperfs:
        while netperf[0].poll() == None:
            time.sleep(1)

def terminate_netperfs(netperfs):
    for netperf in netperfs:
        print "Terminating a netperf with a SIGALRM and doubletap is %s" % netperf[1]
        netperf[0].send_signal(signal.SIGALRM)
        if netperf[1] == True:
            # This must be a test that requires a double-tap to
            # terminate. For example a TCP_MAERTS test which will
            # otherwise remain stuck waiting for a response from the
            # remote, which will not be coming for a Very Long Time
            # (tm)
            netperf[0].send_signal(signal.SIGTERM)

    await_netperfs_termination(netperfs)


# read the tea-leaves of the direction(s) given by the user and return
# things accordingly.  chunks will be the number of chunks into which
# we will break-up the test. do_stream will say whether we need to do
# a stream (outbound) test, do_maerts will say whether we need to do a
# maerts (inbound) test
def parse_direction(directions):

    do_stream = False
    do_maerts = False
    for direction in directions.lower().split(','):
        if (direction == 'inbound') or (direction == 'maerts'):
            do_maerts = True
        elif (direction == 'outbound') or (direction == 'stream'):
            do_stream = True
        elif (direction == 'both') or (direction == 'bidir'):
            do_stream = True
            do_maerts = True

    chunks = 1
    if do_stream and do_maerts:
        chunks = 5
    elif do_stream or do_maerts:
        chunks = 3

    return (chunks, do_stream, do_maerts)

def open_rrd(basename,start_time,end_time,max_interval):
#    print "Opening %s.rrd with start time %d and end time %d" % (basename,int(start_time),int(end_time))

    data_sources = [ 'DS:units:GAUGE:%d:U:U' % max_interval ]
    rra = [ 'RRA:AVERAGE:0.5:1:%d' % ((int(end_time) - int(start_time)) + 1) ]

    rrdtool.create(basename + ".rrd",
                   '--step', '1',
                   '--start', str(int(start_time)),
                   data_sources,
                   rra )

def update_heartbeat(basename,heartbeat):
    print "Updating heartbeat with %d" % heartbeat
    rrdtool.tune(basename + ".rrd",
                 '--heartbeat', 'units:%d' % heartbeat)

def update_rrd(basename,value,timestamp):
#    print "update_rrd"
    rrdtool.update(basename + '.rrd',
                   '%.3f:%f' % (timestamp, value))

def add_to_ksink(basename,start_time,end_time,ksink):
    ((first, last, step),name,results) = rrdtool.fetch(basename + ".rrd",
                                                       'AVERAGE',
                                                       '--start', str(int(start_time)),
                                                       '--end', str(int(end_time)))
#    print "First %d last %d step %d results %d" % (first, last, step, len(results))
    for key,result in enumerate(results,first):
        if result[0] and key in ksink:
            ksink[key] += float(result[0])
        else:
            if result[0]:
                # well, then add the blame thing to the sink
                ksink[key] = float(result[0])

unit_conversion = {
    'Bytes/s' : 8.0,
    'KBytes/s' : 8.0 * 1024.0,
    'MBytes/s' : 8.0 * 1024.0 * 1024.0,
    'GBytes/s' : 8.0 * 1024.0 * 1024.0 * 1024.0,
    'Trans/s' : 1.0,
    '10^0bits/s' : 1.0,
    '10^3bits/s' : 1000.0,
    '10^6bits/s' : 1000000.0,
    '10^9bits/s' : 1000000000.0,
}

def convert_units(raw_result,raw_units):
    if raw_units == 'Bytes/s' : return raw_result * 8.0
    if raw_units == 'KBytes/s' : return raw_result * 8.0 * 1024.0
    if raw_units == 'MBytes/s' : return raw_result * 8.0 * 1024.0 * 1024.0
    if raw_units == 'GBytes/s' : return raw_result * 8.0 * 1024.0 * 1024.0 * 1024.0
    if raw_units == 'Trans/s' : return 1.0 / raw_result
    if raw_units == '10^0bits/s' : return raw_result * 1.0
    if raw_units == '10^3bits/s' : return raw_result * 1000.0
    if raw_units == '10^6bits/s' : return raw_result * 1000000.0
    if raw_units == '10^9bits/s' : return raw_result * 1000000000.0
    return raw_units

def process_result(basename, raw_results, end_time, ksink):
    first_result = True
    have_result = False
    interim_result=0.0
    interim_units="Trans/s"
    interim_interval=1.0
    interim_end=0.0
    max_interval=1



    for raw_result in raw_results:
#        print "raw_result is %s" % raw_result
        if "Interim result:" in raw_result:
            # human format
            fields = raw_result.split()
            interim_result=float(fields[2])
            interim_units=fields[3]
            interim_interval=float(fields[5])
            interim_end=float(fields[9])
            have_result=True
        elif "NETPERF_INTERIM_RESULT" in raw_result:
            # keyval first line
            interim_result=float(raw_result.split('=')[1])
            have_result=False
        elif "NETPERF_UNITS" in raw_result:
            # keyval second line
            interim_units=raw_result.split('=')[1]
            have_result=False
        elif "NETPERF_INTERVAL" in raw_result:
            # keyval keyval third line
            interim_interval=float(raw_result.split('=')[1])
            have_result=False
        elif "NETPERF_ENDING" in raw_result:
            # keyval keyval fourth line
            interim_end=float(raw_result.split('=')[1])
            have_result=True
        else:
            # csv, but we are interested only in those lines with four
            # fields, three commas.  if someone happens to ask for
            # four values from the omni-output selector this may not
            # work so well but we can deal with that when it is known
            # to be a problem.
            fields = raw_result.split(',')
            if len(fields) == 4:
                interim_result = float(fields[0])
                interim_units = fields[1]
                interim_interval = float(fields[2])
                interim_end = float(fields[3])
                have_result = True
            else:
                have_result = False
 
        if first_result and have_result:
            # we could use the overal start time, but using the first
            # timestamp for this instance may save us some space in
            # the rrdfile.  we do though want to subtract the
            # interim_interval from that timestamp to give us some
            # wriggle-room - particularly if the interval happens to
            # end precisely on a step boundary...
 #           print basename, interim_end, interim_interval, end_time, max_interval
            open_rrd(basename,
                     interim_end-interim_interval,
                     end_time,
                     max_interval)
            first_timestamp = interim_end
            first_result = False
#            print "First entry for %s is %f at time %f" % (basename, interim_result,interim_end)

        # perhaps one of these days, once we know that the rrdtool
        # bits can handle it, we will build a big list of results and
        # feed them en mass. until then we will dribble them one at a
        # time
        if have_result:
            if int(math.ceil(interim_interval)) > max_interval:
                max_interval = int(math.ceil(interim_interval))
                update_heartbeat(basename,max_interval)

            update_rrd(basename,convert_units(interim_result,interim_units),interim_end)
            have_result = False

    last_timestamp = interim_end
#    print "First timestamp for this instance %f last %f" % (first_timestamp,last_timestamp)
    return first_timestamp, last_timestamp

def process_result_files(prefix,start_time,end_time,ksink):
    print "Prefix is %s" % prefix
    min_timestamp = 9999999999.9
    results_list = glob.glob(prefix+"*.out")

    for result_file in results_list:
        print "Processing file %s" % result_file
        basename = result_file.replace(".out","")
        raw_results = open(result_file,"r")
        first_timestamp, last_timestamp = process_result(basename,
                                                         raw_results,
                                                         end_time,
                                                         ksink)
        # we have to check each time because we may not be processing
        # the individual results files in order
        min_timestamp = min(min_timestamp,first_timestamp)
        # OK, now we get the massaged results
        add_to_ksink(basename,first_timestamp,last_timestamp,ksink)

#    print "For %s min_timestamp is %s" % (prefix, min_timestamp)

    return min_timestamp

def generate_overall(prefix,start_time,end_time,ksink):
    overall = prefix + "overall"
    open_rrd(overall,start_time-1,end_time,1)

#    print "Starting time %s ending time %s" % (start_time,end_time)
    # one cannot rely on the enumeration of a dictionary being in key
    # order and I do not know how to sort one, so we will simply walk
    # the possible keys based on the start_time and end_time and if we
    # find that key in the kitchen sink, we will use the value to
    # update the overall rrd.
    prevkey = -1
    for key in xrange(int(start_time),int(end_time)+1):
        if key in ksink:
            try:
                update_rrd(overall,ksink[key],key)
                prevkey = key
            except Exception as e:
                print "Update to %s failed for %d, previous %d %s" % (overall, key, prevkey, e)

def graph_overall(prefix, start_time, end_time, direction, units):

    length = int(end_time) - int(start_time)

    rrdtool.graph(prefix + "overall.svg", '--imgformat', 'SVG',
                  '--start', str(int(start_time)),
                  '--end', str(int(end_time)),
                  '-w','%d' % max(800,length),'-h','400',
                  '--right-axis', '1:0',
                  '--font', 'DEFAULT:0:Helvetica',
                  '-t', 'Overall %s' % prefix,
                  '-v', '%s %s' % (direction,units),
                  'DEF:foo=%soverall.rrd:units:AVERAGE' % prefix,
                  'LINE2:foo#00FF0080:%s' % units)

# one of these days I should go more generic
def process_rr(start_time, end_time):
    print "Processing rr from %d to %d" % (start_time,end_time)
    ksink=dict()
    process_result_files("netperf_rr_", start_time, end_time, ksink)
    generate_overall("netperf_rr_", start_time, end_time, ksink)
    graph_overall("netperf_rr_", start_time, end_time, "trans", "trans")
    return min_max_avg_from_rrd("netperf_rr_overall.rrd",
                                start_time,
                                end_time)

def process_streams(start_time, end_time):
    print "Processing streams from %d to %d" % (start_time,end_time)
    ksink=dict()
    process_result_files("netperf_stream_", start_time, end_time, ksink)
    generate_overall("netperf_stream_", start_time, end_time, ksink)
    graph_overall("netperf_stream_", start_time, end_time, "streams", "bits")
    return min_max_avg_from_rrd("netperf_stream_overall.rrd",
                                start_time,
                                end_time)

def process_maerts(start_time,end_time):
    print "Processing maerts from %d to %d" % (start_time,end_time)
    ksink=dict()
    process_result_files("netperf_maerts_", start_time, end_time, ksink)
    generate_overall("netperf_maerts_", start_time, end_time, ksink)
    graph_overall("netperf_maerts_", start_time, end_time, "maerts", "bits")
    return min_max_avg_from_rrd("netperf_maerts_overall.rrd",
                                start_time,
                                end_time)

def min_max_avg_from_rrd(rrdfile, start_time, end_time) :

    result = rrdtool.graph('/dev/null',
                           '--start', str(int(start_time)),
                           '--end', str(int(end_time)),
                           'DEF:foo=%s:units:AVERAGE' % rrdfile,
                           'VDEF:avg=foo,AVERAGE',
                           'VDEF:min=foo,MINIMUM',
                           'VDEF:max=foo,MAXIMUM',
                           'PRINT:avg:"%6.20lf"',
                           'PRINT:min:"%6.20lf"',
                           'PRINT:max:"%6.20lf"')[2]

    iavg = float(result[0].strip('"'))
    imin = float(result[1].strip('"'))
    imax = float(result[2].strip('"'))

    return  imin, iavg, imax

# the meat of the script itself

parser = argparse.ArgumentParser(description="Attempt to measure how much bufferbloat exists between this system and another.")

parser.add_argument("destination", help="Destination of the test.")
parser.add_argument("-a","--annotation",default=None,
                    help="Annotation to add to chart titles")
parser.add_argument("-c","--concurrency", type=int,
                    help="Number of concurrent bulk transfer streams.")
parser.add_argument("-l","--length", type=int, help="Desired overall runtime.")
parser.add_argument("-d","--direction",
                    help="Which in which direction(s) should the bulk transfers run.")
parser.add_argument("--stream-protocol",default="TCP",
                    help="Which protocol to use, TCP or UDP (default TCP)")
parser.add_argument("--rr-protocol",default="TCP",
                    help="Which protocol to use, TCP or UDP (default TCP)")
parser.add_argument("-r","--rr-arguments",default="-r 1",
                    help="Test-specific arguments to use for RR tests")
parser.add_argument("-s","--stream-arguments",
                    default="-s 1M -S 1M -m 64K,64K -M 64K,64K",
                    help="Test-specific arguments to use for STREAM tests")
parser.add_argument("-m","--maerts-arguments",
                    default="-s 1M -S 1M -m 64K,64K -M 64K,64K",
                    help="Test-specific arguments to use for MAERTS tests")

args = parser.parse_args()

print "Destination is " + args.destination
if not args.concurrency:
    args.concurrency = 1
if args.concurrency < 0:
    args.concurrency = 1
if not args.length:
    args.length = 240
if not args.direction:
    args.direction = "outbound"

(chunks, do_streams, do_maerts) = parse_direction(args.direction)

print "There are %d chunks, do_stream is %s and do_maerts is %s" % (chunks, do_streams,do_maerts)

chunk_time = args.length / chunks
#if chunk_time < 30:
#    chunk_time = 30
print "Each time chunk will be %d seconds long" % chunk_time

times=dict()

# launch the rr test here
times['rr_start']=int(time.time())
netperf_rr = launch_rr(destination=args.destination,length=3600)
time.sleep(chunk_time)

if do_streams:
    times['streams_start']=int(time.time())
    netperf_streams = launch_streams(args.concurrency,destination=args.destination,length=3600)
    print "Sleeping for %d" % chunk_time
    time.sleep(chunk_time)
else:
    netperf_streams = []

if do_maerts:
    times['maerts_start']=int(time.time())
    netperf_maerts = launch_maerts(args.concurrency,destination=args.destination,length=3600)
    print "Sleeping for %d" % chunk_time
    time.sleep(chunk_time)
else:
    netperf_maerts = []

if netperf_streams:
    terminate_netperfs(netperf_streams)
    times['streams_stop']=int(time.time())
    if do_maerts:
        print "Sleeping for %d" % chunk_time
        time.sleep(chunk_time)

if netperf_maerts:
    terminate_netperfs(netperf_maerts)
    times['maerts_stop']=int(time.time())

print "Sleeping for %d" % chunk_time
time.sleep(chunk_time)

# terminate the rr test here
terminate_netperfs(netperf_rr)
times['rr_stop']=int(time.time())

# ok, now we have to post-process things

(rr_min, rr_max, rr_avg) = process_rr(times['rr_start'],times['rr_stop'])

print "rr", rr_min, rr_max, rr_avg
rr_specs = [ 'DEF:rr=netperf_rr_overall.rrd:units:AVERAGE',
             'LINE2:rr#00FF0080:TCP_RR Round-Trip Latency' ]

max_tput = 0.0
stream_specs = []
right_axis = []
if do_streams:
    (streams_min, streams_max, streams_avg) = process_streams(times['streams_start'],times['streams_stop'])
    if (streams_max > max_tput) :
        max_tput = streams_max
        # why 2 *? because we want the latency to have twice the vertical
        # space as throughput

        print "rrs", rr_min, rr_max
        scale = (2 * max_tput) / rr_max
        print "scale",scale
        right_axis = [ '--right-axis', '%.20f:0' % scale,
                       '--right-axis-label', 'Bits per Second' ]

    print "streams", streams_min, streams_max, streams_avg

maerts_specs = []
if do_maerts:
    (maerts_min, maerts_max, maerts_avg) = process_maerts(times['maerts_start'],times['maerts_stop'])
    if (maerts_max > max_tput) :
        max_tput = maerts_max
        # why 2 *? because we want the latency to have twice the vertical
        # space as throughput
        scale = (2 * max_tput) / rr_max
        print "scale",scale
        right_axis = [ '--right-axis', '%.20f:0' % scale,
                       '--right-axis-label', 'Bits per Second' ]

    print "maerts", maerts_min, maerts_max, maerts_avg

# why not above? because we need the scale value - rrdtool doesn't
# have "real" two-axis support...

if do_streams:
    stream_specs = [ 'DEF:streams=netperf_stream_overall.rrd:units:AVERAGE',
                     'CDEF:sstreams=streams,%.20f,/' % scale,
                     'LINE2:sstreams#0000FFF0:Throughput to %s' % args.destination ]
if do_maerts:
    maerts_specs = [ 'DEF:maerts=netperf_maerts_overall.rrd:units:AVERAGE',
                     'CDEF:smaerts=maerts,%.20f,/' % scale,
                     'LINE2:smaerts#FF0000F0:Throughput from %s' % args.destination ]


length = int(times['rr_stop'] - times['rr_start'])

print "rr times",length, int(times['rr_start']),int(times['rr_stop'])

if args.annotation :
    title = 'Effect of bulk transfer on round-trip latency to %s : %s' % (args.destination, args.annotation)
else:
    print "don't have annotation"
    title = 'Effect of bulk transfer on latency to %s' % args.destination


rrdtool.graph('bloat2.svg', '--imgformat', 'SVG',
              '--start', str(int(times['rr_start'])),
              '--end', str(int(times['rr_stop'])),
              '--lower-limit', '0',
              '-t', title,
              '-v', 'Seconds',
              '-w', '%d' % max(800,length),
              '-h', '400',
              '--x-grid', 'SECOND:10:SECOND:60:SECOND:60:0:%X',
              rr_specs,
              stream_specs,
              maerts_specs,
              right_axis)

