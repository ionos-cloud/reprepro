#!/bin/bash

set -e

testrun() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" $TESTOPTIONS "$CHANGESTOOL" "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -r $TESTOPTIONS "$CHANGESTOOL" "$@"
else
	"$TESTTOOL" -r $TESTOPTIONS "$CHANGESTOOL" "$@" 3<"$rules".rules
fi
}
testout() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" -o results $TESTOPTIONS "$CHANGESTOOL" "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -o results -r $TESTOPTIONS "$CHANGESTOOL" "$@"
else
	"$TESTTOOL" -o results -r $TESTOPTIONS "$CHANGESTOOL" "$@" 3<"$rules".rules
fi
}
dogrep() {
echo grep -q "$@"
grep -q "$@"
}
dongrep() {
echo "!grep" -q "$@"
! grep -q "$@"
}
dodiff() {
echo diff -u "$@"
diff -u "$@"
}
dodo() {
echo "$@"
"$@"
}

WORKDIR="`pwd`/testdir"

if [ "x$1" == "x--delete" ] ; then
	rm -r "$WORKDIR" || true
	shift
fi

mkdir "$WORKDIR"
cd "$WORKDIR"

if [ "1" -gt "$#" ] || [ "3" -lt "$#" ] ; then
	echo "Syntax: test.sh <src-dir> [<testtool-binary>] [<changestool-binary>]" >&2
	exit 1
fi
SRCDIR="$1"
if [ -z "$TESTOPTIONS" ] ; then
	TESTOPTIONS="-e -a"
#	TESTOPTIONS="-e -a --debug --suppressions=$SRCDIR/valgrind.supp"
fi
if [ "2" -le "$#" ] ; then
	TESTTOOL="$2"
else
	TESTTOOL=testtool
fi
if [ "3" -le "$#" ] ; then
	CHANGESTOOL="$3"
else
	CHANGESTOOL="$SRCDIR/changestool"
fi
TESTS="$SRCDIR/tests"
UPDATETYPE=update
export PATH="$TESTS:$PATH"
if ! [ -x "$CHANGESTOOL" ] ; then
	echo "Could not find $CHANGESTOOL!" >&2
	exit 1
fi
TESTTOOLVERSION="`$TESTTOOL --version`"
case $TESTTOOLVERSION in
	"testtool version "*) ;;
	*) echo "Failed to get version of testtool($TESTTOOL)"
	   exit 1
	   ;;
esac

testrun - 3<<EOF
*=modifychanges: Modify a Debian style .changes file
*=Syntax: modifychanges <changesfile> <commands>
*=Possible commands include:
*= verify
returns 1
EOF

testrun - --help 3<<EOF
stdout
*=modifychanges: Modify a Debian style .changes file
*=Syntax: modifychanges <changesfile> <commands>
*=Possible commands include:
*= verify
returns 0
EOF

testrun - test.changes verify 3<<EOF
stderr
*=No such file 'test.changes'!
stdout
returns 1
EOF

touch test.changes
testrun - test.changes verify 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
*=Could only find spaces within 'test.changes'!
stdout
returns 1
EOF

echo "Format: 2.0" > test.changes
testrun - test.changes verify 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
*=Missing 'Source:' field in test.changes!
=Missing 'Version:' field in test.changes!
=Missing 'Maintainer:' field in test.changes!
stdout
returns 1
EOF

set +v +x
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
