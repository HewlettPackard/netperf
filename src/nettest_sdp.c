#ifndef lint
char	nettest_sdp[]="\
@(#)nettest_sdp.c (c) Copyright 2007 Hewlett-Packard Co. Version 2.4.4";
#else
#define DIRTY
#define WANT_HISTOGRAM
#define WANT_INTERVALS
#endif /* lint */

/****************************************************************/
/*								*/
/*	nettest_sdp.c						*/
/*								*/
/*                                                              */
/*      scan_sdp_args()        get the sdp command line args  */
/*                                                              */
/*	the actual test routines...				*/
/*								*/
/*	send_sdp_stream()	perform a sdp stream test	*/
/*	recv_sdp_stream()					*/
/*	send_sdp_rr()		perform a sdp request/response	*/
/*	recv_sdp_rr()						*/
/*								*/
/*      relies on create_data_socket in nettest_bsd.c           */
/****************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(WANT_SDP)
     
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef NOSTDLIBH
#include <malloc.h>
#else /* NOSTDLIBH */
#include <stdlib.h>
#endif /* NOSTDLIBH */

#if !defined(__VMS)
#include <sys/ipc.h>
#endif /* !defined(__VMS) */
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

/* would seem that not all sdp.h files define a MSG_EOF, but that
   MSG_EOF can be the same as MSG_FIN so lets work with that
   assumption.  initial find by Jon Pedersen. raj 2006-02-01 */
#ifndef MSG_EOF
#ifdef MSG_FIN
#define MSG_EOF MSG_FIN
#else
#error Must have either MSG_EOF or MSG_FIN defined
#endif
#endif 

#include "netlib.h"
#include "netsh.h"
/* get some of the functions from nettest_bsd.c */
#include "nettest_bsd.h"
#include "nettest_sdp.h"

#ifdef WANT_HISTOGRAM
#ifdef __sgi
#include <sys/time.h>
#endif /* __sgi */
#include "hist.h"
#endif /* WANT_HISTOGRAM */

#ifdef WANT_FIRST_BURST
extern int first_burst_size;
#endif /* WANT_FIRST_BURST */



/* these variables are specific to SDP tests. declare */
/* them static to make them global only to this file. */

static int	
  msg_count = 0,	/* number of messages to transmit on association */
  non_block = 0,	/* default to blocking sockets */
  num_associations = 1; /* number of associations on the endpoint */

static  int confidence_iteration;
static  char  local_cpu_method;
static  char  remote_cpu_method;

#ifdef WANT_HISTOGRAM
static struct timeval time_one;
static struct timeval time_two;
static HIST time_hist;
#endif /* WANT_HISTOGRAM */


