#ifdef lint
#define DO_FORE
#define DIRTY
#define INTERVALS
#endif /* lint */
#ifdef DO_FORE
char	nettest_fore[]="\
@(#)nettest_fore.c (c) Copyright 1994,1995 Hewlett-Packard Co. Version 2.0";
     
/****************************************************************/
/*								*/
/*	nettest_fore.c						*/
/*								*/
/*      the FORE sockets parsing routine...                     */
/*                                                              */
/*      scan_fore_args()                                        */
/*                                                              */
/*	the actual test routines...				*/
/*								*/
/*	send_fore_stream()	perform a fore stream test	*/
/*	recv_fore_stream()					*/
/*	send_fore_rr()		perform a fore request/response	*/
/*	recv_fore_rr()						*/
/*								*/
/****************************************************************/
     
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <malloc.h>

#include <fore_atm/fore_atm_user.h>
     
#include "netlib.h"
#include "netsh.h"
#include "nettest_fore.h"



 /* these variables are specific to the FORE sockets tests. declare */
 /* them static to make them global only to this file. some of these */
 /* might not actually have meaning in a FORE API test. */

static int	
  req_size,		/* request size                   	*/
  rsp_size,		/* response size			*/
  send_size,		/* how big are individual sends		*/
  recv_size;		/* how big are individual receives	*/

static Aal_type
  aal;     /* which Atm Adaptation Layer will we use for */
	   /* the test? */

static Atm_sap
  loc_atm_sap,          /* selected for us by the system */
  rem_atm_sap;          /* the remote's "loc_atm_sap" */

 /* I am making the simplifying assumption that we do not need */
 /* different qos parms for the client and server for benchmarking. */
static Atm_qos     
  desired_atm_qos;

static Atm_qos
  selected_atm_qos;   

static Atm_info
  atm_device_info;      /* that which the atm_open routine tells us */
			/* about the device we will be talking over */

static char
  loc_atm_device[32],
  rem_atm_device[32];

extern int
  atm_errno;

static int
  init_done = 0;

char fore_usage[] = "\n\
Usage: netperf [global options] -- [test options] \n\
\n\
FORE Sockets API Test Options:\n\
    -a aal            Use AAL aal as the Adaptation Layer\n\
    -b target,min     Set the mean burst target and/or minimum (kbit pkts)\n\
    -D device_path    The path to the ATM device\n\
    -h                Display this text\n\
    -m bytes          Set the send size (FORE_STREAM)\n\
    -M bytes          Set the recv size (FORE_STREAM)\n\
    -p target,min     Set the peak bandwidth target and/or minimum (kbit/s)\n\
    -P target,min     Set the mean bandwidth target and/or minimum (kbit/s)\n\
    -r bytes,bytes    Set request,response size (FORE_RR)\n\
\n\
For those options taking two parms, at least one must be specified;\n\
specifying one value without a comma will set both parms to that\n\
value, specifying a value with a leading comma will set just the second\n\
parm, a value with a trailing comma will set just the first. To set\n\
each parm to unique values, specify both and separate them with a\n\
comma.\n"; 
     

 /* this routine will set the default values for all the test specific */
 /* variables. it is declared static so that it is not exported */
 /* outside of the module */
static void
init_test_vars()
{

  if (init_done) {
    return;
  }
  else {

    req_size = 1;
    rsp_size = 1;
    send_size = 0;
    recv_size = 0;
    
    aal = aal_type_5;
    loc_atm_sap = 0;
    rem_atm_sap = 0;
    
    desired_atm_qos.peak_bandwidth.target = 0;
    desired_atm_qos.peak_bandwidth.minimum = 0;
    
    desired_atm_qos.mean_bandwidth.target = 0;
    desired_atm_qos.mean_bandwidth.minimum = 0;
    
    desired_atm_qos.mean_burst.target = 0;
    desired_atm_qos.mean_burst.minimum = 0;
    
    strcpy(loc_atm_device,"/dev/fa0");
    strcpy(rem_atm_device,"/dev/fa0");
    
    init_done = 1;

  }
}

 /* This routine will create a data (listen) socket with the apropriate */
 /* options set and return it to the caller. this replaces all the */
 /* duplicate code in each of the test routines and should help make */
 /* things a little easier to understand. since this routine can be */
 /* called by either the netperf or netserver programs, all output */
 /* should be directed towards "where." when being called from */
 /* netserver, care should be taken to insure that the globals */
 /* referenced by this routine are set. initially, this routine is */
 /* very simple because it would seem that most of the "sockops" are */
 /* actually set at connect time and not at socket creation time. */
int
create_fore_socket()
{

  int temp_socket;

  /*set up the data socket                        */
  temp_socket = atm_open(loc_atm_device, O_RDWR, &atm_device_info);
  if (temp_socket < 0){
    atm_error("netperf:create_fore_socket: atm_open");
    fprintf(where,
	    "netperf: create_fore_socket: could not open device %s: %d\n",
	    loc_atm_device,
	    errno);
    fflush(where);
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"create_fore_socket: socket %d obtained...\n",temp_socket);
    /* include salient points from the atm_device_info structure here */
    fflush(where);
  }
  
  return(temp_socket);

}



