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

rm testfile* smallfile* fake.deb
testsuccess