char sdp_usage[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
SDP Sockets Test Options:\n\
    -b number         Send number requests at the start of _RR tests\n\
    -D [L][,R]        Set SDP_NODELAY locally and/or remotely\n\
    -h                Display this text\n\
    -H name,fam       Use name (or IP) and family as target of data connection\n\
    -L name,fam       Use name (or IP) and family as source of data connextion\n\
    -m bytes          Set the size of each sent message\n\
    -M bytes          Set the size of each received messages\n\
    -P local[,remote] Set the local/remote port for the data socket\n\
    -r req,[rsp]      Set request/response sizes (_RR tests)\n\
    -s send[,recv]    Set local socket send/recv buffer sizes\n\
    -S send[,recv]    Set remote socket send/recv buffer sizes\n\
    -V 		      Enable copy avoidance if supported\n\
    -4                Use AF_INET (eg IPv4) on both ends of the data conn\n\
    -6                Use AF_INET6 (eg IPv6) on both ends of the data conn\n\
\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n"; 
     

 /* This routine is intended to retrieve interesting aspects of sdp */
 /* for the data connection. at first, it attempts to retrieve the */
 /* maximum segment size. later, it might be modified to retrieve */
 /* other information, but it must be information that can be */
 /* retrieved quickly as it is called during the timing of the test. */
 /* for that reason, a second routine may be created that can be */
 /* called outside of the timing loop */
static
void
get_sdp_info(int socket, int * mss)
{

#ifdef TCP_MAXSEG
  netperf_socklen_t sock_opt_len;

  sock_opt_len = sizeof(netperf_socklen_t);
  if (getsockopt(socket,
		 getprotobyname("tcp")->p_proto,	
		 TCP_MAXSEG,
		 (char *)mss,
		 &sock_opt_len) == SOCKET_ERROR) {
    fprintf(where,
	    "netperf: get_sdp_info: getsockopt TCP_MAXSEG: errno %d\n",
	    errno);
    fflush(where);
    *mss = -1;
  }
#else
  *mss = -1;
#endif /* TCP_MAXSEG */

}

void 
send_sdp_stream(char remote_host[])
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
%5d   %5d  %5d   %5d %6.4g  %6.2f    %6d   %6.2f %6d\n";

  char *ksink_fmt2 = "\n\
Maximum\n\
Segment\n\
Size (bytes)\n\
%6d\n";
  
  
  float			elapsed_time;
  
  /* what we want is to have a buffer space that is at least one */
  /* send-size greater than our send window. this will insure that we */
  /* are never trying to re-use a buffer that may still be in the hands */
  /* of the transport. This buffer will be malloc'd after we have found */
  /* the size of the local senc socket buffer. We will want to deal */
  /* with alignment and offset concerns as well. */
  
  struct ring_elt *send_ring;
  
  int len;
  unsigned int nummessages = 0;
  SOCKET send_socket;
  int bytes_remaining;
  int sdp_mss = -1;  /* possibly uninitialized on printf far below */

  /* with links like fddi, one can send > 32 bits worth of bytes */
  /* during a test... ;-) at some point, this should probably become a */
  /* 64bit integral type, but those are not entirely common yet */

  unsigned long long local_bytes_sent = 0;
  double	bytes_sent = 0.0;
  
  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;

  double	thruput;
  
  struct addrinfo *remote_res;
  struct addrinfo *local_res;
  
  struct	sdp_stream_request_struct	*sdp_stream_request;
  struct	sdp_stream_response_struct	*sdp_stream_response;
  struct	sdp_stream_results_struct	*sdp_stream_result;
  
  sdp_stream_request  = 
    (struct sdp_stream_request_struct *)netperf_request.content.test_specific_data;
  sdp_stream_response =
    (struct sdp_stream_response_struct *)netperf_response.content.test_specific_data;
  sdp_stream_result   = 
    (struct sdp_stream_results_struct *)netperf_response.content.test_specific_data;
  
#ifdef WANT_HISTOGRAM
  if (verbosity > 1) {
    time_hist = HIST_new();
  }
#endif /* WANT_HISTOGRAM */
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */
  
  /* complete_addrinfos will either succede or exit the process */
  complete_addrinfos(&remote_res,
		     &local_res,
		     remote_host,
		     SOCK_STREAM,
		     IPPROTO_TCP,
		     0);
  
  if ( print_headers ) {
    print_top_test_header("SDP STREAM TEST",local_res,remote_res);
  }

  send_ring = NULL;
  confidence_iteration = 1;
  init_stat();

  /* we have a great-big while loop which controls the number of times */
  /* we run a particular test. this is for the calculation of a */
  /* confidence interval (I really should have stayed awake during */
  /* probstats :). If the user did not request confidence measurement */
  /* (no confidence is the default) then we will only go though the */
  /* loop once. the confidence stuff originates from the folks at IBM */

  while (((confidence < 0) && (confidence_iteration < iteration_max)) ||
	 (confidence_iteration <= iteration_min)) {

    /* initialize a few counters. we have to remember that we might be */
    /* going through the loop more than once. */
    
    nummessages    =	0;
    bytes_sent     =	0.0;
    times_up       = 	0;
    
    /*set up the data socket                        */
    /* fake things out by changing local_res->ai_family to AF_INET_SDP */
    local_res->ai_family = AF_INET_SDP;
    local_res->ai_protocol = 0;
    send_socket = create_data_socket(local_res);
    
    if (send_socket == INVALID_SOCKET){
      perror("netperf: send_sdp_stream: sdp stream data socket");
      exit(1);
    }
    
    if (debug) {
      fprintf(where,"send_sdp_stream: send_socket obtained...\n");
    }
    
    /* at this point, we have either retrieved the socket buffer sizes, */
    /* or have tried to set them, so now, we may want to set the send */
    /* size based on that (because the user either did not use a -m */
    /* option, or used one with an argument of 0). If the socket buffer */
    /* size is not available, we will set the send size to 4KB - no */
    /* particular reason, just arbitrary... */
    if (send_size == 0) {
      if (lss_size > 0) {
	send_size = lss_size;
      }
      else {
	send_size = 4096;
      }
    }
    
    /* set-up the data buffer ring with the requested alignment and offset. */
    /* note also that we have allocated a quantity */
    /* of memory that is at least one send-size greater than our socket */
    /* buffer size. We want to be sure that there are at least two */
    /* buffers allocated - this can be a bit of a problem when the */
    /* send_size is bigger than the socket size, so we must check... the */
    /* user may have wanted to explicitly set the "width" of our send */
    /* buffers, we should respect that wish... */
    if (send_width == 0) {
      send_width = (lss_size/send_size) + 1;
      if (send_width == 1) send_width++;
    }
    
    if (send_ring == NULL) {
      /* only allocate the send ring once. this is a networking test, */
      /* not a memory allocation test. this way, we do not need a */
      /* deallocate_buffer_ring() routine, and I don't feel like */
      /* writing one anyway :) raj 11/94 */
      send_ring = allocate_buffer_ring(send_width,
				       send_size,
				       local_send_align,
				       local_send_offset);
    }

    /* If the user has requested cpu utilization measurements, we must */
    /* calibrate the cpu(s). We will perform this task within the tests */
    /* themselves. If the user has specified the cpu rate, then */
    /* calibrate_local_cpu will return rather quickly as it will have */
    /* nothing to do. If local_cpu_rate is zero, then we will go through */
    /* all the "normal" calibration stuff and return the rate back. */
    
    if (local_cpu_usage) {
      local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
    }
    
    if (!no_control) {
      /* Tell the remote end to do a listen. The server alters the
	 socket paramters on the other side at this point, hence the
	 reason for all the values being passed in the setup
	 message. If the user did not specify any of the parameters,
	 they will be passed as 0, which will indicate to the remote
	 that no changes beyond the system's default should be
	 used. Alignment is the exception, it will default to 1, which
	 will be no alignment alterations. */
    
      netperf_request.content.request_type =	DO_SDP_STREAM;
      sdp_stream_request->send_buf_size	=	rss_size_req;
      sdp_stream_request->recv_buf_size	=	rsr_size_req;
      sdp_stream_request->receive_size	=	recv_size;
      sdp_stream_request->no_delay	=	rem_nodelay;
      sdp_stream_request->recv_alignment	=	remote_recv_align;
      sdp_stream_request->recv_offset	=	remote_recv_offset;
      sdp_stream_request->measure_cpu	=	remote_cpu_usage;
      sdp_stream_request->cpu_rate	=	remote_cpu_rate;
      if (test_time) {
	sdp_stream_request->test_length	=	test_time;
      }
      else {
	sdp_stream_request->test_length	=	test_bytes;
      }
      sdp_stream_request->so_rcvavoid	=	rem_rcvavoid;
      sdp_stream_request->so_sndavoid	=	rem_sndavoid;
#ifdef DIRTY
      sdp_stream_request->dirty_count     =       rem_dirty_count;
      sdp_stream_request->clean_count     =       rem_clean_count;
#endif /* DIRTY */
      sdp_stream_request->port            =    atoi(remote_data_port);
      sdp_stream_request->ipfamily = af_to_nf(remote_res->ai_family);
      if (debug > 1) {
	fprintf(where,
		"netperf: send_sdp_stream: requesting SDP stream test\n");
      }
      
      send_request();
      
      /* The response from the remote will contain all of the relevant
         socket parameters for this test type. We will put them back
         into the variables here so they can be displayed if desired.
         The remote will have calibrated CPU if necessary, and will
         have done all the needed set-up we will have calibrated the
         cpu locally before sending the request, and will grab the
         counter value right after the connect returns. The remote
         will grab the counter right after the accept call. This saves
         the hassle of extra messages being sent for the SDP
         tests.  */
    
      recv_response();
    
      if (!netperf_response.content.serv_errno) {
	if (debug)
	  fprintf(where,"remote listen done.\n");
	rsr_size	      =	sdp_stream_response->recv_buf_size;
	rss_size	      =	sdp_stream_response->send_buf_size;
	rem_nodelay     =	sdp_stream_response->no_delay;
	remote_cpu_usage=	sdp_stream_response->measure_cpu;
	remote_cpu_rate = sdp_stream_response->cpu_rate;
	
	/* we have to make sure that the server port number is in
	   network order */
	set_port_number(remote_res,
			(short)sdp_stream_response->data_port_number);
	
	rem_rcvavoid	= sdp_stream_response->so_rcvavoid;
	rem_sndavoid	= sdp_stream_response->so_sndavoid;
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

#ifdef WANT_DEMO
    DEMO_STREAM_SETUP(lss_size,rsr_size)
#endif

    /*Connect up to the remote port on the data socket  */
    if (connect(send_socket, 
		remote_res->ai_addr,
		remote_res->ai_addrlen) == INVALID_SOCKET){
      perror("netperf: send_sdp_stream: data socket connect failed");
      exit(1);
    }

    /* Data Socket set-up is finished. If there were problems, either */
    /* the connect would have failed, or the previous response would */
    /* have indicated a problem. I failed to see the value of the */
    /* extra  message after the accept on the remote. If it failed, */
    /* we'll see it here. If it didn't, we might as well start pumping */
    /* data. */ 
    
    /* Set-up the test end conditions. For a stream test, they can be */
    /* either time or byte-count based. */
    
    if (test_time) {
      /* The user wanted to end the test after a period of time. */
      times_up = 0;
      bytes_remaining = 0;
      /* in previous revisions, we had the same code repeated throught */
      /* all the test suites. this was unnecessary, and meant more */
      /* work for me when I wanted to switch to POSIX signals, so I */
      /* have abstracted this out into a routine in netlib.c. if you */
      /* are experiencing signal problems, you might want to look */
      /* there. raj 11/94 */
      start_timer(test_time);
    }
    else {
      /* The tester wanted to send a number of bytes. */
      bytes_remaining = test_bytes;
      times_up = 1;
    }
    
    /* The cpu_start routine will grab the current time and possibly */
    /* value of the idle counter for later use in measuring cpu */
    /* utilization and/or service demand and thruput. */
    
    cpu_start(local_cpu_usage);

    /* we only start the interval timer if we are using the
       timer-timed intervals rather than the sit and spin ones. raj
       2006-02-06 */    
#if defined(WANT_INTERVALS)
    INTERVALS_INIT();
#endif /* WANT_INTERVALS */

    /* before we start, initialize a few variables */

#ifdef WANT_DEMO
      if (demo_mode) {
	HIST_timestamp(demo_one_ptr);
      }
#endif
      

    /* We use an "OR" to control test execution. When the test is */
    /* controlled by time, the byte count check will always return false. */
    /* When the test is controlled by byte count, the time test will */
    /* always return false. When the test is finished, the whole */
    /* expression will go false and we will stop sending data. */
    
    while ((!times_up) || (bytes_remaining > 0)) {
      
#ifdef DIRTY
      access_buffer(send_ring->buffer_ptr,
		    send_size,
		    loc_dirty_count,
		    loc_clean_count);
#endif /* DIRTY */
      
#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	/* timestamp just before we go into send and then again just
	 after we come out raj 8/94 */
	/* but lets only do this if there is going to be a histogram
	   displayed */
	HIST_timestamp(&time_one);
      }
#endif /* WANT_HISTOGRAM */

      if((len=send(send_socket,
		   send_ring->buffer_ptr,
		   send_size,
		   0)) != send_size) {
      if ((len >=0) || SOCKET_EINTR(len)) {
	    /* the test was interrupted, must be the end of test */
	    break;
	  }
	perror("netperf: data send error");
	printf("len was %d\n",len);
	exit(1);
      }

      local_bytes_sent += send_size;

#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	/* timestamp the exit from the send call and update the histogram */
	HIST_timestamp(&time_two);
	HIST_add(time_hist,delta_micro(&time_one,&time_two));
      }
#endif /* WANT_HISTOGRAM */      

#ifdef WANT_DEMO
      DEMO_STREAM_INTERVAL(send_size)
#endif 

#if defined(WANT_INTERVALS)
      INTERVALS_WAIT();
#endif /* WANT_INTERVALS */
      
      /* now we want to move our pointer to the next position in the */
      /* data buffer...we may also want to wrap back to the "beginning" */
      /* of the bufferspace, so we will mod the number of messages sent */
      /* by the send width, and use that to calculate the offset to add */
      /* to the base pointer. */
      nummessages++;          
      send_ring = send_ring->next;
      if (bytes_remaining) {
	bytes_remaining -= send_size;
      }
    }

    /* The test is over. Flush the buffers to the remote end. We do a */
    /* graceful release to insure that all data has been taken by the */
    /* remote. */ 

    /* but first, if the verbosity is greater than 1, find-out what */
    /* the SDP maximum segment_size was (if possible) */
    if (verbosity > 1) {
      sdp_mss = -1;
      get_sdp_info(send_socket,&sdp_mss);
    }
    
    if (shutdown(send_socket,SHUT_WR) == SOCKET_ERROR) {
      perror("netperf: cannot shutdown sdp stream socket");
      exit(1);
    }
    
    /* hang a recv() off the socket to block until the remote has */
    /* brought all the data up into the application. it will do a */
    /* shutdown to cause a FIN to be sent our way. We will assume that */
    /* any exit from the recv() call is good... raj 4/93 */
    
    recv(send_socket, send_ring->buffer_ptr, send_size, 0);
    
    /* this call will always give us the elapsed time for the test, and */
    /* will also store-away the necessaries for cpu utilization */
    
    cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being */
						/* measured and how */
						/* long did we really */
						/* run? */
    
    /* we are finished with the socket, so close it to prevent hitting */
    /* the limit on maximum open files. */

    close(send_socket);

    if (!no_control) {
      /* Get the statistics from the remote end. The remote will have
	 calculated service demand and all those interesting
	 things. If it wasn't supposed to care, it will return obvious
	 values. */
    
      recv_response();
      if (!netperf_response.content.serv_errno) {
	if (debug)
	  fprintf(where,"remote results obtained\n");
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
    
      /* We now calculate what our thruput was for the test. In the
	 future, we may want to include a calculation of the thruput
	 measured by the remote, but it should be the case that for a
	 SDP stream test, that the two numbers should be *very*
	 close... We calculate bytes_sent regardless of the way the
	 test length was controlled.  If it was time, we needed to,
	 and if it was by bytes, the user may have specified a number
	 of bytes that wasn't a multiple of the send_size, so we
	 really didn't send what he asked for ;-) */
    
      bytes_sent	= ntohd(sdp_stream_result->bytes_received);
    }
    else {
      bytes_sent = (double)local_bytes_sent;
    }

    thruput	= calc_thruput(bytes_sent);
    
    if (local_cpu_usage || remote_cpu_usage) {
      /* We must now do a little math for service demand and cpu */
      /* utilization for the system(s) */
      /* Of course, some of the information might be bogus because */
      /* there was no idle counter in the kernel(s). We need to make */
      /* a note of this for the user's benefit...*/
      if (local_cpu_usage) {
	
	local_cpu_utilization	= calc_cpu_util(0.0);
	local_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      0.0,
						      0);
      }
      else {
	local_cpu_utilization	= (float) -1.0;
	local_service_demand	= (float) -1.0;
      }
      
      if (remote_cpu_usage) {
	
	remote_cpu_utilization	= sdp_stream_result->cpu_util;
	remote_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      remote_cpu_utilization,
						      sdp_stream_result->num_cpus);
      }
      else {
	remote_cpu_utilization = (float) -1.0;
	remote_service_demand  = (float) -1.0;
      }
    }    
    else {
      /* we were not measuring cpu, for the confidence stuff, we */
      /* should make it -1.0 */
      local_cpu_utilization	= (float) -1.0;
      local_service_demand	= (float) -1.0;
      remote_cpu_utilization = (float) -1.0;
      remote_service_demand  = (float) -1.0;
    }

    /* at this point, we want to calculate the confidence information. */
    /* if debugging is on, calculate_confidence will print-out the */
    /* parameters we pass it */
    
    calculate_confidence(confidence_iteration,
			 elapsed_time,
			 thruput,
			 local_cpu_utilization,
			 remote_cpu_utilization,
			 local_service_demand,
			 remote_service_demand);
    
    
    confidence_iteration++;
  }

  /* at this point, we have finished making all the runs that we */
  /* will be making. so, we should extract what the calcuated values */
  /* are for all the confidence stuff. we could make the values */
  /* global, but that seemed a little messy, and it did not seem worth */
  /* all the mucking with header files. so, we create a routine much */
  /* like calcualte_confidence, which just returns the mean values. */
  /* raj 11/94 */

  retrieve_confident_values(&elapsed_time,
			    &thruput,
			    &local_cpu_utilization,
			    &remote_cpu_utilization,
			    &local_service_demand,
			    &remote_service_demand);

  /* We are now ready to print all the information. If the user */
  /* has specified zero-level verbosity, we will just print the */
  /* local service demand, or the remote service demand. If the */
  /* user has requested verbosity level 1, he will get the basic */
  /* "streamperf" numbers. If the user has specified a verbosity */
  /* of greater than 1, we will display a veritable plethora of */
  /* background information from outside of this block as it it */
  /* not cpu_measurement specific...  */

  if (confidence < 0) {
    /* we did not hit confidence, but were we asked to look for it? */
    if (iteration_max > 1) {
      display_confidence();
    }
  }

  if (local_cpu_usage || remote_cpu_usage) {
    local_cpu_method = format_cpu_method(cpu_method);
    remote_cpu_method = format_cpu_method(sdp_stream_result->cpu_method);
    
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
	      rsr_size,		        /* remote recvbuf size */
	      lss_size,		        /* local sendbuf size */
	      send_size,		/* how large were the sends */
	      elapsed_time,		/* how long was the test */
	      thruput, 		        /* what was the xfer rate */
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
	      send_size,		/* how large were the sends */
	      elapsed_time, 		/* how long did it take */
	      thruput,                  /* how fast did it go */
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
    /* SDP statistics, the alignments of the sends and receives */
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
	    bytes_sent / (double)nummessages,
	    nummessages,
	    bytes_sent / (double)sdp_stream_result->recv_calls,
	    sdp_stream_result->recv_calls);
    fprintf(where,
	    ksink_fmt2,
	    sdp_mss);
    fflush(where);
