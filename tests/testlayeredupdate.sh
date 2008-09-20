#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
	source $(dirname $0)/test.inc
fi

dodo test ! -d db
mkdir -p conf dists
echo "export never" > conf/options
cat > conf/distributions <<EOF
Codename: codename1
Components: a bb
UDebComponents: a
Architectures: x yyyyyyyyyy source
Update: a b - b c

Codename: codename2
Components: a bb
Architectures: x yyyyyyyyyy
Update: c - a
EOF
cat > conf/updates <<EOF
Name: base
VerifyRelease: blindtrust
Method: file:$WORKDIR/testsource
Suite: test

Name: a
Suite: codename1
From: base

Name: b
Suite: codename2
From: base

Name: c
Suite: *
From: base
EOF

mkdir lists db

testrun - -b . update codename2 3<<EOF
returns 255
stderr
*=aptmethod error receiving 'file:$WORKDIR/testsource/dists/codename1/Release':
*=aptmethod error receiving 'file:$WORKDIR/testsource/dists/codename2/Release':
*='File not found'
-v0*=There have been errors!
stdout
EOF

mkdir testsource testsource/dists testsource/dists/codename1 testsource/dists/codename2
touch testsource/dists/codename1/Release testsource/dists/codename2/Release

testrun - -b . update codename2 3<<EOF
returns 255
stderr
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename2/Release' to './lists/base_codename2_Release'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/Release' to './lists/base_codename1_Release'...
*=Missing 'MD5Sum' field in Release file './lists/base_codename2_Release'!
-v0*=There have been errors!
stdout
EOF

cat > testsource/dists/codename1/Release <<EOF
Codename: codename1
Architectures: x yyyyyyyyyy
Components: a bb
MD5Sum:
EOF
cat > testsource/dists/codename2/Release <<EOF
Codename: codename2
Architectures: x yyyyyyyyyy
Components: a bb
MD5Sum:
EOF

testrun - -b . update codename2 3<<EOF
returns 254
stderr
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename2/Release' to './lists/base_codename2_Release'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/Release' to './lists/base_codename1_Release'...
*=Could not find 'a/binary-x/Packages' within './lists/base_codename2_Release'
-v0*=There have been errors!
stdout
EOF

mkdir -p testsource/dists/codename1/a/debian-installer/binary-x
touch testsource/dists/codename1/a/debian-installer/binary-x/Packages
mkdir -p testsource/dists/codename1/a/debian-installer/binary-yyyyyyyyyy
touch testsource/dists/codename1/a/debian-installer/binary-yyyyyyyyyy/Packages
mkdir -p testsource/dists/codename1/a/binary-x
touch testsource/dists/codename1/a/binary-x/Packages
mkdir -p testsource/dists/codename1/a/binary-yyyyyyyyyy
touch testsource/dists/codename1/a/binary-yyyyyyyyyy/Packages
mkdir -p testsource/dists/codename1/a/source
touch testsource/dists/codename1/a/source/Sources
mkdir -p testsource/dists/codename1/bb/binary-x
touch testsource/dists/codename1/bb/binary-x/Packages
mkdir -p testsource/dists/codename1/bb/binary-yyyyyyyyyy
touch testsource/dists/codename1/bb/binary-yyyyyyyyyy/Packages
mkdir -p testsource/dists/codename1/bb/source
touch testsource/dists/codename1/bb/source/Sources

cat > testsource/dists/codename1/Release <<EOF
Codename: codename1
Architectures: x yyyyyyyyyy
Components: a bb
MD5Sum:
 $(cd testsource ; md5releaseline codename1 a/debian-installer/binary-x/Packages)
 $(cd testsource ; md5releaseline codename1 a/debian-installer/binary-yyyyyyyyyy/Packages)
 $(cd testsource ; md5releaseline codename1 a/binary-x/Packages)
 $(cd testsource ; md5releaseline codename1 a/binary-yyyyyyyyyy/Packages)
 $(cd testsource ; md5releaseline codename1 a/source/Sources)
 $(cd testsource ; md5releaseline codename1 bb/binary-x/Packages)
 $(cd testsource ; md5releaseline codename1 bb/binary-yyyyyyyyyy/Packages)
 $(cd testsource ; md5releaseline codename1 bb/source/Sources)
EOF

