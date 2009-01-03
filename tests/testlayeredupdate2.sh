#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
	source $(dirname $0)/test.inc
fi

dodo test ! -d db
mkdir -p conf dists
echo "export never" > conf/options
cat > conf/updatelog.sh <<EOF
#!/bin/sh
echo "\$@" >> '$WORKDIR/updatelog'
exit 0
EOF
cat > conf/shouldnothappen.sh <<EOF
#!/bin/sh
echo "\$@" >> '$WORKDIR/shouldnothappen'
exit 0
EOF
chmod a+x conf/updatelog.sh conf/shouldnothappen.sh
cat > conf/distributions <<EOF
Codename: boring
Suite: unstable
Components: main firmware
Architectures: $FAKEARCHITECTURE coal source
Log:
	--via update updatelog.sh
	--via include shouldnothappen.sh
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

mkdir source1/pool source1/pool/main source1/pool/firmware
mv aa* source1/pool/main
mv bb* source1/pool/firmware
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
mkdir -p source1/dists/suitename/firmware/binary-$FAKEARCHITECTURE
mkdir source1/dists/suitename/firmware/binary-coal
mkdir source1/dists/suitename/firmware/source

cd source1
dpkg-scansources pool/main /dev/null > dists/suitename/main/source/Sources
dpkg-scanpackages pool/main /dev/null > dists/suitename/main/binary-$FAKEARCHITECTURE/Packages
dpkg-scanpackages -a coal pool/main /dev/null > dists/suitename/main/binary-coal/Packages
dpkg-scansources pool/firmware /dev/null > dists/suitename/firmware/source/Sources
dpkg-scanpackages pool/firmware /dev/null > dists/suitename/firmware/binary-$FAKEARCHITECTURE/Packages
dpkg-scanpackages -a coal pool/firmware /dev/null > dists/suitename/firmware/binary-coal/Packages
cd ..

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
-v3*=  processing updates for 'boring|firmware|${FAKEARCHITECTURE}'
-v5*=  reading './lists/a_suitename_firmware_${FAKEARCHITECTURE}_Packages'
-v3*=  processing updates for 'boring|main|source'
-v5*=  reading './lists/b_x_Sources'
-v5*=  reading './lists/a_suitename_main_Sources'
-v3*=  processing updates for 'boring|main|coal'
-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_coal_Packages'
-v3*=  processing updates for 'boring|main|${FAKEARCHITECTURE}'
#-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_${FAKEARCHITECTURE}_Packages'
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
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/main/aa-addons_1-1000_all.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/firmware/bb-addons_2-0_all.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/main/aa_1-1000_$FAKEARCHITECTURE.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/firmware/bb_2-0_$FAKEARCHITECTURE.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/main/aa_1-1000.tar.gz'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/main/aa_1-1000.dsc'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/main/aa-addons_1-1000_all.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/firmware/bb-addons_2-0_all.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/main/aa_1-1000_$FAKEARCHITECTURE.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/firmware/bb_2-0_$FAKEARCHITECTURE.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/main/aa_1-1000.tar.gz'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/main/aa_1-1000.dsc'
stdout
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/firmware"
-v2*=Created directory "./pool/firmware/b"
-v2*=Created directory "./pool/firmware/b/bb"
-v2*=Created directory "./pool/firmware/d"
-v2*=Created directory "./pool/firmware/d/dd"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/c"
-v2*=Created directory "./pool/main/c/cc"
-v2*=Created directory "./pool/main/a"
-v2*=Created directory "./pool/main/a/aa"
-d1*=db: 'pool/firmware/d/dd/dd-addons_2-0_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/firmware/d/dd/dd_2-0_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/c/cc/cc-addons_1-1000_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/c/cc/cc_1-1000_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/c/cc/cc_1-1000.dsc' added to checksums.db(pool).
-d1*=db: 'pool/main/c/cc/cc_1-1000.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/firmware/b/bb/bb-addons_2-0_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/firmware/b/bb/bb_2-0_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/a/aa/aa-addons_1-1000_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/a/aa/aa_1-1000_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/a/aa/aa_1-1000.dsc' added to checksums.db(pool).
-d1*=db: 'pool/main/a/aa/aa_1-1000.tar.gz' added to checksums.db(pool).
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'dd-addons' added to packages.db(boring|firmware|coal).
-d1*=db: 'dd' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'dd-addons' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'cc-addons' added to packages.db(boring|main|coal).
-d1*=db: 'cc' added to packages.db(boring|main|source).
-d1*=db: 'cc' added to packages.db(boring|main|$FAKEARCHITECTURE).
-d1*=db: 'cc-addons' added to packages.db(boring|main|$FAKEARCHITECTURE).
-d1*=db: 'bb-addons' added to packages.db(boring|firmware|coal).
-d1*=db: 'bb' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'bb-addons' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'aa-addons' added to packages.db(boring|main|coal).
-d1*=db: 'aa' added to packages.db(boring|main|source).
-d1*=db: 'aa' added to packages.db(boring|main|$FAKEARCHITECTURE).
-d1*=db: 'aa-addons' added to packages.db(boring|main|$FAKEARCHITECTURE).
stderr
*=Warning: database 'boring|main|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'boring|main|coal' was modified but no index file was exported.
*=Warning: database 'boring|main|source' was modified but no index file was exported.
*=Warning: database 'boring|firmware|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'boring|firmware|coal' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF

