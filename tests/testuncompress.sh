#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
	source $(dirname $0)/test.inc
fi

dodo test ! -d db
dodo test ! -d conf
dodo test ! -d pool

# First test if finding the binaries works properly...

testrun - __dumpuncompressors 3<<EOF
stdout
*=.gz: built-in + '/bin/gunzip'
*=.bz2: built-in + '/bin/bunzip2'
*=.lzma: '/usr/bin/unlzma'
EOF

testrun - --gunzip=NONE --bunzip2=NONE --unlzma=NONE __dumpuncompressors 3<<EOF
stdout
*=.gz: built-in
*=.bz2: built-in
*=.lzma: not supported (install lzma or use --unlzma to tell where unlzma is).
EOF

testrun - --gunzip=false --bunzip2=false --unlzma=false __dumpuncompressors 3<<EOF
stdout
*=.gz: built-in + '/bin/false'
*=.bz2: built-in + '/bin/false'
*=.lzma: '/bin/false'
EOF

touch fakeg fakeb fakel

testrun - --gunzip=./fakeg --bunzip2=./fakeb --unlzma=./fakel __dumpuncompressors 3<<EOF
stdout
*=.gz: built-in
*=.bz2: built-in
*=.lzma: not supported (install lzma or use --unlzma to tell where unlzma is).
EOF

chmod u+x fakeg fakeb fakel

testrun - --gunzip=./fakeg --bunzip2=./fakeb --unlzma=./fakel __dumpuncompressors 3<<EOF
stdout
*=.gz: built-in + './fakeg'
*=.bz2: built-in + './fakeb'
*=.lzma: './fakel'
EOF

rm fakeg fakeb fakel

# Then test the builtin formats and the external one...

echo "start" > testfile
dd if=/dev/zero bs=1024 count=1024 >> testfile
echo "" >> testfile
echo "middle" >> testfile
dd if=/dev/zero bs=1024 count=1024 >> testfile
echo "" >> testfile
echo "end" >> testfile

echo "Ohm" > smallfile

echo gzip -c testfile \> testfile.gz
gzip -c testfile > testfile.gz
echo bzip2 -c testfile \> testfile.bz2
bzip2 -c testfile > testfile.bz2
echo lzma -c testfile \> testfile.lzma
lzma -c testfile > testfile.lzma
echo gzip -c smallfile \> smallfile.gz
gzip -c smallfile > smallfile.gz
echo bzip2 -c smallfile \> smallfile.bz2
bzip2 -c smallfile > smallfile.bz2
echo lzma -c smallfile \> smallfile.lzma
lzma -c smallfile > smallfile.lzma

testrun - __uncompress .gz testfile.gz testfile.gz.uncompressed 3<<EOF
-v1*=Uncompress 'testfile.gz' into 'testfile.gz.uncompressed'...
EOF
dodiff testfile testfile.gz.uncompressed
rm *.uncompressed
testrun - __uncompress .bz2 testfile.bz2 testfile.bz2.uncompressed 3<<EOF
-v1*=Uncompress 'testfile.bz2' into 'testfile.bz2.uncompressed'...
EOF
dodiff testfile testfile.bz2.uncompressed
rm *.uncompressed
testrun - __uncompress .lzma testfile.lzma testfile.lzma.uncompressed 3<<EOF
-v1*=Uncompress 'testfile.lzma' into 'testfile.lzma.uncompressed' using '/usr/bin/unlzma'...
EOF
dodiff testfile testfile.lzma.uncompressed
rm *.uncompressed
testrun - __uncompress .gz smallfile.gz smallfile.gz.uncompressed 3<<EOF
-v1*=Uncompress 'smallfile.gz' into 'smallfile.gz.uncompressed'...
EOF
dodiff smallfile smallfile.gz.uncompressed
rm *.uncompressed
testrun - __uncompress .bz2 smallfile.bz2 smallfile.bz2.uncompressed 3<<EOF
-v1*=Uncompress 'smallfile.bz2' into 'smallfile.bz2.uncompressed'...
EOF
dodiff smallfile smallfile.bz2.uncompressed
rm *.uncompressed
testrun - __uncompress .lzma smallfile.lzma smallfile.lzma.uncompressed 3<<EOF
-v1*=Uncompress 'smallfile.lzma' into 'smallfile.lzma.uncompressed' using '/usr/bin/unlzma'...
EOF
dodiff smallfile smallfile.lzma.uncompressed
rm *.uncompressed

