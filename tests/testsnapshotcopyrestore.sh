#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
	source $(dirname $0)/test.inc
fi

mkdir -p conf
cat > conf/distributions <<CONFEND
Codename: A
Architectures: ${FAKEARCHITECTURE} calculator
Components: dog cat
Log: logfile
 -A=nonexistant -C=nocomponent --type=none --withcontrol noscript.sh

Codename: B
Architectures: ${FAKEARCHITECTURE} source
Components: dog cat
Contents:
Log: logfile
CONFEND
mkdir logs

testrun - -b . export 3<<EOF
stdout
-v2*=Created directory "./db"
-v1*=Exporting B...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/B"
-v2*=Created directory "./dists/B/dog"
-v2*=Created directory "./dists/B/dog/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/B/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/B/dog/source"
-v6*= exporting 'B|dog|source'...
-v6*=  creating './dists/B/dog/source/Sources' (gzipped)
-v2*=Created directory "./dists/B/cat"
-v2*=Created directory "./dists/B/cat/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'B|cat|${FAKEARCHITECTURE}'...
-v2*=Created directory "./dists/B/cat/source"
-v6*=  creating './dists/B/cat/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= exporting 'B|cat|source'...
-v6*=  creating './dists/B/cat/source/Sources' (gzipped)
-v1*= generating Contents-${FAKEARCHITECTURE}...
-v1*=Exporting A...
-v2*=Created directory "./dists/A"
-v2*=Created directory "./dists/A/dog"
-v2*=Created directory "./dists/A/dog/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'A|dog|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/A/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/dog/binary-calculator"
-v6*= exporting 'A|dog|calculator'...
-v6*=  creating './dists/A/dog/binary-calculator/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/cat"
-v2*=Created directory "./dists/A/cat/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'A|cat|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/A/cat/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/cat/binary-calculator"
-v6*= exporting 'A|cat|calculator'...
-v6*=  creating './dists/A/cat/binary-calculator/Packages' (uncompressed,gzipped)
EOF
dodo test -f db/files.db
dodo test -f db/checksums.db
if test -n "$TESTNEWFILESDB" ; then
	dodo rm db/files.db
fi

touch importindex

testrun - -b . _addpackage B importindex bar foo 3<<EOF
returns 255
stderr
*=_addpackage needs -C and -A and -T set!
-v0*=There have been errors!
EOF

testrun - -b . -A source -T dsc _addpackage B importindex bar foo 3<<EOF
returns 255
stderr
*=_addpackage needs -C and -A and -T set!
-v0*=There have been errors!
EOF

testrun - -b . -A ${FAKEARCHITECTURE} -C dog _addpackage B importindex bar foo 3<<EOF
returns 255
stderr
*=_addpackage needs -C and -A and -T set!
-v0*=There have been errors!
EOF

testrun - -b . -T deb -C dog _addpackage B importindex bar foo 3<<EOF
returns 255
stderr
*=_addpackage needs -C and -A and -T set!
-v0*=There have been errors!
EOF

testrun - -b . -T deb -A ${FAKEARCHITECTURE} -C dog _addpackage B importindex bar foo 3<<EOF
stderr
stdout
EOF

cat > importindex <<EOF
Test:
EOF

testrun - -b . -T deb -A ${FAKEARCHITECTURE} -C dog _addpackage B importindex bar foo 3<<EOF
returns 249
stderr
*=Error parsing importindex line 1: Chunk without 'Package:' field!
-v0*=There have been errors!
stdout
EOF

cat > importindex <<EOF
Package: another
Version: 0
Architecture: ${FAKEARCHITECTURE}
MD5Sum: 0
Size: 0
Filename: none
EOF

testrun - -b . -T deb -A ${FAKEARCHITECTURE} -C dog _addpackage B importindex bar foo 3<<EOF
stderr
stdout
EOF

cat > importindex <<EOF
Package: foo
Version: 0
Architecture: ${FAKEARCHITECTURE}
EOF

