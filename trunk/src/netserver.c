/*

	   Copyright (C) 1993-2012 Hewlett-Packard Company
                         ALL RIGHTS RESERVED.

  The enclosed software and documentation includes copyrighted works
  of Hewlett-Packard Co. For as long as you comply with the following
  limitations, you are hereby authorized to (i) use, reproduce, and
  modify the software and documentation, and to (ii) distribute the
  software and documentation, including modifications, for
  non-commercial purposes only.

  1.  The enclosed software and documentation is made available at no
      charge in order to advance the general development of
      high-performance networking products.

  2.  You may not delete any copyright notices contained in the
      software or documentation. All hard copies, and copies in
      source code or object code form, of the software or
      documentation (including modifications) must contain at least
      one of the copyright notices.

  3.  The enclosed software and documentation has not been subjected
      to testing and quality control and is not a Hewlett-Packard Co.
      product. At a future time, Hewlett-Packard Co. may or may not
      offer a version of the software and documentation as a product.

  4.  THE SOFTWARE AND DOCUMENTATION IS PROVIDED "AS IS".
      HEWLETT-PACKARD COMPANY DOES NOT WARRANT THAT THE USE,
      REPRODUCTION, MODIFICATION OR DISTRIBUTION OF THE SOFTWARE OR
      DOCUMENTATION WILL NOT INFRINGE A THIRD PARTY'S INTELLECTUAL
      PROPERTY RIGHTS. HP DOES NOT WARRANT THAT THE SOFTWARE OR
      DOCUMENTATION IS ERROR FREE. HP DISCLAIMS ALL WARRANTIES,
      EXPRESS AND IMPLIED, WITH REGARD TO THE SOFTWARE AND THE
      DOCUMENTATION. HP SPECIFICALLY DISCLAIMS ALL WARRANTIES OF
      MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

  5.  HEWLETT-PACKARD COMPANY WILL NOT IN ANY EVENT BE LIABLE FOR ANY
      DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES
      (INCLUDING LOST PROFITS) RELATED TO ANY USE, REPRODUCTION,
      MODIFICATION, OR DISTRIBUTION OF THE SOFTWARE OR DOCUMENTATION.

*/

#include "netperf_version.h"

char	netserver_id[]="\
@(#)netserver.c (c) Copyright 1993-2012 Hewlett-Packard Co. Version 2.6.0";


#ifdef HAVE_CONFIG_H
#include "config.h"
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

#if HAVE_LIMITS_H
# include <limits.h>
#endif

#if HAVE_SYS_IPC_H
#include <sys/ipc.h>
#endif

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#if HAVE_NETDB_H
#include <netdb.h>
#endif

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if HAVE_ERRNO_H
#include <errno.h>
#endif

#if HAVE_SIGNAL_H
#include <signal.h>
/* some OS's have SIGCLD defined as SIGCHLD */
#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif /* SIGCLD */

#endif

#if !defined(HAVE_SETSID)
#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#endif

#ifdef WIN32
#include <time.h>
#include <winsock2.h>

#if HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif

#include <windows.h>

#include "missing\stdint.h"

#define strdup _strdup
#define sleep(x) Sleep((x)*1000)
#define netperf_socklen_t socklen_t
#endif /* WIN32 */

/* unconditional system includes */

#include <sys/types.h>
#include <stdio.h>
#include <fcntl.h>

/* netperf includes */
#include "netlib.h"
#include "nettest_bsd.h"

#ifdef WANT_UNIX
#include "nettest_unix.h"
#endif /* WANT_UNIX */

#ifdef WANT_DLPI
#include "nettest_dlpi.h"
#endif /* WANT_DLPI */

#ifdef WANT_SCTP
#include "nettest_sctp.h"
#endif

#include "netsh.h"

#ifndef DEBUG_LOG_FILE_DIR
#if defined(WIN32)
#define DEBUG_LOG_FILE_DIR ""
#elif defined(ANDROID)
#define DEBUG_LOG_FILE_DIR "/data/local/tmp/"
#else
#define DEBUG_LOG_FILE_DIR "/tmp/"
#endif
#endif /* DEBUG_LOG_FILE_DIR */

#ifndef DEBUG_LOG_FILE
#define DEBUG_LOG_FILE DEBUG_LOG_FILE_DIR"netserver.debug"
#endif

#if !defined(PATH_MAX)
#define PATH_MAX MAX_PATH
#endif
char     FileName[PATH_MAX];

char     listen_port[10];

