#!/bin/sh
set -e

aclocal
autoheader
automake -a -c
autoconf
rm -rf autom4te.cache || true

if [ "x$1" = "x--configure" ] ; then
	shift
	repreprodir="`pwd`"
	if [ $# > 0 ] ; then
		mkdir -p -- "$1"
		cd "$1" || exit 1
	fi
	shift
	"$repreprodir"/configure --enable-maintainer-mode CFLAGS="-Wall -O2 -g -Wmissing-prototypes -Wstrict-prototypes -DSTUPIDCC=1" "$@"
else
	echo "unsupported option $1" >&2
	exit 1
fi
