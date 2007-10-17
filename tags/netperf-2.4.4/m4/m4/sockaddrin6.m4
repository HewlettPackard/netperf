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

