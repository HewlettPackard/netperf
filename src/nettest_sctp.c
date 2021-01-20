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

#ifndef lint
char	nettest_sctp[]="\
@(#)nettest_sctp.c (c) Copyright 2005-2012 Hewlett-Packard Co, 2021 Hewlett Packard Enterprise Development LP. Version 2.6.0";
#else
#define DIRTY
#define WANT_HISTOGRAM
#define WANT_INTERVALS
#endif /* lint */

/****************************************************************/
/*								*/
/*	nettest_sctp.c						*/
/*								*/
/*                                                              */
/*      scan_sctp_args()        get the sctp command line args  */
/*                                                              */
/*	the actual test routines...				*/
/*								*/
/*	send_sctp_stream()	perform a sctp stream test	*/
/*	recv_sctp_stream()					*/
/*	send_sctp_rr()		perform a sctp request/response	*/
/*	recv_sctp_rr()						*/
/*	send_sctp_stream_udp()	perform a sctp request/response	*/
/*	recv_sctp_stream_upd()	using UDP style API		*/
/*	send_sctp_rr_udp()	perform a sctp request/response	*/
/*	recv_sctp_rr_upd()	using UDP style API		*/
/*								*/
/*      relies on create_data_socket in nettest_bsd.c           */
/****************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if defined(WANT_SCTP)

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
#if defined(HAVE_SYS_IPC_H)
#include <sys/ipc.h>
#endif
#endif /* !defined(__VMS) */
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#include <netdb.h>

/* would seem that not all sctp.h files define a MSG_EOF, but that
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
#include "nettest_sctp.h"

#ifdef WANT_HISTOGRAM
#ifdef __sgi
#include <sys/time.h>
#endif /* __sgi */
#include "hist.h"
#endif /* WANT_HISTOGRAM */

#ifdef WANT_FIRST_BURST
extern int first_burst_size;
#endif /* WANT_FIRST_BURST */



/* these variables are specific to SCTP tests. declare */
/* them static to make them global only to this file. */

static int
  msg_count = 0,	/* number of messages to transmit on association */
  non_block = 0,	/* default to blocking sockets */
  num_associations = 1; /* number of associations on the endpoint */

static  int confidence_iteration;
static  char  local_cpu_method;
static  char  remote_cpu_method;

#ifdef WANT_HISTOGRAM
static HIST time_hist;
#endif /* WANT_HISTOGRAM */


char sctp_usage[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
SCTP Sockets Test Options:\n\
    -b number         Send number requests at the start of _RR tests\n\
    -D [L][,R]        Set SCTP_NODELAY locally and/or remotely\n\
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
    -N number	      Specifies the number of messages to send (_STREAM tests)\n\
    -B		      run the test in non-blocking mode\n\
    -T number	      Number of associations to create (_MANY tests)\n\
    -4                Use AF_INET (eg IPv4) on both ends of the data conn\n\
    -6                Use AF_INET6 (eg IPv6) on both ends of the data conn\n\
\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n";


 /* This routine is intended to retrieve interesting aspects of tcp */
 /* for the data connection. at first, it attempts to retrieve the */
 /* maximum segment size. later, it might be modified to retrieve */
 /* other information, but it must be information that can be */
 /* retrieved quickly as it is called during the timing of the test. */
 /* for that reason, a second routine may be created that can be */
 /* called outside of the timing loop */
static
void
get_sctp_info( int socket, int *mss )
{

  socklen_t sock_opt_len;

  if (sctp_opt_info(socket,
		    0,
		    SCTP_MAXSEG,
		    mss,
		    &sock_opt_len) < 0) {
    lss_size = -1;
  }
}


static
void
sctp_enable_events( int socket, int ev_mask )
{
    struct sctp_event_subscribe ev;

    bzero(&ev, sizeof(ev));

    if (ev_mask & SCTP_SNDRCV_INFO_EV)
	ev.sctp_data_io_event = 1;

    if (ev_mask & SCTP_ASSOC_CHANGE_EV)
	ev.sctp_association_event = 1;

    if (ev_mask & SCTP_PEERADDR_CHANGE_EV)
	ev.sctp_address_event = 1;

    if (ev_mask & SCTP_SND_FAILED_EV)
	ev.sctp_send_failure_event = 1;

    if (ev_mask & SCTP_REMOTE_ERROR_EV)
	ev.sctp_peer_error_event = 1;

    if (ev_mask & SCTP_SHUTDOWN_EV)
	ev.sctp_shutdown_event = 1;

    if (ev_mask & SCTP_PD_EV)
	ev.sctp_partial_delivery_event = 1;

    if (ev_mask & SCTP_ADAPT_EV)
#ifdef HAVE_SCTP_ADAPTATION_LAYER_EVENT
	ev.sctp_adaptation_layer_event = 1;
#else
	ev.sctp_adaption_layer_event = 1;
#endif

    if (setsockopt(socket,
		   IPPROTO_SCTP,
#ifdef SCTP_EVENTS
		   SCTP_EVENTS,
#else
		   SCTP_SET_EVENTS,
#endif
		   (const char*)&ev,
		   sizeof(ev)) != 0 ) {
      fprintf(where,
	      "sctp_enable_event: could not set sctp events errno %d\n",
	      errno);
      fflush(where);
      exit(1);
    }
}


static
sctp_disposition_t
sctp_process_event( int socket, void *buf )
{

    struct sctp_assoc_change *sac;
    struct sctp_send_failed *ssf;
    struct sctp_paddr_change *spc;
    struct sctp_remote_error *sre;
    union sctp_notification *snp;

    snp = buf;

    switch (snp->sn_header.sn_type) {
    case SCTP_ASSOC_CHANGE:
	if (debug) {
	    fprintf(where, "\tSCTP_ASSOC_CHANGE event, type:");
	    fflush(where);
	}
	sac = &snp->sn_assoc_change;
	switch (sac->sac_type) {
	    case SCTP_COMM_UP:
		if (debug) {
		    fprintf(where, "  SCTP_COMM_UP\n");
		    fflush(where);
		}
		break;
	    case SCTP_RESTART:
		if (debug) {
		    fprintf(where, "  SCTP_RESTART\n");
		    fflush(where);
		}
		break;
	    case SCTP_CANT_STR_ASSOC:
		if (debug) {
		    fprintf(where, "  SCTP_CANT_STR_ASSOC\n");
		    fflush(where);
		}
		break;	/* FIXME ignore above status changes */
	    case SCTP_COMM_LOST:
		if (debug) {
		    fprintf(where, "  SCTP_COMM_LOST\n");
		    fflush(where);
		}
		return SCTP_CLOSE;
	    case SCTP_SHUTDOWN_COMP:
		if (debug) {
		    fprintf(where, "  SCTP_SHUTDOWN_COMPLETE\n");
		    fflush(where);
		}
		return SCTP_CLOSE;
		break;
	}

    case SCTP_SEND_FAILED:
	if (debug) {
	    fprintf(where, "\tSCTP_SEND_FAILED event\n");
	    fflush(where);
	}
	ssf = &snp->sn_send_failed;
	break;  /* FIXME ??? ignore this for now */

    case SCTP_PEER_ADDR_CHANGE:
	if (debug) {
	    fprintf(where, "\tSCTP_PEER_ADDR_CHANGE event\n");
	    fflush(where);
	}
	spc = &snp->sn_paddr_change;
	break;	/* FIXME ??? ignore this for now */

    case SCTP_REMOTE_ERROR:
	if (debug) {
	    fprintf(where, "\tSCTP_REMOTE_ERROR event\n");
	    fflush(where);
	}
	sre = &snp->sn_remote_error;
	break;	/* FIXME ??? ignore this for now */
    case SCTP_SHUTDOWN_EVENT:
	if (debug) {
	    fprintf(where, "\tSCTP_SHUTDOWN event\n");
	    fflush(where);
	}
	return SCTP_CLOSE;
    default:
	fprintf(where, "unknown type: %hu\n", snp->sn_header.sn_type);
	fflush(where);
	break;
    }
    return SCTP_OK;
}



/* This routine implements the SCTP unidirectional data transfer test */
/* (a.k.a. stream) for the sockets interface. It receives its */
/* parameters via global variables from the shell and writes its */
/* output to the standard output. */


void
send_sctp_stream( char remote_host[] )
{

  char *tput_title = "\
Recv   Send    Send                          \n\
Socket Socket  Message  Elapsed              \n\
Size   Size    Size     Time     Throughput  \n\
bytes  bytes   bytes    secs.    %s/sec  \n\n";

  char *tput_fmt_0 =
    "%7.2f\n";

  char *tput_fmt_1 =
    "%6d %6d %6d    %-6.2f   %7.2f   \n";

  char *cpu_title = "\
Recv   Send    Send                          Utilization       Service Demand\n\
Socket Socket  Message  Elapsed              Send     Recv     Send    Recv\n\
Size   Size    Size     Time     Throughput  local    remote   local   remote\n\
bytes  bytes   bytes    secs.    %-8.8s/s  %% %c      %% %c      us/KB   us/KB\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c\n";

  char *cpu_fmt_1 =
    "%6d %6d %6d    %-6.2f     %7.2f   %-6.2f   %-6.2f   %-6.3f  %-6.3f\n";

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

#ifdef WANT_INTERVALS
  int interval_count;
  sigset_t signal_set;
#endif

  /* what we want is to have a buffer space that is at least one */
  /* send-size greater than our send window. this will insure that we */
  /* are never trying to re-use a buffer that may still be in the hands */
  /* of the transport. This buffer will be malloc'd after we have found */
  /* the size of the local senc socket buffer. We will want to deal */
  /* with alignment and offset concerns as well. */

#ifdef DIRTY
  int	*message_int_ptr;
#endif

  struct ring_elt *send_ring;

  int len;
  unsigned int nummessages = 0;
  int send_socket;
  int bytes_remaining;
  int sctp_mss;
  int timed_out;

  /* with links like fddi, one can send > 32 bits worth of bytes */
  /* during a test... ;-) at some point, this should probably become a */
  /* 64bit integral type, but those are not entirely common yet */
  double	bytes_sent = 0.0;

#ifdef DIRTY
  int	i;
#endif /* DIRTY */

  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;

  double	thruput;

  struct addrinfo	*remote_res;
  struct addrinfo	*local_res;

  struct	sctp_stream_request_struct	*sctp_stream_request;
  struct	sctp_stream_response_struct	*sctp_stream_response;
  struct	sctp_stream_results_struct	*sctp_stream_result;

  sctp_stream_request  =
    (struct sctp_stream_request_struct *)netperf_request.content.test_specific_data;
  sctp_stream_response =
    (struct sctp_stream_response_struct *)netperf_response.content.test_specific_data;
  sctp_stream_result   =
    (struct sctp_stream_results_struct *)netperf_response.content.test_specific_data;

#ifdef WANT_HISTOGRAM
  time_hist = HIST_new_n(1);
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
		     IPPROTO_SCTP,
		     0);

  if ( print_headers ) {
    print_top_test_header("SCTP STREAM TEST", local_res, remote_res);
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
    timed_out	   =    0;

    /*set up the data socket                        */
    send_socket = create_data_socket(local_res);

    if (send_socket == INVALID_SOCKET){
      perror("netperf: send_sctp_stream: sctp stream data socket");
      exit(1);
    }

    if (debug) {
      fprintf(where,"send_sctp_stream: send_socket obtained...\n");
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

    /* Tell the remote end to do a listen. The server alters the socket */
    /* paramters on the other side at this point, hence the reason for */
    /* all the values being passed in the setup message. If the user did */
    /* not specify any of the parameters, they will be passed as 0, which */
    /* will indicate to the remote that no changes beyond the system's */
    /* default should be used. Alignment is the exception, it will */
    /* default to 1, which will be no alignment alterations. */

    netperf_request.content.request_type	=	DO_SCTP_STREAM;
    sctp_stream_request->send_buf_size	=	rss_size_req;
    sctp_stream_request->recv_buf_size	=	rsr_size_req;
    sctp_stream_request->receive_size	=	recv_size;
    sctp_stream_request->no_delay	=	rem_nodelay;
    sctp_stream_request->recv_alignment	=	remote_recv_align;
    sctp_stream_request->recv_offset	=	remote_recv_offset;
    sctp_stream_request->measure_cpu	=	remote_cpu_usage;
    sctp_stream_request->cpu_rate	=	remote_cpu_rate;
    if (test_time) {
      sctp_stream_request->test_length	=	test_time;
    }
    else {
      if (msg_count)
	  test_bytes = send_size * msg_count;

      sctp_stream_request->test_length	=	test_bytes;
    }
    sctp_stream_request->so_rcvavoid	=	rem_rcvavoid;
    sctp_stream_request->so_sndavoid	=	rem_sndavoid;
#ifdef DIRTY
    sctp_stream_request->dirty_count    =       rem_dirty_count;
    sctp_stream_request->clean_count    =       rem_clean_count;
#endif /* DIRTY */
    sctp_stream_request->port		=	htonl(atoi(remote_data_port));
    sctp_stream_request->ipfamily	=	af_to_nf(remote_res->ai_family);
    sctp_stream_request->non_blocking   =	non_block;


    if (debug > 1) {
      fprintf(where,
	      "netperf: send_sctp_stream: requesting sctp stream test\n");
    }

    send_request();

    /* The response from the remote will contain all of the relevant 	*/
    /* socket parameters for this test type. We will put them back into */
    /* the variables here so they can be displayed if desired.  The	*/
    /* remote will have calibrated CPU if necessary, and will have done	*/
    /* all the needed set-up we will have calibrated the cpu locally	*/
    /* before sending the request, and will grab the counter value right*/
    /* after the connect returns. The remote will grab the counter right*/
    /* after the accept call. This saves the hassle of extra messages	*/
    /* being sent for the sctp tests.					*/

    recv_response();

    if (!netperf_response.content.serv_errno) {
      if (debug)
	fprintf(where,"remote listen done.\n");
      rsr_size	      =	sctp_stream_response->recv_buf_size;
      rss_size	      =	sctp_stream_response->send_buf_size;
      rem_nodelay     =	sctp_stream_response->no_delay;
      remote_cpu_usage=	sctp_stream_response->measure_cpu;
      remote_cpu_rate = sctp_stream_response->cpu_rate;

      /* we have to make sure that the server port number is in */
      /* network order */
      set_port_number(remote_res, (short)sctp_stream_response->data_port_number);

      rem_rcvavoid	= sctp_stream_response->so_rcvavoid;
      rem_sndavoid	= sctp_stream_response->so_sndavoid;
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

    /*Connect up to the remote port on the data socket  */
    if (connect(send_socket,
		remote_res->ai_addr,
		remote_res->ai_addrlen) == INVALID_SOCKET) {
      perror("netperf: send_sctp_stream: data socket connect failed");
      exit(1);
    }

    sctp_enable_events(send_socket, SCTP_ASSOC_CHANGE_EV);

    if (non_block) {
	/* now that we are connected, mark the socket as non-blocking */
	if (!set_nonblock(send_socket)) {
	  perror("netperf: fcntl");
	  exit(1);
	}
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

#ifdef WANT_INTERVALS
    if ((interval_burst) || (demo_mode)) {
      /* zero means that we never pause, so we never should need the */
      /* interval timer, unless we are in demo_mode */
      start_itimer(interval_wate);
    }
    interval_count = interval_burst;
    /* get the signal set for the call to sigsuspend */
    if (sigprocmask(SIG_BLOCK, (sigset_t *)NULL, &signal_set) != 0) {
      fprintf(where,
	      "send_sctp_stream: unable to get sigmask errno %d\n",
	      errno);
      fflush(where);
      exit(1);
    }
#endif /* WANT_INTERVALS */

#ifdef DIRTY
    /* initialize the random number generator for putting dirty stuff */
    /* into the send buffer. raj */
    srand((int) getpid());
#endif

    /* before we start, initialize a few variables */

    /* We use an "OR" to control test execution. When the test is */
    /* controlled by time, the byte count check will always return false. */
    /* When the test is controlled by byte count, the time test will */
    /* always return false. When the test is finished, the whole */
    /* expression will go false and we will stop sending data. */

    while ((!times_up) || (bytes_remaining > 0)) {

#ifdef DIRTY
      /* we want to dirty some number of consecutive integers in the buffer */
      /* we are about to send. we may also want to bring some number of */
      /* them cleanly into the cache. The clean ones will follow any dirty */
      /* ones into the cache. at some point, we might want to replace */
      /* the rand() call with something from a table to reduce our call */
      /* overhead during the test, but it is not a high priority item. */
      message_int_ptr = (int *)(send_ring->buffer_ptr);
      for (i = 0; i < loc_dirty_count; i++) {
	*message_int_ptr = rand();
	message_int_ptr++;
      }
      for (i = 0; i < loc_clean_count; i++) {
	loc_dirty_count = *message_int_ptr;
	message_int_ptr++;
      }
#endif /* DIRTY */

#ifdef WANT_HISTOGRAM
      /* timestamp just before we go into send and then again just after */
      /* we come out raj 8/94 */
      HIST_timestamp_start(time_hist);
#endif /* WANT_HISTOGRAM */

      while ((len=sctp_sendmsg(send_socket,
			       send_ring->buffer_ptr, send_size,
			       NULL, 0,
			       0, 0, 0, 0, 0)) != send_size) {
	if (non_block && errno == EAGAIN)
	    continue;
	else if ((len >=0) || SOCKET_EINTR(len)) {
	  /* the test was interrupted, must be the end of test */
	  timed_out = 1;
	  break;
	}
	perror("netperf: data send error");
	printf("len was %d\n",len);
	exit(1);
      }

      if (timed_out)
	  break;	/* we timed out durint sendmsg, done with test */

#ifdef WANT_HISTOGRAM
      /* timestamp the exit from the send call and update the histogram */
      HIST_timestamp_stop_add(time_hist);
#endif /* WANT_HISTOGRAM */

#ifdef WANT_INTERVALS
      if (demo_mode) {
	units_this_tick += send_size;
      }
      /* in this case, the interval count is the count-down couter */
      /* to decide to sleep for a little bit */
      if ((interval_burst) && (--interval_count == 0)) {
	/* call sigsuspend and wait for the interval timer to get us */
	/* out */
	if (debug > 1) {
	  fprintf(where,"about to suspend\n");
	  fflush(where);
	}
	if (sigsuspend(&signal_set) == EFAULT) {
	  fprintf(where,
		  "send_sctp_stream: fault with sigsuspend.\n");
	  fflush(where);
	  exit(1);
	}
	interval_count = interval_burst;
      }
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
    /* the sctp maximum segment_size was (if possible) */
    if (verbosity > 1) {
      sctp_mss = -1;
      get_sctp_info(send_socket, &sctp_mss);
    }

    shutdown(send_socket, SHUT_WR);

    /* The test server will signal to us when it wants to shutdown.
     * In blocking mode, we can call recvmsg.  In non-blocking
     * mode, we need to select on the socket for reading.
     * We'll assume that all returns are succefull
     */
    if (non_block) {
	fd_set readfds;

	FD_ZERO(&readfds);
	FD_SET(send_socket, &readfds);
	select(send_socket+1, &readfds, NULL, NULL, NULL);
    } else {
	sctp_recvmsg(send_socket, send_ring->buffer_ptr, send_size, NULL,
		0, NULL, 0);
    }

    /* this call will always give us the elapsed time for the test, and */
    /* will also store-away the necessaries for cpu utilization */

    cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being */
						/* measured and how */
						/* long did we really */
						/* run? */

    /* we are finished with the socket, so close it to prevent hitting */
    /* the limit on maximum open files. */
    close(send_socket);

    /* Get the statistics from the remote end. The remote will have */
    /* calculated service demand and all those interesting things. If it */
    /* wasn't supposed to care, it will return obvious values. */

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

    /* We now calculate what our thruput was for the test. In the future, */
    /* we may want to include a calculation of the thruput measured by */
    /* the remote, but it should be the case that for a sctp stream test, */
    /* that the two numbers should be *very* close... We calculate */
    /* bytes_sent regardless of the way the test length was controlled. */
    /* If it was time, we needed to, and if it was by bytes, the user may */
    /* have specified a number of bytes that wasn't a multiple of the */
    /* send_size, so we really didn't send what he asked for ;-) */

    bytes_sent	= ntohd(sctp_stream_result->bytes_received);

    thruput	= (double) calc_thruput(bytes_sent);

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

	remote_cpu_utilization	= sctp_stream_result->cpu_util;
	remote_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      remote_cpu_utilization,
						      sctp_stream_result->num_cpus);
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
    remote_cpu_method = format_cpu_method(sctp_stream_result->cpu_method);

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
	      thruput);
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
	      thruput);/* how fast did it go */
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
    /* sctp statistics, the alignments of the sends and receives */
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
	    bytes_sent / (double)sctp_stream_result->recv_calls,
	    sctp_stream_result->recv_calls);
    fprintf(where,
	    ksink_fmt2,
	    sctp_mss);
    fflush(where);
#ifdef WANT_HISTOGRAM
    fprintf(where,"\n\nHistogram of time spent in send() call.\n");
    fflush(where);
    HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
  }

}




