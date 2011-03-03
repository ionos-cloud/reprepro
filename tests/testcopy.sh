#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
        source $(dirname $0)/test.inc
fi

dodo test ! -e dists
mkdir conf db logs lists

cat >> conf/distributions <<EOF
Codename: a
Architectures: ${FAKEARCHITECTURE} source
Components: one two three

Codename: b
Architectures: ${FAKEARCHITECTURE}
Components: one two four
EOF

DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-1" FAKEVER="4-2" SECTION="one" genpackage.sh

testrun - -b . --export=never --delete --delete include a test.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: database 'a|one|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'a|one|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
stdout
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/one"
-v2*=Created directory "./pool/one/a"
-v2*=Created directory "./pool/one/a/aa"
-d1*=db: 'pool/one/a/aa/aa-addons_4-2_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/one/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/one/a/aa/aa_1-1.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/one/a/aa/aa_1-1.dsc' added to checksums.db(pool).
-d1*=db: 'aa-addons' added to packages.db(a|one|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(a|one|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(a|one|source).
-t1*=db: 'aa' added to tracking.db(a).
-v5*=Deleting 'test.changes'.
EOF

DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-2" FAKEVER="3-2" SECTION="two" genpackage.sh
testrun - -b . --export=never --delete --delete include a test.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: database 'a|two|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'a|two|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
stdout
-v2*=Created directory "./pool/two"
-v2*=Created directory "./pool/two/a"
-v2*=Created directory "./pool/two/a/aa"
-d1*=db: 'pool/two/a/aa/aa-addons_3-2_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/two/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/two/a/aa/aa_1-2.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/two/a/aa/aa_1-2.dsc' added to checksums.db(pool).
-d1*=db: 'aa-addons' added to packages.db(a|two|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(a|two|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(a|two|source).
-t1*=db: 'aa' added to tracking.db(a).
-v5*=Deleting 'test.changes'.
EOF

testrun - -b . ls aa 3<<EOF
stdout
*=aa | 1-1 | a | ${FAKEARCHITECTURE}, source
*=aa | 1-2 | a | ${FAKEARCHITECTURE}, source
returns 0
EOF
testrun - -b . ls aa-addons 3<<EOF
stdout
*=aa-addons | 4-2 | a | ${FAKEARCHITECTURE}
*=aa-addons | 3-2 | a | ${FAKEARCHITECTURE}
returns 0
EOF

testrun - -b . list a 3<<EOF
stdout
*=a|one|${FAKEARCHITECTURE}: aa 1-1
*=a|one|${FAKEARCHITECTURE}: aa-addons 4-2
*=a|one|source: aa 1-1
*=a|two|${FAKEARCHITECTURE}: aa 1-2
*=a|two|${FAKEARCHITECTURE}: aa-addons 3-2
*=a|two|source: aa 1-2
returns 0
EOF

testrun - -b . --export=never copy b a bb cc 3<<EOF
stderr
-v0*=Will not copy as not found: bb, cc.
stdout
-v3*=Not looking into 'a|one|source' as no matching target in 'b'!
-v3*=Not looking into 'a|two|source' as no matching target in 'b'!
-v3*=Not looking into 'a|three|${FAKEARCHITECTURE}' as no matching target in 'b'!
-v3*=Not looking into 'a|three|source' as no matching target in 'b'!
EOF


testrun - -b . --export=never copy b a aa-addons 3<<EOF
stdout
-v3*=Not looking into 'a|one|source' as no matching target in 'b'!
-v3*=Not looking into 'a|two|source' as no matching target in 'b'!
-v3*=Not looking into 'a|three|${FAKEARCHITECTURE}' as no matching target in 'b'!
-v3*=Not looking into 'a|three|source' as no matching target in 'b'!
-v1*=Adding 'aa-addons' '4-2' to 'b|one|${FAKEARCHITECTURE}'.
-d1*=db: 'aa-addons' added to packages.db(b|one|${FAKEARCHITECTURE}).
-v1*=Adding 'aa-addons' '3-2' to 'b|two|${FAKEARCHITECTURE}'.
-d1*=db: 'aa-addons' added to packages.db(b|two|${FAKEARCHITECTURE}).
stderr
*=Warning: database 'b|one|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'b|two|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF

testrun - -b . list b 3<<EOF
stdout
*=b|one|${FAKEARCHITECTURE}: aa-addons 4-2
*=b|two|${FAKEARCHITECTURE}: aa-addons 3-2
returns 0
EOF

testrun - -b . ls aa 3<<EOF
stdout
*=aa | 1-1 | a | ${FAKEARCHITECTURE}, source
*=aa | 1-2 | a | ${FAKEARCHITECTURE}, source
returns 0
EOF
testrun - -b . ls aa-addons 3<<EOF
stdout
*=aa-addons | 4-2 | a | ${FAKEARCHITECTURE}
*=aa-addons | 3-2 | a | ${FAKEARCHITECTURE}
*=aa-addons | 4-2 | b | ${FAKEARCHITECTURE}
*=aa-addons | 3-2 | b | ${FAKEARCHITECTURE}
returns 0
EOF

testrun - -b . --export=never remove b aa-addons 3<<EOF
stdout
-v1*=removing 'aa-addons' from 'b|one|${FAKEARCHITECTURE}'...
-d1*=db: 'aa-addons' removed from packages.db(b|one|${FAKEARCHITECTURE}).
-v1*=removing 'aa-addons' from 'b|two|${FAKEARCHITECTURE}'...
-d1*=db: 'aa-addons' removed from packages.db(b|two|${FAKEARCHITECTURE}).
stderr
*=Warning: database 'b|one|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'b|two|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF

testrun - -b . ls aa-addons 3<<EOF
stdout
*=aa-addons | 4-2 | a | ${FAKEARCHITECTURE}
*=aa-addons | 3-2 | a | ${FAKEARCHITECTURE}
returns 0
EOF

testrun - -b . --export=never copysrc b a aa-addons 3<<EOF
stdout
-v3*=Not looking into 'a|one|source' as no matching target in 'b'!
-v3*=Not looking into 'a|two|source' as no matching target in 'b'!
-v3*=Not looking into 'a|three|${FAKEARCHITECTURE}' as no matching target in 'b'!
-v3*=Not looking into 'a|three|source' as no matching target in 'b'!
stderr
-v0*=Nothing to do as no package with source 'aa-addons' found!
EOF

testrun - -b . --export=never copysrc b a aa 4-2 3-2 3<<EOF
stdout
-v3*=Not looking into 'a|one|source' as no matching target in 'b'!
-v3*=Not looking into 'a|two|source' as no matching target in 'b'!
-v3*=Not looking into 'a|three|${FAKEARCHITECTURE}' as no matching target in 'b'!
-v3*=Not looking into 'a|three|source' as no matching target in 'b'!
stderr
-v0*=Nothing to do as no packages with source 'aa' and a requested source version found!
EOF

testrun - -b . --export=never copysrc b a aa 1-1 2-2 3<<EOF
stdout
-v3*=Not looking into 'a|one|source' as no matching target in 'b'!
-v3*=Not looking into 'a|two|source' as no matching target in 'b'!
-v3*=Not looking into 'a|three|${FAKEARCHITECTURE}' as no matching target in 'b'!
-v3*=Not looking into 'a|three|source' as no matching target in 'b'!
-v1*=Adding 'aa-addons' '4-2' to 'b|one|${FAKEARCHITECTURE}'.
-d1*=db: 'aa-addons' added to packages.db(b|one|${FAKEARCHITECTURE}).
-v1*=Adding 'aa' '1-1' to 'b|one|${FAKEARCHITECTURE}'.
-d1*=db: 'aa' added to packages.db(b|one|${FAKEARCHITECTURE}).
stderr
-v0*=Will not copy as not found: 2-2.
-v6*=Found versions are: 1-1.
*=Warning: database 'b|one|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF

testrun - -b . --export=never copysrc b a aa 1-1 1-2 3<<EOF
stdout
-v3*=Not looking into 'a|one|source' as no matching target in 'b'!
-v3*=Not looking into 'a|two|source' as no matching target in 'b'!
-v3*=Not looking into 'a|three|${FAKEARCHITECTURE}' as no matching target in 'b'!
-v3*=Not looking into 'a|three|source' as no matching target in 'b'!
-v1*=Adding 'aa-addons' '4-2' to 'b|one|${FAKEARCHITECTURE}'.
-d1*=db: 'aa-addons' removed from packages.db(b|one|${FAKEARCHITECTURE}).
-d1*=db: 'aa-addons' added to packages.db(b|one|${FAKEARCHITECTURE}).
-v1*=Adding 'aa' '1-1' to 'b|one|${FAKEARCHITECTURE}'.
-d1*=db: 'aa' removed from packages.db(b|one|${FAKEARCHITECTURE}).
-d1*=db: 'aa' added to packages.db(b|one|${FAKEARCHITECTURE}).
-v1*=Adding 'aa-addons' '3-2' to 'b|two|${FAKEARCHITECTURE}'.
-d1*=db: 'aa-addons' added to packages.db(b|two|${FAKEARCHITECTURE}).
-v1*=Adding 'aa' '1-2' to 'b|two|${FAKEARCHITECTURE}'.
-d1*=db: 'aa' added to packages.db(b|two|${FAKEARCHITECTURE}).
stderr
-v6*=Found versions are: 1-1, 1-2.
*=Warning: downgrading 'aa-addons' from '4-2' to '4-2' in 'b|one|${FAKEARCHITECTURE}'!
*=Warning: downgrading 'aa' from '1-1' to '1-1' in 'b|one|${FAKEARCHITECTURE}'!
*=Warning: database 'b|one|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'b|two|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF

rm -r db conf pool logs lists
testsuccess
