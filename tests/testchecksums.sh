#!/bin/bash

set -e

if test "${MAINTESTOPTIONS+set}" != set ; then
	source $(dirname $0)/test.inc
	STANDALONE="true"
else
	STANDALONE=""
fi

rm -rf db dists pool lists conf

mkdir -p conf
cat > conf/options <<CONFEND
export changed
CONFEND
cat > conf/distributions <<CONFEND
Codename: ATest
Architectures: ${FAKEARCHITECTURE} zzz source
Components: everything

Codename: BTest
Architectures: ${FAKEARCHITECTURE} yyy
Components: everything

Codename: CTest
Architectures: ${FAKEARCHITECTURE} aaa
Components: elsewhere

Codename: DTest
Architectures: ${FAKEARCHITECTURE} uuu
Components: elsewhere
CONFEND

DISTRI=ATest PACKAGE=package EPOCH="" VERSION=999.999.999 REVISION="-999.999" SECTION="otherofs" genpackage.sh
if test -n "$TESTNEWFILESDB" ; then
	echo nooldfilesdb >> conf/options
	dodo test ! -f db/files.db
fi
testrun - -b . include ATest test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/everything"
-v2*=Created directory "./pool/everything/p"
-v2*=Created directory "./pool/everything/p/package"
-e1*=db: 'pool/everything/p/package/package-addons_999.999.999-999.999_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/everything/p/package/package-addons_999.999.999-999.999_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/everything/p/package/package_999.999.999-999.999.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/everything/p/package/package_999.999.999-999.999.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/everything/p/package/package_999.999.999-999.999.dsc' added to files.db(md5sums).
-d1*=db: 'pool/everything/p/package/package_999.999.999-999.999.dsc' added to checksums.db(pool).
-d1*=db: 'package-addons' added to packages.db(ATest|everything|${FAKEARCHITECTURE}).
-d1*=db: 'package-addons' added to packages.db(ATest|everything|zzz).
-d1*=db: 'package' added to packages.db(ATest|everything|${FAKEARCHITECTURE}).
-d1*=db: 'package' added to packages.db(ATest|everything|source).
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/ATest"
-v2*=Created directory "./dists/ATest/everything"
-v2*=Created directory "./dists/ATest/everything/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'ATest|everything|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/ATest/everything/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/ATest/everything/binary-zzz"
-v6*= looking for changes in 'ATest|everything|zzz'...
-v6*=  creating './dists/ATest/everything/binary-zzz/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/ATest/everything/source"
-v6*= looking for changes in 'ATest|everything|source'...
-v6*=  creating './dists/ATest/everything/source/Sources' (gzipped)
EOF

cat > results.expected <<EOF
$(printindexpart pool/everything/p/package/package-addons_999.999.999-999.999_all.deb)

EOF
dodiff results.expected dists/ATest/everything/binary-zzz/Packages

