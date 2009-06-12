#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WANT_OMNI
char nettest_omni_id[]="\
@(#)nettest_omni.c (c) Copyright 2008 Hewlett-Packard Co. Version 2.5.0pre";

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

#ifdef WANT_SCTP
#include <netinet/sctp.h>
#endif

#ifndef WIN32
#if !defined(__VMS)
#include <sys/ipc.h>
#endif /* !defined(__VMS) */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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
#include <windows.h>

#define sleep(x) Sleep((x)*1000)

#define __func__ __FUNCTION__
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

#if defined(WANT_HISTOGRAM) || defined(WANT_DEMO) 
#include "hist.h"
#endif /* WANT_HISTOGRAM */

#ifdef WANT_HISTOGRAM
#ifdef HAVE_GETHRTIME
static hrtime_t time_one;
static hrtime_t time_two;
#elif HAVE_GET_HRT
#include "hrt.h"
static hrt_t time_one;
static hrt_t time_two;
#elif defined(WIN32)
static LARGE_INTEGER time_one;
static LARGE_INTEGER time_two;
#else
static struct timeval time_one;
static struct timeval time_two;
#endif /* HAVE_GETHRTIME */
static HIST time_hist;
#endif /* WANT_HISTOGRAM */

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

/* for a _STREAM test, "a" should be lss_size and "b" should be
   rsr_size. for a _MAERTS test, "a" should be lsr_size and "b" should
   be rss_size. raj 2005-04-06 */
#define DEMO_STREAM_SETUP(a,b) \
    if ((demo_mode) && (demo_units == 0)) { \
      /* take our default value of demo_units to be the larger of \
	 twice the remote's SO_RCVBUF or twice our SO_SNDBUF */ \
      if (a > b) { \
	demo_units = 2*a; \
      } \
      else { \
	demo_units = 2*b; \
      } \
    }

#define DEMO_INTERVAL(units) \
      if (demo_mode) { \
	double actual_interval; \
	units_this_tick += units; \
	if (units_this_tick >= demo_units) { \
	  /* time to possibly update demo_units and maybe output an \
	     interim result */ \
	  HIST_timestamp(demo_two_ptr); \
	  actual_interval = delta_micro(demo_one_ptr,demo_two_ptr); \
	  /* we always want to fine-tune demo_units here whether we \
	     emit an interim result or not.  if we are short, this \
	     will lengthen demo_units.  if we are long, this will \
	     shorten it */ \
	  demo_units = demo_units * (demo_interval / actual_interval); \
	  if (actual_interval >= demo_interval) { \
	    /* time to emit an interim result */ \
	    fprintf(where, \
		    "Interim result: %7.2f %s/s over %.2f seconds\n", \
		    calc_thruput_interval(units_this_tick, \
					  actual_interval/1000000.0), \
		    format_units(), \
		    actual_interval/1000000.0); \
	    units_this_tick = 0.0; \
	    /* now get a new starting timestamp.  we could be clever \
	       and swap pointers - the math we do probably does not \
	       take all that long, but for now this will suffice */ \
	    temp_demo_ptr = demo_one_ptr; \
	    demo_one_ptr = demo_two_ptr; \
	    demo_two_ptr = temp_demo_ptr; \
	  } \
	} \
      }

#define DEMO_STREAM_INTERVAL(units) DEMO_INTERVAL(units)

#define DEMO_RR_SETUP(a) \
    if ((demo_mode) && (demo_units == 0)) { \
      /* take whatever we are given */ \
	demo_units = a; \
    }

#define DEMO_RR_INTERVAL(units) DEMO_INTERVAL(units)

#endif 

#ifdef WANT_INTERVALS
int interval_count;
#ifndef WANT_SPIN
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
    /* get the signal set for the call to sigsuspend */ \
    if (sigprocmask(SIG_BLOCK, (sigset_t *)NULL, &signal_set) != 0) { \
      fprintf(where, \
	      "%s: unable to get sigmask errno %d\n", \
	      __func__, \
	      errno); \
      fflush(where); \
      exit(1); \
    }

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
	if (sigsuspend(&signal_set) == EFAULT) { \
	  fprintf(where, \
		  "%s: fault with sigsuspend.\n", \
                  __func__); \
	  fflush(where); \
	  exit(1); \
	} \
	interval_count = interval_burst; \
      }
#else
/* first out timestamp */
#ifdef HAVE_GETHRTIME
static hrtime_t intvl_one;
static hrtime_t intvl_two;
static hrtime_t *intvl_one_ptr = &intvl_one;
static hrtime_t *intvl_two_ptr = &intvl_two;
static hrtime_t *temp_intvl_ptr = &intvl_one;
#elif defined(WIN32)
static LARGE_INTEGER intvl_one;
static LARGE_INTEGER intvl_two;
static LARGE_INTEGER *intvl_one_ptr = &intvl_one;
static LARGE_INTEGER *intvl_two_ptr = &intvl_two;
static LARGE_INTEGER *temp_intvl_ptr = &intvl_one;
#else
static struct timeval intvl_one;
static struct timeval intvl_two;
static struct timeval *intvl_one_ptr = &intvl_one;
static struct timeval *intvl_two_ptr = &intvl_two;
static struct timeval *temp_intvl_ptr = &intvl_one;
#endif

#define INTERVALS_INIT() \
      if (interval_burst) { \
	HIST_timestamp(intvl_one_ptr); \
      } \
      interval_count = interval_burst; \

#define INTERVALS_WAIT() \
      /* in this case, the interval count is the count-down couter \
	 to decide to sleep for a little bit */ \
      if ((interval_burst) && (--interval_count == 0)) { \
	/* call sigsuspend and wait for the interval timer to get us \
	   out */ \
	if (debug > 1) { \
	  fprintf(where,"about to spin suspend\n"); \
	  fflush(where); \
	} \
        HIST_timestamp(intvl_two_ptr); \
        while(delta_micro(intvl_one_ptr,intvl_two_ptr) < interval_usecs) { \
	  HIST_timestamp(intvl_two_ptr); \
	} \
	temp_intvl_ptr = intvl_one_ptr; \
	intvl_one_ptr = intvl_two_ptr; \
	intvl_two_ptr = temp_intvl_ptr; \
	interval_count = interval_burst; \
      }
#endif
#endif

#define NETPERF_WAITALL 0x1
#define NETPERF_XMIT 0x2
#define NETPERF_RECV 0x4

#define NETPERF_IS_RR(x) (((x & NETPERF_XMIT) && (x & NETPERF_RECV)) || \
			  (!((x & NETPERF_XMIT) || (x & NETPERF_RECV))))

#define NETPERF_RECV_ONLY(x) ((x & NETPERF_RECV) && !(x & NETPERF_XMIT))

#define NETPERF_XMIT_ONLY(x) ((x & NETPERF_XMIT) && !(x & NETPERF_RECV))

#define NETPERF_CC(x) (!(x & NETPERF_XMIT) && !(x & NETPERF_RECV))

extern void get_uuid_string(char *string, size_t size);

/* a boatload of globals while I settle things out */
char *csv_selection_file = NULL;
char *human_selection_file = NULL;
char *keyword_selection_file = NULL;

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
#if 0
int remote_send_dirty_count;
int remote_recv_dirty_count;
int remote_recv_clean_count;
#endif
extern int loc_dirty_count;
extern int loc_clean_count;
extern int rem_dirty_count;
extern int rem_clean_count;
int remote_checksum_off;
int connection_test;
int need_to_connect;
int need_connection;
int bytes_to_send;
double bytes_per_send;
int failed_sends;
int bytes_to_recv;
double bytes_per_recv;
int null_message_ok = 0;
int csv = 0;
int keyword = 0;
uint64_t      trans_completed = 0;
uint64_t      units_remaining;
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
double      local_cpu_utilization_double;
double      local_service_demand_double;
double      remote_cpu_utilization_double;
double      remote_service_demand_double;
double      transaction_rate = 1.0;
double      rtt_latency = -1.0;
int32_t     transport_mss = -2;
char        *local_interface_name=NULL;
char        *remote_interface_name=NULL;
char        local_driver_name[32]="";
char        local_driver_version[32]="";
char        local_driver_firmware[32]="";
char        local_driver_bus[32]="";
char        remote_driver_name[32]="";
char        remote_driver_version[32]="";
char        remote_driver_firmware[32]="";
char        remote_driver_bus[32]="";
char        *local_interface_slot=NULL;
char        *remote_interface_slot=NULL;
int         remote_interface_vendor;
int         remote_interface_device;
int         remote_interface_subvendor;
int         remote_interface_subdevice;
int         local_interface_vendor;
int         local_interface_device;
int         local_interface_subvendor;
int         local_interface_subdevice;
char        *local_system_model;
char        *local_cpu_model;
int         local_cpu_frequency;
char        *remote_system_model;
char        *remote_cpu_model;
int         remote_cpu_frequency;

int         local_security_type_id;
int         local_security_enabled_num;
char        *local_security_type;
char        *local_security_enabled;
char        *local_security_specific;
int         remote_security_type_id;
int         remote_security_enabled_num;
char        *remote_security_enabled;
char        *remote_security_type;
char        *remote_security_specific;

int printing_initialized = 0;

char *sd_str;
char *thruput_format_str;

char *socket_type_str;
char *protocol_str;
char *direction_str;

extern int first_burst_size;

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

int
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

/* you should add to this in the order in which they should appear in
   the default csv (everything) output */

enum netperf_output_name {
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
  LOCAL_CPU_METHOD,
  LOCAL_SD,
  REMOTE_CPU_UTIL,
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
  COMMAND_LINE,
  OUTPUT_END,
  NETPERF_OUTPUT_MAX
};

typedef struct netperf_output_elt {
  enum netperf_output_name output_name;  /* belt and suspenders */
  int max_line_len; /* length of the longest of the "lines" */
  int tot_line_len; /* total length of all lines, including spaces */
  char *line[4];
  char *brief;          /* the brief name of the value */
  char *format;         /* format to apply to value */
  void *display_value;  /* where to find the value */
} netperf_output_elt_t;

netperf_output_elt_t netperf_output_source[NETPERF_OUTPUT_MAX];

/* the list of things we will emit for CSV output.  I suppose we could
   at some point try to make this a special case of output_human_list,
   or at least use some of that space... but for now we won't worry
   about it.  that can come after things are actually working :) raj
   2008-01-23  */
enum netperf_output_name output_csv_list[NETPERF_OUTPUT_MAX];

/* the list of things we will emit for "human" output. up to
   NETPERF_MAX_BLOCKS of output (groups of lines) each out to
   NETPERF_OUTPUT_MAX entries. that should more than cover it */

#define NETPERF_MAX_BLOCKS 4
enum netperf_output_name output_human_list[NETPERF_MAX_BLOCKS][NETPERF_OUTPUT_MAX];

char *direction_to_str(int direction) {
  if (NETPERF_RECV_ONLY(direction)) return "Receive";
  if (NETPERF_XMIT_ONLY(direction)) return "Send";
  if (NETPERF_CC(direction)) return "Connection";
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
      myport = client_port_min;
    }
    /* there will be a ++ before the first call to bind, so subtract one */
    myport--;
    myport_init = 1;
  }
    
 newport:
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
    myport = client_port_min;
  }
  
  /* set up the data socket */
  set_port_number(local_res, (unsigned short)myport);
}

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
dump_netperf_output_list(FILE *where, int csv) {
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
	    "Output Name: %s\n",
	    netperf_output_enum_to_str(netperf_output_source[i].output_name));
    fprintf(where,
	    "\tmax_line_len %d tot_line_len %d display_value %p\n",
	    netperf_output_source[i].max_line_len,
	    netperf_output_source[i].tot_line_len,
	    netperf_output_source[i].display_value);
    fprintf(where,
	    "\tline[0]: |%s|\n",
	    (netperf_output_source[i].line[0] == NULL) ? "" : 
	    netperf_output_source[i].line[0]);
    fprintf(where,
	    "\tline[1]: |%s|\n",
	    (netperf_output_source[i].line[1] == NULL) ? "" : 
	    netperf_output_source[i].line[1]);
    fprintf(where,
	    "\tline[2]: |%s|\n",
	    (netperf_output_source[i].line[2] == NULL) ? "" : 
	    netperf_output_source[i].line[2]);
    fprintf(where,
	    "\tline[3]: |%s|\n",
	    (netperf_output_source[i].line[3] == NULL) ? "" : 
	    netperf_output_source[i].line[3]);
    fprintf(where,
	    "\tbrief: |%s|\n",
	    (netperf_output_source[i].brief == NULL) ? "" : 
	    netperf_output_source[i].brief);
    fprintf(where,
	    "\tformat: |%s|\n",
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
parse_output_csv_selection_file(char *selection_file) {
  FILE *selections;
  char name[81]; /* best be more than enough */
  int namepos;
  int c;
  int j;
  enum netperf_output_name match;
  int line,column;

  selections = fopen(selection_file,"r");
  if (!selections) {
    perror("Could Not Open output selection file");
    exit(-1);
  }
  
  /* should this really be necessary? */
  rewind(selections);

  line = 0;
  column = 1;
  namepos = 0;
  name[0] = 0;
  name[80] = 0;
  j = 0;
  /* let's allow the csv to turn the four lines of a human readable
     output file to be used to create a single line csv output file by
     not worrying about the line count. raj 2008--02-04 */
  while ((c = fgetc(selections)) != EOF) {
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
      output_csv_list[j++] = match_string_to_output(name);
      namepos = 0;
    }
    else if (c == '\n') {
      /* move to the next line after checking for a match */
      name[namepos] = 0;
      output_csv_list[j++] = match_string_to_output(name);
      line++;
      namepos = 0;
    }
    else if (isprint(c)) {
      name[namepos++] = c;
    }
    column++;
  }

  /* ok, do we need/want to do anything here? at present we will
     silently ignore the rest of the file if we exit the loop on line
     count */
  if ((c == EOF) && (namepos > 0)) {
    name[namepos] = 0;
    output_csv_list[j] =   match_string_to_output(name);
  }
}
void
parse_output_human_selection_file(char *selection_file) {
  FILE *selections;
  char name[81]; /* best be more than enough */
  int namepos;
  char c;
  int j;
  enum netperf_output_name match;
  int line,column;

  selections = fopen(selection_file,"r");
  if (!selections) {
    perror("Could Not Open output selection file");
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
      output_human_list[line][j++] = match_string_to_output(name);
      namepos = 0;
    }
    else if (c == '\n') {
      /* move to the next line after checking for a match */
      name[namepos] = 0;
      output_human_list[line++][j++] = match_string_to_output(name);
      namepos = 0;
      j = 0;
    }
    else if (isprint(c)) {
      name[namepos++] = c;
    }
    column++;
  }

  /* ok, do we need/want to do anything here? at present we will
     silently ignore the rest of the file if we exit the loop on line
     count */
  if ((c == EOF) && (namepos > 0)) {
    name[namepos] = 0;
    output_human_list[line][j] =   match_string_to_output(name);
  }

}

void
set_output_csv_list_default() {

  int  i = 0;
  enum netperf_output_name j;

  /* this should cause us to catch everything unless someone botches
     adding an output name to the enum.  raj 2008-02-22 */
  for (j = SOCKET_TYPE; j < OUTPUT_END; j++) {
    output_csv_list[i++] = j;
  }

}

void
set_output_human_list_default() {

  int i, j;  /* line, column */
  enum netperf_output_name k;

  /* Line One SOCKET_TYPE to RESPONSE_SIZE */
  i = 0;
  j = 0;
  for (k = SOCKET_TYPE; k <= RESPONSE_SIZE; k++)
    output_human_list[i][j++] = k;

  /* Line Two LOCAL_CPU_UTIL to TRANSPORT_MSS */
  i = 1;
  j = 0;
  for (k = LOCAL_CPU_UTIL; k <= TRANSPORT_MSS; k++)
    output_human_list[i][j++] = k;

  /* Line Three LOCAL_SEND_THROUGHPUT throught REMOTE_CORK */
  i = 2;
  j = 0;
  for (k = LOCAL_SEND_THROUGHPUT; k <= REMOTE_CORK; k++)
    output_human_list[i][j++] = k;

  /* Line Four LOCAL_SYSNAME through COMMAND_LINE */
  i = 3;
  j = 0;
  for (k = LOCAL_SYSNAME; k <= COMMAND_LINE; k++)
    output_human_list[i][j++] = k;

}

