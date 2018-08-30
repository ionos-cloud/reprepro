#!/bin/bash
set -e
#PACKAGE=bloat+-0a9z.app
#EPOCH=99:
#VERSION=0.9-A:Z+a:z
#REVISION=-0+aA.9zZ
if [ "x$OUTPUT" == "x" ] ; then
	OUTPUT=${PACKAGE}_${VERSION}${REVISION}_${ARCH:-$(dpkg-architecture -qDEB_HOST_ARCH)}.changes
fi

DIR="$PACKAGE-$VERSION"
ARCH="${ARCH:-$(dpkg-architecture -qDEB_HOST_ARCH)}"
rm -rf "$DIR"
mkdir "$DIR"
mkdir "$DIR"/debian
cat >"$DIR"/debian/control <<END
Source: $PACKAGE
Section: $SECTION
Priority: optional
Maintainer: me <guess@who>
Standards-Version: 0.0

Package: $PACKAGE
Architecture: ${ARCH}
Description: bla
 blub

Package: ${PACKAGE}-addons
Architecture: all
Description: bla
 blub
END

if test -n "${DDEB-}" ; then
cat >>"$DIR"/debian/control <<END

Package: ${PACKAGE}-dbgsym
Architecture: ${ARCH}
Description: ${PACKAGE} debug symbols
Package-Type: ddeb
END
fi

if test -z "$DISTRI" ; then
	DISTRI=test1
fi
cat >"$DIR"/debian/changelog <<END
$PACKAGE ($EPOCH$VERSION$REVISION) $DISTRI; urgency=critical

   * new upstream release (Closes: #allofthem)

 -- me <guess@who>  Mon, 01 Jan 1980 01:02:02 +0000
END

mkdir -p "$DIR/debian/source"
if test -z "$REVISION"; then
	echo "3.0 (native)" > "$DIR/debian/source/format"
else
	echo "3.0 (quilt)" > "$DIR/debian/source/format"
	orig_tarball="${PACKAGE}_${VERSION}.orig.tar.gz"
	if test ! -f "$orig_tarball"; then
		tar czvf "$orig_tarball" --files-from /dev/null
	fi
fi

dpkg-source -b "$DIR" > /dev/null
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
	case "$pkg" in
		(*-udeb)
			deb="${pkg}_${VERSION}${REVISION}_${ARCH}.udeb"
			;;
		(*-dbgsym)
			deb="${pkg}_${VERSION}${REVISION}_${ARCH}.ddeb"
			;;
		(*-addons)
			deb="${pkg}_${FAKEVER:-${VERSION}${REVISION}}_all.deb"
			;;
		(*)
			deb="${pkg}_${VERSION}${REVISION}_${ARCH}.deb"
			;;
	esac
	if [ "x$pkg" != "x${pkg%-addons}" -a -n "$FAKEVER" ] ; then
		dpkg-gencontrol -p$pkg -v"$FAKEVER"
	else
		dpkg-gencontrol -p$pkg
	fi
	dpkg --build debian/tmp ../$deb > /dev/null
done
dpkg-genchanges -q "$@" > "$OUTPUT".pre
# simulate dpkg-genchanges behaviour currently in sid so the testsuite runs for backports, too
awk 'BEGIN{inheader=0} /^Files:/ || (inheader && /^ /)  {inheader = 1; next} {inheader = 0 ; print}' "$OUTPUT".pre | sed -e 's/ \+$//' >../"$OUTPUT"
echo "Files:" >> ../"$OUTPUT"
awk 'BEGIN{inheader=0} (inheader && /^ .*\.deb$/)  {print ; next} /^Files:/ || (inheader && /^ /)  {inheader = 1; next} {inheader = 0 ;next}' "$OUTPUT".pre >>../"$OUTPUT"
awk 'BEGIN{inheader=0} /^Files:/ || (inheader && /^ .*\.deb$/) {inheader = 1 ; next } (inheader && /^ /)  {print ; next} {inheader = 0 ;next}' "$OUTPUT".pre >>../"$OUTPUT"
cd ..
rm -r "$DIR"
