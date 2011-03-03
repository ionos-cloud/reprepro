#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
        source $(dirname $0)/test.inc
fi

dodo test ! -e dists
mkdir conf db logs lists

for tracking in true false ; do
if $tracking ; then
echo "this is the test variant with tracking on"
else
echo "this is the test variant with tracking off"
fi

if $tracking ; then
cat >> conf/distributions <<EOF

Codename: a
Architectures: ${FAKEARCHITECTURE} source
Components: all
Tracking: minimal
Log: logab

Codename: b
Architectures: ${FAKEARCHITECTURE}
Components: all
Pull: froma
Log: logab
EOF
setoptions unchanged "" "" tracking
else
cat >> conf/distributions <<EOF

Codename: a
Architectures: ${FAKEARCHITECTURE} source
Components: all
Log: logab

Codename: b
Architectures: ${FAKEARCHITECTURE}
Components: all
Pull: froma
Log: logab
EOF
setoptions unchanged "" ""
fi

checknolog logab
cat > conf/pulls <<EOF
Name: froma
From: a
Architectures: froma>toa froma>toa2 froma2>toa2
Components: c1 c2
UDebComponents: u1 u2
EOF
testrun - -b . --export=changed pull a b 3<<EOF
stderr
*=Error parsing ./conf/pulls, line 3, column 16: Unknown architecture 'froma' in Architectures.
-v0*=There have been errors!
return 255
EOF
cp conf/distributions conf/distributions.old
cat >> conf/distributions <<EOF

Codename: moreatoms
Architectures: froma froma2 toa toa2
Components: c1 c2 u1 u2
EOF

testrun - -b . --export=changed pull a b 3<<EOF
stderr
*=(This will simply be ignored and is not even checked when using --fast).
*=Warning: pull rule 'froma' wants to get something from architecture 'froma',
*=Warning: pull rule 'froma' wants to get something from architecture 'froma2',
*=but there is no such architecture in distribution 'a'.
*=Warning: pull rule 'froma' wants to get something from component 'c1',
*=Warning: pull rule 'froma' wants to get something from component 'c2',
*=but there is no such component in distribution 'a'.
*=Warning: pull rule 'froma' wants to get something from udeb component 'u1',
*=Warning: pull rule 'froma' wants to get something from udeb component 'u2',
*=but there is no such udeb component in distribution 'a'.
*=Warning: pull rule 'froma' wants to put something into architecture 'toa',
*=but no distribution using this has such an architecture.
*=Warning: pull rule 'froma' wants to put something into architecture 'toa2',
*=Warning: pull rule 'froma' wants to put something into component 'c1',
*=but no distribution using this has such an component.
*=Warning: pull rule 'froma' wants to put something into component 'c2',
*=Warning: pull rule 'froma' wants to put something into udeb component 'u1',
*=but no distribution using this has such an udeb component.
*=Warning: pull rule 'froma' wants to put something into udeb component 'u2',
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|${FAKEARCHITECTURE}'
-v0*=Installing (and possibly deleting) packages...
EOF
mv conf/distributions.old conf/distributions
testrun - -b . clearvanished 3<<EOF
stderr
stdout
*=Deleting vanished identifier 'moreatoms|c1|froma'.
*=Deleting vanished identifier 'moreatoms|c1|froma2'.
*=Deleting vanished identifier 'moreatoms|c1|toa'.
*=Deleting vanished identifier 'moreatoms|c1|toa2'.
*=Deleting vanished identifier 'moreatoms|c2|froma'.
*=Deleting vanished identifier 'moreatoms|c2|froma2'.
*=Deleting vanished identifier 'moreatoms|c2|toa'.
*=Deleting vanished identifier 'moreatoms|c2|toa2'.
*=Deleting vanished identifier 'moreatoms|u1|froma'.
*=Deleting vanished identifier 'moreatoms|u1|froma2'.
*=Deleting vanished identifier 'moreatoms|u1|toa'.
*=Deleting vanished identifier 'moreatoms|u1|toa2'.
*=Deleting vanished identifier 'moreatoms|u2|froma'.
*=Deleting vanished identifier 'moreatoms|u2|froma2'.
*=Deleting vanished identifier 'moreatoms|u2|toa'.
*=Deleting vanished identifier 'moreatoms|u2|toa2'.
EOF
cat > conf/pulls <<EOF
Name: froma
From: a
EOF
testrun - -b . --export=changed pull a b 3<<EOF
stderr
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|${FAKEARCHITECTURE}'
-v5*=  looking what to get from 'a|all|${FAKEARCHITECTURE}'
-v0*=Installing (and possibly deleting) packages...
EOF
checklog logab < /dev/null
test ! -d dists/a
test ! -d dists/b
testrun - -b . --export=normal pull b 3<<EOF
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|${FAKEARCHITECTURE}'
-v5*=  looking what to get from 'a|all|${FAKEARCHITECTURE}'
-v0*=Installing (and possibly deleting) packages...
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/b"
-v2*=Created directory "./dists/b/all"
-v2*=Created directory "./dists/b/all/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/b/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
EOF
checklog logab < /dev/null
test ! -d dists/a
test -d dists/b
testrun - -b . --export=normal pull a b 3<<EOF
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|${FAKEARCHITECTURE}'
-v5*=  looking what to get from 'a|all|${FAKEARCHITECTURE}'
-v0*=Installing (and possibly deleting) packages...
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v2*=Created directory "./dists/a"
-v2*=Created directory "./dists/a/all"
-v2*=Created directory "./dists/a/all/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'a|all|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/a/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/a/all/source"
-v6*= looking for changes in 'a|all|source'...
-v6*=  creating './dists/a/all/source/Sources' (gzipped)
EOF
checklog logab < /dev/null
test -d dists/a
test -d dists/b
rm -r dists/a dists/b
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun - -b . --export=never --delete --delete include a test.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: database 'a|all|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
stdout
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/all"
-v2*=Created directory "./pool/all/a"
-v2*=Created directory "./pool/all/a/aa"
-d1*=db: 'pool/all/a/aa/aa-addons_1-1_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-1.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-1.dsc' added to checksums.db(pool).
-d1*=db: 'aa-addons' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(a|all|source).
-t1*=db: 'aa' added to tracking.db(a).
-v5*=Deleting 'test.changes'.
EOF
checklog logab << EOF
DATESTR add a deb all ${FAKEARCHITECTURE} aa-addons 1-1
DATESTR add a deb all ${FAKEARCHITECTURE} aa 1-1
DATESTR add a dsc all source aa 1-1
EOF
test ! -d dists/a
test ! -d dists/b
test ! -f test.changes
test ! -f aa_1-1_${FAKEARCHITECTURE}.deb
test ! -f aa_1-1.dsc
test ! -f aa_1-1.tar.gz
test ! -f aa-addons_1-1_all.deb
test -f pool/all/a/aa/aa-addons_1-1_all.deb
test -f pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb
test -f pool/all/a/aa/aa_1-1.dsc
test -f pool/all/a/aa/aa_1-1.tar.gz
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-1
Files:
 pool/all/a/aa/aa-addons_1-1_all.deb a 1
 pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb b 1
 pool/all/a/aa/aa_1-1.dsc s 1
 pool/all/a/aa/aa_1-1.tar.gz s 1

