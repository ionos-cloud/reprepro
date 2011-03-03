#!/bin/bash

set -e 

WORKDIR="`pwd`/testdir"

if [ "x$1" == "x--delete" ] ; then
	rm -r "$WORKDIR" || true
	shift
fi
HELPER=""
#HELPER="valgrind -v --leak-check=full --suppressions=../valgrind.supp --log-fd=5"
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

set -v -x
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

$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 
$HELPER "$REPREPRO" -b . cleartracks
echo returned: $?
diff results.empty results 
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
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 

echo "now testing .orig.tar.gz handling"
tar -czf test_1.orig.tar.gz test.changes
PACKAGE=test EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh -si
ERRORMSG="`$HELPER "$REPREPRO" -b . include test1 test.changes || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:249"
$HELPER "$REPREPRO" -b . --ignore=missingfile include test1 test.changes
zgrep test_1-2.dsc dists/test1/stupid/source/Sources.gz

tar -czf testb_2.orig.tar.gz test.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-2" SECTION="stupid/base" genpackage.sh -sa
$HELPER "$REPREPRO" -b . include test1 test.changes
zgrep testb_2-2.dsc dists/test1/stupid/source/Sources.gz
rm test2.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-3" SECTION="stupid/base" OUTPUT="test2.changes" genpackage.sh -sd
$HELPER "$REPREPRO" -b . include test1 test2.changes
zgrep testb_2-3.dsc dists/test1/stupid/source/Sources.gz

$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 

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
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 
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
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 
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
echo $ERRORMSG
echo $ERRORMSG | grep -q "error:249"
echo $ERRORMSG | grep -q "Cannot find file './filename_version.tar.gz' needed by 'broken.changes'"
touch filename_version.tar.gz
ERRORMSG="`$HELPER "$REPREPRO" -b . --ignore=unusedarch --ignore=surprisingarch --ignore=wrongdistribution --ignore=missingfield include test2 broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q -v "error:"
echo $ERRORMSG | grep -q "does not start with 'nowhere_'"
echo $ERRORMSG | grep -q "but no files for this"
echo $ERRORMSG | grep -q "put in a distribution not listed within"
echo $ERRORMSG | grep -q "but this is not listed in the Architecture-Header!"
$HELPER "$REPREPRO" -b . dumpunreferenced > results
cat >results.expected <<EOF
pool/stupid/n/nowhere/filename_version.tar.gz
EOF
diff results.expected results 
$HELPER "$REPREPRO" -b . deleteunreferenced
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 
# first remove file, then try to remove the package
$HELPER "$REPREPRO" -b . _forget pool/ugly/s/simple/simple_1_abacus.deb
$HELPER "$REPREPRO" -b . remove test1 simple
ERRORMSG="`$HELPER "$REPREPRO" -b . remove test2 simple 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:249"
echo $ERRORMSG | grep -q "To be forgotten filekey 'pool/ugly/s/simple/simple_1_abacus.deb' was not known"
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 