void
print_omni_init_list() {

  int i;

  /* belts and suspenders everyone... */
  for (i = OUTPUT_NONE; i < NETPERF_OUTPUT_MAX; i++) {
    netperf_output_source[i].output_name = i;
    netperf_output_source[i].max_line_len = 0;
    netperf_output_source[i].tot_line_len = 0;
    netperf_output_source[i].line[0] = "";
    netperf_output_source[i].line[1] = "";
    netperf_output_source[i].line[2] = "";
    netperf_output_source[i].line[3] = "";
    netperf_output_source[i].brief = "";
    netperf_output_source[i].format = "";
    netperf_output_source[i].display_value = NULL;
  }

  netperf_output_source[OUTPUT_NONE].output_name = OUTPUT_NONE;
  netperf_output_source[OUTPUT_NONE].line[0] = " ";
  netperf_output_source[OUTPUT_NONE].format = "%s";
  netperf_output_source[OUTPUT_NONE].display_value = &" ";
  netperf_output_source[OUTPUT_NONE].max_line_len = 
    NETPERF_LINE_MAX(OUTPUT_NONE);
  netperf_output_source[OUTPUT_NONE].tot_line_len = 
    NETPERF_LINE_TOT(OUTPUT_NONE);

  netperf_output_source[COMMAND_LINE].output_name = COMMAND_LINE;
  netperf_output_source[COMMAND_LINE].line[0] = "Command";
  netperf_output_source[COMMAND_LINE].line[1] = "Line";
  netperf_output_source[COMMAND_LINE].format = "\"%s\"";
  netperf_output_source[COMMAND_LINE].display_value = command_line;
  netperf_output_source[COMMAND_LINE].max_line_len = 
    NETPERF_LINE_MAX(COMMAND_LINE);
  netperf_output_source[COMMAND_LINE].tot_line_len = 
    NETPERF_LINE_TOT(COMMAND_LINE);

  netperf_output_source[UUID].output_name = UUID;
  netperf_output_source[UUID].line[0] = "Test";
  netperf_output_source[UUID].line[1] = "UUID";
  netperf_output_source[UUID].format = "%s";
  netperf_output_source[UUID].display_value = test_uuid;
  netperf_output_source[UUID].max_line_len = 
    NETPERF_LINE_MAX(UUID);
  netperf_output_source[UUID].tot_line_len = 
    NETPERF_LINE_TOT(UUID);

  netperf_output_source[RESULT_BRAND].output_name = RESULT_BRAND;
  netperf_output_source[RESULT_BRAND].line[0] = "Result";
  netperf_output_source[RESULT_BRAND].line[1] = "Tag";
  netperf_output_source[RESULT_BRAND].format = "\"%s\"";
  netperf_output_source[RESULT_BRAND].display_value = result_brand;
  netperf_output_source[RESULT_BRAND].max_line_len = 
    NETPERF_LINE_MAX(RESULT_BRAND);
  netperf_output_source[RESULT_BRAND].tot_line_len = 
    NETPERF_LINE_TOT(RESULT_BRAND);

  netperf_output_source[SOCKET_TYPE].output_name = SOCKET_TYPE;
  netperf_output_source[SOCKET_TYPE].line[0] = "Socket";
  netperf_output_source[SOCKET_TYPE].line[1] = "Type";
  netperf_output_source[SOCKET_TYPE].format = "%s";
  netperf_output_source[SOCKET_TYPE].display_value = socket_type_str;
  netperf_output_source[SOCKET_TYPE].max_line_len = 
    NETPERF_LINE_MAX(SOCKET_TYPE);
  netperf_output_source[SOCKET_TYPE].tot_line_len = 
    NETPERF_LINE_TOT(SOCKET_TYPE);

  netperf_output_source[DIRECTION].output_name = DIRECTION;
  netperf_output_source[DIRECTION].line[0] = "Direction";
  netperf_output_source[DIRECTION].line[1] = "";
  netperf_output_source[DIRECTION].format = "%s";
  netperf_output_source[DIRECTION].display_value = direction_str;
  netperf_output_source[DIRECTION].max_line_len = 
    NETPERF_LINE_MAX(DIRECTION);
  netperf_output_source[DIRECTION].tot_line_len = 
    NETPERF_LINE_TOT(DIRECTION);

  netperf_output_source[PROTOCOL].output_name = PROTOCOL;
  netperf_output_source[PROTOCOL].line[0] = "Protocol";
  netperf_output_source[PROTOCOL].format = "%s";
  netperf_output_source[PROTOCOL].display_value = protocol_str;
  netperf_output_source[PROTOCOL].max_line_len = 
    NETPERF_LINE_MAX(PROTOCOL);
  netperf_output_source[PROTOCOL].tot_line_len = 
    NETPERF_LINE_TOT(PROTOCOL);

  netperf_output_source[ELAPSED_TIME].output_name = ELAPSED_TIME;
  netperf_output_source[ELAPSED_TIME].line[0] = "Elapsed";
  netperf_output_source[ELAPSED_TIME].line[1] = "Time";
  netperf_output_source[ELAPSED_TIME].line[2] = "(sec)";
  netperf_output_source[ELAPSED_TIME].format = "%.2f";
  netperf_output_source[ELAPSED_TIME].display_value = &elapsed_time_double;
  netperf_output_source[ELAPSED_TIME].max_line_len = 
    NETPERF_LINE_MAX(ELAPSED_TIME);
  netperf_output_source[ELAPSED_TIME].tot_line_len = 
    NETPERF_LINE_TOT(ELAPSED_TIME);

  netperf_output_source[SOURCE_PORT].output_name = SOURCE_PORT;
  netperf_output_source[SOURCE_PORT].line[0] = "Source";
  netperf_output_source[SOURCE_PORT].line[1] = "Port";
  netperf_output_source[SOURCE_PORT].format = "%s";
  netperf_output_source[SOURCE_PORT].display_value = local_data_port;
  netperf_output_source[SOURCE_PORT].max_line_len = 
    NETPERF_LINE_MAX(SOURCE_PORT);
  netperf_output_source[SOURCE_PORT].tot_line_len = 
    NETPERF_LINE_TOT(SOURCE_PORT);

  netperf_output_source[SOURCE_ADDR].output_name = SOURCE_ADDR;
  netperf_output_source[SOURCE_ADDR].line[0] = "Source";
  netperf_output_source[SOURCE_ADDR].line[1] = "Address";
  netperf_output_source[SOURCE_ADDR].format = "%s";
  netperf_output_source[SOURCE_ADDR].display_value = local_data_address;
  netperf_output_source[SOURCE_ADDR].max_line_len = 
    NETPERF_LINE_MAX(SOURCE_ADDR);
  netperf_output_source[SOURCE_ADDR].tot_line_len = 
    NETPERF_LINE_TOT(SOURCE_ADDR);

  netperf_output_source[SOURCE_FAMILY].output_name = SOURCE_FAMILY;
  netperf_output_source[SOURCE_FAMILY].line[0] = "Source";
  netperf_output_source[SOURCE_FAMILY].line[1] = "Family";
  netperf_output_source[SOURCE_FAMILY].format = "%d";
  netperf_output_source[SOURCE_FAMILY].display_value = &local_data_family;
  netperf_output_source[SOURCE_FAMILY].max_line_len = 
    NETPERF_LINE_MAX(SOURCE_FAMILY);
  netperf_output_source[SOURCE_FAMILY].tot_line_len = 
    NETPERF_LINE_TOT(SOURCE_FAMILY);

  netperf_output_source[DEST_PORT].output_name = DEST_PORT;
  netperf_output_source[DEST_PORT].line[0] = "Destination";
  netperf_output_source[DEST_PORT].line[1] = "Port";
  netperf_output_source[DEST_PORT].format = "%s";
  netperf_output_source[DEST_PORT].display_value = remote_data_port;
  netperf_output_source[DEST_PORT].max_line_len = 
    NETPERF_LINE_MAX(DEST_PORT);
  netperf_output_source[DEST_PORT].tot_line_len = 
    NETPERF_LINE_TOT(DEST_PORT);

  netperf_output_source[DEST_ADDR].output_name = DEST_ADDR;
  netperf_output_source[DEST_ADDR].line[0] = "Destination";
  netperf_output_source[DEST_ADDR].line[1] = "Address";
  netperf_output_source[DEST_ADDR].format = "%s";
  netperf_output_source[DEST_ADDR].display_value = remote_data_address;
  netperf_output_source[DEST_ADDR].max_line_len = 
    NETPERF_LINE_MAX(DEST_ADDR);
  netperf_output_source[DEST_ADDR].tot_line_len = 
    NETPERF_LINE_TOT(DEST_ADDR);

  netperf_output_source[DEST_FAMILY].output_name = DEST_FAMILY;
  netperf_output_source[DEST_FAMILY].line[0] = "Destination";
  netperf_output_source[DEST_FAMILY].line[1] = "Family";
  netperf_output_source[DEST_FAMILY].format = "%d";
  netperf_output_source[DEST_FAMILY].display_value = &remote_data_family;
  netperf_output_source[DEST_FAMILY].max_line_len = 
    NETPERF_LINE_MAX(DEST_FAMILY);
  netperf_output_source[DEST_FAMILY].tot_line_len = 
    NETPERF_LINE_TOT(DEST_FAMILY);

  netperf_output_source[THROUGHPUT].output_name = THROUGHPUT;
  netperf_output_source[THROUGHPUT].line[0] = "Throughput";
  netperf_output_source[THROUGHPUT].line[1] = "";
  netperf_output_source[THROUGHPUT].format = "%.2f";
  netperf_output_source[THROUGHPUT].display_value = &thruput;
  netperf_output_source[THROUGHPUT].max_line_len = 
    NETPERF_LINE_MAX(THROUGHPUT);
  netperf_output_source[THROUGHPUT].tot_line_len = 
    NETPERF_LINE_TOT(THROUGHPUT);

  netperf_output_source[LOCAL_SEND_THROUGHPUT].output_name = LOCAL_SEND_THROUGHPUT;
  netperf_output_source[LOCAL_SEND_THROUGHPUT].line[0] = "Local";
  netperf_output_source[LOCAL_SEND_THROUGHPUT].line[1] = "Send";
  netperf_output_source[LOCAL_SEND_THROUGHPUT].line[2] = "Throughput";
  netperf_output_source[LOCAL_SEND_THROUGHPUT].format = "%.2f";
  netperf_output_source[LOCAL_SEND_THROUGHPUT].display_value = &local_send_thruput;
  netperf_output_source[LOCAL_SEND_THROUGHPUT].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SEND_THROUGHPUT);
  netperf_output_source[LOCAL_SEND_THROUGHPUT].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SEND_THROUGHPUT);

  netperf_output_source[LOCAL_RECV_THROUGHPUT].output_name = LOCAL_RECV_THROUGHPUT;
  netperf_output_source[LOCAL_RECV_THROUGHPUT].line[0] = "Local";
  netperf_output_source[LOCAL_RECV_THROUGHPUT].line[1] = "Recv";
  netperf_output_source[LOCAL_RECV_THROUGHPUT].line[2] = "Throughput";
  netperf_output_source[LOCAL_RECV_THROUGHPUT].format = "%.2f";
  netperf_output_source[LOCAL_RECV_THROUGHPUT].display_value = &local_recv_thruput;
  netperf_output_source[LOCAL_RECV_THROUGHPUT].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RECV_THROUGHPUT);
  netperf_output_source[LOCAL_RECV_THROUGHPUT].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RECV_THROUGHPUT);

  netperf_output_source[REMOTE_SEND_THROUGHPUT].output_name = REMOTE_SEND_THROUGHPUT;
  netperf_output_source[REMOTE_SEND_THROUGHPUT].line[0] = "Remote";
  netperf_output_source[REMOTE_SEND_THROUGHPUT].line[1] = "Send";
  netperf_output_source[REMOTE_SEND_THROUGHPUT].line[2] = "Throughput";
  netperf_output_source[REMOTE_SEND_THROUGHPUT].format = "%.2f";
  netperf_output_source[REMOTE_SEND_THROUGHPUT].display_value = &remote_send_thruput;
  netperf_output_source[REMOTE_SEND_THROUGHPUT].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SEND_THROUGHPUT);
  netperf_output_source[REMOTE_SEND_THROUGHPUT].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SEND_THROUGHPUT);

  netperf_output_source[REMOTE_RECV_THROUGHPUT].output_name = REMOTE_RECV_THROUGHPUT;
  netperf_output_source[REMOTE_RECV_THROUGHPUT].line[0] = "Remote";
  netperf_output_source[REMOTE_RECV_THROUGHPUT].line[1] = "Recv";
  netperf_output_source[REMOTE_RECV_THROUGHPUT].line[2] = "Throughput";
  netperf_output_source[REMOTE_RECV_THROUGHPUT].format = "%.2f";
  netperf_output_source[REMOTE_RECV_THROUGHPUT].display_value = &remote_recv_thruput;
  netperf_output_source[REMOTE_RECV_THROUGHPUT].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RECV_THROUGHPUT);
  netperf_output_source[REMOTE_RECV_THROUGHPUT].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RECV_THROUGHPUT);

  netperf_output_source[THROUGHPUT_UNITS].output_name = THROUGHPUT_UNITS;
  netperf_output_source[THROUGHPUT_UNITS].line[0] = "Throughput";
  netperf_output_source[THROUGHPUT_UNITS].line[1] = "Units";
  netperf_output_source[THROUGHPUT_UNITS].format = "%s/s";
  netperf_output_source[THROUGHPUT_UNITS].display_value = thruput_format_str;
  netperf_output_source[THROUGHPUT_UNITS].max_line_len = 
    NETPERF_LINE_MAX(THROUGHPUT_UNITS);
  netperf_output_source[THROUGHPUT_UNITS].tot_line_len = 
    NETPERF_LINE_TOT(THROUGHPUT_UNITS);

  netperf_output_source[CONFIDENCE_LEVEL].output_name = CONFIDENCE_LEVEL;
  netperf_output_source[CONFIDENCE_LEVEL].line[0] = "Confidence";
  netperf_output_source[CONFIDENCE_LEVEL].line[1] = "Level";
  netperf_output_source[CONFIDENCE_LEVEL].line[2] = "Percent";
  netperf_output_source[CONFIDENCE_LEVEL].format = "%d";
  netperf_output_source[CONFIDENCE_LEVEL].display_value = &confidence_level;
  netperf_output_source[CONFIDENCE_LEVEL].max_line_len = 
    NETPERF_LINE_MAX(CONFIDENCE_LEVEL);
  netperf_output_source[CONFIDENCE_LEVEL].tot_line_len = 
    NETPERF_LINE_TOT(CONFIDENCE_LEVEL);

  netperf_output_source[CONFIDENCE_INTERVAL].output_name = CONFIDENCE_INTERVAL;
  netperf_output_source[CONFIDENCE_INTERVAL].line[0] = "Confidence";
  netperf_output_source[CONFIDENCE_INTERVAL].line[1] = "Width";
  netperf_output_source[CONFIDENCE_INTERVAL].line[2] = "Target";
  netperf_output_source[CONFIDENCE_INTERVAL].format = "%f";
  netperf_output_source[CONFIDENCE_INTERVAL].display_value = &interval_pct;
  netperf_output_source[CONFIDENCE_INTERVAL].max_line_len = 
    NETPERF_LINE_MAX(CONFIDENCE_INTERVAL);
  netperf_output_source[CONFIDENCE_INTERVAL].tot_line_len = 
    NETPERF_LINE_TOT(CONFIDENCE_INTERVAL);

  netperf_output_source[CONFIDENCE_ITERATION].output_name = CONFIDENCE_ITERATION;
  netperf_output_source[CONFIDENCE_ITERATION].line[0] = "Confidence";
  netperf_output_source[CONFIDENCE_ITERATION].line[1] = "Iterations";
  netperf_output_source[CONFIDENCE_ITERATION].line[2] = "Run";
  netperf_output_source[CONFIDENCE_ITERATION].format = "%d";
  netperf_output_source[CONFIDENCE_ITERATION].display_value = &confidence_iteration;
  netperf_output_source[CONFIDENCE_ITERATION].max_line_len = 
    NETPERF_LINE_MAX(CONFIDENCE_ITERATION);
  netperf_output_source[CONFIDENCE_ITERATION].tot_line_len = 
    NETPERF_LINE_TOT(CONFIDENCE_ITERATION);

  netperf_output_source[THROUGHPUT_CONFID].output_name = THROUGHPUT_CONFID;
  netperf_output_source[THROUGHPUT_CONFID].line[0] = "Throughput";
  netperf_output_source[THROUGHPUT_CONFID].line[1] = "Confidence";
  netperf_output_source[THROUGHPUT_CONFID].line[2] = "Width (%)";
  netperf_output_source[THROUGHPUT_CONFID].format = "%.3f";
  netperf_output_source[THROUGHPUT_CONFID].display_value = &result_confid_pct;
  netperf_output_source[THROUGHPUT_CONFID].max_line_len = 
    NETPERF_LINE_MAX(THROUGHPUT_CONFID);
  netperf_output_source[THROUGHPUT_CONFID].tot_line_len = 
    NETPERF_LINE_TOT(THROUGHPUT_CONFID);

  netperf_output_source[LOCAL_CPU_CONFID].output_name = LOCAL_CPU_CONFID;
  netperf_output_source[LOCAL_CPU_CONFID].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_CONFID].line[1] = "CPU";
  netperf_output_source[LOCAL_CPU_CONFID].line[2] = "Confidence";
  netperf_output_source[LOCAL_CPU_CONFID].line[3] = "Width (%)";
  netperf_output_source[LOCAL_CPU_CONFID].format = "%.3f";
  netperf_output_source[LOCAL_CPU_CONFID].display_value = &loc_cpu_confid_pct;
  netperf_output_source[LOCAL_CPU_CONFID].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_CONFID);
  netperf_output_source[LOCAL_CPU_CONFID].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_CONFID);

  netperf_output_source[REMOTE_CPU_CONFID].output_name = REMOTE_CPU_CONFID;
  netperf_output_source[REMOTE_CPU_CONFID].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_CONFID].line[1] = "CPU";
  netperf_output_source[REMOTE_CPU_CONFID].line[2] = "Confidence";
  netperf_output_source[REMOTE_CPU_CONFID].line[3] = "Width (%)";
  netperf_output_source[REMOTE_CPU_CONFID].format = "%.3f";
  netperf_output_source[REMOTE_CPU_CONFID].display_value = &rem_cpu_confid_pct;
  netperf_output_source[REMOTE_CPU_CONFID].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_CONFID);
  netperf_output_source[REMOTE_CPU_CONFID].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_CONFID);

  netperf_output_source[RT_LATENCY].output_name = RT_LATENCY;
  netperf_output_source[RT_LATENCY].line[0] = "Round";
  netperf_output_source[RT_LATENCY].line[1] = "Trip";
  netperf_output_source[RT_LATENCY].line[2] = "Latency";
  netperf_output_source[RT_LATENCY].line[3] = "usec/tran";
  netperf_output_source[RT_LATENCY].format = "%.3f";
  netperf_output_source[RT_LATENCY].display_value = &rtt_latency;
  netperf_output_source[RT_LATENCY].max_line_len = 
    NETPERF_LINE_MAX(RT_LATENCY);
  netperf_output_source[RT_LATENCY].tot_line_len = 
    NETPERF_LINE_TOT(RT_LATENCY);

  netperf_output_source[TRANSACTION_RATE].output_name = TRANSACTION_RATE;
  netperf_output_source[TRANSACTION_RATE].line[0] = "Transaction";
  netperf_output_source[TRANSACTION_RATE].line[1] = "Rate";
  netperf_output_source[TRANSACTION_RATE].line[2] = "Tran/s";
  netperf_output_source[TRANSACTION_RATE].format = "%.3f";
  netperf_output_source[TRANSACTION_RATE].display_value = &transaction_rate;
  netperf_output_source[TRANSACTION_RATE].max_line_len = 
    NETPERF_LINE_MAX(TRANSACTION_RATE);
  netperf_output_source[TRANSACTION_RATE].tot_line_len = 
    NETPERF_LINE_TOT(TRANSACTION_RATE);

  netperf_output_source[TRANSPORT_MSS].output_name = TRANSPORT_MSS;
  netperf_output_source[TRANSPORT_MSS].line[0] = "Transport";
  netperf_output_source[TRANSPORT_MSS].line[1] = "MSS";
  netperf_output_source[TRANSPORT_MSS].line[2] = "bytes";
  netperf_output_source[TRANSPORT_MSS].format = "%d";
  netperf_output_source[TRANSPORT_MSS].display_value = &transport_mss;
  netperf_output_source[TRANSPORT_MSS].max_line_len = 
    NETPERF_LINE_MAX(TRANSPORT_MSS);
  netperf_output_source[TRANSPORT_MSS].tot_line_len = 
    NETPERF_LINE_TOT(TRANSPORT_MSS);

  netperf_output_source[REQUEST_SIZE].output_name = REQUEST_SIZE;
  netperf_output_source[REQUEST_SIZE].line[0] = "Request";
  netperf_output_source[REQUEST_SIZE].line[1] = "Size";
  netperf_output_source[REQUEST_SIZE].line[2] = "Bytes";
  netperf_output_source[REQUEST_SIZE].format = "%d";
  netperf_output_source[REQUEST_SIZE].display_value = &req_size;
  netperf_output_source[REQUEST_SIZE].max_line_len = 
    NETPERF_LINE_MAX(REQUEST_SIZE);
  netperf_output_source[REQUEST_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(REQUEST_SIZE);

  netperf_output_source[RESPONSE_SIZE].output_name = RESPONSE_SIZE;
  netperf_output_source[RESPONSE_SIZE].line[0] = "Response";
  netperf_output_source[RESPONSE_SIZE].line[1] = "Size";
  netperf_output_source[RESPONSE_SIZE].line[2] = "Bytes";
  netperf_output_source[RESPONSE_SIZE].format = "%d";
  netperf_output_source[RESPONSE_SIZE].display_value = &rsp_size;
  netperf_output_source[RESPONSE_SIZE].max_line_len = 
    NETPERF_LINE_MAX(RESPONSE_SIZE);
  netperf_output_source[RESPONSE_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(RESPONSE_SIZE);

  netperf_output_source[BURST_SIZE].output_name = BURST_SIZE;
  netperf_output_source[BURST_SIZE].line[0] = "Initial";
  netperf_output_source[BURST_SIZE].line[1] = "Burst";
  netperf_output_source[BURST_SIZE].line[2] = "Requests";
  netperf_output_source[BURST_SIZE].format = "%d";
  netperf_output_source[BURST_SIZE].display_value = &first_burst_size;
  netperf_output_source[BURST_SIZE].max_line_len = 
    NETPERF_LINE_MAX(BURST_SIZE);
  netperf_output_source[BURST_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(BURST_SIZE);

  netperf_output_source[LSS_SIZE_REQ].output_name = LSS_SIZE_REQ;
  netperf_output_source[LSS_SIZE_REQ].line[0] = "Local";
  netperf_output_source[LSS_SIZE_REQ].line[1] = "Send Socket";
  netperf_output_source[LSS_SIZE_REQ].line[2] = "Size";
  netperf_output_source[LSS_SIZE_REQ].line[3] = "Requested";
  netperf_output_source[LSS_SIZE_REQ].format = "%d";
  netperf_output_source[LSS_SIZE_REQ].display_value = &lss_size_req;
  netperf_output_source[LSS_SIZE_REQ].max_line_len = 
    NETPERF_LINE_MAX(LSS_SIZE_REQ);
  netperf_output_source[LSS_SIZE_REQ].tot_line_len = 
    NETPERF_LINE_TOT(LSS_SIZE_REQ);

  netperf_output_source[LSS_SIZE].output_name = LSS_SIZE;
  netperf_output_source[LSS_SIZE].line[0] = "Local";
  netperf_output_source[LSS_SIZE].line[1] = "Send Socket";
  netperf_output_source[LSS_SIZE].line[2] = "Size";
  netperf_output_source[LSS_SIZE].line[3] = "Initial";
  netperf_output_source[LSS_SIZE].format = "%d";
  netperf_output_source[LSS_SIZE].display_value = &lss_size;
  netperf_output_source[LSS_SIZE].max_line_len = 
    NETPERF_LINE_MAX(LSS_SIZE);
  netperf_output_source[LSS_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(LSS_SIZE);

  netperf_output_source[LSS_SIZE_END].output_name = LSS_SIZE_END;
  netperf_output_source[LSS_SIZE_END].line[0] = "Local";
  netperf_output_source[LSS_SIZE_END].line[1] = "Send Socket";
  netperf_output_source[LSS_SIZE_END].line[2] = "Size";
  netperf_output_source[LSS_SIZE_END].line[3] = "Final";
  netperf_output_source[LSS_SIZE_END].format = "%d";
  netperf_output_source[LSS_SIZE_END].display_value = &lss_size_end;
  netperf_output_source[LSS_SIZE_END].max_line_len = 
    NETPERF_LINE_MAX(LSS_SIZE_END);
  netperf_output_source[LSS_SIZE_END].tot_line_len = 
    NETPERF_LINE_TOT(LSS_SIZE_END);

  netperf_output_source[LSR_SIZE_REQ].output_name = LSR_SIZE_REQ;
  netperf_output_source[LSR_SIZE_REQ].line[0] = "Local";
  netperf_output_source[LSR_SIZE_REQ].line[1] = "Recv Socket";
  netperf_output_source[LSR_SIZE_REQ].line[2] = "Size";
  netperf_output_source[LSR_SIZE_REQ].line[3] = "Requested";
  netperf_output_source[LSR_SIZE_REQ].format = "%d";
  netperf_output_source[LSR_SIZE_REQ].display_value = &lsr_size_req;
  netperf_output_source[LSR_SIZE_REQ].max_line_len = 
    NETPERF_LINE_MAX(LSR_SIZE_REQ);
  netperf_output_source[LSR_SIZE_REQ].tot_line_len = 
    NETPERF_LINE_TOT(LSR_SIZE_REQ);

  netperf_output_source[LSR_SIZE].output_name = LSR_SIZE;
  netperf_output_source[LSR_SIZE].line[0] = "Local";
  netperf_output_source[LSR_SIZE].line[1] = "Recv Socket";
  netperf_output_source[LSR_SIZE].line[2] = "Size";
  netperf_output_source[LSR_SIZE].line[3] = "Initial";
  netperf_output_source[LSR_SIZE].format = "%d";
  netperf_output_source[LSR_SIZE].display_value = &lsr_size;
  netperf_output_source[LSR_SIZE].max_line_len = 
    NETPERF_LINE_MAX(LSR_SIZE);
  netperf_output_source[LSR_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(LSR_SIZE);

  netperf_output_source[LSR_SIZE_END].output_name = LSR_SIZE_END;
  netperf_output_source[LSR_SIZE_END].line[0] = "Local";
  netperf_output_source[LSR_SIZE_END].line[1] = "Recv Socket";
  netperf_output_source[LSR_SIZE_END].line[2] = "Size";
  netperf_output_source[LSR_SIZE_END].line[3] = "Final";
  netperf_output_source[LSR_SIZE_END].format = "%d";
  netperf_output_source[LSR_SIZE_END].display_value = &lsr_size_end;
  netperf_output_source[LSR_SIZE_END].max_line_len = 
    NETPERF_LINE_MAX(LSR_SIZE_END);
  netperf_output_source[LSR_SIZE_END].tot_line_len = 
    NETPERF_LINE_TOT(LSR_SIZE_END);

  netperf_output_source[LOCAL_SEND_SIZE].output_name = LOCAL_SEND_SIZE;
  netperf_output_source[LOCAL_SEND_SIZE].line[0] = "Local";
  netperf_output_source[LOCAL_SEND_SIZE].line[1] = "Send";
  netperf_output_source[LOCAL_SEND_SIZE].line[2] = "Size";
  netperf_output_source[LOCAL_SEND_SIZE].line[3] = "";
  netperf_output_source[LOCAL_SEND_SIZE].format = "%d";
  netperf_output_source[LOCAL_SEND_SIZE].display_value = &send_size;
  netperf_output_source[LOCAL_SEND_SIZE].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SEND_SIZE);
  netperf_output_source[LOCAL_SEND_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SEND_SIZE);

  netperf_output_source[LOCAL_RECV_SIZE].output_name = LOCAL_RECV_SIZE;
  netperf_output_source[LOCAL_RECV_SIZE].line[0] = "Local";
  netperf_output_source[LOCAL_RECV_SIZE].line[1] = "Recv";
  netperf_output_source[LOCAL_RECV_SIZE].line[2] = "Size";
  netperf_output_source[LOCAL_RECV_SIZE].line[3] = "";
  netperf_output_source[LOCAL_RECV_SIZE].format = "%d";
  netperf_output_source[LOCAL_RECV_SIZE].display_value = &recv_size;
  netperf_output_source[LOCAL_RECV_SIZE].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RECV_SIZE);
  netperf_output_source[LOCAL_RECV_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RECV_SIZE);

  netperf_output_source[LOCAL_SEND_CALLS].output_name = LOCAL_SEND_CALLS;
  netperf_output_source[LOCAL_SEND_CALLS].line[0] = "Local";
  netperf_output_source[LOCAL_SEND_CALLS].line[1] = "Send";
  netperf_output_source[LOCAL_SEND_CALLS].line[2] = "Calls";
  netperf_output_source[LOCAL_SEND_CALLS].line[3] = "";
  netperf_output_source[LOCAL_SEND_CALLS].format = "%d";
  netperf_output_source[LOCAL_SEND_CALLS].display_value = &local_send_calls;
  netperf_output_source[LOCAL_SEND_CALLS].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SEND_CALLS);
  netperf_output_source[LOCAL_SEND_CALLS].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SEND_CALLS);

  netperf_output_source[LOCAL_RECV_CALLS].output_name = LOCAL_RECV_CALLS;
  netperf_output_source[LOCAL_RECV_CALLS].line[0] = "Local";
  netperf_output_source[LOCAL_RECV_CALLS].line[1] = "Recv";
  netperf_output_source[LOCAL_RECV_CALLS].line[2] = "Calls";
  netperf_output_source[LOCAL_RECV_CALLS].line[3] = "";
  netperf_output_source[LOCAL_RECV_CALLS].format = "%d";
  netperf_output_source[LOCAL_RECV_CALLS].display_value = &local_receive_calls;
  netperf_output_source[LOCAL_RECV_CALLS].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RECV_CALLS);
  netperf_output_source[LOCAL_RECV_CALLS].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RECV_CALLS);

  netperf_output_source[LOCAL_BYTES_PER_RECV].output_name = LOCAL_BYTES_PER_RECV;
  netperf_output_source[LOCAL_BYTES_PER_RECV].line[0] = "Local";
  netperf_output_source[LOCAL_BYTES_PER_RECV].line[1] = "Bytes";
  netperf_output_source[LOCAL_BYTES_PER_RECV].line[2] = "Per";
  netperf_output_source[LOCAL_BYTES_PER_RECV].line[3] = "Recv";
  netperf_output_source[LOCAL_BYTES_PER_RECV].format = "%.2f";
  netperf_output_source[LOCAL_BYTES_PER_RECV].display_value = &bytes_per_recv;
  netperf_output_source[LOCAL_BYTES_PER_RECV].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_BYTES_PER_RECV);
  netperf_output_source[LOCAL_BYTES_PER_RECV].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_BYTES_PER_RECV);

  netperf_output_source[LOCAL_BYTES_PER_SEND].output_name = LOCAL_BYTES_PER_SEND;
  netperf_output_source[LOCAL_BYTES_PER_SEND].line[0] = "Local";
  netperf_output_source[LOCAL_BYTES_PER_SEND].line[1] = "Bytes";
  netperf_output_source[LOCAL_BYTES_PER_SEND].line[2] = "Per";
  netperf_output_source[LOCAL_BYTES_PER_SEND].line[3] = "Send";
  netperf_output_source[LOCAL_BYTES_PER_SEND].format = "%.2f";
  netperf_output_source[LOCAL_BYTES_PER_SEND].display_value = &bytes_per_send;
  netperf_output_source[LOCAL_BYTES_PER_SEND].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_BYTES_PER_SEND);
  netperf_output_source[LOCAL_BYTES_PER_SEND].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_BYTES_PER_SEND);

  netperf_output_source[LOCAL_BYTES_RECVD].output_name = LOCAL_BYTES_RECVD;
  netperf_output_source[LOCAL_BYTES_RECVD].line[0] = "Local";
  netperf_output_source[LOCAL_BYTES_RECVD].line[1] = "Bytes";
  netperf_output_source[LOCAL_BYTES_RECVD].line[2] = "Received";
  netperf_output_source[LOCAL_BYTES_RECVD].line[3] = "";
  netperf_output_source[LOCAL_BYTES_RECVD].format = "%lld";
  netperf_output_source[LOCAL_BYTES_RECVD].display_value = &bytes_received;
  netperf_output_source[LOCAL_BYTES_RECVD].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_BYTES_RECVD);
  netperf_output_source[LOCAL_BYTES_RECVD].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_BYTES_RECVD);

  netperf_output_source[LOCAL_BYTES_SENT].output_name = LOCAL_BYTES_SENT;
  netperf_output_source[LOCAL_BYTES_SENT].line[0] = "Local";
  netperf_output_source[LOCAL_BYTES_SENT].line[1] = "Bytes";
  netperf_output_source[LOCAL_BYTES_SENT].line[2] = "Sent";
  netperf_output_source[LOCAL_BYTES_SENT].line[3] = "";
  netperf_output_source[LOCAL_BYTES_SENT].format = "%lld";
  netperf_output_source[LOCAL_BYTES_SENT].display_value = &bytes_sent;
  netperf_output_source[LOCAL_BYTES_SENT].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_BYTES_SENT);
  netperf_output_source[LOCAL_BYTES_SENT].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_BYTES_SENT);

  netperf_output_source[LOCAL_BYTES_XFERD].output_name = LOCAL_BYTES_XFERD;
  netperf_output_source[LOCAL_BYTES_XFERD].line[0] = "Local";
  netperf_output_source[LOCAL_BYTES_XFERD].line[1] = "Bytes";
  netperf_output_source[LOCAL_BYTES_XFERD].line[2] = "Xferred";
  netperf_output_source[LOCAL_BYTES_XFERD].line[3] = "";
  netperf_output_source[LOCAL_BYTES_XFERD].format = "%.0f";
  netperf_output_source[LOCAL_BYTES_XFERD].display_value = &bytes_xferd;
  netperf_output_source[LOCAL_BYTES_XFERD].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_BYTES_XFERD);
  netperf_output_source[LOCAL_BYTES_XFERD].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_BYTES_XFERD);

  netperf_output_source[LOCAL_SEND_WIDTH].output_name = LOCAL_SEND_WIDTH;
  netperf_output_source[LOCAL_SEND_WIDTH].line[0] = "Local";
  netperf_output_source[LOCAL_SEND_WIDTH].line[1] = "Send";
  netperf_output_source[LOCAL_SEND_WIDTH].line[2] = "Width";
  netperf_output_source[LOCAL_SEND_WIDTH].format = "%d";
  netperf_output_source[LOCAL_SEND_WIDTH].display_value = &send_width;
  netperf_output_source[LOCAL_SEND_WIDTH].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SEND_WIDTH);
  netperf_output_source[LOCAL_SEND_WIDTH].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SEND_WIDTH);

  netperf_output_source[LOCAL_RECV_WIDTH].output_name = LOCAL_RECV_WIDTH;
  netperf_output_source[LOCAL_RECV_WIDTH].line[0] = "Local";
  netperf_output_source[LOCAL_RECV_WIDTH].line[1] = "Recv";
  netperf_output_source[LOCAL_RECV_WIDTH].line[2] = "Width";
  netperf_output_source[LOCAL_RECV_WIDTH].format = "%d";
  netperf_output_source[LOCAL_RECV_WIDTH].display_value = &recv_width;
  netperf_output_source[LOCAL_RECV_WIDTH].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RECV_WIDTH);
  netperf_output_source[LOCAL_RECV_WIDTH].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RECV_WIDTH);

  netperf_output_source[LOCAL_SEND_OFFSET].output_name = LOCAL_SEND_OFFSET;
  netperf_output_source[LOCAL_SEND_OFFSET].line[0] = "Local";
  netperf_output_source[LOCAL_SEND_OFFSET].line[1] = "Send";
  netperf_output_source[LOCAL_SEND_OFFSET].line[2] = "Offset";
  netperf_output_source[LOCAL_SEND_OFFSET].format = "%d";
  netperf_output_source[LOCAL_SEND_OFFSET].display_value = &local_send_offset;
  netperf_output_source[LOCAL_SEND_OFFSET].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SEND_OFFSET);
  netperf_output_source[LOCAL_SEND_OFFSET].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SEND_OFFSET);

  netperf_output_source[LOCAL_RECV_OFFSET].output_name = LOCAL_RECV_OFFSET;
  netperf_output_source[LOCAL_RECV_OFFSET].line[0] = "Local";
  netperf_output_source[LOCAL_RECV_OFFSET].line[1] = "Recv";
  netperf_output_source[LOCAL_RECV_OFFSET].line[2] = "Offset";
  netperf_output_source[LOCAL_RECV_OFFSET].format = "%d";
  netperf_output_source[LOCAL_RECV_OFFSET].display_value = &local_recv_offset;
  netperf_output_source[LOCAL_RECV_OFFSET].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RECV_OFFSET);
  netperf_output_source[LOCAL_RECV_OFFSET].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RECV_OFFSET);

  netperf_output_source[LOCAL_RECV_ALIGN].output_name = LOCAL_RECV_ALIGN;
  netperf_output_source[LOCAL_RECV_ALIGN].line[0] = "Local";
  netperf_output_source[LOCAL_RECV_ALIGN].line[1] = "Recv";
  netperf_output_source[LOCAL_RECV_ALIGN].line[2] = "Alignment";
  netperf_output_source[LOCAL_RECV_ALIGN].format = "%d";
  netperf_output_source[LOCAL_RECV_ALIGN].display_value = &local_recv_align;
  netperf_output_source[LOCAL_RECV_ALIGN].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RECV_ALIGN);
  netperf_output_source[LOCAL_RECV_ALIGN].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RECV_ALIGN);

  netperf_output_source[LOCAL_SEND_ALIGN].output_name = LOCAL_SEND_ALIGN;
  netperf_output_source[LOCAL_SEND_ALIGN].line[0] = "Local";
  netperf_output_source[LOCAL_SEND_ALIGN].line[1] = "Send";
  netperf_output_source[LOCAL_SEND_ALIGN].line[2] = "Alignment";
  netperf_output_source[LOCAL_SEND_ALIGN].format = "%d";
  netperf_output_source[LOCAL_SEND_ALIGN].display_value = &local_send_align;
  netperf_output_source[LOCAL_SEND_ALIGN].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SEND_ALIGN);
  netperf_output_source[LOCAL_SEND_ALIGN].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SEND_ALIGN);

  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].output_name = LOCAL_SEND_DIRTY_COUNT;
  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].line[0] = "Local";
  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].line[1] = "Send";
  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].line[2] = "Dirty";
  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].line[3] = "Count";
  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].format = "%d";
  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].display_value = &loc_dirty_count;
  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SEND_DIRTY_COUNT);
  netperf_output_source[LOCAL_SEND_DIRTY_COUNT].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SEND_DIRTY_COUNT);

  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].output_name = LOCAL_RECV_DIRTY_COUNT;
  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].line[0] = "Local";
  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].line[1] = "Recv";
  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].line[2] = "Dirty";
  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].line[3] = "Count";
  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].format = "%d";
  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].display_value = &loc_dirty_count;
  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RECV_DIRTY_COUNT);
  netperf_output_source[LOCAL_RECV_DIRTY_COUNT].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RECV_DIRTY_COUNT);

  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].output_name = LOCAL_RECV_CLEAN_COUNT;
  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].line[0] = "Local";
  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].line[1] = "Recv";
  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].line[2] = "Clean";
  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].line[3] = "Count";
  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].format = "%d";
  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].display_value = &loc_clean_count;
  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RECV_CLEAN_COUNT);
  netperf_output_source[LOCAL_RECV_CLEAN_COUNT].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RECV_CLEAN_COUNT);

  netperf_output_source[LOCAL_CPU_UTIL].output_name = LOCAL_CPU_UTIL;
  netperf_output_source[LOCAL_CPU_UTIL].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_UTIL].line[1] = "CPU";
  netperf_output_source[LOCAL_CPU_UTIL].line[2] = "Util";
  netperf_output_source[LOCAL_CPU_UTIL].line[3] = "%";
  netperf_output_source[LOCAL_CPU_UTIL].format = "%.2f";
  netperf_output_source[LOCAL_CPU_UTIL].display_value = &local_cpu_utilization_double;
  netperf_output_source[LOCAL_CPU_UTIL].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_UTIL);
  netperf_output_source[LOCAL_CPU_UTIL].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_UTIL);

  netperf_output_source[LOCAL_CPU_PEAK_UTIL].output_name = LOCAL_CPU_PEAK_UTIL;
  netperf_output_source[LOCAL_CPU_PEAK_UTIL].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_PEAK_UTIL].line[1] = "Peak";
  netperf_output_source[LOCAL_CPU_PEAK_UTIL].line[2] = "Per CPU";
  netperf_output_source[LOCAL_CPU_PEAK_UTIL].line[3] = "Util %";
  netperf_output_source[LOCAL_CPU_PEAK_UTIL].format = "%.2f";
  netperf_output_source[LOCAL_CPU_PEAK_UTIL].display_value = &lib_local_peak_cpu_util;
  netperf_output_source[LOCAL_CPU_PEAK_UTIL].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_PEAK_UTIL);
  netperf_output_source[LOCAL_CPU_PEAK_UTIL].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_PEAK_UTIL);

  netperf_output_source[LOCAL_CPU_PEAK_ID].output_name = LOCAL_CPU_PEAK_ID;
  netperf_output_source[LOCAL_CPU_PEAK_ID].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_PEAK_ID].line[1] = "Peak";
  netperf_output_source[LOCAL_CPU_PEAK_ID].line[2] = "Per CPU";
  netperf_output_source[LOCAL_CPU_PEAK_ID].line[3] = "ID";
  netperf_output_source[LOCAL_CPU_PEAK_ID].format = "%d";
  netperf_output_source[LOCAL_CPU_PEAK_ID].display_value = &lib_local_peak_cpu_id;
  netperf_output_source[LOCAL_CPU_PEAK_ID].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_PEAK_ID);
  netperf_output_source[LOCAL_CPU_PEAK_ID].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_PEAK_ID);

  netperf_output_source[LOCAL_CPU_BIND].output_name = LOCAL_CPU_BIND;
  netperf_output_source[LOCAL_CPU_BIND].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_BIND].line[1] = "CPU";
  netperf_output_source[LOCAL_CPU_BIND].line[2] = "Bind";
  netperf_output_source[LOCAL_CPU_BIND].line[3] = "";
  netperf_output_source[LOCAL_CPU_BIND].format = "%d";
  netperf_output_source[LOCAL_CPU_BIND].display_value = &local_proc_affinity;
  netperf_output_source[LOCAL_CPU_BIND].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_BIND);
  netperf_output_source[LOCAL_CPU_BIND].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_BIND);

  netperf_output_source[LOCAL_SD].output_name = LOCAL_SD;
  netperf_output_source[LOCAL_SD].line[0] = "Local";
  netperf_output_source[LOCAL_SD].line[1] = "Service";
  netperf_output_source[LOCAL_SD].line[2] = "Demand";
  netperf_output_source[LOCAL_SD].line[3] = "";
  netperf_output_source[LOCAL_SD].format = "%.3f";
  netperf_output_source[LOCAL_SD].display_value = &local_service_demand_double;
  netperf_output_source[LOCAL_SD].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SD);
  netperf_output_source[LOCAL_SD].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SD);

  netperf_output_source[SD_UNITS].output_name = SD_UNITS;
  netperf_output_source[SD_UNITS].line[0] = "Service";
  netperf_output_source[SD_UNITS].line[1] = "Demand";
  netperf_output_source[SD_UNITS].line[2] = "Units";
  netperf_output_source[SD_UNITS].format = "%s";
  netperf_output_source[SD_UNITS].display_value = sd_str;
  netperf_output_source[SD_UNITS].max_line_len = 
    NETPERF_LINE_MAX(SD_UNITS);
  netperf_output_source[SD_UNITS].tot_line_len = 
    NETPERF_LINE_TOT(SD_UNITS);

  netperf_output_source[LOCAL_CPU_METHOD].output_name = LOCAL_CPU_METHOD;
  netperf_output_source[LOCAL_CPU_METHOD].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_METHOD].line[1] = "CPU";
  netperf_output_source[LOCAL_CPU_METHOD].line[2] = "Util";
  netperf_output_source[LOCAL_CPU_METHOD].line[3] = "Method";
  netperf_output_source[LOCAL_CPU_METHOD].format = "%c";
  netperf_output_source[LOCAL_CPU_METHOD].display_value = &local_cpu_method;
  netperf_output_source[LOCAL_CPU_METHOD].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_METHOD);
  netperf_output_source[LOCAL_CPU_METHOD].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_METHOD);

  netperf_output_source[LOCAL_CPU_COUNT].output_name = LOCAL_CPU_COUNT;
  netperf_output_source[LOCAL_CPU_COUNT].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_COUNT].line[1] = "CPU";
  netperf_output_source[LOCAL_CPU_COUNT].line[2] = "Count";
  netperf_output_source[LOCAL_CPU_COUNT].format = "%d";
  netperf_output_source[LOCAL_CPU_COUNT].display_value = &lib_num_loc_cpus;
  netperf_output_source[LOCAL_CPU_COUNT].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_COUNT);
  netperf_output_source[LOCAL_CPU_COUNT].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_COUNT);

  netperf_output_source[LOCAL_NODELAY].output_name = LOCAL_NODELAY;
  netperf_output_source[LOCAL_NODELAY].line[0] = "Local";
  netperf_output_source[LOCAL_NODELAY].line[1] = "NODELAY";
  netperf_output_source[LOCAL_NODELAY].line[2] = "";
  netperf_output_source[LOCAL_NODELAY].line[3] = "";
  netperf_output_source[LOCAL_NODELAY].format = "%d";
  netperf_output_source[LOCAL_NODELAY].display_value = &loc_nodelay;
  netperf_output_source[LOCAL_NODELAY].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_NODELAY);
  netperf_output_source[LOCAL_NODELAY].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_NODELAY);

  netperf_output_source[LOCAL_CORK].output_name = LOCAL_CORK;
  netperf_output_source[LOCAL_CORK].line[0] = "Local";
  netperf_output_source[LOCAL_CORK].line[1] = "Cork";
  netperf_output_source[LOCAL_CORK].line[2] = "";
  netperf_output_source[LOCAL_CORK].line[3] = "";
  netperf_output_source[LOCAL_CORK].format = "%d";
  netperf_output_source[LOCAL_CORK].display_value = &loc_tcpcork;
  netperf_output_source[LOCAL_CORK].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CORK);
  netperf_output_source[LOCAL_CORK].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CORK);

  netperf_output_source[RSS_SIZE_REQ].output_name = RSS_SIZE_REQ;
  netperf_output_source[RSS_SIZE_REQ].line[0] = "Remote";
  netperf_output_source[RSS_SIZE_REQ].line[1] = "Send Socket";
  netperf_output_source[RSS_SIZE_REQ].line[2] = "Size";
  netperf_output_source[RSS_SIZE_REQ].line[3] = "Requested";
  netperf_output_source[RSS_SIZE_REQ].format = "%d";
  netperf_output_source[RSS_SIZE_REQ].display_value = &rss_size_req;
  netperf_output_source[RSS_SIZE_REQ].max_line_len = 
    NETPERF_LINE_MAX(RSS_SIZE_REQ);
  netperf_output_source[RSS_SIZE_REQ].tot_line_len = 
    NETPERF_LINE_TOT(RSS_SIZE_REQ);

  netperf_output_source[RSS_SIZE].output_name = RSS_SIZE;
  netperf_output_source[RSS_SIZE].line[0] = "Remote";
  netperf_output_source[RSS_SIZE].line[1] = "Send Socket";
  netperf_output_source[RSS_SIZE].line[2] = "Size";
  netperf_output_source[RSS_SIZE].line[3] = "Initial";
  netperf_output_source[RSS_SIZE].format = "%d";
  netperf_output_source[RSS_SIZE].display_value = &rss_size;
  netperf_output_source[RSS_SIZE].max_line_len = 
    NETPERF_LINE_MAX(RSS_SIZE);
  netperf_output_source[RSS_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(RSS_SIZE);

  netperf_output_source[RSS_SIZE_END].output_name = RSS_SIZE_END;
  netperf_output_source[RSS_SIZE_END].line[0] = "Remote";
  netperf_output_source[RSS_SIZE_END].line[1] = "Send Socket";
  netperf_output_source[RSS_SIZE_END].line[2] = "Size";
  netperf_output_source[RSS_SIZE_END].line[3] = "Final";
  netperf_output_source[RSS_SIZE_END].format = "%d";
  netperf_output_source[RSS_SIZE_END].display_value = &rss_size_end;
  netperf_output_source[RSS_SIZE_END].max_line_len = 
    NETPERF_LINE_MAX(RSS_SIZE_END);
  netperf_output_source[RSS_SIZE_END].tot_line_len = 
    NETPERF_LINE_TOT(RSS_SIZE_END);

  netperf_output_source[RSR_SIZE_REQ].output_name = RSR_SIZE_REQ;
  netperf_output_source[RSR_SIZE_REQ].line[0] = "Remote";
  netperf_output_source[RSR_SIZE_REQ].line[1] = "Recv Socket";
  netperf_output_source[RSR_SIZE_REQ].line[2] = "Size";
  netperf_output_source[RSR_SIZE_REQ].line[3] = "Requested";
  netperf_output_source[RSR_SIZE_REQ].format = "%d";
  netperf_output_source[RSR_SIZE_REQ].display_value = &rsr_size_req;
  netperf_output_source[RSR_SIZE_REQ].max_line_len = 
    NETPERF_LINE_MAX(RSR_SIZE_REQ);
  netperf_output_source[RSR_SIZE_REQ].tot_line_len = 
    NETPERF_LINE_TOT(RSR_SIZE_REQ);

  netperf_output_source[RSR_SIZE].output_name = RSR_SIZE;
  netperf_output_source[RSR_SIZE].line[0] = "Remote";
  netperf_output_source[RSR_SIZE].line[1] = "Recv Socket";
  netperf_output_source[RSR_SIZE].line[2] = "Size";
  netperf_output_source[RSR_SIZE].line[3] = "Initial";
  netperf_output_source[RSR_SIZE].format = "%d";
  netperf_output_source[RSR_SIZE].display_value = &rsr_size;
  netperf_output_source[RSR_SIZE].max_line_len = 
    NETPERF_LINE_MAX(RSR_SIZE);
  netperf_output_source[RSR_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(RSR_SIZE);

  netperf_output_source[RSR_SIZE_END].output_name = RSR_SIZE_END;
  netperf_output_source[RSR_SIZE_END].line[0] = "Remote";
  netperf_output_source[RSR_SIZE_END].line[1] = "Recv Socket";
  netperf_output_source[RSR_SIZE_END].line[2] = "Size";
  netperf_output_source[RSR_SIZE_END].line[3] = "Final";
  netperf_output_source[RSR_SIZE_END].format = "%d";
  netperf_output_source[RSR_SIZE_END].display_value = &rsr_size_end;
  netperf_output_source[RSR_SIZE_END].max_line_len = 
    NETPERF_LINE_MAX(RSR_SIZE_END);
  netperf_output_source[RSR_SIZE_END].tot_line_len = 
    NETPERF_LINE_TOT(RSR_SIZE_END);

  netperf_output_source[REMOTE_SEND_SIZE].output_name = REMOTE_SEND_SIZE;
  netperf_output_source[REMOTE_SEND_SIZE].line[0] = "Remote";
  netperf_output_source[REMOTE_SEND_SIZE].line[1] = "Send";
  netperf_output_source[REMOTE_SEND_SIZE].line[2] = "Size";
  netperf_output_source[REMOTE_SEND_SIZE].line[3] = "";
  netperf_output_source[REMOTE_SEND_SIZE].format = "%d";
  netperf_output_source[REMOTE_SEND_SIZE].display_value = &remote_send_size;
  netperf_output_source[REMOTE_SEND_SIZE].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SEND_SIZE);
  netperf_output_source[REMOTE_SEND_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SEND_SIZE);

  netperf_output_source[REMOTE_RECV_SIZE].output_name = REMOTE_RECV_SIZE;
  netperf_output_source[REMOTE_RECV_SIZE].line[0] = "Remote";
  netperf_output_source[REMOTE_RECV_SIZE].line[1] = "Recv";
  netperf_output_source[REMOTE_RECV_SIZE].line[2] = "Size";
  netperf_output_source[REMOTE_RECV_SIZE].line[3] = "";
  netperf_output_source[REMOTE_RECV_SIZE].format = "%d";
  netperf_output_source[REMOTE_RECV_SIZE].display_value = &remote_recv_size;
  netperf_output_source[REMOTE_RECV_SIZE].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RECV_SIZE);
  netperf_output_source[REMOTE_RECV_SIZE].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RECV_SIZE);

  netperf_output_source[REMOTE_SEND_CALLS].output_name = REMOTE_SEND_CALLS;
  netperf_output_source[REMOTE_SEND_CALLS].line[0] = "Remote";
  netperf_output_source[REMOTE_SEND_CALLS].line[1] = "Send";
  netperf_output_source[REMOTE_SEND_CALLS].line[2] = "Calls";
  netperf_output_source[REMOTE_SEND_CALLS].line[3] = "";
  netperf_output_source[REMOTE_SEND_CALLS].format = "%lld";
  netperf_output_source[REMOTE_SEND_CALLS].display_value = &remote_send_calls;
  netperf_output_source[REMOTE_SEND_CALLS].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SEND_CALLS);
  netperf_output_source[REMOTE_SEND_CALLS].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SEND_CALLS);

  netperf_output_source[REMOTE_RECV_CALLS].output_name = REMOTE_RECV_CALLS;
  netperf_output_source[REMOTE_RECV_CALLS].line[0] = "Remote";
  netperf_output_source[REMOTE_RECV_CALLS].line[1] = "Recv";
  netperf_output_source[REMOTE_RECV_CALLS].line[2] = "Calls";
  netperf_output_source[REMOTE_RECV_CALLS].line[3] = "";
  netperf_output_source[REMOTE_RECV_CALLS].format = "%lld";
  netperf_output_source[REMOTE_RECV_CALLS].display_value = &remote_receive_calls;
  netperf_output_source[REMOTE_RECV_CALLS].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RECV_CALLS);
  netperf_output_source[REMOTE_RECV_CALLS].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RECV_CALLS);

  netperf_output_source[REMOTE_BYTES_PER_RECV].output_name = REMOTE_BYTES_PER_RECV;
  netperf_output_source[REMOTE_BYTES_PER_RECV].line[0] = "Remote";
  netperf_output_source[REMOTE_BYTES_PER_RECV].line[1] = "Bytes";
  netperf_output_source[REMOTE_BYTES_PER_RECV].line[2] = "Per";
  netperf_output_source[REMOTE_BYTES_PER_RECV].line[3] = "Recv";
  netperf_output_source[REMOTE_BYTES_PER_RECV].format = "%.2f";
  netperf_output_source[REMOTE_BYTES_PER_RECV].display_value = &remote_bytes_per_recv;
  netperf_output_source[REMOTE_BYTES_PER_RECV].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_BYTES_PER_RECV);
  netperf_output_source[REMOTE_BYTES_PER_RECV].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_BYTES_PER_RECV);

  netperf_output_source[REMOTE_BYTES_PER_SEND].output_name = REMOTE_BYTES_PER_SEND;
  netperf_output_source[REMOTE_BYTES_PER_SEND].line[0] = "Remote";
  netperf_output_source[REMOTE_BYTES_PER_SEND].line[1] = "Bytes";
  netperf_output_source[REMOTE_BYTES_PER_SEND].line[2] = "Per";
  netperf_output_source[REMOTE_BYTES_PER_SEND].line[3] = "Send";
  netperf_output_source[REMOTE_BYTES_PER_SEND].format = "%.2f";
  netperf_output_source[REMOTE_BYTES_PER_SEND].display_value = &remote_bytes_per_send;
  netperf_output_source[REMOTE_BYTES_PER_SEND].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_BYTES_PER_SEND);
  netperf_output_source[REMOTE_BYTES_PER_SEND].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_BYTES_PER_SEND);

  netperf_output_source[REMOTE_BYTES_RECVD].output_name = REMOTE_BYTES_RECVD;
  netperf_output_source[REMOTE_BYTES_RECVD].line[0] = "Remote";
  netperf_output_source[REMOTE_BYTES_RECVD].line[1] = "Bytes";
  netperf_output_source[REMOTE_BYTES_RECVD].line[2] = "Received";
  netperf_output_source[REMOTE_BYTES_RECVD].line[3] = "";
  netperf_output_source[REMOTE_BYTES_RECVD].format = "%lld";
  netperf_output_source[REMOTE_BYTES_RECVD].display_value = &remote_bytes_received;
  netperf_output_source[REMOTE_BYTES_RECVD].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_BYTES_RECVD);
  netperf_output_source[REMOTE_BYTES_RECVD].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_BYTES_RECVD);

  netperf_output_source[REMOTE_BYTES_SENT].output_name = REMOTE_BYTES_SENT;
  netperf_output_source[REMOTE_BYTES_SENT].line[0] = "Remote";
  netperf_output_source[REMOTE_BYTES_SENT].line[1] = "Bytes";
  netperf_output_source[REMOTE_BYTES_SENT].line[2] = "Sent";
  netperf_output_source[REMOTE_BYTES_SENT].line[3] = "";
  netperf_output_source[REMOTE_BYTES_SENT].format = "%lld";
  netperf_output_source[REMOTE_BYTES_SENT].display_value = &remote_bytes_sent;
  netperf_output_source[REMOTE_BYTES_SENT].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_BYTES_SENT);
  netperf_output_source[REMOTE_BYTES_SENT].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_BYTES_SENT);

  netperf_output_source[REMOTE_BYTES_XFERD].output_name = REMOTE_BYTES_XFERD;
  netperf_output_source[REMOTE_BYTES_XFERD].line[0] = "Remote";
  netperf_output_source[REMOTE_BYTES_XFERD].line[1] = "Bytes";
  netperf_output_source[REMOTE_BYTES_XFERD].line[2] = "Xferred";
  netperf_output_source[REMOTE_BYTES_XFERD].line[3] = "";
  netperf_output_source[REMOTE_BYTES_XFERD].format = "%.0f";
  netperf_output_source[REMOTE_BYTES_XFERD].display_value = &remote_bytes_xferd;
  netperf_output_source[REMOTE_BYTES_XFERD].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_BYTES_XFERD);
  netperf_output_source[REMOTE_BYTES_XFERD].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_BYTES_XFERD);

  netperf_output_source[REMOTE_SEND_WIDTH].output_name = REMOTE_SEND_WIDTH;
  netperf_output_source[REMOTE_SEND_WIDTH].line[0] = "Remote";
  netperf_output_source[REMOTE_SEND_WIDTH].line[1] = "Send";
  netperf_output_source[REMOTE_SEND_WIDTH].line[2] = "Width";
  netperf_output_source[REMOTE_SEND_WIDTH].format = "%d";
  netperf_output_source[REMOTE_SEND_WIDTH].display_value = &remote_send_width;
  netperf_output_source[REMOTE_SEND_WIDTH].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SEND_WIDTH);
  netperf_output_source[REMOTE_SEND_WIDTH].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SEND_WIDTH);

  netperf_output_source[REMOTE_RECV_WIDTH].output_name = REMOTE_RECV_WIDTH;
  netperf_output_source[REMOTE_RECV_WIDTH].line[0] = "Remote";
  netperf_output_source[REMOTE_RECV_WIDTH].line[1] = "Recv";
  netperf_output_source[REMOTE_RECV_WIDTH].line[2] = "Width";
  netperf_output_source[REMOTE_RECV_WIDTH].format = "%d";
  netperf_output_source[REMOTE_RECV_WIDTH].display_value = &remote_recv_width;
  netperf_output_source[REMOTE_RECV_WIDTH].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RECV_WIDTH);
  netperf_output_source[REMOTE_RECV_WIDTH].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RECV_WIDTH);

  netperf_output_source[REMOTE_SEND_OFFSET].output_name = REMOTE_SEND_OFFSET;
  netperf_output_source[REMOTE_SEND_OFFSET].line[0] = "Remote";
  netperf_output_source[REMOTE_SEND_OFFSET].line[1] = "Send";
  netperf_output_source[REMOTE_SEND_OFFSET].line[2] = "Offset";
  netperf_output_source[REMOTE_SEND_OFFSET].format = "%d";
  netperf_output_source[REMOTE_SEND_OFFSET].display_value = &remote_send_offset;
  netperf_output_source[REMOTE_SEND_OFFSET].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SEND_OFFSET);
  netperf_output_source[REMOTE_SEND_OFFSET].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SEND_OFFSET);

  netperf_output_source[REMOTE_RECV_OFFSET].output_name = REMOTE_RECV_OFFSET;
  netperf_output_source[REMOTE_RECV_OFFSET].line[0] = "Remote";
  netperf_output_source[REMOTE_RECV_OFFSET].line[1] = "Recv";
  netperf_output_source[REMOTE_RECV_OFFSET].line[2] = "Offset";
  netperf_output_source[REMOTE_RECV_OFFSET].format = "%d";
  netperf_output_source[REMOTE_RECV_OFFSET].display_value = &remote_recv_offset;
  netperf_output_source[REMOTE_RECV_OFFSET].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RECV_OFFSET);
  netperf_output_source[REMOTE_RECV_OFFSET].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RECV_OFFSET);

  netperf_output_source[REMOTE_RECV_ALIGN].output_name = REMOTE_RECV_ALIGN;
  netperf_output_source[REMOTE_RECV_ALIGN].line[0] = "Remote";
  netperf_output_source[REMOTE_RECV_ALIGN].line[1] = "Recv";
  netperf_output_source[REMOTE_RECV_ALIGN].line[2] = "Alignment";
  netperf_output_source[REMOTE_RECV_ALIGN].format = "%d";
  netperf_output_source[REMOTE_RECV_ALIGN].display_value = &remote_recv_align;
  netperf_output_source[REMOTE_RECV_ALIGN].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RECV_ALIGN);
  netperf_output_source[REMOTE_RECV_ALIGN].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RECV_ALIGN);

  netperf_output_source[REMOTE_SEND_ALIGN].output_name = REMOTE_SEND_ALIGN;
  netperf_output_source[REMOTE_SEND_ALIGN].line[0] = "Remote";
  netperf_output_source[REMOTE_SEND_ALIGN].line[1] = "Send";
  netperf_output_source[REMOTE_SEND_ALIGN].line[2] = "Alignment";
  netperf_output_source[REMOTE_SEND_ALIGN].format = "%d";
  netperf_output_source[REMOTE_SEND_ALIGN].display_value = &remote_send_align;
  netperf_output_source[REMOTE_SEND_ALIGN].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SEND_ALIGN);
  netperf_output_source[REMOTE_SEND_ALIGN].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SEND_ALIGN);

  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].output_name = REMOTE_SEND_DIRTY_COUNT;
  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].line[0] = "Remote";
  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].line[1] = "Send";
  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].line[2] = "Dirty";
  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].line[3] = "Count";
  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].format = "%d";
  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].display_value = &rem_dirty_count;
  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SEND_DIRTY_COUNT);
  netperf_output_source[REMOTE_SEND_DIRTY_COUNT].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SEND_DIRTY_COUNT);

  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].output_name = REMOTE_RECV_DIRTY_COUNT;
  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].line[0] = "Remote";
  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].line[1] = "Recv";
  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].line[2] = "Dirty";
  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].line[3] = "Count";
  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].format = "%d";
  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].display_value = &rem_dirty_count;
  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RECV_DIRTY_COUNT);
  netperf_output_source[REMOTE_RECV_DIRTY_COUNT].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RECV_DIRTY_COUNT);

  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].output_name = REMOTE_RECV_CLEAN_COUNT;
  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].line[0] = "Remote";
  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].line[1] = "Recv";
  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].line[2] = "Clean";
  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].line[3] = "Count";
  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].format = "%d";
  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].display_value = &rem_clean_count;
  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RECV_CLEAN_COUNT);
  netperf_output_source[REMOTE_RECV_CLEAN_COUNT].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RECV_CLEAN_COUNT);

  netperf_output_source[REMOTE_CPU_UTIL].output_name = REMOTE_CPU_UTIL;
  netperf_output_source[REMOTE_CPU_UTIL].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_UTIL].line[1] = "CPU";
  netperf_output_source[REMOTE_CPU_UTIL].line[2] = "Util";
  netperf_output_source[REMOTE_CPU_UTIL].line[3] = "%";
  netperf_output_source[REMOTE_CPU_UTIL].format = "%.2f";
  netperf_output_source[REMOTE_CPU_UTIL].display_value = &remote_cpu_utilization_double;
  netperf_output_source[REMOTE_CPU_UTIL].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_UTIL);
  netperf_output_source[REMOTE_CPU_UTIL].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_UTIL);

  netperf_output_source[REMOTE_CPU_PEAK_UTIL].output_name = REMOTE_CPU_PEAK_UTIL;
  netperf_output_source[REMOTE_CPU_PEAK_UTIL].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_PEAK_UTIL].line[1] = "Peak";
  netperf_output_source[REMOTE_CPU_PEAK_UTIL].line[2] = "Per CPU";
  netperf_output_source[REMOTE_CPU_PEAK_UTIL].line[3] = "Util %";
  netperf_output_source[REMOTE_CPU_PEAK_UTIL].format = "%.2f";
  netperf_output_source[REMOTE_CPU_PEAK_UTIL].display_value = &lib_remote_peak_cpu_util;
  netperf_output_source[REMOTE_CPU_PEAK_UTIL].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_PEAK_UTIL);
  netperf_output_source[REMOTE_CPU_PEAK_UTIL].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_PEAK_UTIL);

  netperf_output_source[REMOTE_CPU_PEAK_ID].output_name = REMOTE_CPU_PEAK_ID;
  netperf_output_source[REMOTE_CPU_PEAK_ID].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_PEAK_ID].line[1] = "Peak";
  netperf_output_source[REMOTE_CPU_PEAK_ID].line[2] = "Per CPU";
  netperf_output_source[REMOTE_CPU_PEAK_ID].line[3] = "ID";
  netperf_output_source[REMOTE_CPU_PEAK_ID].format = "%d";
  netperf_output_source[REMOTE_CPU_PEAK_ID].display_value = &lib_remote_peak_cpu_id;
  netperf_output_source[REMOTE_CPU_PEAK_ID].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_PEAK_ID);
  netperf_output_source[REMOTE_CPU_PEAK_ID].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_PEAK_ID);

  netperf_output_source[REMOTE_CPU_BIND].output_name = REMOTE_CPU_BIND;
  netperf_output_source[REMOTE_CPU_BIND].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_BIND].line[1] = "CPU";
  netperf_output_source[REMOTE_CPU_BIND].line[2] = "Bind";
  netperf_output_source[REMOTE_CPU_BIND].line[3] = "";
  netperf_output_source[REMOTE_CPU_BIND].format = "%d";
  netperf_output_source[REMOTE_CPU_BIND].display_value = &remote_proc_affinity;
  netperf_output_source[REMOTE_CPU_BIND].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_BIND);
  netperf_output_source[REMOTE_CPU_BIND].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_BIND);

  netperf_output_source[REMOTE_SD].output_name = REMOTE_SD;
  netperf_output_source[REMOTE_SD].line[0] = "Remote";
  netperf_output_source[REMOTE_SD].line[1] = "Service";
  netperf_output_source[REMOTE_SD].line[2] = "Demand";
  netperf_output_source[REMOTE_SD].line[3] = "";
  netperf_output_source[REMOTE_SD].format = "%.3f";
  netperf_output_source[REMOTE_SD].display_value = &remote_service_demand_double;
  netperf_output_source[REMOTE_SD].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SD);
  netperf_output_source[REMOTE_SD].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SD);

  netperf_output_source[REMOTE_CPU_METHOD].output_name = REMOTE_CPU_METHOD;
  netperf_output_source[REMOTE_CPU_METHOD].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_METHOD].line[1] = "CPU";
  netperf_output_source[REMOTE_CPU_METHOD].line[2] = "Util";
  netperf_output_source[REMOTE_CPU_METHOD].line[3] = "Method";
  netperf_output_source[REMOTE_CPU_METHOD].format = "%c";
  netperf_output_source[REMOTE_CPU_METHOD].display_value = &remote_cpu_method;
  netperf_output_source[REMOTE_CPU_METHOD].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_METHOD);
  netperf_output_source[REMOTE_CPU_METHOD].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_METHOD);

  netperf_output_source[REMOTE_CPU_COUNT].output_name = REMOTE_CPU_COUNT;
  netperf_output_source[REMOTE_CPU_COUNT].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_COUNT].line[1] = "CPU";
  netperf_output_source[REMOTE_CPU_COUNT].line[2] = "Count";
  netperf_output_source[REMOTE_CPU_COUNT].format = "%d";
  netperf_output_source[REMOTE_CPU_COUNT].display_value = &lib_num_rem_cpus;
  netperf_output_source[REMOTE_CPU_COUNT].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_COUNT);
  netperf_output_source[REMOTE_CPU_COUNT].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_COUNT);

  netperf_output_source[REMOTE_NODELAY].output_name = REMOTE_NODELAY;
  netperf_output_source[REMOTE_NODELAY].line[0] = "Remote";
  netperf_output_source[REMOTE_NODELAY].line[1] = "NODELAY";
  netperf_output_source[REMOTE_NODELAY].line[2] = "";
  netperf_output_source[REMOTE_NODELAY].line[3] = "";
  netperf_output_source[REMOTE_NODELAY].format = "%d";
  netperf_output_source[REMOTE_NODELAY].display_value = &rem_nodelay;
  netperf_output_source[REMOTE_NODELAY].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_NODELAY);
  netperf_output_source[REMOTE_NODELAY].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_NODELAY);

  netperf_output_source[REMOTE_CORK].output_name = REMOTE_CORK;
  netperf_output_source[REMOTE_CORK].line[0] = "Remote";
  netperf_output_source[REMOTE_CORK].line[1] = "Cork";
  netperf_output_source[REMOTE_CORK].line[2] = "";
  netperf_output_source[REMOTE_CORK].line[3] = "";
  netperf_output_source[REMOTE_CORK].format = "%d";
  netperf_output_source[REMOTE_CORK].display_value = &rem_tcpcork;
  netperf_output_source[REMOTE_CORK].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CORK);
  netperf_output_source[REMOTE_CORK].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CORK);

  netperf_output_source[LOCAL_DRIVER_NAME].output_name = LOCAL_DRIVER_NAME;
  netperf_output_source[LOCAL_DRIVER_NAME].line[0] = "Local";
  netperf_output_source[LOCAL_DRIVER_NAME].line[1] = "Driver";
  netperf_output_source[LOCAL_DRIVER_NAME].line[2] = "Name";
  netperf_output_source[LOCAL_DRIVER_NAME].line[3] = "";
  netperf_output_source[LOCAL_DRIVER_NAME].format = "%s";
  netperf_output_source[LOCAL_DRIVER_NAME].display_value = local_driver_name;
  netperf_output_source[LOCAL_DRIVER_NAME].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_DRIVER_NAME);
  netperf_output_source[LOCAL_DRIVER_NAME].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_DRIVER_NAME);

  netperf_output_source[LOCAL_DRIVER_VERSION].output_name = LOCAL_DRIVER_VERSION;
  netperf_output_source[LOCAL_DRIVER_VERSION].line[0] = "Local";
  netperf_output_source[LOCAL_DRIVER_VERSION].line[1] = "Driver";
  netperf_output_source[LOCAL_DRIVER_VERSION].line[2] = "Version";
  netperf_output_source[LOCAL_DRIVER_VERSION].line[3] = "";
  netperf_output_source[LOCAL_DRIVER_VERSION].format = "%s";
  netperf_output_source[LOCAL_DRIVER_VERSION].display_value = local_driver_version;
  netperf_output_source[LOCAL_DRIVER_VERSION].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_DRIVER_VERSION);
  netperf_output_source[LOCAL_DRIVER_VERSION].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_DRIVER_VERSION);

  netperf_output_source[LOCAL_DRIVER_FIRMWARE].output_name = LOCAL_DRIVER_FIRMWARE;
  netperf_output_source[LOCAL_DRIVER_FIRMWARE].line[0] = "Local";
  netperf_output_source[LOCAL_DRIVER_FIRMWARE].line[1] = "Driver";
  netperf_output_source[LOCAL_DRIVER_FIRMWARE].line[2] = "Firmware";
  netperf_output_source[LOCAL_DRIVER_FIRMWARE].line[3] = "";
  netperf_output_source[LOCAL_DRIVER_FIRMWARE].format = "%s";
  netperf_output_source[LOCAL_DRIVER_FIRMWARE].display_value = local_driver_firmware;
  netperf_output_source[LOCAL_DRIVER_FIRMWARE].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_DRIVER_FIRMWARE);
  netperf_output_source[LOCAL_DRIVER_FIRMWARE].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_DRIVER_FIRMWARE);

  netperf_output_source[LOCAL_DRIVER_BUS].output_name = LOCAL_DRIVER_BUS;
  netperf_output_source[LOCAL_DRIVER_BUS].line[0] = "Local";
  netperf_output_source[LOCAL_DRIVER_BUS].line[1] = "Driver";
  netperf_output_source[LOCAL_DRIVER_BUS].line[2] = "Bus";
  netperf_output_source[LOCAL_DRIVER_BUS].line[3] = "";
  netperf_output_source[LOCAL_DRIVER_BUS].format = "%s";
  netperf_output_source[LOCAL_DRIVER_BUS].display_value = local_driver_bus;
  netperf_output_source[LOCAL_DRIVER_BUS].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_DRIVER_BUS);
  netperf_output_source[LOCAL_DRIVER_BUS].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_DRIVER_BUS);

  netperf_output_source[REMOTE_DRIVER_NAME].output_name = REMOTE_DRIVER_NAME;
  netperf_output_source[REMOTE_DRIVER_NAME].line[0] = "Remote";
  netperf_output_source[REMOTE_DRIVER_NAME].line[1] = "Driver";
  netperf_output_source[REMOTE_DRIVER_NAME].line[2] = "Name";
  netperf_output_source[REMOTE_DRIVER_NAME].line[3] = "";
  netperf_output_source[REMOTE_DRIVER_NAME].format = "%s";
  netperf_output_source[REMOTE_DRIVER_NAME].display_value = remote_driver_name;
  netperf_output_source[REMOTE_DRIVER_NAME].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_DRIVER_NAME);
  netperf_output_source[REMOTE_DRIVER_NAME].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_DRIVER_NAME);

  netperf_output_source[REMOTE_DRIVER_VERSION].output_name = REMOTE_DRIVER_VERSION;
  netperf_output_source[REMOTE_DRIVER_VERSION].line[0] = "Remote";
  netperf_output_source[REMOTE_DRIVER_VERSION].line[1] = "Driver";
  netperf_output_source[REMOTE_DRIVER_VERSION].line[2] = "Version";
  netperf_output_source[REMOTE_DRIVER_VERSION].line[3] = "";
  netperf_output_source[REMOTE_DRIVER_VERSION].format = "%s";
  netperf_output_source[REMOTE_DRIVER_VERSION].display_value = remote_driver_version;
  netperf_output_source[REMOTE_DRIVER_VERSION].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_DRIVER_VERSION);
  netperf_output_source[REMOTE_DRIVER_VERSION].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_DRIVER_VERSION);

  netperf_output_source[REMOTE_DRIVER_FIRMWARE].output_name = REMOTE_DRIVER_FIRMWARE;
  netperf_output_source[REMOTE_DRIVER_FIRMWARE].line[0] = "Remote";
  netperf_output_source[REMOTE_DRIVER_FIRMWARE].line[1] = "Driver";
  netperf_output_source[REMOTE_DRIVER_FIRMWARE].line[2] = "Firmware";
  netperf_output_source[REMOTE_DRIVER_FIRMWARE].line[3] = "";
  netperf_output_source[REMOTE_DRIVER_FIRMWARE].format = "%s";
  netperf_output_source[REMOTE_DRIVER_FIRMWARE].display_value = remote_driver_firmware;
  netperf_output_source[REMOTE_DRIVER_FIRMWARE].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_DRIVER_FIRMWARE);
  netperf_output_source[REMOTE_DRIVER_FIRMWARE].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_DRIVER_FIRMWARE);

  netperf_output_source[REMOTE_DRIVER_BUS].output_name = REMOTE_DRIVER_BUS;
  netperf_output_source[REMOTE_DRIVER_BUS].line[0] = "Remote";
  netperf_output_source[REMOTE_DRIVER_BUS].line[1] = "Driver";
  netperf_output_source[REMOTE_DRIVER_BUS].line[2] = "Bus";
  netperf_output_source[REMOTE_DRIVER_BUS].line[3] = "";
  netperf_output_source[REMOTE_DRIVER_BUS].format = "%s";
  netperf_output_source[REMOTE_DRIVER_BUS].display_value = remote_driver_bus;
  netperf_output_source[REMOTE_DRIVER_BUS].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_DRIVER_BUS);
  netperf_output_source[REMOTE_DRIVER_BUS].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_DRIVER_BUS);

  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].output_name = LOCAL_INTERFACE_SUBDEVICE;
  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].line[0] = "Local";
  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].line[1] = "Interface";
  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].line[2] = "Subdevice";
  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].line[3] = "";
  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].format = "0x%.4x";
  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].display_value = &local_interface_subdevice;
  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_INTERFACE_SUBDEVICE);
  netperf_output_source[LOCAL_INTERFACE_SUBDEVICE].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_INTERFACE_SUBDEVICE);

  netperf_output_source[LOCAL_INTERFACE_DEVICE].output_name = LOCAL_INTERFACE_DEVICE;
  netperf_output_source[LOCAL_INTERFACE_DEVICE].line[0] = "Local";
  netperf_output_source[LOCAL_INTERFACE_DEVICE].line[1] = "Interface";
  netperf_output_source[LOCAL_INTERFACE_DEVICE].line[2] = "Device";
  netperf_output_source[LOCAL_INTERFACE_DEVICE].line[3] = "";
  netperf_output_source[LOCAL_INTERFACE_DEVICE].format = "0x%.4x";
  netperf_output_source[LOCAL_INTERFACE_DEVICE].display_value = &local_interface_device;
  netperf_output_source[LOCAL_INTERFACE_DEVICE].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_INTERFACE_DEVICE);
  netperf_output_source[LOCAL_INTERFACE_DEVICE].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_INTERFACE_DEVICE);

  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].output_name = LOCAL_INTERFACE_SUBVENDOR;
  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].line[0] = "Local";
  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].line[1] = "Interface";
  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].line[2] = "Subvendor";
  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].line[3] = "";
  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].format = "0x%.4x";
  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].display_value = &local_interface_subvendor;
  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_INTERFACE_SUBVENDOR);
  netperf_output_source[LOCAL_INTERFACE_SUBVENDOR].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_INTERFACE_SUBVENDOR);

  netperf_output_source[LOCAL_INTERFACE_VENDOR].output_name = LOCAL_INTERFACE_VENDOR;
  netperf_output_source[LOCAL_INTERFACE_VENDOR].line[0] = "Local";
  netperf_output_source[LOCAL_INTERFACE_VENDOR].line[1] = "Interface";
  netperf_output_source[LOCAL_INTERFACE_VENDOR].line[2] = "Vendor";
  netperf_output_source[LOCAL_INTERFACE_VENDOR].line[3] = "";
  netperf_output_source[LOCAL_INTERFACE_VENDOR].format = "0x%.4x";
  netperf_output_source[LOCAL_INTERFACE_VENDOR].display_value = &local_interface_vendor;
  netperf_output_source[LOCAL_INTERFACE_VENDOR].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_INTERFACE_VENDOR);
  netperf_output_source[LOCAL_INTERFACE_VENDOR].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_INTERFACE_VENDOR);

  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].output_name = REMOTE_INTERFACE_SUBDEVICE;
  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].line[0] = "Remote";
  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].line[1] = "Interface";
  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].line[2] = "Subdevice";
  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].line[3] = "";
  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].format = "0x%.4x";
  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].display_value = &remote_interface_subdevice;
  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_INTERFACE_SUBDEVICE);
  netperf_output_source[REMOTE_INTERFACE_SUBDEVICE].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_INTERFACE_SUBDEVICE);

  netperf_output_source[REMOTE_INTERFACE_DEVICE].output_name = REMOTE_INTERFACE_DEVICE;
  netperf_output_source[REMOTE_INTERFACE_DEVICE].line[0] = "Remote";
  netperf_output_source[REMOTE_INTERFACE_DEVICE].line[1] = "Interface";
  netperf_output_source[REMOTE_INTERFACE_DEVICE].line[2] = "Device";
  netperf_output_source[REMOTE_INTERFACE_DEVICE].line[3] = "";
  netperf_output_source[REMOTE_INTERFACE_DEVICE].format = "0x%.4x";
  netperf_output_source[REMOTE_INTERFACE_DEVICE].display_value = &remote_interface_device;
  netperf_output_source[REMOTE_INTERFACE_DEVICE].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_INTERFACE_DEVICE);
  netperf_output_source[REMOTE_INTERFACE_DEVICE].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_INTERFACE_DEVICE);

  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].output_name = REMOTE_INTERFACE_SUBVENDOR;
  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].line[0] = "Remote";
  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].line[1] = "Interface";
  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].line[2] = "Subvendor";
  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].line[3] = "";
  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].format = "0x%.4x";
  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].display_value = &remote_interface_subvendor;
  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_INTERFACE_SUBVENDOR);
  netperf_output_source[REMOTE_INTERFACE_SUBVENDOR].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_INTERFACE_SUBVENDOR);

  netperf_output_source[REMOTE_INTERFACE_VENDOR].output_name = REMOTE_INTERFACE_VENDOR;
  netperf_output_source[REMOTE_INTERFACE_VENDOR].line[0] = "Remote";
  netperf_output_source[REMOTE_INTERFACE_VENDOR].line[1] = "Interface";
  netperf_output_source[REMOTE_INTERFACE_VENDOR].line[2] = "Vendor";
  netperf_output_source[REMOTE_INTERFACE_VENDOR].line[3] = "";
  netperf_output_source[REMOTE_INTERFACE_VENDOR].format = "0x%.4x";
  netperf_output_source[REMOTE_INTERFACE_VENDOR].display_value = &remote_interface_vendor;
  netperf_output_source[REMOTE_INTERFACE_VENDOR].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_INTERFACE_VENDOR);
  netperf_output_source[REMOTE_INTERFACE_VENDOR].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_INTERFACE_VENDOR);

  netperf_output_source[LOCAL_INTERFACE_NAME].output_name = LOCAL_INTERFACE_NAME;
  netperf_output_source[LOCAL_INTERFACE_NAME].line[0] = "Local";
  netperf_output_source[LOCAL_INTERFACE_NAME].line[1] = "Interface";
  netperf_output_source[LOCAL_INTERFACE_NAME].line[2] = "Name";
  netperf_output_source[LOCAL_INTERFACE_NAME].line[3] = "";
  netperf_output_source[LOCAL_INTERFACE_NAME].format = "%s";
  netperf_output_source[LOCAL_INTERFACE_NAME].display_value = local_interface_name;
  netperf_output_source[LOCAL_INTERFACE_NAME].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_INTERFACE_NAME);
  netperf_output_source[LOCAL_INTERFACE_NAME].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_INTERFACE_NAME);

  netperf_output_source[REMOTE_INTERFACE_NAME].output_name = REMOTE_INTERFACE_NAME;
  netperf_output_source[REMOTE_INTERFACE_NAME].line[0] = "Remote";
  netperf_output_source[REMOTE_INTERFACE_NAME].line[1] = "Interface";
  netperf_output_source[REMOTE_INTERFACE_NAME].line[2] = "Name";
  netperf_output_source[REMOTE_INTERFACE_NAME].line[3] = "";
  netperf_output_source[REMOTE_INTERFACE_NAME].format = "%s";
  netperf_output_source[REMOTE_INTERFACE_NAME].display_value = remote_interface_name;
  netperf_output_source[REMOTE_INTERFACE_NAME].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_INTERFACE_NAME);
  netperf_output_source[REMOTE_INTERFACE_NAME].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_INTERFACE_NAME);

  netperf_output_source[LOCAL_INTERFACE_SLOT].output_name = LOCAL_INTERFACE_SLOT;
  netperf_output_source[LOCAL_INTERFACE_SLOT].line[0] = "Local";
  netperf_output_source[LOCAL_INTERFACE_SLOT].line[1] = "Interface";
  netperf_output_source[LOCAL_INTERFACE_SLOT].line[2] = "Slot";
  netperf_output_source[LOCAL_INTERFACE_SLOT].line[3] = "";
  netperf_output_source[LOCAL_INTERFACE_SLOT].format = "%s";
  netperf_output_source[LOCAL_INTERFACE_SLOT].display_value = local_interface_slot;
  netperf_output_source[LOCAL_INTERFACE_SLOT].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_INTERFACE_SLOT);
  netperf_output_source[LOCAL_INTERFACE_SLOT].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_INTERFACE_SLOT);

  netperf_output_source[REMOTE_INTERFACE_SLOT].output_name = REMOTE_INTERFACE_SLOT;
  netperf_output_source[REMOTE_INTERFACE_SLOT].line[0] = "Remote";
  netperf_output_source[REMOTE_INTERFACE_SLOT].line[1] = "Interface";
  netperf_output_source[REMOTE_INTERFACE_SLOT].line[2] = "Slot";
  netperf_output_source[REMOTE_INTERFACE_SLOT].line[3] = "";
  netperf_output_source[REMOTE_INTERFACE_SLOT].format = "%s";
  netperf_output_source[REMOTE_INTERFACE_SLOT].display_value = remote_interface_slot;
  netperf_output_source[REMOTE_INTERFACE_SLOT].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_INTERFACE_SLOT);
  netperf_output_source[REMOTE_INTERFACE_SLOT].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_INTERFACE_SLOT);

  netperf_output_source[REMOTE_MACHINE].output_name = REMOTE_MACHINE;
  netperf_output_source[REMOTE_MACHINE].line[0] = "Remote";
  netperf_output_source[REMOTE_MACHINE].line[1] = "Machine";
  netperf_output_source[REMOTE_MACHINE].line[2] = "";
  netperf_output_source[REMOTE_MACHINE].line[3] = "";
  netperf_output_source[REMOTE_MACHINE].format = "%s";
  netperf_output_source[REMOTE_MACHINE].display_value = remote_machine;
  netperf_output_source[REMOTE_MACHINE].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_MACHINE);
  netperf_output_source[REMOTE_MACHINE].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_MACHINE);

  netperf_output_source[REMOTE_VERSION].output_name = REMOTE_VERSION;
  netperf_output_source[REMOTE_VERSION].line[0] = "Remote";
  netperf_output_source[REMOTE_VERSION].line[1] = "Version";
  netperf_output_source[REMOTE_VERSION].line[2] = "";
  netperf_output_source[REMOTE_VERSION].line[3] = "";
  netperf_output_source[REMOTE_VERSION].format = "%s";
  netperf_output_source[REMOTE_VERSION].display_value = remote_version;
  netperf_output_source[REMOTE_VERSION].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_VERSION);
  netperf_output_source[REMOTE_VERSION].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_VERSION);

  netperf_output_source[REMOTE_RELEASE].output_name = REMOTE_RELEASE;
  netperf_output_source[REMOTE_RELEASE].line[0] = "Remote";
  netperf_output_source[REMOTE_RELEASE].line[1] = "Release";
  netperf_output_source[REMOTE_RELEASE].line[2] = "";
  netperf_output_source[REMOTE_RELEASE].line[3] = "";
  netperf_output_source[REMOTE_RELEASE].format = "%s";
  netperf_output_source[REMOTE_RELEASE].display_value = remote_release;
  netperf_output_source[REMOTE_RELEASE].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_RELEASE);
  netperf_output_source[REMOTE_RELEASE].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_RELEASE);

  netperf_output_source[REMOTE_SYSNAME].output_name = REMOTE_SYSNAME;
  netperf_output_source[REMOTE_SYSNAME].line[0] = "Remote";
  netperf_output_source[REMOTE_SYSNAME].line[1] = "Sysname";
  netperf_output_source[REMOTE_SYSNAME].line[2] = "";
  netperf_output_source[REMOTE_SYSNAME].line[3] = "";
  netperf_output_source[REMOTE_SYSNAME].format = "%s";
  netperf_output_source[REMOTE_SYSNAME].display_value = remote_sysname;
  netperf_output_source[REMOTE_SYSNAME].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SYSNAME);
  netperf_output_source[REMOTE_SYSNAME].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SYSNAME);

  netperf_output_source[LOCAL_MACHINE].output_name = LOCAL_MACHINE;
  netperf_output_source[LOCAL_MACHINE].line[0] = "Local";
  netperf_output_source[LOCAL_MACHINE].line[1] = "Machine";
  netperf_output_source[LOCAL_MACHINE].line[2] = "";
  netperf_output_source[LOCAL_MACHINE].line[3] = "";
  netperf_output_source[LOCAL_MACHINE].format = "%s";
  netperf_output_source[LOCAL_MACHINE].display_value = local_machine;
  netperf_output_source[LOCAL_MACHINE].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_MACHINE);
  netperf_output_source[LOCAL_MACHINE].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_MACHINE);

  netperf_output_source[LOCAL_VERSION].output_name = LOCAL_VERSION;
  netperf_output_source[LOCAL_VERSION].line[0] = "Local";
  netperf_output_source[LOCAL_VERSION].line[1] = "Version";
  netperf_output_source[LOCAL_VERSION].line[2] = "";
  netperf_output_source[LOCAL_VERSION].line[3] = "";
  netperf_output_source[LOCAL_VERSION].format = "%s";
  netperf_output_source[LOCAL_VERSION].display_value = local_version;
  netperf_output_source[LOCAL_VERSION].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_VERSION);
  netperf_output_source[LOCAL_VERSION].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_VERSION);

  netperf_output_source[LOCAL_RELEASE].output_name = LOCAL_RELEASE;
  netperf_output_source[LOCAL_RELEASE].line[0] = "Local";
  netperf_output_source[LOCAL_RELEASE].line[1] = "Release";
  netperf_output_source[LOCAL_RELEASE].line[2] = "";
  netperf_output_source[LOCAL_RELEASE].line[3] = "";
  netperf_output_source[LOCAL_RELEASE].format = "%s";
  netperf_output_source[LOCAL_RELEASE].display_value = local_release;
  netperf_output_source[LOCAL_RELEASE].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_RELEASE);
  netperf_output_source[LOCAL_RELEASE].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_RELEASE);

  netperf_output_source[LOCAL_SYSNAME].output_name = LOCAL_SYSNAME;
  netperf_output_source[LOCAL_SYSNAME].line[0] = "Local";
  netperf_output_source[LOCAL_SYSNAME].line[1] = "Sysname";
  netperf_output_source[LOCAL_SYSNAME].line[2] = "";
  netperf_output_source[LOCAL_SYSNAME].line[3] = "";
  netperf_output_source[LOCAL_SYSNAME].format = "%s";
  netperf_output_source[LOCAL_SYSNAME].display_value = local_sysname;
  netperf_output_source[LOCAL_SYSNAME].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SYSNAME);
  netperf_output_source[LOCAL_SYSNAME].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SYSNAME);

  netperf_output_source[REMOTE_INTERVAL_USECS].output_name = REMOTE_INTERVAL_USECS;
  netperf_output_source[REMOTE_INTERVAL_USECS].line[0] = "Remote";
  netperf_output_source[REMOTE_INTERVAL_USECS].line[1] = "Interval";
  netperf_output_source[REMOTE_INTERVAL_USECS].line[2] = "Usecs";
  netperf_output_source[REMOTE_INTERVAL_USECS].line[3] = "";
  netperf_output_source[REMOTE_INTERVAL_USECS].format = "%d";
  netperf_output_source[REMOTE_INTERVAL_USECS].display_value = &remote_interval_usecs;
  netperf_output_source[REMOTE_INTERVAL_USECS].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_INTERVAL_USECS);
  netperf_output_source[REMOTE_INTERVAL_USECS].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_INTERVAL_USECS);

  netperf_output_source[REMOTE_INTERVAL_BURST].output_name = REMOTE_INTERVAL_BURST;
  netperf_output_source[REMOTE_INTERVAL_BURST].line[0] = "Remote";
  netperf_output_source[REMOTE_INTERVAL_BURST].line[1] = "Interval";
  netperf_output_source[REMOTE_INTERVAL_BURST].line[2] = "Burst";
  netperf_output_source[REMOTE_INTERVAL_BURST].line[3] = "";
  netperf_output_source[REMOTE_INTERVAL_BURST].format = "%d";
  netperf_output_source[REMOTE_INTERVAL_BURST].display_value = &remote_interval_burst;
  netperf_output_source[REMOTE_INTERVAL_BURST].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_INTERVAL_BURST);
  netperf_output_source[REMOTE_INTERVAL_BURST].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_INTERVAL_BURST);

  netperf_output_source[LOCAL_SECURITY_ENABLED].output_name = LOCAL_SECURITY_ENABLED;
  netperf_output_source[LOCAL_SECURITY_ENABLED].line[0] = "Local";
  netperf_output_source[LOCAL_SECURITY_ENABLED].line[1] = "OS";
  netperf_output_source[LOCAL_SECURITY_ENABLED].line[2] = "Security";
  netperf_output_source[LOCAL_SECURITY_ENABLED].line[3] = "Enabled";
  netperf_output_source[LOCAL_SECURITY_ENABLED].format = "%s";
  netperf_output_source[LOCAL_SECURITY_ENABLED].display_value = local_security_enabled;
  netperf_output_source[LOCAL_SECURITY_ENABLED].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SECURITY_ENABLED);
  netperf_output_source[LOCAL_SECURITY_ENABLED].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SECURITY_ENABLED);

  netperf_output_source[LOCAL_SECURITY_TYPE].output_name = LOCAL_SECURITY_TYPE;
  netperf_output_source[LOCAL_SECURITY_TYPE].line[0] = "Local";
  netperf_output_source[LOCAL_SECURITY_TYPE].line[1] = "OS";
  netperf_output_source[LOCAL_SECURITY_TYPE].line[2] = "Security";
  netperf_output_source[LOCAL_SECURITY_TYPE].line[3] = "Type";
  netperf_output_source[LOCAL_SECURITY_TYPE].format = "%s";
  netperf_output_source[LOCAL_SECURITY_TYPE].display_value = local_security_type;
  netperf_output_source[LOCAL_SECURITY_TYPE].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SECURITY_TYPE);
  netperf_output_source[LOCAL_SECURITY_TYPE].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SECURITY_TYPE);

  netperf_output_source[LOCAL_SECURITY_SPECIFIC].output_name = LOCAL_SECURITY_SPECIFIC;
  netperf_output_source[LOCAL_SECURITY_SPECIFIC].line[0] = "Local";
  netperf_output_source[LOCAL_SECURITY_SPECIFIC].line[1] = "OS";
  netperf_output_source[LOCAL_SECURITY_SPECIFIC].line[2] = "Security";
  netperf_output_source[LOCAL_SECURITY_SPECIFIC].line[3] = "Specific";
  netperf_output_source[LOCAL_SECURITY_SPECIFIC].format = "%s";
  netperf_output_source[LOCAL_SECURITY_SPECIFIC].display_value = local_security_specific;
  netperf_output_source[LOCAL_SECURITY_SPECIFIC].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SECURITY_SPECIFIC);
  netperf_output_source[LOCAL_SECURITY_SPECIFIC].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SECURITY_SPECIFIC);

  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].output_name = LOCAL_SECURITY_ENABLED_NUM;
  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].line[0] = "Local";
  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].line[1] = "OS";
  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].line[2] = "Security";
  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].line[3] = "Enabled Num";
  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].format = "%d";
  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].display_value = &local_security_enabled_num;
  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SECURITY_ENABLED_NUM);
  netperf_output_source[LOCAL_SECURITY_ENABLED_NUM].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SECURITY_ENABLED_NUM);

  netperf_output_source[LOCAL_SECURITY_TYPE_ID].output_name = LOCAL_SECURITY_TYPE_ID;
  netperf_output_source[LOCAL_SECURITY_TYPE_ID].line[0] = "Local";
  netperf_output_source[LOCAL_SECURITY_TYPE_ID].line[1] = "OS";
  netperf_output_source[LOCAL_SECURITY_TYPE_ID].line[2] = "Security";
  netperf_output_source[LOCAL_SECURITY_TYPE_ID].line[3] = "Type ID";
  netperf_output_source[LOCAL_SECURITY_TYPE_ID].format = "%d";
  netperf_output_source[LOCAL_SECURITY_TYPE_ID].display_value = &local_security_type_id;
  netperf_output_source[LOCAL_SECURITY_TYPE_ID].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SECURITY_TYPE_ID);
  netperf_output_source[LOCAL_SECURITY_TYPE_ID].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SECURITY_TYPE_ID);

  netperf_output_source[REMOTE_SECURITY_ENABLED].output_name = REMOTE_SECURITY_ENABLED;
  netperf_output_source[REMOTE_SECURITY_ENABLED].line[0] = "Remote";
  netperf_output_source[REMOTE_SECURITY_ENABLED].line[1] = "OS";
  netperf_output_source[REMOTE_SECURITY_ENABLED].line[2] = "Security";
  netperf_output_source[REMOTE_SECURITY_ENABLED].line[3] = "Enabled";
  netperf_output_source[REMOTE_SECURITY_ENABLED].format = "%s";
  netperf_output_source[REMOTE_SECURITY_ENABLED].display_value = remote_security_enabled;
  netperf_output_source[REMOTE_SECURITY_ENABLED].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SECURITY_ENABLED);
  netperf_output_source[REMOTE_SECURITY_ENABLED].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SECURITY_ENABLED);

  netperf_output_source[REMOTE_SECURITY_TYPE].output_name = REMOTE_SECURITY_TYPE;
  netperf_output_source[REMOTE_SECURITY_TYPE].line[0] = "Remote";
  netperf_output_source[REMOTE_SECURITY_TYPE].line[1] = "OS";
  netperf_output_source[REMOTE_SECURITY_TYPE].line[2] = "Security";
  netperf_output_source[REMOTE_SECURITY_TYPE].line[3] = "Type";
  netperf_output_source[REMOTE_SECURITY_TYPE].format = "%s";
  netperf_output_source[REMOTE_SECURITY_TYPE].display_value = remote_security_type;
  netperf_output_source[REMOTE_SECURITY_TYPE].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SECURITY_TYPE);
  netperf_output_source[REMOTE_SECURITY_TYPE].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SECURITY_TYPE);

  netperf_output_source[REMOTE_SECURITY_SPECIFIC].output_name = REMOTE_SECURITY_SPECIFIC;
  netperf_output_source[REMOTE_SECURITY_SPECIFIC].line[0] = "Remote";
  netperf_output_source[REMOTE_SECURITY_SPECIFIC].line[1] = "OS";
  netperf_output_source[REMOTE_SECURITY_SPECIFIC].line[2] = "Security";
  netperf_output_source[REMOTE_SECURITY_SPECIFIC].line[3] = "Specific";
  netperf_output_source[REMOTE_SECURITY_SPECIFIC].format = "%s";
  netperf_output_source[REMOTE_SECURITY_SPECIFIC].display_value = remote_security_specific;
  netperf_output_source[REMOTE_SECURITY_SPECIFIC].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SECURITY_SPECIFIC);
  netperf_output_source[REMOTE_SECURITY_SPECIFIC].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SECURITY_SPECIFIC);

  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].output_name = REMOTE_SECURITY_ENABLED_NUM;
  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].line[0] = "Remote";
  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].line[1] = "OS";
  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].line[2] = "Security";
  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].line[3] = "Enabled";
  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].format = "%d";
  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].display_value = &remote_security_enabled_num;
  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SECURITY_ENABLED_NUM);
  netperf_output_source[REMOTE_SECURITY_ENABLED_NUM].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SECURITY_ENABLED_NUM);

  netperf_output_source[REMOTE_SECURITY_TYPE_ID].output_name = REMOTE_SECURITY_TYPE_ID;
  netperf_output_source[REMOTE_SECURITY_TYPE_ID].line[0] = "Remote";
  netperf_output_source[REMOTE_SECURITY_TYPE_ID].line[1] = "OS";
  netperf_output_source[REMOTE_SECURITY_TYPE_ID].line[2] = "Security";
  netperf_output_source[REMOTE_SECURITY_TYPE_ID].line[3] = "Type";
  netperf_output_source[REMOTE_SECURITY_TYPE_ID].format = "%d";
  netperf_output_source[REMOTE_SECURITY_TYPE_ID].display_value = &remote_security_type_id;
  netperf_output_source[REMOTE_SECURITY_TYPE_ID].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SECURITY_TYPE_ID);
  netperf_output_source[REMOTE_SECURITY_TYPE_ID].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SECURITY_TYPE_ID);

  netperf_output_source[LOCAL_INTERVAL_USECS].output_name = LOCAL_INTERVAL_USECS;
  netperf_output_source[LOCAL_INTERVAL_USECS].line[0] = "Local";
  netperf_output_source[LOCAL_INTERVAL_USECS].line[1] = "Interval";
  netperf_output_source[LOCAL_INTERVAL_USECS].line[2] = "Usecs";
  netperf_output_source[LOCAL_INTERVAL_USECS].line[3] = "";
  netperf_output_source[LOCAL_INTERVAL_USECS].format = "%d";
  netperf_output_source[LOCAL_INTERVAL_USECS].display_value = &interval_usecs;
  netperf_output_source[LOCAL_INTERVAL_USECS].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_INTERVAL_USECS);
  netperf_output_source[LOCAL_INTERVAL_USECS].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_INTERVAL_USECS);

  netperf_output_source[LOCAL_INTERVAL_BURST].output_name = LOCAL_INTERVAL_BURST;
  netperf_output_source[LOCAL_INTERVAL_BURST].line[0] = "Local";
  netperf_output_source[LOCAL_INTERVAL_BURST].line[1] = "Interval";
  netperf_output_source[LOCAL_INTERVAL_BURST].line[2] = "Burst";
  netperf_output_source[LOCAL_INTERVAL_BURST].line[3] = "";
  netperf_output_source[LOCAL_INTERVAL_BURST].format = "%d";
  netperf_output_source[LOCAL_INTERVAL_BURST].display_value = &interval_burst;
  netperf_output_source[LOCAL_INTERVAL_BURST].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_INTERVAL_BURST);
  netperf_output_source[LOCAL_INTERVAL_BURST].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_INTERVAL_BURST);

  netperf_output_source[REMOTE_SYSTEM_MODEL].output_name = REMOTE_SYSTEM_MODEL;
  netperf_output_source[REMOTE_SYSTEM_MODEL].line[0] = "Remote";
  netperf_output_source[REMOTE_SYSTEM_MODEL].line[1] = "System";
  netperf_output_source[REMOTE_SYSTEM_MODEL].line[2] = "Model";
  netperf_output_source[REMOTE_SYSTEM_MODEL].line[3] = "";
  netperf_output_source[REMOTE_SYSTEM_MODEL].format = "%s";
  netperf_output_source[REMOTE_SYSTEM_MODEL].display_value = remote_system_model;
  netperf_output_source[REMOTE_SYSTEM_MODEL].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_SYSTEM_MODEL);
  netperf_output_source[REMOTE_SYSTEM_MODEL].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_SYSTEM_MODEL);

  netperf_output_source[REMOTE_CPU_MODEL].output_name = REMOTE_CPU_MODEL;
  netperf_output_source[REMOTE_CPU_MODEL].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_MODEL].line[1] = "CPU";
  netperf_output_source[REMOTE_CPU_MODEL].line[2] = "Model";
  netperf_output_source[REMOTE_CPU_MODEL].line[3] = "";
  netperf_output_source[REMOTE_CPU_MODEL].format = "%s";
  netperf_output_source[REMOTE_CPU_MODEL].display_value = remote_cpu_model;
  netperf_output_source[REMOTE_CPU_MODEL].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_MODEL);
  netperf_output_source[REMOTE_CPU_MODEL].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_MODEL);

  netperf_output_source[REMOTE_CPU_FREQUENCY].output_name = REMOTE_CPU_FREQUENCY;
  netperf_output_source[REMOTE_CPU_FREQUENCY].line[0] = "Remote";
  netperf_output_source[REMOTE_CPU_FREQUENCY].line[1] = "CPU";
  netperf_output_source[REMOTE_CPU_FREQUENCY].line[2] = "Frequency";
  netperf_output_source[REMOTE_CPU_FREQUENCY].line[3] = "MHz";
  netperf_output_source[REMOTE_CPU_FREQUENCY].format = "%d";
  netperf_output_source[REMOTE_CPU_FREQUENCY].display_value = &remote_cpu_frequency;
  netperf_output_source[REMOTE_CPU_FREQUENCY].max_line_len = 
    NETPERF_LINE_MAX(REMOTE_CPU_FREQUENCY);
  netperf_output_source[REMOTE_CPU_FREQUENCY].tot_line_len = 
    NETPERF_LINE_TOT(REMOTE_CPU_FREQUENCY);

  netperf_output_source[LOCAL_SYSTEM_MODEL].output_name = LOCAL_SYSTEM_MODEL;
  netperf_output_source[LOCAL_SYSTEM_MODEL].line[0] = "Local";
  netperf_output_source[LOCAL_SYSTEM_MODEL].line[1] = "System";
  netperf_output_source[LOCAL_SYSTEM_MODEL].line[2] = "Model";
  netperf_output_source[LOCAL_SYSTEM_MODEL].line[3] = "";
  netperf_output_source[LOCAL_SYSTEM_MODEL].format = "%s";
  netperf_output_source[LOCAL_SYSTEM_MODEL].display_value = local_system_model;
  netperf_output_source[LOCAL_SYSTEM_MODEL].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_SYSTEM_MODEL);
  netperf_output_source[LOCAL_SYSTEM_MODEL].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_SYSTEM_MODEL);

  netperf_output_source[LOCAL_CPU_MODEL].output_name = LOCAL_CPU_MODEL;
  netperf_output_source[LOCAL_CPU_MODEL].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_MODEL].line[1] = "CPU";
  netperf_output_source[LOCAL_CPU_MODEL].line[2] = "Model";
  netperf_output_source[LOCAL_CPU_MODEL].line[3] = "";
  netperf_output_source[LOCAL_CPU_MODEL].format = "%s";
  netperf_output_source[LOCAL_CPU_MODEL].display_value = local_cpu_model;
  netperf_output_source[LOCAL_CPU_MODEL].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_MODEL);
  netperf_output_source[LOCAL_CPU_MODEL].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_MODEL);

  netperf_output_source[LOCAL_CPU_FREQUENCY].output_name = LOCAL_CPU_FREQUENCY;
  netperf_output_source[LOCAL_CPU_FREQUENCY].line[0] = "Local";
  netperf_output_source[LOCAL_CPU_FREQUENCY].line[1] = "CPU";
  netperf_output_source[LOCAL_CPU_FREQUENCY].line[2] = "Frequency";
  netperf_output_source[LOCAL_CPU_FREQUENCY].line[3] = "MHz";
  netperf_output_source[LOCAL_CPU_FREQUENCY].format = "%d";
  netperf_output_source[LOCAL_CPU_FREQUENCY].display_value = &local_cpu_frequency;
  netperf_output_source[LOCAL_CPU_FREQUENCY].max_line_len = 
    NETPERF_LINE_MAX(LOCAL_CPU_FREQUENCY);
  netperf_output_source[LOCAL_CPU_FREQUENCY].tot_line_len = 
    NETPERF_LINE_TOT(LOCAL_CPU_FREQUENCY);

  netperf_output_source[OUTPUT_END].output_name = OUTPUT_END;
  netperf_output_source[OUTPUT_END].line[0] = "This";
  netperf_output_source[OUTPUT_END].line[1] = "Is";
  netperf_output_source[OUTPUT_END].line[2] = "The";
  netperf_output_source[OUTPUT_END].line[3] = "End";
  netperf_output_source[OUTPUT_END].format = "%s";
  netperf_output_source[OUTPUT_END].display_value = NULL;
  netperf_output_source[OUTPUT_END].max_line_len = 
    NETPERF_LINE_MAX(OUTPUT_END);
  netperf_output_source[OUTPUT_END].tot_line_len = 
    NETPERF_LINE_TOT(OUTPUT_END);

}
/* lots of boring, repetitive code */
void 
print_omni_init() {

  int i,j;

  if (printing_initialized) return;

  printing_initialized = 1;

  print_omni_init_list();

  /* belts and suspenders */
  for (i = OUTPUT_NONE; i < NETPERF_OUTPUT_MAX; i++)
    output_csv_list[i] = OUTPUT_END;

  for (j = 0; j < NETPERF_MAX_BLOCKS; j++)
    for (i = OUTPUT_NONE; i < NETPERF_OUTPUT_MAX; i++)
      output_human_list[j][i] = OUTPUT_END;


  /* the default for csv is the kitchen-sink.  ultimately it will be
     possible to override by providing one's own list in a file */

  if ((csv) || (keyword)) {
    if (csv_selection_file) 
      /* name of file, list to fill, number of rows/lines */
      parse_output_csv_selection_file(csv_selection_file);
    else
      set_output_csv_list_default(output_csv_list);
  }
  else {
    if (human_selection_file)
      parse_output_human_selection_file(human_selection_file);
    else
      set_output_human_list_default(output_human_list);
  }
      

}