/* This is the server-side routine for the sctp stream test. It is */
/* implemented as one routine. I could break things-out somewhat, but */
/* didn't feel it was necessary. */

void
recv_sctp_stream( void )
{

  struct sockaddr_in myaddr_in; /* needed to get port number */
  struct sockaddr_storage peeraddr;	/* used in accept */
  int	s_listen,s_data;
  socklen_t 	addrlen;
  int	len;
  unsigned int	receive_calls;
  float	elapsed_time;
  double   bytes_received;

  struct ring_elt *recv_ring;

  struct addrinfo *local_res;
  char local_name[BUFSIZ];
  char port_buffer[PORTBUFSIZE];
  int  msg_flags = 0;

#ifdef DIRTY
  int   *message_int_ptr;
  int   dirty_count;
  int   clean_count;
  int   i;
#endif

#ifdef DO_SELECT
  fd_set readfds;
  struct timeval timeout;
#endif /* DO_SELECT */

  struct	sctp_stream_request_struct	*sctp_stream_request;
  struct	sctp_stream_response_struct	*sctp_stream_response;
  struct	sctp_stream_results_struct	*sctp_stream_results;

#ifdef DO_SELECT
  FD_ZERO(&readfds);
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
#endif /* DO_SELECT */

  sctp_stream_request	=
    (struct sctp_stream_request_struct *)netperf_request.content.test_specific_data;
  sctp_stream_response	=
    (struct sctp_stream_response_struct *)netperf_response.content.test_specific_data;
  sctp_stream_results	=
    (struct sctp_stream_results_struct *)netperf_response.content.test_specific_data;

  if (debug) {
    fprintf(where,"netserver: recv_sctp_stream: entered...\n");
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
    fprintf(where,"recv_sctp_stream: setting the response type...\n");
    fflush(where);
  }

  netperf_response.content.response_type = SCTP_STREAM_RESPONSE;

  if (debug) {
    fprintf(where,"recv_sctp_stream: the response type is set...\n");
    fflush(where);
  }

  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */

  if (debug) {
    fprintf(where,"recv_sctp_stream: requested alignment of %d\n",
	    sctp_stream_request->recv_alignment);
    fflush(where);
  }

  /* create_data_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = sctp_stream_request->send_buf_size;
  lsr_size_req = sctp_stream_request->recv_buf_size;
  loc_nodelay = sctp_stream_request->no_delay;
  loc_rcvavoid = sctp_stream_request->so_rcvavoid;
  loc_sndavoid = sctp_stream_request->so_sndavoid;
  non_block = sctp_stream_request->non_blocking;

  set_hostname_and_port(local_name,
			port_buffer,
			nf_to_af(sctp_stream_request->ipfamily),
			sctp_stream_request->port);

  local_res = complete_addrinfo(local_name,
				local_name,
				port_buffer,
				nf_to_af(sctp_stream_request->ipfamily),
				SOCK_STREAM,
				IPPROTO_SCTP,
				0);

  s_listen = create_data_socket(local_res);

  if (s_listen < 0) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }

  /* what sort of sizes did we end-up with? */
  if (sctp_stream_request->receive_size == 0) {
    if (lsr_size > 0) {
      recv_size = lsr_size;
    }
    else {
      recv_size = 4096;
    }
  }
  else {
    recv_size = sctp_stream_request->receive_size;
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
				   sctp_stream_request->recv_alignment,
				   sctp_stream_request->recv_offset);

  if (debug) {
    fprintf(where,"recv_sctp_stream: set recv_size = %d, align = %d, offset = %d.\n",
		   recv_size, sctp_stream_request->recv_alignment,
		   sctp_stream_request->recv_offset);
    fflush(where);
  }

  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */

  sctp_stream_response->cpu_rate = (float)0.0; 	/* assume no cpu */
  if (sctp_stream_request->measure_cpu) {
    sctp_stream_response->measure_cpu = 1;
    sctp_stream_response->cpu_rate =
      calibrate_local_cpu(sctp_stream_request->cpu_rate);
  }
  else {
    sctp_stream_response->measure_cpu = 0;
  }

  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  sctp_stream_response->send_buf_size = lss_size;
  sctp_stream_response->recv_buf_size = lsr_size;
  sctp_stream_response->no_delay = loc_nodelay;
  sctp_stream_response->so_rcvavoid = loc_rcvavoid;
  sctp_stream_response->so_sndavoid = loc_sndavoid;
  sctp_stream_response->receive_size = recv_size;

  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == -1) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();

    exit(1);
  }

  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen,
		  (struct sockaddr *)&myaddr_in,
		  &addrlen) == -1){
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();

    exit(1);
  }

  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */

  sctp_stream_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response.content.serv_errno   = 0;

  send_response();

  addrlen = sizeof(peeraddr);

  if ((s_data = accept(s_listen,
		      (struct sockaddr *)&peeraddr,
		      &addrlen)) == INVALID_SOCKET) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    exit(1);
  }

  sctp_enable_events(s_data, SCTP_ASSOC_CHANGE_EV | SCTP_SHUTDOWN_EV);

  /* now that we are connected, mark the socket as non-blocking */
  if (non_block) {
      fprintf(where, "setting socket as nonblocking\n");
      fflush(where);
      if (!set_nonblock(s_data)) {
	close(s_data);
	exit(1);
      }
  }

