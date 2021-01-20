/*
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
*/

char    netlib_id[]="\
@(#)netlib.c (c) Copyright 1993-2012 Hewlett-Packard Company, 2021 Hewlett Packard Enterprise Development LP. Version 2.6.0";


/****************************************************************/
/*                                                              */
/*      netlib.c                                                */
/*                                                              */
/*      the common utility routines available to all...         */
/*                                                              */
/*      establish_control()     establish the control socket    */
/*      calibrate_local_cpu()   do local cpu calibration        */
/*      calibrate_remote_cpu()  do remote cpu calibration       */
/*      send_request()          send a request to the remote    */
/*      recv_response()         receive a response from remote  */
/*      send_response()         send a response to the remote   */
/*      recv_request()          recv a request from the remote  */
/*      dump_request()          dump request contents           */
/*      dump_response()         dump response contents          */
/*      cpu_start()             start measuring cpu             */
/*      cpu_stop()              stop measuring cpu              */
/*      calc_cpu_util()         calculate the cpu utilization   */
/*      calc_service_demand()   calculate the service demand    */
/*      calc_thruput()          calulate the tput in units      */
/*      calibrate()             really calibrate local cpu      */
/*      identify_local()        print local host information    */
/*      identify_remote()       print remote host information   */
/*      format_number()         format the number (KB, MB,etc)  */
/*      format_units()          return the format in english    */
/*      msec_sleep()            sleep for some msecs            */
/*      start_timer()           start a timer                   */
/*      random_ip_address()     select a random IP address from */
/*                              specified range                 */
/*                                                              */
/*      the routines you get when WANT_DLPI is defined...       */
/*      ...all moved to src/nettest_dlpi.c                      */
/*                                                              */
/*      dl_open()               open a file descriptor and      */
/*                              attach to the card              */
/*      dl_mtu()                find the MTU of the card        */
/*      dl_bind()               bind the sap do the card        */
/*      dl_connect()            sender's have of connect        */
/*      dl_accpet()             receiver's half of connect      */
/*      dl_set_window()         set the window size             */
/*      dl_stats()              retrieve statistics             */
/*      dl_send_disc()          initiate disconnect (sender)    */
/*      dl_recv_disc()          accept disconnect (receiver)    */
/****************************************************************/

/****************************************************************/
/*                                                              */
/*      Global include files                                    */
/*                                                              */
/****************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

 /* It would seem that most of the includes being done here from
    "sys/" actually have higher-level wrappers at just /usr/include.
    This is based on a spot-check of a couple systems at my disposal.
    If you have trouble compiling you may want to add "sys/" raj
    10/95 */
#include <limits.h>
#include <signal.h>
#ifdef HAVE_SYSCALL_H
#include <syscall.h>
#endif
#ifdef MPE
#  define NSIG _NSIG
#endif /* MPE */
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif


#ifndef WIN32
 /* at some point, I would like to get rid of all these "sys/"
    includes where appropriate. if you have a system that requires/
    them, speak now, or your system may not compile later revisions of
    netperf. raj 1/96 */
#include <unistd.h>
#include <sys/stat.h>
#include <sys/times.h>
#ifndef MPE
#include <time.h>
#include <sys/time.h>
#endif /* MPE */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/utsname.h>
#if !defined(MPE) && !defined(__VMS)
#include <sys/param.h>
#endif /* MPE */

#else /* WIN32 */

#include <process.h>
#include <time.h>
#include <winsock2.h>
#define netperf_socklen_t socklen_t
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <mmsystem.h>
/* the only time someone should need to define DONT_IPV6 in the
   "sources" file is if they are trying to compile on Windows 2000 or
   NT4 and I suspect this may not be their only problem :) */
#ifndef DONT_IPV6
#include <ws2tcpip.h>
#endif

#define SIGALRM (14)
#define sleep(x) Sleep((x)*1000)

#endif /* WIN32 */

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

#ifdef _AIX
#include <sys/select.h>
#include <sys/sched.h>
#include <sys/pri.h>
#define PRIORITY PRI_LOW
#else/* _AIX */
#ifdef __sgi
#include <sys/prctl.h>
#include <sys/schedctl.h>
#define PRIORITY NDPLOMIN
#endif /* __sgi */
#endif /* _AIX */


#ifdef HAVE_MPCTL
#include <sys/mpctl.h>
#endif

#if !defined(HAVE_GETADDRINFO) || !defined(HAVE_GETNAMEINFO)
# include "missing/getaddrinfo.h"
#endif


#include "hist.h"

/****************************************************************/
/*                                                              */
/*      Local Include Files                                     */
/*                                                              */
/****************************************************************/
#define NETLIB
#include "netlib.h"
#include "netsh.h"
#include "netcpu.h"
#include "netperf_version.h"

/****************************************************************/
/*                                                              */
/*      Global constants, macros and variables                  */
/*                                                              */
/****************************************************************/

#if defined(WIN32) || defined(__VMS)
struct  timezone {
        int     dummy ;
        } ;
#ifndef __VMS
SOCKET     win_kludge_socket = INVALID_SOCKET;
SOCKET     win_kludge_socket2 = INVALID_SOCKET;
#endif /* __VMS */
#endif /* WIN32 || __VMS */

#ifndef LONG_LONG_MAX
#define LONG_LONG_MAX 9223372036854775807LL
#endif /* LONG_LONG_MAX */

 /* older versions of netperf knew about the HP kernel IDLE counter.
    this is now obsolete - in favor of either pstat(), times, or a
    process-level looper process. we also now require support for the
    "long" integer type. raj 4/95.  */

int
  lib_num_loc_cpus,    /* the number of cpus in the system */
  lib_num_rem_cpus;    /* how many we think are in the remote */

struct cpu_stats_struct
  lib_local_cpu_stats,
  lib_remote_cpu_stats;

#define PAGES_PER_CHILD 2

int     lib_use_idle;
int     cpu_method;

struct  timeval         time1, time2;
struct  timezone        tz;
float   lib_elapsed,
        lib_local_maxrate,
        lib_remote_maxrate;

float   lib_local_per_cpu_util[MAXCPUS];
int     lib_cpu_map[MAXCPUS];

int     *request_array;
int     *response_array;

/* INVALID_SOCKET == INVALID_HANDLE_VALUE == (unsigned int)(~0) == -1 */
SOCKET  netlib_control = INVALID_SOCKET;
SOCKET  server_sock = INVALID_SOCKET;
int     control_family = AF_UNSPEC;

/* global variables to hold the value for processor affinity */
int     local_proc_affinity = -1,remote_proc_affinity = -1;

/* these are to allow netperf to be run easily through those evil,
   end-to-end breaking things known as firewalls */
char local_data_port[10];
char remote_data_port[10];

char *local_data_address=NULL;
char *remote_data_address=NULL;

char *local_sysname, *remote_sysname;
char *local_release, *remote_release;
char *local_version, *remote_version;
char *local_machine, *remote_machine;

int local_data_family=AF_UNSPEC;
int remote_data_family=AF_UNSPEC;

char *netperf_version;

enum netperf_output_modes netperf_output_mode = HUMAN;

/* in the past, I was overlaying a structure on an array of ints. now
   I am going to have a "real" structure, and point an array of ints
   at it. the real structure will be forced to the same alignment as
   the type "double." this change will mean that pre-2.1 netperfs
   cannot be mixed with 2.1 and later. raj 11/95 */

union   netperf_request_struct  netperf_request;
union   netperf_response_struct netperf_response;

FILE    *where;

char    libfmt = '?';

#ifdef WIN32
HANDLE hAlarm = INVALID_HANDLE_VALUE;
int timed_out=0;
#endif

int     times_up;

#ifdef WIN32
 /* we use a getopt implementation from net.sources */
/*
 * get option letter from argument vector
 */
int
        opterr = 1,             /* should error messages be printed? */
        optind = 1,             /* index into parent argv vector */
        optopt;                 /* character checked for validity */
char
        *optarg;                /* argument associated with option */

#define EMSG    ""

#endif /* WIN32 */

static int measuring_cpu;
int
netlib_get_page_size(void) {

 /* not all systems seem to have the sysconf for page size. for
    those  which do not, we will assume that the page size is 8192
    bytes.  this should be more than enough to be sure that there is
    no page  or cache thrashing by looper processes on MP
    systems. otherwise  that's really just too bad - such systems
    should define  _SC_PAGE_SIZE - raj 4/95 */

#ifndef _SC_PAGE_SIZE
#ifdef WIN32

SYSTEM_INFO SystemInfo;

 GetSystemInfo(&SystemInfo);

 return SystemInfo.dwPageSize;
#else
 return(8192L);
#endif  /* WIN32 */
#else
 return(sysconf(_SC_PAGE_SIZE));
#endif /* _SC_PAGE_SIZE */

}



#ifdef WANT_INTERVALS
#ifdef WIN32
HANDLE WinTimer;
UINT timerRes;
void stop_itimer()
{
	CancelWaitableTimer(WinTimer);
	CloseHandle(WinTimer);
	timeEndPeriod(timerRes);
}
#else
static unsigned int usec_per_itvl;


void
stop_itimer()

{

  struct itimerval new_interval;
  struct itimerval old_interval;

  new_interval.it_interval.tv_sec = 0;
  new_interval.it_interval.tv_usec = 0;
  new_interval.it_value.tv_sec = 0;
  new_interval.it_value.tv_usec = 0;
  if (setitimer(ITIMER_REAL,&new_interval,&old_interval) != 0) {
    /* there was a problem arming the interval timer */
    perror("netperf: setitimer");
    exit(1);
  }
  return;
}
#endif /* WIN32 */
#endif /* WANT_INTERVALS */



#ifdef WIN32
static void
error(char *pch)
{
  if (!opterr) {
    return;             /* without printing */
    }
  fprintf(stderr, "%s: %s: %c\n",
          (NULL != program) ? program : "getopt", pch, optopt);
}

int
getopt(int argc, char **argv, char *ostr)
{
  static char *place = EMSG;    /* option letter processing */
  register char *oli;                   /* option letter list index */

  if (!*place) {
    /* update scanning pointer */
      if (optind >= argc || *(place = argv[optind]) != '-' || !*++place) {
        return EOF;
      }
    if (*place == '-') {
      /* found "--" */
        ++optind;
      place = EMSG ;    /* Added by shiva for Netperf */
        return EOF;
    }
  }

  /* option letter okay? */
  if ((optopt = (int)*place++) == (int)':'
      || !(oli = strchr(ostr, optopt))) {
    if (!*place) {
      ++optind;
    }
    error("illegal option");
    return BADCH;
  }
  if (*++oli != ':') {
    /* don't need argument */
    optarg = NULL;
    if (!*place)
      ++optind;
  } else {
    /* need an argument */
    if (*place) {
      optarg = place;           /* no white space */
    } else  if (argc <= ++optind) {
      /* no arg */
      place = EMSG;
      error("option requires an argument");
      return BADCH;
    } else {
      optarg = argv[optind];            /* white space */
    }
    place = EMSG;
    ++optind;
  }
  return optopt;                        /* return option letter */
}
#endif /* WIN32 */

/*----------------------------------------------------------------------------
 * WIN32 implementation of perror, does not deal very well with WSA errors
 * The stdlib.h version of perror only deals with the ancient XENIX error codes.
 *
 * +*+SAF Why can't all WSA errors go through GetLastError?  Most seem to...
 *--------------------------------------------------------------------------*/

#ifdef WIN32
void PrintWin32Error(FILE *stream, LPSTR text)
{
    LPSTR    szTemp;
    DWORD    dwResult;
    DWORD    dwError;

    dwError = GetLastError();
    dwResult = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM |FORMAT_MESSAGE_ARGUMENT_ARRAY,
        NULL,
        dwError,
        LANG_NEUTRAL,
        (LPTSTR)&szTemp,
        0,
        NULL );

    if (dwResult)
        fprintf(stream, "%s: %s\n", text, szTemp);
    else
        fprintf(stream, "%s: error 0x%x\n", text, dwError);
	fflush(stream);

    if (szTemp)
        LocalFree((HLOCAL)szTemp);
}
#endif /* WIN32 */

char *
nsec_enabled_to_str(int enabled) {
  switch (enabled) {
  case NSEC_UNKNOWN:
    return("Unknown");
  case NSEC_DISABLED:
    return("Disabled");
  case NSEC_PERMISSIVE:
    return("Permissive");
  case NSEC_ENFORCING:
    return("Enforcing");
  default:
    return("UNKNOWN MODE");
  }
}

char * nsec_type_to_str(int type) {
  switch (type) {
  case NSEC_TYPE_UNKNOWN:
    return("Unknown");
  case NSEC_TYPE_SELINUX:
    return("SELinux");
  default:
    return("UNKNOWN TYPE");
  }
}


char *
inet_ttos(int type)
{
  switch (type) {
  case SOCK_DGRAM:
    return("SOCK_DGRAM");
    break;
  case SOCK_STREAM:
    return("SOCK_STREAM");
    break;
#ifdef SOCK_DCCP
  case SOCK_DCCP:
    return("SOCK_DCCP");
#endif
#ifdef SOCK_SEQPACKET
  case SOCK_SEQPACKET:
    return("SOCK_SEQPACKET");
#endif
  default:
    return("SOCK_UNKNOWN");
  }
}




char unknown[32];

char *
inet_ptos(int protocol) {
  switch (protocol) {
  case IPPROTO_TCP:
    return("IPPROTO_TCP");
    break;
  case IPPROTO_UDP:
    return("IPPROTO_UDP");
    break;
#if defined(IPPROTO_SCTP)
  case IPPROTO_SCTP:
    return("IPPROTO_SCTP");
    break;
#endif
#if defined(IPPROTO_DCCP)
  case IPPROTO_DCCP:
    return "IPPROTO_DCCP";
    break;
#endif
#if defined(IPPROTO_UDPLITE)
  case IPPROTO_UDPLITE:
    return "IPPROTO_UDPLITE";
    break;
#endif
  default:
    snprintf(unknown,sizeof(unknown),"IPPROTO_UNKNOWN(%d)",protocol);
    return(unknown);
  }
}

/* one of these days, this should not be required */
#ifndef AF_INET_SDP
#define AF_INET_SDP 27
#define PF_INET_SDP AF_INET_SDP
#endif

char *
inet_ftos(int family)
{
  switch(family) {
  case AF_INET:
    return("AF_INET");
#if defined(AF_INET6)
  case AF_INET6:
    return("AF_INET6");
#endif
#if defined(AF_INET_SDP)
  case AF_INET_SDP:
    return("AF_INET_SDP");
#endif
#if defined(AF_RDS)
  case AF_RDS:
    return("AF_RDS");
#endif
  default:
    return("AF_UNSPEC");
  }
}

int
inet_nton(int af, const void *src, char *dst, int cnt)

{

  switch (af) {
  case AF_INET:
    /* magic constants again... :) */
    if (cnt >= 4) {
      memcpy(dst,src,4);
      return 4;
    }
    else {
      Set_errno(ENOSPC);
      return(-1);
    }
    break;
#if defined(AF_INET6)
  case AF_INET6:
    if (cnt >= 16) {
      memcpy(dst,src,16);
      return(16);
    }
    else {
      Set_errno(ENOSPC);
      return(-1);
    }
    break;
#endif
#if defined(AF_RDS)
  case AF_RDS:
    if (cnt >= 4) {
      memcpy(dst,src,4);
      return 4;
    }
#endif
  default:
    Set_errno(EAFNOSUPPORT);
    return(-1);
  }
}

double
ntohd(double net_double)

{
  /* we rely on things being nicely packed */
  union {
    double whole_thing;
    unsigned int words[2];
    unsigned char bytes[8];
  } conv_rec;

  unsigned char scratch;
  int i;

  /* on those systems where ntohl is a no-op, we want to return the
     original value, unchanged */

  if (ntohl(1L) == 1L) {
    return(net_double);
  }

  conv_rec.whole_thing = net_double;

  /* we know that in the message passing routines that ntohl will have
     been called on the 32 bit quantities. we need to put those back
     the way they belong before we swap */
  conv_rec.words[0] = htonl(conv_rec.words[0]);
  conv_rec.words[1] = htonl(conv_rec.words[1]);

  /* now swap */
  for (i=0; i<= 3; i++) {
    scratch = conv_rec.bytes[i];
    conv_rec.bytes[i] = conv_rec.bytes[7-i];
    conv_rec.bytes[7-i] = scratch;
  }

#if defined(__FLOAT_WORD_ORDER) && defined(__BYTE_ORDER)
  if (__FLOAT_WORD_ORDER != __BYTE_ORDER) {
    /* Fixup mixed endian floating point machines */
    unsigned int scratch = conv_rec.words[0];
    conv_rec.words[0] = conv_rec.words[1];
    conv_rec.words[1] = scratch;
  }
#endif

  return(conv_rec.whole_thing);

}

double
htond(double host_double)

{
  /* we rely on things being nicely packed */
  union {
    double whole_thing;
    unsigned int words[2];
    unsigned char bytes[8];
  } conv_rec;

  unsigned char scratch;
  int i;

  /* on those systems where ntohl is a no-op, we want to return the
     original value, unchanged */

  if (ntohl(1L) == 1L) {
    return(host_double);
  }

  conv_rec.whole_thing = host_double;

  /* now swap */
  for (i=0; i<= 3; i++) {
    scratch = conv_rec.bytes[i];
    conv_rec.bytes[i] = conv_rec.bytes[7-i];
    conv_rec.bytes[7-i] = scratch;
  }

#if defined(__FLOAT_WORD_ORDER) && defined(__BYTE_ORDER)
  if (__FLOAT_WORD_ORDER != __BYTE_ORDER) {
    /* Fixup mixed endian floating point machines */
    unsigned int scratch = conv_rec.words[0];
    conv_rec.words[0] = conv_rec.words[1];
    conv_rec.words[1] = scratch;
  }
#endif

  /* we know that in the message passing routines htonl will be called
     on the 32 bit quantities. we need to set things up so that when
     this happens, the proper order will go out on the network */
  conv_rec.words[0] = htonl(conv_rec.words[0]);
  conv_rec.words[1] = htonl(conv_rec.words[1]);

  return(conv_rec.whole_thing);

}



