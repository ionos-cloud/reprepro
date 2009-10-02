# Shell script snippets used in the test scripts...

set -e

export LC_ALL=C

testrun() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" -C $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -r -C $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@"
else
	"$TESTTOOL" -r -C $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@" 3<"$rules".rules
fi
}
testout() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" -o results $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -o results -r $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@"
else
	"$TESTTOOL" -o results -r $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@" 3<"$rules".rules
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

function setverbosity() {
	if test "x$1" = "xdefault" ; then
		set -- "$DEFAULTVERBOSITY"
	fi
	case "$1" in
		-1)
			VERBOSITYOPTIONS="-D v=-1"
			VERBOSITY="-s"
			verbosity=-1
			;;
		0)
			VERBOSITYOPTIONS="-D v=0"
			VERBOSITY=""
			verbosity=0
			;;
		1)
			VERBOSITYOPTIONS="-D v=1"
			VERBOSITY="-v"
			verbosity=1
			;;
		2)
			VERBOSITYOPTIONS="-D v=2"
			VERBOSITY="-vv"
			verbosity=2
			;;
		3)
			VERBOSITYOPTIONS="-D v=3"
			VERBOSITY="-vvv"
			verbosity=3
			;;
		4)
			VERBOSITYOPTIONS="-D v=4"
			VERBOSITY="-vvvv"
			verbosity=4
			;;
		5)
			VERBOSITYOPTIONS="-D v=5"
			VERBOSITY="-vvvvv"
			verbosity=5
			;;
		6)
			VERBOSITYOPTIONS="-D v=6"
			VERBOSITY="-vvvvvv"
			verbosity=6
			;;
		unchanged)
			;;
		*)
			echo "Unsupported verbosity" >&2
			exit 1
			;;
	esac
}

FAKEARCHITECTURE=abacus
SRCDIR="$(realpath -s "$(dirname $0)/..")"
WORKDIR="`pwd`/testdir"
USE_VALGRIND=""
VALGRIND_SUP=""
VERBOSEDB="1"
DEFAULTVERBOSITY=6

while [ $# -gt 0 ] ; do
	case "$1" in
		--srcdir)
			shift
			SRCDIR="$(realpath -s "$1")"
			shift
			;;
		--delete)
			rm -r "$WORKDIR" || true
			shift
			;;
		--valgrind)
			USE_VALGRIND=1
			shift
			;;
		--valgrind-supp)
			USE_VALGRIND=1
			shift
			VALGRIND_SUP="$1"
			shift
			;;
		--fake-architecture)
			shift
			FAKEARCHITECTURE="$1"
			shift
			;;
		--verbosity)
			shift
			DEFAULTVERBOSITY="$1"
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
export FAKEARCHITECTURE
export FALEN=${#FAKEARCHITECTURE}
# new dpkg-dev supports a fake architecture without tricks (and the old tricks no longer work):
export DEB_HOST_ARCH="$FAKEARCHITECTURE"

if [ "2" -lt "$#" ] ; then
	echo "Syntax: test.sh [<testtool-binary>] [<reprepro-binary>]" >&2
	exit 1
fi
echo "SRCDIR is '$SRCDIR'"
if [ ! -d "$SRCDIR" ] || [ ! -d "$SRCDIR/tests" ] ; then
	echo "Error: Could not find source directory (tried: '$SRCDIR')!" >&2
	exit 1
fi
if [ "1" -le "$#" ] ; then
	TESTTOOL="$(realpath -s "$1")"
else
	TESTTOOL=testtool
fi
if [ "2" -le "$#" ] ; then
	REPREPRO="$(realpath -s "$2")"
else
	REPREPRO="$SRCDIR/reprepro"
fi

if [ -z "$TESTOPTIONS" ] ; then
	if [ -z "$USE_VALGRIND" ] ; then
		TESTOPTIONS="-e -a"
	elif [ -z "$VALGRIND_SUP" ] ; then
		TESTOPTIONS="-e -a --debug --leak-check=full --suppressions=$SRCDIR/valgrind.supp"
	else
		TESTOPTIONS="-e -a --debug --leak-check=full --suppressions=$VALGRIND_SUP"
	fi
fi
MAINTESTOPTIONS="$TESTOPTIONS"


function setoptions() {
	if test $# -lt 3 ; then
		echo "setoptions: Wrong argument count!" >&2
		exit 1
	fi
	setverbosity "$1"
	shift
	TESTOPTIONS="$1 $VERBOSITYOPTIONS $MAINTESTOPTIONS"
	shift
	REPREPROOPTIONS="$1 $VERBOSITY"
	shift
	if test -n "$VERBOSEDB" ; then
		TESTOPTIONS="-D x=0 -D d=1 $TESTOPTIONS"
		REPREPROOPTIONS="--verbosedb $REPREPROOPTIONS"
	else
		TESTOPTIONS="-D x=0 -D d=0 $TESTOPTIONS"
	fi
	TRACKINGTESTOPTIONS="-D t=0"
	while test $# -gt 0 ; do
		case $1 in
			tracking)
				if test -n "$VERBOSEDB" ; then
					TRACKINGTESTOPTIONS="-D t=1"
				fi
				;;
			*)
				echo "setoptions: unsupported third argument!" >&2
				exit 1
				;;
		esac
		shift
	done
	TESTOPTIONS="$TRACKINGTESTOPTIONS $TESTOPTIONS"
	echo testoptions are set to: $TESTOPTIONS
	echo reprepro options are set to: $REPREPROOPTIONS
}
export PATH="$SRCDIR/tests:$PATH"
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

