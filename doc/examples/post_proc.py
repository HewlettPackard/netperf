#!/usr/bin/python
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


# This is a re-writing of post_proc.sh into Python.  Feel free to
# provide feedback on how to make it better - either better
# post-processing or better Python.  Keep in mind it is only the
# second Python script I have ever written :) raj 2012-06-25

# This script will run much faster than post-proc.sh does.  Some of
# that may be Python versus bash+awk+grep+rrdtool.  Much of that is
# undoubtedly from better alogorithms - not going through the data
# umpteen times.  For example, for a test which had up to 8 netperfs
# running and so 8 files to be post-processed:
#
#raj@tardy:~/netperf2_trunk/doc/examples$ time ./post_proc.py netperf_tps.log
#Prefix is netperf_tps
#Average of peak interval is 581554.430 Trans/s from 1340326282 to 1340326404
#Minimum of peak interval is 464912.610 Trans/s from 1340326282 to 1340326404
#Maximum of peak interval is 594025.670 Trans/s from 1340326282 to 1340326404
#
#real	0m0.450s
#user	0m0.430s
#sys	0m0.010s
#raj@tardy:~/netperf2_trunk/doc/examples$ time ./post_proc.sh netperf_tps.log
#Prefix is netperf_tps
#Performing overall summary computations
#Average of peak interval is 581460 Trans/s from 1340326283 to 1340326404
#Minimum of peak interval is 464913 Trans/s from 1340326283 to 1340326404
#Maximum of peak interval is 594026 Trans/s from 1340326283 to 1340326404
#
#real	0m16.873s
#user	0m0.500s
#sys	0m0.690s

import os
import sys
import glob
import math
import rrdtool
import argparse

def find_vrules(source):
    vrules = []
    interval_times = []
    interval_start = 0
    interval_end = 0
    netperf_count=0
    start_time=0.0
    end_time=0.0
    RED="FF0000"
    BLACK="000000"
    resumes=False

    for line in source:
        if "Starting netperfs on" in line:
            netperf_count += 1
        elif "Pausing" in line:
            fields = line.split()
            plural=''
            if netperf_count > 1:
                plural = 's'
            vrule = 'VRULE:%d#%s:%d netperf%s running' % (int(float(fields[5])),RED,int(fields[7]),plural)
            vrules.append(vrule)
            interval_start = int(float(fields[5]))
        elif "Resuming" in line:
            fields = line.split()
            if resumes:
                resume_text=''
            else:
                resume_text='Resuming ramp'

            vrule = "VRULE:%d#%s:%s" % (int(float(fields[2])),BLACK,resume_text)
            vrules.append(vrule)
            interval_end = int(float(fields[2]))
            interval_times.append((interval_start,interval_end))
            resumes=True
        elif "Starting netperfs at" in line:
            start_time = line.split()[3]
        elif "Netperfs started" in line:
            fields = line.split()
            vrule = 'VRULE:%d#%s:All %d netperfs running' % (int(float(fields[3])),RED,netperf_count)
            vrules.append(vrule)
            interval_start = int(float(fields[3]))
        elif "Netperfs stopping" in line:
            fields = line.split()
            vrule = 'VRULE:%d#%s:Rampdown started' % (int(float(fields[2]))-1,BLACK)
            vrules.append(vrule)
            interval_end = int(float(fields[2]))-1
            interval_times.append((interval_start,interval_end))
        elif "Netperfs stopped" in line:
            end_time = line.split()[2]

    return vrules,float(start_time),float(end_time),interval_times

def open_rrd(basename,start_time,end_time,max_interval):
#    print "Opening %s.rrd with start time %d and end time %d" % (basename,int(start_time),int(end_time))

    data_sources = [ 'DS:mbps:GAUGE:%d:U:U' % max_interval ]
    rra = [ 'RRA:AVERAGE:0.5:1:%d' % ((int(end_time) - int(start_time)) + 1) ]

    rrdtool.create(basename + ".rrd",
                   '--step', '1',
                   '--start', str(int(start_time)),
                   data_sources,
                   rra )
    return basename + ".rrd"

