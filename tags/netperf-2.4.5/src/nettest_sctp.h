/*
        Copyright (C) 1993-2003 Hewlett-Packard Company
*/

 /* This file contains the test-specific definitions for netperf's BSD */
 /* sockets tests */


struct	sctp_stream_request_struct {
  int	send_buf_size;
  int	recv_buf_size;	/* how big does the client want it - the */
			/* receive socket buffer that is */ 
  int	receive_size;   /* how many bytes do we want to receive at one */
			/* time? */ 
  int	recv_alignment; /* what is the alignment of the receive */
			/* buffer? */ 
  int	recv_offset;    /* and at what offset from that alignment? */ 
  int	no_delay;       /* do we disable the nagle algorithm for send */
			/* coalescing? */ 
  int	measure_cpu;	/* does the client want server cpu utilization */
			/* measured? */ 
  float	cpu_rate;	/* do we know how fast the cpu is already? */ 
  int	test_length;	/* how long is the test?		*/
  int	so_rcvavoid;    /* do we want the remote to avoid copies on */
			/* receives? */ 
  int	so_sndavoid;    /* do we want the remote to avoid send copies? */
  int   dirty_count;    /* how many integers in the receive buffer */
			/* should be made dirty before calling recv? */  
  int   clean_count;    /* how many integers should be read from the */
			/* recv buffer before calling recv? */ 
  int   port;		/* the to port to which recv side should bind
			   to allow netperf to run through firewalls */
  int   ipfamily;	/* address family of ipaddress */
  int   non_blocking;   /* run the test in non-blocking mode */
};

struct	sctp_stream_response_struct {
  int	recv_buf_size;	/* how big does the client want it	*/
  int	receive_size;
  int	no_delay;
  int	measure_cpu;	/* does the client want server cpu	*/
  int	test_length;	/* how long is the test?		*/
  int	send_buf_size;
  int	data_port_number;	/* connect to me here	*/
  float	cpu_rate;		/* could we measure	*/
  int	so_rcvavoid;	/* could the remote avoid receive copies? */ 
  int	so_sndavoid;	/* could the remote avoid send copies? */
  int   non_blocking;   /* run the test in non-blocking mode */
};

struct sctp_stream_results_struct {
  double         bytes_received;
  unsigned int	 recv_calls;	
  float	         elapsed_time;	/* how long the test ran */
  float	         cpu_util;	/* -1 if not measured */
  float	         serv_dem;	/* -1 if not measured */
  int            cpu_method;    /* how was cpu util measured? */
  int            num_cpus;      /* how many CPUs had the remote? */
};

struct	sctp_rr_request_struct {
  int	recv_buf_size;	/* how big does the client want it	*/
  int	send_buf_size;
  int	recv_alignment;
  int	recv_offset;
  int	send_alignment;
  int	send_offset;
  int	request_size;
  int	response_size;
  int	no_delay;
  int	measure_cpu;	/* does the client want server cpu	*/
  float	cpu_rate;	/* do we know how fast the cpu is?	*/
  int	test_length;	/* how long is the test?		*/
  int	so_rcvavoid;    /* do we want the remote to avoid receive */
			/* copies? */ 
  int	so_sndavoid;    /* do we want the remote to avoid send copies? */
  int   port;		/* the to port to which recv side should bind
			   to allow netperf to run through firewalls */
  int   ipfamily;	/* address family of ipaddress */
  int   non_blocking;   /* run the test in non-blocking mode */
};

struct	sctp_rr_response_struct {
  int	recv_buf_size;	/* how big does the client want it	*/
  int	no_delay;
  int	measure_cpu;	/* does the client want server cpu	*/
  int	test_length;	/* how long is the test?		*/
  int	send_buf_size;
  int	data_port_number;	/* connect to me here	*/
  float	cpu_rate;		/* could we measure	*/
  int	so_rcvavoid;	/* could the remote avoid receive copies? */
  int	so_sndavoid;	/* could the remote avoid send copies? */
  int   non_blocking;   /* run the test in non-blocking mode */
};

struct sctp_rr_results_struct {
  unsigned int  bytes_received;	/* ignored initially */
  unsigned int	recv_calls;	/* ignored initially */
  unsigned int	trans_received;	/* not ignored  */
  float	        elapsed_time;	/* how long the test ran */
  float	        cpu_util;	/* -1 if not measured */
  float	        serv_dem;	/* -1 if not measured */
  int           cpu_method;    /* how was cpu util measured? */
  int           num_cpus;      /* how many CPUs had the remote? */
};

#define SCTP_SNDRCV_INFO_EV		0x01
#define SCTP_ASSOC_CHANGE_EV		0x02
#define SCTP_PEERADDR_CHANGE_EV		0x04
#define SCTP_SND_FAILED_EV		0x08
#define SCTP_REMOTE_ERROR_EV		0x10
#define SCTP_SHUTDOWN_EV		0x20
#define SCTP_PD_EV			0x40
#define SCTP_ADAPT_EV			0x80

typedef enum sctp_disposition {
    SCTP_OK = 1,
    SCTP_CLOSE,
} sctp_disposition_t;

extern void send_sctp_stream();
extern void send_sctp_rr();

extern void recv_sctp_stream();
extern void recv_sctp_rr();

extern void loc_cpu_rate();
extern void rem_cpu_rate();
