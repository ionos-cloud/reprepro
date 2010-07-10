#!/bin/bash

set -e

if test "${MAINTESTOPTIONS+set}" != set ; then
	source $(dirname $0)/test.inc
	STANDALONE="true"
else
	STANDALONE=""
fi

rm -rf db dists pool lists conf gpgtestdir

mkdir -p gpgtestdir
export GNUPGHOME="`pwd`/gpgtestdir"
gpg --import $SRCDIR/tests/good.key

mkdir -p conf
cat > conf/distributions <<CONFEND
Codename: ATest
Architectures: ${FAKEARCHITECTURE} source
Components: everything
SignWith: good@nowhere.tld
CONFEND

gpg --list-keys

testrun - -b . export 3<<EOF
stdout
-v2*=Created directory "./db"
-v1*=Exporting ATest...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/ATest"
-v2*=Created directory "./dists/ATest/everything"
-v2*=Created directory "./dists/ATest/everything/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'ATest|everything|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/ATest/everything/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/ATest/everything/source"
-v6*= exporting 'ATest|everything|source'...
-v6*=  creating './dists/ATest/everything/source/Sources' (gzipped)
-v2*=Successfully created './dists/ATest/Release.gpg.new'
-v2*=Successfully created './dists/ATest/InRelease.new'
EOF

find dists/ATest | sort > results
cat > results.expected <<EOF
dists/ATest
dists/ATest/InRelease
dists/ATest/Release
dists/ATest/Release.gpg
dists/ATest/everything
dists/ATest/everything/binary-abacus
dists/ATest/everything/binary-abacus/Packages
dists/ATest/everything/binary-abacus/Packages.gz
dists/ATest/everything/binary-abacus/Release
dists/ATest/everything/source
dists/ATest/everything/source/Release
dists/ATest/everything/source/Sources.gz
EOF

dodiff results.expected results

dodo gpg --verify dists/ATest/Release.gpg dists/ATest/Release
dodo gpg --verify dists/ATest/InRelease

cp dists/ATest/InRelease InRelease
ed -s InRelease <<'EOF'
H
/^-----BEGIN PGP SIGNED MESSAGE-----$/,/^$/d
/^-----BEGIN PGP SIGNATURE-----$/,$d
w
q
EOF
dodiff dists/ATest/Release InRelease

rm -r conf db dists gpgtestdir InRelease results results.expected

if test x$STANDALONE = xtrue ; then
	set +v +x
	echo
	echo "If the script is still running to show this,"
	echo "all tested cases seem to work. (Though writing some tests more can never harm)."
fi
exit 0