mkdir -p testsource/dists/codename2/a/binary-x
touch testsource/dists/codename2/a/binary-x/Packages
mkdir -p testsource/dists/codename2/a/binary-yyyyyyyyyy
touch testsource/dists/codename2/a/binary-yyyyyyyyyy/Packages
mkdir -p testsource/dists/codename2/bb/binary-x
touch testsource/dists/codename2/bb/binary-x/Packages
mkdir -p testsource/dists/codename2/bb/binary-yyyyyyyyyy
touch testsource/dists/codename2/bb/binary-yyyyyyyyyy/Packages
mkdir -p testsource/dists/codename2/a/debian-installer/binary-x
touch testsource/dists/codename2/a/debian-installer/binary-x/Packages
mkdir -p testsource/dists/codename2/a/debian-installer/binary-yyyyyyyyyy
touch testsource/dists/codename2/a/debian-installer/binary-yyyyyyyyyy/Packages
mkdir -p testsource/dists/codename2/a/source
touch testsource/dists/codename2/a/source/Sources
mkdir -p testsource/dists/codename2/bb/source
touch testsource/dists/codename2/bb/source/Sources

cat > testsource/dists/codename2/Release <<EOF
Codename: codename2
Architectures: x yyyyyyyyyy
Components: a bb
MD5Sum:
 $(cd testsource ; md5releaseline codename2 a/debian-installer/binary-x/Packages)
 $(cd testsource ; md5releaseline codename2 a/debian-installer/binary-yyyyyyyyyy/Packages)
 $(cd testsource ; md5releaseline codename2 a/binary-x/Packages)
 $(cd testsource ; md5releaseline codename2 a/binary-yyyyyyyyyy/Packages)
 $(cd testsource ; md5releaseline codename2 a/source/Sources)
 $(cd testsource ; md5releaseline codename2 bb/binary-x/Packages)
 $(cd testsource ; md5releaseline codename2 bb/binary-yyyyyyyyyy/Packages)
 $(cd testsource ; md5releaseline codename2 bb/source/Sources)
EOF

lzma testsource/dists/codename2/a/binary-x/Packages
lzma testsource/dists/codename2/a/source/Sources
lzma testsource/dists/codename2/bb/source/Sources
lzma testsource/dists/codename2/a/debian-installer/binary-yyyyyyyyyy/Packages
lzma testsource/dists/codename2/bb/binary-yyyyyyyyyy/Packages
lzma testsource/dists/codename2/bb/binary-x/Packages
lzma testsource/dists/codename2/a/binary-yyyyyyyyyy/Packages
lzma testsource/dists/codename2/a/debian-installer/binary-x/Packages

cat >> testsource/dists/codename2/Release <<EOF
 $(cd testsource ; md5releaseline codename2 a/debian-installer/binary-x/Packages.lzma)
 $(cd testsource ; md5releaseline codename2 a/debian-installer/binary-yyyyyyyyyy/Packages.lzma)
 $(cd testsource ; md5releaseline codename2 a/binary-x/Packages.lzma)
 $(cd testsource ; md5releaseline codename2 a/binary-yyyyyyyyyy/Packages.lzma)
 $(cd testsource ; md5releaseline codename2 a/source/Sources.lzma)
 $(cd testsource ; md5releaseline codename2 bb/binary-x/Packages.lzma)
 $(cd testsource ; md5releaseline codename2 bb/binary-yyyyyyyyyy/Packages.lzma)
 $(cd testsource ; md5releaseline codename2 bb/source/Sources.lzma)
EOF


