
dnl This comes from libcurl's acinclude.m4.  it is not clear if this
dnl is original libcurl code, or other code, so we include the libcurl
dnl copyright here
dnl
dnl
dnl Copyright (c) 1996 - 2005, Daniel Stenberg, <daniel@haxx.se>.
dnl
dnl All rights reserved.
dnl
dnl Permission to use, copy, modify, and distribute this software for any purpose
dnl with or without fee is hereby granted, provided that the above copyright
dnl notice and this permission notice appear in all copies.
dnl
dnl THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
dnl IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
dnl FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
dnl NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
dnl DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
dnl OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
dnl OR OTHER DEALINGS IN THE SOFTWARE.
dnl
dnl Except as contained in this notice, the name of a copyright holder shall not
dnl be used in advertising or otherwise to promote the sale, use or other dealings
dnl in this Software without prior written authorization of the copyright holder.

dnl Check for socklen_t: historically on BSD it is an int, and in
dnl POSIX 1g it is a type of its own, but some platforms use different
dnl types for the argument to getsockopt, getpeername, etc.  So we
dnl have to test to find something that will work.

dnl Remove the AC_CHECK_TYPE - on HP-UX it would find a socklen_t, but the
dnl function prototypes for getsockopt et al will not actually use
dnl socklen_t args unless _XOPEN_SOURCE_EXTENDED is defined. so, the
dnl AC_CHECK_TYPE will find a socklen_t and think all is happiness and
dnl joy when you will really get warnings about mismatch types - type
dnl mismatches that would be possibly Bad (tm) in a 64-bit compile.
dnl raj 2005-05-11 this change may be redistributed at will

dnl also, added "extern" to the "int getpeername" in an attempt to resolve
dnl an issue with this code under Solaris 2.9.  this too may be
dnl redistributed at will


AC_DEFUN([OLD_TYPE_SOCKLEN_T],
[
      AC_MSG_CHECKING([for socklen_t equivalent])
      AC_CACHE_VAL([curl_cv_socklen_t_equiv],
      [
         # Systems have either "struct sockaddr *" or
         # "void *" as the second argument to getpeername
         curl_cv_socklen_t_equiv=
         for arg2 in "struct sockaddr" void; do
            for t in int size_t unsigned long "unsigned long" socklen_t; do
               AC_TRY_COMPILE([
                  #ifdef HAVE_SYS_TYPES_H
                  #include <sys/types.h>
                  #endif
                  #ifdef HAVE_SYS_SOCKET_H
                  #include <sys/socket.h>
                  #endif

                  extern int getpeername (int, $arg2 *, $t *);
               ],[
                  $t len;
                  getpeername(0,0,&len);
               ],[
                  curl_cv_socklen_t_equiv="$t"
                  break 2
               ])
            done
         done

         if test "x$curl_cv_socklen_t_equiv" = x; then
        # take a wild guess
            curl_cv_socklen_t_equiv="socklen_t"
            AC_MSG_WARN([Cannot find a type to use in place of socklen_t, guessing socklen_t])
         fi
      ])
      AC_MSG_RESULT($curl_cv_socklen_t_equiv)
      AC_DEFINE_UNQUOTED(netperf_socklen_t, $curl_cv_socklen_t_equiv,
                        [type to use in place of socklen_t if not defined])
])


dnl *
dnl * Copyright (c) 2001  Motoyuki Kasahara
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
dnl * Check for h_errno.
dnl *
AC_DEFUN([AC_DECL_H_ERRNO],
[AC_CACHE_CHECK(for h_errno declaration in netdb.h, ac_cv_decl_h_errno,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <netdb.h>]], [[
h_errno = 0;
]])],[ac_cv_decl_h_errno=yes],[ac_cv_decl_h_errno=no])])
if test "$ac_cv_decl_h_errno" = yes; then
    AC_DEFINE(H_ERRNO_DECLARED, 1,
[Define to 1 if `h_errno' is declared by <netdb.h>])
fi])

dnl *
dnl * Copyright (c) 2001  Motoyuki Kasahara
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
dnl * Check for struct sockaddr_in6
dnl *
AC_DEFUN([AC_STRUCT_SOCKADDR_IN6],
[AC_CACHE_CHECK(for struct sockaddr_in6, ac_cv_struct_sockaddr_in6,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>]], [[
struct sockaddr_in6 address;
]])],[ac_cv_struct_sockaddr_in6=yes],[ac_cv_struct_sockaddr_in6=no])])
if test "$ac_cv_struct_sockaddr_in6" = yes; then
    AC_DEFINE(HAVE_STRUCT_SOCKADDR_IN6, 1,
[Define to 1 if <netinet/in.h> defines `struct sockaddr_in6'])
fi])

dnl *
dnl * Check for struct sockaddr_storage
dnl *
AC_DEFUN([AC_STRUCT_SOCKADDR_STORAGE],
[AC_CACHE_CHECK(for struct sockaddr_storage, ac_cv_struct_sockaddr_storage,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>]], [[
struct sockaddr_storage address;
]])],[ac_cv_struct_sockaddr_storage=yes],[ac_cv_struct_sockaddr_storage=no])])
if test "$ac_cv_struct_sockaddr_storage" = yes; then
    AC_DEFINE(HAVE_STRUCT_SOCKADDR_STORAGE, 1,
[Define to 1 if <netinet/in.h> defines `struct sockaddr_storage'])
fi])

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