/* The original patch from Google used lrand48, but I have been
   informed that is not easily available under Windows. So, rather
   than have some #ifdefs here I'll just simplistically replace
   lrand48 with rand(), which should be "good enough" at some point it
   may be sufficient to just call rand() directly rather than call
   this raj 20101130 */

unsigned int
rand32(){
  return (unsigned int)rand() * 2 + rand() % 2;
}

/* this routine will set the ip address of the sockaddr in the
   addrinfo to a random number in range, based on the address
   family. for grins, we will sanity check the value of mask_len
   against the address family. initial version from google,
   enhancements by raj 20101129 */
void
random_ip_address(struct addrinfo *res, int mask_len)
{
  switch(res->ai_family) {
  case AF_INET: {
    struct sockaddr_in *foo = (struct sockaddr_in *)res->ai_addr;
    unsigned int addr = ntohl(foo->sin_addr.s_addr);
    unsigned int mask = ((unsigned int)1 << (32 - mask_len)) - 1;

    if ((mask_len < 0) || (mask_len > 32)) {
      fprintf(where,
	      "Mask length must be between 0 and 32 inclusive for AF_INET\n");
      fflush(where);
      exit(-1);
    }

    addr = ntohl(foo->sin_addr.s_addr);
    do {
      addr = (addr & ~mask) | (rand32() & mask);
    } while ((addr & 0xff) == 0xff);
    foo->sin_addr.s_addr = htonl(addr);
    break;
  }
#if defined(AF_INET6)
  case AF_INET6: {
    struct sockaddr_in6 *foo = (struct sockaddr_in6 *)res->ai_addr;

    unsigned int i, len;
    unsigned int *addr = (unsigned int *)&(foo->sin6_addr.s6_addr);
    unsigned int mask;

    if ((mask_len < 0) || (mask_len > 128)) {
      fprintf(where,
	      "Mask length must be between 0 and 128 inclusive for AF_INET\n");
      fflush(where);
      exit(-1);
    }

    for (i = 0; i < 4; i ++){
      addr[i] = ntohl(addr[i]);
      len = mask_len - i * 32;
      len = ((len < 32) ? len : 32);
      len = ((len > 0) ? len : 0);
      mask = ((unsigned int)1 << (32 - len)) - 1;
      addr[i] = (addr[i] & ~mask) | (rand32() & mask);
      addr[i] = htonl(addr[i]);
     }
    break;
  }
#endif
  default:
    fprintf(where,
            "Unexpected Address Family of %u\n",res->ai_family);
    fflush(where);
    exit(-1);
  }
}

#if defined(HAVE_SENDFILE)
int netperf_sendfile(SOCKET send_socket, struct ring_elt *send_ring) {

  int  len;
  int  ret = 0;

#if defined(__linux) || defined(__sun)
  off_t     scratch_offset;   /* the linux sendfile() call will update
				 the offset variable, which is
				 something we do _not_ want to happen
				 to the value in the send_ring! so, we
				 have to use a scratch variable. */
#endif /* __linux  || defined(__sun) */

#if defined (__sun)
   size_t  scratch_len;	/* the sun sendfilev() needs a place to
			   tell us how many bytes were written,
			   even though it also returns the value */
   sendfilevec_t sv;
#endif /* __sun */

      /* you can look at netlib.h for a description of the fields we
	 are passing to sendfile(). 08/2000 */
#if defined(__linux)
      scratch_offset = send_ring->offset;
      len=sendfile(send_socket,
		   send_ring->fildes,
		   &scratch_offset,   /* modified after the call! */
		   send_ring->length);
#elif defined (__sun)
      /* We must call with SFV_NOWAIT and a large file size (>= 16MB)
	 to get zero-copy, as well as compiling with
	 -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 */
      sv.sfv_fd = send_ring->fildes;
      sv.sfv_flag = SFV_NOWAIT;
      sv.sfv_off = send_ring->offset;
      sv.sfv_len =  send_ring->length;
      len = sendfilev(send_socket, &sv, 1, &scratch_len);
#elif defined(__FreeBSD__)
      /* so close to HP-UX and yet so far away... :) */
      ret = sendfile(send_ring->fildes,
		     send_socket,
		     send_ring->offset,
		     send_ring->length,
		     NULL,
		     (off_t *)&len,
		     send_ring->flags);
#elif defined(USE_OSX)
      len = send_ring->length;
      ret = sendfile(send_ring->fildes,
		     send_socket,
		     send_ring->offset,
		     (off_t *)&len,
		     NULL,
		     send_ring->flags);
#else /* original sendile HP-UX */
      len=sendfile(send_socket,
		   send_ring->fildes,
		   send_ring->offset,
		   send_ring->length,
		   send_ring->hdtrl,
		   send_ring->flags);
#endif

      /* for OSX and FreeBSD, a non-zero ret means something failed.
	 I would hope that the length fields are set to -1 or the
	 like, but at the moment I do not know I can count on
	 that. for other platforms, ret will be set to zero and we can
	 rely directly on len. raj 2013-05-01 */
      if (ret != 0)
	return -1;
      else
	return len;

}
#endif


/* one of these days, this should be abstracted-out just like the CPU
   util stuff.  raj 2005-01-27 */
int
get_num_cpus()

{

  /* on HP-UX, even when we use the looper procs we need the pstat */
  /* call */

  int temp_cpus;

#ifdef __hpux
#include <sys/pstat.h>

  struct pst_dynamic psd;

  if (pstat_getdynamic((struct pst_dynamic *)&psd,
                       (size_t)sizeof(psd), (size_t)1, 0) != -1) {
    temp_cpus = psd.psd_proc_cnt;
  }
  else {
    temp_cpus = 1;
  }

#else
  /* MW: <unistd.h> was included for non-Windows systems above. */
  /* Thus if _SC_NPROC_ONLN is defined, we should be able to use sysconf. */
#ifdef _SC_NPROCESSORS_ONLN
  temp_cpus = sysconf(_SC_NPROCESSORS_ONLN);

#ifdef USE_PERFSTAT
  temp_cpus = perfstat_cpu(NULL,NULL, sizeof(perfstat_cpu_t), 0);
#endif /* USE_PERFSTAT */

#else /* no _SC_NPROCESSORS_ONLN */

#ifdef WIN32
  SYSTEM_INFO SystemInfo;
  GetSystemInfo(&SystemInfo);

  temp_cpus = SystemInfo.dwNumberOfProcessors;
#else
  /* we need to know some other ways to do this, or just fall-back on
     a global command line option - raj 4/95 */
  temp_cpus = shell_num_cpus;
#endif  /* WIN32 */
#endif /* _SC_NPROCESSORS_ONLN */
#endif /*  __hpux */

  if (temp_cpus > MAXCPUS) {
    fprintf(where,
            "Sorry, this system has more CPUs (%d) than I can handle (%d).\n"
            "Please alter MAXCPUS in netlib.h and recompile.\n",
            temp_cpus,
            MAXCPUS);
    fflush(where);
    exit(1);
  }

  return(temp_cpus);

}

#ifdef WIN32
#ifdef __GNUC__
  #define S64_SUFFIX(x) x##LL
#else
  #define S64_SUFFIX(x) x##i64
#endif

/*
 * Number of 100 nanosecond units from 1/1/1601 to 1/1/1970
 */
#define EPOCH_BIAS  S64_SUFFIX(116444736000000000)

/*
 * Union to facilitate converting from FILETIME to unsigned __int64
 */
typedef union {
        unsigned __int64 ft_scalar;
        FILETIME ft_struct;
} FT;

void
gettimeofday( struct timeval *tv , struct timezone *not_used )
{
        FT nt_time;
        __int64 UnixTime;  /* microseconds since 1/1/1970 */

        GetSystemTimeAsFileTime( &(nt_time.ft_struct) );

        UnixTime = ((nt_time.ft_scalar - EPOCH_BIAS) / S64_SUFFIX(10));
        tv->tv_sec = (long)(time_t)(UnixTime / S64_SUFFIX(1000000));
        tv->tv_usec = (unsigned long)(UnixTime % S64_SUFFIX(1000000));
}
#endif /* WIN32 */


 /* this routine will disable any running timer */
void
stop_timer()
{
#ifndef WIN32
  alarm(0);
#else
  /* at some point we may need some win32 equivalent */
  if (hAlarm != (HANDLE) INVALID_HANDLE_VALUE) {
    SetEvent(hAlarm);
  }
#endif /* WIN32 */

}



/************************************************************************/
/*                                                                      */
/*      signal catcher                                                  */
/*                                                                      */
/************************************************************************/
#ifndef WIN32
void
#if defined(__hpux)
catcher(sig, code, scp)
     int sig;
     int code;
     struct sigcontext *scp;
#else
catcher(int sig)
#endif /* __hpux || __VMS */
{

#ifdef __hpux
  if (debug > 2) {
    fprintf(where,"caught signal %d ",sig);
    if (scp) {
      fprintf(where,"while in syscall %d\n",
              scp->sc_syscall);
    }
    else {
      fprintf(where,"null scp\n");
    }
    fflush(where);
  }
#endif /* RAJ_DEBUG */

  switch(sig) {

  case SIGINT:
    times_up = 1;
    break;
  case SIGALRM:
   if (--test_len_ticks == 0) {
      /* the test is over */
      if (times_up != 0) {
        fprintf(where,"catcher: timer popped with times_up != 0\n");
        fflush(where);
      }
      times_up = 1;
#if defined(WANT_INTERVALS) && !defined(WANT_SPIN)
      stop_itimer();
      /* we should also stop the normal test timer lest it fire at an
	 inopportune moment - we do not know if we got here off the
	 interval timer or the test timer... */
      stop_timer();
#endif /* WANT_INTERVALS */
      break;
    }
    else {
#ifdef WANT_INTERVALS
#ifdef __hpux
      /* the test is not over yet and we must have been using the
	 interval timer. if we were in SYS_SIGSUSPEND we want to
	 re-start the system call. Otherwise, we want to get out of
	 the sigsuspend call. I NEED TO KNOW HOW TO DO THIS FOR OTHER
	 OPERATING SYSTEMS. If you know how, please let me know. rick
	 jones <rick.jones2@hp.com> */
      if (scp->sc_syscall != SYS_SIGSUSPEND) {
        if (debug > 2) {
          fprintf(where,
                  "catcher: Time to send burst > interval!\n");
          fflush(where);
        }
        scp->sc_syscall_action = SIG_RESTART;
      }
#endif /* __hpux */
#else /* WANT_INTERVALS */
      fprintf(where,
              "catcher: interval timer running unexpectedly!\n");
      fflush(where);
      times_up = 1;
#endif /* WANT_INTERVALS */
      break;
    }
  }
  return;
}
#endif /* WIN32 */

void
install_signal_catchers()

{
  /* just a simple little routine to catch a bunch of signals */

#ifndef WIN32
  struct sigaction action;
  int i;

  fprintf(where,"installing catcher for all signals\n");
  fflush(where);

  sigemptyset(&(action.sa_mask));
  action.sa_handler = catcher;

#ifdef SA_INTERRUPT
  action.sa_flags = SA_INTERRUPT;
#else /* SA_INTERRUPT */
  action.sa_flags = 0;
#endif /* SA_INTERRUPT */


  for (i = 1; i <= NSIG; i++) {
    switch (i) {
    case SIGALRM:
    case SIGPROF:
    case SIGSTOP:
    case SIGKILL:
      break;
    default:
      if (sigaction(i,&action,NULL) != 0) {
        fprintf(where,
                "Could not install signal catcher for sig %d, errno %d\n",
                i,
                errno);
        fflush(where);

      }
    }
  }
#else
  return;
#endif /* WIN32 */
}


#ifdef WIN32
#define SIGALRM (14)
void
emulate_alarm( int seconds )
{
  DWORD ErrorCode;
  DWORD HandlesClosedFlags = 0;

  /* Wait on this event for parm seconds. */

  ErrorCode = WaitForSingleObject(hAlarm, seconds*1000);
  if (ErrorCode == WAIT_FAILED)
    {
      perror("WaitForSingleObject failed");
      exit(1);
    }

  if (ErrorCode == WAIT_TIMEOUT)
    {
      /* WaitForSingleObject timed out; this means the timer
	 wasn't canceled. */

      times_up = 1;

      /* Give the other threads time to notice that times_up has
	 changed state before taking the harsh step of closing the
	 sockets. */
      timed_out=0;
      if (WaitForSingleObject(hAlarm, PAD_TIME/2*1000) ==
	  WAIT_TIMEOUT) {
        timed_out=1;
	/* We have yet to find a good way to fully emulate
	   the effects of signals and getting EINTR from
	   system calls under winsock, so what we do here is
	   close the socket out from under the other thread.
	   It is rather kludgy, but should be sufficient to
	   get this puppy shipped.  The concept can be
	   attributed/blamed :) on Robin raj 1/96 */

	if (win_kludge_socket != INVALID_SOCKET) {
	  HandlesClosedFlags |= 1;
	  closesocket(win_kludge_socket);
	}
	if (win_kludge_socket2 != INVALID_SOCKET) {
	  HandlesClosedFlags |= 2;
	  closesocket(win_kludge_socket2);
	}
      }
      if(debug) {
	fprintf(where,
		"emulate_alarm - HandlesClosedFlags: %x\n",
		HandlesClosedFlags);
	fflush(where);
      }
    }
}


#endif /* WIN32 */

void
start_timer(int time)
{

#ifdef WIN32
  /*+*+SAF What if StartTimer is called twice without the first timer */
  /*+*+SAF expiring? */

  DWORD  thread_id ;
  HANDLE tHandle;

  if (hAlarm == (HANDLE) INVALID_HANDLE_VALUE)
    {
      /* Create the Alarm event object */
      hAlarm = CreateEvent(
			   (LPSECURITY_ATTRIBUTES) NULL, /* no security */
			   FALSE,	 /* auto reset event */
			   FALSE,   /* init. state = reset */
			   (void *)NULL);  /* unnamed event object */
      if (hAlarm == (HANDLE) INVALID_HANDLE_VALUE)
	{
	  perror("CreateEvent failure");
	  exit(1);
	}
    }
  else
    {
      ResetEvent(hAlarm);
    }


  tHandle = CreateThread(0,
			 0,
			 (LPTHREAD_START_ROUTINE)emulate_alarm,
			 (LPVOID)(ULONG_PTR)time,
			 0,
			 &thread_id ) ;
  CloseHandle(tHandle);

#else /* not WIN32 */

struct sigaction action;
int ret;

if (debug) {
  fprintf(where,"About to start a timer for %d seconds.\n",time);
  fflush(where);
}

  action.sa_handler = catcher;

#ifdef SA_INTERRUPT
  /* on some systems (SunOS 4.blah), system calls are restarted. we do */
  /* not want that */
  action.sa_flags = SA_INTERRUPT;
#else /* SA_INTERRUPT */
  action.sa_flags = 0;
#endif /* SA_INTERRUPT */

  sigemptyset(&(action.sa_mask));
  sigaddset(&(action.sa_mask),SIGALRM);
  if (sigaction(SIGALRM, &action, NULL) < 0) {
    fprintf(where,
	    "start_timer: error installing alarm handler errno %d\n",
	    errno);
    fflush(where);
    exit(-1);
  }

  sigemptyset(&(action.sa_mask));
  sigaddset(&(action.sa_mask),SIGINT);
  if (sigaction(SIGINT, &action, NULL) < 0) {
    fprintf(where,
	    "start_timer: error installing SIGINT handler errno %d\n",
	    errno);
    fflush(where);
    exit(-1);
  }

  /* this is the easy case - just set the timer for so many seconds */
  ret = alarm(time);
  if (ret != 0) {
    fprintf(where,
            "error starting alarm timer, ret %d errno %d\n",
	    ret,
            errno);
    fflush(where);
    exit(-1);
  }
#endif /* WIN32 */

  test_len_ticks = 1;

}



#ifdef WANT_INTERVALS
/* this routine will enable the interval timer and set things up so
   that for a timed test the test will end at the proper time. it
   should detect the presence of POSIX.4 timer_* routines one of these
   days */
