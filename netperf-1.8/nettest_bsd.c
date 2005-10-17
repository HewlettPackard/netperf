
char	nettest_id[]="@(#)nettest_bsd.c (c) Copyright 1993, \
Hewlett-Packard Company. Version 1.8alpha";
     
/****************************************************************/
/*								*/
/*	nettest_bsd.c						*/
/*								*/
/*      the BSD sockets parsing routine...                      */
/*                                                              */
/*      scan_sockets_args()                                     */
/*                                                              */
/*	the actual test routines...				*/
/*								*/
/*	send_tcp_stream()	perform a tcp stream test	*/
/*	recv_tcp_stream()					*/
/*	send_tcp_rr()		perform a tcp request/response	*/
/*	recv_tcp_rr()						*/
/*	send_udp_stream()	perform a udp stream test	*/
/*	recv_udp_stream()					*/
/*	send_udp_rr()		perform a udp request/response	*/
/*	recv_udp_rr()						*/
/*	loc_cpu_rate()		determine the local cpu maxrate */
/*	rem_cpu_rate()		find the remote cpu maxrate	*/
/*								*/
/****************************************************************/
     
#include <sys/types.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
     
#include "netlib.h"
#include "netsh.h"
#include "nettest_bsd.h"



 /* these variables are specific to the BSD sockets tests. declare */
 /* them static to make them global only to this file. */

static int	
  rss_size,		/* remote socket send buffer size	*/
  rsr_size,		/* remote socket recv buffer size	*/
  lss_size,		/* local  socket send buffer size 	*/
  lsr_size,		/* local  socket recv buffer size 	*/
  req_size = 1,		/* request size                   	*/
  rsp_size = 1,		/* response size			*/
  send_size,		/* how big are individual sends		*/
  recv_size;		/* how big are individual receives	*/

 /* different options for the sockets				*/

int
  loc_nodelay,		/* don't/do use NODELAY	locally		*/
  rem_nodelay,		/* don't/do use NODELAY remotely	*/
  loc_sndavoid,		/* avoid send copies locally		*/
  loc_rcvavoid,		/* avoid recv copies locally		*/
  rem_sndavoid,		/* avoid send copies remotely		*/
  rem_rcvavoid;		/* avoid recv_copies remotely		*/

int
  udp_cksum_off;

