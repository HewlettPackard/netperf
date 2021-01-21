#include "netperf_version.h"

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

char	netsh_id[]="\
@(#)netsh.c (c) Copyright 1993-2012 Hewlett-Packard Company. Version 2.6.0";


/****************************************************************/
/*								*/
/*	Global include files					*/
/*								*/
/****************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <fcntl.h>

#ifndef WIN32
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <time.h>
#include <winsock2.h>
#include "missing\stdint.h"
/* while it is unlikely that anyone running Windows 2000 or NT 4 is
   going to be trying to compile this, if they are they will want to
   define DONT_IPV6 in the sources file */
#ifndef DONT_IPV6
#include <ws2tcpip.h>
#endif
#define netperf_socklen_t socklen_t
extern	int	getopt(int , char **, char *) ;
#endif


#ifndef STRINGS
#include <string.h>
#else /* STRINGS */
#include <strings.h>
#endif /* STRINGS */

/**********************************************************************/
/*                                                                    */
/*          Local Include Files                                       */
/*                                                                    */
/**********************************************************************/

#define  NETSH
#include "netsh.h"
#include "netlib.h"
#include "nettest_bsd.h"

#ifdef WANT_UNIX
#include "nettest_unix.h"
#endif /* WANT_UNIX */

#ifdef WANT_XTI
#include "nettest_xti.h"
#endif /* WANT_XTI */

#ifdef WANT_DLPI
#include "nettest_dlpi.h"
#endif /* WANT_DLPI */

#ifdef WANT_SCTP
#include "nettest_sctp.h"
#endif

/************************************************************************/
/*									*/
/*	Global constants  and macros					*/
/*									*/
/************************************************************************/

/* Some of the args take optional parameters. Since we are using
   getopt to parse the command line, we will tell getopt that they do
   not take parms, and then look for them ourselves */

#define GLOBAL_CMD_LINE_ARGS "A:a:b:B:CcdD:f:F:H:hi:I:jk:K:l:L:n:NO:o:P:p:rSs:t:T:v:VW:w:y:Y:Z:46"

/************************************************************************/
/*									*/
/*	Global variables 						*/
/*									*/
/************************************************************************/

/* some names and such */
char *program;		/* program invocation name */
char *command_line;     /* a copy of the entire command line */

/* stuff to say where this test is going */
char
  host_name[HOSTNAMESIZE] = "",	      /* remote host name or ip addr */
  local_host_name[HOSTNAMESIZE] = "", /* local hostname or ip */
  test_name[BUFSIZ] = "TCP_STREAM",   /* which test to run */
  test_port[PORTBUFSIZE] = "12865",   /* where is the test waiting */
  local_test_port[PORTBUFSIZE] = "0"; /* from whence we should start */

int
  address_family = AF_UNSPEC,       /* which address family remote */
  local_address_family = AF_UNSPEC; /* which address family local */

/* the source of data for filling the buffers */
char    local_fill_file[BUFSIZ] = "";
char    remote_fill_file[32] = ""; /* size limited for control message */

/* output controlling variables */
int
  debug = 0,			/* debugging level */
  print_headers = 1,		/* do/don't display headers */
  verbosity = 1,		/* verbosity level */
  keep_histogram = 0,
  keep_statistics = 0;

/* When specified with -B, this will be displayed at the end of the line
   for output that does not include the test header.  mostly this is
   to help identify a specific netperf result when concurrent netperfs
   are run. raj 2006-02-01 */
char *result_brand = NULL;

/* cpu variables */
int
  local_cpu_usage = 0,	/* you guessed it */
  remote_cpu_usage = 0;	/* still right ! */

float
  local_cpu_rate = 0.0F,
  remote_cpu_rate = 0.0F;

int
  shell_num_cpus=1;

/* the end-test conditions for the tests - either transactions, bytes,
   or time. different vars used for clarity - space is cheap ;-) */

int
  test_time = 10, /* test ends by time */
  test_len_ticks, /* how many times will the timer go off before the
		     test is over? */
  test_bytes = 0, /* test ends on byte count */
  test_trans = 0; /* test ends on tran count */

/* the alignment conditions for the tests */
int
  local_recv_align = 8,	/* alignment for local receives */
  local_send_align = 8,	/* alignment for local sends */
  local_send_offset = 0,
  local_recv_offset = 0,
  remote_recv_align = 8, /* alignment for remote receives */
  remote_send_align = 8, /* alignment for remote sends */
  remote_send_offset = 0,
  remote_recv_offset = 0,
  remote_send_width = 0,
  remote_recv_width = 0;

/* hoist above the if for omni */
int
  interval_usecs = 0,
  interval_wate = 0,
  interval_burst = 0,
  remote_interval_usecs = 0,
  remote_interval_burst = 0;

/* wait time between control/data connection establishment and start
   of data traffic */
int wait_time_secs = 0;


#ifdef DIRTY
int loc_dirty_count = 0,
    loc_clean_count = 0,
    rem_dirty_count = 0,
    rem_clean_count = 0;