void
start_itimer(unsigned int interval_len_msec )
{
#ifdef WIN32
    LARGE_INTEGER liDueTime;
	TIMECAPS ptc;
	MMRESULT mmr;

	/* make sure timer resolution is at least as small as interval length */
	timerRes=interval_len_msec;
	mmr=timeGetDevCaps(&ptc, sizeof (ptc));
	if (mmr==TIMERR_NOERROR){
	  if (interval_len_msec<ptc.wPeriodMin){
		timerRes=ptc.wPeriodMin;
		fprintf(where, "Timer cannot be set to %dmsec.  Minimum timer resolution: %d\n", interval_len_msec, ptc.wPeriodMin);
		fflush(where);
	  }
	}
	/* timeBeginPeriod() affects a global Windows setting.
	Windows uses the lowest value (that is, highest resolution) requested by any process. */
	mmr=timeBeginPeriod(timerRes);
	/* Create a waitable timer. */
    WinTimer = CreateWaitableTimer(NULL, FALSE, "IntervalTimer");
    if (NULL == WinTimer)
    {
        fprintf(where, "CreateWaitableTimer failed (%d)\n", GetLastError());
        fflush(where);
		exit(1);
    }
 	/*The time after which the state of the timer is to be set to signaled the first time,
	in 100 nanosecond intervals.  Negative values indicate relative time. */
    liDueTime.QuadPart=-10000LL*interval_len_msec;
   /* Set the timer to wait for interval_len_msec and periodically signal every interval_len_msec */
    if (!SetWaitableTimer(WinTimer, &liDueTime, interval_len_msec, NULL, NULL, TRUE))
    {
        fprintf(where,"SetWaitableTimer failed (%d)\n", GetLastError());
        fflush(where);
		exit(1);
    }
#else
  unsigned int ticks_per_itvl;

  struct itimerval new_interval;
  struct itimerval old_interval;

  /* if -DWANT_INTERVALS was used, we will use the ticking of the
     itimer to tell us when the test is over. while the user will be
     specifying some number of milliseconds, we know that the interval
     timer is really in units of 1/HZ. so, to prevent the test from
     running "long" it would be necessary to keep this in mind when
     calculating the number of itimer events */

  ticks_per_itvl = ((interval_wate * sysconf(_SC_CLK_TCK) * 1000) /
                    1000000);

  if (ticks_per_itvl == 0) ticks_per_itvl = 1;

  /* how many usecs in each interval? */
  usec_per_itvl = ticks_per_itvl * (1000000 / sysconf(_SC_CLK_TCK));

  /* how many times will the timer pop before the test is over? */
  if (test_time > 0) {
    /* this was a timed test */
    test_len_ticks = (test_time * 1000000) / usec_per_itvl;
  }
  else {
    /* this was not a timed test, use MAXINT */
    test_len_ticks = INT_MAX;
  }

  if (debug) {
    fprintf(where,
	    "setting the interval timer to %d sec %d usec test len %d ticks\n",
            usec_per_itvl / 1000000,
            usec_per_itvl % 1000000,
            test_len_ticks);
    fflush(where);
  }

  /* if this was not a timed test, then we really aught to enable the
     signal catcher raj 2/95 */

  new_interval.it_interval.tv_sec = usec_per_itvl / 1000000;
  new_interval.it_interval.tv_usec = usec_per_itvl % 1000000;
  new_interval.it_value.tv_sec = usec_per_itvl / 1000000;
  new_interval.it_value.tv_usec = usec_per_itvl % 1000000;
  if (setitimer(ITIMER_REAL,&new_interval,&old_interval) != 0) {
    /* there was a problem arming the interval timer */
    perror("netperf: setitimer");
    exit(1);
  }
 #endif /* WIN32*/
}
#endif /* WANT_INTERVALS */

void
netlib_init_cpu_map() {

  int i;
#ifdef HAVE_MPCTL
  int num;
  i = 0;
  /* I go back and forth on whether this should be the system-wide set
     of calls, or if the processor set versions (sans the _SYS) should
     be used.  at the moment I believe that the system-wide version
     should be used. raj 2006-04-03 */
  num = mpctl(MPC_GETNUMSPUS_SYS,0,0);
  lib_cpu_map[i] = mpctl(MPC_GETFIRSTSPU_SYS,0,0);
  for (i = 1;((i < num) && (i < MAXCPUS)); i++) {
    lib_cpu_map[i] = mpctl(MPC_GETNEXTSPU_SYS,lib_cpu_map[i-1],0);
  }
  /* from here, we set them all to -1 because if we launch more
     loopers than actual CPUs, well, I'm not sure why :) */
  for (; i < MAXCPUS; i++) {
    lib_cpu_map[i] = -1;
  }

#else
  /* we assume that there is indeed a contiguous mapping */
  for (i = 0; i < MAXCPUS; i++) {
    lib_cpu_map[i] = i;
  }
#endif
}



/****************************************************************/
/*                                                              */
/*      netlib_init()                                           */
/*                                                              */
/*      initialize the performance library...                   */
/*                                                              */
/****************************************************************/

void
netlib_init()
{
  int i;

  where = stdout;

  request_array = (int *)(&netperf_request);
  response_array = (int *)(&netperf_response);

  for (i = 0; i < MAXCPUS; i++) {
    lib_local_per_cpu_util[i] = -1.0;
  }

  lib_local_cpu_stats.peak_cpu_id = -1;
  lib_local_cpu_stats.peak_cpu_util = -1.0;
  lib_remote_cpu_stats.peak_cpu_id = -1;
  lib_remote_cpu_stats.peak_cpu_util = -1.0;

  netperf_version = strdup(NETPERF_VERSION);

  /* on those systems where we know that CPU numbers may not start at
     zero and be contiguous, we provide a way to map from a
     contiguous, starting from 0 CPU id space to the actual CPU ids.
     at present this is only used for the netcpu_looper stuff because
     we ass-u-me that someone setting processor affinity from the
     netperf commandline will provide a "proper" CPU identifier. raj
     2006-04-03 */

  netlib_init_cpu_map();

  if (debug) {
    fprintf(where,
            "netlib_init: request_array at %p\n"
            "netlib_init: response_array at %p\n",
            request_array,
            response_array);
    fflush(where);
  }

  /* some functionality might want to use random numbers, so we should
     initialize the random number generator */
  srand(getpid());

}

/* this routine will conver the string into an unsigned integer. it is
   used primarily for the command-line options taking a number (such
   as the socket size) which could be rather large. If someone enters
   32M, then the number will be converted to 32 * 1024 * 1024.  If
   they inter 32m, the number will be converted to 32 * 1000 * 1000 */
unsigned int
convert(char *string)

{
  unsigned int base;
  base = atoi(string);
  if (strstr(string,"K")) {
    base *= 1024;
  }
  if (strstr(string,"M")) {
    base *= (1024 * 1024);
  }
  if (strstr(string,"G")) {
    base *= (1024 * 1024 * 1024);
  }
  if (strstr(string,"k")) {
    base *= (1000);
  }
  if (strstr(string,"m")) {
    base *= (1000 * 1000);
  }
  if (strstr(string,"g")) {
    base *= (1000 * 1000 * 1000);
  }
  return(base);
}

/* this routine is like convert, but it is used for an interval time
   specification instead of stuff like socket buffer or send sizes.
   it converts everything to microseconds for internal use.  if there
   is an 'm' at the end it assumes the user provided milliseconds, s
   will imply seconds, u will imply microseconds.  in the future n
   will imply nanoseconds but for now it will be ignored. if there is
   no suffix or an unrecognized suffix, it will be assumed the user
   provided milliseconds, which was the long-time netperf default. one
   of these days, we should probably revisit that nanosecond business
   wrt the return value being just an int rather than a uint64_t or
   something.  raj 2006-02-06 */

unsigned int
convert_timespec(char *string) {

  unsigned int base;
  base = atoi(string);
  if (strstr(string,"m")) {
    base *= 1000;
  }
  else if (strstr(string,"u")) {
    base *= (1);
  }
  else if (strstr(string,"s")) {
    base *= (1000 * 1000);
  }
  else {
    base *= (1000);
  }
  return(base);
}


/* this routine will allocate a circular list of buffers for either
   send or receive operations. each of these buffers will be aligned
   and offset as per the users request. the circumference of this ring
   will be controlled by the setting of width. the buffers will be
   filled with data from the file specified in fill_file. if fill_file
   is an empty string, the buffers will be filled from "default_fill"
   which will be "netperf" so anyone sniffing the traffic will have a
   better idea what this traffic happens to be. */

struct ring_elt *
allocate_buffer_ring(int width, int buffer_size, int alignment, int offset)
{

  struct ring_elt *first_link = NULL;
  struct ring_elt *temp_link  = NULL;
  struct ring_elt *prev_link;

  int i;
  int malloc_size;
  int bytes_left;
  int bytes_read;
  int do_fill;

  FILE *fill_source;
  char default_fill[] = "netperf";
  int  fill_cursor = 0;

  malloc_size = buffer_size + alignment + offset;

  /* did the user wish to have the buffers pre-filled with data from a */
  /* particular source? */
  if (strcmp(local_fill_file,"") == 0) {
    do_fill = 0;
    fill_source = NULL;
  }
  else {
    do_fill = 1;
    fill_source = (FILE *)fopen(local_fill_file,"r");
    if (fill_source == (FILE *)NULL) {
      fprintf(where,"Could not open requested fill file: %s\n",
	      strerror(errno));
      fflush(where);
    }
  }

  assert(width >= 1);

  prev_link = NULL;
  for (i = 1; i <= width; i++) {
    /* get the ring element */
    temp_link = (struct ring_elt *)malloc(sizeof(struct ring_elt));
    if (temp_link == NULL) {
      fprintf(where,
	      "malloc(%u) failed!\n",
	      (unsigned int)sizeof(struct ring_elt));
      exit(-1);
    }
    temp_link->completion_ptr = NULL;
    /* remember the first one so we can close the ring at the end */
    if (i == 1) {
      first_link = temp_link;
    }
    temp_link->buffer_base = (char *)malloc(malloc_size);
    if (temp_link->buffer_base == NULL) {
      fprintf(where,
	      "malloc(%d) failed!\n",
	      malloc_size);
      exit(-1);
    }

#ifndef WIN32
    temp_link->buffer_ptr = (char *)(( (long)(temp_link->buffer_base) +
                          (long)alignment - 1) &
                         ~((long)alignment - 1));
#else
    temp_link->buffer_ptr = (char *)(( (ULONG_PTR)(temp_link->buffer_base) +
                          (ULONG_PTR)alignment - 1) &
                         ~((ULONG_PTR)alignment - 1));
#endif
    temp_link->buffer_ptr += offset;
    /* is where the buffer fill code goes. */
    if (do_fill) {
      char *bufptr = temp_link->buffer_ptr;
      bytes_left = buffer_size;
      while (bytes_left) {
        if (((bytes_read = (int)fread(bufptr,
				      1,
				      bytes_left,
				      fill_source)) == 0) &&
            (feof(fill_source))){
          rewind(fill_source);
        }
	bufptr += bytes_read;
        bytes_left -= bytes_read;
      }
    }
    else {
      /* use the default fill to ID our data traffic on the
	 network. it ain't exactly pretty, but it should work */
      int j;
      char *bufptr = temp_link->buffer_ptr;
      for (j = 0; j < buffer_size; j++) {
	bufptr[j] = default_fill[fill_cursor];
	fill_cursor += 1;
	/* the Windows DDK compiler with an x86_64 target wants a cast
	   here */
	if (fill_cursor >  (int)strlen(default_fill)) {
	  fill_cursor = 0;
	}
      }

    }
    temp_link->next = prev_link;
    prev_link = temp_link;
  }
  if (first_link) {  /* SAF Prefast made me do it... */
    first_link->next = temp_link;
  }

  return(first_link); /* it's a circle, doesn't matter which we return */
}

/* this routine will dirty the first dirty_count bytes of the
   specified buffer and/or read clean_count bytes from the buffer. it
   will go N bytes at a time, the only question is how large should N
   be and if we should be going continguously, or based on some
   assumption of cache line size */

void
access_buffer(char *buffer_ptr,int length, int dirty_count, int clean_count) {

  char *temp_buffer;
  char *limit;
  int  i, dirty_totals;

  temp_buffer = buffer_ptr;
  limit = temp_buffer + length;
  dirty_totals = 0;

  for (i = 0;
       ((i < dirty_count) && (temp_buffer < limit));
       i++) {
    *temp_buffer += (char)i;
    dirty_totals += *temp_buffer;
    temp_buffer++;
  }

  for (i = 0;
       ((i < clean_count) && (temp_buffer < limit));
       i++) {
    dirty_totals += *temp_buffer;
    temp_buffer++;
  }

  if (debug > 100) {
    fprintf(where,
	    "This was here to try to avoid dead-code elimination %d\n",
	    dirty_totals);
    fflush(where);
  }
}


#ifdef HAVE_ICSC_EXS

#include <sys/mman.h>
#include <sys/exs.h>

/* this routine will allocate a circular list of buffers for either
   send or receive operations. each of these buffers will be aligned
   and offset as per the users request. the circumference of this ring
   will be controlled by the setting of send_width. the buffers will
   be filled with data from the file specified in local_fill_file. if
   local_fill_file is an empty string, the buffers will not be filled with
   any particular data */