#ifdef WANT_HISTOGRAM
    fprintf(where,"\n\nHistogram of time spent in send() call.\n");
    fflush(where);
    HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
  }
  
}



/* This routine implements the netperf-side SDP unidirectional data
   transfer test (a.k.a. stream) for the sockets interface where the
   data flow is from the netserver to the netperf.  It receives its
   parameters via global variables from the shell and writes its
   output to the standard output. */


void 
send_sdp_maerts(char remote_host[])
{
  
  char *tput_title = "\
Recv   Send    Send                          \n\
Socket Socket  Message  Elapsed              \n\
Size   Size    Size     Time     Throughput  \n\
bytes  bytes   bytes    secs.    %s/sec  \n\n";
  
  char *tput_fmt_0 =
    "%7.2f %s\n";
  
  char *tput_fmt_1 =
    "%6d %6d %6d    %-6.2f   %7.2f   \n %s";
  
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
Alignment      Offset         %-8.8s %-8.8s    Recvs   %-8.8s Sends\n\
Local  Remote  Local  Remote  Xfered   Per                 Per\n\
Recv   Send    Recv   Send             Recv (avg)          Send (avg)\n\
%5d   %5d  %5d   %5d %6.4g  %6.2f    %6d   %6.2f %6d\n";

  char *ksink_fmt2 = "\n\
Maximum\n\
Segment\n\
Size (bytes)\n\
%6d\n";
  
  
  float			elapsed_time;
  
  /* what we want is to have a buffer space that is at least one */
  /* recv-size greater than our recv window. this will insure that we */
  /* are never trying to re-use a buffer that may still be in the hands */
  /* of the transport. This buffer will be malloc'd after we have found */
  /* the size of the local senc socket buffer. We will want to deal */
  /* with alignment and offset concerns as well. */
  
  struct ring_elt *recv_ring;
  
  int len;
  unsigned int nummessages = 0;
  SOCKET recv_socket;
  int bytes_remaining;
  int sdp_mss = -1;  /* possibly uninitialized on printf far below */

  /* with links like fddi, one can recv > 32 bits worth of bytes */
  /* during a test... ;-) at some point, this should probably become a */
  /* 64bit integral type, but those are not entirely common yet */
  double	bytes_sent = 0.0;
  unsigned long long local_bytes_recvd = 0;

  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;

  double	thruput;
  
  struct addrinfo *remote_res;
  struct addrinfo *local_res;
  
  struct	sdp_maerts_request_struct	*sdp_maerts_request;
  struct	sdp_maerts_response_struct	*sdp_maerts_response;
  struct	sdp_maerts_results_struct	*sdp_maerts_result;
  
  sdp_maerts_request  = 
    (struct sdp_maerts_request_struct *)netperf_request.content.test_specific_data;
  sdp_maerts_response =
    (struct sdp_maerts_response_struct *)netperf_response.content.test_specific_data;
  sdp_maerts_result   = 
    (struct sdp_maerts_results_struct *)netperf_response.content.test_specific_data;
  
#ifdef WANT_HISTOGRAM
  if (verbosity > 1) {
    time_hist = HIST_new();
  }
#endif /* WANT_HISTOGRAM */
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */
  
  complete_addrinfos(&remote_res,
		     &local_res,
		     remote_host,
		     SOCK_STREAM,
		     IPPROTO_TCP,
		     0);
  
  if ( print_headers ) {
    print_top_test_header("SDP MAERTS TEST",local_res,remote_res);
  }

  recv_ring = NULL;
  confidence_iteration = 1;
  init_stat();

  /* we have a great-big while loop which controls the number of times */
  /* we run a particular test. this is for the calculation of a */
  /* confidence interval (I really should have stayed awake during */
  /* probstats :). If the user did not request confidence measurement */
  /* (no confidence is the default) then we will only go though the */
  /* loop once. the confidence stuff originates from the folks at IBM */

  while (((confidence < 0) && (confidence_iteration < iteration_max)) ||
	 (confidence_iteration <= iteration_min)) {

    /* initialize a few counters. we have to remember that we might be */
    /* going through the loop more than once. */
    
    nummessages    =	0;
    bytes_sent     =	0.0;
    times_up       = 	0;
    
    /*set up the data socket                        */
    /* fake things out by changing local_res->ai_family to AF_INET_SDP */
    local_res->ai_family = AF_INET_SDP;
    local_res->ai_protocol = 0;
    recv_socket = create_data_socket(local_res);
    
    if (recv_socket == INVALID_SOCKET){
      perror("netperf: send_sdp_maerts: sdp stream data socket");
      exit(1);
    }
    
    if (debug) {
      fprintf(where,"send_sdp_maerts: recv_socket obtained...\n");
    }

    /* at this point, we have either retrieved the socket buffer sizes, */
    /* or have tried to set them, so now, we may want to set the recv */
    /* size based on that (because the user either did not use a -m */
    /* option, or used one with an argument of 0). If the socket buffer */
    /* size is not available, we will set the recv size to 4KB - no */
    /* particular reason, just arbitrary... */
    if (recv_size == 0) {
      if (lsr_size > 0) {
	recv_size = lsr_size;
      }
      else {
	recv_size = 4096;
      }
    }

    /* set-up the data buffer ring with the requested alignment and offset. */
    /* note also that we have allocated a quantity */
    /* of memory that is at least one recv-size greater than our socket */
    /* buffer size. We want to be sure that there are at least two */
    /* buffers allocated - this can be a bit of a problem when the */
    /* recv_size is bigger than the socket size, so we must check... the */
    /* user may have wanted to explicitly set the "width" of our recv */
    /* buffers, we should respect that wish... */
    if (recv_width == 0) {
      recv_width = (lsr_size/recv_size) + 1;
      if (recv_width == 1) recv_width++;
    }
    
    if (recv_ring == NULL) {
      /* only allocate the recv ring once. this is a networking test, */
      /* not a memory allocation test. this way, we do not need a */
      /* deallocate_buffer_ring() routine, and I don't feel like */
      /* writing one anyway :) raj 11/94 */
      recv_ring = allocate_buffer_ring(recv_width,
				       recv_size,
				       local_recv_align,
				       local_recv_offset);
    }

    /* If the user has requested cpu utilization measurements, we must */
    /* calibrate the cpu(s). We will perform this task within the tests */
    /* themselves. If the user has specified the cpu rate, then */
    /* calibrate_local_cpu will return rather quickly as it will have */
    /* nothing to do. If local_cpu_rate is zero, then we will go through */
    /* all the "normal" calibration stuff and return the rate back. */
    
    if (local_cpu_usage) {
      local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
    }
    
    if (!no_control) {
      /* Tell the remote end to do a listen. The server alters the
	 socket paramters on the other side at this point, hence the
	 reason for all the values being passed in the setup
	 message. If the user did not specify any of the parameters,
	 they will be passed as 0, which will indicate to the remote
	 that no changes beyond the system's default should be
	 used. Alignment is the exception, it will default to 1, which
	 will be no alignment alterations. */

      netperf_request.content.request_type	=	DO_SDP_MAERTS;
      sdp_maerts_request->send_buf_size	=	rss_size_req;
      sdp_maerts_request->recv_buf_size	=	rsr_size_req;
      sdp_maerts_request->send_size	=	send_size;
      sdp_maerts_request->no_delay	=	rem_nodelay;
      sdp_maerts_request->send_alignment	=	remote_send_align;
      sdp_maerts_request->send_offset	=	remote_send_offset;
      sdp_maerts_request->measure_cpu	=	remote_cpu_usage;
      sdp_maerts_request->cpu_rate	=	remote_cpu_rate;
      if (test_time) {
	sdp_maerts_request->test_length	=	test_time;
      }
      else {
	sdp_maerts_request->test_length	=	test_bytes;
      }
      sdp_maerts_request->so_rcvavoid	=	rem_rcvavoid;
      sdp_maerts_request->so_sndavoid	=	rem_sndavoid;
#ifdef DIRTY
      sdp_maerts_request->dirty_count       =       rem_dirty_count;
      sdp_maerts_request->clean_count       =       rem_clean_count;
#endif /* DIRTY */
      sdp_maerts_request->port            = atoi(remote_data_port);
      sdp_maerts_request->ipfamily        = af_to_nf(remote_res->ai_family);
      if (debug > 1) {
	fprintf(where,
		"netperf: send_sdp_maerts: requesting SDP maerts test\n");
      }
      
      send_request();
      
      /* The response from the remote will contain all of the relevant
	 socket parameters for this test type. We will put them back
	 into the variables here so they can be displayed if desired.
	 The remote will have calibrated CPU if necessary, and will
	 have done all the needed set-up we will have calibrated the
	 cpu locally before sending the request, and will grab the
	 counter value right after the connect returns. The remote
	 will grab the counter right after the accept call. This saves
	 the hassle of extra messages being sent for the SDP
	 tests.  */
      
      recv_response();
    
      if (!netperf_response.content.serv_errno) {
	if (debug)
	  fprintf(where,"remote listen done.\n");
	rsr_size	=	sdp_maerts_response->recv_buf_size;
	rss_size	=	sdp_maerts_response->send_buf_size;
	rem_nodelay     =	sdp_maerts_response->no_delay;
	remote_cpu_usage=	sdp_maerts_response->measure_cpu;
	remote_cpu_rate = sdp_maerts_response->cpu_rate;
	send_size       = sdp_maerts_response->send_size;
	
	/* we have to make sure that the server port number is in
	 network order */
      set_port_number(remote_res,
		      (short)sdp_maerts_response->data_port_number);
      rem_rcvavoid	= sdp_maerts_response->so_rcvavoid;
      rem_sndavoid	= sdp_maerts_response->so_sndavoid;
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

#ifdef WANT_DEMO
    DEMO_STREAM_SETUP(lsr_size,rss_size)
#endif

    /*Connect up to the remote port on the data socket  */
    if (connect(recv_socket, 
		remote_res->ai_addr,
		remote_res->ai_addrlen) == INVALID_SOCKET){
      perror("netperf: send_sdp_maerts: data socket connect failed");
      exit(1);
    }

    /* Data Socket set-up is finished. If there were problems, either */
    /* the connect would have failed, or the previous response would */
    /* have indicated a problem. I failed to see the value of the */
    /* extra  message after the accept on the remote. If it failed, */
    /* we'll see it here. If it didn't, we might as well start pumping */
    /* data. */ 
    
    /* Set-up the test end conditions. For a maerts test, they can be */
    /* either time or byte-count based. */
    
    if (test_time) {
      /* The user wanted to end the test after a period of time. */
      times_up = 0;
      bytes_remaining = 0;
      /* in previous revisions, we had the same code repeated throught */
      /* all the test suites. this was unnecessary, and meant more */
      /* work for me when I wanted to switch to POSIX signals, so I */
      /* have abstracted this out into a routine in netlib.c. if you */
      /* are experiencing signal problems, you might want to look */
      /* there. raj 11/94 */
      if (!no_control) {
	/* this is a netperf to netserver test, netserver will close
	   to tell us the test is over, so use PAD_TIME to avoid
	   causing the netserver fits. */
	start_timer(test_time + PAD_TIME);
      }
      else {
	/* this is a netperf to data source test, no PAD_TIME */
	start_timer(test_time);
      }
    }
    else {
      /* The tester wanted to recv a number of bytes. we don't do that 
	 in a SDP_MAERTS test. sorry. raj 2002-06-21 */
      printf("netperf: send_sdp_maerts: test must be timed\n");
      exit(1);
    }
    
    /* The cpu_start routine will grab the current time and possibly */
    /* value of the idle counter for later use in measuring cpu */
    /* utilization and/or service demand and thruput. */
    
    cpu_start(local_cpu_usage);
    
#ifdef WANT_INTERVALS
    INTERVALS_INIT();
#endif /* WANT_INTERVALS */

    /* before we start, initialize a few variables */

#ifdef WANT_DEMO
    if (demo_mode) {
      HIST_timestamp(demo_one_ptr);
    }
#endif

    /* the test will continue until we either get a zero-byte recv()
       on the socket or our failsafe timer expires. most of the time
       we trust that we get a zero-byte recieve from the socket. raj
       2002-06-21 */

#ifdef WANT_HISTOGRAM
    if (verbosity > 1) {
      /* timestamp just before we go into recv and then again just
	 after we come out raj 8/94 */
      /* but only if we are actually going to display a histogram. raj
	 2006-02-07 */
      HIST_timestamp(&time_one);
    }
#endif /* WANT_HISTOGRAM */
    
    while ((!times_up) && (len=recv(recv_socket,
				    recv_ring->buffer_ptr,
				    recv_size,
				    0)) > 0 ) {

#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	/* timestamp the exit from the recv call and update the histogram */
	HIST_timestamp(&time_two);
	HIST_add(time_hist,delta_micro(&time_one,&time_two));
      }
#endif /* WANT_HISTOGRAM */      

#ifdef DIRTY
      access_buffer(recv_ring->buffer_ptr,
		    recv_size,
		    loc_dirty_count,
		    loc_clean_count);
#endif /* DIRTY */

#ifdef WANT_DEMO
      DEMO_STREAM_INTERVAL(len);
#endif

#ifdef WANT_INTERVALS      
      INTERVALS_WAIT();
#endif /* WANT_INTERVALS */
      
      /* now we want to move our pointer to the next position in the */
      /* data buffer...we may also want to wrap back to the "beginning" */
      /* of the bufferspace, so we will mod the number of messages sent */
      /* by the recv width, and use that to calculate the offset to add */
      /* to the base pointer. */
      nummessages++;          
      recv_ring = recv_ring->next;
      if (bytes_remaining) {
	bytes_remaining -= len;
      }

      local_bytes_recvd += len;

#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	/* make sure we timestamp just before we go into recv  */
	/* raj 2004-06-15 */
	HIST_timestamp(&time_one);
      }
#endif /* WANT_HISTOGRAM */
    
    }

    /* an EINTR is to be expected when this is a no_control test */
    if (((len < 0) || SOCKET_EINTR(len)) && (!no_control)) {
      perror("send_sdp_maerts: data recv error");
      printf("len was %d\n",len);
      exit(1);
    }
    
    /* if we get here, it must mean we had a recv return of 0 before
       the watchdog timer expired, or the watchdog timer expired and
       this was a no_control test */

    /* The test is over. Flush the buffers to the remote end. We do a
       graceful release to tell the  remote we have all the data. */  

    /* but first, if the verbosity is greater than 1, find-out what */
    /* the SDP maximum segment_size was (if possible) */
    if (verbosity > 1) {
      sdp_mss = -1;
      get_sdp_info(recv_socket,&sdp_mss);
    }
    
    if (shutdown(recv_socket,SHUT_WR) == SOCKET_ERROR) {
      perror("netperf: cannot shutdown sdp maerts socket");
      exit(1);
    }

    stop_timer();
    
    /* this call will always give us the local elapsed time for the
       test, and will also store-away the necessaries for cpu
       utilization */ 
    
    cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being */
						/* measured and how */
						/* long did we really */
						/* run? */
    
    /* we are finished with the socket, so close it to prevent hitting */
    /* the limit on maximum open files. */

    close(recv_socket);

    if (!no_control) {
      /* Get the statistics from the remote end. The remote will have
         calculated service demand and all those interesting
         things. If it wasn't supposed to care, it will return obvious
         values. */
    
      recv_response();
      if (!netperf_response.content.serv_errno) {
	if (debug)
	  fprintf(where,"remote results obtained\n");
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
      
      /* We now calculate what our thruput was for the test. In the
	 future, we may want to include a calculation of the thruput
	 measured by the remote, but it should be the case that for a
	 SDP maerts test, that the two numbers should be *very*
	 close... We calculate bytes_sent regardless of the way the
	 test length was controlled.  If it was time, we needed to,
	 and if it was by bytes, the user may have specified a number
	 of bytes that wasn't a multiple of the recv_size, so we
	 really didn't recv what he asked for ;-) */
    
      bytes_sent	= ntohd(sdp_maerts_result->bytes_sent);
    }
    else {
      bytes_sent = (double)local_bytes_recvd;
    }


    thruput	= calc_thruput(bytes_sent);

    if (local_cpu_usage || remote_cpu_usage) {
      /* We must now do a little math for service demand and cpu */
      /* utilization for the system(s) */
      /* Of course, some of the information might be bogus because */
      /* there was no idle counter in the kernel(s). We need to make */
      /* a note of this for the user's benefit...*/
      if (local_cpu_usage) {
	
	local_cpu_utilization	= calc_cpu_util(0.0);
	local_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      0.0,
						      0);
      }
      else {
	local_cpu_utilization	= (float) -1.0;
	local_service_demand	= (float) -1.0;
      }
      
      if (remote_cpu_usage) {
	
	remote_cpu_utilization	= sdp_maerts_result->cpu_util;
	remote_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      remote_cpu_utilization,
						      sdp_maerts_result->num_cpus);
      }
      else {
	remote_cpu_utilization = (float) -1.0;
	remote_service_demand  = (float) -1.0;
      }
    }    
    else {
      /* we were not measuring cpu, for the confidence stuff, we */
      /* should make it -1.0 */
      local_cpu_utilization	= (float) -1.0;
      local_service_demand	= (float) -1.0;
      remote_cpu_utilization = (float) -1.0;
      remote_service_demand  = (float) -1.0;
    }

    /* at this point, we want to calculate the confidence information. */
    /* if debugging is on, calculate_confidence will print-out the */
    /* parameters we pass it */

    calculate_confidence(confidence_iteration,
			 elapsed_time,
			 thruput,
			 local_cpu_utilization,
			 remote_cpu_utilization,
			 local_service_demand,
			 remote_service_demand);
    
    
    confidence_iteration++;
  }

  /* at this point, we have finished making all the runs that we */
  /* will be making. so, we should extract what the calcuated values */
  /* are for all the confidence stuff. we could make the values */
  /* global, but that seemed a little messy, and it did not seem worth */
  /* all the mucking with header files. so, we create a routine much */
  /* like calcualte_confidence, which just returns the mean values. */
  /* raj 11/94 */

  retrieve_confident_values(&elapsed_time,
			    &thruput,
			    &local_cpu_utilization,
			    &remote_cpu_utilization,
			    &local_service_demand,
			    &remote_service_demand);

  /* We are now ready to print all the information. If the user */
  /* has specified zero-level verbosity, we will just print the */
  /* local service demand, or the remote service demand. If the */
  /* user has requested verbosity level 1, he will get the basic */
  /* "streamperf" numbers. If the user has specified a verbosity */
  /* of greater than 1, we will display a veritable plethora of */
  /* background information from outside of this block as it it */
  /* not cpu_measurement specific...  */

  if (confidence < 0) {
    /* we did not hit confidence, but were we asked to look for it? */
    if (iteration_max > 1) {
      display_confidence();
    }
  }

  if (local_cpu_usage || remote_cpu_usage) {
    local_cpu_method = format_cpu_method(cpu_method);
    remote_cpu_method = format_cpu_method(sdp_maerts_result->cpu_method);
    
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
	      rsr_size,		        /* remote recvbuf size */
	      lss_size,		        /* local sendbuf size */
	      send_size,		/* how large were the recvs */
	      elapsed_time,		/* how long was the test */
	      thruput, 		        /* what was the xfer rate */
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
	      send_size,		/* how large were the recvs */
	      elapsed_time, 		/* how long did it take */
	      thruput,                  /* how fast did it go */
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
    /* SDP statistics, the alignments of the sends and receives */
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
	    bytes_sent,
	    bytes_sent / (double)nummessages,
	    nummessages,
	    bytes_sent / (double)sdp_maerts_result->send_calls,
	    sdp_maerts_result->send_calls);
    fprintf(where,
	    ksink_fmt2,
	    sdp_mss);
    fflush(where);