#else
int loc_dirty_count = -1,
    loc_clean_count = -1,
    rem_dirty_count = -1,
    rem_clean_count = -1;
#endif /* DIRTY */

 /* some of the vairables for confidence intervals... */

int  confidence_level = 0;
int  iteration_min = 1;
int  iteration_max = 1;
int  result_confidence_only = 0;
double interval = 0.0;

 /* stuff to control the "width" of the buffer rings for sending and
    receiving data */
int	send_width;
int     recv_width;

/* address family */
int	af = AF_INET;

/* socket priority via SO_PRIORITY */
int local_socket_prio = -1;
int remote_socket_prio = -1;

/* and IP_TOS */
int local_socket_tos = -1;
int remote_socket_tos = -1;

/* did someone request processor affinity? */
int cpu_binding_requested = 0;

/* are we not establishing a control connection? */
int no_control = 0;

/* what is the passphrase? */
char *passphrase = NULL;

char netserver_usage[] = "\n\
Usage: netserver [options] \n\
\n\
Options:\n\
    -h                Display this text\n\
    -D                Do not daemonize\n\
    -d                Increase debugging output\n\
    -f                Do not spawn chilren for each test, run serially\n\
    -L name,family    Use name to pick listen address and family for family\n\
    -N                No debugging output, even if netperf asks\n\
    -p portnum        Listen for connect requests on portnum.\n\
    -4                Do IPv4\n\
    -6                Do IPv6\n\
    -v verbosity      Specify the verbosity level\n\
    -V                Display version information and exit\n\
    -Z passphrase     Expect passphrase as the first thing received\n\
\n";

/* netperf_usage done as two concatenated strings to make the MS
   compiler happy when compiling for x86_32.  fix from Spencer
   Frink. */