struct ring_elt *
allocate_exs_buffer_ring (int width, int buffer_size, int alignment, int offset, exs_mhandle_t *mhandlep)
{

    struct ring_elt *first_link;
    struct ring_elt *temp_link;
    struct ring_elt *prev_link;

    int i;
    int malloc_size;
    int bytes_left;
    int bytes_read;
    int do_fill;

    FILE *fill_source;

    int mmap_size;
    char *mmap_buffer, *mmap_buffer_aligned;

    malloc_size = buffer_size + alignment + offset;

    /* did the user wish to have the buffers pre-filled with data from a */
    /* particular source? */
    if (strcmp (local_fill_file, "") == 0) {
        do_fill = 0;
        fill_source = NULL;
    } else {
        do_fill = 1;
        fill_source = (FILE *) fopen (local_fill_file, "r");
        if (fill_source == (FILE *) NULL) {
            perror ("Could not open requested fill file");
            exit (1);
        }
    }

    assert (width >= 1);

    if (debug) {
        fprintf (where,
		 "allocate_exs_buffer_ring: "
                 "width=%d buffer_size=%d alignment=%d offset=%d\n",
                 width, buffer_size, alignment, offset);
    }

    /* allocate shared memory */
    mmap_size = width * malloc_size;
    mmap_buffer = (char *) mmap ((caddr_t)NULL, mmap_size+NBPG-1,
                                 PROT_READ|PROT_WRITE,
                                 MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if (mmap_buffer == NULL) {
        perror ("allocate_exs_buffer_ring: mmap failed");
        exit (1);
    }
    mmap_buffer_aligned = (char *) ((uintptr_t)mmap_buffer & ~(NBPG-1));
    if (debug) {
        fprintf (where,
		 "allocate_exs_buffer_ring: "
                 "mmap buffer size=%d address=0x%p aligned=0x%p\n",
                 mmap_size, mmap_buffer, mmap_buffer_aligned);
    }

    /* register shared memory */
    *mhandlep = exs_mregister ((void *)mmap_buffer_aligned, (size_t)mmap_size, 0);
    if (*mhandlep == EXS_MHANDLE_INVALID) {
        perror ("allocate_exs_buffer_ring: exs_mregister failed");
        exit (1);
    }
    if (debug) {
        fprintf (where, "allocate_exs_buffer_ring: mhandle=%d\n",
                 *mhandlep);
    }

    /* allocate ring elements */
    first_link = (struct ring_elt *) malloc (width * sizeof (struct ring_elt));
    if (first_link == NULL) {
        printf ("malloc(%d) failed!\n", width * sizeof (struct ring_elt));
        exit (1);
    }

    /* initialize buffer ring */
    prev_link = first_link + width - 1;

    for (i = 0, temp_link = first_link; i < width; i++, temp_link++) {

        temp_link->buffer_base = (char *) mmap_buffer_aligned + (i*malloc_size);
#ifndef WIN32
        temp_link->buffer_ptr = (char *)
            (((long)temp_link->buffer_base + (long)alignment - 1) &
             ~((long)alignment - 1));
#else
        temp_link->buffer_ptr = (char *)
            (((ULONG_PTR)temp_link->buffer_base + (ULONG_PTR)alignment - 1) &
             ~((ULONG_PTR)alignment - 1));
#endif
        temp_link->buffer_ptr += offset;

        if (debug) {
            fprintf (where, "allocate_exs_buffer_ring: "
                     "buffer: index=%d base=0x%p ptr=0x%p\n",
                     i, temp_link->buffer_base, temp_link->buffer_ptr);
        }

        /* is where the buffer fill code goes. */
        if (do_fill) {
            bytes_left = buffer_size;
            while (bytes_left) {
                if (((bytes_read = (int) fread (temp_link->buffer_ptr,
                                                1,
                                                bytes_left,
                                                fill_source)) == 0) &&
                    (feof (fill_source))) {
                    rewind (fill_source);
                }
                bytes_left -= bytes_read;
            }
        }

        /* do linking */
        prev_link->next = temp_link;
        prev_link = temp_link;
    }

    return (first_link);  /* it is a circle, doesn't matter which we return */
}

#endif /* HAVE_ICSC_EXS */



#ifdef HAVE_SENDFILE
/* this routine will construct a ring of sendfile_ring_elt structs
   that the routine sendfile_tcp_stream() will use to get parameters
   to its calls to sendfile(). It will setup the ring to point at the
   file specified in the global -F option that is already used to
   pre-fill buffers in the send() case. 08/2000

   if there is no file specified in a global -F option, we will create
   a tempoarary file and fill it with random data and use that
   instead.  raj 2007-08-09 */

struct ring_elt *
alloc_sendfile_buf_ring(int width,
                        int buffer_size,
                        int alignment,
                        int offset)

{

  struct ring_elt *first_link = NULL;
  struct ring_elt *temp_link  = NULL;
  struct ring_elt *prev_link;

  int i;
  int fildes;
  struct stat statbuf;

  /* if the user has not specified a file with the -F option, we will
     fail the test. otherwise, go ahead and try to open the
     file. 08/2000 */
  if (strcmp(local_fill_file,"") == 0) {
    /* use an temp file for the fill file */
    char temp_file[] = {"netperfXXXXXX\0"};
    int *temp_buffer;

    /* make sure we have at least an ints worth, even if the user is
       using an insane buffer size for a sendfile test. we are
       ass-u-me-ing that malloc will return something at least aligned
       on an int boundary... */
    temp_buffer = (int *) malloc(buffer_size + sizeof(int));
    if (temp_buffer) {
      /* ok, we have the buffer we are going to write, lets get a
	 temporary filename */
      fildes = mkstemp(temp_file);
      /* no need to call open because mkstemp did it */
	if (-1 != fildes) {
	  int count;
	  int *int_ptr;

	  /* we initialize the random number generator in
	     netlib_init() now. raj 20110111 */

	  /* unlink the file so it goes poof when we
	     exit. unless/until shown to be a problem we will
	     blissfully ignore the return value. raj 2007-08-09 */
	  unlink(temp_file);

	  /* now fill-out the file with at least buffer_size * width bytes */
	  for (count = 0; count < width; count++) {
	    /* fill the buffer with random data.  it doesn't have to be
	       really random, just "random enough" :) we do this here rather
	       than up above because we want each write to the file to be
	       different random data */
	    int_ptr = temp_buffer;
	    for (i = 0; i <= buffer_size/sizeof(int); i++) {
	      *int_ptr = rand();
	      int_ptr++;
	    }
	    if (write(fildes,temp_buffer,buffer_size+sizeof(int)) !=
		buffer_size + sizeof(int)) {
	      perror("allocate_sendfile_buf_ring: incomplete write");
	      exit(-1);
	    }
	  }
      }
      else {
	perror("alloc_sendfile_buf_ring: could not allocate temp name");
	exit(-1);
      }
    }
    else {
      perror("alloc_sendfile_buf_ring: could not allocate buffer for file");
      exit(-1);
    }
  }
  else {
    /* the user pointed us at a file, so try it */
    fildes = open(local_fill_file , O_RDONLY);
    if (fildes == -1){
      perror("alloc_sendfile_buf_ring: Could not open requested file");
      exit(1);
    }
    /* make sure there is enough file there to allow us to make a
       complete ring. that way we do not need additional logic in the
       ring setup to deal with wrap-around issues. we might want that
       someday, but not just now. 08/2000 */
    if (stat(local_fill_file,&statbuf) != 0) {
      perror("alloc_sendfile_buf_ring: could not stat file");
      exit(1);
    }
    if (statbuf.st_size < (width * buffer_size)) {
      /* the file is too short */
      fprintf(stderr,
	      "alloc_sendfile_buf_ring: specified file too small.\n"
	      "file must be larger than send_width * send_size\n");
      fflush(stderr);
      exit(1);
    }
  }

  /* so, at this point we know that fildes is a descriptor which
     references a file of sufficient size for our nefarious
     porpoises. raj 2007-08-09 */

  prev_link = NULL;
  for (i = 1; i <= width; i++) {
    /* get the ring element. we should probably make sure the malloc()
       was successful, but for now we'll just let the code bomb
       mysteriously. 08/2000 */

    temp_link = (struct ring_elt *)
      malloc(sizeof(struct ring_elt));
    if (temp_link == NULL) {
      fprintf(where,
	      "malloc(%u) failed!\n",
	      (unsigned int) sizeof(struct ring_elt));
      exit(1);
    }

    /* remember the first one so we can close the ring at the end */

    if (i == 1) {
      first_link = temp_link;
    }

    /* now fill-in the fields of the structure with the apropriate
       stuff. just how should we deal with alignment and offset I
       wonder? until something better comes-up, I think we will just
       ignore them. 08/2000 */

    temp_link->fildes = fildes;      /* from which file do we send? */
    temp_link->offset = offset;      /* starting at which offset? */
    offset += buffer_size;           /* get ready for the next elt */
    temp_link->length = buffer_size; /* how many bytes to send */
    temp_link->hdtrl = NULL;         /* no header or trailer */
    temp_link->flags = 0;            /* no flags */

    /* is where the buffer fill code went. */

    temp_link->next = prev_link;
    prev_link = temp_link;
  }
  /* close the ring */
  first_link->next = temp_link;

  return(first_link); /* it's a dummy ring */
}

#endif /* HAVE_SENDFILE */


 /***********************************************************************/
 /*                                                                     */
 /*     dump_request()                                                  */
 /*                                                                     */
 /* display the contents of the request array to the user. it will      */
 /* display the contents in decimal, hex, and ascii, with four bytes    */
 /* per line.                                                           */
 /*                                                                     */
 /***********************************************************************/

void
dump_request()
{
int counter = 0;
fprintf(where,"request contents:\n");
for (counter = 0; counter < ((sizeof(netperf_request)/4)-3); counter += 4) {
  fprintf(where,"%d:\t%8x %8x %8x %8x \t|%4.4s| |%4.4s| |%4.4s| |%4.4s|\n",
          counter,
          request_array[counter],
          request_array[counter+1],
          request_array[counter+2],
          request_array[counter+3],
          (char *)&request_array[counter],
          (char *)&request_array[counter+1],
          (char *)&request_array[counter+2],
          (char *)&request_array[counter+3]);
}
fflush(where);
}


 /***********************************************************************/
 /*                                                                     */
 /*     dump_response()                                                 */
 /*                                                                     */
 /* display the content of the response array to the user. it will      */
 /* display the contents in decimal, hex, and ascii, with four bytes    */
 /* per line.                                                           */
 /*                                                                     */
 /***********************************************************************/

void
dump_response()
{
int counter = 0;

fprintf(where,"response contents\n");
for (counter = 0; counter < ((sizeof(netperf_response)/4)-3); counter += 4) {
  fprintf(where,"%d:\t%8x %8x %8x %8x \t>%4.4s< >%4.4s< >%4.4s< >%4.4s<\n",
          counter,
          response_array[counter],
          response_array[counter+1],
          response_array[counter+2],
          response_array[counter+3],
          (char *)&response_array[counter],
          (char *)&response_array[counter+1],
          (char *)&response_array[counter+2],
          (char *)&response_array[counter+3]);
}
fflush(where);
}

 /*

      format_number()

  return a pointer to a formatted string containing the value passed
  translated into the units specified. It assumes that the base units
  are bytes. If the format calls for bits, it will use SI units (10^)
  if the format calls for bytes, it will use CS units (2^)...  This
  routine should look familiar to uses of the latest ttcp...

  we would like to use "t" or "T" for transactions, but probably
  should leave those for terabits and terabytes respectively, so for
  transactions, we will use "x" which will, by default, do absolutely
  nothing to the result.  why?  so we don't have to special case code
  elsewhere such as in the TCP_RR-as-bidirectional test case.

 */


char *
format_number(double number)
{
  static  char    fmtbuf[64];

  switch (libfmt) {
  case 'B':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f" , number);
    break;
  case 'K':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f" , number / 1024.0);
    break;
  case 'M':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f", number / 1024.0 / 1024.0);
    break;
  case 'G':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f", number / 1024.0 / 1024.0 / 1024.0);
    break;
  case 'b':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f" , number * 8);
    break;
  case 'k':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f", number * 8 / 1000.0);
    break;
  case 'm':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f", number * 8 / 1000.0 / 1000.0);
    break;
  case 'g':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f", number * 8 / 1000.0 / 1000.0 / 1000.0);
    break;
  case 'x':
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f", number);
    break;
  default:
    snprintf(fmtbuf, sizeof(fmtbuf),  "%-7.2f", number / 1024.0);
  }

  return fmtbuf;
}

char
format_cpu_method(int method)
{

  char method_char;

  switch (method) {
  case CPU_UNKNOWN:
    method_char = 'U';
    break;
  case HP_IDLE_COUNTER:
    method_char = 'I';
    break;
  case PSTAT:
    method_char = 'P';
    break;
  case KSTAT:
    method_char = 'K';
    break;
  case KSTAT_10:
    method_char = 'M';
    break;
  case PERFSTAT:
    method_char = 'E';
    break;
  case TIMES:             /* historical only, completely unsuitable
			     for netperf's purposes */
    method_char = 'T';
    break;
  case GETRUSAGE:         /* historical only, completely unsuitable
			     for netperf;s purposes */
    method_char = 'R';
    break;
  case LOOPER:
    method_char = 'L';
    break;
  case NT_METHOD:
    method_char = 'N';
    break;
  case PROC_STAT:
    method_char = 'S';
    break;
  case SYSCTL:
    method_char = 'C';
    break;
  case OSX:
    method_char = 'O';
    break;
  default:
    method_char = '?';
  }

  return method_char;

}

char *
format_units()
{
  static        char    unitbuf[64];

  switch (libfmt) {
  case 'B':
    strcpy(unitbuf, "Bytes");
    break;
  case 'K':
    strcpy(unitbuf, "KBytes");
    break;
  case 'M':
    strcpy(unitbuf, "MBytes");
    break;
  case 'G':
    strcpy(unitbuf, "GBytes");
    break;
  case 'b':
    strcpy(unitbuf, "10^0bits");
    break;
  case 'k':
    strcpy(unitbuf, "10^3bits");
    break;
  case 'm':
    strcpy(unitbuf, "10^6bits");
    break;
  case 'g':
    strcpy(unitbuf, "10^9bits");
    break;
  case 'x':
    strcpy(unitbuf, "Trans");
    break;
  case 'u':
    strcpy(unitbuf,"Usec");
    break;

  default:
    strcpy(unitbuf, "KBytes");
  }

  return unitbuf;
}


/****************************************************************/
/*                                                              */
/*      shutdown_control()                                      */
/*                                                              */
/* tear-down the control connection between me and the server.  */
/****************************************************************/

void
shutdown_control()
{

  char  *buf = (char *)&netperf_response;
  int   buflen = sizeof(netperf_response);

  /* stuff for select, use fd_set for better compliance */
  fd_set        readfds;
  struct        timeval timeout;

  if (debug) {
    fprintf(where,
            "shutdown_control: shutdown of control connection requested.\n");
    fflush(where);
  }

  /* first, we say that we will be sending no more data on the */
  /* connection */
  if (shutdown(netlib_control,1) == SOCKET_ERROR) {
    Print_errno(where,
            "shutdown_control: error in shutdown");
    fflush(where);
    exit(1);
  }

  /* Now, we hang on a select waiting for the socket to become
     readable to receive the shutdown indication from the remote. this
     will be "just" like the recv_response() code

     we only select once. it is assumed that if the response is split
     (which should not be happening, that we will receive the whole
     thing and not have a problem ;-) */

  FD_ZERO(&readfds);
  FD_SET(netlib_control,&readfds);
  timeout.tv_sec  = 60; /* wait one minute then punt */
  timeout.tv_usec = 0;

  /* select had better return one, or there was either a problem or a
     timeout... */
  if (select(FD_SETSIZE,
             &readfds,
             0,
             0,
             &timeout) != 1) {
    Print_errno(where,
            "shutdown_control: no response received");
    fflush(where);
    exit(1);
  }

  /* we now assume that the socket has come ready for reading */
  recv(netlib_control, buf, buflen,0);

}

/*
  bind_to_specific_processor will bind the calling process to the
  processor in "processor"  It has lots of ugly ifdefs to deal with
  all the different ways systems do processor affinity.  this is a
  generalization of work initially done by stephen burger.  raj
  2004/12/13 */

void
bind_to_specific_processor(int use_cpu_affinity, int use_cpu_map)
{

  int mapped_affinity;

  /* this is in place because the netcpu_looper processor affinity
     ass-u-me-s a contiguous CPU id space starting with 0. for the
     regular netperf/netserver affinity, we ass-u-me the user has used
     a suitable CPU id even when the space is not contiguous and
     starting from zero */
  if (use_cpu_map) {
    mapped_affinity = lib_cpu_map[use_cpu_affinity];
  }
  else {
    mapped_affinity = use_cpu_affinity;
  }

#ifdef HAVE_MPCTL
  /* indeed, at some point it would be a good idea to check the return
     status and pass-along notification of error... raj 2004/12/13 */
  mpctl(MPC_SETPROCESS_FORCE, mapped_affinity, getpid());
#elif HAVE_PROCESSOR_BIND
#include <sys/types.h>
#include <sys/processor.h>
#include <sys/procset.h>
  processor_bind(P_PID,P_MYID,mapped_affinity,NULL);
#elif HAVE_BINDPROCESSOR
#include <sys/processor.h>
  /* this is the call on AIX.  It takes a "what" of BINDPROCESS or
     BINDTHRAD, then "who" and finally "where" which is a CPU number
     or it seems PROCESSOR_CLASS_ANY there also seems to be a mycpu()
     call to return the current CPU assignment.  this is all based on
     the sys/processor.h include file.  from empirical testing, it
     would seem that the my_cpu() call returns the current CPU on
     which we are running rather than the CPU binding, so it's return
     value will not tell you if you are bound vs unbound. */
  bindprocessor(BINDPROCESS,getpid(),(cpu_t)mapped_affinity);
#elif HAVE_SCHED_SETAFFINITY
#include <sched.h>
  /* in theory this should cover systems with more CPUs than bits in a
     long, without having to specify __USE_GNU.  we "cheat" by taking
     defines from /usr/include/bits/sched.h, which we ass-u-me is
     included by <sched.h>.  If they are not there we will just
     fall-back on what we had before, which is to use just the size of
     an unsigned long. raj 2006-09-14 */

#if defined(__CPU_SETSIZE)
#define NETPERF_CPU_SETSIZE __CPU_SETSIZE
#if defined(__CPU_SET_S)
#define NETPERF_CPU_SET(cpu, cpusetp)  __CPU_SET_S(cpu, sizeof (cpu_set_t), cpusetp)
#define NETPERF_CPU_ZERO(cpusetp)      __CPU_ZERO_S (sizeof (cpu_set_t), cpusetp)
#else
#define NETPERF_CPU_SET(cpu, cpusetp)  __CPU_SET(cpu, cpusetp)
#define NETPERF_CPU_ZERO(cpusetp)      __CPU_ZERO (cpusetp)
#endif
  typedef cpu_set_t netperf_cpu_set_t;
#else
#define NETPERF_CPU_SETSIZE sizeof(unsigned long)
#define NETPERF_CPU_SET(cpu, cpusetp) *cpusetp = 1 << cpu
#define NETPERF_CPU_ZERO(cpusetp) *cpusetp = (unsigned long)0
  typedef unsigned long netperf_cpu_set_t;
#endif

  netperf_cpu_set_t   netperf_cpu_set;
  unsigned int        len = sizeof(netperf_cpu_set);

  if (mapped_affinity < 8*sizeof(netperf_cpu_set)) {
    NETPERF_CPU_ZERO(&netperf_cpu_set);
    NETPERF_CPU_SET(mapped_affinity,&netperf_cpu_set);

    if (sched_setaffinity(getpid(), len, &netperf_cpu_set)) {
      if (debug) {
	fprintf(stderr, "failed to set PID %d's CPU affinity errno %d\n",
		getpid(),errno);
	fflush(stderr);
      }
    }
  }
  else {
    if (debug) {
	fprintf(stderr,
		"CPU number larger than pre-compiled limits. Consider a recompile.\n");
	fflush(stderr);
      }
  }

#elif HAVE_BIND_TO_CPU_ID
  /* this is the one for Tru64 */
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/processor.h>

  /* really should be checking a return code one of these days. raj
     2005/08/31 */

  bind_to_cpu_id(getpid(), mapped_affinity,0);

#elif WIN32

  {
    ULONG_PTR AffinityMask;
    ULONG_PTR ProcessAffinityMask;
    ULONG_PTR SystemAffinityMask;

    if ((mapped_affinity < 0) ||
	(mapped_affinity > MAXIMUM_PROCESSORS)) {
      fprintf(where,
	      "Invalid use_cpu_affinity specified: %d\n", mapped_affinity);      fflush(where);
      return;
    }

    if (!GetProcessAffinityMask(
				GetCurrentProcess(),
				&ProcessAffinityMask,
				&SystemAffinityMask))
      {
	perror("GetProcessAffinityMask failed");
	fflush(stderr);
	exit(1);
      }

    AffinityMask = (ULONG_PTR)1 << mapped_affinity;

    if (AffinityMask & ProcessAffinityMask) {
      if (!SetThreadAffinityMask( GetCurrentThread(), AffinityMask)) {
	perror("SetThreadAffinityMask failed");
	fflush(stderr);
      }
    } else if (debug) {
      fprintf(where,
	      "Processor affinity set to CPU# %d\n", mapped_affinity);
      fflush(where);
    }
  }

#elif defined(__FreeBSD__)
#include <sys/param.h>
  /* FreeBSD introduced cpuset_setaffinity() in version 7.1 */
#if (__FreeBSD_version > 701000)
#include <sys/cpuset.h>

  cpuset_t mask;

  CPU_ZERO(&mask);
  CPU_SET(mapped_affinity, &mask);
  if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1,
			 sizeof(mask), &mask)) {
	perror("cpuset_setaffinity failed");
	fflush(stderr);
  }
#endif /* __FreeBSD_version */
#else
  if (debug) {
    fprintf(where,
	    "Processor affinity not available for this platform!\n");
    fflush(where);
  }
#endif
}


/*
 * Sets a socket to non-blocking operation.
 */
int
set_nonblock (SOCKET sock)
{
#ifdef WIN32
  unsigned long flags = 1;
  return (ioctlsocket(sock, FIONBIO, &flags) != SOCKET_ERROR);
#else
  return (fcntl(sock, F_SETFL, O_NONBLOCK) != -1);
#endif
}



/* send a request, only converting the first n ints-worth of the
   test-specific data via htonl() before sending on the
   connection. the first two ints, which are before the test-specific
   portion are always converted. raj 2008-02-05 */

