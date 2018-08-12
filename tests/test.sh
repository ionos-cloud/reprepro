#!/bin/dash

# This needs installed:
# apt, dpkg-dev, ed, python3-apt, xz, lzma, python3, dbX.Y-util
# it will fail if run over a changing hour

set -e -u

export LC_ALL=C

SRCDIR="$(readlink -e "$(dirname $0)/..")"
WORKDIR="`pwd`/testdir"
USE_VALGRIND=""
VALGRIND_LEAK=summary
VALGRIND_EXTRA_OPTIONS=""
VALGRIND_SUP=""
TESTOPTIONS=""
VERBOSEDB="1"
TESTSHELLOPTS=
testtorun="all"
verbosity=6
deleteifmarked=true

while [ $# -gt 0 ] ; do
	case "$1" in
		--srcdir)
			shift
			SRCDIR="$(readlink -e "$1")"
			shift
			;;
		--neverdelete)
			deleteifmarked=false
			shift
			;;
		--test)
			shift
			testtorun="$1"
			shift
			;;
		--trace)
			shift
			TESTSHELLOPTS=-x
			;;
		--delete)
			if ! $deleteifmarked ; then
				rm -r "$WORKDIR" || true
			fi
			shift
			;;
		--valgrind)
			USE_VALGRIND=1
			shift
			;;
		--valgrind)
			USE_VALGRIND=1
			VALGRIND_LEAK=full
			shift
			;;
		--valgrind-supp)
			USE_VALGRIND=1
			shift
			VALGRIND_SUP="$1"
			shift
			;;
		--valgrind-opts)
			shift
			VALGRIND_EXTRA_OPTIONS="${VALGRIND_EXTRA_OPTIONS} $1"
			shift
			;;
		--verbosity)
			shift
			verbosity="$1"
			shift
			;;
		--noverbosedb)
			VERBOSEDB=""
			shift
			;;
		--*)
			echo "Unsupported option $1" >&2
			exit 1
			;;
		*)
			break
			;;
	esac
done

if [ "2" -lt "$#" ] ; then
	echo "Syntax: test.sh [<testtool-binary>] [<reprepro-binary>]" >&2
	exit 1
fi
echo "SRCDIR is '$SRCDIR'"
if [ ! -d "$SRCDIR" ] || [ ! -d "$SRCDIR/tests" ] ; then
	echo "Error: Could not find source directory (tried: '$SRCDIR')!" >&2
	exit 1
fi
TESTSDIR="$SRCDIR/tests"
if [ "1" -le "$#" ] ; then
	TESTTOOL="$(readlink -e "$1")"
else
	TESTTOOL=testtool
fi
if [ "2" -le "$#" ] ; then
	REPREPRO="$(readlink -e "$2")"
else
	REPREPRO="$SRCDIR/reprepro"
fi
RREDTOOL="$(dirname "$REPREPRO")/rredtool"

if [ -z "$TESTOPTIONS" ] ; then
	if [ -z "$USE_VALGRIND" ] ; then
		TESTOPTIONS="-e -a"
	elif [ -z "$VALGRIND_SUP" ] ; then
		# leak-check=full is better than leak-check=summary,
		# sadly squeeze's valgrind counts them into the error number
		# with full, and we want to ignore them for childs....
		TESTOPTIONS="-e -a --debug ${VALGRIND_EXTRA_OPTIONS} --leak-check=${VALGRIND_LEAK} --suppressions=$TESTSDIR/valgrind.supp"
	else
		TESTOPTIONS="-e -a --debug ${VALGRIND_EXTRA_OPTIONS} --leak-check=${VALGRIND_LEAK} --suppressions=$VALGRIND_SUP"
	fi
fi
case "$verbosity" in
	-1) VERBOSITY="-s" ;;
	0) VERBOSITY="" ;;
	1) VERBOSITY="-v" ;;
	2) VERBOSITY="-vv" ;;
	3) VERBOSITY="-vvv" ;;
	4) VERBOSITY="-vvvv" ;;
	5) VERBOSITY="-vvvvv" ;;
	6) VERBOSITY="-vvvvvv" ;;
	*) echo "Unsupported verbosity $verbosity" >&2
	   exit 1
	   ;;
esac
TESTOPTIONS="-D v=$verbosity $TESTOPTIONS"
REPREPROOPTIONS="$VERBOSITY"
if test -n "$VERBOSEDB" ; then
	TESTOPTIONS="-D x=0 -D d=1 $TESTOPTIONS"
	REPREPROOPTIONS="--verbosedb $REPREPROOPTIONS"
else
	TESTOPTIONS="-D x=0 -D d=0 $TESTOPTIONS"