char netperf_usage1[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
Global options:\n\
    -a send,recv      Set the local send,recv buffer alignment\n\
    -A send,recv      Set the remote send,recv buffer alignment\n\
    -B brandstr       Specify a string to be emitted with brief output\n\
    -c [cpu_rate]     Report local CPU usage\n\
    -C [cpu_rate]     Report remote CPU usage\n\
    -d                Increase debugging output\n\
    -D time,[units] * Display interim results at least every time interval\n\
                      using units as the initial guess for units per second\n\
                      A negative value for time will make heavy use of the\n\
                      system's timestamping functionality\n\
    -f G|M|K|g|m|k    Set the output units\n\
    -F lfill[,rfill]* Pre-fill buffers with data from specified file\n\
    -h                Display this text\n\
    -H name|ip,fam *  Specify the target machine and/or local ip and family\n\
    -i max,min        Specify the max and min number of iterations (15,1)\n\
    -I lvl[,intvl]    Specify confidence level (95 or 99) (99) \n\
                      and confidence interval in percentage (10)\n\
    -j                Keep additional timing statistics\n\
    -l testlen        Specify test duration (>0 secs) (<0 bytes|trans)\n\
    -L name|ip,fam *  Specify the local ip|name and address family\n\
    -o send,recv      Set the local send,recv buffer offsets\n\
    -O send,recv      Set the remote send,recv buffer offset\n\
    -n numcpu         Set the number of processors for CPU util\n\
    -N                Establish no control connection, do 'send' side only\n\
    -p port,lport*    Specify netserver port number and/or local port\n\
    -P 0|1            Don't/Do display test headers\n\
    -r                Allow confidence to be hit on result only\n\
    -s seconds        Wait seconds between test setup and test start\n\
    -S                Set SO_KEEPALIVE on the data connection\n\
    -t testname       Specify test to perform\n\
    -T lcpu,rcpu      Request netperf/netserver be bound to local/remote cpu\n\
    -v verbosity      Specify the verbosity level\n\
    -W send,recv      Set the number of send,recv buffers\n\
    -v level          Set the verbosity level (default 1, min 0)\n\
    -V                Display the netperf version and exit\n\
    -y local,remote   Set the socket priority\n\
    -Y local,remote   Set the IP_TOS. Use hexadecimal.\n\
    -Z passphrase     Set and pass to netserver a passphrase\n";

char netperf_usage2[] = "\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n\
\n"
"* For these options taking two parms, specifying one value with no comma\n\
will only set the first parms and will leave the second at the default\n\
value. To set the second value it must be preceded with a comma or be a\n\
comma-separated pair. This is to retain previous netperf behaviour.\n";


/* This routine will return the two arguments to the calling routine.
   If the second argument is not specified, and there is no comma,
   then the value of the second argument will be the same as the value
   of the first. If there is a comma, then the value of the second
   argument will be the value of the second argument ;-) */
void
break_args(char *s, char *arg1, char *arg2)

{
  char *ns;
  ns = strchr(s,',');
  if (ns) {
    /* there was a comma arg2 should be the second arg*/
    *ns++ = '\0';
    while ((*arg2++ = *ns++) != '\0');
  }
  else {
    /* there was not a comma, we can use ns as a temp s */
    /* and arg2 should be the same value as arg1 */
    ns = s;
    while ((*arg2++ = *ns++) != '\0');
  };
  while ((*arg1++ = *s++) != '\0');
}

/* break_args_explicit_sep

   this routine is somewhat like break_args in that it will separate a
   pair of values using the given separator.  however, if there is no
   separator this version will not ass-u-me that arg2 should be the
   same as arg1. raj 20101129 */

void
break_args_explicit_sep(char *s, int sep, char *arg1, char *arg2)

{
  char *ns;
  ns = strchr(s,sep);
  if (ns) {
    /* there was a separator arg2 should be the second arg*/
    *ns++ = '\0';
    while ((*arg2++ = *ns++) != '\0');
  }
  else {
    /* there was no separator, so we should make sure that arg2 is \0
       lest something become confused. raj 2005-02-04 */
    *arg2 = '\0';
  };
  while ((*arg1++ = *s++) != '\0');

}

/* break_args_explicit - now just a wrapper around a call to
   break_args_explicit_sep passing-in a ',' as the separator. raj
   20101129 */

void
break_args_explicit(char *s, char *arg1, char *arg2)

{
  break_args_explicit_sep(s, ',', arg1, arg2);
}


/* given a string with possible values for setting an address family,
   convert that into one of the AF_mumble values - AF_INET, AF_INET6,
   AF_UNSPEC as apropriate. the family_string is compared in a
   case-insensitive manner */

int
parse_address_family(char family_string[])
{

  char temp[10];  /* gotta love magic constants :) */

  strncpy(temp,family_string,10);

  if (debug) {
    fprintf(where,
	    "Attempting to parse address family from %s derived from %s\n",
	    temp,
	    family_string);
  }
#if defined(AF_INET6)
  if (strstr(temp,"6")) {
    return(AF_INET6);
  }
#endif
  if (strstr(temp,"inet") ||
      strstr(temp,"4")) {
    return(AF_INET);
  }
#if defined(AF_RDS)
  if (strstr(temp,"af_rds") ||
      strstr(temp,"32")) {
    return(AF_RDS);
  }
#endif
  if (strstr(temp,"unspec") ||
      strstr(temp,"0")) {
    return(AF_UNSPEC);
  }
  fprintf(where,
	  "WARNING! %s not recognized as an address family, using AF_UNPSEC\n"
	  "Are you sure netperf was configured for that address family?\n",
	  family_string);
  fflush(where);
  return(AF_UNSPEC);
}

int
parse_socket_type(char socket_string[]) {

  char temp[10];

  strncpy(temp,socket_string,10);

  if (debug) {
    fprintf(where,
	    "Attempting to parse socket type from %s derived from %s\n",
	    temp,
	    socket_string);
  }

#ifdef SOCK_STREAM
  if (strstr(temp,"stream"))
    return SOCK_STREAM;
#endif
#ifdef SOCK_DGRAM
  if (strstr(temp,"dgram"))
    return SOCK_DGRAM;
#endif
  return NST_UNKN;

}

int
parse_direction(char direction_string[])
{
  char arg1[BUFSIZ],arg2[BUFSIZ];
  int left, right;

  if (NULL == direction_string) {
    return 0;
  }

  if (direction_string[0] == '\0') {
    return 0;
  }

  /* allow someone to "or" break_args_explicit will split at the first
     '|' in the string so we know that arg1 has no '|' in it and arg2
     might */
  break_args_explicit_sep(direction_string,'|',arg1,arg2);

  /* at this point only arg2 could contain a '|' so recurse on that */
  right = parse_direction(arg2);

  /* now we parse the "left side" or arg1 */
  if (arg1[0] == '\0') {
    left = 0;
  }
  else if ((strcasecmp(arg1,"xmit") == 0) ||
	   (strcasecmp(arg1,"send") == 0) ||
	   (strcasecmp(arg1,"stream") == 0) ||
	   (strcasecmp(arg1,"transmit") == 0)) {
    left = NETPERF_XMIT;
  }
  else if ((strcasecmp(arg1,"recv") == 0) ||
	   (strcasecmp(arg1,"receive") == 0) ||
	   (strcasecmp(arg1,"maerts") == 0)) {
    /* yes, another magic number... */
    left =  NETPERF_RECV;
  }
  else  if (strcasecmp(arg1,"rr") == 0) {
    left = NETPERF_XMIT|NETPERF_RECV;
  }
  else {
    /* we now "ass-u-me" it is a number that can be parsed by strtol()
 */
    left = strtol(arg1,NULL,0);
  }

  return (left | right);

}

int
parse_protocol(char protocol_string[])
{
  char temp[10];

  strncpy(temp,protocol_string,10);

  if (debug) {
    fprintf(where,
	    "Attempting to parse protocol from %s derived from %s\n",
	    temp,
	    protocol_string);
  }

  /* we ass-u-me that everyone has IPPROTO_TCP elsewhere so might as
     well here, and avoid issues with windows using an enum.  Kudos to
     Jonathan Cook. */
  if (!strcasecmp(temp,"tcp")){
    socket_type = SOCK_STREAM;
    return IPPROTO_TCP;
  }

  if (!strcasecmp(temp,"udp")) {
    socket_type = SOCK_DGRAM;
    return IPPROTO_UDP;
  }

  /* we keep the rest of the #idefs though because these may not be as
     universal as TCP and UDP... */
#ifdef IPPROTO_SCTP
  if (!strcasecmp(temp,"sctp")) {
    /* it can be more than one socket type */
    return IPPROTO_SCTP;
  }
#endif
#ifdef IPPROTO_SDP
  if (!strcasecmp(temp,"sdp")) {
    socket_type = SOCK_STREAM;
    return IPPROTO_SDP;
  }
#endif
#if defined(IPPROTO_DCCP) && defined(SOCK_DCCP)
  if (!strcasecmp(temp,"dccp")) {
    socket_type = SOCK_DCCP;
    return IPPROTO_DCCP;
  }
#endif
#ifdef IPPROTO_UDPLITE
  if (!strcasecmp(temp,"udplite")) {
    socket_type = SOCK_DGRAM;
    return IPPROTO_UDPLITE;
  }
#endif
  return IPPROTO_IP;
}


void
print_netserver_usage()
{
  fprintf(stderr, "%s", netserver_usage);
}


void
print_netperf_usage()
{
  fprintf(stderr, "%s%s", netperf_usage1, netperf_usage2);
}

/* convert the specified string to upper case if we know how */
static void
convert_to_upper(char *source)
{
#if defined(HAVE_TOUPPER)
  int i,length;

  length = strlen(source);

  for (i=0; i < length; i++) {
    source[i] = toupper(source[i]);
  }
#endif
  return;

}

void
scan_cmd_line(int argc, char *argv[])
{
  extern int	optind;           /* index of first unused arg 	*/
  extern char	*optarg;	  /* pointer to option string	*/

  int           cmnd_len;
  char          *p;

  int		c;

  char	arg1[BUFSIZ],  /* argument holders		*/
    arg2[BUFSIZ];

  program = (char *)malloc(strlen(argv[0]) + 1);
  if (program == NULL) {
    printf("malloc() to store program name failed!\n");
    exit(-1);
  }
  strcpy(program, argv[0]);

  /* brute force, but effective */
  command_line = NULL;
  cmnd_len = 0;
  for (c = 0; c < argc; c++) {
    cmnd_len += strlen(argv[c]);
  }
  cmnd_len += argc;  /* forget thee not the spaces */
  command_line = malloc(cmnd_len+1);

  if (command_line == NULL) {
    printf("malloc(%d) failed!\n",cmnd_len);
    exit(-1);
  }
  p = command_line;
  for (c = 0; c < argc; c++) {
    memcpy(p,argv[c],strlen(argv[c]));
    p += strlen(argv[c]);
    *p = ' ';
    p += 1;
  }
  *--p = 0;

  /* Go through all the command line arguments and break them out. For
     those options that take two parms, specifying only the first will
     set both to that value. Specifying only the second will leave the
     first untouched. To change only the first, use the form first,
     (see the routine break_args.. */

  while ((c= getopt(argc, argv, GLOBAL_CMD_LINE_ARGS)) != EOF) {
    switch (c) {
    case '?':
    case 'h':
      print_netperf_usage();
      exit(1);
    case 'a':
      /* set local alignments */
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	local_send_align = convert(arg1);
      }
      if (arg2[0])
	local_recv_align = convert(arg2);
      break;
    case 'A':
      /* set remote alignments */
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	remote_send_align = convert(arg1);
      }
      if (arg2[0])
	remote_recv_align = convert(arg2);
      break;
    case 'c':
      /* measure local cpu usage please. the user may have specified
         the cpu rate as an optional parm */
      if (argv[optind] && isdigit((unsigned char)argv[optind][0])){
	/* there was an optional parm */
	local_cpu_rate = (float)atof(argv[optind]);
	optind++;
      }
      local_cpu_usage++;
      break;
    case 'C':
      /* measure remote cpu usage please */
      if (argv[optind] && isdigit((unsigned char)argv[optind][0])){
	/* there was an optional parm */
	remote_cpu_rate = (float)atof(argv[optind]);
	optind++;
      }
      remote_cpu_usage++;
      break;
    case 'd':
      debug++;
      break;
    case 'D':
#if (defined WANT_DEMO)
      demo_mode = 1; /* 1 == use units; 2 == always timestamp */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	demo_interval = atof(arg1) * 1000000.0;
	if (demo_interval < 0.0) {
	  demo_interval = demo_interval * -1.0;
	  demo_mode = 2;
	}
      }
      if (arg2[0]) {
	demo_units = convert(arg2);
      }
#else
      printf("Sorry, Demo Mode not configured into this netperf.\n"
	     "Please consider reconfiguring netperf with\n"
	     "--enable-demo=yes and recompiling\n");
#endif
      break;
    case 'f':
      /* set the thruput formatting */
      libfmt = *optarg;
      break;
    case 'F':
      /* set the fill_file variables for pre-filling buffers. check
	 the remote fill file name length against our limit as we will
	 not hear from the remote on errors opening the fill
	 file. Props to Jonathan Cook for the remote name check */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	strncpy(local_fill_file,arg1,sizeof(local_fill_file));
	local_fill_file[sizeof(local_fill_file) - 1] = '\0';
      }
      if (arg2[0]) {
	if (strlen(arg2)>(sizeof(remote_fill_file) - 1)){
	  fprintf(stderr,
		  "Remote fill file name must be less than %d characters\n",
		  (int) sizeof(remote_fill_file));
	  fflush(stderr);
	  exit(-1);
	}

	strncpy(remote_fill_file,arg2,sizeof(remote_fill_file));
	remote_fill_file[sizeof(remote_fill_file) - 1] = '\0';
      }
      break;
    case 'i':
      /* set the iterations min and max for confidence intervals */
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	iteration_max = convert(arg1);
      }
      if (arg2[0] ) {
	iteration_min = convert(arg2);
      }
      /* if the iteration_max is < iteration_min make iteration_max
	 equal iteration_min */
      if (iteration_max < iteration_min) iteration_max = iteration_min;
      /* limit minimum to 3 iterations */
      if (iteration_max < 3) iteration_max = 3;
      if (iteration_min < 3) iteration_min = 3;
      /* limit maximum to 30 iterations */
      if (iteration_max > 30) iteration_max = 30;
      if (iteration_min > 30) iteration_min = 30;
      if (confidence_level == 0) confidence_level = 99;
      if (interval == 0.0) interval = 0.05; /* five percent */
      break;
    case 'I':
      /* set the confidence level (95 or 99) and width */
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	confidence_level = convert(arg1);
      }
      if((confidence_level != 95) && (confidence_level != 99)){
	printf("Only 95%% and 99%% confidence level is supported\n");
	exit(-1);
      }
      if (arg2[0] ) {
	/* it doesn't make much sense to use convert() here so just
	   use strtod() instead. raj 2007-10-24 */
	interval = strtod(arg2,NULL)/100.0;
      }
      /* make sure that iteration_min and iteration_max are at least
	 at a reasonable default value.  if a -i option has previously
	 been parsed, these will no longer be 1, so we can check
	 against 1 */
      if (iteration_min == 1) iteration_min = 3;
      if (iteration_max == 1) iteration_max = 10;
      /* make sure that the interval is set if it isn't at its default
	 value */
      if (interval == 0.0) interval = 0.05; /* five percent */
      break;
    case 'j':
      keep_histogram = 1;
      keep_statistics = 1;
      break;
    case 'k':
      /* local dirty and clean counts */