void
send_request_n(int n)
{

  int   counter,count;

  if (n < 0) count = sizeof(netperf_request)/4;
  else count = 2 + n;

  /* silently truncate if the caller called for more than we have */
  if (count > sizeof(netperf_request)/4) {
    if (debug > 1) {
      fprintf(where,
	      "WARNING, htonl conversion count of %d was larger than netperf_request\n",
	      count - 2);
      fflush(where);
    }
    count = sizeof(netperf_request)/4;
  }

  /* display the contents of the request if the debug level is high
     enough. otherwise, just send the darned thing ;-) */

  if (debug > 1) {
    fprintf(where,
	    "entered send_request_n...contents before %d htonls:\n",
	    count);
    dump_request();
  }

  /* pass the processor affinity request value to netserver this is a
     kludge and I know it.  sgb 8/11/04. we keep this here to deal
     with there being two paths to this place - direct and via
     send_request()  */

  netperf_request.content.dummy = remote_proc_affinity;

  /* put the entire request array into network order. We do this
     arbitrarily rather than trying to figure-out just how much of the
     request array contains real information. this should be simpler,
     and at any rate, the performance of sending control messages for
     this benchmark is not of any real concern. */

  for (counter = 0; counter < count; counter++) {
    request_array[counter] = htonl(request_array[counter]);
  }

  if (debug > 1) {
    fprintf(where,"send_request_n...contents after %d htonls:\n",
	    count);
    dump_request();

    fprintf(where,
            "\nsend_request: about to send %u bytes from %p\n",
            (unsigned int) sizeof(netperf_request),
            &netperf_request);
    fflush(where);
  }

  if (send(netlib_control,
           (char *)&netperf_request,
           sizeof(netperf_request),
           0) != sizeof(netperf_request)) {
    perror("send_request: send call failure");

    exit(1);
  }
}

 /***********************************************************************/
 /*                                                                     */
 /*     send_request()                                                  */
 /*                                                                     */
 /* send a netperf request on the control socket to the remote half of  */
 /* the connection. to get us closer to intervendor interoperability,   */
 /* we will call htonl on each of the int that compose the message to   */
 /* be sent. the server-half of the connection will call the ntohl      */
 /* routine to undo any changes that may have been made...              */
 /*                                                                     */
 /***********************************************************************/

void
send_request()
{

  /* pass the processor affinity request value to netserver this is a
     kludge and I know it.  sgb 8/11/04  */

  netperf_request.content.dummy = remote_proc_affinity;

  /* call send_request_n telling it to convert everything */

  send_request_n(-1);

}

/* send a response, only converting the first n ints-worth of the
   test-specific data via htonl() before sending on the
   connection. the first two ints, which are before the test-specific
   portion are always converted. raj 2008-02-05 */

void
send_response_n(int n)
{
  int   counter, count;
  int	bytes_sent;

  if (n < 0) count = sizeof(netperf_request)/4;
  else count = 2 + n;

  /* silently truncate if the caller called for more than we have */
  if (count > sizeof(netperf_request)/4) {
    if (debug > 1) {
      fprintf(where,
	      "WARNING, htonl conversion count of %d was larger than netperf_request\n",
	      count - 2);
      fflush(where);
    }
    count = sizeof(netperf_request)/4;
  }

  /* display the contents of the request if the debug level is high */
  /* enough. otherwise, just send the darned thing ;-) */

  if (debug > 1) {
    fprintf(where,
            "send_response_n: contents of %u ints before %d htonl,\n",
            (unsigned int) sizeof(netperf_response)/4,
	    count);
    dump_response();
  }

  /* put the entire response_array into network order. We do this
     arbitrarily rather than trying to figure-out just how much of the
     request array contains real information. this should be simpler,
     and at any rate, the performance of sending control messages for
     this benchmark is not of any real concern. */

  for (counter = 0; counter < count; counter++) {
    response_array[counter] = htonl(response_array[counter]);
  }

  if (debug > 1) {
    fprintf(where,
            "send_response_n: contents after htonl\n");
    dump_response();
    fprintf(where,
            "about to send %u bytes from %p\n",
            (unsigned int) sizeof(netperf_response),
            &netperf_response);
    fflush(where);
  }

  /*KC*/
  if ((bytes_sent = send(server_sock,
			 (char *)&netperf_response,
			 sizeof(netperf_response),
			 0)) != sizeof(netperf_response)) {
    perror("send_response_n: send call failure");
    fprintf(where, "BytesSent: %d\n", bytes_sent);
    exit(1);
  }

}

/***********************************************************************/
 /*                                                                     */
 /*     send_response()                                                 */
 /*                                                                     */
 /* send a netperf response on the control socket to the remote half of */
 /* the connection. to get us closer to intervendor interoperability,   */
 /* we will call htonl on each of the int that compose the message to   */
 /* be sent. the other half of the connection will call the ntohl       */
 /* routine to undo any changes that may have been made...              */
 /*                                                                     */
 /***********************************************************************/

void
send_response()
{

  send_response_n(-1);

}

/* go back and "undo" the ntohl that recv_request() did, starting with
   the specified point and going to the end of the request array */
void
fixup_request_n(int n)
{
  int i;
  int limit;

  limit = sizeof(netperf_request) / 4;
  /* we must remember that the request_array also contains two ints of
     "other" stuff, so we start the fixup two in - at least I think we
     should.  raj 2012-04-02 */
  for (i = n + 2; i < limit; i++) {
    request_array[i] = htonl(request_array[i]);
  }
  if (debug > 1) {
    fprintf(where,
	    "%s: request contents after fixup at the %d th int\n",
	    __FUNCTION__,
	    n);
    dump_request();
    fflush(where);
  }
}

/* receive a request, only converting the first n ints-worth of the
   test-specific data via htonl() before sending on the
   connection. the first two ints, which are before the test-specific
   portion are always converted. raj 2008-02-05 */

int
recv_request_timed_n(int n, int seconds)
{
  int     tot_bytes_recvd,
    bytes_recvd,
    bytes_left;
  char    *buf = (char *)&netperf_request;
  int     buflen = sizeof(netperf_request);
  int     counter,count;

  fd_set  readfds;
  struct timeval timeout;

  if (n < 0) count = sizeof(netperf_request)/4;
  else count = 2 + n;

  /* silently truncate if the caller called for more than we have */
  if (count > sizeof(netperf_request)/4) {
    if (debug > 1) {
      fprintf(where,
	      "WARNING, htonl conversion count of %d was larger than netperf_request\n",
	      count - 2);
      fflush(where);
    }
    count = sizeof(netperf_request)/4;
  }

  /* for the time being, we rather rely on select decrementing timeout
     each time to preclude someone with nefarious intent from just
     dribbling data to us piecemeal.  of course, who knows what
     someone with nefarious intent might come-up with. raj 2012-01-23 */
  tot_bytes_recvd = 0;
  bytes_recvd = 0;     /* nt_lint; bytes_recvd uninitialized if buflen == 0 */
  bytes_left      = buflen;
  timeout.tv_sec = seconds;
  timeout.tv_usec = 0;
  do {
    FD_ZERO(&readfds);
    FD_SET(server_sock,&readfds);
    if (select(FD_SETSIZE,
	       &readfds,
	       0,
	       0,
	       (seconds > 0) ? &timeout : NULL) != 1) {
      fprintf(where,
	      "Issue receiving request on control connection. Errno %d (%s)\n",
	      errno,
	      strerror(errno));
      fflush(where);
      close(server_sock);
      return -1;
    }

    if ((bytes_recvd = recv(server_sock, buf, bytes_left, 0)) > 0) {
      tot_bytes_recvd += bytes_recvd;
      buf             += bytes_recvd;
      bytes_left      -= bytes_recvd;
    }
  }  while ((tot_bytes_recvd != buflen) &&
	    (bytes_recvd > 0 ));

  /* put the request into host order */

  for (counter = 0; counter < count; counter++) {
    request_array[counter] = ntohl(request_array[counter]);
  }

  if (debug) {
    fprintf(where,
	    "recv_request: received %d bytes of request.\n",
	    tot_bytes_recvd);
    fflush(where);
  }

  if (bytes_recvd == SOCKET_ERROR) {
    Print_errno(where,
		"recv_request: error on recv");
    fflush(where);
    close(server_sock);
    return -1;
  }

  if (bytes_recvd == 0) {
    /* the remote has shutdown the control connection, we should shut
       it down as well and return */
    if (debug) {
      fprintf(where,
	      "recv_request: remote requested shutdown of control\n");
      fflush(where);
    }

    close(server_sock);
    return 0;
  }

  if (tot_bytes_recvd < buflen) {
    if (debug > 1)
      dump_request();

    fprintf(where,
	    "recv_request: partial request received of %d bytes\n",
	    tot_bytes_recvd);
    fflush(where);
    close(server_sock);
    return -1;
  }

  if (debug > 1) {
    dump_request();
  }

  /* get the processor affinity request value from netperf this is a
     kludge and I know it.  sgb 8/11/04  */

  local_proc_affinity = netperf_request.content.dummy;

  if (local_proc_affinity != -1) {
    bind_to_specific_processor(local_proc_affinity,0);
  }

  return buflen;
}

/* receive a request, only converting the first n ints-worth of the
   test-specific data via htonl() before sending on the
   connection. the first two ints, which are before the test-specific
   portion are always converted. raj 2008-02-05 */

int
recv_request_n(int n)
{

  return recv_request_timed_n(n,0);

}

 /***********************************************************************/
 /*                                                                     */
 /*     recv_request()                                                  */
 /*                                                                     */
 /* receive the remote's request on the control socket. we will put     */
 /* the entire response into host order before giving it to the         */
 /* calling routine. hopefully, this will go most of the way to         */
 /* insuring intervendor interoperability. if there are any problems,   */
 /* we will just punt the entire situation.                             */
 /*                                                                     */
 /***********************************************************************/

int
recv_request()
{

  return recv_request_n(-1);

}

void
recv_response_timed_n(int addl_time, int n)
{
  int     tot_bytes_recvd,
          bytes_recvd = 0,
          bytes_left;
  char    *buf = (char *)&netperf_response;
  int     buflen = sizeof(netperf_response);
  int     counter,count;

  /* stuff for select, use fd_set for better compliance */
  fd_set  readfds;
  struct  timeval timeout;

  tot_bytes_recvd = 0;
  bytes_left      = buflen;

  if (n < 0) count = sizeof(netperf_request)/4;
  else count = 2 + n;

  /* silently truncate if the caller called for more than we have */
  if (count > sizeof(netperf_request)/4) {
    if (debug > 1) {
      fprintf(where,
	      "WARNING, htonl conversion count of %d was larger than netperf_response\n",
	      count - 2);
      fflush(where);
    }
    count = sizeof(netperf_request)/4;
  }

  /* zero out the response structure */

  /* BUG FIX SJB 2/4/93 - should be < not <= */
  for (counter = 0;
       counter < sizeof(netperf_response)/sizeof(int);
       counter++) {
    response_array[counter] = 0;
  }

  /* we only select once. it is assumed that if the response is split
     (which should not be happening, that we will receive the whole
     thing and not have a problem ;-) */

  FD_ZERO(&readfds);
  FD_SET(netlib_control,&readfds);
  timeout.tv_sec  = 120 + addl_time;  /* wait at least two minutes
					 before punting - the
					 USE_LOOPER CPU stuff may
					 cause remote's to have a bit
					 longer time of it than 60
					 seconds would allow.
					 triggered by fix from Jeff
					 Dwork. */
  timeout.tv_usec = 0;

  /* select had better return one, or there was either a problem or a */
  /* timeout... */

  if ((counter = select(FD_SETSIZE,
			&readfds,
			0,
			0,
			&timeout)) != 1) {
    fprintf(where,
	    "%s: no response received. errno %d counter %d\n",
	    __FUNCTION__,
	    errno,
	    counter);
    exit(1);
  }

  while ((tot_bytes_recvd != buflen) &&
	 ((bytes_recvd = recv(netlib_control, buf, bytes_left,0)) > 0 )) {
    tot_bytes_recvd += bytes_recvd;
    buf             += bytes_recvd;
    bytes_left      -= bytes_recvd;
  }

  if (debug) {
    fprintf(where,"recv_response: received a %d byte response\n",
	    tot_bytes_recvd);
    fflush(where);
  }

  /* put the desired quantity of the response into host order */

  for (counter = 0; counter < count; counter++) {
    response_array[counter] = ntohl(response_array[counter]);
  }

  if (bytes_recvd == SOCKET_ERROR) {
    perror("recv_response");
    exit(1);
  }
  if (tot_bytes_recvd < buflen) {
    fprintf(stderr,
	    "recv_response: partial response received: %d bytes\n",
	    tot_bytes_recvd);
    fflush(stderr);
    if (debug > 1)
      dump_response();
    exit(1);
  }
  if (debug > 1) {
    dump_response();
  }
}

/*

  recv_response_timed()

  receive the remote's response on the control socket. we will put the
  entire response into host order before giving it to the calling
  routine. hopefully, this will go most of the way to insuring
  intervendor interoperability. if there are any problems, we will
  just punt the entire situation.

  The call to select at the beginning is to get us out of hang
  situations where the remote gives-up but we don't find-out about
  it. This seems to happen only rarely, but it would be nice to be
  somewhat robust ;-)

  The "_timed" part is to allow the caller to add (or I suppose
  subtract) from the length of timeout on the select call. this was
  added since not all the CPU utilization mechanisms require a 40
  second calibration, and we used to have an aribtrary 40 second sleep
  in "calibrate_remote_cpu" - since we don't _always_ need that, we
  want to simply add 40 seconds to the select() timeout from that
  call, but don't want to change all the "recv_response" calls in the
  code right away.  sooo, we push the functionality of the old
  recv_response() into a new recv_response_timed(addl_timout) call,
  and have recv_response() call recv_response_timed(0).  raj
  2005-05-16

 */


void
recv_response_timed(int addl_time)
{

  /* -1 => convert all the test-specific data via ntohl */
  recv_response_timed_n(addl_time,-1);

}

void
recv_response()
{
  /* 0 => no additional time, -1 => convert all test-specific data */
  recv_response_timed_n(0,-1);
}

void
recv_response_n(int n)
{
  recv_response_timed_n(0,n);
}




#if defined(USE_PSTAT) || defined (USE_SYSCTL)
int
hi_32(big_int)
     long long *big_int;
{
  union overlay_u {
    long long  dword;
    long       words[2];
  } *overlay;

  overlay = (union overlay_u *)big_int;
  /* on those systems which are byte swapped, we really wish to return
     words[1] - at least I think so - raj 4/95 */
  if (htonl(1L) == 1L) {
    /* we are a "normal" :) machine */
    return(overlay->words[0]);
  }
  else {
    return(overlay->words[1]);
  }
}

int
lo_32(big_int)
     long long *big_int;
{
  union overlay_u {
    long long  dword;
    long       words[2];
  } *overlay;

  overlay = (union overlay_u *)big_int;
  /* on those systems which are byte swapped, we really wish to return
     words[0] - at least I think so - raj 4/95 */
  if (htonl(1L) == 1L) {
    /* we are a "normal" :) machine */
    return(overlay->words[1]);
  }
  else {
    return(overlay->words[0]);
  }
}

#endif /* USE_PSTAT || USE_SYSCTL */


void libmain()
{
fprintf(where,"hello world\n");
fprintf(where,"debug: %d\n",debug);
}


void
get_sock_buffer (SOCKET sd, enum sock_buffer which, int *effective_sizep)
{
#ifdef SO_SNDBUF
  int optname = (which == SEND_BUFFER) ? SO_SNDBUF : SO_RCVBUF;
  netperf_socklen_t sock_opt_len;

  sock_opt_len = sizeof(*effective_sizep);
  if (getsockopt(sd, SOL_SOCKET, optname, (char *)effective_sizep,
		 &sock_opt_len) < 0) {
    fprintf(where, "netperf: get_sock_buffer: getsockopt %s: errno %d\n",
	    (which == SEND_BUFFER) ? "SO_SNDBUF" : "SO_RCVBUF", errno);
    fflush(where);
    *effective_sizep = -1;
  }

  if (debug) {
    fprintf(where, "netperf: get_sock_buffer: "
	    "%s socket size determined to be %d\n",
	    (which == SEND_BUFFER) ? "send" : "receive", *effective_sizep);
    fflush(where);
  }

#else
  *effective_sizep = -1;
#endif
}

void
set_sock_buffer (SOCKET sd, enum sock_buffer which, int requested_size, int *effective_sizep)
{
#ifdef SO_SNDBUF

  int optname = (which == SEND_BUFFER) ? SO_SNDBUF : SO_RCVBUF;

  /* seems that under Windows, setting a value of zero is how one
     tells the stack you wish to enable copy-avoidance. Knuth only
     knows what it will do on other stacks, but it might be
     interesting to find-out, so we won't bother #ifdef'ing the change
     to allow asking for 0 bytes. Courtesy of SAF, 2007-05 raj
     2007-05-31 */
  if (requested_size >= 0) {
    if (setsockopt(sd, SOL_SOCKET, optname,
		   (char *)&requested_size, sizeof(int)) < 0) {
      fprintf(where, "netperf: set_sock_buffer: %s option: errno %d (%s)\n",
	      (which == SEND_BUFFER) ? "SO_SNDBUF" : "SO_RCVBUF",
	      errno,
	      strerror(errno));
      fflush(where);
      exit(1);
    }
    if (debug > 1) {
      fprintf(where, "netperf: set_sock_buffer: %s of %d requested.\n",
	      (which == SEND_BUFFER) ? "SO_SNDBUF" : "SO_RCVBUF",
	      requested_size);
      fflush(where);
    }
  }

  /* the getsockopt() call that used to be here has been hoisted into
     its own routine to be used on those platforms where the socket
     buffer sizes might change from the beginning to the end of the
     run. raj 2008-01-15 */

  get_sock_buffer(sd, which, effective_sizep);

#else /* SO_SNDBUF */
  *effective_sizep = -1;
#endif /* SO_SNDBUF */
}

