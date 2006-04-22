#!/bin/bash

set -e -v

WORKDIR="`pwd`/testdir"

if [ "x$1" == "x--delete" ] ; then
	rm -r "$WORKDIR" || true
	shift
fi
HELPER=""
#HELPER="valgrind -v --leak-check=full"
if [ "x$1" == "x--helper" ] ; then
	shift
	HELPER="$1"
	shift
fi

mkdir "$WORKDIR"
cd "$WORKDIR"

if [ "1" -gt "$#" ] || [ "2" -lt "$#" ] ; then
	echo "Syntax: test.sh <src-dir> [<reprepro-binary>]" >&2
	exit 1
fi
SRCDIR="$1"
if [ "2" -le "$#" ] ; then
	REPREPRO="$2"
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
mkdir -p conf
cat > conf/distributions <<CONFEND
Codename: test1
Architectures: abacus source
Components: stupid ugly
Update: Test2toTest1
DebIndices: Packages Release . .gz .bz2
UDebIndices: Packages .gz .bz2
DscIndices: Sources Release . .gz .bz2
Tracking: keep includechanges needsources includebyhand embargoalls

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

$HELPER "$REPREPRO" -b . export
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
diff -u dists/test1/Release.expected dists/test1/Release || exit 1
diff -u dists/test2/Release.expected dists/test2/Release || exit 1

PACKAGE=simple EPOCH="" VERSION=1 REVISION="" SECTION="stupid/base" genpackage.sh
$HELPER "$REPREPRO" -b . include test1 test.changes
echo returned: $?

PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=0.9-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/base" genpackage.sh
$HELPER "$REPREPRO" -b . include test1 test.changes
echo returned: $?

$HELPER "$REPREPRO" -b . -Tdsc remove test1 simple
$HELPER "$REPREPRO" -b . -Tdeb remove test1 bloat+-0a9z.app
$HELPER "$REPREPRO" -b . -A source remove test1 bloat+-0a9z.app
$HELPER "$REPREPRO" -b . -A abacus remove test1 simple
$HELPER "$REPREPRO" -b . -C ugly remove test1 bloat+-0a9z.app-addons
$HELPER "$REPREPRO" -b . -C stupid remove test1 simple-addons
CURDATE="`TZ=GMT LC_ALL=C date +'%a, %d %b %Y %H:%M:%S +0000'`"
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\n%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq' | ed -s dists/test1/Release

diff dists/test1/Release.expected dists/test1/Release || exit 1

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

$HELPER "$REPREPRO" -b . -Tdsc -A source includedsc test2 simple_1.dsc
$HELPER "$REPREPRO" -b . -Tdsc -A source includedsc test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
$HELPER "$REPREPRO" -b . -Tdeb -A abacus includedeb test2 simple_1_abacus.deb
$HELPER "$REPREPRO" -b . -Tdeb -A coal includedeb test2 simple-addons_1_all.deb
$HELPER "$REPREPRO" -b . -Tdeb -A abacus includedeb test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb
$HELPER "$REPREPRO" -b . -Tdeb -A coal includedeb test2 bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
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
diff -u results.expected results
$HELPER "$REPREPRO" -b . listfilter test2 'Source(==simple)|(!Source,Package(==simple))' > results
cat > results.expected << END
test2|ugly|abacus: simple 1
test2|ugly|coal: simple-addons 1
test2|ugly|source: simple 1
END
diff -u results.expected results
$HELPER "$REPREPRO" -b . listfilter test2 'Source(==bloat+-0a9z.app)|(!Source,Package(==bloat+-0a9z.app))' > results
cat > results.expected << END
test2|stupid|abacus: bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
test2|stupid|coal: bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
test2|stupid|source: bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
END
diff -u results.expected results

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

$HELPER "$REPREPRO" -b . $UPDATETYPE test1
$HELPER "$REPREPRO" -b . $UPDATETYPE test1
$HELPER "$REPREPRO" --nolistsdownload -b . $UPDATETYPE test1
find dists/test2/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' | sed -e 's/test2/test1/' -e 's/coal/abacus/' > test2
find dists/test1/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' > test1
diff -u test2 test1

$HELPER "$REPREPRO" -b . check test1 test2
$HELPER "$REPREPRO" -b . checkpool
$HELPER "$REPREPRO" -b . rereference test1 test2
$HELPER "$REPREPRO" -b . check test1 test2

$HELPER "$REPREPRO" -b . dumptracks > results
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
diff -u results.expected results

$HELPER "$REPREPRO" -b . cleartracks
echo returned: $?
$HELPER "$REPREPRO" -b . include test1 test.changes
echo returned: $?
OUTPUT=test2.changes PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=9.0-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/extra" genpackage.sh
$HELPER "$REPREPRO" -b . include test1 test2.changes
echo returned: $?
$HELPER "$REPREPRO" -b . -S test -P test includedeb test1 simple_1_abacus.deb
echo returned: $?
$HELPER "$REPREPRO" -b . -S test -P test includedsc test1 simple_1.dsc
echo returned: $?

$HELPER "$REPREPRO" -b . dumptracks > results
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
diff -u results.expected results