struct listen_elt {
  SOCKET fd;
  struct listen_elt *next;
};

struct listen_elt *listen_list = NULL;

SOCKET   server_control;

int      child;   /* are we the child of inetd or a parent netserver?
		     */
int      netperf_daemon;
int      daemon_parent = 0;
int      not_inetd;
int      want_daemonize;
int      spawn_on_accept;
int      suppress_debug = 0;

extern	char	*optarg;
extern	int	optind, opterr;

/* char  *passphrase = NULL; */

static void
init_netserver_globals() {

#if defined(__VMS) || defined(VMWARE_UW)
  spawn_on_accept = 0;
  want_daemonize = 0;
#else
  spawn_on_accept = 1;
#if defined(WIN32)
  /* we only know how to spawn in WIN32, not daemonize */
  want_daemonize = 0;
#else
  want_daemonize = 1;
#endif /* WIN32 */
#endif /* __VMS || VMWARE_UW */

  child = 0;
  not_inetd = 0;
  netperf_daemon = 0;
}

void
unlink_empty_debug_file() {

#if !defined(WIN32)
  struct stat buf;

  if (stat(FileName,&buf)== 0) {

    if (buf.st_size == 0)
      unlink(FileName);
  }
#endif
}

/* it is important that set_server_sock() be called before this
   routine as we depend on the control socket being dup()ed out of the
   way when we go messing about with the streams. */
