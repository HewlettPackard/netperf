
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WANT_OMNI
char nettest_omni_id[]="\
@(#)nettest_omni.c (c) Copyright 2008-2012 Hewlett-Packard Co. Version 2.6.0";

#include <stdio.h>
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#if HAVE_STRING_H
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif
#if HAVE_STRINGS_H
# include <strings.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#if HAVE_SCHED_H
# include <sched.h>
#endif

#include <fcntl.h>
#ifndef WIN32
#include <errno.h>
#include <signal.h>
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <ctype.h>

#ifdef NOSTDLIBH
#include <malloc.h>
#endif /* NOSTDLIBH */

#include <assert.h>

#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>

/* it would seem that including both <netinet/tcp.h> and <linux/tcp.h>
   is not a path to happiness and joy when one wishes to grab tcp_info
   stats and not get something like the compiler complaining about
   either redefinitions, or missing tcpi_total_retrans. */
#ifdef HAVE_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/tcp.h>
#endif

/* this is to get us the definition of MSG_FASTOPEN. we may need to
   cheat just a litle at first */
#ifdef HAVE_LINUX_SOCKET_H
# include <linux/socket.h>
# ifndef MSG_FASTOPEN
#  warning Using our own value for MSG_FASTOPEN
#  define MSG_FASTOPEN	0x20000000	/* Send data in TCP SYN */
# endif
# ifndef TCP_FASTOPEN
#  warning Using our own value for TCP_FASTOPEN
#  define TCP_FASTOPEN 23
# endif
#endif

#ifdef HAVE_NETINET_SCTP_H
#include <netinet/sctp.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>
#else /* WIN32 */
#include <process.h>
#define netperf_socklen_t socklen_t
#include <winsock2.h>
/* while it is unlikely that anyone running Windows 2000 or NT 4 is
   going to be trying to compile this, if they are they will want to
   define DONT_IPV6 in the sources file */
#ifndef DONT_IPV6
#include <ws2tcpip.h>
#endif
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#define sleep(x) Sleep((x)*1000)

#include "missing\stdint.h"
#endif /* WIN32 */

/* We don't want to use bare constants in the shutdown() call.  In the
   extremely unlikely event that SHUT_WR isn't defined, we will define
   it to the value we used to be passing to shutdown() anyway.  raj
   2007-02-08 */
#if !defined(SHUT_WR)
#define SHUT_WR 1
#endif

#if !defined(HAVE_GETADDRINFO) || !defined(HAVE_GETNAMEINFO)
# include "missing/getaddrinfo.h"
#endif

#include "netlib.h"
#include "netsh.h"
#include "nettest_bsd.h"

/* we only really use this once, but the initial patch to
   src/nettest_bsd.c used it in several places. keep it as a macro
   just for kicks and just in case we do end-up needing to use it
   multiple times. */

#define WAIT_BEFORE_DATA_TRAFFIC() \
{ \
  if (wait_time_secs) \
    sleep(wait_time_secs); \
} \


/* since someone can ask for latency stats, we will always include
   this and do the other other things */
#include "hist.h"

static HIST time_hist;



#ifdef WANT_INTERVALS
int interval_count;
unsigned int interval_wait_microseconds;

/* hoist the timestamps up here so we can use them to factor-out the
   time spent "waiting" */
/* first out timestamp */
#ifdef HAVE_GETHRTIME
static hrtime_t intvl_one;
static hrtime_t intvl_two;
static hrtime_t intvl_wait_start;
static hrtime_t *intvl_one_ptr = &intvl_one;
static hrtime_t *intvl_two_ptr = &intvl_two;
static hrtime_t *temp_intvl_ptr = &intvl_one;
#elif defined(WIN32)
static LARGE_INTEGER intvl_one;
static LARGE_INTEGER intvl_two;
static LARGE_INTEGER intvl_wait_start;
static LARGE_INTEGER *intvl_one_ptr = &intvl_one;
static LARGE_INTEGER *intvl_two_ptr = &intvl_two;
static LARGE_INTEGER *temp_intvl_ptr = &intvl_one;
#else
static struct timeval intvl_one;
static struct timeval intvl_two;
static struct timeval intvl_wait_start;
static struct timeval *intvl_one_ptr = &intvl_one;
static struct timeval *intvl_two_ptr = &intvl_two;
static struct timeval *temp_intvl_ptr = &intvl_one;
#endif

#ifndef WANT_SPIN
#ifdef WIN32
#define INTERVALS_INIT() \
    if (interval_burst) { \
      /* zero means that we never pause, so we never should need the \
         interval timer. we used to use it for demo mode, but we deal \
	 with that with a variant on watching the clock rather than \
	 waiting for a timer. raj 2006-02-06 */ \
      start_itimer(interval_wate); \
    } \
    interval_count = interval_burst; \
    interval_wait_microseconds = 0;
#else
sigset_t signal_set;
#define INTERVALS_INIT() \
    if (interval_burst) { \
      /* zero means that we never pause, so we never should need the \
         interval timer. we used to use it for demo mode, but we deal \
	 with that with a variant on watching the clock rather than \
	 waiting for a timer. raj 2006-02-06 */ \
      start_itimer(interval_wate); \
    } \
    interval_count = interval_burst; \
    interval_wait_microseconds = 0; \
    /* get the signal set for the call to sigsuspend */ \
    if (sigprocmask(SIG_BLOCK, (sigset_t *)NULL, &signal_set) != 0) { \
      fprintf(where, \
	      "%s: unable to get sigmask errno %d\n", \
	      __FUNCTION__, \
	      errno); \
      fflush(where); \
      exit(1); \
    }
#endif /* WIN32 */

#ifdef WIN32
#define INTERVALS_WAIT() \
      /* in this case, the interval count is the count-down counter \
	 to decide to sleep for a little bit */ \
      if ((interval_burst) && (--interval_count == 0)) { \
	/* call WaitForSingleObject and wait for the interval timer to get us \
	   out */ \
	if (debug > 1) { \
	  fprintf(where,"about to suspend\n"); \
	  fflush(where); \
	} \
        HIST_timestamp(&intvl_wait_start); \
    if (WaitForSingleObject(WinTimer, INFINITE) != WAIT_OBJECT_0) { \
        fprintf(where, "WaitForSingleObject failed (%d)\n", GetLastError()); \
	  fflush(where); \
	  exit(1); \
	} \
        HIST_timestamp(&intvl_two); \
        interval_wait_microseconds += \
          delta_micro(&intvl_wait_start,&intvl_two); \
	interval_count = interval_burst; \
      }
#else
#define INTERVALS_WAIT() \
      /* in this case, the interval count is the count-down couter \
	 to decide to sleep for a little bit */ \
      if ((interval_burst) && (--interval_count == 0)) { \
	/* call sigsuspend and wait for the interval timer to get us \
	   out */ \
	if (debug > 1) { \
	  fprintf(where,"about to suspend\n"); \
	  fflush(where); \
	} \
        HIST_timestamp(&intvl_wait_start); \
	if (sigsuspend(&signal_set) == EFAULT) { \
	  fprintf(where, \
		  "%s: fault with sigsuspend.\n", \
                  __FUNCTION__); \
	  fflush(where); \
	  exit(1); \
	} \
        HIST_timestamp(&intvl_two); \
        interval_wait_microseconds += \
          delta_micro(&intvl_wait_start,&intvl_two); \
	interval_count = interval_burst; \
      }
#endif /* WIN32 */
#else

#define INTERVALS_INIT() \
      if (interval_burst) { \
	HIST_timestamp(intvl_one_ptr); \
      } \
      interval_wait_microseconds = 0; \
      interval_count = interval_burst; \

#define INTERVALS_WAIT() \
      /* in this case, the interval count is the count-down couter \
	 to decide to sleep for a little bit */ \
      if ((interval_burst) && (--interval_count == 0)) { \
	/* spin and wait for the interval timer to get us \
	   out */ \
	if (debug > 1) { \
	  fprintf(where,"about to spin suspend\n"); \
	  fflush(where); \
	} \
        \
        HIST_timestamp(&intvl_wait_start); \
        do { \
          HIST_timestamp(intvl_two_ptr); } \
        while(delta_micro(intvl_one_ptr,intvl_two_ptr) < interval_usecs); \
        interval_wait_microseconds += \
          delta_micro(&intvl_wait_start,&intvl_two); \
	temp_intvl_ptr = intvl_one_ptr; \
	intvl_one_ptr = intvl_two_ptr; \
	intvl_two_ptr = temp_intvl_ptr; \
	interval_count = interval_burst; \
      }
#endif
#endif

#define NETPERF_WAITALL 0x1

extern void get_uuid_string(char *string, size_t size);

/* a boatload of globals while I settle things out */
char *output_selection_spec = NULL;

char test_uuid[38];

double result_confid_pct = -1.0;
double loc_cpu_confid_pct = -1.0;
double rem_cpu_confid_pct = -1.0;
double interval_pct = -1.0;

int protocol;
int direction;
int remote_send_size = -1;
int remote_recv_size = -1;
int remote_send_size_req = -1;
int remote_recv_size_req = -1;
int remote_use_sendfile;

extern int loc_dirty_count;
extern int loc_clean_count;
extern int rem_dirty_count;
extern int rem_clean_count;
int remote_checksum_off;
int connection_test;
int dont_give_up = 0;
int use_fastopen = 0;
int use_mtu_discover = 0;
int use_write = 0;
int need_to_connect;
int need_connection;
int bytes_to_send;
double bytes_per_send;
int failed_sends;
int bytes_to_recv;
double bytes_per_recv;
int null_message_ok = 0;

int was_legacy = 0;
int legacy = 0;
int implicit_direction = 0;
int explicit_data_address = 0;
int want_defer_accept = 0;

uint64_t      trans_completed = 0;
int64_t       units_remaining;
uint64_t      bytes_sent = 0;
uint64_t      bytes_received = 0;
uint64_t      local_send_calls = 0;
uint64_t      local_receive_calls = 0;
uint64_t      remote_bytes_sent;
uint64_t      remote_bytes_received;
uint64_t      remote_send_calls;
uint64_t      remote_receive_calls;
double        bytes_xferd;
double        remote_bytes_xferd;
double        remote_bytes_per_recv;
double        remote_bytes_per_send;
float         elapsed_time;
float         local_cpu_utilization;
float	      local_service_demand;
float         remote_cpu_utilization;
float	      remote_service_demand;
double	      thruput;
double        local_send_thruput;
double        local_recv_thruput;
double        remote_send_thruput;
double        remote_recv_thruput;

/* kludges for omni output */
double      elapsed_time_double;
double      local_service_demand_double;
double      remote_service_demand_double;
double      transaction_rate = 1.0;
double      rtt_latency = -1.0;
int32_t     transport_mss = -2;
int32_t     local_transport_retrans = -2;
int32_t     remote_transport_retrans = -2;
char        *deprecated_str = "Deprecated";
int         remote_interface_vendor = -2;
int         remote_interface_device = -2;
int         remote_interface_subvendor = -2;
int         remote_interface_subdevice = -2;
int         local_interface_vendor = -2;
int         local_interface_device = -2;
int         local_interface_subvendor = -2;
int         local_interface_subdevice = -2;
int         local_cpu_frequency = 0;
int         remote_cpu_frequency = 0;

int         local_security_type_id;
int         local_security_enabled_num;
int         remote_security_type_id;
int         remote_security_enabled_num;

char        local_cong_control[16] = "";
char        remote_cong_control[16] = "";
char        local_cong_control_req[16] = "";
char        remote_cong_control_req[16] = "";

int         receive_timeout = -1;

/* new statistics based on code diffs from Google, with raj's own
   personal twist added to make them compatible with the omni
   tests... 20100913 */

/* min and max "latency" */
int         min_latency = -1, max_latency = -1;
/* the percentiles */
int         p50_latency = -1, p90_latency = -1, p99_latency = -1;
/* mean and stddev - while the mean is reduntant with the *_RR test we
   keep it because it won't be for other tests */
double      mean_latency = -1.0, stddev_latency = -1.0;

/* default to zero to avoid randomizing */
int local_mask_len = 0;
int remote_mask_len = 0;

int printing_initialized = 0;

char *sd_str;
char *thruput_format_str;

char *socket_type_str;
char *protocol_str;
char *direction_str;

extern int first_burst_size;

int  parallel_connections = 1;

static int socket_debug = 0;

#if defined(HAVE_SENDFILE) && (defined(__linux) || defined(__sun))
#include <sys/sendfile.h>
#endif /* HAVE_SENDFILE && (__linux || __sun) */

static int confidence_iteration;

static  int local_cpu_method;
static  int remote_cpu_method;

/* these will control the width of port numbers we try to use in the */
/* TCP_CRR and/or TCP_TRR tests. raj 3/95 */
static int client_port_min = 5000;
static int client_port_max = 65535;

 /* different options for the sockets				*/

extern int
  loc_nodelay,		/* don't/do use NODELAY	locally		*/
  rem_nodelay,		/* don't/do use NODELAY remotely	*/
  loc_sndavoid,		/* avoid send copies locally		*/
  loc_rcvavoid,		/* avoid recv copies locally		*/
  rem_sndavoid,		/* avoid send copies remotely		*/
  rem_rcvavoid; 	/* avoid recv_copies remotely		*/

extern int
  loc_tcpcork,
  rem_tcpcork,
  local_connected,
  remote_connected;

enum netperf_output_type {
  NETPERF_TYPE_UNKNOWN,
  NETPERF_TYPE_UINT32,
  NETPERF_TYPE_INT32,
  NETPERF_TYPE_UINT64,
  NETPERF_TYPE_INT64,
  NETPERF_TYPE_CHAR,
  NETPERF_TYPE_FLOAT,
  NETPERF_TYPE_DOUBLE,
};

/* you should add to this in the order in which they should appear in
   the default csv (everything) output */

enum netperf_output_name {
  NETPERF_OUTPUT_UNKNOWN,
  OUTPUT_NONE,
  SOCKET_TYPE,
  PROTOCOL,
  DIRECTION,
  ELAPSED_TIME,
  THROUGHPUT,
  THROUGHPUT_UNITS,
  LSS_SIZE_REQ,
  LSS_SIZE,
  LSS_SIZE_END,
  LSR_SIZE_REQ,
  LSR_SIZE,
  LSR_SIZE_END,
  RSS_SIZE_REQ,
  RSS_SIZE,
  RSS_SIZE_END,
  RSR_SIZE_REQ,
  RSR_SIZE,
  RSR_SIZE_END,
  LOCAL_SEND_SIZE,
  LOCAL_RECV_SIZE,
  REMOTE_SEND_SIZE,
  REMOTE_RECV_SIZE,
  REQUEST_SIZE,
  RESPONSE_SIZE,
  LOCAL_CPU_UTIL,
  LOCAL_CPU_PERCENT_USER,
  LOCAL_CPU_PERCENT_SYSTEM,
  LOCAL_CPU_PERCENT_IOWAIT,
  LOCAL_CPU_PERCENT_IRQ,
  LOCAL_CPU_PERCENT_SWINTR,
  LOCAL_CPU_METHOD,
  LOCAL_SD,
  REMOTE_CPU_UTIL,
  REMOTE_CPU_PERCENT_USER,
  REMOTE_CPU_PERCENT_SYSTEM,
  REMOTE_CPU_PERCENT_IOWAIT,
  REMOTE_CPU_PERCENT_IRQ,
  REMOTE_CPU_PERCENT_SWINTR,
  REMOTE_CPU_METHOD,
  REMOTE_SD,
  SD_UNITS,
  CONFIDENCE_LEVEL,
  CONFIDENCE_INTERVAL,
  CONFIDENCE_ITERATION,
  THROUGHPUT_CONFID,
  LOCAL_CPU_CONFID,
  REMOTE_CPU_CONFID,
  TRANSACTION_RATE,
  RT_LATENCY,
  BURST_SIZE,
  LOCAL_TRANSPORT_RETRANS,
  REMOTE_TRANSPORT_RETRANS,
  TRANSPORT_MSS,
  LOCAL_SEND_THROUGHPUT,
  LOCAL_RECV_THROUGHPUT,
  REMOTE_SEND_THROUGHPUT,
  REMOTE_RECV_THROUGHPUT,
  LOCAL_CPU_BIND,
  LOCAL_CPU_COUNT,
  LOCAL_CPU_PEAK_UTIL,
  LOCAL_CPU_PEAK_ID,
  LOCAL_CPU_MODEL,
  LOCAL_CPU_FREQUENCY,
  REMOTE_CPU_BIND,
  REMOTE_CPU_COUNT,
  REMOTE_CPU_PEAK_UTIL,
  REMOTE_CPU_PEAK_ID,
  REMOTE_CPU_MODEL,
  REMOTE_CPU_FREQUENCY,
  SOURCE_PORT,
  SOURCE_ADDR,
  SOURCE_FAMILY,
  DEST_PORT,
  DEST_ADDR,
  DEST_FAMILY,
  LOCAL_SEND_CALLS,
  LOCAL_RECV_CALLS,
  LOCAL_BYTES_PER_RECV,
  LOCAL_BYTES_PER_SEND,
  LOCAL_BYTES_SENT,
  LOCAL_BYTES_RECVD,
  LOCAL_BYTES_XFERD,
  LOCAL_SEND_OFFSET,
  LOCAL_RECV_OFFSET,
  LOCAL_SEND_ALIGN,
  LOCAL_RECV_ALIGN,
  LOCAL_SEND_WIDTH,
  LOCAL_RECV_WIDTH,
  LOCAL_SEND_DIRTY_COUNT,
  LOCAL_RECV_DIRTY_COUNT,
  LOCAL_RECV_CLEAN_COUNT,
  LOCAL_NODELAY,
  LOCAL_CORK,
  REMOTE_SEND_CALLS,
  REMOTE_RECV_CALLS,
  REMOTE_BYTES_PER_RECV,
  REMOTE_BYTES_PER_SEND,
  REMOTE_BYTES_SENT,
  REMOTE_BYTES_RECVD,
  REMOTE_BYTES_XFERD,
  REMOTE_SEND_OFFSET,
  REMOTE_RECV_OFFSET,
  REMOTE_SEND_ALIGN,
  REMOTE_RECV_ALIGN,
  REMOTE_SEND_WIDTH,
  REMOTE_RECV_WIDTH,
  REMOTE_SEND_DIRTY_COUNT,
  REMOTE_RECV_DIRTY_COUNT,
  REMOTE_RECV_CLEAN_COUNT,
  REMOTE_NODELAY,
  REMOTE_CORK,
  LOCAL_SYSNAME,
  LOCAL_SYSTEM_MODEL,
  LOCAL_RELEASE,
  LOCAL_VERSION,
  LOCAL_MACHINE,
  REMOTE_SYSNAME,
  REMOTE_SYSTEM_MODEL,
  REMOTE_RELEASE,
  REMOTE_VERSION,
  REMOTE_MACHINE,
  LOCAL_INTERFACE_NAME,
  LOCAL_INTERFACE_VENDOR,
  LOCAL_INTERFACE_DEVICE,
  LOCAL_INTERFACE_SUBVENDOR,
  LOCAL_INTERFACE_SUBDEVICE,
  LOCAL_DRIVER_NAME,
  LOCAL_DRIVER_VERSION,
  LOCAL_DRIVER_FIRMWARE,
  LOCAL_DRIVER_BUS,
  LOCAL_INTERFACE_SLOT,
  REMOTE_INTERFACE_NAME,
  REMOTE_INTERFACE_VENDOR,
  REMOTE_INTERFACE_DEVICE,
  REMOTE_INTERFACE_SUBVENDOR,
  REMOTE_INTERFACE_SUBDEVICE,
  REMOTE_DRIVER_NAME,
  REMOTE_DRIVER_VERSION,
  REMOTE_DRIVER_FIRMWARE,
  REMOTE_DRIVER_BUS,
  REMOTE_INTERFACE_SLOT,
  LOCAL_INTERVAL_USECS,
  LOCAL_INTERVAL_BURST,
  REMOTE_INTERVAL_USECS,
  REMOTE_INTERVAL_BURST,
  LOCAL_SECURITY_TYPE_ID,
  LOCAL_SECURITY_TYPE,
  LOCAL_SECURITY_ENABLED_NUM,
  LOCAL_SECURITY_ENABLED,
  LOCAL_SECURITY_SPECIFIC,
  REMOTE_SECURITY_TYPE_ID,
  REMOTE_SECURITY_TYPE,
  REMOTE_SECURITY_ENABLED_NUM,
  REMOTE_SECURITY_ENABLED,
  REMOTE_SECURITY_SPECIFIC,
  RESULT_BRAND,
  UUID,
  MIN_LATENCY,
  MAX_LATENCY,
  P50_LATENCY,
  P90_LATENCY,
  P99_LATENCY,
  MEAN_LATENCY,
  STDDEV_LATENCY,
  LOCAL_SOCKET_PRIO,
  REMOTE_SOCKET_PRIO,
  LOCAL_SOCKET_TOS,
  REMOTE_SOCKET_TOS,
  LOCAL_CONG_CONTROL,
  REMOTE_CONG_CONTROL,
  LOCAL_FILL_FILE,
  REMOTE_FILL_FILE,
  COMMAND_LINE,    /* COMMAND_LINE should always be "last" */
  OUTPUT_END,
  NETPERF_OUTPUT_MAX
};

/* flags for the output groups, lower 16 bits for remote, upper 16
   bits for local */

#define OMNI_WANT_REM_IFNAME  0X00000001
#define OMNI_WANT_LOC_IFNAME  0X00010000
#define OMNI_WANT_REM_IFSLOT  0X00000002
#define OMNI_WANT_LOC_IFSLOT  0X00020000
#define OMNI_WANT_REM_IFIDS   0X00000004
#define OMNI_WANT_LOC_IFIDS   0X00040000
#define OMNI_WANT_REM_DRVINFO 0X00000008
#define OMNI_WANT_LOC_DRVINFO 0X00080000
#define OMNI_WANT_STATS       0X00100010
#define OMNI_WANT_REM_CONG    0X00000020
#define OMNI_WANT_LOC_CONG    0X00200000

unsigned int desired_output_groups = 0;

typedef struct netperf_output_elt {
  enum netperf_output_name output_name;  /* belt and suspenders */
  int max_line_len; /* length of the longest of the "lines" */
  int tot_line_len; /* total length of all lines, including spaces */
  char *line[4];
  char *format;        /* format to apply to value */
  void *display_value; /* where to find the value */
  enum netperf_output_type output_type; /* what type is the value? */
  int  output_default; /* is it included in the default output */
  unsigned int output_group; /* used to avoid some lookups */
} netperf_output_elt_t;

netperf_output_elt_t netperf_output_source[NETPERF_OUTPUT_MAX];

#define NETPERF_MAX_BLOCKS 4

/* let us simply use one, two-dimensional list, and either use or some
   of the additional dimension depending on the type of output we are
   doing.  this should help simplify matters. raj 20110120 */

enum netperf_output_name output_list[NETPERF_MAX_BLOCKS][NETPERF_OUTPUT_MAX];

/* some things for setting the source IP address on outgoing UDP
   sends. borrows liberally from
   http://stackoverflow.com/questions/3062205/setting-the-source-ip-for-a-udp-socket */

int want_use_pktinfo = 0;
int use_pktinfo = 0;
int have_pktinfo = 0;
#ifdef IP_PKTINFO
struct in_pktinfo in_pktinfo;

#ifdef WIN32
//NOTE: Linux's msghdr is very similar to Windows' WSAMSG, but the member names are different :(

//#define cmsghdr WSACMSGHDR
#define CMSG_FIRSTHDR WSA_CMSG_FIRSTHDR
#define CMSG_NXTHDR WSA_CMSG_NXTHDR
#define CMSG_DATA WSA_CMSG_DATA
#define CMSG_SPACE WSA_CMSG_SPACE
#define CMSG_LEN WSA_CMSG_LEN


// Note: the Winsock2 function WSARecvMsg is NOT defined directly in the MSFT headers; it must be determined via an IOCTL interface.
// Taken from MSFT example: https://github.com/theonlylawislove/WindowsSDK7-Samples/blob/master/netds/winsock/recvmsg/rmmc.cpp

/*
 * WSARecvMsg -- support for receiving ancilliary
 * data/control information with a message.
 */
typedef
INT
(PASCAL FAR * LPFN_WSARECVMSG) (
    __in SOCKET s,
    __inout LPWSAMSG lpMsg,
    __out_opt LPDWORD lpdwNumberOfBytesRecvd,
    __inout_opt LPWSAOVERLAPPED lpOverlapped,
    __in_opt LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine
    );

#define WSAID_WSARECVMSG \
    {0xf689d7c8,0x6f1f,0x436b,{0x8a,0x53,0xe5,0x4f,0xe3,0x51,0xc3,0x22}}

LPFN_WSARECVMSG pWSARecvMsg = NULL;

LPFN_WSARECVMSG GetWSARecvMsgFunctionPointer()
{
    LPFN_WSARECVMSG     lpfnWSARecvMsg = NULL;
    GUID                guidWSARecvMsg = WSAID_WSARECVMSG;
    SOCKET              sock = INVALID_SOCKET;
    DWORD               dwBytes = 0;
	int rc;

    sock = socket(AF_INET6,SOCK_DGRAM,0);

    rc= WSAIoctl(sock,
                 SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &guidWSARecvMsg,
                 sizeof(guidWSARecvMsg),
                 &lpfnWSARecvMsg,
                 sizeof(lpfnWSARecvMsg),
                 &dwBytes,
                 NULL,
                 NULL
                 );

    closesocket(sock);

	if (rc == SOCKET_ERROR) {
        //print an error message, such as "WSAIoctl SIO_GET_EXTENSION_FUNCTION_POINTER failed for WSARecvMsg"
		lpfnWSARecvMsg = NULL;  // Just to be sure...
    }


    return lpfnWSARecvMsg;
}

#endif /* WIN32 */
#endif /* IP_PKTINFO */

char *direction_to_str(int direction) {
  if (NETPERF_RECV_ONLY(direction)) return "Receive";
  if (NETPERF_XMIT_ONLY(direction)) return "Send";
  if (NETPERF_CC(direction)) return "Connection";
  else if (connection_test) {
    return "Connect|Send|Recv";
  }
  else return "Send|Recv";
}

static unsigned short
get_port_number(struct addrinfo *res)
{
  switch(res->ai_family) {
  case AF_INET: {
    struct sockaddr_in *foo = (struct sockaddr_in *)res->ai_addr;
    return(ntohs(foo->sin_port));
    break;
  }
#if defined(AF_INET6)
  case AF_INET6: {
    struct sockaddr_in6 *foo = (struct sockaddr_in6 *)res->ai_addr;
    return(ntohs(foo->sin6_port));
    break;
  }
#endif
  default:
    fprintf(where,
	    "Unexpected Address Family %u\n",res->ai_family);
    fflush(where);
    exit(-1);
  }
}

/* does this need to become conditional on the presence of the macros
   or might we ass-u-me that we will not be compiled on something so
   old as to not have what we use? raj 20090803 */
static int
is_multicast_addr(struct addrinfo *res) {
  switch(res->ai_family) {
  case AF_INET: {
    /* IPv4 multicast runs from 224.0.0.0 to 239.255.255.255 or
       0xE0000000 to 0xEFFFFFFF. Thankfully though there are macros
       available to make the checks for one */
    struct in_addr bar = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    /* and here I thought IN_MULTICAST would operate on things in
       network byte order???  raj 20100315 */
    return IN_MULTICAST(ntohl(bar.s_addr));
  }
#if defined(AF_INET6)
  case AF_INET6: {
    struct in6_addr *bar = &(((struct sockaddr_in6 *)res->ai_addr)->sin6_addr);
    return IN6_IS_ADDR_MULTICAST(bar);
  }
#endif
  default:
    fprintf(where,
	    "Unexpected Address Family for Multicast Check %u\n",
	    res->ai_family);
    fflush(where);
    return 0;  /* or should we exit? */
  }
}

static void
set_multicast_ttl(SOCKET sock) {
  int optlen = sizeof(int);

  /* now set/get the TTL */
  if (multicast_ttl >= 0) {
    if (setsockopt(sock,
		   IPPROTO_IP,
#if defined(IP_MULTICAST_TTL)
		   IP_MULTICAST_TTL,
#else
		   IP_TTL,
#endif
		   (const char *)&multicast_ttl,
		   sizeof(multicast_ttl)) == SOCKET_ERROR) {
      fprintf(where,
	      "setsockopt(IP_TTL) failed errno %d\n",
	      errno);
    }
  }
  if (getsockopt(sock,
		 IPPROTO_IP,
		 IP_TTL,
		 (char *)&multicast_ttl,
		 (netperf_socklen_t *)&optlen) < 0) {
    fprintf(where,
	    "getsockopt(IP_TTL) failed errno %d\n",
	    errno);
    multicast_ttl = -2;
  }
}

/* we presume we are only called with something which is actually a
   multicast address. raj 20100315 */
static void
join_multicast_addr(SOCKET sock, struct addrinfo *res) {
  switch(res->ai_family) {
  case AF_INET: {
    struct ip_mreq mreq;
    struct in_addr bar = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    int optlen = sizeof(int);
    int one    = 1;

    mreq.imr_multiaddr.s_addr=bar.s_addr;
    mreq.imr_interface.s_addr=htonl(INADDR_ANY);
    if (setsockopt(sock,
		   IPPROTO_IP,
		   IP_ADD_MEMBERSHIP,
		   (const char *)&mreq,
		   sizeof(mreq)) == 0) {

      /* let others do the same */
      if (setsockopt(sock,
		     SOL_SOCKET,
		     SO_REUSEADDR,
		     (const char *)&one,
		     sizeof(one)) == SOCKET_ERROR) {
	if (debug) {
	  fprintf(where,
		  "join_multicast_addr SO_REUSADDR failed errno %d\n",
		  errno);
	  fflush(where);
	}
      }

      /* now set/get the TTL */
      if (multicast_ttl >= 0) {
	if (setsockopt(sock,
		       IPPROTO_IP,
		       IP_TTL,
		       (const char *)&multicast_ttl,
		       sizeof(multicast_ttl)) == SOCKET_ERROR) {
	  fprintf(where,
		  "setsockopt(IP_TTL) failed errno %d\n",
		  errno);
	}
      }
      if (getsockopt(sock,
		     IPPROTO_IP,
		     IP_TTL,
		     (char *)&multicast_ttl,
		     (netperf_socklen_t *)&optlen) == SOCKET_ERROR) {
	fprintf(where,
		"getsockopt(IP_TTL) failed errno %d\n",
		errno);
	multicast_ttl = -2;
      }
    }
    else {
      if (debug) {
	fprintf(where,
		"setsockopt(IP_ADD_MEMBERSHIP) failed errno %d\n",
		errno);
	fflush(where);
      }
    }
    break;
  }
  case AF_INET6: {
    fprintf(where,"I do not know how to join an IPv6 multicast group\n");
    break;
  }

  }
  return;
}

static void
extract_inet_address_and_port(struct addrinfo *res, void *addr, int len, int *port)
{
 switch(res->ai_family) {
  case AF_INET: {
    struct sockaddr_in *foo = (struct sockaddr_in *)res->ai_addr;
    *port = foo->sin_port;
    memcpy(addr,&(foo->sin_addr),min(len,sizeof(foo->sin_addr)));
    break;
  }
#if defined(AF_INET6)
  case AF_INET6: {
    struct sockaddr_in6 *foo = (struct sockaddr_in6 *)res->ai_addr;
    *port = foo->sin6_port;
    memcpy(addr,&(foo->sin6_addr),min(len,sizeof(foo->sin6_addr)));
    break;
  }
#endif
  default:
    *port = 0xDEADBEEF;
    strncpy(addr,"UNKN FAMILY",len);
  }
}