#ifdef KLUDGE_SOCKET_OPTIONS
  /* this is for those systems which *INCORRECTLY* fail to pass */
  /* attributes across an accept() call. Including this goes against */
  /* my better judgement :( raj 11/95 */

  kludge_socket_options(s_data);

#endif /* KLUDGE_SOCKET_OPTIONS */

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */

  cpu_start(sctp_stream_request->measure_cpu);

  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */

#ifdef DIRTY
    /* we want to dirty some number of consecutive integers in the buffer */
    /* we are about to recv. we may also want to bring some number of */
    /* them cleanly into the cache. The clean ones will follow any dirty */
    /* ones into the cache. */

  dirty_count = sctp_stream_request->dirty_count;
  clean_count = sctp_stream_request->clean_count;
  message_int_ptr = (int *)recv_ring->buffer_ptr;
  for (i = 0; i < dirty_count; i++) {
    *message_int_ptr = rand();
    message_int_ptr++;
  }
  for (i = 0; i < clean_count; i++) {
    dirty_count = *message_int_ptr;
    message_int_ptr++;
  }
#endif /* DIRTY */

  bytes_received = 0;
  receive_calls  = 0;

  while ((len = sctp_recvmsg(s_data,
			    recv_ring->buffer_ptr, recv_size,
			    NULL, 0, NULL, &msg_flags)) != 0) {
    if (len == SOCKET_ERROR) {
	if (non_block && errno == EAGAIN) {
	   if (debug){
	     fprintf(where,
		    "recv_sctp_stream: sctp_recvmsg timed out, trying again\n");
	     fflush(where);
	   }
	   Set_errno(0);
	   continue;
	}
	if (debug) {
	    fprintf(where,
		    "recv_sctp_stream: sctp_recvmsg error %d, exiting",
		    errno);
	    fflush(where);
        }
	netperf_response.content.serv_errno = errno;
	send_response();
	close(s_data);
	exit(1);
    }

    if (msg_flags & MSG_NOTIFICATION) {
	 msg_flags = 0;
	 if (debug) {
	   fprintf(where,
		    "recv_sctp_stream: Got notification... processing\n");
	     fflush(where);
	}
	if (sctp_process_event(s_data, recv_ring->buffer_ptr) == SCTP_CLOSE)
	    break;	/* break out of the recvmsg loop */

	continue;
    }

    bytes_received += len;
    receive_calls++;

    /* more to the next buffer in the recv_ring */
    recv_ring = recv_ring->next;

#ifdef PAUSE
    sleep(1);
#endif /* PAUSE */

#ifdef DIRTY
    message_int_ptr = (int *)(recv_ring->buffer_ptr);
    for (i = 0; i < dirty_count; i++) {
      *message_int_ptr = rand();
      message_int_ptr++;
    }
    for (i = 0; i < clean_count; i++) {
      dirty_count = *message_int_ptr;
      message_int_ptr++;
    }
#endif /* DIRTY */

#ifdef DO_SELECT
	FD_SET(s_data,&readfds);
	select(s_data+1,&readfds,NULL,NULL,&timeout);
#endif /* DO_SELECT */

  }

  /* perform a shutdown to signal the sender that */
  /* we have received all the data sent. raj 4/93 */

  if (close(s_data) == -1) {
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
    }

  cpu_stop(sctp_stream_request->measure_cpu,&elapsed_time);

  /* send the results to the sender			*/

  if (debug) {
    fprintf(where,
	    "recv_sctp_stream: got %g bytes\n",
	    bytes_received);
    fprintf(where,
	    "recv_sctp_stream: got %d recvs\n",
	    receive_calls);
    fflush(where);
  }

  sctp_stream_results->bytes_received	= htond(bytes_received);
  sctp_stream_results->elapsed_time	= elapsed_time;
  sctp_stream_results->recv_calls	= receive_calls;

  if (sctp_stream_request->measure_cpu) {
    sctp_stream_results->cpu_util	= calc_cpu_util(0.0);
  };

  if (debug) {
    fprintf(where,
	    "recv_sctp_stream: test complete, sending results.\n");
    fprintf(where,
	    "                 bytes_received %g receive_calls %d\n",
	    bytes_received,
	    receive_calls);
    fprintf(where,
	    "                 len %d\n",
	    len);
    fflush(where);
  }

  sctp_stream_results->cpu_method = cpu_method;
  sctp_stream_results->num_cpus   = lib_num_loc_cpus;
  send_response();

  /* we are now done with the sockets */
  close(s_listen);

}


/* This routine implements the SCTP unidirectional data transfer test */
/* (a.k.a. stream) for the sockets interface. It receives its */
/* parameters via global variables from the shell and writes its */
/* output to the standard output. */