void
open_debug_file()
{
#if !defined WIN32
#define NETPERF_NULL "/dev/null"
#else
#define NETPERF_NULL "nul"
#endif

  FILE *rd_null_fp;

  if (where != NULL) fflush(where);

  snprintf(FileName,
	   sizeof(FileName),
#if defined(WIN32)
	   "%s\\%s_%d",
	   getenv("TEMP"),
#else
	   "%s_%d",
#endif
	   DEBUG_LOG_FILE,
	   getpid());
  if ((where = fopen((suppress_debug) ? NETPERF_NULL : FileName,
		     "w")) == NULL) {
    perror("netserver: debug file");
    exit(1);
  }

#if !defined(WIN32)

  chmod(FileName,0644);

  /* redirect stdin to "/dev/null" */
  rd_null_fp = fopen(NETPERF_NULL,"r");
  if (NULL == rd_null_fp) {
    fprintf(where,
	    "%s: opening of %s failed: %s (errno %d)\n",
	    __FUNCTION__,
	    NETPERF_NULL,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);
  }

  if (close(STDIN_FILENO) == -1) {
    fprintf(where,
	    "%s: close of STDIN_FILENO failed: %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);
  }

  if (dup(fileno(rd_null_fp)) == -1) {
    fprintf(where,
	    "%s: dup of rd_null_fp to stdin failed: %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);
  }

  /* redirect stdout to "where" */
  if (close(STDOUT_FILENO) == -1) {
    fprintf(where,
	    "%s: close of STDOUT_FILENO failed: %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);
  }

  if (dup(fileno(where)) == -1) {
    fprintf(where,
	    "%s: dup of where to stdout failed: %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);
  }

  /* redirect stderr to "where" */
  if (close(STDERR_FILENO) == -1) {
    fprintf(where,
	    "%s: close of STDERR_FILENO failed: %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);
  }

  if (dup(fileno(where)) == -1) {
    fprintf(where,
	    "%s: dup of where to stderr failed: %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);
  }

#else

  /* Hopefully, by closing stdout & stderr, the subsequent fopen calls
     will get mapped to the correct std handles. */
  fclose(stdout);

  if ((where = fopen(FileName, "w")) == NULL) {
    perror("netserver: fopen of debug file as new stdout failed!");
    exit(1);
  }

  fclose(stderr);

  if ((where = fopen(FileName, "w")) == NULL) {
    fprintf(stdout, "fopen of debug file as new stderr failed!\n");
    exit(1);
  }

#endif

}

/* so, either we are a child of inetd in which case server_sock should
   be stdin, or we are a child of a netserver parent.  there will be
   logic here for all of it, including Windows. it is important that
   this be called before open_debug_file() */

void
set_server_sock() {

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }

#ifdef WIN32
  server_sock = (SOCKET)GetStdHandle(STD_INPUT_HANDLE);
#elif !defined(__VMS)
  if (server_sock != INVALID_SOCKET) {
    fprintf(where,"Yo, Iz ain't invalid!\n");
    fflush(where);
    exit(1);
  }

  /* we dup this to up the reference count so when we do redirection
     of the io streams we don't accidentally toast the control
     connection in the case of our being a child of inetd. */
  server_sock = dup(0);

#else
  if ((server_sock =
       socket(TCPIP$C_AUXS, SOCK_STREAM, 0)) == INVALID_SOCKET) {
    fprintf(stderr,
	    "%s: failed to grab aux server socket: %s (errno %s)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(stderr);
    exit(1);
  }
#endif

}


void
create_listens(char hostname[], char port[], int af) {

  struct addrinfo hints;
  struct addrinfo *local_res;
  struct addrinfo *local_res_temp;
  int count, error;
  int on = 1;
  SOCKET temp_socket;
  struct listen_elt *temp_elt;

  if (debug) {
    fprintf(stderr,
	    "%s: called with host '%s' port '%s' family %s(%d)\n",
	    __FUNCTION__,
            hostname,
	    port,
	    inet_ftos(af),
            af);
    fflush(stderr);
  }
 memset(&hints,0,sizeof(hints));
  hints.ai_family = af;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  count = 0;
  do {
    error = getaddrinfo((char *)hostname,
                        (char *)port,
                        &hints,
                        &local_res);
    count += 1;
    if (error == EAI_AGAIN) {
      if (debug) {
        fprintf(stderr,
		"%s: Sleeping on getaddrinfo EAI_AGAIN\n",
		__FUNCTION__);
        fflush(stderr);
      }
      sleep(1);
    }
  } while ((error == EAI_AGAIN) && (count <= 5));

  if (error) {
    if (debug) {

      fprintf(stderr,
	      "%s: could not resolve remote '%s' port '%s' af %d\n"
	      "\tgetaddrinfo returned %s (%d)\n",
	      __FUNCTION__,
	      hostname,
	      port,
	      af,
	      gai_strerror(error),
	      error);

    }
    return;
  }

  if (debug) {
    dump_addrinfo(stderr, local_res, hostname, port, af);
  }

  local_res_temp = local_res;

  while (local_res_temp != NULL) {

    temp_socket = socket(local_res_temp->ai_family,SOCK_STREAM,0);

    if (temp_socket == INVALID_SOCKET) {
      if (debug) {
	fprintf(stderr,
		"%s could not allocate a socket: %s (errno %d)\n",
		__FUNCTION__,
		strerror(errno),
		errno);
	fflush(stderr);
      }
      local_res_temp = local_res_temp->ai_next;
      continue;
    }

    /* happiness and joy, keep going */
    if (setsockopt(temp_socket,
		   SOL_SOCKET,
		   SO_REUSEADDR,
		   (char *)&on ,
		   sizeof(on)) == SOCKET_ERROR) {
      if (debug) {
	fprintf(stderr,
		"%s: warning: could not set SO_REUSEADDR: %s (errno %d)\n",
		__FUNCTION__,
		strerror(errno),
		errno);
	fflush(stderr);
      }
    }
    /* still happy and joyful */

    if ((bind(temp_socket,
	      local_res_temp->ai_addr,
	      local_res_temp->ai_addrlen) != SOCKET_ERROR) &&
	(listen(temp_socket,1024) != SOCKET_ERROR))  {

      /* OK, now add to the list */
      temp_elt = (struct listen_elt *)malloc(sizeof(struct listen_elt));
      if (temp_elt) {
	temp_elt->fd = temp_socket;
	if (listen_list) {
	  temp_elt->next = listen_list;
	}
	else {
	  temp_elt->next = NULL;
	}
	listen_list = temp_elt;
      }
      else {
	fprintf(stderr,
		"%s: could not malloc a listen_elt\n",
		__FUNCTION__);
	fflush(stderr);
	exit(1);
      }
    }
    else {
      /* we consider a bind() or listen() failure a transient and try
	 the next address */
      if (debug) {
	fprintf(stderr,
		"%s: warning: bind or listen call failure: %s (errno %d)\n",
		__FUNCTION__,
		strerror(errno),
		errno);
	fflush(stderr);
      }
      close(temp_socket);
    }
    local_res_temp = local_res_temp->ai_next;
  }

}

void
setup_listens(char name[], char port[], int af) {

  int do_inet;
  int no_name = 0;
#ifdef AF_INET6
  int do_inet6;
#endif

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }


  if (strcmp(name,"") == 0) {
    no_name = 1;
    switch (af) {
    case AF_UNSPEC:
      do_inet = 1;
#ifdef AF_INET6
      do_inet6 = 1;
#endif
      break;
    case AF_INET:
      do_inet = 1;
#ifdef AF_INET6
      do_inet6 = 0;
#endif
      break;
#ifdef AF_INET6
    case AF_INET6:
      do_inet = 0;
      do_inet6 = 1;
      break;
#endif
    default:
      do_inet = 1;
    }
    /* if we have IPv6, try that one first because it may be a superset */
#ifdef AF_INET6
    if (do_inet6)
      create_listens("::0",port,AF_INET6);
#endif
    if (do_inet)
      create_listens("0.0.0.0",port,AF_INET);
  }
  else {
    create_listens(name,port,af);
  }

  if (listen_list) {
    fprintf(stdout,
	    "Starting netserver with host '%s' port '%s' and family %s\n",
	    (no_name) ? "IN(6)ADDR_ANY" : name,
	    port,
	    inet_ftos(af));
    fflush(stdout);
  }
  else {
    fprintf(stderr,
	    "Unable to start netserver with  '%s' port '%s' and family %s\n",
	    (no_name) ? "IN(6)ADDR_ANY" : name,
	    port,
	    inet_ftos(af));
    fflush(stderr);
    exit(1);
  }
}

SOCKET
set_fdset(struct listen_elt *list, fd_set *fdset) {

  struct listen_elt *temp;
  SOCKET max = INVALID_SOCKET;

  FD_ZERO(fdset);

  temp = list;

  if (debug) {
    fprintf(where,
	    "%s: enter list %p fd_set %p\n",
	    __FUNCTION__,
	    list,
	    fdset);
    fflush(where);
  }

  while (temp) {
    if (temp->fd > max)
      max = temp->fd;

    if (debug) {
      fprintf(where,
	      "setting %d in fdset\n",
	      temp->fd);
      fflush(where);
    }

    FD_SET(temp->fd,fdset);

    temp = temp->next;
  }

  return max;

}

void
close_listens(struct listen_elt *list) {
  struct listen_elt *temp;

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }

  temp = list;

  while (temp) {
    close(temp->fd);
    temp = temp->next;
  }
}

static int
recv_passphrase() {

  /* may need to revisit the timeout. we only respond if there is an
     error with receiving the passphrase */
  if ((recv_request_timed_n(0,20) > 0) &&
      (netperf_request.content.request_type == PASSPHRASE) &&
      (!strcmp(passphrase,
	       (char *)netperf_request.content.test_specific_data))) {
    /* it was okey dokey */
    return 0;
  }
#if defined(SEND_PASSPHRASE_RESPONSE)
  netperf_response.content.response_type = PASSPHRASE;
  netperf_response.content.serv_errno = 403;
    snprintf((char *)netperf_response.content.test_specific_data,
	     sizeof(netperf_response.content.test_specific_data),
	     "Sorry, unable to match with required passphrase\n");
  send_response_n(0);
#endif
  fprintf(where,
	  "Unable to match required passphrase.  Closing control connection\n");
  fflush(where);

  close(server_sock);
  return -1;
}

/* This routine implements the "main event loop" of the netperf server
   code. Code above it will have set-up the control connection so it
   can just merrily go about its business, which is to "schedule"
   performance tests on the server.  */

void
process_requests()
{

  float	temp_rate;

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }

  /* if the netserver was started with a passphrase, look for it in
     the first request to arrive.  if there is no passphrase in the
     first request we will end-up dumping the control connection. raj
     2012-01-23 */

  if ((passphrase != NULL)  && (recv_passphrase()))
      return;

  while (1) {

    if (recv_request() <= 0) {
      close(server_sock);
      return;
    }

    switch (netperf_request.content.request_type) {

    case DEBUG_ON:
      netperf_response.content.response_type = DEBUG_OK;
      if (!suppress_debug) {
	debug++;

	if (debug == 1) {
	  /* we just flipped-on debugging, dump the request because
	     recv_request/recv_request_n will not have dumped it as its
	     dump_request() call is conditional on debug being set. raj
	     2011-07-08 */
	  dump_request();
	}
      }

      send_response();
      break;

    case DEBUG_OFF:
      if (debug)
	debug--;
      netperf_response.content.response_type = DEBUG_OK;
      send_response();
      /* we used to take the trouble to close the debug file, but SAF
	 asked a good question when he asked "Why?" and since I cannot
	 think of a good reason, I have removed the code. raj
	 2011-07-08 */
      break;

    case DO_SYSINFO:
      {
	netperf_response.content.response_type = SYSINFO_RESPONSE;

	snprintf((char *)netperf_response.content.test_specific_data,
		 sizeof(netperf_response.content.test_specific_data),
		 "%c%s%c%s%c%s%c%s",
		 ',',
		 "Deprecated",
		 ','
,		 "Deprecated",
		 ',',
		 "Deprecated",
		 ',',
		 "Deprecated");

	send_response_n(0);
	break;
      }

    case CPU_CALIBRATE:
      netperf_response.content.response_type = CPU_CALIBRATE;
      temp_rate = calibrate_local_cpu(0.0);
      bcopy((char *)&temp_rate,
	    (char *)netperf_response.content.test_specific_data,
	    sizeof(temp_rate));
      bcopy((char *)&lib_num_loc_cpus,
	    (char *)netperf_response.content.test_specific_data +
	            sizeof(temp_rate),
	    sizeof(lib_num_loc_cpus));
      if (debug) {
	fprintf(where,
		"netserver: sending CPU information: rate is %g num cpu %d\n",
		temp_rate,
		lib_num_loc_cpus);
	fflush(where);
      }

      /* we need the cpu_start, cpu_stop in the looper case to kill
         the child proceses raj 7/95 */

#ifdef USE_LOOPER
      cpu_start(1);
      cpu_stop(1,&temp_rate);
#endif /* USE_LOOPER */

      send_response();
      break;

    case DO_TCP_STREAM:
      recv_tcp_stream();
      break;

    case DO_TCP_MAERTS:
      recv_tcp_maerts();
      break;

    case DO_TCP_RR:
      recv_tcp_rr();
      break;

    case DO_TCP_CRR:
      recv_tcp_conn_rr();
      break;

    case DO_TCP_CC:
      recv_tcp_cc();
      break;

#ifdef DO_1644
    case DO_TCP_TRR:
      recv_tcp_tran_rr();
      break;
#endif /* DO_1644 */

#ifdef DO_NBRR
    case DO_TCP_NBRR:
      recv_tcp_nbrr();
      break;
#endif /* DO_NBRR */

    case DO_UDP_STREAM:
      recv_udp_stream();
      break;

    case DO_UDP_RR:
      recv_udp_rr();
      break;

#ifdef WANT_DLPI

    case DO_DLPI_CO_RR:
      recv_dlpi_co_rr();
      break;

    case DO_DLPI_CL_RR:
      recv_dlpi_cl_rr();
      break;

    case DO_DLPI_CO_STREAM:
      recv_dlpi_co_stream();
      break;

    case DO_DLPI_CL_STREAM:
      recv_dlpi_cl_stream();
      break;

#endif /* WANT_DLPI */

#ifdef WANT_UNIX

    case DO_STREAM_STREAM:
      recv_stream_stream();
      break;

    case DO_STREAM_RR:
      recv_stream_rr();
      break;

    case DO_DG_STREAM:
      recv_dg_stream();
      break;

    case DO_DG_RR:
      recv_dg_rr();
      break;

#endif /* WANT_UNIX */

#ifdef WANT_XTI
    case DO_XTI_TCP_STREAM:
      recv_xti_tcp_stream();
      break;

    case DO_XTI_TCP_RR:
      recv_xti_tcp_rr();
      break;

    case DO_XTI_UDP_STREAM:
      recv_xti_udp_stream();
      break;

    case DO_XTI_UDP_RR:
      recv_xti_udp_rr();
      break;

#endif /* WANT_XTI */

#ifdef WANT_SCTP
    case DO_SCTP_STREAM:
      recv_sctp_stream();
      break;

    case DO_SCTP_STREAM_MANY:
      recv_sctp_stream_1toMany();
      break;

    case DO_SCTP_RR:
      recv_sctp_rr();
      break;

    case DO_SCTP_RR_MANY:
      recv_sctp_rr_1toMany();
      break;
#endif

#ifdef WANT_SDP
    case DO_SDP_STREAM:
      recv_sdp_stream();
      break;

    case DO_SDP_MAERTS:
      recv_sdp_maerts();
      break;

    case DO_SDP_RR:
      recv_sdp_rr();
      break;
#endif

#ifdef WANT_OMNI
    case DO_OMNI:
      recv_omni();
      break;
#endif

    case PASSPHRASE:
      if (debug) {
	fprintf(where,"Ignoring an unexpected passphrase control message\n");
	fflush(where);
      }
      break;

    default:
      fprintf(where,"unknown test number %d\n",
	      netperf_request.content.request_type);
      fflush(where);
      netperf_response.content.serv_errno=998;
      send_response();
      break;

    }
  }
}

/* the routine we call when we are going to spawn/fork/whatnot a child
   process from the parent netserver daemon. raj 2011-07-08 */
void
spawn_child() {

#if defined(HAVE_FORK)

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }


  /* flush the usual suspects */
  fflush(stdin);
  fflush(stdout);
  fflush(stderr);
  fflush(where);

  signal(SIGCLD,SIG_IGN);

  switch (fork()) {
  case -1:
    fprintf(where,
	    "%s: fork() error %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);

  case 0:
    /* we are the child, but not of inetd.  we don't know if we are
       the child of a daemonized parent or not, so we still need to
       worry about the standard file descriptors. raj 2011-07-11 */

    close_listens(listen_list);
    open_debug_file();

    child = 1;
    netperf_daemon = 0;
    process_requests();
    exit(0);
    break;

  default:
    /* we are the parent, not a great deal to do here, but we may
       want to reap some children */
#if !defined(HAVE_SETSID)
    /* Only call "waitpid()" if "setsid()" is not used. */
    while(waitpid(-1, NULL, WNOHANG) > 0) {
      if (debug) {
	fprintf(where,
		"%s: reaped a child process\n",
		__FUNCTION__);
      }
    }
#endif
    break;
  }

#elif defined(WIN32)

  BOOL b;
  char *cmdline;
  int cmdline_length;
  int cmd_index;
  PROCESS_INFORMATION pi;
  STARTUPINFO si;
  int i;

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }


  /* create the cmdline array based on strlen(program) + 80 chars */
  cmdline_length = strlen(program) + 80;
  cmdline = malloc(cmdline_length + 1);  // +1 for trailing null

  memset(&si, 0 , sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);

  /* Pass the server_sock as stdin for the new process.  Hopefully
     this will continue to be created with the OBJ_INHERIT
     attribute. */
  si.hStdInput = (HANDLE)server_sock;
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;

  /* Build cmdline for child process */
  strcpy(cmdline, program);
  cmd_index = strlen(cmdline);
  if (verbosity > 1) {
    cmd_index += snprintf(&cmdline[cmd_index],
			  cmdline_length - cmd_index,
			  " -v %d",
			  verbosity);
  }
  for (i=0; i < debug; i++) {
    cmd_index += snprintf(&cmdline[cmd_index],
			  cmdline_length - cmd_index,
			  " -d");
  }
  cmd_index += snprintf(&cmdline[cmd_index],
			cmdline_length - cmd_index,
			" -I %x",
			(int)(UINT_PTR)server_sock);

  /* are these -i settings even necessary? the command line scanning
     does not seem to do anything with them */
  cmd_index += snprintf(&cmdline[cmd_index],
			cmdline_length - cmd_index,
			" -i %x",
			(int)(UINT_PTR)server_control);
  cmd_index += snprintf(&cmdline[cmd_index],
			cmdline_length - cmd_index,
			" -i %x",
			(int)(UINT_PTR)where);

  b = CreateProcess(NULL,    /* Application Name */
		    cmdline,
		    NULL,    /* Process security attributes */
		    NULL,    /* Thread security attributes */
		    TRUE,    /* Inherit handles */
		    0,       /* Creation flags
				PROCESS_QUERY_INFORMATION,  */
		    NULL,    /* Enviornment */
		    NULL,    /* Current directory */
		    &si,     /* StartupInfo */
		    &pi);
  if (!b)
    {
      perror("CreateProcessfailure: ");
      free(cmdline); /* even though we exit :) */
      exit(1);
    }

  /* We don't need the thread or process handles any more;
     let them go away on their own timeframe. */

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  /* the caller/parent will close server_sock */

  free(cmdline);

#else

  fprintf(where,
	  "%s called on platform which cannot spawn children\n",
	  __FUNCTION__);
  fflush(where);
  exit(1);

#endif /* HAVE_FORK */
}

void
accept_connection(SOCKET listen_fd) {

  struct sockaddr_storage peeraddr;
  netperf_socklen_t peeraddrlen;
#if defined(SO_KEEPALIVE)
  int on = 1;
#endif

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }

  peeraddrlen = sizeof(peeraddr);

  /* while server_control is only used by the WIN32 path, but why
     bother ifdef'ing it?  and besides, do we *really* need knowledge
     of server_control in the WIN32 case? do we have to tell the
     child about *all* the listen endpoints? raj 2011-07-08 */
  server_control = listen_fd;

  if ((server_sock = accept(listen_fd,
			   (struct sockaddr *)&peeraddr,
			    &peeraddrlen)) == INVALID_SOCKET) {
    fprintf(where,
	    "%s: accept failure: %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(where);
    exit(1);
  }

#if defined(SO_KEEPALIVE)
  /* we are not terribly concerned if this does not work, it is merely
     duct tape added to belts and suspenders. raj 2011-07-08 */
  setsockopt(server_sock,
	     SOL_SOCKET,
	     SO_KEEPALIVE,
	     (const char *)&on,
	     sizeof(on));
#endif

  if (spawn_on_accept) {
    spawn_child();
    /* spawn_child() only returns when we are the parent */
    close(server_sock);
  }
  else {
    process_requests();
  }
}

void
accept_connections() {

  fd_set read_fds, write_fds, except_fds;
  SOCKET high_fd, candidate;
  int num_ready;

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }

  while (1) {

    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);
    high_fd = set_fdset(listen_list,&read_fds);

#if !defined(WIN32)
    num_ready = select(high_fd + 1,
#else
    num_ready = select(1,
#endif
		       &read_fds,
		       &write_fds,
		       &except_fds,
		       NULL);

    if (num_ready < 0) {
      fprintf(where,
	      "%s: select failure: %s (errno %d)\n",
	      __FUNCTION__,
	      strerror(errno),
	      errno);
      fflush(where);
      exit(1);
    }

    /* try to keep things simple */
    candidate = 0;
    while ((num_ready) && (candidate <= high_fd)) {
      if (FD_ISSET(candidate,&read_fds)) {
	accept_connection(candidate);
	FD_CLR(candidate,&read_fds);
	num_ready--;
      }
      else {
	candidate++;
      }
    }
  }
}

#ifndef WIN32
#define SERVER_ARGS "DdfhL:n:Np:v:VZ:46"
#else
#define SERVER_ARGS "DdfhL:n:Np:v:VZ:46I:i:"
#endif
void
scan_netserver_args(int argc, char *argv[]) {

  int c;
  char arg1[BUFSIZ], arg2[BUFSIZ];

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }

  while ((c = getopt(argc, argv, SERVER_ARGS)) != EOF){
    switch (c) {
    case '?':
    case 'h':
      print_netserver_usage();
      exit(1);
    case 'd':
      debug++;
      suppress_debug = 0;
      break;
    case 'D':
      /* perhaps one of these days we'll take an argument */
      want_daemonize = 0;
      not_inetd = 1;
      break;
    case 'f':
      spawn_on_accept = 0;
      not_inetd = 1;
      break;
#ifdef WIN32
    case 'I':
      child = TRUE;
      break;
    case 'i':
      break;
#endif
    case 'L':
      not_inetd = 1;
      break_args_explicit(optarg,arg1,arg2);
      if (arg1[0]) {
	strncpy(local_host_name,arg1,sizeof(local_host_name));
      }
      if (arg2[0]) {
	local_address_family = parse_address_family(arg2);
      }
      break;
    case 'n':
      shell_num_cpus = atoi(optarg);
      if (shell_num_cpus > MAXCPUS) {
	fprintf(stderr,
		"netserver: This version can only support %d CPUs. Please"
		"increase MAXCPUS in netlib.h and recompile.\n",
		MAXCPUS);
	fflush(stderr);
	exit(1);
      }
      break;
    case 'N':
      suppress_debug = 1;
      debug = 0;
      break;
    case 'p':
      /* we want to open a listen socket at a specified port number */
      strncpy(listen_port,optarg,sizeof(listen_port));
      not_inetd = 1;
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
    case '4':
      local_address_family = AF_INET;
      break;
    case '6':
#if defined(AF_INET6)
      local_address_family = AF_INET6;
#else
      local_address_family = AF_UNSPEC;
#endif
      break;
    case 'v':
      /* say how much to say */
      verbosity = atoi(optarg);
      break;
    case 'V':
      printf("Netperf version %s\n",NETPERF_VERSION);
      exit(0);
      break;

    }
  }
}