void
dump_addrinfo(FILE *dumploc, struct addrinfo *info,
              const char *host, char *port, int family)
{
  struct sockaddr *ai_addr;
  struct addrinfo *temp;
  temp=info;

  fprintf(dumploc,
	  "getaddrinfo returned the following for host '%s' port '%s' "
	  " family %s\n",
	  host,
	  port,
	  inet_ftos(family));

  while (temp) {
    /* seems that Solaris 10 GA bits will not give a canonical name
       for ::0 or 0.0.0.0, and their fprintf() cannot deal with a null
       pointer, so we have to check for a null pointer.  probably a
       safe thing to do anyway, eventhough it was not necessary on
       linux or hp-ux. raj 2005-02-09 */
    fprintf(dumploc,
	    "\tcannonical name: '%s'\n"
            "\tflags: %x family: %s: socktype: %s protocol %s addrlen %d\n",
	    (temp->ai_canonname) ? temp->ai_canonname : "(nil)",
            temp->ai_flags,
            inet_ftos(temp->ai_family),
            inet_ttos(temp->ai_socktype),
            inet_ptos(temp->ai_protocol),
            temp->ai_addrlen);
    ai_addr = temp->ai_addr;
    if (ai_addr != NULL) {
      int i;
      fprintf(dumploc,
              "\tsa_family: %s sadata:",
              inet_ftos(ai_addr->sa_family));
      for (i = 0; i < (int) temp->ai_addrlen; i++) {
	fprintf(dumploc,
		(temp->ai_family == AF_INET) ? " %d" : " %.2x",
		(u_char)ai_addr->sa_data[i]);
      }
      fprintf(dumploc,"\n");
    }
    temp = temp->ai_next;
  }
  fflush(dumploc);
}

