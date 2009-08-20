#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
	source $(dirname $0)/test.inc
fi

mkdir test-1
mkdir test-1/debian
cat >test-1/debian/control <<END
Source: test
Section: interpreters
Priority: required
Maintainer: me <guess@who>
Standards-Version: 0.0

Package: sibling
Architecture: any
Description: bla
 blub

Package: siblingtoo
Architecture: any
Description: bla
 blub

Package: mytest
Architecture: all
Description: bla
 blub
END
cat >test-1/debian/changelog <<END
test (1-1) test; urgency=critical

   * new upstream release (Closes: #allofthem)

 -- me <guess@who>  Mon, 01 Jan 1980 01:02:02 +0000
END
mkdir -p test-1/debian/tmp/DEBIAN
touch test-1/debian/tmp/best-file-in-the-root
cd test-1
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg-gencontrol -psibling -v2
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg --build debian/tmp ..
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg-gencontrol -psiblingtoo -v3
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg --build debian/tmp ..
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg-gencontrol -pmytest -v2
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg --build debian/tmp ..
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg-genchanges -b > ../test-1.changes
FAKEARCHITECTUE="somemore" DEB_HOST_ARCH="somemore" dpkg-gencontrol -psiblingtoo -v3
FAKEARCHITECTUE="somemore" DEB_HOST_ARCH="somemore" dpkg --build debian/tmp ..
cd ..
rm -r test-1
mkdir test-2
mkdir test-2/debian
cat >test-2/debian/control <<END
Source: test
Section: interpreters
Priority: required
Maintainer: me <guess@who>
Standards-Version: 0.0

Package: sibling
Architecture: any
Description: bla
 blub

Package: siblingalso
Architecture: any
Description: bla
 blub

Package: mytest
Architecture: all
Description: bla
 blub
END
cat >test-2/debian/changelog <<END
test (2-1) test; urgency=critical

   * bla bla bla (Closes: #allofthem)

 -- me <guess@who>  Mon, 01 Jan 1980 01:02:02 +0000
test (1-1) test; urgency=critical

   * new upstream release (Closes: #allofthem)

 -- me <guess@who>  Mon, 01 Jan 1980 01:02:02 +0000
END
mkdir -p test-2/debian/tmp/DEBIAN
touch test-2/debian/tmp/best-file-in-the-root
cd test-2
dpkg-gencontrol -psiblingalso -v3.1
dpkg --build debian/tmp ..
dpkg-gencontrol -pmytest -v2.4
dpkg --build debian/tmp ..
dpkg-gencontrol -psibling -v2.2
dpkg --build debian/tmp ..
dpkg-genchanges -b > ../test-2.changes
rm debian/files
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg-gencontrol -psibling -v2.2
FAKEARCHITECTUE="another" DEB_HOST_ARCH="another" dpkg --build debian/tmp ..
dpkg-genchanges -b > ../test-2a.changes
cd ..
rm -r test-2

for tracking in false true ; do

echo "with tracking is $tracking"

mkdir conf
cat > conf/distributions <<EOF
Codename: two
Components: main bad
Architectures: source $FAKEARCHITECTURE another somemore

Codename: test
Components: main bad
Architectures: source $FAKEARCHITECTURE another somemore
EOF

testrun - -b . -A another include test test-1.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
-v3*=Limiting 'mytest_2_all.deb' to architectures another as requested.
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/t"
-v2*=Created directory "./pool/main/t/test"
-d1*=db: 'pool/main/t/test/mytest_2_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/t/test/siblingtoo_3_another.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/t/test/sibling_2_another.deb' added to checksums.db(pool).
-d1*=db: 'mytest' added to packages.db(test|main|another).
-d1*=db: 'siblingtoo' added to packages.db(test|main|another).
-d1*=db: 'sibling' added to packages.db(test|main|another).
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test"
-v2*=Created directory "./dists/test/main"
-v2*=Created directory "./dists/test/main/binary-$FAKEARCHITECTURE"
-v2*=Created directory "./dists/test/main/binary-another"
-v2*=Created directory "./dists/test/main/binary-somemore"
-v2*=Created directory "./dists/test/main/source"
-v2*=Created directory "./dists/test/bad"
-v2*=Created directory "./dists/test/bad/binary-$FAKEARCHITECTURE"
-v2*=Created directory "./dists/test/bad/binary-another"
-v2*=Created directory "./dists/test/bad/binary-somemore"
-v2*=Created directory "./dists/test/bad/source"
-v6*= looking for changes in 'test|main|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/test/main/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|another'...
-v6*=  creating './dists/test/main/binary-another/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|somemore'...
-v6*=  creating './dists/test/main/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|source'...
-v6*=  creating './dists/test/main/source/Sources' (gzipped)
-v6*= looking for changes in 'test|bad|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/test/bad/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|bad|another'...
-v6*=  creating './dists/test/bad/binary-another/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|bad|somemore'...
-v6*=  creating './dists/test/bad/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|bad|source'...
-v6*=  creating './dists/test/bad/source/Sources' (gzipped)
EOF

testrun - -b . -A "$FAKEARCHITECTURE" include test test-2.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
-v3*=Limiting 'mytest_2.4_all.deb' to architectures $FAKEARCHITECTURE as requested.
stdout
-d1*=db: 'pool/main/t/test/mytest_2.4_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/t/test/siblingalso_3.1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/t/test/sibling_2.2_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'mytest' added to packages.db(test|main|${FAKEARCHITECTURE}).
-d1*=db: 'siblingalso' added to packages.db(test|main|${FAKEARCHITECTURE}).
-d1*=db: 'sibling' added to packages.db(test|main|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test/main/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|another'...
-v6*= looking for changes in 'test|main|somemore'...
-v6*= looking for changes in 'test|main|source'...
-v6*= looking for changes in 'test|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|bad|another'...
-v6*= looking for changes in 'test|bad|somemore'...
-v6*= looking for changes in 'test|bad|source'...
EOF

if $tracking ; then
echo "Tracking: minimal" >> conf/distributions
testrun - -b . retrack test 3<<EOF
stdout
*=Retracking test...
#2 times:
-d1*=db: 'test' added to tracking.db(test).
EOF
fi

testrun - -b . list test 3<<EOF
stdout
*=test|main|${FAKEARCHITECTURE}: mytest 2.4
*=test|main|${FAKEARCHITECTURE}: sibling 2.2
*=test|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=test|main|another: mytest 2
*=test|main|another: sibling 2
*=test|main|another: siblingtoo 3
EOF

testrun - -b . flood test 3<<EOF
stdout
-d1*=db: 'mytest' added to packages.db(test|main|somemore).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|main|another'...
-v6*= looking for changes in 'test|main|somemore'...
-v6*=  replacing './dists/test/main/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|source'...
-v6*= looking for changes in 'test|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|bad|another'...
-v6*= looking for changes in 'test|bad|somemore'...
-v6*= looking for changes in 'test|bad|source'...
EOF

testrun - -b . list test 3<<EOF
stdout
*=test|main|${FAKEARCHITECTURE}: mytest 2.4
*=test|main|${FAKEARCHITECTURE}: sibling 2.2
*=test|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=test|main|another: mytest 2
*=test|main|another: sibling 2
*=test|main|another: siblingtoo 3
*=test|main|somemore: mytest 2.4
EOF

testrun - -b . -C main -A somemore includedeb test siblingtoo_3_somemore.deb 3<<EOF
stdout
-d1*=db: 'pool/main/t/test/siblingtoo_3_somemore.deb' added to checksums.db(pool).
-d1*=db: 'siblingtoo' added to packages.db(test|main|somemore).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|main|another'...
-v6*= looking for changes in 'test|main|somemore'...
-v6*=  replacing './dists/test/main/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|source'...
-v6*= looking for changes in 'test|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|bad|another'...
-v6*= looking for changes in 'test|bad|somemore'...
-v6*= looking for changes in 'test|bad|source'...
EOF

testrun empty -b . flood test

testrun - -b . -A somemore remove test mytest 3<<EOF
stdout
-v1*=removing 'mytest' from 'test|main|somemore'...
-d1*=db: 'mytest' removed from packages.db(test|main|somemore).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|main|another'...
-v6*= looking for changes in 'test|main|somemore'...
-v6*=  replacing './dists/test/main/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|source'...
-v6*= looking for changes in 'test|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|bad|another'...
-v6*= looking for changes in 'test|bad|somemore'...
-v6*= looking for changes in 'test|bad|source'...
EOF

testrun - -b . list test 3<<EOF
stdout
*=test|main|${FAKEARCHITECTURE}: mytest 2.4
*=test|main|${FAKEARCHITECTURE}: sibling 2.2
*=test|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=test|main|another: mytest 2
*=test|main|another: sibling 2
*=test|main|another: siblingtoo 3
*=test|main|somemore: siblingtoo 3
EOF

testrun - -b . flood test 3<<EOF
stdout
-d1*=db: 'mytest' added to packages.db(test|main|somemore).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|main|another'...
-v6*= looking for changes in 'test|main|somemore'...
-v6*=  replacing './dists/test/main/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|source'...
-v6*= looking for changes in 'test|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|bad|another'...
-v6*= looking for changes in 'test|bad|somemore'...
-v6*= looking for changes in 'test|bad|source'...
EOF

testrun - -b . list test 3<<EOF
stdout
*=test|main|${FAKEARCHITECTURE}: mytest 2.4
*=test|main|${FAKEARCHITECTURE}: sibling 2.2
*=test|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=test|main|another: mytest 2
*=test|main|another: sibling 2
*=test|main|another: siblingtoo 3
*=test|main|somemore: siblingtoo 3
*=test|main|somemore: mytest 2
EOF

testrun - -b . -C main includedeb test sibling_2.2_another.deb 3<<EOF
stdout
-d1*=db: 'pool/main/t/test/sibling_2.2_another.deb' added to checksums.db(pool).
-d1*=db: 'sibling' removed from packages.db(test|main|another).
-d1*=db: 'sibling' added to packages.db(test|main|another).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|main|another'...
-v6*=  replacing './dists/test/main/binary-another/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|somemore'...
-v6*= looking for changes in 'test|main|source'...
-v6*= looking for changes in 'test|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|bad|another'...
-v6*= looking for changes in 'test|bad|somemore'...
-v6*= looking for changes in 'test|bad|source'...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/main/t/test/sibling_2_another.deb
-d1*=db: 'pool/main/t/test/sibling_2_another.deb' removed from checksums.db(pool).
EOF

if $tracking ; then
testout - -b . dumptracks test 3<<EOF
EOF
cat > results.expected <<EOF
Distribution: test
Source: test
Version: 1-1
Files:
 pool/main/t/test/mytest_2_all.deb a 2
 pool/main/t/test/siblingtoo_3_another.deb b 1
 pool/main/t/test/siblingtoo_3_somemore.deb b 1

Distribution: test
Source: test
Version: 2-1
Files:
 pool/main/t/test/mytest_2.4_all.deb a 1
 pool/main/t/test/sibling_2.2_${FAKEARCHITECTURE}.deb b 1
 pool/main/t/test/siblingalso_3.1_${FAKEARCHITECTURE}.deb b 1
 pool/main/t/test/sibling_2.2_another.deb b 1

EOF
dodiff results.expected results
fi

testrun - -b . flood test 3<<EOF
stdout
-d1*=db: 'mytest' removed from packages.db(test|main|another).
-d1*=db: 'mytest' added to packages.db(test|main|another).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|main|another'...
-v6*=  replacing './dists/test/main/binary-another/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|somemore'...
-v6*= looking for changes in 'test|main|source'...
-v6*= looking for changes in 'test|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test|bad|another'...
-v6*= looking for changes in 'test|bad|somemore'...
-v6*= looking for changes in 'test|bad|source'...
EOF

testrun - -b . list test 3<<EOF
stdout
*=test|main|${FAKEARCHITECTURE}: mytest 2.4
*=test|main|${FAKEARCHITECTURE}: sibling 2.2
*=test|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=test|main|another: mytest 2.4
*=test|main|another: sibling 2.2
*=test|main|another: siblingtoo 3
*=test|main|somemore: siblingtoo 3
*=test|main|somemore: mytest 2
EOF

if $tracking ; then
testout - -b . dumptracks test 3<<EOF
EOF
cat > results.expected <<EOF
Distribution: test
Source: test
Version: 1-1
Files:
 pool/main/t/test/mytest_2_all.deb a 1
 pool/main/t/test/siblingtoo_3_another.deb b 1
 pool/main/t/test/siblingtoo_3_somemore.deb b 1

Distribution: test
Source: test
Version: 2-1
Files:
 pool/main/t/test/mytest_2.4_all.deb a 2
 pool/main/t/test/sibling_2.2_${FAKEARCHITECTURE}.deb b 1
 pool/main/t/test/siblingalso_3.1_${FAKEARCHITECTURE}.deb b 1
 pool/main/t/test/sibling_2.2_another.deb b 1

EOF
dodiff results.expected results
fi

cat > conf/incoming << EOF
Name: myrule
Allow: test>two
Options: limit_arch_all
IncomingDir: i
TempDir: tmp
EOF

ls *.changes
mkdir i tmp
cp test-1.changes sibling_2_another.deb siblingtoo_3_another.deb mytest_2_all.deb i/

testrun - -b . processincoming myrule 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'pool/main/t/test/sibling_2_another.deb' added to checksums.db(pool).
-d1*=db: 'mytest' added to packages.db(two|main|another).
-d1*=db: 'siblingtoo' added to packages.db(two|main|another).
-d1*=db: 'sibling' added to packages.db(two|main|another).
-v1*=deleting './i/mytest_2_all.deb'...
-v1*=deleting './i/siblingtoo_3_another.deb'...
-v1*=deleting './i/test-1.changes'...
-v1*=deleting './i/sibling_2_another.deb'...
-v0*=Exporting indices...
-v2*=Created directory "./dists/two"
-v2*=Created directory "./dists/two/main"
-v2*=Created directory "./dists/two/main/binary-$FAKEARCHITECTURE"
-v2*=Created directory "./dists/two/main/binary-another"
-v2*=Created directory "./dists/two/main/binary-somemore"
-v2*=Created directory "./dists/two/main/source"
-v2*=Created directory "./dists/two/bad"
-v2*=Created directory "./dists/two/bad/binary-$FAKEARCHITECTURE"
-v2*=Created directory "./dists/two/bad/binary-another"
-v2*=Created directory "./dists/two/bad/binary-somemore"
-v2*=Created directory "./dists/two/bad/source"
-v6*= looking for changes in 'two|main|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/two/main/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|main|another'...
-v6*=  creating './dists/two/main/binary-another/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|main|somemore'...
-v6*=  creating './dists/two/main/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|main|source'...
-v6*=  creating './dists/two/main/source/Sources' (gzipped)
-v6*= looking for changes in 'two|bad|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/two/bad/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|bad|another'...
-v6*=  creating './dists/two/bad/binary-another/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|bad|somemore'...
-v6*=  creating './dists/two/bad/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|bad|source'...
-v6*=  creating './dists/two/bad/source/Sources' (gzipped)
EOF

testrun - -b . list two 3<<EOF
stdout
*=two|main|another: mytest 2
*=two|main|another: sibling 2
*=two|main|another: siblingtoo 3
EOF

testrun - -b . flood two 3<<EOF
stdout
-d1*=db: 'mytest' added to packages.db(two|main|somemore).
-d1*=db: 'mytest' added to packages.db(two|main|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v6*= looking for changes in 'two|main|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/two/main/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|main|another'...
-v6*= looking for changes in 'two|main|somemore'...
-v6*=  replacing './dists/two/main/binary-somemore/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|main|source'...
-v6*= looking for changes in 'two|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'two|bad|another'...
-v6*= looking for changes in 'two|bad|somemore'...
-v6*= looking for changes in 'two|bad|source'...
EOF

testrun - -b . list two 3<<EOF
stdout
*=two|main|${FAKEARCHITECTURE}: mytest 2
*=two|main|another: mytest 2
*=two|main|another: sibling 2
*=two|main|another: siblingtoo 3
*=two|main|somemore: mytest 2
EOF

dodo rmdir i
mkdir i

cp test-2.changes siblingalso_3.1_abacus.deb mytest_2.4_all.deb sibling_2.2_abacus.deb  i/

testrun - -b . processincoming myrule 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'mytest' removed from packages.db(two|main|${FAKEARCHITECTURE}).
-d1*=db: 'mytest' added to packages.db(two|main|${FAKEARCHITECTURE}).
-d1*=db: 'siblingalso' added to packages.db(two|main|${FAKEARCHITECTURE}).
-d1*=db: 'sibling' added to packages.db(two|main|${FAKEARCHITECTURE}).
-v1*=deleting './i/mytest_2.4_all.deb'...
-v1*=deleting './i/siblingalso_3.1_${FAKEARCHITECTURE}.deb'...
-v1*=deleting './i/sibling_2.2_${FAKEARCHITECTURE}.deb'...
-v1*=deleting './i/test-2.changes'...
-v0*=Exporting indices...
-v6*= looking for changes in 'two|main|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/two/main/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|main|another'...
-v6*= looking for changes in 'two|main|somemore'...
-v6*= looking for changes in 'two|main|source'...
-v6*= looking for changes in 'two|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'two|bad|another'...
-v6*= looking for changes in 'two|bad|somemore'...
-v6*= looking for changes in 'two|bad|source'...
EOF

testrun - -b . list two 3<<EOF
stdout
*=two|main|${FAKEARCHITECTURE}: mytest 2.4
*=two|main|${FAKEARCHITECTURE}: sibling 2.2
*=two|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=two|main|another: mytest 2
*=two|main|another: sibling 2
*=two|main|another: siblingtoo 3
*=two|main|somemore: mytest 2
EOF

testrun empty -b . flood test

testrun - -b . list two 3<<EOF
stdout
*=two|main|${FAKEARCHITECTURE}: mytest 2.4
*=two|main|${FAKEARCHITECTURE}: sibling 2.2
*=two|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=two|main|another: mytest 2
*=two|main|another: sibling 2
*=two|main|another: siblingtoo 3
*=two|main|somemore: mytest 2
EOF

dodo rmdir i
mkdir i
cp test-2a.changes sibling_2.2_another.deb i/

testrun - -b . processincoming myrule 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'sibling' removed from packages.db(two|main|another).
-d1*=db: 'sibling' added to packages.db(two|main|another).
-v1*=deleting './i/sibling_2.2_another.deb'...
-v1*=deleting './i/test-2a.changes'...
-v0*=Exporting indices...
-v6*= looking for changes in 'two|main|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'two|main|another'...
-v6*=  replacing './dists/two/main/binary-another/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|main|somemore'...
-v6*= looking for changes in 'two|main|source'...
-v6*= looking for changes in 'two|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'two|bad|another'...
-v6*= looking for changes in 'two|bad|somemore'...
-v6*= looking for changes in 'two|bad|source'...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/main/t/test/sibling_2_another.deb
-d1*=db: 'pool/main/t/test/sibling_2_another.deb' removed from checksums.db(pool).
EOF

testrun - -b . list two 3<<EOF
stdout
*=two|main|${FAKEARCHITECTURE}: mytest 2.4
*=two|main|${FAKEARCHITECTURE}: sibling 2.2
*=two|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=two|main|another: mytest 2
*=two|main|another: sibling 2.2
*=two|main|another: siblingtoo 3
*=two|main|somemore: mytest 2
EOF

testrun - -b . flood test 3<<EOF
-d1*=db: 'mytest' removed from packages.db(two|main|another).
-d1*=db: 'mytest' added to packages.db(two|main|another).
-v0*=Exporting indices...
-v6*= looking for changes in 'two|main|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'two|main|another'...
-v6*=  replacing './dists/two/main/binary-another/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'two|main|somemore'...
-v6*= looking for changes in 'two|main|source'...
-v6*= looking for changes in 'two|bad|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'two|bad|another'...
-v6*= looking for changes in 'two|bad|somemore'...
-v6*= looking for changes in 'two|bad|source'...
EOF

testrun - -b . list two 3<<EOF
stdout
*=two|main|${FAKEARCHITECTURE}: mytest 2.4
*=two|main|${FAKEARCHITECTURE}: sibling 2.2
*=two|main|${FAKEARCHITECTURE}: siblingalso 3.1
*=two|main|another: mytest 2.4
*=two|main|another: sibling 2.2
*=two|main|another: siblingtoo 3
*=two|main|somemore: mytest 2
EOF

rm -r conf dists pool db
dodo rmdir i tmp
done

rm *.deb *.changes results results.expected
testsuccess