DISTRI=dummy PACKAGE=aa EPOCH="" VERSION=2 REVISION=-1 SECTION="base" genpackage.sh -sa
DISTRI=dummy PACKAGE=bb EPOCH="" VERSION=1 REVISION=-1 SECTION="firmware/base" genpackage.sh -sa
DISTRI=dummy PACKAGE=ee EPOCH="" VERSION=2 REVISION=-1 SECTION="firmware/base" genpackage.sh -sa

rm source1/pool/firmware/bb*
mv aa* source1/pool/main
mv ee* bb* source1/pool/firmware

cd source1
dpkg-scansources pool/main /dev/null > dists/suitename/main/source/Sources
dpkg-scanpackages pool/main /dev/null > dists/suitename/main/binary-$FAKEARCHITECTURE/Packages
dpkg-scanpackages -a coal pool/main /dev/null > dists/suitename/main/binary-coal/Packages
dpkg-scansources pool/firmware /dev/null > dists/suitename/firmware/source/Sources
dpkg-scanpackages pool/firmware /dev/null > dists/suitename/firmware/binary-$FAKEARCHITECTURE/Packages
dpkg-scanpackages -a coal pool/firmware /dev/null > dists/suitename/firmware/binary-coal/Packages
cd ..

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

sed -e 's/Update: - 1/Update: 1/' -i conf/distributions
ed -s conf/updates <<EOF
1a
FilterList: upgradeonly
.
w
q
EOF

testrun - --keepunreferenced update boring 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source2/x/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/main/source/Sources'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/main/binary-$FAKEARCHITECTURE/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/main/binary-coal/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/firmware/binary-$FAKEARCHITECTURE/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/firmware/binary-coal/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source2/x/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/main/source/Sources'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/main/binary-$FAKEARCHITECTURE/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/main/binary-coal/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/firmware/binary-$FAKEARCHITECTURE/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/firmware/binary-coal/Packages'
stdout
-v0*=Calculating packages to get...
-v4*=  nothing to do for 'boring|firmware|source'
-v3*=  processing updates for 'boring|firmware|coal'
-v5*=  reading './lists/a_suitename_firmware_coal_Packages'
-v3*=  processing updates for 'boring|firmware|${FAKEARCHITECTURE}'
-v5*=  reading './lists/a_suitename_firmware_${FAKEARCHITECTURE}_Packages'
-v3*=  processing updates for 'boring|main|source'
-v5*=  reading './lists/b_x_Sources'
-v5*=  reading './lists/a_suitename_main_Sources'
-v3*=  processing updates for 'boring|main|coal'
-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_coal_Packages'
-v3*=  processing updates for 'boring|main|${FAKEARCHITECTURE}'
#-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_${FAKEARCHITECTURE}_Packages'
-v0*=Getting packages...
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/main/aa-addons_2-1_all.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/main/aa_2-1_$FAKEARCHITECTURE.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/main/aa_2-1.tar.gz'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/main/aa_2-1.dsc'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/main/aa-addons_2-1_all.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/main/aa_2-1_$FAKEARCHITECTURE.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/main/aa_2-1.tar.gz'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/main/aa_2-1.dsc'
stdout
-d1*=db: 'pool/main/a/aa/aa-addons_2-1_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/a/aa/aa_2-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/a/aa/aa_2-1.dsc' added to checksums.db(pool).
-d1*=db: 'pool/main/a/aa/aa_2-1.tar.gz' added to checksums.db(pool).
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'aa-addons' removed from packages.db(boring|main|coal).
-d1*=db: 'aa-addons' added to packages.db(boring|main|coal).
-d1*=db: 'aa' removed from packages.db(boring|main|source).
-d1*=db: 'aa' added to packages.db(boring|main|source).
-d1*=db: 'aa' removed from packages.db(boring|main|$FAKEARCHITECTURE).
-d1*=db: 'aa' added to packages.db(boring|main|$FAKEARCHITECTURE).
-d1*=db: 'aa-addons' removed from packages.db(boring|main|$FAKEARCHITECTURE).
-d1*=db: 'aa-addons' added to packages.db(boring|main|$FAKEARCHITECTURE).
-v1*=4 files lost their last reference.
-v1*=(dumpunreferenced lists such files, use deleteunreferenced to delete them.)
stderr
*=Warning: database 'boring|main|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'boring|main|coal' was modified but no index file was exported.
*=Warning: database 'boring|main|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF

#remove upgradeonly again, letting ee in
ed -s conf/updates <<EOF
%g/FilterList: upgradeonly/d
w
q
EOF

testrun - --keepunreferenced update boring 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source2/x/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source2/x/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/Release'
stdout
-v0*=Nothing to do found. (Use --noskipold to force processing)
EOF

testrun - --nolistsdownload --keepunreferenced update boring 3<<EOF
stderr
stdout
-v0*=Nothing to do found. (Use --noskipold to force processing)
EOF

testrun - --nolistsdownload --noskipold --keepunreferenced update boring 3<<EOF
stderr
stdout
-v0*=Calculating packages to get...
-v4*=  nothing to do for 'boring|firmware|source'
-v3*=  processing updates for 'boring|firmware|coal'
-v5*=  reading './lists/a_suitename_firmware_coal_Packages'
-v3*=  processing updates for 'boring|firmware|${FAKEARCHITECTURE}'
-v5*=  reading './lists/a_suitename_firmware_${FAKEARCHITECTURE}_Packages'
-v3*=  processing updates for 'boring|main|source'
-v5*=  reading './lists/b_x_Sources'
-v5*=  reading './lists/a_suitename_main_Sources'
-v3*=  processing updates for 'boring|main|coal'
-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_coal_Packages'
-v3*=  processing updates for 'boring|main|${FAKEARCHITECTURE}'
#-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_${FAKEARCHITECTURE}_Packages'
-v0*=Getting packages...
-v2*=Created directory "./pool/firmware/e"
-v2*=Created directory "./pool/firmware/e/ee"
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/firmware/ee-addons_2-1_all.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/firmware/ee_2-1_$FAKEARCHITECTURE.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/firmware/ee-addons_2-1_all.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/firmware/ee_2-1_$FAKEARCHITECTURE.deb'
stdout
-d1*=db: 'pool/firmware/e/ee/ee-addons_2-1_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/firmware/e/ee/ee_2-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'ee-addons' added to packages.db(boring|firmware|coal).
-d1*=db: 'ee' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'ee-addons' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
stderr
*=Warning: database 'boring|firmware|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'boring|firmware|coal' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
EOF

#  reinsert delete rule, this should cause a downgrade of bb
sed -e 's/Update: 1/Update: - 1/' -i conf/distributions

# changes to the clean rules causes automatic reprocessing, so new noskipold needed here

testrun - --keepunreferenced update boring 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source2/x/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source1/dists/suitename/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source2/x/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source1/dists/suitename/Release'
stdout
-v0*=Calculating packages to get...
-v3*=  processing updates for 'boring|firmware|source'
# 6 times:
-v5*=  marking everything to be deleted
-v3*=  processing updates for 'boring|firmware|coal'
-v5*=  reading './lists/a_suitename_firmware_coal_Packages'
-v3*=  processing updates for 'boring|firmware|${FAKEARCHITECTURE}'
-v5*=  reading './lists/a_suitename_firmware_${FAKEARCHITECTURE}_Packages'
-v3*=  processing updates for 'boring|main|source'
-v5*=  reading './lists/b_x_Sources'
-v5*=  reading './lists/a_suitename_main_Sources'
-v3*=  processing updates for 'boring|main|coal'
-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_coal_Packages'
-v3*=  processing updates for 'boring|main|${FAKEARCHITECTURE}'
#-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/a_suitename_main_${FAKEARCHITECTURE}_Packages'
-v0*=Getting packages...
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/firmware/bb-addons_1-1_all.deb'
-v6*=aptmethod start 'copy:$WORKDIR/source1/pool/firmware/bb_1-1_$FAKEARCHITECTURE.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/firmware/bb-addons_1-1_all.deb'
-v1*=aptmethod got 'copy:$WORKDIR/source1/pool/firmware/bb_1-1_$FAKEARCHITECTURE.deb'
*=Warning: downgrading 'bb-addons' from '2-0' to '1-1' in 'boring|firmware|coal'!
*=Warning: downgrading 'bb' from '2-0' to '1-1' in 'boring|firmware|${FAKEARCHITECTURE}'!
*=Warning: downgrading 'bb-addons' from '2-0' to '1-1' in 'boring|firmware|${FAKEARCHITECTURE}'!
stdout
-d1*=db: 'pool/firmware/b/bb/bb-addons_1-1_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/firmware/b/bb/bb_1-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'bb-addons' removed from packages.db(boring|firmware|coal).
-d1*=db: 'bb' removed from packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'bb-addons' removed from packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'bb-addons' added to packages.db(boring|firmware|coal).
-d1*=db: 'bb' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
-d1*=db: 'bb-addons' added to packages.db(boring|firmware|$FAKEARCHITECTURE).
stderr
*=Warning: database 'boring|firmware|${FAKEARCHITECTURE}' was modified but no index file was exported.
*=Warning: database 'boring|firmware|coal' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
stdout
-v1*=2 files lost their last reference.
-v1*=(dumpunreferenced lists such files, use deleteunreferenced to delete them.)
EOF