void
pick_next_port_number(struct addrinfo *local_res, struct addrinfo *remote_res) {

  static int myport_init = 0;
  static unsigned short myport = 0;

  if (0 == myport_init)  {
    /* pick a nice random spot between client_port_min and
       client_port_max for our initial port number, but only for a
       connection oriented test. otherwise, we will want to set myport
       to a specific port provided by the user if they have so provided
       a specific port :)  raj 2008-01-08 */
    srand(getpid());
    if (client_port_max - client_port_min) {
      myport = client_port_min +
	(rand() % (client_port_max - client_port_min));
    }
    else {
      myport = (unsigned short)client_port_min;
    }
    /* there will be a ++ before the first call to bind, so subtract one */
    myport--;
    myport_init = 1;
  }

  /* newport: */
  /* pick a new port number */
  myport++;

  /* check to see if we are using the port number on which the
     server is sitting _before_ we check against the boundaries lest
     the server sits at the upper boundary. if this happens to be a
     loopback test, trying to use the same portnumber would lead to
     unsatisfying results and should be avoided.  if this isn't a
     loopback test, avoiding using the same port number doesn't
     seriously affect anything anyway */

  if (myport == get_port_number(remote_res)) myport++;

  /* wrap the port number when we reach the upper bound.  for
     students of networking history, some ancient stacks (1980's and
     early 1990's perhaps) mistakenly treated these port numbers as
     signed 16 bit quantities.  we make no effort here to support
     such stacks. raj 2008-01-08 */
  if (myport >= client_port_max) {
    myport = (unsigned short)client_port_min;
  }

  /* set up the data socket */
  set_port_number(local_res, (unsigned short)myport);
}

/* at some point this should become a table lookup... raj 20090813 */
char *
netperf_output_enum_to_str(enum netperf_output_name output_name)
{
  switch (output_name) {
  case OUTPUT_NONE:
    return "OUTPUT_NONE";
  case   COMMAND_LINE:
    return "COMMAND_LINE";
  case UUID:
    return "UUID";
  case RESULT_BRAND:
    return "RESULT_BRAND";
  case   SOCKET_TYPE:
    return "SOCKET_TYPE";
  case   DIRECTION:
    return "DIRECTION";
  case   PROTOCOL:
    return "PROTOCOL";
  case   ELAPSED_TIME:
    return "ELAPSED_TIME";
  case   SOURCE_PORT:
    return "SOURCE_PORT";
  case   SOURCE_ADDR:
    return "SOURCE_ADDR";
  case SOURCE_FAMILY:
    return "SOURCE_FAMILY";
  case   DEST_PORT:
    return "DEST_PORT";
  case   DEST_ADDR:
    return "DEST_ADDR";
  case DEST_FAMILY:
    return "DEST_FAMILY";
  case THROUGHPUT:
    return "THROUGHPUT";
  case LOCAL_SEND_THROUGHPUT:
    return "LOCAL_SEND_THROUGHPUT";
  case LOCAL_RECV_THROUGHPUT:
    return "LOCAL_RECV_THROUGHPUT";
  case REMOTE_SEND_THROUGHPUT:
    return "REMOTE_SEND_THROUGHPUT";
  case REMOTE_RECV_THROUGHPUT:
    return "REMOTE_RECV_THROUGHPUT";
  case THROUGHPUT_UNITS:
    return "THROUGHPUT_UNITS";
  case CONFIDENCE_LEVEL:
    return "CONFIDENCE_LEVEL";
  case CONFIDENCE_INTERVAL:
    return "CONFIDENCE_INTERVAL";
  case CONFIDENCE_ITERATION:
    return "CONFIDENCE_ITERATION";
  case THROUGHPUT_CONFID:
    return "THROUGHPUT_CONFID";
  case LOCAL_CPU_CONFID:
    return "LOCAL_CPU_CONFID";
  case REMOTE_CPU_CONFID:
    return "REMOTE_CPU_CONFID";
  case RT_LATENCY:
    return "RT_LATENCY";
  case TRANSACTION_RATE:
    return "TRANSACTION_RATE";
  case BURST_SIZE:
    return "BURST_SIZE";
  case LOCAL_TRANSPORT_RETRANS:
    return "LOCAL_TRANSPORT_RETRANS";
  case REMOTE_TRANSPORT_RETRANS:
    return "REMOTE_TRANSPORT_RETRANS";
  case TRANSPORT_MSS:
    return "TRANSPORT_MSS";
  case REQUEST_SIZE:
    return "REQUEST_SIZE";
  case RESPONSE_SIZE:
    return "RESPONSE_SIZE";
  case   LSS_SIZE_REQ:
    return "LSS_SIZE_REQ";
  case   LSS_SIZE:
    return "LSS_SIZE";
  case   LSS_SIZE_END:
    return "LSS_SIZE_END";
  case   LSR_SIZE_REQ:
    return "LSR_SIZE_REQ";
  case   LSR_SIZE:
    return "LSR_SIZE";
  case   LSR_SIZE_END:
    return "LSR_SIZE_END";
  case   LOCAL_SEND_SIZE:
    return "LOCAL_SEND_SIZE";
  case   LOCAL_RECV_SIZE:
    return "LOCAL_RECV_SIZE";
  case   LOCAL_SEND_CALLS:
    return "LOCAL_SEND_CALLS";
  case   LOCAL_RECV_CALLS:
    return "LOCAL_RECV_CALLS";
  case   LOCAL_BYTES_PER_RECV:
    return "LOCAL_BYTES_PER_RECV";
  case   LOCAL_BYTES_PER_SEND:
    return "LOCAL_BYTES_PER_SEND";
  case   LOCAL_BYTES_SENT:
    return "LOCAL_BYTES_SENT";
  case   LOCAL_BYTES_RECVD:
    return "LOCAL_BYTES_RECVD";
  case   LOCAL_BYTES_XFERD:
    return "LOCAL_BYTES_XFERD";
  case LOCAL_SEND_OFFSET:
    return "LOCAL_SEND_OFFSET";
  case LOCAL_RECV_OFFSET:
    return "LOCAL_RECV_OFFSET";
  case LOCAL_RECV_ALIGN:
    return "LOCAL_RECV_ALIGN";
  case LOCAL_SEND_ALIGN:
    return "LOCAL_SEND_ALIGN";
  case LOCAL_SEND_WIDTH:
    return "LOCAL_SEND_WIDTH";
  case LOCAL_RECV_WIDTH:
    return "LOCAL_RECV_WIDTH";
  case   LOCAL_SEND_DIRTY_COUNT:
    return "LOCAL_SEND_DIRTY_COUNT";
  case   LOCAL_RECV_DIRTY_COUNT:
    return "LOCAL_RECV_DIRTY_COUNT";
  case   LOCAL_RECV_CLEAN_COUNT:
    return "LOCAL_RECV_CLEAN_COUNT";
  case   LOCAL_CPU_UTIL:
    return "LOCAL_CPU_UTIL";
  case   LOCAL_CPU_PERCENT_USER:
    return "LOCAL_CPU_PERCENT_USER";
  case   LOCAL_CPU_PERCENT_SYSTEM:
    return "LOCAL_CPU_PERCENT_SYSTEM";
  case   LOCAL_CPU_PERCENT_IOWAIT:
    return "LOCAL_CPU_PERCENT_IOWAIT";
  case   LOCAL_CPU_PERCENT_IRQ:
    return "LOCAL_CPU_PERCENT_IRQ";
  case   LOCAL_CPU_PERCENT_SWINTR:
    return "LOCAL_CPU_PERCENT_SWINTR";
  case   LOCAL_CPU_BIND:
    return "LOCAL_CPU_BIND";
  case   LOCAL_SD:
    return "LOCAL_SD";
  case   SD_UNITS:
    return "SD_UNITS";
  case   LOCAL_CPU_METHOD:
    return "LOCAL_CPU_METHOD";
  case LOCAL_CPU_COUNT:
    return "LOCAL_CPU_COUNT";
  case   LOCAL_CPU_PEAK_UTIL:
    return "LOCAL_CPU_PEAK_UTIL";
  case LOCAL_CPU_PEAK_ID:
    return "LOCAL_CPU_PEAK_ID";
  case   LOCAL_NODELAY:
    return "LOCAL_NODELAY";
  case   LOCAL_CORK:
    return "LOCAL_CORK";
  case   RSS_SIZE_REQ:
    return "RSS_SIZE_REQ";
  case   RSS_SIZE:
    return "RSS_SIZE";
  case   RSS_SIZE_END:
    return "RSS_SIZE_END";
  case   RSR_SIZE_REQ:
    return "RSR_SIZE_REQ";
  case   RSR_SIZE:
    return "RSR_SIZE";
  case   RSR_SIZE_END:
    return "RSR_SIZE_END";
  case   REMOTE_SEND_SIZE:
    return "REMOTE_SEND_SIZE";
  case   REMOTE_RECV_SIZE:
    return "REMOTE_RECV_SIZE";
  case   REMOTE_SEND_CALLS:
    return "REMOTE_SEND_CALLS";
  case   REMOTE_RECV_CALLS:
    return "REMOTE_RECV_CALLS";
  case   REMOTE_BYTES_PER_RECV:
    return "REMOTE_BYTES_PER_RECV";
  case   REMOTE_BYTES_PER_SEND:
    return "REMOTE_BYTES_PER_SEND";
  case   REMOTE_BYTES_SENT:
    return "REMOTE_BYTES_SENT";
  case   REMOTE_BYTES_RECVD:
    return "REMOTE_BYTES_RECVD";
  case   REMOTE_BYTES_XFERD:
    return "REMOTE_BYTES_XFERD";
  case REMOTE_SEND_OFFSET:
    return "REMOTE_SEND_OFFSET";
  case REMOTE_RECV_OFFSET:
    return "REMOTE_RECV_OFFSET";
  case REMOTE_RECV_ALIGN:
    return "REMOTE_RECV_ALIGN";
  case REMOTE_SEND_ALIGN:
    return "REMOTE_SEND_ALIGN";
  case REMOTE_SEND_WIDTH:
    return "REMOTE_SEND_WIDTH";
  case REMOTE_RECV_WIDTH:
    return "REMOTE_RECV_WIDTH";
  case   REMOTE_SEND_DIRTY_COUNT:
    return "REMOTE_SEND_DIRTY_COUNT";
  case   REMOTE_RECV_DIRTY_COUNT:
    return "REMOTE_RECV_DIRTY_COUNT";
  case   REMOTE_RECV_CLEAN_COUNT:
    return "REMOTE_RECV_CLEAN_COUNT";
  case   REMOTE_CPU_UTIL:
    return "REMOTE_CPU_UTIL";
  case   REMOTE_CPU_PERCENT_USER:
    return "REMOTE_CPU_PERCENT_USER";
  case   REMOTE_CPU_PERCENT_SYSTEM:
    return "REMOTE_CPU_PERCENT_SYSTEM";
  case   REMOTE_CPU_PERCENT_IOWAIT:
    return "REMOTE_CPU_PERCENT_IOWAIT";
  case   REMOTE_CPU_PERCENT_IRQ:
    return "REMOTE_CPU_PERCENT_IRQ";
  case   REMOTE_CPU_PERCENT_SWINTR:
    return "REMOTE_CPU_PERCENT_SWINTR";
  case   REMOTE_CPU_BIND:
    return "REMOTE_CPU_BIND";
  case   REMOTE_SD:
    return "REMOTE_SD";
  case   REMOTE_CPU_METHOD:
    return "REMOTE_CPU_METHOD";
  case REMOTE_CPU_COUNT:
    return "REMOTE_CPU_COUNT";
  case REMOTE_CPU_PEAK_UTIL:
    return "REMOTE_CPU_PEAK_UTIL";
  case REMOTE_CPU_PEAK_ID:
    return "REMOTE_CPU_PEAK_ID";
  case   REMOTE_NODELAY:
    return "REMOTE_NODELAY";
  case   REMOTE_CORK:
    return "REMOTE_CORK";
  case LOCAL_INTERFACE_SLOT:
    return "LOCAL_INTERFACE_SLOT";
  case REMOTE_INTERFACE_SLOT:
    return "REMOTE_INTERFACE_SLOT";
  case REMOTE_INTERFACE_SUBDEVICE:
    return "REMOTE_INTERFACE_SUBDEVICE";
  case REMOTE_INTERFACE_SUBVENDOR:
    return "REMOTE_INTERFACE_SUBVENDOR";
  case REMOTE_INTERFACE_DEVICE:
    return "REMOTE_INTERFACE_DEVICE";
  case REMOTE_INTERFACE_VENDOR:
    return "REMOTE_INTERFACE_VENDOR";
  case LOCAL_INTERFACE_SUBDEVICE:
    return "LOCAL_INTERFACE_SUBDEVICE";
  case LOCAL_INTERFACE_SUBVENDOR:
    return "LOCAL_INTERFACE_SUBVENDOR";
  case LOCAL_INTERFACE_DEVICE:
    return "LOCAL_INTERFACE_DEVICE";
  case LOCAL_INTERFACE_VENDOR:
    return "LOCAL_INTERFACE_VENDOR";
  case LOCAL_INTERFACE_NAME:
    return "LOCAL_INTERFACE_NAME";
  case REMOTE_INTERFACE_NAME:
    return "REMOTE_INTERFACE_NAME";
  case REMOTE_DRIVER_NAME:
    return "REMOTE_DRIVER_NAME";
  case REMOTE_DRIVER_VERSION:
    return "REMOTE_DRIVER_VERSION";
  case REMOTE_DRIVER_FIRMWARE:
    return "REMOTE_DRIVER_FIRMWARE";
  case REMOTE_DRIVER_BUS:
    return "REMOTE_DRIVER_BUS";
  case LOCAL_DRIVER_NAME:
    return "LOCAL_DRIVER_NAME";
  case LOCAL_DRIVER_VERSION:
    return "LOCAL_DRIVER_VERSION";
  case LOCAL_DRIVER_FIRMWARE:
    return "LOCAL_DRIVER_FIRMWARE";
  case LOCAL_INTERVAL_USECS:
    return "LOCAL_INTERVAL_USECS";
  case LOCAL_INTERVAL_BURST:
    return "LOCAL_INTERVAL_BURST";
  case REMOTE_INTERVAL_USECS:
    return "REMOTE_INTERVAL_USECS";
  case REMOTE_INTERVAL_BURST:
    return "REMOTE_INTERVAL_BURST";
  case LOCAL_SECURITY_TYPE_ID:
    return "LOCAL_SECURITY_TYPE_ID";
  case LOCAL_SECURITY_ENABLED_NUM:
    return "LOCAL_SECURITY_ENABLED_NUM";
  case LOCAL_SECURITY_TYPE:
    return "LOCAL_SECURITY_TYPE";
  case LOCAL_SECURITY_ENABLED:
    return "LOCAL_SECURITY_ENABLED";
  case LOCAL_SECURITY_SPECIFIC:
    return "LOCAL_SECURITY_SPECIFIC";
  case REMOTE_SECURITY_TYPE_ID:
    return "REMOTE_SECURITY_TYPE_ID";
  case REMOTE_SECURITY_ENABLED_NUM:
    return "REMOTE_SECURITY_ENABLED_NUM";
  case REMOTE_SECURITY_TYPE:
    return "REMOTE_SECURITY_TYPE";
  case REMOTE_SECURITY_ENABLED:
    return "REMOTE_SECURITY_ENABLED";
  case REMOTE_SECURITY_SPECIFIC:
    return "REMOTE_SECURITY_SPECIFIC";
  case LOCAL_DRIVER_BUS:
    return "LOCAL_DRIVER_BUS";
  case REMOTE_SYSNAME:
    return "REMOTE_SYSNAME";
  case REMOTE_MACHINE:
    return "REMOTE_MACHINE";
  case REMOTE_VERSION:
    return "REMOTE_VERSION";
  case REMOTE_RELEASE:
    return "REMOTE_RELEASE";
  case LOCAL_SYSNAME:
    return "LOCAL_SYSNAME";
  case LOCAL_MACHINE:
    return "LOCAL_MACHINE";
  case LOCAL_VERSION:
    return "LOCAL_VERSION";
  case LOCAL_RELEASE:
    return "LOCAL_RELEASE";
  case REMOTE_CPU_MODEL:
    return "REMOTE_CPU_MODEL";
  case REMOTE_CPU_FREQUENCY:
    return "REMOTE_CPU_FREQUENCY";
  case REMOTE_SYSTEM_MODEL:
    return "REMOTE_SYSTEM_MODEL";
  case LOCAL_CPU_MODEL:
    return "LOCAL_CPU_MODEL";
  case LOCAL_CPU_FREQUENCY:
    return "LOCAL_CPU_FREQUENCY";
  case LOCAL_SYSTEM_MODEL:
    return "LOCAL_SYSTEM_MODEL";
  case MIN_LATENCY:
    return "MIN_LATENCY";
  case MAX_LATENCY:
    return "MAX_LATENCY";
  case P50_LATENCY:
    return "P50_LATENCY";
  case P90_LATENCY:
    return "P90_LATENCY";
  case P99_LATENCY:
    return "P99_LATENCY";
  case MEAN_LATENCY:
    return "MEAN_LATENCY";
  case STDDEV_LATENCY:
    return "STDDEV_LATENCY";
  case LOCAL_SOCKET_PRIO:
    return "LOCAL_SOCKET_PRIO";
  case REMOTE_SOCKET_PRIO:
    return "REMOTE_SOCKET_PRIO";
  case LOCAL_SOCKET_TOS:
    return "LOCAL_SOCKET_TOS";
  case REMOTE_SOCKET_TOS:
    return "REMOTE_SOCKET_TOS";
  case LOCAL_CONG_CONTROL:
    return "LOCAL_CONG_CONTROL";
  case REMOTE_CONG_CONTROL:
    return "REMOTE_CONG_CONTROL";
  case LOCAL_FILL_FILE:
    return "LOCAL_FILL_FILE";
  case REMOTE_FILL_FILE:
    return "REMOTE_FILL_FILE";
  case OUTPUT_END:
    return "OUTPUT_END";
  default:
    return "!UNKNOWN OUTPUT SELECTOR!";
  }
}

void
print_netperf_output_entry(FILE *where, enum netperf_output_name what)
{
}

void print_omni_init_list();

void
dump_netperf_output_list(FILE *where) {

  int i,j;

  for (i = 0; i < NETPERF_MAX_BLOCKS; i++) {
    fprintf(where,"Output Block %d\n",i + 1);
    for (j = 0; j < NETPERF_OUTPUT_MAX; j++) {
      fprintf(where,"%s ",netperf_output_enum_to_str(output_list[i][j]));
    }
    fprintf(where,"\n");
  }
  fflush(where);
}

void
dump_netperf_output_choices(FILE *where, int csv) {
  int i;

  print_omni_init_list();

  for (i = OUTPUT_NONE; i < NETPERF_OUTPUT_MAX; i++){
    if (OUTPUT_NONE != i) {
      fprintf(where,"%c",(csv) ? ',' : '\n');
    }
    fprintf(where,
	    "%s",
	    netperf_output_enum_to_str(netperf_output_source[i].output_name));
  }
  fprintf(where,"\n");
  fflush(where);
}

void
dump_netperf_output_source(FILE *where)
{
  int i;

  /* belts and suspenders everyone... */
  for (i = OUTPUT_NONE; i < NETPERF_OUTPUT_MAX; i++) {
    fprintf(where,
	    "Output Name: %s\n"
	    "\tmax_line_len %d tot_line_len %d display_value %p\n"
	    "\tline[0]: |%s|\n"
	    "\tline[1]: |%s|\n"
	    "\tline[2]: |%s|\n"
	    "\tline[3]: |%s|\n"
	    "\tformat: |%s|\n",
	    netperf_output_enum_to_str(netperf_output_source[i].output_name),
	    netperf_output_source[i].max_line_len,
	    netperf_output_source[i].tot_line_len,
	    netperf_output_source[i].display_value,
	    (netperf_output_source[i].line[0] == NULL) ? "" :
	    netperf_output_source[i].line[0],
	    (netperf_output_source[i].line[1] == NULL) ? "" :
	    netperf_output_source[i].line[1],
	    (netperf_output_source[i].line[2] == NULL) ? "" :
	    netperf_output_source[i].line[2],
	    (netperf_output_source[i].line[3] == NULL) ? "" :
	    netperf_output_source[i].line[3],
	    (netperf_output_source[i].format == NULL) ? "" :
	    netperf_output_source[i].format);
  }
  fflush(where);
}

#define MY_MAX(a,b) ((a > b) ? a : b)

#define NETPERF_LINE_MAX(x) \
    MY_MAX(MY_MAX(MY_MAX(strlen(netperf_output_source[x].line[0]),\
		         strlen(netperf_output_source[x].line[1])),\
	          strlen(netperf_output_source[x].line[2])),\
	   strlen(netperf_output_source[x].line[3]))

#define NETPERF_LINE_TOT(x) \
    strlen(netperf_output_source[x].line[0]) +\
    strlen(netperf_output_source[x].line[1]) +\
    strlen(netperf_output_source[x].line[2]) +\
    strlen(netperf_output_source[x].line[3]) + 4

enum netperf_output_name
match_string_to_output_mnenomic(char *candidate) {

  enum netperf_output_name name;
  for (name = OUTPUT_NONE; name < NETPERF_OUTPUT_MAX; name++) {
    if(!strcasecmp(candidate,netperf_output_enum_to_str(name)))
      return name;
  }
  return NETPERF_OUTPUT_UNKNOWN;
}

enum netperf_output_name
match_string_to_output(char *candidate)
{
  char *h1,*temp;
  enum netperf_output_name name;
  int k,len;

  /* at some point we may need/want to worry about leading and
     trailing spaces, but for now we will leave that onus on the
     user. */

  for (name = OUTPUT_NONE; name < NETPERF_OUTPUT_MAX; name++) {
    /* try for a match based on the nmemonic/enum */
    if (!strcasecmp(candidate,netperf_output_enum_to_str(name)))
      return name;

    /* try for a match on the actual header text */
    temp = malloc(NETPERF_LINE_TOT(name));
    h1 = temp;
    if (h1 != NULL) {
      for (k = 0; ((k < 4) &&
		   (NULL != netperf_output_source[name].line[k]) &&
		   (strcmp("",netperf_output_source[name].line[k]))); k++) {
	len = sprintf(h1,
		      "%s",
		      netperf_output_source[name].line[k]);
	*(h1 + len) = ' ';
	/* now move to the next starting column. for csv we aren't worried
	   about alignment between the header and the value lines */
	h1 += len + 1;
      }
      /* this time we want null termination please */
      *(h1 - 1) = 0;
      if (!strcasecmp(candidate,temp)) {
	free(temp);
	return name;
      }
      else
	free(temp);
    }
  }
  /* if we get here it means there was no match */
  return OUTPUT_NONE;
}


void
set_output_list_all() {

  int i, j;  /* line, column */
  enum netperf_output_name k;

  /* Line One SOCKET_TYPE to RESPONSE_SIZE */
  i = 0;
  j = 0;
  for (k = SOCKET_TYPE; k <= RESPONSE_SIZE; k++) {
    output_list[i][j++] = k;
    desired_output_groups |= netperf_output_source[k].output_group;
  }

  /* Line Two LOCAL_CPU_UTIL to TRANSPORT_MSS */
  i = 1;
  j = 0;
  for (k = LOCAL_CPU_UTIL; k <= TRANSPORT_MSS; k++) {
    output_list[i][j++] = k;
    desired_output_groups |= netperf_output_source[k].output_group;
  }

  /* Line Three LOCAL_SEND_THROUGHPUT throught REMOTE_CORK */
  i = 2;
  j = 0;
  for (k = LOCAL_SEND_THROUGHPUT; k <= REMOTE_CORK; k++) {
    output_list[i][j++] = k;
    desired_output_groups |= netperf_output_source[k].output_group;
  }

  /* Line Four LOCAL_SYSNAME through COMMAND_LINE */
  i = 3;
  j = 0;
  for (k = LOCAL_SYSNAME; k <= COMMAND_LINE; k++) {
    output_list[i][j++] = k;
    desired_output_groups |= netperf_output_source[k].output_group;
  }

}

void
parse_output_selection_file(char *selection_file) {
  FILE *selections;
  char name[81]; /* best be more than enough */
  int namepos;
  int c;
  int j;
  int line,column;

  selections = fopen(selection_file,"r");
  if (!selections) {
    fprintf(where,
	    "Could not open output selection file '%s' errno %d\n",
	    selection_file,
	    errno);
    fflush(where);
    exit(-1);
  }

  line = 0;
  column = 1;
  namepos = 0;
  name[0] = 0;
  name[80] = 0;
  j = 0;
  while (((c = fgetc(selections)) != EOF) && (line < 4)) {
    if (namepos == 80) {
      /* too long */

      fprintf(where,
	      "Output selection starting column %d on line %d is too long\n",
	      line + 1,
	      column);
      fflush(where);
      exit(-1);
    }
    if (c == ',') {
      /* time to check for a match, but only if we won't overflow the
	 current row of the array  */
      if (j == NETPERF_OUTPUT_MAX) {
	fprintf(where,"Too many output selectors on line %d\n",line);
	fflush(where);
	exit(-1);
      }
      name[namepos] = 0;
      output_list[line][j++] = match_string_to_output(name);
      namepos = 0;
    }
    else if (c == '\n') {
      /* move to the next line after checking for a match */
      name[namepos] = 0;
      output_list[line++][j++] = match_string_to_output(name);
      namepos = 0;
      j = 0;
    }
    else if (isprint(c)) {
      name[namepos++] = (char)c;
    }
    column++;
  }

  /* ok, do we need/want to do anything here? at present we will
     silently ignore the rest of the file if we exit the loop on line
     count */
  if ((c == EOF) && (namepos > 0)) {
    name[namepos] = 0;
    output_list[line][j] =   match_string_to_output(name);
  }

}

void
parse_output_selection_line(int line, char *list) {

  char *token;
  int j;
  enum netperf_output_name name;

  /* belt and suspenders */
  if (line < 0) {
    fprintf(where,
	    "parse_output_selection_line called with negative line number %d\n",line);
    fflush(where);
    exit(-1);
  }

  /* silently ignore extra lines and only warn if debug is set */
  if (line >= NETPERF_MAX_BLOCKS) {
    if (debug) {
      fprintf(where,
	      "There can be no more than %d output selection lines."
	      " Ignoring output selection line %d |%s|\n",
	      NETPERF_MAX_BLOCKS,
	      line + 1,
	      list);
      fflush(where);
    }
    return;
  }


  j=0;
  token = strtok(list," ,");
  while ((token) && (j < NETPERF_OUTPUT_MAX)) {

    name = match_string_to_output_mnenomic(token);

    if ((name == NETPERF_OUTPUT_UNKNOWN) && (debug)) {
      fprintf(where,"Ignoring unknown output selector %d |%s| on line %d\n",
	      j + 1,
	      token,
	      line +1);
      fflush(where);
    }
    else {
      output_list[line][j] = name;
      desired_output_groups |= netperf_output_source[name].output_group;
      j++;
    }

    token = strtok(NULL," ,");
  }
  if ((token) && (debug)) {
    fprintf(where,
	    "There can be no more than %d output selectors per line. "
	    "Ignoring remaining selectors on line %d\n",
	    NETPERF_OUTPUT_MAX,line +1);
    fflush(where);
  }
}

void
parse_output_selection_direct(char *output_selection) {

  char *source,*line,*remainder,*temp;
  char *f1, *f2, *f3;
  int i,len,done;

  len = strlen(output_selection);

  source = strdup(output_selection);
  line = (char *) malloc(len+1);
  remainder = (char *) malloc(len+1);

  if ((NULL == source) ||
      (NULL == line) ||
      (NULL == remainder)) {
    fprintf(where,"Unable to malloc memory for output selection parsing\n");
    fflush(where);
    exit(-1);
  }

  f1 = source;
  f2 = line;
  f3 = remainder;

  i = 0;
  done = 0;
  do {
    break_args_explicit_sep(source,';',line,remainder);
    if (line[0]) {
      parse_output_selection_line(i,line);
    }
    if (remainder[0]) {
      temp = source;
      source = remainder;
      remainder = temp;
      i++;
      /*
      if (i == NETPERF_MAX_BLOCKS) {
	fprintf(where,
		"Too many output blocks requested, maximum is %d\n",
		NETPERF_MAX_BLOCKS);
	fflush(where);
	exit(-1);
      }
      */
      continue;
    }
    else {
      done = 1;
    }
  } while (!done);

  free(f1);
  free(f2);
  free(f3);

}

/* building blocks for output selection */
#define NETPERF_TPUT "ELAPSED_TIME,THROUGHPUT,THROUGHPUT_UNITS"
#define NETPERF_OUTPUT_STREAM "LSS_SIZE_END,RSR_SIZE_END,LOCAL_SEND_SIZE"
#define NETPERF_OUTPUT_MAERTS "RSS_SIZE_END,LSR_SIZE_END,REMOTE_SEND_SIZE"
#define NETPERF_CPU "LOCAL_CPU_UTIL,LOCAL_CPU_PERCENT_USER,LOCAL_CPU_PERCENT_SYSTEM,LOCAL_CPU_PERCENT_IOWAIT,LOCAL_CPU_PERCENT_IRQ,LOCAL_CPU_PERCENT_SWINTR,LOCAL_CPU_METHOD,REMOTE_CPU_UTIL,REMOTE_CPU_PERCENT_USER,REMOTE_CPU_PERCENT_SYSTEM,REMOTE_CPU_PERCENT_IOWAIT,REMOTE_CPU_PERCENT_IRQ,REMOTE_CPU_PERCENT_SWINTR,REMOTE_CPU_METHOD,LOCAL_SD,REMOTE_SD,SD_UNITS"
#define NETPERF_RR "LSS_SIZE_END,LSR_SIZE_END,RSR_SIZE_END,RSS_SIZE_END,REQUEST_SIZE,RESPONSE_SIZE"

void
set_output_list_by_test() {

  char *stream_no_cpu = NETPERF_OUTPUT_STREAM "," NETPERF_TPUT;
  char *stream_cpu = NETPERF_OUTPUT_STREAM "," NETPERF_TPUT "," NETPERF_CPU;
  char *maerts_no_cpu = NETPERF_OUTPUT_MAERTS "," NETPERF_TPUT;
  char *maerts_cpu =  NETPERF_OUTPUT_MAERTS "," NETPERF_TPUT "," NETPERF_CPU;
  char *rr_no_cpu = NETPERF_RR "," NETPERF_TPUT;
  char *rr_cpu = NETPERF_RR "," NETPERF_TPUT "," NETPERF_CPU;

  if (debug) {
    fprintf(where,"%s setting the output list by test\n",
	    __FUNCTION__);
    fflush(where);
  }

  if (NETPERF_XMIT_ONLY(direction)) {
    if (!(local_cpu_usage || remote_cpu_usage))
      parse_output_selection_direct(stream_no_cpu);
    else
      parse_output_selection_direct(stream_cpu);
  }
  else if (NETPERF_RECV_ONLY(direction)) {
    if (!(local_cpu_usage || remote_cpu_usage))
      parse_output_selection_direct(maerts_no_cpu);
    else
      parse_output_selection_direct(maerts_cpu);
  }
  else if (NETPERF_CC(direction)) {
    if (!(local_cpu_usage || remote_cpu_usage))
      parse_output_selection_direct(rr_no_cpu);
    else
      parse_output_selection_direct(rr_cpu);
  }
  else if (NETPERF_IS_RR(direction)) {
    if (!(local_cpu_usage || remote_cpu_usage))
      parse_output_selection_direct(rr_no_cpu);
    else
      parse_output_selection_direct(rr_cpu);
  }
  else {
    /* no idea */
    if (debug) {
      fprintf(where,"Cannot determine default test output, using mins\n");
      fflush(where);
    }
    parse_output_selection_direct(NETPERF_TPUT "," NETPERF_CPU);
  }
}