void
daemonize() {
#if defined(HAVE_FORK)

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }

  /* flush the usual suspects */
  fflush(stdin);
  fflush(stdout);
  fflush(stderr);

  switch (fork()) {
  case -1:
    fprintf(stderr,
	    "%s: fork() error %s (errno %d)\n",
	    __FUNCTION__,
	    strerror(errno),
	    errno);
    fflush(stderr);
    exit(1);
  case 0:

    /* perhaps belt and suspenders, but if we dump core, perhaps
       better to do so here. we won't worry about the call being
       successful though. raj 2011-07-08 */
    chdir(DEBUG_LOG_FILE_DIR);

    /* we are the child. we should get a new "where" to match our new
       pid */

    open_debug_file();

#ifdef HAVE_SETSID
      setsid();
#else
      setpgrp();
#endif /* HAVE_SETSID */

      signal(SIGCLD, SIG_IGN);

      /* ok, we can start accepting control connections now */
      accept_connections();

  default:
    /* we are the parent, nothing to do but exit? */
    exit(0);
  }

#else
  fprintf(where,
	  "%s called on platform which cannot daemonize\n",
	  __FUNCTION__);
  fflush(where);
  exit(1);
#endif /* HAVE_FORK */
}

static void
check_if_inetd() {

  if (debug) {
    fprintf(where,
	    "%s: enter\n",
	    __FUNCTION__);
    fflush(where);
  }

  if (not_inetd) {
    return;
  }
  else {
#if !defined(WIN32) && !defined(__VMS)
    struct sockaddr_storage name;
    netperf_socklen_t namelen;

    namelen = sizeof(name);
    if (getsockname(0,
		    (struct sockaddr *)&name,
		    &namelen) == SOCKET_ERROR) {
      not_inetd = 1;
    }
    else {
      not_inetd = 0;
      child = 1;
    }
#endif
  }
}