testrun - -b . -T deb -A ${FAKEARCHITECTURE} -C dog _addpackage B importindex bar foo 3<<EOF
returns 255
stderr
*=Data does not look like binary control: 'Package: foo
*=Version: 0
*=Architecture: ${FAKEARCHITECTURE}'
-v0*=There have been errors!
stdout
EOF

cat > importindex <<EOF
Package: foo
Version: 0
Architecture: ${FAKEARCHITECTURE}
MD5sum: 0
Size: 0
Filename: none
EOF

testrun - -b . -T deb -A ${FAKEARCHITECTURE} -C dog _addpackage B importindex bar foo 3<<EOF
returns 249
stderr
*=Error: cannot yet deal with files changing their position
*=(pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb vs none in foo version 0)
-v0*=There have been errors!
stdout
EOF

mkdir -p pool/dog/f/foo
echo "some data" > pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb

cat > importindex <<EOF
Package: foo
Version: 0
Architecture: ${FAKEARCHITECTURE}
MD5sum: $(md5 pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb)
Size: $(stat -c "%s" pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb)
Filename: pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
EOF

testrun - -b . -T deb -A ${FAKEARCHITECTURE} -C dog _addpackage B importindex bar foo 3<<EOF
returns 249
stderr
*=Error: package foo version 0 lists file pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb not yet in the pool!
-v0*=There have been errors!
stdout
EOF

testrun empty -b . dumpunreferenced

testrun - -b . _detect pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb 3<<EOF
stderr
stdout
-d1*=db: 'pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
EOF

testrun - -b . dumpunreferenced 3<<EOF
stderr
stdout
*=pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
EOF

# TODO: why is there no error for faulty .deb here?