/* why? because one cannot simply pass a pointer to snprintf :) for
   our nefarious porpoises, we only expect to handle single-value
   format statements, not a full-blown format */
int 
my_long_long_snprintf(char *buffer, size_t size, const char *format, void *value)
{
  const char *fmt = format;
  while (*fmt)
    switch (*fmt++) {
    case 'd':
    case 'i':
      return snprintf(buffer, size, format, *(long long *)value);
    case 'u':
    case 'o':
    case 'x':
    case 'X':
      return snprintf(buffer, size, format, *(unsigned long long *)value);
    }
  return -1;
}

int
my_long_snprintf(char *buffer, size_t size, const char *format, void *value)
{
  const char *fmt = format;
  while (*fmt)
    switch (*fmt++) {
    case 'd':
    case 'i':
      return snprintf(buffer, size, format, *(long *)value);
    case 'u':
    case 'o':
    case 'x':
    case 'X':
      return snprintf(buffer, size, format, *(unsigned long *)value);
    case 'l':
      return my_long_long_snprintf(buffer, size, format, value);
    }
  return -1;
}

int
my_snprintf(char *buffer, size_t size, const char *format, void *value)
{
  const char *fmt = format;

  while (*fmt)
    switch (*fmt++) {
    case 'c':
      return snprintf(buffer, size, format, *(int *)value);
    case 'f':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
      return snprintf(buffer, size, format, *(double *)value);
    case 's':
      return snprintf(buffer, size, format, (char *)value);
    case 'd':
    case 'i':
      return snprintf(buffer, size, format, *(int *)value);
    case 'u':
    case 'o':
    case 'x':
    case 'X':
      return snprintf(buffer, size, format, *(unsigned int *)value);
    case 'l':
      return my_long_snprintf(buffer, size, format, value);
    }
  return -1;
}

