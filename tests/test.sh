#!/bin/bash

set -e

testrun() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" $TESTOPTIONS "$REPREPRO" "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -r $TESTOPTIONS "$REPREPRO" "$@"
else
	"$TESTTOOL" -r $TESTOPTIONS "$REPREPRO" "$@" 3<"$rules".rules
fi
}
testout() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" -o results $TESTOPTIONS "$REPREPRO" "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -o results -r $TESTOPTIONS "$REPREPRO" "$@"
else
	"$TESTTOOL" -o results -r $TESTOPTIONS "$REPREPRO" "$@" 3<"$rules".rules
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
USE_VALGRIND=""

if [ "x$1" == "x--delete" ] ; then
	rm -r "$WORKDIR" || true
	shift
fi
if [ "x$1" == "x--valgrind" ] ; then
	USE_VALGRIND=1
	shift
fi

mkdir "$WORKDIR"
cd "$WORKDIR"

if [ "1" -gt "$#" ] || [ "3" -lt "$#" ] ; then
	echo "Syntax: test.sh <src-dir> [<testtool-binary>] [<reprepro-binary>]" >&2
	exit 1
fi
SRCDIR="$1"
if [ -z "$TESTOPTIONS" ] ; then
	if [ -z "$USE_VALGRIND" ] ; then
		TESTOPTIONS="-e -a"
	else
		TESTOPTIONS="-e -a --debug --leak-check=full --suppressions=$SRCDIR/valgrind.supp"
	fi
fi
if [ "2" -le "$#" ] ; then
	TESTTOOL="$2"
else
	TESTTOOL=testtool
fi
if [ "3" -le "$#" ] ; then
	REPREPRO="$3"
else
	REPREPRO="$SRCDIR/reprepro"
fi
TESTS="$SRCDIR/tests"
UPDATETYPE=update
export PATH="$TESTS:$PATH"
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
touch results.empty

mkdir -p conf
cat > conf/options <<CONFEND
export changed
CONFEND
cat > conf/distributions <<CONFEND
Codename: A
Architectures: abacus calculator
Components: dog cat

Codename: B
Architectures: abacus source
Components: dog cat
Contents: 1
CONFEND
testrun "" -b . export
find dists -type f |sort > results
cat > results.expected <<END
dists/A/cat/binary-abacus/Packages
dists/A/cat/binary-abacus/Packages.gz
dists/A/cat/binary-abacus/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-abacus/Packages
dists/A/dog/binary-abacus/Packages.gz
dists/A/dog/binary-abacus/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-abacus/Packages
dists/B/cat/binary-abacus/Packages.gz
dists/B/cat/binary-abacus/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-abacus.gz
dists/B/dog/binary-abacus/Packages
dists/B/dog/binary-abacus/Packages.gz
dists/B/dog/binary-abacus/Release
dists/B/dog/source/Release
dists/B/dog/source/Sources.gz
dists/B/Release
END
dodiff results.expected results
testrun - -b . -V import default 3<<EOF
returns 254
stderr
*=Unable to open file ./conf/imports: No such file or directory
*=There have been errors!
EOF
touch conf/imports
testrun - -b . -V import default 3<<EOF
returns 249
stderr
*=No definition for 'default' found in './conf/imports'!
*=There have been errors!
EOF
cat > conf/imports <<EOF
Name: bla
EOF
testrun - -b . -V import default 3<<EOF
returns 249
stderr
*=No definition for 'default' found in './conf/imports'!
*=There have been errors!
EOF
cat > conf/imports <<EOF
Name: bla

Name: default

Name: blub
EOF
testrun - -b . -V import default 3<<EOF
returns 249
stderr
*=Expected 'TempDir' header not found in definition for 'default' in './conf/imports'!
=Stop reading further chunks from './conf/imports' due to previous errors.
*=There have been errors!
EOF
cat > conf/imports <<EOF
Name: bla

Name: default
TempDir: temp

Name: blub
EOF
testrun - -b . -V import default 3<<EOF
returns 249
stderr
*=Expected 'IncomingDir' header not found in definition for 'default' in './conf/imports'!
=Stop reading further chunks from './conf/imports' due to previous errors.
*=There have been errors!
EOF
cat > conf/imports <<EOF
Name: bla

Name: default
TempDir: temp
IncomingDir: i

Name: blub
EOF
testrun - -b . import default 3<<EOF
returns 255
stderr
*='default' in './conf/imports' has neither a 'Allow' nor a 'Default' definition!
*=Aborting as nothing would be left in.
=Stop reading further chunks from './conf/imports' due to previous errors.
*=There have been errors!
EOF
cat > conf/imports <<EOF
Name: bla

Name: default
TempDir: temp
IncomingDir: i
Allow: A B

Name: blub
EOF
testrun - -b . import default 3<<EOF
returns 254
stderr
*=Cannot scan './i': No such file or directory
*=There have been errors!
EOF
mkdir i
testrun "" -b . import default
(cd i ; PACKAGE=bird EPOCH="" VERSION=1 REVISION="" SECTION="tasty" genpackage.sh)
echo returned: $?
DSCMD5S="$(md5sum i/bird_1.dsc | cut -d' ' -f1) $(stat -c '%s' i/bird_1.dsc)"
TARMD5S="$(md5sum i/bird_1.tar.gz | cut -d' ' -f1) $(stat -c '%s' i/bird_1.tar.gz)"
DEBMD5="$(md5sum i/bird_1_abacus.deb | cut -d' ' -f1)"
DEBSIZE="$(stat -c '%s' i/bird_1_abacus.deb)"
DEBAMD5="$(md5sum i/bird-addons_1_all.deb | cut -d' ' -f1)"
DEBASIZE="$(stat -c '%s' i/bird-addons_1_all.deb)"
testrun - -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=No distribution found for 'test.changes'!
*=There have been errors!
EOF
sed -i -e 's/test1/A/' i/test.changes
testrun - -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*='test.changes' lists architecture 'source' not found in distribution 'A'!
*=There have been errors!
EOF
sed -i -e 's/Distribution: A/Distribution: B/' i/test.changes
cp -a i i2
testrun - -V -b . import default 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
*=Created directory "./pool"
*=Created directory "./pool/dog"
*=Created directory "./pool/dog/b"
*=Created directory "./pool/dog/b/bird"
*=Exporting indices...
*= generating Contents-abacus...
*=Reading filelist for pool/dog/b/bird/bird_1_abacus.deb
*=Reading filelist for pool/dog/b/bird/bird-addons_1_all.deb
stdout
*=db: 'bird' added to 'B|dog|source'.
*=db: 'bird' added to 'B|dog|abacus'.
*=db: 'bird-addons' added to 'B|dog|abacus'.
*=deleting './i/bird_1.dsc'...
*=deleting './i/bird_1.tar.gz'...
*=deleting './i/bird_1_abacus.deb'...
*=deleting './i/bird-addons_1_all.deb'...
*=deleting './i/test.changes'...
EOF
find temp -type f > results
dodiff results.empty results
find i -type f > results
dodiff results.empty results
cat > results.expected <<EOF
FILE                                                    LOCATION
x	    tasty/bird,tasty/bird-addons
a/1	    tasty/bird,tasty/bird-addons
dir/another	    tasty/bird,tasty/bird-addons
dir/file	    tasty/bird,tasty/bird-addons
dir/subdir/file	    tasty/bird,tasty/bird-addons
EOF
gunzip -c dists/B/Contents-abacus.gz > results
dodiff results.expected results
cat > results.expected <<EOF
Package: bird
Version: 1
Architecture: abacus
Installed-Size: 20
Maintainer: me <guess@who>
Priority: superfluous
Section: tasty
Filename: pool/dog/b/bird/bird_1_abacus.deb
Size: $DEBSIZE
MD5sum: $DEBMD5
Description: bla
 blub

Package: bird-addons
Version: 1
Architecture: all
Installed-Size: 24
Maintainer: me <guess@who>
Source: bird
Priority: superfluous
Section: tasty
Filename: pool/dog/b/bird/bird-addons_1_all.deb
Size: $DEBASIZE
MD5sum: $DEBAMD5
Description: bla
 blub

EOF
dodiff results.expected dists/B/dog/binary-abacus/Packages
cat > results.expected <<EOF
Package: bird
Format: 1.0
Version: 1
Binary: bird, bird-addons
Maintainer: me <guess@who>
Architecture: any
Standards-Version: 0.0
Priority: superfluous
Section: tasty
Directory: pool/dog/b/bird
Files: 
 $DSCMD5S bird_1.dsc
 $TARMD5S bird_1.tar.gz