testout - -b . update codename2 3<<EOF
stderr
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename2/Release' to './lists/base_codename2_Release'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/Release' to './lists/base_codename1_Release'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/a/binary-x/Packages'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/a/binary-x/Packages' to './lists/base_codename1_a_x_Packages'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/bb/binary-x/Packages'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/bb/binary-x/Packages' to './lists/base_codename1_bb_x_Packages'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/a/binary-yyyyyyyyyy/Packages'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/a/binary-yyyyyyyyyy/Packages' to './lists/base_codename1_a_yyyyyyyyyy_Packages'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/bb/binary-yyyyyyyyyy/Packages'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/bb/binary-yyyyyyyyyy/Packages' to './lists/base_codename1_bb_yyyyyyyyyy_Packages'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/a/binary-x/Packages.lzma'
-v2*=Uncompress '$WORKDIR/testsource/dists/codename2/a/binary-x/Packages.lzma' into './lists/base_codename2_a_x_Packages' using '/usr/bin/unlzma'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/bb/binary-x/Packages.lzma'
-v2*=Uncompress '$WORKDIR/testsource/dists/codename2/bb/binary-x/Packages.lzma' into './lists/base_codename2_bb_x_Packages' using '/usr/bin/unlzma'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/a/binary-yyyyyyyyyy/Packages.lzma'
-v2*=Uncompress '$WORKDIR/testsource/dists/codename2/a/binary-yyyyyyyyyy/Packages.lzma' into './lists/base_codename2_a_yyyyyyyyyy_Packages' using '/usr/bin/unlzma'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/bb/binary-yyyyyyyyyy/Packages.lzma'
-v2*=Uncompress '$WORKDIR/testsource/dists/codename2/bb/binary-yyyyyyyyyy/Packages.lzma' into './lists/base_codename2_bb_yyyyyyyyyy_Packages' using '/usr/bin/unlzma'...
EOF
true > results.expected
if [ $verbosity -ge 0 ] ; then
echo "Calculating packages to get..." > results.expected ; fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename2|bb|yyyyyyyyyy'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename2_bb_yyyyyyyyyy_Packages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename1_bb_yyyyyyyyyy_Packages'" >>results.expected ; fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename2|bb|x'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename2_bb_x_Packages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename1_bb_x_Packages'" >>results.expected ; fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename2|a|yyyyyyyyyy'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename2_a_yyyyyyyyyy_Packages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename1_a_yyyyyyyyyy_Packages'" >>results.expected ; fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename2|a|x'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename2_a_x_Packages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename1_a_x_Packages'" >>results.expected ; fi
if [ $verbosity -ge 0 ] ; then
echo "Getting packages..." >>results.expected ; fi
if [ $verbosity -ge 1 ] ; then
echo "Shutting down aptmethods..." >>results.expected ; fi
if [ $verbosity -ge 0 ] ; then
echo "Installing (and possibly deleting) packages..." >>results.expected ; fi
dodiff results.expected results
mv results.expected results2.expected

testout - -b . update codename1 3<<EOF
stderr
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename2/Release' to './lists/base_codename2_Release'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/Release' to './lists/base_codename1_Release'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/a/debian-installer/binary-x/Packages'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/a/debian-installer/binary-x/Packages' to './lists/base_codename1_a_x_uPackages'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/a/debian-installer/binary-yyyyyyyyyy/Packages'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/a/debian-installer/binary-yyyyyyyyyy/Packages' to './lists/base_codename1_a_yyyyyyyyyy_uPackages'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/a/debian-installer/binary-x/Packages.lzma'
-v2*=Uncompress '$WORKDIR/testsource/dists/codename2/a/debian-installer/binary-x/Packages.lzma' into './lists/base_codename2_a_x_uPackages' using '/usr/bin/unlzma'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/a/debian-installer/binary-yyyyyyyyyy/Packages.lzma'
-v2*=Uncompress '$WORKDIR/testsource/dists/codename2/a/debian-installer/binary-yyyyyyyyyy/Packages.lzma' into './lists/base_codename2_a_yyyyyyyyyy_uPackages' using '/usr/bin/unlzma'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/a/source/Sources'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/a/source/Sources' to './lists/base_codename1_a_Sources'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/bb/source/Sources'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/bb/source/Sources' to './lists/base_codename1_bb_Sources'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/a/source/Sources.lzma'
-v2*=Uncompress '$WORKDIR/testsource/dists/codename2/a/source/Sources.lzma' into './lists/base_codename2_a_Sources' using '/usr/bin/unlzma'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/bb/source/Sources.lzma'
-v2*=Uncompress '$WORKDIR/testsource/dists/codename2/bb/source/Sources.lzma' into './lists/base_codename2_bb_Sources' using '/usr/bin/unlzma'...
EOF

