#!/bin/bash
set -e
#PACKAGE=bloat+-0a9z.app
#EPOCH=99:
#VERSION=0.9-A:Z+a:z
#REVISION=-0+aA.9zZ
if [ "x$OUTPUT" == "x" ] ; then
	OUTPUT=test.changes
fi

DIR="$PACKAGE-$VERSION"
mkdir "$DIR"
mkdir "$DIR"/debian
cat >"$DIR"/debian/control <<END
Source: $PACKAGE
Section: $SECTION
Priority: superfluous
Maintainer: me <guess@who>
Standards-Version: 0.0

Package: $PACKAGE
Architecture: $FAKEARCHITECTURE
Description: bla
 blub

Package: ${PACKAGE}-addons
Architecture: all
Description: bla
 blub
END
if test -z "$DISTRI" ; then
	DISTRI=test1
fi
cat >"$DIR"/debian/changelog <<END
$PACKAGE ($EPOCH$VERSION$REVISION) $DISTRI; urgency=critical

   * new upstream release (Closes: #allofthem)

 -- me <guess@who>  Mon, 01 Jan 1980 01:02:02 +0000
END

dpkg-source -b "$DIR"
mkdir -p "$DIR"/debian/tmp/DEBIAN
touch "$DIR"/debian/tmp/x
mkdir "$DIR"/debian/tmp/a
touch "$DIR"/debian/tmp/a/1
mkdir "$DIR"/debian/tmp/dir
touch "$DIR"/debian/tmp/dir/file
touch "$DIR"/debian/tmp/dir/another
mkdir "$DIR"/debian/tmp/dir/subdir
touch "$DIR"/debian/tmp/dir/subdir/file
cd "$DIR"
for pkg in `grep '^Package: ' debian/control | sed -e 's/^Package: //'` ; do
	if [ "x$pkg" != "x${pkg%-addons}" -a -n "$FAKEVER" ] ; then
		dpkg-gencontrol -p$pkg -v"$FAKEVER"
	else
		dpkg-gencontrol -p$pkg
	fi
	dpkg --build debian/tmp ..
done
#dpkg-genchanges > ../"${PACKAGE}_$VERSION$REVISION"_abbacus.changes
dpkg-genchanges "$@" > ../"$OUTPUT"
cd ..
rm -r "$DIR"