void
send_sctp_stream_1toMany( char remote_host[] )
{

  char *tput_title = "\
Recv   Send    Send                          \n\
Socket Socket  Message  Elapsed              \n\
Size   Size    Size     Time     Throughput  \n\
bytes  bytes   bytes    secs.    %s/sec  \n\n";

  char *tput_fmt_0 =
    "%7.2f\n";

  char *tput_fmt_1 =
    "%6d %6d %6d    %-6.2f   %7.2f   \n";

  char *cpu_title = "\
Recv   Send    Send                          Utilization       Service Demand\n\
Socket Socket  Message  Elapsed              Send     Recv     Send    Recv\n\
Size   Size    Size     Time     Throughput  local    remote   local   remote\n\
bytes  bytes   bytes    secs.    %-8.8s/s  %% %c      %% %c      us/KB   us/KB\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c\n";

  char *cpu_fmt_1 =
    "%6d %6d %6d    %-6.2f     %7.2f   %-6.2f   %-6.2f   %-6.3f  %-6.3f\n";

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

#ifdef WANT_INTERVALS
  int interval_count;
  sigset_t signal_set;
#endif

  /* what we want is to have a buffer space that is at least one */
  /* send-size greater than our send window. this will insure that we */
  /* are never trying to re-use a buffer that may still be in the hands */
  /* of the transport. This buffer will be malloc'd after we have found */
  /* the size of the local senc socket buffer. We will want to deal */
  /* with alignment and offset concerns as well. */

#ifdef DIRTY
  int	*message_int_ptr;
#endif

  struct ring_elt *send_ring;

  int len;
  unsigned int nummessages = 0;
  int *send_socket;
  int bytes_remaining;
  int sctp_mss;

  /* with links like fddi, one can send > 32 bits worth of bytes */
  /* during a test... ;-) at some point, this should probably become a */
  /* 64bit integral type, but those are not entirely common yet */
  double	bytes_sent = 0.0;

#ifdef DIRTY
  int	i;
#endif /* DIRTY */

  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;

  double	thruput;

  struct addrinfo *remote_res;
  struct addrinfo *local_res;

  struct	sctp_stream_request_struct	*sctp_stream_request;
  struct	sctp_stream_response_struct	*sctp_stream_response;
  struct	sctp_stream_results_struct	*sctp_stream_result;

  sctp_stream_request  =
    (struct sctp_stream_request_struct *)netperf_request.content.test_specific_data;
  sctp_stream_response =
    (struct sctp_stream_response_struct *)netperf_response.content.test_specific_data;
  sctp_stream_result   =
    (struct sctp_stream_results_struct *)netperf_response.content.test_specific_data;

#ifdef WANT_HISTOGRAM
  time_hist = HIST_new_n(1);
#endif /* WANT_HISTOGRAM */

  complete_addrinfos(&remote_res,
		     &local_res,
		     remote_host,
		     SOCK_SEQPACKET,
		     IPPROTO_SCTP,
		     0);

  if ( print_headers ) {
    print_top_test_header("SCTP 1-TO-MANY STREAM TEST",local_res,remote_res);
  }

  send_ring = NULL;
  confidence_iteration = 1;
  init_stat();

  send_socket = malloc(sizeof (int) * num_associations);
  if (send_socket == NULL) {
      fprintf(where, "send_sctp_stream_1toMany: failed to allocation sockets!\n");
      exit(1);
  }

  /* we have a great-big while loop which controls the number of times */
  /* we run a particular test. this is for the calculation of a */
  /* confidence interval (I really should have stayed awake during */
  /* probstats :). If the user did not request confidence measurement */
  /* (no confidence is the default) then we will only go though the */
  /* loop once. the confidence stuff originates from the folks at IBM */

  while (((confidence < 0) && (confidence_iteration < iteration_max)) ||
	 (confidence_iteration <= iteration_min)) {

    int		j=0;
    int		timed_out = 0;


    /* initialize a few counters. we have to remember that we might be */
    /* going through the loop more than once. */

    nummessages    =	0;
    bytes_sent     =	0.0;
    times_up       = 	0;

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

    /* Tell the remote end to do a listen. The server alters the socket */
    /* paramters on the other side at this point, hence the reason for */
    /* all the values being passed in the setup message. If the user did */
    /* not specify any of the parameters, they will be passed as 0, which */
    /* will indicate to the remote that no changes beyond the system's */
    /* default should be used. Alignment is the exception, it will */
    /* default to 1, which will be no alignment alterations. */

    netperf_request.content.request_type	=	DO_SCTP_STREAM_MANY;
    sctp_stream_request->send_buf_size	=	rss_size_req;
    sctp_stream_request->recv_buf_size	=	rsr_size_req;
    sctp_stream_request->receive_size	=	recv_size;
    sctp_stream_request->no_delay	=	rem_nodelay;
    sctp_stream_request->recv_alignment	=	remote_recv_align;
    sctp_stream_request->recv_offset	=	remote_recv_offset;
    sctp_stream_request->measure_cpu	=	remote_cpu_usage;
    sctp_stream_request->cpu_rate	=	remote_cpu_rate;
    if (test_time) {
      sctp_stream_request->test_length	=	test_time;
    }
    else {
      if (msg_count)
	  test_bytes = send_size * msg_count;

      sctp_stream_request->test_length	=	test_bytes*num_associations;
    }
    sctp_stream_request->so_rcvavoid	=	rem_rcvavoid;
    sctp_stream_request->so_sndavoid	=	rem_sndavoid;
#ifdef DIRTY
    sctp_stream_request->dirty_count    =       rem_dirty_count;
    sctp_stream_request->clean_count    =       rem_clean_count;
#endif /* DIRTY */
    sctp_stream_request->port		= 	(atoi(remote_data_port));
    sctp_stream_request->ipfamily	=	af_to_nf(remote_res->ai_family);
    sctp_stream_request->non_blocking   =	non_block;


    if (debug > 1) {
      fprintf(where,
	      "netperf: send_sctp_stream_1toMany: requesting sctp stream test\n");
    }

    send_request();

    /* The response from the remote will contain all of the relevant 	*/
    /* socket parameters for this test type. We will put them back into */
    /* the variables here so they can be displayed if desired.  The	*/
    /* remote will have calibrated CPU if necessary, and will have done	*/
    /* all the needed set-up we will have calibrated the cpu locally	*/
    /* before sending the request, and will grab the counter value right*/
    /* after the connect returns. The remote will grab the counter right*/
    /* after the accept call. This saves the hassle of extra messages	*/
    /* being sent for the sctp tests.					*/

    recv_response();

    if (!netperf_response.content.serv_errno) {
      if (debug)
	fprintf(where,"remote listen done.\n");
      rsr_size	      =	sctp_stream_response->recv_buf_size;
      rss_size	      =	sctp_stream_response->send_buf_size;
      rem_nodelay     =	sctp_stream_response->no_delay;
      remote_cpu_usage=	sctp_stream_response->measure_cpu;
      remote_cpu_rate = sctp_stream_response->cpu_rate;

      /* we have to make sure that the server port number is in */
      /* network order */
      set_port_number(remote_res, (unsigned short)sctp_stream_response->data_port_number);
      rem_rcvavoid	= sctp_stream_response->so_rcvavoid;
      rem_sndavoid	= sctp_stream_response->so_sndavoid;
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

    /*set up the the array of data sockets  and connect them to the server */

    for (j = 0; j < num_associations; j++) {
	send_socket[j] = create_data_socket(local_res);

	if (send_socket[j] < 0){
	  perror("netperf: send_sctp_stream_1toMany: sctp stream data socket");
	  exit(1);
	}

	if (debug) {
	  fprintf(where,"send_sctp_stream_1toMany: send_socket obtained...\n");
	}

	/*Connect up to the remote port on the data socket  */
	if (connect(send_socket[j],
		    remote_res->ai_addr,
		    remote_res->ai_addrlen) == INVALID_SOCKET){
	  perror("netperf: send_sctp_stream_1toMany: data socket connect failed");
	  exit(1);
	}

	/* Do it after connect is successfull, so that we don't see COMM_UP */
	sctp_enable_events(send_socket[j], SCTP_ASSOC_CHANGE_EV);

	if (non_block) {
	    /* now that we are connected, mark the socket as non-blocking */
	    if (!set_nonblock(send_socket[j])) {
	      perror("netperf: fcntl");
	      exit(1);
	    }
	}
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
      bytes_remaining = test_bytes * num_associations;
      times_up = 1;
    }

    /* The cpu_start routine will grab the current time and possibly */
    /* value of the idle counter for later use in measuring cpu */
    /* utilization and/or service demand and thruput. */

    cpu_start(local_cpu_usage);

#ifdef WANT_INTERVALS
    if ((interval_burst) || (demo_mode)) {
      /* zero means that we never pause, so we never should need the */
      /* interval timer, unless we are in demo_mode */
      start_itimer(interval_wate);
    }
    interval_count = interval_burst;
    /* get the signal set for the call to sigsuspend */
    if (sigprocmask(SIG_BLOCK, (sigset_t *)NULL, &signal_set) != 0) {
      fprintf(where,
	      "send_sctp_stream_1toMany: unable to get sigmask errno %d\n",
	      errno);
      fflush(where);
      exit(1);
    }
#endif /* WANT_INTERVALS */

#ifdef DIRTY
    /* initialize the random number generator for putting dirty stuff */
    /* into the send buffer. raj */
    srand((int) getpid());
#endif

    /* before we start, initialize a few variables */

    /* We use an "OR" to control test execution. When the test is */
    /* controlled by time, the byte count check will always return false. */
    /* When the test is controlled by byte count, the time test will */
    /* always return false. When the test is finished, the whole */
    /* expression will go false and we will stop sending data. */

    while ((!times_up) || (bytes_remaining > 0)) {

#ifdef DIRTY
      /* we want to dirty some number of consecutive integers in the buffer */
      /* we are about to send. we may also want to bring some number of */
      /* them cleanly into the cache. The clean ones will follow any dirty */
      /* ones into the cache. at some point, we might want to replace */
      /* the rand() call with something from a table to reduce our call */
      /* overhead during the test, but it is not a high priority item. */
      message_int_ptr = (int *)(send_ring->buffer_ptr);
      for (i = 0; i < loc_dirty_count; i++) {
	*message_int_ptr = rand();
	message_int_ptr++;
      }
      for (i = 0; i < loc_clean_count; i++) {
	loc_dirty_count = *message_int_ptr;
	message_int_ptr++;
      }
#endif /* DIRTY */

#ifdef WANT_HISTOGRAM
      /* timestamp just before we go into send and then again just after */
      /* we come out raj 8/94 */
      HIST_timestamp_start(time_hist);
#endif /* WANT_HISTOGRAM */

      for (j = 0; j < num_associations; j++) {

	  if((len=sctp_sendmsg(send_socket[j],
			       send_ring->buffer_ptr,
			       send_size,
			       (struct sockaddr *)remote_res->ai_addr,
			       remote_res->ai_addrlen,
			       0, 0, 0, 0, 0)) != send_size) {
	    if ((len >=0) || SOCKET_EINTR(len)) {
	      /* the test was interrupted, must be the end of test */
	      timed_out = 1;
	      break;
	    } else if (non_block && errno == EAGAIN) {
		j--;	 /* send again on the same socket */
		Set_errno(0);
		continue;
	    }
	    perror("netperf: data send error");
	    printf("len was %d\n",len);
	    exit(1);
	  }
      }

      if (timed_out)
	  break;	/* test is over, try next iteration */

#ifdef WANT_HISTOGRAM
      /* timestamp the exit from the send call and update the histogram */
      HIST_timestamp_stop_add(time_hist);
#endif /* WANT_HISTOGRAM */

#ifdef WANT_INTERVALS
      if (demo_mode) {
	units_this_tick += send_size;
      }
      /* in this case, the interval count is the count-down couter */
      /* to decide to sleep for a little bit */
      if ((interval_burst) && (--interval_count == 0)) {
	/* call sigsuspend and wait for the interval timer to get us */
	/* out */
	if (debug > 1) {
	  fprintf(where,"about to suspend\n");
	  fflush(where);
	}
	if (sigsuspend(&signal_set) == EFAULT) {
	  fprintf(where,
		  "send_sctp_stream_1toMany: fault with sigsuspend.\n");
	  fflush(where);
	  exit(1);
	}
	interval_count = interval_burst;
      }
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
    /* the sctp maximum segment_size was (if possible) */
    if (verbosity > 1) {
      sctp_mss = -1;
      get_sctp_info(send_socket[0], &sctp_mss);
    }

    /* signal the server that we are all done writing, this will
     * initiate a shutdonw of one of the associations on the
     * server and trigger an event telling the server it's all done
     */
    sctp_sendmsg(send_socket[0], NULL, 0, remote_res->ai_addr,
		 remote_res->ai_addrlen, 0, MSG_EOF, 0, 0, 0);


    /* The test server will initiate closure of all associations
     * when it's done reading. We want a basic mechanism to catch this
     * and are using SCTP events for this.
     * In blocking mode, we can call recvmsg with the last socket we created.
     * In non-blocking  mode, we need to select on the socket for reading.
     * We'll assume that all returns are succefull and signify
     * closure.
     * It is sufficient to do this on a single socket in the client.
     * We choose to do it on a socket other then the one that send MSG_EOF.
     * This means that anything comming in on that socket will be a shutdown.
     */
    if (non_block) {
	fd_set readfds;

	FD_ZERO(&readfds);
	FD_SET(send_socket[num_associations-1], &readfds);
	select(send_socket[num_associations-1]+1, &readfds, NULL, NULL, NULL);
    } else {
	sctp_recvmsg(send_socket[num_associations], send_ring->buffer_ptr,
		     send_size, NULL, 0, NULL, 0);
    }

    /* this call will always give us the elapsed time for the test, and */
    /* will also store-away the necessaries for cpu utilization */

    cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being */
						/* measured and how */
						/* long did we really */
						/* run? */

    /* we are finished with our sockets, so close them to prevent hitting */
    /* the limit on maximum open files. */
    for (j = 0; j < num_associations; j++)
	close(send_socket[j]);

    /* Get the statistics from the remote end. The remote will have */
    /* calculated service demand and all those interesting things. If it */
    /* wasn't supposed to care, it will return obvious values. */

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

    /* We now calculate what our thruput was for the test. In the future, */
    /* we may want to include a calculation of the thruput measured by */
    /* the remote, but it should be the case that for a sctp stream test, */
    /* that the two numbers should be *very* close... We calculate */
    /* bytes_sent regardless of the way the test length was controlled. */
    /* If it was time, we needed to, and if it was by bytes, the user may */
    /* have specified a number of bytes that wasn't a multiple of the */
    /* send_size, so we really didn't send what he asked for ;-) */

    bytes_sent	= ntohd(sctp_stream_result->bytes_received);

    thruput	= (double) calc_thruput(bytes_sent);

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

	remote_cpu_utilization	= sctp_stream_result->cpu_util;
	remote_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      remote_cpu_utilization,
						      sctp_stream_result->num_cpus);
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
    remote_cpu_method = format_cpu_method(sctp_stream_result->cpu_method);

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
	      thruput);
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
	      thruput);/* how fast did it go */
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
    /* sctp statistics, the alignments of the sends and receives */
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
	    bytes_sent / (double)sctp_stream_result->recv_calls,
	    sctp_stream_result->recv_calls);
    fprintf(where,
	    ksink_fmt2,
	    sctp_mss);
    fflush(where);
#ifdef WANT_HISTOGRAM
    fprintf(where,"\n\nHistogram of time spent in send() call.\n");
    fflush(where);
    HIST_report(time_hist);
#endif /* WANT_HISTOGRAM */
  }

}



/* This is the server-side routine for the sctp stream test. It is */
/* implemented as one routine. I could break things-out somewhat, but */
/* didn't feel it was necessary. */

