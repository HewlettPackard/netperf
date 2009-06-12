#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

FULL-PACKAGE-NAME=netperf
VERSION=2.4.5
BUG-REPORT-ADDRESS=netperf-feedback@netperf.org

AC_PREREQ(2.59)
AC_INIT(netperf, 2.4.5)
# use the target version rather than host - one day we may want cross-compile
AC_CANONICAL_TARGET
AC_CONFIG_SRCDIR([src/hist.h])
AM_INIT_AUTOMAKE([dist-zip])
AM_CONFIG_HEADER(config.h)
# AC_CONFIG_HEADER(config.h)

AC_CONFIG_LIBOBJ_DIR(src/missing)

# make sure we build netperf_version.h
touch src/netperf_version.h.in

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB

AC_C_CONST

# Checks for libraries.
AC_HAVE_LIBRARY(m)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h endian.h errno.h fcntl.h limits.h malloc.h netdb.h netinet/in.h signal.h stdlib.h string.h strings.h sys/ioctl.h sys/mman.h sys/param.h sys/socket.h sys/time.h unistd.h ifaddrs.h sys/sockio.h])

# Some platforms require these.  There may be a better way.
AC_HAVE_LIBRARY(socket)
if test "$ac_cv_lib_socket_main" = yes ; then
    AC_HAVE_LIBRARY(nsl)
    AC_HAVE_LIBRARY(sendfile)
    AC_SYS_LARGEFILE
fi

# this one is for Tru64 and bind_to_cpu_id
AC_HAVE_LIBRARY(mach)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
# AC_TYPE_SOCKLEN_T

OLD_TYPE_SOCKLEN_T

# AC_TYPE_IN_PORT_T
AC_DECL_H_ERRNO
AC_STRUCT_SOCKADDR_STORAGE
AC_HEADER_TIME
AC_HEADER_STDBOOL
AC_CHECK_SA_LEN(ac_cv_sockaddr_has_sa_len)

# Checks for library functions.
# AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
# AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_SETPGRP
AC_TYPE_SIGNAL
# AC_FUNC_STAT
# remove pstat_getdynamic (at least for now) since we don't do
# anything conditional with the check anyway...
AC_CHECK_FUNCS([alarm bzero gethostbyname gethrtime gettimeofday inet_ntoa memset memcpy munmap select socket sqrt strcasecmp strchr strstr strtoul uname])

#AC_CONFIG_SUBDIRS(src/missing)

# does this platform need the replacement getaddrinfo
AC_CHECK_FUNCS([getnameinfo getaddrinfo inet_ntop getifaddrs])
# AC_REPLACE_FUNCS([getaddrinfo])


if test "$ac_cv_func_getaddrinfo$ac_cv_func_getnameinfo" != yesyes ; then
   AC_MSG_NOTICE([Requesting replacement getaddrinfo/getnameinfo])
   AC_LIBOBJ(getaddrinfo)
   HAVE_MISSING=yes
fi
if test "$ac_cv_func_inet_ntop" != yes ; then
   AC_MSG_NOTICE([Requesting replacement inet_ntop])
   AC_LIBOBJ(inet_ntop)
   HAVE_MISSING=yes
fi
AM_CONDITIONAL(NEED_LIBCOMPAT, test "$HAVE_MISSING" = "yes")

AC_CHECK_FUNCS(sendfile)

AC_CHECK_FUNCS(uname)

# check for the various CPU binding calls
AC_CHECK_FUNCS(mpctl processor_bind sched_setaffinity bind_to_cpu_id bindprocessor)

# see if we should be enabling histogram support

AC_MSG_CHECKING(whether to include histogram support)

AC_ARG_ENABLE(histogram,
	[AS_HELP_STRING([--enable-histogram],[include individual op timing, may affect result]) ])

case "$enable_histogram" in
	yes)
		use_histogram=true
		;;
	no)
		use_histogram=false
		;;
	'')
		# whatever
		use_histogram=false
		;;
	*)
		AC_MSG_ERROR([--enable-histogram takes yes or no])
		;;
esac

if $use_histogram
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_histogram
then
	AC_DEFINE([WANT_HISTOGRAM],,[Define to one to enable histogram support. May affect results.])
fi
# see if we should be enabling histogram support

AC_MSG_CHECKING(whether to include dirty support)

AC_ARG_ENABLE(dirty,
	[AS_HELP_STRING([--enable-dirty],[write to buffers each time, may affect result]) ])

