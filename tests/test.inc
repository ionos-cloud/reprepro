# Shell script snippets used in the test scripts...

set -e -u

export LC_ALL=C

testrun() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" -C $TRACKINGTESTOPTIONS $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -r -C $TRACKINGTESTOPTIONS $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@"
else
	"$TESTTOOL" -r -C $TRACKINGTESTOPTIONS $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@" 3<"$rules".rules
fi
}
testout() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" -o results $TRACKINGTESTOPTIONS $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -o results -r $TRACKINGTESTOPTIONS $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@"
else
	"$TESTTOOL" -o results -r $TRACKINGTESTOPTIONS $TESTOPTIONS "$REPREPRO" $REPREPROOPTIONS "$@" 3<"$rules".rules
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

FAKEARCHITECTURE=abacus

if test -z "$SRCDIR" ||  ! test -d "$SRCDIR" ; then
	echo "SRCDIR='$SRCDIR' not a valid directory!" >&2
	exit 1
fi
if test -z "$TESTSDIR" ||  ! test -d "$TESTSDIR" ; then
	echo "TESTSDIR='$TESTSDIR' not a valid directory!" >&2
	exit 1
fi
if test -z "$WORKDIR" ||  ! test -d "$WORKDIR" ; then
	echo "WORKDIR='$WORKDIR' not a valid directory!" >&2
	exit 1
fi

export FAKEARCHITECTURE
export FALEN=${#FAKEARCHITECTURE}
# new dpkg-dev supports a fake architecture without tricks (and the old tricks no longer work):
export DEB_HOST_ARCH="$FAKEARCHITECTURE"

export PATH="$TESTSDIR:$PATH"
if ! [ -x "$REPREPRO" ] ; then
	echo "Could not find $REPREPRO!" >&2
	exit 1
fi

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
function sha1and7size() {
cat <<EOF
$(sha1sum "$1" | cut -d' ' -f1) $(stat -c "%7s" "$1")
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

function testsuccess() {
	echo "Test$TESTNAME completed successfully"
}

cat > empty.rules <<EOF
stdout
stderr
returns 0
EOF

