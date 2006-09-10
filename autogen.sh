#!/bin/sh
set -e

aclocal 
autoheader
automake -a -c
autoconf

./configure --enable-maintainer-mode CFLAGS="-Wall -O2 -g -Wmissing-prototypes -Wstrict-prototypes -DSTUPIDCC=1" "$@"
