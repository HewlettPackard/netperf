SUBDIRS = missing

CLEANFILES = netperf_version.h

bin_PROGRAMS = netperf netserver

AM_CFLAGS = $(NETPERF_CFLAGS)

USE_CPU_SOURCE=netcpu_@NETCPU_SOURCE@.c
USE_RT_SOURCE=netrt_@NETRTLKUP_SOURCE@.c
USE_DRV_SOURCE=netdrv_@NETDRVLKUP_SOURCE@.c
USE_SLOT_SOURCE=netslot_@NETSLOTLKUP_SOURCE@.c
USE_SYS_SOURCE=netsys_@NETSYSLKUP_SOURCE@.c
USE_SEC_SOURCE=netsec_@NETSECLKUP_SOURCE@.c

EXTRA_DIST = netcpu_none.c netcpu_looper.c netcpu_pstat.c netcpu_pstatnew.c netcpu_perfstat.c netcpu_procstat.c netcpu_kstat.c netcpu_kstat10.c netcpu_sysctl.c netcpu_ntperf.c netcpu_osx.c dirs NetPerfDir/* NetServerDir/* netperf_version.h.in netrt_rtnetlink.c netrt_none.c netrt_rtmget.c netdrv_ethtool.c netdrv_none.c netslot_linux.c netslot_none.c netsys_none.c netsys_hpux11i.c netsys_linux.c netsys_solaris.c netdrv_solaris.c netslot_solaris.c netsec_linux.c netsec_none.c Makefile.uw

COMMON_SRC = hist.h netlib.c netlib.h netcpu.h netsh.c netsh.h nettest_bsd.c nettest_bsd.h nettest_dlpi.c nettest_dlpi.h nettest_unix.c nettest_unix.h nettest_xti.c nettest_xti.h nettest_sctp.c nettest_sctp.h netperf_version.h nettest_sdp.c nettest_sdp.h nettest_omni.c net_uuid.c

netperf_SOURCES = netperf.c $(COMMON_SRC) $(USE_CPU_SOURCE) $(USE_RT_SOURCE) $(USE_DRV_SOURCE) $(USE_SLOT_SOURCE) $(USE_SYS_SOURCE) $(USE_SEC_SOURCE)
netserver_SOURCES = netserver.c $(COMMON_SRC) $(USE_CPU_SOURCE) $(USE_RT_SOURCE) $(USE_DRV_SOURCE) $(USE_SLOT_SOURCE) $(USE_SYS_SOURCE) $(USE_SEC_SOURCE)

# if there are any "missing" routines, the libobjs should cover it
if NEED_LIBCOMPAT
netperf_LDADD = missing/libcompat.a
netserver_LDADD = missing/libcompat.a
endif