EOF
gunzip -c dists/B/dog/source/Sources.gz > results
dodiff results.expected results

echo "DebOverride: debo" >> conf/distributions
echo "DscOverride: dsco" >> conf/distributions
mkdir override
echo "bird Section cat/tasty" > override/debo
echo "bird Priority hungry" >> override/debo
echo "bird Task lunch" >> override/debo
echo "bird-addons Section cat/ugly" >> override/debo
echo "bird Section cat/nest" > override/dsco
echo "bird Priority hurry" >> override/dsco
echo "bird Homepage gopher://tree" >> override/dsco

mv i2/* i/
rmdir i2
testrun - -V -b . import default 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
*=Created directory "./pool/cat"
*=Created directory "./pool/cat/b"
*=Created directory "./pool/cat/b/bird"
*=Exporting indices...
*= generating Contents-abacus...
*=Reading filelist for pool/cat/b/bird/bird_1_abacus.deb
*=Reading filelist for pool/cat/b/bird/bird-addons_1_all.deb
stdout
*=db: 'bird' added to 'B|cat|source'.
*=db: 'bird' added to 'B|cat|abacus'.
*=db: 'bird-addons' added to 'B|cat|abacus'.
*=deleting './i/bird_1.dsc'...
*=deleting './i/bird_1.tar.gz'...
*=deleting './i/bird_1_abacus.deb'...
*=deleting './i/bird-addons_1_all.deb'...
*=deleting './i/test.changes'...
EOF
find temp -type f > results
dodiff results.empty results
find i -type f > results
dodiff results.empty results
cat > results.expected <<EOF
FILE                                                    LOCATION
x	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
a/1	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
dir/another	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
dir/file	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
dir/subdir/file	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
EOF
gunzip -c dists/B/Contents-abacus.gz > results
dodiff results.expected results
cat > results.expected <<EOF
Package: bird
Version: 1
Architecture: abacus
Installed-Size: 20
Maintainer: me <guess@who>
Task: lunch
Priority: hungry
Section: cat/tasty
Filename: pool/cat/b/bird/bird_1_abacus.deb
Size: $DEBSIZE
MD5sum: $DEBMD5
Description: bla
 blub

Package: bird-addons
Version: 1
Architecture: all
Installed-Size: 24
Maintainer: me <guess@who>
Source: bird
Priority: superfluous
Section: cat/ugly
Filename: pool/cat/b/bird/bird-addons_1_all.deb
Size: $DEBASIZE
MD5sum: $DEBAMD5
Description: bla
 blub

EOF
dodiff results.expected dists/B/cat/binary-abacus/Packages
cat > results.expected <<EOF
Package: bird
Format: 1.0
Version: 1
Binary: bird, bird-addons
Maintainer: me <guess@who>
Architecture: any
Standards-Version: 0.0
Homepage: gopher://tree
Priority: hurry
Section: cat/nest
Directory: pool/cat/b/bird
Files: 
 $DSCMD5S bird_1.dsc
 $TARMD5S bird_1.tar.gz

EOF
BIRDDSCMD5S="$DSCMD5S"
BIRDTARMD5S="$TARMD5S"
gunzip -c dists/B/cat/source/Sources.gz > results
dodiff results.expected results

# now missing: checking what all can go wrong in a .changes or .dsc file...
mkdir pkg
mkdir pkg/a
touch pkg/a/b
mkdir pkg/DEBIAN
cat > pkg/DEBIAN/control <<EOF
Package: indebname
Version: 1:versionindeb~1
Source: sourceindeb (sourceversionindeb)
EOF
dpkg-deb -b pkg i/debfilename_debfileversion~2_coal.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_coal.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_coal.deb)"
cat > i/test.changes <<EOF
EOF
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Could only find spaces within './i/test.changes'!
*=There have been errors!
EOF
cat > i/test.changes <<EOF
Dummyfield: test
EOF
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Source' field!
*=There have been errors!
EOF
echo "Source: sourceinchanges" > i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Binary' field!
*=There have been errors!
EOF
echo "Binary: binaryinchanges" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Architecture' field!
*=There have been errors!
EOF
echo "Architecture: funny" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Version' field!
*=There have been errors!
EOF
echo "Version: 999:versioninchanges-0~" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Distribution' field!
*=There have been errors!
EOF
echo "Distribution: A" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Files' field!
*=There have been errors!
EOF
echo "Files:" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=In 'test.changes': Empty 'Files' section!
*=There have been errors!
EOF
# as it does not look for the file, but scanned the directory
# and looked for it, there is no problem here, though it might
# look like one
echo " md5sum size - - ../ööü_v_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 249
stderr
=Data seems not to be signed trying to use directly...
*=In 'test.changes': file '../ööü_v_all.deb' not found in the incoming dir!
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " md5sum size - - \300\257.\300\257_v_funny.deb" >> i/test.changes
touch "$(echo -e 'i/\300\257.\300\257_v_funny.deb')"
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*='test.changes' lists architecture 'funny' not found in distribution 'A'!
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " md5sum size - - \300\257.\300\257_v_all.deb" >> i/test.changes
mv "$(echo -e 'i/\300\257.\300\257_v_funny.deb')" "$(echo -e 'i/\300\257.\300\257_v_all.deb')"
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*='all' is not listed in the Architecture header of 'test.changes' but file 'À¯.À¯_v_all.deb' looks like it!
*=There have been errors!
EOF
sed -i -e 's/funny/all/' i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Invalid filename 'À¯.À¯_v_all.deb' listed in 'test.changes': contains 8-bit characters
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " md5sum size - - debfilename_debfileversion~2_coal.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*='coal' is not listed in the Architecture header of 'test.changes' but file 'debfilename_debfileversion~2_coal.deb' looks like it!
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " md5sum size - - debfilename_debfileversion~2_all.deb" >> i/test.changes
mv i/debfilename_debfileversion~2_coal.deb i/debfilename_debfileversion~2_all.deb
# // TODO: that should be ERROR: instead of WARNING:
testrun - -V -b . import default 3<<EOF
returns 254
stderr
=Data seems not to be signed trying to use directly...
*=WARNING: './i/debfilename_debfileversion~2_all.deb' has md5sum '$DEBMD5S', while 'md5sum size' was expected.
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
# TODO: these will hopefully change to not divulge the place of the temp dir some day...
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Cannot find Maintainer-header in control file of ./temp/debfilename_debfileversion~2_all.deb!
*=There have been errors!
EOF
echo "Maintainer: noone <me@nowhere>" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Cannot find Description-header in control file of ./temp/debfilename_debfileversion~2_all.deb!
*=There have been errors!
EOF
echo ...
echo "Description: test-package" >> pkg/DEBIAN/control
echo " a package to test reprepro" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Cannot find Architecture-header in control file of ./temp/debfilename_debfileversion~2_all.deb!
*=There have been errors!
EOF
echo "Architecture: coal" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Name part of filename ('debfilename') and name within the file ('indebname') do not match for 'debfilename_debfileversion~2_all.deb' in 'test.changes'!
*=There have been errors!
EOF
mv i/debfilename_debfileversion~2_all.deb i/indebname_debfileversion~2_all.deb
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Architecture 'coal' of 'indebname_debfileversion~2_all.deb' does not match 'all' specified in 'test.changes'!
*=There have been errors!
EOF
sed -i -e "s/^Architecture: coal/Architecture: all/" pkg/DEBIAN/control
dpkg-deb -b pkg i/indebname_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/indebname_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/indebname_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Source-header 'sourceinchanges' of 'test.changes' and source name 'sourceindeb' within the file 'indebname_debfileversion~2_all.deb' do not match!
*=There have been errors!
EOF
sed -i -e 's/sourceinchanges/sourceindeb/' i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Version-header '999:versioninchanges-0~' of 'test.changes' and source version 'sourceversionindeb' within the file 'indebname_debfileversion~2_all.deb' do not match!
*=There have been errors!
EOF
sed -i -e 's/999:versioninchanges-0~/sourceversionindeb/' i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Name 'indebname' of binary 'indebname_debfileversion~2_all.deb' is not listed in Binaries-header of 'test.changes'!
*=There have been errors!
EOF
sed -i -e 's/binaryinchanges/indebname/' i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No section found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
*=There have been errors!
EOF
echo "Section: test" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/indebname_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/indebname_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/indebname_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No section found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S test - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No priority found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
*=There have been errors!
EOF
echo "Priority: survival" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/indebname_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/indebname_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/indebname_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S test - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No priority found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S section priority indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 0
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Created directory "./pool/dog/s"
*=Created directory "./pool/dog/s/sourceindeb"
*=Exporting indices...
stdout
*=db: 'indebname' added to 'A|dog|abacus'.
*=db: 'indebname' added to 'A|dog|calculator'.
*=deleting './i/indebname_debfileversion~2_all.deb'...
*=deleting './i/test.changes'...
EOF
find pool/dog/s -type f > results
echo "pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb" > results.expected
dodiff results.expected results

touch i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
cat > i/test.changes <<EOF
Source: sourceinchanges
Binary: nothing
Architecture: all
Version: 1:versioninchanges
Distribution: A
Files:
 md5sum size - - dscfilename_fileversion~.dsc
EOF
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*='source' is not listed in the Architecture header of 'test.changes' but file 'dscfilename_fileversion~.dsc' looks like it!
*=There have been errors!
EOF
sed -i -e 's/^Architecture: all$/Architecture: source/' i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*='test.changes' lists architecture 'source' not found in distribution 'A'!
*=There have been errors!
EOF
sed -i -e 's/^Distribution: A$/Distribution: B/' i/test.changes
testrun - -V -b . import default 3<<EOF
returns 254
stderr
=Data seems not to be signed trying to use directly...
*=WARNING: './i/dscfilename_fileversion~.dsc' has md5sum 'd41d8cd98f00b204e9800998ecf8427e 0', while 'md5sum size' was expected.
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Could only find spaces within './temp/dscfilename_fileversion~.dsc'!
*=There have been errors!
EOF
echo "Dummyheader:" > i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Missing 'Source'-header in ./temp/dscfilename_fileversion~.dsc!
*=There have been errors!
EOF
echo "Source: nameindsc" > i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Cannot find 'Format'-header in ./temp/dscfilename_fileversion~.dsc!
*=There have been errors!
EOF
echo "Format: 1.0" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Cannot find 'Maintainer'-header in ./temp/dscfilename_fileversion~.dsc!
*=There have been errors!
EOF
echo "Maintainer: guess who <me@nowhere>" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Cannot find 'Standards-Version'-header in ./temp/dscfilename_fileversion~.dsc!
*=Missing 'Version'-header in ./temp/dscfilename_fileversion~.dsc!
*=There have been errors!
EOF
echo "Standards-Version: 0" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Missing 'Version'-header in ./temp/dscfilename_fileversion~.dsc!
*=There have been errors!
EOF
echo "Version: versionindsc" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Missing 'Files'-header in ./temp/dscfilename_fileversion~.dsc!
*=There have been errors!
EOF
echo "Files:  " >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Name part of filename ('dscfilename') and name within the file ('nameindsc') do not match for 'dscfilename_fileversion~.dsc' in 'test.changes'!
*=There have been errors!
EOF
sed -i 's/^Source: nameindsc$/Source: dscfilename/g' i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Source-header 'sourceinchanges' of 'test.changes' and name 'dscfilename' within the file 'dscfilename_fileversion~.dsc' do not match!
*=There have been errors!
EOF
sed -i 's/^Source: sourceinchanges$/Source: dscfilename/' i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Version-header '1:versioninchanges' of 'test.changes' and version 'versionindsc' within the file 'dscfilename_fileversion~.dsc' do not match!
*=There have been errors!
EOF
sed -i 's/^Version: 1:versioninchanges$/Version: versionindsc/' i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No section found for 'dscfilename' ('dscfilename_fileversion~.dsc' in 'test.changes')!
*=There have been errors!
EOF
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No priority found for 'dscfilename' ('dscfilename_fileversion~.dsc' in 'test.changes')!
*=There have been errors!
EOF
echo -e "g/^Format:/d\nw\nq\n" | ed -s i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy can't-live-without dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Cannot find 'Format'-header in ./temp/dscfilename_fileversion~.dsc!
*=There have been errors!
EOF
echo -e "1i\nFormat: 1.0\n.\nw\nq\n" | ed -s i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
OLDDSCFILENAMEMD5S="$DSCMD5S"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy can't-live-without dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 0
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Created directory "./pool/dog/d"
*=Created directory "./pool/dog/d/dscfilename"
=Exporting indices...
stdout
*=db: 'dscfilename' added to 'B|dog|source'.
*=deleting './i/dscfilename_fileversion~.dsc'...
*=deleting './i/test.changes'...
EOF
# TODO: check Sources.gz
cat >i/strangefile <<EOF
just a line to make it non-empty
EOF
cat >i/dscfilename_fileversion~.dsc <<EOF
Format: 1.0
Source: dscfilename
Maintainer: guess who <me@nowhere>
Standards-Version: 0
Version: 1:newversion~
Files:
 md5sumindsc sizeindsc strangefile
EOF
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
cat >i/test.changes <<EOF
Source: dscfilename
Binary: nothing
Architecture: source
Version: 1:newversion~
Distribution: B
Files:
 $DSCMD5S dummy can't-live-without dscfilename_fileversion~.dsc
EOF
# this is a stupid error message, needs to get some context
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=Error in parsing size or missing space afterwards!
*=There have been errors!
EOF
sed -i "s/ sizeindsc / 666 /" i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy unneeded dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=file 'strangefile' is needed for 'dscfilename_fileversion~.dsc', not yet registered in the pool and not found in 'test.changes'
*=There have been errors!
EOF
echo " md5suminchanges 666 - - strangefile" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
*=No underscore in filename in 'md5suminchanges 666 - - strangefile'!
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " md5suminchanges 666 - - strangefile_xyz" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 249
stderr
=Data seems not to be signed trying to use directly...
=Unknown filetype: 'md5suminchanges 666 - - strangefile_xyz', assuming to be source format...
*=In 'test.changes': file 'strangefile_xyz' not found in the incoming dir!
*=There have been errors!
EOF
mv i/strangefile i/strangefile_xyz
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Unknown filetype: 'md5suminchanges 666 - - strangefile_xyz', assuming to be source format...
*=file 'strangefile' is needed for 'dscfilename_fileversion~.dsc', not yet registered in the pool and not found in 'test.changes'
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " md5sumindsc 666 - - strangefile_xyz" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 254
stderr
=Data seems not to be signed trying to use directly...
=Unknown filetype: 'md5sumindsc 666 - - strangefile_xyz', assuming to be source format...
*=WARNING: './i/strangefile_xyz' has md5sum '31a1096ff883d52f0c1f39e652d6336f 33', while 'md5sumindsc 666' was expected.
*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/dscfilename_fileversion~.dsc
echo " 31a1096ff883d52f0c1f39e652d6336f 33 strangefile_xyz" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
DSCFILENAMEMD5S="$DSCMD5S"
echo -e '$-1,$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy unneeded dscfilename_fileversion~.dsc" >> i/test.changes
echo " 33a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 255
stderr
=Data seems not to be signed trying to use directly...
=Unknown filetype: '33a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz', assuming to be source format...
*=file 'strangefile_xyz' is listed with md5sum '33a1096ff883d52f0c1f39e652d6336f 33' in 'test.changes' but with md5sum '31a1096ff883d52f0c1f39e652d6336f 33' in 'dscfilename_fileversion~.dsc'!
*=There have been errors!
EOF
find pool -type f | sort > results
cat > results.expected <<EOF
pool/cat/b/bird/bird_1_abacus.deb
pool/cat/b/bird/bird_1.dsc
pool/cat/b/bird/bird_1.tar.gz
pool/cat/b/bird/bird-addons_1_all.deb
pool/dog/b/bird/bird_1_abacus.deb
pool/dog/b/bird/bird_1.dsc
pool/dog/b/bird/bird_1.tar.gz
pool/dog/b/bird/bird-addons_1_all.deb
pool/dog/d/dscfilename/dscfilename_versionindsc.dsc
pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb
EOF
dodiff results.expected results
find dists -type f | sort > results
cat > results.expected <<EOF
dists/A/cat/binary-abacus/Packages
dists/A/cat/binary-abacus/Packages.gz
dists/A/cat/binary-abacus/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-abacus/Packages
dists/A/dog/binary-abacus/Packages.gz
dists/A/dog/binary-abacus/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-abacus/Packages
dists/B/cat/binary-abacus/Packages.gz
dists/B/cat/binary-abacus/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-abacus.gz
dists/B/dog/binary-abacus/Packages
dists/B/dog/binary-abacus/Packages.gz
dists/B/dog/binary-abacus/Release
dists/B/dog/source/Release
dists/B/dog/source/Sources.gz
dists/B/Release
EOF
dodiff results.expected results
gunzip -c dists/B/dog/source/Sources.gz > results
cat > results.expected <<EOF
Package: bird
Format: 1.0
Version: 1
Binary: bird, bird-addons
Maintainer: me <guess@who>
Architecture: any
Standards-Version: 0.0
Priority: superfluous
Section: tasty
Directory: pool/dog/b/bird
Files: 
 $BIRDDSCMD5S bird_1.dsc
 $BIRDTARMD5S bird_1.tar.gz

Package: dscfilename
Format: 1.0
Maintainer: guess who <me@nowhere>
Standards-Version: 0
Version: versionindsc
Priority: can't-live-without
Section: dummy
Directory: pool/dog/d/dscfilename
Files: 
 $OLDDSCFILENAMEMD5S dscfilename_versionindsc.dsc

EOF
dodiff results.expected results
testout "" -b . dumpunreferenced
dodiff results.empty results
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " 31a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz" >> i/test.changes
testrun - -V -b . import default 3<<EOF
returns 0
stderr
=Data seems not to be signed trying to use directly...
=Unknown filetype: '31a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz', assuming to be source format...
=Exporting indices...
=Deleting files no longer referenced...
stdout
*=db: removed old 'dscfilename' from 'B|dog|source'.
*=db: 'dscfilename' added to 'B|dog|source'.
*=deleting './i/dscfilename_fileversion~.dsc'...
*=deleting './i/test.changes'...
*=deleting './i/strangefile_xyz'...
*=deleting and forgetting pool/dog/d/dscfilename/dscfilename_versionindsc.dsc
EOF

find pool -type f | sort > results
cat > results.expected <<EOF
pool/cat/b/bird/bird_1_abacus.deb
pool/cat/b/bird/bird_1.dsc
pool/cat/b/bird/bird_1.tar.gz
pool/cat/b/bird/bird-addons_1_all.deb
pool/dog/b/bird/bird_1_abacus.deb
pool/dog/b/bird/bird_1.dsc
pool/dog/b/bird/bird_1.tar.gz
pool/dog/b/bird/bird-addons_1_all.deb
pool/dog/d/dscfilename/dscfilename_newversion~.dsc
pool/dog/d/dscfilename/strangefile_xyz
pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb
EOF
dodiff results.expected results
find dists -type f | sort > results
cat > results.expected <<EOF
dists/A/cat/binary-abacus/Packages
dists/A/cat/binary-abacus/Packages.gz
dists/A/cat/binary-abacus/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-abacus/Packages
dists/A/dog/binary-abacus/Packages.gz
dists/A/dog/binary-abacus/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-abacus/Packages
dists/B/cat/binary-abacus/Packages.gz
dists/B/cat/binary-abacus/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-abacus.gz
dists/B/dog/binary-abacus/Packages
dists/B/dog/binary-abacus/Packages.gz
dists/B/dog/binary-abacus/Release
dists/B/dog/source/Release
dists/B/dog/source/Sources.gz
dists/B/Release
EOF
dodiff results.expected results
gunzip -c dists/B/dog/source/Sources.gz > results
cat > results.expected <<EOF
Package: bird
Format: 1.0
Version: 1
Binary: bird, bird-addons
Maintainer: me <guess@who>
Architecture: any
Standards-Version: 0.0
Priority: superfluous
Section: tasty
Directory: pool/dog/b/bird
Files: 
 $BIRDDSCMD5S bird_1.dsc
 $BIRDTARMD5S bird_1.tar.gz

Package: dscfilename
Format: 1.0
Maintainer: guess who <me@nowhere>
Standards-Version: 0
Version: 1:newversion~
Priority: unneeded
Section: dummy
Directory: pool/dog/d/dscfilename
Files: 
 $DSCFILENAMEMD5S dscfilename_newversion~.dsc
 31a1096ff883d52f0c1f39e652d6336f 33 strangefile_xyz

EOF
dodiff results.expected results

testout "" -b . dumpunreferenced
dodiff results.empty results

#echo "preliminary finish due to testing"
#exit 0
rm -r conf db pool dists i pkg
#echo "preliminary finish due to testing"
#exit 0
###############################################################################

mkdir -p conf
cat > conf/options <<CONFEND
export changed
CONFEND
cat > conf/distributions <<CONFEND
Codename: test1
Architectures: abacus source
Components: stupid ugly
Update: Test2toTest1
DebIndices: Packages Release . .gz .bz2
UDebIndices: Packages .gz .bz2
DscIndices: Sources Release .gz .bz2
Tracking: keep includechanges includebyhand

Codename: test2
Architectures: abacus coal source
Components: stupid ugly
Origin: Brain
Label: Only a test
Suite: broken
Version: 9999999.02
DebIndices: Packages Release . .gz $SRCDIR/docs/bzip.example
UDebIndices: Packages .gz
DscIndices: Sources Release . .gz $SRCDIR/docs/bzip.example
Description: test with all fields set
DebOverride: binoverride
DscOverride: srcoverride
CONFEND

set -v
testrun "" -b . export
test -f dists/test1/Release
test -f dists/test2/Release

EMPTYGZMD5SUM=7029066c27ac6f5ef18d660d5741979a
EMPTYBZ2MD5SUM=4059d198768f9f8dc9372dc1c54bc3c3
cat > dists/test1/Release.expected <<END
Codename: test1
Date: normalized
Architectures: abacus
Components: stupid ugly
MD5Sum:
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-abacus/Packages
 $EMPTYGZMD5SUM 20 stupid/binary-abacus/Packages.gz
 $EMPTYBZ2MD5SUM 14 stupid/binary-abacus/Packages.bz2
 d9f0fad5d54ad09dd4ecee86c73b64d4 39 stupid/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/source/Sources
 $EMPTYGZMD5SUM 20 stupid/source/Sources.gz
 $EMPTYBZ2MD5SUM 14 stupid/source/Sources.bz2
 e38c7da133734e1fd68a7e344b94fe96 39 stupid/source/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-abacus/Packages
 $EMPTYGZMD5SUM 20 ugly/binary-abacus/Packages.gz
 $EMPTYBZ2MD5SUM 14 ugly/binary-abacus/Packages.bz2
 236fcd9339b1813393819d464e37c7c6 37 ugly/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/source/Sources
 $EMPTYGZMD5SUM 20 ugly/source/Sources.gz
 $EMPTYBZ2MD5SUM 14 ugly/source/Sources.bz2
 ed4ee9aa5d080f67926816133872fd02 37 ugly/source/Release
END
cat > dists/test2/Release.expected <<END
Origin: Brain
Label: Only a test
Suite: broken
Codename: test2
Version: 9999999.02
Date: normalized
Architectures: abacus coal
Components: stupid ugly
Description: test with all fields set
MD5Sum:
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-abacus/Packages
 $EMPTYGZMD5SUM 20 stupid/binary-abacus/Packages.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/binary-abacus/Packages.bz2
 e142c47c1be0c32cd120138066b73c73 146 stupid/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-coal/Packages
 $EMPTYGZMD5SUM 20 stupid/binary-coal/Packages.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/binary-coal/Packages.bz2
 10ae2f283e1abdd3facfac6ed664035d 144 stupid/binary-coal/Release
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/source/Sources
 $EMPTYGZMD5SUM 20 stupid/source/Sources.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/source/Sources.bz2
 b923b3eb1141e41f0b8bb74297ac8a36 146 stupid/source/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-abacus/Packages
 $EMPTYGZMD5SUM 20 ugly/binary-abacus/Packages.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/binary-abacus/Packages.bz2
 22eb57e60d3c621b8bd8461eae218b16 144 ugly/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-coal/Packages
 $EMPTYGZMD5SUM 20 ugly/binary-coal/Packages.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/binary-coal/Packages.bz2
 7a05de3b706d08ed06779d0ec2e234e9 142 ugly/binary-coal/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/source/Sources
 $EMPTYGZMD5SUM 20 ugly/source/Sources.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/source/Sources.bz2
 e73a8a85315766763a41ad4dc6744bf5 144 ugly/source/Release
END
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\n%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq' | ed -s dists/test1/Release
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\n%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq' | ed -s dists/test2/Release
dodiff dists/test1/Release.expected dists/test1/Release || exit 1
dodiff dists/test2/Release.expected dists/test2/Release || exit 1

cat > include.rules <<EOF
stderr
=Data seems not to be signed trying to use directly...
=Exporting indices...
EOF
cat > includedel.rules <<EOF
stderr
=Data seems not to be signed trying to use directly...
=Exporting indices...
*=Deleting files no longer referenced...
EOF

PACKAGE=simple EPOCH="" VERSION=1 REVISION="" SECTION="stupid/base" genpackage.sh
testrun include -b . include test1 test.changes
echo returned: $?

PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=0.9-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/base" genpackage.sh
testrun include -b . include test1 test.changes
echo returned: $?

cat >remove.rules <<EOF
stderr
=Exporting indices...
*=Deleting files no longer referenced...
EOF
testrun remove -b . -Tdsc remove test1 simple 
testrun remove -b . -Tdeb remove test1 bloat+-0a9z.app
testrun remove -b . -A source remove test1 bloat+-0a9z.app
testrun remove -b . -A abacus remove test1 simple
testrun remove -b . -C ugly remove test1 bloat+-0a9z.app-addons
testrun remove -b . -C stupid remove test1 simple-addons
CURDATE="`TZ=GMT LC_ALL=C date +'%a, %d %b %Y %H:%M:%S +0000'`"
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\n%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq' | ed -s dists/test1/Release

dodiff dists/test1/Release.expected dists/test1/Release || exit 1

mkdir -p override
cat > override/srcoverride <<END
simple Section ugly/games
simple Priority optional
simple Maintainer simple.source.maintainer
bloat+-0a9z.app Section stupid/X11
bloat+-0a9z.app Priority optional
bloat+-0a9z.app X-addition totally-unsupported
bloat+-0a9z.app Maintainer bloat.source.maintainer
END
cat > override/binoverride <<END
simple Maintainer simple.maintainer
simple Section ugly/base
simple Priority optional
simple-addons Section ugly/addons
simple-addons Priority optional
simple-addons Maintainer simple.add.maintainer
bloat+-0a9z.app Maintainer bloat.maintainer
bloat+-0a9z.app Section stupid/base
bloat+-0a9z.app Priority optional
bloat+-0a9z.app-addons Section stupid/addons
bloat+-0a9z.app-addons Maintainer bloat.add.maintainer
bloat+-0a9z.app-addons Priority optional
END

testrun include -b . -Tdsc -A source includedsc test2 simple_1.dsc
testrun include -b . -Tdsc -A source includedsc test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
testrun include -b . -Tdeb -A abacus includedeb test2 simple_1_abacus.deb
testrun include -b . -Tdeb -A coal includedeb test2 simple-addons_1_all.deb
testrun include -b . -Tdeb -A abacus includedeb test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb
testrun include -b . -Tdeb -A coal includedeb test2 bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
find dists/test2/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^\(Package\|Maintainer\|Section\|Priority\): ' > results
cat >results.expected <<END
dists/test2/stupid/binary-abacus/Packages.gz:Package: bloat+-0a9z.app
dists/test2/stupid/binary-abacus/Packages.gz:Maintainer: bloat.maintainer
dists/test2/stupid/binary-abacus/Packages.gz:Priority: optional
dists/test2/stupid/binary-abacus/Packages.gz:Section: stupid/base
dists/test2/stupid/binary-coal/Packages.gz:Package: bloat+-0a9z.app-addons
dists/test2/stupid/binary-coal/Packages.gz:Maintainer: bloat.add.maintainer
dists/test2/stupid/binary-coal/Packages.gz:Priority: optional
dists/test2/stupid/binary-coal/Packages.gz:Section: stupid/addons
dists/test2/stupid/source/Sources.gz:Package: bloat+-0a9z.app
dists/test2/stupid/source/Sources.gz:Maintainer: bloat.source.maintainer
dists/test2/stupid/source/Sources.gz:Priority: optional
dists/test2/stupid/source/Sources.gz:Section: stupid/X11
dists/test2/ugly/binary-abacus/Packages.gz:Package: simple
dists/test2/ugly/binary-abacus/Packages.gz:Maintainer: simple.maintainer
dists/test2/ugly/binary-abacus/Packages.gz:Priority: optional
dists/test2/ugly/binary-abacus/Packages.gz:Section: ugly/base
dists/test2/ugly/binary-coal/Packages.gz:Package: simple-addons
dists/test2/ugly/binary-coal/Packages.gz:Maintainer: simple.add.maintainer
dists/test2/ugly/binary-coal/Packages.gz:Priority: optional
dists/test2/ugly/binary-coal/Packages.gz:Section: ugly/addons
dists/test2/ugly/source/Sources.gz:Package: simple
dists/test2/ugly/source/Sources.gz:Maintainer: simple.source.maintainer
dists/test2/ugly/source/Sources.gz:Priority: optional
dists/test2/ugly/source/Sources.gz:Section: ugly/games
END
dodiff results.expected results
rm results
testout "" -b . listfilter test2 'Source(==simple)|(!Source,Package(==simple))'
ls -la results
cat > results.expected << END
test2|ugly|abacus: simple 1
test2|ugly|coal: simple-addons 1
test2|ugly|source: simple 1
END
dodiff results.expected results
testout "" -b . listfilter test2 'Source(==bloat+-0a9z.app)|(!Source,Package(==bloat+-0a9z.app))'
cat > results.expected << END
test2|stupid|abacus: bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
test2|stupid|coal: bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
test2|stupid|source: bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
END
dodiff results.expected results

cat >conf/updates <<END
Name: Test2toTest1
Method: copy:$WORKDIR
Suite: test2
Architectures: coal>abacus abacus source
FilterFormula: Priority(==optional),Package(>=alpha),Package(<=zeta)
FilterList: error list
ListHook: /bin/cp
END

cat >conf/list <<END
simple-addons		install
bloat+-0a9z.app 	install
simple			install
bloat+-0a9z.app-addons	install
END

cat >update.rules <<EOF
stderr
=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
=Calculating packages to get...
=Getting packages...
=Installing (and possibly deleting) packages...
=Exporting indices...
EOF
cat >emptyupdate.rules <<EOF
=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
*=Nothing to do found. (Use --noskipold to force processing)
EOF
cat >nolistsupdate.rules <<EOF
*=Ignoring --skipold because of --nolistsdownload
=WARNING: Single-Instance not yet supported!
=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
*=Warning: As --nolistsdownload is given, index files are NOT checked.
=Calculating packages to get...
=Getting packages...
*=Installing (and possibly deleting) packages...
EOF

testrun update -b . $UPDATETYPE test1
testrun emptyupdate -b . $UPDATETYPE test1
testrun nolistsupdate --nolistsdownload -b . $UPDATETYPE test1
find dists/test2/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' | sed -e 's/test2/test1/' -e 's/coal/abacus/' > test2
find dists/test1/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' > test1
dodiff test2 test1

testrun "" -b . check test1 test2
testrun "" -b . checkpool
testrun "" -b . rereference test1 test2
testrun "" -b . check test1 test2

testout "" -b . dumptracks
cat >results.expected <<END
Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/test.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple-addons_1_all.deb a 0
 pool/stupid/s/simple/simple_1_abacus.deb b 0
 pool/stupid/s/simple/simple_1.dsc s 0
 pool/stupid/s/simple/simple_1.tar.gz s 0
 pool/stupid/s/simple/test.changes c 0

END
dodiff results.expected results

testout "" -b . dumpunreferenced
dodiff results.empty results 
cat >delete.rules <<EOF
stderr
=Deleting files no longer referenced...
EOF
testrun delete -b . cleartracks
echo returned: $?
dodiff results.empty results 
testrun include -b . include test1 test.changes
echo returned: $?
OUTPUT=test2.changes PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=9.0-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/extra" genpackage.sh
testrun includedel -b . include test1 test2.changes
echo returned: $?
testrun include -b . -S test -P test includedeb test1 simple_1_abacus.deb
echo returned: $?
testrun include -b . -S test -P test includedsc test1 simple_1.dsc
echo returned: $?

testout "" -b . dumptracks
cat >results.expected <<END
Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/test.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_abacus.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/test2.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_abacus.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

END
dodiff results.expected results
testout "" -b . dumpunreferenced
dodiff results.empty results 

echo "now testing .orig.tar.gz handling"
tar -czf test_1.orig.tar.gz test.changes
PACKAGE=test EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh -si
testrun - -b . include test1 test.changes 3<<EOF
returns 249
stderr
=Data seems not to be signed trying to use directly...
*=Unable to find ./pool/stupid/t/test/test_1.orig.tar.gz!
*=Perhaps you forgot to give dpkg-buildpackage the -sa option,
*= or you cound try --ignore=missingfile
*=There have been errors!
EOF
testrun - -b . --ignore=missingfile include test1 test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
*=Unable to find ./pool/stupid/t/test/test_1.orig.tar.gz!
*=Looking around if it is elsewhere as --ignore=missingfile given.
*=Exporting indices...
EOF
dodo zgrep test_1-2.dsc dists/test1/stupid/source/Sources.gz

tar -czf testb_2.orig.tar.gz test.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-2" SECTION="stupid/base" genpackage.sh -sa
testrun include -b . include test1 test.changes
dodo zgrep testb_2-2.dsc dists/test1/stupid/source/Sources.gz
rm test2.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-3" SECTION="stupid/base" OUTPUT="test2.changes" genpackage.sh -sd
testrun includedel -b . include test1 test2.changes
dodo zgrep testb_2-3.dsc dists/test1/stupid/source/Sources.gz

testout "" -b . dumpunreferenced
dodiff results.empty results 

echo "now testing some error messages:"
PACKAGE=4test EPOCH="1:" VERSION=b.1 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun -  -b . include test1 test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Package version 'b.1-1.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Package version 'b.1-1_all.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Exporting indices...
EOF

cat >includeerror.rules <<EOF
returns 255
stderr
*=There have been errors!
=reprepro [--delete] include <distribution> <.changes-file>
EOF
testrun includeerror -b . include unknown 3<<EOF
testrun includeerror -b . include unknown test.changes test2.changes
testrun - -b . include unknown test.changes 3<<EOF
stderr
*=There have been errors!
*=No distribution definition of 'unknown' found in './conf/distributions'!
returns 249
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results 
mkdir conf2
testrun - -b . --confdir conf2 update 3<<EOF
returns 249
stderr
*=Could not find 'conf2/distributions'!
=(Have you forgotten to specify a basedir by -b?
=To only set the conf/ dir use --confdir)
*=There have been errors!
EOF
touch conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
returns 249
stderr
*=No distribution definitons found!
*=There have been errors!
EOF
echo -e 'Codename: foo' > conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
stderr
*=While parsing distribution definition, required field Architectures not found!
*=There have been errors!
returns 249
EOF
echo -e 'Architectures: abacus fingers' >> conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
*=While parsing distribution definition, required field Components not found!
*=There have been errors!
returns 249
EOF
echo -e 'Components: unneeded bloated i386' >> conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
*=Unable to open file conf2/updates: No such file or directory
*=There have been errors!
returns 254
EOF
touch conf2/updates
testrun update -b . --confdir conf2 --noskipold update
echo "Format: 2.0" > broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Date' field!
=To Ignore use --ignore=missingfield.
*=There have been errors!
returns 255
EOF
echo "Date: today" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Source' field
*=There have been errors!
returns 255
EOF
echo "Source: nowhere" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Binary' field
*=There have been errors!
returns 255
EOF
echo "Binary: phantom" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Architecture' field
*=There have been errors!
returns 255
EOF
echo "Architecture: brain" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Version' field
*=There have been errors!
returns 255
EOF
echo "Version: old" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=In 'broken.changes': Missing 'Distribution' field
*=There have been errors!
returns 255
EOF
echo "Distribution: old" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=In 'broken.changes': Missing 'Urgency' field!
=To Ignore use --ignore=missingfield.
*=There have been errors!
returns 255
EOF
echo "Distribution: old" >> broken.changes
testrun - -b . --ignore=missingfield include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=In 'broken.changes': Missing 'Urgency' field!
*=In 'broken.changes': Missing 'Maintainer' field!
*=In 'broken.changes': Missing 'Description' field!
*=In 'broken.changes': Missing 'Changes' field!
=Ignoring as --ignore=missingfield given.
*=In 'broken.changes': Missing 'Files' field!
*=There have been errors!
returns 255
EOF
echo "Files:" >> broken.changes
testrun - -b . --ignore=missingfield include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=In 'broken.changes': Missing 'Urgency' field!
*=In 'broken.changes': Missing 'Maintainer' field!
*=In 'broken.changes': Missing 'Description' field!
*=In 'broken.changes': Missing 'Changes' field!
*=broken.changes: Not enough files in .changes!
=Ignoring as --ignore=missingfield given.
*=There have been errors!
returns 255
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results 
echo " d41d8cd98f00b204e9800998ecf8427e 0 section priority filename_version.tar.gz" >> broken.changes
testrun - -b . --ignore=missingfield include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
=In 'broken.changes': Missing 'Urgency' field!
=Ignoring as --ignore=missingfield given.
=In 'broken.changes': Missing 'Maintainer' field!
=In 'broken.changes': Missing 'Description' field!
=In 'broken.changes': Missing 'Changes' field!
*=Warning: Strange file 'filename_version.tar.gz'!
=Looks like source but does not start with 'nowhere_' as I would have guessed!
=I hope you know what you do.
# grr, this message has really to improve...
=Warning: Package version 'version.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=.changes put in a distribution not listed within it!
=To ignore use --ignore=wrongdistribution.
*=There have been errors!
returns 255
EOF
testrun - -b . --ignore=unusedarch --ignore=surprisingarch --ignore=wrongdistribution --ignore=missingfield include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
=In 'broken.changes': Missing 'Urgency' field!
=Ignoring as --ignore=missingfield given.
=In 'broken.changes': Missing 'Maintainer' field!
=In 'broken.changes': Missing 'Description' field!
=In 'broken.changes': Missing 'Changes' field!
=Warning: Strange file 'filename_version.tar.gz'!
=Looks like source but does not start with 'nowhere_' as I would have guessed!
=I hope you know what you do.
# again
=Warning: Package version 'version.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=.changes put in a distribution not listed within it!
*=Ignoring as --ignore=wrongdistribution given.
*=Architecture-header lists architecture 'brain', but no files for this!
*=Ignoring as --ignore=unusedarch given.
*='filename_version.tar.gz' looks like architecture 'source', but this is not listed in the Architecture-Header!
*=Ignoring as --ignore=surprisingarch given.
*=Cannot find file './filename_version.tar.gz' needed by 'broken.changes'!
*=There have been errors!
returns 249
EOF
touch filename_version.tar.gz
testrun - -b . --ignore=unusedarch --ignore=surprisingarch --ignore=wrongdistribution --ignore=missingfield include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
=In 'broken.changes': Missing 'Urgency' field!
=Ignoring as --ignore=missingfield given.
=In 'broken.changes': Missing 'Maintainer' field!
=In 'broken.changes': Missing 'Description' field!
=In 'broken.changes': Missing 'Changes' field!
=Warning: Strange file 'filename_version.tar.gz'!
*=Looks like source but does not start with 'nowhere_' as I would have guessed!
=I hope you know what you do.
# again
=Warning: Package version 'version.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=.changes put in a distribution not listed within it!
*=Ignoring as --ignore=wrongdistribution given.
*=Architecture-header lists architecture 'brain', but no files for this!
*=Ignoring as --ignore=unusedarch given.
*='filename_version.tar.gz' looks like architecture 'source', but this is not listed in the Architecture-Header!
*=Ignoring as --ignore=surprisingarch given.
EOF
testout "" -b . dumpunreferenced
cat >results.expected <<EOF
pool/stupid/n/nowhere/filename_version.tar.gz
EOF
dodiff results.expected results 
testrun "" -b . deleteunreferenced
testout "" -b . dumpunreferenced
dodiff results.empty results 
# first remove file, then try to remove the package
testrun "" -b . _forget pool/ugly/s/simple/simple_1_abacus.deb
testrun - -b . remove test1 simple 3<<EOF
# ???
=Warning: tracking database of test1 missed files for simple_1.
*=Exporting indices...
*=Deleting files no longer referenced...
EOF
testrun - -b . remove test2 simple 3<<EOF
=Exporting indices...
=Deleting files no longer referenced...
*=To be forgotten filekey 'pool/ugly/s/simple/simple_1_abacus.deb' was not known.
*=There have been errors!
returns 249
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results 

cat > broken.changes <<EOF
Format: -1.0
Date: yesterday
Source: differently
Version: 0another
Architecture: source abacus
Urgency: super-hyper-duper-important
Maintainer: still me <guess@who>
Description: missing
Changes: missing
Binary: none and nothing
Distribution: test2
Files:
 `md5sum 4test_b.1-1.dsc| cut -d" " -f 1` `stat -c%s 4test_b.1-1.dsc` a b differently_0another.dsc
 `md5sum 4test_b.1-1_abacus.deb| cut -d" " -f 1` `stat -c%s 4test_b.1-1_abacus.deb` a b 4test_b.1-1_abacus.deb
EOF
#todo: make it work without this..
cp 4test_b.1-1.dsc differently_0another.dsc
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Strange file '4test_b.1-1.dsc'!
=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=I don't know what to do having a .dsc without a .diff.gz or .tar.gz in 'broken.changes'!
*=There have been errors!
returns 255
EOF
cat >> broken.changes <<EOF
 `md5sum 4test_b.1-1.tar.gz| cut -d" " -f 1` `stat -c%s 4test_b.1-1.tar.gz` a b 4test_b.1-1.tar.gz
EOF
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Strange file '4test_b.1-1.dsc'!
=Warning: Strange file '4test_b.1-1.tar.gz'!
=Warning: Package version 'b.1-1.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*='./pool/stupid/d/differently/4test_b.1-1_abacus.deb' has packagename '4test' not listed in the .changes file!
*=To ignore use --ignore=surprisingbinary.
*=There have been errors!
returns 255
EOF
testrun - -b . --ignore=surprisingbinary include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Strange file '4test_b.1-1.dsc'!
=Warning: Strange file '4test_b.1-1.tar.gz'!
=Warning: Package version 'b.1-1.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*='./pool/stupid/d/differently/4test_b.1-1_abacus.deb' has packagename '4test' not listed in the .changes file!
*=Ignoring as --ignore=surprisingbinary given.
*='./pool/stupid/d/differently/4test_b.1-1_abacus.deb' lists source package '4test', but .changes says it is 'differently'!
*=There have been errors!
returns 255
EOF
cat > broken.changes <<EOF
Format: -1.0
Date: yesterday
Source: 4test
Version: 0orso
Architecture: source abacus
Urgency: super-hyper-duper-important
Maintainer: still me <guess@who>
Description: missing
Changes: missing
Binary: 4test
Distribution: test2
Files:
 `md5sum 4test_b.1-1.dsc| cut -d" " -f 1` `stat -c%s 4test_b.1-1.dsc` a b 4test_0orso.dsc
 `md5sum 4test_b.1-1_abacus.deb| cut -d" " -f 1` `stat -c%s 4test_b.1-1_abacus.deb` a b 4test_b.1-1_abacus.deb
 `md5sum 4test_b.1-1.tar.gz| cut -d" " -f 1` `stat -c%s 4test_b.1-1.tar.gz` a b 4test_b.1-1.tar.gz
EOF
cp 4test_b.1-1.dsc 4test_0orso.dsc
testrun - -b . include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Package version 'b.1-1.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Strange file '4test_b.1-1.dsc'!
=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*='./pool/stupid/4/4test/4test_b.1-1_abacus.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=To ignore use --ignore=wrongsourceversion.
*=There have been errors!
returns 255
EOF
testrun - -b . --ignore=wrongsourceversion include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Package version 'b.1-1.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Strange file '4test_b.1-1.dsc'!
=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*='./pool/stupid/4/4test/4test_b.1-1_abacus.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=Ignoring as --ignore=wrongsourceversion given.
*='4test_0orso.dsc' says it is version '1:b.1-1', while .changes file said it is '0orso'
*=To ignore use --ignore=wrongversion.
*=There have been errors!
returns 255
EOF
testrun - -b . --ignore=wrongsourceversion --ignore=wrongversion include test2 broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Package version 'b.1-1.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Warning: Strange file '4test_b.1-1.dsc'!
=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*='./pool/stupid/4/4test/4test_b.1-1_abacus.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=Ignoring as --ignore=wrongsourceversion given.
*='4test_0orso.dsc' says it is version '1:b.1-1', while .changes file said it is '0orso'
*=Ignoring as --ignore=wrongversion given.
=Exporting indices...
EOF
testrun - -b . remove test2 4test 3<<EOF
=Exporting indices...
=Deleting files no longer referenced...
stdout
*=deleting and forgetting pool/stupid/4/4test/4test_0orso.dsc
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results 

for tracking in true false ; do
cat > conf/distributions <<EOF
Codename: X
Architectures: none
Components: test
EOF
if $tracking ; then
testrun - -b . --delete clearvanished 3<<EOF
stderr
*=Deleting vanished identifier 'foo|bloated|abacus'.
*=Deleting vanished identifier 'foo|bloated|fingers'.
*=Deleting vanished identifier 'foo|i386|abacus'.
*=Deleting vanished identifier 'foo|i386|fingers'.
*=Deleting vanished identifier 'foo|unneeded|abacus'.
*=Deleting vanished identifier 'foo|unneeded|fingers'.
*=Deleting vanished identifier 'test1|stupid|abacus'.
*=Deleting vanished identifier 'test1|stupid|source'.
*=Deleting vanished identifier 'test1|ugly|abacus'.
*=Deleting vanished identifier 'test1|ugly|source'.
*=Deleting vanished identifier 'test2|stupid|abacus'.
*=Deleting vanished identifier 'test2|stupid|coal'.
*=Deleting vanished identifier 'test2|stupid|source'.
*=Deleting vanished identifier 'test2|ugly|abacus'.
*=Deleting vanished identifier 'test2|ugly|coal'.
*=Deleting vanished identifier 'test2|ugly|source'.
*=Deleting files no longer referenced...
stdout
*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb
*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz
*=deleting and forgetting pool/ugly/s/simple/simple-addons_1_all.deb
EOF
else
testrun - -b . --delete clearvanished 3<<EOF
stderr
*=Deleting vanished identifier 'a|all|abacus'.
*=Deleting vanished identifier 'a|all|source'.
*=Deleting vanished identifier 'b|all|abacus'.
*=Deleting files no longer referenced...
stdout
EOF
fi
testout "" -b . dumpunreferenced
dodiff results.empty results 

if $tracking ; then
cat >> conf/distributions <<EOF

Codename: a
Architectures: abacus source
Components: all
Tracking: minimal

Codename: b
Architectures: abacus
Components: all
Pull: froma
EOF
else
cat >> conf/distributions <<EOF

Codename: a
Architectures: abacus source
Components: all

Codename: b
Architectures: abacus
Components: all
Pull: froma
EOF
fi
cat > conf/pulls <<EOF
Name: froma
From: a
EOF

rm -r dists
testrun - -b . cleartracks a 3<<EOF
=Deleting files no longer referenced...
EOF
testout "" -b . dumptracks a
dodiff results.empty results 
testout "" -b . dumpunreferenced
dodiff results.empty results 
cat >pull.rules <<EOF
stderr
*=Calculating packages to pull...
*=Installing (and possibly deleting) packages...
=Exporting indices...
EOF
cat >pulldel.rules <<EOF
stderr
*=Calculating packages to pull...
*=Installing (and possibly deleting) packages...
*=Deleting files no longer referenced...
=Exporting indices...
EOF
testrun pull -b . --export=changed pull a b
test ! -d dists/a
test ! -d dists/b
testrun pull -b . --export=normal pull b
test ! -d dists/a
test -d dists/b
testrun pull -b . --export=normal pull a b
test -d dists/a
test -d dists/b
rm -r dists/a dists/b
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun - -b . --export=never --delete --delete include a test.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=Warning: database 'a|all|abacus' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF
test ! -d dists/a
test ! -d dists/b
test ! -f test.changes
test ! -f aa_1-1_abacus.deb
test ! -f aa_1-1.dsc 
test ! -f aa_1-1.tar.gz
test ! -f aa-addons_1-1_all.deb
test -f pool/all/a/aa/aa-addons_1-1_all.deb
test -f pool/all/a/aa/aa_1-1_abacus.deb
test -f pool/all/a/aa/aa_1-1.dsc
test -f pool/all/a/aa/aa_1-1.tar.gz
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-1
Files:
 pool/all/a/aa/aa-addons_1-1_all.deb a 1
 pool/all/a/aa/aa_1-1_abacus.deb b 1
 pool/all/a/aa/aa_1-1.dsc s 1
 pool/all/a/aa/aa_1-1.tar.gz s 1

END
if $tracking; then diff results.expected results ; else diff results.empty results ; fi
testrun "" -b . export a
dogrep "Version: 1-1" dists/a/all/binary-abacus/Packages
rm -r dists/a
testrun pull -b . --export=changed pull a b
test ! -d dists/a
test -d dists/b
dogrep "Version: 1-1" dists/b/all/binary-abacus/Packages
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh
testrun includedel -b . --export=changed --delete include a test.changes
test -f test.changes
test ! -f aa_1-2_abacus.deb
test ! -f aa_1-2.dsc 
test ! -f aa_1-2.tar.gz
test ! -f aa-addons_1-2_all.deb
test -d dists/a
dogrep "Version: 1-2" dists/a/all/binary-abacus/Packages
dogrep "Version: 1-1" dists/b/all/binary-abacus/Packages
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-2
Files:
 pool/all/a/aa/aa-addons_1-2_all.deb a 1
 pool/all/a/aa/aa_1-2_abacus.deb b 1
 pool/all/a/aa/aa_1-2.dsc s 1
 pool/all/a/aa/aa_1-2.tar.gz s 1

END
if $tracking; then diff results.expected results ; else diff results.empty results ; fi
rm -r dists/a dists/b
testrun pulldel -b . --export=changed pull a b
test ! -d dists/a
test -d dists/b
dogrep "Version: 1-2" dists/b/all/binary-abacus/Packages
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-3" SECTION="stupid/base" genpackage.sh
testrun - -b . --export=never include a test.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=Warning: database 'a|all|abacus' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
=Deleting files no longer referenced...
EOF
test -f test.changes
test -f aa_1-3_abacus.deb
test -f aa_1-3.dsc 
test -f aa_1-3.tar.gz
test -f aa-addons_1-3_all.deb
test ! -f pool/all/a/aa/aa_1-2.dsc
test -f pool/all/a/aa/aa_1-2_abacus.deb # still in b
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-3
Files:
 pool/all/a/aa/aa-addons_1-3_all.deb a 1
 pool/all/a/aa/aa_1-3_abacus.deb b 1
 pool/all/a/aa/aa_1-3.dsc s 1
 pool/all/a/aa/aa_1-3.tar.gz s 1

END
if $tracking; then diff results.expected results ; else diff results.empty results ; fi
testout "" -b . dumpunreferenced
dodiff results.empty results 
DISTRI=a PACKAGE=ab EPOCH="" VERSION=2 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun - -b . --delete --delete --export=never include a test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
*=Warning: database 'a|all|abacus' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
=Changes will only be visible after the next 'export'!
EOF
testrun pulldel -b . --export=changed pull b
dogrep "Version: 1-3" dists/b/all/binary-abacus/Packages
dogrep "Version: 2-1" dists/b/all/binary-abacus/Packages
test ! -f pool/all/a/aa/aa_1-2_abacus.deb
test -f pool/all/a/aa/aa_1-3_abacus.deb
DISTRI=a PACKAGE=ab EPOCH="" VERSION=3 REVISION="-1" SECTION="stupid/base" genpackage.sh
grep -v '\.tar\.gz' test.changes > broken.changes
testrun - -b . --delete --delete include a broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=I don't know what to do having a .dsc without a .diff.gz or .tar.gz in 'broken.changes'!
*=There have been errors!
returns 255
EOF
echo ' d41d8cd98f00b204e9800998ecf8427e 0 stupid/base superfluous ab_3-1.diff.gz' >> broken.changes
testrun - -b . --delete --delete include a broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=Cannot find file './ab_3-1.diff.gz' needed by 'broken.changes'!
*=There have been errors!
returns 249
EOF
test -f broken.changes
test ! -f ab_3-1.diff.gz
test -f ab-addons_3-1_all.deb
test -f ab_3-1_abacus.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test ! -f pool/all/a/ab/ab-addons_3-1_all.deb
test ! -f pool/all/a/ab/ab_3-1_abacus.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
touch ab_3-1.diff.gz
testrun includedel -b . --delete -T deb include a broken.changes
testout "" -b . dumpunreferenced
dodiff results.empty results 
test -f broken.changes
test -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-3
Files:
 pool/all/a/aa/aa-addons_1-3_all.deb a 1
 pool/all/a/aa/aa_1-3_abacus.deb b 1
 pool/all/a/aa/aa_1-3.dsc s 1
 pool/all/a/aa/aa_1-3.tar.gz s 1

Distribution: a
Source: ab
Version: 2-1
Files:
 pool/all/a/ab/ab_2-1.dsc s 1
 pool/all/a/ab/ab_2-1.tar.gz s 1

Distribution: a
Source: ab
Version: 3-1
Files:
 pool/all/a/ab/ab-addons_3-1_all.deb a 1
 pool/all/a/ab/ab_3-1_abacus.deb b 1

END
if $tracking; then diff results.expected results ; else diff results.empty results ; fi
testout "" -b . dumpunreferenced
dodiff results.empty results 
testrun - -b . --delete --delete include a broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=Unable to find ./pool/all/a/ab/ab_3-1.tar.gz!
=Perhaps you forgot to give dpkg-buildpackage the -sa option,
= or you cound try --ignore=missingfile
*=There have been errors!
returns 249
EOF
test -f broken.changes
test -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
cat broken.changes
testrun - -b . -T dsc --delete --delete --ignore=missingfile include a broken.changes 3<<EOF
=Data seems not to be signed trying to use directly...
*=Unable to find ./pool/all/a/ab/ab_3-1.tar.gz!
*=Looking around if it is elsewhere as --ignore=missingfile given.
*=Exporting indices...
*=Deleting files no longer referenced...
EOF
test ! -f broken.changes
test ! -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test ! -f ab_3-1.dsc
# test ! -f pool/all/a/ab/ab_3-1.diff.gz # decide later (TODO: let reprepro check for those)
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test -f pool/all/a/ab/ab_3-1.dsc
testout "" -b . dumpunreferenced
cat > results.expected << EOF
pool/all/a/ab/ab_3-1.diff.gz
EOF
dodiff results.empty results || diff results.expected results
testrun "" -b . deleteunreferenced

DISTRI=b PACKAGE=ac EPOCH="" VERSION=1 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun include -b . -A abacus --delete --delete --ignore=missingfile include b test.changes
dogrep '^Package: aa$' dists/b/all/binary-abacus/Packages
dogrep '^Package: aa-addons$' dists/b/all/binary-abacus/Packages
dogrep '^Package: ab$' dists/b/all/binary-abacus/Packages
dogrep '^Package: ab-addons$' dists/b/all/binary-abacus/Packages
dogrep '^Package: ac$' dists/b/all/binary-abacus/Packages
dogrep '^Package: ac-addons$' dists/b/all/binary-abacus/Packages
echo "Update: - froma" >> conf/distributions
cat >conf/updates <<END
Name: froma
Method: copy:$WORKDIR
Suite: a
ListHook: /bin/cp
END
testrun - -b . predelete b 3<<EOF
=WARNING: Single-Instance not yet supported!
*=Removing obsolete or to be replaced packages...
*=Exporting indices...
*=Deleting files no longer referenced...
EOF
dogrep '^Package: aa$' dists/b/all/binary-abacus/Packages
dogrep '^Package: aa-addons$' dists/b/all/binary-abacus/Packages
dongrep '^Package: ab$' dists/b/all/binary-abacus/Packages
dongrep '^Package: ab-addons$' dists/b/all/binary-abacus/Packages
dongrep '^Package: ac$' dists/b/all/binary-abacus/Packages
dongrep '^Package: ac-addons$' dists/b/all/binary-abacus/Packages
test ! -f pool/all/a/ac/ac-addons_1-1_all.deb
test ! -f pool/all/a/ab/ab_2-1_abacus.deb
test -f pool/all/a/aa/aa_1-3_abacus.deb
testrun - -VVVb . copy b a ab ac 3<<EOF
stderr
*=Exporting indices...
*= looking for changes in 'b|all|abacus'...
=Adding reference to 'pool/all/a/ab/ab_3-1_abacus.deb' by 'b|all|abacus'
stdout
*=Moving 'ab' from 'a|all|abacus' to 'b|all|abacus'.
*=Not looking into 'a|all|source' as no matching target in 'b'!
*=No instance of 'ab' found in 'a|all|source'!
*=No instance of 'ac' found in 'a|all|abacus'!
*=No instance of 'ac' found in 'a|all|source'!
=Looking for 'ab' in 'a' to be copied to 'b'...
=db: 'ab' added to 'b|all|abacus'.
=Looking for 'ac' in 'a' to be copied to 'b'...
EOF
done
set +v +x
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
