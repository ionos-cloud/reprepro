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

# avoid architecture dependency of the test-suite:
export DEB_HOST_ARCH="abacus"

export PATH="$TESTSDIR:$PATH"
if ! [ -x "$REPREPRO" ] ; then
	echo "Could not find $REPREPRO!" >&2
	exit 1
fi

checknolog() {
	dodo test ! -f logs/"$1"
}
checklog() {
	sort > results.log.expected
	LOGDATE="$(date +'%Y-%m-%d %H:')"
	echo normalizing "$1": DATESTR is "$LOGDATE??:??"
	sed -e 's/^'"$LOGDATE"'[0-9][0-9]:[0-9][0-9] /DATESTR /g' logs/"$1" | sort > results.log
	dodiff results.log.expected results.log
	rm logs/"$1" results.log
}
md5() {
md5sum "$1" | cut -d' ' -f1
}
sha1() {
sha1sum "$1" | cut -d' ' -f1
}
sha256() {
sha256sum "$1" | cut -d' ' -f1
}
printindexpart() {
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
withoutchecksums() {
awk 'BEGIN{inheader=0} /^Checksums-.*:/ || (inheader && /^ /)  {inheader = 1; next} {inheader = 0 ; print}' "$@"
}
mdandsize() {
cat <<EOF
$(md5sum "$1" | cut -d' ' -f1) $(stat -c "%s" "$1")
EOF
}
sha() {
echo -n ":1:"
sha1sum "$1" | cut -d' ' -f1
}
sha1andsize() {
cat <<EOF
$(sha1sum "$1" | cut -d' ' -f1) $(stat -c "%s" "$1")
EOF
}
sha1and7size() {
cat <<EOF
$(sha1sum "$1" | cut -d' ' -f1) $(stat -c "%7s" "$1")
EOF
}
sha2() {
echo -n ":2:"
sha256sum "$1" | cut -d' ' -f1
}
sha2andsize() {
cat <<EOF
$(sha256sum "$1" | cut -d' ' -f1) $(stat -c "%s" "$1")
EOF
}
sizeonly() {
stat -c "%s" "$1"
}
sha2only() {
sha256sum "$1" | cut -d' ' -f1
}
fullchecksum() {
cat <<EOF
$(sha "$1") $(sha2 "$1") $(md5sum "$1" | cut -d' ' -f1) $(stat -c "%s" "$1")
EOF
}
md5releaseline() {
 echo "$(mdandsize dists/"$1"/"$2") $2"
}
sha1releaseline() {
 echo "$(sha1andsize dists/"$1"/"$2") $2"
}
sha2releaseline() {
 echo "$(sha2andsize dists/"$1"/"$2") $2"
}
normalizerelease() {
 sed -e 's/^Date: .*/Date: normalized/' "$1"
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

testsuccess() {
	echo "Test$TESTNAME completed successfully"
}

odb() {
	printf -- '-v2*=Created directory "./db"'
}

otd() {
	local name="$1" version="$2" codename="$3"
	printf -- "-d1*=db: '%s' '%s' removed from tracking.db(%s)." "$name" "$version" "$codename"
}
ottd() {
	local name="$1" version="$2" codename="$3"
	printf -- "-t1*=db: '%s' '%s' removed from tracking.db(%s)." "$name" "$version" "$codename"
}
ota() {
	printf -- "-d1*=db: '%s' added to tracking.db(%s)." "$2" "$1"
}
otta() {
	printf -- "-t1*=db: '%s' added to tracking.db(%s)." "$2" "$1"
}
ofa() {
	printf -- "-d1*=db: '%s' added to checksums.db(pool)." "$1"
}
ofd() {
	if ${2:-true} ; then
		printf -- "-v1${3-*}"'=deleting and forgetting %s\n' "$1"
	fi
	printf -- "-d1${3-*}=db: '%s' removed from checksums.db(pool)." "$1"
}
opa() {
	local name="$1" version="$2" codename="$3" component="$4" arch="$5" type="$6" u=""
	if test "$type" = "udeb" ; then u='u|' ; fi
	printf -- "-d1*=db: '%s' added to packages.db(%s%s|%s|%s)." \
		"$name" "$u" "$codename" "$component" "$arch"
}
opu() {
	local name="$1" oldversion="$2" version="$3" codename="$4" component="$5" arch="$6" type="$7" u=""
	if test "$type" = "udeb" ; then u='u|' ; fi
	printf -- "-d1*=db: '%s' removed from packages.db(%s%s|%s|%s).\n" \
		"$name" "$u" "$codename" "$component" "$arch"
	printf -- "-d1*=db: '%s' added to packages.db(%s%s|%s|%s)." \
		"$name" "$u" "$codename" "$component" "$arch"
}
opd() {
	local name="$1" version="$2" codename="$3" component="$4" arch="$5" type="$6" u=""
	if test "$type" = "udeb" ; then u='u|' ; fi
	printf -- "-v1*=removing '%s' from '%s%s|%s|%s'...\n" \
		"$name" "$u" "$codename" "$component" "$arch"
	printf -- "-d1*=db: '%s' removed from packages.db(%s%s|%s|%s)." \
		"$name" "$u" "$codename" "$component" "$arch"
}

cat > empty.rules <<EOF
stdout
stderr
returns 0
EOF