case "$enable_dirty" in
	yes)
		use_dirty=true
		;;
	no)
		use_dirty=false
		;;
	'')
		# whatever
		use_dirty=false
		;;
	*)
		AC_MSG_ERROR([--enable-dirty takes yes or no])
		;;
esac

if $use_dirty
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_dirty
then
	AC_DEFINE([DIRTY],,[Define to one to enable dirty buffer support. May affect results.])
fi

# see if we should be enabling demo support

AC_MSG_CHECKING(whether to include demo support)

AC_ARG_ENABLE(demo,
	[AS_HELP_STRING([--enable-demo],[emit interim results during the run. May affect results.])])

case "$enable_demo" in
	yes)
		use_demo=true
		;;
	no)
		use_demo=false
		;;
	'')
		# whatever
		use_demo=false
		;;
	*)
		AC_MSG_ERROR([--enable-demo takes yes or no])
		;;
esac

if $use_demo
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_demo
then
	AC_DEFINE([WANT_DEMO],,[Define to one to enable demo support. May affect results.])
fi

# see if we should be including the AF_UNIX tests

AC_MSG_CHECKING(whether to include Unix-domain socket tests)

AC_ARG_ENABLE(unixdomain,
	[AS_HELP_STRING([--enable-unixdomain],[include Unix Domain socket tests])])

case "$enable_unixdomain" in
     yes)
		use_unixdomain=true
		;;
     no)	
		use_unixdomain=false
		;;
     '')
		use_unixdomain=false
		;;
     *)
		AC_MSG_ERROR([--enable-unixdomain takes yes or no])
		;;
esac

if $use_unixdomain
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_unixdomain
then
	AC_DEFINE([WANT_UNIX],,[Define to one to include Unix Domain socket tests.])
fi

# see if we should be including the DLPI tests

AC_MSG_CHECKING(whether to include DLPI tests)

AC_ARG_ENABLE(dlpi,
	[AS_HELP_STRING([--enable-dlpi],[include DLPI (link-layer) tests])])

case "$enable_dlpi" in
     yes)
		use_dlpi=true
		;;
     no)	
		use_dlpi=false
		;;
     '')
		use_dlpi=false
		;;
     *)
		AC_MSG_ERROR([--enable-dlpi takes yes or no])
		;;
esac

if $use_dlpi
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_dlpi
then
	AC_DEFINE([WANT_DLPI],,[Define to one to include DLPI tests.])
fi

# see if we should be including the DCCP tests

AC_MSG_CHECKING(whether to include DCCP tests)

AC_ARG_ENABLE(dccp,
	[AS_HELP_STRING([--enable-dccp],[include DCCP tests])])

case "$enable_dccp" in
     yes)
		use_dccp=true
		;;
     no)	
		use_dccp=false
		;;
     '')
		use_dccp=false
		;;
     *)
		AC_MSG_ERROR([--enable-dccp takes yes or no])
		;;
esac

if $use_dccp
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_dccp
then
	AC_DEFINE([WANT_DCCP],,[Define to one to include DCCP tests.])
fi

# see if we should be including the OMNI tests

AC_MSG_CHECKING(whether to include OMNI tests)

AC_ARG_ENABLE(omni,
	[AS_HELP_STRING([--enable-omni],[include OMNI tests])])

case "$enable_omni" in
     yes)
		use_omni=true
		;;
     no)	
		use_omni=false
		;;
     '')
		use_omni=false
		;;
     *)
		AC_MSG_ERROR([--enable-omni takes yes or no])
		;;
esac

if $use_omni
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_omni
then
	AC_DEFINE([WANT_OMNI],,[Define to one to include OMNI tests.])
fi


# see if we should be including the XTI tests

AC_MSG_CHECKING(whether to include XTI tests)

AC_ARG_ENABLE(xti,
	[AS_HELP_STRING([--enable-xti],[include XTI socket tests])])

case "$enable_xti" in
     yes)
		use_xti=true
		;;
     no)	
		use_xti=false
		;;
     '')
		use_xti=false
		;;
     *)
		AC_MSG_ERROR([--enable-xti takes yes or no])
		;;
esac

if $use_xti
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_xti
then
	AC_DEFINE([WANT_XTI],,[Define to one to include XTI tests.])
fi

# see if we should be including the SDP tests

AC_MSG_CHECKING(whether to include SDP tests)

AC_ARG_ENABLE(sdp,
	[AS_HELP_STRING([--enable-sdp],[include SDP socket tests])])

