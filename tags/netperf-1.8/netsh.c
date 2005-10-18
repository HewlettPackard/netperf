
char	netsh_id[]="@(#)netsh.c (c) Copyright 1993, Hewlett-Packard Company.\
	Version 1.8alpha";


/****************************************************************/
/*								*/
/*	Global include files					*/
/*								*/
/****************************************************************/

#include <sys/types.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#ifndef STRINGS
#include <string.h>
#else /* STRINGS */
#include <strings.h>
#endif /* STRINGS */

double atof();

/**********************************************************************/
/*                                                                    */
/*          Local Include Files                                       */
/*                                                                    */
/**********************************************************************/

#define  NETSH
#include "netsh.h"
#include "netlib.h"

/************************************************************************/
/*									*/
/*	Global constants  and macros					*/
/*									*/
/************************************************************************/

 /* This define is only needed for HP-UX 7.x and earlier... */
#ifdef HP
#define bcopy(s,d,h) memcpy((d),(s),(h))
#define bzero(p,h) memset((p),0,(h))
#endif /* HP */

 /* Some of the args take optional parameters. Since we are using */
 /* getopt to parse the command line, we will tell getopt that they do */
 /* not take parms, and then look for them ourselves */
#define GLOBAL_CMD_LINE_ARGS "A:a:Ccdf:H:hl:O:o:P:p:t:v:"

/************************************************************************/
/*									*/
/*	Extern variables 						*/
/*									*/
/************************************************************************/

extern int errno;
extern char *sys_errlist[ ];
extern int sys_nerr;


/************************************************************************/
/*									*/
/*	Global variables 						*/
/*									*/
/************************************************************************/

/* some names and such                                                  */
char	*program;		/* program invocation name		*/
char	username[BUFSIZ];	/* login name of user			*/
char	cmd_file[BUFSIZ];	/* name of the commands file		*/

/* stuff to say where this test is going                                */
char	host_name[HOSTNAMESIZE];	/* remote host name or ip addr  */
char    test_name[64];			/* which test to run 		*/
short	test_port;			/* where is the test waiting    */

/* output controlling variables                                         */
int
  debug,			/* debugging level */
  print_headers,		/* do/don't display headers */
  verbosity;		/* verbosity level */

/* cpu variables */
int
  local_cpu_usage,	/* you guessed it			*/
  remote_cpu_usage;	/* still right !			*/

float			       
  local_cpu_rate,
  remote_cpu_rate;

/* the end-test conditions for the tests - either transactions, bytes,  */
/* or time. different vars used for clarity - space is cheap ;-)        */
int	
  test_time,		/* test ends by time			*/
  test_bytes,		/* test ends on byte count		*/
  test_trans;		/* test ends on tran count		*/

/* the alignment conditions for the tests				*/
int
  local_recv_align,	/* alignment for local receives		*/
  local_send_align,	/* alignment for local sends		*/
  local_send_offset = 0,
  local_recv_offset = 0,
  remote_recv_align,	/* alignment for remote receives	*/
  remote_send_align,	/* alignment for remote sends		*/
  remote_send_offset = 0,
  remote_recv_offset = 0;

#ifdef INTERVALS
int
  interval_usecs,
  interval_wate;
interval_burst;
#endif /* INTERVALS */

#ifdef DIRTY
int	loc_dirty_count;
int	loc_clean_count;
int	rem_dirty_count;
int	rem_clean_count;
#endif /* DIRTY */

/* stuff to controll the bufferspace "width" */
int	send_width;

char netserver_usage[] = "\n\
Usage: netserver [options] \n\
\n\
Options:\n\
    -h                Display this text\n\
    -p portnum        Listen for connect requests on portnum.\n\
\n";