END
if $tracking; then dodiff results.expected results ; else dodiff results.empty results ; fi
testrun - -b . export a 3<<EOF
stdout
-v1*=Exporting a...
-v2*=Created directory "./dists/a"
-v2*=Created directory "./dists/a/all"
-v2*=Created directory "./dists/a/all/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'a|all|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/a/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/a/all/source"
-v6*= exporting 'a|all|source'...
-v6*=  creating './dists/a/all/source/Sources' (gzipped)
EOF
checknolog logab
dogrep "Version: 1-1" dists/a/all/binary-${FAKEARCHITECTURE}/Packages
rm -r dists/a
testout - -b . dumppull b 3<<EOF
stderr
EOF
cat > results.expected <<EOF
Updates needed for 'b|all|${FAKEARCHITECTURE}':
add 'aa' - '1-1' 'froma'
add 'aa-addons' - '1-1' 'froma'
EOF
dodiff results results.expected
testrun - -b . --export=changed pull a b 3<<EOF
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|${FAKEARCHITECTURE}'
-v5*=  looking what to get from 'a|all|${FAKEARCHITECTURE}'
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'aa' added to packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa-addons' added to packages.db(b|all|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v2*=Created directory "./dists/b"
-v2*=Created directory "./dists/b/all"
-v2*=Created directory "./dists/b/all/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/b/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
EOF
checklog logab << EOF
DATESTR add b deb all ${FAKEARCHITECTURE} aa 1-1
DATESTR add b deb all ${FAKEARCHITECTURE} aa-addons 1-1
EOF
test ! -d dists/a
test -d dists/b
dogrep "Version: 1-1" dists/b/all/binary-${FAKEARCHITECTURE}/Packages
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh
testrun - -b . --export=changed --delete include a test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'pool/all/a/aa/aa-addons_1-2_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-2.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-2.dsc' added to checksums.db(pool).
-d1*=db: 'aa-addons' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa-addons' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' removed from packages.db(a|all|source).
-d1*=db: 'aa' added to packages.db(a|all|source).
-t1*=db: 'aa' added to tracking.db(a).
-t1*=db: 'aa' '1-1' removed from tracking.db(a).
-v0*=Exporting indices...
-v2*=Created directory "./dists/a"
-v2*=Created directory "./dists/a/all"
-v2*=Created directory "./dists/a/all/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'a|all|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/a/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/a/all/source"
-v6*= looking for changes in 'a|all|source'...
-v6*=  creating './dists/a/all/source/Sources' (gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa_1-1.dsc
-d1*=db: 'pool/all/a/aa/aa_1-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-1.tar.gz
-d1*=db: 'pool/all/a/aa/aa_1-1.tar.gz' removed from checksums.db(pool).
EOF
checklog logab << EOF
DATESTR replace a deb all ${FAKEARCHITECTURE} aa-addons 1-2 1-1
DATESTR replace a deb all ${FAKEARCHITECTURE} aa 1-2 1-1
DATESTR replace a dsc all source aa 1-2 1-1
EOF
test -f test.changes
test ! -f aa_1-2_${FAKEARCHITECTURE}.deb
test ! -f aa_1-2.dsc
test ! -f aa_1-2.tar.gz
test ! -f aa-addons_1-2_all.deb
test -d dists/a
dogrep "Version: 1-2" dists/a/all/binary-${FAKEARCHITECTURE}/Packages
dogrep "Version: 1-1" dists/b/all/binary-${FAKEARCHITECTURE}/Packages
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-2
Files:
 pool/all/a/aa/aa-addons_1-2_all.deb a 1
 pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb b 1
 pool/all/a/aa/aa_1-2.dsc s 1
 pool/all/a/aa/aa_1-2.tar.gz s 1

END
if $tracking; then dodiff results.expected results ; else dodiff results.empty results ; fi
rm -r dists/a dists/b
testout - -b . dumppull b 3<<EOF
stderr
EOF
cat > results.expected <<EOF
Updates needed for 'b|all|${FAKEARCHITECTURE}':
update 'aa' '1-1' '1-2' 'froma'
update 'aa-addons' '1-1' '1-2' 'froma'
EOF
dodiff results results.expected
testrun - -b . --export=changed pull a b 3<<EOF
stderr
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|${FAKEARCHITECTURE}'
-v5*=  looking what to get from 'a|all|${FAKEARCHITECTURE}'
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'aa' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa-addons' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa-addons' added to packages.db(b|all|${FAKEARCHITECTURE}).
-v0=Exporting indices...
-v2*=Created directory "./dists/b"
-v2*=Created directory "./dists/b/all"
-v2*=Created directory "./dists/b/all/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/b/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-1_all.deb
-d1*=db: 'pool/all/a/aa/aa-addons_1-1_all.deb' removed from checksums.db(pool).
EOF
checklog logab << EOF
DATESTR replace b deb all ${FAKEARCHITECTURE} aa 1-2 1-1
DATESTR replace b deb all ${FAKEARCHITECTURE} aa-addons 1-2 1-1
EOF
test ! -d dists/a
test -d dists/b
dogrep "Version: 1-2" dists/b/all/binary-${FAKEARCHITECTURE}/Packages
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-3" SECTION="stupid/base" genpackage.sh
testrun - -b . --export=never include a test.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: database 'a|all|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
stdout
-d1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/all/a/aa/aa_1-3.dsc' added to checksums.db(pool).
-d1*=db: 'aa-addons' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa-addons' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' removed from packages.db(a|all|source).
-d1*=db: 'aa' added to packages.db(a|all|source).
-t1*=db: 'aa' added to tracking.db(a).
-t1*=db: 'aa' '1-2' removed from tracking.db(a).
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa_1-2.dsc
-d1*=db: 'pool/all/a/aa/aa_1-2.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-2.tar.gz
-d1*=db: 'pool/all/a/aa/aa_1-2.tar.gz' removed from checksums.db(pool).
EOF
checklog logab << EOF
DATESTR replace a deb all ${FAKEARCHITECTURE} aa-addons 1-3 1-2
DATESTR replace a deb all ${FAKEARCHITECTURE} aa 1-3 1-2
DATESTR replace a dsc all source aa 1-3 1-2
EOF
test -f test.changes
test -f aa_1-3_${FAKEARCHITECTURE}.deb
test -f aa_1-3.dsc
test -f aa_1-3.tar.gz
test -f aa-addons_1-3_all.deb
test ! -f pool/all/a/aa/aa_1-2.dsc
test -f pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb # still in b
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-3
Files:
 pool/all/a/aa/aa-addons_1-3_all.deb a 1
 pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb b 1
 pool/all/a/aa/aa_1-3.dsc s 1
 pool/all/a/aa/aa_1-3.tar.gz s 1

END
if $tracking; then dodiff results.expected results ; else dodiff results.empty results ; fi
testout "" -b . dumpunreferenced
dodiff results.empty results
DISTRI=a PACKAGE=ab EPOCH="" VERSION=2 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun - -b . --delete --delete --export=never include a test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
*=Warning: database 'a|all|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
=Changes will only be visible after the next 'export'!
stdout
-v2*=Created directory "./pool/all/a/ab"
-d1*=db: 'pool/all/a/ab/ab-addons_2-1_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/ab/ab_2-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/ab/ab_2-1.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/all/a/ab/ab_2-1.dsc' added to checksums.db(pool).
-d1*=db: 'ab-addons' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'ab' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'ab' added to packages.db(a|all|source).
-t1*=db: 'ab' added to tracking.db(a).
-v5*=Deleting 'test.changes'.
EOF
checklog logab << EOF
DATESTR add a deb all ${FAKEARCHITECTURE} ab-addons 2-1
DATESTR add a deb all ${FAKEARCHITECTURE} ab 2-1
DATESTR add a dsc all source ab 2-1
EOF
testout - -b . dumppull b 3<<EOF
stderr
EOF
cat > results.expected <<EOF
Updates needed for 'b|all|${FAKEARCHITECTURE}':
update 'aa' '1-2' '1-3' 'froma'
update 'aa-addons' '1-2' '1-3' 'froma'
add 'ab' - '2-1' 'froma'
add 'ab-addons' - '2-1' 'froma'
EOF
dodiff results results.expected

testrun - -b . --export=changed pull b 3<<EOF
stderr
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|${FAKEARCHITECTURE}'
-v5*=  looking what to get from 'a|all|${FAKEARCHITECTURE}'
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'aa' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa-addons' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'aa-addons' added to packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'ab' added to packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'ab-addons' added to packages.db(b|all|${FAKEARCHITECTURE}).
-v0=Exporting indices...
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/b/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-2_all.deb
-d1*=db: 'pool/all/a/aa/aa-addons_1-2_all.deb' removed from checksums.db(pool).
EOF
checklog logab << EOF
DATESTR replace b deb all ${FAKEARCHITECTURE} aa 1-3 1-2
DATESTR replace b deb all ${FAKEARCHITECTURE} aa-addons 1-3 1-2
DATESTR add b deb all ${FAKEARCHITECTURE} ab 2-1
DATESTR add b deb all ${FAKEARCHITECTURE} ab-addons 2-1
EOF
testout - -b . dumppull b 3<<EOF
stderr
EOF
cat > results.expected <<EOF
Updates needed for 'b|all|${FAKEARCHITECTURE}':
keep 'aa' '1-3' '1-3'
keep 'aa-addons' '1-3' '1-3'
keep 'ab' '2-1' '2-1'
keep 'ab-addons' '2-1' '2-1'
EOF
dodiff results results.expected
dogrep "Version: 1-3" dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dogrep "Version: 2-1" dists/b/all/binary-${FAKEARCHITECTURE}/Packages
test ! -f pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb
test -f pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
DISTRI=a PACKAGE=ab EPOCH="" VERSION=3 REVISION="-1" SECTION="stupid/base" genpackage.sh
grep -v '\.tar\.gz' test.changes > broken.changes
testrun - -b . --delete --delete include a broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=I don't know what to do having a .dsc without a .diff.gz or .tar.gz in 'broken.changes'!
-v0*=There have been errors!
returns 255
EOF
checknolog logab
echo " $EMPTYMD5 stupid/base superfluous ab_3-1.diff.gz" >> broken.changes
testrun - -b . --delete --delete include a broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Cannot find file './ab_3-1.diff.gz' needed by 'broken.changes'!
-v0*=There have been errors!
returns 249
EOF
checknolog logab
test -f broken.changes
test ! -f ab_3-1.diff.gz
test -f ab-addons_3-1_all.deb
test -f ab_3-1_${FAKEARCHITECTURE}.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test ! -f pool/all/a/ab/ab-addons_3-1_all.deb
test ! -f pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
touch ab_3-1.diff.gz
testrun - -b . --delete -T deb include a broken.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'ab-addons' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'ab-addons' added to packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'ab' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-d1*=db: 'ab' added to packages.db(a|all|${FAKEARCHITECTURE}).
-t1*=db: 'ab' added to tracking.db(a).
-v0*=Exporting indices...
-v2*=Created directory "./dists/a"
-v2*=Created directory "./dists/a/all"
-v2*=Created directory "./dists/a/all/binary-${FAKEARCHITECTURE}"
-v6*= looking for changes in 'a|all|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/a/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/a/all/source"
-v6*= looking for changes in 'a|all|source'...
-v6*=  creating './dists/a/all/source/Sources' (gzipped)
EOF
checklog logab <<EOF
DATESTR replace a deb all ${FAKEARCHITECTURE} ab-addons 3-1 2-1
DATESTR replace a deb all ${FAKEARCHITECTURE} ab 3-1 2-1
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
test -f broken.changes
test -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_${FAKEARCHITECTURE}.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-3
Files:
 pool/all/a/aa/aa-addons_1-3_all.deb a 1
 pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb b 1
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
 pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb b 1

END
if $tracking; then dodiff results.expected results ; else dodiff results.empty results ; fi
testout "" -b . dumpunreferenced
dodiff results.empty results
testrun - -b . --delete --delete include a broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Unable to find pool/all/a/ab/ab_3-1.tar.gz needed by ab_3-1.dsc!
*=Perhaps you forgot to give dpkg-buildpackage the -sa option,
= or you could try --ignore=missingfile to guess possible files to use.
-v0*=There have been errors!
stdout
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' added to checksums.db(pool).
-d1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' added to checksums.db(pool).
-v0*=Deleting files just added to the pool but not used (to avoid use --keepunusednewfiles next time)
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.diff.gz
-d1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' removed from checksums.db(pool).
returns 249
EOF
test -f broken.changes
test -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_${FAKEARCHITECTURE}.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
cat broken.changes
testrun - -b . -T dsc --delete --delete --ignore=missingfile include a broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Unable to find pool/all/a/ab/ab_3-1.tar.gz!
*=Perhaps you forgot to give dpkg-buildpackage the -sa option.
*=--ignore=missingfile was given, searching for file...
stdout
-d1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' added to checksums.db(pool).
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' added to checksums.db(pool).
-d1*=db: 'ab' removed from packages.db(a|all|source).
-d1*=db: 'ab' added to packages.db(a|all|source).
-t1*=db: 'ab' '2-1' removed from tracking.db(a).
-v0*=Deleting files just added to the pool but not used (to avoid use --keepunusednewfiles next time)
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.diff.gz
-d1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from checksums.db(pool).
-v5*=Deleting 'broken.changes'.
-v0*=Exporting indices...
-v6*= looking for changes in 'a|all|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'a|all|source'...
-v6*=  replacing './dists/a/all/source/Sources' (gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/ab/ab_2-1.dsc
-d1*=db: 'pool/all/a/ab/ab_2-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_2-1.tar.gz
-d1*=db: 'pool/all/a/ab/ab_2-1.tar.gz' removed from checksums.db(pool).
EOF
checklog logab <<EOF
DATESTR replace a dsc all source ab 3-1 2-1
EOF
test ! -f broken.changes
test ! -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_${FAKEARCHITECTURE}.deb
test ! -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
test -f pool/all/a/ab/ab_3-1.dsc
testout "" -b . dumpunreferenced
cat > results.expected << EOF
pool/all/a/ab/ab_3-1.diff.gz
EOF
dodiff results.empty results || dodiff results.expected results
testrun - -b . deleteunreferenced 3<<EOF
stdout
-v1=deleting and forgetting pool/all/a/ab/ab_3-1.diff.gz
-d1=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from checksums.db(pool).
EOF

DISTRI=b PACKAGE=ac EPOCH="" VERSION=1 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun - -b . -A ${FAKEARCHITECTURE} --delete --delete --ignore=missingfile include b test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v2*=Skipping 'ac_1-1.dsc' as architecture 'source' is not in the requested set.
-v2*=Skipping 'ac_1-1.tar.gz' as architecture 'source' is not in the requested set.
-v3*=Limiting 'ac-addons_1-1_all.deb' to architectures ${FAKEARCHITECTURE} as requested.
stdout
-v2*=Created directory "./pool/all/a/ac"
-d1*=db: 'pool/all/a/ac/ac-addons_1-1_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/all/a/ac/ac_1-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'ac-addons' added to packages.db(b|all|${FAKEARCHITECTURE}).
-d1*=db: 'ac' added to packages.db(b|all|${FAKEARCHITECTURE}).
-v5*=Deleting 'test.changes'.
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/b/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
EOF
checklog logab <<EOF
DATESTR add b deb all ${FAKEARCHITECTURE} ac-addons 1-1
DATESTR add b deb all ${FAKEARCHITECTURE} ac 1-1
EOF
dogrep '^Package: aa$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dogrep '^Package: aa-addons$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dogrep '^Package: ab$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dogrep '^Package: ab-addons$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dogrep '^Package: ac$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dogrep '^Package: ac-addons$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
echo "Update: - froma" >> conf/distributions
cat >conf/updates <<END
Name: froma
Method: copy:$WORKDIR
VerifyRelease: blindtrust
Suite: a
ListHook: /bin/cp
END
testout - -b . dumpupdate b 3<<EOF
-v6*=aptmethod start 'copy:$WORKDIR/dists/a/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/a/Release'
-v6*=aptmethod start 'copy:$WORKDIR/dists/a/all/binary-${FAKEARCHITECTURE}/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/a/all/binary-${FAKEARCHITECTURE}/Packages.gz'
-v2*=Uncompress './lists/froma_a_all_${FAKEARCHITECTURE}_Packages.gz' into './lists/froma_a_all_${FAKEARCHITECTURE}_Packages' using '/bin/gunzip'...
-v6*=Called /bin/cp './lists/froma_a_all_${FAKEARCHITECTURE}_Packages' './lists/_b_all_${FAKEARCHITECTURE}_froma_froma_a_all_${FAKEARCHITECTURE}_Packages'
-v6*=Listhook successfully returned!
EOF
cat > results.expected <<EOF
Updates needed for 'b|all|${FAKEARCHITECTURE}':
keep 'aa' '1-3' '1-3'
keep 'aa-addons' '1-3' '1-3'
update 'ab' '2-1' '3-1' 'froma'
update 'ab-addons' '2-1' '3-1' 'froma'
delete 'ac' '1-1'
delete 'ac-addons' '1-1'
EOF
dodiff results.expected results
testrun - -b . predelete b 3<<EOF
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/a/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/a/Release'
-v6*=Called /bin/cp './lists/froma_a_all_${FAKEARCHITECTURE}_Packages' './lists/_b_all_${FAKEARCHITECTURE}_froma_froma_a_all_${FAKEARCHITECTURE}_Packages'
-v6*=Listhook successfully returned!
stdout
-v0*=Removing obsolete or to be replaced packages...
-v3*=  processing updates for 'b|all|${FAKEARCHITECTURE}'
-v5*=  marking everything to be deleted
-v5*=  reading './lists/_b_all_${FAKEARCHITECTURE}_froma_froma_a_all_${FAKEARCHITECTURE}_Packages'
-d1*=db: 'ac-addons' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-v1*=removing 'ab' from 'b|all|${FAKEARCHITECTURE}'...
-d1*=db: 'ab' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-v1*=removing 'ab-addons' from 'b|all|${FAKEARCHITECTURE}'...
-d1*=db: 'ab-addons' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-v1*=removing 'ac' from 'b|all|${FAKEARCHITECTURE}'...
-d1*=db: 'ac' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-v1*=removing 'ac-addons' from 'b|all|${FAKEARCHITECTURE}'...
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/b/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v1*=Shutting down aptmethods...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/ab/ab_2-1_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/all/a/ab/ab_2-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_2-1_all.deb
-d1*=db: 'pool/all/a/ab/ab-addons_2-1_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ac/ac_1-1_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/all/a/ac/ac_1-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ac/ac-addons_1-1_all.deb
-d1*=db: 'pool/all/a/ac/ac-addons_1-1_all.deb' removed from checksums.db(pool).
-v2*=removed now empty directory ./pool/all/a/ac
EOF
testout - -b . dumpupdate b 3<<EOF
-v6*=aptmethod start 'copy:$WORKDIR/dists/a/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/a/Release'
-v6*=Called /bin/cp './lists/froma_a_all_${FAKEARCHITECTURE}_Packages' './lists/_b_all_${FAKEARCHITECTURE}_froma_froma_a_all_${FAKEARCHITECTURE}_Packages'
-v6*=Listhook successfully returned!
EOF
cat > results.expected <<EOF
Updates needed for 'b|all|${FAKEARCHITECTURE}':
keep 'aa' '1-3' '1-3'
keep 'aa-addons' '1-3' '1-3'
add 'ab' - '3-1' 'froma'
add 'ab-addons' - '3-1' 'froma'
EOF
dodiff results.expected results
checklog logab <<EOF
DATESTR remove b deb all ${FAKEARCHITECTURE} ab 2-1
DATESTR remove b deb all ${FAKEARCHITECTURE} ab-addons 2-1
DATESTR remove b deb all ${FAKEARCHITECTURE} ac 1-1
DATESTR remove b deb all ${FAKEARCHITECTURE} ac-addons 1-1
EOF
dogrep '^Package: aa$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dogrep '^Package: aa-addons$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dongrep '^Package: ab$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dongrep '^Package: ab-addons$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dongrep '^Package: ac$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
dongrep '^Package: ac-addons$' dists/b/all/binary-${FAKEARCHITECTURE}/Packages
test ! -f pool/all/a/ac/ac-addons_1-1_all.deb
test ! -f pool/all/a/ab/ab_2-1_${FAKEARCHITECTURE}.deb
test -f pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
testrun - -b . copy b a ab ac 3<<EOF
stderr
-v0*=Will not copy as not found: ac.
stdout
-v9*=Adding reference to 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' by 'b|all|${FAKEARCHITECTURE}'
-v1*=Adding 'ab' '3-1' to 'b|all|${FAKEARCHITECTURE}'.
-v3*=Not looking into 'a|all|source' as no matching target in 'b'!
-d1*=db: 'ab' added to packages.db(b|all|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/b/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
EOF
# readd?
#-v3*=No instance of 'ab' found in 'a|all|source'!
#-v3*=No instance of 'ac' found in 'a|all|${FAKEARCHITECTURE}'!
#-v3*=No instance of 'ac' found in 'a|all|source'!
checklog logab <<EOF
DATESTR add b deb all ${FAKEARCHITECTURE} ab 3-1
EOF
if $tracking ; then
testout "" -b . dumptracks
cat > results.expected <<EOF
Distribution: a
Source: aa
Version: 1-3
Files:
 pool/all/a/aa/aa-addons_1-3_all.deb a 1
 pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb b 1
 pool/all/a/aa/aa_1-3.dsc s 1
 pool/all/a/aa/aa_1-3.tar.gz s 1

Distribution: a
Source: ab
Version: 3-1
Files:
 pool/all/a/ab/ab-addons_3-1_all.deb a 1
 pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb b 1
 pool/all/a/ab/ab_3-1.dsc s 1
 pool/all/a/ab/ab_3-1.tar.gz s 1

EOF
dodiff results.expected results
testout "" -b . dumpreferences
cat > results.expected <<EOF
a aa 1-3 pool/all/a/aa/aa-addons_1-3_all.deb
a|all|${FAKEARCHITECTURE} pool/all/a/aa/aa-addons_1-3_all.deb
b|all|${FAKEARCHITECTURE} pool/all/a/aa/aa-addons_1-3_all.deb
a aa 1-3 pool/all/a/aa/aa_1-3.dsc
a|all|source pool/all/a/aa/aa_1-3.dsc
a aa 1-3 pool/all/a/aa/aa_1-3.tar.gz
a|all|source pool/all/a/aa/aa_1-3.tar.gz
a aa 1-3 pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
a|all|${FAKEARCHITECTURE} pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
b|all|${FAKEARCHITECTURE} pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
a ab 3-1 pool/all/a/ab/ab-addons_3-1_all.deb
a|all|${FAKEARCHITECTURE} pool/all/a/ab/ab-addons_3-1_all.deb
a ab 3-1 pool/all/a/ab/ab_3-1.dsc
a|all|source pool/all/a/ab/ab_3-1.dsc
a ab 3-1 pool/all/a/ab/ab_3-1.tar.gz
a|all|source pool/all/a/ab/ab_3-1.tar.gz
a ab 3-1 pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
a|all|${FAKEARCHITECTURE} pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
b|all|${FAKEARCHITECTURE} pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
EOF
dodiff results.expected results
fi
rm -r -f db2
cp -a db db2
echo tracking is $tracking
testrun - --keepunreferenced --dbdir ./db2 -b . removesrc a unknown 3<<EOF
stderr
-t1*=Nothing about source package 'unknown' found in the tracking data of 'a'!
-t1*=This either means nothing from this source in this version is there,
-t1*=or the tracking information might be out of date.
stdout
EOF
testrun - --keepunreferenced --dbdir ./db2 -b . removesrc a ab 3-1 3<<EOF
stdout
-v1*=removing 'ab-addons' from 'a|all|${FAKEARCHITECTURE}'...
-d1*=db: 'ab-addons' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-v1*=removing 'ab' from 'a|all|${FAKEARCHITECTURE}'...
-d1*=db: 'ab' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-v1*=removing 'ab' from 'a|all|source'...
-d1*=db: 'ab' removed from packages.db(a|all|source).
-t1*=db: 'ab' '3-1' removed from tracking.db(a).
-v0*=Exporting indices...
-v6*= looking for changes in 'a|all|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/a/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'a|all|source'...
-v6*=  replacing './dists/a/all/source/Sources' (gzipped)
-v1*=3 files lost their last reference.
-v1*=(dumpunreferenced lists such files, use deleteunreferenced to delete them.)
EOF
if $tracking ; then
checklog logab <<EOF
DATESTR remove a deb all ${FAKEARCHITECTURE} ab-addons 3-1
DATESTR remove a deb all ${FAKEARCHITECTURE} ab 3-1
DATESTR remove a dsc all source ab 3-1
EOF
else
checklog logab <<EOF
DATESTR remove a deb all ${FAKEARCHITECTURE} ab 3-1
DATESTR remove a deb all ${FAKEARCHITECTURE} ab-addons 3-1
DATESTR remove a dsc all source ab 3-1
EOF
fi
rm -r db2
cp -a db db2
testrun - --keepunreferenced --dbdir ./db2 -b . removesrc a ab 3<<EOF
stdout
-v1*=removing 'ab-addons' from 'a|all|${FAKEARCHITECTURE}'...
-d1*=db: 'ab-addons' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-v1*=removing 'ab' from 'a|all|${FAKEARCHITECTURE}'...
-d1*=db: 'ab' removed from packages.db(a|all|${FAKEARCHITECTURE}).
-v1*=removing 'ab' from 'a|all|source'...
-d1*=db: 'ab' removed from packages.db(a|all|source).
-t1*=db: 'ab' '3-1' removed from tracking.db(a).
-v0*=Exporting indices...
-v6*= looking for changes in 'a|all|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/a/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'a|all|source'...
-v6*=  replacing './dists/a/all/source/Sources' (gzipped)
-v1*=3 files lost their last reference.
-v1*=(dumpunreferenced lists such files, use deleteunreferenced to delete them.)
EOF
if $tracking ; then
checklog logab <<EOF
DATESTR remove a deb all ${FAKEARCHITECTURE} ab-addons 3-1
DATESTR remove a deb all ${FAKEARCHITECTURE} ab 3-1
DATESTR remove a dsc all source ab 3-1
EOF
else
checklog logab <<EOF
DATESTR remove a deb all ${FAKEARCHITECTURE} ab 3-1
DATESTR remove a deb all ${FAKEARCHITECTURE} ab-addons 3-1
DATESTR remove a dsc all source ab 3-1
EOF
fi
testout "" --keepunreferenced --dbdir ./db2 dumppull
cat > results.expected <<EOF
Updates needed for 'b|all|${FAKEARCHITECTURE}':
keep 'aa' '1-3' '1-3'
keep 'aa-addons' '1-3' '1-3'
keep 'ab' '3-1' unavailable
EOF
dodiff results.expected results
testrun - --keepunreferenced --dbdir ./db2 -b . removefilter b "Version (== 1-3), Package (>> aa)" 3<<EOF
stdout
-v1*=removing 'aa-addons' from 'b|all|${FAKEARCHITECTURE}'...
-d1*=db: 'aa-addons' removed from packages.db(b|all|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/b/all/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
EOF
checklog logab <<EOF
DATESTR remove b deb all ${FAKEARCHITECTURE} aa-addons 1-3
EOF
testout "" --keepunreferenced --dbdir ./db2 dumppull
cat > results.expected <<EOF
Updates needed for 'b|all|${FAKEARCHITECTURE}':
keep 'aa' '1-3' '1-3'
add 'aa-addons' - '1-3' 'froma'
keep 'ab' '3-1' unavailable
EOF
dodiff results.expected results
if $tracking ; then
testrun - -b . --delete removealltracks a 3<<EOF
stdout
-v0*=Deleting all tracks for a...
EOF
testout "" -b . dumptracks
dodiff results.empty results
fi
testout "" -b . dumpreferences
cat > results.expected <<EOF
a|all|${FAKEARCHITECTURE} pool/all/a/aa/aa-addons_1-3_all.deb
b|all|${FAKEARCHITECTURE} pool/all/a/aa/aa-addons_1-3_all.deb
a|all|source pool/all/a/aa/aa_1-3.dsc
a|all|source pool/all/a/aa/aa_1-3.tar.gz
a|all|${FAKEARCHITECTURE} pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
b|all|${FAKEARCHITECTURE} pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
a|all|${FAKEARCHITECTURE} pool/all/a/ab/ab-addons_3-1_all.deb
a|all|source pool/all/a/ab/ab_3-1.dsc
a|all|source pool/all/a/ab/ab_3-1.tar.gz
a|all|${FAKEARCHITECTURE} pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
b|all|${FAKEARCHITECTURE} pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
EOF
dodiff results.expected results
cat > conf/distributions <<EOF
Codename: X
Architectures: none
Components: test
EOF
checknolog logab
if $tracking ; then
testrun - -b . --delete clearvanished 3<<EOF
-v4*=Strange, 'X|test|none' does not appear in packages.db yet.
stdout
*=Deleting vanished identifier 'a|all|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'a|all|source'.
*=Deleting vanished identifier 'b|all|${FAKEARCHITECTURE}'.
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-3_all.deb
-d1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.dsc
-d1*=db: 'pool/all/a/aa/aa_1-3.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.tar.gz
-d1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v2*=removed now empty directory ./pool/all/a/aa
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_3-1_all.deb
-d1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.tar.gz
-d1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v2*=removed now empty directory ./pool/all/a/ab
-v2*=removed now empty directory ./pool/all/a
-v2*=removed now empty directory ./pool/all
-v2*=removed now empty directory ./pool
EOF
else
testrun - -b . --delete clearvanished 3<<EOF
# -v4*=Strange, 'X|test|none' does not appear in packages.db yet.
stdout
*=Deleting vanished identifier 'a|all|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'a|all|source'.
*=Deleting vanished identifier 'b|all|${FAKEARCHITECTURE}'.
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-3_all.deb
-d1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.dsc
-d1*=db: 'pool/all/a/aa/aa_1-3.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.tar.gz
-d1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v2*=removed now empty directory ./pool/all/a/aa
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_3-1_all.deb
-d1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.tar.gz
-d1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v2*=removed now empty directory ./pool/all/a/ab
-v2*=removed now empty directory ./pool/all/a
-v2*=removed now empty directory ./pool/all
-v2*=removed now empty directory ./pool
EOF
fi
checknolog logab
testout "" -b . dumptracks
dodiff results.empty results
testout "" -b . dumpunreferenced
dodiff results.empty results
rm -r dists
done
rm -r db db2 conf lists logs
rm aa* ab* ac* results.log.expected results.expected results
testsuccess
