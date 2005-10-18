/*
	Copyright (C) 1993, Hewlett-Packard Company
*/

/* library routine specifc defines					*/
#define		MAXSPECDATA	14	/* how many ints worth of data	*/
                                        /* can tests send...		*/
#define		MAXTIMES	4	/* how many times may we loop	*/
                                        /* to calibrate			*/
#define		MAXMESSAGESIZE	65536
#define		MAXALIGNMENT	16384
#define		MAXOFFSET	 4096
#define		DATABUFFERLEN	MAXMESSAGESIZE+MAXALIGNMENT+MAXOFFSET

#define		DEBUG_ON		1
#define		DEBUG_OFF		2
#define		DEBUG_OK		3
#define		NODE_IDENTIFY		4
#define		CPU_CALIBRATE		5

#define		DO_TCP_STREAM		10
#define		TCP_STREAM_RESPONSE	11
#define		TCP_STREAM_RESULTS	12

#define		DO_TCP_RR		13
#define		TCP_RR_RESPONSE		14
#define		TCP_RR_RESULTS		15

#define		DO_UDP_STREAM		16
#define		UDP_STREAM_RESPONSE	17
#define		UDP_STREAM_RESULTS	18

#define		DO_UDP_RR		19
#define		UDP_RR_RESPONSE		20
#define		UDP_RR_RESULTS		21

#define		DO_DLPI_CO_STREAM	22
#define		DLPI_CO_STREAM_RESPONSE	23
#define		DLPI_CO_STREAM_RESULTS	24

#define		DO_DLPI_CO_RR		25
#define		DLPI_CO_RR_RESPONSE	26
#define		DLPI_CO_RR_RESULTS	27

#define		DO_DLPI_CL_STREAM	28
#define		DLPI_CL_STREAM_RESPONSE	29
#define		DLPI_CL_STREAM_RESULTS	30

#define		DO_DLPI_CL_RR		31
#define		DLPI_CL_RR_RESPONSE	32
#define		DLPI_CL_RR_RESULTS	33

#define		DO_TCP_CRR		34
#define		TCP_CRR_RESPONSE	35
#define		TCP_CRR_RESULTS		36

#define		DO_STREAM_STREAM	37
#define		STREAM_STREAM_RESPONSE	38
#define		STREAM_STREAM_RESULTS	39

#define		DO_STREAM_RR		40
#define		STREAM_RR_RESPONSE	41
#define		STREAM_RR_RESULTS	42

#define		DO_DG_STREAM		43
#define		DG_STREAM_RESPONSE	44
#define		DG_STREAM_RESULTS	45

#define		DO_DG_RR		46
#define		DG_RR_RESPONSE		47
#define		DG_RR_RESULTS		48

#define		DO_FORE_STREAM		49
#define		FORE_STREAM_RESPONSE	50
#define		FORE_STREAM_RESULTS	51

#define		DO_FORE_RR		52
#define		FORE_RR_RESPONSE	53
#define		FORE_RR_RESULTS		54

struct netperf_request_struct {
	int	request_type;
	int	test_specific_data[MAXSPECDATA];
};

struct netperf_response_struct {
	int response_type;
	int serv_errno;
	int test_specific_data[MAXSPECDATA];
};

struct ring_elt {
  struct ring_elt *next;  /* next element in the ring */
  char *buffer_base;      /* in case we have to free it at somepoint */
  char *buffer_ptr;       /* the aligned and offset pointer */
};

#ifndef NETLIB

extern	struct netperf_request_struct *netperf_request;
extern	struct netperf_response_struct *netperf_response;

extern	char	libfmt;

extern  int     server_sock;
extern  int     times_up;

extern	FILE	*where;
extern	void	libmain();
extern	double	calc_thruput();
extern	float	calc_xfered();
extern	float	calibrate_local_cpu();
extern  float	calibrate_remote_cpu();
extern  float	calc_cpu_util();
extern	float	calc_service_demand();
extern  void    catcher();
extern  struct ring_elt *allocate_buffer_ring();
extern  int     dl_connect();
extern  int     dl_bind();
extern  int     dl_open();
#endif

 /* if your system has bcopy and bzero, include it here, otherwise, we */
 /* will try to use memcpy aand memset. fix from Bruce Barnett @ GE. */
#ifdef hpux
#define HAVE_BCOPY
#define HAVE_BZERO
#endif

#ifndef HAVE_BCOPY
#define bcopy(s,d,h) memcpy((d),(s),(h))
#endif

#ifndef HAVE_BZERO
#define bzero(p,h) memset((p),0,(h))
#endif