char netperf_usage[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
Global options:\n\
    -c [cpu_rate]     Report local CPU usage\n\
    -C [cpu_rate]     Report remote CPU usage\n\
    -d                Increase debugging output\n\
    -f G|M|K|g|m|k    Set the output units\n\
    -h                Display this text\n\
    -H name|ip        Specify the target machine\n\
    -l testlen        Specify test duration (>0 secs) (<0 bytes|trans)\n\
    -p port           Specify netserver port number\n\
    -P 0|1            Don't/Do display test headers\n\
    -t testname       Specify test to perform\n\
    -v verbosity      Specify the verbosity level\n\
\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n"; 


/* This routine will return the two arguments to the calling routine. */
/* If the second argument is not specified, and there is no comma, */
/* then the value of the second argument will be the same as the */
/* value of the first. If there is a comma, then the value of the */
/* second argument will be the value of the second argument ;-) */
void
break_args(s, arg1, arg2)
char	*s, *arg1, *arg2;

{
  char *ns;
  ns = strchr(s,',');
  if (ns) {
    /* there was a comma arg2 should be the second arg*/
    *ns++ = '\0';
    while (*arg2++ = *ns++);
  }
  else {
    /* there was not a comma, we can use ns as a temp s */
    /* and arg2 should be the same value as arg1 */
    ns = s;
    while (*arg2++ = *ns++);
  };
  while (*arg1++ = *s++);
}

void
set_defaults()
{
  
  /* stuff to say where this test is going                              */
  strcpy(host_name,"localhost");	/* remote host name or ip addr  */
  strcpy(test_name,"TCP_STREAM");	/* which test to run 		*/
  test_port	= 12865;	        /* where is the test waiting    */
  
  /* output controlling variables                               */
  debug			= 0;/* debugging level			*/
  print_headers		= 1;/* do print test headers		*/
  verbosity		= 1;/* verbosity level			*/
  /* cpu variables */
  local_cpu_usage	= 0;/* measure local cpu		*/
  remote_cpu_usage	= 0;/* what do you think ;-)		*/
  
  local_cpu_rate	= 0.0;
  remote_cpu_rate	= 0.0;
  
  /* the end-test conditions for the tests - either transactions, bytes,  */
  /* or time. different vars used for clarity - space is cheap ;-)        */
  test_time	= 10;	/* test ends by time			*/
  test_bytes	= 0;	/* test ends on byte count		*/
  test_trans	= 0;	/* test ends on tran count		*/
  
  /* the alignment conditions for the tests				*/
  local_recv_align	= 8;	/* alignment for local receives	*/
  local_send_align	= 8;	/* alignment for local sends	*/
  remote_recv_align	= 8;	/* alignment for remote receives*/
  remote_send_align	= 8;	/* alignment for remote sends	*/
  
#ifdef INTERVALS
  /* rate controlling stuff */
  interval_usecs  = 0;
  interval_wate   = 1;
  interval_burst  = 0;
#endif /* INTERVALS */
  
#ifdef DIRTY
  /* dirty and clean cache stuff */
  loc_dirty_count = 0;
  loc_clean_count = 0;
  rem_dirty_count = 0;
  rem_clean_count = 0;
#endif /* DIRTY */
  
}
     

void
print_netserver_usage()
{
  fprintf(stderr,netserver_usage);
}


void
print_netperf_usage()
{
  fprintf(stderr,netperf_usage);
}

void
scan_cmd_line(argc, argv)
     int	argc;
     char	*argv[];

{
  extern int	optind, opterrs;  /* index of first unused arg 	*/
  extern char	*optarg;	  /* pointer to option string	*/
  
  int		c;
  
  char	arg1[BUFSIZ],  /* argument holders		*/
  arg2[BUFSIZ];
  
  program = (char *)malloc(strlen(argv[0]) + 1);
  strcpy(program, argv[0]);
  
  /* Go through all the command line arguments and break them */
  /* out. For those options that take two parms, specifying only */
  /* the first will set both to that value. Specifying only the */
  /* second will leave the first untouched. To change only the */
  /* first, use the form first, (see the routine break_args.. */
  
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
	local_send_align = atoi(arg1);
      }
      if (arg2[0])
	local_recv_align = atoi(arg2);
      break;
    case 'A':
      /* set remote alignments */
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	remote_send_align = atoi(arg1);
      }
      if (arg2[0])
	remote_recv_align = atoi(arg2);
      break;
    case 'f':
      /* set the thruput formatting */
      libfmt = *optarg;
      break;
    case 'k':
      /* local dirty and clean counts */
#ifdef DIRTY
      break_args(optarg,arg1,arg2);
      if (arg1[0]) {
	loc_dirty_count = atoi(arg1);
      }
      if (arg2[0] ) {
	loc_clean_count = atoi(arg2);
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
	rem_dirty_count = atoi(arg1);
      }
      if (arg2[0] ) {
	rem_clean_count = atoi(arg2);
      }
#else
      printf("I don't know how to get dirty.\n");
