#!/usr/bin/python -u

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