void
print_omni_csv()
{

  int j,k,buflen,vallen;

  char *hdr1 = NULL;
  char *val1 = NULL;
  char tmpval[1024];

  buflen = 0;
  for (j = 0; 
       ((j < NETPERF_OUTPUT_MAX) && 
	(output_csv_list[j] != OUTPUT_END));
       j++) {
    if ((netperf_output_source[output_csv_list[j]].format != NULL) &&
	(netperf_output_source[output_csv_list[j]].display_value != NULL)) {
      vallen = 
	my_snprintf(tmpval,
		    1024,
		    netperf_output_source[output_csv_list[j]].format,
		    (netperf_output_source[output_csv_list[j]].display_value));
      if (vallen == -1) {
	fprintf(where,"my_snprintf failed on %s with format %s\n",
		netperf_output_enum_to_str(j),
		netperf_output_source[output_csv_list[j]].format);
	fflush(where);
      }
      vallen += 1; /* forget not the terminator */
    }
    else
      vallen = 0;

    if (vallen > 
	netperf_output_source[output_csv_list[j]].tot_line_len)
      netperf_output_source[output_csv_list[j]].tot_line_len = vallen;
    
    buflen += 
      netperf_output_source[output_csv_list[j]].tot_line_len;
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
  char *h1 = hdr1;
  char *v1 = val1;
  for (j = 0; 
       ((j < NETPERF_OUTPUT_MAX) && 
	(output_csv_list[j] != OUTPUT_END));
       j++) {
    int len;
    len = 0; 
    if (print_headers) {
      for (k = 0; ((k < 4) && 
		   (NULL != 
		    netperf_output_source[output_csv_list[j]].line[k]) &&
		   (strcmp("",netperf_output_source[output_csv_list[j]].line[k]))); k++) {

	len = sprintf(h1,
		      "%s",
		      netperf_output_source[output_csv_list[j]].line[k]);
	*(h1 + len) = ' ';
	/* now move to the next starting column. for csv we aren't worried
	   about alignment between the header and the value lines */
	h1 += len + 1;
      }
      *(h1 - 1) = ',';
    }
    if ((netperf_output_source[output_csv_list[j]].format != NULL) &&
	(netperf_output_source[output_csv_list[j]].display_value != NULL)) {
      /* tot_line_len is bogus here, but should be "OK" ? */
      len = my_snprintf(v1,
			netperf_output_source[output_csv_list[j]].tot_line_len,
			netperf_output_source[output_csv_list[j]].format,
			netperf_output_source[output_csv_list[j]].display_value);

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

  /* ok, _now_ null terminate each line by nuking the last comma.  do
     we have an OBOB here? */
  if (print_headers) *(h1-1) = 0;
  *(v1-1) = 0;
  /* and now spit it out, but only if it is going to have something
     in it. we don't want a bunch of blank lines or nulls...  */
  if (output_csv_list[0] != OUTPUT_END) {
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

  int j;
  char tmpval[1024];
  int vallen;

  for (j = 0; 
       ((j < NETPERF_OUTPUT_MAX) && 
	(output_csv_list[j] != OUTPUT_END));
       j++) {
    if ((netperf_output_source[output_csv_list[j]].format != NULL) &&
	(netperf_output_source[output_csv_list[j]].display_value != NULL)) {
      vallen = 
	my_snprintf(tmpval,
		    1024,
		    netperf_output_source[output_csv_list[j]].format,
		    (netperf_output_source[output_csv_list[j]].display_value));
      if (vallen == -1) {
	snprintf(tmpval,
		 1024,
		 "my_snprintf failed with format %s\n",
		 netperf_output_source[output_csv_list[j]].format);
      }
      fprintf(where,
	      "%s=%s\n",netperf_output_enum_to_str(output_csv_list[j]),
	      tmpval);
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
	  (output_human_list[i][j] != OUTPUT_END));
	 j++) {
      if ((netperf_output_source[output_human_list[i][j]].format != NULL) &&
	  (netperf_output_source[output_human_list[i][j]].display_value != NULL))
	vallen = my_snprintf(tmpval,
			     1024,
			     netperf_output_source[output_human_list[i][j]].format,
			     (netperf_output_source[output_human_list[i][j]].display_value)) + 1; /* need to count the \n */
      else
	vallen = 0;

      if (vallen > 
	  netperf_output_source[output_human_list[i][j]].max_line_len)
	netperf_output_source[output_human_list[i][j]].max_line_len = vallen;
      
      buflen += 
	netperf_output_source[output_human_list[i][j]].max_line_len + 1;
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
	  (output_human_list[i][j] != OUTPUT_END));
	 j++) {
      if (print_headers) {
	for (k = 0; k < 4; k++) {
	  memcpy(h[k],
		 netperf_output_source[output_human_list[i][j]].line[k],
		 strlen(netperf_output_source[output_human_list[i][j]].line[k]));
	}
      }
      if ((netperf_output_source[output_human_list[i][j]].format != NULL) &&
	  (netperf_output_source[output_human_list[i][j]].display_value != NULL)) {
	int len;
	len = my_snprintf(v1,
			  netperf_output_source[output_human_list[i][j]].max_line_len,
			  netperf_output_source[output_human_list[i][j]].format,
			  netperf_output_source[output_human_list[i][j]].display_value);
	/* nuke the trailing \n" from the string routine.  */
	*(v1 + len) = ' ';
      }
      /* now move to the next starting column */
    for (k = 0; (k < 4) && (print_headers); k++) {
	h[k] += 
	  netperf_output_source[output_human_list[i][j]].max_line_len + 1;
      }
      v1 += netperf_output_source[output_human_list[i][j]].max_line_len + 1;
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
    if (output_human_list[i][0] != OUTPUT_END) {
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

  if (csv) 
    print_omni_csv();
  else if (keyword)
    print_omni_keyword();
  else
    print_omni_human();

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
connect_data_socket(SOCKET send_socket, struct addrinfo *remote_res) 
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
    if ((SOCKET_EADDRINUSE(ret)) || SOCKET_EADDRNOTAVAIL(ret)) {
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

int
send_data(SOCKET data_socket, struct ring_elt *send_ring, uint32_t bytes_to_send, struct sockaddr *destination, int destlen) {

  int len;

  /* if the user has supplied a destination, we use sendto, otherwise
     we use send.  we ass-u-me blocking operations always, so no need
     to check for eagain or the like. */

  if (debug > 1) {
    fprintf(where,
	    "send_data sock %d ring %p bytes %d dest %p len %d\n",
	    data_socket,
	    send_ring,
	    bytes_to_send,
	    destination,
	    destlen);
    fflush(where);
  }

  if (destination) {
    len = sendto(data_socket,
		 send_ring->buffer_ptr,
		 bytes_to_send,
		 0,
		 destination,
		 destlen);
  }
  else {
    len = send(data_socket,
	       send_ring->buffer_ptr,
	       bytes_to_send,
	       0);
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
       netperf test has been with UDP.  famous last words :) */
    if (errno == ENOBUFS)
      return -2;
    else {
      fprintf(where,"send_data: data send error: errno %d",errno);
      return -3;
    }
  }
  return len;
}

int
recv_data(SOCKET data_socket, struct ring_elt *recv_ring, uint32_t bytes_to_recv, struct sockaddr *source, int *sourcelen, uint32_t flags, uint32_t *num_receives) {

  char *temp_message_ptr;
  int bytes_left;
  int bytes_recvd;
  int my_recvs;

  /* receive data off the data_socket, ass-u-me-ing a blocking socket
     all the way!-) 2008-01-08 */
  my_recvs = 0;
  bytes_left = bytes_to_recv;
  temp_message_ptr  = recv_ring->buffer_ptr;

  if (debug > 1) {
    fprintf(where,
	    "recv_data sock %d, elt %p, bytes %d source %p srclen %d, flags %x num_recv %p\n",
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
      bytes_recvd = recvfrom(data_socket,
			     temp_message_ptr,
			     bytes_left,
			     0,
			     source,
			     sourcelen);
    }
    else {
      /* just call recv */
      bytes_recvd = recv(data_socket,
			 temp_message_ptr,
			 bytes_left,
			 0);
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
close_data_socket(SOCKET data_socket, struct sockaddr *peer, int peerlen)
{

  int ret;
  char buffer[4];

  if (protocol == IPPROTO_UDP) {
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
	    "disconnect_d_s sock %d init %d do_close %d protocol %d\n",
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

  if (protocol != IPPROTO_UDP) {
    if (initiate)
      shutdown(data_socket, SHUT_WR);
    
    /* we are expecting to get either a return of zero indicating
       connection close, or an error.  */
    bytes_recvd = recv(data_socket,
		       buffer,
		       1,
		       0);
    
    if (bytes_recvd != 0) {
      /* connection close, call close. we assume that the requisite */
      /* number of bytes have been received */
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
	if (do_close) close(data_socket);
	return -1;
      }
    }
  }
  
  if (do_close) 
    close(data_socket);
  
  return 0;
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
	    "netperf: get_transport_info: getsockopt: errno %d\n",
	    errno);
    fflush(where);
    *mss = -1;
  }
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

 /* this code is intended to be "the two routines to run them all" for
    BSDish sockets.  it comes about as part of a desire to shrink the
    code footprint of netperf and to avoid having so many blessed
    routines to alter as time goes by.  the downside is there will be
    more "ifs" than there were before. there may be some other
    "complications" for things like demo mode or perhaps histograms if
    we ever want to track individual RTTs when burst mode is in use
    etc etc... raj 2008-01-07 */

void
send_omni(char remote_host[])
{
  
  
  int len;
  int ret,rret;
  int connected = 0;
  int timed_out = 0;
  int pad_time = 0;

  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  struct sockaddr_storage remote_addr;
  struct sockaddr_storage my_addr;
  int                     remote_addr_len = sizeof(remote_addr);
  int                     my_addr_len = sizeof(my_addr);

  SOCKET	data_socket;
  int           need_socket;

  int   temp_recvs;
  
  struct addrinfo *local_res;
  struct addrinfo *remote_res;

  struct	omni_request_struct	*omni_request;
  struct	omni_response_struct	*omni_response;
  struct	omni_results_struct	*omni_result;
  
#ifdef WANT_FIRST_BURST
#define REQUEST_CWND_INITIAL 2
  /* "in the beginning..." the WANT_FIRST_BURST stuff was like both
     Unix and the state of New Jersey - both were simple an unspoiled.
     then it was realized that some stacks are quite picky about
     initial congestion windows and a non-trivial initial burst of
     requests would not be individual segments even with TCP_NODELAY
     set. so, we have to start tracking a poor-man's congestion window
     up here in window space because we want to try to make something
     happen that frankly, we cannot guarantee with the specification
     of TCP.  ain't that grand?-)  raj 2006-01-30 */
  int requests_outstanding = 0;
  int request_cwnd = REQUEST_CWND_INITIAL;  /* we ass-u-me that having
					       three requests
					       outstanding at the
					       beginning of the test
					       is ok with TCP stacks
					       of interest. the first
					       two will come from our
					       first_burst loop, and
					       the third from our
					       regularly scheduled
					       send */
#endif

  omni_request = 
    (struct omni_request_struct *)netperf_request.content.test_specific_data;
  omni_response = 
    (struct omni_response_struct *)netperf_response.content.test_specific_data;
  omni_result =
    (struct omni_results_struct *)netperf_response.content.test_specific_data;
  

  /* before we start doing things with our own requests and responses
     lets go ahead and find-out about the remote system. at some point
     we probably need to put this somewhere else... */
  get_remote_system_info();
  
#ifdef WANT_HISTOGRAM
  if (verbosity > 1) {
    time_hist = HIST_new();
  }
#endif /* WANT_HISTOGRAM */

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
    print_top_test_header("OMNI TEST",local_res,remote_res);
  }

  /* initialize a few counters */
  
  need_socket   = 1;

  if (connection_test) 
    pick_next_port_number(local_res,remote_res);

  
  /* If the user has requested cpu utilization measurements, we must */
  /* calibrate the cpu(s). We will perform this task within the tests */
  /* themselves. If the user has specified the cpu rate, then */
  /* calibrate_local_cpu will return rather quickly as it will have */
  /* nothing to do. If local_cpu_rate is zero, then we will go through */
  /* all the "normal" calibration stuff and return the rate back.*/
  
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

#ifdef WANT_FIRST_BURST
    /* we have to remember to reset the number of transactions
       outstanding and the "congestion window for each new
       iteration. raj 2006-01-31 */
    requests_outstanding = 0;
    request_cwnd = REQUEST_CWND_INITIAL;
#endif


    data_socket = create_data_socket(local_res);
    
    if (data_socket == INVALID_SOCKET) {
      perror("netperf: send_omni: unable to create data socket");
      exit(1);
    }
    need_socket = 0;

    /* we need to consider if this is a request/response test, if we
       are receiving, if we are sending, etc, when setting-up our recv
       and send buffer rings. we should only need to do this once, and
       that would be when the relevant _ring variable is NULL. raj
       2008-01-18 */
    if ((direction & NETPERF_XMIT) && (NULL == send_ring)) {
      if (req_size > 0) {
	/* request/response test */
	if (send_width == 0) send_width = 1;
	bytes_to_send = req_size;
      }
      else {
	/* stream test */
	if (send_size == 0) {
	  if (lss_size > 0) {
	    send_size = lss_size;
	  }
	  else {
	    send_size = 4096;
	  }
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
		"send_omni: %d entry send_ring obtained...\n",
		send_width);
      }
    }
    
    if ((direction & NETPERF_RECV) && (NULL == recv_ring)) {
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
		"send_omni: %d entry recv_ring obtained...\n",
		recv_width);
      }
    }
    
    if (!no_control) { /* foo */
  
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
      omni_request->send_buf_size	   = rss_size_req;
      omni_request->send_size              = remote_send_size_req;
      omni_request->send_alignment	   = remote_send_align;
      omni_request->send_offset	           = remote_send_offset;
      omni_request->send_width             = 1; /* FIX THIS */
      omni_request->request_size	   = req_size;
      
      omni_request->recv_buf_size	   = rsr_size_req;
      omni_request->receive_size           = remote_recv_size_req;
      omni_request->recv_alignment	   = remote_recv_align;
      omni_request->recv_offset	           = remote_recv_offset;
      omni_request->recv_width             = 1; /* FIX THIS */
      omni_request->response_size	   = rsp_size;
      
      omni_request->no_delay	           = rem_nodelay;
      omni_request->use_sendfile           = remote_use_sendfile;
      omni_request->connect_test           = connection_test;
      
      omni_request->measure_cpu	           = remote_cpu_usage;
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
      
      omni_request->checksum_off           = remote_checksum_off;
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
					  omni_request->ipaddr,
					  &(omni_request->netperf_port));
      
      if (debug > 1) {
	fprintf(where,"netperf: send_omni: requesting OMNI test\n");
      }
    

      send_request();

    
      /* the response from the remote should contain all the relevant
	 socket and other parameters we need to know for this test.
	 so, we can shove them back into the relevant variables here
	 and be on our way. */

      recv_response_n(OMNI_RESPONSE_CONV_CUTOFF); /* brittle, but functional */
  
      if (!netperf_response.content.serv_errno) {
	rsr_size	 = omni_response->recv_buf_size;
	remote_recv_size = omni_response->receive_size;
	rss_size	 = omni_response->send_buf_size;
	remote_send_size = omni_response->send_size;
	rem_nodelay      = omni_response->no_delay;
	remote_use_sendfile = omni_response->use_sendfile;
	remote_cpu_usage = omni_response->measure_cpu;
	remote_cpu_rate  = omni_response->cpu_rate;
	remote_send_width = omni_response->send_width;
	remote_recv_width = omni_response->recv_width;
	/* make sure that port numbers are in network order because
	   recv_response will have put everything into host order */
	set_port_number(remote_res,
			(unsigned short)omni_response->data_port);
	
	if (debug) {
	  fprintf(where,"remote listen done.\n");
	  fprintf(where,"remote port is %u\n",get_port_number(remote_res));
	  fflush(where);
	}
	/* just in case the remote didn't null terminate */
	if (NULL == remote_system_model) {
	  omni_response->system_model[sizeof(omni_response->system_model)-1] = 0;
	  remote_system_model = strdup(omni_response->system_model);
	}
	if (NULL == remote_cpu_model) {
	  omni_response->cpu_model[sizeof(omni_response->cpu_model) -1 ] = 0;
	  remote_cpu_model = strdup(omni_response->cpu_model);
	}
	remote_cpu_frequency = omni_response->cpu_frequency;

	if (NULL == remote_security_specific) {
	  omni_response->security_string[sizeof(omni_response->security_string) - 1] = 0;
	  remote_security_specific = strdup(omni_response->security_string);
	}
	/* top bits type, bottom bits enabled */
	remote_security_type_id = (int) omni_response->security_info >> 16;
	remote_security_enabled_num = (short)omni_response->security_info;
	remote_security_type = nsec_type_to_str(remote_security_type_id);
	remote_security_enabled = 
	  nsec_enabled_to_str(remote_security_enabled_num);
      }
      else {
	Set_errno(netperf_response.content.serv_errno);
	fprintf(where,
		"netperf: remote error %d",
		netperf_response.content.serv_errno);
	perror("");
	fflush(where);
	exit(1);
      }
    
    }
    else {
      if (NULL == remote_system_model) 
	remote_system_model = strdup("Unknown System Model");
      if (NULL == remote_cpu_model)
	remote_cpu_model = strdup("Unknown CPU Model");
      remote_cpu_frequency = -1;
    }

#ifdef WANT_DEMO
    /* at some point we will have to be more clever about this, but
       for now we won't */

    DEMO_RR_SETUP(100);
#endif

    /* if we are not a connectionless protocol, we need to connect. at
       some point even if we are a connectionless protocol, we may
       still want to "connect" for convenience raj 2008-01-14 */
    need_to_connect = (protocol != IPPROTO_UDP);
      
    /* Set-up the test end conditions. For tests over a
       "reliable/connection-oriented" transport (eg TCP, SCTP, etc) this
       can be either time or byte/transaction count based.  for
       unreliable transport or connection tests it can only be time
       based.  having said that, we rely entirely on other code to
       enforce this before we even get here. raj 2008-01-08 */
    
    if (test_time) {
      /* The user wanted to end the test after a period of time.  if
	 we are a recv-only test, we need to protect ourself against
	 the remote going poof, but we want to make sure we don't
	 give-up before they finish, so we will add a PAD_TIME to the
	 timer.  if we are RR or XMIT, there should be no need for
	 padding */
      times_up = 0;
      units_remaining = 0;
      if ((!no_control) && (NETPERF_RECV_ONLY(direction)))
	pad_time = PAD_TIME;
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
      HIST_timestamp(demo_one_ptr);
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
      
#ifdef WANT_HISTOGRAM
      /* only pull the timestamp if we are actually going to use the
	 results of the work.  we put the call here so it can work for
	 any sort of test - connection, request/response, or stream.
	 no, it isn't "perfect" for all of them - for some it will
	 include a few more "if's" than a purpose-written routine, but
	 it _should_ be the case that the time spent up here is
	 epsilon compared to time spent elsewhere in the stack so it
	 should not be a big deal.  famous last words of raj
	 2008-01-08 */
      if (verbosity > 1) {
	HIST_timestamp(&time_one);
      }
#endif /* WANT_HISTOGRAM */

    again:

      if (need_socket) {
	if (connection_test) 
	  pick_next_port_number(local_res,remote_res);

	data_socket = create_data_socket(local_res);
  
	if (data_socket == INVALID_SOCKET) {
	  perror("netperf: send_omni: unable to create data socket");
	  exit(1);
	}
	need_socket = 0;
      }

      /* only connect if and when we need to */
      if (need_to_connect) {
	/* assign to data_socket since connect_data_socket returns
	   SOCKET and not int thanks to Windows. */
	ret = connect_data_socket(data_socket,remote_res);
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
	  fprintf(where,"transient! transient! torpedo in the water!\n");
	  fflush(where);
	  close(data_socket);
	  connected = 0;  /* probably redundant but what the heck... */
	  need_socket = 1;
	  need_to_connect = 1;
	  /* this will stuff the next local port number within bounds
	     into our local res, and then when the goto has us
	     allocating a new socket it will do the right thing with the
	     bind() call */
	  pick_next_port_number(local_res,remote_res);
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
      /* we can inject no more than request_cwnd, which will grow with
	 time, and no more than first_burst_size.  we don't use <= to
	 account for the "regularly scheduled" send call.  of course
	 that makes it more a "max_outstanding_ than a
	 "first_burst_size" but for now we won't fix the names. also,
	 I suspect the extra check against < first_burst_size is
	 redundant since later I expect to make sure that request_cwnd
	 can never get larger than first_burst_size, but just at the
	 moment I'm feeling like a belt and suspenders kind of
	 programmer. raj 2006-01-30 */
      /* we only want to inject the burst if this is a full-on
	 request/response test. otherwise it doesn't make any sense
	 anyway. raj 2008-01-25 */
      while ((first_burst_size > 0) &&
	     (requests_outstanding < request_cwnd) &&
	     (requests_outstanding < first_burst_size) &&
	     (NETPERF_IS_RR(direction)) &&
	     (!connection_test)) {
	if (debug) {
	  fprintf(where,
		  "injecting, req_outstanding %d req_cwnd %d burst %d\n",
		  requests_outstanding,
		  request_cwnd,
		  first_burst_size);
	}
	if ((ret = send_data(data_socket,
			     send_ring,
			     bytes_to_send,
			     (connected) ? NULL : remote_res->ai_addr,
			     remote_res->ai_addrlen)) != bytes_to_send) {
	  /* in theory, we should never hit the end of the test in the
	     first burst */
	  perror("send_omni: initial burst data send error");
	  exit(-1);
	}
	local_send_calls += 1;
	requests_outstanding += 1;
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
			remote_res->ai_addrlen);
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
	else {
	  /* presently at least, -2 and -3 are equally bad on recv */
	  perror("netperf: send_omni: recv_data failed");
	  exit(1);
	}
	recv_ring = recv_ring->next;

#ifdef WANT_FIRST_BURST
	/* so, since we've gotten a response back, update the
	   bookkeeping accordingly.  there is one less request
	   outstanding and we can put one more out there than before. */
	requests_outstanding -= 1;
	if ((request_cwnd < first_burst_size) &&
	    (NETPERF_IS_RR(direction))) {
	  request_cwnd += 1;
	  if (debug) {
	    fprintf(where,
		    "incr req_cwnd to %d first_burst %d reqs_outstndng %d\n",
		    request_cwnd,
		    first_burst_size,
		    requests_outstanding);
	  }
	}
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

	/* we will only make this call the one time - after the first
	   call, the value will be real or -1. if this is a connection
	   test we want to do this here because later we won't be
	   connected and the data may no longer be available */
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
	     around the loop */
	  need_to_connect = 1;
	  connected = 0;
	  need_socket = 1;
	  pick_next_port_number(local_res,remote_res);
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


#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	HIST_timestamp(&time_two);
	HIST_add(time_hist,delta_micro(&time_one,&time_two));
      }
#endif /* WANT_HISTOGRAM */
    
#ifdef WANT_DEMO
      if (NETPERF_IS_RR(direction)) {
	DEMO_INTERVAL(1);
      }
      else if (NETPERF_XMIT_ONLY(direction)) {
	DEMO_INTERVAL(bytes_to_send);
      }
      else {
	DEMO_INTERVAL(rret);
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

    if (transport_mss == -2) 
      get_transport_info(data_socket,
			 &transport_mss,
			 local_res->ai_protocol);
    

    find_security_info(&local_security_enabled_num,
		       &local_security_type_id,
		       &local_security_specific);
    local_security_enabled = nsec_enabled_to_str(local_security_enabled_num);
    local_security_type    = nsec_type_to_str(local_security_type_id);

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
    
    find_system_info(&local_system_model,
		     &local_cpu_model,
		     &local_cpu_frequency);

    local_interface_name = 
      find_egress_interface(local_res->ai_addr,remote_res->ai_addr);

    find_driver_info(local_interface_name,local_driver_name,local_driver_version,local_driver_firmware,local_driver_bus,32);

    local_interface_slot = find_interface_slot(local_interface_name);

    find_interface_ids(local_interface_name,
		       &local_interface_vendor,
		       &local_interface_device,
		       &local_interface_subvendor,
		       &local_interface_subdevice);

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
  
      recv_response_n(OMNI_RESULTS_CONF_CUTOFF);
      if (!netperf_response.content.serv_errno) {
	if (debug)
	  fprintf(where,"remote results obtained\n");
	remote_cpu_method = format_cpu_method(omni_result->cpu_method);
	/* why?  because some stacks want to be clever and autotune their
	   socket buffer sizes, which means that if we accept the defaults,
	   the size we get from getsockopt() at the beginning of a
	   connection may not be what we would get at the end of the
	   connection... */
	lib_num_rem_cpus = omni_result->num_cpus;
	lib_remote_peak_cpu_util = (double)omni_result->peak_cpu_util;
	lib_remote_peak_cpu_id = omni_result->peak_cpu_id;
	rsr_size_end = omni_result->recv_buf_size;
	rss_size_end = omni_result->send_buf_size;
	remote_bytes_sent = (uint64_t)omni_result->bytes_sent_hi << 32;
	remote_bytes_sent += omni_result->bytes_sent_lo;
	remote_send_calls = omni_result->send_calls;
	remote_bytes_received = (uint64_t)omni_result->bytes_received_hi << 32;
	remote_bytes_received += omni_result->bytes_received_lo;
	remote_receive_calls = omni_result->recv_calls;
	remote_bytes_xferd = remote_bytes_received + remote_bytes_sent;
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
	omni_result->ifname[15] = 0; /* belt and suspenders */
	remote_interface_name = strdup(omni_result->ifname);
	remote_interface_slot = strdup(omni_result->ifslot);
	strncpy(remote_driver_name,omni_result->driver,32);
	strncpy(remote_driver_version,omni_result->version,32);
	strncpy(remote_driver_firmware,omni_result->firmware,32);
	strncpy(remote_driver_bus,omni_result->bus,32);
	remote_driver_name[31] = 0;
	remote_driver_version[31] = 0;
	remote_driver_firmware[31] = 0;
	remote_driver_bus[31] = 0;
	remote_interface_vendor = omni_result->vendor;
	remote_interface_device = omni_result->device;
	remote_interface_subvendor = omni_result->subvendor;
	remote_interface_subdevice = omni_result->subdevice;
      }
      else {
	Set_errno(netperf_response.content.serv_errno);
	fprintf(where,
		"netperf: remote error %d",
		netperf_response.content.serv_errno);
	perror("");
	fflush(where);
      
	exit(1);
      }
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
    
    bytes_xferd  = bytes_sent + bytes_received;

    /* if the output format is 'x' we know the test was
       request/response.  if the libfmt is something else, it could be
       xmit, recv or bidirectional. if we were the receiver then we
       can use our byte totals even if it is
       UDP/unreliable. otherwise, we use the remote totals - they
       should be the same if the protocol is reliable, and if it is
       unreliable then we want what was actually received */
    if ('x' == libfmt)
      /* it was a request/response test */
      thruput = calc_thruput(trans_completed);
    else if (NETPERF_RECV_ONLY(direction))
      thruput      = calc_thruput(bytes_xferd);
    else 
      thruput = calc_thruput(remote_bytes_xferd);

    if (NETPERF_IS_RR(direction)) {
      char tmpfmt;
      if (!connection_test) {
      /* calculate the round trip latency, using the transaction rate
	 whether or not the user was asking for thruput to be in 'x'
	 units please... however... a connection_test only ever has
	 one transaction in flight at one time */
      rtt_latency = 
	(((double)1.0/(trans_completed/elapsed_time)) * (double)1000000.0) * 
	(double) (1 + ((first_burst_size > 0) ? first_burst_size : 0));
      }
      else 
	rtt_latency = 
	  ((double)1.0/(trans_completed/elapsed_time)) * (double)1000000.0;
      tmpfmt = libfmt;
      libfmt = 'x';
      transaction_rate = calc_thruput(trans_completed);
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

      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
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
  local_cpu_utilization_double = (double)local_cpu_utilization;
  local_service_demand_double = (double)local_service_demand;
  remote_cpu_utilization_double = (double)remote_cpu_utilization;
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
  if ('x' == libfmt) libfmt = 'm';
  local_send_thruput = calc_thruput(bytes_sent);
  local_recv_thruput = calc_thruput(bytes_received);
  remote_send_thruput = calc_thruput(remote_bytes_sent);
  remote_recv_thruput = calc_thruput(remote_bytes_received);

  print_omni();

#if defined(DEBUG_OMNI_OUTPUT)  
 {
   /* just something quick to sanity check the output selectors. this
      should be gone for "production" :) */
   int i;
   print_omni_init();
   output_csv_list[1] = OUTPUT_END;
   for (i = OUTPUT_NONE; i < NETPERF_OUTPUT_MAX; i++) {
     output_csv_list[0] = i;
     print_omni_csv();
   }
 }
#endif

  /* likely as not we are going to do something slightly different here */
  if (verbosity > 1) {

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
    fflush(where);
    HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */

  }
  
}



/* the name is something of a misnomer since this test could send, or
   receive, or both, but it matches the historical netperf routine
   naming. */
void
recv_omni()
{
  
  char  *message;
  struct addrinfo *local_res;
  char local_name[BUFSIZ];
  char port_buffer[PORTBUFSIZE];

  struct sockaddr_storage myaddr_in, peeraddr_in;
  SOCKET s_listen, data_socket;
  netperf_socklen_t 	addrlen;

  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  int	timed_out = 0;
  int   pad_time = 0;
  int   need_to_connect;
  int   need_to_accept;
  int   connected;
  int   ret;
  int   temp_recvs;
  
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
    fprintf(where,"netserver: recv_omni: entered...\n");
    fflush(where);
  }
  
  /* based on what we have been told by the remote netperf, we want to
     setup our endpoint for the "data connection" and let the remote
     netperf know the situation. */

  if (debug) {
    fprintf(where,"recv_omni: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response.content.response_type = OMNI_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_omni: the response type is set...\n");
    fflush(where);
  }

  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_omni: grabbing a socket...\n");
    fflush(where);
  }

  /* create_data_socket expects to find some things in the global
     variables, so set the globals based on the values in the request.
     once the socket has been created, we will set the response values
     based on the updated value of those globals. raj 7/94 */
  lss_size_req = omni_request->send_buf_size;
  lsr_size_req = omni_request->recv_buf_size;
  loc_nodelay = omni_request->no_delay;
  loc_rcvavoid = omni_request->so_rcvavoid;
  loc_sndavoid = omni_request->so_sndavoid;

#ifdef WANT_INTERVALS
  interval_usecs = omni_request->interval_usecs;
  interval_wate  = interval_usecs / 1000;
  interval_burst = omni_request->interval_burst;
#else
  interval_usecs = 0;
  interval_wate  = 1;
  interval_burst = 0;
#endif

  connection_test = omni_request->connect_test;
  direction       = omni_request->direction;

  set_hostname_and_port(local_name,
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

  s_listen = create_data_socket(local_res);
  
  if (s_listen == INVALID_SOCKET) {
    netperf_response.content.serv_errno = errno;
    send_response();
    if (debug) {
      fprintf(where,"could not create data socket\n");
      fflush(where);
    }
    exit(1);
  }

  /* We now alter the message_ptr variables to be at the desired */
  /* alignments with the desired offsets. */
  
  if (debug) {
    fprintf(where,
	    "recv_omni: requested recv alignment of %d offset %d\n",
	    omni_request->recv_alignment,
	    omni_request->recv_offset);
    fprintf(where,
	    "recv_omni: requested send alignment of %d offset %d\n",
	    omni_request->send_alignment,
	    omni_request->send_offset);
    fflush(where);
  }

  omni_response->send_size = omni_request->send_size;
  omni_response->send_width = omni_request->send_width;
  if (omni_request->direction & NETPERF_XMIT) {
    if (omni_request->response_size > 0) {
      /* request/response_test */
      bytes_to_send = omni_request->response_size;
      if (omni_request->send_width == 0) send_width = 1;
      else send_width = omni_request->send_width;
    }
    else {
      if (omni_request->send_size == -1) {
	if (lss_size > 0) bytes_to_send = lss_size;
	else bytes_to_send = 4096;
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
  omni_response->recv_width = omni_response->recv_width;
  if (omni_request->direction & NETPERF_RECV) {
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
     accept */
  if (need_to_accept) {
    if (listen(s_listen, 5) == SOCKET_ERROR) {
      netperf_response.content.serv_errno = errno;
      close(s_listen);
      send_response();
      if (debug) {
	fprintf(where,"could not listen\n");
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
    exit(1);
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
  omni_response->measure_cpu = 0;
  if (omni_request->measure_cpu) {
    omni_response->measure_cpu = 1;
    omni_response->cpu_rate = 
      calibrate_local_cpu(omni_request->cpu_rate);
  }
  
  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  omni_response->send_buf_size = lss_size;
  omni_response->recv_buf_size = lsr_size;
  omni_response->no_delay = loc_nodelay;
  omni_response->so_rcvavoid = loc_rcvavoid;
  omni_response->so_sndavoid = loc_sndavoid;
  omni_response->interval_usecs = interval_usecs;
  omni_response->interval_burst = interval_burst;

  find_system_info(&local_system_model,&local_cpu_model,&local_cpu_frequency);
  strncpy(omni_response->system_model,local_system_model,sizeof(omni_response->system_model));
  omni_response->system_model[sizeof(omni_response->system_model)-1] = 0;
  strncpy(omni_response->cpu_model,local_cpu_model,sizeof(omni_response->cpu_model));
  omni_response->cpu_model[sizeof(omni_response->cpu_model)-1] = 0;
  omni_response->cpu_frequency = local_cpu_frequency;

  find_security_info(&local_security_enabled_num,
		     &local_security_type_id,
		     &local_security_specific);
  /* top bits type, bottom bits enabled */
  omni_response->security_info = local_security_type_id << 16;
  omni_response->security_info += local_security_enabled_num & 0xffff;
  strncpy(omni_response->security_string,
	  local_security_specific,
	  sizeof(omni_response->security_string));
  omni_response->security_string[sizeof(omni_response->security_string)-1] = 0;

  send_response_n(OMNI_RESPONSE_CONV_CUTOFF); /* brittle, but functional */

  local_send_calls = 0;
  local_receive_calls = 0;

  addrlen = sizeof(peeraddr_in);
  memset(&peeraddr_in,0,sizeof(peeraddr_in));

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(omni_request->measure_cpu);

  /* if the test is timed, set a timer of suitable length.  if the
     test is by byte/transaction count, we don't need a timer - or
     rather we rely on the netperf to only ask us to do transaction
     counts over "reliable" protocols.  perhaps at some point we
     should add a check herebouts to verify that... */

  if (omni_request->test_length > 0) {
    times_up = 0;
    units_remaining = 0;
    /* if we are the sender and only sending, then we don't need/want
       the padding, otherwise, we need the padding */ 
    if (!(NETPERF_XMIT_ONLY(omni_request->direction)))
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
	fprintf(where,"recv_omni: accept: errno = %d\n",errno);
	fflush(where);
	close(s_listen);
	
	exit(1);
      }
      
      if (debug) {
	fprintf(where,"recv_omni: accepted data connection.\n");
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
	 socket */
      if (omni_request->protocol == IPPROTO_UDP) {
	data_socket = s_listen;
	set_sockaddr_family_addr_port(&peeraddr_in,
				      nf_to_af(omni_request->ipfamily),
				      omni_request->ipaddr,
				      omni_request->netperf_port);
      }
    }

    if (need_to_connect) {
      /* initially this will only be used for UDP tests as a TCP or
	 other connection-oriented test will always have us making an
	 accept() call raj 2008-01-11 */
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
	!times_up) {
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
      }
      else if (ret == 0) {
	/* is this the end of a test, just a zero-byte recv, or
	   something else? that is an exceedingly good question and
	   one for which I don't presently have a good answer, but
	   that won't stop me from guessing :) raj 2008-01-09 */
	fprintf(where,"read zero conn_test %d null_message_ok %d\n",
		connection_test,null_message_ok);
	fflush(where);
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
	fprintf(where,"YO! TIMESUP!\n");
	fflush(where);
	times_up = 1;
	timed_out = 1;
	break;
      }
      else {
	/* presently at least, -2 and -3 are equally bad on recv */
	/* we need a response message here for the control connection
	   before we exit! */
	exit(1);
      }
      recv_ring = recv_ring->next;
    }

    /* if we should try to send something, then by all means, let us
       try to send something. */
    if ((omni_request->direction & NETPERF_XMIT) &&
	!times_up) {
      ret = send_data(data_socket,
		      send_ring,
		      bytes_to_send,
		      (connected) ? NULL : (struct sockaddr *)&peeraddr_in,
		      addrlen);

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
	exit(1);
      }

    }

    if (connection_test) {
#ifdef __linux
      /* so, "Linux" with autotuning likes to alter the socket buffer
	 sizes over the life of the connection, but only does so when
	 one takes the defaults at time of socket creation.  if we
	 took those defaults, we should inquire as to what the values
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
      ret = close_data_socket(data_socket,NULL,0);
      if (ret == -1) {
	times_up = 1;
	timed_out = 1;
	break;
      }
      else if (ret < 0) {
	perror("netperf: recv_omni: close_data_socket failed");
	fflush(where);
	exit(1);
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
  
  cpu_stop(omni_request->measure_cpu,&elapsed_time);
  
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
    close_data_socket(data_socket,NULL,0);
  }
  else {
    close_data_socket(data_socket,(struct sockaddr *)&peeraddr_in,addrlen);
    lsr_size_end = lsr_size;
    lss_size_end = lss_size;
  }

  /* send the results to the sender  */
  
  omni_results->send_calls      = local_send_calls;
  omni_results->bytes_received_lo = bytes_received & 0x00000000FFFFFFFFULL;
  omni_results->bytes_received_hi = (bytes_received & 0xFFFFFFFF00000000ULL) >> 32;
  omni_results->recv_buf_size   = lsr_size_end;
  omni_results->recv_calls      = local_receive_calls;
  omni_results->bytes_sent_lo   = bytes_sent & 0x00000000FFFFFFFFULL;
  omni_results->bytes_sent_hi   = (bytes_sent & 0xFFFFFFFF00000000ULL) >> 32;
  omni_results->send_buf_size   = lss_size_end;
  omni_results->trans_received	= trans_completed;
  omni_results->elapsed_time	= elapsed_time;
  omni_results->cpu_method      = cpu_method;
  omni_results->num_cpus        = lib_num_loc_cpus;
  if (omni_request->measure_cpu) {
    omni_results->cpu_util = calc_cpu_util(elapsed_time);
  }
  omni_results->peak_cpu_util   = (float)lib_local_peak_cpu_util;
  omni_results->peak_cpu_id     = lib_local_peak_cpu_id;
  local_interface_name = 
    find_egress_interface(local_res->ai_addr,(struct sockaddr *)&peeraddr_in);
  strncpy(omni_results->ifname,local_interface_name,16);
  omni_results->ifname[15] = 0;
  local_interface_slot = find_interface_slot(local_interface_name);
  strncpy(omni_results->ifslot,local_interface_slot,16);
  omni_results->ifslot[15] = 0;
  find_interface_ids(local_interface_name,
		     &omni_results->vendor,
		     &omni_results->device,
		     &omni_results->subvendor,
		     &omni_results->subdevice);
  find_driver_info(local_interface_name,
		   omni_results->driver,
		   omni_results->version,
		   omni_results->firmware,
		   omni_results->bus,
		   32);
  if (debug) {
    fprintf(where,
	    "recv_omni: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response_n(OMNI_RESULTS_CONF_CUTOFF);

  /* when we implement this, it will look a little strange, but we do
     it to avoid certain overheads when running aggregates and using
     confidence intervals.  we will post a recv_request() call to get
     the next message or EOF on the control connection.  either the
     netperf will close the control connection, which will tell us we
     are done, or the netperf will send us another "DO_OMNI" message,
     which by definition should be identical to the first DO_OMNI
     message we received.

     in this way we can avoid overheads like allocating the buffer
     rings and the listen socket and the like */
  
}

void
scan_omni_args(int argc, char *argv[])

{

#define OMNI_ARGS "b:cCd:DnNhH:kL:m:M:oOp:P:r:s:S:t:T:u:Vw:W:46"

  extern char	*optarg;	  /* pointer to option string	*/
  
  int		c;
  int           have_uuid = 0;

  char	
    arg1[BUFSIZ],  /* argument holders		*/
    arg2[BUFSIZ];

  if (debug) {
    int i;
    printf("%s called with the following argument vector\n",
	   __func__);
    for (i = 0; i< argc; i++) {
      printf("%s ",argv[i]);
    }
    printf("\n");
  }

  strncpy(local_data_port,"0",sizeof(local_data_port));
  strncpy(remote_data_port,"0",sizeof(remote_data_port));

  /* default to a STREAM socket type. i wonder if this should be part
     of send_omni or here... */
  socket_type = nst_to_hst(NST_STREAM);
  socket_type_str = hst_to_str(socket_type);

  /* default to TCP. i wonder if this should be here or in
     send_omni? */
#ifdef IPPROTO_TCP
  protocol = IPPROTO_TCP;
#endif

  /* we will check to see if this needs to remain 0 or set to
     something else when we get finished scanning all the argument
     values */
  direction = 0;

  /* default is to be a stream test, so req_size and rsp_size should
     be < 0)  */

  req_size = rsp_size = -1;
     
  /* Go through all the command line arguments and break them */
  /* out. For those options that take two parms, specifying only */
  /* the first will set both to that value. Specifying only the */
  /* second will leave the first untouched. To change only the */
  /* first, use the form "first," (see the routine break_args.. */
  
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
      print_sockets_usage();
      exit(1);
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
      /* arbitrarily set the direction variable */
      direction = strtol(optarg,NULL,0);
      break;
    case 'D':
      /* set the TCP nodelay flag */
      loc_nodelay = 1;
      rem_nodelay = 1;
      break;
    case 'H':
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	/* make sure we leave room for the NULL termination boys and
	   girls. raj 2005-02-82 */ 
	remote_data_address = malloc(strlen(arg1)+1);
	strcpy(remote_data_address,arg1);
      }
      if (arg2[0])
	remote_data_family = parse_address_family(arg2);
      break;
    case 'k':
      csv = 0;
      keyword = 1;
      /* obliterate any previous file name */
      if (human_selection_file) {
	free(human_selection_file);
	human_selection_file = NULL;
      }
      if (csv_selection_file) {
	free(csv_selection_file);
	csv_selection_file = NULL;
      }
      if (argv[optind] && ((unsigned char)argv[optind][0] != '-')) {
	/* we assume that what follows is the name of a file with the
	   list of desired output values. */
	csv_selection_file = strdup(argv[optind]);
	optind++;
	/* special case - if the file name is "?" then we will emit a
	   list of the available outputs */
	if (strcmp(csv_selection_file,"?") == 0) {
	  dump_netperf_output_list(stdout,1);
	  exit(1);
	}
      }
      break;
    case 'L':
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	/* make sure we leave room for the NULL termination boys and
	   girls. raj 2005-02-82 */ 
	local_data_address = malloc(strlen(arg1)+1);
	strcpy(local_data_address,arg1);
      }
      if (arg2[0])
	local_data_family = parse_address_family(arg2);
      break;
    case 'm':
      /* set the send size. if we set the local send size it will add
	 XMIT to direction.  if we set the remote send size it will
	 add RECV to the direction.  likely as not this will need some
	 additional throught */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	send_size = convert(arg1);
	direction |= NETPERF_XMIT;
      }
      if (arg2[0]) {
	remote_send_size_req = convert(arg2);
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
	direction |= NETPERF_XMIT;
      }
      if (arg2[0]) {
	recv_size = convert(arg2);
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
      csv = 1;
      keyword = 0;
      /* obliterate any previous file name */
      if (human_selection_file) {
	free(human_selection_file);
	human_selection_file = NULL;
      }
      if (csv_selection_file) {
	free(csv_selection_file);
	csv_selection_file = NULL;
      }
      if (argv[optind] && ((unsigned char)argv[optind][0] != '-')) {
	/* we assume that what follows is the name of a file with the
	   list of desired output values. */
	csv_selection_file = strdup(argv[optind]);
	optind++;
	/* special case - if the file name is "?" then we will emit a
	   list of the available outputs */
	if (strcmp(csv_selection_file,"?") == 0) {
	  dump_netperf_output_list(stdout,1);
	  exit(1);
	}
      }
      break;
    case 'O':
      csv = 0;
      keyword = 0;
      /* obliterate any previous file name */
      if (human_selection_file) {
	free(human_selection_file);
	human_selection_file = NULL;
      }
      if (csv_selection_file) {
	free(csv_selection_file);
	csv_selection_file = NULL;
      }
      if (argv[optind] && ((unsigned char)argv[optind][0] != '-')) {
	/* we assume that what follows is the name of a file with the
	   list of desired output values */
	human_selection_file = strdup(argv[optind]);
	optind++;
	if (strcmp(human_selection_file,"?") == 0) {
	  dump_netperf_output_list(stdout,0);
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
    case 'r':
      /* set the request/response sizes. setting request/response
	 sizes implicitly sets direction to XMIT and RECV */ 
      direction |= NETPERF_XMIT;
      direction |= NETPERF_RECV;
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	req_size = convert(arg1);
      if (arg2[0])	
	rsp_size = convert(arg2);
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
#ifdef SO_SND_COPYAVOID
      loc_sndavoid = 1;
#else
      loc_sndavoid = 0;
      printf("Local send copy avoidance not available.\n");
#endif
#ifdef SO_RCV_COPYAVOID
      loc_rcvavoid = 1;
#else
      loc_rcvavoid = 0;
      printf("Local recv copy avoidance not available.\n");
#endif
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
  /* some other sanity checks we need to make would include stuff when
     the user has set -m and -M such that both XMIT and RECV are set
     and has not set -r. initially we will not allow that.  at some
     point we might allow that if the user has also set -r, but until
     then the code will simply ignore the values from -m and -M when
     -r is set. */

#if defined(WANT_FIRST_BURST) 
#if defined(WANT_HISTOGRAM)
  /* if WANT_FIRST_BURST and WANT_HISTOGRAM are defined and the user
     indeed wants a non-zero first burst size, and we would emit a
     histogram, then we should emit a warning that the two are not
     compatible. raj 2006-01-31 */
  if ((first_burst_size > 0) && (verbosity >= 2)) {
    fprintf(stderr,
	    "WARNING! Histograms and first bursts are incompatible!\n");
    fflush(stderr);
  }
#endif
#endif

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

    fprintf(where,"I don't know about no control connection tests yet\n");
    exit(1);

    if (strcmp(remote_data_port,"0") == 0) {
      /* we need to select either the discard port, echo port or
	 chargen port dedepending on the test name. raj 2007-02-08 */
      if (strstr(test_name,"STREAM") ||
	  strstr(test_name,"SENDFILE")) {
	strncpy(remote_data_port,"discard",sizeof(remote_data_port));
      }
      else if (strstr(test_name,"RR")) {
	strncpy(remote_data_port,"echo",sizeof(remote_data_port));
      }
      else if (strstr(test_name,"MAERTS")) {
	strncpy(remote_data_port,"chargen",sizeof(remote_data_port));
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

    if (strstr(test_name,"STREAM") ||
	strstr(test_name,"SENDFILE")) {
      recv_size = -1;
    }
    else if (strstr(test_name,"RR")) {
      /* I am however _certain_ that for a no control RR test the
	 response size must equal the request size since 99 times out
	 of ten we will be speaking to the echo service somewhere */
      rsp_size = req_size;
    }
    else if (strstr(test_name,"MAERTS")) {
      send_size = -1;
    }
    else {
      printf("No default port known for the %s test, please set one yourself\n",test_name);
      exit(-1);
    }
  }
}

#endif /* WANT_OMNI */
