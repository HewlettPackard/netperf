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


import sys
import argparse
import numpy as np
from pprint import pprint

# this nifty little bit of code will automagically next another
# Vividict as needed.  it was found online
class Vividict(dict):
    def __missing__(self, key):
        value = self[key] = type(self)()
        return value

# the idea here is to process a result line, already split at the
# delimeter.  if we are organizing by specified fields, we do that,
# possibly with a recursive call.  otherwise we just append and go.
# Vividict automatically creates another Vividict as we nest, but when
# we are a the end of the nesting, we don't want a Vividict but a
# plain old list
def process_line(d, line, fields, result):

    # are we organizing by any fields?
    if fields != None:
        # are we at the last field of the list?
        key = line[fields[0]]
        if len(fields) == 1 :
            # is there already an list here?
            if (len(d[key]) > 0) :
                # then just append this result
                d[key].append(float(line[result]))
            else:
                # we want a list rather than a vividict at the end
                d[key] = [float(line[result])]
        else:
            # recurse with the next Vividict based on the current
            # field selection and the next bit of fields on which to
            # organize
            process_line(d[key], line, fields[1:], result)
    else:
        # we aren't organizing on any fields. perhaps this could just
        # be a list then?
        d['all'].append(float(line[result]))

def process_results(results,fields,result,delimeter,strip) :

    d = Vividict()
    ignored = 0

    # are we actually selecting on any fields? how many fields must
    # there be minimum on each line?
    if fields == None:
        min_fields = result
        d['all'] = []
    else:
        min_fields = max(fields + [result])

    for line in results:
        l = line.strip().replace(strip,"").split(delimeter)
        # filter-out lines which are too short
        if len(l) < min_fields:
            ignored += 1
        else:
            process_line(d, l, fields, result)

    return (d,ignored)

def print_stats(values,prefix,delimeter):
    line = prefix
    line += "%c%.3f" % (delimeter,np.min(values))
    line += "%c%.3f" % (delimeter,np.percentile(values,10))
    line += "%c%.3f" % (delimeter,np.median(values))
    line += "%c%.3f" % (delimeter,np.average(values))
    line += "%c%.3f" % (delimeter,np.percentile(values,90))
    line += "%c%.3f" % (delimeter,np.percentile(values,99))
    line += "%c%.3f" % (delimeter,np.max(values))
    line += "%c%d" % (delimeter,len(values))
    print line

# intially based on myprint() from
# http://stackoverflow.com/questions/10756427/loop-through-all-nested-dictionary-values
def print_results_inner(d,prefix,delimeter):
    for k, v in d.iteritems():
        if len(prefix) == 0:
            newprefix = k
        else:
            newprefix=prefix+delimeter+k
        if isinstance(v, dict):
            print_results_inner(v,newprefix,delimeter)
        else:
            print_stats(v,newprefix,delimeter)

def add_stats_headers(header,delimeter):
    header+="Min"+delimeter
    header+="P10"+delimeter
    header+="Median"+delimeter
    header+="Average"+delimeter
    header+="P90"+delimeter
    header+="P99"+delimeter
    header+="Max"+delimeter
    header+="Count"
    return header

def print_results(d,fields,delimeter):

    if fields == None:
        header = "All"+delimeter
    else:
        header = ""
        for f in fields:
            header = header + "Field" + str(f)
            header = header + delimeter

    header = add_stats_headers(header,delimeter)
    print header
    print_results_inner(d,"",delimeter)

def setup_parser() :
    parser = argparse.ArgumentParser()
    parser.add_argument("-d", "--delimeter",
                        default=',',
                        help="Character to use for the field separator. Default ','.")
    parser.add_argument("-f", "--field", action='append', type=int,
                        help="Field to use to order results. Counting starts at 0. May be repeated.")
    parser.add_argument("-r", "--result", type=int, required=True,
                        help="Field number for the result. Counting starts at 0/ Required.")
    parser.add_argument("-s", "--strip",default='"',
                        help="Character to strip from each line prior to processing. Default '\"'.")
    parser.add_argument('filename',nargs='?')

    return parser

if __name__ == '__main__':

    parser = setup_parser()

    args = parser.parse_args()

    fields = args.field
    result = args.result
    delimeter = args.delimeter
    strip = args.strip

    if args.filename != None:
        results = open(args.filename,"r")
    else:
        results = sys.stdin

    d,ignored = process_results(results,fields,result,delimeter,strip)

    print_results(d,fields,delimeter)
    print "%d too-short lines ignored." % ignored
