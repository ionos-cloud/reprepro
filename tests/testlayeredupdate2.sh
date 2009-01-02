#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
	source $(dirname $0)/test.inc
fi

dodo test ! -d db
mkdir -p conf dists
echo "export never" > conf/options
cat > conf/distributions <<EOF
Codename: boring
Suite: unstable
Components: main firmware
Architectures: $FAKEARCHITECTURE coal source
Update: - 1 2 3 4

Codename: interesting
Suite: experimental
Components: main firmware
Architectures: $FAKEARCHITECTURE coal source
Update: 5 6 - 7 8

Codename: dummy
Components: dummycomponent
Architectures: dummyarchitecture
EOF
mkdir source1 source2
cat > conf/updates <<EOF
Name: a
VerifyRelease: blindtrust
Method: copy:$WORKDIR/source1
Architectures: dummyarchitecture
Components: dummycomponent

Name: b
VerifyRelease: blindtrust
Method: copy:$WORKDIR/source2
Architectures: dummyarchitecture
Flat: dummycomponent

Name: ca
From: a
Architectures: dummyarchitecture
Components: dummycomponent

Name: ma
From: ca
Architectures: dummyarchitecture
Components: main

Name: wa
From: ma
Suite: suitename
Architectures: source

Name: 3
From: wa

Name: 4
Suite: suitename
From: a
Architectures: $FAKEARCHITECTURE coal
#without this I do not get a warning, why?
Components: main firmware

Name: pre1
Flat: firmware
From: b
#without this I do not get a warning, why?
Architectures: $FAKEARCHITECTURE coal
FilterFormula: section (>=firmware/), section(<< firmware0)

Name: 1
From: pre1
Suite: x

Name: 2
Flat: main
From: b
#without this I do not get a warning, why?
Architectures: $FAKEARCHITECTURE coal source
FilterFormula: section (<<firmware/) | section(>= firmware0) | !section
Suite: x

Name: 5
From: b

Name: 6
From: b

Name: 7
From: b

Name: 8
From: b
EOF

DISTRI=dummy PACKAGE=aa EPOCH="" VERSION=1 REVISION=-1000 SECTION="base" genpackage.sh -sa
DISTRI=dummy PACKAGE=bb EPOCH="" VERSION=2 REVISION=-0 SECTION="firmware/base" genpackage.sh -sa
DISTRI=dummy PACKAGE=cc EPOCH="" VERSION=1 REVISION=-1000 SECTION="base" genpackage.sh -sa
DISTRI=dummy PACKAGE=dd EPOCH="" VERSION=2 REVISION=-0 SECTION="firmware/base" genpackage.sh -sa

mv aa* source1
mv bb* source1
mv cc* source2
mv dd* source2

mkdir source2/x
cd source2
echo 'dpkg-scanpackages . /dev/null > x/Packages'
dpkg-scanpackages . /dev/null > x/Packages
cd ..
cat > sourcesections <<EOF
cc standard base
dd standard firmware/base
EOF
cd source2
echo 'dpkg-scansources . sourcesections > x/Sources'
dpkg-scansources . ../sourcesections > x/Sources
cd ..
rm sourcesections

cat > source2/x/Release <<EOF
Codename: x
Suite: toostupidfornonflat
Architectures: coal $FAKEARCHITECTURE
MD5Sum:
 $(mdandsize source2/x/Sources) Sources
 $(mdandsize source2/x/Packages) Packages
EOF

mkdir -p source1/dists/suitename/main/binary-$FAKEARCHITECTURE
mkdir source1/dists/suitename/main/binary-coal
mkdir source1/dists/suitename/main/source
touch source1/dists/suitename/main/binary-$FAKEARCHITECTURE/Packages
touch source1/dists/suitename/main/binary-coal/Packages
touch source1/dists/suitename/main/source/Sources
mkdir -p source1/dists/suitename/firmware/binary-$FAKEARCHITECTURE
mkdir source1/dists/suitename/firmware/binary-coal
mkdir source1/dists/suitename/firmware/source
touch source1/dists/suitename/firmware/binary-$FAKEARCHITECTURE/Packages
touch source1/dists/suitename/firmware/binary-coal/Packages
touch source1/dists/suitename/firmware/source/Sources

