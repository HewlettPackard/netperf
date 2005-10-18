dnl *
dnl * Copyright (c) 2001, 2003  Motoyuki Kasahara
dnl *
dnl * Redistribution and use in source and binary forms, with or without
dnl * modification, are permitted provided that the following conditions
dnl * are met:
dnl * 1. Redistributions of source code must retain the above copyright
dnl *    notice, this list of conditions and the following disclaimer.
dnl * 2. Redistributions in binary form must reproduce the above copyright
dnl *    notice, this list of conditions and the following disclaimer in the
dnl *    documentation and/or other materials provided with the distribution.
dnl * 3. Neither the name of the project nor the names of its contributors
dnl *    may be used to endorse or promote products derived from this software
dnl *    without specific prior written permission.
dnl * 
dnl * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
dnl * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
dnl * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
dnl * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORSBE
dnl * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
dnl * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
dnl * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
dnl * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
dnl * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
dnl * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
dnl * THE POSSIBILITY OF SUCH DAMAGE.
dnl *

dnl * 
dnl * Check for socklen_t.
dnl * 
AC_DEFUN([AC_TYPE_SOCKLEN_T],
[AC_CACHE_CHECK([for socklen_t], ac_cv_type_socklen_t,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>]], [[
socklen_t socklen;
]])],[ac_cv_type_socklen_t=yes],[ac_cv_type_socklen_t=no])])
if test "$ac_cv_type_socklen_t" != yes; then
    AC_DEFINE(socklen_t, int,
[Define to `int' if <sys/types.h> or <sys/socket.h> does not define.])
fi])

dnl * 
dnl * Check for in_port_t.
dnl * 
AC_DEFUN([AC_TYPE_IN_PORT_T],
[AC_CACHE_CHECK([for in_port_t], ac_cv_type_in_port_t,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>]], [[
in_port_t in_port;
]])],[ac_cv_type_in_port_t=yes],[ac_cv_type_in_port_t=no])])
if test "$ac_cv_type_in_port_t" != yes; then
    ac_cv_sin_port_size=unknown
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    int main() {
	struct sockaddr_in addr;
	return (sizeof(addr.sin_port) == sizeof(long)) ? 0 : 1;
    }
    ]])],[ac_cv_sin_port_size=long],[],[])
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    int main() {
	struct sockaddr_in addr;
	return (sizeof(addr.sin_port) == sizeof(int)) ? 0 : 1;
    }
    ]])],[ac_cv_sin_port_size=int],[],[])
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    int main() {
	struct sockaddr_in addr;
	return (sizeof(addr.sin_port) == sizeof(short)) ? 0 : 1;
    }
    ]])],[ac_cv_sin_port_size=short],[],[])
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    int main() {
	struct sockaddr_in addr;
	return (sizeof(addr.sin_port) == sizeof(char)) ? 0 : 1;
    }
    ]])],[ac_cv_sin_port_size=char],[],[])
    if test "$ac_cv_sin_port_size" = unknown; then
	AC_MSG_ERROR([Failed to get size of sin_port in struct sockaddr_in.])
    fi
    AC_DEFINE_UNQUOTED(in_port_t, unsigned $ac_cv_sin_port_size,
[Define to `unsigned char', `unsigned short', `unsigned int' or
`unsigned long' according with size of `sin_port' in `struct sockaddr_in',
if <sys/types.h>, <sys/socket.h> or <netinet/in.h> does not define
`in_port_t'.])
fi])

dnl * 
dnl * Check for sa_family_t.
dnl * 
AC_DEFUN([AC_TYPE_SA_FAMILY_T],
[AC_CACHE_CHECK([for sa_family_t], ac_cv_type_sa_family_t,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>]], [[
sa_family_t sa_family;
]])],[ac_cv_type_sa_family_t=yes],[ac_cv_type_sa_family_t=no])])
if test "$ac_cv_type_sa_family_t" != yes; then
    ac_cv_sa_family_size=unknown
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    int main() {
	struct sockaddr addr;
	return (sizeof(addr.sa_family) == sizeof(long)) ? 0 : 1;
    }
    ]])],[ac_cv_sa_family_size=long],[],[])
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    int main() {
	struct sockaddr addr;
	return (sizeof(addr.sa_family) == sizeof(int)) ? 0 : 1;
    }
    ]])],[ac_cv_sa_family_size=int],[],[])
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    int main() {
	struct sockaddr addr;
	return (sizeof(addr.sa_family) == sizeof(short)) ? 0 : 1;
    }
    ]])],[ac_cv_sa_family_size=short],[],[])
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
    #include <sys/types.h>
    #include <sys/socket.h>
    int main() {
	struct sockaddr addr;
	return (sizeof(addr.sa_family) == sizeof(char)) ? 0 : 1;
    }
    ]])],[ac_cv_sa_family_size=char],[],[])
    if test "$ac_cv_sa_family_size" = unknown; then
	AC_MSG_ERROR([Failed to get size of sa_family in struct sockaddr.])
    fi
    AC_DEFINE_UNQUOTED(sa_family_t, unsigned $ac_cv_sa_family_size,
[Define to `unsigned char', `unsigned short', `unsigned int' or
`unsigned long' according with size of `sa_family' in `struct sockaddr',
if <sys/types.h> or <sys/socket.h> does not define `sa_family_t'.])
fi])