void
recv_sctp_stream_1toMany( void )
{

  struct sockaddr_in myaddr_in;
  int	s_recv;
  socklen_t 	addrlen;
  int	len;
  unsigned int	receive_calls;
  float	elapsed_time;
  double   bytes_received;
  int	msg_flags = 0;

  struct ring_elt *recv_ring;

  struct addrinfo *local_res;
  char local_name[BUFSIZ];
  char port_buffer[PORTBUFSIZE];

#ifdef DIRTY
  int   *message_int_ptr;
  int   dirty_count;
  int   clean_count;
  int   i;
#endif

#ifdef DO_SELECT
  fd_set readfds;
  struct timeval timeout;
#endif

  struct	sctp_stream_request_struct	*sctp_stream_request;
  struct	sctp_stream_response_struct	*sctp_stream_response;
  struct	sctp_stream_results_struct	*sctp_stream_results;

#ifdef DO_SELECT
  FD_ZERO(&readfds);
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
#endif

  sctp_stream_request	=
    (struct sctp_stream_request_struct *)netperf_request.content.test_specific_data;
  sctp_stream_response	=
    (struct sctp_stream_response_struct *)netperf_response.content.test_specific_data;
  sctp_stream_results	=
    (struct sctp_stream_results_struct *)netperf_response.content.test_specific_data;

  if (debug) {
    fprintf(where,"netserver: recv_sctp_stream: entered...\n");
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
    fprintf(where,"recv_sctp_stream_1toMany: setting the response type...\n");
    fflush(where);
  }

  netperf_response.content.response_type = SCTP_STREAM_MANY_RESPONSE;

  if (debug) {
    fprintf(where,"recv_sctp_stream_1toMany: the response type is set...\n");
    fflush(where);
  }

  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */

  if (debug) {
    fprintf(where,"recv_sctp_stream_1toMany: requested alignment of %d\n",
	    sctp_stream_request->recv_alignment);
    fflush(where);
  }

  /* create_data_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = sctp_stream_request->send_buf_size;
  lsr_size_req = sctp_stream_request->recv_buf_size;
  loc_nodelay = sctp_stream_request->no_delay;
  loc_rcvavoid = sctp_stream_request->so_rcvavoid;
  loc_sndavoid = sctp_stream_request->so_sndavoid;
  non_block = sctp_stream_request->non_blocking;

  set_hostname_and_port(local_name,
			port_buffer,
			nf_to_af(sctp_stream_request->ipfamily),
			sctp_stream_request->port);

  local_res = complete_addrinfo(local_name,
				local_name,
				port_buffer,
				nf_to_af(sctp_stream_request->ipfamily),
				SOCK_SEQPACKET,
				IPPROTO_SCTP,
				0);

  s_recv = create_data_socket(local_res);

  if (s_recv < 0) {
    netperf_response.content.serv_errno = errno;
    send_response();
    exit(1);
  }

  /* what sort of sizes did we end-up with? */
  if (sctp_stream_request->receive_size == 0) {
    if (lsr_size > 0) {
      recv_size = lsr_size;
    }
    else {
      recv_size = 4096;
    }
  }
  else {
    recv_size = sctp_stream_request->receive_size;
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
				   sctp_stream_request->recv_alignment,
				   sctp_stream_request->recv_offset);

  if (debug) {
    fprintf(where,"recv_sctp_stream: receive alignment and offset set...\n");
    fflush(where);
  }

  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_recv, 5) == -1) {
    netperf_response.content.serv_errno = errno;
    close(s_recv);
    send_response();

    exit(1);
  }

  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_recv,
		  (struct sockaddr *)&myaddr_in,
		  &addrlen) == -1){
    netperf_response.content.serv_errno = errno;
    close(s_recv);
    send_response();

    exit(1);
  }

  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */

  sctp_stream_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response.content.serv_errno   = 0;

  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */

  sctp_stream_response->cpu_rate = (float)0.0; 	/* assume no cpu */
  if (sctp_stream_request->measure_cpu) {
    sctp_stream_response->measure_cpu = 1;
    sctp_stream_response->cpu_rate =
      calibrate_local_cpu(sctp_stream_request->cpu_rate);
  }
  else {
    sctp_stream_response->measure_cpu = 0;
  }

  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  sctp_stream_response->send_buf_size = lss_size;
  sctp_stream_response->recv_buf_size = lsr_size;
  sctp_stream_response->no_delay = loc_nodelay;
  sctp_stream_response->so_rcvavoid = loc_rcvavoid;
  sctp_stream_response->so_sndavoid = loc_sndavoid;
  sctp_stream_response->receive_size = recv_size;

  send_response();


  sctp_enable_events(s_recv, SCTP_ASSOC_CHANGE_EV | SCTP_SHUTDOWN_EV);

  /* now that we are connected, mark the socket as non-blocking */
  if (non_block) {
      if (!set_nonblock(s_recv)) {
	close(s_recv);
	exit(1);
      }
  }


  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */

  cpu_start(sctp_stream_request->measure_cpu);

  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */

#ifdef DIRTY
    /* we want to dirty some number of consecutive integers in the buffer */
    /* we are about to recv. we may also want to bring some number of */
    /* them cleanly into the cache. The clean ones will follow any dirty */
    /* ones into the cache. */

  dirty_count = sctp_stream_request->dirty_count;
  clean_count = sctp_stream_request->clean_count;
  message_int_ptr = (int *)recv_ring->buffer_ptr;
  for (i = 0; i < dirty_count; i++) {
    *message_int_ptr = rand();
    message_int_ptr++;
  }
  for (i = 0; i < clean_count; i++) {
    dirty_count = *message_int_ptr;
    message_int_ptr++;
  }
#endif /* DIRTY */

  bytes_received = 0;
  receive_calls  = 0;

  while ((len = sctp_recvmsg(s_recv, recv_ring->buffer_ptr, recv_size,
			     NULL, 0,  /* we don't care who it's from */
			     NULL, &msg_flags)) != 0) {
    if (len < 0) {
      if (non_block && errno == EAGAIN) {
	Set_errno(0);
	continue;
      }
      netperf_response.content.serv_errno = errno;
      send_response();
      close(s_recv);
      exit(1);
    }

    if (msg_flags & MSG_NOTIFICATION) {
	if (sctp_process_event(s_recv, recv_ring->buffer_ptr) == SCTP_CLOSE)
	    break;

	continue;
    }

    bytes_received += len;
    receive_calls++;

    /* more to the next buffer in the recv_ring */
    recv_ring = recv_ring->next;

#ifdef PAUSE
    sleep(1);
#endif /* PAUSE */

#ifdef DIRTY
    message_int_ptr = (int *)(recv_ring->buffer_ptr);
    for (i = 0; i < dirty_count; i++) {
      *message_int_ptr = rand();
      message_int_ptr++;
    }
    for (i = 0; i < clean_count; i++) {
      dirty_count = *message_int_ptr;
      message_int_ptr++;
    }
#endif /* DIRTY */

#ifdef DO_SELECT
	FD_SET(s_recv,&readfds);
	select(s_recv+1,&readfds,NULL,NULL,&timeout);
#endif /* DO_SELECT */

  }

  /* perform a shutdown to signal the sender.  in this case, sctp
   * will close all associations on this socket
   */
  if (close(s_recv) == -1) {
      netperf_response.content.serv_errno = errno;
      send_response();
      exit(1);
  }

  cpu_stop(sctp_stream_request->measure_cpu,&elapsed_time);

  /* send the results to the sender			*/

  if (debug) {
    fprintf(where,
	    "recv_sctp_stream: got %g bytes\n",
	    bytes_received);
    fprintf(where,
	    "recv_sctp_stream: got %d recvs\n",
	    receive_calls);
    fflush(where);
  }

  sctp_stream_results->bytes_received	= htond(bytes_received);
  sctp_stream_results->elapsed_time	= elapsed_time;
  sctp_stream_results->recv_calls	= receive_calls;

  if (sctp_stream_request->measure_cpu) {
    sctp_stream_results->cpu_util	= calc_cpu_util(0.0);
  };

  if (debug) {
    fprintf(where,
	    "recv_sctp_stream: test complete, sending results.\n");
    fprintf(where,
	    "                 bytes_received %g receive_calls %d\n",
	    bytes_received,
	    receive_calls);
    fprintf(where,
	    "                 len %d\n",
	    len);
    fflush(where);
  }

  sctp_stream_results->cpu_method = cpu_method;
  sctp_stream_results->num_cpus   = lib_num_loc_cpus;
  send_response();
}


 /* this routine implements the sending (netperf) side of the SCTP_RR */
 /* test. */

void
send_sctp_rr( char remote_host[] )
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
bytes  bytes  bytes   bytes  secs.   per sec  %% %c    %% %c    us/Tr   us/Tr\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c\n";

  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f  %-6.2f %-6.2f %-6.3f  %-6.3f\n";

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
  int	send_socket;
  int	trans_remaining;
  int   msg_flags = 0;
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

  struct addrinfo *remote_res;
  struct addrinfo *local_res;

  struct	sctp_rr_request_struct	*sctp_rr_request;
  struct	sctp_rr_response_struct	*sctp_rr_response;
  struct	sctp_rr_results_struct	*sctp_rr_result;

#ifdef WANT_INTERVALS
  int	interval_count;
  sigset_t signal_set;
#endif /* WANT_INTERVALS */

  sctp_rr_request =
    (struct sctp_rr_request_struct *)netperf_request.content.test_specific_data;
  sctp_rr_response =
    (struct sctp_rr_response_struct *)netperf_response.content.test_specific_data;
  sctp_rr_result =
    (struct sctp_rr_results_struct *)netperf_response.content.test_specific_data;