/* OK, so how does all this work you ask?  Well, we are in a maze of
   twisty options, all different.  Netserver can be invoked as a child
   of inetd or the VMS auxiliary server process, or a parent netserver
   process. In those cases, we could/should follow the "child"
   path. However, there are really two "child" paths through the
   netserver code.

   When this netserver is a child of a parent netserver in the
   case of *nix, the child process will be created by a
   spawn_child_process() in accept_connections() and will not hit the
   "child" path here in main().

   When this netserver is a child of a parent netserver in the case of
   windows, the child process will have been spawned via a
   Create_Process() call in spawn_child_process() in
   accept_connections, but will flow through here again. We rely on
   the scan_netserver_args() call to have noticed the magic option
   that tells us we are a child process.

   When this netserver is launched from the command line we will first
   set-up the listen endpoint(s) for the controll connection.  At that
   point we decide if we want to and can become our own daemon, or
   stay attached to the "terminal."  When this happens under *nix, we
   will again take a fork() path via daemonize() and will not come
   back through main().  If we ever learn how to become our own daemon
   under Windows, we will undoubtedly take a Create_Process() path
   again and will come through main() once again - that is what the
   "daemon" case here is all about.

   It is hoped that this is all much clearer than the old spaghetti
   code that netserver had become.  raj 2011-07-11 */