case "$enable_sdp" in
     yes)
		# probably need to be a bit more sophisticated here
		AC_CHECK_LIB(sdp,t_open)
		use_sdp=true
		;;
     no)	
		use_sdp=false
		;;
     '')
		use_sdp=false
		;;
     *)
		AC_MSG_ERROR([--enable-sdp takes yes or no])
		;;
esac

if $use_sdp
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_sdp
then
	AC_DEFINE([WANT_SDP],,[Define to one to include SDP tests.])
fi

# see if we should be including the ICSC-EXS tests

AC_MSG_CHECKING(whether to include ICSC-EXS tests)

AC_ARG_ENABLE(exs,
	[AS_HELP_STRING([--enable-exs],[include ICSC async sockets tests])])

case "$enable_exs" in
     yes)
		use_exs=true
		AC_CHECK_HEADER(sys/exs.h,,[use_exs=false])
		AC_CHECK_LIB(exs,exs_init,,[use_exs=false])
		;;
     no)	
		use_exs=false
		;;
     '')
		use_exs=false
		;;
     *)
		AC_MSG_ERROR([--enable-exs takes yes or no])
		;;
esac

if $use_exs
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_exs
then
	AC_DEFINE([HAVE_ICSC_EXS],,[Define to one to include ICSC-EXS tests.])
fi

# see if we should be enabling SCTP support


AC_ARG_ENABLE(sctp,
	[AS_HELP_STRING([--enable-sctp],[include tests to measure SCTP performance ])])

case "$enable_sctp" in
	yes)
		use_sctp=true
		AC_CHECK_HEADERS(netinet/sctp.h,,use_sctp=false,
[[
#include <sys/socket.h>
]])
		case "$host" in
		*-*-freebsd7.*)
			# FreeBSD 7.x SCTP support doesn't need -lsctp.
			;;
		*)
			AC_HAVE_LIBRARY(sctp,,use_sctp=false)
			;;
		esac
		AC_CHECK_MEMBER(struct sctp_event_subscribe.sctp_adaptation_layer_event,
				, , [#include <netinet/sctp.h>])
		if test "$ac_cv_member_struct_sctp_event_subscribe_sctp_adaptation_layer_event" = "yes"; then
			AC_DEFINE([HAVE_SCTP_ADAPTATION_LAYER_EVENT], 1,
				  [Define to 1 if `struct sctp_event_subscribe' has a `sctp_adaptation_layer_event' member])
		fi
		;;
	no)
		use_sctp=false
		;;
	'')
		# whatever
		use_sctp=false
		;;
	*)
		AC_MSG_ERROR([--enable-sctp takes yes or no])
		;;
esac

AC_MSG_CHECKING(whether to include SCTP tests)

if $use_sctp
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_sctp
then
	AC_DEFINE([WANT_SCTP],,[Define to one to include SCTP tests.])
fi

# see if we should be enabling paced sends

AC_MSG_CHECKING([whether to include paced send (intervals) support])

AC_ARG_ENABLE(intervals,
	[AS_HELP_STRING([--enable-intervals],[include ability to pace operations, may affect result])])

case "$enable_intervals" in
	yes)
		use_intervals=true
		;;
	no)
		use_intervals=false
		;;
	'')
		use_intervals=false
		;;
	*)
		AC_MSG_ERROR([--enable-intervals takes yes or no])
		;;
esac

if $use_intervals
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_intervals
then
	AC_DEFINE([WANT_INTERVALS],,[Define to one to enable paced operation support. May affect results.])
fi

# see if paced sends should wait and spin

AC_MSG_CHECKING([whether paced sends should spin])

AC_ARG_ENABLE(spin,
	[AS_HELP_STRING([--enable-spin],[paced operations (--enable-intervals) should sit and spin - WILL affect result])])

case "$enable_spin" in
	yes)
		use_spin=true
		;;
	no)
		use_spin=false
		;;
	'')
		use_spin=false
		;;
	*)
		AC_MSG_ERROR([--enable-spin takes yes or no])
		;;
esac

if $use_spin
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_spin
then
	AC_DEFINE([WANT_INTERVALS],,[Define to one to enable paced operation support. May affect results.])
	AC_DEFINE([WANT_SPIN],,[Define to one to spin waiting on paced operation. WILL AFFEFCT CPU UTILIZATION])
fi

# see if we should be enabling initial request bursts

AC_MSG_CHECKING([whether to include initial burst support in _RR tests])

