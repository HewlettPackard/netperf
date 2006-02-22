#SUBDIRS = missing

bin_PROGRAMS = netperf netserver

AM_CFLAGS = $(NETPERF_CFLAGS)

USE_CPU_SOURCE=netcpu_@NETCPU_SOURCE@.c

EXTRA_DIST = netcpu_none.c netcpu_looper.c netcpu_pstat.c netcpu_pstatnew.c netcpu_perfstat.c netcpu_procstat.c netcpu_kstat.c netcpu_kstat10.c netcpu_sysctl.c netcpu_ntperf.c nettest_ipv6.c nettest_ipv6.h missing/* dirs NetPerfDir/* NetServerDir/*

COMMON_SRC = hist.h netlib.c netlib.h netcpu.h netsh.c netsh.h nettest_bsd.c nettest_bsd.h nettest_dlpi.c nettest_dlpi.h nettest_dns.c nettest_dns.h nettest_unix.c nettest_unix.h nettest_xti.c nettest_xti.h nettest_sctp.c nettest_sctp.h

netperf_SOURCES = netperf.c $(COMMON_SRC) $(USE_CPU_SOURCE)
netserver_SOURCES = netserver.c $(COMMON_SRC) $(USE_CPU_SOURCE)

# if there are any "missing" routines, the libobjs should cover it
netperf_LDADD = @LIBOBJS@
netserver_LDADD = @LIBOBJS@

missing/getaddrinfo.o: $(srcdir)/missing/getaddrinfo.c
	$(COMPILE) -o $@ -c $(srcdir)/missing/getaddrinfo.c
missing/inet_ntop.o: $(srcdir)/missing/inet_ntop.c
	$(COMPILE) -o $@ -c $(srcdir)/missing/inet_ntop.c