#ifdef WANT_HISTOGRAM
    fprintf(where,"\n\nHistogram of time spent in recv() call.\n");
    fflush(where);
    HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
  }
  
}
/* This is the server-side routine for the sdp stream test. It is */
/* implemented as one routine. I could break things-out somewhat, but */
/* didn't feel it was necessary. */

void
recv_sdp_stream()
{
  
  struct sockaddr_in myaddr_in, peeraddr_in;
  SOCKET s_listen,s_data;
  netperf_socklen_t addrlen;
  int	len;
  unsigned int	receive_calls;
  float	elapsed_time;
  double   bytes_received;
  
  struct ring_elt *recv_ring;

  struct addrinfo *local_res;
  char local_name[BUFSIZ];
  char port_buffer[PORTBUFSIZE];

#ifdef DO_SELECT
  fd_set readfds;
  struct timeval timeout;
#endif /* DO_SELECT */

  struct	sdp_stream_request_struct	*sdp_stream_request;
  struct	sdp_stream_response_struct	*sdp_stream_response;
  struct	sdp_stream_results_struct	*sdp_stream_results;
  
#ifdef DO_SELECT
  FD_ZERO(&readfds);
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
#endif /* DO_SELECT */

  sdp_stream_request	= 
    (struct sdp_stream_request_struct *)netperf_request.content.test_specific_data;
  sdp_stream_response	= 
    (struct sdp_stream_response_struct *)netperf_response.content.test_specific_data;
  sdp_stream_results	= 
    (struct sdp_stream_results_struct *)netperf_response.content.test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_sdp_stream: entered...\n");
    fflush(where);
  }
  
  /* We want to set-up the listen socket with all the desired */
  /* parameters and then let the initiator know that all is ready. If */
  /* socket size defaults are to be used, then the initiator will have */
  /* sent us 0's. If the socket sizes cannot be changed, then we will */
  /* send-back what they are. If that information cannot be determined, */
  /* then we send-back -1's for the sizes. If things go wrong for any */
  /* reason, we will drop back ten yards and punt. */
  
  /* If anything goes wrong, we want the remote to know about it. It */
  /* would be best if the error that the remote reports to the user is */
  /* the actual error we encountered, rather than some bogus unexpected */
  /* response type message. */
  
  if (debug) {
    fprintf(where,"recv_sdp_stream: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response.content.response_type = SDP_STREAM_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_sdp_stream: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */
  
  if (debug) {
    fprintf(where,"recv_sdp_stream: requested alignment of %d\n",
	    sdp_stream_request->recv_alignment);
    fflush(where);
  }

  /* create_data_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = sdp_stream_request->send_buf_size;
  lsr_size_req = sdp_stream_request->recv_buf_size;
  loc_nodelay  = sdp_stream_request->no_delay;
  loc_rcvavoid = sdp_stream_request->so_rcvavoid;
  loc_sndavoid = sdp_stream_request->so_sndavoid;

  set_hostname_and_port(local_name,
			port_buffer,
			nf_to_af(sdp_stream_request->ipfamily),
			sdp_stream_request->port);

  local_res = complete_addrinfo(local_name,
				local_name,
				port_buffer,
				nf_to_af(sdp_stream_request->ipfamily),
				SOCK_STREAM,
				IPPROTO_TCP,
				0);

  /* fake things out by changing local_res->ai_family to AF_INET_SDP */
  local_res->ai_family = AF_INET_SDP;
  local_res->ai_protocol = 0;
  s_listen = create_data_socket(local_res);
  
  if (s_listen == INVALID_SOCKET) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }
  
#ifdef WIN32
  /* The test timer can fire during operations on the listening socket,
     so to make the start_timer below work we have to move
     it to close s_listen while we are blocked on accept. */
  win_kludge_socket2 = s_listen;
#endif
  
  /* what sort of sizes did we end-up with? */
  if (sdp_stream_request->receive_size == 0) {
    if (lsr_size > 0) {
      recv_size = lsr_size;
    }
    else {
      recv_size = 4096;
    }
  }
  else {
    recv_size = sdp_stream_request->receive_size;
  }
  
  /* we want to set-up our recv_ring in a manner analagous to what we */
  /* do on the sending side. this is more for the sake of symmetry */
  /* than for the needs of say copy avoidance, but it might also be */
  /* more realistic - this way one could conceivably go with a */
  /* double-buffering scheme when taking the data an putting it into */
  /* the filesystem or something like that. raj 7/94 */

  if (recv_width == 0) {
    recv_width = (lsr_size/recv_size) + 1;
    if (recv_width == 1) recv_width++;
  }

  recv_ring = allocate_buffer_ring(recv_width,
				   recv_size,
				   sdp_stream_request->recv_alignment,
				   sdp_stream_request->recv_offset);

  if (debug) {
    fprintf(where,"recv_sdp_stream: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == SOCKET_ERROR) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen, 
		  (struct sockaddr *)&myaddr_in,
		  &addrlen) == SOCKET_ERROR){
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */
  
  sdp_stream_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response.content.serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */
  
  sdp_stream_response->cpu_rate = (float)0.0; 	/* assume no cpu */
  if (sdp_stream_request->measure_cpu) {
    sdp_stream_response->measure_cpu = 1;
    sdp_stream_response->cpu_rate = 
      calibrate_local_cpu(sdp_stream_request->cpu_rate);
  }
  else {
    sdp_stream_response->measure_cpu = 0;
  }
  
  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  sdp_stream_response->send_buf_size = lss_size;
  sdp_stream_response->recv_buf_size = lsr_size;
  sdp_stream_response->no_delay = loc_nodelay;
  sdp_stream_response->so_rcvavoid = loc_rcvavoid;
  sdp_stream_response->so_sndavoid = loc_sndavoid;
  sdp_stream_response->receive_size = recv_size;

  send_response();
  
  addrlen = sizeof(peeraddr_in);
  
  if ((s_data=accept(s_listen,
		     (struct sockaddr *)&peeraddr_in,
		     &addrlen)) == INVALID_SOCKET) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    exit(1);
  }

#ifdef KLUDGE_SOCKET_OPTIONS
  /* this is for those systems which *INCORRECTLY* fail to pass */
  /* attributes across an accept() call. Including this goes against */
  /* my better judgement :( raj 11/95 */

  kludge_socket_options(s_data);

#endif /* KLUDGE_SOCKET_OPTIONS */
  
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(sdp_stream_request->measure_cpu);
  
  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */
  
  /* there used to be an #ifdef DIRTY call to access_buffer() here,
     but we have switched from accessing the buffer before the recv()
     call to accessing the buffer after the recv() call.  The
     accessing before was, IIRC, related to having dirty data when
     doing page-flipping copy avoidance. */

  bytes_received = 0;
  receive_calls  = 0;

  while ((len = recv(s_data, recv_ring->buffer_ptr, recv_size, 0)) != 0) {
    if (len == SOCKET_ERROR )
	{
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
    }
    bytes_received += len;
    receive_calls++;

#ifdef DIRTY
    /* we access the buffer after the recv() call now, rather than before */
    access_buffer(recv_ring->buffer_ptr,
		  recv_size,
		  sdp_stream_request->dirty_count,
		  sdp_stream_request->clean_count);
#endif /* DIRTY */


    /* move to the next buffer in the recv_ring */
    recv_ring = recv_ring->next;

#ifdef PAUSE
    sleep(1);
#endif /* PAUSE */

#ifdef DO_SELECT
	FD_SET(s_data,&readfds);
	select(s_data+1,&readfds,NULL,NULL,&timeout);
#endif /* DO_SELECT */

  }
  
  /* perform a shutdown to signal the sender that */
  /* we have received all the data sent. raj 4/93 */

  if (shutdown(s_data,SHUT_WR) == SOCKET_ERROR) {
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
    }
  
  cpu_stop(sdp_stream_request->measure_cpu,&elapsed_time);
  
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_sdp_stream: got %g bytes\n",
	    bytes_received);
    fprintf(where,
	    "recv_sdp_stream: got %d recvs\n",
	    receive_calls);
    fflush(where);
  }
  
  sdp_stream_results->bytes_received	= htond(bytes_received);
  sdp_stream_results->elapsed_time	= elapsed_time;
  sdp_stream_results->recv_calls	= receive_calls;
  
  sdp_stream_results->cpu_method = cpu_method;
  sdp_stream_results->num_cpus   = lib_num_loc_cpus;
  
  if (sdp_stream_request->measure_cpu) {
    sdp_stream_results->cpu_util	= calc_cpu_util(0.0);
  };
  
  if (debug) {
    fprintf(where,
	    "recv_sdp_stream: test complete, sending results.\n");
    fprintf(where,
	    "                 bytes_received %g receive_calls %d\n",
	    bytes_received,
	    receive_calls);
    fprintf(where,
	    "                 len %d\n",
	    len);
    fflush(where);
  }

  send_response();

  /* we are now done with the sockets */
  close(s_data);
  close(s_listen);

  }