#ifdef WANT_HISTOGRAM
  time_hist = HIST_new_n(1);
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
		     IPPROTO_SCTP,
		     0);

  if ( print_headers ) {
    print_top_test_header("SCTP REQUEST/RESPONSE TEST", local_res, remote_res);
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
    send_socket = create_data_socket(local_res);

    if (send_socket < 0){
      perror("netperf: send_sctp_rr: sctp stream data socket");
      exit(1);
    }

    if (debug) {
      fprintf(where,"send_sctp_rr: send_socket obtained...\n");
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

    /* Tell the remote end to do a listen. The server alters the socket */
    /* paramters on the other side at this point, hence the reason for */
    /* all the values being passed in the setup message. If the user did */
    /* not specify any of the parameters, they will be passed as 0, which */
    /* will indicate to the remote that no changes beyond the system's */
    /* default should be used. Alignment is the exception, it will */
    /* default to 8, which will be no alignment alterations. */

    netperf_request.content.request_type	=	DO_SCTP_RR;
    sctp_rr_request->recv_buf_size	=	rsr_size_req;
    sctp_rr_request->send_buf_size	=	rss_size_req;
    sctp_rr_request->recv_alignment     =	remote_recv_align;
    sctp_rr_request->recv_offset        =	remote_recv_offset;
    sctp_rr_request->send_alignment     =	remote_send_align;
    sctp_rr_request->send_offset	=	remote_send_offset;
    sctp_rr_request->request_size	=	req_size;
    sctp_rr_request->response_size	=	rsp_size;
    sctp_rr_request->no_delay	        =	rem_nodelay;
    sctp_rr_request->measure_cpu	=	remote_cpu_usage;
    sctp_rr_request->cpu_rate	        =	remote_cpu_rate;
    sctp_rr_request->so_rcvavoid	        =	rem_rcvavoid;
    sctp_rr_request->so_sndavoid	        =	rem_sndavoid;
    if (test_time) {
      sctp_rr_request->test_length	=	test_time;
    }
    else {
      sctp_rr_request->test_length	=	test_trans * -1;
    }
    sctp_rr_request->non_blocking	= 	non_block;
    sctp_rr_request->ipfamily           = af_to_nf(remote_res->ai_family);

    if (debug > 1) {
      fprintf(where,"netperf: send_sctp_rr: requesting SCTP rr test\n");
    }

    send_request();

    /* The response from the remote will contain all of the relevant 	*/
    /* socket parameters for this test type. We will put them back into */
    /* the variables here so they can be displayed if desired.  The	*/
    /* remote will have calibrated CPU if necessary, and will have done	*/
    /* all the needed set-up we will have calibrated the cpu locally	*/
    /* before sending the request, and will grab the counter value right*/
    /* after the connect returns. The remote will grab the counter right*/
    /* after the accept call. This saves the hassle of extra messages	*/
    /* being sent for the sctp tests.					*/

    recv_response();

    if (!netperf_response.content.serv_errno) {
      if (debug)
	fprintf(where,"remote listen done.\n");
      rsr_size          = sctp_rr_response->recv_buf_size;
      rss_size          = sctp_rr_response->send_buf_size;
      rem_nodelay       = sctp_rr_response->no_delay;
      remote_cpu_usage  = sctp_rr_response->measure_cpu;
      remote_cpu_rate   = sctp_rr_response->cpu_rate;
      /* make sure that port numbers are in network order */
      set_port_number(remote_res,
		      (unsigned short)sctp_rr_response->data_port_number);
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

    /*Connect up to the remote port on the data socket  */
    if (connect(send_socket,
		remote_res->ai_addr,
		remote_res->ai_addrlen) <0){
      perror("netperf: send_sctp_rr data socket connect failed");
      exit(1);
    }

    /* don't need events for 1-to-1 API with request-response tests */
    sctp_enable_events(send_socket, 0);

    /* set non-blocking if needed */
    if (non_block) {
       if (!set_nonblock(send_socket)) {
	    close(send_socket);
	    exit(1);
	}
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
    if ((interval_burst) || (demo_mode)) {
      /* zero means that we never pause, so we never should need the */
      /* interval timer, unless we are in demo_mode */
      start_itimer(interval_wate);
    }
    interval_count = interval_burst;
    /* get the signal set for the call to sigsuspend */
    if (sigprocmask(SIG_BLOCK, (sigset_t *)NULL, &signal_set) != 0) {
      fprintf(where,
	      "send_sctp_rr: unable to get sigmask errno %d\n",
	      errno);
      fflush(where);
      exit(1);
    }
#endif /* WANT_INTERVALS */

    /* We use an "OR" to control test execution. When the test is */
    /* controlled by time, the byte count check will always return false. */
    /* When the test is controlled by byte count, the time test will */
    /* always return false. When the test is finished, the whole */
    /* expression will go false and we will stop sending data. I think I */
    /* just arbitrarily decrement trans_remaining for the timed test, but */
    /* will not do that just yet... One other question is whether or not */
    /* the send buffer and the receive buffer should be the same buffer. */

#ifdef WANT_FIRST_BURST
    {
      int i;
      for (i = 0; i < first_burst_size; i++) {
 	if((len=sctp_sendmsg(send_socket,
 			     send_ring->buffer_ptr, req_size,
 			     NULL, 0,	/* don't need addrs with 1-to-1 */
 			     0, 0, 0, 0, 0)) != req_size) {
	  /* we should never hit the end of the test in the first burst */
	  perror("send_sctp_rr: initial burst data send error");
	  exit(1);
	}
      }
    }
#endif /* WANT_FIRST_BURST */

    while ((!times_up) || (trans_remaining > 0)) {
      /* send the request. we assume that if we use a blocking socket, */
      /* the request will be sent at one shot. */

#ifdef WANT_HISTOGRAM
      /* timestamp just before our call to send, and then again just */
      /* after the receive raj 8/94 */
      HIST_timestamp_start(time_hist);
#endif /* WANT_HISTOGRAM */

      while ((len=sctp_sendmsg(send_socket,
			       send_ring->buffer_ptr, req_size,
			       NULL, 0, /* don't need addrs with 1-to-1 */
			       0, 0, 0, 0, 0)) != req_size) {
	if (non_block && errno == EAGAIN) {
	    /* try sending again */
	    continue;
	} else if (SOCKET_EINTR(len) || (errno == 0)) {
	  /* we hit the end of a */
	  /* timed test. */
	  timed_out = 1;
	  break;
	}
	perror("send_sctp_rr: data send error");
	exit(1);
      }

      if (timed_out) {
	/* we timed out while sending. break out another level */
	break;
      }
      send_ring = send_ring->next;

      /* receive the response */
      rsp_bytes_left = rsp_size;
      temp_message_ptr  = recv_ring->buffer_ptr;
      do {
	msg_flags = 0;
	if ((rsp_bytes_recvd=sctp_recvmsg(send_socket,
					 temp_message_ptr, rsp_bytes_left,
					 NULL, 0,
					 NULL, &msg_flags)) < 0) {
	  if (errno == EINTR) {
	    /* We hit the end of a timed test. */
	    timed_out = 1;
	    break;
	  } else if (non_block && errno == EAGAIN) {
	      continue;
	  }
	  perror("send_sctp_rr: data recv error");
	  exit(1);
	}
	rsp_bytes_left -= rsp_bytes_recvd;
	temp_message_ptr  += rsp_bytes_recvd;
      }	while (!(msg_flags & MSG_EOR));

      recv_ring = recv_ring->next;

      if (timed_out) {
	/* we may have been in a nested while loop - we need */
	/* another call to break. */
	break;
      }

#ifdef WANT_HISTOGRAM
      HIST_timestamp_stop_add(time_hist);
#endif /* WANT_HISTOGRAM */
#ifdef WANT_INTERVALS
      if (demo_mode) {
	units_this_tick += 1;
      }
      /* in this case, the interval count is the count-down couter */
      /* to decide to sleep for a little bit */
      if ((interval_burst) && (--interval_count == 0)) {
	/* call sigsuspend and wait for the interval timer to get us */
	/* out */
	if (debug > 1) {
	  fprintf(where,"about to suspend\n");
	  fflush(where);
	}
	if (sigsuspend(&signal_set) == EFAULT) {
	  fprintf(where,
		  "send_sctp_rr: fault with signal set!\n");
	  fflush(where);
	  exit(1);
	}
	interval_count = interval_burst;
      }
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

    /* At this point we used to call shutdown on the data socket to be */
    /* sure all the data was delivered, but this was not germane in a */
    /* request/response test, and it was causing the tests to "hang" when */
    /* they were being controlled by time. So, I have replaced this */
    /* shutdown call with a call to close that can be found later in the */
    /* procedure. */

    /* this call will always give us the elapsed time for the test, and */
    /* will also store-away the necessaries for cpu utilization */

    cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being */
						/* measured? how long */
						/* did we really run? */

    /* Get the statistics from the remote end. The remote will have */
    /* calculated CPU utilization. If it wasn't supposed to care, it */
    /* will return obvious values. */

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

    /* We now calculate what our throughput was for the test. */

    bytes_xferd	= (req_size * nummessages) + (rsp_size * nummessages);
    thruput	= nummessages/elapsed_time;

    if (local_cpu_usage || remote_cpu_usage) {
      /* We must now do a little math for service demand and cpu */
      /* utilization for the system(s) */
      /* Of course, some of the information might be bogus because */
      /* there was no idle counter in the kernel(s). We need to make */
      /* a note of this for the user's benefit...*/
      if (local_cpu_usage) {
	local_cpu_utilization = calc_cpu_util(0.0);
	/* since calc_service demand is doing ms/Kunit we will */
	/* multiply the number of transaction by 1024 to get */
	/* "good" numbers */
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
	remote_cpu_utilization = sctp_rr_result->cpu_util;
	/* since calc_service demand is doing ms/Kunit we will */
	/* multiply the number of transaction by 1024 to get */
	/* "good" numbers */
	remote_service_demand = calc_service_demand((double) nummessages*1024,
						    0.0,
						    remote_cpu_utilization,
						    sctp_rr_result->num_cpus);
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

    /* we are now done with the socket, so close it */
    close(send_socket);

  }

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
    remote_cpu_method = format_cpu_method(sctp_rr_result->cpu_method);

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


 /* this routine implements the receive (netserver) side of a TCP_RR */
 /* test */
void
recv_sctp_rr( void )
{

  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  struct addrinfo *local_res;
  char local_name[BUFSIZ];
  char port_buffer[PORTBUFSIZE];

  struct sockaddr_in        myaddr_in, peeraddr_in;
  int	s_listen, s_data;
  socklen_t 	addrlen;
  char	*temp_message_ptr;
  int	trans_received;
  int	trans_remaining;
  int	bytes_sent;
  int	request_bytes_recvd;
  int	request_bytes_remaining;
  int	timed_out = 0;
  float	elapsed_time;

  struct	sctp_rr_request_struct	*sctp_rr_request;
  struct	sctp_rr_response_struct	*sctp_rr_response;
  struct	sctp_rr_results_struct	*sctp_rr_results;

  sctp_rr_request =
    (struct sctp_rr_request_struct *)netperf_request.content.test_specific_data;
  sctp_rr_response =
    (struct sctp_rr_response_struct *)netperf_response.content.test_specific_data;
  sctp_rr_results =
    (struct sctp_rr_results_struct *)netperf_response.content.test_specific_data;

  if (debug) {
    fprintf(where,"netserver: recv_sctp_rr: entered...\n");
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
    fprintf(where,"recv_sctp_rr: setting the response type...\n");
    fflush(where);
  }

  netperf_response.content.response_type = SCTP_RR_RESPONSE;

  if (debug) {
    fprintf(where,"recv_sctp_rr: the response type is set...\n");
    fflush(where);
  }

  /* allocate the recv and send rings with the requested alignments */
  /* and offsets. raj 7/94 */
  if (debug) {
    fprintf(where,"recv_sctp_rr: requested recv alignment of %d offset %d\n",
	    sctp_rr_request->recv_alignment,
	    sctp_rr_request->recv_offset);
    fprintf(where,"recv_sctp_rr: requested send alignment of %d offset %d\n",
	    sctp_rr_request->send_alignment,
	    sctp_rr_request->send_offset);
    fflush(where);
  }

  /* at some point, these need to come to us from the remote system */
  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  send_ring = allocate_buffer_ring(send_width,
				   sctp_rr_request->response_size,
				   sctp_rr_request->send_alignment,
				   sctp_rr_request->send_offset);

  recv_ring = allocate_buffer_ring(recv_width,
				   sctp_rr_request->request_size,
				   sctp_rr_request->recv_alignment,
				   sctp_rr_request->recv_offset);


  /* Grab a socket to listen on, and then listen on it. */

  if (debug) {
    fprintf(where,"recv_sctp_rr: grabbing a socket...\n");
    fflush(where);
  }

  /* create_data_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = sctp_rr_request->send_buf_size;
  lsr_size_req = sctp_rr_request->recv_buf_size;
  loc_nodelay = sctp_rr_request->no_delay;
  loc_rcvavoid = sctp_rr_request->so_rcvavoid;
  loc_sndavoid = sctp_rr_request->so_sndavoid;
  non_block = sctp_rr_request->non_blocking;

  set_hostname_and_port(local_name,
			port_buffer,
			nf_to_af(sctp_rr_request->ipfamily),
			sctp_rr_request->port);

  local_res = complete_addrinfo(local_name,
				local_name,
				port_buffer,
				nf_to_af(sctp_rr_request->ipfamily),
				SOCK_STREAM,
				IPPROTO_SCTP,
				0);

  s_listen = create_data_socket(local_res);

  if (s_listen < 0) {
    netperf_response.content.serv_errno = errno;
    send_response();

    exit(1);
  }

  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == -1) {
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();

    exit(1);
  }


  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen,
		  (struct sockaddr *)&myaddr_in, &addrlen) == -1){
    netperf_response.content.serv_errno = errno;
    close(s_listen);
    send_response();

    exit(1);
  }

  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */

  sctp_rr_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response.content.serv_errno   = 0;

  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */

  sctp_rr_response->cpu_rate = (float)0.0; 	/* assume no cpu */
  sctp_rr_response->measure_cpu = 0;

  if (sctp_rr_request->measure_cpu) {
    sctp_rr_response->measure_cpu = 1;
    sctp_rr_response->cpu_rate = calibrate_local_cpu(sctp_rr_request->cpu_rate);
  }


  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  sctp_rr_response->send_buf_size = lss_size;
  sctp_rr_response->recv_buf_size = lsr_size;
  sctp_rr_response->no_delay = loc_nodelay;
  sctp_rr_response->so_rcvavoid = loc_rcvavoid;
  sctp_rr_response->so_sndavoid = loc_sndavoid;
  sctp_rr_response->test_length = sctp_rr_request->test_length;
  send_response();

  addrlen = sizeof(peeraddr_in);

  if ((s_data = accept(s_listen,
		       (struct sockaddr *)&peeraddr_in,
		       &addrlen)) == -1) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);

    exit(1);
  }

  /* we do not need events on a 1-to-1 RR test.  The test will finish
   * once all transactions are done.
   */

  /* now that we are connected, mark the socket as non-blocking */
  if (non_block) {
    if (!set_nonblock(s_data)) {
      perror("netperf: set_nonblock");
	exit(1);
    }
  }

#ifdef KLUDGE_SOCKET_OPTIONS
  /* this is for those systems which *INCORRECTLY* fail to pass */
  /* attributes across an accept() call. Including this goes against */
  /* my better judgement :( raj 11/95 */

  kludge_socket_options(s_data);

#endif /* KLUDGE_SOCKET_OPTIONS */

  if (debug) {
    fprintf(where,"recv_sctp_rr: accept completes on the data connection.\n");
    fflush(where);
  }

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */

  cpu_start(sctp_rr_request->measure_cpu);

  /* The loop will exit when we hit the end of the test time, or when */
  /* we have exchanged the requested number of transactions. */

  if (sctp_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
    start_timer(sctp_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = sctp_rr_request->test_length * -1;
  }

  trans_received = 0;

  while ((!times_up) || (trans_remaining > 0)) {
    int msg_flags = 0;

    temp_message_ptr = recv_ring->buffer_ptr;
    request_bytes_remaining = sctp_rr_request->request_size;
    while(!(msg_flags & MSG_EOR)) {
      if((request_bytes_recvd=sctp_recvmsg(s_data,
					temp_message_ptr,
					request_bytes_remaining,
					NULL, 0,
					NULL, &msg_flags)) < 0) {
	if (errno == EINTR) {
	  /* the timer popped */
	  timed_out = 1;
	  break;
	} else if (non_block && errno == EAGAIN) {
	    continue;  /* while request_bytes_remaining */
	}
	netperf_response.content.serv_errno = errno;
	send_response();
	exit(1);
      }
      request_bytes_remaining -= request_bytes_recvd;
      temp_message_ptr += request_bytes_recvd;
    }

    recv_ring = recv_ring->next;

    if (timed_out) {
      /* we hit the end of the test based on time - lets */
      /* bail out of here now... */
      if (debug) {
	fprintf(where,"yo55\n");
	fflush(where);
      }
      break;
    }


    /* Now, send the response to the remote
     * In 1-to-1 API destination addr is not needed.
     */
    while ((bytes_sent=sctp_sendmsg(s_data,
				    send_ring->buffer_ptr,
				    sctp_rr_request->response_size,
				    NULL, 0,
				    0, 0, 0, 0, 0)) == -1) {
      if (errno == EINTR) {
	/* the test timer has popped */
	timed_out = 1;
	break;
      } else if (non_block && errno == EAGAIN) {
	 continue;
      }

      netperf_response.content.serv_errno = 982;
      send_response();
      exit(1);
    }

    if (timed_out) {
      /* we hit the end of the test based on time - lets */
      /* bail out of here now... */
      if (debug) {
	fprintf(where,"yo6\n");
	fflush(where);
      }
      break;
    }

    send_ring = send_ring->next;

    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
  }


  /* The loop now exits due to timeout or transaction count being */
  /* reached */

  cpu_stop(sctp_rr_request->measure_cpu,&elapsed_time);

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
	    "recv_sctp_rr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }

  sctp_rr_results->bytes_received = (trans_received *
				    (sctp_rr_request->request_size +
				     sctp_rr_request->response_size));
  sctp_rr_results->trans_received = trans_received;
  sctp_rr_results->elapsed_time   = elapsed_time;
  sctp_rr_results->cpu_method     = cpu_method;
  sctp_rr_results->num_cpus       = lib_num_loc_cpus;
  if (sctp_rr_request->measure_cpu) {
    sctp_rr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }

  if (debug) {
    fprintf(where,
	    "recv_sctp_rr: test complete, sending results.\n");
    fflush(where);
  }

  /* we are now done with the sockets */
  send_response();

  close(s_data);
  close(s_listen);

}



/* this routine implements the sending (netperf) side of the
   SCTP_RR_1TOMANY test */

void
send_sctp_rr_1toMany( char remote_host[] )
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
bytes  bytes  bytes   bytes  secs.   per sec  %% %c    %% %c    us/Tr   us/Tr\n\n";

  char *cpu_fmt_0 =
    "%6.3f %c\n";

  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f  %-6.2f %-6.2f %-6.3f  %-6.3f\n";

  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";

  char *ksink_fmt = "\
Alignment      Offset\n\
Local  Remote  Local  Remote\n\
Send   Recv    Send   Recv\n\
%5d  %5d   %5d  %5d\n";


  int			timed_out = 0;
  float			elapsed_time;

  int	len, j = 0;
  char	*temp_message_ptr;
  int	nummessages;
  int	*send_socket;
  int	trans_remaining;
  double	bytes_xferd;
  int   msg_flags = 0;

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

  struct	sctp_rr_request_struct	*sctp_rr_request;
  struct	sctp_rr_response_struct	*sctp_rr_response;
  struct	sctp_rr_results_struct	*sctp_rr_result;

#ifdef WANT_INTERVALS
  int	interval_count;
  sigset_t signal_set;
#endif /* WANT_INTERVALS */

  sctp_rr_request =
    (struct sctp_rr_request_struct *)netperf_request.content.test_specific_data;
  sctp_rr_response =
    (struct sctp_rr_response_struct *)netperf_response.content.test_specific_data;
  sctp_rr_result =
    (struct sctp_rr_results_struct *)netperf_response.content.test_specific_data;

#ifdef WANT_HISTOGRAM
  time_hist = HIST_new_n(1);
#endif /* WANT_HISTOGRAM */

  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */

  complete_addrinfos(&remote_res,
		     &local_res,
		     remote_host,
		     SOCK_SEQPACKET,
		     IPPROTO_SCTP,
		     0);

  if ( print_headers ) {
    print_top_test_header("SCTP 1-TO-MANY REQUEST/RESPONSE TEST",local_res,remote_res);
  }

  /* initialize a few counters */

  send_ring = NULL;
  recv_ring = NULL;
  confidence_iteration = 1;
  init_stat();

  send_socket = malloc(sizeof(int) * num_associations);
  if (send_socket == NULL) {
      fprintf(where,
	      "Could not create the socket array for %d associations",
	      num_associations);
      fflush(where);
      exit(1);
  }

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

    /* If the user has requested cpu utilization measurements, we must */
    /* calibrate the cpu(s). We will perform this task within the tests */
    /* themselves. If the user has specified the cpu rate, then */
    /* calibrate_local_cpu will return rather quickly as it will have */
    /* nothing to do. If local_cpu_rate is zero, then we will go through */
    /* all the "normal" calibration stuff and return the rate back.*/

    if (local_cpu_usage) {
      local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
    }

    /* Tell the remote end to do a listen. The server alters the socket */
    /* paramters on the other side at this point, hence the reason for */
    /* all the values being passed in the setup message. If the user did */
    /* not specify any of the parameters, they will be passed as 0, which */
    /* will indicate to the remote that no changes beyond the system's */
    /* default should be used. Alignment is the exception, it will */
    /* default to 8, which will be no alignment alterations. */

    netperf_request.content.request_type =	DO_SCTP_RR_MANY;
    sctp_rr_request->recv_buf_size     =	rsr_size_req;
    sctp_rr_request->send_buf_size     =	rss_size_req;
    sctp_rr_request->recv_alignment    =	remote_recv_align;
    sctp_rr_request->recv_offset       =	remote_recv_offset;
    sctp_rr_request->send_alignment    =	remote_send_align;
    sctp_rr_request->send_offset       =	remote_send_offset;
    sctp_rr_request->request_size      =	req_size;
    sctp_rr_request->response_size     =	rsp_size;
    sctp_rr_request->no_delay	       =	rem_nodelay;
    sctp_rr_request->measure_cpu       =	remote_cpu_usage;
    sctp_rr_request->cpu_rate	       =	remote_cpu_rate;
    sctp_rr_request->so_rcvavoid       =	rem_rcvavoid;
    sctp_rr_request->so_sndavoid       =	rem_sndavoid;
    if (test_time) {
      sctp_rr_request->test_length     =	test_time;
    }
    else {
      sctp_rr_request->test_length     =	test_trans * num_associations
						* -1;
    }
    sctp_rr_request->non_blocking      = 	non_block;
    sctp_rr_request->port              =       atoi(remote_data_port);
    sctp_rr_request->ipfamily          =       af_to_nf(remote_res->ai_family);
    if (debug > 1) {
      fprintf(where,"netperf: send_sctp_rr_1toMany: requesting SCTP rr test\n");
    }

    send_request();

    /* The response from the remote will contain all of the relevant 	*/
    /* socket parameters for this test type. We will put them back into */
    /* the variables here so they can be displayed if desired.  The	*/
    /* remote will have calibrated CPU if necessary, and will have done	*/
    /* all the needed set-up we will have calibrated the cpu locally	*/
    /* before sending the request, and will grab the counter value right*/
    /* after the connect returns. The remote will grab the counter right*/
    /* after the accept call. This saves the hassle of extra messages	*/
    /* being sent for the sctp tests.					*/

    recv_response();

    if (!netperf_response.content.serv_errno) {
      rsr_size          = sctp_rr_response->recv_buf_size;
      rss_size          = sctp_rr_response->send_buf_size;
      rem_nodelay       = sctp_rr_response->no_delay;
      remote_cpu_usage  = sctp_rr_response->measure_cpu;
      remote_cpu_rate   = sctp_rr_response->cpu_rate;
      /* make sure that port numbers are in network order */
      set_port_number(remote_res,
		      (unsigned short)sctp_rr_response->data_port_number);
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

    /*set up the data socket list  */
    for (j = 0; j < num_associations; j++) {
      send_socket[j] = create_data_socket(local_res);

      if (send_socket < 0){
	perror("netperf: send_sctp_rr_1toMany: sctp stream data socket");
	exit(1);
      }

      /*Connect up to the remote port on the data socket  */
      if (connect(send_socket[j],
		  remote_res->ai_addr,
		  remote_res->ai_addrlen) < 0){
	perror("netperf: data socket connect failed");

	exit(1);
      }

      /* The client end of the 1-to-Many test uses 1-to-1 sockets.
       * it doesn't need events.
       */
      sctp_enable_events(send_socket[j], 0);

      if (non_block) {
        if (!set_nonblock(send_socket[j])) {
	  close(send_socket[j]);
	  exit(1);
	}
      }
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
      trans_remaining = test_bytes * num_associations;
      times_up = 1;
    }

    /* The cpu_start routine will grab the current time and possibly */
    /* value of the idle counter for later use in measuring cpu */
    /* utilization and/or service demand and thruput. */

    cpu_start(local_cpu_usage);

#ifdef WANT_INTERVALS
    if ((interval_burst) || (demo_mode)) {
      /* zero means that we never pause, so we never should need the */
      /* interval timer, unless we are in demo_mode */
      start_itimer(interval_wate);
    }
    interval_count = interval_burst;
    /* get the signal set for the call to sigsuspend */
    if (sigprocmask(SIG_BLOCK, (sigset_t *)NULL, &signal_set) != 0) {
      fprintf(where,
	      "send_sctp_rr_1toMany: unable to get sigmask errno %d\n",
	      errno);
      fflush(where);
      exit(1);
    }
#endif /* WANT_INTERVALS */

    /* We use an "OR" to control test execution. When the test is */
    /* controlled by time, the byte count check will always return false. */
    /* When the test is controlled by byte count, the time test will */
    /* always return false. When the test is finished, the whole */
    /* expression will go false and we will stop sending data. I think I */
    /* just arbitrarily decrement trans_remaining for the timed test, but */
    /* will not do that just yet... One other question is whether or not */
    /* the send buffer and the receive buffer should be the same buffer. */

#ifdef WANT_FIRST_BURST
    {
      int i;
      for (j = 0; j < num_associations; j++) {
	  for (i = 0; i < first_burst_size; i++) {
	    if((len=sctp_sendmsg(send_socket[j],
			 send_ring->buffer_ptr, req_size,
			 remote_res->ai_addr,
			 remote_res->ai_addrlen,
			 0, 0, 0, 0, 0)) != req_size) {
	      /* we should never hit the end of the test in the first burst */
	      perror("send_sctp_rr_1toMany: initial burst data send error");
	      exit(1);
	    }
	  }
      }
    }
#endif /* WANT_FIRST_BURST */

    while ((!times_up) || (trans_remaining > 0)) {
      /* send the request. we assume that if we use a blocking socket, */
      /* the request will be sent at one shot. */

      /* this is a fairly poor way of testing 1toMany connections.
       * For each association we measure round trip time to account for
       * any delay in lookups and delivery.  To stress the server a bit
       * more we would need a distributed client test, or at least multiple
       * processes.  I want to force as much paralellism as possible, but
       * this will do for the fist take. vlad
       */
      for (j = 0; j < num_associations; j++) {
#ifdef WANT_HISTOGRAM
	/* timestamp just before our call to send, and then again just */
	/* after the receive raj 8/94 */
	HIST_timestamp_start(time_hist);
#endif /* WANT_HISTOGRAM */

	while ((len=sctp_sendmsg(send_socket[j],
				 send_ring->buffer_ptr, req_size,
				 remote_res->ai_addr,
				 remote_res->ai_addrlen,
				 0, 0, 0, 0, 0)) != req_size) {
	  if (non_block && errno == EAGAIN) {
	    /* try sending again */
	    continue;
	  } else if ((errno == EINTR) || (errno == 0)) {
	    /* we hit the end of a */
	    /* timed test. */
	    timed_out = 1;
	    break;
	  }
	  perror("send_sctp_rr_1toMany: data send error");
	  exit(1);
	}

	if (timed_out) {
	  /* we may have been in a nested while loop - we need */
	  /* another call to break. */
	  break;
	}

	/* setup for the next time */
	send_ring = send_ring->next;

	rsp_bytes_left = rsp_size;
	temp_message_ptr  = recv_ring->buffer_ptr;
	while (!(msg_flags & MSG_EOR)) {
	  if((rsp_bytes_recvd = sctp_recvmsg(send_socket[j],
					     temp_message_ptr,
					     rsp_bytes_left,
					     NULL, 0,
					     NULL, &msg_flags)) < 0) {
	    if (errno == EINTR) {
	      /* We hit the end of a timed test. */
	      timed_out = 1;
	      break;
	    } else if (non_block && errno == EAGAIN) {
	      continue;
	    }
	    perror("send_sctp_rr_1toMany: data recv error");
	    exit(1);
	  }
	  rsp_bytes_left -= rsp_bytes_recvd;
	  temp_message_ptr  += rsp_bytes_recvd;
	}
	recv_ring = recv_ring->next;

	if (timed_out) {
	  /* we may have been in a nested while loop - we need */
	  /* another call to break. */
	  break;
	}

#ifdef WANT_HISTOGRAM
	HIST_timestamp_stop_add(time_hist);
#endif /* WANT_HISTOGRAM */

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
    }

    /* At this point we used to call shutdown on the data socket to be */
    /* sure all the data was delivered, but this was not germane in a */
    /* request/response test, and it was causing the tests to "hang" when */
    /* they were being controlled by time. So, I have replaced this */
    /* shutdown call with a call to close that can be found later in the */
    /* procedure. */

    /* this call will always give us the elapsed time for the test, and */
    /* will also store-away the necessaries for cpu utilization */

    cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being */
						/* measured? how long */
						/* did we really run? */

    /* Get the statistics from the remote end. The remote will have */
    /* calculated CPU utilization. If it wasn't supposed to care, it */
    /* will return obvious values. */

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

    /* We now calculate what our throughput was for the test. */

    bytes_xferd	= (req_size * nummessages) + (rsp_size * nummessages);
    thruput	= nummessages/elapsed_time;

    if (local_cpu_usage || remote_cpu_usage) {
      /* We must now do a little math for service demand and cpu */
      /* utilization for the system(s) */
      /* Of course, some of the information might be bogus because */
      /* there was no idle counter in the kernel(s). We need to make */
      /* a note of this for the user's benefit...*/
      if (local_cpu_usage) {
	local_cpu_utilization = calc_cpu_util(0.0);
	/* since calc_service demand is doing ms/Kunit we will */
	/* multiply the number of transaction by 1024 to get */
	/* "good" numbers */
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
	remote_cpu_utilization = sctp_rr_result->cpu_util;
	/* since calc_service demand is doing ms/Kunit we will */
	/* multiply the number of transaction by 1024 to get */
	/* "good" numbers */
	remote_service_demand = calc_service_demand((double) nummessages*1024,
						    0.0,
						    remote_cpu_utilization,
						    sctp_rr_result->num_cpus);
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

    /* we are now done with the socket, so close it */
    for (j = 0; j < num_associations; j++)
	close(send_socket[j]);
  }

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
    remote_cpu_method = format_cpu_method(sctp_rr_result->cpu_method);

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


 /* this routine implements the receive (netserver) side of a TCP_RR */
 /* test */
void
recv_sctp_rr_1toMany( void )
{

  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;


  struct sockaddr_in        myaddr_in; 	/* needed to get the port number */
  struct sockaddr_storage   peeraddr;   /* to communicate with peer */
  struct addrinfo *local_res;
  char   local_name[BUFSIZ];
  char   port_buffer[PORTBUFSIZE];
  int    msg_flags;

  int	s_rcv;
  socklen_t 	addrlen;
  int	trans_received;
  int	trans_remaining;
  int	bytes_sent;
  int	bytes_recvd;
  int	recv_buf_size;
  int	timed_out = 0;
  float	elapsed_time;

  struct	sctp_rr_request_struct	*sctp_rr_request;
  struct	sctp_rr_response_struct	*sctp_rr_response;
  struct	sctp_rr_results_struct	*sctp_rr_results;

  sctp_rr_request =
    (struct sctp_rr_request_struct *)netperf_request.content.test_specific_data;
  sctp_rr_response =
    (struct sctp_rr_response_struct *)netperf_response.content.test_specific_data;
  sctp_rr_results =
    (struct sctp_rr_results_struct *)netperf_response.content.test_specific_data;

  if (debug) {
    fprintf(where,"netserver: recv_sctp_rr_1toMany: entered...\n");
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
    fprintf(where,"recv_sctp_rr_1toMany: setting the response type...\n");
    fflush(where);
  }

  netperf_response.content.response_type = SCTP_RR_MANY_RESPONSE;

  if (debug) {
    fprintf(where,"recv_sctp_rr_1toMany: the response type is set...\n");
    fflush(where);
  }

  /* allocate the recv and send rings with the requested alignments */
  /* and offsets. raj 7/94 */
  if (debug) {
    fprintf(where,"recv_sctp_rr_1toMany: requested recv alignment of %d offset %d\n",
	    sctp_rr_request->recv_alignment,
	    sctp_rr_request->recv_offset);
    fprintf(where,"recv_sctp_rr_1toMany: requested send alignment of %d offset %d\n",
	    sctp_rr_request->send_alignment,
	    sctp_rr_request->send_offset);
    fflush(where);
  }

  /* at some point, these need to come to us from the remote system */
  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  send_ring = allocate_buffer_ring(send_width,
				   sctp_rr_request->response_size,
				   sctp_rr_request->send_alignment,
				   sctp_rr_request->send_offset);

  recv_ring = allocate_buffer_ring(recv_width,
				   sctp_rr_request->request_size,
				   sctp_rr_request->recv_alignment,
				   sctp_rr_request->recv_offset);


  /* create_data_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */
  lss_size_req = sctp_rr_request->send_buf_size;
  lsr_size_req = sctp_rr_request->recv_buf_size;
  loc_nodelay = sctp_rr_request->no_delay;
  loc_rcvavoid = sctp_rr_request->so_rcvavoid;
  loc_sndavoid = sctp_rr_request->so_sndavoid;
  non_block = sctp_rr_request->non_blocking;

  set_hostname_and_port(local_name,
		        port_buffer,
			nf_to_af(sctp_rr_request->ipfamily),
			sctp_rr_request->port);

  local_res = complete_addrinfo(local_name,
				local_name,
				port_buffer,
				nf_to_af(sctp_rr_request->ipfamily),
				SOCK_SEQPACKET,
				IPPROTO_SCTP,
				0);

  /* Grab a socket to listen on, and then listen on it. */
  if (debug) {
    fprintf(where,"recv_sctp_rr_1toMany: grabbing a socket...\n");
    fflush(where);
  }

  s_rcv = create_data_socket(local_res);

  if (s_rcv < 0) {
    netperf_response.content.serv_errno = errno;
    send_response();

    exit(1);
  }

  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_rcv, 5) == -1) {
    netperf_response.content.serv_errno = errno;
    close(s_rcv);
    send_response();

    exit(1);
  }


  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_rcv,
		  (struct sockaddr *)&myaddr_in, &addrlen) == -1){
    netperf_response.content.serv_errno = errno;
    close(s_rcv);
    send_response();

    exit(1);
  }

  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */

  sctp_rr_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response.content.serv_errno   = 0;

  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */

  sctp_rr_response->cpu_rate = (float)0.0; 	/* assume no cpu */
  sctp_rr_response->measure_cpu = 0;

  if (sctp_rr_request->measure_cpu) {
    sctp_rr_response->measure_cpu = 1;
    sctp_rr_response->cpu_rate = calibrate_local_cpu(sctp_rr_request->cpu_rate);
  }


  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  sctp_rr_response->send_buf_size = lss_size;
  sctp_rr_response->recv_buf_size = lsr_size;
  sctp_rr_response->no_delay = loc_nodelay;
  sctp_rr_response->so_rcvavoid = loc_rcvavoid;
  sctp_rr_response->so_sndavoid = loc_sndavoid;
  sctp_rr_response->test_length = sctp_rr_request->test_length;
  send_response();

  /* Don't need events */
  sctp_enable_events(s_rcv, 0);

  /* now that we are connected, mark the socket as non-blocking */
  if (non_block) {
    if (!set_nonblock(s_rcv)) {
      perror("netperf: set_nonblock");
	exit(1);
    }
  }

  /* FIXME:  The way 1-to-Many test operates right now, we are including
   * association setup time into our measurements.  The reason for this
   * is that the client creates multiple endpoints and connects each
   * endpoint to us using the connect call.  On this end we simply call
   * recvmsg() to get data becuase there is no equivalen of accept() for
   * 1-to-Many API.
   * I think this is OK, but if it were to be fixed, the server side
   * would need to know how many associations are being setup and
   * have a recvmsg() loop with SCTP_ASSOC_CHANGE events waiting for
   * all the associations to be be established.
   * I am punting on this for now.
   */


  addrlen = sizeof(peeraddr);

  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */

  cpu_start(sctp_rr_request->measure_cpu);

  /* The loop will exit when we hit the end of the test time, or when */
  /* we have exchanged the requested number of transactions. */

  if (sctp_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
    start_timer(sctp_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = sctp_rr_request->test_length * -1;
  }

  trans_received = 0;

  while ((!times_up) || (trans_remaining > 0)) {

    recv_buf_size = sctp_rr_request->request_size;

    /* Receive the data.  We don't particularly care which association
     * the data came in on.  We'll simply be doing a receive untill
     * we get and MSG_EOR flag (meaning that a single transmission was
     * received) and a send to the same address, so the RR would be for
     * the same associations.
     * We can get away with this because the client will establish all
     * the associations before transmitting any data.  Any partial data
     * will not have EOR thus will we will not send a response untill
     * we get everything.
     */

    do {
      msg_flags = 0;
      if((bytes_recvd = sctp_recvmsg(s_rcv,
				     recv_ring->buffer_ptr,
				     recv_buf_size,
				     (struct sockaddr *)&peeraddr, &addrlen,
				     0, &msg_flags)) == SOCKET_ERROR) {
	if (SOCKET_EINTR(bytes_recvd)) {
	  /* the timer popped */
	  timed_out = 1;
	  break;
	} else if (non_block && errno == EAGAIN) {
	    /* do recvmsg again */
	    continue;
	}
	netperf_response.content.serv_errno = errno;
	send_response();
	exit(1);
      }
    } while(!(msg_flags & MSG_EOR));

    recv_ring = recv_ring->next;

    if (timed_out) {
      /* we hit the end of the test based on time - lets */
      /* bail out of here now... */
      if (debug) {
	fprintf(where,"yo5\n");
	fflush(where);
      }
      break;
    }

    /* Now, send the response to the remote */
    while ((bytes_sent=sctp_sendmsg(s_rcv,
			      send_ring->buffer_ptr,
			      sctp_rr_request->response_size,
			      (struct sockaddr *)&peeraddr, addrlen,
			      0, 0, 0, 0, 0)) == SOCKET_ERROR) {
      if (SOCKET_EINTR(bytes_sent)) {
	/* the test timer has popped */
	timed_out = 1;
	break;
      } else if (non_block && errno == EAGAIN) {
	 continue;
      }

      netperf_response.content.serv_errno = 992;
      send_response();
      exit(1);
    }

    if (timed_out) {
      if (debug) {
	fprintf(where,"yo6\n");
	fflush(where);
      }
      /* we hit the end of the test based on time - lets */
      /* bail out of here now... */
      break;
    }

    send_ring = send_ring->next;

    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
  }


  /* The loop now exits due to timeout or transaction count being */
  /* reached */

  cpu_stop(sctp_rr_request->measure_cpu,&elapsed_time);

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
	    "recv_sctp_rr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }

  sctp_rr_results->bytes_received = (trans_received *
				    (sctp_rr_request->request_size +
				     sctp_rr_request->response_size));
  sctp_rr_results->trans_received = trans_received;
  sctp_rr_results->elapsed_time   = elapsed_time;
  sctp_rr_results->cpu_method     = cpu_method;
  sctp_rr_results->num_cpus       = lib_num_loc_cpus;
  if (sctp_rr_request->measure_cpu) {
    sctp_rr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }

  if (debug) {
    fprintf(where,
	    "recv_sctp_rr: test complete, sending results.\n");
    fflush(where);
  }

  /* we are now done with the sockets */
  close(s_rcv);

  send_response();

}


void
print_sctp_usage( void )
{

  printf("%s",sctp_usage);
  exit(1);

}

void
scan_sctp_args( int argc, char *argv[] )
{

#define SOCKETS_ARGS "BDhH:I:L:m:M:P:r:s:S:VN:T:46"

  extern char	*optarg;	  /* pointer to option string	*/

  int		c;

  char
    arg1[BUFSIZ],  /* argument holders		*/
    arg2[BUFSIZ];

  if (no_control) {
    fprintf(where,
	    "The SCTP tests do not know how to deal with no control tests\n");
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
      print_sctp_usage();
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
	  printf("Number of SCTP associations must be >= 1\n");
	  exit(1);
      }
      break;
    };
  }
}

#endif  /* WANT_SCTP */