#endif /* DIRTY */
      break;
    case 'o':
      /* set the local offsets */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	local_send_offset = atoi(arg1);
      if (arg2[0])
	local_recv_offset = atoi(arg2);
      break;
    case 'O':
      /* set the remote offsets */
      break_args(optarg,arg1,arg2);
      if (arg1[0]) 
	remote_send_offset = atoi(arg1);
      if (arg2[0])
	remote_recv_offset = atoi(arg2);
      break;
    case 'P':
      /* to print or not to print, that is */
      /* the header question */
      print_headers = atoi(optarg);
      break;
    case 't':
      /* set the test name */
      strcpy(test_name,optarg);
      break;
    case 'W':
      /* set the "width" of the user space data */
      /* buffer. This will be the number of */
      /* send_size buffers malloc'd in the */
      /* *_STREAM test. It may be enhanced to set */
      /* both send and receive "widths" but for now */
      /* it is just the sending *_STREAM. */
      send_width = atoi(optarg);
      break;
    case 'l':
      /* determine test end conditions */
      /* assume a timed test */
      test_time = atoi(optarg);
      test_bytes = test_trans = 0;
      if (test_time < 0) {
	test_bytes = -1 * test_time;
	test_trans = test_bytes;
	test_time = 0;
      }
      break;
    case 'd':
      debug++;
      break;
    case 'v':
      /* say how much to say */
      verbosity = atoi(optarg);
      break;
    case 'c':
      /* measure local cpu usage please. the user */
      /* may have specified the cpu rate as an */
      /* optional parm */
      if (isdigit(argv[optind][0])){
	/* there was an optional parm */
	local_cpu_rate = atof(argv[optind]);
	optind++;
      }
      local_cpu_usage++;
      break;
    case 'C':
      /* measure remote cpu usage please */
      if (isdigit(argv[optind][0])){
	/* there was an optional parm */
	remote_cpu_rate = atof(argv[optind]);
	optind++;
      }
      remote_cpu_usage++;
      break;
    case 'p':
      /* specify an alternate port number */
      test_port = (short) atoi(optarg);
      break;
    case 'H':
      /* save-off the host identifying information */
      strcpy(host_name,optarg);
      break;
    case 'w':
      /* We want to send requests at a certain wate. */
      /* Remember that there are 1000000 usecs in a */
      /* second, and that the packet rate is */
      /* expressed in packets per second. */
#ifdef INTERVALS
      interval_usecs = 1000000 / atoi(optarg);
      interval_wate  = atoi(optarg);
#else
      fprintf(where,
	      "Packet rate control is not compiled in.\n");
#endif
      break;
    case 'b':
      /* we want to have a burst so many packets per */
      /* interval. */
#ifdef INTERVALS
      interval_burst = atoi(optarg);
#else
      fprintf(where,
	      "Packet burst size is not compiled in. \n");
#endif INTERVALS
      break;
    };
  }
  /* we have encountered a -- in global command-line */
  /* processing and assume that this means we have gotten to the */
  /* test specific options. this is a bit kludgy and if anyone has */
  /* a better solution, i would love to see it */
  if (optind != argc) {
    if ((strcmp(test_name,"TCP_STREAM") == 0) || 
	(strcmp(test_name,"TCP_RR") == 0) ||
	(strcmp(test_name,"TCP_ARR") == 0) ||
	(strcmp(test_name,"UDP_STREAM") == 0) ||
	(strcmp(test_name,"UDP_RR") == 0))
      {
	scan_sockets_args(argc, argv);
      }
    else if ((strcmp(test_name,"DLCO_RR") == 0) ||
	     (strcmp(test_name,"DLCL_RR") == 0) ||
	     (strcmp(test_name,"DLCO_STREAM") == 0) ||
	     (strcmp(test_name,"DLCL_STREAM") == 0))
      {
	scan_dlpi_args(argc, argv);
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
  printf("Report local CPU %d\n",local_cpu_usage);
  printf("Report remote CPU %d\n",remote_cpu_usage);
  printf("Verbosity: %d\n",verbosity);
  printf("Debug: %d\n",debug);
  printf("Port: %d\n",test_port);
  printf("Test name: %s\n",test_name);
  printf("Test bytes: %d Test time: %d Test trans: %d\n",
	 test_bytes,
	 test_time,
	 test_trans);
  printf("Host name: %s\n",host_name);
  printf("\n");
}
