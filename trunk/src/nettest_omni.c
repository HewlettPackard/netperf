#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WANT_OMNI
char nettest_omni_id[]="\
@(#)nettest_dlpi.c (c) Copyright 2008 Hewlett-Packard Co. Version 2.5.0pre";

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

#define NETPERF_WAITALL 0x1
#define NETPERF_XMIT 0x2
#define NETPERF_RECV 0x4

#define NETPERF_IS_RR(x) (((x & NETPERF_XMIT) && (x & NETPERF_RECV)) || \
			  (!((x & NETPERF_XMIT) || (x & NETPERF_RECV)))

/* a boatload of globals while I settle things out */
int socket_type;
int protocol;
int direction;
int remote_send_size = -1;
int remote_recv_size = -1;
int remote_use_sendfile;
int connect_test;
int remote_send_dirty_count;
int remote_recv_dirty_count;
int remote_recv_clean_count;
int remote_checksum_off;
int connection_test;
int need_to_connect;
int need_connection;
int bytes_to_send;
int bytes_per_send;
int failed_sends;
int bytes_to_recv;
int bytes_per_recv;
int null_message_ok;
uint64_t	trans_completed;
uint64_t	units_remaining;
uint64_t	bytes_sent;
uint64_t      bytes_received;
uint64_t      local_send_calls;
uint64_t      local_receive_calls;
uint64_t      remote_bytes_sent;
uint64_t      remote_bytes_received;
uint64_t      remote_send_calls;
uint64_t      remote_receive_calls;

int sd_kb = 1;  /* is the service demand per KB or per tran? */

extern int first_burst_size;

#if defined(HAVE_SENDFILE) && (defined(__linux) || defined(__sun))
#include <sys/sendfile.h>
#endif /* HAVE_SENDFILE && (__linux || __sun) */