true > results.expected
if [ $verbosity -ge 0 ] ; then
echo "Calculating packages to get..." > results.expected ; fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename1|bb|source'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename1_bb_Sources'" >>results.expected
echo "  reading './lists/base_codename2_bb_Sources'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename2_bb_Sources'" >>results.expected
echo "  reading './lists/base_codename1_bb_Sources'" >>results.expected
fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename1|bb|yyyyyyyyyy'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename1_bb_yyyyyyyyyy_Packages'" >>results.expected
echo "  reading './lists/base_codename2_bb_yyyyyyyyyy_Packages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename2_bb_yyyyyyyyyy_Packages'" >>results.expected
echo "  reading './lists/base_codename1_bb_yyyyyyyyyy_Packages'" >>results.expected
fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename1|bb|x'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename1_bb_x_Packages'" >>results.expected
echo "  reading './lists/base_codename2_bb_x_Packages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename2_bb_x_Packages'" >>results.expected
echo "  reading './lists/base_codename1_bb_x_Packages'" >>results.expected
fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename1|a|source'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename1_a_Sources'" >>results.expected
echo "  reading './lists/base_codename2_a_Sources'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename2_a_Sources'" >>results.expected
echo "  reading './lists/base_codename1_a_Sources'" >>results.expected
fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'u|codename1|a|yyyyyyyyyy'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename1_a_yyyyyyyyyy_uPackages'" >>results.expected
echo "  reading './lists/base_codename2_a_yyyyyyyyyy_uPackages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename2_a_yyyyyyyyyy_uPackages'" >>results.expected
echo "  reading './lists/base_codename1_a_yyyyyyyyyy_uPackages'" >>results.expected
fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename1|a|yyyyyyyyyy'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename1_a_yyyyyyyyyy_Packages'" >>results.expected
echo "  reading './lists/base_codename2_a_yyyyyyyyyy_Packages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename2_a_yyyyyyyyyy_Packages'" >>results.expected
echo "  reading './lists/base_codename1_a_yyyyyyyyyy_Packages'" >>results.expected
fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'u|codename1|a|x'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename1_a_x_uPackages'" >>results.expected
echo "  reading './lists/base_codename2_a_x_uPackages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename2_a_x_uPackages'" >>results.expected
echo "  reading './lists/base_codename1_a_x_uPackages'" >>results.expected
fi
if [ $verbosity -ge 3 ] ; then
echo "  processing updates for 'codename1|a|x'" >>results.expected ; fi
if [ $verbosity -ge 5 ] ; then
echo "  reading './lists/base_codename1_a_x_Packages'" >>results.expected
echo "  reading './lists/base_codename2_a_x_Packages'" >>results.expected
echo "  marking everything to be deleted" >>results.expected
echo "  reading './lists/base_codename2_a_x_Packages'" >>results.expected
echo "  reading './lists/base_codename1_a_x_Packages'" >>results.expected
fi
if [ $verbosity -ge 0 ] ; then
echo "Getting packages..." >>results.expected ; fi
if [ $verbosity -ge 1 ] ; then
echo "Shutting down aptmethods..." >>results.expected ; fi
if [ $verbosity -ge 0 ] ; then
echo "Installing (and possibly deleting) packages..." >>results.expected ; fi
dodiff results.expected results

testrun - -b . update codename2 codename1 3<<EOF
stderr
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename2/Release' to './lists/base_codename2_Release'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/Release' to './lists/base_codename1_Release'...
stdout
-v0*=Nothing to do found. (Use --noskipold to force processing)
EOF
rm lists/_codename*
testout - -b . update codename2 codename1 3<<EOF
stderr
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename1/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename2/Release' to './lists/base_codename2_Release'...
-v1*=aptmethod got 'file:$WORKDIR/testsource/dists/codename2/Release'
-v2*=Copy file '$WORKDIR/testsource/dists/codename1/Release' to './lists/base_codename1_Release'...
EOF
grep '^C' results.expected > resultsboth.expected
grep '^  ' results2.expected >> resultsboth.expected
grep '^  ' results.expected >> resultsboth.expected
grep '^[^ C]' results.expected >> resultsboth.expected
dodiff resultsboth.expected results

sed -i -e "s/Method: file:/Method: copy:/" conf/updates

rm lists/_codename*
testout - -b . update codename1 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/Release'
EOF
dodiff results.expected results

rm -r lists ; mkdir lists

testout - -b . update codename2 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/a/binary-x/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/a/binary-x/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/bb/binary-x/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/bb/binary-x/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/a/binary-yyyyyyyyyy/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/a/binary-yyyyyyyyyy/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/bb/binary-yyyyyyyyyy/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/bb/binary-yyyyyyyyyy/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/a/binary-x/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/a/binary-x/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_a_x_Packages.lzma' into './lists/base_codename2_a_x_Packages' using '/usr/bin/unlzma'...
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/bb/binary-x/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/bb/binary-x/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_bb_x_Packages.lzma' into './lists/base_codename2_bb_x_Packages' using '/usr/bin/unlzma'...
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/a/binary-yyyyyyyyyy/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/a/binary-yyyyyyyyyy/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_a_yyyyyyyyyy_Packages.lzma' into './lists/base_codename2_a_yyyyyyyyyy_Packages' using '/usr/bin/unlzma'...
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/bb/binary-yyyyyyyyyy/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/bb/binary-yyyyyyyyyy/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_bb_yyyyyyyyyy_Packages.lzma' into './lists/base_codename2_bb_yyyyyyyyyy_Packages' using '/usr/bin/unlzma'...
EOF
dodiff results2.expected results

