char	netlib_id[]="\
@(#)netlib.c (c) Copyright 1993, 1994 Hewlett-Packard Company. Version 2.0PL1";

/****************************************************************/
/*								*/
/*	netlib.c						*/
/*								*/
/*	the common utility routines available to all...		*/
/*								*/
/*	establish_control()	establish the control socket	*/
/*	calibrate_local_cpu()	do local cpu calibration	*/
/*	calibrate_remote_cpu()	do remote cpu calibration	*/
/*	send_request()		send a request to the remote	*/
/*	recv_response()		receive a response from remote	*/
/*	send_response()		send a response to the remote	*/
/*	recv_request()		recv a request from the remote	*/
/*	dump_request()		dump request contents		*/
/*	dump_response()		dump response contents		*/
/*	cpu_start()		start measuring cpu		*/
/*	cpu_stop()		stop measuring cpu		*/
/*	calc_cpu_util()		calculate the cpu utilization	*/
/*	calc_service_demand()	calculate the service demand	*/
/*	calc_thruput()		calulate the tput in units	*/
/*	calibrate()		really calibrate local cpu	*/
/*	identify_local()	print local host information	*/
/*	identify_remote()	print remote host information	*/
/*	format_number()		format the number (KB, MB,etc)	*/
/*	format_units()		return the format in english    */
/*	msec_sleep()		sleep for some msecs		*/
/*      start_timer()           start a timer                   */
/*								*/
/*      the routines you get when DO_DLPI is defined...         */
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
/*								*/
/*	Global include files				      	*/
/*							       	*/
/****************************************************************/
#include <limits.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/times.h>
 /* seems that POSIX specifies that fcntl.h is simply <fctnl.h> while */
 /* for some reason, I was including <sys/fcntl.h>. This breaks */
 /* Ultrix, and was discovered by Johnathan Stone at Stanford. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <sys/utsname.h>
#include <math.h>
#include <string.h>
#include <sys/param.h>

#include <sys/mman.h>

#ifdef _AIX
#include <sys/select.h>
#endif /* _AIX */

#ifdef USE_PSTAT
#include <sys/dk.h>
#include <sys/pstat.h>
#endif /* USE_PSTAT */

 /* not all systems seem to have the sysconf for page size. for those */
 /* which do not, we will assume that the page size is 8192 bytes. */
 /* this should be more than enough to be sure that there is no page */
 /* or cache thrashing by looper processes on MP systems. otherwise */
 /* that's really just too bad - such systems should define */
 /* _SC_PAGE_SIZE - raj 4/95 */
#ifndef _SC_PAGE_SIZE
#define NETPERF_PAGE_SIZE 8192
#else
#define NETPERF_PAGE_SIZE sysconf(_SC_PAGE_SIZE)
#endif /* _SC+PAGE_SIZE */

#ifdef DO_DLPI
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/poll.h>
#ifdef __osf__
#include <sys/dlpihdr.h>
#else /* __osf__ */
#include <sys/dlpi.h>
#ifdef __hpux
#include <sys/dlpi_ext.h>
#endif /* __hpux */
#endif /* __osf__ */
#endif /* DO_DLPI */

#ifdef HISTOGRAM
#include "hist.h"
#endif /* HISTOGRAM */
/****************************************************************/
/*                                                              */
/*	Local Include Files					*/
/*                                                              */
/****************************************************************/
#define NETLIB
#include "netlib.h"
#include "netsh.h"


/****************************************************************/
/*		       						*/
/*	Global constants, macros and variables			*/
/*							       	*/
/****************************************************************/

#ifndef LONG_LONG_MAX
#define LONG_LONG_MAX 9223372036854775807LL
#endif /* LONG_LONG_MAX */

 /* older versions of netperf knew about the HP kernel IDLE counter. */
 /* this is now obsolete - in favor of either pstat(), times, or a */
 /* process-level looper process. we also now require support for the */
 /* "long" integer type. raj 4/95.  */

int 
  lib_num_cpus;    /* the number of cpus in the system */

#define PAGES_PER_CHILD 2
long long
  *lib_idle_address[MAXCPUS], /* addresses of the per-cpu idle counters	*/
  lib_start_count[MAXCPUS],  /* idle counter initial value per-cpu      */
  lib_end_count[MAXCPUS];    /* idle counter final value per-cpu	*/

int
  lib_idle_pids[MAXCPUS];    /* the pids of the per-cpu idle loopers */

int
  lib_child_num;
  

long long
  *lib_base_pointer;

int
  lib_idle_fd;
  

  

int	lib_use_idle;
int     cpu_method;

 /* if there is no IDLE counter in the kernel */
#ifdef USE_PSTAT
struct	pst_dynamic	pst_dynamic_info;
long			cp_time1[PST_MAX_CPUSTATES];
long	                cp_time2[PST_MAX_CPUSTATES];
#else
struct	tms		times_data1, 
                        times_data2;
#endif

struct	timeval		time1, time2;
struct	timezone	tz;
float	lib_elapsed,
	lib_local_maxrate,
	lib_remote_maxrate,
	lib_local_cpu_util,
	lib_remote_cpu_util;

#define	RESPONSE_SIZE 64
#define	REQUEST_SIZE 64
int	request_array[REQUEST_SIZE];
int	response_array[RESPONSE_SIZE];

int	netlib_control;

int	server_sock;

int	tcp_proto_num;

 /* all the request/response stuff needs to be fleshed-out for the  */
 /* different test types. I may define a common core and then give */
 /* each test some fixed amount of space to play with. */

struct	netlib_request_type { int	request_type;
			      int	request_data[MAXSPECDATA];
		       };	

struct	netlib_response_type	{ int	response_type;
				  int	serv_errno;
				  int	response_data[MAXSPECDATA];
			  };

struct	netperf_request_struct	*netperf_request;
struct	netperf_response_struct	*netperf_response;

FILE	*where;

char	libfmt = 'm';
	
#ifdef DO_DLPI
/* some stuff for DLPI control messages */
#define DLPI_DATA_SIZE 2048

unsigned long control_data[DLPI_DATA_SIZE];
struct strbuf control_message = {DLPI_DATA_SIZE, 0, (char *)control_data};

#endif /* DO_DLPI */

int	times_up;

static int measuring_cpu;

#ifdef INTERVALS
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
    perror("clusterperf: cluster_root: setitimer");
    exit(1);
  }
  return;
}
#endif /* INTERVALS */


double
ntohd(net_double)
     double net_double;

{
  /* we rely on things being nicely packed */
  union {
    double whole_thing;
    unsigned int words[2];
    unsigned char bytes[8];
  } conv_rec;

  unsigned char scratch;
  int i;

  /* on those systems where ntohl is a no-op, we want to return the */
  /* original value, unchanged */

  if (ntohl(1L) == 1L) {
    return(net_double);
  }

  conv_rec.whole_thing = net_double;

  /* we know that in the message passing routines that ntohl will have */
  /* been called on the 32 bit quantities. we need to put those back */
  /* the way they belong before we swap */
  conv_rec.words[0] = htonl(conv_rec.words[0]);
  conv_rec.words[1] = htonl(conv_rec.words[1]);
  
  /* now swap */
  for (i=0; i<= 3; i++) {
    scratch = conv_rec.bytes[i];
    conv_rec.bytes[i] = conv_rec.bytes[7-i];
    conv_rec.bytes[7-i] = scratch;
  }
  
  return(conv_rec.whole_thing);
  
}
double
htond(host_double)
     double host_double;

{
  /* we rely on things being nicely packed */
  union {
    double whole_thing;
    unsigned int words[2];
    unsigned char bytes[8];
  } conv_rec;

  unsigned char scratch;
  int i;

  /* on those systems where ntohl is a no-op, we want to return the */
  /* original value, unchanged */

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
  
  /* we know that in the message passing routines htonl will */
  /* be called on the 32 bit quantities. we need to set things up so */
  /* that when this happens, the proper order will go out on the */
  /* network */
  conv_rec.words[0] = htonl(conv_rec.words[0]);
  conv_rec.words[1] = htonl(conv_rec.words[1]);
  
  return(conv_rec.whole_thing);
  
}


int
get_num_cpus()

{
/* on HP-UX, even when we use the looper procs we need the pstat call */
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
  /* we need to know some other ways to do this, or just fall-back on */
  /* a global command line option - raj 4/95 */
  temp_cpus = 1;
#endif /*  __hpux */
  if (temp_cpus > MAXCPUS) {
    fprintf(where,
	    "Sorry, this system has more CPUs (%d) than I can handle (%d).\n",
	    temp_cpus,
	    MAXCPUS);
    fprintf(where,
	    "Please alter MAXCPUS in netlib.h and recompile.\n");
    fflush(where);
    exit(1);
  }

  return(temp_cpus);
  
}  

     

/************************************************************************/
/*									*/
/*	signal catcher		                                	*/
/*									*/
/************************************************************************/

void
#ifdef __hpux
catcher(sig, code, scp)
     int sig;
     int code;
     struct sigcontext *scp;
#else /* __hpux */
catcher(sig)
     int sig;