void
parse_output_selection(char *output_selection) {

  if (debug) {
    fprintf(where,"%s is parsing the output selection '%s'\n",
	    __FUNCTION__,
	    output_selection);
    fflush(where);
  }

  /* is it the magic keyword? */
  if (strcasecmp(output_selection,"all") == 0) {
    set_output_list_all();
  }
  /* do not forget the case when the output_selection is a single
     mnemonic without any separators... */
  else if (strchr(output_selection,',') ||
	   strchr(output_selection,';') ||
	   (match_string_to_output_mnenomic(output_selection) !=
	    NETPERF_OUTPUT_UNKNOWN)) {
    parse_output_selection_direct(output_selection);
  }
  else {
    parse_output_selection_file(output_selection);
  }
  if (debug > 2) {
    dump_netperf_output_list(stderr);
  }
  return;
}

static void
set_output_elt(enum netperf_output_name name,
	       char *line0, char *line1, char *line2, char *line3,
	       char *format,
	       void *value,
	       unsigned int out_default,
	       unsigned int group,
	       enum netperf_output_type type) {

  netperf_output_source[name].output_name = name;
  netperf_output_source[name].line[0] = line0;
  netperf_output_source[name].line[1] = line1;
  netperf_output_source[name].line[2] = line2;
  netperf_output_source[name].line[3] = line3;
  netperf_output_source[name].format = format;
  netperf_output_source[name].display_value = value;
  netperf_output_source[name].output_default = out_default;
  netperf_output_source[name].output_group = group;
  netperf_output_source[name].max_line_len = NETPERF_LINE_MAX(name);
  netperf_output_source[name].tot_line_len = NETPERF_LINE_TOT(name);
  netperf_output_source[name].output_type = type;
}