def update_heartbeat(basename,heartbeat):
#    print "Updating heartbeat with %d" % heartbeat
    rrdtool.tune(basename + ".rrd",
                 '--heartbeat', 'mbps:%d' % heartbeat)


def update_rrd(basename,value,timestamp):
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
                print "Key %d not in ksink" % key

def process_result(basename, raw_results, end_time, ksink):
    first_result = True
    have_result = False
    had_results = False
    interim_result=0.0
    interim_units="Trans/s"
    interim_interval=1.0
    interim_end=0.0
    max_interval=1



    for raw_result in raw_results:
#        print "Checking result %s" % raw_result
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
#            print "First entry for %s is %f at time %f" % (basename, interim_result,interim_end)
            open_rrd(basename,
                     interim_end-interim_interval,
                     end_time,
                     max_interval)
            first_timestamp = interim_end
            first_result = False

        if int(math.ceil(interim_interval)) > max_interval:
            max_interval = int(math.ceil(interim_interval))
            update_heartbeat(basename,max_interval)

        # perhaps one of these days, once we know that the rrdtool
        # bits can handle it, we will build a big list of results and
        # feed them en mass. until then we will dribble them one at a
        # time
        if have_result:
            #print "updating rrd with %s at %s" % (interim_result, interim_end)
            try:
                update_rrd(basename,interim_result,interim_end)
            except Exception as e:
                print "Update to %s with %s at %s failed with %s" % (basename,interim_result,interim_end,e)
            have_result = False
            had_results = True

    if had_results:
        last_timestamp = interim_end
 #       print "First timestamp for this instance %f last %f" % (first_timestamp,last_timestamp)
        return first_timestamp, last_timestamp
    else:
        return 0, 0

def process_result_files(prefix,start_time,end_time,ksink):
    print "Prefix is %s" % prefix
    min_timestamp = 9999999999.9
    results_list = glob.glob(prefix+"*.out")

    for result_file in results_list:
        basename = result_file.replace(".out","")
        raw_results = open(result_file,"r")
#        print "Processing file %s" % basename
        first_timestamp, last_timestamp = process_result(basename,
                                                         raw_results,
                                                         end_time,
                                                         ksink)
        if (first_timestamp != 0) and (last_timestamp != 0):
            # we have to check each time because we may not be processing
            # the individual results files in order
            min_timestamp = min(min_timestamp,first_timestamp)
            # OK, now we get the massaged results
            add_to_ksink(basename,first_timestamp,last_timestamp,ksink)

#    print "For %s min_timestamp is %s" % (prefix, min_timestamp)
    return min_timestamp

def generate_overall(prefix,start_time,end_time,ksink):
    overall = prefix + "_overall"
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

def overall_min_max_avg(prefix,start_time,end_time,intervals):

    max_average = 0.0
    min_graph_interval = 60
    length = int(end_time) - int(start_time)

    # (iavg, imin, imax, istart, iend)
    # first will be overwritten with peak when known
    results_list=[(0.0, 0.0, 0.0, 0, 0)]

    rrdtool.create(prefix + "_intervals.rrd",
                   '--step', '1',
                   '--start', str(int(start_time) - 1),
                   'DS:avg:GAUGE:1:U:U', 'RRA:AVERAGE:0.5:1:%d' % int(length),
                   'DS:min:GAUGE:1:U:U', 'RRA:AVERAGE:0.5:1:%d' % int(length),
                   'DS:max:GAUGE:1:U:U', 'RRA:AVERAGE:0.5:1:%d' % int(length))

    for id, interval in enumerate(intervals,start=1):
        # something to customize the x-axis labling
        graph_interval = interval[1] - interval[0]
        if (graph_interval > 0 and graph_interval < min_graph_interval):
            min_graph_interval = graph_interval

        start = interval[0] + 1
        # take care if there was a long delay between when we started
        # netperf and when we started getting results out of it.
        if (start < start_time):
            start = int(start_time + 1)
        end = interval[1] - 1
        # if we have a bogus interval, skip it
        if (start >= end):
            continue
        # we have no interest in the size of the graph (the first two
        # items in the list) so slice just the part of interest
        result = rrdtool.graph('/dev/null',
                               '--start', str(start),
                               '--end', str(end),
                               'DEF:foo=%s_overall.rrd:mbps:AVERAGE' % prefix,
                               'VDEF:avg=foo,AVERAGE',
                               'VDEF:min=foo,MINIMUM',
                               'VDEF:max=foo,MAXIMUM',
                               'PRINT:avg:"%6.2lf"',
                               'PRINT:min:"%6.2lf"',
                               'PRINT:max:"%6.2lf"')[2]