AC_ARG_ENABLE(burst,
	[AS_HELP_STRING([--enable-burst],[include intial request burst ability in _RR tests, may affect result])])

case "$enable_burst" in
	yes)
		use_burst=true
		;;
	no)
		use_burst=false
		;;
	'')
		use_burst=false
		;;
	*)
		AC_MSG_ERROR([--enable-burst takes yes or no])
		;;
esac

if $use_burst
then
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)
fi

if $use_burst
then
	AC_DEFINE([WANT_FIRST_BURST],,[Define to one to enable initial _RR burst support. May affect results.])
fi

# time to see about CPU utilization measurements

AC_MSG_CHECKING([which CPU utilization measurement type to use])

AC_ARG_ENABLE(cpuutil,
	[AS_HELP_STRING([--enable-cpuutil],[include code to measure CPU utilization using specified mechanism])])

NETCPU_SOURCE="$enable_cpuutil"
case "$enable_cpuutil" in
	pstat)
		use_cpuutil=true
		AC_DEFINE([USE_PSTAT],,[Use HP-UX's pstat interface to measure CPU util.])
		;;
	pstatnew)
		use_cpuutil=true
		AC_DEFINE([USE_PSTAT],,[Use HP-UX's pstat interface to measure CPU util.])
		;;
        perfstat)
                use_cpuutil=true
                AC_DEFINE([USE_PERFSTAT],,[Use AIX's perfstat interface to measure CPU util.])
		AC_HAVE_LIBRARY(perfstat)
                ;;

	looper)
		use_cpuutil=true
		AC_DEFINE([USE_LOOPER],,[Use looper/soaker processes to measure CPU util.])
		;;		
	procstat)
		use_cpuutil=true
		AC_DEFINE([USE_PROC_STAT],,[Use Linux's procstat interface to measure CPU util.])
		;;
	kstat)
		use_cpuutil=true
		AC_DEFINE([USE_KSTAT],,[Use Solaris's kstat interface to measure CPU util.])
		AC_HAVE_LIBRARY(kstat)
		;;
	kstat10)
		use_cpuutil=true
		AC_DEFINE([USE_KSTAT],,[Use Solaris's kstat interface to measure CPU util.])
		AC_HAVE_LIBRARY(kstat)
		;;
	osx)
		use_cpuutil=true
		AC_DEFINE([USE_OSX],,[Use MacOS X's host_info interface to measure CPU util.])
		;;
	'')
# ia64-hp-hpux11.23
# i386-pc-solaris2.10
# guess it automagically in a nice big case statement
		case $target in
		     *-*-linux*)
			use_cpuutil=true
			AC_DEFINE([USE_PROC_STAT],,[Use Linux's procstat interface to measure CPU util.])
			enable_cpuutil="procstat - auto"
			NETCPU_SOURCE="procstat"
			;;
		     *-*-hpux11.23 | *-*-hpux11.31)
			use_cpuutil=true
			AC_DEFINE([USE_PSTAT],,[Use HP-UX's pstat interface to measure CPU util.])
			enable_cpuutil="pstatnew - auto"
			NETCPU_SOURCE="pstatnew"
			;;
		     *-*-hpux11* | *-*-hpux10*)
			use_cpuutil=true
			AC_DEFINE([USE_PSTAT],,[Use HP-UX's pstat interface to measure CPU util.])
			enable_cpuutil="pstat - auto"
			NETCPU_SOURCE="pstat"
			;;
		     *-*-aix5.*)
			use_puutil=true
			AC_DEFINE([USE_PERFSTAT],,[Use AIX's perfstat interface to measure CPU util.])
			AC_HAVE_LIBRARY(perfstat)
			enable_cpuutil="perfstat - auto"
			NETCPU_SOURCE="perfstat"
			;;
		     *-*-solaris2.1*)
			use_cpuutil=true
			AC_DEFINE([USE_KSTAT],,[Use Solaris's kstat interface to measure CPU util.])
			AC_HAVE_LIBRARY(kstat)
			enable_cpuutil="kstat10 - auto"
			NETCPU_SOURCE="kstat10"
			;;
		     *-*-solaris2.*)
			use_cpuutil=true
			AC_DEFINE([USE_KSTAT],,[Use Solaris's kstat interface to measure CPU util.])
			AC_HAVE_LIBRARY(kstat)
			enable_cpuutil="kstat - auto"
			NETCPU_SOURCE="kstat"
			;;
                     *-*-freebsd[[4-7]].* | *-*-netbsd[[1-9]].* )
			use_cpuutil=true
			AC_DEFINE([USE_SYSCTL],,[Use MumbleBSD's sysctl interface to measure CPU util.])
			enable_cpuutil="sysctl - auto"
			NETCPU_SOURCE="sysctl"
			;;
		    *-*-darwin*)
			use_cpuutil=true
			AC_DEFINE([USE_OSX],,[Use MacOS X's host_info interface to measure CPU util.])
			enable_cpuutil="osx - auto"
			NETCPU_SOURCE="osx"
			;;
		     *)
			use_cpuutil=false
			NETCPU_SOURCE="none"
			enable_cpuutil="none. Consider teaching configure about your platform."
			;;
		esac
		;;
	none)
		use_cpuutil=false
		;;
	*)
		AC_MSG_ERROR([--enable-cpuutil takes kstat, kstat10, looper, osx, perfstat, procstat, pstat, pstatnew, sysctl or none])
		;;
esac

	AC_MSG_RESULT("$enable_cpuutil")

AC_SUBST(NETCPU_SOURCE)

# time to see about route lookup mechanisms

AC_MSG_CHECKING([which route lookup type to use])

AC_ARG_ENABLE(rtlookup,
	[AS_HELP_STRING([--enable-rtlookup],[include code to find the probable egress interface using specified mechanism])])

NETRTLKUP_SOURCE="$enable_rtlookup"
case "$enable_rtlookup" in
	rtmget)
		use_rtlookup=true
		;;

	rtnetlink)
		use_rtlookup=true
		;;
	'')
# guess it automagically in a nice big case statement
		case $target in
		     *-*-linux*)
			use_rtlookup=true
			enable_rtlookup="rtnetlink - auto"
			NETRTLKUP_SOURCE="rtnetlink"
			;;
		     *-*-hpux11.31 | *-*-solaris* | *-*-aix5*)
			use_rtlookup=true
			enable_rtlookup="rtmget - auto"
			NETRTLKUP_SOURCE="rtmget"
			;;
                     *-*-freebsd[[4-7]].* | *-*-darwin*)
			use_rtlookup=true
			enable_rtlookup="rtmget - auto"
			NETRTLKUP_SOURCE="rtmget"
			;;
		     *)
			use_rtlookup=false
			NETRTLKUP_SOURCE="none"
			enable_rtlookup="none. Consider teaching configure about your platform."
			;;
		esac
		;;
	none)
		use_rtlookup=false
		;;
	*)
		AC_MSG_ERROR([--enable-rtlookup takes rtmget, rtnetlink or none])
		;;
esac

	AC_MSG_RESULT("$enable_rtlookup")

AC_SUBST(NETRTLKUP_SOURCE)

# time to see about slot lookup mechanisms

AC_MSG_CHECKING([which slot lookup type to use])

AC_ARG_ENABLE(slotlookup,
	[AS_HELP_STRING([--enable-slotlookup],[include code to find the probable egress interface using specified mechanism])])

NETSLOTLKUP_SOURCE="$enable_slotlookup"
case "$enable_slotlookup" in

	linux)
		use_slotlookup=true
		;;
	solaris)
		use_slotlookup=true
		;;
	'')