/* This is the server-side routine for the sdp maerts test. It is
   implemented as one routine. I could break things-out somewhat, but
   didn't feel it was necessary. */ 

void
recv_sdp_maerts()
{
  
  struct sockaddr_in myaddr_in, peeraddr_in;
  struct addrinfo *local_res;
  char  local_name[BUFSIZ];
  char  port_buffer[PORTBUFSIZE];

  SOCKET	s_listen,s_data;
  netperf_socklen_t 	addrlen;
  int	len;
  unsigned int	send_calls;
  float	elapsed_time;
  double   bytes_sent = 0.0 ;
  
  struct ring_elt *send_ring;

  struct	sdp_maerts_request_struct	*sdp_maerts_request;
  struct	sdp_maerts_response_struct	*sdp_maerts_response;
  struct	sdp_maerts_results_struct	*sdp_maerts_results;
  
  sdp_maerts_request	= 
    (struct sdp_maerts_request_struct *)netperf_request.content.test_specific_data;
  sdp_maerts_response	= 
    (struct sdp_maerts_response_struct *)netperf_response.content.test_specific_data;
  sdp_maerts_results	= 
    (struct sdp_maerts_results_struct *)netperf_response.content.test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_sdp_maerts: entered...\n");
    fflush(where);
  }
  
  /* We want to set-up the listen socket with all the desired
     parameters and then let the initiator know that all is ready. If
     socket size defaults are to be used, then the initiator will have
     sent us 0's. If the socket sizes cannot be changed, then we will
     send-back what they are. If that information cannot be
     determined, then we send-back -1's for the sizes. If things go
     wrong for any reason, we will drop back ten yards and punt. */
  
  /* If anything goes wrong, we want the remote to know about it. It
     would be best if the error that the remote reports to the user is
     the actual error we encountered, rather than some bogus
     unexpected response type message. */
  
  if (debug) {
    fprintf(where,"recv_sdp_maerts: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response.content.response_type = SDP_MAERTS_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_sdp_maerts: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */
  
  if (debug) {
    fprintf(where,"recv_sdp_maerts: requested alignment of %d\n",
	    sdp_maerts_request->send_alignment);
    fflush(where);
  }

  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_sdp_maerts: grabbing a socket...\n");
    fflush(where);
  }
  
  /* create_data_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = sdp_maerts_request->send_buf_size;
  lsr_size_req = sdp_maerts_request->recv_buf_size;
  loc_nodelay = sdp_maerts_request->no_delay;
  loc_rcvavoid = sdp_maerts_request->so_rcvavoid;
  loc_sndavoid = sdp_maerts_request->so_sndavoid;

  set_hostname_and_port(local_name,
			port_buffer,
			nf_to_af(sdp_maerts_request->ipfamily),
			sdp_maerts_request->port);

  local_res = complete_addrinfo(local_name,
				local_name,
				port_buffer,
				nf_to_af(sdp_maerts_request->ipfamily),
				SOCK_STREAM,
				IPPROTO_TCP,
				0);

  /* fake things out by changing local_res->ai_family to AF_INET_SDP */
  local_res->ai_family = AF_INET_SDP;
  local_res->ai_protocol = 0;
  s_listen = create_data_socket(local_res);
  
  if (s_listen == INVALID_SOCKET) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }
  
#ifdef WIN32
  /* The test timer can fire during operations on the listening socket,
     so to make the start_timer below work we have to move
     it to close s_listen while we are blocked on accept. */
  win_kludge_socket2 = s_listen;
#endif

  
  /* what sort of sizes did we end-up with? */
  if (sdp_maerts_request->send_size == 0) {
    if (lss_size > 0) {
      send_size = lss_size;
    }
    else {
      send_size = 4096;
    }
  }
  else {
    send_size = sdp_maerts_request->send_size;
  }
  
  /* we want to set-up our recv_ring in a manner analagous to what we */
  /* do on the recving side. this is more for the sake of symmetry */
  /* than for the needs of say copy avoidance, but it might also be */
  /* more realistic - this way one could conceivably go with a */
  /* double-buffering scheme when taking the data an putting it into */
  /* the filesystem or something like that. raj 7/94 */

  if (send_width == 0) {
    send_width = (lsr_size/send_size) + 1;
    if (send_width == 1) send_width++;
  }

  send_ring = allocate_buffer_ring(send_width,
				   send_size,
				   sdp_maerts_request->send_alignment,
				   sdp_maerts_request->send_offset);

  if (debug) {
    fprintf(where,"recv_sdp_maerts: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == SOCKET_ERROR) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen, 
		  (struct sockaddr *)&myaddr_in,
		  &addrlen) == SOCKET_ERROR){
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */
  
  sdp_maerts_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response.content.serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */
  
  sdp_maerts_response->cpu_rate = (float)0.0; 	/* assume no cpu */
  if (sdp_maerts_request->measure_cpu) {
    sdp_maerts_response->measure_cpu = 1;
    sdp_maerts_response->cpu_rate = 
      calibrate_local_cpu(sdp_maerts_request->cpu_rate);
  }
  else {
    sdp_maerts_response->measure_cpu = 0;
  }
  
  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  sdp_maerts_response->send_buf_size = lss_size;
  sdp_maerts_response->recv_buf_size = lsr_size;
  sdp_maerts_response->no_delay = loc_nodelay;
  sdp_maerts_response->so_rcvavoid = loc_rcvavoid;
  sdp_maerts_response->so_sndavoid = loc_sndavoid;
  sdp_maerts_response->send_size = send_size;

  send_response();
  
  addrlen = sizeof(peeraddr_in);

  /* we will start the timer before the accept() to be somewhat
     analagous to the starting of the timer before the connect() call
     in the SDP_STREAM test. raj 2002-06-21 */

  start_timer(sdp_maerts_request->test_length);

  /* Now it's time to start receiving data on the connection. We will
     first grab the apropriate counters and then start grabbing. */
  
  cpu_start(sdp_maerts_request->measure_cpu);
  

  if ((s_data=accept(s_listen,
		     (struct sockaddr *)&peeraddr_in,
		     &addrlen)) == INVALID_SOCKET) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    exit(1);
  }