#        print "from %d to %d iavg, imin, imax are %s" % (start,end,result)
        iavg = float(result[0].strip('"'))
        imin = float(result[1].strip('"'))
        imax = float(result[2].strip('"'))
        results_list.append((iavg, imin, imax, start, end))

        for time in xrange(start,end+1):
            rrdtool.update(prefix + "_intervals.rrd",
                           '%d:%f:%f:%f' % (time, iavg, imin, imax))
        if iavg > max_average:
            peak_interval_id = id;
            peak_interval_start = start
            peak_interval_end = end
            max_average = iavg
            max_minimum = imin
            max_maximum = imax


    results_list[0]= (max_average, max_minimum, max_maximum, peak_interval_start, peak_interval_end)

    return peak_interval_id, min_graph_interval, results_list

def units_et_al_by_prefix(prefix):
    units = "bits/s"
    multiplier = "1000000"
    direction = "Bidirectional"

    if ("pps" in prefix) or ("tps" in prefix):
        units = "Trans/s"
        multiplier = "1"
    elif "inbound" in prefix:
        direction = "Inbound"
    elif "outbound" in prefix:
        direction = "Outbound"

    return units, multiplier, direction

def graph_overall(prefix,start_time,end_time,vrules,peak_interval_id=None,peak_average=0.0,major_interval=60,annotation=None,override=None):

    length = int(end_time) - int(start_time)

    xgrid_setting = 'SECOND:%d:SECOND:%d:SECOND:%d:0:%%X' % (major_interval/2, major_interval, major_interval)

    units, multiplier, direction = units_et_al_by_prefix(prefix)

#    print units,multiplier,direction
#    print "Vrules",vrules

    interval_specs = []
    if peak_interval_id:
        interval_specs = [ 'DEF:bar=%s_intervals.rrd:avg:AVERAGE' % prefix,
                           'CDEF:intvl=bar,%s,*' % multiplier,
                           'LINE2:intvl#0F0F0F40:Interval average. Peak of %.3f during interval %d' % (peak_average, peak_interval_id) ]

    title = "Overall %s" % (override if override else prefix)
    if annotation:
        title = "Overall %s %s" % ((override if override else prefix), annotation)

    rrdtool.graph(prefix + "_overall.svg", '--imgformat', 'SVG',
                  '--start', str(int(start_time)),
                  '--end', str(int(end_time)),
                  '-w','%d' % max(800,length),'-h','400',
                  '--right-axis', '1:0',
                  '--x-grid', xgrid_setting,
                  vrules,
                  '--font', 'DEFAULT:0:Helvetica',
                  '-t', title,
                  '-v', '%s %s' % (direction,units),
                  'DEF:foo=%s_overall.rrd:mbps:AVERAGE' % prefix,
                  'CDEF:bits=foo,%s,*' % multiplier,
                  'LINE2:bits#00FF0080:%s' % units,
                  interval_specs)

