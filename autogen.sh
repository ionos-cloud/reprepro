#!/bin/sh
set -e

mkdir -p ac
aclocal
autoheader
automake -a -c
autoconf
rm -rf autom4te.cache || true

if [ $# -lt 1 ] ; then
	exit 0
fi

if [ "x$1" = "x--configure" ] ; then
	shift
	repreprodir="`pwd`"
	if [ $# -gt 0 ] ; then
		mkdir -p -- "$1"
		cd "$1" || exit 1
		shift
	fi
	"$repreprodir"/configure --enable-maintainer-mode CFLAGS="-Wall -O2 -g -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wunused-parameter -Wsign-compare" CPPFLAGS="" "$@"
else
	echo "unsupported option $1" >&2
	exit 1
fi