# guess it automagically in a nice big case statement
		case $target in
		     *-*-linux*)
			use_slotlookup=true
			enable_slotlookup="linux - auto"
			NETSLOTLKUP_SOURCE="linux"
			;;
		     *-*-solaris*)
			use_slotlookup=true
			enable_slotlookup="solaris - auto"
			NETSLOTLKUP_SOURCE="solaris"
			AC_HAVE_LIBRARY(devinfo)
			;;
		     *)
			use_slotlookup=false
			NETSLOTLKUP_SOURCE="none"
			enable_slotlookup="none. Consider teaching configure about your platform."
			;;
		esac
		;;
	none)
		use_slotlookup=false
		;;
	*)
		AC_MSG_ERROR([--enable-slotlookup takes linux or none])
		;;
esac

	AC_MSG_RESULT("$enable_slotlookup")

AC_SUBST(NETSLOTLKUP_SOURCE)


# time to see about sec lookup mechanisms

AC_MSG_CHECKING([which sec lookup type to use])

AC_ARG_ENABLE(seclookup,
	[AS_HELP_STRING([--enable-seclookup],[include code to find the system security mechanism and its state])])

NETSECLKUP_SOURCE="$enable_seclookup"
case "$enable_seclookup" in

	linux)
		use_seclookup=true
		;;
	'')
