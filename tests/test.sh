#!/bin/bash

set -e -v

WORKDIR="`pwd`/testdir"

if [ "x$1" == "x--delete" ] ; then
	rm -r "$WORKDIR" || true
	shift
fi

mkdir "$WORKDIR"
cd "$WORKDIR"

if [ "$#" != "1" ] ; then
	echo "Syntax: test.sh <src-dir>" >&2
	exit 1
fi
SRCDIR="$1"
REPREPRO="$SRCDIR/reprepro"
TESTS="$SRCDIR/tests"
UPDATETYPE=iteratedupdate
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

"$REPREPRO" -b . export
test -f dists/test1/Release
test -f dists/test2/Release

cat > dists/test1/Release.expected <<END
Codename: test1
Date: normalized
Architectures: abacus
Components: stupid ugly
MD5Sum:
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-abacus/Packages
 7029066c27ac6f5ef18d660d5741979a 20 stupid/binary-abacus/Packages.gz
 d9f0fad5d54ad09dd4ecee86c73b64d4 39 stupid/binary-abacus/Release
 7029066c27ac6f5ef18d660d5741979a 20 stupid/source/Sources.gz
 e38c7da133734e1fd68a7e344b94fe96 39 stupid/source/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-abacus/Packages
 7029066c27ac6f5ef18d660d5741979a 20 ugly/binary-abacus/Packages.gz
 236fcd9339b1813393819d464e37c7c6 37 ugly/binary-abacus/Release
 7029066c27ac6f5ef18d660d5741979a 20 ugly/source/Sources.gz
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
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/binary-abacus/Packages.bz2
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-abacus/Packages
 7029066c27ac6f5ef18d660d5741979a 20 stupid/binary-abacus/Packages.gz
 e142c47c1be0c32cd120138066b73c73 146 stupid/binary-abacus/Release
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/binary-coal/Packages.bz2
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-coal/Packages
 7029066c27ac6f5ef18d660d5741979a 20 stupid/binary-coal/Packages.gz
 10ae2f283e1abdd3facfac6ed664035d 144 stupid/binary-coal/Release
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/source/Sources.bz2
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/source/Sources
 7029066c27ac6f5ef18d660d5741979a 20 stupid/source/Sources.gz
 b923b3eb1141e41f0b8bb74297ac8a36 146 stupid/source/Release
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/binary-abacus/Packages.bz2
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-abacus/Packages
 7029066c27ac6f5ef18d660d5741979a 20 ugly/binary-abacus/Packages.gz
 22eb57e60d3c621b8bd8461eae218b16 144 ugly/binary-abacus/Release
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/binary-coal/Packages.bz2
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-coal/Packages
 7029066c27ac6f5ef18d660d5741979a 20 ugly/binary-coal/Packages.gz
 7a05de3b706d08ed06779d0ec2e234e9 142 ugly/binary-coal/Release
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/source/Sources.bz2
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/source/Sources
 7029066c27ac6f5ef18d660d5741979a 20 ugly/source/Sources.gz
 e73a8a85315766763a41ad4dc6744bf5 144 ugly/source/Release
END
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\nw\nq' | ed -s dists/test1/Release
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\nw\nq' | ed -s dists/test2/Release
diff -u dists/test1/Release.expected dists/test1/Release || exit 1
diff -u dists/test2/Release.expected dists/test2/Release || exit 1

PACKAGE=simple EPOCH="" VERSION=1 REVISION="" SECTION="stupid/base" genpackage.sh
"$REPREPRO" -b . include test1 test.changes
echo returned: $?

PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=0.9-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/base" genpackage.sh
"$REPREPRO" -b . include test1 test.changes
echo returned: $?

"$REPREPRO" -b . -Tdsc remove test1 simple
"$REPREPRO" -b . -Tdeb remove test1 bloat+-0a9z.app
"$REPREPRO" -b . -A source remove test1 bloat+-0a9z.app
"$REPREPRO" -b . -A abacus remove test1 simple
"$REPREPRO" -b . -C ugly remove test1 bloat+-0a9z.app-addons
"$REPREPRO" -b . -C stupid remove test1 simple-addons
CURDATE="`TZ=GMT LC_ALL=C date +'%a, %d %b %Y %H:%M:%S +0000'`"
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\nw\nq' | ed -s dists/test1/Release

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

"$REPREPRO" -b . -Tdsc -A source includedsc test2 simple_1.dsc
"$REPREPRO" -b . -Tdsc -A source includedsc test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
"$REPREPRO" -b . -Tdeb -A abacus includedeb test2 simple_1_abacus.deb
"$REPREPRO" -b . -Tdeb -A coal includedeb test2 simple-addons_1_all.deb
"$REPREPRO" -b . -Tdeb -A abacus includedeb test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb
"$REPREPRO" -b . -Tdeb -A coal includedeb test2 bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
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
"$REPREPRO" -b . listfilter test2 'Source(==simple)|(!Source,Package(==simple))' > results
cat > results.expected << END
test2|ugly|abacus: simple 1
test2|ugly|coal: simple-addons 1
test2|ugly|source: simple 1
END
diff -u results.expected results
"$REPREPRO" -b . listfilter test2 'Source(==bloat+-0a9z.app)|(!Source,Package(==bloat+-0a9z.app))' > results
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

"$REPREPRO" -b . $UPDATETYPE test1
"$REPREPRO" -b . $UPDATETYPE test1
"$REPREPRO" --nolistsdownload -b . $UPDATETYPE test1
find dists/test2/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' | sed -e 's/test2/test1/' -e 's/coal/abacus/' > test2
find dists/test1/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' > test1
diff -u test2 test1

"$REPREPRO" -b . check test1 test2
"$REPREPRO" -b . checkpool
"$REPREPRO" -b . rereference test1 test2
"$REPREPRO" -b . check test1 test2

"$REPREPRO" -b . dumptracks > results
cat >results.expected <<END
Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/test.changes c 1

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple-addons_1_all.deb a 0
 pool/stupid/s/simple/simple_1_abacus.deb b 0
 pool/stupid/s/simple/simple_1.dsc s 0
 pool/stupid/s/simple/simple_1.tar.gz s 0
 pool/stupid/s/simple/test.changes c 1

END
diff -u results.expected results

"$REPREPRO" -b . cleartracks
"$REPREPRO" -b . include test1 test.changes
OUTPUT=test2.changes PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=9.0-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/extra" genpackage.sh
"$REPREPRO" -VVVVVb . include test1 test2.changes
echo returned: $?

"$REPREPRO" -b . dumptracks > results
cat >results.expected <<END
Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/test.changes c 1

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_abacus.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/test2.changes c 1

END
diff -u results.expected results

echo "now testing some error messages:"
ERRORCODE=0
"$REPREPRO" -b . include unknown || ERRORCODE=$?
[ $ERRORCODE == 255 ]
ERRORCODE=0
"$REPREPRO" -b . include unknown test.changes test2.changes || ERRORCODE=$?
[ $ERRORCODE == 255 ]
ERRORCODE=0
"$REPREPRO" -b . include unknown test.changes || ERRORCODE=$?
[ $ERRORCODE == 249 ]

set +v 
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
