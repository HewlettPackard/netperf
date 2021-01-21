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
                  break
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
