#!/bin/bash
set -e
#PACKAGE=bloat+-0a9z.app
#EPOCH=99:
#VERSION=0.9-A:Z+a:z
#REVISION=-0+aA.9zZ

DIR="$PACKAGE-$VERSION"
mkdir "$DIR"
mkdir "$DIR"/debian
cat >"$DIR"/debian/control <<END
Source: $PACKAGE
Section: $SECTION
Priority: superfluous
Maintainer: me <guess@who>

Package: $PACKAGE
Architecture: abacus
Description: bla
 blub

Package: ${PACKAGE}-addons
Architecture: all
Description: bla
 blub
END
cat >"$DIR"/debian/changelog <<END
$PACKAGE ($EPOCH$VERSION$REVISION) test1; urgency=critical

   * new upstream release (Closes: #allofthem)

 -- me <guess@who>  Mon, 01 Jan 1980 01:02:02 +0000
END

dpkg-source -b "$DIR"
mkdir -p "$DIR"/debian/tmp/DEBIAN
cd "$DIR"
for pkg in `grep '^Package: ' debian/control | sed -e 's/^Package: //'` ; do
	dpkg-gencontrol -p$pkg
	dpkg --build debian/tmp .. 
done
#dpkg-genchanges > ../"${PACKAGE}_$VERSION$REVISION"_abbacus.changes
dpkg-genchanges > ../test.changes