testrun - -b . -T deb -A ${FAKEARCHITECTURE} -C dog _addpackage B importindex bar foo 3<<EOF
stderr
stdout
-v1*=Adding 'foo' '0' to 'B|dog|${FAKEARCHITECTURE}'.
-d1*=db: 'foo' added to packages.db(B|dog|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/B/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-${FAKEARCHITECTURE}...
EOF

testrun empty -b . dumpunreferenced

echo "dsc-content" > pool/dog/f/foo/foo_1.dsc
echo "tar-content" > pool/dog/f/foo/foo_1.tar.gz

cat > importindex <<EOF
Package: foo
Version: 1
Directory: pool/dog/f/foo
Files:
 $(mdandsize pool/dog/f/foo/foo_1.dsc) foo_1.dsc
 $(mdandsize pool/dog/f/foo/foo_1.tar.gz) foo_1.tar.gz
EOF

testrun - -b . -T dsc -C dog _addpackage B importindex bar foo 3<<EOF
returns 249
stderr
*=Error: package foo version 1 lists file pool/dog/f/foo/foo_1.dsc not yet in the pool!
-v0*=There have been errors!
stdout
EOF

testrun empty -b . dumpunreferenced

testrun - -b . _detect pool/dog/f/foo/foo_1.dsc 3<<EOF
stderr
stdout
-d1*=db: 'pool/dog/f/foo/foo_1.dsc' added to checksums.db(pool).
-e1*=db: 'pool/dog/f/foo/foo_1.dsc' added to files.db(md5sums).
EOF

testrun - -b . -T dsc -C dog _addpackage B importindex bar foo 3<<EOF
returns 249
stderr
*=Error: package foo version 1 lists file pool/dog/f/foo/foo_1.tar.gz not yet in the pool!
-v0*=There have been errors!
stdout
EOF

testrun - -b . _detect pool/dog/f/foo/foo_1.tar.gz 3<<EOF
stderr
stdout
-d1*=db: 'pool/dog/f/foo/foo_1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/dog/f/foo/foo_1.tar.gz' added to files.db(md5sums).
EOF

testrun - -b . dumpunreferenced 3<<EOF
stderr
stdout
*=pool/dog/f/foo/foo_1.dsc
*=pool/dog/f/foo/foo_1.tar.gz
EOF

testrun - -b . -T dsc -C dog _addpackage B importindex bar foo 3<<EOF
stderr
stdout
-v1*=Adding 'foo' '1' to 'B|dog|source'.
-d1*=db: 'foo' added to packages.db(B|dog|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
EOF

testrun empty -b . dumpunreferenced

testrun - -b . gensnapshot B now 3<<EOF
stdout
-v2*=Created directory "./dists/B/snapshots"
-v2*=Created directory "./dists/B/snapshots/now"
-v2*=Created directory "./dists/B/snapshots/now/dog"
-v2*=Created directory "./dists/B/snapshots/now/dog/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/B/snapshots/now/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/B/snapshots/now/dog/source"
-v6*= exporting 'B|dog|source'...
-v6*=  creating './dists/B/snapshots/now/dog/source/Sources' (gzipped)
-v2*=Created directory "./dists/B/snapshots/now/cat"
-v2*=Created directory "./dists/B/snapshots/now/cat/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'B|cat|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/B/snapshots/now/cat/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/B/snapshots/now/cat/source"
-v6*= exporting 'B|cat|source'...
-v6*=  creating './dists/B/snapshots/now/cat/source/Sources' (gzipped)
EOF

testrun - -b . dumpreferences 3<<EOF
stdout
*=B|dog|${FAKEARCHITECTURE} pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
*=s=B=now pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
*=B|dog|source pool/dog/f/foo/foo_1.dsc
*=s=B=now pool/dog/f/foo/foo_1.dsc
*=B|dog|source pool/dog/f/foo/foo_1.tar.gz
*=s=B=now pool/dog/f/foo/foo_1.tar.gz
EOF

testrun - -b . gensnapshot A now 3<<EOF
stdout
-v2*=Created directory "./dists/A/snapshots"
-v2*=Created directory "./dists/A/snapshots/now"
-v2*=Created directory "./dists/A/snapshots/now/dog"
-v2*=Created directory "./dists/A/snapshots/now/dog/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'A|dog|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/A/snapshots/now/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/snapshots/now/dog/binary-calculator"
-v6*= exporting 'A|dog|calculator'...
-v6*=  creating './dists/A/snapshots/now/dog/binary-calculator/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/snapshots/now/cat"
-v2*=Created directory "./dists/A/snapshots/now/cat/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'A|cat|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/A/snapshots/now/cat/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/snapshots/now/cat/binary-calculator"
-v6*= exporting 'A|cat|calculator'...
-v6*=  creating './dists/A/snapshots/now/cat/binary-calculator/Packages' (uncompressed,gzipped)
EOF

testout "" -b . dumpreferences
grep '^.|' results | sed -e 's/|[^ ]* / contains /' | uniq > references.normal
grep '^s=' results | sed -e 's/^s=\(.\)=[^ ]* /\1 contains /' > references.snapshot
dodiff -u references.normal references.snapshot
rm references.normal references.snapshot
# Remove contents from original, to make them more look alike:
for n in dists/B/Release dists/B/snapshots/now/Release dists/A/Release dists/A/snapshots/now/Release ; do
	ed -s $n <<EOF
g/^Date: /s/ .*/ unified/
g,^Suite: ./snapshots/now$,d
w
q
EOF
done
mkdir tmp
mv dists/B/Contents-${FAKEARCHITECTURE}.gz tmp
mv dists/B/snapshots/now dists/B.snapshot
mv dists/A/snapshots/now dists/A.snapshot
printf 'g/Contents-/d\nw\nq\n' | ed -s dists/B/Release
rmdir dists/B/snapshots
rmdir dists/A/snapshots
dodiff -r -u dists/B.snapshot dists/B
dodiff -r -u dists/A.snapshot dists/A
mkdir rmdir dists/B/snapshots
mkdir dists/A/snapshots
mv dists/B.snapshot dists/B/snapshots/now
mv dists/A.snapshot dists/A/snapshots/now
mv tmp/Contents-${FAKEARCHITECTURE}.gz dists/B/

testrun empty -b . dumpunreferenced

testrun - -b . restore B before foo 3<<EOF
stderr
*=Could not find './dists/B/snapshots/before/dog/binary-${FAKEARCHITECTURE}/Packages' nor './dists/B/snapshots/before/dog/binary-${FAKEARCHITECTURE}/Packages.gz',
*=ignoring that part of the snapshot.
*=Could not find './dists/B/snapshots/before/dog/source/Sources' nor './dists/B/snapshots/before/dog/source/Sources.gz',
*=Could not find './dists/B/snapshots/before/cat/binary-${FAKEARCHITECTURE}/Packages' nor './dists/B/snapshots/before/cat/binary-${FAKEARCHITECTURE}/Packages.gz',
*=Could not find './dists/B/snapshots/before/cat/source/Sources' nor './dists/B/snapshots/before/cat/source/Sources.gz',
stdout
EOF

testrun - -b . dumpreferences 3<<EOF
stdout
*=B|dog|${FAKEARCHITECTURE} pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
*=s=B=now pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
*=B|dog|source pool/dog/f/foo/foo_1.dsc
*=s=B=now pool/dog/f/foo/foo_1.dsc
*=B|dog|source pool/dog/f/foo/foo_1.tar.gz
*=s=B=now pool/dog/f/foo/foo_1.tar.gz
EOF

testrun - -b . restore B now foo 3<<EOF
stderr
*=Warning: downgrading 'foo' from '0' to '0' in 'B|dog|${FAKEARCHITECTURE}'!
*=Warning: downgrading 'foo' from '1' to '1' in 'B|dog|source'!
stdout
-v1*=Adding 'foo' '0' to 'B|dog|${FAKEARCHITECTURE}'.
-d1*=db: 'foo' removed from packages.db(B|dog|${FAKEARCHITECTURE}).
-d1*=db: 'foo' added to packages.db(B|dog|${FAKEARCHITECTURE}).
-v1*=Adding 'foo' '1' to 'B|dog|source'.
-d1*=db: 'foo' removed from packages.db(B|dog|source).
-d1*=db: 'foo' added to packages.db(B|dog|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/B/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-${FAKEARCHITECTURE}...
EOF

testrun empty -b . dumpunreferenced
testrun - -b . dumpreferences 3<<EOF
stdout
*=B|dog|${FAKEARCHITECTURE} pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
*=s=B=now pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
*=B|dog|source pool/dog/f/foo/foo_1.dsc
*=s=B=now pool/dog/f/foo/foo_1.dsc
*=B|dog|source pool/dog/f/foo/foo_1.tar.gz
*=s=B=now pool/dog/f/foo/foo_1.tar.gz
EOF

testrun - -b . restoresrc B now foo 0 1 3<<EOF
stderr
*=Warning: downgrading 'foo' from '0' to '0' in 'B|dog|${FAKEARCHITECTURE}'!
*=Warning: downgrading 'foo' from '1' to '1' in 'B|dog|source'!
stdout
-v1*=Adding 'foo' '0' to 'B|dog|${FAKEARCHITECTURE}'.
-d1*=db: 'foo' removed from packages.db(B|dog|${FAKEARCHITECTURE}).
-d1*=db: 'foo' added to packages.db(B|dog|${FAKEARCHITECTURE}).
-v1*=Adding 'foo' '1' to 'B|dog|source'.
-d1*=db: 'foo' removed from packages.db(B|dog|source).
-d1*=db: 'foo' added to packages.db(B|dog|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/B/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-${FAKEARCHITECTURE}...
EOF

testrun - -b . restoresrc B now foo 0 3<<EOF
stderr
*=Warning: downgrading 'foo' from '0' to '0' in 'B|dog|${FAKEARCHITECTURE}'!
stdout
-v1*=Adding 'foo' '0' to 'B|dog|${FAKEARCHITECTURE}'.
-d1*=db: 'foo' removed from packages.db(B|dog|${FAKEARCHITECTURE}).
-d1*=db: 'foo' added to packages.db(B|dog|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/B/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-${FAKEARCHITECTURE}...
EOF

testrun - -b . restoresrc B now foo 1 3<<EOF
stderr
*=Warning: downgrading 'foo' from '1' to '1' in 'B|dog|source'!
stdout
-v1*=Adding 'foo' '1' to 'B|dog|source'.
-d1*=db: 'foo' removed from packages.db(B|dog|source).
-d1*=db: 'foo' added to packages.db(B|dog|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
EOF
testrun - -b . restorefilter B now 'Directory' 3<<EOF
stderr
*=Warning: downgrading 'foo' from '1' to '1' in 'B|dog|source'!
stdout
-v1*=Adding 'foo' '1' to 'B|dog|source'.
-d1*=db: 'foo' removed from packages.db(B|dog|source).
-d1*=db: 'foo' added to packages.db(B|dog|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
EOF

testrun - -b . remove B bar foo 3<<EOF
stderr
-v0*=Not removed as not found: bar
stdout
-v1*=removing 'foo' from 'B|dog|abacus'...
-d1*=db: 'foo' removed from packages.db(B|dog|${FAKEARCHITECTURE}).
-v1*=removing 'foo' from 'B|dog|source'...
-d1*=db: 'foo' removed from packages.db(B|dog|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/B/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-${FAKEARCHITECTURE}...
-v0*=Deleting files no longer referenced...
EOF

testrun - -b . dumpreferences 3<<EOF
stdout
*=s=B=now pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb
*=s=B=now pool/dog/f/foo/foo_1.dsc
*=s=B=now pool/dog/f/foo/foo_1.tar.gz
EOF

testrun empty -b . dumpunreferenced

dodo test -f pool/dog/f/foo/foo_1.dsc

testrun - -b . restore B now bar foo 3<<EOF
stderr
stdout
-v1*=Adding 'foo' '0' to 'B|dog|${FAKEARCHITECTURE}'.
-d1*=db: 'foo' added to packages.db(B|dog|${FAKEARCHITECTURE}).
-v1*=Adding 'foo' '1' to 'B|dog|source'.
-d1*=db: 'foo' added to packages.db(B|dog|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/B/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-${FAKEARCHITECTURE}...
EOF

testrun empty -b . _removereferences s=B=now

testrun empty -b . dumpunreferenced

testrun - -b . remove B bar foo 3<<EOF
stderr
-v0*=Not removed as not found: bar
stdout
-v1*=removing 'foo' from 'B|dog|abacus'...
-d1*=db: 'foo' removed from packages.db(B|dog|${FAKEARCHITECTURE}).
-v1*=removing 'foo' from 'B|dog|source'...
-d1*=db: 'foo' removed from packages.db(B|dog|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|abacus'...
-v6*=  replacing './dists/B/dog/binary-abacus/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|abacus'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-abacus...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/dog/f/foo/foo_0_abacus.deb
-d1*=db: 'pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-e1*=db: 'pool/dog/f/foo/foo_0_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-v1*=deleting and forgetting pool/dog/f/foo/foo_1.dsc
-d1*=db: 'pool/dog/f/foo/foo_1.dsc' removed from checksums.db(pool).
-e1*=db: 'pool/dog/f/foo/foo_1.dsc' removed from files.db(md5sums).
-v1*=deleting and forgetting pool/dog/f/foo/foo_1.tar.gz
-v1*=removed now empty directory ./pool/dog/f/foo
-v1*=removed now empty directory ./pool/dog/f
-v1*=removed now empty directory ./pool/dog
-v1*=removed now empty directory ./pool
-d1*=db: 'pool/dog/f/foo/foo_1.tar.gz' removed from checksums.db(pool).
-e1*=db: 'pool/dog/f/foo/foo_1.tar.gz' removed from files.db(md5sums).
EOF

testrun empty -b . dumpunreferenced
testrun empty -b . dumpreferences

rm -r conf db dists importindex logs
testsuccess