mkdir "$WORKDIR"
cd "$WORKDIR"

touch results.empty
cat > empty.rules <<EOF
stdout
stderr
returns 0
EOF
function checknolog() {
	dodo test ! -f logs/"$1"
}
function checklog() {
	cat > results.log.expected
	LOGDATE="$(date +'%Y-%m-%d %H:')"
	echo normalizing "$1": DATESTR is "$LOGDATE??:??"
	sed -i -e 's/^'"$LOGDATE"'[0-9][0-9]:[0-9][0-9] /DATESTR /g' logs/"$1"
	dodiff results.log.expected logs/"$1"
	rm logs/"$1"
}
function md5() {
md5sum "$1" | cut -d' ' -f1
}
function sha1() {
sha1sum "$1" | cut -d' ' -f1
}
function sha256() {
sha256sum "$1" | cut -d' ' -f1
}
function printindexpart() {
	FILENAME="$1"
	dpkg-deb -I "$FILENAME" control >"$FILENAME".control
	ed -s "$FILENAME".control << EOF
H
/^Description:/ kd
/^Priority/ m 'd-1
/^Section/ m 'd-1
'd i
Filename: $FILENAME
Size: $(stat -c "%s" "$FILENAME")
SHA256: $(sha256 "$FILENAME")
SHA1: $(sha1 "$FILENAME")
MD5sum: $(md5 "$FILENAME")
.
$ a

.
w
q
EOF
cat "$FILENAME".control
rm "$FILENAME".control
}
function withoutchecksums() {
awk 'BEGIN{inheader=0} /^Checksums-.*: / || (inheader && /^ /)  {inheader = 1; next} {inheader = 0 ; print}' "$@"
}
function mdandsize() {
cat <<EOF
$(md5sum "$1" | cut -d' ' -f1) $(stat -c "%s" "$1")
EOF
}
function sha() {
echo -n ":1:"
sha1sum "$1" | cut -d' ' -f1
}
function sha1andsize() {
cat <<EOF
$(sha1sum "$1" | cut -d' ' -f1) $(stat -c "%s" "$1")
EOF
}
function sha2() {
echo -n ":2:"
sha256sum "$1" | cut -d' ' -f1
}
function sha2andsize() {
cat <<EOF
$(sha256sum "$1" | cut -d' ' -f1) $(stat -c "%s" "$1")
EOF
}
function fullchecksum() {
cat <<EOF
$(sha "$1") $(sha2 "$1") $(md5sum "$1" | cut -d' ' -f1) $(stat -c "%s" "$1")
EOF
}
function md5releaseline() {
 echo "$(mdandsize dists/"$1"/"$2") $2"
}
function sha1releaseline() {
 echo "$(sha1andsize dists/"$1"/"$2") $2"
}
function sha2releaseline() {
 echo "$(sha2andsize dists/"$1"/"$2") $2"
}


EMPTYMD5ONLY="d41d8cd98f00b204e9800998ecf8427e"
EMPTYMD5="d41d8cd98f00b204e9800998ecf8427e 0"
EMPTYGZMD5="7029066c27ac6f5ef18d660d5741979a 20"
EMPTYBZ2MD5="4059d198768f9f8dc9372dc1c54bc3c3 14"
EMPTYSHA1="da39a3ee5e6b4b0d3255bfef95601890afd80709 0"
EMPTYGZSHA1="46c6643f07aa7f6bfe7118de926b86defc5087c4 20"
EMPTYBZ2SHA1="64a543afbb5f4bf728636bdcbbe7a2ed0804adc2 14"
EMPTYSHA2="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 0"
EMPTYGZSHA2="59869db34853933b239f1e2219cf7d431da006aa919635478511fabbfc8849d2 20"
EMPTYBZ2SHA2="d3dda84eb03b9738d118eb2be78e246106900493c0ae07819ad60815134a8058 14"

function runtest() {
	echo "Running test '$1'.."
	TESTNAME=" $1"
	source "$SRCDIR/tests/test$1.sh"
}
function testsuccess() {
	echo "Test$TESTNAME completed successfully"
}

setoptions default "" ""
TESTINCSETUP=issetup