#endif /* __hpux */
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
    fprintf(where,"netperf: caught SIGINT\n");
    fflush(where);
    exit(1);
    break;
  case SIGALRM: 
   if (--test_len_ticks == 0) {
      /* the test is over */
      if (times_up != 0) {
	fprintf(where,"catcher: timer popped with times_up != 0\n");
	fflush(where);
      }
      times_up = 1;
#ifdef INTERVALS
      stop_itimer();
#endif /* INTERVALS */
      break;
    }
    else {
#ifdef INTERVALS
#ifdef __hpux
      /* the test is not over yet and we must have been using the */
      /* interval timer. if we were in SYS_SIGSUSPEND we want to */
      /* re-start the system call. Otherwise, we want to get out of */
      /* the sigsuspend call. I NEED TO KNOW HOW TO DO THIS FOR OTHER */
      /* OPERATING SYSTEMS. If you know how, please let me know. rick */
      /* jones <raj@cup.hp.com> */
      if (scp->sc_syscall != SYS_SIGSUSPEND) {
	if (debug > 2) {
	  fprintf(where,
		  "catcher: Time to send burst > interval!\n");
	  fflush(where);
	}
	scp->sc_syscall_action = SIG_RESTART;
      }
#endif /* __hpux */
      if (demo_mode) {
	/* spit-out what the performance was in units/s. based on our */
	/* knowledge of the interval length we do not need to call */
	/* gettimeofday() raj 2/95 */
	fprintf(where,"%g\n",(units_this_tick * 
			      (double) 1000000 / 
			      (double) usec_per_itvl));
	fflush(where);
	units_this_tick = (double) 0.0;
      }
#else /* INTERVALS */
      fprintf(where,
	      "catcher: interval timer running unexpectedly!\n");
      fflush(where);
      times_up = 1;
#endif /* INTERVALS */      
      break;
    }
  }
  return;
}

void
install_signal_catchers()

{
  /* just a simple little routine to catch a bunch of signals */

  struct sigaction action;
  int i;

  fprintf(where,"installing catcher for all signals\n");
  fflush(where);

  sigemptyset(&(action.sa_mask));
  action.sa_handler = catcher;
  action.sa_flags = 0;

  for (i = 1; i <= NSIG; i++) {
    if (i != SIGALRM) {
      if (sigaction(SIGALRM,&action,NULL) != 0) {
	fprintf(where,
		"Could not install signal catcher for sig %d, errno %d\n",
		i,
		errno);
	fflush(where);

      }
    }
  }

}


void
start_timer(time)
     int time;
{
  struct sigaction action;

if (debug) {
  fprintf(where,"About to start a timer for %d seconds.\n",time);
  fflush(where);
}

  action.sa_handler = catcher;
  sigemptyset(&(action.sa_mask));
  sigaddset(&(action.sa_mask),SIGALRM);

#ifdef SA_INTERRUPT
  /* on some systems (SunOS 4.blah), system calls are restarted. we do */
  /* not want that */
  action.sa_flags = SA_INTERRUPT;
#else /* SA_INTERRUPT */
  action.sa_flags = 0;
#endif /* SA_INTERRUPT */

  if (sigaction(SIGALRM, &action, NULL) < 0) {
    fprintf(where,"start_timer: error installing alarm handler ");
    fprintf(where,"errno %d\n",errno);
    fflush(where);
    exit(1);
  }

  /* this is the easy case - just set the timer for so many seconds */ 
  if (alarm(time) != 0) {
    fprintf(where,
	    "error starting alarm timer, errno %d\n",
	    errno);
    fflush(where);
  }
  test_len_ticks = 1;
} 

#ifdef INTERVALS
 /* this routine will enable the interval timer and set things up so */
 /* that for a timed test the test will end at the proper time. it */
 /* should detect the presence of POSIX.4 timer_* routines one of */
 /* these days */
void
start_itimer( interval_len_msec )
     unsigned int interval_len_msec;
{

  unsigned int ticks_per_itvl;

  struct itimerval new_interval;
  struct itimerval old_interval;

  /* if -DINTERVALS was used, we will use the ticking of the itimer to */
  /* tell us when the test is over. while the user will be specifying */
  /* some number of milliseconds, we know that the interval timer is */
  /* really in units of 1/HZ. so, to prevent the test from running */
  /* "long" it would be necessary to keep this in mind when calculating */
  /* the number of itimer events */

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
    fprintf(where,"setting the interval timer to %d sec %d usec ",
	    usec_per_itvl / 1000000,
	    usec_per_itvl % 1000000);
    fprintf(where,"test len %d ticks\n",
	    test_len_ticks);
    fflush(where);
  }

  /* if this was not a timed test, then we really aught to enable the */
  /* signal catcher raj 2/95 */

  new_interval.it_interval.tv_sec = usec_per_itvl / 1000000;
  new_interval.it_interval.tv_usec = usec_per_itvl % 1000000;  
  new_interval.it_value.tv_sec = usec_per_itvl / 1000000;
  new_interval.it_value.tv_usec = usec_per_itvl % 1000000;  
  if (setitimer(ITIMER_REAL,&new_interval,&old_interval) != 0) {
    /* there was a problem arming the interval timer */ 
    perror("clusterperf: cluster_root: setitimer");
    exit(1);
  }
}
#endif /* INTERVALS */

/****************************************************************/
/*								*/
/*	netlib_init()						*/
/*								*/
/*	initialize the performance library...			*/
/*								*/
/****************************************************************/

void
netlib_init()
{
  where		   = stdout;
  netperf_request  = (struct netperf_request_struct *)request_array;
  netperf_response = (struct netperf_response_struct *)response_array;

}

 /* this routine will conver the string into an unsigned integer. it */
 /* is used primarily for the command-line options taking a number */
 /* (such as the socket size) which could be rather large. If someone */
 /* enters 32M, then the number will be converted to 32 * 1024 * 1024. */
 /* If they inter 32m, the number will be converted to 32 * 1000 * */
 /* 1000 */
unsigned int
convert(string)
     char *string;

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


 /* this routine will allocate a circular list of buffers for either */
 /* send or receive operations. each of these buffers will be aligned */
 /* and offset as per the users request. the circumference of this */
 /* ring will be controlled by the setting of send_width. the buffers */
 /* will be filled with data from the file specified in fill_file. if */
 /* fill_file is an empty string, the buffers will not be filled with */
 /* any particular data */

struct ring_elt *
allocate_buffer_ring(width, buffer_size, alignment, offset)
     int width;
     int buffer_size;
     int alignment;
     int offset;
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

  malloc_size = buffer_size + alignment + offset;

  /* did the user wish to have the buffers pre-filled with data from a */
  /* particular source? */
  if (strcmp(fill_file,"") == 0) {
    do_fill = 0;
  }
  else {
    do_fill = 1;
    fill_source = (FILE *)fopen(fill_file,"r");
    if (fill_source == (FILE *)NULL) {
      perror("Could not open requested fill file");
      exit(1);
    }
  }

  prev_link = NULL;
  for (i = 1; i <= width; i++) {
    /* get the ring element */
    temp_link = (struct ring_elt *)malloc(sizeof(struct ring_elt));
    /* remember the first one so we can close the ring at the end */
    if (i == 1) {
      first_link = temp_link;
    }
    temp_link->buffer_base = (char *)malloc(malloc_size);
    temp_link->buffer_ptr = (char *)(( (long)(temp_link->buffer_base) + 
			  (long)alignment - 1) &	
			 ~((long)alignment - 1));
    temp_link->buffer_ptr += offset;
    /* is where the buffer fill code goes. */
    if (do_fill) {
      bytes_left = buffer_size;
      while (bytes_left) {
	if (((bytes_read = fread(temp_link->buffer_ptr,
				 1,
				 bytes_left,
				 fill_source)) == 0) &&
	    (feof(fill_source))){
	  rewind(fill_source);
	}
	bytes_left -= bytes_read;
      }
    }
    temp_link->next = prev_link;
    prev_link = temp_link;
  }
  first_link->next = temp_link;

  return(first_link); /* it's a circle, doesn't matter which we return */
}

 /***********************************************************************/
 /*									*/
 /*	dump_request()							*/
 /*									*/
 /* display the contents of the request array to the user. it will	*/
 /* display the contents in decimal, hex, and ascii, with four bytes	*/
 /* per line.								*/
 /*									*/
 /***********************************************************************/

dump_request()
{
int counter = 0;
fprintf(where,"request contents:\n");
for (counter = 0; counter < REQUEST_SIZE-3; counter += 4) {
  fprintf(where,"%d:\t%8x %8x %8x %8x \t|%4.4s| |%4.4s| |%4.4s| |%4.4s|\n",
	  counter,
	  request_array[counter],
	  request_array[counter+1],
	  request_array[counter+2],
	  request_array[counter+3],
	  &request_array[counter],
	  &request_array[counter+1],
	  &request_array[counter+2],
	  &request_array[counter+3]);
}
fflush(where);
}


 /***********************************************************************/
 /*									*/
 /*	dump_response()							*/
 /*									*/
 /* display the content of the response array to the user. it will	*/
 /* display the contents in decimal, hex, and ascii, with four bytes	*/
 /* per line.								*/
 /*									*/
 /***********************************************************************/

dump_response()
{
int counter = 0;

fprintf(where,"response contents\n");
for (counter = 0; counter < REQUEST_SIZE-3; counter += 4) {
  fprintf(where,"%d:\t%8x %8x %8x %8x \t>%4.4s< >%4.4s< >%4.4s< >%4.4s<\n",
	  counter,
	  response_array[counter],
	  response_array[counter+1],
	  response_array[counter+2],
	  response_array[counter+3],
	  &response_array[counter],
	  &response_array[counter+1],
	  &response_array[counter+2],
	  &response_array[counter+3]);
}
fflush(where);
}

 /***********************************************************************/
 /*									*/
 /*	format_number()							*/
 /*									*/
 /* return a pointer to a formatted string containing the value passed  */
 /* translated into the units specified. It assumes that the base units */
 /* are bytes. If the format calls for bits, it will use SI units (10^) */
 /* if the format calls for bytes, it will use CS units (2^)...		*/
 /* This routine should look familiar to uses of the latest ttcp...	*/
 /*									*/
 /***********************************************************************/