#ifdef DIRTY
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	loc_dirty_count = convert(arg1);
      }
      if (arg2[0] ) {
	loc_clean_count = convert(arg2);
      }
#else
      printf("I don't know how to get dirty.\n");
#endif /* DIRTY */
      break;
    case 'K':
      /* remote dirty and clean counts */
#ifdef DIRTY
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	rem_dirty_count = convert(arg1);
      }
      if (arg2[0] ) {
	rem_clean_count = convert(arg2);
      }
#else
      printf("I don't know how to get dirty.\n");
#endif /* DIRTY */
      break;
    case 'n':
      shell_num_cpus = atoi(optarg);
      break;
    case 'N':
      no_control = 1;
      break;
    case 'o':
      /* set the local offsets */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	local_send_offset = convert(arg1);
      if (arg2[0])
	local_recv_offset = convert(arg2);
      break;
    case 'O':
      /* set the remote offsets */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	remote_send_offset = convert(arg1);
      if (arg2[0])
	remote_recv_offset = convert(arg2);
      break;
    case 'P':
      /* to print or not to print, that is */
      /* the header question */
      print_headers = convert(optarg);
      break;
    case 'r':
      /* the user wishes that we declare confidence when hit on the
	 result even if not yet reached on CPU utilization.  only
	 meaningful if cpu util is enabled */
      result_confidence_only = 1;
      break;
    case 'S':
      /* the user wishes us to set SO_KEEPALIVE */
      want_keepalive = 1;
      break;
    case 's':
      /* the user wishes us to sleep/pause some length of time before
	 actually starting the test */
      wait_time_secs = convert(optarg);
      break;
    case 't':
      /* set the test name and shift it to upper case so we don't have
	 to worry about compares on things like substrings */
      strncpy(test_name,optarg,sizeof(test_name)-1);
      convert_to_upper(test_name);
      break;
    case 'T':
      /* We want to set the processor on which netserver or netperf
         will run */
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	local_proc_affinity = convert(arg1);
	bind_to_specific_processor(local_proc_affinity,0);
      }
      if (arg2[0]) {
	remote_proc_affinity = convert(arg2);
      }
      cpu_binding_requested = 1;
      break;
    case 'W':
      /* set the "width" of the user space data buffer ring. This will
	 be the number of send_size buffers malloc'd in the tests */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	send_width = convert(arg1);
      if (arg2[0])
	recv_width = convert(arg2);
      break;
    case 'y':
      break_args(optarg, arg1, arg2);