void
send_fore_stream(remote_host)
char	remote_host[];
{
  /*********************************************************************/
  /*								       */
  /*               	FORE Unidirectional Send Test                  */
  /*								       */
  /*********************************************************************/
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
  local_cpu_utilization, 
  remote_cpu_utilization;
  
  float	local_service_demand, remote_service_demand;
  double	local_thruput, remote_thruput;
  double	bytes_sent;
  double	bytes_recvd;
  
  
  int	len;
  int	*message_int_ptr;
  struct ring_elt *send_ring;
  int	failed_sends;
  int 	messages_sent;
  int 	data_socket;
  
  
#ifdef INTERVALS
  int	interval_count;
#endif /* INTERVALS */
#ifdef DIRTY
  int	i;
#endif /* DIRTY */
  
  Atm_endpoint server;
  
  struct        sigaction       action;

  struct	fore_stream_request_struct	*fore_stream_request;
  struct	fore_stream_response_struct	*fore_stream_response;
  struct	fore_stream_results_struct	*fore_stream_results;
  
  init_test_vars();

  fore_stream_request	= 
    (struct fore_stream_request_struct *)netperf_request->test_specific_data;
  fore_stream_response	= 
    (struct fore_stream_response_struct *)netperf_response->test_specific_data;
  fore_stream_results	= 
    (struct fore_stream_results_struct *)netperf_response->test_specific_data;
  
  bzero((char *)&server,
	sizeof(server));
  
  /* this call will retrieve the remote system's NSAP, which for */
  /* previously IP-only literate types is something like an IP address */
  if (atm_gethostbyname(remote_host,&server.nsap) < 0){
    fprintf(where,
	    "send_fore_stream: could not resolve the name%s\n",
	    remote_host);
    fflush(where);
    exit(1);
  }
  
  if ( print_headers ) {
    printf("FORE UNIDIRECTIONAL SEND TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      printf(cpu_title,format_units());
    else
      printf(tput_title,format_units());
  }	
  
  failed_sends	= 0;
  messages_sent	= 0;
  times_up	= 0;
  
  /*set up the data socket			*/
  data_socket = create_fore_socket();
  
  if (data_socket < 0){
    perror("netperf: send_fore_stream: data socket");
    exit(1);
  }
  
  /* now, we want to see if we need to set the send_size. if the user */
  /* did not specify a send_size on the command line, we will use the */
  /* mtu from the atm_device_info structure that was returned when the */
  /* socket was opened */
  if (send_size == 0) {
      send_size = atm_device_info.mtu;
  }
  
  /* now we should bind a source SAP - if the user specified a */
  /* non-zero SAP, we will try that. otherwise, it will be assigned by */
  /* the driver. I expect that it will be assigned by the driver just */
  /* about all the time - expecially until I actually allow the */
  /* setting of the SAP's from the command line :) raj 8/94 */
  /* I specify a qlen parm of "0" because this is the client */
  if (atm_bind(data_socket,
	       loc_atm_sap,
	       &loc_atm_sap,
	       0) < 0) {
    atm_error("netperf: send_fore_stream: atm_bind");
    exit(1);
  }

  /* set-up the data buffer with the requested alignment and offset, */
  /* most of the numbers here are just a hack to pick something nice */
  /* and big in an attempt to never try to send a buffer a second time */
  /* before it leaves the node...unless the user set the width */
  /* explicitly. */
  if (send_width == 0) send_width = 32;

  send_ring = allocate_buffer_ring(send_width,
				   send_size,
				   local_send_align,
				   local_send_offset);

  /* if the user supplied a cpu rate, this call will complete rather */
  /* quickly, otherwise, the cpu rate will be retured to us for */
  /* possible display. The Library will keep it's own copy of this data */
  /* for use elsewhere. We will only display it. (Does that make it */
  /* "opaque" to us?) */
  
  if (local_cpu_usage)
    local_cpu_rate = calibrate_local_cpu(local_cpu_rate);
  
  /* Tell the remote end to set up the data connection. The server */
  /* sends back the port number and alters the socket parameters there. */
  /* Of course this is a datagram service so no connection is actually */
  /* set up, the server just sets up the socket and binds it. */
  
  netperf_request->request_type = DO_FORE_STREAM;
  fore_stream_request->message_size	= send_size;
  fore_stream_request->recv_alignment	= remote_recv_align;
  fore_stream_request->recv_offset	= remote_recv_offset;
  fore_stream_request->measure_cpu	= remote_cpu_usage;
  fore_stream_request->cpu_rate		= remote_cpu_rate;
  fore_stream_request->test_length	= test_time;
  fore_stream_request->dev_name_len     = strlen(rem_atm_device);
  strcpy(fore_stream_request->atm_device,rem_atm_device);

#ifdef __alpha
  
  /* ok - even on a DEC box, strings are strings. I didn't really want */
  /* to ntohl the words of a string. since I don't want to teach the */
  /* send_ and recv_ _request and _response routines about the types, */
  /* I will put "anti-ntohl" calls here. I imagine that the "pure" */
  /* solution would be to use XDR, but I am still leary of being able */
  /* to find XDR libs on all platforms I want running netperf. raj */
  {
    int *charword;
    int *initword;
    int *lastword;
    
    initword = (int *) fore_stream_request->atm_device;
    lastword = initword + ((strlen(rem_atm_device) + 3) / 4);
    
    for (charword = initword;
	 charword < lastword;
	 charword++) {
      
      *charword = ntohl(*charword);
    }
  }
#endif /* __alpha */

  send_request();
  
  recv_response();
  
  /* at some point here I need to be able to distinguish between an */
  /* atm_errno and a unix errno. perhaps I'll make one negative or */
  /* something raj 8/94 in the meantime, I'll just live with bogus */
  /* error messages */
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"send_fore_stream: remote data connection done.\n");
  }
  else {
    errno = netperf_response->serv_errno;
    perror("send_fore_stream: error on remote");
    exit(1);
  }
  
  /* Place the port number returned by the remote into the sockaddr */
  /* structure so our sends can be sent to the correct place. Also get */
  /* some of the returned socket buffer information for user display. */
  
  server.asap = fore_stream_response->server_asap;
  remote_cpu_rate	= fore_stream_response->cpu_rate;
  
  /* We "connect" up to the remote. since this is a stream test, we */
  /* can specify that the connection is suppsed to be "simplex" */
  
  if (atm_connect(data_socket,
		  &server,
		  &desired_atm_qos,
		  (Atm_qos_sel *)(&selected_atm_qos),
		  aal,
		  simplex) <0){
    atm_error("send_fore_stream: data socket connect failed");
    exit(1);
  }
  
  /* set up the timer to call us after test_time	*/
  start_timer(test_time);
  
  /* Get the start count for the idle counter and the start time */
  
  cpu_start(local_cpu_usage);
  
#ifdef INTERVALS
  interval_count = interval_burst;
#endif
  
  /* Send datagrams like there was no tomorrow. at somepoint it might */
  /* be nice to set this up so that a quantity of bytes could be sent, */
  /* but we still need some sort of end of test trigger on the receive */
  /* side. that could be a select with a one second timeout, but then */
  /* if there is a test where none of the data arrives for awile and */
  /* then starts again, we would end the test too soon. something to */
  /* think about... */
  while (!times_up) {

#ifdef DIRTY
    /* we want to dirty some number of consecutive integers in the buffer */
    /* we are about to send. we may also want to bring some number of */
    /* them cleanly into the cache. The clean ones will follow any dirty */
    /* ones into the cache. */
    message_int_ptr = (int *)(send_ring->buffer_ptr);
    for (i = 0; i < loc_dirty_count; i++) {
      *message_int_ptr = 4;
      message_int_ptr++;
    }
    for (i = 0; i < loc_clean_count; i++) {
      loc_dirty_count = *message_int_ptr;
      message_int_ptr++;
    }
#endif /* DIRTY */

    if ((len=atm_send(data_socket,
		      send_ring->buffer_ptr,
		      send_size))  != send_size) {
      /* need to check what is returned when the atm_send called is */
      /* interrupted by a signal */
      if ((len >= 0) || (errno == EINTR))
	break;
      if (errno == ENOBUFS) {
	/* what is the error message when we are sending too fast? is */
	/* there one? will I be flow controlled based on the qos? */
	failed_sends++;
	continue;
      }
      perror("fore_send: data send error");
      exit(1);
    }
    messages_sent++;          
    
    /* now we want to move our pointer to the next position in the */
    /* data buffer... */

    send_ring = send_ring->next;
    
    
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
  
  /* Get the statistics from the remote end	*/
  recv_response();
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"send_fore_stream: remote results obtained\n");
  }
  else {
    errno = netperf_response->serv_errno;
    perror("send_fore_stream: error on remote");
    exit(1);
  }

  /* The test is over. */
  
  if (atm_close(data_socket) != 0) {
    /* we will not consider this a fatal error. just display a message */
    /* and move on */
    atm_error("netperf: cannot shutdown fore socket");
    perror("netperf: cannot shutdown fore socket");
  }
  

  bytes_sent	= send_size * messages_sent;
  local_thruput	= calc_thruput(bytes_sent);
  
  messages_recvd	= fore_stream_results->messages_recvd;
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
      
      remote_cpu_utilization	= fore_stream_results->cpu_util;
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
	      selected_atm_qos.mean_burst.target,		/* local sendbuf size */
	      send_size,		/* how large were the sends */
	      elapsed_time,		/* how long was the test */
	      messages_sent,
	      failed_sends,
	      local_thruput, 		/* what was the xfer rate */
	      local_cpu_utilization,	/* local cpu */
	      local_service_demand,	/* local service demand */
	      selected_atm_qos.mean_burst.target,
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
	      selected_atm_qos.mean_burst.target, 		/* local sendbuf size */
	      send_size,		/* how large were the sends */
	      elapsed_time, 		/* how long did it take */
	      messages_sent,
	      failed_sends,
	      local_thruput,
	      selected_atm_qos.mean_burst.target, 		/* remote recvbuf size */
	      elapsed_time,
	      messages_recvd,
	      remote_thruput
	      );
      break;
    }
  }
}


 /* this routine implements the receive side (netserver) of the */
 /* FORE_STREAM performance test. */