char *
format_number(number)
double	number;
{
	static	char	fmtbuf[64];
	
	switch (libfmt) {
	case 'K':
		sprintf(fmtbuf, "%-7.2f" , number / 1024.0);
		break;
	case 'M':
		sprintf(fmtbuf, "%-7.2f", number / 1024.0 / 1024.0);
		break;
	case 'G':
		sprintf(fmtbuf, "%-7.2f", number / 1024.0 / 1024.0 / 1024.0);
		break;
	case 'k':
		sprintf(fmtbuf, "%-7.2f", number * 8 / 1000.0);
		break;
	case 'm':
		sprintf(fmtbuf, "%-7.2f", number * 8 / 1000.0 / 1000.0);
		break;
	case 'g':
		sprintf(fmtbuf, "%-7.2f", number * 8 / 1000.0 / 1000.0 / 1000.0);
		break;

		default:
		sprintf(fmtbuf, "%-7.2f", number / 1024.0);
	}

	return fmtbuf;
}

char
format_cpu_method(method)
     int method;
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
  case TIMES:
    method_char = 'T';
    break;
  case GETRUSAGE:
    method_char = 'R';
    break;
  case LOOPER:
    method_char = 'L';
    break;
  default:
    method_char = '?';
  }
  
  return method_char;

}

char *
format_units()
{
  static	char	unitbuf[64];
  
  switch (libfmt) {
  case 'K':
    sprintf(unitbuf, "%s", "KBytes");
    break;
  case 'M':
    sprintf(unitbuf, "%s", "MBytes");
    break;
  case 'G':
    sprintf(unitbuf, "%s", "GBytes");
    break;
  case 'k':
    sprintf(unitbuf, "%s", "10^3bits");
    break;
  case 'm':
    sprintf(unitbuf, "%s", "10^6bits");
    break;
  case 'g':
    sprintf(unitbuf, "%s", "10^9bits");
    break;
    
  default:
    sprintf(unitbuf, "%s", "KBytes");
  }
  
  return unitbuf;
}


/****************************************************************/
/*								*/
/*	shutdown_control()					*/
/*								*/
/* tear-down the control connection between me and the server.  */
/****************************************************************/

void 
shutdown_control()
{

  char 	*buf = (char *)netperf_response;
  int 	buflen = sizeof(response_array);

  /* stuff for select, use fd_set for better compliance */
  fd_set	readfds, writefds, exceptfds;
  struct	timeval	timeout;

  if (debug) {
    fprintf(where,
	    "shutdown_control: shutdown of control connection requested.\n");
    fflush(where);
  }

  /* first, we say that we will be sending no more data on the */
  /* connection */
  if (shutdown(netlib_control,1) == -1) {
    fprintf(where,
	    "shutdown_control: error in shutdown. errno %d\n",
	    errno);
    fflush(where);
    exit(1);
  }

  /* Now, we hang on a select waiting for the socket to become */
  /* readable to receive the shutdown indication from the remote. this */
  /* will be "just" like the recv_response() code */

  /* we only select once. it is assumed that if the response is split */
  /* (which should not be happening, that we will receive the whole */
  /* thing and not have a problem ;-) */

  FD_ZERO(&readfds);
  FD_SET(netlib_control,&readfds);
  timeout.tv_sec  = 60; /* wait one minute then punt */
  timeout.tv_usec = 0;

  /* select had better return one, or there was either a problem or a */
  /* timeout... */
  if (select(FD_SETSIZE,
	     &readfds,
	     0,
	     0,
	     &timeout) != 1) {
    fprintf(where,
	    "shutdown_control: no response received. errno %d\n",
	    errno);
    fflush(where);
    exit(1);
  }

  /* we now assume that the socket has come ready for reading */
  recv(netlib_control, buf, buflen,0);

}


 /***********************************************************************/
 /*									*/
 /*	send_request()							*/
 /*									*/
 /* send a netperf request on the control socket to the remote half of 	*/
 /* the connection. to get us closer to intervendor interoperability, 	*/
 /* we will call htonl on each of the int that compose the message to 	*/
 /* be sent. the server-half of the connection will call the ntohl 	*/
 /* routine to undo any changes that may have been made... 		*/
 /* 									*/
 /***********************************************************************/

send_request()
{
  int	counter=0;
  
  /* display the contents of the request if the debug level is high */
  /* enough. otherwise, just send the darned thing ;-) */
  
  if (debug > 1) {
    fprintf(where,"entered send_request...\n");
  }
  fflush(stdout);
  if (debug > 1) {
    dump_request();
  }
  
  /* put the entire request array into network order. We do this */
  /* arbitrarily rather than trying to figure-out just how much */
  /* of the request array contains real information. this should */
  /* be simpler, and at any rate, the performance of sending */
  /* control messages for this benchmark is not of any real */
  /* concern. */ 
  
  for (counter=0;counter < sizeof(request_array)/4; counter++) {
    request_array[counter] = htonl(request_array[counter]);
  }
  
  if (send(netlib_control,
	   (char *)netperf_request,
	   sizeof(request_array),
	   0) != sizeof(request_array)) {
    perror("send_request: send call failure");
    
    exit(1);
  }
}

/***********************************************************************/
 /*									*/
 /*	send_response()							*/
 /*									*/
 /* send a netperf response on the control socket to the remote half of */
 /* the connection. to get us closer to intervendor interoperability, 	*/
 /* we will call htonl on each of the int that compose the message to 	*/
 /* be sent. the other half of the connection will call the ntohl 	*/
 /* routine to undo any changes that may have been made... 		*/
 /* 									*/
 /***********************************************************************/

send_response()
{
int	counter=0;

 /* display the contents of the request if the debug level is high */
 /* enough. otherwise, just send the darned thing ;-) */

if (debug > 1) {
	dump_response();
}

 /* put the entire response_array into network order. We do this */
 /* arbitrarily rather than trying to figure-out just how much of the */
 /* request array contains real information. this should be simpler, */
 /* and at any rate, the performance of sending control messages for */
 /* this benchmark is not of any real concern. */

for (counter=0;counter < sizeof(request_array)/4; counter++) {
	response_array[counter] = htonl(response_array[counter]);
}

/*KC*/
if (send(server_sock,
	 (char *)netperf_response,
	 sizeof(response_array),
	 0) != sizeof(response_array)) {
  perror("send_response: send call failure");
  exit(1);
}

}

 /***********************************************************************/
 /* 									*/
 /*	recv_request()							*/
 /*									*/
 /* receive the remote's request on the control socket. we will put	*/
 /* the entire response into host order before giving it to the		*/
 /* calling routine. hopefully, this will go most of the way to		*/
 /* insuring intervendor interoperability. if there are any problems,	*/
 /* we will just punt the entire situation.				*/
 /*									*/
 /***********************************************************************/

recv_request()
{
int 	tot_bytes_recvd,
        bytes_recvd, 
        bytes_left;
char 	*buf = (char *)netperf_request;
int	buflen = sizeof(request_array);
int	counter;

tot_bytes_recvd = 0;	
bytes_left      = buflen;
while ((tot_bytes_recvd != buflen) &&
       ((bytes_recvd = recv(server_sock, buf, bytes_left,0)) > 0 )) {
  tot_bytes_recvd += bytes_recvd;
  buf             += bytes_recvd;
  bytes_left      -= bytes_recvd;
}

/* put the request into host order */

for (counter = 0; counter < sizeof(request_array)/sizeof(int); counter++) {
  request_array[counter] = ntohl(request_array[counter]);
}

if (debug) {
  fprintf(where,
	  "recv_request: received %d bytes of request.\n",
	  tot_bytes_recvd);
  fflush(where);
}

if (bytes_recvd == -1) {
  fprintf(where,
	  "recv_request: error on recv, errno %d\n",
	  errno);
  fflush(where);
  exit(1);
}

if (bytes_recvd == 0) {
  /* the remote has shutdown the control connection, we should shut it */
  /* down as well and exit */

  if (debug) {
    fprintf(where,
	    "recv_request: remote reqeusted shutdown of control\n");
    fflush(where);
  }

  shutdown_control();
  exit(0);
}

if (tot_bytes_recvd < buflen) {
  if (debug > 1)
    dump_request();

  fprintf(where,
	  "recv_request: partial request received of %d bytes\n",
	  tot_bytes_recvd);
  fflush(where);
  exit(1);
}

if (debug > 1) {
  dump_request();
}
}

 /***********************************************************************/
 /* 									*/
 /*	recv_response()							*/
 /*									*/
 /* receive the remote's response on the control socket. we will put	*/
 /* the entire response into host order before giving it to the		*/
 /* calling routine. hopefully, this will go most of the way to		*/
 /* insuring intervendor interoperability. if there are any problems,	*/
 /* we will just punt the entire situation.				*/
 /*									*/
 /* The call to select at the beginning is to get us out of hang	*/
 /* situations where the remote gives-up but we don't find-out about	*/
 /* it. This seems to happen only rarely, but it would be nice to be	*/
 /* somewhat robust ;-)							*/
 /***********************************************************************/

