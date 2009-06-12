/*
 * Copyright (c) 1999 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the Kungliga Tekniska
 *      Högskolan and its contributors.
 *
 * 4. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id: inet_ntop.c,v 1.7 2003/11/16 09:36:50 guy Exp $ */

#ifndef lint
static const char rcsid[] =
     "@(#) $Header: /tcpdump/master/tcpdump/missing/inet_ntop.c,v 1.7 2003/11/16 09:36:50 guy Exp $";
#endif

/* we aren't tcpdump :) */
#ifdef notdef
#include <tcpdump-stdinc.h>
#endif

#include <stdio.h>
#include <sys/types.h>

#ifndef WIN32
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#else   /* WIN32 */
#include <time.h>
#include <winsock2.h>
#ifdef DO_IPV6
#include <ws2tcpip.h>
#endif  /* DO_IPV6 */
#include <windows.h>

/* The below are copied from netlib.h */
#ifdef errno
/* delete the one from stdlib.h  */
/*#define errno       (*_errno()) */
#undef errno
#endif
#define errno GetLastError()
#define Set_errno(num) SetLastError((num))

/* INVALID_SOCKET == INVALID_HANDLE_VALUE == (unsigned int)(~0) */
/* SOCKET_ERROR == -1 */
#define ENOTSOCK WSAENOTSOCK
#define EINTR    WSAEINTR
#define ENOBUFS  WSAENOBUFS
#define EWOULDBLOCK           WSAEWOULDBLOCK
#define EAFNOSUPPORT  WSAEAFNOSUPPORT
/* from public\sdk\inc\crt\errno.h */
#define ENOSPC          28
#endif  /* WIN32 */

/*
 *
 */

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN    16
#endif

static const char *
inet_ntop_v4 (const void *src, char *dst, size_t size)
{
    const char digits[] = "0123456789";
    int i;
    struct in_addr *addr = (struct in_addr *)src;
    u_long a = ntohl(addr->s_addr);
    const char *orig_dst = dst;

    if (size < INET_ADDRSTRLEN) {
      Set_errno(ENOSPC);
      return NULL;
    }
    for (i = 0; i < 4; ++i) {
	int n = (a >> (24 - i * 8)) & 0xFF;
	int non_zerop = 0;

	if (non_zerop || n / 100 > 0) {
	    *dst++ = digits[n / 100];
	    n %= 100;
	    non_zerop = 1;
	}
	if (non_zerop || n / 10 > 0) {
	    *dst++ = digits[n / 10];
	    n %= 10;
	    non_zerop = 1;
	}
	*dst++ = digits[n];
	if (i != 3)
	    *dst++ = '.';
    }
    *dst++ = '\0';
    return orig_dst;
}

const char *
inet_ntop(int af, const void *src, char *dst, size_t size)
{
    switch (af) {
    case AF_INET :
	return inet_ntop_v4 (src, dst, size);
    default :
      Set_errno(EAFNOSUPPORT);
      return NULL;
    }
}