for tracking in true false ; do
cat > conf/distributions <<EOF
Codename: X
Architectures: none
Components: test
EOF
$HELPER "$REPREPRO" -b . --delete clearvanished
echo returned $?
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 

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
$HELPER "$REPREPRO" -b . cleartracks a
$HELPER "$REPREPRO" -b . dumptracks a > results
diff results.empty results 
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 
$HELPER "$REPREPRO" -b . --export=changed pull a b
test ! -d dists/a
test ! -d dists/b
$HELPER "$REPREPRO" -b . --export=normal pull b
test ! -d dists/a
test -d dists/b
$HELPER "$REPREPRO" -b . --export=normal pull a b
test -d dists/a
test -d dists/b
rm -r dists/a dists/b
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-1" SECTION="stupid/base" genpackage.sh
$HELPER "$REPREPRO" -b . --export=never --delete --delete include a test.changes
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
$HELPER "$REPREPRO" -b . dumptracks a > results
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
$HELPER "$REPREPRO" -b . export a
grep -q "Version: 1-1" dists/a/all/binary-abacus/Packages
rm -r dists/a
$HELPER "$REPREPRO" -b . --export=changed pull a b
test ! -d dists/a
test -d dists/b
grep -q "Version: 1-1" dists/b/all/binary-abacus/Packages
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh
$HELPER "$REPREPRO" -b . --export=changed --delete include a test.changes
test -f test.changes
test ! -f aa_1-2_abacus.deb
test ! -f aa_1-2.dsc 
test ! -f aa_1-2.tar.gz
test ! -f aa-addons_1-2_all.deb
test -d dists/a
grep -q "Version: 1-2" dists/a/all/binary-abacus/Packages
grep -q "Version: 1-1" dists/b/all/binary-abacus/Packages
$HELPER "$REPREPRO" -b . dumptracks a > results
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
$HELPER "$REPREPRO" -b . --export=changed pull a b
test ! -d dists/a
test -d dists/b
grep -q "Version: 1-2" dists/b/all/binary-abacus/Packages
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-3" SECTION="stupid/base" genpackage.sh
$HELPER "$REPREPRO" -b . --export=never include a test.changes
test -f test.changes
test -f aa_1-3_abacus.deb
test -f aa_1-3.dsc 
test -f aa_1-3.tar.gz
test -f aa-addons_1-3_all.deb
test ! -f pool/all/a/aa/aa_1-2.dsc
test -f pool/all/a/aa/aa_1-2_abacus.deb # still in b
$HELPER "$REPREPRO" -b . dumptracks a > results
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
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 
DISTRI=a PACKAGE=ab EPOCH="" VERSION=2 REVISION="-1" SECTION="stupid/base" genpackage.sh
$HELPER "$REPREPRO" -b . --delete --delete --export=never include a test.changes
$HELPER "$REPREPRO" -b . --export=changed pull b
grep -q "Version: 1-3" dists/b/all/binary-abacus/Packages
grep -q "Version: 2-1" dists/b/all/binary-abacus/Packages
test ! -f pool/all/a/aa/aa_1-2_abacus.deb
test -f pool/all/a/aa/aa_1-3_abacus.deb
DISTRI=a PACKAGE=ab EPOCH="" VERSION=3 REVISION="-1" SECTION="stupid/base" genpackage.sh
grep -v '\.tar\.gz' test.changes > broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . --delete --delete include a broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:255"
echo $ERRORMSG | grep -q "I don't know what to do having a .dsc without a .diff.gz or .tar.gz in 'broken.changes'!"
echo ' d41d8cd98f00b204e9800998ecf8427e 0 stupid/base superfluous ab_3-1.diff.gz' >> broken.changes
ERRORMSG="`$HELPER "$REPREPRO" -b . --delete --delete include a broken.changes 2>&1 || echo "error:$?"`"
echo $ERRORMSG | grep -q "error:249"
echo $ERRORMSG | grep -q "Cannot find file './ab_3-1.diff.gz' needed by 'broken.changes'!"
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
$HELPER "$REPREPRO" -b . --delete -T deb include a broken.changes
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 
test -f broken.changes
test -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
$HELPER "$REPREPRO" -b . dumptracks a > results
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
$HELPER "$REPREPRO" -b . dumpunreferenced > results
diff results.empty results 
ERRORMSG="`$HELPER "$REPREPRO" -b . --delete --delete include a broken.changes 2>&1 || echo "error:$?"`"
test -f broken.changes
test -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
echo $ERRORMSG | grep -q "error:249"
echo $ERRORMSG | grep -q "Unable to find ./pool/all/a/ab/ab_3-1.tar.gz!"
cat broken.changes
$HELPER "$REPREPRO" -b . -T dsc --delete --delete --ignore=missingfile include a broken.changes
test ! -f broken.changes
test ! -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test ! -f ab_3-1.dsc
# test ! -f pool/all/a/ab/ab_3-1.diff.gz # decide later (TODO: let reprepro check for those)
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test -f pool/all/a/ab/ab_3-1.dsc
$HELPER "$REPREPRO" -b . dumpunreferenced > results
cat > results.expected << EOF
pool/all/a/ab/ab_3-1.diff.gz
EOF
diff results.empty results || diff results.expected results
$HELPER "$REPREPRO" -b . deleteunreferenced

DISTRI=b PACKAGE=ac EPOCH="" VERSION=1 REVISION="-1" SECTION="stupid/base" genpackage.sh
$HELPER "$REPREPRO" -b . -A abacus --delete --delete --ignore=missingfile include b test.changes
grep -q '^Package: aa$' dists/b/all/binary-abacus/Packages
grep -q '^Package: aa-addons$' dists/b/all/binary-abacus/Packages
grep -q '^Package: ab$' dists/b/all/binary-abacus/Packages
grep -q '^Package: ab-addons$' dists/b/all/binary-abacus/Packages
grep -q '^Package: ac$' dists/b/all/binary-abacus/Packages
grep -q '^Package: ac-addons$' dists/b/all/binary-abacus/Packages
echo "Update: - froma" >> conf/distributions
cat >conf/updates <<END
Name: froma
Method: copy:$WORKDIR
Suite: a
ListHook: /bin/cp
END
$HELPER "$REPREPRO" -VVVb . predelete b
grep -q '^Package: aa$' dists/b/all/binary-abacus/Packages
grep -q '^Package: aa-addons$' dists/b/all/binary-abacus/Packages
! grep -q '^Package: ab$' dists/b/all/binary-abacus/Packages
! grep -q '^Package: ab-addons$' dists/b/all/binary-abacus/Packages
! grep -q '^Package: ac$' dists/b/all/binary-abacus/Packages
! grep -q '^Package: ac-addons$' dists/b/all/binary-abacus/Packages
test ! -f pool/all/a/ac/ac-addons_1-1_all.deb
test ! -f pool/all/a/ab/ab_2-1_abacus.deb
test -f pool/all/a/aa/aa_1-3_abacus.deb
$HELPER "$REPREPRO" -VVVb . copy b a ab ac
done
set +v +x
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