struct addrinfo *
resolve_host(char *hostname,
	     char *port,
	     int   family)
{
  struct addrinfo   hints;
  struct addrinfo  *ai;
  int count;
  int error;

  if (debug) {
    fprintf(where,
            "resolve_host called with host '%s' port '%s' family %s\n",
            hostname,
            port,
            inet_ftos(family));
    fflush(where);
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
  count = 0;
  do {
    error = getaddrinfo((char *)hostname,
                        (char *)port,
                        &hints,
                        &ai);
    count += 1;
    if (error == EAI_AGAIN) {
      if (debug) {
        fprintf(where,"Sleeping on getaddrinfo EAI_AGAIN\n");
        fflush(where);
      }
      sleep(1);
    }
  } while ((error == EAI_AGAIN) && (count <= 5));

  if (error) {
    printf("%s: could not resolve host '%s' port '%s' af %s"
	   "\n\tgetaddrinfo returned %d %s\n",
	   __FUNCTION__,
           hostname,
           port,
           inet_ftos(family),
           error,
           gai_strerror(error));
    return(NULL);
  }

  if (debug) {
    dump_addrinfo(where, ai, hostname, port, family);
  }

  return (ai);
}

/*
  establish_control()

  set-up the control connection between netperf and the netserver so
  we can actually run some tests. if we cannot establish the control
  connection, that may or may not be a good thing, so we will let the
  caller decide what to do.

  to assist with pesky end-to-end-unfriendly things like firewalls, we
  allow the caller to specify both the remote hostname and port, and
  the local addressing info.  i believe that in theory it is possible

  another, but for the time being, we are only going to take-in one
  requested address family parameter. this means that the only way
  (iirc) that we might get a mixed-mode connection would be if the
  address family is specified as AF_UNSPEC, and getaddrinfo() returns
  different families for the local and server names.

  the "names" can also be IP addresses in ASCII string form.

  raj 2003-02-27 */

SOCKET
establish_control_internal(char *hostname,
			   char *port,
			   int   remfam,
			   char *localhost,
			   char *localport,
			   int   locfam)
{
  int not_connected;
  SOCKET control_sock;

  struct addrinfo  *local_res;
  struct addrinfo  *remote_res;
  struct addrinfo  *local_res_temp;
  struct addrinfo  *remote_res_temp;

  remote_res = resolve_host(hostname, port, remfam);
  if (!remote_res)
    return(INVALID_SOCKET);

  local_res = resolve_host(localhost, localport, locfam);
  if (!local_res)
    return(INVALID_SOCKET);

  if (debug) {
    fprintf(where,
            "establish_control called with host '%s' port '%s' remfam %s\n"
            "\t\tlocal '%s' port '%s' locfam %s\n",
            hostname,
            port,
            inet_ftos(remfam),
            localhost,
            localport,
            inet_ftos(locfam));
    fflush(where);
  }

  not_connected = 1;
  local_res_temp = local_res;
  remote_res_temp = remote_res;
  /* we want to loop through all the possibilities. looping on the
     local addresses will be handled within the while loop.  I suppose
     these is some more "C-expert" way to code this, but it has not
     lept to mind just yet :) raj 2003-02024 */

  while (remote_res_temp != NULL) {

    /* I am guessing that we should use the address family of the
       local endpoint, and we will not worry about mixed family types
       - presumeably the stack or other transition mechanisms will be
       able to deal with that for us. famous last words :) raj
       2003-02-26 */
    control_sock = socket(local_res_temp->ai_family,
                          SOCK_STREAM,
                          0);
    if (control_sock == INVALID_SOCKET) {
      /* at some point we'll need a more generic "display error"
         message for when/if we use GUIs and the like. unlike a bind
         or connect failure, failure to allocate a socket is
         "immediately fatal" and so we return to the caller. raj
         2003-02-24 */
      if (debug) {
        perror("establish_control: unable to allocate control socket");
      }
      return(INVALID_SOCKET);
    }

    /* if we are going to control the local enpoint addressing, we
       need to call bind. of course, we should probably be setting one
       of the SO_REUSEmumble socket options? raj 2005-02-04 */
    if (bind(control_sock,
	     local_res_temp->ai_addr,
	     local_res_temp->ai_addrlen) == 0) {
      if (debug) {
	fprintf(where,
		"bound control socket to %s and %s\n",
		localhost,
		localport);
      }

      if (connect(control_sock,
		  remote_res_temp->ai_addr,
		  remote_res_temp->ai_addrlen) == 0) {
	/* we have successfully connected to the remote netserver */
	if (debug) {
	  fprintf(where,
		  "successful connection to remote netserver at %s and %s\n",
		  hostname,
		  port);
	}
	not_connected = 0;
	/* this should get us out of the while loop */
	break;
      } else {
	/* the connect call failed */
	if (debug) {
	  fprintf(where,
		  "establish_control: connect failed, errno %d %s\n"
		  "    trying next address combination\n",
		  errno,
		  strerror(errno));
	  fflush(where);
	}
      }
    }
    else {
      /* the bind failed */
      if (debug) {
	fprintf(where,
		"establish_control: bind failed, errno %d %s\n"
		"    trying next address combination\n",
		errno,
		strerror(errno));
	fflush(where);
      }
    }

    if ((local_res_temp = local_res_temp->ai_next) == NULL) {
      /* wrap the local and move to the next server, don't forget to
         close the current control socket. raj 2003-02-24 */
      local_res_temp = local_res;
      /* the outer while conditions will deal with the case when we
         get to the end of all the possible remote addresses. */
      remote_res_temp = remote_res_temp->ai_next;
      /* it is simplest here to just close the control sock. since
         this is not a performance critical section of code, we
         don't worry about overheads for socket allocation or
         close. raj 2003-02-24 */
    }
    close(control_sock);
  }

  control_family = local_res_temp->ai_family;

  /* we no longer need the addrinfo stuff */
  freeaddrinfo(local_res);
  freeaddrinfo(remote_res);

  /* so, we are either connected or not */
  if (not_connected) {
    fprintf(where,
	    "establish control: are you sure there is a netserver "
	    "listening on %s at port %s?\n",
	    hostname,
	    port);
    fflush(where);
    control_family = AF_UNSPEC;
    return(INVALID_SOCKET);
  }
  /* at this point, we are connected.  we probably want some sort of
     version check with the remote at some point. raj 2003-02-24 */
  return(control_sock);
}

void
establish_control(char *hostname,
		  char *port,
		  int   remfam,
		  char *localhost,
		  char *localport,
		  int   locfam)

{

  netlib_control = establish_control_internal(hostname,
					      port,
					      remfam,
					      localhost,
					      localport,
					      locfam);
  if (netlib_control == INVALID_SOCKET) {
    fprintf(where,
	    "establish_control could not establish the control"
	    " connection from %s port %s address family %s to %s"
	    " port %s address family %s\n",
	    localhost,localport,inet_ftos(locfam),
	    hostname,port,inet_ftos(remfam));
    fflush(where);
    exit(INVALID_SOCKET);
  }
}




 /***********************************************************************/
 /*                                                                     */
 /*     get_id()                                                        */
 /*                                                                     */
 /* Return a string to the calling routine that contains the            */
 /* identifying information for the host we are running on. This        */
 /* information will then either be displayed locally, or returned to   */
 /* a remote caller for display there.                                  */
 /*                                                                     */
 /***********************************************************************/

char *
get_id()
{
	static char id_string[80];
#ifdef WIN32
char                    system_name[MAX_COMPUTERNAME_LENGTH+1] ;
DWORD                   name_len = MAX_COMPUTERNAME_LENGTH + 1 ;
#else
struct  utsname         system_name;
#endif /* WIN32 */

#ifdef WIN32
 SYSTEM_INFO SystemInfo;
 GetSystemInfo( &SystemInfo ) ;
 if ( !GetComputerName(system_name , &name_len) )
   strcpy(system_name , "no_name") ;
#else
 if (uname(&system_name) <0) {
   perror("identify_local: uname");
   exit(1);
 }
#endif /* WIN32 */

 snprintf(id_string, sizeof(id_string),
#ifdef WIN32
	  "%-15s%-15s%d.%d%d",
	  "Windows NT",
	  system_name ,
	  GetVersion() & 0xFF ,
	  GetVersion() & 0xFF00 ,
	  SystemInfo.dwProcessorType

#else
	  "%-15s%-15s%-15s%-15s%-15s",
	  system_name.sysname,
	  system_name.nodename,
	  system_name.release,
	  system_name.version,
	  system_name.machine
#endif /* WIN32 */
	  );
 return (id_string);
}


 /***********************************************************************/
 /*                                                                     */
 /*     identify_local()                                                */
 /*                                                                     */
 /* Display identifying information about the local host to the user.   */
 /* At first release, this information will be the same as that which   */
 /* is returned by the uname -a command, with the exception of the      */
 /* idnumber field, which seems to be a non-POSIX item, and hence       */
 /* non-portable.                                                       */
 /*                                                                     */
 /***********************************************************************/

void
identify_local()
{

char *local_id;

local_id = get_id();

fprintf(where,"Local Information \n\
Sysname       Nodename       Release        Version        Machine\n");

fprintf(where,"%s\n",
       local_id);

}


 /***********************************************************************/
 /*                                                                     */
 /*     identify_remote()                                               */
 /*                                                                     */
 /* Display identifying information about the remote host to the user.  */
 /* At first release, this information will be the same as that which   */
 /* is returned by the uname -a command, with the exception of the      */
 /* idnumber field, which seems to be a non-POSIX item, and hence       */
 /* non-portable. A request is sent to the remote side, which will      */
 /* return a string containing the utsname information in a             */
 /* pre-formatted form, which is then displayed after the header.       */
 /*                                                                     */
 /***********************************************************************/

void
identify_remote()
{

  char    *remote_id="";

  /* send a request for node info to the remote */
  netperf_request.content.request_type = NODE_IDENTIFY;

  send_request();

  /* and now wait for the reply to come back */

  recv_response();

  if (netperf_response.content.serv_errno) {
    Set_errno(netperf_response.content.serv_errno);
    perror("identify_remote: on remote");
    exit(1);
  }

  fprintf(where,"Remote Information \n\
Sysname       Nodename       Release        Version        Machine\n");

  fprintf(where,"%s",
	  remote_id);
}

void
cpu_start(int measure_cpu)
{

  gettimeofday(&time1,
               &tz);

  if (measure_cpu) {
    cpu_util_init();
    measuring_cpu = 1;
    cpu_method = get_cpu_method();
    cpu_start_internal();
  }
}


void
cpu_stop(int measure_cpu, float *elapsed)

{

  int     sec,
    usec;

  if (measure_cpu) {
    cpu_stop_internal();
    cpu_util_terminate();
  }

  gettimeofday(&time2,
	       &tz);

  if (time2.tv_usec < time1.tv_usec) {
    time2.tv_usec += 1000000;
    time2.tv_sec  -= 1;
  }

  sec     = time2.tv_sec - time1.tv_sec;
  usec    = time2.tv_usec - time1.tv_usec;
  lib_elapsed     = (float)sec + ((float)usec/(float)1000000.0);
#ifdef WIN32
  if (timed_out) lib_elapsed-=PAD_TIME/2;
#endif
  *elapsed = lib_elapsed;

}


double
calc_thruput_interval(double units_received,double elapsed)

{
  double        divisor;

  /* We will calculate the thruput in libfmt units/second */
  switch (libfmt) {
  case 'K':
    divisor = 1024.0;
    break;
  case 'M':
    divisor = 1024.0 * 1024.0;
    break;
  case 'G':
    divisor = 1024.0 * 1024.0 * 1024.0;
    break;
  case 'k':
    divisor = 1000.0 / 8.0;
    break;
  case 'm':
    divisor = 1000.0 * 1000.0 / 8.0;
    break;
  case 'g':
    divisor = 1000.0 * 1000.0 * 1000.0 / 8.0;
    break;
  case 'x':
  case 'b':
  case 'B':
    divisor = 1.0;
    break;
  case 'u':
    /* latency in microseconds a bit squirrely but we don't want to
       really muck with things for the default return statement.
       invert transactions per second and multiply to get microseconds
       per transaction */
    return (1 / (units_received / elapsed)) * 1000000.0;

  default:
    divisor = 1024.0;
  }

  return (units_received / divisor / elapsed);

}

double
calc_thruput(double units_received)

{
  return(calc_thruput_interval(units_received,lib_elapsed));
}

/* these "_omni" versions are ones which understand 'x' as a unit,
   meaning transactions/s.  we have a separate routine rather than
   convert the existing routine so we don't have to go and change
   _all_ the nettest_foo.c files at one time.  raj 2007-06-08 */

double
calc_thruput_interval_omni(double units_received,double elapsed)

{
  double        divisor;

  /* We will calculate the thruput in libfmt units/second */
  switch (libfmt) {
  case 'K':
    divisor = 1024.0;
    break;
  case 'M':
    divisor = 1024.0 * 1024.0;
    break;
  case 'G':
    divisor = 1024.0 * 1024.0 * 1024.0;
    break;
  case 'k':
    divisor = 1000.0 / 8.0;
    break;
  case 'm':
    divisor = 1000.0 * 1000.0 / 8.0;
    break;
  case 'g':
    divisor = 1000.0 * 1000.0 * 1000.0 / 8.0;
    break;
  case 'x':
  case 'b':
  case 'B':
    divisor = 1.0;
    break;
  case 'u':
    /* latency in microseconds a bit squirrely but we don't want to
       really muck with things for the default return statement.
       invert transactions per second and multiply to get microseconds
       per transaction */
    return (1 / (units_received / elapsed)) * 1000000.0;

  default:
    fprintf(where,
	    "WARNING calc_throughput_internal_omni: unknown units %c\n",
	    libfmt);
    fflush(where);
    divisor = 1024.0;
  }

  return (units_received / divisor / elapsed);

}

double
calc_thruput_omni(double units_received)

{
  return(calc_thruput_interval_omni(units_received,lib_elapsed));
}





float
calc_cpu_util(float elapsed_time)
{
  float temp_util;
  int i;
  temp_util = calc_cpu_util_internal(elapsed_time);

  /* now, what was the most utilized CPU and its util? */
  for (i = 0; i < MAXCPUS; i++) {
    if (lib_local_per_cpu_util[i] > lib_local_cpu_stats.peak_cpu_util) {
      lib_local_cpu_stats.peak_cpu_util = lib_local_per_cpu_util[i];
      lib_local_cpu_stats.peak_cpu_id = lib_cpu_map[i];
    }
  }

  return temp_util;
}

float
calc_service_demand_internal(double unit_divisor,
			     double units_sent,
			     float elapsed_time,
			     float cpu_utilization,
			     int num_cpus)

{

  double service_demand;
  double thruput;

  if (debug) {
    fprintf(where,
	    "calc_service_demand called:  units_sent = %f\n"
	    "                             elapsed_time = %f\n"
	    "                             cpu_util = %f\n"
	    "                             num cpu = %d\n",
            units_sent,
            elapsed_time,
            cpu_utilization,
            num_cpus);
    fflush(where);
  }

  if (num_cpus == 0) num_cpus = lib_num_loc_cpus;

  if (elapsed_time == 0.0) {
    elapsed_time = lib_elapsed;
  }
  if (cpu_utilization == 0.0) {
    cpu_utilization = lib_local_cpu_stats.cpu_util;
  }

  thruput = (units_sent /
             (double) unit_divisor /
             (double) elapsed_time);

  /* on MP systems, it is necessary to multiply the service demand by
     the number of CPU's. at least, I believe that to be the case:)
     raj 10/95 */

  /* thruput has a "per second" component. if we were using 100% (
     100.0) of the CPU in a second, that would be 1 second, or 1
     millisecond, so we multiply cpu_utilization by 10 to go to
     milliseconds, or 10,000 to go to micro seconds. With revision
     2.1, the service demand measure goes to microseconds per unit.
     raj 12/95 */
  service_demand = (cpu_utilization*10000.0/thruput) *
    (float) num_cpus;

  if (debug) {
    fprintf(where,
	    "calc_service_demand using:   units_sent = %f\n"
	    "                             elapsed_time = %f\n"
	    "                             cpu_util = %f\n"
	    "                             num cpu = %d\n"
	    "calc_service_demand got:     thruput = %f\n"
	    "                             servdem = %f\n",
            units_sent,
            elapsed_time,
            cpu_utilization,
            num_cpus,
            thruput,
            service_demand);
    fflush(where);
  }
  return (float)service_demand;
}

float calc_service_demand(double units_sent,
                          float elapsed_time,
                          float cpu_utilization,
                          int num_cpus)

{

  double unit_divisor = (double)1024.0;

  return(calc_service_demand_internal(unit_divisor,
				      units_sent,
				      elapsed_time,
				      cpu_utilization,
				      num_cpus));
}

/* use the value of libfmt to determine the unit_divisor */
float calc_service_demand_fmt(double units_sent,
			      float elapsed_time,
			      float cpu_utilization,
			      int num_cpus)

{
  double unit_divisor;

  if ('x' == libfmt) unit_divisor = 1.0;
  else unit_divisor = 1024.0;

  return(calc_service_demand_internal(unit_divisor,
				      units_sent,
				      elapsed_time,
				      cpu_utilization,
				      num_cpus));
}



float
calibrate_local_cpu(float local_cpu_rate)
{

  lib_num_loc_cpus = get_num_cpus();

  lib_use_idle = 0;
#ifdef USE_LOOPER
  cpu_util_init();
  lib_use_idle = 1;
#endif /* USE_LOOPER */

  if (local_cpu_rate > 0) {
    /* The user think that he knows what the cpu rate is. We assume
       that all the processors of an MP system are essentially the
       same - for this reason we do not have a per processor maxrate.
       if the machine has processors which are different in
       performance, the CPU utilization will be skewed. raj 4/95 */
    lib_local_maxrate = local_cpu_rate;
  }
  else {
    /* if neither USE_LOOPER nor USE_PSTAT are defined, we return a
       0.0 to indicate that times or getrusage should be used. raj
       4/95 */
    lib_local_maxrate = (float)0.0;
#if defined(USE_PROC_STAT) || defined(USE_LOOPER) || defined(USE_PSTAT) || defined(USE_KSTAT) || defined(USE_PERFSTAT) || defined(USE_SYSCTL)
    lib_local_maxrate = calibrate_idle_rate(4,10);
#endif
  }
  return lib_local_maxrate;
}


float
calibrate_remote_cpu()
{
  float remrate;

  netperf_request.content.request_type = CPU_CALIBRATE;
  send_request();
  /* we know that calibration will last at least 40 seconds, so go to
     sleep for that long so the 60 second select in recv_response will
     not pop. raj 7/95 */

  /* we know that CPU calibration may last as long as 40 seconds, so
     make sure we "select" for at least that long while looking for
     the response. raj 2005-05-16 */
  recv_response_timed(40);

  if (netperf_response.content.serv_errno) {
    /* initially, silently ignore remote errors and pass back a zero
       to the caller this should allow us to mix rev 1.0 and rev 1.1
       netperfs... */
    return((float)0.0);
  }
  else {
    /* the rate is the first word of the test_specific data */
    bcopy((char *)netperf_response.content.test_specific_data,
          (char *)&remrate,
          sizeof(remrate));
    bcopy((char *)netperf_response.content.test_specific_data + sizeof(remrate),
	  (char *)&lib_num_rem_cpus,
	  sizeof(lib_num_rem_cpus));
/*    remrate = (float) netperf_response.content.test_specific_data[0]; */
    return(remrate);
  }
}



#ifndef WIN32
/* WIN32 requires that at least one of the file sets to select be
   non-null.  Since msec_sleep routine is only called by nettest_dlpi
   & nettest_unix, let's duck this issue. */

int
msec_sleep( int msecs )
{
  int           rval ;

  struct timeval timeout;

  timeout.tv_sec = msecs / 1000;
  timeout.tv_usec = (msecs - (msecs/1000) *1000) * 1000;
  if ((rval = select(0,
             0,
             0,
             0,
             &timeout))) {
    if ( SOCKET_EINTR(rval) ) {
      return(1);
    }
    perror("msec_sleep: select");
    exit(1);
  }
  return(0);
}
#endif /* WIN32 */

#if defined(WANT_INTERVALS) || defined(WANT_DEMO)

int demo_mode;                    /* are we actually in demo mode? = 0
				     == not in demo mode; 1 == classic
				     unit based demo mode; 2 == always
				     timestamp demo mode */
double demo_interval = 1000000.0; /* what is the desired interval to
				     display interval results. default
				     is one second in units of
				     microseconds */
double demo_units = 0.0;          /* what is our current best guess as
				     to how many work units must be
				     done to be near the desired
				     reporting interval? */

double units_this_tick;
#endif

#ifdef WANT_DEMO
#ifdef HAVE_GETHRTIME
static hrtime_t demo_one;
static hrtime_t demo_two;
static hrtime_t *demo_one_ptr = &demo_one;
static hrtime_t *demo_two_ptr = &demo_two;
static hrtime_t *temp_demo_ptr = &demo_one;
#elif defined(WIN32)
static LARGE_INTEGER demo_one;
static LARGE_INTEGER demo_two;
static LARGE_INTEGER *demo_one_ptr = &demo_one;
static LARGE_INTEGER *demo_two_ptr = &demo_two;
static LARGE_INTEGER *temp_demo_ptr = &demo_one;
#else
static struct timeval demo_one;
static struct timeval demo_two;
static struct timeval *demo_one_ptr = &demo_one;
static struct timeval *demo_two_ptr = &demo_two;
static struct timeval *temp_demo_ptr = &demo_one;
#endif

void demo_first_timestamp() {
  HIST_timestamp(demo_one_ptr);
}

void demo_reset() {
  if (debug) {
    fprintf(where,
	    "Resetting interim results\n");
    fflush(where);
  }
  units_this_tick = 0;
  demo_first_timestamp();
}

/* for a _STREAM test, "a" should be lss_size and "b" should be
   rsr_size. for a _MAERTS test, "a" should be lsr_size and "b" should
   be rss_size. raj 2005-04-06 */
void demo_stream_setup(uint32_t a, uint32_t b) {
  if ((demo_mode) && (demo_units == 0)) {
    /* take our default value of demo_units to be the larger of
       twice the remote's SO_RCVBUF or twice our SO_SNDBUF */
    if (a > b) {
      demo_units = 2*a;
    }
    else {
      demo_units = 2*b;
    }
  }
}

void demo_interval_display(double actual_interval)
{
  static int count = 0;
  struct timeval now;

  gettimeofday(&now,NULL);
  switch (netperf_output_mode) {
  case HUMAN:
    fprintf(where,
	    "Interim result: %7.2f %s/s over %.3f seconds ending at %ld.%.3ld\n",
	    calc_thruput_interval(units_this_tick,
				  actual_interval/1000000.0),
	    format_units(),
	    actual_interval/1000000.0,
	    now.tv_sec,
	    (long) now.tv_usec/1000);
    break;
  case CSV:
    fprintf(where,
	    "%7.2f,%s/s,%.3f,%ld.%.3ld\n",
	    calc_thruput_interval(units_this_tick,
				  actual_interval/1000000.0),
	    format_units(),
	    actual_interval/1000000.0,
	    now.tv_sec,
	    (long) now.tv_usec/1000);
    break;
  case KEYVAL:
    fprintf(where,
	    "NETPERF_INTERIM_RESULT[%d]=%.2f\n"
	    "NETPERF_UNITS[%d]=%s/s\n"
	    "NETPERF_INTERVAL[%d]=%.3f\n"
	    "NETPERF_ENDING[%d]=%ld.%.3ld\n",
	    count,
	    calc_thruput_interval(units_this_tick,
				  actual_interval/1000000.0),
	    count,
	    format_units(),
	    count,
	    actual_interval/1000000.0,
	    count,
	    now.tv_sec,
	    (long) now.tv_usec/1000);
    count += 1;
    break;
  default:
    fprintf(where,
	    "Hey Ricky you not fine, theres a bug at demo time. Hey Ricky!");
    fflush(where);
    exit(-1);
  }
  fflush(where);
}

/* this has gotten long enough to warrant being an inline function
   rather than a macro, and it has been enough years since all the
   important compilers have supported such a construct so it should
   not be a big deal. raj 2012-01-23 */

void demo_interval_tick(uint32_t units)
{
  double actual_interval = 0.0;

  switch (demo_mode) {
  case 0:
    return;
  case 1: /* use the unit accumulation first */
    units_this_tick += units;
    if (units_this_tick >= demo_units) {
      /* time to possibly update demo_units and maybe output an
	 interim result */
      HIST_timestamp(demo_two_ptr);
      actual_interval = delta_micro(demo_one_ptr,demo_two_ptr);
      /* we always want to fine-tune demo_units here whether we emit
	 an interim result or not.  if we are short, this will
	 lengthen demo_units.  if we are long, this will shorten it */
      demo_units = demo_units * (demo_interval / actual_interval);
    }
    else
      return;
    break;
  case 2:  /* Always timestamp */
    units_this_tick += units;
    HIST_timestamp(demo_two_ptr);
    actual_interval = delta_micro(demo_one_ptr,demo_two_ptr);

    break;
  default:
    fprintf(where,
	    "Unexpected value of demo_mode of %d. Please report this as a bug.\n",
	    demo_mode);
    fflush(where);
    exit(-1);
  }



  /* units == 0 will be when we have completed a test.  we want to
     emit a final interim results if there is anything to report */
  if (actual_interval >= demo_interval) {
    /* time to emit an interim result, giving the current time to the
       millisecond for compatability with RRD  */
    demo_interval_display(actual_interval);
    units_this_tick = 0.0;
    /* now get a new starting timestamp.  we could be clever
       and swap pointers - the math we do probably does not
       take all that long, but for now this will suffice */
    temp_demo_ptr = demo_one_ptr;
    demo_one_ptr = demo_two_ptr;
    demo_two_ptr = temp_demo_ptr;

  }
}

void demo_interval_final() {
  double actual_interval;

  switch (demo_mode) {
  case 0:
    return;
  case 1:
  case 2:
    if (units_this_tick > 0.0) {
      HIST_timestamp(demo_two_ptr);
      actual_interval = delta_micro(demo_one_ptr,demo_two_ptr);
      demo_interval_display(actual_interval);
      units_this_tick = 0.0;
    }
  }
}

void demo_stream_interval(uint32_t units) {
  demo_interval_tick(units);
}

void demo_rr_setup(uint32_t a) {
  if ((demo_mode) && (demo_units == 0)) {
    /* take whatever we are given */
    demo_units = a;
  }
}

void demo_rr_interval(uint32_t units) {
  demo_interval_tick(units);
}

#endif

/* hist.c

   Given a time difference in microseconds, increment one of 61
   different buckets:

   0 - 9 in increments of 1 usec
   0 - 9 in increments of 10 usecs
   0 - 9 in increments of 100 usecs
   1 - 9 in increments of 1 msec
   1 - 9 in increments of 10 msecs
   1 - 9 in increments of 100 msecs
   1 - 9 in increments of 1 sec
   1 - 9 in increments of 10 sec
   > 100 secs

   This will allow any time to be recorded to within an accuracy of
   10%, and provides a compact representation for capturing the
   distribution of a large number of time differences (e.g.
   request-response latencies).

   Colin Low  10/6/93
   Rick Jones 2004-06-15 extend to unit and ten usecs
*/

/* #include "sys.h" */

/*#define HIST_TEST*/

HIST
HIST_new_n(int max_outstanding) {
  HIST h;

  if((h = (HIST) malloc(sizeof(struct histogram_struct))) == NULL) {
    perror("HIST_new_n - histogram_struct malloc failed");
    exit(1);
  }
  HIST_clear(h);

  /* we never want to have a full queue, so will trade a little space
     for that. one day we may still have to check for a full queue */
  h->limit = max_outstanding + 1;

  /* now allocate the time_ones based on h->limit */
#ifdef HAVE_GETHRTIME
  h->time_ones = (hrtime_t *) malloc(h->limit * sizeof(hrtime_t));
#elif HAVE_GET_HRT
  h->time_ones = (hrt_t *) malloc(h->limit * sizeof(hrt_t));
#elif defined(WIN32)
  h->time_ones = (LARGE_INTEGER *) malloc(h->limit *
					  sizeof(LARGE_INTEGER));
#else
  h->time_ones = (struct timeval *) malloc(h->limit *
					   sizeof(struct timeval));
#endif /* HAVE_GETHRTIME */
  if (h->time_ones == NULL) {
    perror("HIST_new_n - time_ones malloc failed");
    exit(1);
  }

  return h;
}

HIST
HIST_new(void){
  return HIST_new_n(0);
}


void
HIST_clear(HIST h){
   int i;
   for(i = 0; i < HIST_NUM_OF_BUCKET; i++){
      h->unit_usec[i] = 0;
      h->ten_usec[i] = 0;
      h->hundred_usec[i] = 0;
      h->unit_msec[i] = 0;
      h->ten_msec[i] = 0;
      h->hundred_msec[i] = 0;
      h->unit_sec[i] = 0;
      h->ten_sec[i] = 0;
   }
   h->ridiculous = 0;
   h->total = 0;
   h->sum = 0;
   h->sumsquare = 0;
   h->hmin = 0;
   h->hmax = 0;
   h->limit = 0;
   h->count = 0;
   h->producer = 0;
   h->consumer = 0;
   h->time_ones = NULL;
}

void
HIST_purge(HIST h) {
  h->count = 0;
  h->producer = 0;
  h->consumer = 0;
}

void
HIST_add(register HIST h, int time_delta){
   register float val;
   register int base = HIST_NUM_OF_BUCKET / 10;

   /* check for < 0 added via VMware ESX patches. */

   /* hoisted up to the top because we do not want to count any
      ridiculous values in the actual statistics. right? raj
      2011-07-28 */
   if (time_delta < 0) {
     h->ridiculous++;
     return;
   }

   if (!h->total)
      h->hmin = h->hmax = time_delta;
   h->total++;
   h->sum += time_delta;
   /* am I just being paranoid about the overhead of pow() when we
      aren't all that interested in the statistics derived from it?
      raj 20100914 */
   if (keep_statistics) {
     h->sumsquare += pow(time_delta, 2);
   }
   h->hmin = ((h->hmin < time_delta) ? h->hmin : time_delta);
   h->hmax = ((h->hmax > time_delta) ? h->hmax : time_delta);
   val = (float) time_delta;
   if(val < 10) h->unit_usec[(int)(val * base)]++;
   else {
     val /= 10;
     if(val < 10) h->ten_usec[(int)(val * base)]++;
     else {
       val /= 10;
       if(val < 10) h->hundred_usec[(int)(val * base)]++;
       else {
	 val /= 10;
	 if(val < 10) h->unit_msec[(int)(val * base)]++;
	 else {
	   val /= 10;
	   if(val < 10) h->ten_msec[(int)(val * base)]++;
	   else {
	     val /= 10;
	     if(val < 10) h->hundred_msec[(int)(val * base)]++;
	     else {
               val /= 10;
               if(val < 10) h->unit_sec[(int)(val * base)]++;
               else {
		 val /= 10;
		 if(val < 10) h->ten_sec[(int)(val * base)]++;
		 else h->ridiculous++;
               }
	     }
	   }
	 }
       }
     }
   }
}

void
output_row(FILE *fd, char *title, int *row){
  register int i;
  register int j;
  register int base =  HIST_NUM_OF_BUCKET / 10;
  register int sum;
  fprintf(where,"%s", title);
  for(i = 0; i < 10; i++){
    sum = 0;
    for (j = i * base; j <  (i + 1) * base; j++) {
      sum += row[j];
    }
    fprintf(where,": %4d", sum);
  }
  fprintf(where,"\n");
}

int
sum_row(int *row) {
  int sum = 0;
  int i;
  for (i = 0; i < HIST_NUM_OF_BUCKET; i++) sum += row[i];
  return(sum);
}

void
HIST_report(HIST h){
#ifndef OLD_HISTOGRAM
   output_row(stdout, "UNIT_USEC     ", h->unit_usec);
   output_row(stdout, "TEN_USEC      ", h->ten_usec);
   output_row(stdout, "HUNDRED_USEC  ", h->hundred_usec);
#else
   h->hundred_usec[0] += sum_row(h->unit_usec);
   h->hundred_usec[0] += sum_row(h->ten_usec);
   output_row(stdout, "TENTH_MSEC    ", h->hundred_usec);
#endif
   output_row(stdout, "UNIT_MSEC     ", h->unit_msec);
   output_row(stdout, "TEN_MSEC      ", h->ten_msec);
   output_row(stdout, "HUNDRED_MSEC  ", h->hundred_msec);
   output_row(stdout, "UNIT_SEC      ", h->unit_sec);
   output_row(stdout, "TEN_SEC       ", h->ten_sec);
   fprintf(where,">100_SECS: %d\n", h->ridiculous);
   fprintf(where,"HIST_TOTAL:      %d\n", h->total);
   if (debug) {
     fprintf(where,
	     "sum %"PRIi64", sumsquare %f, limit %d count %d\n",
	     h->sum,
	     h->sumsquare,
	     h->limit,
	     h->count);
   }
}

/* search buckets for each unit */
int
HIST_search_bucket(int *unit, int num, int *last, int *current, double scale){
  int base = HIST_NUM_OF_BUCKET / 10;
  int i;
  for (i = 0; i < HIST_NUM_OF_BUCKET; i++){
    *last = *current;
    *current += unit[i];
    if (*current >= num)
      return (int)((i + (double)(num - *last)/(*current - *last)) * scale/base);
  }
  return 0;
}

/* get percentile from histogram */
int
HIST_get_percentile(HIST h, const double percentile){
  double win_kludge = percentile * (double) h->total;
  int num = (int) win_kludge;
  int last = 0;
  int current = 0;
  int result;

  if (!num)
    return 0;

  /* search in unit usec range */
  result = HIST_search_bucket(h->unit_usec, num, &last, &current, 1e0);
  if (result)
    return result;

  /* search in ten usec range */
  result = HIST_search_bucket(h->ten_usec, num, &last, &current, 1e1);
  if (result)
    return result;

  /* search in ten hundred usec range */
  result = HIST_search_bucket(h->hundred_usec, num, &last, &current, 1e2);
  if (result)
    return result;

  /* search in unic msec range */
  result = HIST_search_bucket(h->unit_msec, num, &last, &current, 1e3);
  if (result)
    return result;

  /* search in ten msec range */
  result = HIST_search_bucket(h->ten_msec, num, &last, &current, 1e4);
  if (result)
    return result;

  /* search in hundred msec range */
  result = HIST_search_bucket(h->hundred_msec, num, &last, &current, 1e5);
  if (result)
    return result;

  /* search in unit sec range */
  result = HIST_search_bucket(h->unit_sec, num, &last, &current, 1e6);
  if (result)
    return result;

  /* search in ten sec range */
  result = HIST_search_bucket(h->ten_sec, num, &last, &current, 1e7);
  if (result)
    return result;

  return (int)(1e8);
}


/* get basic stats */
void
HIST_get_stats(HIST h, int *min, int *max, double *mean, double *stddev){
  *min = h->hmin;
  *max = h->hmax;
  if (h->total){
    *mean = (double)h->sum / (double)h->total;
    *stddev = (h->sumsquare * h->total - pow((double)h->sum, 2)) /
      pow(h->total, 2);
    *stddev = sqrt(*stddev);
  }
  else{
    *mean = 0;
    *stddev = 0;
  }
}


/* with the advent of sit-and-spin intervals support, we might as well
   make these things available all the time, not just for demo or
   histogram modes. raj 2006-02-06 */
#ifdef HAVE_GETHRTIME

void
HIST_timestamp(hrtime_t *timestamp)
{
  *timestamp = gethrtime();
}

int
delta_micro(hrtime_t *begin, hrtime_t *end)
{
  long nsecs;
  nsecs = (*end) - (*begin);
  return(nsecs/1000);
}

#elif defined(HAVE_GET_HRT)
#include "hrt.h"

void
HIST_timestamp(hrt_t *timestamp)
{
  *timestamp = get_hrt();
}

int
delta_micro(hrt_t *begin, hrt_t *end)
{

  return((int)get_hrt_delta(*end,*begin));

}
#elif defined(WIN32)
void HIST_timestamp(LARGE_INTEGER *timestamp)
{
	QueryPerformanceCounter(timestamp);
}

int delta_micro(LARGE_INTEGER *begin, LARGE_INTEGER *end)
{
	LARGE_INTEGER DeltaTimestamp;
	static LARGE_INTEGER TickHz = {{0,0}};

	if (TickHz.QuadPart == 0)
	{
		QueryPerformanceFrequency(&TickHz);
	}

	/*+*+ Rick; this will overflow after ~2000 seconds, is that
	  good enough? Spencer: Yes, that should be more than good
	  enough for histogram support */

	DeltaTimestamp.QuadPart = (end->QuadPart - begin->QuadPart) *
	  1000000/TickHz.QuadPart;
	assert((DeltaTimestamp.HighPart == 0) &&
	       ((int)DeltaTimestamp.LowPart >= 0));

	return (int)DeltaTimestamp.LowPart;
}

#else

void
HIST_timestamp(struct timeval *timestamp)
{
  gettimeofday(timestamp,NULL);
}

 /* return the difference (in micro seconds) between two timeval */
 /* timestamps */
int
delta_micro(struct timeval *begin,struct timeval *end)

{

  int usecs, secs;

  if (end->tv_usec < begin->tv_usec) {
    /* borrow a second from the tv_sec */
    end->tv_usec += 1000000;
    end->tv_sec--;
  }
  usecs = end->tv_usec - begin->tv_usec;
  secs  = end->tv_sec - begin->tv_sec;

  usecs += (secs * 1000000);

  return(usecs);

}
#endif /* HAVE_GETHRTIME */

void
HIST_timestamp_start(HIST h) {

  if (NULL == h) {
    fprintf(where,"HIST_timestamp_start called with NULL histogram\n");
    fflush(where);
    exit(-1);
  }
  if (h->count == h->limit) {
    fprintf(where,"HIST_timestamp_start called with full time_ones\n");
  }

  HIST_timestamp(&(h->time_ones[h->producer]));
  h->producer += 1;
  h->producer %= h->limit;
  h->count += 1;


}

/* snap an ending timestamp and add the delta to the histogram */
void
HIST_timestamp_stop_add(HIST h) {

  if (NULL == h) {
    fprintf(where,"HIST_timestamp_stop called with NULL histogram\n");
    fflush(where);
    exit(-1);
  }

  if (h->consumer == h->producer) {
    fprintf(where,
	    "HIST_timestamp_stop called with empty time_ones consumer %d producer %d\n",
	    h->consumer,
	    h->producer);
    fflush(where);
    exit(-1);
  }
  /* take our stopping timestamp */
  HIST_timestamp(&(h->time_two));

  /* now add it */
  HIST_add(h,delta_micro(&(h->time_ones[h->consumer]),&(h->time_two)));
  h->consumer += 1;
  h->consumer %= h->limit;
  h->count -= 1;

}



/* these routines for confidence intervals are courtesy of IBM. They
   have been modified slightly for more general usage beyond TCP/UDP
   tests. raj 11/94 I would suspect that this code carries an IBM
   copyright that is much the same as that for the original HP netperf
   code */
int     confidence_iterations; /* for iterations */

double
  result_confid=-10.0,
  loc_cpu_confid=-10.0,
  rem_cpu_confid=-10.0,

  measured_sum_result=0.0,
  measured_square_sum_result=0.0,
  measured_mean_result=0.0,
  measured_var_result=0.0,

  measured_sum_local_cpu=0.0,
  measured_square_sum_local_cpu=0.0,
  measured_mean_local_cpu=0.0,
  measured_var_local_cpu=0.0,

  measured_sum_remote_cpu=0.0,
  measured_square_sum_remote_cpu=0.0,
  measured_mean_remote_cpu=0.0,
  measured_var_remote_cpu=0.0,

  measured_sum_local_service_demand=0.0,
  measured_square_sum_local_service_demand=0.0,
  measured_mean_local_service_demand=0.0,
  measured_var_local_service_demand=0.0,

  measured_sum_remote_service_demand=0.0,
  measured_square_sum_remote_service_demand=0.0,
  measured_mean_remote_service_demand=0.0,
  measured_var_remote_service_demand=0.0,

  measured_sum_local_time=0.0,
  measured_square_sum_local_time=0.0,
  measured_mean_local_time=0.0,
  measured_var_local_time=0.0,

  measured_mean_remote_time=0.0,

  measured_fails,
  measured_local_results,
  confidence=-10.0;
/*  interval=0.1; */

/************************************************************************/
/*                                                                      */
/*      Constants for Confidence Intervals                              */
/*                                                                      */
/************************************************************************/
void
init_stat()
{
        measured_sum_result=0.0;
        measured_square_sum_result=0.0;
        measured_mean_result=0.0;
        measured_var_result=0.0;

        measured_sum_local_cpu=0.0;
        measured_square_sum_local_cpu=0.0;
        measured_mean_local_cpu=0.0;
        measured_var_local_cpu=0.0;

        measured_sum_remote_cpu=0.0;
        measured_square_sum_remote_cpu=0.0;
        measured_mean_remote_cpu=0.0;
        measured_var_remote_cpu=0.0;

        measured_sum_local_service_demand=0.0;
        measured_square_sum_local_service_demand=0.0;
        measured_mean_local_service_demand=0.0;
        measured_var_local_service_demand=0.0;

        measured_sum_remote_service_demand=0.0;
        measured_square_sum_remote_service_demand=0.0;
        measured_mean_remote_service_demand=0.0;
        measured_var_remote_service_demand=0.0;

        measured_sum_local_time=0.0;
        measured_square_sum_local_time=0.0;
        measured_mean_local_time=0.0;
        measured_var_local_time=0.0;

        measured_mean_remote_time=0.0;

        measured_fails = 0.0;
        measured_local_results=0.0,
        confidence=-10.0;
}

/* this routine does a simple table lookup for some statistical
   function that I would remember if I stayed awake in my probstats
   class... raj 11/94 */
double
confid(int level, int freedom)
{
double  t99[35],t95[35];

   t95[1]=12.706;
   t95[2]= 4.303;
   t95[3]= 3.182;
   t95[4]= 2.776;
   t95[5]= 2.571;
   t95[6]= 2.447;
   t95[7]= 2.365;
   t95[8]= 2.306;
   t95[9]= 2.262;
   t95[10]= 2.228;
   t95[11]= 2.201;
   t95[12]= 2.179;
   t95[13]= 2.160;
   t95[14]= 2.145;
   t95[15]= 2.131;
   t95[16]= 2.120;
   t95[17]= 2.110;
   t95[18]= 2.101;
   t95[19]= 2.093;
   t95[20]= 2.086;
   t95[21]= 2.080;
   t95[22]= 2.074;
   t95[23]= 2.069;
   t95[24]= 2.064;
   t95[25]= 2.060;
   t95[26]= 2.056;
   t95[27]= 2.052;
   t95[28]= 2.048;
   t95[29]= 2.045;
   t95[30]= 2.042;

   t99[1]=63.657;
   t99[2]= 9.925;
   t99[3]= 5.841;
   t99[4]= 4.604;
   t99[5]= 4.032;
   t99[6]= 3.707;
   t99[7]= 3.499;
   t99[8]= 3.355;
   t99[9]= 3.250;
   t99[10]= 3.169;
   t99[11]= 3.106;
   t99[12]= 3.055;
   t99[13]= 3.012;
   t99[14]= 2.977;
   t99[15]= 2.947;
   t99[16]= 2.921;
   t99[17]= 2.898;
   t99[18]= 2.878;
   t99[19]= 2.861;
   t99[20]= 2.845;
   t99[21]= 2.831;
   t99[22]= 2.819;
   t99[23]= 2.807;
   t99[24]= 2.797;
   t99[25]= 2.787;
   t99[26]= 2.779;
   t99[27]= 2.771;
   t99[28]= 2.763;
   t99[29]= 2.756;
   t99[30]= 2.750;

   if(level==95){
        return(t95[freedom]);
   } else if(level==99){
        return(t99[freedom]);
   } else{
        return(0);
   }
}

void
calculate_confidence(int confidence_iterations,
                     float time,
                     double result,
                     float loc_cpu,
                     float rem_cpu,
                     float loc_sd,
                     float rem_sd)
{

  if (debug) {
    fprintf(where,
            "calculate_confidence: itr  %d; time %f; res  %f\n"
            "                               lcpu %f; rcpu %f\n"
            "                               lsdm %f; rsdm %f\n",
            confidence_iterations,
            time,
            result,
            loc_cpu,
            rem_cpu,
            loc_sd,
            rem_sd);
    fflush(where);
  }

  /* the test time */
  measured_sum_local_time               +=
    (double) time;
  measured_square_sum_local_time        +=
    (double) time*time;
  measured_mean_local_time              =
    (double) measured_sum_local_time/confidence_iterations;
  measured_var_local_time               =
    (double) measured_square_sum_local_time/confidence_iterations
      -measured_mean_local_time*measured_mean_local_time;

  /* the test result */
  measured_sum_result           +=
    (double) result;
  measured_square_sum_result    +=
    (double) result*result;
  measured_mean_result          =
    (double) measured_sum_result/confidence_iterations;
  measured_var_result           =
    (double) measured_square_sum_result/confidence_iterations
      -measured_mean_result*measured_mean_result;

  /* local cpu utilization */
  measured_sum_local_cpu        +=
    (double) loc_cpu;
  measured_square_sum_local_cpu +=
    (double) loc_cpu*loc_cpu;
  measured_mean_local_cpu       =
    (double) measured_sum_local_cpu/confidence_iterations;
  measured_var_local_cpu        =
    (double) measured_square_sum_local_cpu/confidence_iterations
      -measured_mean_local_cpu*measured_mean_local_cpu;

  /* remote cpu util */
  measured_sum_remote_cpu       +=
    (double) rem_cpu;
  measured_square_sum_remote_cpu+=
    (double) rem_cpu*rem_cpu;
  measured_mean_remote_cpu      =
    (double) measured_sum_remote_cpu/confidence_iterations;
  measured_var_remote_cpu       =
    (double) measured_square_sum_remote_cpu/confidence_iterations
      -measured_mean_remote_cpu*measured_mean_remote_cpu;

  /* local service demand */
  measured_sum_local_service_demand     +=
    (double) loc_sd;
  measured_square_sum_local_service_demand+=
    (double) loc_sd*loc_sd;
  measured_mean_local_service_demand    =
    (double) measured_sum_local_service_demand/confidence_iterations;
  measured_var_local_service_demand     =
    (double) measured_square_sum_local_service_demand/confidence_iterations
      -measured_mean_local_service_demand*measured_mean_local_service_demand;

  /* remote service demand */
  measured_sum_remote_service_demand    +=
    (double) rem_sd;
  measured_square_sum_remote_service_demand+=
    (double) rem_sd*rem_sd;
  measured_mean_remote_service_demand   =
    (double) measured_sum_remote_service_demand/confidence_iterations;
  measured_var_remote_service_demand    =
    (double) measured_square_sum_remote_service_demand/confidence_iterations
      -measured_mean_remote_service_demand*measured_mean_remote_service_demand;

  if(confidence_iterations>1){
     result_confid= (double) interval -
       2.0 * confid(confidence_level,confidence_iterations-1)*
         sqrt(measured_var_result/(confidence_iterations-1.0)) /
           measured_mean_result;

     loc_cpu_confid= (double) interval -
       2.0 * confid(confidence_level,confidence_iterations-1)*
         sqrt(measured_var_local_cpu/(confidence_iterations-1.0)) /
           measured_mean_local_cpu;

     rem_cpu_confid= (double) interval -
       2.0 * confid(confidence_level,confidence_iterations-1)*
         sqrt(measured_var_remote_cpu/(confidence_iterations-1.0)) /
           measured_mean_remote_cpu;

     if(debug){
       printf("Conf_itvl %2d: results:%4.1f%% loc_cpu:%4.1f%% rem_cpu:%4.1f%%\n",
              confidence_iterations,
              (interval-result_confid)*100.0,
              (interval-loc_cpu_confid)*100.0,
              (interval-rem_cpu_confid)*100.0);
     }

     /* if the user has requested that we only wait for the result to
	be confident rather than the result and CPU util(s) then do
	so. raj 2007-08-08 */
     if (!result_confidence_only) {
       confidence = min(min(result_confid,loc_cpu_confid),rem_cpu_confid);
     }
     else {
       confidence = result_confid;
     }
  }
}

 /* here ends the IBM code */

void
retrieve_confident_values(float *elapsed_time,
                          double *thruput,
                          float *local_cpu_utilization,
                          float *remote_cpu_utilization,
                          float *local_service_demand,
                          float *remote_service_demand)

{
  *elapsed_time            = (float)measured_mean_local_time;
  *thruput                 = measured_mean_result;
  *local_cpu_utilization   = (float)measured_mean_local_cpu;
  *remote_cpu_utilization  = (float)measured_mean_remote_cpu;
  *local_service_demand    = (float)measured_mean_local_service_demand;
  *remote_service_demand   = (float)measured_mean_remote_service_demand;
}

double
get_result_confid()
{
  return (double) (100.0 * (interval - result_confid));
}

double
get_loc_cpu_confid()
{
  return (double) (100.0 * (interval - loc_cpu_confid));
}

double
get_rem_cpu_confid()
{
  return (double) (100.0 * (interval - rem_cpu_confid));
}

/* display_confidence() is called when we could not achieve the
   desired confidence in the results. it will print the achieved
   confidence to "where" raj 11/94 */
void
display_confidence()

{
  fprintf(where,
          "!!! WARNING\n"
          "!!! Desired confidence was not achieved within "
          "the specified iterations.\n"
          "!!! This implies that there was variability in "
          "the test environment that\n"
          "!!! must be investigated before going further.\n"
          "!!! Confidence intervals: Throughput      : %4.3f%%\n"
          "!!!                       Local CPU util  : %4.3f%%\n"
          "!!!                       Remote CPU util : %4.3f%%\n\n",
          100.0 * (interval - result_confid),
          100.0 * (interval - loc_cpu_confid),
          100.0 * (interval - rem_cpu_confid));
}