testrun - --unlzma=false __uncompress .lzma smallfile.lzma smallfile.lzma.uncompressed 3<<EOF
-v1*=Uncompress 'smallfile.lzma' into 'smallfile.lzma.uncompressed' using '/bin/false'...
*='/bin/false' < smallfile.lzma > smallfile.lzma.uncompressed exited with errorcode 1!
-v0*=There have been errors!
returns 255
EOF
dodo test ! -e smallfile.lzma.uncompressed


# Now check for compressed parts of an .a file:

cat > control <<EOF
Package: fake
Version: fake
Architecture: all
EOF

# looks like control.tar.lzma is not possible because the name is too
# long for the old ar format dpkg-deb needs...
echo tar -cf - ./control \| bzip2 \> control.tar.bz2
tar -cf - ./control | bzip2 > control.tar.bz2
rm control
echo tar -cf - testfile\* \| lzma \> data.tar.lzma
tar -cf - testfile* | lzma > data.tar.lzma
echo 2.0 > debian-binary
dodo ar qcfS fake.deb debian-binary control.tar.bz2 data.tar.lzma
rm *.tar.bz2 *.tar.lzma debian-binary

# TODO: there could be a problem here with .deb files that have data after the
# ./control file in data.tar and using an external uncompressor.
# But how to test this when there is no way to trigger it in the default built?

testrun - __extractcontrol fake.deb 3<<EOF
stdout
*=Package: fake
*=Version: fake
*=Architecture: all
*=
EOF
testrun - __extractfilelist fake.deb 3<<EOF
stdout
*=/testfile
*=/testfile.bz2
*=/testfile.gz
*=/testfile.lzma
EOF

rm fake.deb

# Now check extracting Section/Priority from an .dsc
mkdir debian
cat > debian/control <<EOF
Package: fake
Maintainer: Me
Section: admin
Priority: extra

Package: abinary
Architecture: all
EOF
echo generating fake dirs
for n in $(seq 100000) ; do echo "/$n"  ; done > debian/dirs
dd if=/dev/zero of=debian/zzz bs=1024 count=4096
tar -cf - debian | lzma > fake_1-1.debian.tar.lzma
mkdir fake-1
mkdir fake-1.orig
cp -al debian fake-1/debian
cp -al debian fake-1.orig/debian
sed -e 's/1/2/' fake-1/debian/dirs > fake-1/debian.dirs.new
mv fake-1/debian.dirs.new fake-1/debian/dirs
diff -ruN fake-1.orig fake-1 | lzma > fake_1-1.diff.lzma
rm -r debian

# .debian.tar and .diff usually do not happen at the same time, but easier testing...
cat > fake_1-1.dsc << EOF
Format: 3.0
Source: fake
Binary: abinary
Architecture: all
Version: 17
Maintainer: Me
Files:
 $(mdandsize fake_1-1.diff.lzma) fake_1-1.diff.lzma
 $(mdandsize fake_1-1.debian.tar.lzma) fake_1-1.debian.tar.lzma
 00000000000000000000000000000000 0 fake_1.orig.tar.lzma
EOF

testrun - __extractsourcesection fake_1-1.dsc 3<<EOF
=Data seems not to be signed trying to use directly...
stdout
*=Section: admin
*=Priority: extra
EOF

# now check only partial reading of the .diff:
rm fake-1/debian/control
cat > fake-1/debian/control <<EOF
Package: fake
Maintainer: MeToo
Section: base
Priority: required

Package: abinary
Architecture: all
EOF
diff -ruN fake-1.orig fake-1 | lzma > fake_1-1.diff.lzma
rm -r fake-1 fake-1.orig


cat > fake_1-1.dsc << EOF
Format: 3.0
Source: fake
Binary: abinary
Architecture: all
Version: 17
Maintainer: Me
Files:
 $(mdandsize fake_1-1.diff.lzma) fake_1-1.diff.lzma
 $(mdandsize fake_1-1.debian.tar.lzma) fake_1-1.debian.tar.lzma
 00000000000000000000000000000000 0 fake_1.orig.tar.lzma
EOF

testrun - __extractsourcesection fake_1-1.dsc 3<<EOF
=Data seems not to be signed trying to use directly...
stdout
*=Section: base
*=Priority: required
EOF

rm testfile* smallfile*
# TODO: test error messages of truncated files and files with errors in the
# compressed stream...
testsuccess