#if defined(SO_PRIORITY)
      if (arg1[0])
	local_socket_prio = convert(arg1);
#else
      if (debug) {
	fprintf(where,
		"Setting SO_PRIORITY is not supported on this platform, request to set SO_PRIORITY locally ignored.\n");
	fflush(where);
      }
      local_socket_prio = -3;
#endif
      if (arg2[0])
	remote_socket_prio = convert(arg2);
      break;
    case 'Y':
      break_args(optarg, arg1, arg2);
#if defined(IP_TOS) || defined(IPV6_TCLASS)
      if (arg1[0])
	local_socket_tos = parse_ipqos(arg1);
#else
      if (debug) {
	fprintf(where,
		"Setting IP type-of-service is not supported on this platform, request to set it locally ignored.\n");
	fflush(where);
      }
      local_socket_tos = -1;
#endif
      if (arg2[0]) {
	remote_socket_tos = parse_ipqos(arg2);
	if (debug) {
	  fprintf(where,
		  "Setting remote_socket_tos to 0x%x\n",
		  remote_socket_tos);
	  fflush(where);
	}
      }
      break;
    case 'Z':
      /* only copy as much of the passphrase as could fit in the
	 test-specific portion of a control message. Windows does not
	 seem to have a strndup() so just malloc and strncpy it.  we
	 weren't checking the strndup() return so won't bother with
	 checking malloc(). we will though make certain we only
	 allocated it once in the event that someone puts -Z on the
	 command line more than once */
      if (passphrase == NULL)
	passphrase = malloc(sizeof(netperf_request.content.test_specific_data));
      strncpy(passphrase,
	      optarg,
	      sizeof(netperf_request.content.test_specific_data));
      passphrase[sizeof(netperf_request.content.test_specific_data) - 1] = '\0';
      break;
    case 'l':
      /* determine test end conditions */
      /* assume a timed test */
      test_time = convert(optarg);
      test_bytes = test_trans = 0;
      if (test_time < 0) {
	test_bytes = -1 * test_time;
	test_trans = test_bytes;
	test_time = 0;
      }
      break;
    case 'v':
      /* say how much to say */
      verbosity = convert(optarg);
      break;
    case 'p':
      /* specify an alternate port number we use break_args_explicit
	 here to maintain backwards compatibility with previous
	 generations of netperf where having a single value did not
	 set both remote _and_ local port number. raj 2005-02-04 */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0])
	strncpy(test_port,arg1,PORTBUFSIZE);
      if (arg2[0])
	strncpy(local_test_port,arg2,PORTBUFSIZE);
      break;
    case 'H':
      /* save-off the host identifying information, use
	 break_args_explicit since passing just one value should not
	 set both */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0])
	strncpy(host_name,arg1,sizeof(host_name));
      if (arg2[0])
	address_family = parse_address_family(arg2);
      break;
    case 'L':
      /* save-off the local control socket addressing information. use
	 break_args_explicit since passing just one value should not
	 set both */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0])
	strncpy(local_host_name,arg1,sizeof(local_host_name));
      if (arg2[0])
	local_address_family = parse_address_family(arg2);
      break;
    case 'w':
      /* We want to send requests at a certain wate.  Remember that
         there are 1000000 usecs in a second, and that the packet rate
         is expressed in packets per millisecond. shuffle the #ifdef
         around a bit to deal with both netperf and netserver possibly
         doing intervals with omni tests */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