# guess it automagically in a nice big case statement
		case $target in
		     *-*-linux*)
		        AC_HAVE_LIBRARY(dl)
			use_seclookup=true
			enable_seclookup="linux - auto"
			NETSECLKUP_SOURCE="linux"
			;;
		     *)
			use_seclookup=false
			NETSECLKUP_SOURCE="none"
			enable_seclookup="none. Consider teaching configure about your platform."
			;;
		esac
		;;
	none)
		use_seclookup=false
		;;
	*)
		AC_MSG_ERROR([--enable-seclookup takes linux or none])
		;;
esac

	AC_MSG_RESULT("$enable_seclookup")

AC_SUBST(NETSECLKUP_SOURCE)


# time to see about driver lookup mechanisms

AC_MSG_CHECKING([which driver info lookup type to use])

AC_ARG_ENABLE(drvlookup,
	[AS_HELP_STRING([--enable-drvlookup],[include code to find the driver information for the probable egress interface])])

NETDRVLKUP_SOURCE="$enable_drvlookup"
case "$enable_drvlookup" in
	ethtool)
		use_drvlookup=true
		;;

	solaris)
		use_drvlookup=true
		;;
	'')
# guess it automagically in a nice big case statement
		case $target in
		     *-*-linux*)
			use_drvlookup=true
			enable_drvlookup="ethtool - auto"
			NETDRVLKUP_SOURCE="ethtool"
			;;
		     *-*-solaris*)
		        use_drvlookup=true
			enable_drvlookup="solaris - auto"
			NETDRVLKUP_SOURCE="solaris"
			;;
		     *)
			use_drvlookup=false
			NETDRVLKUP_SOURCE="none"
			enable_drvlookup="none. Consider teaching configure about your platform."
			;;
		esac
		;;
	none)
		use_drvlookup=false
		;;
	*)
		AC_MSG_ERROR([--enable-drvlookup takes ethtool or none])
		;;
esac

	AC_MSG_RESULT("$enable_drvlookup")

AC_SUBST(NETDRVLKUP_SOURCE)

# time to see about system lookup mechanisms

AC_MSG_CHECKING([which system info lookup type to use])

AC_ARG_ENABLE(syslookup,
	[AS_HELP_STRING([--enable-syslookup],[include code to find some rudimentary system information])])

NETSYSLKUP_SOURCE="$enable_syslookup"
case "$enable_syslookup" in
	hpux11i)
		use_syslookup=true
		;;

	linux)
		use_syslookup=true;
		AC_CHECK_HEADERS([smbios/SystemInfo.h])
		AC_HAVE_LIBRARY(smbios)
		;;
        solaris)
		use_syslookup=true;
		# this will basically tell us if we can use libsmbios
		AC_CHECK_HEADERS([sys/sbmios.h])
		AC_HAVE_LIBRARY(smbios)
		;;
	'')
# guess it automagically in a nice big case statement
		case $target in
		     *-*-hpux11*)
			use_syslookup=true
			enable_syslookup="hpux11i - auto"
			NETSYSLKUP_SOURCE="hpux11i"
			;;
		     *-*-linux*)
			use_syslookup=true
			enable_syslookup="linux - auto"
			AC_CHECK_HEADERS([smbios/SystemInfo.h])
			AC_HAVE_LIBRARY(smbios)
			NETSYSLKUP_SOURCE="linux"
			;;
		     *-*-solaris*)
		        use_syslookup=true
			enable_syslookup="solaris - auto"
			NETSYSLKUP_SOURCE="solaris"
			AC_CHECK_HEADERS([sys/smbios.h])
			AC_HAVE_LIBRARY(smbios)
			;;
		     *)
			use_syslookup=false
			NETSYSLKUP_SOURCE="none"
			enable_syslookup="none. Consider teaching configure about your platform."
			;;
		esac
		;;
	none)
		use_syslookup=false
		;;
	*)
		AC_MSG_ERROR([--enable-syslookup takes hpux11i, linux or none])
		;;
esac

	AC_MSG_RESULT("$enable_syslookup")

AC_SUBST(NETSYSLKUP_SOURCE)

# now spit it all out
AC_CONFIG_FILES([Makefile
                 src/netperf_version.h
                 src/Makefile
		 src/missing/Makefile
		 src/missing/m4/Makefile
		 doc/Makefile
		 doc/examples/Makefile
                 netperf.spec])

AC_OUTPUT