cat > source1/dists/suitename/Release <<EOF
Codename: hohoho
Suite: suitename
Architectures: coal $FAKEARCHITECTURE
MD5Sum:
 $(cd source1 ; md5releaseline suitename main/binary-$FAKEARCHITECTURE/Packages)
 $(cd source1 ; md5releaseline suitename main/binary-coal/Packages)
 $(cd source1 ; md5releaseline suitename main/source/Sources)
 $(cd source1 ; md5releaseline suitename firmware/binary-$FAKEARCHITECTURE/Packages)
 $(cd source1 ; md5releaseline suitename firmware/binary-coal/Packages)
 $(cd source1 ; md5releaseline suitename firmware/source/Sources)
EOF

testrun - update boring 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source2/x/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source2/x/Sources'
-v6*=aptmethod start 'copy:$WORKDIR/source2/x/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/main/source/Sources'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/main/binary-$FAKEARCHITECTURE/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/main/binary-coal/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/firmware/binary-$FAKEARCHITECTURE/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/firmware/binary-coal/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source2/x/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source2/x/Sources'
-v1*=aptmethod got 'copy:$WORKDIR/source2/x/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/main/source/Sources'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/main/binary-$FAKEARCHITECTURE/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/main/binary-coal/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/firmware/binary-$FAKEARCHITECTURE/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/firmware/binary-coal/Packages'
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./lists"
-v0*=Calculating packages to get...
-v3*=  processing updates for 'boring|firmware|source'
# 6 times:
-v5*=  marking everything to be deleted
-v3*=  processing updates for 'boring|firmware|coal'
-v5*=  reading './lists/a_suitename_firmware_coal_Packages'
-v3*=  processing updates for 'boring|firmware|abacus'
-v5*=  reading './lists/a_suitename_firmware_${FAKEARCHITECTURE}_Packages'
-v3*=  processing updates for 'boring|main|source'
-v5*=  reading './lists/b_x_Sources'
-v5*=  reading './lists/a_suitename_main_Sources'
-v3*=  processing updates for 'boring|main|coal'
-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_coal_Packages'
-v3*=  processing updates for 'boring|main|abacus'
#-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_abacus_Packages'
-v0*=Getting packages...
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source2/./cc-addons_1-1000_all.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source2/./dd-addons_2-0_all.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source2/./cc_1-1000_$FAKEARCHITECTURE.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source2/./dd_2-0_$FAKEARCHITECTURE.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source2/./cc_1-1000.tar.gz'
-v6*=aptmethod start 'copy:$WORKDIR/source2/./cc_1-1000.dsc'
-v1*=aptmethod got 'copy:$WORKDIR/source2/./cc-addons_1-1000_all.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source2/./dd-addons_2-0_all.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source2/./cc_1-1000_$FAKEARCHITECTURE.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source2/./dd_2-0_$FAKEARCHITECTURE.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source2/./cc_1-1000.tar.gz'
-v1*=aptmethod got 'copy:$WORKDIR/source2/./cc_1-1000.dsc'
stdout
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/firmware"
-v2*=Created directory "./pool/firmware/d"
-v2*=Created directory "./pool/firmware/d/dd"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/c"
-v2*=Created directory "./pool/main/c/cc"
-d1*=db: 'pool/firmware/d/dd/dd-addons_2-0_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/firmware/d/dd/dd_2-0_abacus.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/c/cc/cc-addons_1-1000_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/c/cc/cc_1-1000_abacus.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/c/cc/cc_1-1000.dsc' added to checksums.db(pool).
-d1*=db: 'pool/main/c/cc/cc_1-1000.tar.gz' added to checksums.db(pool).
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'dd-addons' added to packages.db(boring|firmware|coal).
-d1*=db: 'dd' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'dd-addons' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'cc-addons' added to packages.db(boring|main|coal).
-d1*=db: 'cc' added to packages.db(boring|main|source).
-d1*=db: 'cc' added to packages.db(boring|main|$FAKEARCHITECTURE).
-d1*=db: 'cc-addons' added to packages.db(boring|main|$FAKEARCHITECTURE).
stderr
*=Warning: database 'boring|main|abacus' was modified but no index file was exported.
*=Warning: database 'boring|main|coal' was modified but no index file was exported.
*=Warning: database 'boring|main|source' was modified but no index file was exported.
*=Warning: database 'boring|firmware|abacus' was modified but no index file was exported.
*=Warning: database 'boring|firmware|coal' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF

#exit 2

rm -r -f db conf dists pool lists source1 source2
testsuccess
