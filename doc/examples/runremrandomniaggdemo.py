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


import logging
import argparse
import paramiko
import csv

def read_netperfs(netperfs_file) :

    with open(netperfs_file) as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            print(row['netperfs'], row['netperfs_priv'], row['netperfs_host'])


def setup_parser():

    parser = argparse.ArgumentsParser()

    parser.add_argument('-n','--netperfs',
                        help="Specify a list of systems on which to run netperf");
    parser.add_argument('-N','--netservers',
                        help="Specify a list of systems on which netserver runs")

    parser.add_argument('--allow-same-host',
                        help="Allow a netperf/netserver pairing on the same host",
                        action="store_true")

    parser.add_argument('--allow-same-instance',
                        help="Allow a netperf/netserver pairing on the same instance",
                        action="store_true")

    return parser

if __name__ == '__main__':
    print "Let's run some netperf"

    setup_parser();

    args = parser.parse_args()