#ifdef WANT_INTERVALS
	interval_usecs = convert_timespec(arg1);
	interval_wate  = interval_usecs / 1000;
#else
	fprintf(where,
		"Packet rate control is not compiled in.\n");
#endif
      }
      if (arg2[0]) {
	/* we pass usecs to the remote and let it deal to cover both
	   intervals and spin methods. if he wasn't intervals enabled
	   he will return a suitable value back to us */
	remote_interval_usecs = convert_timespec(arg2);
      }
      break;
    case 'b':
      /* we want to have a burst so many packets per */
      /* interval. */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
#ifdef WANT_INTERVALS
	interval_burst = convert(arg1);
	/* set a default in case the user does not include the -w
	   option */
	if (interval_usecs == 0) {
	  interval_wate = 1;
	  interval_usecs = 1000;
	}
#else
	fprintf(where,
		"Packet burst size is not compiled in. \n");
#endif /* WANT_INTERVALS */
      }
      /* there is no ifdef here because we are sending this value to
	 the remote, which may or may not have been compiled for
	 intervals and we don't have a way of knowing on this side
	 until we try */
      if (arg2[0]) {
	remote_interval_burst = convert(arg2);
	if (remote_interval_usecs == 0) {
	  remote_interval_usecs = 1000;
	}
      }
      break;
    case 'B':
      result_brand = malloc(strlen(optarg)+1);
      if (NULL != result_brand) {
	strcpy(result_brand,optarg);
      }
      else {
	fprintf(where,
		"Unable to malloc space for result brand\n");
      }
      break;
    case '4':
      address_family = AF_INET;
      local_address_family = AF_INET;
      break;
    case '6':
