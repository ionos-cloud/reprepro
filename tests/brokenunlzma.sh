#!/bin/sh
if [ $# -ne 0 ] ; then
	echo "brokenunlzma.sh: Wrong number of arguments: $#" >&2
	exit 17
fi
unlzma
if test -f breakon2nd ; then
	rm breakon2nd
	exit 0;
fi
# Breaking an .lzma stream is hard, faking it is more reproduceable...
echo "brokenunlzma.sh: claiming broken archive" >&2
exit 1
