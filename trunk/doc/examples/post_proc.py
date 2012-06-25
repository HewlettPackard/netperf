#!/usr/bin/python

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
    output = "Would have opened %s.rrd with start time %d and end time %d" % (basename,int(start_time),int(end_time))

    data_sources = [ 'DS:mbps:GAUGE:%d:U:U' % max_interval ]
    rra = [ 'RRA:AVERAGE:0.5:1:%d' % ((int(end_time) - int(start_time)) + 1) ]

    rrdtool.create(basename + ".rrd",
                   '--step', '1',
                   '--start', str(int(start_time)-1),
                   data_sources,
                   rra )

def update_heartbeat(basename,heartbeat):
    print "Updating heartbeat with %d" % heartbeat
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

def process_result(basename, raw_results, start_time,end_time, ksink):
    first_result = True
    have_result = False
    interim_result=0.0
    interim_units="Trans/s"
    interim_interval=1.0
    interim_end=0.0
    max_interval=1



    for raw_result in raw_results:
        if "Interim result:" in raw_result:
            # human format
            fields = raw_result.split()
            interim_result=float(fields[2])
            interim_units=fields[3]
            interim_interval=float(fields[5])
            interim_end=float(fields[9])
            have_result=True
        elif "NETPERF_INTERIM_RESULT" in raw_result:
            # keyval
            interim_result=float(raw_result.split('=')[1])
            have_result=False
        elif "NETPERF_UNITS" in raw_result:
            # keyval
            interim_units=raw_result.split('=')[1]
            have_result=False
        elif "NETPERF_INTERVAL" in raw_result:
            # keyval
            interim_interval=float(raw_result.split('=')[1])
            have_result=False
        elif "NETPERF_ENDING" in raw_result:
            # keyval
            interim_end=float(raw_result.split('=')[1])
            have_result=True
        else:
            # csv
            fields = raw_result.split(',')
            if len(fields) == 4:
                interim_result = float(fields[0])
                interim_units = fields[1]
                interim_interval = float(fields[2])
                interim_end = float(fields[3])
                have_result = True
            else:
                have_result = False
                
        if first_result:
            # we could use the overal start time, but using the
            # first timestamp for this instance may save us some
            # space in the rrdfile
            open_rrd(basename,interim_end,end_time,max_interval)
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
            update_rrd(basename,interim_result,interim_end)
            have_result = False

    last_timestamp = interim_end
#    print "First timestamp for this instance %f last %f" % (first_timestamp,last_timestamp)
    return first_timestamp, last_timestamp

def process_result_files(prefix,start_time,end_time,ksink):
    print "Prefix is",prefix
    
    results_list = glob.glob(prefix+"*.out")

    for result_file in results_list:
        basename = result_file.replace(".out","")
        raw_results = open(result_file,"r")
        first_timestamp, last_timestamp = process_result(basename,
                                                         raw_results,
                                                         start_time,
                                                         end_time,
                                                         ksink)
        # OK, now we get the massaged results
        add_to_ksink(basename,first_timestamp,last_timestamp,ksink)

def generate_overall(prefix,start_time,end_time,ksink):
    overall = prefix + "_overall"
    open_rrd(overall,start_time,end_time,1)

    # this strikes me as being very brittle - can one really rely on
    # dictionaries to iterate in order of the key when the key is an
    # integer? for now I will assume so.
    for key in ksink:
        update_rrd(overall,ksink[key],key)

def overall_min_max_avg(prefix,start_time,end_time,intervals):

    max_average = 0.0
    length = int(end_time) - int(start_time)

    rrdtool.create(prefix + "_intervals.rrd",
                   '--step', '1',
                   '--start', str(int(start_time) - 1),
                   'DS:avg:GAUGE:1:U:U', 'RRA:AVERAGE:0.5:1:%d' % int(length),
                   'DS:min:GAUGE:1:U:U', 'RRA:AVERAGE:0.5:1:%d' % int(length),
                   'DS:max:GAUGE:1:U:U', 'RRA:AVERAGE:0.5:1:%d' % int(length))

    for id, interval in enumerate(intervals,start=1):
        start = interval[0] + 1
        end = interval[1] - 1
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
#        print "from %d to %d is %s" % (start,end,result)
        iavg = float(result[0].strip('"'))
        imin = float(result[1].strip('"'))
        imax = float(result[2].strip('"'))
        for time in xrange(start,end+1):
            rrdtool.update(prefix + "_intervals.rrd",
                           '%d:%f:%f:%f' % (time, iavg, imin, imax))
        if iavg > max_average:
            peak_interval_id = id;
            max_average = iavg
            max_minimum = imin
            max_maximum = imax

    return peak_interval_id, max_average, max_minimum, max_maximum

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