#ifdef KLUDGE_SOCKET_OPTIONS
  
  /* this is for those systems which *INCORRECTLY* fail to pass
     attributes across an accept() call. Including this goes against
     my better judgement :( raj 11/95 */

  kludge_socket_options(s_data);

#endif /* KLUDGE_SOCKET_OPTIONS */
  
  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */
  
  bytes_sent = 0.0;
  send_calls  = 0;

  len = 0;   /* nt-lint; len is not initialized (printf far below) if
		times_up initially true.*/
  times_up = 0; /* must remember to initialize this little beauty */
  while (!times_up) {

#ifdef DIRTY
    /* we want to dirty some number of consecutive integers in the buffer */
    /* we are about to send. we may also want to bring some number of */
    /* them cleanly into the cache. The clean ones will follow any dirty */
    /* ones into the cache. */

  access_buffer(send_ring->buffer_ptr,
		send_size,
		sdp_maerts_request->dirty_count,
		sdp_maerts_request->clean_count);

#endif /* DIRTY */

    if((len=send(s_data,
		 send_ring->buffer_ptr,
		 send_size,
		 0)) != send_size) {
		if ((len >=0) || SOCKET_EINTR(len)) {
	      /* the test was interrupted, must be the end of test */
	      break;
		}
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
    }

    bytes_sent += len;
    send_calls++;

    /* more to the next buffer in the send_ring */
    send_ring = send_ring->next;

  }
  
  /* perform a shutdown to signal the sender that */
  /* we have received all the data sent. raj 4/93 */

  if (shutdown(s_data,SHUT_WR) == SOCKET_ERROR) {
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
    }

  /* hang a recv() off the socket to block until the remote has
     brought all the data up into the application. it will do a
     shutdown to cause a FIN to be sent our way. We will assume that
     any exit from the recv() call is good... raj 4/93 */
    
  recv(s_data, send_ring->buffer_ptr, send_size, 0);
    
  
  cpu_stop(sdp_maerts_request->measure_cpu,&elapsed_time);
  
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_sdp_maerts: got %g bytes\n",
	    bytes_sent);
    fprintf(where,
	    "recv_sdp_maerts: got %d sends\n",
	    send_calls);
    fflush(where);
  }
  
  sdp_maerts_results->bytes_sent	= htond(bytes_sent);
  sdp_maerts_results->elapsed_time	= elapsed_time;
  sdp_maerts_results->send_calls	= send_calls;
  
  if (sdp_maerts_request->measure_cpu) {
    sdp_maerts_results->cpu_util	= calc_cpu_util(0.0);
  };
  
  if (debug) {
    fprintf(where,
	    "recv_sdp_maerts: test complete, sending results.\n");
    fprintf(where,
	    "                 bytes_sent %g send_calls %d\n",
	    bytes_sent,
	    send_calls);
    fprintf(where,
	    "                 len %d\n",
	    len);
    fflush(where);
  }
  
  sdp_maerts_results->cpu_method = cpu_method;
  sdp_maerts_results->num_cpus   = lib_num_loc_cpus;
  send_response();

  /* we are now done with the sockets */
  close(s_data);
  close(s_listen);

  }


 /* this routine implements the sending (netperf) side of the SDP_RR */
 /* test. */