sed -i test.changes -e s/ATest/BTest/
testrun - -b . -T deb include BTest test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'package-addons' added to packages.db(BTest|everything|${FAKEARCHITECTURE}).
-d1*=db: 'package-addons' added to packages.db(BTest|everything|yyy).
-d1*=db: 'package' added to packages.db(BTest|everything|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v2*=Created directory "./dists/BTest"
-v2*=Created directory "./dists/BTest/everything"
-v2*=Created directory "./dists/BTest/everything/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'BTest|everything|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/BTest/everything/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/BTest/everything/binary-yyy"
-v6*= looking for changes in 'BTest|everything|yyy'...
-v6*=  creating './dists/BTest/everything/binary-yyy/Packages' (uncompressed,gzipped)
EOF

cat > results.expected <<EOF
$(printindexpart pool/everything/p/package/package-addons_999.999.999-999.999_all.deb)

EOF
dodiff results.expected dists/BTest/everything/binary-yyy/Packages

testout - -b . _listchecksums 3<<EOF
EOF

cat > results.expected <<EOF
pool/everything/p/package/package-addons_999.999.999-999.999_all.deb $(fullchecksum package-addons_999.999.999-999.999_all.deb)
pool/everything/p/package/package_999.999.999-999.999.dsc $(fullchecksum package_999.999.999-999.999.dsc)
pool/everything/p/package/package_999.999.999-999.999.tar.gz $(fullchecksum package_999.999.999-999.999.tar.gz)
pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(fullchecksum package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
EOF
dodiff results.expected results

cat > fakechecksums <<EOF
pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb :9:futuristicchecksum $(mdandsize pool/everything/p/package/package-addons_999.999.999-999.999_all.deb)
pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb :9:futuristicchecksum $(mdandsize pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
EOF
testrun - -b . _addchecksums <fakechecksums 3<<EOF
stdout
-e1*=db: 'pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
EOF

# TODO: test also behaviour when file is not there. As it has to be read to be
# added, that will give hard to grok errors currently, which need some improvement.
mkdir -p pool/elsewhere//p/package
ln pool/everything/p/package/package-addons_999.999.999-999.999_all.deb pool/elsewhere/p/package/
ln pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb pool/elsewhere/p/package/

sed -i test.changes -e s/BTest/CTest/
testrun - -b . -T deb include CTest test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-e1*=db: 'pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'package-addons' added to packages.db(CTest|elsewhere|${FAKEARCHITECTURE}).
-d1*=db: 'package-addons' added to packages.db(CTest|elsewhere|aaa).
-d1*=db: 'package' added to packages.db(CTest|elsewhere|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v2*=Created directory "./dists/CTest"
-v2*=Created directory "./dists/CTest/elsewhere"
-v2*=Created directory "./dists/CTest/elsewhere/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'CTest|elsewhere|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/CTest/elsewhere/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/CTest/elsewhere/binary-aaa"
-v6*= looking for changes in 'CTest|elsewhere|aaa'...
-v6*=  creating './dists/CTest/elsewhere/binary-aaa/Packages' (uncompressed,gzipped)
EOF

cat > results.expected <<EOF
$(printindexpart pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb)

EOF
dodiff results.expected dists/CTest/elsewhere/binary-aaa/Packages

sed -i test.changes -e s/CTest/DTest/
testrun - -b . -T deb include DTest test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'package-addons' added to packages.db(DTest|elsewhere|${FAKEARCHITECTURE}).
-d1*=db: 'package-addons' added to packages.db(DTest|elsewhere|uuu).
-d1*=db: 'package' added to packages.db(DTest|elsewhere|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v2*=Created directory "./dists/DTest"
-v2*=Created directory "./dists/DTest/elsewhere"
-v2*=Created directory "./dists/DTest/elsewhere/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'DTest|elsewhere|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/DTest/elsewhere/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/DTest/elsewhere/binary-uuu"
-v6*= looking for changes in 'DTest|elsewhere|uuu'...
-v6*=  creating './dists/DTest/elsewhere/binary-uuu/Packages' (uncompressed,gzipped)
EOF

cat > results.expected <<EOF
$(printindexpart pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb)

EOF
dodiff results.expected dists/DTest/elsewhere/binary-uuu/Packages

testout - -b . _listchecksums 3<<EOF
EOF

cat > results.expected <<EOF
pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb $(sha package-addons_999.999.999-999.999_all.deb) :9:futuristicchecksum $(mdandsize package-addons_999.999.999-999.999_all.deb)
pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(sha package_999.999.999-999.999_${FAKEARCHITECTURE}.deb) :9:futuristicchecksum $(mdandsize package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
pool/everything/p/package/package-addons_999.999.999-999.999_all.deb $(fullchecksum package-addons_999.999.999-999.999_all.deb)
pool/everything/p/package/package_999.999.999-999.999.dsc $(fullchecksum package_999.999.999-999.999.dsc)
pool/everything/p/package/package_999.999.999-999.999.tar.gz $(fullchecksum package_999.999.999-999.999.tar.gz)
pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(fullchecksum package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
EOF
dodiff results.expected results

testrun - -b . checkpool 3<<EOF
EOF

testrun - -b . collectnewchecksums 3<<EOF
EOF

cp db/checksums.db db/checksums.saved.db
if test -z "$TESTNEWFILESDB" ; then
cp db/files.db db/files.saved.db
fi

testrun - -b . _forget pool/everything/p/package/package_999.999.999-999.999.tar.gz 3<<EOF
stdout
-d1*=db: 'pool/everything/p/package/package_999.999.999-999.999.tar.gz' removed from checksums.db(pool).
-e1*=db: 'pool/everything/p/package/package_999.999.999-999.999.tar.gz' removed from files.db(md5sums).
EOF

if test -z "$TESTNEWFILESDB" ; then
cp db/checksums.db db/checksums.crippled.db
cp db/files.db db/files.crippled.db
fi

testout - -b . _listchecksums 3<<EOF
EOF
cat > results.expected <<EOF
pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb $(sha package-addons_999.999.999-999.999_all.deb) :9:futuristicchecksum $(mdandsize package-addons_999.999.999-999.999_all.deb)
pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(sha package_999.999.999-999.999_${FAKEARCHITECTURE}.deb) :9:futuristicchecksum $(mdandsize package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
pool/everything/p/package/package-addons_999.999.999-999.999_all.deb $(fullchecksum package-addons_999.999.999-999.999_all.deb)
pool/everything/p/package/package_999.999.999-999.999.dsc $(fullchecksum package_999.999.999-999.999.dsc)
pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(fullchecksum package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
EOF
dodiff results.expected results

if test -z "$TESTNEWFILESDB" ; then
cp db/checksums.saved.db db/checksums.db

testout - -b . _listchecksums 3<<EOF
EOF
cat > results.expected <<EOF
pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb $(sha package-addons_999.999.999-999.999_all.deb) :9:futuristicchecksum $(mdandsize package-addons_999.999.999-999.999_all.deb)
pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(sha package_999.999.999-999.999_${FAKEARCHITECTURE}.deb) :9:futuristicchecksum $(mdandsize package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
pool/everything/p/package/package-addons_999.999.999-999.999_all.deb $(fullchecksum package-addons_999.999.999-999.999_all.deb)
pool/everything/p/package/package_999.999.999-999.999.dsc $(fullchecksum package_999.999.999-999.999.dsc)
pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(fullchecksum package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
EOF
dodiff results.expected results

fi

testrun - -b . checkpool 3<<EOF
*=WARNING: file 'pool/everything/p/package/package_999.999.999-999.999.tar.gz'
*=is listed in checksums.db but not the legacy (but still binding) files.db!
*=This should normaly only happen if information about the file was collected
*=by a version at least 3.3.0 and deleted by an earlier version. But then the
*=file should be deleted, too. But it seems to still be there. Strange.
EOF

dodo mv pool/everything/p/package/package_999.999.999-999.999.tar.gz s

testrun - -b . checkpool 3<<EOF
*=deleting left over entry for file 'pool/everything/p/package/package_999.999.999-999.999.tar.gz'
*=listed in checksums.db but not the legacy (but still canonical) files.db!
*=This should only happen when information about it was collected with a
*=version of at least 3.3.0 of reprepro but deleted later with a earlier
*=version. But in that case it is normal.
stdout
-d1*=db: 'pool/everything/p/package/package_999.999.999-999.999.tar.gz' removed from checksums.db(pool).
EOF

dodo mv s pool/everything/p/package/package_999.999.999-999.999.tar.gz

testrun - -b . collectnewchecksums 3<<EOF
EOF

testout - -b . _listchecksums 3<<EOF
EOF
cat > results.expected <<EOF
pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb $(sha package-addons_999.999.999-999.999_all.deb) :9:futuristicchecksum $(mdandsize package-addons_999.999.999-999.999_all.deb)
pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(sha package_999.999.999-999.999_${FAKEARCHITECTURE}.deb) :9:futuristicchecksum $(mdandsize package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
pool/everything/p/package/package-addons_999.999.999-999.999_all.deb $(fullchecksum package-addons_999.999.999-999.999_all.deb)
pool/everything/p/package/package_999.999.999-999.999.dsc $(fullchecksum package_999.999.999-999.999.dsc)
pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(fullchecksum package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
EOF
dodiff results.expected results

if test -z "$TESTNEWFILESDB" ; then
cp db/files.saved.db db/files.db
cp db/checksums.crippled.db db/checksums.db

testout - -b . _listchecksums 3<<EOF
EOF
cat > results.expected <<EOF
pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb $(sha package-addons_999.999.999-999.999_all.deb) :9:futuristicchecksum $(mdandsize package-addons_999.999.999-999.999_all.deb)
pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(sha package_999.999.999-999.999_${FAKEARCHITECTURE}.deb) :9:futuristicchecksum $(mdandsize package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
pool/everything/p/package/package-addons_999.999.999-999.999_all.deb $(fullchecksum package-addons_999.999.999-999.999_all.deb)
pool/everything/p/package/package_999.999.999-999.999.dsc $(fullchecksum package_999.999.999-999.999.dsc)
pool/everything/p/package/package_999.999.999-999.999.tar.gz $(mdandsize package_999.999.999-999.999.tar.gz)
pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(fullchecksum package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
EOF
dodiff results.expected results


testrun - -b . collectnewchecksums 3<<EOF
stdout
-e1=db: 'pool/everything/p/package/package_999.999.999-999.999.tar.gz' added to files.db(md5sums).
stdout
-d1*=db: 'pool/everything/p/package/package_999.999.999-999.999.tar.gz' added to checksums.db(pool).
EOF

testout - -b . _listchecksums 3<<EOF
EOF
cat > results.expected <<EOF
pool/elsewhere/p/package/package-addons_999.999.999-999.999_all.deb $(sha package-addons_999.999.999-999.999_all.deb) :9:futuristicchecksum $(mdandsize package-addons_999.999.999-999.999_all.deb)
pool/elsewhere/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(sha package_999.999.999-999.999_${FAKEARCHITECTURE}.deb) :9:futuristicchecksum $(mdandsize package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
pool/everything/p/package/package-addons_999.999.999-999.999_all.deb $(fullchecksum package-addons_999.999.999-999.999_all.deb)
pool/everything/p/package/package_999.999.999-999.999.dsc $(fullchecksum package_999.999.999-999.999.dsc)
pool/everything/p/package/package_999.999.999-999.999.tar.gz $(fullchecksum package_999.999.999-999.999.tar.gz)
pool/everything/p/package/package_999.999.999-999.999_${FAKEARCHITECTURE}.deb $(fullchecksum package_999.999.999-999.999_${FAKEARCHITECTURE}.deb)
EOF
dodiff results.expected results
fi


if test x$STANDALONE = xtrue ; then
	set +v +x
	echo
	echo "If the script is still running to show this,"
	echo "all tested cases seem to work. (Though writing some tests more can never harm)."
fi
exit 0
