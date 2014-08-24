#!/bin/sh
if [ $# -ne 0 ] ; then
	echo "brokenuncompressor.sh: Wrong number of arguments: $#" >&2
	exit 17
fi
$uncompressor
if test -f breakon2nd ; then
	rm breakon2nd
	exit 0;
fi
# Breaking an .lzma stream is hard, faking it is more reproduceable...
echo "brokenuncompressor.sh: claiming broken archive" >&2
exit 1