testout - -b . update codename1 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/a/debian-installer/binary-x/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/a/debian-installer/binary-x/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/a/debian-installer/binary-yyyyyyyyyy/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/a/debian-installer/binary-yyyyyyyyyy/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/a/debian-installer/binary-x/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/a/debian-installer/binary-x/Packages.lzma'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/a/debian-installer/binary-yyyyyyyyyy/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/a/debian-installer/binary-yyyyyyyyyy/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_a_x_uPackages.lzma' into './lists/base_codename2_a_x_uPackages' using '/usr/bin/unlzma'...
-v2*=Uncompress './lists/base_codename2_a_yyyyyyyyyy_uPackages.lzma' into './lists/base_codename2_a_yyyyyyyyyy_uPackages' using '/usr/bin/unlzma'...
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/a/source/Sources'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/a/source/Sources'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/bb/source/Sources'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/bb/source/Sources'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/a/source/Sources.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/a/source/Sources.lzma'
-v2*=Uncompress './lists/base_codename2_a_Sources.lzma' into './lists/base_codename2_a_Sources' using '/usr/bin/unlzma'...
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/bb/source/Sources.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/bb/source/Sources.lzma'
-v2*=Uncompress './lists/base_codename2_bb_Sources.lzma' into './lists/base_codename2_bb_Sources' using '/usr/bin/unlzma'...
EOF
dodiff results.expected results

# Test repositories without uncompressed files listed:
printf '%%g/^ .*[^a]$/d\nw\nq\n' | ed -s testsource/dists/codename2/Release
# lists/_codename* no longer has to be deleted, as without the uncompressed checksums
# reprepro does not know it already processed those (it only saves the uncompressed
# checksums of the already processed files)

# As the checksums for the uncompressed files are not know, and the .lzma files
# not saved, the lzma files have to be downloaded again:
testout - -b . update codename2 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/a/binary-x/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/a/binary-x/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_a_x_Packages.lzma' into './lists/base_codename2_a_x_Packages' using '/usr/bin/unlzma'...
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/bb/binary-x/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/bb/binary-x/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_bb_x_Packages.lzma' into './lists/base_codename2_bb_x_Packages' using '/usr/bin/unlzma'...
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/a/binary-yyyyyyyyyy/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/a/binary-yyyyyyyyyy/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_a_yyyyyyyyyy_Packages.lzma' into './lists/base_codename2_a_yyyyyyyyyy_Packages' using '/usr/bin/unlzma'...
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/bb/binary-yyyyyyyyyy/Packages.lzma'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/bb/binary-yyyyyyyyyy/Packages.lzma'
-v2*=Uncompress './lists/base_codename2_bb_yyyyyyyyyy_Packages.lzma' into './lists/base_codename2_bb_yyyyyyyyyy_Packages' using '/usr/bin/unlzma'...
EOF
dodiff results2.expected results

# last time the .lzma files should have not been deleted, so no download
# but uncompress has still to be done...
testout - -b . update codename2 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename1/Release'
-v6*=aptmethod start 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/testsource/dists/codename2/Release'
-v2*=Uncompress './lists/base_codename2_a_x_Packages.lzma' into './lists/base_codename2_a_x_Packages' using '/usr/bin/unlzma'...
-v2*=Uncompress './lists/base_codename2_bb_x_Packages.lzma' into './lists/base_codename2_bb_x_Packages' using '/usr/bin/unlzma'...
-v2*=Uncompress './lists/base_codename2_a_yyyyyyyyyy_Packages.lzma' into './lists/base_codename2_a_yyyyyyyyyy_Packages' using '/usr/bin/unlzma'...
-v2*=Uncompress './lists/base_codename2_bb_yyyyyyyyyy_Packages.lzma' into './lists/base_codename2_bb_yyyyyyyyyy_Packages' using '/usr/bin/unlzma'...
EOF
dodiff results2.expected results

rm -r -f db conf dists pool lists testsource
echo "TODO: write tests for the error messages (unknown Update rules, unknown From: rules, unused stuf, ..."
testsuccess