#if defined(AF_INET6)
      address_family = AF_INET6;
      local_address_family = AF_INET6;
#else
      printf("This netperf was not compiled on an IPv6 capable system!\n");
      exit(-1);
#endif
      break;
    case 'V':
      printf("Netperf version %s\n",NETPERF_VERSION);
      exit(0);
      break;
    };
  }
  /* ok, what should our default hostname and local binding info be?
   */

  if ('\0' == host_name[0]) {
    /* host_name was not set */
    switch (address_family) {
    case AF_INET:
#if defined(AF_RDS)
    case AF_RDS:
#endif
      strcpy(host_name,"localhost");
      break;
    case AF_UNSPEC:
      /* what to do here? case it off the local_address_family I
	 suppose */
      switch (local_address_family) {
      case AF_INET:
      case AF_UNSPEC:
#if defined(AF_RDS)
      case AF_RDS:
#endif
	strcpy(host_name,"localhost");
	break;
#if defined(AF_INET6)
      case AF_INET6:
	strcpy(host_name,"::1");
	break;
#endif
      default:
	printf("Netperf does not understand %d as an address family\n",
	       address_family);
	exit(-1);
      }
      break;
#if defined(AF_INET6)
    case AF_INET6:
      strcpy(host_name,"::1");
      break;
#endif
    default:
      printf("Netperf does not understand %d as an address family\n",
	     address_family);
      exit(-1);
    }
  } else {
    /* resolve the hostname and pull the address family from the
       addrinfo */
    struct addrinfo *ai;

    ai = resolve_host(host_name, NULL, address_family);
    if (!ai) {
      printf("Netperf could not resolve %s as a host name\n", host_name);
      exit(-1);
    }
    address_family = ai->ai_family;
    freeaddrinfo(ai);
  }


  /* now, having established the name to which the control will
     connect, from what should it come? */
  if ('\0' == local_host_name[0]) {
    switch (local_address_family) {
    case AF_INET:
#if defined(AF_RDS)
    case AF_RDS:
#endif
      strcpy(local_host_name,"0.0.0.0");
      break;
    case AF_UNSPEC:
      switch (address_family) {
      case AF_INET:
      case AF_UNSPEC:
#if defined(AF_RDS)
    case AF_RDS:
#endif
	strcpy(local_host_name,"0.0.0.0");
	break;
#if defined(AF_INET6)
      case AF_INET6:
	strcpy(local_host_name,"::0");
	break;
#endif
      default:
	printf("Netperf does not understand %d as an address family\n",
	       address_family);
	exit(-1);
      }
      break;
#if defined(AF_INET6)
    case AF_INET6:
      strcpy(local_host_name,"::0");
      break;
#endif
    default:
      printf("Netperf does not understand %d as an address family\n",
	     address_family);
      exit(-1);
    }
  }

  /* so, if we aren't even going to establish a control connection we
     should set certain "remote" settings to reflect this, regardless
     of what else may have been set on the command line */
  if (no_control) {
    remote_socket_prio = -1;
    remote_socket_tos = -1;
    remote_recv_align = -1;
    remote_send_align = -1;
    remote_send_offset = -1;
    remote_recv_offset = -1;
    remote_cpu_rate = (float)-1.0;
    remote_cpu_usage = 0;
  }

  /* parsing test-specific options used to be conditional on there
     being a "--" in the option stream.  however, some of the tests
     have other initialization happening in their "scan" routines so
     we want to call them regardless. raj 2005-02-08 */
  /* while the parsing of the command line will upshift the test name,
     since we don't know that there will always be a way to do so? we
     will retain for now the strcasecmp calls rather than switch to
     strcmp. raj 20101220 */
    if (
#ifndef WANT_MIGRATION
	(strcasecmp(test_name,"TCP_STREAM") == 0) ||
	(strcasecmp(test_name,"TCP_MAERTS") == 0) ||
	(strcasecmp(test_name,"TCP_RR") == 0) ||
	(strcasecmp(test_name,"TCP_CRR") == 0) ||
	(strcasecmp(test_name,"UDP_STREAM") == 0) ||
	(strcasecmp(test_name,"UDP_RR") == 0) ||
#endif
#ifdef HAVE_ICSC_EXS
	(strcasecmp(test_name,"EXS_TCP_STREAM") == 0) ||
#endif /* HAVE_ICSC_EXS */
#ifdef HAVE_SENDFILE
	(strcasecmp(test_name,"TCP_SENDFILE") == 0) ||
#endif /* HAVE_SENDFILE */
	(strcasecmp(test_name,"TCP_CC") == 0) ||
	(strcasecmp(test_name,"TCP_MSS") == 0) ||
#ifdef DO_1644
	(strcasecmp(test_name,"TCP_TRR") == 0) ||
#endif /* DO_1644 */
#ifdef DO_NBRR
	(strcasecmp(test_name,"TCP_TRR") == 0) ||
#endif /* DO_NBRR */
	(0))
      {
	scan_sockets_args(argc, argv);
      }

