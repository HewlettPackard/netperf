/*
        Copyright (C) 1993, Hewlett-Packard Company
*/

 /* This file contains the test-specific definitions for netperf's BSD */
 /* sockets tests */

#define PAD_TIME 2

struct	hippi_stream_request_struct {
  int	rem_recv_bufs;
  int	message_size;
  int	recv_alignment;
  int	recv_offset;
  int	measure_cpu;
  float	cpu_rate;
  int	test_length;
  int   dev_name_len;   /* the length of the device name string. this */
			/* is used to put it into the proper order on */
			/* @#$% byte-swapped boxes... */
  char  hippi_device[32]; /* the path to the dlpi device */
  int   client_sap;
  int   server_sap;
  int   recv_flow_control;
  unsigned char  mac_addr[6];
};

struct	hippi_stream_response_struct {
  int	num_recv_bufs;
  int	measure_cpu;
  int	test_length;
  float	cpu_rate;
  int   client_sap;
  int   server_sap;
  int   recv_flow_control;
  unsigned char  mac_addr[6];
};

struct	hippi_stream_results_struct {
  int	messages_recvd;
  int	bytes_received;
  float	elapsed_time;
  float	cpu_util;
  int   cpu_method;
  int   num_cpus;
};


struct	hippi_rr_request_struct {
  int	num_recv_bufs;	/* how big does the client want it	*/
  int	recv_alignment;
  int	recv_offset;
  int	send_alignment;
  int	send_offset;
  int	request_size;
  int	response_size;
  int	measure_cpu;	/* does the client want server cpu	*/
  float	cpu_rate;	/* do we know how fast the cpu is?	*/
  int	test_length;	/* how long is the test?		*/
  int   dev_name_len;   /* the length of the device name string. this */
			/* is used to put it into the proper order on */
			/* @#$% byte-swapped boxes... */
  char  hippi_device[32]; /* the path to the dlpi device */
  int   client_sap;
  int   server_sap;
  int   recv_flow_control;
  unsigned char  mac_addr[6];
};

struct	hippi_rr_response_struct {
  int	num_recv_bufs;	/* how big does the client want it	*/
  int	measure_cpu;	/* does the client want server cpu	*/
  int	test_length;	/* how long is the test?		*/
  float	cpu_rate;	/* could we measure	*/
  int   client_sap;
  int   server_sap;
  int   recv_flow_control;
  unsigned char  mac_addr[6];
};

struct hippi_rr_results_struct {
  int	bytes_received;	/* ignored initially */
  int	recv_calls;	/* ignored initially */
  int	trans_received;	/* not ignored  */
  float	elapsed_time;	/* how long the test ran */
  float	cpu_util;	/* -1 if not measured */
  float	serv_dem;	/* -1 if not measured */
  int   cpu_method;
  int   num_cpus;
};