int
recv_fore_stream()
{
  struct ring_elt *recv_ring;

  Atm_endpoint myaddr, peeraddr;
  Aal_type my_aal;

  int connection_id;

  int	s_data;
  int	len;
  int	bytes_received = 0;
  float	elapsed_time;
  
  int	message_size;
  int	messages_recvd = 0;
  int	measure_cpu;
  
  struct        sigaction     action;

  struct	fore_stream_request_struct	*fore_stream_request;
  struct	fore_stream_response_struct	*fore_stream_response;
  struct	fore_stream_results_struct	*fore_stream_results;
  
  init_test_vars();

  fore_stream_request  = 
    (struct fore_stream_request_struct *)netperf_request->test_specific_data;
  fore_stream_response = 
    (struct fore_stream_response_struct *)netperf_response->test_specific_data;
  fore_stream_results  = 
    (struct fore_stream_results_struct *)netperf_response->test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_fore_stream: entered...\n");
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
    fprintf(where,"recv_fore_stream: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response->response_type = FORE_STREAM_RESPONSE;
  
  if (debug > 2) {
    fprintf(where,"recv_fore_stream: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variable to be at the desired */
  /* alignment with the desired offset. */
  
  if (debug > 1) {
    fprintf(where,"recv_fore_stream: requested alignment of %d\n",
	    fore_stream_request->recv_alignment);
    fflush(where);
  }

  if (recv_width == 0) recv_width = 1;

  recv_ring = allocate_buffer_ring(recv_width,
				   fore_stream_request->message_size,
				   fore_stream_request->recv_alignment,
				   fore_stream_request->recv_offset);

  if (debug > 1) {
    fprintf(where,"recv_fore_stream: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* create_fore_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */

  selected_atm_qos.mean_burst.target = fore_stream_request->recv_buf_size;

#ifdef __alpha
  
  /* ok - even on a DEC box, strings are strings. I din't really want */
  /* to ntohl the words of a string. since I don't want to teach the */
  /* send_ and recv_ _request and _response routines about the types, */
  /* I will put "anti-ntohl" calls here. I imagine that the "pure" */
  /* solution would be to use XDR, but I am still leary of being able */
  /* to find XDR libs on all platforms I want running netperf. raj */
  {
    int *charword;
    int *initword;
    int *lastword;
    
    initword = (int *) fore_stream_request->dlpi_device;
    lastword = initword + ((fore_stream_request->dev_name_len + 3) / 4);
    
    for (charword = initword;
	 charword < lastword;
	 charword++) {
      
      *charword = htonl(*charword);
    }
  }
#endif /* __alpha */

  strncpy(loc_atm_device,
	  fore_stream_request->atm_device,
	  fore_stream_request->dev_name_len);
    
  s_data = create_fore_socket();
  
  if (s_data < 0) {
    netperf_response->serv_errno = errno;
    send_response();
    exit(1);
  }
  
  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. There may be a desire to */
  /* nail this socket to a specific address in a multi-homed, */
  /* multi-connection situation, but for now, we'll ignore the issue */
  /* and concentrate on single connection testing. we specify the qlen */
  /* parm as 1 because this is the server side. raj 8/94 */
  
  if (atm_bind(s_data,
	       loc_atm_sap,
	       (Atm_sap *)(&fore_stream_response->server_asap),
	       1) == -1) {
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }
  
  fore_stream_response->test_length = fore_stream_request->test_length;
  
  netperf_response->serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a -1 to */
  /* the initiator. */
  
  fore_stream_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (fore_stream_request->measure_cpu) {
    /* We will pass the rate into the calibration routine. If the */
    /* user did not specify one, it will be 0.0, and we will do a */
    /* "real" calibration. Otherwise, all it will really do is */
    /* store it away... */
    fore_stream_response->measure_cpu = 1;
    fore_stream_response->cpu_rate = 
      calibrate_local_cpu(fore_stream_request->cpu_rate);
  }
  
  message_size	= fore_stream_request->message_size;
  test_time	= fore_stream_request->test_length;
  
  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */

  send_response();
  
  /* now we have to listen and accept on the connection. I have some */
  /* question about where this should be. Should this be after the */
  /* call to cpu_start? */
  if (atm_listen(s_data,
		 &connection_id,
		 &peeraddr,
		 &desired_atm_qos,
		 &my_aal) < 0) {
    fprintf(where,
	    "netperf: recv_fore_stream: atm_listen: atm_errno = %d\n",
	    atm_errno);
    fflush(where);
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }

  /* we'll just ape the QOS that came to us on the atm_listen call. */
  /* there is no real reason to examine it or anything since we don't */
  /* have values that we might prefer. raj 8/94 */
  if (atm_accept(s_data,
		 s_data,
		 connection_id,
		 &desired_atm_qos,
		 simplex) < 0) {
    fprintf(where,
	    "netperf: recv_fore_stream: atm_accept: atm_errno = %d\n",
	    atm_errno);
    fflush(where);
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(fore_stream_request->measure_cpu);
  
  /* The loop will exit when the timer pops, or if we happen to recv a */
  /* message of less than send_size bytes... */
  
  times_up = 0;
  start_timer(test_time + PAD_TIME);
  
  if (debug) {
    fprintf(where,"recv_fore_stream: about to enter inner sanctum.\n");
    fflush(where);
  }
  
  while (!times_up) {
    /* do I have to put this receive in a while loop as I would with */
    /* TCP, or will ATM with either AAL3/4 or AAL5 preserve message */
    /* boundaries? for now, assume that it preserves message */
    /* boundaries. raj 8/94 */
    if ((len = atm_recv(s_data, 
			recv_ring->buffer_ptr,
			message_size)) != message_size) {
      if ((len == -1) && (errno != EINTR)) {
	netperf_response->serv_errno = atm_errno;
	send_response();
	exit(1);
      }
      break;
    }
    messages_recvd++;
    recv_ring = recv_ring->next;
  }
  
  if (debug) {
    fprintf(where,"recv_fore_stream: got %d messages.\n",messages_recvd);
    fflush(where);
  }
  
  
  /* The loop now exits due timer or < send_size bytes received. */
  
  cpu_stop(fore_stream_request->measure_cpu,&elapsed_time);
  
  if (times_up) {
    /* we ended on a timer, subtract the PAD_TIME */
    elapsed_time -= (float)PAD_TIME;
  }
  else {
    alarm(0);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_fore_stream: test ended in %f seconds.\n",elapsed_time);
    fflush(where);
  }
  
  
  /* We will count the "off" message that got us out of the loop */
  bytes_received = (messages_recvd * message_size) + len;
  
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_fore_stream: got %d bytes\n",
	    bytes_received);
    fflush(where);
  }
  
  netperf_response->response_type	= FORE_STREAM_RESULTS;
  fore_stream_results->bytes_received	= bytes_received;
  fore_stream_results->messages_recvd	= messages_recvd;
  fore_stream_results->elapsed_time	= elapsed_time;
  if (fore_stream_request->measure_cpu) {
    fore_stream_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  else {
    fore_stream_results->cpu_util	= -1.0;
  }
  
  if (debug > 1) {
    fprintf(where,
	    "recv_fore_stream: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

int send_fore_rr(remote_host)
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
  
  
  float			elapsed_time;
  
  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  int	len;
  int	nummessages;
  int	send_socket;
  int	trans_remaining;
  int	bytes_xferd;
  
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
  
  Atm_endpoint server, peeraddr, myaddr;
  
  struct        sigaction       action;

  struct	fore_rr_request_struct	*fore_rr_request;
  struct	fore_rr_response_struct	*fore_rr_response;
  struct	fore_rr_results_struct	*fore_rr_result;
  
  init_test_vars();

  fore_rr_request  =
    (struct fore_rr_request_struct *)netperf_request->test_specific_data;
  fore_rr_response =
    (struct fore_rr_response_struct *)netperf_response->test_specific_data;
  fore_rr_result	 =
    (struct fore_rr_results_struct *)netperf_response->test_specific_data;
  
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
  
  /* this call will retrieve the remote system's NSAP, which for */
  /* previously IP-only literate types is something like an IP address */
  if (atm_gethostbyname(remote_host,&server.nsap) < 0){
    fprintf(where,
	    "send_fore_stream: could not resolve the name%s\n",
	    remote_host);
    fflush(where);
    exit(1);
  }

  if ( print_headers ) {
    fprintf(where,"FORE REQUEST/RESPONSE TEST\n");
    if (local_cpu_usage || remote_cpu_usage)
      fprintf(where,cpu_title,format_units());
    else
      fprintf(where,tput_title,format_units());
  }
  
  /* initialize a few counters */
  
  nummessages	=	0;
  bytes_xferd	=	0;
  times_up 	= 	0;
  
  /* set-up the data buffers with the requested alignment and offset */

  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  send_ring = allocate_buffer_ring(send_width,
				   req_size,
				   local_send_align,
				   local_send_offset);

  recv_ring = allocate_buffer_ring(recv_width,
				   rsp_size,
				   local_recv_align,
				   local_recv_offset);

  /*set up the data socket                        */
  send_socket = create_fore_socket();
  
  if (send_socket < 0){
    perror("netperf: send_fore_rr: fore rr data socket");
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"send_fore_rr: send_socket obtained...\n");
  }
  
  
  /* now we should bind a source SAP - if the user specified a */
  /* non-zero SAP, we will try that. otherwise, it will be assigned by */
  /* the driver. I expect that it will be assigned by the driver just */
  /* about all the time - expecially until I actually allow the */
  /* setting of the SAP's from the command line :) raj 8/94 */
  /* I specify a qlen parm of "0" because this is the client */
  if (atm_bind(send_socket,
	       loc_atm_sap,
	       &loc_atm_sap,
	       0) < 0) {
    atm_error("netperf: send_fore_stream: atm_bind");
    exit(1);
  }

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
  
  netperf_request->request_type	  =	DO_FORE_RR;
  fore_rr_request->recv_buf_size  =	desired_atm_qos.mean_burst.target;
  fore_rr_request->send_buf_size  =	desired_atm_qos.mean_burst.target;
  fore_rr_request->recv_alignment =	remote_recv_align;
  fore_rr_request->recv_offset	  =	remote_recv_offset;
  fore_rr_request->send_alignment =	remote_send_align;
  fore_rr_request->send_offset	  =	remote_send_offset;
  fore_rr_request->request_size	  =	req_size;
  fore_rr_request->response_size  =	rsp_size;
  fore_rr_request->measure_cpu	  =	remote_cpu_usage;
  fore_rr_request->cpu_rate	  =	remote_cpu_rate;
  fore_rr_request->aal            =       aal;
  if (test_time) {
    fore_rr_request->test_length  =	test_time;
  }
  else {
    fore_rr_request->test_length  =	test_trans * -1;
  }
  fore_rr_request->dev_name_len   =       strlen(rem_atm_device);
  strcpy(fore_rr_request->atm_device,rem_atm_device);
  if (debug > 1) {
    fprintf(where,
	    "requesting FORE request/response test\n");
    fflush(where);
  }
  
#ifdef __alpha
  
  /* ok - even on a DEC box, strings are strings. I didn't really want */
  /* to ntohl the words of a string. since I don't want to teach the */
  /* send_ and recv_ _request and _response routines about the types, */
  /* I will put "anti-ntohl" calls here. I imagine that the "pure" */
  /* solution would be to use XDR, but I am still leary of being able */
  /* to find XDR libs on all platforms I want running netperf. raj */
  {
    int *charword;
    int *initword;
    int *lastword;
    
    initword = (int *) fore_rr_request->atm_device;
    lastword = initword + ((strlen(rem_atm_device) + 3) / 4);
    
    for (charword = initword;
	 charword < lastword;
	 charword++) {
      
      *charword = ntohl(*charword);
    }
  }
#endif /* __alpha */

  send_request();
  
  /* The response from the remote will contain all of the relevant 	*/
  /* socket parameters for this test type. We will put them back into 	*/
  /* the variables here so they can be displayed if desired.  The	*/
  /* remote will have calibrated CPU if necessary, and will have done	*/
  /* all the needed set-up we will have calibrated the cpu locally	*/
  /* before sending the request, and will grab the counter value right	*/
  /* after the connect returns. The remote will grab the counter right	*/
  /* after the accept call. This saves the hassle of extra messages	*/
  /* being sent for the FORE tests.					*/
  
  recv_response();
  
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote listen done.\n");
    desired_atm_qos.mean_burst.target = fore_rr_response->recv_buf_size;
    desired_atm_qos.mean_burst.target = fore_rr_response->send_buf_size;
    remote_cpu_usage= fore_rr_response->measure_cpu;
    remote_cpu_rate = fore_rr_response->cpu_rate;
    server.asap     = fore_rr_response->server_asap;
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /* Connect up to the remote port on the data socket. This will set */
  /* the default destination address on this socket.  */
  
  if (debug) {
    int switchid;
    int portid;
    GET_SWITCH(switchid,server.nsap);
    GET_PORT(portid,server.nsap);
    fprintf(where,"send_fore_rr: about to connect to asap %x\n",
	    server.asap);
    fprintf(where,"              on socket %d\n",send_socket);
    fprintf(where,"              with aal %d\n",aal);
    fprintf(where,"              on switch %d\n",switchid);
    fprintf(where,"              at port %d\n",portid);
    fflush(where);
  }


  if (atm_connect(send_socket,
		  &server,
		  &desired_atm_qos,
		  (Atm_qos_sel *)(&selected_atm_qos),
		  aal,
		  duplex) <0){
    atm_error("send_fore_rr: data socket connect failed");
    exit(1);
  }
  
  /* Data Socket set-up is finished. If there were problems, either the */
  /* connect would have failed, or the previous response would have */
  /* indicated a problem. I failed to see the value of the extra */
  /* message after the accept on the remote. If it failed, we'll see it */
  /* here. If it didn't, we might as well start pumping data. */
  
#ifdef FORE_KLUDGE
  /* there is some sort of race condition with the FORE_RR test on HP */
  /* platforms (99 MHz PA-7100's anyhow) where it would appear that we */
  /* can receive a connection reply and get data to the remote before */
  /* it gets completely set-up. so, to make sure that the remote is */
  /* completely set-up, we will sleep here for one second. when I can */
  /* find-out exactly what is going wrong and get it fixed (perhaps */
  /* with a patch to the Fore code, this will be removed. raj 8/94 */
  sleep(1);
#endif /* FORE_KLUDGE */

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
    if((len=atm_send(send_socket,
		     send_ring->buffer_ptr,
		     req_size)) != req_size) {
      if (errno == EINTR) {
	/* We likely hit */
	/* test-end time. */
	break;
      }
      atm_error("send_fore_rr: data send error");
      exit(1);
    }
    send_ring = send_ring->next;

    /* receive the response. with FORE we will get it all, or nothing */
    
    if((rsp_bytes_recvd=atm_recv(send_socket,
				 recv_ring->buffer_ptr,
				 rsp_size)) != rsp_size) {
      if (errno == EINTR) {
	/* Again, we have likely hit test-end time */
	break;
      }
      atm_error("send_fore_rr: data recv error");
      exit(1);
    }
    recv_ring = recv_ring->next;

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
    atm_errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    atm_error("        if it was atm");
    fprintf(stderr,"        the errno was: %d atm_errno %d\n",
	    errno,
	    atm_errno);
    fflush(where);
    exit(1);
  }

  /* The test is over. */
  
  if (atm_close(send_socket) != 0) {
    /* we will not consider this a fatal error. just display a message */
    /* and move on */
    atm_error("netperf: cannot shutdown fore socket");
    perror("netperf: cannot shutdown fore socket");
  }
  

  /* We now calculate what our thruput was for the test. In the future, */
  /* we may want to include a calculation of the thruput measured by */
  /* the remote, but it should be the case that for a FORE stream test, */
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
      remote_cpu_utilization = fore_rr_result->cpu_util;
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
	      selected_atm_qos.mean_burst.target,		/* local sendbuf size */
	      selected_atm_qos.mean_burst.target,
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
	      selected_atm_qos.mean_burst.target,
	      selected_atm_qos.mean_burst.target);
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
	      selected_atm_qos.mean_burst.target,
	      selected_atm_qos.mean_burst.target,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* how large were the responses */
	      elapsed_time, 		/* how long did it take */
	      nummessages/elapsed_time);
      fprintf(where,
	      tput_fmt_1_line_2,
	      selected_atm_qos.mean_burst.target, 		/* remote recvbuf size */
	      selected_atm_qos.mean_burst.target);
      
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
    /* FORE statistics, the alignments of the sends and receives */
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

 /* this routine implements the receive side (netserver) of a FORE_RR */
 /* test. */
int 
recv_fore_rr()
{
  
  struct ring_elt *recv_ring;
  struct ring_elt *send_ring;

  Atm_endpoint myaddr,  peeraddr;
  Aal_type my_aal;
  int connection_id;
  int	s_data;
  int 	addrlen;
  int	measure_cpu;
  int	trans_received;
  int	trans_remaining;
  float	elapsed_time;
  
  struct        sigaction       action;

  struct	fore_rr_request_struct	*fore_rr_request;
  struct	fore_rr_response_struct	*fore_rr_response;
  struct	fore_rr_results_struct	*fore_rr_results;
  
  init_test_vars();

  fore_rr_request  = 
    (struct fore_rr_request_struct *)netperf_request->test_specific_data;
  fore_rr_response = 
    (struct fore_rr_response_struct *)netperf_response->test_specific_data;
  fore_rr_results  = 
    (struct fore_rr_results_struct *)netperf_response->test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_fore_rr: entered...\n");
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
    fprintf(where,"recv_fore_rr: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response->response_type = FORE_RR_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_fore_rr: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variables to be at the desired */
  /* alignments with the desired offsets. */
  
  if (debug) {
    fprintf(where,"recv_fore_rr: requested recv alignment of %d offset %d\n",
	    fore_rr_request->recv_alignment,
	    fore_rr_request->recv_offset);
    fprintf(where,"recv_fore_rr: requested send alignment of %d offset %d\n",
	    fore_rr_request->send_alignment,
	    fore_rr_request->send_offset);
    fflush(where);
  }

  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  recv_ring = allocate_buffer_ring(recv_width,
				   fore_rr_request->request_size,
				   fore_rr_request->recv_alignment,
				   fore_rr_request->recv_offset);

  send_ring = allocate_buffer_ring(send_width,
				   fore_rr_request->response_size,
				   fore_rr_request->send_alignment,
				   fore_rr_request->send_offset);

  if (debug) {
    fprintf(where,"recv_fore_rr: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Let's clear-out our endpoints for the sake of cleanlines. Then we */
  /* can put in OUR values !-) At some point, we may want to nail this */
  /* socket to a particular network-level address. raj 8/94 */
  
  bzero((char *)&myaddr,
	sizeof(myaddr));
  bzero((char *)&peeraddr,
	sizeof(peeraddr));
  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_fore_rr: grabbing a socket...\n");
    fflush(where);
  }

  /* create_fore_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */

#ifdef __alpha
  
  /* ok - even on a DEC box, strings are strings. I din't really want */
  /* to ntohl the words of a string. since I don't want to teach the */
  /* send_ and recv_ _request and _response routines about the types, */
  /* I will put "anti-ntohl" calls here. I imagine that the "pure" */
  /* solution would be to use XDR, but I am still leary of being able */
  /* to find XDR libs on all platforms I want running netperf. raj */
  {
    int *charword;
    int *initword;
    int *lastword;
    
    initword = (int *) fore_rr_request->atm_device;
    lastword = initword + ((fore_rr_request->dev_name_len + 3) / 4);
    
    for (charword = initword;
	 charword < lastword;
	 charword++) {
      
      *charword = htonl(*charword);
    }
  }
#endif /* __alpha */

  strncpy(loc_atm_device,
	  fore_rr_request->atm_device,
	  fore_rr_request->dev_name_len);
  s_data = create_fore_socket();
  
  if (s_data < 0) {
    netperf_response->serv_errno = errno;
    send_response();
    
    exit(1);
  }
  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. There may be a desire to */
  /* nail this socket to a specific address in a multi-homed, */
  /* multi-connection situation, but for now, we'll ignore the issue */
  /* and concentrate on single connection testing. we specify the qlen */
  /* parm as 1 because this is the server side. raj 8/94 */
  
  if (debug) {
    fprintf(where,"recv_fore_rr: about to bind\n");
    fflush(where);
  }

  if (atm_bind(s_data,
	       loc_atm_sap,

	       (Atm_sap *)(&fore_rr_response->server_asap),
	       2) == -1) {
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }

  if (debug) {
    fprintf(where,"recv_fore_rr: bind complete asap is %x\n",
	    fore_rr_response->server_asap);
    fflush(where);
  }

  
  netperf_response->serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */
  
  fore_rr_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (fore_rr_request->measure_cpu) {
    fore_rr_response->measure_cpu = 1;
    fore_rr_response->cpu_rate = 
      calibrate_local_cpu(fore_rr_request->cpu_rate);
  }
   
  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  fore_rr_response->send_buf_size = selected_atm_qos.mean_burst.target;
  fore_rr_response->recv_buf_size = selected_atm_qos.mean_burst.target;
 
  if (debug) {
    fprintf(where,"recv_fore_rr: about to respond\n");
    fflush(where);
  }

  send_response();
  
  /* now we have to listen and accept on the connection. I have some */
  /* question about where this should be. Should this be after the */
  /* call to cpu_start? */

  connection_id = 0;

  if (debug) {
    int switchid;
    int portid;
    GET_SWITCH(switchid,peeraddr.nsap);
    GET_PORT(portid,peeraddr.nsap);
    fprintf(where,"recv_fore_rr: about to listen\n");
    fprintf(where,"              s_data is %d\n",s_data);
    fprintf(where,"              connid is %d\n",connection_id);
    fprintf(where,"              switch is %d\n",switchid);
    fprintf(where,"              portid is %d\n",portid);
    fprintf(where,"              aal    is %d\n",my_aal);
    fflush(where);
  }


  if (atm_listen(s_data,
		 &connection_id,
		 &peeraddr,
		 &desired_atm_qos,
		 &my_aal) < 0) {
    fprintf(where,
	    "netperf: recv_fore_rr: atm_listen: atm_errno = %d\n",
	    atm_errno);
    fflush(where);
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }

  if (debug) {
    int switchid;
    int portid;
    GET_SWITCH(switchid,peeraddr.nsap);
    GET_PORT(portid,peeraddr.nsap);
    fprintf(where,"recv_fore_rr: about to accept\n");
    fprintf(where,"              s_data is %d\n",s_data);
    fprintf(where,"              asap is %d\n",peeraddr.asap);
    fprintf(where,"              connid is %d\n",connection_id);
    fprintf(where,"              switch is %d\n",switchid);
    fprintf(where,"              portid is %d\n",portid);
    fprintf(where,"              aal    is %d\n",my_aal);
    fflush(where);
  }


  if (atm_accept(s_data,
		 s_data,
		 connection_id,
		 &desired_atm_qos,
		 duplex) < 0) {
    fprintf(where,
	    "netperf: recv_fore_rr: atm_accept: atm_errno = %d\n",
	    atm_errno);
    fflush(where);
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }

  trans_received = 0;
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(fore_rr_request->measure_cpu);
  
  if (fore_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
    start_timer(fore_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = fore_rr_request->test_length * -1;
  }
  
  addrlen = sizeof(peeraddr);
  bzero((char *)&peeraddr, addrlen);
  
  while ((!times_up) || (trans_remaining > 0)) {
    
    /* receive the request from the other side. the question remains */
    /* as to whether or not the FORE API provides a stream or message */
    /* paradigm. we will assume a message paradigm for the moment */
    /* raj 8/94 */
    if (atm_recv(s_data,
		 recv_ring->buffer_ptr,
		 fore_rr_request->request_size) != 
	fore_rr_request->request_size) {
      if (errno == EINTR) {
	/* we must have hit the end of test time. */
	break;
      }
      if (debug) {
	fprintf(where,
		"netperf: recv_fore_rr: atm_send: errno %d atm_errno %d\n",
		errno,
		atm_errno);
	fflush(where);
      }
      netperf_response->serv_errno = atm_errno;
      send_response();
      exit(1);
    }
    recv_ring = recv_ring->next;

    /* Now, send the response to the remote */
    if (atm_send(s_data,
		 send_ring->buffer_ptr,
		 fore_rr_request->response_size) != 
	fore_rr_request->response_size) {
      if (errno == EINTR) {
	/* we have hit end of test time. */
	break;
      }
      if (debug) {
	fprintf(where,
		"netperf: recv_fore_rr: atm_send: errno %d atm_errno %d\n",
		errno,
		atm_errno);
	fflush(where);
      }
      netperf_response->serv_errno = atm_errno;
      send_response();
      exit(1);
    }
    send_ring = send_ring->next;
    
    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug) {
      fprintf(where,
	      "recv_fore_rr: Transaction %d complete.\n",
	      trans_received);
      fflush(where);
    }
    
  }
  
  
  /* The loop now exits due to timeout or transaction count being */
  /* reached */
  
  cpu_stop(fore_rr_request->measure_cpu,&elapsed_time);
  
  if (times_up) {
    /* we ended the test by time, which was at least 2 seconds */
    /* longer than we wanted to run. so, we want to subtract */
    /* PAD_TIME from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_fore_rr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }
  
  fore_rr_results->bytes_received	= (trans_received * 
					   (fore_rr_request->request_size + 
					    fore_rr_request->response_size));
  fore_rr_results->trans_received	= trans_received;
  fore_rr_results->elapsed_time	= elapsed_time;
  if (fore_rr_request->measure_cpu) {
    fore_rr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_fore_rr: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

int send_fore_crr(remote_host)
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
  
  
  float			elapsed_time;
  
  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  int	len;
  int	nummessages;
  int	send_socket;
  int	trans_remaining;
  int	bytes_xferd;
  
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
  
  Atm_endpoint server, peeraddr, myaddr;
  
  struct        sigaction       action;

  struct	fore_rr_request_struct	*fore_rr_request;
  struct	fore_rr_response_struct	*fore_rr_response;
  struct	fore_rr_results_struct	*fore_rr_result;
  
  init_test_vars();

  fore_rr_request  =
    (struct fore_rr_request_struct *)netperf_request->test_specific_data;
  fore_rr_response =
    (struct fore_rr_response_struct *)netperf_response->test_specific_data;
  fore_rr_result	 =
    (struct fore_rr_results_struct *)netperf_response->test_specific_data;
  
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
  
  /* this call will retrieve the remote system's NSAP, which for */
  /* previously IP-only literate types is something like an IP address */
  if (atm_gethostbyname(remote_host,&server.nsap) < 0){
    fprintf(where,
	    "send_fore_stream: could not resolve the name%s\n",
	    remote_host);
    fflush(where);
    exit(1);
  }

  if ( print_headers ) {
    fprintf(where,"FORE Connection/Request/Response Test\n");
    if (local_cpu_usage || remote_cpu_usage)
      fprintf(where,cpu_title,format_units());
    else
      fprintf(where,tput_title,format_units());
  }
  
  /* initialize a few counters */
  
  nummessages	=	0;
  bytes_xferd	=	0;
  times_up 	= 	0;
  
  /* set-up the data buffers with the requested alignment and offset */

  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  send_ring = allocate_buffer_ring(send_width,
				   req_size,
				   local_send_align,
				   local_send_offset);

  recv_ring = allocate_buffer_ring(recv_width,
				   rsp_size,
				   local_recv_align,
				   local_recv_offset);

  /*set up the data socket                        */
  send_socket = create_fore_socket();
  
  if (send_socket < 0){
    perror("netperf: send_fore_crr: fore rr data socket");
    exit(1);
  }
  
  if (debug) {
    fprintf(where,"send_fore_crr: send_socket obtained...\n");
  }
  
  
  /* now we should bind a source SAP - if the user specified a */
  /* non-zero SAP, we will try that. otherwise, it will be assigned by */
  /* the driver. I expect that it will be assigned by the driver just */
  /* about all the time - expecially until I actually allow the */
  /* setting of the SAP's from the command line :) raj 8/94 */
  /* I specify a qlen parm of "0" because this is the client */
  if (atm_bind(send_socket,
	       loc_atm_sap,
	       &loc_atm_sap,
	       0) < 0) {
    atm_error("netperf: send_fore_stream: atm_bind");
    exit(1);
  }

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
  
  netperf_request->request_type	  =	DO_FORE_RR;
  fore_rr_request->recv_buf_size  =	desired_atm_qos.mean_burst.target;
  fore_rr_request->send_buf_size  =	desired_atm_qos.mean_burst.target;
  fore_rr_request->recv_alignment =	remote_recv_align;
  fore_rr_request->recv_offset	  =	remote_recv_offset;
  fore_rr_request->send_alignment =	remote_send_align;
  fore_rr_request->send_offset	  =	remote_send_offset;
  fore_rr_request->request_size	  =	req_size;
  fore_rr_request->response_size  =	rsp_size;
  fore_rr_request->measure_cpu	  =	remote_cpu_usage;
  fore_rr_request->cpu_rate	  =	remote_cpu_rate;
  fore_rr_request->aal            =       aal;
  if (test_time) {
    fore_rr_request->test_length  =	test_time;
  }
  else {
    fore_rr_request->test_length  =	test_trans * -1;
  }
  fore_rr_request->dev_name_len   =       strlen(rem_atm_device);
  strcpy(fore_rr_request->atm_device,rem_atm_device);
  if (debug > 1) {
    fprintf(where,
	    "requesting FORE request/response test\n");
    fflush(where);
  }
  
#ifdef __alpha
  
  /* ok - even on a DEC box, strings are strings. I didn't really want */
  /* to ntohl the words of a string. since I don't want to teach the */
  /* send_ and recv_ _request and _response routines about the types, */
  /* I will put "anti-ntohl" calls here. I imagine that the "pure" */
  /* solution would be to use XDR, but I am still leary of being able */
  /* to find XDR libs on all platforms I want running netperf. raj */
  {
    int *charword;
    int *initword;
    int *lastword;
    
    initword = (int *) fore_rr_request->atm_device;
    lastword = initword + ((strlen(rem_atm_device) + 3) / 4);
    
    for (charword = initword;
	 charword < lastword;
	 charword++) {
      
      *charword = ntohl(*charword);
    }
  }
#endif /* __alpha */

  send_request();
  
  /* The response from the remote will contain all of the relevant 	*/
  /* socket parameters for this test type. We will put them back into 	*/
  /* the variables here so they can be displayed if desired.  The	*/
  /* remote will have calibrated CPU if necessary, and will have done	*/
  /* all the needed set-up we will have calibrated the cpu locally	*/
  /* before sending the request, and will grab the counter value right	*/
  /* after the connect returns. The remote will grab the counter right	*/
  /* after the accept call. This saves the hassle of extra messages	*/
  /* being sent for the FORE tests.					*/
  
  recv_response();
  
  if (!netperf_response->serv_errno) {
    if (debug)
      fprintf(where,"remote listen done.\n");
    desired_atm_qos.mean_burst.target = fore_rr_response->recv_buf_size;
    desired_atm_qos.mean_burst.target = fore_rr_response->send_buf_size;
    remote_cpu_usage= fore_rr_response->measure_cpu;
    remote_cpu_rate = fore_rr_response->cpu_rate;
    server.asap     = fore_rr_response->server_asap;
  }
  else {
    errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    
    exit(1);
  }
  
  /* Connect up to the remote port on the data socket. This will set */
  /* the default destination address on this socket.  */
  
  if (debug) {
    int switchid;
    int portid;
    GET_SWITCH(switchid,server.nsap);
    GET_PORT(portid,server.nsap);
    fprintf(where,"send_fore_crr: about to connect to asap %x\n",
	    server.asap);
    fprintf(where,"              on socket %d\n",send_socket);
    fprintf(where,"              with aal %d\n",aal);
    fprintf(where,"              on switch %d\n",switchid);
    fprintf(where,"              at port %d\n",portid);
    fflush(where);
  }


  if (atm_connect(send_socket,
		  &server,
		  &desired_atm_qos,
		  (Atm_qos_sel *)(&selected_atm_qos),
		  aal,
		  duplex) <0){
    atm_error("send_fore_crr: data socket connect failed");
    exit(1);
  }
  
  /* Data Socket set-up is finished. If there were problems, either the */
  /* connect would have failed, or the previous response would have */
  /* indicated a problem. I failed to see the value of the extra */
  /* message after the accept on the remote. If it failed, we'll see it */
  /* here. If it didn't, we might as well start pumping data. */
  
#ifdef FORE_KLUDGE
  /* there is some sort of race condition with the FORE_RR test on HP */
  /* platforms (99 MHz PA-7100's anyhow) where it would appear that we */
  /* can receive a connection reply and get data to the remote before */
  /* it gets completely set-up. so, to make sure that the remote is */
  /* completely set-up, we will sleep here for one second. when I can */
  /* find-out exactly what is going wrong and get it fixed (perhaps */
  /* with a patch to the Fore code, this will be removed. raj 8/94 */
  sleep(1);
#endif /* FORE_KLUDGE */

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
    if((len=atm_send(send_socket,
		     send_ring->buffer_ptr,
		     req_size)) != req_size) {
      if (errno == EINTR) {
	/* We likely hit */
	/* test-end time. */
	break;
      }
      atm_error("send_fore_crr: data send error");
      exit(1);
    }
    send_ring = send_ring->next;

    /* receive the response. with FORE we will get it all, or nothing */
    
    if((rsp_bytes_recvd=atm_recv(send_socket,
				 recv_ring->buffer_ptr,
				 rsp_size)) != rsp_size) {
      if (errno == EINTR) {
	/* Again, we have likely hit test-end time */
	break;
      }
      atm_error("send_fore_crr: data recv error");
      exit(1);
    }
    recv_ring = recv_ring->next;

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
    atm_errno = netperf_response->serv_errno;
    perror("netperf: remote error");
    atm_error("        if it was atm");
    fprintf(stderr,"        the errno was: %d atm_errno %d\n",
	    errno,
	    atm_errno);
    fflush(where);
    exit(1);
  }

  /* The test is over. */
  
  if (atm_close(send_socket) != 0) {
    /* we will not consider this a fatal error. just display a message */
    /* and move on */
    atm_error("netperf: cannot shutdown fore socket");
    perror("netperf: cannot shutdown fore socket");
  }
  

  /* We now calculate what our thruput was for the test. In the future, */
  /* we may want to include a calculation of the thruput measured by */
  /* the remote, but it should be the case that for a FORE stream test, */
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
      remote_cpu_utilization = fore_rr_result->cpu_util;
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
	      selected_atm_qos.mean_burst.target,		/* local sendbuf size */
	      selected_atm_qos.mean_burst.target,
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
	      selected_atm_qos.mean_burst.target,
	      selected_atm_qos.mean_burst.target);
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
	      selected_atm_qos.mean_burst.target,
	      selected_atm_qos.mean_burst.target,
	      req_size,		/* how large were the requests */
	      rsp_size,		/* how large were the responses */
	      elapsed_time, 		/* how long did it take */
	      nummessages/elapsed_time);
      fprintf(where,
	      tput_fmt_1_line_2,
	      selected_atm_qos.mean_burst.target, 		/* remote recvbuf size */
	      selected_atm_qos.mean_burst.target);
      
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
    /* FORE statistics, the alignments of the sends and receives */
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

 /* this routine implements the receive side (netserver) of a FORE_RR */
 /* test. */
int 
recv_fore_crr()
{
  
  struct ring_elt *recv_ring;
  struct ring_elt *send_ring;

  Atm_endpoint myaddr,  peeraddr;
  Aal_type my_aal;
  int connection_id;
  int	s_data;
  int 	addrlen;
  int	measure_cpu;
  int	trans_received;
  int	trans_remaining;
  float	elapsed_time;
  
  struct        sigaction       action;

  struct	fore_rr_request_struct	*fore_rr_request;
  struct	fore_rr_response_struct	*fore_rr_response;
  struct	fore_rr_results_struct	*fore_rr_results;
  
  init_test_vars();

  fore_rr_request  = 
    (struct fore_rr_request_struct *)netperf_request->test_specific_data;
  fore_rr_response = 
    (struct fore_rr_response_struct *)netperf_response->test_specific_data;
  fore_rr_results  = 
    (struct fore_rr_results_struct *)netperf_response->test_specific_data;
  
  if (debug) {
    fprintf(where,"netserver: recv_fore_crr: entered...\n");
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
    fprintf(where,"recv_fore_crr: setting the response type...\n");
    fflush(where);
  }
  
  netperf_response->response_type = FORE_RR_RESPONSE;
  
  if (debug) {
    fprintf(where,"recv_fore_crr: the response type is set...\n");
    fflush(where);
  }
  
  /* We now alter the message_ptr variables to be at the desired */
  /* alignments with the desired offsets. */
  
  if (debug) {
    fprintf(where,"recv_fore_crr: requested recv alignment of %d offset %d\n",
	    fore_rr_request->recv_alignment,
	    fore_rr_request->recv_offset);
    fprintf(where,"recv_fore_crr: requested send alignment of %d offset %d\n",
	    fore_rr_request->send_alignment,
	    fore_rr_request->send_offset);
    fflush(where);
  }

  if (send_width == 0) send_width = 1;
  if (recv_width == 0) recv_width = 1;

  recv_ring = allocate_buffer_ring(recv_width,
				   fore_rr_request->request_size,
				   fore_rr_request->recv_alignment,
				   fore_rr_request->recv_offset);

  send_ring = allocate_buffer_ring(send_width,
				   fore_rr_request->response_size,
				   fore_rr_request->send_alignment,
				   fore_rr_request->send_offset);

  if (debug) {
    fprintf(where,"recv_fore_crr: receive alignment and offset set...\n");
    fflush(where);
  }
  
  /* Let's clear-out our endpoints for the sake of cleanlines. Then we */
  /* can put in OUR values !-) At some point, we may want to nail this */
  /* socket to a particular network-level address. raj 8/94 */
  
  bzero((char *)&myaddr,
	sizeof(myaddr));
  bzero((char *)&peeraddr,
	sizeof(peeraddr));
  
  /* Grab a socket to listen on, and then listen on it. */
  
  if (debug) {
    fprintf(where,"recv_fore_crr: grabbing a socket...\n");
    fflush(where);
  }

  /* create_fore_socket expects to find some things in the global */
  /* variables, so set the globals based on the values in the request. */
  /* once the socket has been created, we will set the response values */
  /* based on the updated value of those globals. raj 7/94 */

#ifdef __alpha
  
  /* ok - even on a DEC box, strings are strings. I din't really want */
  /* to ntohl the words of a string. since I don't want to teach the */
  /* send_ and recv_ _request and _response routines about the types, */
  /* I will put "anti-ntohl" calls here. I imagine that the "pure" */
  /* solution would be to use XDR, but I am still leary of being able */
  /* to find XDR libs on all platforms I want running netperf. raj */
  {
    int *charword;
    int *initword;
    int *lastword;
    
    initword = (int *) fore_rr_request->atm_device;
    lastword = initword + ((fore_rr_request->dev_name_len + 3) / 4);
    
    for (charword = initword;
	 charword < lastword;
	 charword++) {
      
      *charword = htonl(*charword);
    }
  }
#endif /* __alpha */

  strncpy(loc_atm_device,
	  fore_rr_request->atm_device,
	  fore_rr_request->dev_name_len);
  s_data = create_fore_socket();
  
  if (s_data < 0) {
    netperf_response->serv_errno = errno;
    send_response();
    
    exit(1);
  }
  /* Let's get an address assigned to this socket so we can tell the */
  /* initiator how to reach the data socket. There may be a desire to */
  /* nail this socket to a specific address in a multi-homed, */
  /* multi-connection situation, but for now, we'll ignore the issue */
  /* and concentrate on single connection testing. we specify the qlen */
  /* parm as 1 because this is the server side. raj 8/94 */
  
  if (debug) {
    fprintf(where,"recv_fore_crr: about to bind\n");
    fflush(where);
  }

  if (atm_bind(s_data,
	       loc_atm_sap,

	       (Atm_sap *)(&fore_rr_response->server_asap),
	       2) == -1) {
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }

  if (debug) {
    fprintf(where,"recv_fore_crr: bind complete asap is %x\n",
	    fore_rr_response->server_asap);
    fflush(where);
  }

  
  netperf_response->serv_errno   = 0;
  
  /* But wait, there's more. If the initiator wanted cpu measurements, */
  /* then we must call the calibrate routine, which will return the max */
  /* rate back to the initiator. If the CPU was not to be measured, or */
  /* something went wrong with the calibration, we will return a 0.0 to */
  /* the initiator. */
  
  fore_rr_response->cpu_rate = 0.0; 	/* assume no cpu */
  if (fore_rr_request->measure_cpu) {
    fore_rr_response->measure_cpu = 1;
    fore_rr_response->cpu_rate = 
      calibrate_local_cpu(fore_rr_request->cpu_rate);
  }
   
  /* before we send the response back to the initiator, pull some of */
  /* the socket parms from the globals */
  fore_rr_response->send_buf_size = selected_atm_qos.mean_burst.target;
  fore_rr_response->recv_buf_size = selected_atm_qos.mean_burst.target;
 
  if (debug) {
    fprintf(where,"recv_fore_crr: about to respond\n");
    fflush(where);
  }

  send_response();
  
  /* now we have to listen and accept on the connection. I have some */
  /* question about where this should be. Should this be after the */
  /* call to cpu_start? */

  connection_id = 0;

  if (debug) {
    int switchid;
    int portid;
    GET_SWITCH(switchid,peeraddr.nsap);
    GET_PORT(portid,peeraddr.nsap);
    fprintf(where,"recv_fore_crr: about to listen\n");
    fprintf(where,"              s_data is %d\n",s_data);
    fprintf(where,"              connid is %d\n",connection_id);
    fprintf(where,"              switch is %d\n",switchid);
    fprintf(where,"              portid is %d\n",portid);
    fprintf(where,"              aal    is %d\n",my_aal);
    fflush(where);
  }


  if (atm_listen(s_data,
		 &connection_id,
		 &peeraddr,
		 &desired_atm_qos,
		 &my_aal) < 0) {
    fprintf(where,
	    "netperf: recv_fore_crr: atm_listen: atm_errno = %d\n",
	    atm_errno);
    fflush(where);
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }

  if (debug) {
    int switchid;
    int portid;
    GET_SWITCH(switchid,peeraddr.nsap);
    GET_PORT(portid,peeraddr.nsap);
    fprintf(where,"recv_fore_crr: about to accept\n");
    fprintf(where,"              s_data is %d\n",s_data);
    fprintf(where,"              asap is %d\n",peeraddr.asap);
    fprintf(where,"              connid is %d\n",connection_id);
    fprintf(where,"              switch is %d\n",switchid);
    fprintf(where,"              portid is %d\n",portid);
    fprintf(where,"              aal    is %d\n",my_aal);
    fflush(where);
  }


  if (atm_accept(s_data,
		 s_data,
		 connection_id,
		 &desired_atm_qos,
		 duplex) < 0) {
    fprintf(where,
	    "netperf: recv_fore_crr: atm_accept: atm_errno = %d\n",
	    atm_errno);
    fflush(where);
    netperf_response->serv_errno = atm_errno;
    send_response();
    exit(1);
  }

  trans_received = 0;
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(fore_rr_request->measure_cpu);
  
  if (fore_rr_request->test_length > 0) {
    times_up = 0;
    trans_remaining = 0;
    start_timer(fore_rr_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    trans_remaining = fore_rr_request->test_length * -1;
  }
  
  addrlen = sizeof(peeraddr);
  bzero((char *)&peeraddr, addrlen);
  
  while ((!times_up) || (trans_remaining > 0)) {
    
    /* receive the request from the other side. the question remains */
    /* as to whether or not the FORE API provides a stream or message */
    /* paradigm. we will assume a message paradigm for the moment */
    /* raj 8/94 */
    if (atm_recv(s_data,
		 recv_ring->buffer_ptr,
		 fore_rr_request->request_size) != 
	fore_rr_request->request_size) {
      if (errno == EINTR) {
	/* we must have hit the end of test time. */
	break;
      }
      if (debug) {
	fprintf(where,
		"netperf: recv_fore_crr: atm_send: errno %d atm_errno %d\n",
		errno,
		atm_errno);
	fflush(where);
      }
      netperf_response->serv_errno = atm_errno;
      send_response();
      exit(1);
    }
    recv_ring = recv_ring->next;

    /* Now, send the response to the remote */
    if (atm_send(s_data,
		 send_ring->buffer_ptr,
		 fore_rr_request->response_size) != 
	fore_rr_request->response_size) {
      if (errno == EINTR) {
	/* we have hit end of test time. */
	break;
      }
      if (debug) {
	fprintf(where,
		"netperf: recv_fore_crr: atm_send: errno %d atm_errno %d\n",
		errno,
		atm_errno);
	fflush(where);
      }
      netperf_response->serv_errno = atm_errno;
      send_response();
      exit(1);
    }
    send_ring = send_ring->next;
    
    trans_received++;
    if (trans_remaining) {
      trans_remaining--;
    }
    
    if (debug) {
      fprintf(where,
	      "recv_fore_crr: Transaction %d complete.\n",
	      trans_received);
      fflush(where);
    }
    
  }
  
  
  /* The loop now exits due to timeout or transaction count being */
  /* reached */
  
  cpu_stop(fore_rr_request->measure_cpu,&elapsed_time);
  
  if (times_up) {
    /* we ended the test by time, which was at least 2 seconds */
    /* longer than we wanted to run. so, we want to subtract */
    /* PAD_TIME from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }
  /* send the results to the sender			*/
  
  if (debug) {
    fprintf(where,
	    "recv_fore_crr: got %d transactions\n",
	    trans_received);
    fflush(where);
  }
  
  fore_rr_results->bytes_received	= (trans_received * 
					   (fore_rr_request->request_size + 
					    fore_rr_request->response_size));
  fore_rr_results->trans_received	= trans_received;
  fore_rr_results->elapsed_time	= elapsed_time;
  if (fore_rr_request->measure_cpu) {
    fore_rr_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_fore_crr: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();
  
}

void
print_fore_usage()
{

  printf("%s",fore_usage);
  exit(1);

}
void
scan_fore_args(argc, argv)
     int	argc;
     char	*argv[];

{
#define FORE_ARGS "a:b:D:hm:M:p:P:r:"
  extern int	optind, opterrs;  /* index of first unused arg 	*/
  extern char	*optarg;	  /* pointer to option string	*/
  
  int		c;
  
  char	
    arg1[BUFSIZ],  /* argument holders		*/
    arg2[BUFSIZ];
  
  /* the first thing that we want to do is set all the defaults for */
  /* the test-specific parms. */

  init_test_vars();
  init_done = 1;

  /* Go through all the command line arguments and break them */
  /* out. For those options that take two parms, specifying only */
  /* the first will set both to that value. Specifying only the */
  /* second will leave the first untouched. To change only the */
  /* first, use the form "first," (see the routine break_args.. */
  
  while ((c= getopt(argc, argv, FORE_ARGS)) != EOF) {
    switch (c) {
    case '?':	
    case 'h':
      print_fore_usage();
      exit(1);
    case 'a':
      /* set the TCP nodelay flag */
      if (atoi(optarg) == 5) aal = aal_type_5;
      if (atoi(optarg) == 4) aal = aal_type_4;
      if (atoi(optarg) == 3) aal = aal_type_3;
      break;
    case 'b':
      /* set the mean burst target and minimum. the units are kilobit */
      /* packets  */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	desired_atm_qos.mean_burst.target = atoi(arg1);
      if (arg2[0])
	desired_atm_qos.mean_burst.minimum = atoi(arg2);
      break;
    case 'D':
      /* set the atm device file name for use in the atm_open() call. */
      /* at some point we should do some error checking... */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	strcpy(loc_atm_device,arg1);
      if (arg2[0])
	strcpy(rem_atm_device,arg2);
      break;
    case 'p':
      /* set the peak bandwidth target and minimum. the units are */
      /* kilobits per second */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	desired_atm_qos.peak_bandwidth.target = atoi(arg1);
      if (arg2[0])
	desired_atm_qos.peak_bandwidth.minimum = atoi(arg2);
      break;
    case 'P':
      /* set the mean bandwidth target and minimum. the units are */
      /* kilobits per second */
      break_args(optarg,arg1,arg2);
      if (arg1[0])
	desired_atm_qos.mean_bandwidth.target = atoi(arg1);
      if (arg2[0])
	desired_atm_qos.mean_bandwidth.minimum = atoi(arg2);
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
    };
  }
}
#endif /* DO_FORE */