#ifdef WANT_DLPI
    else if ((strcasecmp(test_name,"DLCO_RR") == 0) ||
	     (strcasecmp(test_name,"DLCL_RR") == 0) ||
	     (strcasecmp(test_name,"DLCO_STREAM") == 0) ||
	     (strcasecmp(test_name,"DLCL_STREAM") == 0))
      {
	scan_dlpi_args(argc, argv);
      }
#endif /* WANT_DLPI */

#ifdef WANT_UNIX
    else if ((strcasecmp(test_name,"STREAM_RR") == 0) ||
	     (strcasecmp(test_name,"DG_RR") == 0) ||
	     (strcasecmp(test_name,"STREAM_STREAM") == 0) ||
	     (strcasecmp(test_name,"DG_STREAM") == 0))
      {
	scan_unix_args(argc, argv);
      }
#endif /* WANT_UNIX */

#ifdef WANT_XTI
    else if ((strcasecmp(test_name,"XTI_TCP_RR") == 0) ||
	     (strcasecmp(test_name,"XTI_TCP_STREAM") == 0) ||
	     (strcasecmp(test_name,"XTI_UDP_RR") == 0) ||
	     (strcasecmp(test_name,"XTI_UDP_STREAM") == 0))
      {
	scan_xti_args(argc, argv);
      }
#endif /* WANT_XTI */

#ifdef WANT_SCTP
    else if ((strcasecmp(test_name,"SCTP_STREAM") == 0) ||
	     (strcasecmp(test_name,"SCTP_RR") == 0) ||
	     (strcasecmp(test_name,"SCTP_STREAM_MANY") == 0) ||
	     (strcasecmp(test_name,"SCTP_RR_MANY") == 0))
    {
      scan_sctp_args(argc, argv);
    }
#endif

#ifdef WANT_SDP
    else if((strcasecmp(test_name,"SDP_STREAM") == 0) ||
	    (strcasecmp(test_name,"SDP_MAERTS") == 0) ||
	    (strcasecmp(test_name,"SDP_RR") == 0))
      {
	scan_sdp_args(argc, argv);
      }
#endif

#ifdef WANT_OMNI
    else if ((strcasecmp(test_name,"OMNI") == 0) ||
#ifdef WANT_MIGRATION
	     (strcasecmp(test_name,"TCP_STREAM") == 0) ||
	     (strcasecmp(test_name,"TCP_MAERTS") == 0) ||
	     (strcasecmp(test_name,"TCP_RR") == 0) ||
	     (strcasecmp(test_name,"TCP_CRR") == 0) ||
	     (strcasecmp(test_name,"UDP_STREAM") == 0) ||
	     (strcasecmp(test_name,"UDP_RR") == 0) ||
#endif
	     (strcasecmp(test_name,"UUID") == 0)) {
      scan_omni_args(argc, argv);
    }
#endif

    /* what is our default value for the output units?  if the test
       name contains "RR" or "rr" or "Rr" or "rR" then the default is
       'x' for transactions. otherwise it is 'm' for megabits (10^6)
       however...  if this is an "omni" test then we want to defer
       this decision to scan_omni_args */

    if (strcasecmp(test_name,"omni")) {
      if ('?' == libfmt) {
	/* we use a series of strstr's here because not everyone has
	   strcasestr and I don't feel like up or downshifting text */
	if ((strstr(test_name,"RR")) ||
	    (strstr(test_name,"rr")) ||
	    (strstr(test_name,"Rr")) ||
	    (strstr(test_name,"rR"))) {
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
	if ((strstr(test_name,"RR") == NULL) &&
	    (strstr(test_name,"rr") == NULL) &&
	    (strstr(test_name,"Rr") == NULL) &&
	    (strstr(test_name,"rR") == NULL)) {
	  libfmt = 'm';
	}
      }
    }
}


void
dump_globals()
{
  printf("Program name: %s\n", program);
  printf("Local send alignment: %d\n",local_send_align);
  printf("Local recv alignment: %d\n",local_recv_align);
  printf("Remote send alignment: %d\n",remote_send_align);
  printf("Remote recv alignment: %d\n",remote_recv_align);
  printf("Local socket priority: %d\n", local_socket_prio);
  printf("Remote socket priority: %d\n", remote_socket_prio);
  printf("Local socket TOS: %s\n", iptos2str(local_socket_tos));
  printf("Remote socket TOS: %s\n", iptos2str(remote_socket_tos));
  printf("Report local CPU %d\n",local_cpu_usage);
  printf("Report remote CPU %d\n",remote_cpu_usage);
  printf("Verbosity: %d\n",verbosity);
  printf("Debug: %d\n",debug);
  printf("Port: %s\n",test_port);
  printf("Test name: %s\n",test_name);
  printf("Test bytes: %d Test time: %d Test trans: %d\n",
	 test_bytes,
	 test_time,
	 test_trans);
  printf("Host name: %s\n",host_name);
  printf("\n");
}
