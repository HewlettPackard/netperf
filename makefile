#
# @(#)Makefile	1.7	03/04/93
#
# Makefile to build netperf benchmark tool
#
#
# This tells the script where the executables and scripts are supposed
# to go. Some people might be occustomed to "/usr/etc/net_perf", but
# for the rest of the world, it is probably better to put the binaries
# in /usr/local/netperf or /var/netperf. This will be more
# standard for people outside of the HP R&D labs...still working on it.
#
#NETPERF_HOME = /usr/etc/netperf
#NETPERF_HOME = /var/netperf
NETPERF_HOME = /usr/local/netperf

# The compiler on your system might be somewhere else
#CC = /bin/cc -g
CC = /bin/cc -DDO_DLPI -DUSE_PSTAT

#LIBS = -lstr
LIBS = -lstr

# These flags enable some non-mainline features used in the lab. For
# more information, please consult the source code.
#CFLAGS = -DDIRTY -DINTERVALS
#CFLAGS = -DDO_DLPI 
CFLAGS = 

SHAR_SOURCE_FILES = netlib.c netlib.h netperf.c netserver.c \
		    netsh.c netsh.h nettest_bsd.c nettest_dlpi.c \
		    nettest_bsd.h nettest_dlpi.h makefile

SHAR_EXE_FILES    = ACKNWLDGMNTS COPYRIGHT README \
                    netperf.ps netperf.pcl netperf netserver \
		    netperf.man netserver.man

SHAR_SCRIPT_FILES = tcp_stream_script udp_stream_script \
                    tcp_rr_script udp_rr_script tcp_range_script

NETSERVER_OBJS	  = netserver.o nettest_bsd.o nettest_dlpi.o netlib.o netsh.o

NETPERF_OBJS	  = netperf.o netsh.o netlib.o nettest_bsd.o nettest_dlpi.o

NETPERF_SCRIPTS   = tcp_range_script tcp_stream_script tcp_rr_script \
                    udp_stream_script udp_rr_script

all: netperf netserver

netperf:	$(NETPERF_OBJS)
		$(CC) -o $@ $(NETPERF_OBJS) $(LIBS)

netserver:	$(NETSERVER_OBJS)
		$(CC) -o $@ $(NETSERVER_OBJS) $(LIBS)

netperf.o:	netperf.c netsh.h

netsh.o:	netsh.c netsh.h nettest_bsd.h netlib.h

netlib.o:	netlib.c netlib.h netsh.h

nettest_bsd.o:	nettest_bsd.c nettest_bsd.h netlib.h netsh.h

nettest_dlpi.o:	nettest_dlpi.c nettest_dlpi.h netlib.h netsh.h

netserver.o:	netserver.c nettest_bsd.h netlib.h

install:	netperf netserver
		chmod -w *.[ch]
		chmod +x $(NETPERF_SCRIPTS)
		cp netperf $(NETPERF_HOME)
		cp netserver $(NETPERF_HOME)
		cp $(NETPERF_SCRIPTS) $(NETPERF_HOME)
clean:
	rm -f *.o netperf netserver core

extraclean:
	rm -f *.o netperf netserver core netperf.tar.Z netperf.shar \
	netperf_src.shar

deinstall:
	echo do this to deinstall rm -rf $(NETPERF_HOME)/*

netperf_src.shar: $(SHAR_SOURCE_FILES)
	shar -bcCsZ $(SHAR_SOURCE_FILES) > netperf_src.shar

netperf.shar:	$(SHAR_SOURCE_FILES) $(SHAR_EXE_FILES) $(SHAR_SCRIPT_FILES)
	shar -bcCsZ $(SHAR_SOURCE_FILES) $(SHAR_EXE_FILES) \
		$(SHAR_SCRIPT_FILES) > netperf.shar

netperf.tar:	$(SHAR_SOURCE_FILES) $(SHAR_EXE_FILES) $(SHAR_SCRIPT_FILES)
	tar -cf netperf.tar $(SHAR_SOURCE_FILES) $(SHAR_EXE_FILES) \
		$(SHAR_SCRIPT_FILES)
	compress netperf.tar


	