recv_response()
{
int 	tot_bytes_recvd,
        bytes_recvd, 
        bytes_left;
char 	*buf = (char *)netperf_response;
int 	buflen = sizeof(response_array);
int	counter;

 /* stuff for select, use fd_set for better compliance */
fd_set	readfds,
        writefds,
        exceptfds;
struct	timeval	timeout;

tot_bytes_recvd = 0;	
bytes_left      = buflen;

/* zero out the response structure */

/* BUG FIX SJB 2/4/93 - should be < not <= */
for (counter = 0; counter < sizeof(response_array)/sizeof(int); counter++) {
        response_array[counter] = 0;
}

 /* we only select once. it is assumed that if the response is split */
 /* (which should not be happening, that we will receive the whole */
 /* thing and not have a problem ;-) */

FD_ZERO(&readfds);
FD_SET(netlib_control,&readfds);
timeout.tv_sec  = 60; /* wait one minute then punt */
timeout.tv_usec = 0;

 /* select had better return one, or there was either a problem or a */
 /* timeout... */
if (select(FD_SETSIZE,
	   &readfds,
	   0,
	   0,
	   &timeout) != 1) {
  fprintf(where,"netperf: receive_response: no response received.\n");
  perror("");
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

/* put the response into host order */

for (counter = 0; counter < sizeof(response_array)/sizeof(int); counter++) {
  response_array[counter] = ntohl(response_array[counter]);
}

if (bytes_recvd == -1) {
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

int
hi_32(big_int)
     long long *big_int;
{
  union overlay_u {
    long long  dword;
    long       words[2];
  } *overlay;

  overlay = (union overlay_u *)big_int;
  /* on those systems which are byte swapped, we really wish to */
  /* return words[1] - at least I think so - raj 4/95 */
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
  /* on those systems which are byte swapped, we really wish to */
  /* return words[0] - at least I think so - raj 4/95 */
  if (htonl(1L) == 1L) {
    /* we are a "normal" :) machine */
    return(overlay->words[1]);
  }
  else {
    return(overlay->words[0]);
  }
}


#ifdef USE_LOOPER

 /* calibrate_looper */

 /* Loop a number of times, sleeping wait_time seconds each and */
 /* count how high tnhe idle counter gets each time. Return  the */
 /* measured cpu rate to the calling routine. raj 4/95 */

float
calibrate_looper(times,wait_time)
     int times;
     int wait_time;

{

  long long	
    firstcnt[MAXCPUS],
    secondcnt[MAXCPUS];

  float	
    elapsed,
    temp_rate,
    rate[MAXTIMES],
    local_maxrate;

  long	
    sec,
    usec;

  int	
    i,
    j;
  
  long	count;
  struct  timeval time1, time2 ;
  struct  timezone tz;
  
  if (times > MAXTIMES) {
    times = MAXTIMES;
  }

  local_maxrate = -1;
  
  for(i = 0; i < times; i++) {
    rate[i] = 0.0;
    for (j = 0; j < lib_num_cpus; j++) {
      firstcnt[j] = *(lib_idle_address[j]);
    }
    gettimeofday (&time1, &tz);
    sleep(wait_time);
    gettimeofday (&time2, &tz);

    if (time2.tv_usec < time1.tv_usec)
      {
	time2.tv_usec += 1000000;
	time2.tv_sec -=1;
      }
    sec = time2.tv_sec - time1.tv_sec;
    usec = time2.tv_usec - time1.tv_usec;
    elapsed = (float)sec + ((float)usec/1000000.0);
    
    if(debug) {
      fprintf(where, "Calibration for counter run: %d\n",i);
      fprintf(where,"\tsec = %ld usec = %ld\n",sec,usec);
      fprintf(where,"\telapsed time = %g\n",elapsed);
    }

    for (j = 0; j < lib_num_cpus; j++) {
      secondcnt[j] = *(lib_idle_address[j]);
      if(debug) {
	/* I know that there are situations where compilers know about */
	/* long long, but the library fucntions do not... raj 4/95 */
	fprintf(where,
		"\tfirstcnt[%d] = 0x%8.8x%8.8x secondcnt[%d] = 0x%8.8x%8.8x\n",
		j,
		hi_32(&firstcnt[j]),
		lo_32(&firstcnt[j]),
		j,
		hi_32(&secondcnt[j]),
		lo_32(&secondcnt[j]));
      }
      /* we assume that it would wrap no more than once. we also */
      /* assume that the result of subtracting will "fit" raj 4/95 */
      temp_rate = (secondcnt[j] >= firstcnt[j]) ?
	(float)(secondcnt[j] - firstcnt[j])/elapsed : 
	  (float)(secondcnt[j]-firstcnt[j]+LONG_LONG_MAX)/elapsed;
      if (temp_rate > rate[i]) rate[i] = temp_rate;
      if(debug) {
	fprintf(where,"\trate[%d] = %g\n",i,rate[i]);
	fflush(where);
      }
      if (local_maxrate < rate[i]) local_maxrate = rate[i];
    }
  }
  if(debug) {
    fprintf(where,"\tlocal maxrate = %g per sec. \n",local_maxrate);
    fflush(where);
  }
  return local_maxrate;
}
#endif /* USE_LOOPER */


#ifdef USE_PSTAT
#ifdef PSTAT_IPCINFO
/****************************************************************/
/*								*/
/*	calibrate_pstat                                         */
/*								*/
/*	Loop a number of times, sleeping wait_time seconds each */
/* and count how high the idle counter gets each time. Return	*/
/* the measured cpu rate to the calling routine.		*/
/*								*/
/****************************************************************/

float
calibrate_pstat(times,wait_time)
     int times;
     int wait_time;

{
  long long
    firstcnt[MAXCPUS],
    secondcnt[MAXCPUS];

  float	
    elapsed, 
    temp_rate,
    rate[MAXTIMES],
    local_maxrate;

  long	
    sec,
    usec;

  int	
    i,
    j;
  
  long	count;

  struct  timeval time1, time2;
  struct  timezone tz;

  struct pst_processor *psp;
  
  if (times > MAXTIMES) {
    times = MAXTIMES;
  }
  
  local_maxrate = -1.0;

  psp       = (struct pst_processor *)malloc(lib_num_cpus * sizeof(*psp));

  for(i = 0; i < times; i++) {
    rate[i] = 0.0;
    /* get the idle sycle counter for each processor */
    if (pstat_getprocessor(psp, sizeof(*psp), lib_num_cpus, 0) != -1) {
      for (j = 0; j < lib_num_cpus; j++) {
	union overlay_u {
	  long long full;
	  long      word[2];
	} *overlay;
	overlay = (union overlay_u *)&(firstcnt[j]);
	overlay->word[0] = psp[j].psp_idlecycles.psc_hi;
	overlay->word[1] = psp[j].psp_idlecycles.psc_lo;
      }
    }
    else {
      fprintf(where,"pstat_setprocessor failure errno %d\n");
      fflush(where);
      exit(1);
    }

    gettimeofday (&time1, &tz);
    sleep(wait_time);
    gettimeofday (&time2, &tz);
    
    if (time2.tv_usec < time1.tv_usec)
      {
	time2.tv_usec += 1000000;
	time2.tv_sec -=1;
      }
    sec = time2.tv_sec - time1.tv_sec;
    usec = time2.tv_usec - time1.tv_usec;
    elapsed = (float)sec + ((float)usec/1000000.0);

    if(debug) {
      fprintf(where, "Calibration for counter run: %d\n",i);
      fprintf(where,"\tsec = %ld usec = %ld\n",sec,usec);
      fprintf(where,"\telapsed time = %g\n",elapsed);
    }

    if (pstat_getprocessor(psp, sizeof(*psp), lib_num_cpus, 0) != -1) {
      for (j = 0; j < lib_num_cpus; j++) {
	union overlay_u {
	  long long full;
	  long      word[2];
	} *overlay;
	overlay = (union overlay_u *)&(secondcnt[j]);
	overlay->word[0] = psp[j].psp_idlecycles.psc_hi;
	overlay->word[1] = psp[j].psp_idlecycles.psc_lo;
	if(debug) {
	  /* I know that there are situations where compilers know about */
	  /* long long, but the library fucntions do not... raj 4/95 */
	  fprintf(where,
		  "\tfirstcnt[%d] = 0x%8.8x%8.8x secondcnt[%d] = 0x%8.8x%8.8x\n",
		  j,
		  hi_32(&firstcnt[j]),
		  lo_32(&firstcnt[j]),
		  j,
		  hi_32(&secondcnt[j]),
		  lo_32(&secondcnt[j]));
	}
	temp_rate = (secondcnt[j] >= firstcnt[j]) ? 
	  (float)(secondcnt[j] - firstcnt[j] )/elapsed : 
	    (float)(secondcnt[j] - firstcnt[j] + LONG_LONG_MAX)/elapsed;
	if (temp_rate > rate[i]) rate[i] = temp_rate;
	if(debug) {
	  fprintf(where,"\trate[%d] = %g\n",i,rate[i]);
	  fflush(where);
	}
	if (local_maxrate < rate[i]) local_maxrate = rate[i];
      }
    }
    else {
      fprintf(where,"pstat failure; errno %d\n",errno);
      fflush(where);
      exit(1);
    }
  }
  if(debug) {
    fprintf(where,"\tlocal maxrate = %g per sec. \n",local_maxrate);
    fflush(where);
  }
  return local_maxrate;
}
#endif /* PSTAT_IPCINFO */
#endif /* USE_PSTAT */


void libmain()
{
fprintf(where,"hello world\n");
fprintf(where,"debug: %d\n",debug);
}

/****************************************************************/
/*								*/
/*	establish_control()					*/
/*								*/
/* set-up the control connection between me and the server so	*/
/* we can actually run some tests. if we cannot establish the   */
/* control connection, we might as well punt...			*/
/* the variables for the control socket are kept in this lib	*/
/* so as to 'hide' them from the upper routines as much as	*/
/* possible so we can change them without affecting anyone...	*/
/****************************************************************/

struct	sockaddr_in	server;         /* remote host address          */
struct	servent		*sp;            /* server entity                */
struct	hostent		*hp;            /* host entity                  */

void 
establish_control(hostname,port)
char 		hostname[];
short int	port;
{
  if (debug > 1) {
    fprintf(where,"establish_control: entered with %s and %d\n",
	    hostname,
	    port);
  }
  
  if ((hp = gethostbyname(hostname)) == NULL) {
    fprintf(where,"netperf: establish_control: unknown host\n");
    exit(1);
  }
  
  if (debug > 1) {
    fprintf(where,"got the host info... \n");
    fflush(where);
  }
  
  
  /********************************************************/
  /* Set up the control socket netlib_control first	*/
  /* for the time being we will assume that all set-ups	*/
  /* are for tcp/ip and sockets...			*/
  /********************************************************/
  
  bzero((char *)&server,
	sizeof(server));
  bcopy(hp->h_addr,
	(char *)&server.sin_addr,
	hp->h_length);
  server.sin_family = hp->h_addrtype;
  server.sin_port = htons(port);
  
  if (debug > 1) {
    fprintf(where,"creating a socket\n");
    fflush(stdout);
  }
  
  netlib_control = socket(hp->h_addrtype,
			  SOCK_STREAM,
			  tcp_proto_num);
  
  if (netlib_control < 0){
    perror("establish_control: control socket");
    exit(1);
  }
  
  if (debug > 1) {
    fprintf(where,"about to connect\n");
    fflush(stdout);
  }
  
  if (connect(netlib_control, 
	      (struct sockaddr *)&server, 
	      sizeof(server)) <0){
    perror("establish_control: control socket connect failed");
    exit(1);
  }
  if (debug) {
    fprintf(where,"establish_control: connect completes\n");
  }
  
  /********************************************************/
  /* The Control Socket set-up is done, so now we want	*/
  /* to test for connectivity on the connection		*/
  /********************************************************/
  
  if (debug) 
    netperf_request->request_type = DEBUG_ON;
  else
    netperf_request->request_type = DEBUG_OFF;
  
  send_request();
  recv_response();
  
  if (netperf_response->response_type != DEBUG_OK) {
    fprintf(stderr,"establish_control: Unknown response to debug check\n");
    exit(1);
  }
  else if(debug)
    fprintf(where,"establish_control: check for connectivity ok\n");

}



 /***********************************************************************/
 /*									*/
 /*	get_id()							*/
 /*									*/
 /* Return a string to the calling routine that contains the		*/
 /* identifying information for the host we are running on. This	*/
 /* information will then either be displayed locally, or returned to	*/
 /* a remote caller for display there.					*/
 /*									*/
 /***********************************************************************/

get_id(id_string)
char *id_string;
{
struct	utsname		system_name;

if (uname(&system_name) <0) {
	perror("identify_local: uname");
	exit(1);
}

sprintf(id_string,
	"%-15s%-15s%-15s%-15s%-15s",
	system_name.sysname,
	system_name.nodename,
	system_name.release,
	system_name.version,
	system_name.machine);
}


 /***********************************************************************/
 /*									*/
 /*	identify_local()						*/
 /*									*/
 /* Display identifying information about the local host to the user.	*/
 /* At first release, this information will be the same as that which	*/
 /* is returned by the uname -a command, with the exception of the	*/
 /* idnumber field, which seems to be a non-POSIX item, and hence	*/
 /* non-portable.							*/
 /*									*/
 /***********************************************************************/

identify_local()
{

char	local_id[80];

get_id(local_id);

fprintf(where,"Local Information \n\
Sysname       Nodename       Release        Version        Machine\n");

fprintf(where,"%s\n",
       local_id);

}


 /***********************************************************************/
 /*									*/
 /*	identify_remote()						*/
 /*									*/
 /* Display identifying information about the remote host to the user.	*/
 /* At first release, this information will be the same as that which	*/
 /* is returned by the uname -a command, with the exception of the	*/
 /* idnumber field, which seems to be a non-POSIX item, and hence	*/
 /* non-portable. A request is sent to the remote side, which will	*/
 /* return a string containing the utsname information in a		*/
 /* pre-formatted form, which is then displayed after the header.	*/
 /*									*/
 /***********************************************************************/

identify_remote()
{

char	*remote_id="";

/* send a request for node info to the remote */
netperf_request->request_type = NODE_IDENTIFY;

send_request();

/* and now wait for the reply to come back */

recv_response();

if (netperf_response->serv_errno) {
	errno = netperf_response->serv_errno;
	perror("identify_remote: on remote");
	exit(1);
}

fprintf(where,"Remote Information \n\
Sysname       Nodename       Release        Version        Machine\n");

/* fprintf(where,"%s",
       remote_id); */
}

cpu_start(measure_cpu)
     int measure_cpu;
{

  int	i;

  gettimeofday(&time1,
	       &tz);
  
  if (measure_cpu) {
    measuring_cpu = 1;
    if (lib_use_idle) {
      cpu_method = LOOPER;
      for (i = 0; i < lib_num_cpus; i++){
	lib_start_count[i] = *lib_idle_address[i];
      }
    }
    else {
#ifdef	USE_PSTAT
      cpu_method = PSTAT;
#ifdef PSTAT_IPCINFO
      /* we need to know if we have the 10.0 pstat interface */
      /* available. I know that at 10.0, the define for PSTAT_IPCINFO */
      /* was added, but that it is not there prior. so, this should */
      /* act as the automagic compile trigger that I need. raj 4/95 */
      cpu_method = HP_IDLE_COUNTER;
      {
	/* get the idle sycle counter for each processor */
	struct pst_processor *psp;
	union overlay_u {
	  long long full;
	  long      word[2];
	} *overlay;

	psp = (struct pst_processor *)malloc(lib_num_cpus * sizeof(*psp));
	if (pstat_getprocessor(psp, sizeof(*psp), lib_num_cpus, 0) != -1) {
	  int i;
	  for (i = 0; i < lib_num_cpus; i++) {
	    overlay = (union overlay_u *)&(lib_start_count[i]);
	    overlay->word[0] = psp[i].psp_idlecycles.psc_hi;
	    overlay->word[1] = psp[i].psp_idlecycles.psc_lo;
	    if(debug) {
	      fprintf(where,
		      "\tlib_start_count[%d] = 0x%8.8x%8.8x\n",
		      i,
		      hi_32(&lib_start_count[i]),
		      lo_32(&lib_start_count[i]));
	      fflush(where);
	    }
	  }
	  free(psp);
	}
      }
#else
      /* this is what we should get when compileing on an HP-UX 9.X */
      /* system. raj 4/95 */
      pstat_getdynamic((struct pst_dynamic *)&pst_dynamic_info,
		       sizeof(pst_dynamic_info),1,0);
      for (i = 0; i < PST_MAX_CPUSTATES; i++) {
	cp_time1[i] = pst_dynamic_info.psd_cpu_time[i];
      }
#endif /* PSTAT_IPCINFO */
#else
      cpu_method = TIMES;
      times(&times_data1);
#endif
    }
  }
}

cpu_stop(measure_cpu,elapsed)
int	measure_cpu;
float	*elapsed;
{
#include <sys/wait.h>
#include <sys/mman.h>

int	sec,
        usec;

int	i;

if (measure_cpu) {
  if (lib_use_idle) {
    for (i = 0; i < lib_num_cpus; i++){
      lib_end_count[i] = *lib_idle_address[i];
    }
    /* now go through and kill-off all the child processes */
    for (i = 0; i < lib_num_cpus; i++){
      kill(lib_idle_pids[i],SIGQUIT);
    }
    /* reap the children */
    while(waitpid(-1, NULL, WNOHANG) > 0) { }

    /* finally, unlink the mmaped file */
    munmap((caddr_t)lib_base_pointer,
	   ((NETPERF_PAGE_SIZE * PAGES_PER_CHILD) * 
	    lib_num_cpus));
    unlink("/tmp/netperf_cpu");
  }
  else {
#ifdef	USE_PSTAT
#ifdef PSTAT_IPCINFO
    {
      struct pst_processor *psp;
      union overlay_u {
	long long full;
	long      word[2];
      } *overlay;
      psp = (struct pst_processor *)malloc(lib_num_cpus * sizeof(*psp));
      if (pstat_getprocessor(psp, sizeof(*psp), lib_num_cpus, 0) != -1) {
	for (i = 0; i < lib_num_cpus; i++) {
	  overlay = (union overlay_u *)&(lib_end_count[i]);
	  overlay->word[0] = psp[i].psp_idlecycles.psc_hi;
	  overlay->word[1] = psp[i].psp_idlecycles.psc_lo;
	  if(debug) {
	    fprintf(where,
		    "\tlib_end_count[%d]   = 0x%8.8x%8.8x\n",
		    i,
		    hi_32(&lib_end_count[i]),
		    lo_32(&lib_end_count[i]));
	    fflush(where);
	  }
	}
	free(psp);
      }
      else {
	fprintf(where,"pstat_getprocessor failure: errno %d\n",errno);
	fflush(where);
	exit(1);
      }
    }
#else
    {
      pstat_getdynamic(&pst_dynamic_info, sizeof(pst_dynamic_info),1,0);
      for (i = 0; i < PST_MAX_CPUSTATES; i++) {
	cp_time2[i] = pst_dynamic_info.psd_cpu_time[i];
      }
    }    
#endif
#else
    times(&times_data2);
#endif
	}
}
gettimeofday(&time2,
	     &tz);

if (time2.tv_usec < time1.tv_usec) {
  time2.tv_usec	+= 1000000;
  time2.tv_sec	-= 1;
}

sec	= time2.tv_sec - time1.tv_sec;
usec	= time2.tv_usec - time1.tv_usec;
lib_elapsed	= (float)sec + ((float)usec/1000000.0);

*elapsed = lib_elapsed;

	
}

double
calc_thruput(units_received)
double units_received;

{
  double	tmp_tput;
  double	divisor;

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
    
  default:
    divisor = 1024.0;
  }
  
  return (units_received / divisor / lib_elapsed);

}

float 
calc_cpu_util(elapsed_time)
     float elapsed_time;
{
  
  float	actual_rate;
  float correction_factor;
  float temp_util;
  int	cpu_time_ticks;
  long	ticks_sec;
  long  diff;
  int	i;
  

  lib_local_cpu_util = 0.0;
  /* It is possible that the library measured a time other than */
  /* the one that the user want for the cpu utilization */
  /* calculations - for example, tests that were ended by */
  /* watchdog timers such as the udp stream test. We let these */
  /* tests tell up what the elapsed time should be. */
  
  if (elapsed_time != 0.0) {
    correction_factor = 1.0 + 
      ((lib_elapsed - elapsed_time) / elapsed_time);
  }
  else {
    correction_factor = 1.0;
  }
  
  if (lib_use_idle) {
    for (i = 0; i < lib_num_cpus; i++) {
      actual_rate = (lib_end_count[i] > lib_start_count[i]) ?
	(float)(lib_end_count[i] - lib_start_count[i])/lib_elapsed :
	  (float)(lib_end_count[i] - lib_start_count[i] +
		  LONG_LONG_MAX)/ lib_elapsed;
      if (debug) {
	fprintf(where,
		"calc_cpu_util: actual_rate on processor %d is %f\n",
		i,
		actual_rate);
      }
      lib_local_cpu_util += (lib_local_maxrate - actual_rate) /
	lib_local_maxrate * 100;
    }
    /* we want the average across all n processors */
    lib_local_cpu_util /= (float)lib_num_cpus;
  }
  else {
#ifdef USE_PSTAT
#ifdef PSTAT_IPCINFO
    {
      /* this looks just like the looper case. at least I think it */
      /* should :) raj 4/95 */
      for (i = 0; i < lib_num_cpus; i++) {
	/* we assume that the two are not more than a long apart. I */
	/* know that this is bad, but trying to go from long longs to */
	/* a float (perhaps a double) is boggling my mind right now. */
	/* raj 4/95 */

	long long 
	  diff;

	if (lib_end_count[i] >= lib_start_count[i]) {
	  diff = lib_end_count[i] - lib_start_count[i];
	}
	else {
	  diff = lib_end_count[i] - lib_start_count[i] + LONG_LONG_MAX;
	}
	actual_rate = (float) diff / lib_elapsed;
	temp_util = (lib_local_maxrate - actual_rate) /
	  lib_local_maxrate * 100;
        lib_local_cpu_util += temp_util;
	if (debug) {
	  fprintf(where,
		  "calc_cpu_util: actual_rate on cpu %d is %f cpu %6.2f\n",
		  i,
		  actual_rate,
                  temp_util);
	}
      }
      /* we want the average across all n processors */
      lib_local_cpu_util /= (float)lib_num_cpus;
    }
#else
    {
      /* we had no idle counter, but there was a pstat. we */
      /* will use the cpu_time_ticks variable for the total */
      /* ticks calculation */
      cpu_time_ticks = 0;
      /* how many ticks were there in our interval? */
      for (i = 0; i < PST_MAX_CPUSTATES; i++) {
	diff = cp_time2[i] - cp_time1[i];
	cpu_time_ticks += diff;
	if (debug) {
	  fprintf(where,
		  "%d cp_time1 %d cp_time2 %d diff %d cpu_time_ticks is %d \n",
		  i,
		  cp_time1[i],
		  cp_time2[i],
		  diff,
		  cpu_time_ticks);
	  fflush(where);
	}
      }
      if (!cpu_time_ticks)
	cpu_time_ticks = 1;
      /* cpu used is 100% minus the idle time - right ?-) */
      lib_local_cpu_util = 1.0 - ( ((float)(cp_time2[CP_IDLE] - 
					    cp_time1[CP_IDLE]))
				  / (float)cpu_time_ticks);
      lib_local_cpu_util *= 100.0;
      if (debug) {
	fprintf(where,
		"calc_cpu_util has cpu_time_ticks at %d\n",cpu_time_ticks);
	fprintf(where,"sysconf ticks is %g\n",
		sysconf(_SC_CLK_TCK) * lib_elapsed);
	fprintf(where,"calc_cpu_util has idle_ticks at %d\n",
		(cp_time2[CP_IDLE] - cp_time1[CP_IDLE]));
	fflush(where);
      }
    }
#endif /* PSTAT_IPCINFO */
#else
    /* we had no kernel idle counter, so use what we can */
    ticks_sec = sysconf(_SC_CLK_TCK);
    cpu_time_ticks = ((times_data2.tms_utime - times_data1.tms_utime) +
		      (times_data2.tms_stime -
		       times_data1.tms_stime));
    
    if (debug) {
      fprintf(where,"calc_cpu_util has cpu_time_ticks at %d\n",cpu_time_ticks);
      fprintf(where,"calc_cpu_util has tick_sec at %d\n",ticks_sec);
      fprintf(where,"calc_cpu_util has lib_elapsed at %f\n",lib_elapsed);
      fflush(where);
    }
    
    lib_local_cpu_util = (float) (((double) (cpu_time_ticks) /
				   (double) ticks_sec /
				   (double) lib_elapsed) *
				  (double) 100.0);
#endif
  }
  lib_local_cpu_util *= correction_factor;
  return lib_local_cpu_util;
}

float calc_service_demand(units_sent,
			  elapsed_time,
			  cpu_utilization)
double	units_sent;
float	elapsed_time;
float	cpu_utilization;

{
  float	unit_divisor = 1024.0;
  float	service_demand;
  float	thruput;
  
  if (debug) {
    fprintf(where,"calc_service_demand called:  units_sent = %f\n",
	    units_sent);
    fprintf(where,"                             elapsed_time = %f\n",
	    elapsed_time);
    fprintf(where,"                             cpu_util = %f\n",
	    cpu_utilization);
    fflush(where);
  }
  
  /* for the first release, the units will remain ms/KB */
  if (elapsed_time == 0.0) {
    elapsed_time = lib_elapsed;
  }
  if (cpu_utilization == 0.0) {
    cpu_utilization = lib_local_cpu_util;
  }
  
  thruput = ((float) units_sent / unit_divisor / elapsed_time);
  service_demand = cpu_utilization*10/thruput;
  
  if (debug) {
    fprintf(where,"calc_service_demand using:   units_sent = %f\n",
	    units_sent);
    fprintf(where,"                             elapsed_time = %f\n",
	    elapsed_time);
    fprintf(where,"                             cpu_util = %f\n",
	    cpu_utilization);
    fprintf(where,"calc_service_demand got:     thruput = %f\n",
	    thruput);
    fprintf(where,"                             servdem = %f\n",
	    service_demand);
    fflush(where);
  }
  
  return service_demand;
}


#ifdef USE_LOOPER
void
start_looper_processes()
{

  unsigned int  
    i,
    file_size;
  
  lib_idle_fd = open("/tmp/netperf_cpu",O_RDWR | O_CREAT | O_EXCL);
  
  if (lib_idle_fd == -1) {
    fprintf(where,"create_looper: file creation; errno %d\n",errno);
    fflush(where);
    exit(1);
  }
  
  if (chmod("/tmp/netperf_cpu",0644) == -1) {
    fprintf(where,"create_looper: chmod; errno %d\n",errno);
    fflush(where);
    exit(1);
  }
  
  /* with the file descriptor in place, lets be sure that the file is */
  /* large enough. we want at least two pages for each processor. the */
  /* child for any one processor will write to the first of his two */
  /* pages, and the second page will be a buffer in case there is page */
  /* prefetching. if your system pre-fetches more than a single page, */
  /* well, you'll have to modify this or live with it :( raj 4/95 */
  
  file_size = ((NETPERF_PAGE_SIZE * PAGES_PER_CHILD) * 
	       lib_num_cpus);
  
  if (truncate("/tmp/netperf_cpu",file_size) == -1) {
    fprintf(where,"create_looper: truncate: errno %d\n",errno);
    fflush(where);
    exit(1);
  }
  
  /* the file should be large enough now, so we can mmap it */
  
  /* if the system does not have MAP_VARIABLE, just define it to */
  /* be zero. it is only used/needed on HP-UX (?) raj 4/95 */
#ifndef MAP_VARIABLE
#define MAP_VARIABLE 0x0000
#endif /* MAP_VARIABLE */
  if ((lib_base_pointer = (long long *)mmap(NULL,
					    file_size,
					    PROT_READ | PROT_WRITE,
					    MAP_FILE | MAP_SHARED | MAP_VARIABLE,
					    lib_idle_fd,
					    0)) == (long long *)-1) {
    fprintf(where,"create_looper: mmap: errno %d\n",errno);
    fflush(where);
    exit(1);
  }
  

  if (debug > 1) {
    fprintf(where,"num CPUs %d, file_size %d, lib_base_pointer 0x%8.0x\n",
	    lib_num_cpus,
	    file_size,
	    lib_base_pointer);
    fflush(where);
  }

  /* we should have a valid base pointer. lets fork */
  
  lib_child_num = -1;
  for (i = 0; i < lib_num_cpus; i++) {
    switch (lib_idle_pids[i] = fork()) {
    case -1:
      perror("netperf: fork");
      exit(1);
    case 0:
      /* we are the child. we could decide to exec some separate */
      /* program, but that doesn't really seem worthwhile - raj 4/95 */
      if (debug > 1) {
	fprintf(where,"Looper child %d is born, pid %d\n",i,getpid());
	fflush(where);
      }
      lib_child_num = i;
      /* reset our base pointer to be at the appropriate offset */
      lib_base_pointer = (long long *) ((char *)lib_base_pointer + 
					(NETPERF_PAGE_SIZE * 
					 PAGES_PER_CHILD * lib_child_num));
      for (*lib_base_pointer = 0LL;
	   ;
	   (*lib_base_pointer)++) {
	if (!(*lib_base_pointer % 1000000)) {
	  /* every once and again, make sure that our process priority is */
	  /* nice and low */
	  nice(39);
	}
      }
      /* we should never really get here, but if we do, just exit(0) */
      exit(0);
      break;
    default:
      /* we must be the parent */
      lib_idle_address[i] = (long long *) ((char *)lib_base_pointer + 
					   (NETPERF_PAGE_SIZE * 
					    PAGES_PER_CHILD * i));
      if (debug) {
	fprintf(where,"lib_idle_address[%d] is %8.8x\n",
		i,
		lib_idle_address[i]);
	fflush(where);
      }
    }
  }

  /* we need to have the looper processes settled-in before we do */
  /* anything with them, so lets sleep for say 30 seconds. raj 4/95 */
  sleep(30);
}

#endif /* USE_LOOPER */


float
calibrate_local_cpu(local_cpu_rate)
     float	local_cpu_rate;
{
  
  int i;

  lib_num_cpus = get_num_cpus();

  lib_use_idle = 0;
#ifdef USE_LOOPER
  /* we want to get the looper processes going */
  start_looper_processes();
  lib_use_idle = 1;
#endif /* USE_LOOPER */

  if (local_cpu_rate > 0) {
    /* The user think that he knows what the cpu rate is. We assume */
    /* that all the processors of an MP system are essentially the */
    /* same - for this reason we do not have a per processor maxrate. */
    /* if the machine has processors which are different in */
    /* performance, the CPU utilization will be skewed. raj 4/95 */
    lib_local_maxrate = local_cpu_rate;
  }
  else {
    /* if neither USE_LOOPER nor USE_PSTAT are defined, we return a */
    /* 0.0 to indicate that times or getrusage should be used. raj */
    /* 4/95 */
    lib_local_maxrate = 0.0;
#ifdef USE_LOOPER    
    lib_local_maxrate = calibrate_looper(4,10);
#endif
#ifdef USE_PSTAT
#ifdef PSTAT_IPCINFO
    /* one version of pstat needs calibration */
    lib_local_maxrate = calibrate_pstat(4,10);
#endif /* PSTAT_IPCINFO */
#endif /* USE_PSTAT */
  }
  return lib_local_maxrate;
}


float
calibrate_remote_cpu()
{
  float	*remrate;

  netperf_request->request_type = CPU_CALIBRATE;
  send_request();
  recv_response();
  if (netperf_response->serv_errno) {
    /* initially, silently ignore remote errors and pass */
    /* back a zero to the caller this should allow us to */
    /* mix rev 1.0 and rev 1.1 netperfs... */
    return(0.0);
  }
  else {
    remrate = (float *)
      netperf_response->test_specific_data;
    return(*remrate);
  }	
}

int
msec_sleep( msecs )
     int msecs;
{

  struct timeval interval;

  interval.tv_sec = msecs / 1000;
  interval.tv_usec = (msecs - (msecs/1000) *1000) * 1000;
  if (select(0,
	     0,
	     0,
	     0,
	     &interval)) {
    if (errno == EINTR) {
      return(1);
    }
    perror("msec_sleep: select");
    exit(1);
  }
  return(0);
}

#ifdef HISTOGRAM
/* hist.c

   Given a time difference in microseconds, increment one of 61
   different buckets: 
   
   0 - 9 in increments of 100 usecs
   1 - 9 in increments of 1 msec
   1 - 9 in increments of 10 msecs
   1 - 9 in increments of 100 msecs
   1 - 9 in increments of 1 sec
   1 - 9 in increments of 10 sec
   > 100 secs
   
   This will allow any time to be recorded to within an accuracy of
   10%, and provides a compact  representation for capturing the
   distribution of a large number of time differences (e.g.
   request-response latencies).
   
   Colin Low  10/6/93
*/

/* #include "sys.h" */

/*#define HIST_TEST*/

HIST HIST_new(void){
   HIST h;
   if((h = (HIST) malloc(sizeof(struct histogram_struct))) == NULL) {
     perror("HIST_new - malloc failed");
     exit(1);
   }
   HIST_clear(h);
   return h;
}

void HIST_clear(HIST h){
   int i;
   for(i = 0; i < 10; i++){
      h->tenth_msec[i] = 0;
      h->unit_msec[i] = 0;
      h->ten_msec[i] = 0;
      h->hundred_msec[i] = 0;
      h->unit_sec[i] = 0;
      h->ten_sec[i] = 0;
   }
   h->ridiculous = 0;
   h->total = 0;
}

void HIST_add(register HIST h, int time_delta){
   register int val;
   h->total++;
   val = time_delta/100;
   if(val <= 9) h->tenth_msec[val]++;
   else {
      val = val/10;
      if(val <= 9) h->unit_msec[val]++;
      else {
         val = val/10;
         if(val <= 9) h->ten_msec[val]++;
         else {
            val = val/10;
            if(val <= 9) h->hundred_msec[val]++;
            else {
               val = val/10;
               if(val <= 9) h->unit_sec[val]++;
               else {
                   val = val/10;
                   if(val <= 9) h->ten_sec[val]++;
                   else h->ridiculous++;
               }
            }
         }
      }
   }
}

#define RB_printf printf

void output_row(FILE *fd, char *title, int *row){
   register int i;
   RB_printf("%s", title);
   for(i = 0; i < 10; i++) RB_printf(": %4d", row[i]);
   RB_printf("\n");
}


void HIST_report(HIST h){
   output_row(stdout, "TENTH_MSEC    ", h->tenth_msec);
   output_row(stdout, "UNIT_MSEC     ", h->unit_msec);
   output_row(stdout, "TEN_MSEC      ", h->ten_msec);
   output_row(stdout, "HUNDRED_MSEC  ", h->hundred_msec);
   output_row(stdout, "UNIT_SEC      ", h->unit_sec);
   output_row(stdout, "TEN_SEC       ", h->ten_sec);
   RB_printf(">100_SECS: %d\n", h->ridiculous);
   RB_printf("HIST_TOTAL:      %d\n", h->total);
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

#endif /* HISTOGRAM */

#ifdef DO_DLPI

int
put_control(fd, len, pri, ack)
     int fd, len, pri, ack;
{
  int error;
  int flags = 0;
  dl_error_ack_t *err_ack = (dl_error_ack_t *)control_data;

  control_message.len = len;

  if ((error = putmsg(fd, &control_message, 0, pri)) < 0 ) {
    fprintf(where,"put_control: putmsg error %d\n",error);
    fflush(where);
    return(-1);
  }
  if ((error = getmsg(fd, &control_message, 0, &flags)) < 0) {
    fprintf(where,"put_control: getsmg error %d\n",error);
    fflush(where);
    return(-1);
  }
  if (err_ack->dl_primitive != ack) {
    fprintf(where,"put_control: acknowledgement error wanted %u got %u \n",
	    ack,err_ack->dl_primitive);
    if (err_ack->dl_primitive == DL_ERROR_ACK) {
      fprintf(where,"             dl_error_primitive: %u\n",
	      err_ack->dl_error_primitive);
      fprintf(where,"             dl_errno:           %u\n",
	      err_ack->dl_errno);
      fprintf(where,"             dl_unix_errno       %u\n",
	      err_ack->dl_unix_errno);
    }
    fflush(where);
    return(-1);
  }

  return(0);
}
    
int
dl_open(devfile,ppa)
     char devfile[];
     int ppa;
     
{
  int fd;
  dl_attach_req_t *attach_req = (dl_attach_req_t *)control_data;

  if ((fd = open(devfile, O_RDWR)) == -1) {
    fprintf(where,"netperf: dl_open: open of %s failed, errno = %d\n",
	    devfile,
	    errno);
    return(-1);
  }

  attach_req->dl_primitive = DL_ATTACH_REQ;
  attach_req->dl_ppa = ppa;

  if (put_control(fd, sizeof(dl_attach_req_t), 0, DL_OK_ACK) < 0) {
    fprintf(where,
	    "netperf: dl_open: could not send control message, errno = %d\n",
	    errno);
    return(-1);
  }
  return(fd);
}

int
dl_bind(fd, sap, mode, dlsap_ptr, dlsap_len)
     int fd, sap, mode;
     char *dlsap_ptr;
     int *dlsap_len;
{
  dl_bind_req_t *bind_req = (dl_bind_req_t *)control_data;
  dl_bind_ack_t *bind_ack = (dl_bind_ack_t *)control_data;

  bind_req->dl_primitive = DL_BIND_REQ;
  bind_req->dl_sap = sap;
  bind_req->dl_max_conind = 1;
  bind_req->dl_service_mode = mode;
  bind_req->dl_conn_mgmt = 0;
  bind_req->dl_xidtest_flg = 0;

  if (put_control(fd, sizeof(dl_bind_req_t), 0, DL_BIND_ACK) < 0) {
    fprintf(where,
	    "netperf: dl_bind: could not send control message, errno = %d\n",
	    errno);
    return(-1);
  }

  /* at this point, the control_data portion of the control message */
  /* structure should contain a DL_BIND_ACK, which will have a full */
  /* DLSAP in it. we want to extract this and pass it up so that    */
  /* it can be passed around. */
  if (*dlsap_len >= bind_ack->dl_addr_length) {
    bcopy((char *)bind_ack+bind_ack->dl_addr_offset,
          dlsap_ptr,
          bind_ack->dl_addr_length);
    *dlsap_len = bind_ack->dl_addr_length;
    return(0);
  }
  else { 
    return (-1); 
  }
}

int
dl_connect(fd, rem_addr, rem_addr_len)
     int fd;
     unsigned char *rem_addr;
     int rem_addr_len;
{
  dl_connect_req_t *connection_req = (dl_connect_req_t *)control_data;
  dl_connect_con_t *connection_con = (dl_connect_con_t *)control_data;
  struct pollfd pinfo;

  int flags = 0;

  /* this is here on the off chance that we really want some data */
  u_long data_area[512];
  struct strbuf data_message;

  int error;

  data_message.maxlen = 2048;
  data_message.len = 0;
  data_message.buf = (char *)data_area;

  connection_req->dl_primitive = DL_CONNECT_REQ;
  connection_req->dl_dest_addr_length = rem_addr_len;
  connection_req->dl_dest_addr_offset = sizeof(dl_connect_req_t);
  connection_req->dl_qos_length = 0;
  connection_req->dl_qos_offset = 0;
  bcopy (rem_addr, 
	 (unsigned char *)control_data + sizeof(dl_connect_req_t),
	 rem_addr_len);

  /* well, I would call the put_control routine here, but the sequence */
  /* of connection stuff with DLPI is a bit screwey with all this */
  /* message passing - Toto, I don't think were in Berkeley anymore. */

  control_message.len = sizeof(dl_connect_req_t) + rem_addr_len;
  if ((error = putmsg(fd,&control_message,0,0)) !=0) {
    fprintf(where,"dl_connect: putmsg failure, errno = %d, error 0x%x \n",
            errno,error);
    fflush(where);
    return(-1);
  };

  pinfo.fd = fd;
  pinfo.events = POLLIN | POLLPRI;
  pinfo.revents = 0;

  if ((error = getmsg(fd,&control_message,&data_message,&flags)) != 0) {
    fprintf(where,"dl_connect: getmsg failure, errno = %d, error 0x%x \n",
            errno,error);
    fflush(where);
    return(-1);
  }
  while (control_data[0] == DL_TEST_CON) {
    /* i suppose we spin until we get an error, or a connection */
    /* indication */
    if((error = getmsg(fd,&control_message,&data_message,&flags)) !=0) {
       fprintf(where,"dl_connect: getmsg failure, errno = %d, error = 0x%x\n",
               errno,error);
       fflush(where);
       return(-1);
    }
  }

  /* we are out - it either worked or it didn't - which was it? */
  if (control_data[0] == DL_CONNECT_CON) {
    return(0);
  }
  else {
    return(-1);
  }
}

int
dl_accept(fd, rem_addr, rem_addr_len)
     int fd;
     unsigned char *rem_addr;
     int rem_addr_len;
{
  dl_connect_ind_t *connect_ind = (dl_connect_ind_t *)control_data;
  dl_connect_res_t *connect_res = (dl_connect_res_t *)control_data;
  int tmp_cor;
  int flags = 0;

  /* hang around and wait for a connection request */
  getmsg(fd,&control_message,0,&flags);
  while (control_data[0] != DL_CONNECT_IND) {
    getmsg(fd,&control_message,0,&flags);
  }

  /* now respond to the request. at some point, we may want to be sure */
  /* that the connection came from the correct station address, but */
  /* will assume that we do not have to worry about it just now. */

  tmp_cor = connect_ind->dl_correlation;

  connect_res->dl_primitive = DL_CONNECT_RES;
  connect_res->dl_correlation = tmp_cor;
  connect_res->dl_resp_token = 0;
  connect_res->dl_qos_length = 0;
  connect_res->dl_qos_offset = 0;
  connect_res->dl_growth = 0;

  return(put_control(fd, sizeof(dl_connect_res_t), 0, DL_OK_ACK));

}

int
dl_set_window(fd, window)
     int fd, window;
{
  return(0);
}

void
dl_stats(fd)
     int fd;
{
}

int
dl_send_disc(fd)
     int fd;
{
}

int
dl_recv_disc(fd)
     int fd;
{
}
#endif /* DO_DLPI*/

 /* these routines for confidence intervals are courtesy of IBM. They */
 /* have been modified slightly for more general usage beyond TCP/UDP */
 /* tests. raj 11/94 I would suspect that this code carries an IBM */
 /* copyright that is much the same as that for the original HP */
 /* netperf code */
int	confidence_iterations; /* for iterations */

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
/*     									*/
/*     	Constants for Confidence Intervals 		             	*/
/*     									*/
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

 /* this routine does a simple table lookup for some statistical */
 /* function that I would remember if I stayed awake in my probstats */
 /* class... raj 11/94 */
double 
confid(level,freedom)
int	level;
int	freedom;
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

double 
calculate_confidence(confidence_iterations,
		     time,
		     result,
		     loc_cpu,
		     rem_cpu,
		     loc_sd,
		     rem_sd)
int	confidence_iterations;
float   time;
double	result;
float	loc_cpu;
float	rem_cpu;
float   loc_sd;
float   rem_sd;
{

  if (debug) {
    fprintf(where,
	    "calculate_confidence: itr  %d; time %f; res  %f\n",
	    confidence_iterations,
	    time,
	    result);
    fprintf(where,
	    "                               lcpu %f; rcpu %f\n",
	    loc_cpu,
	    rem_cpu);
    fprintf(where,
	    "                               lsdm %f; rsdm %f\n",
	    loc_sd,
	    rem_sd);
    fflush(where);
  }

  /* the test time */
  measured_sum_local_time		+= 
    (double) time;
  measured_square_sum_local_time	+= 
    (double) time*time;
  measured_mean_local_time		=  
    (double) measured_sum_local_time/confidence_iterations;
  measured_var_local_time		=  
    (double) measured_square_sum_local_time/confidence_iterations
      -measured_mean_local_time*measured_mean_local_time;
  
  /* the test result */
  measured_sum_result		+= 
    (double) result;
  measured_square_sum_result	+= 
    (double) result*result;
  measured_mean_result		=  
    (double) measured_sum_result/confidence_iterations;
  measured_var_result		=  
    (double) measured_square_sum_result/confidence_iterations
      -measured_mean_result*measured_mean_result;

  /* local cpu utilization */
  measured_sum_local_cpu	+= 
    (double) loc_cpu;
  measured_square_sum_local_cpu	+= 
    (double) loc_cpu*loc_cpu;
  measured_mean_local_cpu	= 
    (double) measured_sum_local_cpu/confidence_iterations;
  measured_var_local_cpu	= 
    (double) measured_square_sum_local_cpu/confidence_iterations
      -measured_mean_local_cpu*measured_mean_local_cpu;

  /* remote cpu util */
  measured_sum_remote_cpu	+=
    (double) rem_cpu;
  measured_square_sum_remote_cpu+=
    (double) rem_cpu*rem_cpu;
  measured_mean_remote_cpu	= 
    (double) measured_sum_remote_cpu/confidence_iterations;
  measured_var_remote_cpu	= 
    (double) measured_square_sum_remote_cpu/confidence_iterations
      -measured_mean_remote_cpu*measured_mean_remote_cpu;

  /* local service demand */
  measured_sum_local_service_demand	+=
    (double) loc_sd;
  measured_square_sum_local_service_demand+=
    (double) loc_sd*loc_sd;
  measured_mean_local_service_demand	= 
    (double) measured_sum_local_service_demand/confidence_iterations;
  measured_var_local_service_demand	= 
    (double) measured_square_sum_local_service_demand/confidence_iterations
      -measured_mean_local_service_demand*measured_mean_local_service_demand;

  /* remote service demand */
  measured_sum_remote_service_demand	+=
    (double) rem_sd;
  measured_square_sum_remote_service_demand+=
    (double) rem_sd*rem_sd;
  measured_mean_remote_service_demand	= 
    (double) measured_sum_remote_service_demand/confidence_iterations;
  measured_var_remote_service_demand	= 
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

     confidence = min(min(result_confid,loc_cpu_confid),rem_cpu_confid);

  }
}

 /* here ends the IBM code */

void
retrieve_confident_values(elapsed_time,
			  thruput,
			  local_cpu_utilization,
			  remote_cpu_utilization,
			  local_service_demand,
			  remote_service_demand)
     float  *elapsed_time;
     double *thruput;
     float  *local_cpu_utilization;
     float  *remote_cpu_utilization;
     float  *local_service_demand;
     float  *remote_service_demand;

{
  *elapsed_time            = measured_mean_local_time;
  *thruput                 = measured_mean_result;
  *local_cpu_utilization   = measured_mean_local_cpu;
  *remote_cpu_utilization  = measured_mean_remote_cpu;
  *local_service_demand    = measured_mean_local_service_demand;
  *remote_service_demand   = measured_mean_remote_service_demand;
}

 /* display_confidence() is called when we could not achieve the */
 /* desirec confidence in the results. it will print the achieved */
 /* confidence to "where" raj 11/94 */
void
display_confidence()

{
  fprintf(where,
	  "!!! WARNING\n");
  fprintf(where,
	  "!!! Desired confidence was not achieved within ");
  fprintf(where,
	  "the specified iterations.\n");
  fprintf(where,
	  "!!! This implies that there was variability in ");
  fprintf(where,
	  "the test environment that\n");
  fprintf(where,
	  "!!! must be investigated before going further.\n");
  fprintf(where,
	  "!!! Confidence intervals: Throughput      : %4.1f%%\n",
	  100.0 * (interval - result_confid));
  fprintf(where,
	  "!!!                       Local CPU util  : %4.1f%%\n",
	  100.0 * (interval - loc_cpu_confid));
  fprintf(where,
	  "!!!                       Remote CPU util : %4.1f%%\n\n",
	  100.0 * (interval - rem_cpu_confid));
}
