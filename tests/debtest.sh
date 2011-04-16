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
mkdir -p conf
cat > conf/distributions <<CONFEND
Codename: test
Architectures: abacus source
Components: stupid ugly
CONFEND

set -v
mkdir test
mkdir test/DEBIAN
echo "Package: test1" > test/DEBIAN/control
echo "Version: 1" >> test/DEBIAN/control
echo "Maintainer: me <its@me>" >> test/DEBIAN/control
echo "Description: test" >> test/DEBIAN/control
echo " bla fasel" >> test/DEBIAN/control
mkdir -p test/usr/bin
echo "echo hallo world" > test/usr/bin/hallo
dpkg-deb -b test

ERRORMSG="`$HELPER "$REPREPRO" -b . includedeb test test.deb 2>&1 || echo "error:$?"`"
echo $ERRORMSG
echo $ERRORMSG | grep -q "Cannot find Architecture-header"
echo $ERRORMSG | grep -q "error:255"

echo "Package: test1" > test/DEBIAN/control
echo "Version: 1" >> test/DEBIAN/control
echo "Maintainer: me <its@me>" >> test/DEBIAN/control
echo "Architecture: abacus" >> test/DEBIAN/control
echo "Description: test" >> test/DEBIAN/control
echo " bla fasel" >> test/DEBIAN/control
dpkg-deb -b test

ERRORMSG="`$HELPER "$REPREPRO" -b . includedeb test test.deb 2>&1 || echo "error:$?"`"
echo $ERRORMSG
echo $ERRORMSG | grep -q "No section was given"
echo $ERRORMSG | grep -q "error:255"

ERRORMSG="`$HELPER "$REPREPRO" -b . -S funnystuff includedeb test test.deb 2>&1 || echo "error:$?"`"
echo $ERRORMSG
echo $ERRORMSG | grep -q "No priority was given"
echo $ERRORMSG | grep -q "error:255"

$HELPER "$REPREPRO" -b . -S funnystuff -P useless includedeb test test.deb
echo returned: $?

echo "Package: test2" > test/DEBIAN/control
echo "Version: 1" >> test/DEBIAN/control
echo "Maintainer: me <its@me>" >> test/DEBIAN/control
echo "Section: funnystuff" >> test/DEBIAN/control
echo "Priority: useless" >> test/DEBIAN/control
echo "Architecture: abacus" >> test/DEBIAN/control
echo "Description: test" >> test/DEBIAN/control
echo " bla fasel" >> test/DEBIAN/control
dpkg-deb -b test

$HELPER "$REPREPRO" -b . includedeb test test.deb
echo returned: $?

echo "Package: bla" > test/DEBIAN/control
echo "Version: 1" >> test/DEBIAN/control
echo "Maintainer: me <its@me>" >> test/DEBIAN/control
echo "Section: funnystuff" >> test/DEBIAN/control
echo "Priority: useless" >> test/DEBIAN/control
echo "Architecture: abacus" >> test/DEBIAN/control
echo "Description: test" >> test/DEBIAN/control
echo " bla fasel" >> test/DEBIAN/control
(cd test/DEBIAN &&  tar -cvvzf ../../control.tar.gz ./control)
(cd test &&  tar -cvvzf ../data.tar.gz ./usr)
#wrong ar:
ar r bla.deb data.tar.gz control.tar.gz
rm *.tar.gz

$HELPER "$REPREPRO" -b . includedeb test bla.deb
echo returned: $?

echo -e ',g/Version/s/1/2/\nw' | ed -s test/DEBIAN/control
#not also wrong paths:
(cd test/DEBIAN &&  tar -cvvzf ../../control.tar.gz control)
(cd test &&  tar -cvvzf ../data.tar.gz usr)
ar r bla2.deb data.tar.gz control.tar.gz

$HELPER "$REPREPRO" -b . includedeb test bla2.deb
echo returned: $?

set +v 
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
