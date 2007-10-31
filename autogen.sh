#!/bin/sh
set -e

aclocal 
autoheader
automake -a -c
autoconf

if [ "x$1" = "x--configurein" ] ; then
	repreprodir="`pwd`"
	mkdir -p -- "$2"
	cd "$2" || exit 1
	"$repreprodir"/configure --enable-maintainer-mode CFLAGS="-Wall -O2 -g -Wmissing-prototypes -Wstrict-prototypes -DSTUPIDCC=1" "$@"
fi