#Now it gets evil! Name flat and non-flat the same
dodo sed -i -e 's/suitename/x/'  source1/dists/suitename/Release
mv source1/dists/suitename source1/dists/x
mv source1/dists source2/dists
dodo sed -i -e 's/suitename/x/' -e 's/^From: a$/From: b/' -e 's/Flat: dummycomponent/#&/' conf/updates

testrun - update boring 3<<EOF
stderr
-v0*=Warning: From the same remote repository 'copy:${WORKDIR}/source2', distribution 'x'
-v0*=is requested both flat and non-flat. While this is possible
-v0*=(having copy:${WORKDIR}/source2/dists/x and copy:${WORKDIR}/source2/x), it is unlikely.
-v0*=To no longer see this message, use --ignore=flatandnonflat.
-v6*=aptmethod start 'copy:$WORKDIR/source2/x/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source2/dists/x/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source2/dists/x/main/source/Sources'
-v6*=aptmethod start 'copy:$WORKDIR/source2/dists/x/main/binary-$FAKEARCHITECTURE/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source2/dists/x/main/binary-coal/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source2/dists/x/firmware/binary-$FAKEARCHITECTURE/Packages'
-v6*=aptmethod start 'copy:$WORKDIR/source2/dists/x/firmware/binary-coal/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source2/x/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source2/dists/x/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source2/dists/x/main/source/Sources'
-v1*=aptmethod got 'copy:$WORKDIR/source2/dists/x/main/binary-$FAKEARCHITECTURE/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source2/dists/x/main/binary-coal/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source2/dists/x/firmware/binary-$FAKEARCHITECTURE/Packages'
-v1*=aptmethod got 'copy:$WORKDIR/source2/dists/x/firmware/binary-coal/Packages'
stdout
-v0*=Calculating packages to get...
-v0*=  nothing new for 'boring|firmware|source' (use --noskipold to process anyway)
-v3*=  processing updates for 'boring|firmware|coal'
# 5 times:
-v5*=  marking everything to be deleted
-v5*=  reading './lists/b_x_firmware_coal_Packages'
-v3*=  processing updates for 'boring|firmware|${FAKEARCHITECTURE}'
-v5*=  reading './lists/b_x_firmware_${FAKEARCHITECTURE}_Packages'
-v3*=  processing updates for 'boring|main|source'
-v5*=  reading './lists/b_x_Sources'
-v5*=  reading './lists/b_x_main_Sources'
-v3*=  processing updates for 'boring|main|coal'
-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/b_x_main_coal_Packages'
-v3*=  processing updates for 'boring|main|${FAKEARCHITECTURE}'
#-v5*=  reading './lists/b_x_Packages'
-v5*=  reading './lists/b_x_main_${FAKEARCHITECTURE}_Packages'
-v0*=Getting packages...
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
stderr
EOF

testrun - --ignore=flatandnonflat update boring 3<<EOF
stderr
-v6*=aptmethod start 'copy:$WORKDIR/source2/x/Release'
-v6*=aptmethod start 'copy:$WORKDIR/source2/dists/x/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source2/x/Release'
-v1*=aptmethod got 'copy:$WORKDIR/source2/dists/x/Release'
stdout
-v0*=Nothing to do found. (Use --noskipold to force processing)
stderr
EOF

