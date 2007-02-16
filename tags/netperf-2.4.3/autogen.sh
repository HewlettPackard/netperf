#! /bin/sh

aclocal -I src/missing/m4 \
&& automake  --add-missing \
&& autoconf 
