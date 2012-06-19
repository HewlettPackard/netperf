/*
        Copyright (C) 1993-2012 Hewlett-Packard Company
*/

/* libraried performance include file 				*/
/* the define NOPERFEXTERN tels us not to re-define all the 	*/

/* defines and defaults */
#define		HOSTNAMESIZE 	255
#define         PORTBUFSIZE     10
#define		DEFAULT_SIZE	32768
#define		HOST_NAME	"127.0.0.1"
#define		TEST_PORT	"12865"

/* output controlling variables                                         */
#define 	DEBUG 0		/* debugging level			*/
#define 	VERBOSITY 0	/* verbosity level			*/

/* the end-test conditions for the tests - either transactions, bytes,  */
/* or time. different vars used for clarity - space is cheap ;-)        */
#define 	TEST_TIME 10	/* test ends by time			*/
#define 	TEST_BYTES 0	/* test ends on byte count		*/
#define 	TEST_TRANS 0	/* test ends on tran count		*/

/* the alignment conditions for the tests				*/
#define 	LOC_RECV_ALIGN	4	/* alignment for local receives	*/
#define 	LOC_SEND_ALIGN	4	/* alignment for local sends	*/
#define 	REM_RECV_ALIGN	4	/* alignment for remote receive	*/
#define 	REM_SEND_ALIGN	4	/* alignment for remote sends	*/

/* which way are we going and what are we doing in this handbasket?-) */
#define NETPERF_XMIT 0x2
#define NETPERF_RECV 0x4

#define NETPERF_IS_RR(x) (((x & NETPERF_XMIT) && (x & NETPERF_RECV)) || \
			  (!((x & NETPERF_XMIT) || (x & NETPERF_RECV))))

#define NETPERF_RECV_ONLY(x) ((x & NETPERF_RECV) && !(x & NETPERF_XMIT))

#define NETPERF_XMIT_ONLY(x) ((x & NETPERF_XMIT) && !(x & NETPERF_RECV))

#define NETPERF_CC(x) (!(x & NETPERF_XMIT) && !(x & NETPERF_RECV))


/* misc defines for the hell of it					*/
#ifndef MAXLONG
#define 	MAXLONG  	4294967295UL
#endif /* MAXLONG */

#ifdef WANT_DCCP

/* include netinet/in.h to see if SOCK_DCCP and IPPROTO_DCCP are there */
#include <netinet/in.h>

#ifndef SOCK_DCCP
#define DCCP_WARNING
#define SOCK_DCCP 6
#endif

#ifndef IPPROTO_DCCP
#define DCCP_WARNING
#define IPPROTO_DCCP 33  /* defined by the IANA */
#endif

#ifndef SOL_DCCP
#define DCCP_WARNING
#define SOL_DCCP 269
#endif

#ifdef DCCP_WARNING
#warning This platform is missing one of sock_dccp ipproto_dccp or sol_dccp
#endif

#endif

#ifndef NETSH
extern char		*program; /* program invocation name		*/
extern char             *command_line;  /* how we were invoked          */

extern char  *passphrase;

/* stuff to say where this test is going                                */
extern char	host_name[HOSTNAMESIZE];/* remote host name or ip addr  */
extern char     local_host_name[HOSTNAMESIZE];
extern char	test_port[PORTBUFSIZE]; /* where is the test waiting    */
extern char     local_test_port[PORTBUFSIZE];
extern int      address_family;
extern int      local_address_family;
extern int      parse_address_family(char family_string[]);
extern int      parse_socket_type(char socket_string[]);
extern int      parse_protocol(char protocol_string[]);
extern int      parse_direction(char direction_string[]);
extern void     set_defaults();
extern void     scan_cmd_line(int argc, char *argv[]);
extern void     dump_globals();
extern void     break_args(char *s, char *arg1, char *arg2);
extern void     break_args_explicit(char *s, char *arg1, char *arg2);
extern void     break_args_explicit_sep(char *s, int sep, char *arg1, char *arg2);
extern void     print_netserver_usage();

/* output controlling variables                                         */
extern int
  debug,	    /* debugging level */
  print_headers,    /* do/don't print test headers */
  verbosity,	    /* verbosity level */
  keep_histogram,   /* do we keep a histogram of interesting times? */
  keep_statistics;  /* do we keep/calculate expensive statistics? */

/* the end-test conditions for the tests - either transactions, bytes,  */
/* or time. different vars used for clarity - space is cheap ;-)        */
extern int
  test_time,		/* test ends by time			*/
  test_len_ticks,
  test_bytes,		/* test ends on byte count		*/
  test_trans;		/* test ends on tran count		*/

/* wait time between control/data connection establishment and start
   of data traffic  */
extern int wait_time_secs;

/* the alignment conditions for the tests				*/
extern int
  local_recv_align,	/* alignment for local receives		*/
  local_send_align,	/* alignment for local sends		*/
  remote_recv_align,	/* alignment for remote receives	*/
  remote_send_align,	/* alignment for remote sends		*/
  local_send_offset,
  local_recv_offset,
  remote_send_offset,
  remote_recv_offset,
  remote_send_width,
  remote_recv_width;

/* hoist these above the #if to deal with either netperf or netserver
   configured for it */

extern	int          interval_usecs;
extern  int          interval_wate;
extern	int	     interval_burst;
extern  int          remote_interval_usecs;
extern  int          remote_interval_burst;


#ifdef DIRTY
extern int	rem_dirty_count;
extern int	rem_clean_count;
extern int	loc_dirty_count;
extern int	loc_clean_count;
#endif /* DIRTY */

/* stuff for confidence intervals */

extern int  confidence_level;
extern int  iteration_min;
extern int  iteration_max;
extern int  result_confidence_only;
extern double interval;
extern double interval_pct;

extern int cpu_binding_requested;

/* stuff to control the bufferspace "width" */
extern int	send_width;
extern int      recv_width;

/* control the socket priority */
extern int local_socket_prio;
extern int remote_socket_prio;

extern int local_socket_tos;
extern int remote_socket_tos;

/* address family */
extern int	af;

/* different options for other things */
extern int
  local_cpu_usage,
  remote_cpu_usage;

extern float
  local_cpu_rate,
  remote_cpu_rate;

extern int
  shell_num_cpus;

extern	char
  test_name[BUFSIZ];

extern char
  local_fill_file[BUFSIZ],
  remote_fill_file[32];

extern char *
  result_brand;

extern int
  no_control;

#ifdef WANT_DLPI

extern int
  loc_ppa,
  rem_ppa;

extern int
  dlpi_sap;

#endif /* WANT_DLPI */

#endif

extern int parse_ipqos(const char *cp);
extern const char * iptos2str(int iptos);