void
print_omni_init_list() {

  int i;

  if (debug) {
    fprintf(where,"%s called\n",
	    __FUNCTION__);
  }

  /* belts and suspenders everyone... */
  for (i = NETPERF_OUTPUT_UNKNOWN; i < NETPERF_OUTPUT_MAX; i++) {
    netperf_output_source[i].output_name = i;
    netperf_output_source[i].max_line_len = 0;
    netperf_output_source[i].tot_line_len = 0;
    netperf_output_source[i].line[0] = "";
    netperf_output_source[i].line[1] = "";
    netperf_output_source[i].line[2] = "";
    netperf_output_source[i].line[3] = "";
    netperf_output_source[i].format = "";
    netperf_output_source[i].display_value = NULL;
    netperf_output_source[i].output_default = 1;
    netperf_output_source[i].output_group = 0;
    netperf_output_source[i].output_type = NETPERF_TYPE_UNKNOWN;
  }

  set_output_elt(OUTPUT_NONE, " ", "", "", "", "%s", &" ",1, 0,
		 NETPERF_TYPE_CHAR);

  set_output_elt(COMMAND_LINE, "Command","Line","","","\"%s\"",
		 command_line,1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(UUID, "Test", "UUID", "", "", "%s", test_uuid, 1, 0,
		 NETPERF_TYPE_CHAR);

  set_output_elt(RESULT_BRAND, "Result", "Tag", "", "", "\"%s\"",
		 result_brand, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(SOCKET_TYPE, "Socket", "Type", "", "", "%s",
		 socket_type_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(DIRECTION, "Direction", "", "", "", "%s",
		 direction_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(PROTOCOL, "Protocol", "", "", "", "%s",
		 protocol_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(ELAPSED_TIME, "Elapsed", "Time", "(sec)", "", "%.2f",
		 &elapsed_time_double, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(SOURCE_PORT, "Source", "Port", "", "", "%s",
		 local_data_port, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(SOURCE_ADDR, "Source", "Address", "", "", "%s",
		 local_data_address, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(SOURCE_FAMILY, "Source", "Family", "", "", "%d",
		 &local_data_family, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(DEST_PORT, "Destination", "Port", "", "", "%s",
		 remote_data_port, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(DEST_ADDR, "Destination", "Address", "", "", "%s",
		 remote_data_address, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(DEST_FAMILY, "Destination", "Family", "", "", "%d",
		 &remote_data_family, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(THROUGHPUT, "Throughput", "", "", "", "%.2f",
		 &thruput, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(LOCAL_SEND_THROUGHPUT, "Local", "Send", "Throughput", "",
		 "%.2f", &local_send_thruput, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(LOCAL_RECV_THROUGHPUT, "Local", "Recv", "Throughput", "",
		 "%.2f", &local_recv_thruput, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(REMOTE_SEND_THROUGHPUT, "Remote", "Send", "Throughput", "",
		 "%.2f", &remote_send_thruput, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(REMOTE_RECV_THROUGHPUT, "Remote", "Recv", "Throughput", "",
		 "%.2f", &remote_recv_thruput, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(THROUGHPUT_UNITS, "Throughput", "Units", "", "", "%s/s",
		 thruput_format_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(CONFIDENCE_LEVEL, "Confidence", "Level", "Percent", "", "%d",
		 &confidence_level, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(CONFIDENCE_INTERVAL, "Confidence", "Width", "Target", "",
		 "%f", &interval_pct, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(CONFIDENCE_ITERATION, "Confidence", "Iterations", "Run", "",
		 "%d", &confidence_iteration, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(THROUGHPUT_CONFID, "Throughput", "Confidence", "Width (%)",
		 "", "%.3f", &result_confid_pct, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(LOCAL_CPU_CONFID, "Local", "CPU", "Confidence", "Width (%)",
		 "%.3f", &loc_cpu_confid_pct, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(REMOTE_CPU_CONFID, "Remote", "CPU", "Confidence", "Width (%)",
		 "%.3f", &rem_cpu_confid_pct, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(RT_LATENCY, "Round", "Trip", "Latency", "usec/tran", "%.3f",
		 &rtt_latency, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(TRANSACTION_RATE, "Transaction", "Rate", "Tran/s", "", "%.3f",
		 &transaction_rate, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(TRANSPORT_MSS, "Transport", "MSS", "bytes", "", "%d",
		 &transport_mss, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_TRANSPORT_RETRANS, "Local", "Transport",
		 "Retransmissions", "", "%d", &local_transport_retrans, 1, 0,
		 NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_TRANSPORT_RETRANS, "Remote", "Transport",
		 "Retransmissions", "", "%d", &remote_transport_retrans, 1, 0,
		 NETPERF_TYPE_INT32);

  set_output_elt(REQUEST_SIZE, "Request", "Size", "Bytes", "", "%d",
		 &req_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(RESPONSE_SIZE, "Response", "Size", "Bytes", "", "%d",
		 &rsp_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(BURST_SIZE, "Initial", "Burst", "Requests", "", "%d",
		 &first_burst_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LSS_SIZE_REQ, "Local", "Send Socket", "Size", "Requested",
		 "%d", &lss_size_req, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LSS_SIZE, "Local", "Send Socket", "Size", "Initial", "%d",
		 &lss_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LSS_SIZE_END, "Local", "Send Socket", "Size", "Final", "%d",
		 &lss_size_end, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LSR_SIZE_REQ, "Local", "Recv Socket", "Size", "Requested",
		 "%d", &lsr_size_req, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LSR_SIZE, "Local", "Recv Socket", "Size", "Initial", "%d",
		 &lsr_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LSR_SIZE_END, "Local", "Recv Socket", "Size", "Final", "%d",
		 &lsr_size_end, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SEND_SIZE, "Local", "Send", "Size", "", "%d",
		 &send_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_RECV_SIZE, "Local", "Recv", "Size", "", "%d",
		 &recv_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SEND_CALLS, "Local", "Send", "Calls", "", "%"PRIu64,
		 &local_send_calls, 1, 0, NETPERF_TYPE_UINT64);

  set_output_elt(LOCAL_RECV_CALLS, "Local", "Recv", "Calls", "", "%"PRIu64,
		 &local_receive_calls, 1, 0, NETPERF_TYPE_UINT64);

  set_output_elt(LOCAL_BYTES_PER_RECV, "Local", "Bytes", "Per", "Recv", "%.2f",
		 &bytes_per_recv, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(LOCAL_BYTES_PER_SEND, "Local", "Bytes", "Per", "Send", "%.2f",
		 &bytes_per_send, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(LOCAL_BYTES_RECVD, "Local", "Bytes", "Received", "", "%"PRIu64,
		 &bytes_received, 1, 0, NETPERF_TYPE_UINT64);

  set_output_elt(LOCAL_BYTES_SENT, "Local", "Bytes", "Sent", "", "%"PRIu64,
		 &bytes_sent, 1, 0, NETPERF_TYPE_UINT64);

  set_output_elt(LOCAL_BYTES_XFERD, "Local", "Bytes", "Xferred", "", "%.0f",
		 &bytes_xferd, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(LOCAL_SEND_WIDTH, "Local", "Send", "Width", "", "%d",
		 &send_width, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_RECV_WIDTH, "Local", "Recv", "Width", "", "%d",
		 &recv_width, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SEND_OFFSET, "Local", "Send", "Offset", "", "%d",
		 &local_send_offset, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_RECV_OFFSET, "Local", "Recv", "Offset", "", "%d",
		 &local_recv_offset, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_RECV_ALIGN, "Local", "Recv", "Alignment", "", "%d",
		 &local_recv_align, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SEND_ALIGN, "Local", "Send", "Alignment", "", "%d",
		 &local_send_align, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SEND_DIRTY_COUNT, "Local", "Send", "Dirty", "Count",
		 "%d", &loc_dirty_count, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_RECV_DIRTY_COUNT, "Local", "Recv", "Dirty", "Count",
		 "%d", &loc_dirty_count, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_RECV_CLEAN_COUNT, "Local", "Recv", "Clean", "Count",
		 "%d", &loc_clean_count, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_CPU_UTIL, "Local", "CPU", "Util", "%", "%.2f",
		 &local_cpu_utilization, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(LOCAL_CPU_PERCENT_USER, "Local", "CPU", "User", "%", "%.2f",
		 &lib_local_cpu_stats.cpu_user, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(LOCAL_CPU_PERCENT_SYSTEM,
                 "Local", "CPU", "System", "%", "%.2f",
		 &lib_local_cpu_stats.cpu_system, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(LOCAL_CPU_PERCENT_IOWAIT,
                 "Local", "CPU", "I/O", "%", "%.2f",
		 &lib_local_cpu_stats.cpu_iowait, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(LOCAL_CPU_PERCENT_IRQ,
                 "Local", "CPU", "IRQ", "%", "%.2f",
		 &lib_local_cpu_stats.cpu_irq, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(LOCAL_CPU_PERCENT_SWINTR,
                 "Local", "CPU", "swintr", "%", "%.2f",
		 &lib_local_cpu_stats.cpu_swintr, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(LOCAL_CPU_PEAK_UTIL, "Local", "Peak", "Per CPU", "Util %",
		 "%.2f", &lib_local_cpu_stats.peak_cpu_util, 1, 0,
                 NETPERF_TYPE_FLOAT);

  set_output_elt(LOCAL_CPU_PEAK_ID, "Local", "Peak", "Per CPU", "ID", "%d",
		 &lib_local_cpu_stats.peak_cpu_id, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_CPU_BIND, "Local", "CPU", "Bind", "", "%d",
		 &local_proc_affinity, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SD, "Local", "Service", "Demand", "", "%.3f",
		 &local_service_demand_double, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(SD_UNITS, "Service", "Demand", "Units", "", "%s",
		 sd_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_CPU_METHOD, "Local", "CPU", "Util", "Method", "%c",
		 &local_cpu_method, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_CPU_COUNT, "Local", "CPU", "Count", "", "%d",
		 &lib_num_loc_cpus, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_NODELAY, "Local", "NODELAY", "", "", "%d",
		 &loc_nodelay, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_CORK, "Local", "Cork", "", "", "%d",
		 &loc_tcpcork, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(RSS_SIZE_REQ, "Remote", "Send Socket", "Size", "Requested",
		 "%d", &rss_size_req, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(RSS_SIZE, "Remote", "Send Socket", "Size", "Initial", "%d",
		 &rss_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(RSS_SIZE_END, "Remote", "Send Socket", "Size", "Final", "%d",
		 &rss_size_end, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(RSR_SIZE_REQ, "Remote", "Recv Socket", "Size", "Requested",
		 "%d", &rsr_size_req, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(RSR_SIZE, "Remote", "Recv Socket", "Size", "Initial", "%d",
		 &rsr_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(RSR_SIZE_END, "Remote", "Recv Socket", "Size", "Final", "%d",
		 &rsr_size_end, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SEND_SIZE, "Remote", "Send", "Size", "", "%d",
		 &remote_send_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_RECV_SIZE, "Remote", "Recv", "Size", "", "%d",
		 &remote_recv_size, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SEND_CALLS, "Remote", "Send", "Calls", "", "%"PRIu64,
		 &remote_send_calls, 1, 0, NETPERF_TYPE_UINT64);

  set_output_elt(REMOTE_RECV_CALLS, "Remote", "Recv", "Calls", "", "%"PRIu64,
		 &remote_receive_calls, 1, 0, NETPERF_TYPE_UINT64);

  set_output_elt(REMOTE_BYTES_PER_RECV, "Remote", "Bytes", "Per", "Recv",
		 "%.2f", &remote_bytes_per_recv, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(REMOTE_BYTES_PER_SEND, "Remote", "Bytes", "Per", "Send",
		 "%.2f", &remote_bytes_per_send, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(REMOTE_BYTES_RECVD, "Remote", "Bytes", "Received", "",
		 "%"PRIu64, &remote_bytes_received, 1, 0, NETPERF_TYPE_UINT64);

  set_output_elt(REMOTE_BYTES_SENT, "Remote", "Bytes", "Sent", "", "%"PRIu64,
		 &remote_bytes_sent, 1, 0, NETPERF_TYPE_UINT64);

  set_output_elt(REMOTE_BYTES_XFERD, "Remote", "Bytes", "Xferred", "", "%.0f",
		 &remote_bytes_xferd, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(REMOTE_SEND_WIDTH, "Remote", "Send", "Width", "", "%d",
		 &remote_send_width, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_RECV_WIDTH, "Remote", "Recv", "Width", "", "%d",
		 &remote_recv_width, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SEND_OFFSET, "Remote", "Send", "Offset", "", "%d",
		 &remote_send_offset, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_RECV_OFFSET, "Remote", "Recv", "Offset", "", "%d",
		 &remote_recv_offset, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_RECV_ALIGN, "Remote", "Recv", "Alignment", "", "%d",
		 &remote_recv_align, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SEND_ALIGN, "Remote", "Send", "Alignment", "", "%d",
		 &remote_send_align, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SEND_DIRTY_COUNT, "Remote", "Send", "Dirty", "Count",
		 "%d", &rem_dirty_count, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_RECV_DIRTY_COUNT, "Remote", "Recv", "Dirty", "Count",
		 "%d", &rem_dirty_count, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_RECV_CLEAN_COUNT, "Remote", "Recv", "Clean", "Count",
		 "%d", &rem_clean_count, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_CPU_UTIL, "Remote", "CPU", "Util", "%", "%.2f",
		 &remote_cpu_utilization, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(REMOTE_CPU_PERCENT_USER, "Remote", "CPU", "User", "%", "%.2f",
		 &lib_remote_cpu_stats.cpu_user, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(REMOTE_CPU_PERCENT_SYSTEM,
                 "Remote", "CPU", "System", "%", "%.2f",
		 &lib_remote_cpu_stats.cpu_system, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(REMOTE_CPU_PERCENT_IOWAIT,
                 "Remote", "CPU", "I/O", "%", "%.2f",
		 &lib_remote_cpu_stats.cpu_iowait, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(REMOTE_CPU_PERCENT_IRQ,
                 "Remote", "CPU", "IRQ", "%", "%.2f",
		 &lib_remote_cpu_stats.cpu_irq, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(REMOTE_CPU_PERCENT_SWINTR,
                 "Remote", "CPU", "swintr", "%", "%.2f",
		 &lib_remote_cpu_stats.cpu_swintr, 1, 0, NETPERF_TYPE_FLOAT);

  set_output_elt(REMOTE_CPU_PEAK_UTIL, "Remote", "Peak", "Per CPU", "Util %",
		 "%.2f", &lib_remote_cpu_stats.peak_cpu_util, 1, 0,
                 NETPERF_TYPE_FLOAT);

  set_output_elt(REMOTE_CPU_PEAK_ID, "Remote", "Peak", "Per CPU", "ID", "%d",
		 &lib_remote_cpu_stats.peak_cpu_id, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_CPU_BIND, "Remote", "CPU", "Bind", "", "%d",
		 &remote_proc_affinity, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SD, "Remote", "Service", "Demand", "", "%.3f",
		 &remote_service_demand_double, 1, 0, NETPERF_TYPE_DOUBLE);

  set_output_elt(REMOTE_CPU_METHOD, "Remote", "CPU", "Util", "Method", "%c",
		 &remote_cpu_method, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_CPU_COUNT, "Remote", "CPU", "Count", "", "%d",
		 &lib_num_rem_cpus, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_NODELAY, "Remote", "NODELAY", "", "", "%d",
		 &rem_nodelay, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_CORK, "Remote", "Cork", "", "", "%d",
		 &rem_tcpcork, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_DRIVER_NAME, "Local", "Driver", "Name", "", "%s",
		 deprecated_str, 1, OMNI_WANT_LOC_DRVINFO,
		 NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_DRIVER_VERSION, "Local", "Driver", "Version", "", "%s",
		 deprecated_str, 1, OMNI_WANT_LOC_DRVINFO,
		 NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_DRIVER_FIRMWARE, "Local", "Driver", "Firmware", "",
		 "%s", deprecated_str, 1, OMNI_WANT_LOC_DRVINFO,
		 NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_DRIVER_BUS, "Local", "Driver", "Bus", "", "%s",
		 deprecated_str, 1, OMNI_WANT_LOC_DRVINFO, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_DRIVER_NAME, "Remote", "Driver", "Name", "", "%s",
		 deprecated_str, 1, OMNI_WANT_REM_DRVINFO,
		 NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_DRIVER_VERSION, "Remote", "Driver", "Version", "",
		 "%s", deprecated_str, 1, OMNI_WANT_REM_DRVINFO,
		 NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_DRIVER_FIRMWARE, "Remote", "Driver", "Firmware", "",
		 "%s", deprecated_str, 1, OMNI_WANT_REM_DRVINFO,
		 NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_DRIVER_BUS, "Remote", "Driver", "Bus", "", "%s",
		 deprecated_str, 1, OMNI_WANT_REM_DRVINFO,
		 NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_INTERFACE_SUBDEVICE, "Local", "Interface", "Subdevice",
		 "", "0x%.4x", &local_interface_subdevice, 1,
		 OMNI_WANT_LOC_IFIDS, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_INTERFACE_DEVICE, "Local", "Interface", "Device", "",
		 "0x%.4x", &local_interface_device, 1, OMNI_WANT_LOC_IFIDS,
		 NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_INTERFACE_SUBVENDOR, "Local", "Interface", "Subvendor",
		 "", "0x%.4x", &local_interface_subvendor, 1,
		 OMNI_WANT_LOC_IFIDS, NETPERF_TYPE_UINT32);

  set_output_elt(LOCAL_INTERFACE_VENDOR, "Local", "Interface", "Vendor", "",
		 "0x%.4x", &local_interface_vendor, 1, OMNI_WANT_LOC_IFIDS,
		 NETPERF_TYPE_UINT32);

  set_output_elt(REMOTE_INTERFACE_SUBDEVICE, "Remote", "Interface",
		 "Subdevice", "", "0x%.4x", &remote_interface_subdevice, 1,
		 OMNI_WANT_REM_IFIDS, NETPERF_TYPE_UINT32);

  set_output_elt(REMOTE_INTERFACE_DEVICE, "Remote", "Interface", "Device", "",
		 "0x%.4x", &remote_interface_device, 1, OMNI_WANT_REM_IFIDS,
		 NETPERF_TYPE_UINT32);

  set_output_elt(REMOTE_INTERFACE_SUBVENDOR, "Remote", "Interface",
		 "Subvendor", "", "0x%.4x", &remote_interface_subvendor, 1,
		 OMNI_WANT_REM_IFIDS, NETPERF_TYPE_UINT32);

  set_output_elt(REMOTE_INTERFACE_VENDOR, "Remote", "Interface", "Vendor", "",
		 "0x%.4x", &remote_interface_vendor, 1, OMNI_WANT_REM_IFIDS,
		 NETPERF_TYPE_UINT32);

  set_output_elt(LOCAL_INTERFACE_NAME, "Local", "Interface", "Name", "", "%s",
		 deprecated_str, 1, OMNI_WANT_LOC_IFNAME,
		 NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_INTERFACE_NAME, "Remote", "Interface", "Name", "",
		 "%s", deprecated_str, 1, OMNI_WANT_REM_IFNAME,
		 NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_INTERFACE_SLOT, "Local", "Interface", "Slot", "", "%s",
		 deprecated_str, 1, OMNI_WANT_LOC_IFSLOT,
		 NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_INTERFACE_SLOT,  "Remote",  "Interface",  "Slot",  "",
		 "%s",  deprecated_str, 1, OMNI_WANT_REM_IFSLOT,
		 NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_MACHINE, "Remote", "Machine", "", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_VERSION, "Remote", "Version", "", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_RELEASE, "Remote", "Release", "", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_SYSNAME, "Remote", "Sysname", "", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_MACHINE, "Local", "Machine", "", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_VERSION, "Local", "Version", "", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_RELEASE, "Local", "Release", "", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_SYSNAME, "Local", "Sysname", "", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_INTERVAL_USECS, "Remote", "Interval", "Usecs", "",
		 "%d", &remote_interval_usecs, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_INTERVAL_BURST, "Remote", "Interval", "Burst", "",
		 "%d", &remote_interval_burst, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SECURITY_ENABLED, "Local", "OS", "Security", "Enabled",
		 "%s", deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_SECURITY_TYPE, "Local", "OS", "Security", "Type", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_SECURITY_SPECIFIC, "Local", "OS", "Security",
		 "Specific", "%s", deprecated_str, 1, 0,
		 NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_SECURITY_ENABLED_NUM, "Local", "OS", "Security",
		 "Enabled Num", "%d", &local_security_enabled_num, 1, 0,
		 NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SECURITY_TYPE_ID, "Local", "OS", "Security", "Type ID",
		 "%d", &local_security_type_id, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SECURITY_ENABLED, "Remote", "OS", "Security",
		 "Enabled", "%s", deprecated_str, 1, 0,
		 NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_SECURITY_TYPE, "Remote", "OS", "Security", "Type",
		 "%s", deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_SECURITY_SPECIFIC, "Remote", "OS", "Security",
		 "Specific", "%s", deprecated_str, 1, 0,
		 NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_SECURITY_ENABLED_NUM, "Remote", "OS", "Security",
		 "Enabled", "%d", &remote_security_enabled_num, 1, 0,
		 NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SECURITY_TYPE_ID, "Remote", "OS", "Security", "Type",
		 "%d", &remote_security_type_id, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_INTERVAL_USECS, "Local", "Interval", "Usecs", "", "%d",
		 &interval_usecs, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_INTERVAL_BURST, "Local", "Interval", "Burst", "", "%d",
		 &interval_burst, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SYSTEM_MODEL, "Remote", "System", "Model", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_CPU_MODEL, "Remote", "CPU", "Model", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_CPU_FREQUENCY, "Remote", "CPU", "Frequency", "MHz",
		 "%d", &remote_cpu_frequency, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SYSTEM_MODEL, "Local", "System", "Model", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_CPU_MODEL, "Local", "CPU", "Model", "", "%s",
		 deprecated_str, 1, 0, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_CPU_FREQUENCY, "Local", "CPU", "Frequency", "MHz", "%d",
		 &local_cpu_frequency, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(MIN_LATENCY, "Minimum", "Latency", "Microseconds", "", "%d",
		 &min_latency, 0, OMNI_WANT_STATS, NETPERF_TYPE_INT32);

  set_output_elt(MAX_LATENCY, "Maximum", "Latency", "Microseconds", "", "%d",
		 &max_latency, 0, OMNI_WANT_STATS, NETPERF_TYPE_INT32);

  set_output_elt(P50_LATENCY, "50th", "Percentile", "Latency", "Microseconds",
		 "%d", &p50_latency, 0, OMNI_WANT_STATS, NETPERF_TYPE_INT32);

  set_output_elt(P90_LATENCY, "90th", "Percentile", "Latency", "Microseconds",
		 "%d", &p90_latency, 0, OMNI_WANT_STATS, NETPERF_TYPE_INT32);

  set_output_elt(P99_LATENCY, "99th", "Percentile", "Latency", "Microseconds",
		 "%d", &p99_latency, 0, OMNI_WANT_STATS, NETPERF_TYPE_INT32);

  set_output_elt(MEAN_LATENCY, "Mean", "Latency", "Microseconds", "", "%.2f",
		 &mean_latency, 0, OMNI_WANT_STATS, NETPERF_TYPE_DOUBLE);

  set_output_elt(STDDEV_LATENCY, "Stddev", "Latency", "Microseconds", "",
		 "%.2f", &stddev_latency, 0, OMNI_WANT_STATS, NETPERF_TYPE_DOUBLE);

  set_output_elt(LOCAL_SOCKET_PRIO, "Local", "Socket", "Priority", "", "%d",
		 &local_socket_prio, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SOCKET_PRIO, "Remote", "Socket", "Priority", "", "%d"
		 , &remote_socket_prio, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_SOCKET_TOS, "Local", "Socket", "TOS", "", "0x%.2x",
		 &local_socket_tos, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(REMOTE_SOCKET_TOS, "Remote", "Socket", "TOS", "", "0x%.2x",
		 &remote_socket_tos, 1, 0, NETPERF_TYPE_INT32);

  set_output_elt(LOCAL_CONG_CONTROL, "Local", "Congestion", "Control",
		 "Algorithm", "%s", local_cong_control, 0,
		 OMNI_WANT_LOC_CONG, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_CONG_CONTROL, "Remote", "Congestion", "Control",
		 "Algorithm", "%s", remote_cong_control, 0,
		 OMNI_WANT_REM_CONG, NETPERF_TYPE_CHAR);

  set_output_elt(LOCAL_FILL_FILE, "Local", "Fill", "File", "", "%s",
		 local_fill_file, 0, 0, NETPERF_TYPE_CHAR);

  set_output_elt(REMOTE_FILL_FILE, "Remote", "Fill", "File", "", "%s",
		 remote_fill_file, 0, 0, NETPERF_TYPE_CHAR);

  set_output_elt(OUTPUT_END, "This", "Is", "The", "End", "%s",
		 NULL, 0, 0, NETPERF_TYPE_CHAR);

}

void
print_omni_init() {

  int i,j;

  if (debug) {
    fprintf(where,"%s entered\n",
	    __FUNCTION__);
    fflush(where);
  }

  /* why is this before the if you ask? because some of the output
     specifiers are char * rather than char[] and when I wanted to
     start setting output_group flags I was needing to call
     print_omni_init() before the char * 's were malloced, which meant
     the netperf_output_source got NULL pointers.  there is
     undoubtedly a cleaner way to do all this. raj 20110629 */

  print_omni_init_list();

  if (printing_initialized) return;

  printing_initialized = 1;


  /* belts and suspenders */
  for (j = 0; j < NETPERF_MAX_BLOCKS; j++)
    for (i = 0; i < NETPERF_OUTPUT_MAX; i++)
      output_list[j][i] = OUTPUT_END;


  if (output_selection_spec) {
      parse_output_selection(output_selection_spec);
  }
  else {
      set_output_list_by_test();
  }

}

/* why? because one cannot simply pass a pointer to snprintf - well
   except when it is expecting one... */
int
my_snprintf(char *buffer, size_t size, netperf_output_elt_t *output_elt)
{
  switch (output_elt->output_type) {
  case NETPERF_TYPE_CHAR:
    return snprintf(buffer, size,
		    output_elt->format,
		    (char *)output_elt->display_value);
    break;
  case NETPERF_TYPE_INT32:
    return snprintf(buffer, size,
		    output_elt->format,
		    *(int *)(output_elt->display_value));
    break;
  case NETPERF_TYPE_UINT32:
    return snprintf(buffer, size,
		    output_elt->format,
		    *(unsigned int *)(output_elt->display_value));
    break;
  case NETPERF_TYPE_INT64:
    return snprintf(buffer, size,
		    output_elt->format,
		    *(long long *)(output_elt->display_value));
    break;
  case NETPERF_TYPE_UINT64:
    return snprintf(buffer, size,
		    output_elt->format,
		    *(unsigned long long *)(output_elt->display_value));
    break;
  case NETPERF_TYPE_FLOAT:
    return snprintf(buffer, size,
		    output_elt->format,
		    *(float *)(output_elt->display_value));
    break;
  case NETPERF_TYPE_DOUBLE:
    return snprintf(buffer, size,
		    output_elt->format,
		    *(double *)(output_elt->display_value));
    break;
  default:
    fprintf(stderr,
	    "Unknown/unsupported output_elt output_type of %d\n",
	    output_elt->output_type);
    fflush(stderr);
    exit(-1);
  }
}

void
print_omni_csv()
{

  int i,j,k,buflen,vallen;

  char *hdr1 = NULL;
  char *val1 = NULL;
  char *h1 = NULL;
  char *v1 = NULL;
  char tmpval[1024];

  buflen = 0;
  for (i = 0; i < NETPERF_MAX_BLOCKS; i++) {
    for (j = 0;
	 ((j < NETPERF_OUTPUT_MAX) &&
	  (output_list[i][j] != OUTPUT_END));
	 j++) {
      if ((netperf_output_source[output_list[i][j]].format != NULL) &&
	  (netperf_output_source[output_list[i][j]].display_value != NULL)) {
	vallen =
	  my_snprintf(tmpval,
		      1024,
		      &(netperf_output_source[output_list[i][j]]));
	if (vallen == -1) {
	  fprintf(where,"my_snprintf failed on %s with format %s\n",
		  netperf_output_enum_to_str(j),
		  netperf_output_source[output_list[i][j]].format);
	  fflush(where);
	}
	vallen += 1; /* forget not the terminator */
      }
      else
	vallen = 0;

      if (vallen >
	  netperf_output_source[output_list[i][j]].tot_line_len)
	netperf_output_source[output_list[i][j]].tot_line_len = vallen;

      buflen +=
	netperf_output_source[output_list[i][j]].tot_line_len;
    }
  }

  if (print_headers) hdr1 = malloc(buflen + 1);
  val1 = malloc(buflen + 1);

  if (((hdr1 == NULL) && (print_headers)) ||
      (val1 == NULL)) {
    fprintf(where,"unable to allocate output buffers\n");
    fflush(where);
    exit(-1);
  }

  if (print_headers) memset(hdr1,' ',buflen + 1);
  memset(val1,' ',buflen + 1);

  /* ostensibly, we now "know" that we have enough space in all our
     strings, and we have spaces where we want them etc */
  h1 = hdr1;
  v1 = val1;
  for (i = 0; i < NETPERF_MAX_BLOCKS; i++) {
    for (j = 0;
	 ((j < NETPERF_OUTPUT_MAX) &&
	  (output_list[i][j] != OUTPUT_END));
	 j++) {
      int len;
      len = 0;
      if (print_headers) {
	for (k = 0; ((k < 4) &&
		     (NULL !=
		      netperf_output_source[output_list[i][j]].line[k]) &&
		     (strcmp("",netperf_output_source[output_list[i][j]].line[k]))); k++) {

	  len = sprintf(h1,
			"%s",
			netperf_output_source[output_list[i][j]].line[k]);
	  *(h1 + len) = ' ';
	  /* now move to the next starting column. for csv we aren't worried
	     about alignment between the header and the value lines */
	  h1 += len + 1;
	}
	*(h1 - 1) = ',';
      }
      if ((netperf_output_source[output_list[i][j]].format != NULL) &&
	  (netperf_output_source[output_list[i][j]].display_value != NULL)) {
	/* tot_line_len is bogus here, but should be "OK" ? */
	len = my_snprintf(v1,
			  netperf_output_source[output_list[i][j]].tot_line_len,
			  &(netperf_output_source[output_list[i][j]]));

	/* nuke the trailing \n" from the string routine.  */
	*(v1 + len) = ',';
	v1 += len + 1;
      }
      else {
	/* we need a ',' even if there is no value */
	*v1 = ',';
	v1 += 2;
      }
    }
  }

  /* ok, _now_ null terminate each line by nuking the last comma.  do
     we have an OBOB here? */
  if (print_headers) *(h1-1) = 0;
  *(v1-1) = 0;
  /* and now spit it out, but only if it is going to have something
     in it. we don't want a bunch of blank lines or nulls...  */
  if (output_list[0][0] != OUTPUT_END) {
    if (print_headers) printf("%s\n",hdr1);
    printf("%s\n",val1);
  }

  if (hdr1 != NULL) free(hdr1);
  if (val1 != NULL) free(val1);

}

void
print_omni_keyword()
{
  /* this one should be the simplest of all - no buffers to allocate,
     just spit it all out. raj 20080805 */

  int i,j;
  char tmpval[1024];
  int vallen;

  for (i = 0; i < NETPERF_MAX_BLOCKS; i++) {
    for (j = 0;
	 ((j < NETPERF_OUTPUT_MAX) &&
	  (output_list[i][j] != OUTPUT_END));
	 j++) {
      if ((netperf_output_source[output_list[i][j]].format != NULL) &&
	  (netperf_output_source[output_list[i][j]].display_value != NULL)) {
	vallen =
	  my_snprintf(tmpval,
		      1024,
		      &(netperf_output_source[output_list[i][j]]));
	if (vallen == -1) {
	  snprintf(tmpval,
		   1024,
		   "my_snprintf failed with format %s\n",
		   netperf_output_source[output_list[i][j]].format);
	}
	fprintf(where,
		"%s=%s\n",netperf_output_enum_to_str(output_list[i][j]),
		tmpval);
      }
    }
  }
  fflush(where);
}

void
print_omni_human()
{

  int i,j,k,buflen,buflen_max;

  char *hdr[4];
  char *val1 = NULL;
  char tmpval[1024];  /* excessive, but we may have the command line */
  int  vallen;

  for (k = 0; k < 4; k ++) {
    hdr[k] = NULL;
  }

  /* decisions, decisions... walk the list twice to only need to
     allocate the charcter buffers once, or walk it once and possibly
     reallocate them as I go... oh, lets walk it twice just for fun to
     start. since only now do we know that the values are around to be
     printed, we should try the snprintf for the value and see how
     much space it wants and update max_line_len accordingly */
  buflen_max = 0;
  for (i = 0; i < NETPERF_MAX_BLOCKS; i++) {
    buflen = 0;
    for (j = 0;
	 ((j < NETPERF_OUTPUT_MAX) &&
	  (output_list[i][j] != OUTPUT_END));
	 j++) {
      if ((netperf_output_source[output_list[i][j]].format != NULL) &&
	  (netperf_output_source[output_list[i][j]].display_value !=
	   NULL))
	/* need to count the \n */
	vallen = my_snprintf(tmpval,
			     1024,
			     &(netperf_output_source[output_list[i][j]])) + 1;
      else
	vallen = 0;

      if (vallen >
	  netperf_output_source[output_list[i][j]].max_line_len)
	netperf_output_source[output_list[i][j]].max_line_len = vallen;

      buflen +=
	netperf_output_source[output_list[i][j]].max_line_len + 1;
    }

    if (buflen > buflen_max)
      buflen_max = buflen;
  }

  /* more belts and suspenders */
  for (k = 0; (k < 4) && (print_headers); k++) {
    hdr[k] = malloc(buflen_max+1);
  }
  val1 = malloc(buflen_max+1);

  /* we could probably be more succinct here but perhaps the compiler
     can figure that out for us :) */
  for (k = 0; (k < 4) && (print_headers); k++) {
    if (hdr[k] == NULL) {
      fprintf(where,"Unable to allocate output buffers\n");
      fflush(where);
      exit(-1);
    }
  }

  /* ostensibly, we now "know" that we have enough space in all our
     strings, and we have spaces where we want them etc */
  for (i = 0; i < NETPERF_MAX_BLOCKS; i++) {
    char *h[4];
    char *v1 = val1;

    for (k = 0; k < 4; k++) h[k] = hdr[k];

    /* we want to blank things out each time since we skip around a lot */
    for (k = 0; (k < 4) && (print_headers); k++) {
      memset(hdr[k],' ',buflen_max+1);
    }
    memset(val1,' ',buflen_max+1);


    for (j = 0;
	 ((j < NETPERF_OUTPUT_MAX) &&
	  (output_list[i][j] != OUTPUT_END));
	 j++) {
      if (print_headers) {
	for (k = 0; k < 4; k++) {
	  memcpy(h[k],
		 netperf_output_source[output_list[i][j]].line[k],
		 strlen(netperf_output_source[output_list[i][j]].line[k]));
	}
      }
      if ((netperf_output_source[output_list[i][j]].format != NULL) &&
	  (netperf_output_source[output_list[i][j]].display_value != NULL)) {
	int len;
	len = my_snprintf(v1,
			  netperf_output_source[output_list[i][j]].max_line_len,
			  &(netperf_output_source[output_list[i][j]]));
	/* nuke the trailing \n" from the string routine.  */
	*(v1 + len) = ' ';
      }
      /* now move to the next starting column */
    for (k = 0; (k < 4) && (print_headers); k++) {
	h[k] +=
	  netperf_output_source[output_list[i][j]].max_line_len + 1;
      }
      v1 += netperf_output_source[output_list[i][j]].max_line_len + 1;
    }
    /* ok, _now_ null terminate each line.  do we have an OBOB here? */
    for (k = 0; (k < 4) && (print_headers); k++) {
      *h[k] = 0;
    }
    *v1 = 0;
    /* and now spit it out, but only if it is going to have something
       in it. we don't want a bunch of blank lines or nulls... at some
     point we might want to work backwards collapsine whitespace from
     the right but for now, we won't bother */
    if (output_list[i][0] != OUTPUT_END) {
      if (i > 0) printf("\n"); /* we want a blank line between blocks ? */
      for (k = 0; (k < 4) && (print_headers); k++) {
	printf("%s\n",hdr[k]);
      }
      printf("%s\n",val1);
    }
  };
  for (k = 0; k < 4; k++) {
    if (hdr[k] != NULL) free(hdr[k]);
  }
}

void
print_omni()
{

  print_omni_init();

  if (debug > 2)
    dump_netperf_output_source(where);

  switch (netperf_output_mode) {
  case CSV:
    print_omni_csv();
    break;
  case KEYVAL:
    print_omni_keyword();
    break;
  case HUMAN:
    print_omni_human();
    break;
  default:
    fprintf(where,"Yo Rick! There is a bug in netperf_output_mode!\n");
    fflush(where);
    exit(-1);
  }

}
/* for the next few routines (connect, accept, send, recv,
   disconnect/close) we will use a return of -1 to mean times up, -2
   to mean a transient error (eg ENOBUFS on a UDP send call) and -3 to
   mean hard error.  this means it is ok for the connect routine to
   return a 0 (zero) if that happens to be the fd/SOCKET we get and in
   theory we will be able to support zero-length messages on those
   protocols which support it.  all in theory of course. raj
   2008-01-09 */

int
connect_data_socket(SOCKET send_socket, struct addrinfo *remote_res, int dont_give_up)
{
  int ret;

  /* Connect up to the remote port on the data socket  */
  if ((ret = connect(send_socket,
		     remote_res->ai_addr,
		     remote_res->ai_addrlen)) == INVALID_SOCKET) {
    if (SOCKET_EINTR(ret))  {
      /* we interpret this to mean that the test is supposed to be
	 over, so return a value of -1 to the caller */
      return -1;
    }
    if ((SOCKET_EADDRINUSE(ret)) || SOCKET_EADDRNOTAVAIL(ret) || dont_give_up) {
      /* likely something our explicit bind() would have caught in
	 the past, so go get another port, via create_data_socket.
	 yes, this is a bit more overhead than before, but the
	 condition should be rather rare. we only get a new port if
	 this was a connection-including test like TCP_CRR or
	 TCP_CC. Otherwise we need to return an error. raj
	 2008-01-08 */
      return -2;
    }
    else
      /* -3 means there was an error */
      return -3;
  }
  return 0;
}

static
int send_pktinfo(SOCKET data_socket, char *buffer, int len, struct sockaddr *destination, int destlen) {
#ifdef IP_PKTINFO
//NOTE: Linux's msghdr is very similar to Windows' WSAMSG, but the member names are different :(
#ifndef WIN32
  struct msghdr msg;
  struct iovec iovec[1];
#else
  struct _WSAMSG msg;
  struct _WSABUF iovec[1];
  DWORD BytesSent = 0;
  int rc = 0;
#endif
  char msg_control[512];
  struct cmsghdr *cmsg;
  int cmsg_space = 0;

#ifndef WIN32
  iovec[0].iov_base = buffer;
  iovec[0].iov_len = len;

  msg.msg_name = destination;
  msg.msg_namelen = destlen;
  msg.msg_iov = iovec;
  msg.msg_iovlen = 1;
  msg.msg_control = msg_control;
  msg.msg_controllen = sizeof(msg_control);
  msg.msg_flags = 0;
#else
  iovec[0].buf = buffer;
  iovec[0].len = len;

  msg.name = destination;
  msg.namelen = destlen;
  msg.lpBuffers = iovec;
  msg.dwBufferCount = 1;
  msg.Control.buf = msg_control;
  msg.Control.len = sizeof(msg_control);
  msg.dwFlags = 0;
#endif //WIN32

  cmsg = CMSG_FIRSTHDR(&msg);
  if (have_pktinfo) {
    cmsg->cmsg_level = IPPROTO_IP;
    cmsg->cmsg_type = IP_PKTINFO;
    cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
    *(struct in_pktinfo*)CMSG_DATA(cmsg) = in_pktinfo;
    cmsg_space += CMSG_SPACE(sizeof(in_pktinfo));
  }
#ifndef WIN32
  msg.msg_controllen = cmsg_space;
  return sendmsg(data_socket, &msg, 0);
#else
  msg.Control.len = cmsg_space;
  rc = WSASendMsg(data_socket, &msg, 0, &BytesSent, NULL, NULL);
  //+*+ If rc != 0 we should call WSAGetLastError and print some kind of error msg...
  return BytesSent;
#endif
#else
  return -1;
#endif /* IP_PKTINFO */
}


int
send_data(SOCKET data_socket, struct ring_elt *send_ring, uint32_t bytes_to_send, struct sockaddr *destination, int destlen, int protocol) {

  int len;

  /* if the user has supplied a destination, we use sendto, otherwise
     we use send.  we ass-u-me blocking operations always, so no need
     to check for eagain or the like. */

  if (debug > 2) {
    fprintf(where,
	    "%s sock %d, ring elt %p, bytes %d, dest %p, len %d\n",
	    __FUNCTION__,
	    data_socket,
	    send_ring,
	    bytes_to_send,
	    destination,
	    destlen);
    fflush(where);
  }

  if (destination) {
    if (have_pktinfo) {
      len = send_pktinfo(data_socket,
			 send_ring->buffer_ptr,
			 bytes_to_send,
			 destination,
			 destlen);
    }
    else {
      len = sendto(data_socket,
		   send_ring->buffer_ptr,
		   bytes_to_send,
#if defined(MSG_FASTOPEN)
		   (use_fastopen && protocol == IPPROTO_TCP) ? MSG_FASTOPEN : 0,
#else
		   0,
#endif
		   destination,
		   destlen);
    }
  }
  else {
    if (!use_write) {
      len = send(data_socket,
		 send_ring->buffer_ptr,
		 bytes_to_send,
		 0);
    }
    else {
#ifndef WIN32
      len = write(data_socket,
		  send_ring->buffer_ptr,
		  bytes_to_send);
#else
      fprintf(where,"I'm sorry Dave I cannot write() under Windows\n");
      fflush(where);
      return -3;
#endif
    }
  }
  if(len != bytes_to_send) {
    /* don't forget that some platforms may do a partial send upon
       receipt of the interrupt and not return an EINTR... */
    if (SOCKET_EINTR(len) || (len >= 0))
      {
	/* we hit the end of a  timed test. */
	return -1;
      }
    /* if this is UDP it is possible to receive an ENOBUFS on the send
       call and it would not be a fatal error.  of course if we were
       to return 0 then it would make the test think it was over when
       it really wasn't.  the question becomes what to do.  for the
       time being, the answer will likely be to return something like
       -2 to indicate a non-fatal error happened on the send and let
       the caller figure it out :) we won't actually check to see if
       this is UDP - it is the author's experience in many, Many, MANY
       years that the only time an ENOBUFS has been returned in a
       netperf test has been with UDP.  famous last words :)

       And so, many years later, we learn that with virtio one can
       also get ENOMEM.  So much for famous last words - Thanks Paolo
       :) */

#ifdef WIN32
	// Until MSFT gets around to defining yhis in winsock2.h
#ifndef ENOMEM
#define ENOMEM ENOBUFS
#endif
#endif

    if ((errno == ENOBUFS) || (errno == ENOMEM))
      return -2;
    else {
      fprintf(where,"%s: data send error: %s (errno %d)\n",
	      __FUNCTION__, strerror(errno), errno);
      return -3;
    }
  }
  return len;
}

#if defined(__linux)
static int
recv_data_no_copy(SOCKET data_socket, struct ring_elt *recv_ring, uint32_t bytes_to_recv, struct sockaddr *source, netperf_socklen_t *sourcelen, uint32_t flags, uint32_t *num_receives) {

#ifndef SPLICE_F_MOVE
# define SPLICE_F_MOVE 0x01
#endif
#ifndef SPLICE_F_NONBLOCK
# define SPLICE_F_NONBLOCK 0x02
#endif

  static int pfd[2] = {-1, -1};
  static int fdnull = -1;


  char *temp_message_ptr;
  int bytes_left;
  int bytes_recvd;
  int my_recvs;
  int my_flags = SPLICE_F_MOVE | SPLICE_F_NONBLOCK; /* values
						       suggested by
						       Eric Dumazet */
  int ret;

  if (pfd[0] == -1) {
    if (pipe(pfd)) {
      fprintf(where,
	      "%s pipe call failed with errno %d '%s'\n",
	      __FUNCTION__,
	      errno,
	      strerror(errno));
      return -4;  /* this will cause recv_data to do things the
		     old-fashioned way for the test */
    }
    if ((fdnull = open("/dev/null",O_WRONLY)) == -1) {
      fprintf(where,
	      "%s open call failed with errno %d '%s'\n",
	      __FUNCTION__,
	      errno,
	      strerror(errno));
      return -4;
    }
  }

  /* receive data off the data_socket, ass-u-me-ing a blocking socket
     all the way!-) 2008-01-08 */
  my_recvs = 0;
  bytes_left = bytes_to_recv;

  if (debug > 1) {
    fprintf(where,
	    "%s sock %d, ring elt %p, bytes %d, source %p, srclen %d, flags %x, num_recv %p\n",
	    __FUNCTION__,
	    data_socket,
	    recv_ring,
	    bytes_to_recv,
	    source,
	    (source != NULL) ? *sourcelen : -1,
	    flags,
	    num_receives);
    fflush(where);
  }
  do {

    bytes_recvd = splice(data_socket,
			 NULL,
			 pfd[1],
			 NULL,
			 bytes_left,
			 my_flags);


    if (bytes_recvd > 0) {
      /* per Eric Dumazet, we should just let this second splice call
	 move as many bytes as it can and not worry about how much.
	 this should make the call more robust when made on a system
	 under memory pressure */
      splice(pfd[0], NULL, fdnull, NULL, 1 << 30, my_flags);
      bytes_left -= bytes_recvd;
    }
    else {
      break;
    }
    my_recvs++; /* should the pair of splices count as one? */
  } while ((bytes_left > 0) && (flags & NETPERF_WAITALL));

  *num_receives = my_recvs;

  /* OK, we are out of the loop - now what? */
  if (bytes_recvd < 0) {
    /* did the timer hit, or was there an error? */
    if (SOCKET_EINTR(bytes_recvd))
      {
	/* We hit the end of a timed test. */
	return -1;
      }
    /* it was a hard error */
    return -3;
  }


  /* this looks a little funny, but should be correct.  if we had
     NETPERF_WAITALL set and we got here, it means we got all the
     bytes of the request/response.  otherwise we would have hit the
     error or end of test cases.  if NETPERF_WAITALL isn't set, this
     is a STREAM test, and we will have only made one call to recv, so
     bytes_recvd will be accurate. */
  if (bytes_left)
    return bytes_recvd;
  else
    return bytes_to_recv;

}

#endif

static
int recv_pktinfo(SOCKET data_socket, char *message_ptr, int bytes_to_recv,  int my_flags, struct sockaddr *source, netperf_socklen_t *sourcelen) {

#ifdef IP_PKTINFO
//NOTE: Linux's msghdr is very similar to Windows' WSAMSG, but the member names are different :(
#ifndef WIN32
  struct iovec  my_iovec;
  struct msghdr my_header;
#else
  struct _WSAMSG my_header;
  struct _WSABUF my_iovec;
  DWORD BytesRecvd = 0;
  int rc;
#endif
  struct cmsghdr *cmsg;
  struct in_pktinfo *pktinfo;

  char control_buf[512];
  int onoff = 1;
  int ret;

#ifndef WIN32
  my_iovec.iov_base = message_ptr;
  my_iovec.iov_len = bytes_to_recv;

  my_header.msg_name = source;
  my_header.msg_namelen = *sourcelen;
  my_header.msg_iov = &my_iovec;
  my_header.msg_iovlen = 1;
  my_header.msg_control = control_buf;
  my_header.msg_controllen = sizeof(control_buf);
#else
  my_iovec.buf = message_ptr;
  my_iovec.len = bytes_to_recv;

  my_header.name = source;
  my_header.namelen = *sourcelen;
  my_header.lpBuffers = &my_iovec;
  my_header.dwBufferCount = 1;
  my_header.Control.buf = control_buf;
  my_header.Control.len = sizeof(control_buf);
  my_header.dwFlags = 0;
#endif
  /* not going to bother checking, if it doesn't work we are no
     worse-off than we were before. we are going to ignore IPv6 for
     the time being */
  setsockopt(data_socket, IPPROTO_IP, IP_PKTINFO, (char *)&onoff, sizeof(onoff));

#ifndef WIN32
  ret = recvmsg(data_socket, &my_header, 0);
#else
  if (pWSARecvMsg == NULL) pWSARecvMsg = GetWSARecvMsgFunctionPointer();
  rc = pWSARecvMsg(data_socket, &my_header, &BytesRecvd, NULL, NULL);
  //+*+ If rc != 0 we should call WSAGetLastError and print some kind of error msg...
  ret = BytesRecvd;
#endif  //WIN32

  if (ret >= 0) {
    struct sockaddr_in me;
    struct sockaddr_in clear;
    netperf_socklen_t melen = sizeof(me);
    for (cmsg = CMSG_FIRSTHDR(&my_header);
	 cmsg != NULL;
	 cmsg = CMSG_NXTHDR(&my_header, cmsg)) {
      if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO) {
	in_pktinfo = *(struct in_pktinfo *)CMSG_DATA(cmsg);
	have_pktinfo = 1;
      }
    }
  }

  onoff = 0;
  setsockopt(data_socket, IPPROTO_IP, IP_PKTINFO, (char *)&onoff, sizeof(onoff));

  return ret;
#else
  return -1;
#endif
}


int
recv_data(SOCKET data_socket, struct ring_elt *recv_ring, uint32_t bytes_to_recv, struct sockaddr *source, netperf_socklen_t *sourcelen, uint32_t flags, uint32_t *num_receives) {

  char *temp_message_ptr;
  int bytes_left;
  int bytes_recvd;
  int my_recvs;
  int my_flags = 0; /* will we one day want to set MSG_WAITALL? */

#if defined(__linux)
  int ret;
  if (loc_rcvavoid == 1) {
    ret = recv_data_no_copy(data_socket, recv_ring, bytes_to_recv, source, sourcelen, flags, num_receives);
    if (ret != -4)
      return ret;
    else
      loc_rcvavoid = 0;
  }
#endif

  /* receive data off the data_socket, ass-u-me-ing a blocking socket
     all the way!-) 2008-01-08 */
  my_recvs = 0;
  bytes_left = bytes_to_recv;
  temp_message_ptr  = recv_ring->buffer_ptr;

  if (debug > 1) {
    fprintf(where,
	    "%s sock %d, ring elt %p, bytes %d, source %p, srclen %d, flags %x, num_recv %p\n",
	    __FUNCTION__,
	    data_socket,
	    recv_ring,
	    bytes_to_recv,
	    source,
	    (source != NULL) ? *sourcelen : -1,
	    flags,
	    num_receives);
    fflush(where);
  }
  do {
    if (source) {
      /* call recvfrom it does look a little silly here inside the do
	 while, but I think it is ok - a UDP or other DGRAM or
	 SEQPACKET (?) socket, which should be the only time we
	 pass-in a source pointer will have a semantic that should get
	 us out of the dowhile on the first call anyway.  if it
	 turns-out not to be the case, then we can hoist the if above
	 the do and put the dowhile in the else. */
      if (use_pktinfo) {
	bytes_recvd = recv_pktinfo(data_socket,
				   temp_message_ptr,
				   bytes_left,
				   my_flags,
				   source,
				   sourcelen);
	use_pktinfo = 0;
      }
      else {
	bytes_recvd = recvfrom(data_socket,
			       temp_message_ptr,
			       bytes_left,
			       my_flags,
			       source,
			       sourcelen);
      }
    }
    else {
      /* just call recv */
      bytes_recvd = recv(data_socket,
			 temp_message_ptr,
			 bytes_left,
			 my_flags);
    }
    if (bytes_recvd > 0) {
      bytes_left -= bytes_recvd;
      temp_message_ptr += bytes_recvd;
    }
    else {
      break;
    }
    my_recvs++;
  } while ((bytes_left > 0) && (flags & NETPERF_WAITALL));

  *num_receives = my_recvs;

  /* OK, we are out of the loop - now what? */
  if (bytes_recvd < 0) {
    /* did the timer hit, or was there an error? */
    if (SOCKET_EINTR(bytes_recvd))
      {
	/* We hit the end of a timed test. */
	return -1;
      }
    if (SOCKET_EAGAIN(bytes_recvd) ||
	SOCKET_EWOULDBLOCK(bytes_recvd)) {
      return -2;
    }
    /* it was a hard error */
    return -3;
  }


  /* this looks a little funny, but should be correct.  if we had
     NETPERF_WAITALL set and we got here, it means we got all the
     bytes of the request/response.  otherwise we would have hit the
     error or end of test cases.  if NETPERF_WAITALL isn't set, this
     is a STREAM test, and we will have only made one call to recv, so
     bytes_recvd will be accurate. */
  if (bytes_left)
    return bytes_recvd;
  else
    return bytes_to_recv;

}


int
close_data_socket(SOCKET data_socket, struct sockaddr *peer, int peerlen, int protocol)
{

  int ret;
  char buffer[4];

  if (debug) {
    fprintf(where,
	    "%s sock %d peer %p peerlen %d protocol %d\n",
	    __FUNCTION__,
	    data_socket,
	    peer,
	    peerlen,
	    protocol);
    fflush(where);
  }

  if (protocol == IPPROTO_UDP) {
    /* try to give the remote a signal. what this means if we ever
       wanted to actually send zero-length messages remains to be seen
       :)  */
    int i;
    for (i = 0; i < 3; i++) {
      if (peer)
	ret = sendto(data_socket,
		     buffer,
		     0,
		     0,
		     peer,
		     peerlen);
      else
	ret = send(data_socket,
		   buffer,
		   0,
		   0);
      if (SOCKET_EINTR(ret)) {
	close(data_socket);
	return -1;
      }
    }
  }
  ret = close(data_socket);

  if (SOCKET_EINTR(ret)) {
    /* end of test */
    return -1;
  }
  else if (ret == 0) {
    return ret;
  }
  else
    return -3;

}

int
disconnect_data_socket(SOCKET data_socket, int initiate, int do_close, struct sockaddr *peer, int peerlen)
{

  char buffer[4];
  int bytes_recvd;

  if (debug) {
    fprintf(where,
	    "%s sock %d init %d do_close %d protocol %d\n",
	    __FUNCTION__,
	    data_socket,
	    initiate,
	    do_close,
	    protocol);
    fflush(where);
  }

  /* at some point we'll need to abstract this a little.  for now, if
     the protocol is UDP, we try to send some number of zero-length
     datagrams to allow the remote to get out of its loop without
     having to wait for the padded timer to expire. if it isn't UDP,
     we assume a reliable connection and can do the usual graceful
     shutdown thing */

  /* this needs to be revisited for the netperf receiving case when
     the test is terminated by a Ctrl-C.  raj 2012-01-24 */

  if (protocol != IPPROTO_UDP) {
    if (initiate)
      shutdown(data_socket, SHUT_WR);

    /* we are expecting to get either a return of zero indicating
       connection close, or an error. of course, we *may* never
       receive anything from the remote which means we probably really
       aught to have a select here but until we are once bitten we
       will remain twice bold. */
    bytes_recvd = recv(data_socket,
		       buffer,
		       1,
		       0);

    if (bytes_recvd != 0) {
      /* connection close, call close. we assume that the requisite
         number of bytes have been received */
      if (SOCKET_EINTR(bytes_recvd))
	{
	  /* We hit the end of a timed test. */
	  return -1;
	}
      return -3;
    }
  }
  else {
    int i;
    for (i = 0; i < 3; i++) {
      if (peer)
	bytes_recvd = sendto(data_socket,
			     buffer,
			     0,
			     0,
			     peer,
			     peerlen);
      else
	bytes_recvd = send(data_socket,
			   buffer,
			   0,
			   0);
      /* we only really care if the timer expired on us */
      if (SOCKET_EINTR(bytes_recvd)) {
	if (do_close)
	  close(data_socket);
	return -1;
      }
    }
  }

  if (do_close)
    close(data_socket);

  return 0;
}

#ifdef HAVE_LINUX_TCP_H
static void
dump_tcp_info(struct tcp_info *tcp_info)
{

  fprintf(stderr,
	  "tcpi_rto %d tcpi_ato %d tcpi_pmtu %d tcpi_rcv_ssthresh %d\n"
	  "tcpi_rtt %d tcpi_rttvar %d tcpi_snd_ssthresh %d tpci_snd_cwnd %d\n"
	  "tcpi_reordering %d tcpi_total_retrans %d\n",
	  tcp_info->tcpi_rto,
	  tcp_info->tcpi_ato,
	  tcp_info->tcpi_pmtu,
	  tcp_info->tcpi_rcv_ssthresh,
	  tcp_info->tcpi_rtt,
	  tcp_info->tcpi_rttvar,
	  tcp_info->tcpi_snd_ssthresh,
	  tcp_info->tcpi_snd_cwnd,
	  tcp_info->tcpi_reordering,
	  tcp_info->tcpi_total_retrans);

  return;
}

#endif

static int
get_transport_retrans(SOCKET socket, int protocol) {

#ifdef HAVE_LINUX_TCP_H
  struct tcp_info tcp_info;

  int ret;
  netperf_socklen_t infosize;

  if (protocol != IPPROTO_TCP)
    return -1;

  infosize = sizeof(struct tcp_info);

  if ((ret = getsockopt(socket,protocol,TCP_INFO,&tcp_info,&infosize)) < 0) {
    if (debug) {
      fprintf(where,
	      "%s: getsockopt errno %d %s\n",
	      __FUNCTION__,
	      errno,
	      strerror(errno));
      fflush(where);
    }
    return -1;
  }
  else {

    /* we assume that if we have LINUX_TCP_H we also have getenv */
    if (debug > 1 || getenv("DUMP_TCP_INFO")) {
      dump_tcp_info(&tcp_info);
    }
    return tcp_info.tcpi_total_retrans;
  }


#else
  return -1;
#endif
}


static void
get_transport_info(SOCKET socket, int *mss, int protocol)
{

  netperf_socklen_t sock_opt_len;
  int option;
  sock_opt_len = sizeof(netperf_socklen_t);

  switch (protocol) {
#if defined(IPPROTO_TCP) && defined(TCP_MAXSEG)
  case IPPROTO_TCP:
    option = TCP_MAXSEG;
    break;
#endif

#if defined(IPPROTO_SCTP) && defined(SCTP_MAXSEG)
  case IPPROTO_SCTP:
    option = SCTP_MAXSEG;
    break;
#endif
  default:
    *mss = -1;
    return;
  }

  if (getsockopt(socket,
		 protocol,
		 option,
		 (char *)mss,
		 &sock_opt_len) == SOCKET_ERROR) {
    fprintf(where,
	    "%s: getsockopt: errno %d\n",
	    __FUNCTION__,
	    errno);
    fflush(where);
    *mss = -1;
  }

}

static void
get_transport_cong_control(SOCKET socket, int protocol, char cong_control[], int len)
{
#ifdef TCP_CONGESTION
  int my_len = len;
  if (protocol != IPPROTO_TCP) {
    strncpy(cong_control,"TCP Only",len);
  }
  else if (getsockopt(socket,
		      protocol, TCP_CONGESTION, cong_control, &my_len) ==
	   SOCKET_ERROR) {
    snprintf(cong_control,len,"%d errno",errno);
  }
#else
  strncpy(cong_control,"Unavailable",len);
#endif
  cong_control[len-1] = '\0';
}

static void
set_transport_cong_control(SOCKET socket, int protocol, char cong_control[], int len)
{
#ifdef TCP_CONGESTION
  if (protocol == IPPROTO_TCP) {
    /* if it fails, we'll pick that up via the subsequent "get" */
    setsockopt(socket, protocol, TCP_CONGESTION, cong_control, len);
  }
#endif
}

static void
set_receive_timeout(SOCKET sock, int timeout)
{
#ifdef SO_RCVTIMEO
#ifndef WIN32
  struct timeval foo;

  foo.tv_sec = timeout;
  foo.tv_usec = 0;

  if (setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&foo,sizeof(foo)) < 0) {
    if (debug) {
      fprintf(where,"Note - attempt to set a receive timeout on the data socket failed with errno %d (%s)\n",
	      errno,
	      strerror(errno));
      fflush(where);
    }
  }
#endif
#endif
}

static SOCKET
omni_create_data_socket(struct addrinfo *res)
{
  SOCKET temp_socket;

  temp_socket = create_data_socket(res);

  if (temp_socket != SOCKET_ERROR) {
    if (local_cong_control_req[0] != '\0') {
      set_transport_cong_control(temp_socket,
				 res->ai_protocol,
				 local_cong_control_req,
				 sizeof(local_cong_control_req));
    }

    if ((res->ai_protocol == IPPROTO_UDP) &&
	(receive_timeout != -1)) {
      set_receive_timeout(temp_socket, receive_timeout);
    }

    if (socket_debug) {
      int one = 1;
      setsockopt(temp_socket,
		 SOL_SOCKET,
		 SO_DEBUG,
		 (char *)&one,
		 sizeof(one));
    }

#ifdef IP_MTU_DISCOVER
    if (use_mtu_discover) {
      int type = IP_PMTUDISC_DO;
      setsockopt(temp_socket,
		 SOL_IP,
		 IP_MTU_DISCOVER,
		 (char *)&type,
		 sizeof(type));
    }
#endif
  }
  return temp_socket;
}
/* choosing the default send size is a trifle more complicated than it
   used to be as we have to account for different protocol limits */

#define UDP_LENGTH_MAX (0xFFFF - 28)

static int
choose_send_size(int lss, int protocol) {

  int send_size;

  if (lss > 0) {
    send_size = lss_size;

    /* we will assume that everyone has IPPROTO_UDP and thus avoid an
       issue with Windows using an enum */
    if ((protocol == IPPROTO_UDP) && (send_size > UDP_LENGTH_MAX))
      send_size = UDP_LENGTH_MAX;

  }
  else {
    send_size = 4096;
  }
  return send_size;
}

/* brain dead simple way to get netperf to emit a uuid. sadly, by this
   point we will have already established the control connection but
   those are the breaks. we do _NOT_ include a trailing newline
   because we want to be able to use this in a script */

void
print_uuid(char remote_host[])
{
  printf("%s",test_uuid);
}
#if defined(__linux)
/*
 * Linux has this odd behavior where if the socket buffers are larger
 * than a device's txqueuelen, the kernel will silently drop transmits
 * which would not fit into the tx queue, and not pass an ENOBUFS
 * error back to the application.  As a result, a UDP stream test can
 * report absurd transmit bandwidths (like 20Gb/s on a 1GbE NIC).
 * This behavior can be avoided if you request extended error
 * reporting on the socket.  This is done by setting the IP_RECVERR
 * socket option at the IP level.
 */
static void
enable_enobufs(int s)
{
  int on = 1;

  /* Per Brian Ginsbach, the likes of SLES12 do not have "ip" listed
     in /etc/protocols and other distros may not have the correct
     value.  At his suggestion, we'll just go with IPPROTO_IP.  And
     while I am here, perhaps we should be trying IPPROTO_IPV6 if that
     fails */
  if (setsockopt(s, IPPROTO_IP, IP_RECVERR, (char *)&on, sizeof(on)) < 0) {
    if (setsockopt(s, IPPROTO_IPV6, IPV6_RECVERR, (char *)&on, sizeof(on)) < 0) {
      fprintf(where, "%s failed: setsockopt (errno %d)\n",__FUNCTION__,errno);
      fflush(where);
      return;
    }
  }
}
#endif

void
set_omni_request_flags(struct omni_request_struct *omni_request) {

      /* we have no else clauses here because we previously set flags
	 to zero above raj 20090803 */
      if (rem_nodelay)
	omni_request->flags |= OMNI_NO_DELAY;

      if (remote_use_sendfile)
	omni_request->flags |= OMNI_USE_SENDFILE;

      if (connection_test)
	omni_request->flags |= OMNI_CONNECT_TEST;

      if (remote_checksum_off)
	omni_request->flags |= OMNI_CHECKSUM_OFF;

      if (remote_cpu_usage)
	omni_request->flags |= OMNI_MEASURE_CPU;

      if (routing_allowed)
	omni_request->flags |= OMNI_ROUTING_ALLOWED;

      if (desired_output_groups & OMNI_WANT_REM_IFNAME)
	omni_request->flags |= OMNI_WANT_IFNAME;

      if (desired_output_groups & OMNI_WANT_REM_IFSLOT)
	omni_request->flags |= OMNI_WANT_IFSLOT;

      if (desired_output_groups & OMNI_WANT_REM_IFIDS)
	omni_request->flags |= OMNI_WANT_IFIDS;

      if (desired_output_groups & OMNI_WANT_REM_DRVINFO)
	omni_request->flags |= OMNI_WANT_DRVINFO;

      if (desired_output_groups & OMNI_WANT_REM_CONG)
	omni_request->flags |= OMNI_WANT_REM_CONG;

      if (use_fastopen)
	omni_request->flags |= OMNI_FASTOPEN;

      if (want_use_pktinfo)
	omni_request->flags |= OMNI_USE_PKTINFO;

      if (want_defer_accept)
	omni_request->flags |= OMNI_WANT_DEFER_ACCEPT;

      if (remote_connected)
	omni_request->flags |= OMNI_USE_CONNECTED;

      if (use_mtu_discover)
	omni_request->flags |= OMNI_USE_MTU_DISCOVER;
}


/* this code is intended to be "the two routines to run them all" for
   BSDish sockets.  it comes about as part of a desire to shrink the
   code footprint of netperf and to avoid having so many blessed
   routines to alter as time goes by.  the downside is there will be
   more "ifs" than there were before. raj 2008-01-07 */

void
send_omni_inner(char remote_host[], unsigned int legacy_caller, char header_str[])
{

  int ret,rret;
  int connected = 0;
  int timed_out = 0;
  int pad_time = 0;

  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  struct sockaddr_storage remote_addr;
  struct sockaddr_storage my_addr;
  int                     remote_addr_len = sizeof(remote_addr);
  netperf_socklen_t       my_addr_len = sizeof(my_addr);

  SOCKET	data_socket;
  int           need_socket;

  uint32_t   temp_recvs;
  char  tmpfmt;

  struct addrinfo *local_res;
  struct addrinfo *remote_res;

  struct	omni_request_struct	*omni_request;
  struct	omni_response_struct	*omni_response;
  struct	omni_results_struct	*omni_result;

#ifdef WANT_FIRST_BURST
  int requests_outstanding = 0;
#endif

  omni_request =
    (struct omni_request_struct *)netperf_request.content.test_specific_data;
  omni_response =
    (struct omni_response_struct *)netperf_response.content.test_specific_data;
  omni_result =
    (struct omni_results_struct *)netperf_response.content.test_specific_data;


  if (keep_histogram) {
    if (first_burst_size > 0)
      time_hist = HIST_new_n(first_burst_size + 1);
    else
      time_hist = HIST_new_n(1);
  }

  /* since we are now disconnected from the code that established the
     control socket, and since we want to be able to use different
     protocols and such, we are passed the name of the remote host and
     must turn that into the test specific addressing information. */

  complete_addrinfos(&remote_res,
		     &local_res,
		     remote_host,
		     socket_type,
		     protocol,
		     0);

  if ( print_headers ) {
    print_top_test_header(header_str,local_res,remote_res);
  }

  /* initialize a few counters */

  need_socket   = 1;

  if (connection_test)
    pick_next_port_number(local_res,remote_res);


  /* If the user has requested cpu utilization measurements, we must
     calibrate the cpu(s). We will perform this task within the tests
     themselves. If the user has specified the cpu rate, then
     calibrate_local_cpu will return rather quickly as it will have
     nothing to do. If local_cpu_rate is zero, then we will go through
     all the "normal" calibration stuff and return the rate back.*/

  if (local_cpu_usage) {
    local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
  }

  confidence_iteration = 1;
  init_stat();

  send_ring = NULL;
  recv_ring = NULL;

  /* you will keep running the test until you get it right! :) */
  while (((confidence < 0) && (confidence_iteration <= iteration_max)) ||
	 (confidence_iteration <= iteration_min)) {

    trans_completed = 0;
    bytes_xferd	= 0.0;
    remote_bytes_xferd = 0.0;
    times_up 	= 0;
    bytes_sent = 0;
    bytes_received = 0;
    local_send_calls = 0;
    local_receive_calls = 0;

    /* since we are tracking the number of outstanding requests for
       timestamping purposes, and since the previous iteration of
       using confidence intervals may not have completed all of them,
       we now need to forget about them or we will mistakenly fill our
       tracking array. raj 2011-03-14 */
    if (keep_histogram) {
      HIST_purge(time_hist);
    }

#ifdef WANT_FIRST_BURST
    /* we have to remember to reset the number of transactions
       outstanding and the "congestion window for each new
       iteration. raj 2006-01-31. */
    requests_outstanding = 0;
#endif

    /* if the command-line included requests to randomize the IP
       addresses, then honor it.  of course, this may not work all that
       well for some tests... raj 20101129 */
    if (local_mask_len)
      random_ip_address(local_res, local_mask_len);
    if (remote_mask_len)
      random_ip_address(remote_res, remote_mask_len);

    data_socket = omni_create_data_socket(local_res);

    if (data_socket == INVALID_SOCKET) {
      perror("netperf: send_omni: unable to create data socket");
      exit(1);
    }
#if defined(__linux)
    /* we really only need this for a UDP_STREAM test. we particularly
       do not want it for a CC or CRR test. raj 2012-08-06 */
    if ((protocol == IPPROTO_UDP) &&
	NETPERF_XMIT_ONLY(direction)) {
      enable_enobufs(data_socket);
    }
#endif
    need_socket = 0;

    /* we need to consider if this is a request/response test, if we
       are receiving, if we are sending, etc, when setting-up our recv
       and send buffer rings. we should only need to do this once, and
       that would be when the relevant _ring variable is NULL. raj
       2008-01-18 */
    if (direction & NETPERF_XMIT) {
      if (is_multicast_addr(remote_res)) {
	set_multicast_ttl(data_socket);
      }

      if (NULL == send_ring) {
	if (req_size > 0) {
	  /* request/response test */
	  if (send_width == 0) send_width = 1;
	  bytes_to_send = req_size;
	}
	else {
	  /* stream test */
	  if (send_size == 0) {
	    send_size = choose_send_size(lss_size,protocol);
	  }
	  if (send_width == 0)
	    send_width = (lss_size/send_size) + 1;
	  if (send_width == 1) send_width++;
	  bytes_to_send = send_size;
	}

	send_ring = allocate_buffer_ring(send_width,
					 bytes_to_send,
					 local_send_align,
					 local_send_offset);
	if (debug) {
	  fprintf(where,
		  "%s: %d entry send_ring obtained...\n",
		  __FUNCTION__,
		  send_width);
	}
      }
    }

    if (direction & NETPERF_RECV) {
      /* do we need to join a multicast group? */
      if (is_multicast_addr(local_res)) {
	join_multicast_addr(data_socket, local_res);
      }

      /* do we need to allocate a recv_ring? */
      if (NULL == recv_ring) {
	if (rsp_size > 0) {
	  if (recv_width == 0) recv_width = 1;
	  bytes_to_recv = rsp_size;
	}
	else {
	  /* stream test */
	  if (recv_size == 0) {
	    if (lsr_size > 0) {
	      recv_size = lsr_size;
	    }
	    else {
	      recv_size = 4096;
	    }
	  }
	  if (recv_width == 0) {
	    recv_width = (lsr_size/recv_size) + 1;
	    if (recv_width == 1) recv_width++;
	  }
	  bytes_to_recv = recv_size;
	}

	recv_ring = allocate_buffer_ring(recv_width,
					 bytes_to_recv,
					 local_recv_align,
					 local_recv_offset);
	if (debug) {
	  fprintf(where,
		  "%s: %d entry recv_ring obtained...\n",
		  __FUNCTION__,
		  recv_width);
	}
      }
    }

    if (!no_control) {

      /* Tell the remote end to do a listen or otherwise prepare for
	 what is to come. The server alters the socket paramters on the
	 other side at this point, hence the reason for all the values
	 being passed in the setup message. If the user did not specify
	 any of the parameters, they will be passed as values which will
	 indicate to the remote that no changes beyond the system's
	 default should be used. Alignment is the exception, it will
	 default to 8, which will probably be no alignment
	 alterations. */

      netperf_request.content.request_type = DO_OMNI;
      omni_request->flags                  = 0;
      omni_request->send_buf_size	   = rss_size_req;
      omni_request->send_size              = remote_send_size_req;
      omni_request->send_alignment	   = remote_send_align;
      omni_request->send_offset	           = remote_send_offset;
      omni_request->send_width             = send_width;
      omni_request->request_size	   = req_size;

      omni_request->recv_buf_size	   = rsr_size_req;
      omni_request->receive_size           = remote_recv_size_req;
      omni_request->recv_alignment	   = remote_recv_align;
      omni_request->recv_offset	           = remote_recv_offset;
      omni_request->recv_width             = recv_width;
      omni_request->response_size	   = rsp_size;
      omni_request->socket_prio            = remote_socket_prio;
      omni_request->socket_tos             = remote_socket_tos;

      set_omni_request_flags(omni_request);

      /* perhaps this should be made conditional on
	 remote_cong_control_req[0] not being NULL? */
      strncpy(omni_request->cong_control,
	      remote_cong_control_req,
	      sizeof(omni_request->cong_control));
      omni_request->cong_control[sizeof(omni_request->cong_control) - 1] =
	'\0';

      if (want_keepalive)
	omni_request->flags |= OMNI_WANT_KEEPALIVE;

      omni_request->cpu_rate	           = remote_cpu_rate;
      if (test_time)
	omni_request->test_length	   = test_time;
      else
	omni_request->test_length	   = test_trans * -1;
      omni_request->so_rcvavoid	           = rem_rcvavoid;
      omni_request->so_sndavoid	           = rem_sndavoid;
      omni_request->send_dirty_count       = rem_dirty_count;
      omni_request->recv_dirty_count       = rem_dirty_count;
      omni_request->recv_clean_count       = rem_clean_count;

      omni_request->data_port              = atoi(remote_data_port);
      omni_request->ipfamily               = af_to_nf(remote_res->ai_family);
      omni_request->socket_type            = hst_to_nst(socket_type);
      omni_request->protocol               = protocol;

      omni_request->interval_burst         = remote_interval_burst;
      omni_request->interval_usecs         = remote_interval_usecs;

      omni_request->direction              = 0;
      /* yes, the sense here is correct - if we are transmitting, they
	 receive, if we are receiving, they are transmitting... */
      if (direction & NETPERF_XMIT)
	omni_request->direction |= NETPERF_RECV;
      if (direction & NETPERF_RECV)
	omni_request->direction |= NETPERF_XMIT;

      /* some tests may require knowledge of our local addressing. such
	 tests will for the time being require that the user specify a
	 local IP/name so we can extract them from the data_socket. */
      getsockname(data_socket, (struct sockaddr *)&my_addr, &my_addr_len);

      ret = get_sockaddr_family_addr_port(&my_addr,
					  nf_to_af(omni_request->ipfamily),
					  omni_request->netperf_ip,
					  &(omni_request->netperf_port));
      ret = get_sockaddr_family_addr_port((struct sockaddr_storage *)remote_res->ai_addr,
					  nf_to_af(omni_request->ipfamily),
					  omni_request->netserver_ip,
					  &(omni_request->data_port));
      /* if the user didn't explicitly set the remote data address we
	 don't want to pass along the one we picked implicitly, or a
	 netserver sitting behind a (BLETCH) NAT will be asked to try
	 to bind to the "public" IP. */
      if (!explicit_data_address) {
	omni_request->netserver_ip[0] = 0;
	omni_request->netserver_ip[1] = 0;
	omni_request->netserver_ip[2] = 0;
	omni_request->netserver_ip[3] = 0;
      }
      if (debug > 1) {
	fprintf(where,"netperf: %s: requesting OMNI test\n",__FUNCTION__);
      }

      strncpy(omni_request->fill_file,
	      remote_fill_file,
	      sizeof(omni_request->fill_file));

      send_request_n(OMNI_REQUEST_CONV_CUTOFF);


      /* the response from the remote should contain all the relevant
	 socket and other parameters we need to know for this test.
	 so, we can shove them back into the relevant variables here
	 and be on our way. */

      recv_response_n(OMNI_RESPONSE_CONV_CUTOFF); /* brittle, but functional */

      if (!netperf_response.content.serv_errno) {
	rsr_size	    = omni_response->recv_buf_size;
	remote_recv_size    = omni_response->receive_size;
	rss_size	    = omni_response->send_buf_size;
	remote_send_size    = omni_response->send_size;
	rem_nodelay         = omni_response->flags & OMNI_NO_DELAY;
	remote_use_sendfile = omni_response->flags & OMNI_USE_SENDFILE;
	remote_cpu_usage    = omni_response->flags & OMNI_MEASURE_CPU;
	remote_cpu_rate     = omni_response->cpu_rate;
	remote_send_width   = omni_response->send_width;
	remote_recv_width   = omni_response->recv_width;
	remote_socket_prio  = omni_response->socket_prio;
	remote_socket_tos   = omni_response->socket_tos;

	/* make sure that port numbers are in network order because
	   recv_response will have put everything into host order */
	set_port_number(remote_res,
			(unsigned short)omni_response->data_port);

	if (debug) {
	  fprintf(where,"remote listen done.\n");
	  fprintf(where,"remote port is %u\n",get_port_number(remote_res));
	  fflush(where);
	}
      }
      else {
	Set_errno(netperf_response.content.serv_errno);
	fprintf(where,
		"netperf: remote error %d",
		netperf_response.content.serv_errno);
	perror("");
	fflush(where);
	exit(-1);
      }

    }

#ifdef WANT_DEMO
    /* at some point we will have to be more clever about this, but
       for now we won't */

    demo_rr_setup(100);
#endif

    /* if we are not a connectionless protocol, we need to connect. at
       some point even if we are a connectionless protocol, we may
       still want to "connect" for convenience raj 2008-01-14 */
    need_to_connect = (protocol != IPPROTO_UDP) || local_connected;

    /* possibly wait just a moment before actually starting - used
       mainly when one is doing many many many concurrent netperf
       tests */
    WAIT_BEFORE_DATA_TRAFFIC();

    /* Set-up the test end conditions. For tests over a
       "reliable/connection-oriented" transport (eg TCP, SCTP, etc)
       this can be either time or byte/transaction count based.  for
       unreliable transport or connection tests it can only be time
       based.  having said that, we rely entirely on other code to
       enforce this before we even get here. raj 2008-01-08 */

    /* enable a test_time of 0 to mean just keep running until
       something other than alarm() generates a signal. raj
       2012-02-01 */
    if ((test_time) || ((test_trans == 0) && (test_bytes == 0))) {
      /* The user wanted to end the test after a period of time.  if
	 we are a recv-only test, we need to protect ourself against
	 the remote going poof, but we want to make sure we don't
	 give-up before they finish, so we will add a PAD_TIME to the
	 timer.  if we are RR or XMIT, there should be no need for
	 padding */
      times_up = 0;
      units_remaining = 0;
      if ((!no_control) &&
	  (NETPERF_RECV_ONLY(direction)) &&
	  ((test_trans == 0) && (test_bytes == 0)))
	    pad_time = 0;
      start_timer(test_time + pad_time);
    }
    else {
      /* The tester wanted to send a number of bytes or exchange a
	 number of transactions. */
      if (NETPERF_IS_RR(direction))
	units_remaining = test_trans;
      else
	units_remaining = test_bytes;
      times_up = 1;
    }

    /* grab the current time, and if necessary any starting information
       for the gathering of CPU utilization at this end. */
    cpu_start(local_cpu_usage);

#if defined(WANT_INTERVALS)
    INTERVALS_INIT();
#endif /* WANT_INTERVALS */

#ifdef WANT_DEMO
    if (demo_mode) {
      demo_first_timestamp();
    }
#endif

    /* the "OR" here allows us to control test length by either
       byte/transaction count or by timer.  when the test is
       byte/transaction count based the time test will always evaluate
       false. when the test is controlled by time, the byte/transaction
       count will always evaluate to false.  when the test is finished
       the whole expression will go false and we will stop sending
       data. at least that is the plan :)  raj 2008-01-08 */

    while ((!times_up) || (units_remaining > 0)) {

      /* we need to be careful about when we snap a timestamp
	 depending on the test parameters. this one *should* cover
	 everything but the burst request/response test - famous last
	 words of course. raj 20110111 */

      if (keep_histogram) {
	HIST_timestamp_start(time_hist);
      }


    again:

      if (need_socket) {
	if (connection_test)
	  pick_next_port_number(local_res,remote_res);

	data_socket = omni_create_data_socket(local_res);

	if (data_socket == INVALID_SOCKET) {
	  perror("netperf: send_omni: unable to create data socket");
	  exit(1);
	}
	need_socket = 0;
#if defined(__linux)
	if ((protocol == IPPROTO_UDP) &&
	    (direction & NETPERF_XMIT)) {
	  enable_enobufs(data_socket);
	}
#endif
      }

      /* only connect if and when we need to */
      if (need_to_connect && !use_fastopen) {
	/* assign to data_socket since connect_data_socket returns
	   SOCKET and not int thanks to Windows. */
	ret = connect_data_socket(data_socket,remote_res,dont_give_up);
	if (ret == 0) {
	  connected = 1;
	  need_to_connect = 0;
	}
	else if (ret == -1) {
	  times_up = 1;
	  timed_out = 1;
	  break;
	}
	else if ((ret == -2) && connection_test) {
	  /* transient error  on a connection test means go around and
	     try again with another local port number */
	  if (debug) {
	    fprintf(where,"transient! transient! torpedo in the water!\n");
	    fflush(where);
	  }
	  close(data_socket);
	  connected = 0;  /* probably redundant but what the heck... */
	  need_socket = 1;
	  need_to_connect = 1;
	  /* this will stuff the next local port number within bounds
	     into our local res, and then when the goto has us
	     allocating a new socket it will do the right thing with the
	     bind() call */
	  pick_next_port_number(local_res,remote_res);
	  /* yes Virginia, a goto.  perhaps one day we will rewrite
	     the code to avoid it but for now, a goto... raj */
	  goto again;
	}
	else {
	  /* either this was a hard failure (-3) or a soft failure on
	     something other than a connection test */
	  perror("netperf: send_omni: connect_data_socket failed");
	  exit(1);
	}
      }

#ifdef WANT_FIRST_BURST
      /* Long ago and far away, on just about any *nix, one could
	 avoid having multiple requests bundled into the same TCP
	 segment simply by setting TCP_NODELAY and perhaps not trying
	 to have more outstanding at one time than our guesstimate as
	 to the TCP congestion window.  In that way one could use a
	 burst mode TCP_RR test as part of trying to measure maximum
	 packets per second (PPS) on a system or through a NIC (well,
	 assuming there weren't many retransmissions anyway) These
	 days with Linux the dominant *nix and with it having made it
	 virtually impossible to do any longer, it is no longer worth
	 it to try the application-layer backflips.  So, I am removing
	 them.  At some point we'll simply have to enhance this code
	 to deal with multiple connections at one time, each with just
	 the one transaction in flight for our PPS testing.  Multiple
	 netperfs, each with one connection and one transaction in
	 flight rapidly becomes a context-switching benchmark rather
	 than "networking".  raj 2015-04-20 */

      while ((first_burst_size > 0) &&
	     (requests_outstanding < first_burst_size) &&
	     (NETPERF_IS_RR(direction)) &&
	     (!connection_test)) {
	if (debug > 1) {
	  fprintf(where,
		  "injecting, req_outstanding %d burst %d\n",
		  requests_outstanding,
		  first_burst_size);
	}

	if ((ret = send_data(data_socket,
			     send_ring,
			     bytes_to_send,
			     (connected) ? NULL : remote_res->ai_addr,
			     remote_res->ai_addrlen,
			     protocol)) != bytes_to_send) {
	  /* in theory, we should never hit the end of the test in the
	     first burst.  however, in practice I have indeed seen
	     some ENOBUFS happen in some aggregate tests.  so we need
	     to be a bit more sophisticated in how we handle it. raj
	     20130516 */
	  if (ret != -2) {
	    perror("send_omni: initial burst data send error");
	    exit(-1);
	  }
	  failed_sends++;
	}
	else {
	  local_send_calls += 1;
	  requests_outstanding += 1;
	}

	/* yes, it seems a trifle odd having this *after* the send()
	   just above, but really this is for the next send() or
	   recv() call below or in the iteration of this loop, and the
	   first HIST_timestamp_start() call at the top of the
	   outermost loop will be for the first send() call here in
	   the burst code.  clear ain't it?-) raj 20110111 */

	if (keep_histogram) {
	  HIST_timestamp_start(time_hist);
	}
      }

#endif /* WANT_FIRST_BURST */

      /* if we should try to send something, then by all means, let us
	 try to send something. */
      if (direction & NETPERF_XMIT) {

	ret = send_data(data_socket,
			send_ring,
			bytes_to_send,
			(connected) ? NULL : remote_res->ai_addr,
			/* if the destination above is NULL, this is ignored */
			remote_res->ai_addrlen,
			protocol);
	/* the order of these if's will seem a triffle strange, but they
	   are my best guess as to order of probabilty and/or importance
	   to the overhead raj 2008-01-09*/
	if (ret == bytes_to_send) {
	  /* if this is a send-only test controlled by byte count we
	     decrement units_remaining by the bytes sent */
	  if (!(direction & NETPERF_RECV) && (units_remaining > 0)) {
	    units_remaining -= ret;
	  }
	  bytes_sent += ret;
	  send_ring = send_ring->next;
	  local_send_calls++;
	}
	else if (ret == -2) {
	  /* what to do here -2 means a non-fatal error - probably
	     ENOBUFS and so our send didn't happen.  in the old code for
	     UDP_STREAM we would just continue in the while loop.  it
	     isn't clear that is what to do here, so we will simply
	     increment the failed_sends stat and fall-through. If this
	     is a UDP_STREAM style of test, the net effect should be the
	     same. if this is a UDP_RR with a really-big burst count, I
	     don't think we were checking for ENOBUFS there anyway and
	     so would have failed.  Here we can just let things
	     slide. */
	  failed_sends++;
	}
	else if (ret == 0) {
	  /* was this a zero-byte send? if it was, then ostensibly we
	     would hit the ret == bytes_to_send case which means we'd
	     never get here as we are using blocking semantics */
	  fprintf(where,"HOW DID I GET HERE?\n");
	  fflush(where);
	}
	else if (ret == -1) {
	  times_up = 1;
	  timed_out = 1;
	  break;
	}
	else {
	  perror("netperf: send_omni: send_data failed");
	  exit(1);
	}

      }

#ifdef WANT_FIRST_BURST
      /* it isn't clear we need to check the directions here.  the
	 increment should be cheaper than the conditional, and it
	 shouldn't hurt the other directions because they'll never
	 look at them. famous last words of raj 2008-01-25 */
      requests_outstanding += 1;
#endif

#ifdef WIN32
      /* this is used so the timer thread can close the socket out
	from under us, which to date is the easiest/cleanest/least
	Windows-specific way I can find to force the winsock calls to
	return WSAEINTR with the test is over. anything that will run
	on 95 and NT and is closer to what netperf expects from Unix
	signals and such would be appreciated raj 1/96 */
      win_kludge_socket = data_socket;
#endif /* WIN32 */

      if (direction & NETPERF_RECV) {
	rret = recv_data(data_socket,
			recv_ring,
			bytes_to_recv,
			(connected) ? NULL : (struct sockaddr *)&remote_addr,
			/* if remote_addr NULL this is ignored */
			&remote_addr_len,
			/* if XMIT also set this is RR so waitall */
			(direction & NETPERF_XMIT) ? NETPERF_WAITALL: 0,
			&temp_recvs);
	if (rret > 0) {
	  /* if this is a recv-only test controlled by byte count we
	     decrement the units_remaining by the bytes received */
	  if (!(direction & NETPERF_XMIT) && (units_remaining > 0)) {
	    units_remaining -= rret;
	  }
	  bytes_received += rret;
	  local_receive_calls += temp_recvs;
	}
	else if (rret == 0) {
	  /* is this the end of a test, just a zero-byte recv, or
	     something else? that is an exceedingly good question and
	     one for which I don't presently have a good answer, but
	     that won't stop me from guessing :) raj 2008-01-09 */
	  if (!((connection_test) || (null_message_ok))) {
	    /* if it is neither a connection_test nor null_message_ok it
	       must be the end of the test */
	    times_up = 1; /* ostensibly the signal handler did this */
	    break;
	  }
	  local_receive_calls += temp_recvs;
	}
	else if (rret == -1) {
	  /* test timed-out */
	  times_up = 1;
	  timed_out = 1;
	  break;
	}
	else if (rret == -2) {
	  /* we timed-out on a data receive.  this is only allowed for
	     a UDP_RR test.  we want to set things up so we start
	     ramping up again like we were at the beginning. if we
	     actually timeout it means that all has been lost.  or at
	     least we assume so */
	  if (debug) {
	    fprintf(where,"Timeout receiving resonse from remote\n");
	    fflush(where);
	  }
#ifdef WANT_FIRST_BURST
	  if (first_burst_size) {
	    requests_outstanding = 0;
	  }
#endif
	  if (keep_histogram) {
	    HIST_purge(time_hist);
	  }
#ifdef WANT_DEMO
	  /* "start over" on a demo interval. we will forget about
	  everything that happened in the demo interval up to the
	  timeout and begin fresh. */
	  demo_reset();
#endif /* WANT_DEMO */

	  continue;
	}
	else {
	  /* anything else is bad */
	  perror("netperf: send_omni: recv_data failed");
	  exit(1);
	}
	recv_ring = recv_ring->next;

#ifdef WANT_FIRST_BURST
	/* so, since we've gotten a response back, update the
	   bookkeeping accordingly.  there is one less request
	   outstanding and we can put one more out there than
	   before. */
	requests_outstanding -= 1;
#endif

      }

      /* if this is a connection test, we want to do some stuff about
	 connection close here in the test loop. raj 2008-01-08 */
      if (connection_test) {

#ifdef __linux
	/* so, "Linux" with autotuning likes to alter the socket buffer
	   sizes over the life of the connection, but only does so when
	   one takes the defaults at time of socket creation.  if we
	   took those defaults, we should inquire as to what the values
	   ultimately became. raj 2008-01-15 */
	/* however annoying having to do this might be, it really
	   shouldn't be done over and over again. instead we will
	   assume it does not change, which is fine since we would
	   have only reported one of them anyway. raj 20100917 */
	if ((lsr_size_req < 0) && (-1 == lsr_size_end))
	  get_sock_buffer(data_socket, RECV_BUFFER, &lsr_size_end);
	else
	  lsr_size_end = lsr_size;
	if ((lss_size_req < 0) && (-1 == lss_size_end))
	  get_sock_buffer(data_socket, SEND_BUFFER, &lss_size_end);
	else
	  lss_size_end = lss_size;
#else
	lsr_size_end = lsr_size;
	lss_size_end = lss_size;
#endif

	/* we will only make this call the one time - after the first
	   call, the value will be real or -1. if this is a connection
	   test we want to do this here because later we won't be
	   connected and the data may no longer be available. */
	if (transport_mss == -2)
	  get_transport_info(data_socket,
			     &transport_mss,
			     local_res->ai_protocol);


	ret = disconnect_data_socket(data_socket,
				     (no_control) ? 1 : 0,
				     1,
				     NULL,
				     0);
	if (ret == 0) {
	  /* we will need a new connection to be established next time
	     around the loop.  However, the next time around the loop
	     will already be picking the next port number */
	  need_to_connect = 1;
	  connected = 0;
	  need_socket = 1;
	}
	else if (ret == -1) {
	  times_up = 1;
	  timed_out = 1;
	  break;
	}
	else {
	  perror("netperf: send_omni: disconnect_data_socket failed");
	  exit(1);
	}
      }


      if (keep_histogram) {
	HIST_timestamp_stop_add(time_hist);
      }

#ifdef WANT_DEMO
      if (NETPERF_IS_RR(direction)) {
	if (libfmt == 'x') {
	  demo_interval_tick(1);
	}
	else {
	  demo_interval_tick(req_size + rsp_size);
	}
      }
      else if (NETPERF_XMIT_ONLY(direction)) {
	demo_interval_tick(bytes_to_send);
      }
      else {
	demo_interval_tick(rret);
      }
#endif

#if defined(WANT_INTERVALS)
      INTERVALS_WAIT();
#endif /* WANT_INTERVALS */


      /* was this a "transaction" test? */
      if (NETPERF_IS_RR(direction)) {
	trans_completed++;
	if (units_remaining) {
	  units_remaining--;
	}
      }


    }

    /* we are now, ostensibly, at the end of this iteration */

#if defined(WANT_DEMO)
    /* if we were in demo mode this will ensure one final interim
       result, which, naturally might be a bit early :) */
    demo_interval_final();
#endif

    if (transport_mss == -2)
      get_transport_info(data_socket,
			 &transport_mss,
			 local_res->ai_protocol);
    local_transport_retrans = get_transport_retrans(data_socket,
						    local_res->ai_protocol);


    /* so, if we have/had a data connection, we will want to close it
       now, and this will be independent of whether there is a control
       connection. */

    if (connected) {

#ifdef __linux
      /* so, "Linux" with autotuning likes to alter the socket buffer
	 sizes over the life of the connection, but only does so when
	 one takes the defaults at time of socket creation.  if we took
	 those defaults, we should inquire as to what the values
	 ultimately became. raj 2008-01-15 */
      if (lsr_size_req < 0)
	get_sock_buffer(data_socket, RECV_BUFFER, &lsr_size_end);
      else
	lsr_size_end = lsr_size;
      if (lss_size_req < 0)
	get_sock_buffer(data_socket, SEND_BUFFER, &lss_size_end);
      else
	lss_size_end = lss_size;
#else
      lsr_size_end = lsr_size;
      lss_size_end = lss_size;
#endif
      if ((desired_output_groups & OMNI_WANT_LOC_CONG) &&
	  (local_cong_control[0] == '\0')) {
	get_transport_cong_control(data_socket,
				   local_res->ai_protocol,
				   local_cong_control,
				   sizeof(local_cong_control));
      }

      /* CHECK PARMS HERE; */
      ret = disconnect_data_socket(data_socket,
				   1,
				   1,
				   NULL,
				   0);
      connected = 0;
      need_socket = 1;

    }
    else {
      /* this is the UDP case at present */
      ret = disconnect_data_socket(data_socket,
				   1,
				   1,
				   remote_res->ai_addr,
				   remote_res->ai_addrlen);
      need_socket = 1;
      lsr_size_end = lsr_size;
      lss_size_end = lss_size;
    }

    /* this call will always give us the elapsed time for the test, and
       will also store-away the necessaries for cpu utilization */

    cpu_stop(local_cpu_usage,&elapsed_time);

    /* if we timed-out, and had padded the timer, we need to subtract
       the pad_time from the elapsed time on the assumption that we
       were essentially idle for pad_time and just waiting for a timer
       to expire on something like a UDP test.  if we have not padded
       the timer, pad_time will be zero.  if we have not timed out
       then we want to make sure we stop the timer. */
    if (timed_out) {
      if (debug) {
	fprintf(where,"Adjusting elapsed_time by %d seconds\n",pad_time);
	fflush(where);
      }
      elapsed_time -= (float)pad_time;
    }
    else {
      stop_timer();
    }

    if (!no_control) {
      /* Get the statistics from the remote end. The remote will have
	 calculated service demand and all those interesting things. If
	 it wasn't supposed to care, it will return obvious values. */

      recv_response_n(OMNI_RESULTS_CONV_CUTOFF);
      if (!netperf_response.content.serv_errno) {
	if (debug)
	  fprintf(where,"remote results obtained\n");
	remote_cpu_method = format_cpu_method(omni_result->cpu_method);
	lib_num_rem_cpus = omni_result->num_cpus;
	lib_remote_cpu_stats.cpu_util = omni_result->cpu_util;
	lib_remote_cpu_stats.cpu_user = omni_result->cpu_percent_user;
	lib_remote_cpu_stats.cpu_system = omni_result->cpu_percent_system;
	lib_remote_cpu_stats.cpu_iowait = omni_result->cpu_percent_iowait;
	lib_remote_cpu_stats.cpu_irq = omni_result->cpu_percent_irq;
	lib_remote_cpu_stats.cpu_swintr = omni_result->cpu_percent_swintr;
	lib_remote_cpu_stats.peak_cpu_util = omni_result->peak_cpu_util;
	lib_remote_cpu_stats.peak_cpu_id = omni_result->peak_cpu_id;
	/* why?  because some stacks want to be clever and autotune their
	   socket buffer sizes, which means that if we accept the defaults,
	   the size we get from getsockopt() at the beginning of a
	   connection may not be what we would get at the end of the
	   connection... */
	rsr_size_end = omni_result->recv_buf_size;
	rss_size_end = omni_result->send_buf_size;
	remote_bytes_sent = (uint64_t)omni_result->bytes_sent_hi << 32;
	remote_bytes_sent += omni_result->bytes_sent_lo;
	remote_send_calls = omni_result->send_calls;
	remote_bytes_received = (uint64_t)omni_result->bytes_received_hi << 32;
	remote_bytes_received += omni_result->bytes_received_lo;
	remote_receive_calls = omni_result->recv_calls;
	remote_bytes_xferd = (double) remote_bytes_received +
	                              remote_bytes_sent;
	if (omni_result->recv_calls > 0)
	  remote_bytes_per_recv = (double) remote_bytes_received /
	    (double) omni_result->recv_calls;
	else
	  remote_bytes_per_recv = 0.0;
	if (omni_result->send_calls > 0)
	  remote_bytes_per_send = (double) remote_bytes_sent /
	    (double) omni_result->send_calls;
	else
	  remote_bytes_per_send = 0.0;

	remote_transport_retrans = omni_result->transport_retrans;
	/* what was the congestion control? */
	if (desired_output_groups & OMNI_WANT_REM_CONG) {
	  strncpy(remote_cong_control,
		  omni_result->cong_control,
		  sizeof(remote_cong_control));
	  remote_cong_control[sizeof(remote_cong_control) - 1] = '\0';
	}
      }
      else {
	Set_errno(netperf_response.content.serv_errno);
	fprintf(where,
		"netperf: remote error %d",
		netperf_response.content.serv_errno);
	perror("");
	fflush(where);

	exit(-1);
      }
    }
    else {
      /* when we are sending, in a no_control test, we have to
	 ass-u-me that everything we sent was received, otherwise, we
	 will report a transfer rate of zero. */
      remote_bytes_xferd = (double) bytes_sent;
    }

    /* so, what was the end result? */
    local_cpu_method = format_cpu_method(cpu_method);

    if (local_send_calls > 0)
      bytes_per_send = (double) bytes_sent / (double) local_send_calls;
    else bytes_per_send = 0.0;

    if (local_receive_calls > 0)
      bytes_per_recv = (double) bytes_received / (double) local_receive_calls;
    else
      bytes_per_recv = 0.0;

    bytes_xferd  = (double) bytes_sent + bytes_received;

    /* if the output format is 'x' we know the test was
       request/response.  if the libfmt is something else, it could be
       xmit, recv or bidirectional. if we were the receiver then we
       can use our byte totals even if it is
       UDP/unreliable. otherwise, we use the remote totals - they
       should be the same if the protocol is reliable, and if it is
       unreliable then we want what was actually received */
    if ('x' == libfmt)
      /* it was a request/response test */
      thruput = calc_thruput((double)trans_completed);
    else if (NETPERF_RECV_ONLY(direction))
      thruput      = calc_thruput(bytes_xferd);
    else
      thruput = calc_thruput(remote_bytes_xferd);

    if (NETPERF_IS_RR(direction)) {
      float rtt_elapsed_time = elapsed_time;

#ifdef WANT_INTERVALS
      /* if the test was paced, we need to subtract the time we were
	 sitting paced from the time we use to calculate the average
	 rtt_latency. Of course, won't really know how long we were
	 sitting unless we bracket the sit with timing calls, which
	 will be additional overhead affecting CPU utilization.  but,
	 there is no such thing as a free lunch is there :) raj
	 20110121 */
      if (interval_burst) {
	rtt_elapsed_time -= (float)(interval_wait_microseconds / 1000000.0);
      }
#endif /* WANT_INTERVALS */

      if (!connection_test) {
      /* calculate the round trip latency, using the transaction rate
	 whether or not the user was asking for thruput to be in 'x'
	 units please... however... a connection_test only ever has
	 one transaction in flight at one time */
      rtt_latency =
	(((double)1.0/(trans_completed/rtt_elapsed_time)) *
	 (double)1000000.0) *
	(double) (1 + ((first_burst_size > 0) ? first_burst_size : 0));
      }
      else {
	rtt_latency = ((double)1.0/(trans_completed/rtt_elapsed_time)) *
	  (double)1000000.0;
      }
      tmpfmt = libfmt;
      libfmt = 'x';
      transaction_rate = calc_thruput((double)trans_completed);
      libfmt = tmpfmt;
    }

    /* ok, time to possibly calculate cpu util and/or service demand */
    if (local_cpu_usage) {

      local_cpu_utilization = calc_cpu_util(elapsed_time);

      /* we need to decide what to feed the service demand beast,
	 which will, ultimately, depend on what sort of test it is and
	 whether or not the user asked for something specific - as in
	 per KB even on a TCP_RR test if it is being (ab)used as a
	 bidirectional bulk-transfer test. raj 2008-01-14 */
      local_service_demand  =
	calc_service_demand_fmt(('x' == libfmt) ? (double)trans_completed: bytes_xferd,
				0.0,
				0.0,
				0);
    }
    else {
      local_cpu_utilization	= (float) -1.0;
      local_service_demand	= (float) -1.0;
    }

    if (remote_cpu_usage) {

      remote_cpu_utilization = omni_result->cpu_util;

      remote_service_demand =
	calc_service_demand_fmt(('x' == libfmt) ? (double) trans_completed: bytes_xferd,
				0.0,
				remote_cpu_utilization,
				omni_result->num_cpus);
    }
    else {
      remote_cpu_utilization = (float) -1.0;
      remote_service_demand  = (float) -1.0;
    }

    /* time to calculate our confidence */
    calculate_confidence(confidence_iteration,
			 elapsed_time,
			 thruput,
			 local_cpu_utilization,
			 remote_cpu_utilization,
			 local_service_demand,
			 remote_service_demand);

    /* this this is the end of the confidence while loop? */
    confidence_iteration++;
  }

  /* we end with confidence_iteration one larger than the number of
     iterations.  if we weren't doing confidence intervals this will
     still be reported as one */
  confidence_iteration--;

#if defined(WANT_INTERVALS)
#ifdef WIN32
  stop_itimer();
#endif
#endif /* WANT_INTERVALS */

/* at some point we may want to actually display some results :) */

  retrieve_confident_values(&elapsed_time,
			    &thruput,
			    &local_cpu_utilization,
			    &remote_cpu_utilization,
			    &local_service_demand,
			    &remote_service_demand);

  /* a kludge for omni printing because I don't know how to tell that
     something is a float vs a double in my_snprintf() given what it
     is passed and I'm not ready to force all the netlib.c stuff to
     use doubles rather than floats. help there would be
     appreciated. raj 2008-01-28 */
  elapsed_time_double = (double) elapsed_time;
  local_service_demand_double = (double)local_service_demand;
  remote_service_demand_double = (double)remote_service_demand;

  if ('x' == libfmt) sd_str = "usec/Tran";
  else sd_str = "usec/KB";

  if (iteration_max > 1) {
    result_confid_pct = get_result_confid();
    loc_cpu_confid_pct = get_loc_cpu_confid();
    rem_cpu_confid_pct = get_rem_cpu_confid();
    interval_pct = interval * 100.0;
  }

  /* at some point we need to average these during a confidence
     interval run, and when we do do that, we need to make sure we
     restore the value of libfmt correctly */
  tmpfmt = libfmt;
  if ('x' == libfmt) {
    libfmt = 'm';
  }
  local_send_thruput = calc_thruput((double)bytes_sent);
  local_recv_thruput = calc_thruput((double)bytes_received);
  remote_send_thruput = calc_thruput((double)remote_bytes_sent);
  remote_recv_thruput = calc_thruput((double)remote_bytes_received);

  libfmt = tmpfmt;

  /* were we tracking possibly expensive statistics? */
  if (keep_statistics) {
    HIST_get_stats(time_hist,
		   &min_latency,
		   &max_latency,
		   &mean_latency,
		   &stddev_latency);
    p50_latency = HIST_get_percentile(time_hist, 0.50);
    p90_latency = HIST_get_percentile(time_hist, 0.90);
    p99_latency = HIST_get_percentile(time_hist, 0.99);

  }

  /* if we are running a legacy test we do not do the nifty new omni
     output stuff */
  if (!legacy) {
    print_omni();
  }

#if defined(DEBUG_OMNI_OUTPUT)
 {
   /* just something quick to sanity check the output selectors. this
      should be gone for "production" :) */
   int i;
   print_omni_init();
   output_list[0][1] = OUTPUT_END;
   for (i = OUTPUT_NONE; i < NETPERF_OUTPUT_MAX; i++) {
     output_list[0][0] = i;
     print_omni_csv();
   }
 }
#endif

  /* likely as not we are going to do something slightly different here */
  if ((verbosity > 1) && (!legacy)) {

#ifdef WANT_HISTOGRAM
    fprintf(where,"\nHistogram of ");
    if (NETPERF_RECV_ONLY(direction))
      fprintf(where,"recv");
    if (NETPERF_XMIT_ONLY(direction))
      fprintf(where,"send");
    if (NETPERF_IS_RR(direction)) {
      if (connection_test) {
	if (NETPERF_CC(direction)) {
	  fprintf(where,"connect/close");
	}
	else {
	  fprintf(where,"connect/request/response/close");
	}
      }
      else {
	fprintf(where,"request/response");
      }
    }
    fprintf(where," times\n");
    HIST_report(time_hist);
    fflush(where);
#endif /* WANT_HISTOGRAM */

  }

}


void
send_omni(char remote_host[])
{
  char name_buf[32];
  snprintf(name_buf,sizeof(name_buf),"OMNI %s TEST",direction_str);
  name_buf[31] = '\0';
  send_omni_inner(remote_host, 0, name_buf);
}

#if defined(WIN32)
#if !defined(InetNtop)
/* +*+ Why isn't this in the winsock headers yet? */
const char *
inet_ntop(int af, const void *src, char *dst, size_t size);
#endif
#endif

static void
set_hostname_and_port_2(void *addr, char *hostname, char *portstr, int family, int port)
{

  inet_ntop(family, addr, hostname, BUFSIZ);

  sprintf(portstr, "%u", port);

}




/* the name is something of a misnomer since this test could send, or
   receive, or both, but it matches the historical netperf routine
   naming convention for what runs in the netserver context. */
void
recv_omni()
{

  struct addrinfo *local_res;
  char local_name[BUFSIZ];
  char port_buffer[PORTBUFSIZE];

  struct sockaddr_storage myaddr_in, peeraddr_in;
  int peeraddr_set = 0;
  SOCKET s_listen, data_socket;
  netperf_socklen_t 	addrlen;

  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  int	timed_out = 0;
  int   pad_time = 0;
  int   need_to_connect = 0;
  int   need_to_accept;
  int   connected;
  int   first_receive = 1;
  int   ret;
  uint32_t   temp_recvs;

  struct	omni_request_struct	*omni_request;
  struct	omni_response_struct	*omni_response;
  struct	omni_results_struct	*omni_results;

  omni_request =
    (struct omni_request_struct *)netperf_request.content.test_specific_data;
  omni_response =
    (struct omni_response_struct *)netperf_response.content.test_specific_data;
  omni_results =
    (struct omni_results_struct *)netperf_response.content.test_specific_data;

  if (debug) {
    fprintf(where,"netserver: %s: entered...\n",__FUNCTION__);
    fflush(where);
  }

  /* netserver has no good way of knowing where the conversion cutoff
     point is, so we have to fix it after the fact */
  fixup_request_n(OMNI_REQUEST_CONV_CUTOFF);

  /* thus fixed-up, we can extract the requested congestion control
     algorithm */
  strncpy(local_cong_control_req,
	  omni_request->cong_control,
	  sizeof(local_cong_control_req));

  /* based on what we have been told by the remote netperf, we want to
     setup our endpoint for the "data connection" and let the remote
     netperf know the situation. */

  if (debug) {
    fprintf(where,"%s: setting the response type...\n",__FUNCTION__);
    fflush(where);
  }

  netperf_response.content.response_type = OMNI_RESPONSE;

  if (debug) {
    fprintf(where,"%s: the response type is set...\n",__FUNCTION__);
    fflush(where);
  }

  /* Grab a socket to listen on, and then listen on it. */

  if (debug) {
    fprintf(where,"%s: grabbing a socket...\n",__FUNCTION__);
    fflush(where);
  }

  /* create_data_socket expects to find some things in the global
     variables, so set the globals based on the values in the request.
     once the socket has been created, we will set the response values
     based on the updated value of those globals. raj 7/94 */
  lss_size_req    = omni_request->send_buf_size;
  lsr_size_req    = omni_request->recv_buf_size;
  loc_nodelay     = (omni_request->flags) & OMNI_NO_DELAY;
  loc_rcvavoid    = omni_request->so_rcvavoid;
  loc_sndavoid    = omni_request->so_sndavoid;
  routing_allowed = (omni_request->flags) & OMNI_ROUTING_ALLOWED;
  want_keepalive  = (omni_request->flags) & OMNI_WANT_KEEPALIVE;
  local_connected = (omni_request->flags) & OMNI_USE_CONNECTED;
  local_socket_prio = omni_request->socket_prio;
  local_socket_tos  = omni_request->socket_tos;
  want_defer_accept = omni_request->flags & OMNI_WANT_DEFER_ACCEPT;

#ifdef WANT_INTERVALS
  interval_usecs = omni_request->interval_usecs;
  interval_wate  = interval_usecs / 1000;
  interval_burst = omni_request->interval_burst;
#else
  interval_usecs = 0;
  interval_wate  = 1;
  interval_burst = 0;
#endif

  connection_test = omni_request->flags & OMNI_CONNECT_TEST;
#ifdef TCP_FASTOPEN
  use_fastopen = omni_request->flags & OMNI_FASTOPEN;
#endif

#ifdef IP_MTU_DISCOVER
  use_mtu_discover = omni_request->flags & OMNI_USE_MTU_DISCOVER;
#endif

  direction       = omni_request->direction;
  use_pktinfo = (omni_request->flags) & OMNI_USE_PKTINFO;

  /* let's be quite certain the fill file string is null terminated */
  omni_request->fill_file[sizeof(omni_request->fill_file) - 1] = '\0';
  strncpy(local_fill_file,
	  omni_request->fill_file,
	  sizeof(local_fill_file));

  /* kludgy, because I have no way at present to say how many bytes
     needed to be swapped around for the request from which this is
     pulled, and it is probably all wrong for IPv6 :( */
  switch (nf_to_af(omni_request->ipfamily)) {
  case AF_INET6:
    /* yes indeed it is, do nothing, bz */
    break;
  case AF_INET:
  default:
    for (ret=0; ret < 4; ret++) {
      omni_request->netserver_ip[ret] = htonl(omni_request->netserver_ip[ret]);
      omni_request->netperf_ip[ret] = htonl(omni_request->netperf_ip[ret]);
    }
    break;
  }

  set_hostname_and_port_2(omni_request->netserver_ip,
			  local_name,
			  port_buffer,
			  nf_to_af(omni_request->ipfamily),
			  omni_request->data_port);

  local_res = complete_addrinfo(local_name,
				local_name,
				port_buffer,
				nf_to_af(omni_request->ipfamily),
				nst_to_hst(omni_request->socket_type),
				omni_request->protocol,
				0);

  s_listen = omni_create_data_socket(local_res);

  if (s_listen == INVALID_SOCKET) {
    netperf_response.content.serv_errno = errno;
    send_response();
    if (debug) {
      fprintf(where,"could not create data socket\n");
      fflush(where);
    }
    exit(-1);
  }

  /* We now alter the message_ptr variables to be at the desired */
  /* alignments with the desired offsets. */

  if (debug) {
    fprintf(where,
	    "recv_omni: requested recv alignment of %d offset %d\n"
	    "recv_omni: requested send alignment of %d offset %d\n",
	    omni_request->recv_alignment,
	    omni_request->recv_offset,
	    omni_request->send_alignment,
	    omni_request->send_offset);
    fflush(where);
  }

  omni_response->send_size = omni_request->send_size;
  omni_response->send_width = omni_request->send_width;
  omni_response->socket_prio = local_socket_prio;
  omni_response->socket_tos = local_socket_tos;

  if (omni_request->direction & NETPERF_XMIT) {
#ifdef fo
    /* do we need to set multicast ttl? */
    if (is_multicast_addr(remote_res)) {
      /* yes, s_listen - for a UDP test we will be copying it to
	 data_socket but that hasn't happened yet. raj 20100315 */
      set_multicast_ttl(s_listen);
    }
#endif

    if (omni_request->response_size > 0) {
      /* request/response_test */
      bytes_to_send = omni_request->response_size;
      if (omni_request->send_width == 0) send_width = 1;
      else send_width = omni_request->send_width;
    }
    else {
      if (omni_request->send_size == -1) {
	bytes_to_send = choose_send_size(lss_size,omni_request->protocol);
      }
      else bytes_to_send = omni_request->send_size;
      /* set the send_width */
      if (omni_request->send_width == 0) {
	send_width = (lss_size/bytes_to_send) + 1;
	if (send_width == 1) send_width++;
      }
      else
	send_width = omni_request->send_width;
    }
    send_ring = allocate_buffer_ring(send_width,
				     bytes_to_send,
				     omni_request->send_alignment,
				     omni_request->send_offset);

    omni_response->send_width = send_width;
    omni_response->send_size = bytes_to_send;
  }

  omni_response->receive_size = omni_request->receive_size;
  omni_response->recv_width = omni_request->recv_width;
  if (omni_request->direction & NETPERF_RECV) {

    /* do we need to join a multicast group? */
    if (is_multicast_addr(local_res)) {
      /* yes, s_listen - for a UDP test we will be copying it to
	 data_socket but that hasn't happened yet. raj 20100315 */
      join_multicast_addr(s_listen, local_res);
    }

    if (omni_request->request_size > 0) {
      /* request/response test */
      bytes_to_recv = omni_request->request_size;
      if (omni_request->recv_width == 0) recv_width = 1;
      else recv_width = omni_request->recv_width;
    }
    else {
      if (omni_request->receive_size == -1) {
	if (lsr_size > 0) bytes_to_recv = lsr_size;
	else  bytes_to_recv = 4096;
      }
      else {
	bytes_to_recv = omni_request->receive_size;
      }
      /* set the recv_width */
      if (omni_request->recv_width == 0) {
	recv_width = (lsr_size/bytes_to_recv) + 1;
	if (recv_width == 1) recv_width++;
      }
      else
	recv_width = omni_request->recv_width;
    }
    recv_ring = allocate_buffer_ring(recv_width,
				     bytes_to_recv,
				     omni_request->recv_alignment,
				     omni_request->recv_offset);

    omni_response->receive_size = bytes_to_recv;
    omni_response->recv_width = recv_width;
  }

#ifdef WIN32
  /* The test timer can fire during operations on the listening socket,
     so to make the start_timer below work we have to move
     it to close s_listen while we are blocked on accept. */
  win_kludge_socket2 = s_listen;
#endif

  need_to_accept = (omni_request->protocol != IPPROTO_UDP);

  /* we need to hang a listen for everything that needs at least one
     accept. the age-old constant of 5 is probably OK for our purposes
     but does not necessarily represent best practice */
  if (need_to_accept) {
    int backlog = 5;
#ifdef TCP_FASTOPEN
    /* one of these days I will have to go find-out what the backlog
       is supposed to be here.  until then, I'll just set it to five
       like the listen() call does - it is classic, and was what was
       used in the online example I found */
    if (use_fastopen &&
	(setsockopt(s_listen,IPPROTO_TCP, TCP_FASTOPEN, &backlog, sizeof(backlog)) ==
	 SOCKET_ERROR)) {
      netperf_response.content.serv_errno = errno;
      close(s_listen);
      send_response();
      if (debug) {
	fprintf(where,"netperfserver: %s could not fastopen\n",__FUNCTION__);
	fflush(where);
      }
      exit(1);
    }
#endif /* TCP_FASTOPEN */
#ifdef TCP_DEFER_ACCEPT
    if (want_defer_accept &&
	(setsockopt(s_listen, IPPROTO_TCP, TCP_DEFER_ACCEPT, &backlog, sizeof(backlog)) == SOCKET_ERROR)) {
      netperf_response.content.serv_errno = errno;
      close(s_listen);
      send_response();
      if (debug) {
	fprintf(where,
		"netperfserver: %s could not defer accept\n",__FUNCTION__);
	fflush(where);
      }
      exit(1);
    }
#endif /* TCP_DEFER_ACCEPT */
    if (listen(s_listen, backlog) == SOCKET_ERROR) {
      netperf_response.content.serv_errno = errno;
      close(s_listen);
      send_response();
      if (debug) {
	fprintf(where,"netperfserver: %s could not listen\n",__FUNCTION__);
	fflush(where);
      }
      exit(1);
    }
  }

  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen,
		  (struct sockaddr *)&myaddr_in,
		  &addrlen) == SOCKET_ERROR){
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();
    if (debug) {
      fprintf(where,"could not getsockname\n");
      fflush(where);
    }
    exit(-1);
  }

  /* Now myaddr_in contains the port and the internet address this is
     returned to the sender also implicitly telling the sender that
     the socket buffer sizing has been done. likely as not, the IP
     address will be the wildcard - so we only really need to extract
     the port number. since send_response is going to call htonl on
     all the fields, we want to initially put the port number in there
     in host order. */

  omni_response->data_port =
    (int) ntohs(((struct sockaddr_in *)&myaddr_in)->sin_port);
  if (debug) {
    fprintf(where,"telling the remote to call me at %d\n",
	    omni_response->data_port);
    fflush(where);
  }
  netperf_response.content.serv_errno   = 0;

  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */

  omni_response->cpu_rate = (float)0.0; 	/* assume no cpu */
  omni_response->flags &= ~OMNI_MEASURE_CPU;
  if (omni_request->flags & OMNI_MEASURE_CPU) {
    omni_response->flags |= OMNI_MEASURE_CPU;
    omni_response->cpu_rate =
      calibrate_local_cpu(omni_request->cpu_rate);
  }

  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  omni_response->send_buf_size = lss_size;
  omni_response->recv_buf_size = lsr_size;
  if (loc_nodelay)
    omni_response->flags |= OMNI_NO_DELAY;
  else
    omni_response->flags &= ~OMNI_NO_DELAY;

  omni_response->so_rcvavoid = loc_rcvavoid;
  omni_response->so_sndavoid = loc_sndavoid;
  omni_response->interval_usecs = interval_usecs;
  omni_response->interval_burst = interval_burst;

  send_response_n(OMNI_RESPONSE_CONV_CUTOFF); /* brittle, but functional */

  local_send_calls = 0;
  local_receive_calls = 0;

  addrlen = sizeof(peeraddr_in);
  memset(&peeraddr_in,0,sizeof(peeraddr_in));

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */

  cpu_start(omni_request->flags & OMNI_MEASURE_CPU);

  /* if the test is timed, set a timer of suitable length.  if the
     test is by byte/transaction count, we don't need a timer - or
     rather we rely on the netperf to only ask us to do transaction
     counts over "reliable" protocols.  perhaps at some point we
     should add a check herebouts to verify that... */

  if (omni_request->test_length >= 0) {
    times_up = 0;
    units_remaining = 0;
	test_time=omni_request->test_length;
    /* if we are the sender and only sending, then we don't need/want
       the padding, otherwise, we need the padding */
    if (!(NETPERF_XMIT_ONLY(omni_request->direction)) &&
	(omni_request->test_length > 0))
      pad_time = PAD_TIME;
    start_timer(omni_request->test_length + pad_time);
  }
  else {
    times_up = 1;
    units_remaining = omni_request->test_length * -1;
  }

#if defined(WANT_INTERVALS)
  INTERVALS_INIT();
#endif /* WANT_INTERVALS */


  trans_completed = 0;
  bytes_sent = 0;
  bytes_received = 0;
  connected = 0;

  while ((!times_up) || (units_remaining > 0)) {

    if (need_to_accept) {
      /* accept a connection from the remote */
#ifdef WIN32
      /* The test timer will probably fire during this accept,
	 so to make the start_timer above work we have to move
	 it to close s_listen while we are blocked on accept. */
      win_kludge_socket = s_listen;
#endif
      if ((data_socket=accept(s_listen,
			      (struct sockaddr *)&peeraddr_in,
			      &addrlen)) == INVALID_SOCKET) {
	if (errno == EINTR) {
	  /* the timer popped */
	  times_up = 1; /* ostensibly the signal hander dealt with this?*/
	  timed_out = 1;
	  break;
	}
	netperf_response.content.serv_errno = errno;
	send_response();
	fprintf(where,"%s: accept: errno = %d\n",__FUNCTION__,errno);
	fflush(where);
	close(s_listen);

	exit(-1);
      }

      if (debug) {
	fprintf(where,"%s: accepted data connection.\n",__FUNCTION__);
	fflush(where);
      }
      need_to_accept = 0;
      connected = 1;

#ifdef KLUDGE_SOCKET_OPTIONS
      /* this is for those systems which *INCORRECTLY* fail to pass
	 attributes across an accept() call. Including this goes
	 against my better judgement :( raj 11/95 */

      kludge_socket_options(data_socket);

#endif /* KLUDGE_SOCKET_OPTIONS */

    }
    else {
      /* I wonder if duping would be better here? we also need to set
	 peeraddr_in so we can send to netperf if this isn't a
	 request/response test or if we are going to connect() the
	 socket, but we only need to do it once. */
      if ((omni_request->protocol == IPPROTO_UDP) &&
	  (!peeraddr_set)) {
	peeraddr_set = 1;
	data_socket = s_listen;
	set_sockaddr_family_addr_port(&peeraddr_in,
				      nf_to_af(omni_request->ipfamily),
				      omni_request->netperf_ip,
				      omni_request->netperf_port);
      }
    }

#ifdef WIN32
  /* this is used so the timer thread can close the socket out from
     under us, which to date is the easiest/cleanest/least
     Windows-specific way I can find to force the winsock calls to
     return WSAEINTR with the test is over. anything that will run on
     95 and NT and is closer to what netperf expects from Unix signals
     and such would be appreciated raj 1/96 */
  win_kludge_socket = data_socket;
#endif /* WIN32 */

    /* in recv_omni, we check recv first, and _then_ send, otherwise,
       a request/response test will be all messed-up :) and that then
       is why there are two routines to rule them all rather than just
       one :) */
    if ((omni_request->direction & NETPERF_RECV) &&
	((!times_up) || (units_remaining > 0))) {
      ret = recv_data(data_socket,
		      recv_ring,
		      bytes_to_recv,
		      (connected) ? NULL : (struct sockaddr *)&peeraddr_in,
		      &addrlen,
		      /* if XMIT also, then this is RR test so waitall */
		      (direction & NETPERF_XMIT) ? NETPERF_WAITALL: 0,
		      &temp_recvs);
      if (ret > 0) {
	/* if this is a recv-only test controlled by byte count we
	   decrement the units_remaining by the bytes received */
	if (!(direction & NETPERF_XMIT) && (units_remaining > 0)) {
	  units_remaining -= ret;
	}
	bytes_received += ret;
	local_receive_calls += temp_recvs;
	if (first_receive && local_connected) {
	  connect(data_socket,
		  (struct sockaddr *)&peeraddr_in,
		  addrlen);
	  first_receive = 0;
	}
      }
      else if (ret == 0) {
	/* is this the end of a test, just a zero-byte recv, or
	   something else? that is an exceedingly good question and
	   one for which I don't presently have a good answer, but
	   that won't stop me from guessing :) raj 2008-01-09 */
	if (!((connection_test) || (null_message_ok))) {
	  /* if it is neither a connection_test nor null_message_ok it
	     must be the end of the test */
	  times_up = 1;
	  break;
	}
	local_receive_calls += temp_recvs;
      }
      else if (ret == -1) {
	/* test timed-out */
	times_up = 1;
	timed_out = 1;
	break;
      }
      else {
	/* presently at least, -2 and -3 are equally bad on recv */
	/* we need a response message here for the control connection
	   before we exit! */
	netperf_response.content.serv_errno = errno;
	send_response();
	exit(-1);
      }
      recv_ring = recv_ring->next;
    }

    /* if we should try to send something, then by all means, let us
       try to send something. */
    if ((omni_request->direction & NETPERF_XMIT) &&
	((!times_up) || (units_remaining > 0))) {

      /* there used to be some code here looking sched_yield() until
	 there was no more queued, unsent data on the socket but
	 frankly, I've no idea what that was all about so I have
	 removed it. It may have been part of a kludge to try to avoid
	 coalescing requests and responses */

      if (omni_request->protocol == IPPROTO_UDP && need_to_connect &&
          !connected) {
        if (connect(data_socket,
                    (struct sockaddr*)&peeraddr_in,
                    addrlen) == INVALID_SOCKET) {
	  netperf_response.content.serv_errno = errno;
	  send_response();
	  close(data_socket);
	  exit(-1);
        }
        connected = 1;
      }

      ret = send_data(data_socket,
		      send_ring,
		      bytes_to_send,
		      (connected) ? NULL : (struct sockaddr *)&peeraddr_in,
		      addrlen,
		      omni_request->protocol);

      /* the order of these if's will seem a triffle strange, but they
	 are my best guess as to order of probabilty and/or importance
	 to the overhead raj 2008-01-09*/
      if (ret == bytes_to_send) {
	/* if this is a send-only test controlled by byte count we
	   decrement units_remaining by the bytes sent */
	if (!(direction & NETPERF_RECV) && (units_remaining > 0)) {
	  units_remaining -= ret;
	}
	bytes_sent += ret;
	send_ring = send_ring->next;
	local_send_calls++;
      }
      else if (ret == -2) {
	/* what to do here -2 means a non-fatal error - probably
	   ENOBUFS and so our send didn't happen.  in the old code for
	   UDP_STREAM we would just continue in the while loop.  it
	   isn't clear that is what to do here, so we will simply
	   increment the failed_sends stat and fall-through. If this
	   is a UDP_STREAM style of test, the net effect should be the
	   same. if this is a UDP_RR with a really-big burst count, I
	   don't think we were checking for ENOBUFS there anyway and
	   so would have failed.  Here we can just let things
	   slide. */
	failed_sends++;
      }
      else if (ret == 0) {
	/* was this a zero-byte send? if it was, then ostensibly we
	   would hit the ret == bytes_to_send case which means we'd
	   never get here as we are using blocking semantics */
      }
      else if (ret == -1) {
	times_up = 1;
	timed_out = 1;
	break;
      }
      else {
	/* we need a response message back to netperf here before we
	   exit */
	/* NEED RESPONSE; */
	netperf_response.content.serv_errno = errno;
	send_response();
	exit(-1);
      }

    }

    if (connection_test) {
#ifdef __linux
      /* so, "Linux" with autotuning likes to alter the socket buffer
	 sizes over the life of the connection, but only does so when
	 one takes the defaults at time of socket creation.  if we
	 took those defaults, we should inquire as to what the values
	 ultimately became. raj 2008-01-15 */
      /* but as annoying as it is to have to make these calls, don't
	 penalize linux by calling them over and over again. instead
	 we will simply ass-u-me that it will become the same value
	 over and over again. raj 20100917 */
      if ((lsr_size_req < 0) && (-1 == lsr_size_end))
	get_sock_buffer(data_socket, RECV_BUFFER, &lsr_size_end);
      else
	lsr_size_end = lsr_size;
      if ((lss_size_req < 0) && (-1 == lss_size_end))
	get_sock_buffer(data_socket, SEND_BUFFER, &lss_size_end);
      else
	lss_size_end = lss_size;
#else
      lsr_size_end = lsr_size;
      lss_size_end = lss_size;
#endif
      ret = close_data_socket(data_socket,NULL,0,omni_request->protocol);
      if (ret == -1) {
	times_up = 1;
	timed_out = 1;
	break;
      }
      else if (ret < 0) {
	netperf_response.content.serv_errno = errno;
	send_response();
	perror("netperf: recv_omni: close_data_socket failed");
	fflush(where);
	exit(-1);
      }
      /* we will need a new connection to be established */
      need_to_accept = 1;
      connected = 0;
    }

#if defined(WANT_INTERVALS)
    INTERVALS_WAIT();
#endif /* WANT_INTERVALS */

    /* was this a "transaction" test? don't for get that a TCP_CC
       style test will have no xmit or recv :) so, we check for either
       both XMIT and RECV set, or neither XMIT nor RECV set */
    if (NETPERF_IS_RR(omni_request->direction)) {
      trans_completed++;
      if (units_remaining) {
	units_remaining--;
      }
    }
  }

  /* The current iteration loop now exits due to timeout or unit count
     being  reached */
  stop_timer();
  cpu_stop(omni_request->flags & OMNI_MEASURE_CPU,&elapsed_time);
  close(s_listen);

#if defined(WANT_INTERVALS)
#ifdef WIN32
  stop_itimer();
#endif
#endif /* WANT_INTERVALS */

 if (timed_out) {
    /* we ended the test by time, which may have been PAD_TIME seconds
       longer than we wanted to run. so, we want to subtract pad_time
       from the elapsed_time. if we didn't pad the timer pad_time will
       be 0 so we can just subtract it anyway :) */
    if (debug) {
      fprintf(where,"Adjusting elapsed time by %d seconds\n",pad_time);
      fflush(where);
    }
    elapsed_time -= pad_time;
  }

  remote_transport_retrans = get_transport_retrans(data_socket,
						   omni_request->protocol);

  if (connected) {
#ifdef __linux
    /* so, "Linux" with autotuning likes to alter the socket buffer
       sizes over the life of the connection, but only does so when
       one takes the defaults at time of socket creation.  if we took
       those defaults, we should inquire as to what the values
       ultimately became. raj 2008-01-15 */
    if (lsr_size_req < 0)
      get_sock_buffer(data_socket, RECV_BUFFER, &lsr_size_end);
    else
      lsr_size_end = lsr_size;
    if (lss_size_req < 0)
      get_sock_buffer(data_socket, SEND_BUFFER, &lss_size_end);
    else
      lss_size_end = lss_size;
#else
    lsr_size_end = lsr_size;
    lss_size_end = lss_size;
#endif
    if (omni_request->flags & OMNI_WANT_REM_CONG) {
      get_transport_cong_control(data_socket,
				 local_res->ai_protocol,
				 omni_results->cong_control,
				 sizeof(omni_results->cong_control));
    }
    else {
      strncpy(omni_results->cong_control,"",sizeof(omni_results->cong_control));
    }


    close_data_socket(data_socket,NULL,0,omni_request->protocol);
  }
  else {
    close_data_socket(data_socket,(struct sockaddr *)&peeraddr_in,addrlen,omni_request->protocol);
    lsr_size_end = lsr_size;
    lss_size_end = lss_size;
  }

  /* send the results to the sender  */

  omni_results->send_calls      = (uint32_t) local_send_calls;
  omni_results->bytes_received_lo = bytes_received & 0x00000000FFFFFFFFULL;
  omni_results->bytes_received_hi = (bytes_received & 0xFFFFFFFF00000000ULL) >> 32;
  omni_results->recv_buf_size   = lsr_size_end;
  omni_results->recv_calls      = (uint32_t) local_receive_calls;
  omni_results->bytes_sent_lo   = bytes_sent & 0x00000000FFFFFFFFULL;
  omni_results->bytes_sent_hi   = (bytes_sent & 0xFFFFFFFF00000000ULL) >> 32;
  omni_results->send_buf_size   = lss_size_end;
  omni_results->trans_received	= (uint32_t) trans_completed;
  omni_results->elapsed_time	= elapsed_time;
  omni_results->transport_retrans = remote_transport_retrans;
  omni_results->cpu_method      = cpu_method;
  omni_results->num_cpus        = lib_num_loc_cpus;
  if (omni_request->flags & OMNI_MEASURE_CPU) {
    omni_results->cpu_util            = calc_cpu_util(elapsed_time);
    omni_results->cpu_percent_user    = lib_local_cpu_stats.cpu_user;
    omni_results->cpu_percent_system  = lib_local_cpu_stats.cpu_system;
    omni_results->cpu_percent_iowait  = lib_local_cpu_stats.cpu_iowait;
    omni_results->cpu_percent_irq     = lib_local_cpu_stats.cpu_irq;
    omni_results->cpu_percent_swintr  = lib_local_cpu_stats.cpu_swintr;
    omni_results->peak_cpu_util       = lib_local_cpu_stats.peak_cpu_util;
    omni_results->peak_cpu_id         = lib_local_cpu_stats.peak_cpu_id;
  }

#if defined(WANT_INTERVALS)
#ifdef WIN32
  stop_itimer();
#endif
#endif /* WANT_INTERVALS */

  if (debug) {
    fprintf(where,
	    "%s: test complete, sending results.\n",
	    __FUNCTION__);
    fflush(where);
  }

  send_response_n(OMNI_RESULTS_CONV_CUTOFF);

}


#ifdef WANT_MIGRATION
void
send_tcp_stream(char remote_host[])
{

  char *tput_title = "\
Recv   Send    Send                          \n\
Socket Socket  Message  Elapsed              \n\
Size   Size    Size     Time     Throughput  \n\
bytes  bytes   bytes    secs.    %s/sec  \n\n";

  char *tput_fmt_0 =
    "%7.2f %s\n";

  char *tput_fmt_1 =
    "%6d %6d %6d    %-6.2f   %7.2f   %s\n";

  char *cpu_title = "\
Recv   Send    Send                          Utilization       Service Demand\n\
Socket Socket  Message  Elapsed              Send     Recv     Send    Recv\n\
Size   Size    Size     Time     Throughput  local    remote   local   remote\n\
bytes  bytes   bytes    secs.    %-8.8s/s  %% %c      %% %c      us/KB   us/KB\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c %s\n";

  char *cpu_fmt_1 =
    "%6d %6d %6d    %-6.2f     %7.2f   %-6.2f   %-6.2f   %-6.3f  %-6.3f %s\n";

  char *ksink_fmt = "\n\
Alignment      Offset         %-8.8s %-8.8s    Sends   %-8.8s Recvs\n\
Local  Remote  Local  Remote  Xfered   Per                 Per\n\
Send   Recv    Send   Recv             Send (avg)          Recv (avg)\n\
%5d   %5d  %5d   %5d %6"PRId64"  %6.2f    %6"PRId64"   %6.2f %6"PRId64"\n";

  char *ksink_fmt2 = "\n\
Maximum\n\
Segment\n\
Size (bytes)\n\
%6d\n";

  send_omni_inner(remote_host, legacy, "MIGRATED TCP STREAM TEST");


  if (legacy) {

    /* We are now ready to print all the information, but only if we
       are truly acting as a legacy test. If the user has specified
       zero-level verbosity, we will just print the local service
       demand, or the remote service demand. If the user has requested
       verbosity level 1, he will get the basic "streamperf"
       numbers. If the user has specified a verbosity of greater than
       1, we will display a veritable plethora of background
       information from outside of this block as it it not
       cpu_measurement specific...  */

    if (confidence < 0) {
      /* we did not hit confidence, but were we asked to look for it? */
      if (iteration_max > 1) {
	display_confidence();
      }
    }

    if (local_cpu_usage || remote_cpu_usage) {

      switch (verbosity) {
      case 0:
	if (local_cpu_usage) {
	  fprintf(where,
		  cpu_fmt_0,
		  local_service_demand,
		  local_cpu_method,
		  ((print_headers) ||
		   (result_brand == NULL)) ? "" : result_brand);
	}
	else {
	  fprintf(where,
		  cpu_fmt_0,
		  remote_service_demand,
		  remote_cpu_method,
		  ((print_headers) ||
		   (result_brand == NULL)) ? "" : result_brand);
	}
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,
		  cpu_title,
		  format_units(),
		  local_cpu_method,
		  remote_cpu_method);
	}

	fprintf(where,
		cpu_fmt_1,		/* the format string */
		rsr_size,		/* remote recvbuf size */
		lss_size,		/* local sendbuf size */
		send_size,	        /* how large were the sends */
		elapsed_time,		/* how long was the test */
		thruput, 		/* what was the xfer rate */
		local_cpu_utilization,	/* local cpu */
		remote_cpu_utilization,	/* remote cpu */
		local_service_demand,	/* local service demand */
		remote_service_demand,	/* remote service demand */
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	break;
      }
    }
    else {
      /* The tester did not wish to measure service demand. */

      switch (verbosity) {
      case 0:
	fprintf(where,
		tput_fmt_0,
		thruput,
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,tput_title,format_units());
	}
	fprintf(where,
		tput_fmt_1,		/* the format string */
		rsr_size, 		/* remote recvbuf size */
		lss_size, 		/* local sendbuf size */
		send_size,	        /* how large were the sends */
		elapsed_time, 		/* how long did it take */
		thruput,                /* how fast did it go */
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	break;
      }
    }

    /* it would be a good thing to include information about some of the */
    /* other parameters that may have been set for this test, but at the */
    /* moment, I do not wish to figure-out all the  formatting, so I will */
    /* just put this comment here to help remind me that it is something */
    /* that should be done at a later time. */

    if (verbosity > 1) {
      /* The user wanted to know it all, so we will give it to him. */
      /* This information will include as much as we can find about */
      /* TCP statistics, the alignments of the sends and receives */
      /* and all that sort of rot... */

      /* this stuff needs to be worked-out in the presence of confidence */
      /* intervals and multiple iterations of the test... raj 11/94 */

      fprintf(where,
	      ksink_fmt,
	      "Bytes",
	      "Bytes",
	      "Bytes",
	      local_send_align,
	      remote_recv_align,
	      local_send_offset,
	      remote_recv_offset,
	      bytes_sent,
	      bytes_sent / (double)local_send_calls,
	      local_send_calls,
	      bytes_sent / (double)remote_receive_calls,
	      remote_receive_calls);
      fprintf(where,
	      ksink_fmt2,
	      transport_mss);
#ifdef WANT_HISTOGRAM
      fprintf(where,"\n\nHistogram of time spent in send() call.\n");
      HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
      fflush(where);
    }

  }
}

void
send_tcp_maerts(char remote_host[])
{

  char *tput_title = "\
Recv   Send    Send                          \n\
Socket Socket  Message  Elapsed              \n\
Size   Size    Size     Time     Throughput  \n\
bytes  bytes   bytes    secs.    %s/sec  \n\n";

  char *tput_fmt_0 =
    "%7.2f %s\n";

  char *tput_fmt_1 =
    "%6d %6d %6d    %-6.2f   %7.2f   %s\n";

  char *cpu_title = "\
Recv   Send    Send                          Utilization       Service Demand\n\
Socket Socket  Message  Elapsed              Recv     Send     Recv    Send\n\
Size   Size    Size     Time     Throughput  local    remote   local   remote\n\
bytes  bytes   bytes    secs.    %-8.8s/s  %% %c      %% %c      us/KB   us/KB\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c %s\n";

  char *cpu_fmt_1 =
    "%6d %6d %6d    %-6.2f     %7.2f   %-6.2f   %-6.2f   %-6.3f  %-6.3f %s\n";

  char *ksink_fmt = "\n\
Alignment      Offset         %-8.8s %-8.8s    Recvs   %-8.8s Sends\n\
Local  Remote  Local  Remote  Xfered   Per                 Per\n\
Recv   Send    Recv   Send             Recv (avg)          Send (avg)\n\
%5d   %5d  %5d   %5d %6"PRId64"  %6.2f    %6d   %6.2f %6d\n";

  char *ksink_fmt2 = "\n\
Maximum\n\
Segment\n\
Size (bytes)\n\
%6d\n";

  send_omni_inner(remote_host, legacy, "MIGRATED TCP MAERTS TEST");


  /* We are now ready to print all the information, but only if we are
     truly acting as a leacy test.  If the user has specified
     zero-level verbosity, we will just print the local service
     demand, or the remote service demand. If the user has requested
     verbosity level 1, he will get the basic "streamperf" numbers. If
     the user has specified a verbosity of greater than 1, we will
     display a veritable plethora of background information from
     outside of this block as it it not cpu_measurement
     specific...  */

  if (legacy) {

    if (confidence < 0) {
      /* we did not hit confidence, but were we asked to look for it? */
      if (iteration_max > 1) {
	display_confidence();
      }
    }

    if (local_cpu_usage || remote_cpu_usage) {

      switch (verbosity) {
      case 0:
	if (local_cpu_usage) {
	  fprintf(where,
		  cpu_fmt_0,
		  local_service_demand,
		  local_cpu_method,
		  ((print_headers) ||
		   (result_brand == NULL)) ? "" : result_brand);
	}
	else {
	  fprintf(where,
		  cpu_fmt_0,
		  remote_service_demand,
		  remote_cpu_method,
		  ((print_headers) ||
		   (result_brand == NULL)) ? "" : result_brand);
	}
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,
		  cpu_title,
		  format_units(),
		  local_cpu_method,
		  remote_cpu_method);
	}

	fprintf(where,
		cpu_fmt_1,		/* the format string */
		rsr_size,		/* remote recvbuf size */
		lss_size,		/* local sendbuf size */
		remote_send_size,	/* how large were the recvs */
		elapsed_time,		/* how long was the test */
		thruput, 		/* what was the xfer rate */
		local_cpu_utilization,	/* local cpu */
		remote_cpu_utilization,	/* remote cpu */
		local_service_demand,	/* local service demand */
		remote_service_demand,	/* remote service demand */
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	break;
      }
    }
    else {
      /* The tester did not wish to measure service demand. */

      switch (verbosity) {
      case 0:
	fprintf(where,
		tput_fmt_0,
		thruput,
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,tput_title,format_units());
	}
	fprintf(where,
		tput_fmt_1,		/* the format string */
		lsr_size, 		/* local recvbuf size */
		rss_size, 		/* remot sendbuf size */
		remote_send_size,	/* how large were the recvs */
		elapsed_time, 		/* how long did it take */
		thruput,                /* how fast did it go */
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	break;
      }
    }

    /* it would be a good thing to include information about some of the */
    /* other parameters that may have been set for this test, but at the */
    /* moment, I do not wish to figure-out all the  formatting, so I will */
    /* just put this comment here to help remind me that it is something */
    /* that should be done at a later time. */

    if (verbosity > 1) {
      /* The user wanted to know it all, so we will give it to him. */
      /* This information will include as much as we can find about */
      /* TCP statistics, the alignments of the sends and receives */
      /* and all that sort of rot... */

      /* this stuff needs to be worked-out in the presence of confidence */
      /* intervals and multiple iterations of the test... raj 11/94 */

      fprintf(where,
	      ksink_fmt,
	      "Bytes",
	      "Bytes",
	      "Bytes",
	      local_recv_align,
	      remote_recv_align,
	      local_recv_offset,
	      remote_recv_offset,
	      bytes_received,
	      bytes_received / (double)local_receive_calls,
	      local_receive_calls,
	      remote_bytes_sent / (double)remote_send_calls,
	      remote_send_calls);
      fprintf(where,
	      ksink_fmt2,
	      transport_mss);

#ifdef WANT_HISTOGRAM
      fprintf(where,"\n\nHistogram of time spent in recv() call.\n");
      HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
      fflush(where);
    }
  }
}


void
send_tcp_rr(char remote_host[]) {

  char *tput_title = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  Trans.\n\
Send   Recv   Size     Size    Time     Rate         \n\
bytes  Bytes  bytes    bytes   secs.    per sec   \n\n";

  char *tput_title_band = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  \n\
Send   Recv   Size     Size    Time     Throughput \n\
bytes  Bytes  bytes    bytes   secs.    %s/sec   \n\n";

  char *tput_fmt_0 =
    "%7.2f %s\n";

  char *tput_fmt_1_line_1 = "\
%-6d %-6d %-6d   %-6d  %-6.2f   %7.2f   %s\n";
  char *tput_fmt_1_line_2 = "\
%-6d %-6d\n";

  char *cpu_title = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Trans.   CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    Rate     local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per sec  %% %c    %% %c    us/Tr   us/Tr\n\n";

  char *cpu_title_tput = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Tput     CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    %-8.8s local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per sec  %% %c    %% %c    us/KB   us/KB\n\n";

  char *cpu_title_latency = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Latency  CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    usecs    local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per tran %% %c    %% %c    us/Tr   us/Tr\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c %s\n";

  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f  %-6.2f %-6.2f %-6.3f  %-6.3f %s\n";

  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";

  char *ksink_fmt = "\
Alignment      Offset         RoundTrip  Trans    Throughput\n\
Local  Remote  Local  Remote  Latency    Rate     %-8.8s/s\n\
Send   Recv    Send   Recv    usec/Tran  per sec  Outbound   Inbound\n\
%5d  %5d   %5d  %5d   %-6.3f   %-6.3f %-6.3f    %-6.3f\n";

  send_omni_inner(remote_host, legacy, "MIGRATED TCP REQUEST/RESPONSE TEST");

  if (legacy) {
    /* We are now ready to print all the information. If the user has
       specified zero-level verbosity, we will just print the local
       service demand, or the remote service demand. If the user has
       requested verbosity level 1, he will get the basic "streamperf"
       numbers. If the user has specified a verbosity of greater than 1,
       we will display a veritable plethora of background information
       from outside of this block as it it not cpu_measurement
       specific...  */

    if (confidence < 0) {
      /* we did not hit confidence, but were we asked to look for it? */
      if (iteration_max > 1) {
	display_confidence();
      }
    }

    if (local_cpu_usage || remote_cpu_usage) {

      switch (verbosity) {
      case 0:
	if (local_cpu_usage) {
	  fprintf(where,
		  cpu_fmt_0,
		  local_service_demand,
		  local_cpu_method,
		  ((print_headers) ||
		   (result_brand == NULL)) ? "" : result_brand);
	}
	else {
	  fprintf(where,
		  cpu_fmt_0,
		  remote_service_demand,
		  remote_cpu_method,
		  ((print_headers) ||
		   (result_brand == NULL)) ? "" : result_brand);
	}
	break;
      case 1:
      case 2:
	if (print_headers) {
	  if ('x' == libfmt) {
	    fprintf(where,
		    cpu_title,
		    local_cpu_method,
		    remote_cpu_method);
	  }
	  else {
	    fprintf(where,
		    cpu_title_tput,
		    format_units(),
		    local_cpu_method,
		    remote_cpu_method);
	  }
	}

	fprintf(where,
		cpu_fmt_1_line_1,		/* the format string */
		lss_size,		/* local sendbuf size */
		lsr_size,
		req_size,		/* how large were the requests */
		rsp_size,		/* guess */
		elapsed_time,		/* how long was the test */
		thruput,
		local_cpu_utilization,	/* local cpu */
		remote_cpu_utilization,	/* remote cpu */
		local_service_demand,	/* local service demand */
		remote_service_demand,	/* remote service demand */
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	fprintf(where,
		cpu_fmt_1_line_2,
		rss_size,
		rsr_size);
	break;
      }
    }
    else {
      /* The tester did not wish to measure service demand. */

      switch (verbosity) {
      case 0:
	fprintf(where,
		tput_fmt_0,
		thruput,
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,
		  ('x' == libfmt) ? tput_title : tput_title_band,
		  format_units());
	}

	fprintf(where,
		tput_fmt_1_line_1,	/* the format string */
		lss_size,
		lsr_size,
		req_size,		/* how large were the requests */
		rsp_size,		/* how large were the responses */
		elapsed_time, 		/* how long did it take */
		/* are we trans or do we need to convert to bytes then
		   bits? at this point, thruput is in our "confident"
		   transactions per second. we can convert to a
		   bidirectional bitrate by multiplying that by the sum
		   of the req_size and rsp_size.  we pass that to
		   calc_thruput_interval_omni with an elapsed time of
		   1.0 s to get it converted to [kmg]bits/s or
		   [KMG]Bytes/s */
		thruput,
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	fprintf(where,
		tput_fmt_1_line_2,
		rss_size, 		/* remote recvbuf size */
		rsr_size);

	break;
      }
    }

    /* it would be a good thing to include information about some of the */
    /* other parameters that may have been set for this test, but at the */
    /* moment, I do not wish to figure-out all the  formatting, so I will */
    /* just put this comment here to help remind me that it is something */
    /* that should be done at a later time. */

    /* how to handle the verbose information in the presence of */
    /* confidence intervals is yet to be determined... raj 11/94 */
    if (verbosity > 1) {
      /* The user wanted to know it all, so we will give it to him. */
      /* This information will include as much as we can find about */
      /* TCP statistics, the alignments of the sends and receives */
      /* and all that sort of rot... */

      /* normally, you might think that if we were messing about with
	 the value of libfmt we would need to put it back again, but
	 since this is basically the last thing we are going to do with
	 it, it does not matter.  so there :) raj 2007-06-08 */
      /* if the user was asking for transactions, then we report
	 megabits per second for the unidirectional throughput,
	 otherwise we use the desired units. */
      if ('x' == libfmt) {
	libfmt = 'm';
      }

      fprintf(where,
	      ksink_fmt,
	      format_units(),
	      local_send_align,
	      remote_recv_offset,
	      local_send_offset,
	      remote_recv_offset,
	      /* if the user has enable burst mode, we have to remember
		 to account for that in the number of transactions
		 outstanding at any one time. otherwise we will
		 underreport the latency of individual
		 transactions. learned from saf by raj 2007-06-08  */
	      (((double)1.0/transaction_rate)*(double)1000000.0) *
	      (double) (1 + ((first_burst_size > 0) ? first_burst_size : 0)),
	      transaction_rate,
	      calc_thruput_interval_omni(transaction_rate * (double)req_size,
					 1.0),
	      calc_thruput_interval_omni(transaction_rate * (double)rsp_size,
					 1.0));

#ifdef WANT_HISTOGRAM
      fprintf(where,"\nHistogram of request/response times\n");
      HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
      fflush(where);
    }
  }
}


void
send_tcp_conn_rr(char remote_host[])
{

  char *tput_title = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  Trans.\n\
Send   Recv   Size     Size    Time     Rate         \n\
bytes  Bytes  bytes    bytes   secs.    per sec   \n\n";

  char *tput_fmt_0 =
    "%7.2f\n";

  char *tput_fmt_1_line_1 = "\
%-6d %-6d %-6d   %-6d  %-6.2f   %7.2f   \n";
  char *tput_fmt_1_line_2 = "\
%-6d %-6d\n";

  char *cpu_title = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Trans.   CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    Rate     local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per sec  %%      %%      us/Tr   us/Tr\n\n";

  char *cpu_fmt_0 =
    "%6.3f\n";

  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f   %-6.2f %-6.2f %-6.3f  %-6.3f\n";

  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";

  char *ksink_fmt = "\n\
Alignment      Offset\n\
Local  Remote  Local  Remote\n\
Send   Recv    Send   Recv\n\
%5d  %5d   %5d  %5d\n";

  send_omni_inner(remote_host,
		  legacy,
		  "MIGRATED TCP Connect/Request/Response TEST");

  /* We are now ready to print all the information. If the user */
  /* has specified zero-level verbosity, we will just print the */
  /* local service demand, or the remote service demand. If the */
  /* user has requested verbosity level 1, he will get the basic */
  /* "streamperf" numbers. If the user has specified a verbosity */
  /* of greater than 1, we will display a veritable plethora of */
  /* background information from outside of this block as it it */
  /* not cpu_measurement specific...  */

  if (legacy) {
    if (confidence < 0) {
      /* we did not hit confidence, but were we asked to look for it? */
      if (iteration_max > 1) {
	display_confidence();
      }
    }

    if (local_cpu_usage || remote_cpu_usage) {

      switch (verbosity) {
      case 0:
	if (local_cpu_usage) {
	  fprintf(where,
		  cpu_fmt_0,
		  local_service_demand,
		  local_cpu_method);
	}
	else {
	  fprintf(where,
		  cpu_fmt_0,
		  remote_service_demand,
		  remote_cpu_method);
	}
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,
		  cpu_title,
		  local_cpu_method,
		  remote_cpu_method);
	}

	fprintf(where,
		cpu_fmt_1_line_1,	/* the format string */
		lss_size,		/* local sendbuf size */
		lsr_size,
		req_size,		/* how large were the requests */
		rsp_size,		/* guess */
		elapsed_time,		/* how long was the test */
		thruput,
		local_cpu_utilization,	/* local cpu */
		remote_cpu_utilization,	/* remote cpu */
		local_service_demand,	/* local service demand */
		remote_service_demand);	/* remote service demand */
	fprintf(where,
		cpu_fmt_1_line_2,
		rss_size,
		rsr_size);
	break;
      }
    }
    else {
      /* The tester did not wish to measure service demand. */

      switch (verbosity) {
      case 0:
	fprintf(where,
		tput_fmt_0,
		thruput);
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,tput_title,format_units());
	}

	fprintf(where,
		tput_fmt_1_line_1,	/* the format string */
		lss_size,
		lsr_size,
		req_size,		/* how large were the requests */
		rsp_size,		/* how large were the responses */
		elapsed_time, 		/* how long did it take */
		thruput);
	fprintf(where,
		tput_fmt_1_line_2,
		rss_size, 		/* remote recvbuf size */
		rsr_size);

	break;
      }
    }

    /* it would be a good thing to include information about some of the */
    /* other parameters that may have been set for this test, but at the */
    /* moment, I do not wish to figure-out all the  formatting, so I will */
    /* just put this comment here to help remind me that it is something */
    /* that should be done at a later time. */

    /* how to handle the verbose information in the presence of */
    /* confidence intervals is yet to be determined... raj 11/94 */
    if (verbosity > 1) {
      /* The user wanted to know it all, so we will give it to him. */
      /* This information will include as much as we can find about */
      /* TCP statistics, the alignments of the sends and receives */
      /* and all that sort of rot... */

      fprintf(where,
	      ksink_fmt,
	      local_send_align,
	      remote_recv_align,
	      local_send_offset,
	      remote_recv_offset);

#ifdef WANT_HISTOGRAM
      fprintf(where,"\nHistogram of request/response times\n");
      HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
      fflush(where);
    }
  }
}

void
send_udp_stream(char remote_host[])
{
  /**********************************************************************/
  /*									*/
  /*               	UDP Unidirectional Send Test                    */
  /*									*/
  /**********************************************************************/

  char *tput_title = "\
Socket  Message  Elapsed      Messages                \n\
Size    Size     Time         Okay Errors   Throughput\n\
bytes   bytes    secs            #      #   %s/sec\n\n";

  char *tput_fmt_0 =
    "%7.2f\n";

  char *tput_fmt_1 = "\
%6d  %6d   %-7.2f   %7"PRIu64" %6d    %7.2f\n\
%6d           %-7.2f   %7"PRIu64"           %7.2f\n\n";


  char *cpu_title = "\
Socket  Message  Elapsed      Messages                   CPU      Service\n\
Size    Size     Time         Okay Errors   Throughput   Util     Demand\n\
bytes   bytes    secs            #      #   %s/sec %% %c%c     us/KB\n\n";

  char *cpu_fmt_0 =
    "%6.2f %c\n";

  char *cpu_fmt_1 = "\
%6d  %6d   %-7.2f   %7"PRIu64" %6d    %7.1f     %-6.2f   %-6.3f\n\
%6d           %-7.2f   %7"PRIu64"           %7.1f     %-6.2f   %-6.3f\n\n";


  send_omni_inner(remote_host, legacy, "MIGRATED UDP STREAM TEST");

  if (legacy) {
    /* We are now ready to print all the information. If the user has
       specified zero-level verbosity, we will just print the local
       service demand, or the remote service demand. If the user has
       requested verbosity level 1, he will get the basic "streamperf"
       numbers. If the user has specified a verbosity of greater than
       1, we will display a veritable plethora of background
       information from outside of this block as it it not
       cpu_measurement specific...  */


    if (confidence < 0) {
      /* we did not hit confidence, but were we asked to look for it? */
      if (iteration_max > 1) {
	display_confidence();
      }
    }

    if (local_cpu_usage || remote_cpu_usage) {

      switch (verbosity) {
      case 0:
	if (local_cpu_usage) {
	  fprintf(where,
		  cpu_fmt_0,
		  local_service_demand,
		  local_cpu_method);
	}
	else {
	  fprintf(where,
		  cpu_fmt_0,
		  remote_service_demand,
		  local_cpu_method);
	}
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,
		  cpu_title,
		  format_units(),
		  local_cpu_method,
		  remote_cpu_method);
	}

	fprintf(where,
		cpu_fmt_1,		/* the format string */
		lss_size,		/* local sendbuf size */
		send_size,		/* how large were the sends */
		elapsed_time,		/* how long was the test */
		local_send_calls,
		failed_sends,
		local_send_thruput, 	/* what was the xfer rate */
		local_cpu_utilization,	/* local cpu */
		local_service_demand,	/* local service demand */
		rsr_size,
		elapsed_time,
		remote_receive_calls,
		remote_recv_thruput,
		remote_cpu_utilization,	/* remote cpu */
		remote_service_demand);	/* remote service demand */
	break;
      }
    }
    else {
      /* The tester did not wish to measure service demand. */
      switch (verbosity) {
      case 0:
	fprintf(where,
		tput_fmt_0,
		local_send_thruput);
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,tput_title,format_units());
	}
	fprintf(where,
		tput_fmt_1,		/* the format string */
		lss_size, 		/* local sendbuf size */
		send_size,		/* how large were the sends */
		elapsed_time, 		/* how long did it take */
		local_send_calls,
		failed_sends,
		local_send_thruput,
		rsr_size, 		/* remote recvbuf size */
		elapsed_time,
		remote_receive_calls,
		remote_recv_thruput);
	break;
      }
    }

#ifdef WANT_HISTOGRAM
    if (verbosity > 1) {
      fprintf(where,"\nHistogram of time spent in send() call\n");
      HIST_report(time_hist);
    }
#endif /* WANT_HISTOGRAM */
    fflush(where);
  }
}

void
send_udp_rr(char remote_host[])
{

  char *tput_title = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  Trans.\n\
Send   Recv   Size     Size    Time     Rate         \n\
bytes  Bytes  bytes    bytes   secs.    per sec   \n\n";

  char *tput_title_band = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  \n\
Send   Recv   Size     Size    Time     Throughput \n\
bytes  Bytes  bytes    bytes   secs.    %s/sec   \n\n";

  char *tput_fmt_0 =
    "%7.2f %s\n";

  char *tput_fmt_1_line_1 = "\
%-6d %-6d %-6d   %-6d  %-6.2f   %7.2f   %s\n";

  char *tput_fmt_1_line_2 = "\
%-6d %-6d\n";

  char *cpu_title = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Trans.   CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    Rate     local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per sec  %% %c    %% %c    us/Tr   us/Tr\n\n";

  char *cpu_title_tput = "\
Local /Remote\n\
Socket Size   Request Resp.  Elapsed Tput     CPU    CPU    S.dem   S.dem\n\
Send   Recv   Size    Size   Time    %-8.8s local  remote local   remote\n\
bytes  bytes  bytes   bytes  secs.   per sec  %% %c    %% %c    us/KB   us/KB\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c %s\n";

  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f   %-6.2f %-6.2f %-6.3f  %-6.3f %s\n";

  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";

  send_omni_inner(remote_host, legacy, "MIGRATED UDP REQUEST/RESPONSE TEST");

  if (legacy) {
    /* We are now ready to print all the information. If the user has
       specified zero-level verbosity, we will just print the local
       service demand, or the remote service demand. If the user has
       requested verbosity level 1, he will get the basic "streamperf"
       numbers. If the user has specified a verbosity of greater than
       1, we will display a veritable plethora of background
       information from outside of this block as it it not
       cpu_measurement specific...  */

    if (confidence < 0) {
      /* we did not hit confidence, but were we asked to look for it? */
      if (iteration_max > 1) {
	display_confidence();
      }
    }

    if (local_cpu_usage || remote_cpu_usage) {

      switch (verbosity) {
      case 0:
	if (local_cpu_usage) {
	  fprintf(where,
		  cpu_fmt_0,
		  local_service_demand,
		  local_cpu_method,
		  ((print_headers) ||
		   (result_brand == NULL)) ? "" : result_brand);

	}
	else {
	  fprintf(where,
		  cpu_fmt_0,
		  remote_service_demand,
		  remote_cpu_method,
		  ((print_headers) ||
		   (result_brand == NULL)) ? "" : result_brand);

	}
	break;
      case 1:
      case 2:
	if (print_headers) {
	  if ('x' == libfmt) {
	    fprintf(where,
		    cpu_title,
		    local_cpu_method,
		    remote_cpu_method);
	  }
	  else {
	    fprintf(where,
		    cpu_title_tput,
		    format_units(),
		    local_cpu_method,
		    remote_cpu_method);
	  }
	}

	fprintf(where,
		cpu_fmt_1_line_1,		/* the format string */
		lss_size,		/* local sendbuf size */
		lsr_size,
		req_size,		/* how large were the requests */
		rsp_size,		/* guess */
		elapsed_time,		/* how long was the test */
		thruput,
		local_cpu_utilization,	/* local cpu */
		remote_cpu_utilization,	/* remote cpu */
		local_service_demand,	/* local service demand */
		remote_service_demand,	/* remote service demand */
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	fprintf(where,
		cpu_fmt_1_line_2,
		rss_size,
		rsr_size);
	break;
      }
    }
    else {
      /* The tester did not wish to measure service demand. */
      switch (verbosity) {
      case 0:
	fprintf(where,
		tput_fmt_0,
		thruput,
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	break;
      case 1:
      case 2:
	if (print_headers) {
	  fprintf(where,
		  ('x' == libfmt) ? tput_title : tput_title_band,
		  format_units());
	}

	fprintf(where,
		tput_fmt_1_line_1,	/* the format string */
		lss_size,
		lsr_size,
		req_size,		/* how large were the requests */
		rsp_size,		/* how large were the responses */
		elapsed_time, 		/* how long did it take */
		thruput,
		((print_headers) ||
		 (result_brand == NULL)) ? "" : result_brand);
	fprintf(where,
		tput_fmt_1_line_2,
		rss_size, 		/* remote recvbuf size */
		rsr_size);

	break;
      }
    }

    /* it would be a good thing to include information about some of the */
    /* other parameters that may have been set for this test, but at the */
    /* moment, I do not wish to figure-out all the  formatting, so I will */
    /* just put this comment here to help remind me that it is something */
    /* that should be done at a later time. */

    /* how to handle the verbose information in the presence of */
    /* confidence intervals is yet to be determined... raj 11/94 */

    if (verbosity > 1) {
      /* The user wanted to know it all, so we will give it to him. */
      /* This information will include as much as we can find about */
      /* UDP statistics, the alignments of the sends and receives */
      /* and all that sort of rot... */

#ifdef WANT_HISTOGRAM
      fprintf(where,"\nHistogram of request/reponse times.\n");
      HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
    }
    fflush(where);
  }
}


#endif /* WANT_MIGRATION */


/* using legacy test names will cause certain default settings to be
   made before we scan the test-specific arguments.  raj 2010-07-20 */
static void
set_omni_defaults_by_legacy_testname() {

  /* the uber defaults are for a unidirectional test using TCP */
  protocol = IPPROTO_TCP;
  socket_type = SOCK_STREAM;
  connection_test = 0;
  req_size = rsp_size = -1;
  was_legacy = 1;
  legacy = 1;
  implicit_direction = 0;  /* do we allow certain options to
			      implicitly affect the test direction? */
  if (strcasecmp(test_name,"TCP_STREAM") == 0) {
    direction = NETPERF_XMIT;
  }
  else if (strcasecmp(test_name,"TCP_MAERTS") == 0) {
    direction = NETPERF_RECV;
  }
  else if (strcasecmp(test_name,"TCP_RR") == 0) {
    req_size = rsp_size = 1;
    direction = 0;
    direction |= NETPERF_XMIT;
    direction |= NETPERF_RECV;
  }
  else if (strcasecmp(test_name,"UDP_STREAM") == 0) {
     protocol = IPPROTO_UDP;
    socket_type = SOCK_DGRAM;
  }
  else if (strcasecmp(test_name,"UDP_RR") == 0) {
     protocol = IPPROTO_UDP;
    socket_type = SOCK_DGRAM;
    direction = 0;
    direction |= NETPERF_XMIT;
    direction |= NETPERF_RECV;
    req_size = rsp_size = 1;
  }
  else if (strcasecmp(test_name,"TCP_CC") == 0) {
    direction = 0;
    connection_test = 1;
  }
  else if (strcasecmp(test_name,"TCP_CRR") == 0) {
    direction = 0;
    direction |= NETPERF_XMIT;
    direction |= NETPERF_RECV;
    req_size = rsp_size = 1;
    connection_test = 1;
  }
  else if (strcasecmp(test_name,"omni") == 0) {
    /* there is not much to do here but clear the legacy flag */
    was_legacy = 0;
    legacy = 0;
    implicit_direction = 1;
  }
  socket_type_str = hst_to_str(socket_type);
}

char omni_usage[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
OMNI and Migrated BSD Sockets Test Options:\n\
    -b number         Send number requests at start of _RR tests\n\
    -c                Explicitly declare this a connection test such as\n\
                      TCP_CRR or TCP_CC\n\
    -C                Set TCP_CORK when available\n\
    -d direction      Explicitly set test direction based on bitwise OR\n\
                      of 0x2 for transmit and 0x4 for receive. Default:\n\
                      based on test type\n\
    -D [L][,R]        Set TCP_NODELAY locally and/or remotely (TCP_*)\n\
    -f                Set IP_MTU_DISCOVER:IP_PMTU_DISC_DO where available\n\
    -F                Use TCP Fast Open functionality where available\n\
    -h                Display this text\n\
    -H name[/mask],fam  Use name (or IP) and family as target of data connection\n\
                      A mask value will cause randomization of the IP used\n\
    -k [file]         Generate keyval output optionally based on file\n\
                      Use filename of '?' to get the list of choices\n\
    -K loc[,rem]      Set the local and/or remote congestion control\n\
                      algorithm to use on those platforms where it can\n\
                      be set.\n\
    -L name[/mask],fam  Use name (or IP) and family as source of data connection\n\
                      A mask value will cause randomization of the IP used\n\
    -m local,remote   Set the send size for _STREAM/_MAERTS tests\n\
    -M local,remote   Set the recv size for _STREAM/_MAERTS tests\n\
    -n                Use the connected socket for UDP locally\n\
    -N                Use the connected socket for UDP remotely\n\
    -o [file]         Generate CSV output optionally based on file\n\
                      Use filename of '?' to get the list of choices\n\
    -O [file]         Generate classic-style output based on file\n\
                      Use filename of '?' to get the list of choices\n\
    -p min[,max]      Set the min/max port numbers for TCP_CRR, TCP_TRR\n\
    -P local[,remote] Set the local/remote port for the data socket\n\
    -r req,[rsp]      Set request/response sizes (TCP_RR, UDP_RR)\n\
    -R 0/1            Allow routing of traffic on data connection.\n\
                      Default: 0 (off) for UDP_STREAM, 1 (on) otherwise\n\
    -s send[,recv]    Set local socket send/recv buffer sizes\n\
    -S send[,recv]    Set remote socket send/recv buffer sizes\n\
    -t type           Explicitly set socket type. Default is implicit\n\
                      based on other settings\n\
    -T protocol       Explicitly set data connection protocol. Default is\n\
                      implicit based on other settings\n\
    -u uuid           Use the supplied string as the UUID for this test.\n\
    -4                Use AF_INET (eg IPv4) on both ends of the data conn\n\
    -6                Use AF_INET6 (eg IPv6) on both ends of the data conn\n\
\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n";

void
print_omni_usage()
{

  fwrite(omni_usage, sizeof(char), strlen(omni_usage), stdout);
  exit(1);

}


void
scan_omni_args(int argc, char *argv[])

{

#define OMNI_ARGS "aBb:cCd:De:fFgG:hH:i:Ij:kK:l:L:m:M:nNoOp:P:q:r:R:s:S:t:T:u:UVw:W:46"

  extern char	*optarg;	  /* pointer to option string	*/

  int		c;
  int           have_uuid = 0;
  int           have_R_option = 0;

  char
    arg1[BUFSIZ],  /* argument holders		*/
    arg2[BUFSIZ],
    arg3[BUFSIZ];

  if (debug) {
    int i;
    printf("%s called with the following argument vector\n",
	   __FUNCTION__);
    for (i = 0; i< argc; i++) {
      printf("%s ",argv[i]);
    }
    printf("\n");
  }

  /* double-check struct sizes */
  {
    const union netperf_request_struct * u = (const union netperf_request_struct *)0;
    if (debug) {
      fprintf(where, "sizeof(omni_request_struct)=%d/%d\n",
              (int)sizeof(struct omni_request_struct),
              (int)sizeof(u->content.test_specific_data));
      fprintf(where, "sizeof(omni_response_struct)=%d/%d\n",
              (int)sizeof(struct omni_response_struct),
              (int)sizeof(u->content.test_specific_data));
      fprintf(where, "sizeof(omni_results_struct)=%d/%d\n",
              (int)sizeof(struct omni_results_struct),
              (int)sizeof(u->content.test_specific_data));
    }
    assert(sizeof(struct omni_request_struct)
           <= sizeof(u->content.test_specific_data));
    assert(sizeof(struct omni_response_struct)
           <= sizeof(u->content.test_specific_data));
    assert(sizeof(struct omni_results_struct)
           <= sizeof(u->content.test_specific_data));
  }

  strncpy(local_data_port,"0",sizeof(local_data_port));
  strncpy(remote_data_port,"0",sizeof(remote_data_port));

  /* this will handle setting default settings based on test name */
  set_omni_defaults_by_legacy_testname();

  /* Go through all the command line arguments and break them out. For
     those options that take two parms, specifying only the first will
     set both to that value. Specifying only the second will leave the
     first untouched. To change only the first, use the form "first,"
     (see the routine break_args.. */

  while ((c= getopt(argc, argv, OMNI_ARGS)) != EOF) {
    switch (c) {
    case '?':
    case '4':
      remote_data_family = AF_INET;
      local_data_family = AF_INET;
      break;
    case '6':
#if defined(AF_INET6)
      remote_data_family = AF_INET6;
      local_data_family = AF_INET6;
#else
      fprintf(stderr,
	      "This netperf was not compiled on an IPv6 capable host!\n");
      fflush(stderr);
      exit(-1);
#endif
      break;
    case 'h':
      print_omni_usage();
      exit(1);
    case 'a':
      want_defer_accept = 1;
      break;
    case 'B':
      want_use_pktinfo = 1;
      break;
    case 'b':
#ifdef WANT_FIRST_BURST
      first_burst_size = atoi(optarg);
#else /* WANT_FIRST_BURST */
      printf("Initial request burst functionality not compiled-in!\n");
#endif /* WANT_FIRST_BURST */
      break;
    case 'c':
      /* this is a connection test */
      connection_test = 1;
      break;
    case 'C':
#ifdef TCP_CORK
      /* set TCP_CORK */
      loc_tcpcork = 1;
      rem_tcpcork = 1; /* however, at first, we ony have cork affect loc */
#else
      printf("WARNING: TCP_CORK not available on this platform!\n");
#endif /* TCP_CORK */
      break;
    case 'd':
      /* arbitrarily set the direction variable, but only for an
	 actual omni test and then disable implicit setting of
	 direction */
      if (!was_legacy) {
	direction = parse_direction(optarg);
	implicit_direction = 0;
      }
      break;
    case 'D':
      /* set the TCP nodelay flag */
      loc_nodelay = 1;
      rem_nodelay = 1;
      break;
    case 'f':
#ifdef IP_MTU_DISCOVER
      use_mtu_discover = 1;
#else
      printf("WARNING: IP_MTU_DISCOVER not available on this platform\n");
#endif
      break;
    case 'F':
#if defined(MSG_FASTOPEN)
      use_fastopen = 1;
#else
      printf("WARNING: TCP FASTOPEN not available on this platform!\n");
#endif
      break;
    case 'e':
      /* set the rEceive timeout */
      receive_timeout = atoi(optarg);
      break;
    case 'g':
      /* enable SO_DEBUG, or at least make the attempt, on the data socket */
      socket_debug = 1;
      break;
    case 'G':
      /* set the value for a tcp_maxseG call*/
      transport_mss_req = atoi(optarg);
      break;
    case 'H':
      break_args_explicit_sep(optarg,',',arg1,arg2);
      if (arg1[0]) {
	/* check to see if there was a width, which we would want to
	   be arg3. for simplicities sake, we will assume the width
	   must follow the address and not the address family - ie
	   1.2.3.4/24,inet.  This means we can just pass optarg again
	   as the source rather than have to shuffle arg values. */
	break_args_explicit_sep(optarg,'/',arg1,arg3);
	if (arg1[0]) {
	  remote_data_address = malloc(strlen(arg1)+1);
	  strcpy(remote_data_address,arg1);
	  explicit_data_address = 1;
	}
	if (arg3[0]) {
	  remote_mask_len = convert(arg3);
	}
      }
      if (arg2[0]) {
	remote_data_family = parse_address_family(arg2);
      }
      break;
    case 'i':
      fprintf(stderr,"The support for check_interval has been removed because the contributing editor no longer knew what it was for\n");
      fflush(stderr);
      break;
    case 'I':
      use_write = 1;
      break;
    case 'j':
      parallel_connections = atoi(optarg);
      break;
    case 'k':
      netperf_output_mode = KEYVAL;
      legacy = 0;
      /* obliterate any previous file name */
      if (output_selection_spec) {
	free(output_selection_spec);
	output_selection_spec = NULL;
      }
      if (argv[optind] && ((unsigned char)argv[optind][0] != '-')) {
	/* we assume that what follows is the name of a file with the
	   list of desired output values. */
	output_selection_spec = strdup(argv[optind]);
	optind++;
	/* special case - if the file name is "?" then we will emit a
	   list of the available outputs */
	if (strcmp(output_selection_spec,"?") == 0) {
	  dump_netperf_output_choices(stdout,1);
	  exit(1);
	}
      }
      break;
    case 'K':
      /* "Kongestion Kontrol */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	strncpy(local_cong_control_req,arg1,sizeof(local_cong_control_req));
      if (arg2[2])
	strncpy(remote_cong_control_req,arg2,sizeof(remote_cong_control_req));
      break;
    case 'l':
      multicast_ttl = atoi(optarg);
      break;
    case 'L':
      break_args_explicit_sep(optarg,',',arg1,arg2);
      if (arg1[0]) {
	/* check to see if there was a width, which we would want to
	   be arg3. for simplicities sake, we will assume the width
	   must follow the address and not the address family - ie
	   1.2.3.4/24,inet.  This means we can just pass optarg again
	   as the source rather than have to shuffle arg values. */
	break_args_explicit_sep(optarg,'/',arg1,arg3);
	if (arg1[0]) {
	  local_data_address = malloc(strlen(arg1)+1);
	  strcpy(local_data_address,arg1);
	}
	if (arg3[0]) {
	  local_mask_len = convert(arg3);
	}
      }
      if (arg2[0]) {
	local_data_family = parse_address_family(arg2);
      }
      break;
    case 'm':
      /* set the send size. if we set the local send size it will add
	 XMIT to direction.  if we set the remote send size it will
	 add RECV to the direction.  likely as not this will need some
	 additional throught */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	send_size = convert(arg1);
	if (implicit_direction)
	  direction |= NETPERF_XMIT;
      }
      if (arg2[0]) {
	remote_send_size_req = convert(arg2);
	if (implicit_direction)
	  direction |= NETPERF_RECV;
      }
      break;
    case 'M':
      /* set the recv sizes.  if we set the local recv size it will
	 add RECV to direction.  if we set the remote recv size it
	 will add XMIT to direction  */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	remote_recv_size_req = convert(arg1);
	if (implicit_direction)
	  direction |= NETPERF_XMIT;
      }
      if (arg2[0]) {
	recv_size = convert(arg2);
	if (implicit_direction)
	  direction |= NETPERF_RECV;
      }
      break;
    case 'n':
      /* set the local socket type */
      local_connected = 1;
      break;
    case 'N':
      /* set the remote socket type */
      remote_connected = 1;
      break;
    case 'o':
      netperf_output_mode = CSV;
      legacy = 0;
      /* obliterate any previous file name */
      if (output_selection_spec) {
	free(output_selection_spec);
	output_selection_spec = NULL;
      }
      if (output_selection_spec) {
	free(output_selection_spec);
	output_selection_spec = NULL;
      }
      if (argv[optind] && ((unsigned char)argv[optind][0] != '-')) {
	/* we assume that what follows is the name of a file with the
	   list of desired output values. */
	output_selection_spec = strdup(argv[optind]);
	optind++;
	/* special case - if the file name is "?" then we will emit a
	   list of the available outputs */
	if (strcmp(output_selection_spec,"?") == 0) {
	  dump_netperf_output_choices(stdout,1);
	  exit(1);
	}
      }
      break;
    case 'O':
      netperf_output_mode = HUMAN;
      legacy = 0;
      /* obliterate any previous file name */
      if (output_selection_spec) {
	free(output_selection_spec);
	output_selection_spec = NULL;
      }
      if (argv[optind] && ((unsigned char)argv[optind][0] != '-')) {
	/* we assume that what follows is the name of a file with the
	   list of desired output values */
	output_selection_spec = strdup(argv[optind]);
	optind++;
	if (strcmp(output_selection_spec,"?") == 0) {
	  dump_netperf_output_choices(stdout,0);
	  exit(1);
	}
      }
      break;
    case 'p':
      /* set the min and max port numbers for the TCP_CRR and TCP_TRR */
      /* tests. */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	client_port_min = atoi(arg1);
      if (arg2[0])
	client_port_max = atoi(arg2);
      break;
    case 'P':
      /* set the local and remote data port numbers for the tests to
	 allow them to run through those blankety blank end-to-end
	 breaking firewalls. raj 2004-06-15 */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	strncpy(local_data_port,arg1,sizeof(local_data_port));
      if (arg2[0])
	strncpy(remote_data_port,arg2,sizeof(remote_data_port));
      break;
    case 'q':
      /* set the local socket pacing rate */
      pacing_rate = atoi(optarg);
      break;
    case 'r':
      /* set the request/response sizes. setting request/response
	 sizes implicitly sets direction to XMIT and RECV */
      if (implicit_direction) {
	direction |= NETPERF_XMIT;
	direction |= NETPERF_RECV;
      }
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	req_size = convert(arg1);
      if (arg2[0])
	rsp_size = convert(arg2);
      break;
    case 'R':
      routing_allowed = atoi(optarg);
      have_R_option = 1;
      break;
    case 's':
      /* set local socket sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	lss_size_req = convert(arg1);
      if (arg2[0])
	lsr_size_req = convert(arg2);
      break;
    case 'S':
      /* set remote socket sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	rss_size_req = convert(arg1);
      if (arg2[0])
	rsr_size_req = convert(arg2);
      break;
    case 't':
      /* set the socket type */
      socket_type = parse_socket_type(optarg);
      break;
    case 'T':
      /* set the protocol - aka "Transport" */
      protocol = parse_protocol(optarg);
      break;
    case 'u':
      /* use the supplied string as the UUID for this test. at some
	 point we may want to sanity check the string we are given but
	 for now we won't worry about it */
      strncpy(test_uuid,optarg,sizeof(test_uuid));
      /* strncpy may leave us with a string without a null at the end */
      test_uuid[sizeof(test_uuid) - 1] = 0;
      have_uuid = 1;
      break;
    case 'U':
      /* we don't want to give-up on the failure of a connect() call */
      dont_give_up = 1;
      break;
    case 'W':
      /* set the "width" of the user space data */
      /* buffer. This will be the number of */
      /* send_size buffers malloc'd in the */
      /* *_STREAM test. It may be enhanced to set */
      /* both send and receive "widths" but for now */
      /* it is just the sending *_STREAM. */
      send_width = convert(optarg);
      break;
    case 'V' :
      /* we want to do copy avoidance and will set */
      /* it for everything, everywhere, if we really */
      /* can. of course, we don't know anything */
      /* about the remote... */
      loc_sndavoid = 1;
      loc_rcvavoid = 1;
      rem_sndavoid = 1;
      rem_rcvavoid = 1;
      break;
    };
  }

  /* generate the UUID for this test if the user has not supplied it */
  if (!have_uuid)
    get_uuid_string(test_uuid,sizeof(test_uuid));

  protocol_str = protocol_to_str(protocol);
  /* ok, if we have gone through all that, and direction is still
     zero, let us see if it needs to be set to something else. */
  if ((0 == direction) && (!connection_test)) direction = NETPERF_XMIT;
  direction_str = direction_to_str(direction);

  /* to cover the backside of blithering idiots who run unidirectional
     UDP tests on test setups where they might trash their corporate
     WAN, we grudgingly provide a safety latch. unless explicitly
     enabled, UDP_STREAM/UDP_MAERTS sockets will not allow themselves
     to be routed via a gateway. raj 20091026 */

  if ((!have_R_option) &&
      (protocol == IPPROTO_UDP) &&
      (!NETPERF_IS_RR(direction))) {
    routing_allowed = 0;
  }

  /* some other sanity checks we need to make would include stuff when
     the user has set -m and -M such that both XMIT and RECV are set
     and has not set -r. initially we will not allow that.  at some
     point we might allow that if the user has also set -r, but until
     then the code will simply ignore the values from -m and -M when
     -r is set. */

#if defined(WANT_HISTOGRAM)
  if (verbosity > 1) keep_histogram = 1;
#endif

  /* did the user use -d 6 but not set -r? */
  if (NETPERF_IS_RR(direction) && !NETPERF_CC(direction)) {
    if (req_size == -1)
      req_size = 1;
    if (rsp_size == -1)
      rsp_size = 1;
  }

  /* ok, time to sanity check the output units */
  if ('?' == libfmt) {
    /* if this is a RR test then set it to 'x' for transactions */
    if (NETPERF_IS_RR(direction)) {
      libfmt = 'x';
    }
    else {
      libfmt = 'm';
    }
  }
  else if ('x' == libfmt) {
    /* now, a format of 'x' makes no sense for anything other than
       an RR test. if someone has been silly enough to try to set
       that, we will reset it silently to default - namely 'm' */
    if (!NETPERF_IS_RR(direction)) {
      libfmt = 'm';
    }
  }

  /* this needs to be strdup :) */
  thruput_format_str = strdup(format_units());

  /* so, if there is to be no control connection, we want to have some
     different settings for a few things */

  if (no_control) {
    if (strcmp(remote_data_port,"0") == 0) {
      /* we need to select either the discard port, echo port or
	 chargen port dedepending on the test direction. raj
	 20101220 */
      if (NETPERF_XMIT_ONLY(direction)) {
	strncpy(remote_data_port,"discard",sizeof(remote_data_port));
	recv_size = -1;
      }
      else if (NETPERF_RECV_ONLY(direction)) {
	strncpy(remote_data_port,"chargen",sizeof(remote_data_port));
	send_size = -1;
      }
      else if (NETPERF_IS_RR(direction) || NETPERF_CC(direction)) {
	strncpy(remote_data_port,"echo",sizeof(remote_data_port));
	rsp_size = req_size;
      }
      else {
	printf("No default port known for the %s test, please set one yourself\n",test_name);
	exit(-1);
      }
    }
    remote_data_port[sizeof(remote_data_port) - 1] = '\0';

    /* I go back and forth on whether these should become -1 or if
       they should become 0 for a no_control test. what do you think?
       raj 2006-02-08 */

    rem_rcvavoid = -1;
    rem_sndavoid = -1;
    rss_size_req = -1;
    rsr_size_req = -1;
    rem_nodelay = -1;

  }
  /* so, did the user request a few things implicitly via output selection? */
  if (!legacy)
    print_omni_init();

  if (desired_output_groups & OMNI_WANT_STATS) {
    keep_statistics = 1;
    keep_histogram = 1;
  }

}

#endif /* WANT_OMNI */
