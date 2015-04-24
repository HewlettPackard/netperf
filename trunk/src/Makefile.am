SUBDIRS = missing

CLEANFILES = netperf_version.h

bin_PROGRAMS = netperf netserver

AM_CFLAGS = $(NETPERF_CFLAGS)

USE_CPU_SOURCE=netcpu_@NETCPU_SOURCE@.c

EXTRA_DIST = netcpu_none.c netcpu_looper.c netcpu_pstat.c netcpu_pstatnew.c netcpu_perfstat.c netcpu_procstat.c netcpu_kstat.c netcpu_kstat10.c netcpu_sysctl.c netcpu_ntperf.c netcpu_osx.c dirs NetPerfDir/* NetServerDir/* netperf_version.h.in Makefile.uw

COMMON_SRC = hist.h netlib.c netlib.h netcpu.h netsh.c netsh.h nettest_bsd.c nettest_bsd.h nettest_dlpi.c nettest_dlpi.h nettest_unix.c nettest_unix.h nettest_xti.c nettest_xti.h nettest_sctp.c nettest_sctp.h netperf_version.h nettest_sdp.c nettest_sdp.h nettest_omni.c net_uuid.c dscp.c

netperf_SOURCES = netperf.c $(COMMON_SRC) $(USE_CPU_SOURCE) $(USE_SYS_SOURCE) $(USE_SEC_SOURCE) $(USE_FIRE_SOURCE)
netserver_SOURCES = netserver.c $(COMMON_SRC) $(USE_CPU_SOURCE) $(USE_SYS_SOURCE) $(USE_SEC_SOURCE) $(USE_FIRE_SOURCE)

# if there are any "missing" routines, the libobjs should cover it
if NEED_LIBCOMPAT
netperf_LDADD = missing/libcompat.a
netserver_LDADD = missing/libcompat.ap
endif