def graph_overall(prefix,start_time,end_time,vrules,peak_interval_id=None,peak_average=0.0):

    length = int(end_time) - int(start_time)

    units, multiplier, direction = units_et_al_by_prefix(prefix)

#    print units,multiplier,direction
#    print "Vrules",vrules

    interval_specs = []
    if peak_interval_id:
        interval_specs = [ 'DEF:bar=%s_intervals.rrd:avg:AVERAGE' % prefix,
                           'CDEF:intvl=bar,%s,*' % multiplier,
                           'LINE2:intvl#0F0F0F40:Interval average. Peak of %.3f during interval %d' % (peak_average, peak_interval_id) ]

    rrdtool.graph(prefix + "_overall.png", '--imgformat', 'PNG',
                  '--start', str(int(start_time)),
                  '--end', str(int(end_time)),
                  '-w','%d' % max(800,length),'-h','400',
                  vrules,
                  '--font', 'DEFAULT:0:Helvetica',
                  '-t', 'Overall %s' % prefix,
                  '-v', '%s %s' % (direction,units),
                  'DEF:foo=%s_overall.rrd:mbps:AVERAGE' % prefix,
                  'CDEF:bits=foo,%s,*' % multiplier,
                  'LINE2:bits#00FF0080:%s' % units,
                  interval_specs)

def graph_individual(prefix,start_time,end_time,vrules):

    units, multiplier, direction = units_et_al_by_prefix(prefix)

    length = int(end_time) - int(start_time)

    for individual in glob.glob(prefix+"*.out"):
        basename = individual.strip(".out")
        rrdtool.graph(basename + ".png",
                      '--imgformat','PNG',
                      '--start', str(int(start_time)),
                      '--end', str(int(end_time)),
                      '--font',  'DEFAULT:0:Helvetica',
                      '-w', '%d' % max(800,length), '-h', '400',
                      vrules,
                      '-t', '%s %s' % (basename,prefix),
                      '-v', '%s %s' % (direction, units),
                      'DEF:foo=%s.rrd:mbps:AVERAGE' % basename,
                      'CDEF:bits=foo,%s,*' % multiplier,
                      'LINE2:bits#00FF0080:%s' % units)

if __name__ == '__main__':

    filename = sys.argv[1]
    prefix = filename.replace(".log","")
    source = open(filename,"r")
    vrules,start_time,end_time,intervals = find_vrules(source)
    #print vrules

    # it would certainly be nice to be able to add to a dict on the fly
    length = int(end_time + 1) - int(start_time)
    ksink=dict(zip(xrange(int(start_time),
                          int(end_time)+1),
                   [0.0] * length))

    process_result_files(prefix,start_time,end_time,ksink)
    generate_overall(prefix,start_time,end_time,ksink)
    peak_interval_id, peak_average, peak_minimum, peak_maximum = overall_min_max_avg(prefix,start_time,end_time,intervals)
    graph_overall(prefix,start_time,end_time,vrules,peak_interval_id,peak_average)
    graph_individual(prefix,start_time,end_time,vrules)
    
    # we only need the units
    units = units_et_al_by_prefix(prefix)[0]
    print "Average of peak interval is %.3f %s from %d to %d" % (peak_average, units, intervals[peak_interval_id-1][0], intervals[peak_interval_id-1][1])
    print "Minimum of peak interval is %.3f %s from %d to %d" % (peak_minimum, units, intervals[peak_interval_id-1][0], intervals[peak_interval_id-1][1])
    print "Maximum of peak interval is %.3f %s from %d to %d" % (peak_maximum, units, intervals[peak_interval_id-1][0], intervals[peak_interval_id-1][1])
