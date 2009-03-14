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
-v2*=Uncompress 'testfile.gz' into 'testfile.gz.uncompressed'...
EOF
dodiff testfile testfile.gz.uncompressed
rm *.uncompressed
testrun - __uncompress .bz2 testfile.bz2 testfile.bz2.uncompressed 3<<EOF
-v2*=Uncompress 'testfile.bz2' into 'testfile.bz2.uncompressed'...
EOF
dodiff testfile testfile.bz2.uncompressed
rm *.uncompressed
testrun - __uncompress .lzma testfile.lzma testfile.lzma.uncompressed 3<<EOF
-v2*=Uncompress 'testfile.lzma' into 'testfile.lzma.uncompressed' using '/usr/bin/unlzma'...
EOF
dodiff testfile testfile.lzma.uncompressed
rm *.uncompressed
testrun - __uncompress .gz smallfile.gz smallfile.gz.uncompressed 3<<EOF
-v2*=Uncompress 'smallfile.gz' into 'smallfile.gz.uncompressed'...
EOF
dodiff smallfile smallfile.gz.uncompressed
rm *.uncompressed
testrun - __uncompress .bz2 smallfile.bz2 smallfile.bz2.uncompressed 3<<EOF
-v2*=Uncompress 'smallfile.bz2' into 'smallfile.bz2.uncompressed'...
EOF
dodiff smallfile smallfile.bz2.uncompressed
rm *.uncompressed
testrun - __uncompress .lzma smallfile.lzma smallfile.lzma.uncompressed 3<<EOF
-v2*=Uncompress 'smallfile.lzma' into 'smallfile.lzma.uncompressed' using '/usr/bin/unlzma'...
EOF
dodiff smallfile smallfile.lzma.uncompressed
rm *.uncompressed

testrun - --unlzma=false __uncompress .lzma smallfile.lzma smallfile.lzma.uncompressed 3<<EOF
-v2*=Uncompress 'smallfile.lzma' into 'smallfile.lzma.uncompressed' using '/bin/false'...
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

# It would be nice to damage the .lzma file here, but that has a problem:
# A random damage to the file will usually lead to some garbage output
# before lzma realizes the error.
# Once reprepro sees the garbage (which will usually not be a valid diff)
# it will decide it is a format it does not understand and abort further
# reading giving up.
# This is a race condition with one of the following results:
#   reprepro is much faster: no error output (as unknown format is no error,
#                                             but only no success)
#   reprepro a bit faster: unlzma can still output an error, but not
#                          is terminated by reprepro before issuing an error code.
#   unlzma is faster: reprepro will see an child returning with error...
#
# Thus we can only fake a damaged file by replacing the uncompressor:

testrun - --unlzma=brokenunlzma.sh __extractsourcesection fake_1-1.dsc 3<<EOF
returns 255
=Data seems not to be signed trying to use directly...
*=brokenunlzma.sh: claiming broken archive
*=Error reading from ./fake_1-1.diff.lzma: $SRCDIR/tests/brokenunlzma.sh exited with code 1!
-v0*=There have been errors!
stdout
EOF

mv fake_1-1.debian.tar.lzma save.tar.lzma

# a missing file is no error, but no success either...
testrun - __extractsourcesection fake_1-1.dsc 3<<EOF
=Data seems not to be signed trying to use directly...
stdout
EOF

cp save.tar.lzma fake_1.orig.tar.lzma
# a missing file is no error, but no success either (and not reading further files)
testrun - __extractsourcesection fake_1-1.dsc 3<<EOF
=Data seems not to be signed trying to use directly...
stdout
EOF

dodo mkdir debian
dodo touch debian/test
echo tar -cf - debian \| lzma \> fake_1-1.debian.tar.lzma
tar -cf - debian | lzma > fake_1-1.debian.tar.lzma
rm -r debian

testrun - __extractsourcesection fake_1-1.dsc 3<<EOF
=Data seems not to be signed trying to use directly...
stdout
*=Section: admin
*=Priority: extra
EOF

touch breakon2nd
testrun - --unlzma=brokenunlzma.sh __extractsourcesection fake_1-1.dsc 3<<EOF
returns 255
=Data seems not to be signed trying to use directly...
*=brokenunlzma.sh: claiming broken archive
*=Error reading from ./fake_1-1.debian.tar.lzma: $SRCDIR/tests/brokenunlzma.sh exited with code 1!
-v0*=There have been errors!
stdout
EOF

testrun - --unlzma=brokenunlzma.sh __extractsourcesection fake_1-1.dsc 3<<EOF
returns 255
=Data seems not to be signed trying to use directly...
*=brokenunlzma.sh: claiming broken archive
*=Error reading from ./fake_1-1.diff.lzma: $SRCDIR/tests/brokenunlzma.sh exited with code 1!
-v0*=There have been errors!
stdout
EOF


# sadly different output depending on libarchive version....
# dd if=/dev/zero of=fake_1-1.debian.tar.lzma bs=5 count=1
# 
# testrun - __extractsourcesection fake_1-1.dsc 3<<EOF
# returns 255
# =Data seems not to be signed trying to use directly...
# *=/usr/bin/unlzma: Read error
# *=Error 84 trying to extract control information from ./fake_1-1.debian.tar.lzma:
# *=Empty input file: Invalid or incomplete multibyte or wide character
# -v0*=There have been errors!
# stdout
# EOF

mv save.tar.lzma fake_1-1.debian.tar.lzma
rm fake_1.orig.tar.lzma

# now check only partial reading of the .diff
# (i.e. diff containing a control):
rm fake-1/debian/control
cat > fake-1/debian/control <<EOF
Package: fake
Maintainer: MeToo
Section: base
Priority: required

Package: abinary
Architecture: all
EOF
cat > fake-1/debian/aaaaa <<EOF
also test debian/control not being the first file...
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

testrun - --unlzma=false __extractsourcesection fake_1-1.dsc 3<<EOF
returns 255
=Data seems not to be signed trying to use directly...
*=Error reading from ./fake_1-1.diff.lzma: /bin/false exited with code 1!
-v0*=There have been errors!
stdout
EOF

cat > fake_1-2.diff <<EOF
--- bla/Makefile
+++ bla/Makefile
@@ -1000,1 +1000,1 @@
 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
--- bla/debian/control
+++ bla/debian/control
@@ -0,0 +1,10 @@
+Source: fake
+Section: sssss
+# new-fangled comment
+Priority: ppp
+Homepage: gopher://never-never-land/
+
EOF
dodo gzip fake_1-2.diff

cat > fake_1-2.dsc << EOF
Format: 3.0
Source: fake
Binary: abinary
Architecture: all
Version: 17
Maintainer: Me
Files:
 $(mdandsize fake_1-2.diff.gz) fake_1-2.diff.gz
 00000000000000000000000000000000 0 fake_1.orig.tar.gz
EOF

testrun - __extractsourcesection fake_1-2.dsc 3<<EOF
=Data seems not to be signed trying to use directly...
stdout
*=Section: sssss
*=Priority: ppp
EOF

rm fake*
testsuccess