static  char  local_cpu_method;
static  char  remote_cpu_method;

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
	    "Unexpected Address Family of %u\n",res->ai_family);
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
  static myport = 0;

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

  if (debug) {
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
    if (SOCKET_EINTR(len))
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

  void * temp_message_ptr;
  int bytes_left;
  int bytes_recvd;
  int my_recvs;

  /* receive data off the data_socket, ass-u-me-ing a blocking socket
     all the way!-) 2008-01-08 */
  my_recvs = 0;
  bytes_left = bytes_to_recv;
  temp_message_ptr  = recv_ring->buffer_ptr;

  if (debug) {
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
    fprintf(where,"recv_data bytes_recvd %d bytes_left %d\n",
	    bytes_recvd,bytes_left);
    fflush(where);
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
close_data_socket(SOCKET data_socket)
{

  int ret;

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
disconnect_data_socket(SOCKET data_socket, int initiate, int do_close) 
{

  char buffer[4];
  int bytes_recvd;

  fprintf(where,
	  "disconnect_d_s sock %d init %d do_close %d\n",
	  data_socket,
	  initiate,
	  do_close);
  fflush(where);

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
	return 0;
      }
    return -1;
    }
    
    if (do_close) 
      close(data_socket);

    return 1;
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
  
  int			timed_out = 0;
  float			elapsed_time;
  
  int len;
  int ret;
  int connected;

  struct ring_elt *send_ring;
  struct ring_elt *recv_ring;

  struct sockaddr_storage remote_addr;
  int                     remote_addr_len = sizeof(remote_addr);

  SOCKET	data_socket;
  int           need_socket;

  double	bytes_xferd;
  double        remote_bytes_xferd;

  int   temp_recvs;

  float	        local_cpu_utilization;
  float	        local_service_demand;
  float	        remote_cpu_utilization;
  float	        remote_service_demand;
  double	thruput;
  
  struct addrinfo *local_res;
  struct addrinfo *remote_res;

  struct	omni_request_struct	*omni_request;
  struct	omni_response_struct	*omni_response;
  struct	omni_results_struct	*omni_result;
  
  omni_request = 
    (struct omni_request_struct *)netperf_request.content.test_specific_data;
  omni_response = 
    (struct omni_response_struct *)netperf_response.content.test_specific_data;
  omni_result =
    (struct omni_results_struct *)netperf_response.content.test_specific_data;
  
  
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

  printf("omni: direction %x\n",direction);

  /* initialize a few counters */
  
  bytes_xferd	= 0.0;
  remote_bytes_xferd = 0.0;
  times_up 	= 0;
  need_socket   = 1;

  if (connection_test) 
    pick_next_port_number(local_res,remote_res);

  data_socket = create_data_socket(local_res);
  
  if (data_socket == INVALID_SOCKET) {
    perror("netperf: send_omni: unable to create data socket");
    exit(1);
  }
  need_socket = 0;

  /* we need to consider if this is a request/response test, if we are
     receiving, if we are sending, etc, when setting-up our recv and
     send buffer rings.  raj 2008-01-07 */
  if (direction & NETPERF_XMIT) {
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

  if (direction & NETPERF_RECV) {
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
    omni_request->send_buf_size	         = rss_size_req;
    omni_request->send_size              = remote_send_size;
    omni_request->send_alignment	 = remote_send_align;
    omni_request->send_offset	         = remote_send_offset;
    omni_request->send_width             = 1; /* FIX THIS */
    omni_request->request_size	         = req_size;

    omni_request->recv_buf_size	         = rsr_size_req;
    omni_request->receive_size           = remote_recv_size;
    omni_request->recv_alignment	 = remote_recv_align;
    omni_request->recv_offset	         = remote_recv_offset;
    omni_request->recv_width             = 1; /* FIX THIS */
    omni_request->response_size	         = rsp_size;

    omni_request->no_delay	         = rem_nodelay;
    omni_request->use_sendfile           = remote_use_sendfile;
    omni_request->connect_test           = connect_test;

    omni_request->measure_cpu	         = remote_cpu_usage;
    omni_request->cpu_rate	         = remote_cpu_rate;
    if (test_time) {
      omni_request->test_length	         = test_time;
    }
    else {
      omni_request->test_length	         = test_trans * -1;
    }
    omni_request->so_rcvavoid	         = rem_rcvavoid;
    omni_request->so_sndavoid	         = rem_sndavoid;
    omni_request->send_dirty_count       = remote_send_dirty_count;
    omni_request->recv_dirty_count       = remote_recv_dirty_count;
    omni_request->recv_clean_count       = remote_recv_clean_count;

    omni_request->checksum_off           = remote_checksum_off;
    omni_request->data_port              = atoi(remote_data_port);
    omni_request->ipfamily               = af_to_nf(remote_res->ai_family);
    omni_request->socket_type            = hst_to_nst(socket_type);
    omni_request->protocol               = protocol;

    omni_request->direction              = 0;
    /* yes, the sense here is correct - if we are transmitting, they
       receive, if we are receiving, they are transmitting... */
    if (direction & NETPERF_XMIT)
      omni_request->direction |= NETPERF_RECV;
    if (direction & NETPERF_RECV)
      omni_request->direction |= NETPERF_XMIT;

    /* some tests may require knowledge of our local addressing. such
       tests will for the time being require that the user specify a
       local IP/name and port number, so we can extract them from the
       local_res addrinfo. we "know" that the ipaddr "array" has
       enough space for a full ipv6 address */
    extract_inet_address_and_port(local_res,
				  omni_request->ipaddr,
				  sizeof(omni_request->ipaddr),
				  &(omni_request->netperf_port));
    
    if (debug > 1) {
      fprintf(where,"netperf: send_omni: requesting OMNI test\n");
    }
    
    send_request();
    
    /* The response from the remote will contain all of the relevant
       socket parameters for this test type. We will put them back
       into the variables here so they can be displayed if desired.
       The remote will have calibrated CPU if necessary, and will have
       done all the needed set-up we will have calibrated the cpu
       locally before sending the request, and will grab the counter
       value right after the connect returns. The remote will grab the
       counter right after the accept call. This saves the hassle of
       extra messages being sent for the TCP tests.  */
  
    recv_response();
  
    if (!netperf_response.content.serv_errno) {
      rsr_size	       = omni_response->recv_buf_size;
      remote_recv_size = omni_response->receive_size;
      rss_size	       = omni_response->send_buf_size;
      remote_send_size = omni_response->send_size;
      rem_nodelay      = omni_response->no_delay;
      remote_use_sendfile = omni_response->use_sendfile;
      remote_cpu_usage = omni_response->measure_cpu;
      remote_cpu_rate  = omni_response->cpu_rate;
      /* make sure that port numbers are in network order */
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
      exit(1);
    }
  }
#ifdef WANT_DEMO
  DEMO_RR_SETUP(100);
#endif

  /* if we are not a connectionless protocol, we need to connect. at
     some point even if we are a connectionless protocol, we may still
     want to "connect" for convenience raj 2008-01-14 */
  need_to_connect = (protocol != IPPROTO_UDP);

  /* Set-up the test end conditions. For tests over a
     "reliable/connection-oriented" transport (eg TCP, SCTP, etc) this
     can be either time or byte/transaction count based.  for
     unreliable transport or connection tests it can only be time
     based.  having said that, we rely entirely on other code to
     enforce this before we even get here. raj 2008-01-08 */
  
  if (test_time) {
    /* The user wanted to end the test after a period of time. */
    times_up = 0;
    units_remaining = 0;
    start_timer(test_time);
  }
  else {
    /* The tester wanted to send a number of bytes or exchange a
       number of transactions. */
    if ((direction & NETPERF_XMIT) && (direction & NETPERF_RECV))
      units_remaining = test_trans;
    else
      units_remaining = test_bytes;
    times_up = 1;
  }

  /* grab the current time, and if necessary any starting information
     for the gathering of CPU utilization at this end. */
  cpu_start(local_cpu_usage);

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
       it _should_ be the case that the time spent up here is epsilon
       compared to time spent elsewhere in the stack so it should not
       be a big deal.  famous last words of raj 2008-01-08 */
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
	break;
      }
      else if ((ret == -2) && connection_test) {
	/* transient error  on a connection test means go around and
	   try again with another local port number */
	close(data_socket);
	need_socket = 1;
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
      }
      else if (ret == -1) {
	times_up = 1;
	break;
      }
      else {
	perror("netperf: send_omni: send_data failed");
	exit(1);
      }

    }


    if (direction & NETPERF_RECV) {
      ret = recv_data(data_socket,
		      recv_ring,
		      bytes_to_recv,
		      (connected) ? NULL : (struct sockaddr *)&remote_addr,
		      /* if remote_addr NULL this is ignored */
		      &remote_addr_len,
		      /* if XMIT also set this is RR so waitall */
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
	break;
      }
      else {
	/* presently at least, -2 and -3 are equally bad on recv */
	perror("netperf: send_omni: recv_data failed");
	exit(1);
      }
      recv_ring = recv_ring->next;
    }


    /* if this is a connection test, we want to do some stuff about
       connection close here in the test loop. raj 2008-01-08 */
    if (connection_test) {
      ret = disconnect_data_socket(data_socket,
				   (no_control) ? 1 : 0,
				   1);
      if (ret == 0) {
	/* we will need a new connection to be established next time
	   around the loop */
	need_connection = 1;
	connected = 0;
	need_socket = 1;
	pick_next_port_number(local_res,remote_res);
      }
      else if (ret == -1) {
	times_up = 1;
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
    DEMO_RR_INTERVAL(1);
#endif

    /* was this a "transaction" test? don't for get that a TCP_CC
       style test will have no xmit or recv :) so, we check for either
       both XMIT and RECV set, or neither XMIT nor RECV set */
    if (((direction & NETPERF_XMIT) && (direction & NETPERF_RECV)) ||
	!((direction & NETPERF_XMIT) || (direction & NETPERF_RECV))) {
      trans_completed++;
      if (units_remaining) {
	units_remaining--;
      }
    }
    
    
  }

  /* we are now, ostensibly, at the end of this iteration */

  /* so, if we have/had a data connection, we will want to close it
     now, and this will be independent of whether there is a control
     connection. */
  fprintf(where,"test over connected %d\n",connected);;
  fflush(where);

  if (connected) {
    /* before we do close the connection, we may want to retrieve some
       of the socket parameters - Linux, thanks to its autotuning, can
       have the final socket buffer sizes different from the initial
       socket buffer sizes.  isn't that nice.  raj 2008-01-08 */
    /* FILL THIS IN; */
    /* CHECK PARMRS HERE; */
    ret = disconnect_data_socket(data_socket,
				 1,
				 1);
    connected = 0;
    need_socket = 1;

  }
  
  /* this call will always give us the elapsed time for the test, and
     will also store-away the necessaries for cpu utilization */

  cpu_stop(local_cpu_usage,&elapsed_time);
  
  if (!no_control) {
    /* Get the statistics from the remote end. The remote will have
       calculated service demand and all those interesting things. If
       it wasn't supposed to care, it will return obvious values. */
  
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
  }

  /* so, what was the end result? */

  /* why?  because some stacks want to be clever and autotune their
     socket buffer sizes, which means that if we accept the defaults,
     the size we get from getsockopt() at the beginning of a
     connection may not be what we would get at the end of the
     connection... */
  rsr_size_end = omni_result->recv_buf_size;
  rss_size_end = omni_result->send_buf_size;

  /* to we need to pull something from omni_results here? */
  bytes_xferd  = bytes_sent + bytes_received;
  thruput      = calc_thruput(bytes_xferd);
  remote_bytes_xferd = omni_result->bytes_received +
    omni_result->bytes_sent;
  
  printf("bytes xfered %g  remote %g trans %d elapsed %g\n",
	 bytes_xferd,
	 remote_bytes_xferd,
	 omni_result->trans_received,
	 elapsed_time);

  if (local_cpu_usage || remote_cpu_usage) {
    /* We must now do a little math for service demand and cpu */
    /* utilization for the system(s) */
    /* Of course, some of the information might be bogus because */
    /* there was no idle counter in the kernel(s). We need to make */
    /* a note of this for the user's benefit...*/
    if (local_cpu_usage) {
      if (local_cpu_rate == 0.0) {
	fprintf(where,
		"WARNING WARNING WARNING  WARNING WARNING WARNING  WARNING!\n");
	fprintf(where,
		"Local CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      local_cpu_utilization = calc_cpu_util(0.0);

      /* we need to decide what to feed the service demand beast,
	 which will, ultimately, depend on what sort of test it is and
	 whether or not the user asked for something specific - as in
	 per KB even on a TCP_RR test if it is being (ab)used as a
	 bidirectional bulk-transfer test. raj 2008-01-14 */
      local_service_demand  = 
	calc_service_demand((sd_kb) ? bytes_xferd : (double)trans_completed * 1024,
			    0.0,
			    0.0,
			    0);
    }
    else {
      local_cpu_utilization	= (float) -1.0;
      local_service_demand	= (float) -1.0;
    }
    
    if (remote_cpu_usage) {
      if (remote_cpu_rate == 0.0) {
	fprintf(where,
		"DANGER  DANGER  DANGER    DANGER  DANGER  DANGER    DANGER!\n");
	fprintf(where,
		"Remote CPU usage numbers based on process information only!\n");
	fflush(where);
      }
      remote_cpu_utilization = omni_result->cpu_util;
      /* since calc_service demand is doing ms/Kunit we will */
      /* multiply the number of transaction by 1024 to get */
      /* "good" numbers */
      remote_service_demand = calc_service_demand((sd_kb) ? bytes_xferd : 
(double) trans_completed * 1024,
						  0.0,
						  remote_cpu_utilization,
						  omni_result->num_cpus);
    }
    else {
      remote_cpu_utilization = (float) -1.0;
      remote_service_demand  = (float) -1.0;
    }
    
    /* at some point we may want to actually display some results :) */


  }
  else {
    /* The tester did not wish to measure service demand. */

  }
  
  /* likely as not we are going to do something slightly different here */
  if (verbosity > 1) {

#ifdef WANT_HISTOGRAM
    fprintf(where,"\nHistogram of request/response times\n");
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
  int   need_to_connect;
  int   need_to_accept;
  int   connected;
  int   ret;
  int   temp_recvs;
  float	elapsed_time;
  
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

  fprintf(where,"recv_omni direction %x\n",omni_request->direction);
  fflush(where);

  if (omni_request->direction & NETPERF_XMIT) {
    fprintf(where,"about to allocate a buffer ring send_width %d rsp %d send %d\n",omni_request->send_width, omni_request->response_size,omni_request->send_size);
    fflush(where);
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
    fprintf(where,"about to allocate a buffer ring send_width %d size %d\n",send_width, bytes_to_send);
    fflush(where);
    send_ring = allocate_buffer_ring(send_width,
				     bytes_to_send,
				     omni_request->send_alignment,
				     omni_request->send_offset);
				     
  }

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

    fprintf(where,"about to allocate a buffer ring recv_width %d size %d\n",recv_width, bytes_to_recv);
    fflush(where);
    recv_ring = allocate_buffer_ring(recv_width,
				     bytes_to_recv,
				     omni_request->recv_alignment,
				     omni_request->recv_offset);
				     
  }

#ifdef WIN32
  /* The test timer can fire during operations on the listening socket,
     so to make the start_timer below work we have to move
     it to close s_listen while we are blocked on accept. */
  win_kludge_socket2 = s_listen;
#endif

  fprintf(where,"protocol %d\n",omni_request->protocol);
  fflush(where);
  need_to_accept = (omni_request->protocol != IPPROTO_UDP);
  
  /* we need to hang a listen for everything that needs at least one
     accept */
  if (need_to_accept) {
    fprintf(where,"listening\n");
    fflush(where);
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

  send_response();
  
  addrlen = sizeof(peeraddr_in);
  
  /* Now it's time to start receiving data on the connection. We will */
  /* first grab the apropriate counters and then start grabbing. */
  
  cpu_start(omni_request->measure_cpu);
  
  /* The loop will exit when the sender does a shutdown, which will */
  /* return a length of zero   */
  
  if (omni_request->test_length > 0) {
    times_up = 0;
    units_remaining = 0;
    start_timer(omni_request->test_length + PAD_TIME);
  }
  else {
    times_up = 1;
    units_remaining = omni_request->test_length * -1;
  }
  
  trans_completed = 0;

  while ((!times_up) || (units_remaining > 0)) {

    if (need_to_accept) {
      fprintf(where,"accepting\n");
      fflush(where);
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
	  times_up = 1;
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

    fprintf(where,"one  direction %x\n",omni_request->direction);
    fflush(where);

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
      fprintf(where,"receiving %d bytes\n",bytes_to_recv);
      fflush(where);
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
      ret = close_data_socket(data_socket);
      if (ret < 0) {
	perror("netperf: send_omni: disconnect_data_socket failed");
	exit(1);
      }
      else if (ret == 0) {
	times_up = 1;
	break;
      }
      /* we will need a new connection to be established */
      need_connection = 1;
      connected = 0;
    }


    /* was this a "transaction" test? don't for get that a TCP_CC
       style test will have no xmit or recv :) so, we check for either
       both XMIT and RECV set, or neither XMIT nor RECV set */
    if (((direction & NETPERF_XMIT) && (direction & NETPERF_RECV)) ||
	!((direction & NETPERF_XMIT) || (direction & NETPERF_RECV))) {
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
    /* we ended the test by time, which was at least PAD_TIME seconds
       longer than we wanted to run. so, we want to subtract PAD_TIME
       from the elapsed_time. */
    elapsed_time -= PAD_TIME;
  }

  if (connected) {
    close_data_socket(data_socket);
  }

  /* send the results to the sender  */
  
  omni_results->bytes_received	= bytes_received;
  omni_results->bytes_sent      = bytes_sent;
  omni_results->trans_received	= trans_completed;
  omni_results->elapsed_time	= elapsed_time;
  if (omni_request->measure_cpu) {
    omni_results->cpu_util	= calc_cpu_util(elapsed_time);
  }
  
  if (debug) {
    fprintf(where,
	    "recv_omni: test complete, sending results.\n");
    fflush(where);
  }
  
  send_response();

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

#define OMNI_ARGS "b:cCDnNhH:L:m:M:p:P:r:s:S:t:T:Vw:W:46"

  extern char	*optarg;	  /* pointer to option string	*/
  
  int		c;
  
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

  /* default to TCP. i wonder if this should be here or in
     send_omni? */
#ifdef IPPROTO_TCP
  protocol = IPPROTO_TCP;
#endif

  /* default to direction being NETPERF_XMIT */
  direction = NETPERF_XMIT;

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
    case 'm':
      /* set the send size. if we set the local send size it will add
	 XMIT to direction.  if we set the remote send size it will
	 add RECV to the direction */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	send_size = convert(arg1);
	direction |= NETPERF_XMIT;
      }
      if (arg2[0]) {
	remote_send_size = convert(arg2);
	direction |= NETPERF_RECV;
      }
      break;
    case 'M':
      /* set the recv sizes.  if we set the local recv size it will
	 add RECV to direction.  if we set the remote recv size it
	 will add XMIT to direction  */
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	remote_recv_size = convert(arg1);
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
      printf("direction %d\n",direction);
      direction |= NETPERF_XMIT;
      printf("direction %d\n",direction);
      direction |= NETPERF_RECV;
      printf("direction %d\n",direction);
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