def graph_individual(prefix,start_time,end_time,vrules,major_interval=60,annotation=None,override=None):

    units, multiplier, direction = units_et_al_by_prefix(prefix)

    length = int(end_time) - int(start_time)


    for individual in glob.glob(prefix+"*.rrd"):
        basename = individual.strip(".rrd")

        title = "%s %s" % (basename,(override if override else prefix))
        if annotation:
            title = "%s %s %s" % (basename, (override if override else prefix), annotation)

        try:
            rrdtool.graph(basename + ".svg",
                          '--imgformat','SVG',
                          '--start', str(int(start_time)),
                          '--end', str(int(end_time)),
                          '--font',  'DEFAULT:0:Helvetica',
                          '-w', '%d' % max(800,length), '-h', '400',
                          '--right-axis', '1:0',
                          vrules,
                          '-t', title,
                          '-v', '%s %s' % (direction, units),
                          'DEF:foo=%s.rrd:mbps:AVERAGE' % basename,
                          'CDEF:bits=foo,%s,*' % multiplier,
                          'LINE2:bits#00FF0080:%s' % units)
        except:
            # at some point we should make certain this was for the
            # "intervals" rrd file but until then just pass
            pass

def setup_parser() :
    parser = argparse.ArgumentParser()
    parser.add_argument("-i", "--individual", action='store_true',
                        default=False,
                        help="Generate graphs of individual tests")
    parser.add_argument("-I", "--intervals", action='store_true',
                        default=False,
                        help="Emit the results for all intervals, not just peak")
    parser.add_argument("-a", "--annotation",default=None,
                        help="Annotation to add to chart titles")
    parser.add_argument("-t", "--title", default=None,
                        help="String to use for chart title. Default based on test")
    parser.add_argument('filename')

    return parser

if __name__ == '__main__':

    parser = setup_parser()
    args = parser.parse_args()

    filename = args.filename
    prefix = filename.replace(".log","")
    source = open(filename,"r")
    vrules,start_time,end_time,intervals = find_vrules(source)
    #print vrules

    # at one point for some reason I thought one could not add to a
    # dict on the fly, which of course I now know is silly, but for
    # the time being I will preallocate the entire dict in one fell
    # swoop until I can modify add_to_ksink() accordingly
    length = int(end_time + 1) - int(start_time)
    ksink=dict(zip(xrange(int(start_time),
                          int(end_time)+1),
                   [0.0] * length))

    min_timestamp = process_result_files(prefix,start_time,end_time,ksink)
    if min_timestamp == 9999999999.9:
        print "There were no valid results for this prefix!"
        exit()

#    print "Min timestamp for %s is %s start time is %s end_time is %s" % (prefix,min_timestamp,start_time,end_time)
    generate_overall(prefix,min_timestamp-2,end_time,ksink)
    peak_interval_id, min_graph_interval, results_list = overall_min_max_avg(prefix,min_timestamp,end_time,intervals)
    peak_average = results_list[0][0]
    peak_minimum = results_list[0][1]
    peak_maximum = results_list[0][2]
    peak_start = results_list[0][3]
    peak_end = results_list[0][4]

    graph_overall(prefix, min_timestamp, end_time, vrules, peak_interval_id,
                  peak_average, major_interval=min_graph_interval,
                  annotation=args.annotation, override=args.title)
    if args.individual:
        graph_individual(prefix, min_timestamp, end_time, vrules,
                         major_interval=min_graph_interval,
                         annotation=args.annotation,override=args.title)

    units, multiplier, direction = units_et_al_by_prefix(prefix)
    print "Average of peak interval is %.3f %s from %d to %d" % (results_list[0][0] * float(multiplier), units, peak_start, peak_end)
    print "Minimum of peak interval is %.3f %s from %d to %d" % (peak_minimum * float(multiplier), units, peak_start, peak_end)
    print "Maximum of peak interval is %.3f %s from %d to %d" % (peak_maximum * float(multiplier), units, peak_start, peak_end)

    if args.intervals:
        for id, interval in enumerate(results_list[1:]):
            print "Average of interval %d is %.3f %s from %d to %d" % (id, interval[0] * float(multiplier), units, interval[3], interval[4])
            print "Minimum of interval %d is %.3f %s from %d to %d" % (id, interval[1] * float(multiplier), units, interval[3], interval[4])
            print "Maximum of interval %d is %.3f %s from %d to %d" % (id, interval[2] * float(multiplier), units, interval[3], interval[4])