echo "now testing .orig.tar.gz handling"
tar -czf test_1.orig.tar.gz test.changes
PACKAGE=test EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh -si
ERRORMSG="`$HELPER "$REPREPRO" -b . include test1 test.changes || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:249"
$HELPER "$REPREPRO" -b . --ignore=missingfile include test1 test.changes
grep test_1-2.dsc dists/test1/stupid/source/Sources

tar -czf testb_2.orig.tar.gz test.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-2" SECTION="stupid/base" genpackage.sh -sa
$HELPER "$REPREPRO" -b . include test1 test.changes
grep testb_2-2.dsc dists/test1/stupid/source/Sources
rm -r testb-2 test2.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-3" SECTION="stupid/base" OUTPUT="test2.changes" genpackage.sh -sd
$HELPER "$REPREPRO" -b . include test1 test2.changes
grep testb_2-3.dsc dists/test1/stupid/source/Sources

echo "now testing some error messages:"
PACKAGE=4test EPOCH="1:" VERSION=b.1 REVISION="-1" SECTION="stupid/base" genpackage.sh
ERRORMSG="`$HELPER "$REPREPRO" -b . include test1 test.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -v "error:"
echo $ERRORMSG | grep -q "Warning: Package version 'b.1-1.dsc' does not start with a digit"

ERRORCODE=0
$HELPER "$REPREPRO" -b . include unknown || ERRORCODE=$?
[ $ERRORCODE == 255 ]
ERRORCODE=0
$HELPER "$REPREPRO" -b . include unknown test.changes test2.changes || ERRORCODE=$?
[ $ERRORCODE == 255 ]
ERRORCODE=0
$HELPER "$REPREPRO" -b . include unknown test.changes || ERRORCODE=$?
[ $ERRORCODE == 249 ]
mkdir conf2
ERRORMSG="`$HELPER "$REPREPRO" -b . --confdir conf2 update 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "Could not find 'conf2/distributions'!"
echo $ERRORMSG | grep -q "error:249"
touch conf2/distributions
ERRORMSG="`$HELPER "$REPREPRO" -b . --confdir conf2 update 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "No distribution definitons found!"
echo $ERRORMSG | grep -q "error:249"
echo -e 'Codename: foo' > conf2/distributions
ERRORMSG="`$HELPER "$REPREPRO" -b . --confdir conf2 update 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "required field Architectures not found"
echo $ERRORMSG | grep -q "error:249"
echo -e 'Architectures: abacus fingers' >> conf2/distributions
ERRORMSG="`$HELPER "$REPREPRO" -b . --confdir conf2 update 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "required field Components not found"
echo $ERRORMSG | grep -q "error:249"
echo -e 'Components: unneeded bloated i386' >> conf2/distributions
ERRORMSG="`$HELPER "$REPREPRO" -b . --confdir conf2 update 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "conf2/updates: No such file or directory"
echo $ERRORMSG | grep -q "error:254"
touch conf2/updates
$HELPER "$REPREPRO" -b . --confdir conf2 update
echo "Format: 2.0" > broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Date' field!"
echo "Date: today" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Source' field"
echo "Source: nowhere" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Binary' field"
echo "Binary: phantom" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Architecture' field"
echo "Architecture: brain" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Version' field"
echo "Version: old" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Distribution' field"
echo "Distribution: old" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Urgency' field"
echo "Distribution: old" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . --ignore=missingfield include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Urgency' field"
echo $ERRORMSG | grep -q "Missing 'Maintainer' field"
echo $ERRORMSG | grep -q "Missing 'Description' field"
echo $ERRORMSG | grep -q "Missing 'Changes' field"
echo $ERRORMSG | grep -q "Missing 'Files' field"
echo "Files:" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . --ignore=missingfield include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "Missing 'Urgency' field"
echo $ERRORMSG | grep -q "Missing 'Maintainer' field"
echo $ERRORMSG | grep -q "Missing 'Description' field"
echo $ERRORMSG | grep -q "Missing 'Changes' field"
echo $ERRORMSG | grep -q "Not enough files in .changes"
echo " d41d8cd98f00b204e9800998ecf8427e 0 section priority filename_version.tar.gz" >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . --ignore=missingfield include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "does not start with 'nowhere_'"
echo $ERRORMSG | grep -q ".changes put in a distribution not listed within"
ERRORMSG="`$HELPER "$REPREPRO" -b . --ignore=unusedarch --ignore=surprisingarch --ignore=wrongdistribution --ignore=missingfield include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:249"
echo $ERRORMSG | grep -q "Could not open './filename_version.tar.gz"
touch filename_version.tar.gz
ERRORMSG="`$HELPER "$REPREPRO" -b . --ignore=unusedarch --ignore=surprisingarch --ignore=wrongdistribution --ignore=missingfield include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q -v "error:"
echo $ERRORMSG | grep -q "does not start with 'nowhere_'"
echo $ERRORMSG | grep -q "but no files for this"
echo $ERRORMSG | grep -q "put in a distribution not listed within"
echo $ERRORMSG | grep -q "but this is not listed in the Architecture-Header!"

set +v 
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
