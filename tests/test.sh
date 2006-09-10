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

if [ "x$1" == "x--delete" ] ; then
	rm -r "$WORKDIR" || true
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
	TESTOPTIONS="-e -a"
#	TESTOPTIONS="-e -a --debug --suppressions=$SRCDIR/valgrind.supp"
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
dodiff -u results.expected results
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
*=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Warning: Package version 'b.1-1.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Warning: Package version 'b.1-1_all.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
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
