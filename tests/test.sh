#!/bin/bash

set -e

WORKDIR="`pwd`/testdir"

mkdir "$WORKDIR"
cd "$WORKDIR"

if [ "$#" != "1" ] ; then
	echo "Syntax: test.sh <src-dir>" >&2
	exit 1
fi
SRCDIR="$1"
REPREPRO="$SRCDIR/reprepro"
TESTS="$SRCDIR/tests"
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

Codename: test2
Architectures: abacus source
Components: stupid ugly
Origin: Brain
Label: Only a test
Suite: broken
Version: 9999999.02
Description: test with all fields set
Override: binoverride
SrcOverride: srcoverride
CONFEND

#This is a bit ugly, as there could be a second in between...
"$REPREPRO" -b . export
CURDATE="`TZ=GMT LC_ALL=C date +'%a, %d %b %Y %H:%M:%S +0000'`"
test -f dists/test1/Release
test -f dists/test2/Release

cat > dists/test1/Release.expected <<END
Codename: test1
Date: $CURDATE
Architectures: abacus
Components: stupid ugly
MD5Sum:
 d9f0fad5d54ad09dd4ecee86c73b64d4 39 stupid/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-abacus/Packages
 7029066c27ac6f5ef18d660d5741979a 20 stupid/binary-abacus/Packages.gz
 e38c7da133734e1fd68a7e344b94fe96 39 stupid/source/Release
 7029066c27ac6f5ef18d660d5741979a 20 stupid/source/Sources.gz
 236fcd9339b1813393819d464e37c7c6 37 ugly/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-abacus/Packages
 7029066c27ac6f5ef18d660d5741979a 20 ugly/binary-abacus/Packages.gz
 ed4ee9aa5d080f67926816133872fd02 37 ugly/source/Release
 7029066c27ac6f5ef18d660d5741979a 20 ugly/source/Sources.gz
END
cat > dists/test2/Release.expected <<END
Origin: Brain
Label: Only a test
Suite: broken
Codename: test2
Version: 9999999.02
Date: $CURDATE
Architectures: abacus
Components: stupid ugly
Description: test with all fields set
MD5Sum:
 e142c47c1be0c32cd120138066b73c73 146 stupid/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-abacus/Packages
 7029066c27ac6f5ef18d660d5741979a 20 stupid/binary-abacus/Packages.gz
 b923b3eb1141e41f0b8bb74297ac8a36 146 stupid/source/Release
 7029066c27ac6f5ef18d660d5741979a 20 stupid/source/Sources.gz
 22eb57e60d3c621b8bd8461eae218b16 144 ugly/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-abacus/Packages
 7029066c27ac6f5ef18d660d5741979a 20 ugly/binary-abacus/Packages.gz
 e73a8a85315766763a41ad4dc6744bf5 144 ugly/source/Release
 7029066c27ac6f5ef18d660d5741979a 20 ugly/source/Sources.gz
END
diff dists/test1/Release.expected dists/test1/Release || exit 1
diff dists/test2/Release.expected dists/test2/Release || exit 1

PACKAGE=bloat+-0a9z.app
VERSION=99:0.9-A:Z+a:z-0+aA.9zZ

mkdir "$PACKAGE-$VERSION"
mkdir "$PACKAGE-$VERSION"/debian
cat >"$PACKAGE-$VERSION"/debian/control <<END
Source: $PACKAGE
Section: base
Priority: superfluous
Maintainer: me <guess@who>

Package: $PACKAGE
Architecture: abbacus
END
cat >"$PACKAGE-$VERSION"/debian/changelog <<END
$PACKAGE ($VERSION) test1; urgency=critical

   * new upstream release (Closes: #allofthem)

 -- me <guess@who>  Mon, 01 Jan 1980 01:02:02 +0000
END

PACKAGE=simple EPOCH="" VERSION=123 REVISION=-0 SECTION="stupid/base" genpackage.sh
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
echo -e '%g/^Date:/s/Date: .*/Date: '"$CURDATE"'/\nw\nq' | ed -s dists/test1/Release.expected

diff dists/test1/Release.expected dists/test1/Release || exit 1

echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