void
send_sdp_rr(char remote_host[])
{
  
  char *tput_title = "\
Local /Remote\n\
Socket Size   Request  Resp.   Elapsed  Trans.\n\
Send   Recv   Size     Size    Time     Rate         \n\
bytes  Bytes  bytes    bytes   secs.    per sec   \n\n";
  
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
  
  char *cpu_fmt_0 =
    "%6.3f %c %s\n";
  
  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f  %-6.2f %-6.2f %-6.3f  %-6.3f %s\n";
  
  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";
  
  char *ksink_fmt = "\
Alignment      Offset\n\
Local  Remote  Local  Remote\n\
Send   Recv    Send   Recv\n\
%5d  %5d   %5d  %5d\n";
  
  
  int			timed_out = 0;
  float			elapsed_time;
  
  int	len;
  char	*temp_message_ptr;
  int	nummessages;
  SOCKET	send_socket;
  int	trans_remaining;
  double	bytes_xferd;

  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;
  
  int	rsp_bytes_left;
  int	rsp_bytes_recvd;
  
  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;
  double	thruput;
  
  struct addrinfo *local_res;
  struct addrinfo *remote_res;

  struct	sdp_rr_request_struct	*sdp_rr_request;
  struct	sdp_rr_response_struct	*sdp_rr_response;
  struct	sdp_rr_results_struct	*sdp_rr_result;

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
     of SDP.  ain't that grand?-)  raj 2006-01-30 */
  int requests_outstanding = 0;
  int request_cwnd = REQUEST_CWND_INITIAL;  /* we ass-u-me that having
					       three requests
					       outstanding at the
					       beginning of the test
					       is ok with SDP stacks
					       of interest. the first
					       two will come from our
					       first_burst loop, and
					       the third from our
					       regularly scheduled
					       send */
#endif

  sdp_rr_request = 
    (struct sdp_rr_request_struct *)netperf_request.content.test_specific_data;
  sdp_rr_response=
    (struct sdp_rr_response_struct *)netperf_response.content.test_specific_data;
  sdp_rr_result	=
    (struct sdp_rr_results_struct *)netperf_response.content.test_specific_data;
  
#ifdef WANT_HISTOGRAM
  if (verbosity > 1) {
    time_hist = HIST_new();
  }
#endif /* WANT_HISTOGRAM */

  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */

  complete_addrinfos(&remote_res,
		     &local_res,
		     remote_host,
		     SOCK_STREAM,
		     IPPROTO_TCP,
		     0);

  if ( print_headers ) {
    print_top_test_header("SDP REQUEST/RESPONSE TEST",local_res,remote_res);
  }
  
  /* initialize a few counters */
  
  send_ring = NULL;
  recv_ring = NULL;
  confidence_iteration = 1;
  init_stat();

  /* we have a great-big while loop which controls the number of times */
  /* we run a particular test. this is for the calculation of a */
  /* confidence interval (I really should have stayed awake during */
  /* probstats :). If the user did not request confidence measurement */
  /* (no confidence is the default) then we will only go though the */
  /* loop once. the confidence stuff originates from the folks at IBM */

  while (((confidence < 0) && (confidence_iteration < iteration_max)) ||
	 (confidence_iteration <= iteration_min)) {

    /* initialize a few counters. we have to remember that we might be */
    /* going through the loop more than once. */

    nummessages     = 0;
    bytes_xferd     = 0.0;
    times_up        = 0;
    timed_out       = 0;
    trans_remaining = 0;

#ifdef WANT_FIRST_BURST
    /* we have to remember to reset the number of transactions
       outstanding and the "congestion window for each new
       iteration. raj 2006-01-31 */
    requests_outstanding = 0;
    request_cwnd = REQUEST_CWND_INITIAL;
#endif


    /* set-up the data buffers with the requested alignment and offset. */
    /* since this is a request/response test, default the send_width and */
    /* recv_width to 1 and not two raj 7/94 */

    if (send_width == 0) send_width = 1;
    if (recv_width == 0) recv_width = 1;
  
    if (send_ring == NULL) {
      send_ring = allocate_buffer_ring(send_width,
				       req_size,
				       local_send_align,
				       local_send_offset);
    }

    if (recv_ring == NULL) {
      recv_ring = allocate_buffer_ring(recv_width,
				       rsp_size,
				       local_recv_align,
				       local_recv_offset);
    }
    
    /*set up the data socket                        */
    /* fake things out by changing local_res->ai_family to AF_INET_SDP */
    local_res->ai_family = AF_INET_SDP;
    local_res->ai_protocol = 0;
    send_socket = create_data_socket(local_res);
  
    if (send_socket == INVALID_SOCKET){
      perror("netperf: send_sdp_rr: sdp stream data socket");
      exit(1);
    }
    
    if (debug) {
      fprintf(where,"send_sdp_rr: send_socket obtained...\n");
    }
  
    /* If the user has requested cpu utilization measurements, we must */
    /* calibrate the cpu(s). We will perform this task within the tests */
    /* themselves. If the user has specified the cpu rate, then */
    /* calibrate_local_cpu will return rather quickly as it will have */
    /* nothing to do. If local_cpu_rate is zero, then we will go through */
    /* all the "normal" calibration stuff and return the rate back.*/
    
    if (local_cpu_usage) {
      local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
    }
    
    if (!no_control) {
      /* Tell the remote end to do a listen. The server alters the
	 socket paramters on the other side at this point, hence the
	 reason for all the values being passed in the setup
	 message. If the user did not specify any of the parameters,
	 they will be passed as 0, which will indicate to the remote
	 that no changes beyond the system's default should be
	 used. Alignment is the exception, it will default to 8, which
	 will be no alignment alterations. */
    
      netperf_request.content.request_type	=	DO_SDP_RR;
      sdp_rr_request->recv_buf_size	=	rsr_size_req;
      sdp_rr_request->send_buf_size	=	rss_size_req;
      sdp_rr_request->recv_alignment    =	remote_recv_align;
      sdp_rr_request->recv_offset	=	remote_recv_offset;
      sdp_rr_request->send_alignment    =	remote_send_align;
      sdp_rr_request->send_offset	=	remote_send_offset;
      sdp_rr_request->request_size	=	req_size;
      sdp_rr_request->response_size	=	rsp_size;
      sdp_rr_request->no_delay	        =	rem_nodelay;
      sdp_rr_request->measure_cpu	=	remote_cpu_usage;
      sdp_rr_request->cpu_rate	        =	remote_cpu_rate;
      sdp_rr_request->so_rcvavoid	=	rem_rcvavoid;
      sdp_rr_request->so_sndavoid	=	rem_sndavoid;
      if (test_time) {
	sdp_rr_request->test_length	=	test_time;
      }
      else {
	sdp_rr_request->test_length	=	test_trans * -1;
      }
      sdp_rr_request->port              =      atoi(remote_data_port);
      sdp_rr_request->ipfamily = af_to_nf(remote_res->ai_family);
      
      if (debug > 1) {
	fprintf(where,"netperf: send_sdp_rr: requesting SDP rr test\n");
      }
      
      send_request();
      
      /* The response from the remote will contain all of the relevant
	 socket parameters for this test type. We will put them back
	 into the variables here so they can be displayed if desired.
	 The remote will have calibrated CPU if necessary, and will
	 have done all the needed set-up we will have calibrated the
	 cpu locally before sending the request, and will grab the
	 counter value right after the connect returns. The remote
	 will grab the counter right after the accept call. This saves
	 the hassle of extra messages being sent for the SDP
	 tests.  */
  
      recv_response();
  
      if (!netperf_response.content.serv_errno) {
	if (debug)
	  fprintf(where,"remote listen done.\n");
	rsr_size          = sdp_rr_response->recv_buf_size;
	rss_size          = sdp_rr_response->send_buf_size;
	rem_nodelay       = sdp_rr_response->no_delay;
	remote_cpu_usage  = sdp_rr_response->measure_cpu;
	remote_cpu_rate   = sdp_rr_response->cpu_rate;
	/* make sure that port numbers are in network order */
	set_port_number(remote_res,(short)sdp_rr_response->data_port_number);
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

#ifdef WANT_DEMO
    DEMO_RR_SETUP(1000)
#endif

    /*Connect up to the remote port on the data socket  */
    if (connect(send_socket, 
		remote_res->ai_addr,
		remote_res->ai_addrlen) == INVALID_SOCKET){
      perror("netperf: data socket connect failed");
      
      exit(1);
    }
    
    /* Data Socket set-up is finished. If there were problems, either the */
    /* connect would have failed, or the previous response would have */
    /* indicated a problem. I failed to see the value of the extra */
    /* message after the accept on the remote. If it failed, we'll see it */
    /* here. If it didn't, we might as well start pumping data. */
    
    /* Set-up the test end conditions. For a request/response test, they */
    /* can be either time or transaction based. */
    
    if (test_time) {
      /* The user wanted to end the test after a period of time. */
      times_up = 0;
      trans_remaining = 0;
      start_timer(test_time);
    }
    else {
      /* The tester wanted to send a number of bytes. */
      trans_remaining = test_bytes;
      times_up = 1;
    }
    
    /* The cpu_start routine will grab the current time and possibly */
    /* value of the idle counter for later use in measuring cpu */
    /* utilization and/or service demand and thruput. */
    
    cpu_start(local_cpu_usage);

#ifdef WANT_INTERVALS
    INTERVALS_INIT();
#endif /* WANT_INTERVALS */
    
    /* We use an "OR" to control test execution. When the test is */
    /* controlled by time, the byte count check will always return false. */
    /* When the test is controlled by byte count, the time test will */
    /* always return false. When the test is finished, the whole */
    /* expression will go false and we will stop sending data. I think I */
    /* just arbitrarily decrement trans_remaining for the timed test, but */
    /* will not do that just yet... One other question is whether or not */
    /* the send buffer and the receive buffer should be the same buffer. */

#ifdef WANT_DEMO
      if (demo_mode) {
	HIST_timestamp(demo_one_ptr);
      }
#endif

    while ((!times_up) || (trans_remaining > 0)) {
      /* send the request. we assume that if we use a blocking socket, */
      /* the request will be sent at one shot. */

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
      while ((first_burst_size > 0) &&
	     (requests_outstanding < request_cwnd) &&
	     (requests_outstanding < first_burst_size)) {
	if (debug) {
	  fprintf(where,
		  "injecting, req_outstndng %d req_cwnd %d burst %d\n",
		  requests_outstanding,
		  request_cwnd,
		  first_burst_size);
	}
	if ((len = send(send_socket,
			send_ring->buffer_ptr,
			req_size,
			0)) != req_size) {
	  /* we should never hit the end of the test in the first burst */
	  perror("send_sdp_rr: initial burst data send error");
	  exit(-1);
	}
	requests_outstanding += 1;
      }

#endif /* WANT_FIRST_BURST */
      
#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	/* timestamp just before our call to send, and then again just
	   after the receive raj 8/94 */
	/* but only if we are actually going to display one. raj
	   2007-02-07 */

	HIST_timestamp(&time_one);
      }
#endif /* WANT_HISTOGRAM */
      
      if ((len = send(send_socket,
		      send_ring->buffer_ptr,
		      req_size,
		      0)) != req_size) {
	if (SOCKET_EINTR(len) || (errno == 0)) {
	  /* we hit the end of a */
	  /* timed test. */
	  timed_out = 1;
	  break;
	}
	perror("send_sdp_rr: data send error");
	exit(1);
      }
      send_ring = send_ring->next;

#ifdef WANT_FIRST_BURST
      requests_outstanding += 1;
#endif

      /* receive the response */
      rsp_bytes_left = rsp_size;
      temp_message_ptr  = recv_ring->buffer_ptr;
      while(rsp_bytes_left > 0) {
	if((rsp_bytes_recvd=recv(send_socket,
				 temp_message_ptr,
				 rsp_bytes_left,
				 0)) == SOCKET_ERROR) {
		if ( SOCKET_EINTR(rsp_bytes_recvd) ) {
		    /* We hit the end of a timed test. */
			timed_out = 1;
			break;
		}
	  perror("send_sdp_rr: data recv error");
	  exit(1);
	}
	rsp_bytes_left -= rsp_bytes_recvd;
	temp_message_ptr  += rsp_bytes_recvd;
      }	
      recv_ring = recv_ring->next;
      
#ifdef WANT_FIRST_BURST
      /* so, since we've gotten a response back, update the
	 bookkeeping accordingly.  there is one less request
	 outstanding and we can put one more out there than before. */
      requests_outstanding -= 1;
      if (request_cwnd < first_burst_size) {
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
      if (timed_out) {
	/* we may have been in a nested while loop - we need */
	/* another call to break. */
	break;
      }
      
#ifdef WANT_HISTOGRAM
      if (verbosity > 1) {
	HIST_timestamp(&time_two);
	HIST_add(time_hist,delta_micro(&time_one,&time_two));
      }
#endif /* WANT_HISTOGRAM */

#ifdef WANT_DEMO
      DEMO_RR_INTERVAL(1);
#endif

#ifdef WANT_INTERVALS      
      INTERVALS_WAIT();
#endif /* WANT_INTERVALS */
      
      nummessages++;          
      if (trans_remaining) {
	trans_remaining--;
      }
      
      if (debug > 3) {
	if ((nummessages % 100) == 0) {
	  fprintf(where,
		  "Transaction %d completed\n",
		  nummessages);
	  fflush(where);
	}
      }
    }

    /* At this point we used to call shutdown on the data socket to be
       sure all the data was delivered, but this was not germane in a
       request/response test, and it was causing the tests to "hang"
       when they were being controlled by time. So, I have replaced
       this shutdown call with a call to close that can be found later
       in the procedure. */
    
    /* this call will always give us the elapsed time for the test,
       and will also store-away the necessaries for cpu utilization */
    
    cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being */
						/* measured? how long */
						/* did we really run? */
    
    if (!no_control) {
      /* Get the statistics from the remote end. The remote will have
	 calculated CPU utilization. If it wasn't supposed to care, it
	 will return obvious values. */ 
    
      recv_response();
      if (!netperf_response.content.serv_errno) {
	if (debug)
	  fprintf(where,"remote results obtained\n");
      }
      else {
	Set_errno(netperf_response.content.serv_errno);
	fprintf(where,"netperf: remote error %d",
		netperf_response.content.serv_errno);
	perror("");
	fflush(where);
	exit(1);
      }
    }
    
    /* We now calculate what our throughput was for the test. */
  
    bytes_xferd	= (req_size * nummessages) + (rsp_size * nummessages);
    thruput	= nummessages/elapsed_time;
  
    if (local_cpu_usage || remote_cpu_usage) {
      /* We must now do a little math for service demand and cpu
       utilization for the system(s) Of course, some of the
       information might be bogus because there was no idle counter in
       the kernel(s). We need to make a note of this for the user's
       benefit... */
      if (local_cpu_usage) {
	local_cpu_utilization = calc_cpu_util(0.0);
 	/* since calc_service demand is doing ms/Kunit we will
	   multiply the number of transaction by 1024 to get "good"
	   numbers */
	local_service_demand  = calc_service_demand((double) nummessages*1024,
						    0.0,
						    0.0,
						    0);
      }
      else {
	local_cpu_utilization	= (float) -1.0;
	local_service_demand	= (float) -1.0;
      }
      
      if (remote_cpu_usage) {
	remote_cpu_utilization = sdp_rr_result->cpu_util;
	/* since calc_service demand is doing ms/Kunit we will
	   multiply the number of transaction by 1024 to get "good"
	   numbers */
	remote_service_demand = calc_service_demand((double) nummessages*1024,
						    0.0,
						    remote_cpu_utilization,
						    sdp_rr_result->num_cpus);
      }
      else {
	remote_cpu_utilization = (float) -1.0;
	remote_service_demand  = (float) -1.0;
      }
      
    }
    else {
      /* we were not measuring cpu, for the confidence stuff, we */
      /* should make it -1.0 */
      local_cpu_utilization	= (float) -1.0;
      local_service_demand	= (float) -1.0;
      remote_cpu_utilization = (float) -1.0;
      remote_service_demand  = (float) -1.0;
    }

    /* at this point, we want to calculate the confidence information.
       if debugging is on, calculate_confidence will print-out the
       parameters we pass it */
    
    calculate_confidence(confidence_iteration,
			 elapsed_time,
			 thruput,
			 local_cpu_utilization,
			 remote_cpu_utilization,
			 local_service_demand,
			 remote_service_demand);
    
    
    confidence_iteration++;

    /* we are now done with the socket, so close it */
    close(send_socket);

  }

  retrieve_confident_values(&elapsed_time,
			    &thruput,
			    &local_cpu_utilization,
			    &remote_cpu_utilization,
			    &local_service_demand,
			    &remote_service_demand);

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
    local_cpu_method = format_cpu_method(cpu_method);
    remote_cpu_method = format_cpu_method(sdp_rr_result->cpu_method);
    
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
		local_cpu_method,
		remote_cpu_method);
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
	fprintf(where,tput_title,format_units());
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
    /* SDP statistics, the alignments of the sends and receives */
    /* and all that sort of rot... */
    
    fprintf(where,
	    ksink_fmt,
	    local_send_align,
	    remote_recv_offset,
	    local_send_offset,
	    remote_recv_offset);

#ifdef WANT_HISTOGRAM
    fprintf(where,"\nHistogram of request/response times\n");
    fflush(where);
    HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */

  }
  
}
 /* this routine implements the receive (netserver) side of a SDP_RR */
 /* test */
