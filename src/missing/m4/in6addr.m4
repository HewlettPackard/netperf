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
dnl * Check for struct in6_addr
dnl *
AC_DEFUN([AC_STRUCT_IN6_ADDR],
[AC_CACHE_CHECK(for struct in6_addr, ac_cv_struct_in6_addr,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>]], [[
struct in6_addr address;
]])],[ac_cv_struct_in6_addr=yes],[ac_cv_struct_in6_addr=no])])
if test "$ac_cv_struct_in6_addr" = yes; then
    AC_DEFINE(HAVE_STRUCT_IN6_ADDR, 1,
[Define to 1 if <netinet/in.h> defines `struct in6_addr'])
fi])

dnl *
dnl * Check for in6addr_any.
dnl *
AC_DEFUN([AC_DECL_IN6ADDR_ANY],
[AC_REQUIRE([AC_STRUCT_IN6_ADDR])
if test $ac_cv_struct_in6_addr = no; then
    ac_cv_decl_in6addr_any=no
else
    AC_CACHE_CHECK(for in6addr_any declaration in netinet/in.h,
    ac_cv_decl_in6addr_any,
    [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>]], [[
unsigned char *address;
address = (char *)&in6addr_any;
]])],[ac_cv_decl_in6addr_any=yes],[ac_cv_decl_in6addr_any=no])])
    if test "$ac_cv_decl_in6addr_any" = yes; then
        AC_DEFINE(IN6ADDR_ANY_DECLARED, 1,
[Define to 1 if `in6addr_any' is declared by <netinet/in.h>])
    fi
fi])

dnl *
dnl * Check for in6addr_loopback.
dnl *
AC_DEFUN([AC_DECL_IN6ADDR_LOOPBACK],
[AC_REQUIRE([AC_STRUCT_IN6_ADDR])
if test $ac_cv_struct_in6_addr = no; then
    ac_cv_decl_in6addr_loopback=no
else
    AC_CACHE_CHECK(for in6addr_loopback declaration in netinet/in.h,
    ac_cv_decl_in6addr_loopback,
    [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>]], [[
unsigned char *address;
address = (char *)&in6addr_loopback;
]])],[ac_cv_decl_in6addr_loopback=yes],[ac_cv_decl_in6addr_loopback=no])])
    if test "$ac_cv_decl_in6addr_loopback" = yes; then
        AC_DEFINE(IN6ADDR_LOOPBACK_DECLARED, 1,
[Define to 1 if `in6addr_loopback' is declared by <netinet/in.h>])
    fi
fi])