fi
TRACKINGTESTOPTIONS="-D t=0"

if ! [ -x "$REPREPRO" ] ; then
	echo "Could not find $REPREPRO!" >&2
	exit 1
fi
TESTTOOLVERSION="`$TESTTOOL --version`"
case $TESTTOOLVERSION in
	"testtool version "*) ;;
	*) echo "Failed to get version of testtool($TESTTOOL)"
	   exit 1
	   ;;
esac

if test -d "$WORKDIR" && test -f "$WORKDIR/ThisDirectoryWillBeDeleted" && $deleteifmarked ; then
	rm -r "$WORKDIR" || exit 3
fi

if ! which fakeroot >/dev/null 2>&1 ; then
	echo "WARNING: fakeroot not installed, some tests might fail!"
fi
if ! which python3 >/dev/null 2>&1 ; then
	echo "WARNING: python3 not installed, some tests might fail!"
fi
if ! which lzma >/dev/null 2>&1 ; then
	echo "WARNING: lzma not installed, some tests might fail!"
fi
if ! which ed >/dev/null 2>&1 ; then
	echo "WARNING: ed not installed, some tests might fail!"
fi
if ! which lunzip >/dev/null 2>&1 ; then
	echo "WARNING: lunzip not installed, some tests might be incomplete!"
else
if ! which lzip >/dev/null 2>&1 ; then
	echo "WARNING: lunzip installed but lunzip not, some tests might fail!"
fi
fi
if ! dpkg -s python3-apt | grep -q -s "Status: .* ok installed" ; then
	echo "WARNING: python3-apt not installed, some tests might fail!"
fi
if ! dpkg -s dpkg-dev | grep -q -s "Status: .* ok installed" ; then
	echo "WARNING: dpkg-dev not installed, most tests might fail!"
fi

mkdir "$WORKDIR" || exit 1
echo "Remove this file to avoid silent removal" > "$WORKDIR"/ThisDirectoryWillBeDeleted
cd "$WORKDIR"

# dpkg-deb doesn't like too restrictive directories
umask 022

number_tests=0
number_missing=0
number_success=0
number_skipped=0
number_failed=0

runtest() {
	if ! test -f "$SRCDIR/tests/$1.test" ; then
		echo "Cannot find $SRCDIR/tests/$1.test!" >&2
		number_missing="$(( $number_missing + 1 ))"
		return
	fi
	number_tests="$(( $number_tests + 1 ))"
	echo "Running test '$1'.."
	TESTNAME=" $1"
	mkdir "dir_$1"
	rc=0
	( cd "dir_$1" || exit 1
	  export TESTNAME
	  export SRCDIR TESTSDIR
	  export TESTTOOL RREDTOOL REPREPRO
	  export TRACKINGTESTOPTIONS TESTOPTIONS REPREPROOPTIONS verbosity
  	  WORKDIR="$WORKDIR/dir_$1" CALLEDFROMTESTSUITE=true dash $TESTSHELLOPTS "$SRCDIR/tests/$1.test"
	) > "log_$1" 2>&1 || rc=$?
	if test "$rc" -ne 0 ; then
		number_failed="$(( $number_failed + 1 ))"
		echo "test '$1' failed (see $WORKDIR/log_$1 for details)!" >&2
	elif grep -q -s '^SKIPPED: ' "log_$1" ; then
		number_skipped="$(( $number_skipped + 1 ))"
		echo "test '$1' skipped:"
		sed -n -e 's/^SKIPPED://p' "log_$1"
		rm -r "dir_$1" "log_$1"
	else
		number_success="$(( $number_success + 1 ))"
		rm -r "dir_$1" "log_$1"
	fi
}

if test x"$testtorun" != x"all" ; then
	runtest "$testtorun"
else
	runtest export
	runtest buildinfo
	runtest updatepullreject
	runtest descriptions
	runtest easyupdate
	runtest srcfilterlist
	runtest uploaders
	runtest wrongarch
	runtest flood
	runtest exporthooks
	runtest updatecorners
	runtest packagediff
	runtest includeextra
	runtest atoms
	runtest trackingcorruption
	runtest layeredupdate
	runtest layeredupdate2
	runtest uncompress
	runtest check
	runtest flat
	runtest subcomponents
	runtest snapshotcopyrestore
	runtest various1
	runtest various2
	runtest various3
	runtest copy
	runtest buildneeding
	runtest morgue
	runtest diffgeneration
	runtest onlysmalldeletes
	runtest override
	runtest includeasc
	runtest listcodenames
fi
echo "$number_tests tests, $number_success succeded, $number_failed failed, $number_skipped skipped, $number_missing missing"
exit 0