cat > results.expected <<EOF
add boring deb firmware coal bb-addons 2-0 -- pool/firmware/b/bb/bb-addons_2-0_all.deb
add boring deb firmware coal dd-addons 2-0 -- pool/firmware/d/dd/dd-addons_2-0_all.deb
add boring deb firmware ${FAKEARCHITECTURE} bb 2-0 -- pool/firmware/b/bb/bb_2-0_${FAKEARCHITECTURE}.deb
add boring deb firmware ${FAKEARCHITECTURE} bb-addons 2-0 -- pool/firmware/b/bb/bb-addons_2-0_all.deb
add boring deb firmware ${FAKEARCHITECTURE} dd 2-0 -- pool/firmware/d/dd/dd_2-0_${FAKEARCHITECTURE}.deb
add boring deb firmware ${FAKEARCHITECTURE} dd-addons 2-0 -- pool/firmware/d/dd/dd-addons_2-0_all.deb
add boring dsc main source aa 1-1000 -- pool/main/a/aa/aa_1-1000.dsc pool/main/a/aa/aa_1-1000.tar.gz
add boring dsc main source cc 1-1000 -- pool/main/c/cc/cc_1-1000.dsc pool/main/c/cc/cc_1-1000.tar.gz
add boring deb main coal aa-addons 1-1000 -- pool/main/a/aa/aa-addons_1-1000_all.deb
add boring deb main coal cc-addons 1-1000 -- pool/main/c/cc/cc-addons_1-1000_all.deb
add boring deb main ${FAKEARCHITECTURE} aa 1-1000 -- pool/main/a/aa/aa_1-1000_${FAKEARCHITECTURE}.deb
add boring deb main ${FAKEARCHITECTURE} aa-addons 1-1000 -- pool/main/a/aa/aa-addons_1-1000_all.deb
add boring deb main ${FAKEARCHITECTURE} cc 1-1000 -- pool/main/c/cc/cc_1-1000_${FAKEARCHITECTURE}.deb
add boring deb main ${FAKEARCHITECTURE} cc-addons 1-1000 -- pool/main/c/cc/cc-addons_1-1000_all.deb
replace boring dsc main source aa 2-1 1-1000 -- pool/main/a/aa/aa_2-1.dsc pool/main/a/aa/aa_2-1.tar.gz -- pool/main/a/aa/aa_1-1000.dsc pool/main/a/aa/aa_1-1000.tar.gz
replace boring deb main coal aa-addons 2-1 1-1000 -- pool/main/a/aa/aa-addons_2-1_all.deb -- pool/main/a/aa/aa-addons_1-1000_all.deb
replace boring deb main ${FAKEARCHITECTURE} aa 2-1 1-1000 -- pool/main/a/aa/aa_2-1_${FAKEARCHITECTURE}.deb -- pool/main/a/aa/aa_1-1000_${FAKEARCHITECTURE}.deb
replace boring deb main ${FAKEARCHITECTURE} aa-addons 2-1 1-1000 -- pool/main/a/aa/aa-addons_2-1_all.deb -- pool/main/a/aa/aa-addons_1-1000_all.deb
add boring deb firmware coal ee-addons 2-1 -- pool/firmware/e/ee/ee-addons_2-1_all.deb
add boring deb firmware ${FAKEARCHITECTURE} ee 2-1 -- pool/firmware/e/ee/ee_2-1_${FAKEARCHITECTURE}.deb
add boring deb firmware ${FAKEARCHITECTURE} ee-addons 2-1 -- pool/firmware/e/ee/ee-addons_2-1_all.deb
replace boring deb firmware coal bb-addons 1-1 2-0 -- pool/firmware/b/bb/bb-addons_1-1_all.deb -- pool/firmware/b/bb/bb-addons_2-0_all.deb
replace boring deb firmware ${FAKEARCHITECTURE} bb 1-1 2-0 -- pool/firmware/b/bb/bb_1-1_${FAKEARCHITECTURE}.deb -- pool/firmware/b/bb/bb_2-0_${FAKEARCHITECTURE}.deb
replace boring deb firmware ${FAKEARCHITECTURE} bb-addons 1-1 2-0 -- pool/firmware/b/bb/bb-addons_1-1_all.deb -- pool/firmware/b/bb/bb-addons_2-0_all.deb
EOF

dodo test ! -f shouldnothappen
dodiff results.expected updatelog
rm updatelog results.expected
rm -r -f db conf dists pool lists source1 source2 test.changes
testsuccess
