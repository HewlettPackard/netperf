dnl Copyright (c) 1995, 1996, 1997, 1998
dnl tising materials mentioning
dnl dnl features or use of this software display the following acknowledgement:
dnl dnl ``This product includes software developed by the University of California,
dnl dnl Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
dnl dnl the University nor the names of its contributors may be used to endorse
dnl dnl or promote products derived from this software without specific prior
dnl dnl written permission.
dnl dnl THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
dnl dnl WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
dnl dnl MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
dnl dnl
dnl dnl LBL autoconf macros
dnl dnl
dnl
dnl
dnl Checks to see if the sockaddr struct has the 4.4 BSD sa_len member
dnl borrowed from LBL libpcap
AC_DEFUN(AC_CHECK_SA_LEN, [
        AC_MSG_CHECKING(if sockaddr struct has sa_len member)
        AC_CACHE_VAL($1,
        AC_TRY_COMPILE([
#               include <sys/types.h>
#               include <sys/socket.h>],
                [u_int i = sizeof(((struct sockaddr *)0)->sa_len)],
                $1=yes,
                $1=no))
        AC_MSG_RESULT($$1)
                if test $$1 = yes ; then
                        AC_DEFINE([HAVE_SOCKADDR_SA_LEN],1,[Define if struct sockaddr has the sa_len member])
        fi
])