int _cdecl
main(int argc, char *argv[]) {

#ifdef WIN32
  WSADATA	wsa_data ;

  /* Initialize the winsock lib do we still want version 2.2? */
  if ( WSAStartup(MAKEWORD(2,2), &wsa_data) == SOCKET_ERROR ){
    printf("WSAStartup() failed : %lu\n", GetLastError()) ;
    return -1 ;
  }
#endif /* WIN32 */

  /* Save away the program name */
  program = (char *)malloc(strlen(argv[0]) + 1);
  if (program == NULL) {
    printf("malloc for program name failed!\n");
    return -1 ;
  }
  strcpy(program, argv[0]);

  init_netserver_globals();

  netlib_init();

  strncpy(local_host_name,"",sizeof(local_host_name));
  local_address_family = AF_UNSPEC;
  strncpy(listen_port,TEST_PORT,sizeof(listen_port));

  scan_netserver_args(argc, argv);

  check_if_inetd();

  if (child) {
    /* we are the child of either an inetd or parent netserver via
       spawning (Windows) rather than fork()ing. if we were fork()ed
       we would not be coming through this way. set_server_sock() must
       be called before open_debug_file() or there is a chance that
       we'll toast the descriptor when we do not wish it. */
    set_server_sock();
    open_debug_file();
    process_requests();
  }
  else if (daemon_parent) {
    /* we are the parent daemonized netserver
       process. accept_connections() will decide if we want to spawn a
       child process */
    accept_connections();
  }
  else {
    /* we are the top netserver process, so we have to create the
       listen endpoint(s) and decide if we want to daemonize */
    setup_listens(local_host_name,listen_port,local_address_family);
    if (want_daemonize) {
      daemonize();
    }
    accept_connections();
  }

  unlink_empty_debug_file();

#ifdef WIN32
  WSACleanup();
#endif

  return 0;

}
