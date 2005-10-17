
/*
 
              Copyright (C) 1993 Hewlett-Packard Company
                         ALL RIGHTS RESERVED.
 
  The enclosed software and documention includes copyrighted works of
  Hewlett-Packard Co. For as long as you comply with the following
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
  
  5.  HEWLETT-PACKARD COMPANY WILL NOT IN ANY EVENT BE LIABLE FOT ANY
      DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES
      (INCLUDING LOST PROFITS) RELATED TO ANY USE, REPRODUCTION,
      MODIFICATION, OR DISTRIBUTION OF THE SOFTWARE OR DOCUMENTATION.
 
*/
char	netperf_id[]="@(#)netperf.c (c) Copyright 1993, \
Hewlett-Packard Company.	Version 1.9";

#include <stdio.h>
#include "netsh.h"

 /* this file contains the main for the netperf program. all the other */
 /* routines can be found in the file netsh.c */


int
main(argc,argv)
int	argc;
char	*argv[];

{

netlib_init();
set_defaults();
scan_cmd_line(argc,argv);

if (debug) {
	dump_globals();
}

if (debug) {
	printf("remotehost is %s and port %d\n",host_name,test_port);
	fflush(stdout);
}


establish_control(host_name,test_port);

if (strcmp(test_name,"TCP_STREAM") == 0) {
	send_tcp_stream(host_name);
}
else if (strcmp(test_name,"TCP_RR") == 0) {
	send_tcp_rr(host_name);
}
else if (strcmp(test_name,"TCP_CRR") == 0) {
	send_tcp_conn_rr(host_name);
}
else if (strcmp(test_name,"UDP_STREAM") == 0) {
	send_udp_stream(host_name);
}
else if (strcmp(test_name,"UDP_RR") == 0) {
	send_udp_rr(host_name);
}
else if (strcmp(test_name,"LOC_CPU") == 0) {
	loc_cpu_rate();
}
else if (strcmp(test_name,"REM_CPU") == 0) {
	rem_cpu_rate();
}
#ifdef DO_DLPI
else if (strcmp(test_name,"DLCO_RR") == 0) {
	send_dlpi_co_rr();
}
else if (strcmp(test_name,"DLCL_RR") == 0) {
	send_dlpi_cl_rr();
}
else if (strcmp(test_name,"DLCO_STREAM") == 0) {
	send_dlpi_co_stream();
}
else if (strcmp(test_name,"DLCL_STREAM") == 0) {
	send_dlpi_cl_stream();
}
#endif /* DO_DLPI */
#ifdef DO_UNIX
else if (strcmp(test_name,"STREAM_RR") == 0) {
	send_stream_rr();
}
else if (strcmp(test_name,"DG_RR") == 0) {
	send_dg_rr();
}
else if (strcmp(test_name,"STREAM_STREAM") == 0) {
	send_stream_stream();
}
else if (strcmp(test_name,"DG_STREAM") == 0) {
	send_dg_stream();
}
#endif /* DO_UNIX */
#ifdef DO_FORE
else if (strcmp(test_name,"FORE_STREAM") == 0) {
	send_fore_stream(host_name);
}
else if (strcmp(test_name,"FORE_RR") == 0) {
	send_fore_rr(host_name);
}
#endif /* DO_FORE */
else {
	printf("The test you requested is unknown to this netperf.\n");
	printf("Please verify that you have the correct test name, \n");
	printf("and that test family has been compiled into this netperf.\n");
	exit(1);
}
return(0);
}


