/*
        Copyright (C) 1993, Hewlett-Packard Company
*/

 /* This file contains the test-specific definitions for netperf's BSD */
 /* sockets tests */

#define PAD_TIME 2

struct	fore_stream_request_struct {
  int	recv_buf_size;
  int	message_size;
  int	recv_alignment;
  int	recv_offset;
  int	checksum_off;
  int	measure_cpu;
  float	cpu_rate;
  int	test_length;
  int	aal;
  int   dev_name_len;   /* the length of the device name string. this */
			/* is used to put it into the proper order on */
			/* @#$% byte-swapped boxes... */
  char  atm_device[32]; /* the path to the dlpi device */
};

struct	fore_stream_response_struct {
  int	recv_buf_size;
  int	send_buf_size;
  int	measure_cpu;
  int	test_length;
  int	server_asap;
  float	cpu_rate;
  int	so_rcvavoid;	/* could the remote avoid receive copies? */
  int	so_sndavoid;	/* could the remote avoid send copies? */
};

struct	fore_stream_results_struct {
  int	messages_recvd;
  int	bytes_received;
  float	elapsed_time;
  float	cpu_util;
};


struct	fore_rr_request_struct {
  int	recv_buf_size;	/* how big does the client want it	*/
  int	send_buf_size;
  int	recv_alignment;
  int	recv_offset;
  int	send_alignment;
  int	send_offset;
  int	request_size;
  int	response_size;
  int	measure_cpu;	/* does the client want server cpu	*/
  float	cpu_rate;	/* do we know how fast the cpu is?	*/
  int	test_length;	/* how long is the test?		*/
  int   aal;
  int   dev_name_len;   /* the length of the device name string. this */
			/* is used to put it into the proper order on */
			/* @#$% byte-swapped boxes... */
  char  atm_device[32]; /* the path to the dlpi device */
};

struct	fore_rr_response_struct {
  int	recv_buf_size;	/* how big does the client want it	*/
  int	no_delay;
  int	measure_cpu;	/* does the client want server cpu	*/
  int	test_length;	/* how long is the test?		*/
  int	send_buf_size;
  int	server_asap;	/* connect to me here	*/
  float	cpu_rate;		/* could we measure	*/
  int	so_rcvavoid;	/* could the remote avoid receive copies? */
  int	so_sndavoid;	/* could the remote avoid send copies? */
};

struct fore_rr_results_struct {
  int	bytes_received;	/* ignored initially */
  int	recv_calls;	/* ignored initially */
  int	trans_received;	/* not ignored  */
  float	elapsed_time;	/* how long the test ran */
  float	cpu_util;	/* -1 if not measured */
  float	serv_dem;	/* -1 if not measured */
};