char sockets_usage[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
TCP/UDP BSD Sockets Test Options:\n\
    -D [L][,R]        Set TCP_NODELAY locally and/or remotely (TCP_*)\n\
    -h                Display this text\n\
    -m bytes          Set the send size (TCP_STREAM, UDP_STREAM)\n\
    -M bytes          Set the recv size (TCP_STREAM, UDP_STREAM)\n\
    -r bytes          Set request size (TCP_RR, UDP_RR)\n\
    -R bytes          Set response size (TCP_RR, UDP_RR)\n\
    -s send[,recv]    Set local socket send/recv buffer sizes\n\
    -S send[,recv]    Set remote socket send/recv buffer sizes\n\
\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n"; 
     

/* This routine implements the TCP unidirectional data transfer test */
/* (a.k.a. stream) for the sockets interface. It receives its */
/* parameters via global variables from the shell and writes its */
/* output to the standard output. */


void 
send_tcp_stream(remote_host)
char	remote_host[];
{
  
  char *tput_title = "\
Recv   Send    Send                          \n\
Socket Socket  Message  Elapsed              \n\
Size   Size    Size     Time     Throughput  \n\
bytes  bytes   bytes    secs.    %s/sec  \n\n";
  
  char *tput_fmt_0 =
    "%7.2f\n";
  
  char *tput_fmt_1 =
    "%5d  %5d  %6d    %-6.2f   %7.2f   \n";
  
  char *cpu_title = "\
Recv   Send    Send                          Utilization    Service Demand\n\
Socket Socket  Message  Elapsed              Send   Recv    Send    Recv\n\
Size   Size    Size     Time     Throughput  local  remote  local   remote\n\
bytes  bytes   bytes    secs.    %-8.8s/s  %%      %%       ms/KB   ms/KB\n\n";
  
  char *cpu_fmt_0 =
    "%6.3f\n";
  
  char *cpu_fmt_1 =
    "%5d  %5d  %6d    %-6.2f     %7.2f   %-6.2f %-6.2f  %-6.3f  %-6.3f\n";
  
  char *ksink_fmt = "\n\
Alignment      Offset         %-8.8s %-8.8s    Sends   %-8.8s Recvs\n\
Local  Remote  Local  Remote  Xfered   Per                 Per\n\
Send   Recv    Send   Recv             Send (avg)          Recv (avg)\n\
%5d   %5d  %5d   %5d %6.4g  %6.2f     %6d %6.2f   %6d\n";
  
  
  int 			one = 1;
  float			elapsed_time;
  
#ifdef INTERVALS
  int interval_count;
#endif
  
  /* what we want is to have a buffer space that is at least one */
  /* send-size greater than our send window. this will insure that we */
  /* are never trying to re-use a buffer that may still be in the hands */
  /* of the transport. This buffer will be malloc'd after we have found */
  /* the size of the local senc socket buffer. We will want to deal */
  /* with alignment and offset concerns as well. */
  
  char	*message_base;
  char	*message_ptr;
  int	*message_int_ptr;
  int	message_max_offset;
  int	message_offset;
  int	malloc_size;
  
  int	len;
  int	nummessages;
  int	send_socket;
  int	bytes_remaining;
  int	sock_opt_len = sizeof(int);
  /* with links like fddi, one can send > 32 bits worth of bytes */
  /* during a test... ;-) */
  double	bytes_sent;
  
#ifdef DIRTY
  int	i;
#endif /* DIRTY */
  
  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;
  double	thruput;
  
  struct	hostent	        *hp;
  struct	sockaddr_in	server;
  
  struct        sigaction       action;

  struct	tcp_stream_request_struct	*tcp_stream_request;
  struct	tcp_stream_response_struct	*tcp_stream_response;
  struct	tcp_stream_results_struct	*tcp_stream_result;
  
  tcp_stream_request  = 
    (struct tcp_stream_request_struct *)netperf_request->test_specific_data;
  tcp_stream_response =
    (struct tcp_stream_response_struct *)netperf_response->test_specific_data;
  tcp_stream_result   = 
    (struct tcp_stream_results_struct *)netperf_response->test_specific_data;
  
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */
  
  bzero((char *)&server,
	sizeof(server));
  
  if ((hp = gethostbyname(remote_host)) == NULL) {
    fprintf(where,
	    "send_tcp_stream: could not resolve the name%s\n",
	    remote_host);
    fflush(where);
  }
  
  bcopy(hp->h_addr,
	(char *)&server.sin_addr,
	hp->h_length);
  
  server.sin_family = hp->h_addrtype;
  
  
  if ( print_headers ) {
    fprintf(where,"TCP STREAM TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      fprintf(where,cpu_title,format_units());
    else
      fprintf(where,tput_title,format_units());
  }
  
  /* initialize a few counters */
  
  nummessages	=	0;
  bytes_sent	=	0.0;
  times_up 	= 	0;
  message_offset 	=	0;
  
  /*set up the data socket                        */
  send_socket = socket(AF_INET, 
		       SOCK_STREAM,
		       0);
  
  if (send_socket < 0){
    perror("netperf: send_tcp_stream: tcp stream data socket");
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"send_tcp_stream: send_socket obtained...\n");
  }
  
  /* Modify the local socket size. The reason we alter the send buffer */
  /* size here rather than when the connection is made is to take care */
  /* of decreases in buffer size. Decreasing the window size after */
  /* connection establishment is a TCP no-no. Also, by setting the */
  /* buffer (window) size before the connection is established, we can */
  /* control the TCP MSS (segment size). The MSS is never more that 1/2 */
  /* the minimum receive buffer size at each half of the connection. */
  /* This is why we are altering the receive buffer size on the sending */
  /* size of a unidirectional transfer. If the user has not requested */
  /* that the socket buffers be altered, we will try to find-out what */
  /* their values are. If we cannot touch the socket buffer in any way, */
  /* we will set the values to -1 to indicate that.  */
  
#ifdef SO_SNDBUF
  if (lss_size > 0) {
    if(setsockopt(send_socket, SOL_SOCKET, SO_SNDBUF,
		  (char *)&lss_size, sizeof(int)) < 0) {
      perror("netperf: send_tcp_stream: socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_stream: socket send size altered from system default...\n");
      fprintf(where,"                          send: %d\n",lss_size);
    }
  }
  if (lsr_size > 0) {
    if(setsockopt(send_socket, SOL_SOCKET, SO_RCVBUF,
		  (char *)&lsr_size, sizeof(int)) < 0) {
      perror("netperf: send_tcp_stream: receive socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_stream: socket recv size altered from system default...\n");
      fprintf(where,"                          recv: %d\n",lsr_size);
    }
  }
  
  
  /* Now, we will find-out what the size actually became, and report */
  /* that back to the user. If the call fails, we will just report a -1 */
  /* back to the initiator for the recv buffer size. */
  
  
  if (getsockopt(send_socket,
		 SOL_SOCKET,	
		 SO_SNDBUF,
		 (char *)&lss_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_tcp_stream: getsockopt");
    lss_size = -1;
  }
  if (getsockopt(send_socket,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&lsr_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_tcp_stream: getsockopt");
    lsr_size = -1;
  }
  
  if (debug) {
    fprintf(where,"netperf: send_tcp_stream: socket sizes determined...\n");
    fprintf(where,"         send: %d recv: %d\n",lss_size,lsr_size);
  }
  
#else SO_SNDBUF
  
  lss_size = -1;
  lsr_size = -1;
  
#endif /* SO_SNDBUF */
  
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
  
  /* now, we may wish to enable the copy avoidance features on the */
  /* local system. of course, this may not be possible... */
  
#ifdef SO_RCV_COPYAVOID
  if (loc_rcvavoid) {
    if (setsockopt(send_socket,
		   SOL_SOCKET,
		   SO_RCV_COPYAVOID,
		   &loc_rcvavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable receive copy avoidance");
      loc_rcvavoid = 0;
    }
  }
#endif
  
#ifdef SO_SND_COPYAVOID
  if (loc_sndavoid) {
    if (setsockopt(send_socket,
		   SOL_SOCKET,
		   SO_SND_COPYAVOID,
		   &loc_sndavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable send copy avoidance");
      loc_sndavoid = 0;
    }
  }
#endif
  
  
  /* set-up the data buffer with the requested alignment and offset. */
  /* After we have calculated the proper starting address, we want to */
  /* put that back into the message_base variable so we go back to the */
  /* proper place. note that this means that only the first send is */
  /* guaranteed to be at the alignment specified by the -a parameter. I */
  /* think that this is a little more "real-world" than what was found */
  /* in previous versions. note also that we have allocated a quantity */
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
  
  malloc_size = (send_width * send_size) + local_send_align + local_send_offset;
  message_base = (char *)malloc(malloc_size);
  message_ptr = (char *)(( (long)message_base + 
			  (long)local_send_align - 1) &	
			 ~((long)local_send_align - 1));
  message_ptr = message_ptr + local_send_offset;
  message_base = message_ptr;
  
  /* Now, we will see about setting the TCP_NO_DELAY flag on the local */
  /* socket. We will only do this for those systems that actually */
  /* support the option. If it fails, note the fact, but keep going. */
  
#ifdef TCP_NODELAY
  if (loc_nodelay) {
    if(setsockopt(send_socket,
		  getprotobyname("tcp")->p_proto,
		  TCP_NODELAY,
		  &one,
		  sizeof(one)) < 0) {
      perror("netperf: setsockopt: nodelay");
    }
    
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_stream: TCP_NODELAY has been requested...\n");
      fflush(where);
    }
  }
#else TCP_NODELAY
  
  loc_nodelay = 0;
  
#endif /* TCP_NODELAY */
  
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
  /* default to 1, which will be no alignment alterations. */
  
  netperf_request->request_type		=	DO_TCP_STREAM;
  tcp_stream_request->send_buf_size	=	rss_size;
  tcp_stream_request->recv_buf_size	=	rsr_size;
  tcp_stream_request->receive_size	=	recv_size;
  tcp_stream_request->no_delay		=	rem_nodelay;
  tcp_stream_request->recv_alignment	=	remote_recv_align;
  tcp_stream_request->recv_offset	=	remote_recv_offset;
  tcp_stream_request->measure_cpu	=	remote_cpu_usage;
  tcp_stream_request->cpu_rate		=	remote_cpu_rate;
  if (test_time) {
    tcp_stream_request->test_length	=	test_time;
  }
  else {
    tcp_stream_request->test_length	=	test_bytes;
  }
  tcp_stream_request->so_rcvavoid	=	rem_rcvavoid;
  tcp_stream_request->so_sndavoid	=	rem_sndavoid;
#ifdef DIRTY
  tcp_stream_request->dirty_count       =       rem_dirty_count;
  tcp_stream_request->clean_count       =       rem_clean_count;
#endif /* DIRTY */
  
  
  if (debug > 1) {
    fprintf(where,"netperf: send_tcp_stream: requesting TCP stream test\n");
  }
  
  send_request();
  
  /* The response from the remote will contain all of the relevant 	*/
  /* socket parameters for this test type. We will put them back into 	*/
  /* the variables here so they can be displayed if desired.  The	*/
  /* remote will have calibrated CPU if necessary, and will have done	*/
  /* all the needed set-up we will have calibrated the cpu locally	*/
  /* before sending the request, and will grab the counter value right	*/
  /* after the connect returns. The remote will grab the counter right	*/
  /* after the accept call. This saves the hassle of extra messages	*/
  /* being sent for the TCP tests.					*/
  
  recv_response();
  
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote listen done.\n");
    rsr_size	=	tcp_stream_response->recv_buf_size;
    rss_size	=	tcp_stream_response->send_buf_size;
    rem_nodelay	=	tcp_stream_response->no_delay;
    remote_cpu_usage=	tcp_stream_response->measure_cpu;
    remote_cpu_rate = 	tcp_stream_response->cpu_rate;
    /* we have to make sure that the server port number is in */
    /* network order */
    server.sin_port     =	tcp_stream_response->data_port_number;
    server.sin_port     = 	htons(server.sin_port); 
    rem_rcvavoid	=	tcp_stream_response->so_rcvavoid;
    rem_sndavoid	=	tcp_stream_response->so_sndavoid;
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /*Connect up to the remote port on the data socket  */
  if (connect(send_socket, 
	      (struct sockaddr *)&server,
	      sizeof(server)) <0){
    perror("netperf: data socket connect failed");
    printf(" port: %d\n",ntohs(server.sin_port));
    exit(1);
  }
  
  /* Data Socket set-up is finished. If there were problems, either the */
  /* connect would have failed, or the previous response would have */
  /* indicated a problem. I failed to see the value of the extra */
  /* message after the accept on the remote. If it failed, we'll see it */
  /* here. If it didn't, we might as well start pumping data. */
  
  /* Set-up the test end conditions. For a stream test, they can be */
  /* either time or byte-count based. */
  
  if (test_time) {
    /* The user wanted to end the test after a period of time. */
    times_up = 0;
    bytes_remaining = 0;
#ifdef SUNOS4
    /* on some systems (SunOS 4.blah), system calls are restarted. we do */
    /* not want that */
    action.sa_handler = catcher;
    action.sa_flags = SA_INTERRUPT;
    if (sigaction(SIGALRM, &action, NULL) < 0) {
      fprintf(where,"send_tcp_stream: error creating alarm signal.\n");
      fprintf(where,"errno %d\n",errno);
      fflush(where);
      exit(1);
    }
#else /* SUNOS4 */
    signal(SIGALRM,catcher);
#endif /* SUNOS4 */
    alarm(test_time);
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
  
  /* We use an "OR" to control test execution. When the test is */
  /* controlled by time, the byte count check will always return false. */
  /* When the test is controlled by byte count, the time test will */
  /* always return false. When the test is finished, the whole */
  /* expression will go false and we will stop sending data. */
  
#ifdef DIRTY
  /* initialize the random number generator for putting dirty stuff */
  /* into the send buffer. raj */
  srand((int) getpid());
#endif
  
  while ((!times_up) || (bytes_remaining > 0)) {
    
#ifdef DIRTY
    /* we want to dirty some number of consecutive integers in the buffer */
    /* we are about to send. we may also want to bring some number of */
    /* them cleanly into the cache. The clean ones will follow any dirty */
    /* ones into the cache. */
    message_int_ptr = (int *)message_ptr;
    for (i = 0; i < loc_dirty_count; i++) {
      *message_int_ptr = rand();
      message_int_ptr++;
    }
    for (i = 0; i < loc_clean_count; i++) {
      loc_dirty_count = *message_int_ptr;
      message_int_ptr++;
    }
#endif /* DIRTY */
    
    if((len=send(send_socket,
		 message_ptr,
		 send_size,
		 0)) != send_size) {
      if ((len >= 0) || (errno == EINTR))
	break;
      perror("netperf: data send error");
      
      exit(1);
    }
#ifdef INTERVALS
    for (interval_count = 0;
	 interval_count < interval_wate;
	 interval_count++);
#endif
    
    /* now we want to move our pointer to the next position in the */
    /* data buffer...we may also want to wrap back to the "beginning" */
    /* of the bufferspace, so we will mod the number of messages sent */
    /* by the send width, and use that to calculate the offset to add */
    /* to the base pointer. */
    nummessages++;          
    message_ptr = message_base + ((nummessages % send_width) * send_size);
    if (bytes_remaining) {
      bytes_remaining -= send_size;
    }
  }
  
  /* The test is over. Flush the buffers to the remote end. We do a */
  /* graceful release to insure that all data has been taken by the */
  /* remote. */ 
  
  if (shutdown(send_socket,1) == -1) {
    perror("netperf: cannot shutdown tcp stream socket");
    exit(1);
  }
  
  /* hang a recv() off the socket to block until the remote has */
  /* brought all the data up into the application. it will do a */
  /* shutdown to cause a FIN to be sent our way. We will assume that */
  /* any exit from the recv() call is good... raj 4/93 */
  
  recv(send_socket, message_ptr, send_size, 0);

  /* this call will always give us the elapsed time for the test, and */
  /* will also store-away the necessaries for cpu utilization */
  
  cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being measured? */
  /* how long did we really run? */
  
  /* Get the statistics from the remote end. The remote will have */
  /* calculated service demand and all those interesting things. If it */
  /* wasn't supposed to care, it will return obvious values. */
  
  recv_response();
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote results obtained\n");
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /* We now calculate what our thruput was for the test. In the future, */
  /* we may want to include a calculation of the thruput measured by */
  /* the remote, but it should be the case that for a TCP stream test, */
  /* that the two numbers should be *very* close... We calculate */
  /* bytes_sent regardless of the way the test length was controlled. */
  /* If it was time, we needed to, and if it was by bytes, the user may */
  /* have specified a number of bytes that wasn't a multiple of the */
  /* send_size, so we really didn't send what he asked for ;-) */
  
  bytes_sent	= ((double) send_size * (double) nummessages) + (double) len;
  thruput		= calc_thruput(bytes_sent);
  
  if (local_cpu_usage || remote_cpu_usage) {
    /* We must now do a little math for service demand and cpu */
    /* utilization for the system(s) */
    /* Of course, some of the information might be bogus because */
    /* there was no idle counter in the kernel(s). We need to make */
    /* a note of this for the user's benefit...*/
    if (local_cpu_usage) {
      if (local_cpu_rate == 0.0) {
	fprintf(where,"WARNING WARNING WARNING  WARNING WARNING WARNING  WARNING!\n");
	fprintf(where,"Local CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      local_cpu_utilization	= calc_cpu_util(0.0);
      local_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      0.0);
    }
    else {
      local_cpu_utilization	= -1.0;
      local_service_demand	= -1.0;
    }
    
    if (remote_cpu_usage) {
      if (remote_cpu_rate == 0.0) {
	fprintf(where,"DANGER   DANGER  DANGER   DANGER   DANGER  DANGER   DANGER!\n");
	fprintf(where,"Remote CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      remote_cpu_utilization	= tcp_stream_result->cpu_util;
      remote_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      remote_cpu_utilization);
    }
    else {
      remote_cpu_utilization = -1.0;
      remote_service_demand  = -1.0;
    }
    
    /* We are now ready to print all the information. If the user */
    /* has specified zero-level verbosity, we will just print the */
    /* local service demand, or the remote service demand. If the */
    /* user has requested verbosity level 1, he will get the basic */
    /* "streamperf" numbers. If the user has specified a verbosity */
    /* of greater than 1, we will display a veritable plethora of */
    /* background information from outside of this block as it it */
    /* not cpu_measurement specific...  */
    
    switch (verbosity) {
    case 0:
      if (local_cpu_usage) {
	fprintf(where,
		cpu_fmt_0,
		local_service_demand);
      }
      else {
	fprintf(where,
		cpu_fmt_0,
		remote_service_demand);
      }
      break;
    case 1:
    case 2:
      fprintf(where,
	      cpu_fmt_1,		/* the format string */
	      rsr_size,		/* remote recvbuf size */
	      lss_size,		/* local sendbuf size */
	      send_size,		/* how large were the sends */
	      elapsed_time,		/* how long was the test */
	      thruput, 		/* what was the xfer rate */
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
    /* TCP statistics, the alignments of the sends and receives */
    /* and all that sort of rot... */
    
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
	    bytes_sent / (double)tcp_stream_result->recv_calls,
	    tcp_stream_result->recv_calls);
  }
  
}


/* This is the server-side routine for the tcp stream test. It is */
/* implemented as one routine. I could break things-out somewhat, but */
/* didn't feel it was necessary. */

int 
recv_tcp_stream()
{
  
  struct sockaddr_in myaddr_in, peeraddr_in;
  int	s_listen,s_data;
  int 	addrlen;
  int	len;
  int	sock_opt_len = sizeof(int);
  int	measure_cpu;
  int	bytes_received;
  int	receive_calls;
  float	elapsed_time;
  
  char	*message_ptr;
  char	*message_base;
  int   *message_int_ptr;
  int	malloc_size;
  int	message_offset;
  int	message_max_offset;
  int   dirty_count;
  int   clean_count;
  int   i;
  
  struct	tcp_stream_request_struct	*tcp_stream_request;
  struct	tcp_stream_response_struct	*tcp_stream_response;
  struct	tcp_stream_results_struct	*tcp_stream_results;
  
  tcp_stream_request	= 
    (struct tcp_stream_request_struct *)netperf_request->test_specific_data;
  tcp_stream_response	= 
    (struct tcp_stream_response_struct *)netperf_response->test_specific_data;
  tcp_stream_results	= 
    (struct tcp_stream_results_struct *)netperf_response->test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_tcp_stream: entered...\n");
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
    fprintf(where,"recv_tcp_stream: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response->response_type = TCP_STREAM_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_tcp_stream: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */
  
  if (debug) {
    fprintf(where,"recv_tcp_stream: requested alignment of %d\n",
	    tcp_stream_request->recv_alignment);
    fflush(where);
  }
  
  /* Let's clear-out our sockaddr for the sake of cleanlines. Then we */
  /* can put in OUR values !-) At some point, we may want to nail this */
  /* socket to a particular network-level address, but for now, */
  /* INADDR_ANY should be just fine. */
  
  bzero((char *)&myaddr_in,
	sizeof(myaddr_in));
  myaddr_in.sin_family      = AF_INET;
  myaddr_in.sin_addr.s_addr = INADDR_ANY;
  myaddr_in.sin_port        = 0;
  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_tcp_stream: grabbing a socket...\n");
    fflush(where);
  }
  
  s_listen = socket(AF_INET,
		    SOCK_STREAM,
		    0);
  
  if (s_listen < 0) {
    netperf_response->serv_errno = errno;
    send_response();
    
    exit(1);
  }
  
  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. There may be a desire to */
  /* nail this socket to a specific IP address in a multi-homed, */
  /* multi-connection situation, but for now, we'll ignore the issue */
  /* and concentrate on single connection testing. */
  
  if (bind(s_listen,
	   &myaddr_in,
	   sizeof(myaddr_in)) == -1) {
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  /* The initiator may have wished-us to modify the socket buffer */
  /* sizes. We should give it a shot. If he didn't ask us to change the */
  /* sizes, we should let him know what sizes were in use at this end. */
  /* If none of this code is compiled-in, then we will tell the */
  /* initiator that we were unable to play with the socket buffer by */
  /* setting the size in the response to -1. */
  
#ifdef SO_RCVBUF
  
  if (tcp_stream_request->recv_buf_size) {
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_RCVBUF,
		  (char *)&(tcp_stream_request->recv_buf_size),
		  sizeof(int)) < 0) {
      /* Do we really care if this */
      /* call fails? Perhaps we */
      /* should keep going and just */
      /* say what the size was? */
      netperf_response->serv_errno = errno;
      send_response();
      
      exit(1);
    }
  }
  /* Now, we will find-out what the size actually became, and report */
  /* that back to the user. If the call fails, we will just report a -1 */
  /* back to the initiator for the recv buffer size. */
  
  if (getsockopt(s_listen,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&(tcp_stream_response->recv_buf_size),
		 &sock_opt_len) < 0) {
    tcp_stream_response->recv_buf_size = -1;
  }
  
#else /* the system won't let us play with the buffers */
  
  tcp_stream_response->recv_buf_size	= -1;
  
#endif /* SO_RCVBUF */
  
  /* what sort of sizes did we end-up with? */
  if (tcp_stream_request->receive_size == 0) {
    if (tcp_stream_response->recv_buf_size > 0) {
      recv_size = tcp_stream_response->recv_buf_size;
    }
    else {
      recv_size = 4096;
    }
  }
  else {
    recv_size = tcp_stream_request->receive_size;
  }
  /* tell the other fellow what our receive size became */
  tcp_stream_response->receive_size = recv_size;
  
  /* we may have been requested to enable the copy avoidance features. */
  /* can we actually do this? */
  if (tcp_stream_request->so_rcvavoid) {
#ifdef SO_RCV_COPYAVOID
    /* be optimistic */
    tcp_stream_response->so_rcvavoid = 1;
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_RCV_COPYAVOID,
		  &tcp_stream_request->so_rcvavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      tcp_stream_response->so_rcvavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    tcp_stream_response->so_rcvavoid = 0;
#endif
  }
  
  if (tcp_stream_request->so_sndavoid) {
#ifdef SO_SND_COPYAVOID
    /* be optimistic */
    tcp_stream_response->so_sndavoid = 1;
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_SND_COPYAVOID,
		  &tcp_stream_request->so_sndavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      tcp_stream_response->so_sndavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    tcp_stream_response->so_sndavoid = 0;
#endif
  }
  
  /* just a little prep work for when we may have to behave like the */
  /* sending side... */
  message_base = (char *)malloc(recv_size * 2);
  message_ptr = (char *)(((long)message_base + 
		(long)tcp_stream_request->recv_alignment -1) &
		 ~((long)tcp_stream_request->recv_alignment - 1));
  message_ptr = message_ptr + tcp_stream_request->recv_offset;
  
  if (debug) {
    fprintf(where,"recv_tcp_stream: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == -1) {
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen, &myaddr_in, &addrlen) == -1){
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */
  
  tcp_stream_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response->serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */
  
  tcp_stream_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (tcp_stream_request->measure_cpu) {
    tcp_stream_response->measure_cpu = 1;
    tcp_stream_response->cpu_rate = calibrate_local_cpu(tcp_stream_request->cpu_rate);
  }
  
  send_response();
  
  addrlen = sizeof(peeraddr_in);
  
  if ((s_data=accept(s_listen,&peeraddr_in,&addrlen)) ==
      -1) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    exit(1);
  }
  
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(tcp_stream_request->measure_cpu);
  
  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */
  
#ifdef DIRTY
    /* we want to dirty some number of consecutive integers in the buffer */
    /* we are about to recv. we may also want to bring some number of */
    /* them cleanly into the cache. The clean ones will follow any dirty */
    /* ones into the cache. */

  dirty_count = tcp_stream_request->dirty_count;
  clean_count = tcp_stream_request->clean_count;
  message_int_ptr = (int *)message_ptr;
  for (i = 0; i < dirty_count; i++) {
    *message_int_ptr = rand();
    message_int_ptr++;
  }
  for (i = 0; i < clean_count; i++) {
    dirty_count = *message_int_ptr;
    message_int_ptr++;
  }
#endif DIRTY

  while (len = recv(s_data, message_ptr, recv_size, 0)) {
    if (len == -1) {
      netperf_response->serv_errno = errno;
      send_response();
      exit(1);
    }
    bytes_received += len;
    receive_calls++;

#ifdef DIRTY
    message_int_ptr = (int *)message_ptr;
    for (i = 0; i < dirty_count; i++) {
      *message_int_ptr = rand();
      message_int_ptr++;
    }
    for (i = 0; i < clean_count; i++) {
      dirty_count = *message_int_ptr;
      message_int_ptr++;
    }
#endif DIRTY
  }
  
  /* The loop now exits due to zero bytes received. */
  /* perform a shutdown to signal the sender that */
  /* we have received all the data sent. raj 4/93 */

  if (shutdown(s_data,1) == -1) {
      netperf_response->serv_errno = errno;
      send_response();
      exit(1);
    }
  
  cpu_stop(tcp_stream_request->measure_cpu,&elapsed_time);
  
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_tcp_stream: got %d bytes\n",
	    bytes_received);
    fprintf(where,
	    "recv_tcp_stream: got %d recvs\n",
	    receive_calls);
    fflush(where);
  }
  
  tcp_stream_results->bytes_received	= bytes_received;
  tcp_stream_results->elapsed_time	= elapsed_time;
  tcp_stream_results->recv_calls		= receive_calls;
  
  if (tcp_stream_request->measure_cpu) {
    tcp_stream_results->cpu_util	= calc_cpu_util(0.0);
  };
  
  if (debug > 1) {
    fprintf(where,
	    "recv_tcp_stream: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
}

/*********************************/

int 
send_tcp_rr(remote_host)
     char	remote_host[];
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
bytes  bytes  bytes   bytes  secs.   per sec  %%      %%      ms/Tr   ms/Tr\n\n";
  
  char *cpu_fmt_0 =
    "%6.3f\n";
  
  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f   %-6.2f %-6.2f %-6.3f  %-6.3f\n";
  
  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";
  
  char *ksink_fmt = "\
Alignment      Offset\n\
Local  Remote  Local  Remote\n\
Send   Recv    Send   Recv\n\
%5d  %5d   %5d  %5d";
  
  
  int 			one = 1;
  int			timed_out = 0;
  float			elapsed_time;
  
  int	len;
  char	*send_message_ptr;
  char	*recv_message_ptr;
  char	*temp_message_ptr;
  int	nummessages;
  int	send_socket;
  int	trans_remaining;
  double	bytes_xferd;
  int	sock_opt_len = sizeof(int);
  
  int	rsp_bytes_left;
  int	rsp_bytes_recvd;
  
  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;
  double	thruput;
  
  struct	hostent	        *hp;
  struct	sockaddr_in	server;
  
  struct        sigaction       action;

  struct	tcp_rr_request_struct	*tcp_rr_request;
  struct	tcp_rr_response_struct	*tcp_rr_response;
  struct	tcp_rr_results_struct	*tcp_rr_result;
  
  tcp_rr_request	= (struct tcp_rr_request_struct *)netperf_request->test_specific_data;
  tcp_rr_response	= (struct tcp_rr_response_struct *)netperf_response->test_specific_data;
  tcp_rr_result	= (struct tcp_rr_results_struct *)netperf_response->test_specific_data;
  
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */
  
  bzero((char *)&server,
	sizeof(server));
  
  if ((hp = gethostbyname(remote_host)) == NULL) {
    fprintf(where,
	    "send_tcp_rr: could not resolve the name%s\n",
	    remote_host);
    fflush(where);
  }
  
  bcopy(hp->h_addr,
	(char *)&server.sin_addr,
	hp->h_length);
  
  server.sin_family = hp->h_addrtype;
  
  
  if ( print_headers ) {
    fprintf(where,"TCP REQUEST/RESPONSE TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      fprintf(where,cpu_title,format_units());
    else
      fprintf(where,tput_title,format_units());
  }
  
  /* initialize a few counters */
  
  nummessages	=	0;
  bytes_xferd	=	0.0;
  times_up 	= 	0;
  
  /* set-up the data buffers with the requested alignment and offset */
  temp_message_ptr = (char *)malloc(MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET);
  send_message_ptr = (char *)(( (long) temp_message_ptr + 
			(long) local_send_align - 1) &	
			~((long) local_send_align - 1));
  send_message_ptr = send_message_ptr + local_send_offset;
  temp_message_ptr = (char *)malloc(MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET);
  recv_message_ptr = (char *)(( (long) temp_message_ptr + 
			(long) local_recv_align - 1) &	
			~((long) local_recv_align - 1));
  recv_message_ptr = recv_message_ptr + local_recv_offset;
  
  /*set up the data socket                        */
  send_socket = socket(AF_INET, 
		       SOCK_STREAM,
		       0);
  
  if (send_socket < 0){
    perror("netperf: send_tcp_rr: tcp stream data socket");
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"send_tcp_rr: send_socket obtained...\n");
  }
  
  /* Modify the local socket size. The reason we alter the send buffer */
  /* size here rather than when the connection is made is to take care */
  /* of decreases in buffer size. Decreasing the window size after */
  /* connection establishment is a TCP no-no. Also, by setting the */
  /* buffer (window) size before the connection is established, we can */
  /* control the TCP MSS (segment size). The MSS is never more that 1/2 */
  /* the minimum receive buffer size at each half of the connection. */
  /* This is why we are altering the receive buffer size on the sending */
  /* size of a unidirectional transfer. If the user has not requested */
  /* that the socket buffers be altered, we will try to find-out what */
  /* their values are. If we cannot touch the socket buffer in any way, */
  /* we will set the values to -1 to indicate that.  */
  
#ifdef SO_SNDBUF
  if (lss_size > 0) {
    if(setsockopt(send_socket, SOL_SOCKET, SO_SNDBUF,
		  (char *)&lss_size, sizeof(int)) < 0) {
      perror("netperf: send_tcp_rr: socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_rr: socket send size altered from system default...\n");
      fprintf(where,"                          send: %d\n",lss_size);
    }
  }
  if (lsr_size > 0) {
    if(setsockopt(send_socket, SOL_SOCKET, SO_RCVBUF,
		  (char *)&lsr_size, sizeof(int)) < 0) {
      perror("netperf: send_tcp_rr: receive socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_rr: socket recv size altered from system default...\n");
      fprintf(where,"                          recv: %d\n",lsr_size);
    }
  }
  
  
  /* Now, we will find-out what the size actually became, and report */
  /* that back to the user. If the call fails, we will just report a -1 */
  /* back to the initiator for the recv buffer size. */
  
  
  if (getsockopt(send_socket,
		 SOL_SOCKET,	
		 SO_SNDBUF,
		 (char *)&lss_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_tcp_rr: getsockopt");
    lss_size = -1;
  }
  if (getsockopt(send_socket,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&lsr_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_tcp_rr: getsockopt");
    lsr_size = -1;
  }
  
  if (debug) {
    fprintf(where,"netperf: send_tcp_rr: socket sizes determined...\n");
    fprintf(where,"         send: %d recv: %d\n",lss_size,lsr_size);
  }
  
#else SO_SNDBUF
  
  lss_size = -1;
  lsr_size = -1;
  
#endif SO_SNDBUF
  
  /* Now, we will see about setting the TCP_NO_DELAY flag on the local */
  /* socket. We will only do this for those systems that actually */
  /* support the option. If it fails, note the fact, but keep going. */
  
#ifdef TCP_NODELAY
  if (loc_nodelay) {
    if(setsockopt(send_socket,
		  getprotobyname("tcp")->p_proto,
		  TCP_NODELAY,
		  &one,
		  sizeof(one)) < 0) {
      perror("netperf: setsockopt: nodelay");
    }
    
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_rr: TCP_NODELAY has been requested...\n");
      fflush(where);
    }
  }
#else TCP_NODELAY
  
  loc_nodelay = 0;
  
#endif TCP_NODELAY
  
  /* we may wish to enable the copy avoidance features at this point. */
  /* It will either be the case that the option was not present at */
  /* compile time, or we may fail the socket call. */
  
#ifdef SO_RCV_COPYAVOID
  if (loc_rcvavoid) {
    if (setsockopt(send_socket,
		   SOL_SOCKET,
		   SO_RCV_COPYAVOID,
		   &loc_rcvavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable receive copy avoidance");
      loc_rcvavoid = 0;
    }
  }
#endif
  
#ifdef SO_SND_COPYAVOID
  if (loc_sndavoid) {
    if (setsockopt(send_socket,
		   SOL_SOCKET,
		   SO_SND_COPYAVOID,
		   &loc_sndavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable send copy avoidance");
      loc_sndavoid = 0;
    }
  }
#endif
  
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
  
  netperf_request->request_type	=	DO_TCP_RR;
  tcp_rr_request->recv_buf_size	=	rsr_size;
  tcp_rr_request->send_buf_size	=	rss_size;
  tcp_rr_request->recv_alignment	=	remote_recv_align;
  tcp_rr_request->recv_offset	=	remote_recv_offset;
  tcp_rr_request->send_alignment	=	remote_send_align;
  tcp_rr_request->send_offset	=	remote_send_offset;
  tcp_rr_request->request_size	=	req_size;
  tcp_rr_request->response_size	=	rsp_size;
  tcp_rr_request->no_delay	=	rem_nodelay;
  tcp_rr_request->measure_cpu	=	remote_cpu_usage;
  tcp_rr_request->cpu_rate	=	remote_cpu_rate;
  tcp_rr_request->so_rcvavoid	=	rem_rcvavoid;
  tcp_rr_request->so_sndavoid	=	rem_sndavoid;
  if (test_time) {
    tcp_rr_request->test_length	=	test_time;
  }
  else {
    tcp_rr_request->test_length	=	test_trans * -1;
  }
  
  if (debug > 1) {
    fprintf(where,"netperf: send_tcp_rr: requesting TCP rr test\n");
  }
  
  send_request();
  
  /* The response from the remote will contain all of the relevant 	*/
  /* socket parameters for this test type. We will put them back into 	*/
  /* the variables here so they can be displayed if desired.  The	*/
  /* remote will have calibrated CPU if necessary, and will have done	*/
  /* all the needed set-up we will have calibrated the cpu locally	*/
  /* before sending the request, and will grab the counter value right	*/
  /* after the connect returns. The remote will grab the counter right	*/
  /* after the accept call. This saves the hassle of extra messages	*/
  /* being sent for the TCP tests.					*/
  
  recv_response();
  
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote listen done.\n");
    rsr_size	=	tcp_rr_response->recv_buf_size;
    rss_size	=	tcp_rr_response->send_buf_size;
    rem_nodelay	=	tcp_rr_response->no_delay;
    remote_cpu_usage=	tcp_rr_response->measure_cpu;
    remote_cpu_rate = 	tcp_rr_response->cpu_rate;
    /* make sure that port numbers are in network order */
    server.sin_port	=	tcp_rr_response->data_port_number;
    server.sin_port =	htons(server.sin_port);
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /*Connect up to the remote port on the data socket  */
  if (connect(send_socket, 
	      (struct sockaddr *)&server,
	      sizeof(server)) <0){
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
#ifdef SUNOS4
    /* on some systems (SunOS 4.blah), system calls are restarted. we do */
    /* not want that for a request/response test */
    action.sa_handler = catcher;
    action.sa_flags = SA_INTERRUPT;
    if (sigaction(SIGALRM, &action, NULL) < 0) {
      fprintf(where,"send_tcp_rr: error creating alarm signal.\n");
      fflush(where);
      exit(1);
    }
#else /* SUNOS4 */
    signal(SIGALRM,catcher);
#endif /* SUNOS4 */
    alarm(test_time);
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
  
  /* We use an "OR" to control test execution. When the test is */
  /* controlled by time, the byte count check will always return false. */
  /* When the test is controlled by byte count, the time test will */
  /* always return false. When the test is finished, the whole */
  /* expression will go false and we will stop sending data. I think I */
  /* just arbitrarily decrement trans_remaining for the timed test, but */
  /* will not do that just yet... One other question is whether or not */
  /* the send buffer and the receive buffer should be the same buffer. */
  
  while ((!times_up) || (trans_remaining > 0)) {
    /* send the request */
    if((len=send(send_socket,
		 send_message_ptr,
		 req_size,
		 0)) != req_size) {
      if (errno == EINTR) {
	/* we hit the end of a */
	/* timed test. */
	timed_out = 1;
	break;
      }
      perror("send_tcp_rr: data send error");
      exit(1);
    }
    
    /* receive the response */
    rsp_bytes_left = rsp_size;
    temp_message_ptr  = recv_message_ptr;
    while(rsp_bytes_left > 0) {
      if((rsp_bytes_recvd=recv(send_socket,
			       temp_message_ptr,
			       rsp_bytes_left,
			       0)) < 0) {
	if (errno == EINTR) {
	  /* We hit the end of a timed test. */
	  timed_out = 1;
	  break;
	}
	perror("send_tcp_rr: data recv error");
	exit(1);
      }
      rsp_bytes_left -= rsp_bytes_recvd;
      temp_message_ptr  += rsp_bytes_recvd;
    }	
    
    if (timed_out) {
      /* we may have been in a nested while loop - we need */
      /* another call to break. */
      break;
    }
    
    nummessages++;          
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug > 3) {
      fprintf(where,
	      "Transaction %d completed\n",
	      nummessages);
      fflush(where);
    }
  }
  
  /* At this point we used to call shutdown onthe data socket to be */
  /* sure all the data was delivered, but this was not germane in a */
  /* request/response test, and it was causing the tests to "hang" when */
  /* they were being controlled by time. So, I have replaced this */
  /* shutdown call with a call to close that can be found later in the */
  /* procedure. */
  
  /* this call will always give us the elapsed time for the test, and */
  /* will also store-away the necessaries for cpu utilization */
  
  cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being measured? */
  /* how long did we really run? */
  
  /* Get the statistics from the remote end. The remote will have */
  /* calculated service demand and all those interesting things. If it */
  /* wasn't supposed to care, it will return obvious values. */
  
  recv_response();
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote results obtained\n");
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /* We now calculate what our thruput was for the test. In the future, */
  /* we may want to include a calculation of the thruput measured by */
  /* the remote, but it should be the case that for a TCP stream test, */
  /* that the two numbers should be *very* close... We calculate */
  /* bytes_sent regardless of the way the test length was controlled. */
  /* If it was time, we needed to, and if it was by bytes, the user may */
  /* have specified a number of bytes that wasn't a multiple of the */
  /* send_size, so we really didn't send what he asked for ;-) We use */
  /* Kbytes/s as the units of thruput for a TCP stream test, where K = */
  /* 1024. A future enhancement *might* be to choose from a couple of */
  /* unit selections. */ 
  
  bytes_xferd	= (req_size * nummessages) + (rsp_size * nummessages);
  thruput		= calc_thruput(bytes_xferd);
  
  if (local_cpu_usage || remote_cpu_usage) {
    /* We must now do a little math for service demand and cpu */
    /* utilization for the system(s) */
    /* Of course, some of the information might be bogus because */
    /* there was no idle counter in the kernel(s). We need to make */
    /* a note of this for the user's benefit...*/
    if (local_cpu_usage) {
      if (local_cpu_rate == 0.0) {
	fprintf(where,"WARNING WARNING WARNING  WARNING WARNING WARNING  WARNING!\n");
	fprintf(where,"Local CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      local_cpu_utilization = calc_cpu_util(0.0);
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      local_service_demand  = calc_service_demand((double) nummessages*1024,
						      0.0,
						      0.0);
    }
    else {
      local_cpu_utilization	= -1.0;
      local_service_demand	= -1.0;
    }
    
    if (remote_cpu_usage) {
      if (remote_cpu_rate == 0.0) {
	fprintf(where,"DANGER  DANGER  DANGER    DANGER  DANGER  DANGER    DANGER!\n");
	fprintf(where,"Remote CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      remote_cpu_utilization = tcp_rr_result->cpu_util;
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      remote_service_demand = calc_service_demand((double) nummessages*1024,
						  0.0,
						  remote_cpu_utilization);
    }
    else {
      remote_cpu_utilization = -1.0;
      remote_service_demand  = -1.0;
    }
    
    /* We are now ready to print all the information. If the user */
    /* has specified zero-level verbosity, we will just print the */
    /* local service demand, or the remote service demand. If the */
    /* user has requested verbosity level 1, he will get the basic */
    /* "streamperf" numbers. If the user has specified a verbosity */
    /* of greater than 1, we will display a veritable plethora of */
    /* background information from outside of this block as it it */
    /* not cpu_measurement specific...  */
    
    switch (verbosity) {
    case 0:
      if (local_cpu_usage) {
	fprintf(where,
		cpu_fmt_0,
		local_service_demand);
      }
      else {
	fprintf(where,
		cpu_fmt_0,
		remote_service_demand);
      }
      break;
    case 1:
      fprintf(where,
	      cpu_fmt_1_line_1,		/* the format string */
	      lss_size,		/* local sendbuf size */
	      lsr_size,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* guess */
	      elapsed_time,		/* how long was the test */
	      nummessages/elapsed_time,
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
	      nummessages/elapsed_time);
      break;
    case 1:
      fprintf(where,
	      tput_fmt_1_line_1,	/* the format string */
	      lss_size,
	      lsr_size,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* how large were the responses */
	      elapsed_time, 		/* how long did it take */
	      nummessages/elapsed_time);
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
  
  if (verbosity > 1) {
    /* The user wanted to know it all, so we will give it to him. */
    /* This information will include as much as we can find about */
    /* TCP statistics, the alignments of the sends and receives */
    /* and all that sort of rot... */
    
    fprintf(where,
	    ksink_fmt);
  }
  /* The test is over. Kill the data socket */
  
  if (close(send_socket) == -1) {
    perror("send_tcp_rr: cannot shutdown tcp stream socket");
  }
  
}

void
  send_udp_stream(remote_host)
char	remote_host[];
{
  /************************************************************************/
  /*									*/
  /*               	UDP Unidirectional Send Test                    */
  /*									*/
  /************************************************************************/
  char *tput_title =
    "Socket  Message  Elapsed      Messages                \n\
Size    Size     Time         Okay Errors   Throughput\n\
bytes   bytes    secs            #      #   %s/sec\n\n";
  
  char *tput_fmt_0 =
    "%7.2f\n";
  
  char *tput_fmt_1 =
    "%5d   %5d    %-7.2f   %7d %6d    %7.2f\n\
%5d            %-7.2f   %7d           %7.2f\n\n";
  
  
  char *cpu_title =
    "Socket  Message  Elapsed      Messages                   CPU     Service\n\
Size    Size     Time         Okay Errors   Throughput   Util    Demand\n\
bytes   bytes    secs            #      #   %s/sec   %%       ms/KB\n\n";
  
  char *cpu_fmt_0 =
    "%6.2f\n";
  
  char *cpu_fmt_1 =
    "%5d   %5d    %-7.2f   %7d %6d    %7.1f      %-6.2f  %-6.3f\n\
%5d            %-7.2f   %7d           %7.1f      %-6.2f  %-6.3f\n\n";
  
  int	messages_recvd;
  float	elapsed_time, 
  recv_elapsed, 
  local_cpu_utilization, 
  remote_cpu_utilization;
  
  float	local_service_demand, remote_service_demand;
  double	local_thruput, remote_thruput;
  double	bytes_sent;
  double	bytes_recvd;
  
  
  int	len;
  int	*message_int_ptr;
  char	*message_ptr;
  char	*message_base;
  int	message_offset;
  int	message_max_offset;
  int	failed_sends;
  int	failed_cows;
  int 	messages_sent;
  int 	data_socket;
  
  int	sock_opt_len=sizeof(int);
  
#ifdef INTERVALS
  int	interval_count;
#endif INTERVALS
#ifdef DIRTY
  int	i;
#endif DIRTY
  
  struct	hostent	        *hp;
  struct	sockaddr_in	server;
  
  struct        sigaction       action;

  struct	udp_stream_request_struct	*udp_stream_request;
  struct	udp_stream_response_struct	*udp_stream_response;
  struct	udp_stream_results_struct	*udp_stream_results;
  
  udp_stream_request	= (struct udp_stream_request_struct *)netperf_request->test_specific_data;
  udp_stream_response	= (struct udp_stream_response_struct *)netperf_response->test_specific_data;
  udp_stream_results	= (struct udp_stream_results_struct *)netperf_response->test_specific_data;
  
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */
  
  bzero((char *)&server,
	sizeof(server));
  
  if ((hp = gethostbyname(remote_host)) == NULL) {
    fprintf(where,
	    "send_udp_stream: could not resolve the name%s\n",
	    remote_host);
    fflush(where);
  }
  
  bcopy(hp->h_addr,
	(char *)&server.sin_addr,
	hp->h_length);
  
  server.sin_family = hp->h_addrtype;
  
  if ( print_headers ) {
    if (udp_cksum_off)
      printf("UDP UNIDIRECTIONAL SEND TEST - Checksumming Disabled\n");
    else
      printf("UDP UNIDIRECTIONAL SEND TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      printf(cpu_title,format_units());
    else
      printf(tput_title,format_units());
  }	
  
  failed_sends	= 0;
  failed_cows	= 0;
  messages_sent	= 0;
  times_up	= 0;
  
  /*set up the data socket			*/
  data_socket = socket(AF_INET,
		       SOCK_DGRAM,
		       0);
  
  if (data_socket < 0){
    perror("udp_send: data socket");
    exit(1);
  }
  
  /* Modify the local socket size (SNDBUF size)    */
  
#ifdef SO_SNDBUF
  if (lss_size > 0) {
    if(setsockopt(data_socket, SOL_SOCKET, SO_SNDBUF,
		  (char *)&lss_size, sizeof(int)) < 0) {
      perror("netperf: send_udp_stream: socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_udp_stream: socket send size altered from system default...\n");
      fprintf(where,"                          send: %d\n",lss_size);
    }
  }
  if (lsr_size > 0) {
    if(setsockopt(data_socket, SOL_SOCKET, SO_RCVBUF,
		  (char *)&lsr_size, sizeof(int)) < 0) {
      perror("netperf: send_udp_stream: receive socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_udp_stream: socket recv size altered from system default...\n");
      fprintf(where,"                          recv: %d\n",lsr_size);
    }
  }
  
  
  /* Now, we will find-out what the size actually became, and report */
  /* that back to the user. If the call fails, we will just report a -1 */
  /* back to the initiator for the recv buffer size. */
  
  
  if (getsockopt(data_socket,
		 SOL_SOCKET,	
		 SO_SNDBUF,
		 (char *)&lss_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_udp_stream: getsockopt");
    lss_size = -1;
  }
  if (getsockopt(data_socket,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&lsr_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_udp_stream: getsockopt");
    lsr_size = -1;
  }
  
  if (debug) {
    fprintf(where,"netperf: send_udp_stream: socket sizes determined...\n");
    fprintf(where,"         send: %d recv: %d\n",lss_size,lsr_size);
  }
  
#else SO_SNDBUF
  
  lss_size = -1;
  lsr_size = -1;
  
#endif SO_SNDBUF
  
#ifdef SO_RCV_COPYAVOID
  if (loc_rcvavoid) {
    if (setsockopt(data_socket,
		   SOL_SOCKET,
		   SO_RCV_COPYAVOID,
		   &loc_rcvavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable receive copy avoidance");
      loc_rcvavoid = 0;
    }
  }
#endif
  
#ifdef SO_SND_COPYAVOID
  if (loc_sndavoid) {
    if (setsockopt(data_socket,
		   SOL_SOCKET,
		   SO_SND_COPYAVOID,
		   &loc_sndavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable send copy avoidance");
      loc_sndavoid = 0;
    }
  }
#endif
  
  /* now, we want to see if we need to set the send_size */
  if (send_size == 0) {
    if (lss_size > 0) {
      send_size = lss_size;
    }
    else {
      send_size = 4096;
    }
  }
  
  
  /* set-up the data buffer with the requested alignment and offset, */
  /* most of the numbers here are just a hack to pick something nice */
  /* and big in an attempt to never try to send a buffer a second time */
  /* before it leaves the node...unless the user set the width */
  /* explicitly. */
  if (send_width == 0) send_width = 32;
  message_max_offset = send_size * send_width;
  message_offset = 0;
  message_base = (char *)malloc(send_size * (send_width + 1) + local_send_align + local_send_offset);
  message_ptr = (char *)(( (long) message_base + 
			(long) local_send_align - 1) &	
			~((long) local_send_align - 1));
  message_ptr = message_ptr + local_send_offset;
  message_base = message_ptr;
  
  /* At this point, we want to do things like disable UDP checksumming */
  /* and measure the cpu rate and all that so we are ready to go */
  /* immediately after the test response message is delivered. */
  
  /* if the user supplied a cpu rate, this call will complete rather */
  /* quickly, otherwise, the cpu rate will be retured to us for */
  /* possible display. The Library will keep it's own copy of this data */
  /* for use elsewhere. We will only display it. (Does that make it */
  /* "opaque" to us?) */
  
  if (local_cpu_usage)
    local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
  
  if (udp_cksum_off) {
    fprintf(where,"send_udp_stream: UDP checksum disable not implemented\n");
  }
  
  /* Tell the remote end to set up the data connection. The server */
  /* sends back the port number and alters the socket parameters there. */
  /* Of course this is a datagram service so no connection is actually */
  /* set up, the server just sets up the socket and binds it. */
  
  netperf_request->request_type = DO_UDP_STREAM;
  udp_stream_request->recv_buf_size	= rsr_size;
  udp_stream_request->message_size	= send_size;
  udp_stream_request->recv_alignment	= remote_recv_align;
  udp_stream_request->recv_offset		= remote_recv_offset;
  udp_stream_request->measure_cpu		= remote_cpu_usage;
  udp_stream_request->cpu_rate		= remote_cpu_rate;
  udp_stream_request->checksum_off	= udp_cksum_off;
  udp_stream_request->test_length		= test_time;
  udp_stream_request->so_rcvavoid		= rem_rcvavoid;
  udp_stream_request->so_sndavoid		= rem_sndavoid;
  
  send_request();
  
  recv_response();
  
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"send_udp_stream: remote data connection done.\n");
  }
  else {
    errno = netperf_response->serv_errno;
    perror("send_udp_stream: error on remote");
    exit(1);
  }
  
  /* Place the port number returned by the remote into the sockaddr */
  /* structure so our sends can be sent to the correct place. Also get */
  /* some of the returned socket buffer information for user display. */
  
  /* make sure that port numbers are in the proper order */
  server.sin_port = udp_stream_response->data_port_number;
  server.sin_port = htons(server.sin_port);
  rsr_size	= udp_stream_response->recv_buf_size;
  rss_size	= udp_stream_response->send_buf_size;
  remote_cpu_rate	= udp_stream_response->cpu_rate;
  
  /* We "connect" up to the remote post to allow is to use the send */
  /* call instead of the sendto call. Presumeably, this is a little */
  /* simpler, and a little more efficient. I think that it also means */
  /* that we can be informed of certain things, but am not sure yet... */
  
  if (connect(data_socket,
	      (struct sockaddr *)&server,
	      sizeof(server)) <0){
    perror("send_udp_stream: data socket connect failed");
    exit(1);
  }
  
  /* set up the timer to call us after test_time	*/
#ifdef SUNOS4
  /* on some systems (SunOS 4.blah), system calls are restarted. we do */
  /* not want that for a request/response test */
  action.sa_handler = catcher;
  action.sa_flags = SA_INTERRUPT;
  if (sigaction(SIGALRM, &action, NULL) < 0) {
    fprintf(where,"send_udp_stream: error creating alarm signal.\n");
    fflush(where);
    exit(1);
  }
#else /* SUNOS4 */
  signal(SIGALRM, catcher);
#endif /* SUNOS4 */
  alarm(test_time);
  
  /* Get the start count for the idle counter and the start time */
  
  cpu_start(local_cpu_usage);
  
#ifdef INTERVALS
  interval_count = interval_burst;
#endif
  
  /* Send datagrams like there was no tomorrow */
  while (!times_up) {
#ifdef DIRTY
    /* we want to dirty some number of consecutive integers in the buffer */
    /* we are about to send. we may also want to bring some number of */
    /* them cleanly into the cache. The clean ones will follow any dirty */
    /* ones into the cache. */
    message_int_ptr = (int *)message_ptr;
    for (i = 0; i < loc_dirty_count; i++) {
      *message_int_ptr = 4;
      message_int_ptr++;
    }
    for (i = 0; i < loc_clean_count; i++) {
      loc_dirty_count = *message_int_ptr;
      message_int_ptr++;
    }
#endif DIRTY
    if ((len=send(data_socket,
		  message_ptr,
		  send_size,
		  0))  != send_size) {
      if ((len >= 0) || (errno == EINTR))
	break;
      if (errno == ENOBUFS) {
	failed_sends++;
	continue;
      }
      if (errno == EAGAIN) {
	failed_cows++;
	continue;
      }
      perror("udp_send: data send error");
      exit(1);
    }
    messages_sent++;          
    
    /* now we want to move our pointer to the next position in the */
    /* data buffer...since there was a successful send */
    message_offset += send_size;
    if (message_offset > message_max_offset) {
      message_offset = 0;
    }
    message_ptr = message_base + message_offset;
    
    
#ifdef INTERVALS
    /* in this case, the interval count is the count-down couter */
    /* to decide to sleep for a little bit */
    if ((interval_burst) && (--interval_count == 0)) {
      /* call the sleep routine for some milliseconds, if our */
      /* timer popped while we were in there, we want to */
      /* break out of the loop. */
      if (msec_sleep(interval_wate)) {
	break;
      }
      interval_count = interval_burst;
    }
    
#endif
    
  }
  
  /* This is a timed test, so the remote will be returning to us after */
  /* a time. We should not need to send any "strange" messages to tell */
  /* the remote that the test is completed, unless we decide to add a */
  /* number of messages to the test. */
  
  /* the test is over, so get stats and stuff */
  cpu_stop(local_cpu_usage,	
	   &elapsed_time);
  
  if (udp_cksum_off) {
    /* We must turn checksumming back on. We are assuming that it */
    /* was on when we started. */
  }
  
  /* Get the statistics from the remote end	*/
  recv_response();
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"send_udp_stream: remote results obtained\n");
  }
  else {
    errno = netperf_response->serv_errno;
    perror("send_udp_stream: error on remote");
    exit(1);
  }
  
  bytes_sent	= send_size * messages_sent;
  local_thruput	= calc_thruput(bytes_sent);
  
  messages_recvd	= udp_stream_results->messages_recvd;
  bytes_recvd	= send_size * messages_recvd;
  
  /* we asume that the remote ran for as long as we did */
  
  remote_thruput	= calc_thruput(bytes_recvd);
  
  /* print the results for this socket and message size */
  
  if (local_cpu_usage || remote_cpu_usage) {
    /* We must now do a little math for service demand and cpu */
    /* utilization for the system(s) We pass zeros for the local */
    /* cpu utilization and elapsed time to tell the routine to use */
    /* the libraries own values for those. */
    if (local_cpu_usage) {
      if (local_cpu_rate == 0.0) {
	fprintf(where,"WARNING WARNING WARNING  WARNING WARNING WARNING  WARNING!\n");
	fprintf(where,"Local CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      
      local_cpu_utilization	= calc_cpu_util(0.0);
      local_service_demand	= calc_service_demand(bytes_sent,
						      0.0,
						      0.0);
    }
    else {
      local_cpu_utilization	= -1.0;
      local_service_demand	= -1.0;
    }
    
    /* The local calculations could use variables being kept by */
    /* the local netlib routines. The remote calcuations need to */
    /* have a few things passed to them. */
    if (remote_cpu_usage) {
      if (remote_cpu_rate == 0.0) {
	fprintf(where,"DANGER   DANGER  DANGER   DANGER  DANGER   DANGER   DANGER!\n");
	fprintf(where,"REMOTE CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      
      remote_cpu_utilization	= udp_stream_results->cpu_util;
      remote_service_demand	= calc_service_demand(bytes_recvd,
						      0.0,
						      remote_cpu_utilization);
    }
    else {
      remote_cpu_utilization	= -1.0;
      remote_service_demand	= -1.0;
    }
    
    /* We are now ready to print all the information. If the user */
    /* has specified zero-level verbosity, we will just print the */
    /* local service demand, or the remote service demand. If the */
    /* user has requested verbosity level 1, he will get the basic */
    /* "streamperf" numbers. If the user has specified a verbosity */
    /* of greater than 1, we will display a veritable plethora of */
    /* background information from outside of this block as it it */
    /* not cpu_measurement specific...  */
    
    switch (verbosity) {
    case 0:
      if (local_cpu_usage) {
	fprintf(where,
		cpu_fmt_0,
		local_service_demand);
      }
      else {
	fprintf(where,
		cpu_fmt_0,
		remote_service_demand);
      }
      break;
    case 1:
      fprintf(where,
	      cpu_fmt_1,		/* the format string */
	      lss_size,		/* local sendbuf size */
	      send_size,		/* how large were the sends */
	      elapsed_time,		/* how long was the test */
	      messages_sent,
	      failed_sends,
	      local_thruput, 		/* what was the xfer rate */
	      local_cpu_utilization,	/* local cpu */
	      local_service_demand,	/* local service demand */
	      rsr_size,
	      elapsed_time,
	      messages_recvd,
	      remote_thruput,
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
	      local_thruput);
      break;
    case 1:
      fprintf(where,
	      tput_fmt_1,		/* the format string */
	      lss_size, 		/* local sendbuf size */
	      send_size,		/* how large were the sends */
	      elapsed_time, 		/* how long did it take */
	      messages_sent,
	      failed_sends,
	      local_thruput,
	      rsr_size, 		/* remote recvbuf size */
	      elapsed_time,
	      messages_recvd,
	      remote_thruput
	      );
      break;
    }
  }
}

int
  recv_udp_stream()
{
  
  char message[MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET];
  struct	sockaddr_in        myaddr_in,
  peeraddr_in;
  int	s_data;
  int 	addrlen;
  int	len;
  int	sock_opt_len = sizeof(int);
  char	*message_ptr;
  int	bytes_received = 0;
  float	elapsed_time;
  
  int	message_size;
  int	messages_recvd = 0;
  int	measure_cpu;
  
  struct        sigaction     action;

  struct	udp_stream_request_struct	*udp_stream_request;
  struct	udp_stream_response_struct	*udp_stream_response;
  struct	udp_stream_results_struct	*udp_stream_results;
  
  udp_stream_request	= (struct udp_stream_request_struct *)netperf_request->test_specific_data;
  udp_stream_response	= (struct udp_stream_response_struct *)netperf_response->test_specific_data;
  udp_stream_results	= (struct udp_stream_results_struct *)netperf_response->test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_udp_stream: entered...\n");
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
  
  if (debug > 1) {
    fprintf(where,"recv_udp_stream: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response->response_type = UDP_STREAM_RESPONSE;
  
  if (debug > 2) {
    fprintf(where,"recv_udp_stream: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */
  
  if (debug > 1) {
    fprintf(where,"recv_udp_stream: requested alignment of %d\n",
	    udp_stream_request->recv_alignment);
    fflush(where);
  }
  message_ptr = (char *)(( (long)message + 
			(long ) udp_stream_request->recv_alignment -1) & 
			~((long) udp_stream_request->recv_alignment - 1));
  message_ptr = message_ptr + udp_stream_request->recv_offset;
  
  if (debug > 1) {
    fprintf(where,"recv_udp_stream: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Let's clear-out our sockaddr for the sake of cleanlines. Then we */
  /* can put in OUR values !-) At some point, we may want to nail this */
  /* socket to a particular network-level address, but for now, */
  /* INADDR_ANY should be just fine. */
  
  bzero((char *)&myaddr_in,
	sizeof(myaddr_in));
  myaddr_in.sin_family      = AF_INET;
  myaddr_in.sin_addr.s_addr = INADDR_ANY;
  myaddr_in.sin_port        = 0;
  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug > 1) {
    fprintf(where,"recv_udp_stream: grabbing a socket...\n");
    fflush(where);
  }
  
  s_data = socket(AF_INET,
		  SOCK_DGRAM,
		  getprotobyname("udp")->p_proto);
  
  if (s_data < 0) {
    netperf_response->serv_errno = errno;
    send_response();
    exit(1);
  }
  
  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. There may be a desire to */
  /* nail this socket to a specific IP address in a multi-homed, */
  /* multi-connection situation, but for now, we'll ignore the issue */
  /* and concentrate on single connection testing. */
  
  if (bind(s_data,
	   &myaddr_in,
	   sizeof(myaddr_in)) == -1) {
    netperf_response->serv_errno = errno;
    send_response();
    exit(1);
  }
  /* The initiator may have wished-us to modify the socket buffer */
  /* sizes. We should give it a shot. If he didn't ask us to change the */
  /* sizes, we should let him know what sizes were in use at this end. */
  /* If none of this code is compiled-in, then we will tell the */
  /* initiator that we were unable to play with the socket buffer by */
  /* setting the size in the response to -1. We should be sure that */
  /* there will be enough space in the socket buffer to hold addressing */
  /* information, so we will add the size of a sockaddr_in to the size.*/
  
#ifdef SO_RCVBUF
  
  if (udp_stream_request->recv_buf_size) {
    udp_stream_request->recv_buf_size += sizeof(struct sockaddr_in);
    if(setsockopt(s_data,
		  SOL_SOCKET,
		  SO_RCVBUF,
		  (char *)&(udp_stream_request->recv_buf_size),
		  sizeof(int)) < 0) {
      /* Do we really care if this */
      /* call fails? Perhaps we */
      /* should keep going and just */
      /* say what the size was? */
      netperf_response->serv_errno = errno;
      send_response();
      exit(1);
    }
  }
  /* Now, we will find-out what the size actually became, and report */
  /* that back to the user. If the call fails, we will just report a -1 */
  /* back to the initiator for the recv buffer size. */
  
  if (getsockopt(s_data,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&(udp_stream_response->recv_buf_size),
		 &sock_opt_len) < 0) {
    udp_stream_response->recv_buf_size = -1;
  }
  
#else /* the system won't let us play with the buffers */
  
  udp_stream_response->recv_buf_size	= -1;
  
#endif SO_RCVBUF
  
  /* we may have been requested to enable the copy avoidance features. */
  /* can we actually do this? */
  if (udp_stream_request->so_rcvavoid) {
#ifdef SO_RCV_COPYAVOID
    /* be optimistic */
    udp_stream_response->so_rcvavoid = 1;
    if(setsockopt(s_data,
		  SOL_SOCKET,
		  SO_RCV_COPYAVOID,
		  &udp_stream_request->so_rcvavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      udp_stream_response->so_rcvavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    udp_stream_response->so_rcvavoid = 0;
#endif
  }
  
  udp_stream_response->test_length = udp_stream_request->test_length;
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_data, &myaddr_in, &addrlen) == -1){
    netperf_response->serv_errno = errno;
    close(s_data);
    send_response();
    
    exit(1);
  }
  
  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */
  
  udp_stream_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response->serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */
  
  udp_stream_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (udp_stream_request->measure_cpu) {
    /* We will pass the rate into the calibration routine. If the */
    /* user did not specify one, it will be 0.0, and we will do a */
    /* "real" calibration. Otherwise, all it will really do is */
    /* store it away... */
    udp_stream_response->measure_cpu = 1;
    udp_stream_response->cpu_rate = calibrate_local_cpu(udp_stream_request->cpu_rate);
  }
  
  message_size	= udp_stream_request->message_size;
  test_time	= udp_stream_request->test_length;
  
  send_response();
  
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(udp_stream_request->measure_cpu);
  
  /* The loop will exit when the timer pops, or if we happen to recv a */
  /* message of less than send_size bytes... */
  
  times_up = 0;
#ifdef SUNOS4
  /* on some systems (SunOS 4.blah), system calls are restarted. we do */
  /* not want that for a request/response test */
  action.sa_handler = catcher;
  action.sa_flags = SA_INTERRUPT;
  if (sigaction(SIGALRM, &action, NULL) < 0) {
    fprintf(where,"recv_udp_stream: error creating alarm signal.\n");
    fflush(where);
    exit(1);
  }
#else /* SUNOS4 */
  signal(SIGALRM,catcher);
#endif /* SUNOS4 */

  alarm(test_time + PAD_TIME);
  
  if (debug) {
    fprintf(where,"recv_udp_stream: about to enter inner sanctum.\n");
    fflush(where);
  }
  
  while (!times_up) {
    if ((len = recv(s_data, message_ptr, message_size, 0)) != message_size) {
      if ((len == -1) && (errno != EINTR)) {
	netperf_response->serv_errno = errno;
	send_response();
	exit(1);
      }
      break;
    }
    messages_recvd++;
  }
  
  if (debug) {
    fprintf(where,"recv_udp_stream: got %d messages.\n",messages_recvd);
    fflush(where);
  }
  
  
  /* The loop now exits due timer or < send_size bytes received. */
  
  cpu_stop(udp_stream_request->measure_cpu,&elapsed_time);
  
  if (times_up) {
    /* we ended on a timer, subtract the PAD_TIME */
    elapsed_time -= (float)PAD_TIME;
  }
  else {
    alarm(0);
  }
  
  if (debug) {
    fprintf(where,"recv_udp_stream: test ended in %f seconds.\n",elapsed_time);
    fflush(where);
  }
  
  
  /* We will count the "off" message */
  bytes_received = (messages_recvd * message_size) + len;
  
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_udp_stream: got %d bytes\n",
	    bytes_received);
    fflush(where);
  }
  
  netperf_response->response_type		= UDP_STREAM_RESULTS;
  udp_stream_results->bytes_received	= bytes_received;
  udp_stream_results->messages_recvd	= messages_recvd;
  udp_stream_results->elapsed_time	= elapsed_time;
  if (udp_stream_request->measure_cpu) {
    udp_stream_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  else {
    udp_stream_results->cpu_util	= -1.0;
  }
  
  if (debug > 1) {
    fprintf(where,
	    "recv_udp_stream: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

int send_udp_rr(remote_host)
     char	remote_host[];
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
bytes  bytes  bytes   bytes  secs.   per sec  %%      %%      ms/Tr   ms/Tr\n\n";
  
  char *cpu_fmt_0 =
    "%6.3f\n";
  
  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f   %-6.2f %-6.2f %-6.3f  %-6.3f\n";
  
  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";
  
  char *ksink_fmt = "\
Alignment      Offset\n\
Local  Remote  Local  Remote\n\
Send   Recv    Send   Recv\n\
%5d  %5d   %5d  %5d";
  
  
  int 			one = 1;
  float			elapsed_time;
  
  /* we add MAXALIGNMENT and MAXOFFSET to insure that there is enough */
  /* space for a maximally aligned, maximally sized message. At some */
  /* point, we may want to actually make this even larger and cycle */
  /* through the thing one piece at a time.*/
  
  int	len;
  char	*send_message_ptr;
  char	*recv_message_ptr;
  char	*temp_message_ptr;
  int	nummessages;
  int	send_socket;
  int	trans_remaining;
  int	bytes_xferd;
  int	sock_opt_len = sizeof(int);
  
  int	rsp_bytes_left;
  int	rsp_bytes_recvd;
  
  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;
  float	thruput;
  
#ifdef INTERVALS
  /* timing stuff */
#define	MAX_KEPT_TIMES	1024
  int	time_index = 0;
  int	unused_buckets;
  int	kept_times[MAX_KEPT_TIMES];
  int	sleep_usecs;
  unsigned	int	total_times=0;
  struct	timezone	dummy_zone;
  struct	timeval		send_time;
  struct	timeval		recv_time;
  struct	timeval		sleep_timeval;
#endif
  
  struct	hostent	        *hp;
  struct	sockaddr_in	server, peeraddr_in, myaddr_in;
  int	        addrlen;
  
  struct        sigaction       action;

  struct	udp_rr_request_struct	*udp_rr_request;
  struct	udp_rr_response_struct	*udp_rr_response;
  struct	udp_rr_results_struct	*udp_rr_result;
  
  udp_rr_request	= (struct udp_rr_request_struct *)netperf_request->test_specific_data;
  udp_rr_response	= (struct udp_rr_response_struct *)netperf_response->test_specific_data;
  udp_rr_result	= (struct udp_rr_results_struct *)netperf_response->test_specific_data;
  
  /* we want to zero out the times, so we can detect unused entries. */
#ifdef INTERVALS
  time_index = 0;
  while (time_index < MAX_KEPT_TIMES) {
    kept_times[time_index] = 0;
    time_index += 1;
  }
  time_index = 0;
#endif
  
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */
  
  bzero((char *)&server,
	sizeof(server));
  
  if ((hp = gethostbyname(remote_host)) == NULL) {
    fprintf(where,
	    "send_udp_rr: could not resolve the name%s\n",
	    remote_host);
    fflush(where);
  }
  
  bcopy(hp->h_addr,
	(char *)&server.sin_addr,
	hp->h_length);
  
  server.sin_family = hp->h_addrtype;
  
  
  if ( print_headers ) {
    fprintf(where,"UDP REQUEST/RESPONSE TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      fprintf(where,cpu_title,format_units());
    else
      fprintf(where,tput_title,format_units());
  }
  
  /* initialize a few counters */
  
  nummessages	=	0;
  bytes_xferd	=	0;
  times_up 	= 	0;
  
  /* set-up the data buffer with the requested alignment and offset */
  temp_message_ptr = (char *)malloc(MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET);
  send_message_ptr = (char *)(( (long)temp_message_ptr + 
			(long) local_send_align - 1) &	
			~((long) local_send_align - 1));
  send_message_ptr = send_message_ptr + local_send_offset;
  temp_message_ptr = (char *)malloc(MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET);
  recv_message_ptr = (char *)(( (long)temp_message_ptr + 
			(long) local_recv_align - 1) &	
			~((long) local_recv_align - 1));
  recv_message_ptr = recv_message_ptr + local_recv_offset;
  
  /*set up the data socket                        */
  send_socket = socket(AF_INET, 
		       SOCK_DGRAM,
		       0);
  
  if (send_socket < 0){
    perror("netperf: send_udp_rr: udp rr data socket");
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"send_udp_rr: send_socket obtained...\n");
  }
  
  /* Modify the local socket size. If the user has not requested that */
  /* the socket buffers be altered, we will try to find-out what their */
  /* values are. If we cannot touch the socket buffer in any way, we */
  /* will set the values to -1 to indicate that.  The receive socket */
  /* must have enough space to hold addressing information so += a */
  /* sizeof struct sockaddr_in to it. */ 
  
#ifdef SO_SNDBUF
  if (lss_size > 0) {
    if(setsockopt(send_socket, SOL_SOCKET, SO_SNDBUF,
		  (char *)&lss_size, sizeof(int)) < 0) {
      perror("netperf: send_udp_rr: socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_udp_rr: socket send size altered from system default...\n");
      fprintf(where,"                          send: %d\n",lss_size);
    }
  }
  if (lsr_size > 0) {
    lsr_size += sizeof(struct sockaddr_in);
    if(setsockopt(send_socket, SOL_SOCKET, SO_RCVBUF,
		  (char *)&lsr_size, sizeof(int)) < 0) {
      perror("netperf: send_udp_rr: receive socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_udp_rr: socket recv size altered from system default...\n");
      fprintf(where,"                          recv: %d\n",lsr_size);
    }
  }
  
  
  /* Now, we will find-out what the size actually became, and report */
  /* that back to the user. If the call fails, we will just report a -1 */
  /* back to the initiator for the recv buffer size. */
  
  
  if (getsockopt(send_socket,
		 SOL_SOCKET,	
		 SO_SNDBUF,
		 (char *)&lss_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_udp_rr: getsockopt");
    lss_size = -1;
  }
  if (getsockopt(send_socket,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&lsr_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_udp_rr: getsockopt");
    lsr_size = -1;
  }
  
  if (debug) {
    fprintf(where,"netperf: send_udp_rr: socket sizes determined...\n");
    fprintf(where,"         send: %d recv: %d\n",lss_size,lsr_size);
  }
  
#else SO_SNDBUF
  
  lss_size = -1;
  lsr_size = -1;
  
#endif SO_SNDBUF
  
  /* the user may have requested copy avoidance... */
#ifdef SO_RCV_COPYAVOID
  if (loc_rcvavoid) {
    if (setsockopt(send_socket,
		   SOL_SOCKET,
		   SO_RCV_COPYAVOID,
		   &loc_rcvavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable receive copy avoidance");
      loc_rcvavoid = 0;
    }
  }
#endif
  
#ifdef SO_SND_COPYAVOID
  if (loc_sndavoid) {
    if (setsockopt(send_socket,
		   SOL_SOCKET,
		   SO_SND_COPYAVOID,
		   &loc_sndavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable send copy avoidance");
      loc_sndavoid = 0;
    }
  }
#endif
  
  /* If the user has requested cpu utilization measurements, we must */
  /* calibrate the cpu(s). We will perform this task within the tests */
  /* themselves. If the user has specified the cpu rate, then */
  /* calibrate_local_cpu will return rather quickly as it will have */
  /* nothing to do. If local_cpu_rate is zero, then we will go through */
  /* all the "normal" calibration stuff and return the rate back. If */
  /* there is no idle counter in the kernel idle loop, the */
  /* local_cpu_rate will be set to -1. */
  
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
  
  netperf_request->request_type	=	DO_UDP_RR;
  udp_rr_request->recv_buf_size	=	rsr_size;
  udp_rr_request->send_buf_size	=	rss_size;
  udp_rr_request->recv_alignment	=	remote_recv_align;
  udp_rr_request->recv_offset	=	remote_recv_offset;
  udp_rr_request->send_alignment	=	remote_send_align;
  udp_rr_request->send_offset	=	remote_send_offset;
  udp_rr_request->request_size	=	req_size;
  udp_rr_request->response_size	=	rsp_size;
  udp_rr_request->measure_cpu	=	remote_cpu_usage;
  udp_rr_request->cpu_rate	=	remote_cpu_rate;
  udp_rr_request->so_rcvavoid	=	rem_rcvavoid;
  udp_rr_request->so_sndavoid	=	rem_sndavoid;
  if (test_time) {
    udp_rr_request->test_length	=	test_time;
  }
  else {
    udp_rr_request->test_length	=	test_trans * -1;
  }
  
  if (debug > 1) {
    fprintf(where,"netperf: send_udp_rr: requesting UDP request/response test\n");
  }
  
  send_request();
  
  /* The response from the remote will contain all of the relevant 	*/
  /* socket parameters for this test type. We will put them back into 	*/
  /* the variables here so they can be displayed if desired.  The	*/
  /* remote will have calibrated CPU if necessary, and will have done	*/
  /* all the needed set-up we will have calibrated the cpu locally	*/
  /* before sending the request, and will grab the counter value right	*/
  /* after the connect returns. The remote will grab the counter right	*/
  /* after the accept call. This saves the hassle of extra messages	*/
  /* being sent for the UDP tests.					*/
  
  recv_response();
  
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote listen done.\n");
    rsr_size	=	udp_rr_response->recv_buf_size;
    rss_size	=	udp_rr_response->send_buf_size;
    remote_cpu_usage=	udp_rr_response->measure_cpu;
    remote_cpu_rate = 	udp_rr_response->cpu_rate;
    /* port numbers in proper order */
    server.sin_port	=	udp_rr_response->data_port_number;
    server.sin_port = 	htons(server.sin_port);
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /* Connect up to the remote port on the data socket. This will set */
  /* the default destination address on this socket.  */
  
  if ( connect(send_socket, 
	       (struct sockaddr *)&server,
	       sizeof(server)) < 0 ) {
    perror("netperf: data socket connect failed");
    
    exit(1);
  }
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(send_socket, &myaddr_in, &addrlen) == -1){
    perror("bogus dude");
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
#ifdef SUNOS4 */
    /* on some systems (SunOS 4.blah), system calls are restarted. we do */
    /* not want that for a request/response test */
    action.sa_handler = catcher;
    action.sa_flags = SA_INTERRUPT;
    if (sigaction(SIGALRM, &action, NULL) < 0) {
      fprintf(where,"send_udp_rr: error creating alarm signal.\n");
      fflush(where);
      exit(1);
    }
#else /* SUNOS4 */
    signal(SIGALRM, catcher);
#endif /* SUNOS4 */
    alarm(test_time);
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
  
  /* We use an "OR" to control test execution. When the test is */
  /* controlled by time, the byte count check will always return false. */
  /* When the test is controlled by byte count, the time test will */
  /* always return false. When the test is finished, the whole */
  /* expression will go false and we will stop sending data. I think I */
  /* just arbitrarily decrement trans_remaining for the timed test, but */
  /* will not do that just yet... One other question is whether or not */
  /* the send buffer and the receive buffer should be the same buffer. */
  while ((!times_up) || (trans_remaining > 0)) {
    /* send the request */
#ifdef INTERVALS
    gettimeofday(&send_time,&dummy_zone);
#endif
    if((len=send(send_socket,
		 send_message_ptr,
		 req_size,
		 0)) != req_size) {
      if (errno == EINTR) {
	/* We likely hit */
	/* test-end time. */
	break;
      }
      perror("send_udp_rr: data send error");
      exit(1);
    }
    
    /* receive the response. with UDP we will get it all, or nothing */
    
    if((rsp_bytes_recvd=recv(send_socket,
			     recv_message_ptr,
			     rsp_size,
			     0)) != rsp_size) {
      if (errno == EINTR) {
	/* Again, we have likely hit test-end time */
	break;
      }
      perror("send_udp_rr: data recv error");
      exit(1);
    }
#ifdef INTERVALS
    gettimeofday(&recv_time,&dummy_zone);
    
    /* now we do some arithmatic on the two timevals */
    if (recv_time.tv_usec < send_time.tv_usec) {
      /* we wrapped around a second */
      recv_time.tv_usec += 1000000;
      recv_time.tv_sec  -= 1;
    }
    
    /* and store it away */
    kept_times[time_index] = (recv_time.tv_sec - send_time.tv_sec) * 1000000;
    kept_times[time_index] += (recv_time.tv_usec - send_time.tv_usec);
    
    /* at this point, we may wish to sleep for some period of */
    /* time, so we see how long that last transaction just took, */
    /* and sleep for the difference of that and the interval. We */
    /* will not sleep if the time would be less than a */
    /* millisecond.  */
    if (interval_usecs > 0) {
      sleep_usecs = interval_usecs - kept_times[time_index];
      if (sleep_usecs > 1000) {
	/* we sleep */
	sleep_timeval.tv_sec = sleep_usecs / 1000000;
	sleep_timeval.tv_usec = sleep_usecs % 1000000;
	select(0,
	       0,
	       0,
	       0,
	       &sleep_timeval);
      }
    }
    
    /* now up the time index */
    time_index = (time_index +1)%MAX_KEPT_TIMES;
#endif
    nummessages++;          
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug > 3) {
      fprintf(where,"Transaction %d completed\n",nummessages);
      fflush(where);
    }
    
  }
  
  /* The test is over. Flush the buffers to the remote end. We do a */
  /* graceful release to insure that all data has been taken by the */
  /* remote. Of course, since this was a request/response test, there */
  /* should be no data outstanding on the socket ;-) */ 
  
  if (shutdown(send_socket,1) == -1) {
    perror("bsdperf: cannot shutdown udp stream socket");
    
    exit(1);
  }
  
  /* this call will always give us the elapsed time for the test, and */
  /* will also store-away the necessaries for cpu utilization */
  
  cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being measured? */
  /* how long did we really run? */
  
  /* Get the statistics from the remote end. The remote will have */
  /* calculated service demand and all those interesting things. If it */
  /* wasn't supposed to care, it will return obvious values. */
  
  recv_response();
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote results obtained\n");
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /* We now calculate what our thruput was for the test. In the future, */
  /* we may want to include a calculation of the thruput measured by */
  /* the remote, but it should be the case that for a UDP stream test, */
  /* that the two numbers should be *very* close... We calculate */
  /* bytes_sent regardless of the way the test length was controlled. */
  /* If it was time, we needed to, and if it was by bytes, the user may */
  /* have specified a number of bytes that wasn't a multiple of the */
  /* send_size, so we really didn't send what he asked for ;-) We use */
  
  bytes_xferd	= (req_size * nummessages) + (rsp_size * nummessages);
  thruput		= calc_thruput(bytes_xferd);
  
  if (local_cpu_usage || remote_cpu_usage) {
    /* We must now do a little math for service demand and cpu */
    /* utilization for the system(s) */
    /* Of course, some of the information might be bogus because */
    /* there was no idle counter in the kernel(s). We need to make */
    /* a note of this for the user's benefit...*/
    if (local_cpu_usage) {
      if (local_cpu_rate == 0.0) {
	fprintf(where,"WARNING WARNING WARNING  WARNING WARNING WARNING  WARNING!\n");
	fprintf(where,"Local CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      local_cpu_utilization = calc_cpu_util(0.0);
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      local_service_demand  = calc_service_demand((double) nummessages*1024,
						  0.0,
						  0.0);
    }
    else {
      local_cpu_utilization	= -1.0;
      local_service_demand	= -1.0;
    }
    
    if (remote_cpu_usage) {
      if (remote_cpu_rate == 0.0) {
	fprintf(where,"DANGER  DANGER  DANGER    DANGER  DANGER  DANGER    DANGER!\n");
	fprintf(where,"Remote CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      remote_cpu_utilization = udp_rr_result->cpu_util;
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      remote_service_demand  = calc_service_demand((double) nummessages*1024,
						   0.0,
						   remote_cpu_utilization);
    }
    else {
      remote_cpu_utilization = -1.0;
      remote_service_demand  = -1.0;
    }
    
    /* We are now ready to print all the information. If the user */
    /* has specified zero-level verbosity, we will just print the */
    /* local service demand, or the remote service demand. If the */
    /* user has requested verbosity level 1, he will get the basic */
    /* "streamperf" numbers. If the user has specified a verbosity */
    /* of greater than 1, we will display a veritable plethora of */
    /* background information from outside of this block as it it */
    /* not cpu_measurement specific...  */
    
    switch (verbosity) {
    case 0:
      if (local_cpu_usage) {
	fprintf(where,
		cpu_fmt_0,
		local_service_demand);
      }
      else {
	fprintf(where,
		cpu_fmt_0,
		remote_service_demand);
      }
      break;
    case 1:
    case 2:
      fprintf(where,
	      cpu_fmt_1_line_1,		/* the format string */
	      lss_size,		/* local sendbuf size */
	      lsr_size,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* guess */
	      elapsed_time,		/* how long was the test */
	      nummessages/elapsed_time,
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
	      nummessages/elapsed_time);
      break;
    case 1:
    case 2:
      fprintf(where,
	      tput_fmt_1_line_1,	/* the format string */
	      lss_size,
	      lsr_size,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* how large were the responses */
	      elapsed_time, 		/* how long did it take */
	      nummessages/elapsed_time);
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
  
  if (verbosity > 1) {
    /* The user wanted to know it all, so we will give it to him. */
    /* This information will include as much as we can find about */
    /* UDP statistics, the alignments of the sends and receives */
    /* and all that sort of rot... */
    
#ifdef INTERVALS
    kept_times[MAX_KEPT_TIMES] = 0;
    time_index = 0;
    while (time_index < MAX_KEPT_TIMES) {
      if (kept_times[time_index] > 0) {
	total_times += kept_times[time_index];
      }
      else
	unused_buckets++;
      time_index += 1;
    }
    total_times /= (MAX_KEPT_TIMES-unused_buckets);
    fprintf(where,
	    "Average response time %d usecs\n",
	    total_times);
#endif
  }
}

int 
  recv_udp_rr()
{
  
  char message[MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET];
  struct	sockaddr_in        myaddr_in,
  peeraddr_in;
  int	s_data;
  int 	addrlen;
  int	len;
  int	sock_opt_len = sizeof(int);
  int	measure_cpu;
  char	*recv_message_ptr;
  char	*send_message_ptr;
  char	*temp_message_ptr;
  int	trans_received;
  int	trans_remaining;
  int	bytes_received;
  int	bytes_sent;
  int	request_bytes_recvd;
  int	request_bytes_remaining;
  float	elapsed_time;
  
  struct        sigaction       action;

  struct	udp_rr_request_struct	*udp_rr_request;
  struct	udp_rr_response_struct	*udp_rr_response;
  struct	udp_rr_results_struct	*udp_rr_results;
  
  udp_rr_request	= (struct udp_rr_request_struct *)netperf_request->test_specific_data;
  udp_rr_response	= (struct udp_rr_response_struct *)netperf_response->test_specific_data;
  udp_rr_results	= (struct udp_rr_results_struct *)netperf_response->test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_udp_rr: entered...\n");
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
    fprintf(where,"recv_udp_rr: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response->response_type = UDP_RR_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_udp_rr: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variables to be at the desired */
  /* alignments with the desired offsets. */
  
  if (debug) {
    fprintf(where,"recv_udp_rr: requested recv alignment of %d offset %d\n",
	    udp_rr_request->recv_alignment,
	    udp_rr_request->recv_offset);
    fprintf(where,"recv_udp_rr: requested send alignment of %d offset %d\n",
	    udp_rr_request->send_alignment,
	    udp_rr_request->send_offset);
    fflush(where);
  }
  recv_message_ptr = (char *)(( (long)message + 
			(long) udp_rr_request->recv_alignment -1) & 
			~((long) udp_rr_request->recv_alignment - 1));
  recv_message_ptr = recv_message_ptr + udp_rr_request->recv_offset;
  
  send_message_ptr = (char *)(( (long)message + 
			(long) udp_rr_request->send_alignment -1) & 
			~((long) udp_rr_request->send_alignment - 1));
  send_message_ptr = send_message_ptr + udp_rr_request->send_offset;
  
  if (debug) {
    fprintf(where,"recv_udp_rr: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Let's clear-out our sockaddr for the sake of cleanlines. Then we */
  /* can put in OUR values !-) At some point, we may want to nail this */
  /* socket to a particular network-level address, but for now, */
  /* INADDR_ANY should be just fine. */
  
  bzero((char *)&myaddr_in,
	sizeof(myaddr_in));
  myaddr_in.sin_family      = AF_INET;
  myaddr_in.sin_addr.s_addr = INADDR_ANY;
  myaddr_in.sin_port        = 0;
  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_udp_rr: grabbing a socket...\n");
    fflush(where);
  }
  
  s_data = socket(AF_INET,
		  SOCK_DGRAM,
		  0);
  
  if (s_data < 0) {
    netperf_response->serv_errno = errno;
    send_response();
    
    exit(1);
  }
  
  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. There may be a desire to */
  /* nail this socket to a specific IP address in a multi-homed, */
  /* multi-connection situation, but for now, we'll ignore the issue */
  /* and concentrate on single connection testing. */
  
  if (bind(s_data,
	   &myaddr_in,
	   sizeof(myaddr_in)) == -1) {
    netperf_response->serv_errno = errno;
    close(s_data);
    send_response();
    
    exit(1);
  }
  /* The initiator may have wished-us to modify the socket buffer */
  /* sizes. We should give it a shot. If he didn't ask us to change the */
  /* sizes, we should let him know what sizes were in use at this end. */
  /* If none of this code is compiled-in, then we will tell the */
  /* initiator that we were unable to play with the socket buffer by */
  /* setting the size in the response to -1. We should add enough space */
  /* to the user's request to insure that addressing information can be */
  /* saved, so let's add the sizeof a struct sockaddr_in. */
  
#ifdef SO_RCVBUF
  
  if (udp_rr_request->recv_buf_size) {
    udp_rr_request->recv_buf_size += sizeof(struct sockaddr_in);
    if(setsockopt(s_data,
		  SOL_SOCKET,
		  SO_RCVBUF,
		  (char *)&(udp_rr_request->recv_buf_size),
		  sizeof(int)) < 0) {
      /* Do we really care if this */
      /* call fails? Perhaps we */
      /* should keep going and just */
      /* say what the size was? */
      netperf_response->serv_errno = errno;
      send_response();
      
      exit(1);
    }
  }
  
  if (udp_rr_request->send_buf_size) {
    if(setsockopt(s_data,
		  SOL_SOCKET,
		  SO_SNDBUF,
		  (char *)&(udp_rr_request->send_buf_size),
		  sizeof(int)) < 0) {
      /* Do we really care if this */
      /* call fails? Perhaps we */
      /* should keep going and just */
      /* say what the size was? */
      netperf_response->serv_errno = errno;
      send_response();
      
      exit(1);
    }
  }
  /* Now, we will find-out what the sizes actually became, and report */
  /* them back to the user. If the calls fail, we will just report a -1 */
  /* back to the initiator for the buffer size. */
  
  if (getsockopt(s_data,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&(udp_rr_response->recv_buf_size),
		 &sock_opt_len) < 0) {
    udp_rr_response->recv_buf_size = -1;
  }
  if (getsockopt(s_data,
		 SOL_SOCKET,	
		 SO_SNDBUF,
		 (char *)&(udp_rr_response->send_buf_size),
		 &sock_opt_len) < 0) {
    udp_rr_response->send_buf_size = -1;
  }
  
  
#else /* the system won't let us play with the buffers */
  
  udp_rr_response->recv_buf_size	= -1;
  udp_rr_response->send_buf_size	= -1;
  
#endif SO_RCVBUF
  
  /* we may have been requested to enable the copy avoidance features. */
  /* can we actually do this? */
  if (udp_rr_request->so_rcvavoid) {
#ifdef SO_RCV_COPYAVOID
    /* be optimistic */
    udp_rr_response->so_rcvavoid = 1;
    if(setsockopt(s_data,
		  SOL_SOCKET,
		  SO_RCV_COPYAVOID,
		  &udp_rr_request->so_rcvavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      udp_rr_response->so_rcvavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    udp_rr_response->so_rcvavoid = 0;
#endif
  }
  
  if (udp_rr_request->so_sndavoid) {
#ifdef SO_SND_COPYAVOID
    /* be optimistic */
    udp_rr_response->so_sndavoid = 1;
    if(setsockopt(s_data,
		  SOL_SOCKET,
		  SO_SND_COPYAVOID,
		  &udp_rr_request->so_sndavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      udp_rr_response->so_sndavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    udp_rr_response->so_sndavoid = 0;
#endif
  }
  
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_data, &myaddr_in, &addrlen) == -1){
    netperf_response->serv_errno = errno;
    close(s_data);
    send_response();
    
    exit(1);
  }
  
  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */
  
  udp_rr_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response->serv_errno   = 0;
  
  fprintf(where,"recv port number %d\n",myaddr_in.sin_port);
  fflush(where);
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */
  
  udp_rr_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (udp_rr_request->measure_cpu) {
    udp_rr_response->measure_cpu = 1;
    udp_rr_response->cpu_rate = calibrate_local_cpu(udp_rr_request->cpu_rate);
  }
  
  send_response();
  
  
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(udp_rr_request->measure_cpu);
  
  if (udp_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
#ifdef SUNOS4 
    /* on some systems (SunOS 4.blah), system calls are restarted. we do */
    /* not want that for a request/response test */
    action.sa_handler = catcher;
    action.sa_flags = SA_INTERRUPT; 
    if (sigaction(SIGALRM, &action, NULL) < 0) {
      fprintf(where,"recv_udp_stream: error creating alarm signal.\n");
      fflush(where);
      exit(1);
    }
#else /* SUNOS4 */
    signal(SIGALRM, catcher);
#endif /* SUNOS4 */
    alarm(udp_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = udp_rr_request->test_length * -1;
  }
  
  addrlen = sizeof(peeraddr_in);
  bzero((char *)&peeraddr_in, addrlen);
  
  while ((!times_up) || (trans_remaining > 0)) {
    
    if (debug) {
      fprintf(where,"Here we go folks...\n");
      fprintf(where,"request_size is %d\n",
	      udp_rr_request->request_size);
      fprintf(where,"response_size is %d\n",
	      udp_rr_request->response_size);
      fprintf(where,"trans remaining %d\n",
	      trans_remaining);
      fflush(where);
    }
    
    /* receive the request from the other side */
    if (recvfrom(s_data,
		 recv_message_ptr,
		 udp_rr_request->request_size,
		 0,
		 &peeraddr_in,
		 &addrlen) != udp_rr_request->request_size) {
      if (errno == EINTR) {
	/* we must have hit the end of test time. */
	break;
      }
      netperf_response->serv_errno = errno;
      send_response();
      exit(1);
    }
    
    /* Now, send the response to the remote */
    if (sendto(s_data,
	       send_message_ptr,
	       udp_rr_request->response_size,
	       0,
	       &peeraddr_in,
	       addrlen) != udp_rr_request->response_size) {
      if (errno == EINTR) {
	/* we have hit end of test time. */
	break;
      }
      netperf_response->serv_errno = errno;
      send_response();
      exit(1);
    }
    
    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug) {
      fprintf(where,
	      "recv_udp_rr: Transaction %d complete.\n",
	      trans_received);
      fflush(where);
    }
    
  }
  
  
  /* The loop now exits due to timeout or transaction count being */
  /* reached */
  
  cpu_stop(udp_rr_request->measure_cpu,&elapsed_time);
  
  if (times_up) {
    /* we ended the test by time, which was at least 2 seconds */
    /* longer than we wanted to run. so, we want to subtract */
    /* PAD_TIME from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_udp_rr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }
  
  udp_rr_results->bytes_received	= (trans_received * 
					   (udp_rr_request->request_size + 
					    udp_rr_request->response_size));
  udp_rr_results->trans_received	= trans_received;
  udp_rr_results->elapsed_time	= elapsed_time;
  if (udp_rr_request->measure_cpu) {
    udp_rr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_udp_rr: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

int 
  recv_tcp_rr()
{
  
  char message[MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET];
  struct	sockaddr_in        myaddr_in,
  peeraddr_in;
  int	s_listen,s_data;
  int 	addrlen;
  int	len;
  int	sock_opt_len = sizeof(int);
  int	one = 1;
  int	measure_cpu;
  char	*recv_message_ptr;
  char	*send_message_ptr;
  char	*temp_message_ptr;
  int	trans_received;
  int	trans_remaining;
  int	bytes_received;
  int	bytes_sent;
  int	request_bytes_recvd;
  int	request_bytes_remaining;
  int	timed_out = 0;
  float	elapsed_time;
  
  struct        sigaction    action;

  struct	tcp_rr_request_struct	*tcp_rr_request;
  struct	tcp_rr_response_struct	*tcp_rr_response;
  struct	tcp_rr_results_struct	*tcp_rr_results;
  
  tcp_rr_request	= (struct tcp_rr_request_struct *)netperf_request->test_specific_data;
  tcp_rr_response	= (struct tcp_rr_response_struct *)netperf_response->test_specific_data;
  tcp_rr_results	= (struct tcp_rr_results_struct *)netperf_response->test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_tcp_rr: entered...\n");
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
    fprintf(where,"recv_tcp_rr: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response->response_type = TCP_RR_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_tcp_rr: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variables to be at the desired */
  /* alignments with the desired offsets. */
  
  if (debug) {
    fprintf(where,"recv_tcp_rr: requested recv alignment of %d offset %d\n",
	    tcp_rr_request->recv_alignment,
	    tcp_rr_request->recv_offset);
    fprintf(where,"recv_tcp_rr: requested send alignment of %d offset %d\n",
	    tcp_rr_request->send_alignment,
	    tcp_rr_request->send_offset);
    fflush(where);
  }
  recv_message_ptr = (char *)(( (long)message + 
			(long) tcp_rr_request->recv_alignment -1) & 
			~((long) tcp_rr_request->recv_alignment - 1));
  recv_message_ptr = recv_message_ptr + tcp_rr_request->recv_offset;
  
  send_message_ptr = (char *)(( (long)message + 
			(long) tcp_rr_request->send_alignment -1) & 
			~((long) tcp_rr_request->send_alignment - 1));
  send_message_ptr = send_message_ptr + tcp_rr_request->send_offset;
  
  if (debug) {
    fprintf(where,"recv_tcp_rr: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Let's clear-out our sockaddr for the sake of cleanlines. Then we */
  /* can put in OUR values !-) At some point, we may want to nail this */
  /* socket to a particular network-level address, but for now, */
  /* INADDR_ANY should be just fine. */
  
  bzero((char *)&myaddr_in,
	sizeof(myaddr_in));
  myaddr_in.sin_family      = AF_INET;
  myaddr_in.sin_addr.s_addr = INADDR_ANY;
  myaddr_in.sin_port        = 0;
  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_tcp_rr: grabbing a socket...\n");
    fflush(where);
  }
  
  s_listen = socket(AF_INET,
		    SOCK_STREAM,
		    0);
  
  if (s_listen < 0) {
    netperf_response->serv_errno = errno;
    send_response();
    
    exit(1);
  }
  
  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. There may be a desire to */
  /* nail this socket to a specific IP address in a multi-homed, */
  /* multi-connection situation, but for now, we'll ignore the issue */
  /* and concentrate on single connection testing. */
  
  if (bind(s_listen,
	   &myaddr_in,
	   sizeof(myaddr_in)) == -1) {
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  /* The initiator may have wished-us to modify the socket buffer */
  /* sizes. We should give it a shot. If he didn't ask us to change the */
  /* sizes, we should let him know what sizes were in use at this end. */
  /* If none of this code is compiled-in, then we will tell the */
  /* initiator that we were unable to play with the socket buffer by */
  /* setting the size in the response to -1. */
  
#ifdef SO_RCVBUF
  
  if (tcp_rr_request->recv_buf_size) {
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_RCVBUF,
		  (char *)&(tcp_rr_request->recv_buf_size),
		  sizeof(int)) < 0) {
      /* Do we really care if this */
      /* call fails? Perhaps we */
      /* should keep going and just */
      /* say what the size was? */
      netperf_response->serv_errno = errno;
      send_response();
      
      exit(1);
    }
  }
  
  if (tcp_rr_request->send_buf_size) {
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_SNDBUF,
		  (char *)&(tcp_rr_request->send_buf_size),
		  sizeof(int)) < 0) {
      /* Do we really care if this */
      /* call fails? Perhaps we */
      /* should keep going and just */
      /* say what the size was? */
      netperf_response->serv_errno = errno;
      send_response();
      
      exit(1);
    }
  }
  /* Now, we will find-out what the sizes actually became, and report */
  /* them back to the user. If the calls fail, we will just report a -1 */
  /* back to the initiator for the buffer size. */
  
  if (getsockopt(s_listen,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&(tcp_rr_response->recv_buf_size),
		 &sock_opt_len) < 0) {
    tcp_rr_response->recv_buf_size = -1;
  }
  if (getsockopt(s_listen,
		 SOL_SOCKET,	
		 SO_SNDBUF,
		 (char *)&(tcp_rr_response->send_buf_size),
		 &sock_opt_len) < 0) {
    tcp_rr_response->send_buf_size = -1;
  }
  
  
#else /* the system won't let us play with the buffers */
  
  tcp_rr_response->recv_buf_size	= -1;
  tcp_rr_response->send_buf_size	= -1;
  
#endif SO_RCVBUF
  
  /* we need to do something with nodelay */
#ifdef TCP_NODELAY
  if (tcp_rr_request->no_delay) {
    if(setsockopt(s_listen,
		  getprotobyname("tcp")->p_proto,
		  TCP_NODELAY,
		  &one,
		  sizeof(one)) < 0) {
      fprintf(where,
	      "recv_tcp_rr: error on setting nodelay: %d\n",
	      errno);
      fflush(where);
      tcp_rr_response->no_delay = 0;
    }
    
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_rr: TCP_NODELAY has been requested...\n");
      fflush(where);
    }
  }
#else TCP_NODELAY
  
  tcp_rr_response->no_delay = 0;
  
#endif TCP_NODELAY
  
  /* we may have been requested to enable the copy avoidance features. */
  /* can we actually do this? */
  if (tcp_rr_request->so_rcvavoid) {
#ifdef SO_RCV_COPYAVOID
    /* be optimistic */
    tcp_rr_response->so_rcvavoid = 1;
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_RCV_COPYAVOID,
		  &tcp_rr_request->so_rcvavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      tcp_rr_response->so_rcvavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    tcp_rr_response->so_rcvavoid = 0;
#endif
  }
  
  if (tcp_rr_request->so_sndavoid) {
#ifdef SO_SND_COPYAVOID
    /* be optimistic */
    tcp_rr_response->so_sndavoid = 1;
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_SND_COPYAVOID,
		  &tcp_rr_request->so_sndavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      tcp_rr_response->so_sndavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    tcp_rr_response->so_sndavoid = 0;
#endif
  }
  
  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == -1) {
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen, &myaddr_in, &addrlen) == -1){
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */
  
  tcp_rr_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response->serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */
  
  tcp_rr_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (tcp_rr_request->measure_cpu) {
    tcp_rr_response->measure_cpu = 1;
    tcp_rr_response->cpu_rate = calibrate_local_cpu(tcp_rr_request->cpu_rate);
  }
  
  send_response();
  
  addrlen = sizeof(peeraddr_in);
  
  if ((s_data=accept(s_listen,&peeraddr_in,&addrlen)) ==
      -1) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"recv_tcp_rr: accept completes on the data connection.\n");
    fflush(where);
  }
  
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(tcp_rr_request->measure_cpu);
  
  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */
  
  if (tcp_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
#ifdef SUNOS4
    /* on some systems (SunOS 4.blah), system calls are restarted. we do */
    /* not want that for a request/response test */
    action.sa_handler = catcher;
    action.sa_flags = SA_INTERRUPT;
    if (sigaction(SIGALRM, &action, NULL) < 0) {
      fprintf(where,"recv_tcp_rr: error creating alarm signal.\n");
      fflush(where);
      exit(1);
    }
#else /* SUNOS4 */
    signal(SIGALRM, catcher);
#endif /* SUNOS4 */
    alarm(tcp_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = tcp_rr_request->test_length * -1;
  }
  
  while ((!times_up) || (trans_remaining > 0)) {
    temp_message_ptr	= recv_message_ptr;
    request_bytes_remaining	= tcp_rr_request->request_size;
    
    /* receive the request from the other side */
    while(request_bytes_remaining > 0) {
      if((request_bytes_recvd=recv(s_data,
				   temp_message_ptr,
				   request_bytes_remaining,
				   0)) < 0) {
	if (errno == EINTR) {
	  /* the timer popped */
	  timed_out = 1;
	  break;
	}
	netperf_response->serv_errno = errno;
	send_response();
	exit(1);
      }
      else {
	request_bytes_remaining -= request_bytes_recvd;
	temp_message_ptr  += request_bytes_recvd;
      }
    }
    
    if (timed_out) {
      /* we hit the end of the test based on time - lets */
      /* bail out of here now... */
      fprintf(where,"yo5\n");
      fflush(where);						
      break;
    }
    
    /* Now, send the response to the remote */
    if((bytes_sent=send(s_data,
			send_message_ptr,
			tcp_rr_request->response_size,
			0)) == -1) {
      if (errno == EINTR) {
	/* the test timer has popped */
	timed_out = 1;
	fprintf(where,"yo6\n");
	fflush(where);						
	break;
      }
      netperf_response->serv_errno = 999;
      send_response();
      exit(1);
    }
    
    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug) {
      fprintf(where,
	      "recv_tcp_rr: Transaction %d complete\n",
	      trans_received);
      fflush(where);
    }
  }
  
  
  /* The loop now exits due to timeout or transaction count being */
  /* reached */
  
  cpu_stop(tcp_rr_request->measure_cpu,&elapsed_time);
  
  if (timed_out) {
    /* we ended the test by time, which was at least 2 seconds */
    /* longer than we wanted to run. so, we want to subtract */
    /* PAD_TIME from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_tcp_rr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }
  
  tcp_rr_results->bytes_received	= (trans_received * 
					   (tcp_rr_request->request_size + 
					    tcp_rr_request->response_size));
  tcp_rr_results->trans_received	= trans_received;
  tcp_rr_results->elapsed_time	= elapsed_time;
  if (tcp_rr_request->measure_cpu) {
    tcp_rr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_tcp_rr: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

int
  loc_cpu_rate()
{
  /* a rather simple little test - it merely calibrates the local cpu */
  /* and prints the results. There are no headers to allow someone to */
  /* find a rate and use it in other tests automagically by setting a */
  /* variable equal to the output of this test. We ignore any rates */
  /* that may have been specified. In fact, we ignore all of the */
  /* command line args! */
  
  fprintf(where,
	  "%g",
	  calibrate_local_cpu(0.0));
  
}	

int
  rem_cpu_rate()
{
  /* this test is much like the local variant, except that it works for */
  /* the remote system, so in this case, we do pay attention to the */
  /* value of the '-H' command line argument. */
  
  fprintf(where,
	  "%g",
	  calibrate_remote_cpu(0.0));
  
}

/*********************************/

int 
send_tcp_async_rr(remote_host)
     char	remote_host[];
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
bytes  bytes  bytes   bytes  secs.   per sec  %%      %%      ms/Tr   ms/Tr\n\n";
  
  char *cpu_fmt_0 =
    "%6.3f\n";
  
  char *cpu_fmt_1_line_1 = "\
%-6d %-6d %-6d  %-6d %-6.2f  %-6.2f   %-6.2f %-6.2f %-6.3f  %-6.3f\n";
  
  char *cpu_fmt_1_line_2 = "\
%-6d %-6d\n";
  
  char *ksink_fmt = "\
Alignment      Offset\n\
Local  Remote  Local  Remote\n\
Send   Recv    Send   Recv\n\
%5d  %5d   %5d  %5d";
  
  
  int 			one = 1;
  int			timed_out = 0;
  float			elapsed_time;
  
  int	len;
  char	*send_message_ptr;
  char	*recv_message_ptr;
  char	*temp_message_ptr;
  int	nummessages;
  int	send_socket;
  int	trans_remaining;
  double	bytes_xferd;
  int	sock_opt_len = sizeof(int);
  
  int	rsp_bytes_left;
  int	rsp_bytes_recvd;
  
  float	local_cpu_utilization;
  float	local_service_demand;
  float	remote_cpu_utilization;
  float	remote_service_demand;
  double	thruput;
  
  struct	hostent	        *hp;
  struct	sockaddr_in	server;
  
  struct        sigaction       action;

  struct	tcp_arr_request_struct	*tcp_arr_request;
  struct	tcp_arr_response_struct	*tcp_arr_response;
  struct	tcp_arr_results_struct	*tcp_arr_result;
  
  tcp_arr_request = 
    (struct tcp_arr_request_struct *)netperf_request->test_specific_data;
  tcp_arr_response = 
    (struct tcp_arr_response_struct *)netperf_response->test_specific_data;
  tcp_arr_result =
    (struct tcp_arr_results_struct *)netperf_response->test_specific_data;
  
  /* since we are now disconnected from the code that established the */
  /* control socket, and since we want to be able to use different */
  /* protocols and such, we are passed the name of the remote host and */
  /* must turn that into the test specific addressing information. */
  
  bzero((char *)&server,
	sizeof(server));
  
  if ((hp = gethostbyname(remote_host)) == NULL) {
    fprintf(where,
	    "send_tcp_rr: could not resolve the name%s\n",
	    remote_host);
    fflush(where);
  }
  
  bcopy(hp->h_addr,
	(char *)&server.sin_addr,
	hp->h_length);
  
  server.sin_family = hp->h_addrtype;
  
  
  if ( print_headers ) {
    fprintf(where,"TCP Async REQUEST/RESPONSE TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      fprintf(where,cpu_title,format_units());
    else
      fprintf(where,tput_title,format_units());
  }
  
  /* initialize a few counters */
  
  nummessages	=	0;
  bytes_xferd	=	0.0;
  times_up 	= 	0;
  
  /* set-up the data buffers with the requested alignment and offset */
  temp_message_ptr = (char *)malloc(MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET);
  send_message_ptr = (char *)(( (long) temp_message_ptr + 
			(long) local_send_align - 1) &	
			~((long) local_send_align - 1));
  send_message_ptr = send_message_ptr + local_send_offset;
  temp_message_ptr = (char *)malloc(MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET);
  recv_message_ptr = (char *)(( (long) temp_message_ptr + 
			(long) local_recv_align - 1) &	
			~((long) local_recv_align - 1));
  recv_message_ptr = recv_message_ptr + local_recv_offset;
  
  /*set up the data socket                        */
  send_socket = socket(AF_INET, 
		       SOCK_STREAM,
		       0);
  
  if (send_socket < 0){
    perror("netperf: send_tcp_rr: tcp stream data socket");
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"send_tcp_rr: send_socket obtained...\n");
  }
  
  /* Modify the local socket size. The reason we alter the send buffer */
  /* size here rather than when the connection is made is to take care */
  /* of decreases in buffer size. Decreasing the window size after */
  /* connection establishment is a TCP no-no. Also, by setting the */
  /* buffer (window) size before the connection is established, we can */
  /* control the TCP MSS (segment size). The MSS is never more that 1/2 */
  /* the minimum receive buffer size at each half of the connection. */
  /* This is why we are altering the receive buffer size on the sending */
  /* size of a unidirectional transfer. If the user has not requested */
  /* that the socket buffers be altered, we will try to find-out what */
  /* their values are. If we cannot touch the socket buffer in any way, */
  /* we will set the values to -1 to indicate that.  */
  
#ifdef SO_SNDBUF
  if (lss_size > 0) {
    if(setsockopt(send_socket, SOL_SOCKET, SO_SNDBUF,
		  (char *)&lss_size, sizeof(int)) < 0) {
      perror("netperf: send_tcp_rr: socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_rr: socket send size altered from system default...\n");
      fprintf(where,"                          send: %d\n",lss_size);
    }
  }
  if (lsr_size > 0) {
    if(setsockopt(send_socket, SOL_SOCKET, SO_RCVBUF,
		  (char *)&lsr_size, sizeof(int)) < 0) {
      perror("netperf: send_tcp_rr: receive socket size option");
      
      exit(1);
    }
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_rr: socket recv size altered from system default...\n");
      fprintf(where,"                          recv: %d\n",lsr_size);
    }
  }
  
  
  /* Now, we will find-out what the size actually became, and report */
  /* that back to the user. If the call fails, we will just report a -1 */
  /* back to the initiator for the recv buffer size. */
  
  
  if (getsockopt(send_socket,
		 SOL_SOCKET,	
		 SO_SNDBUF,
		 (char *)&lss_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_tcp_rr: getsockopt");
    lss_size = -1;
  }
  if (getsockopt(send_socket,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&lsr_size,
		 &sock_opt_len) < 0) {
    perror("netperf: send_tcp_rr: getsockopt");
    lsr_size = -1;
  }
  
  if (debug) {
    fprintf(where,"netperf: send_tcp_rr: socket sizes determined...\n");
    fprintf(where,"         send: %d recv: %d\n",lss_size,lsr_size);
  }
  
#else SO_SNDBUF
  
  lss_size = -1;
  lsr_size = -1;
  
#endif SO_SNDBUF
  
  /* Now, we will see about setting the TCP_NO_DELAY flag on the local */
  /* socket. We will only do this for those systems that actually */
  /* support the option. If it fails, note the fact, but keep going. */
  
#ifdef TCP_NODELAY
  if (loc_nodelay) {
    if(setsockopt(send_socket,
		  getprotobyname("tcp")->p_proto,
		  TCP_NODELAY,
		  &one,
		  sizeof(one)) < 0) {
      perror("netperf: setsockopt: nodelay");
    }
    
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_rr: TCP_NODELAY has been requested...\n");
      fflush(where);
    }
  }
#else TCP_NODELAY
  
  loc_nodelay = 0;
  
#endif TCP_NODELAY
  
  /* we may wish to enable the copy avoidance features at this point. */
  /* It will either be the case that the option was not present at */
  /* compile time, or we may fail the socket call. */
  
#ifdef SO_RCV_COPYAVOID
  if (loc_rcvavoid) {
    if (setsockopt(send_socket,
		   SOL_SOCKET,
		   SO_RCV_COPYAVOID,
		   &loc_rcvavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable receive copy avoidance");
      loc_rcvavoid = 0;
    }
  }
#endif
  
#ifdef SO_SND_COPYAVOID
  if (loc_sndavoid) {
    if (setsockopt(send_socket,
		   SOL_SOCKET,
		   SO_SND_COPYAVOID,
		   &loc_sndavoid,
		   sizeof(int)) < 0) {
      printf("netperf: send_tcp_stream: Could not enable send copy avoidance");
      loc_sndavoid = 0;
    }
  }
#endif
  
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
  
  netperf_request->request_type	=	DO_TCP_ARR;
  tcp_arr_request->recv_buf_size	=	rsr_size;
  tcp_arr_request->send_buf_size	=	rss_size;
  tcp_arr_request->recv_alignment	=	remote_recv_align;
  tcp_arr_request->recv_offset	=	remote_recv_offset;
  tcp_arr_request->send_alignment	=	remote_send_align;
  tcp_arr_request->send_offset	=	remote_send_offset;
  tcp_arr_request->request_size	=	req_size;
  tcp_arr_request->response_size	=	rsp_size;
  tcp_arr_request->no_delay	=	rem_nodelay;
  tcp_arr_request->measure_cpu	=	remote_cpu_usage;
  tcp_arr_request->cpu_rate	=	remote_cpu_rate;
  tcp_arr_request->so_rcvavoid	=	rem_rcvavoid;
  tcp_arr_request->so_sndavoid	=	rem_sndavoid;
  if (test_time) {
    tcp_arr_request->test_length	=	test_time;
  }
  else {
    tcp_arr_request->test_length	=	test_trans * -1;
  }
  
  if (debug > 1) {
    fprintf(where,"netperf: send_tcp_async_rr: requesting TCP async rr test\n");
  }
  
  send_request();
  
  /* The response from the remote will contain all of the relevant 	*/
  /* socket parameters for this test type. We will put them back into 	*/
  /* the variables here so they can be displayed if desired.  The	*/
  /* remote will have calibrated CPU if necessary, and will have done	*/
  /* all the needed set-up we will have calibrated the cpu locally	*/
  /* before sending the request, and will grab the counter value right	*/
  /* after the connect returns. The remote will grab the counter right	*/
  /* after the accept call. This saves the hassle of extra messages	*/
  /* being sent for the TCP tests.					*/
  
  recv_response();
  
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote listen done.\n");
    rsr_size	=	tcp_arr_response->recv_buf_size;
    rss_size	=	tcp_arr_response->send_buf_size;
    rem_nodelay	=	tcp_arr_response->no_delay;
    remote_cpu_usage=	tcp_arr_response->measure_cpu;
    remote_cpu_rate = 	tcp_arr_response->cpu_rate;
    /* make sure that port numbers are in network order */
    server.sin_port	=	tcp_arr_response->data_port_number;
    server.sin_port =	htons(server.sin_port);
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /*Connect up to the remote port on the data socket  */
  if (connect(send_socket, 
	      (struct sockaddr *)&server,
	      sizeof(server)) <0){
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
#ifdef SUNOS4
    /* on some systems (SunOS 4.blah), system calls are restarted. we do */
    /* not want that for a request/response test */
    action.sa_handler = catcher;
    action.sa_flags = SA_INTERRUPT; 
    if (sigaction(SIGALRM, &action, NULL) < 0) {
      fprintf(where,"send_tcp_arr: error creating alarm signal.\n");
      fflush(where);
      exit(1);
    }
#else /* SUNOS4 */
    signal(SIGALRM, catcher);
#endif /* SUNOS4 */
    alarm(test_time);
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
  
  /* We use an "OR" to control test execution. When the test is */
  /* controlled by time, the byte count check will always return false. */
  /* When the test is controlled by byte count, the time test will */
  /* always return false. When the test is finished, the whole */
  /* expression will go false and we will stop sending data. I think I */
  /* just arbitrarily decrement trans_remaining for the timed test, but */
  /* will not do that just yet... One other question is whether or not */
  /* the send buffer and the receive buffer should be the same buffer. */
  
  while ((!times_up) || (trans_remaining > 0)) {
    /* send the request */
    if((len=send(send_socket,
		 send_message_ptr,
		 req_size,
		 0)) != req_size) {
      if (errno == EINTR) {
	/* we hit the end of a */
	/* timed test. */
	timed_out = 1;
	break;
      }
      perror("send_tcp_rr: data send error");
      exit(1);
    }
    
    /* receive the response */
    rsp_bytes_left = rsp_size;
    temp_message_ptr  = recv_message_ptr;
    while(rsp_bytes_left > 0) {
      if((rsp_bytes_recvd=recv(send_socket,
			       temp_message_ptr,
			       rsp_bytes_left,
			       0)) < 0) {
	if (errno == EINTR) {
	  /* We hit the end of a timed test. */
	  timed_out = 1;
	  break;
	}
	perror("send_tcp_rr: data recv error");
	exit(1);
      }
      rsp_bytes_left -= rsp_bytes_recvd;
      temp_message_ptr  += rsp_bytes_recvd;
    }	
    
    if (timed_out) {
      /* we may have been in a nested while loop - we need */
      /* another call to break. */
      break;
    }
    
    nummessages++;          
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug > 3) {
      fprintf(where,
	      "Transaction %d completed\n",
	      nummessages);
      fflush(where);
    }
  }
  
  /* At this point we used to call shutdown onthe data socket to be */
  /* sure all the data was delivered, but this was not germane in a */
  /* request/response test, and it was causing the tests to "hang" when */
  /* they were being controlled by time. So, I have replaced this */
  /* shutdown call with a call to close that can be found later in the */
  /* procedure. */
  
  /* this call will always give us the elapsed time for the test, and */
  /* will also store-away the necessaries for cpu utilization */
  
  cpu_stop(local_cpu_usage,&elapsed_time);	/* was cpu being measured? */
  /* how long did we really run? */
  
  /* Get the statistics from the remote end. The remote will have */
  /* calculated service demand and all those interesting things. If it */
  /* wasn't supposed to care, it will return obvious values. */
  
  recv_response();
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote results obtained\n");
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /* We now calculate what our thruput was for the test. In the future, */
  /* we may want to include a calculation of the thruput measured by */
  /* the remote, but it should be the case that for a TCP stream test, */
  /* that the two numbers should be *very* close... We calculate */
  /* bytes_sent regardless of the way the test length was controlled. */
  /* If it was time, we needed to, and if it was by bytes, the user may */
  /* have specified a number of bytes that wasn't a multiple of the */
  /* send_size, so we really didn't send what he asked for ;-) We use */
  /* Kbytes/s as the units of thruput for a TCP stream test, where K = */
  /* 1024. A future enhancement *might* be to choose from a couple of */
  /* unit selections. */ 
  
  bytes_xferd	= (req_size * nummessages) + (rsp_size * nummessages);
  thruput		= calc_thruput(bytes_xferd);
  
  if (local_cpu_usage || remote_cpu_usage) {
    /* We must now do a little math for service demand and cpu */
    /* utilization for the system(s) */
    /* Of course, some of the information might be bogus because */
    /* there was no idle counter in the kernel(s). We need to make */
    /* a note of this for the user's benefit...*/
    if (local_cpu_usage) {
      if (local_cpu_rate == 0.0) {
	fprintf(where,"WARNING WARNING WARNING  WARNING WARNING WARNING  WARNING!\n");
	fprintf(where,"Local CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      local_cpu_utilization = calc_cpu_util(0.0);
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      local_service_demand  = calc_service_demand((double) nummessages*1024,
						      0.0,
						      0.0);
    }
    else {
      local_cpu_utilization	= -1.0;
      local_service_demand	= -1.0;
    }
    
    if (remote_cpu_usage) {
      if (remote_cpu_rate == 0.0) {
	fprintf(where,"DANGER  DANGER  DANGER    DANGER  DANGER  DANGER    DANGER!\n");
	fprintf(where,"Remote CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      remote_cpu_utilization = tcp_arr_result->cpu_util;
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      remote_service_demand = calc_service_demand((double) nummessages*1024,
						  0.0,
						  remote_cpu_utilization);
    }
    else {
      remote_cpu_utilization = -1.0;
      remote_service_demand  = -1.0;
    }
    
    /* We are now ready to print all the information. If the user */
    /* has specified zero-level verbosity, we will just print the */
    /* local service demand, or the remote service demand. If the */
    /* user has requested verbosity level 1, he will get the basic */
    /* "streamperf" numbers. If the user has specified a verbosity */
    /* of greater than 1, we will display a veritable plethora of */
    /* background information from outside of this block as it it */
    /* not cpu_measurement specific...  */
    
    switch (verbosity) {
    case 0:
      if (local_cpu_usage) {
	fprintf(where,
		cpu_fmt_0,
		local_service_demand);
      }
      else {
	fprintf(where,
		cpu_fmt_0,
		remote_service_demand);
      }
      break;
    case 1:
      fprintf(where,
	      cpu_fmt_1_line_1,		/* the format string */
	      lss_size,		/* local sendbuf size */
	      lsr_size,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* guess */
	      elapsed_time,		/* how long was the test */
	      nummessages/elapsed_time,
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
	      nummessages/elapsed_time);
      break;
    case 1:
      fprintf(where,
	      tput_fmt_1_line_1,	/* the format string */
	      lss_size,
	      lsr_size,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* how large were the responses */
	      elapsed_time, 		/* how long did it take */
	      nummessages/elapsed_time);
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
  
  if (verbosity > 1) {
    /* The user wanted to know it all, so we will give it to him. */
    /* This information will include as much as we can find about */
    /* TCP statistics, the alignments of the sends and receives */
    /* and all that sort of rot... */
    
    fprintf(where,
	    ksink_fmt);
  }
  /* The test is over. Kill the data socket */
  
  if (close(send_socket) == -1) {
    perror("send_tcp_rr: cannot shutdown tcp stream socket");
  }
  
}


int 
  recv_tcp_arr()
{
  
  char message[MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET];
  struct	sockaddr_in        myaddr_in,
  peeraddr_in;
  int	s_listen,s_data;
  int 	addrlen;
  int	len;
  int	sock_opt_len = sizeof(int);
  int	one = 1;
  int	measure_cpu;
  char	*recv_message_ptr;
  char	*send_message_ptr;
  char	*temp_message_ptr;
  int	trans_received;
  int	trans_remaining;
  int	bytes_received;
  int	bytes_sent;
  int	request_bytes_recvd;
  int	request_bytes_remaining;
  int	timed_out = 0;
  float	elapsed_time;
  
  struct        sigaction      action;

  struct	tcp_arr_request_struct	*tcp_arr_request;
  struct	tcp_arr_response_struct	*tcp_arr_response;
  struct	tcp_arr_results_struct	*tcp_arr_results;
  
  tcp_arr_request = 
    (struct tcp_arr_request_struct *)netperf_request->test_specific_data;
  tcp_arr_response = 
    (struct tcp_arr_response_struct *)netperf_response->test_specific_data;
  tcp_arr_results = 
    (struct tcp_arr_results_struct *)netperf_response->test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_tcp_arr: entered...\n");
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
    fprintf(where,"recv_tcp_arr: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response->response_type = TCP_ARR_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_tcp_arr: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variables to be at the desired */
  /* alignments with the desired offsets. */
  
  if (debug) {
    fprintf(where,"recv_tcp_arr: requested recv alignment of %d offset %d\n",
	    tcp_arr_request->recv_alignment,
	    tcp_arr_request->recv_offset);
    fprintf(where,"recv_tcp_arr: requested send alignment of %d offset %d\n",
	    tcp_arr_request->send_alignment,
	    tcp_arr_request->send_offset);
    fflush(where);
  }
  recv_message_ptr = (char *)(( (long)message + 
			(long) tcp_arr_request->recv_alignment -1) & 
			~((long) tcp_arr_request->recv_alignment - 1));
  recv_message_ptr = recv_message_ptr + tcp_arr_request->recv_offset;
  
  send_message_ptr = (char *)(( (long)message + 
			(long) tcp_arr_request->send_alignment -1) & 
			~((long) tcp_arr_request->send_alignment - 1));
  send_message_ptr = send_message_ptr + tcp_arr_request->send_offset;
  
  if (debug) {
    fprintf(where,"recv_tcp_arr: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Let's clear-out our sockaddr for the sake of cleanlines. Then we */
  /* can put in OUR values !-) At some point, we may want to nail this */
  /* socket to a particular network-level address, but for now, */
  /* INADDR_ANY should be just fine. */
  
  bzero((char *)&myaddr_in,
	sizeof(myaddr_in));
  myaddr_in.sin_family      = AF_INET;
  myaddr_in.sin_addr.s_addr = INADDR_ANY;
  myaddr_in.sin_port        = 0;
  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_tcp_arr: grabbing a socket...\n");
    fflush(where);
  }
  
  s_listen = socket(AF_INET,
		    SOCK_STREAM,
		    0);
  
  if (s_listen < 0) {
    netperf_response->serv_errno = errno;
    send_response();
    
    exit(1);
  }
  
  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. There may be a desire to */
  /* nail this socket to a specific IP address in a multi-homed, */
  /* multi-connection situation, but for now, we'll ignore the issue */
  /* and concentrate on single connection testing. */
  
  if (bind(s_listen,
	   &myaddr_in,
	   sizeof(myaddr_in)) == -1) {
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  /* The initiator may have wished-us to modify the socket buffer */
  /* sizes. We should give it a shot. If he didn't ask us to change the */
  /* sizes, we should let him know what sizes were in use at this end. */
  /* If none of this code is compiled-in, then we will tell the */
  /* initiator that we were unable to play with the socket buffer by */
  /* setting the size in the response to -1. */
  
#ifdef SO_RCVBUF
  
  if (tcp_arr_request->recv_buf_size) {
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_RCVBUF,
		  (char *)&(tcp_arr_request->recv_buf_size),
		  sizeof(int)) < 0) {
      /* Do we really care if this */
      /* call fails? Perhaps we */
      /* should keep going and just */
      /* say what the size was? */
      netperf_response->serv_errno = errno;
      send_response();
      
      exit(1);
    }
  }
  
  if (tcp_arr_request->send_buf_size) {
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_SNDBUF,
		  (char *)&(tcp_arr_request->send_buf_size),
		  sizeof(int)) < 0) {
      /* Do we really care if this */
      /* call fails? Perhaps we */
      /* should keep going and just */
      /* say what the size was? */
      netperf_response->serv_errno = errno;
      send_response();
      
      exit(1);
    }
  }
  /* Now, we will find-out what the sizes actually became, and report */
  /* them back to the user. If the calls fail, we will just report a -1 */
  /* back to the initiator for the buffer size. */
  
  if (getsockopt(s_listen,
		 SOL_SOCKET,	
		 SO_RCVBUF,
		 (char *)&(tcp_arr_response->recv_buf_size),
		 &sock_opt_len) < 0) {
    tcp_arr_response->recv_buf_size = -1;
  }
  if (getsockopt(s_listen,
		 SOL_SOCKET,	
		 SO_SNDBUF,
		 (char *)&(tcp_arr_response->send_buf_size),
		 &sock_opt_len) < 0) {
    tcp_arr_response->send_buf_size = -1;
  }
  
  
#else /* the system won't let us play with the buffers */
  
  tcp_arr_response->recv_buf_size	= -1;
  tcp_arr_response->send_buf_size	= -1;
  
#endif SO_RCVBUF
  
  /* we need to do something with nodelay */
#ifdef TCP_NODELAY
  if (tcp_arr_request->no_delay) {
    if(setsockopt(s_listen,
		  getprotobyname("tcp")->p_proto,
		  TCP_NODELAY,
		  &one,
		  sizeof(one)) < 0) {
      fprintf(where,
	      "recv_tcp_arr: error on setting nodelay: %d\n",
	      errno);
      fflush(where);
      tcp_arr_response->no_delay = 0;
    }
    
    if (debug > 1) {
      fprintf(where,"netperf: send_tcp_arr: TCP_NODELAY has been requested...\n");
      fflush(where);
    }
  }
#else TCP_NODELAY
  
  tcp_arr_response->no_delay = 0;
  
#endif TCP_NODELAY
  
  /* we may have been requested to enable the copy avoidance features. */
  /* can we actually do this? */
  if (tcp_arr_request->so_rcvavoid) {
#ifdef SO_RCV_COPYAVOID
    /* be optimistic */
    tcp_arr_response->so_rcvavoid = 1;
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_RCV_COPYAVOID,
		  &tcp_arr_request->so_rcvavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      tcp_arr_response->so_rcvavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    tcp_arr_response->so_rcvavoid = 0;
#endif
  }
  
  if (tcp_arr_request->so_sndavoid) {
#ifdef SO_SND_COPYAVOID
    /* be optimistic */
    tcp_arr_response->so_sndavoid = 1;
    if(setsockopt(s_listen,
		  SOL_SOCKET,
		  SO_SND_COPYAVOID,
		  &tcp_arr_request->so_sndavoid,
		  sizeof(int)) < 0) {
      /* we asked for it, but could not get it, so set the */
      /* response to zero so the initiator knows... */
      tcp_arr_response->so_sndavoid = 0;
    }
#else
    /* it wasn't compiled in... */
    tcp_arr_response->so_sndavoid = 0;
#endif
  }
  
  /* Now, let's set-up the socket to listen for connections */
  if (listen(s_listen, 5) == -1) {
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  
  /* now get the port number assigned by the system  */
  addrlen = sizeof(myaddr_in);
  if (getsockname(s_listen, &myaddr_in, &addrlen) == -1){
    netperf_response->serv_errno = errno;
    close(s_listen);
    send_response();
    
    exit(1);
  }
  
  /* Now myaddr_in contains the port and the internet address this is */
  /* returned to the sender also implicitly telling the sender that the */
  /* socket buffer sizing has been done. */
  
  tcp_arr_response->data_port_number = (int) ntohs(myaddr_in.sin_port);
  netperf_response->serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */
  
  tcp_arr_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (tcp_arr_request->measure_cpu) {
    tcp_arr_response->measure_cpu = 1;
    tcp_arr_response->cpu_rate = calibrate_local_cpu(tcp_arr_request->cpu_rate);
  }
  
  send_response();
  
  addrlen = sizeof(peeraddr_in);
  
  if ((s_data=accept(s_listen,&peeraddr_in,&addrlen)) ==
      -1) {
    /* Let's just punt. The remote will be given some information */
    close(s_listen);
    
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"recv_tcp_arr: accept completes on the data connection.\n");
    fflush(where);
  }
  
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(tcp_arr_request->measure_cpu);
  
  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */
  
  if (tcp_arr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
#ifdef SUNOS4
    /* on some systems (SunOS 4.blah), system calls are restarted. we do */
    /* not want that */
    action.sa_handler = catcher;
    action.sa_flags = SA_INTERRUPT; 
    if (sigaction(SIGALRM, &action, NULL) < 0) {
      fprintf(where,"recv_tcp_arr: error creating alarm signal.\n");
      fflush(where);
      exit(1);
    }
#else /* SUNOS4 */
    signal(SIGALRM, catcher);
#endif /* SUNOS4 */
    alarm(tcp_arr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = tcp_arr_request->test_length * -1;
  }
  
  while ((!times_up) || (trans_remaining > 0)) {
    temp_message_ptr	= recv_message_ptr;
    request_bytes_remaining	= tcp_arr_request->request_size;
    
    /* receive the request from the other side */
    while(request_bytes_remaining > 0) {
      if((request_bytes_recvd=recv(s_data,
				   temp_message_ptr,
				   request_bytes_remaining,
				   0)) < 0) {
	if (errno == EINTR) {
	  /* the timer popped */
	  timed_out = 1;
	  break;
	}
	netperf_response->serv_errno = errno;
	send_response();
	exit(1);
      }
      else {
	request_bytes_remaining -= request_bytes_recvd;
	temp_message_ptr  += request_bytes_recvd;
      }
    }
    
    if (timed_out) {
      /* we hit the end of the test based on time - lets */
      /* bail out of here now... */
      fprintf(where,"yo5\n");
      fflush(where);						
      break;
    }
    
    /* Now, send the response to the remote */
    if((bytes_sent=send(s_data,
			send_message_ptr,
			tcp_arr_request->response_size,
			0)) == -1) {
      if (errno == EINTR) {
	/* the test timer has popped */
	timed_out = 1;
	fprintf(where,"yo6\n");
	fflush(where);						
	break;
      }
      netperf_response->serv_errno = 999;
      send_response();
      exit(1);
    }
    
    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug) {
      fprintf(where,
	      "recv_tcp_arr: Transaction %d complete\n",
	      trans_received);
      fflush(where);
    }
  }
  
  
  /* The loop now exits due to timeout or transaction count being */
  /* reached */
  
  cpu_stop(tcp_arr_request->measure_cpu,&elapsed_time);
  
  if (timed_out) {
    /* we ended the test by time, which was at least 2 seconds */
    /* longer than we wanted to run. so, we want to subtract */
    /* PAD_TIME from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_tcp_arr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }
  
  tcp_arr_results->bytes_received	= (trans_received * 
					   (tcp_arr_request->request_size + 
					    tcp_arr_request->response_size));
  tcp_arr_results->trans_received	= trans_received;
  tcp_arr_results->elapsed_time	= elapsed_time;
  if (tcp_arr_request->measure_cpu) {
    tcp_arr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_tcp_arr: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

void
print_sockets_usage()
{

  printf("%s",sockets_usage);
  exit(1);

}
void
scan_sockets_args(argc, argv)
     int	argc;
     char	*argv[];

{
#define SOCKETS_ARGS "Dhm:M:r:s:S:Vw:W:z"
  extern int	optind, opterrs;  /* index of first unused arg 	*/
  extern char	*optarg;	  /* pointer to option string	*/
  
  int		c;
  
  char	
    arg1[BUFSIZ],  /* argument holders		*/
    arg2[BUFSIZ];
  
  /* Go through all the command line arguments and break them */
  /* out. For those options that take two parms, specifying only */
  /* the first will set both to that value. Specifying only the */
  /* second will leave the first untouched. To change only the */
  /* first, use the form "first," (see the routine break_args.. */
  
  while ((c= getopt(argc, argv, SOCKETS_ARGS)) != EOF) {
    switch (c) {
    case '?':	
    case 'h':
      print_sockets_usage();
      exit(1);
    case 'D':
      /* set the TCP nodelay flag */
      loc_nodelay = 1;
      rem_nodelay = 1;
      break;
    case 's':
      /* set local socket sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	lss_size = atoi(arg1);
      if (arg2[0])
	lsr_size = atoi(arg2);
      break;
    case 'S':
      /* set remote socket sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	rss_size = atoi(arg1);
      if (arg2[0])
	rsr_size = atoi(arg2);
      break;
    case 'r':
      /* set the request/response sizes */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	req_size = atoi(arg1);
      if (arg2[0])	
	rsp_size = atoi(arg2);
      break;
    case 'm':
      /* set the send size */
      send_size = atoi(optarg);
      break;
    case 'M':
      /* set the recv size */
      recv_size = atoi(optarg);
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
    case 'z':
      /* The user wishes to disable checksumming */
      udp_cksum_off = 1;
      break;
    };
  }
}