void
recv_sdp_rr()
{
  
  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  struct addrinfo *local_res;
  char local_name[BUFSIZ];
  char port_buffer[PORTBUFSIZE];

  struct	sockaddr_in        myaddr_in,
  peeraddr_in;
  SOCKET	s_listen,s_data;
  netperf_socklen_t 	addrlen;
  char	*temp_message_ptr;
  int	trans_received;
  int	trans_remaining;
  int	bytes_sent;
  int	request_bytes_recvd;
  int	request_bytes_remaining;
  int	timed_out = 0;
  int   sock_closed = 0;
  float	elapsed_time;
  
  struct	sdp_rr_request_struct	*sdp_rr_request;
  struct	sdp_rr_response_struct	*sdp_rr_response;
  struct	sdp_rr_results_struct	*sdp_rr_results;
  
  sdp_rr_request = 
    (struct sdp_rr_request_struct *)netperf_request.content.test_specific_data;
  sdp_rr_response =
    (struct sdp_rr_response_struct *)netperf_response.content.test_specific_data;
  sdp_rr_results =
    (struct sdp_rr_results_struct *)netperf_response.content.test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_sdp_rr: entered...\n");
    fflush(where);
  }
  
  /* We want to set-up the listen socket with all the desired */
  /* parameters and then let the initiator know that all is ready. If */
  /* socket size defaults are to be used, then the initiator will have */
  /* sent us 0's. If the socket sizes cannot be changed, then we will */
  /* send-back what they are. If that information cannot be determined, */
  /* then we send-back -1's for the sizes. If things go wrong for any */
  /* reason, we will drop back ten yards and punt. */
  
  /* If anything goes wrong, we want the remote to know about it. It */
  /* would be best if the error that the remote reports to the user is */
  /* the actual error we encountered, rather than some bogus unexpected */
  /* response type message. */
  
  if (debug) {
    fprintf(where,"recv_sdp_rr: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response.content.response_type = SDP_RR_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_sdp_rr: the response type is set...\n");
    fflush(where);
  }
  
  /* allocate the recv and send rings with the requested alignments */
  /* and offsets. raj 7/94 */
  if (debug) {
    fprintf(where,"recv_sdp_rr: requested recv alignment of %d offset %d\n",
	    sdp_rr_request->recv_alignment,
	    sdp_rr_request->recv_offset);
    fprintf(where,"recv_sdp_rr: requested send alignment of %d offset %d\n",
	    sdp_rr_request->send_alignment,
	    sdp_rr_request->send_offset);
    fflush(where);
  }

  /* at some point, these need to come to us from the remote system */
  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  send_ring = allocate_buffer_ring(send_width,
				   sdp_rr_request->response_size,
				   sdp_rr_request->send_alignment,
				   sdp_rr_request->send_offset);

  recv_ring = allocate_buffer_ring(recv_width,
				   sdp_rr_request->request_size,
				   sdp_rr_request->recv_alignment,
				   sdp_rr_request->recv_offset);

  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_sdp_rr: grabbing a socket...\n");
    fflush(where);
  }

  /* create_data_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = sdp_rr_request->send_buf_size;
  lsr_size_req = sdp_rr_request->recv_buf_size;
  loc_nodelay = sdp_rr_request->no_delay;
  loc_rcvavoid = sdp_rr_request->so_rcvavoid;
  loc_sndavoid = sdp_rr_request->so_sndavoid;

  set_hostname_and_port(local_name,
			port_buffer,
			nf_to_af(sdp_rr_request->ipfamily),
			sdp_rr_request->port);

  local_res = complete_addrinfo(local_name,
				local_name,
				port_buffer,
				nf_to_af(sdp_rr_request->ipfamily),
				SOCK_STREAM,
				IPPROTO_TCP,
				0);

  /* fake things out by changing local_res->ai_family to AF_INET_SDP */
  local_res->ai_family = AF_INET_SDP;
  local_res->ai_protocol = 0;
  s_listen = create_data_socket(local_res);
  
  if (s_listen == INVALID_SOCKET) {
    netperf_response.content.serv_errno = errno;
    send_response();
    
    exit(1);
  }
  
  
#ifdef WIN32
  /* The test timer can fire during operations on the listening socket,
     so to make the start_timer below work we have to move
     it to close s_listen while we are blocked on accept. */
  win_kludge_socket2 = s_listen;
#endif

  
  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == SOCKET_ERROR) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen,
		  (struct sockaddr *)&myaddr_in, 
		  &addrlen) == SOCKET_ERROR) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */
  
  sdp_rr_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response.content.serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */
  
  sdp_rr_response->cpu_rate = (float)0.0; 	/* assume no cpu */
  sdp_rr_response->measure_cpu = 0;

  if (sdp_rr_request->measure_cpu) {
    sdp_rr_response->measure_cpu = 1;
    sdp_rr_response->cpu_rate = calibrate_local_cpu(sdp_rr_request->cpu_rate);
  }
  
  
  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  sdp_rr_response->send_buf_size = lss_size;
  sdp_rr_response->recv_buf_size = lsr_size;
  sdp_rr_response->no_delay = loc_nodelay;
  sdp_rr_response->so_rcvavoid = loc_rcvavoid;
  sdp_rr_response->so_sndavoid = loc_sndavoid;
  sdp_rr_response->test_length = sdp_rr_request->test_length;
  send_response();
  
  addrlen = sizeof(peeraddr_in);
  
  if ((s_data = accept(s_listen,
		       (struct sockaddr *)&peeraddr_in,
		       &addrlen)) == INVALID_SOCKET) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    
    exit(1);
  }
  
#ifdef KLUDGE_SOCKET_OPTIONS
  /* this is for those systems which *INCORRECTLY* fail to pass */
  /* attributes across an accept() call. Including this goes against */
  /* my better judgement :( raj 11/95 */

  kludge_socket_options(s_data);

#endif /* KLUDGE_SOCKET_OPTIONS */

#ifdef WIN32
  /* this is used so the timer thread can close the socket out from */
  /* under us, which to date is the easiest/cleanest/least */
  /* Windows-specific way I can find to force the winsock calls to */
  /* return WSAEINTR with the test is over. anything that will run on */
  /* 95 and NT and is closer to what netperf expects from Unix signals */
  /* and such would be appreciated raj 1/96 */
  win_kludge_socket = s_data;
#endif /* WIN32 */

  if (debug) {
    fprintf(where,"recv_sdp_rr: accept completes on the data connection.\n");
    fflush(where);
  }
  
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(sdp_rr_request->measure_cpu);
  
  /* The loop will exit when we hit the end of the test time, or when */
  /* we have exchanged the requested number of transactions. */
  
  if (sdp_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
    start_timer(sdp_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = sdp_rr_request->test_length * -1;
  }

  trans_received = 0;
  
  while ((!times_up) || (trans_remaining > 0)) {
    temp_message_ptr = recv_ring->buffer_ptr;
    request_bytes_remaining	= sdp_rr_request->request_size;
    while(request_bytes_remaining > 0) {
      if((request_bytes_recvd=recv(s_data,
				   temp_message_ptr,
				   request_bytes_remaining,
				   0)) == SOCKET_ERROR) {
	if (SOCKET_EINTR(request_bytes_recvd))
	{
	  timed_out = 1;
	  break;
	}

	netperf_response.content.serv_errno = errno;
	send_response();
	exit(1);
      }
      else if( request_bytes_recvd == 0 ) {
	if (debug) {
	  fprintf(where,"zero is my hero\n");
	  fflush(where);
	}
	sock_closed = 1;
	break;
      }
      else {
	request_bytes_remaining -= request_bytes_recvd;
	temp_message_ptr  += request_bytes_recvd;
      }
    }

    recv_ring = recv_ring->next;

    if ((timed_out) || (sock_closed)) {
      /* we hit the end of the test based on time - or the socket
	 closed on us along the way.  bail out of here now... */
      if (debug) {
	fprintf(where,"yo5\n");
	fflush(where);
      }						
      break;
    }
    
    /* Now, send the response to the remote */
    if((bytes_sent=send(s_data,
			send_ring->buffer_ptr,
			sdp_rr_request->response_size,
			0)) == SOCKET_ERROR) {
      if (SOCKET_EINTR(bytes_sent)) {
	/* the test timer has popped */
	timed_out = 1;
	fprintf(where,"yo6\n");
	fflush(where);						
	break;
      }
      netperf_response.content.serv_errno = 992;
      send_response();
      exit(1);
    }
    
    send_ring = send_ring->next;

    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
  }
  
  
  /* The loop now exits due to timeout or transaction count being */
  /* reached */
  
  cpu_stop(sdp_rr_request->measure_cpu,&elapsed_time);
  
  stop_timer();

  if (timed_out) {
    /* we ended the test by time, which was at least 2 seconds */
    /* longer than we wanted to run. so, we want to subtract */
    /* PAD_TIME from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }

  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_sdp_rr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }
  
  sdp_rr_results->bytes_received = (trans_received * 
				    (sdp_rr_request->request_size + 
				     sdp_rr_request->response_size));
  sdp_rr_results->trans_received = trans_received;
  sdp_rr_results->elapsed_time   = elapsed_time;
  sdp_rr_results->cpu_method     = cpu_method;
  sdp_rr_results->num_cpus       = lib_num_loc_cpus;
  if (sdp_rr_request->measure_cpu) {
    sdp_rr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_sdp_rr: test complete, sending results.\n");
    fflush(where);
  }
  
  /* we are now done with the sockets */
  close(s_data);
  close(s_listen);

  send_response();
  
}



void
print_sdp_usage()
{

  printf("%s",sdp_usage);
  exit(1);

}
void
scan_sdp_args(argc, argv)
     int	argc;
     char	*argv[];

{

#define SOCKETS_ARGS "b:DhH:I:L:m:M:P:r:s:S:V46"

  extern char	*optarg;	  /* pointer to option string	*/
  
  int		c;
  
  char	
    arg1[BUFSIZ],  /* argument holders		*/
    arg2[BUFSIZ];

  if (no_control) {
    fprintf(where,
	    "The SDP tests do not know how to deal with no control tests\n");
    exit(-1);
  }

  strncpy(local_data_port,"0",sizeof(local_data_port));
  strncpy(remote_data_port,"0",sizeof(remote_data_port));
  
  /* Go through all the command line arguments and break them */
  /* out. For those options that take two parms, specifying only */
  /* the first will set both to that value. Specifying only the */
  /* second will leave the first untouched. To change only the */
  /* first, use the form "first," (see the routine break_args.. */
  
  while ((c= getopt(argc, argv, SOCKETS_ARGS)) != EOF) {
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
      print_sdp_usage();
      exit(1);
    case 'b':
#ifdef WANT_FIRST_BURST
      first_burst_size = atoi(optarg);
#else /* WANT_FIRST_BURST */
      printf("Initial request burst functionality not compiled-in!\n");
#endif /* WANT_FIRST_BURST */
      break;
    case 'D':
      /* set the nodelay flag */
      loc_nodelay = 1;
      rem_nodelay = 1;
      break;
    case 'H':
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	/* make sure we leave room for the NULL termination boys and
	   girls. raj 2005-02-82 */ 
	remote_data_address = malloc(strlen(arg1)+1);
	strncpy(remote_data_address,arg1,strlen(arg1));
      }
      if (arg2[0])
	remote_data_family = parse_address_family(arg2);
      break;
    case 'L':
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	/* make sure we leave room for the NULL termination boys and
	   girls. raj 2005-02-82 */ 
	local_data_address = malloc(strlen(arg1)+1);
	strncpy(local_data_address,arg1,strlen(arg1));
      }
      if (arg2[0])
	local_data_family = parse_address_family(arg2);
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
    case 'r':
      /* set the request/response sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	req_size = convert(arg1);
      if (arg2[0])	
	rsp_size = convert(arg2);
      break;
    case 'm':
      /* set size of the buffer for each sent message */
      send_size = convert(optarg);
      break;
    case 'M':
      /* set the size of the buffer for each received message */
      recv_size = convert(optarg);
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
	send_width = convert(optarg);
	break;
    case 'V':
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
    case 'N':
      /* this opton allows the user to set the number of 
       * messages to send.  This in effect modifies the test
       * time.  If we know the message size, then the we can
       * express the test time as message_size * number_messages
       */
      msg_count = convert (optarg);
      if (msg_count > 0)
	  test_time = 0;
      break;
    case 'B':
      non_block = 1;
      break;
    case 'T':
      num_associations = atoi(optarg);
      if (num_associations <= 1) {
	  printf("Number of SDP associations must be >= 1\n");
	  exit(1);
      }
      break;
    };
  }
}

#endif  /* WANT_SDP */
