#!/bin/bash

set -e

source $(dirname $0)/test.inc

runtest check
runtest flat
runtest subcomponents
runtest snapshotcopyrestore
runtest various1

echo "Running various other old test..."
mkdir -p conf logs
cat > conf/options <<CONFEND
export changed
CONFEND
cat > conf/distributions <<CONFEND
Codename: test1
Architectures: ${FAKEARCHITECTURE} source
Components: stupid ugly
Update: Test2toTest1
DebIndices: Packages Release . .gz .bz2
UDebIndices: Packages .gz .bz2
DscIndices: Sources Release .gz .bz2
Tracking: keep includechanges includebyhand
Log: log1

Codename: test2
Architectures: ${FAKEARCHITECTURE} coal source
Components: stupid ugly
Origin: Brain
Label: Only a test
Suite: broken
Version: 9999999.02
DebIndices: Packages Release . .gz $SRCDIR/docs/bzip.example testhook
UDebIndices: Packages .gz
DscIndices: Sources Release . .gz $SRCDIR/docs/bzip.example testhook
Description: test with all fields set
DebOverride: binoverride
DscOverride: srcoverride
Log: log2
CONFEND

cat > conf/testhook <<'EOF'
#!/bin/sh
echo "testhook got $#: '$1' '$2' '$3' '$4'"
if test -f "$1/$3.deprecated" ; then
	echo "$3.deprecated.tobedeleted" >&3
fi
echo "super-compressed" > "$1/$3.super.new"
echo "$3.super.new" >&3
EOF
chmod a+x conf/testhook

mkdir -p "dists/test2/stupid/binary-${FAKEARCHITECTURE}"
touch "dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages.deprecated"

set -v
checknolog logfile
if test -n "$TESTNEWFILESDB" ; then
	dodo test ! -f db/files.db
fi
testrun - -b . export 3<<EOF
stdout
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'new'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'new'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'new'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'new'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'new'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'new'
-v2*=Created directory "./db"
-v1*=Exporting test2...
-v6*= exporting 'test2|stupid|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v11*=Exporthook successfully returned!
-v2*=Created directory "./dists/test2/stupid/binary-coal"
-v6*= exporting 'test2|stupid|coal'...
-v6*=  creating './dists/test2/stupid/binary-coal/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v2*=Created directory "./dists/test2/stupid/source"
-v6*= exporting 'test2|stupid|source'...
-v6*=  creating './dists/test2/stupid/source/Sources' (uncompressed,gzipped,script: bzip.example,testhook)
-v2*=Created directory "./dists/test2/ugly"
-v2*=Created directory "./dists/test2/ugly/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v2*=Created directory "./dists/test2/ugly/binary-coal"
-v6*= exporting 'test2|ugly|coal'...
-v6*=  creating './dists/test2/ugly/binary-coal/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v2*=Created directory "./dists/test2/ugly/source"
-v6*= exporting 'test2|ugly|source'...
-v6*=  creating './dists/test2/ugly/source/Sources' (uncompressed,gzipped,script: bzip.example,testhook)
-v1*=Exporting test1...
-v2*=Created directory "./dists/test1"
-v2*=Created directory "./dists/test1/stupid"
-v2*=Created directory "./dists/test1/stupid/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v2*=Created directory "./dists/test1/stupid/source"
-v6*= exporting 'test1|stupid|source'...
-v6*=  creating './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v2*=Created directory "./dists/test1/ugly"
-v2*=Created directory "./dists/test1/ugly/binary-${FAKEARCHITECTURE}"
-v6*= exporting 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*=  creating './dists/test1/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v2*=Created directory "./dists/test1/ugly/source"
-v6*= exporting 'test1|ugly|source'...
-v6*=  creating './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
EOF
dodo test '!' -e "dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages.deprecated"
if test -n "$TESTNEWFILESDB" ; then
	dodo rm db/files.db
fi
test -f dists/test1/Release
test -f dists/test2/Release

cat > dists/test1/stupid/binary-${FAKEARCHITECTURE}/Release.expected <<END
Component: stupid
Architecture: ${FAKEARCHITECTURE}
END
dodiff dists/test1/stupid/binary-${FAKEARCHITECTURE}/Release.expected dists/test1/stupid/binary-${FAKEARCHITECTURE}/Release
cat > dists/test1/ugly/binary-${FAKEARCHITECTURE}/Release.expected <<END
Component: ugly
Architecture: ${FAKEARCHITECTURE}
END
echo "super-compressed" > "fakesuper"
FAKESUPERMD5="$(mdandsize fakesuper)"
FAKESUPERSHA1="$(sha1andsize fakesuper)"
FAKESUPERSHA2="$(sha2andsize fakesuper)"

dodiff dists/test1/ugly/binary-${FAKEARCHITECTURE}/Release.expected dists/test1/ugly/binary-${FAKEARCHITECTURE}/Release
cat > dists/test1/Release.expected <<END
Codename: test1
Date: normalized
Architectures: ${FAKEARCHITECTURE}
Components: stupid ugly
MD5Sum:
 $EMPTYMD5 stupid/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZMD5 stupid/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2MD5 stupid/binary-${FAKEARCHITECTURE}/Packages.bz2
 $(mdandsize dists/test1/stupid/binary-${FAKEARCHITECTURE}/Release) stupid/binary-${FAKEARCHITECTURE}/Release
 $EMPTYMD5 stupid/source/Sources
 $EMPTYGZMD5 stupid/source/Sources.gz
 $EMPTYBZ2MD5 stupid/source/Sources.bz2
 e38c7da133734e1fd68a7e344b94fe96 39 stupid/source/Release
 $EMPTYMD5 ugly/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZMD5 ugly/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2MD5 ugly/binary-${FAKEARCHITECTURE}/Packages.bz2
 $(mdandsize dists/test1/ugly/binary-${FAKEARCHITECTURE}/Release) ugly/binary-${FAKEARCHITECTURE}/Release
 $EMPTYMD5 ugly/source/Sources
 $EMPTYGZMD5 ugly/source/Sources.gz
 $EMPTYBZ2MD5 ugly/source/Sources.bz2
 ed4ee9aa5d080f67926816133872fd02 37 ugly/source/Release
SHA1:
 $(sha1andsize dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages) stupid/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZSHA1 stupid/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2SHA1 stupid/binary-${FAKEARCHITECTURE}/Packages.bz2
 $(sha1andsize dists/test1/stupid/binary-${FAKEARCHITECTURE}/Release) stupid/binary-${FAKEARCHITECTURE}/Release
 $EMPTYSHA1 stupid/source/Sources
 $EMPTYGZSHA1 stupid/source/Sources.gz
 $EMPTYBZ2SHA1 stupid/source/Sources.bz2
 ff71705a4cadaec55de5a6ebbfcd726caf2e2606 39 stupid/source/Release
 $EMPTYSHA1 ugly/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZSHA1 ugly/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2SHA1 ugly/binary-${FAKEARCHITECTURE}/Packages.bz2
 $(sha1andsize dists/test1/ugly/binary-${FAKEARCHITECTURE}/Release) ugly/binary-${FAKEARCHITECTURE}/Release
 $EMPTYSHA1 ugly/source/Sources
 $EMPTYGZSHA1 ugly/source/Sources.gz
 $EMPTYBZ2SHA1 ugly/source/Sources.bz2
 b297876e9d6ee3ee6083160003755047ede22a96 37 ugly/source/Release
SHA256:
 $(sha2andsize dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages) stupid/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZSHA2 stupid/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2SHA2 stupid/binary-${FAKEARCHITECTURE}/Packages.bz2
 $(sha2andsize dists/test1/stupid/binary-${FAKEARCHITECTURE}/Release) stupid/binary-${FAKEARCHITECTURE}/Release
 $EMPTYSHA2 stupid/source/Sources
 $EMPTYGZSHA2 stupid/source/Sources.gz
 $EMPTYBZ2SHA2 stupid/source/Sources.bz2
 b88352d8e0227a133e2236c3a8961581562ee285980fc20bb79626d0d208aa51 39 stupid/source/Release
 $EMPTYSHA2 ugly/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZSHA2 ugly/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2SHA2 ugly/binary-${FAKEARCHITECTURE}/Packages.bz2
 $(sha2andsize dists/test1/ugly/binary-${FAKEARCHITECTURE}/Release) ugly/binary-${FAKEARCHITECTURE}/Release
 $EMPTYSHA2 ugly/source/Sources
 $EMPTYGZSHA2 ugly/source/Sources.gz
 $EMPTYBZ2SHA2 ugly/source/Sources.bz2
 edb5450a3f98a140b938c8266b8b998ba8f426c80ac733fe46423665d5770d9f 37 ugly/source/Release
END
cat > dists/test2/stupid/binary-${FAKEARCHITECTURE}/Release.expected <<END
Archive: broken
Version: 9999999.02
Component: stupid
Origin: Brain
Label: Only a test
Architecture: ${FAKEARCHITECTURE}
Description: test with all fields set
END
dodiff dists/test2/stupid/binary-${FAKEARCHITECTURE}/Release.expected dists/test2/stupid/binary-${FAKEARCHITECTURE}/Release
cat > dists/test2/ugly/binary-${FAKEARCHITECTURE}/Release.expected <<END
Archive: broken
Version: 9999999.02
Component: ugly
Origin: Brain
Label: Only a test
Architecture: ${FAKEARCHITECTURE}
Description: test with all fields set
END
dodiff dists/test1/ugly/binary-${FAKEARCHITECTURE}/Release.expected dists/test1/ugly/binary-${FAKEARCHITECTURE}/Release
cat > dists/test2/Release.expected <<END
Origin: Brain
Label: Only a test
Suite: broken
Codename: test2
Version: 9999999.02
Date: normalized
Architectures: ${FAKEARCHITECTURE} coal
Components: stupid ugly
Description: test with all fields set
MD5Sum:
 $EMPTYMD5 stupid/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZMD5 stupid/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2MD5 stupid/binary-${FAKEARCHITECTURE}/Packages.bz2
 $FAKESUPERMD5 stupid/binary-${FAKEARCHITECTURE}/Packages.super
 $(mdandsize dists/test2/stupid/binary-${FAKEARCHITECTURE}/Release) stupid/binary-${FAKEARCHITECTURE}/Release
 $EMPTYMD5 stupid/binary-coal/Packages
 $EMPTYGZMD5 stupid/binary-coal/Packages.gz
 $EMPTYBZ2MD5 stupid/binary-coal/Packages.bz2
 $FAKESUPERMD5 stupid/binary-coal/Packages.super
 10ae2f283e1abdd3facfac6ed664035d 144 stupid/binary-coal/Release
 $EMPTYMD5 stupid/source/Sources
 $EMPTYGZMD5 stupid/source/Sources.gz
 $EMPTYBZ2MD5 stupid/source/Sources.bz2
 $FAKESUPERMD5 stupid/source/Sources.super
 b923b3eb1141e41f0b8bb74297ac8a36 146 stupid/source/Release
 $EMPTYMD5 ugly/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZMD5 ugly/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2MD5 ugly/binary-${FAKEARCHITECTURE}/Packages.bz2
 $FAKESUPERMD5 ugly/binary-${FAKEARCHITECTURE}/Packages.super
 $(mdandsize dists/test2/ugly/binary-${FAKEARCHITECTURE}/Release) ugly/binary-${FAKEARCHITECTURE}/Release
 $EMPTYMD5 ugly/binary-coal/Packages
 $EMPTYGZMD5 ugly/binary-coal/Packages.gz
 $EMPTYBZ2MD5 ugly/binary-coal/Packages.bz2
 $FAKESUPERMD5 ugly/binary-coal/Packages.super
 7a05de3b706d08ed06779d0ec2e234e9 142 ugly/binary-coal/Release
 $EMPTYMD5 ugly/source/Sources
 $EMPTYGZMD5 ugly/source/Sources.gz
 $EMPTYBZ2MD5 ugly/source/Sources.bz2
 $FAKESUPERMD5 ugly/source/Sources.super
 e73a8a85315766763a41ad4dc6744bf5 144 ugly/source/Release
SHA1:
 $EMPTYSHA1 stupid/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZSHA1 stupid/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2SHA1 stupid/binary-${FAKEARCHITECTURE}/Packages.bz2
 $FAKESUPERSHA1 stupid/binary-${FAKEARCHITECTURE}/Packages.super
 $(sha1andsize dists/test2/stupid/binary-${FAKEARCHITECTURE}/Release) stupid/binary-${FAKEARCHITECTURE}/Release
 $EMPTYSHA1 stupid/binary-coal/Packages
 $EMPTYGZSHA1 stupid/binary-coal/Packages.gz
 $EMPTYBZ2SHA1 stupid/binary-coal/Packages.bz2
 $FAKESUPERSHA1 stupid/binary-coal/Packages.super
 $(sha1andsize dists/test2/stupid/binary-coal/Release) stupid/binary-coal/Release
 $EMPTYSHA1 stupid/source/Sources
 $EMPTYGZSHA1 stupid/source/Sources.gz
 $EMPTYBZ2SHA1 stupid/source/Sources.bz2
 $FAKESUPERSHA1 stupid/source/Sources.super
 $(sha1andsize dists/test2/stupid/source/Release) stupid/source/Release
 $EMPTYSHA1 ugly/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZSHA1 ugly/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2SHA1 ugly/binary-${FAKEARCHITECTURE}/Packages.bz2
 $FAKESUPERSHA1 ugly/binary-${FAKEARCHITECTURE}/Packages.super
 $(sha1andsize dists/test2/ugly/binary-${FAKEARCHITECTURE}/Release) ugly/binary-${FAKEARCHITECTURE}/Release
 $EMPTYSHA1 ugly/binary-coal/Packages
 $EMPTYGZSHA1 ugly/binary-coal/Packages.gz
 $EMPTYBZ2SHA1 ugly/binary-coal/Packages.bz2
 $FAKESUPERSHA1 ugly/binary-coal/Packages.super
 $(sha1andsize dists/test2/ugly/binary-coal/Release) ugly/binary-coal/Release
 $EMPTYSHA1 ugly/source/Sources
 $EMPTYGZSHA1 ugly/source/Sources.gz
 $EMPTYBZ2SHA1 ugly/source/Sources.bz2
 $FAKESUPERSHA1 ugly/source/Sources.super
 $(sha1andsize dists/test2/ugly/source/Release) ugly/source/Release
SHA256:
 $EMPTYSHA2 stupid/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZSHA2 stupid/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2SHA2 stupid/binary-${FAKEARCHITECTURE}/Packages.bz2
 $FAKESUPERSHA2 stupid/binary-${FAKEARCHITECTURE}/Packages.super
 $(sha2andsize dists/test2/stupid/binary-${FAKEARCHITECTURE}/Release) stupid/binary-${FAKEARCHITECTURE}/Release
 $EMPTYSHA2 stupid/binary-coal/Packages
 $EMPTYGZSHA2 stupid/binary-coal/Packages.gz
 $EMPTYBZ2SHA2 stupid/binary-coal/Packages.bz2
 $FAKESUPERSHA2 stupid/binary-coal/Packages.super
 $(sha2andsize dists/test2/stupid/binary-coal/Release) stupid/binary-coal/Release
 $EMPTYSHA2 stupid/source/Sources
 $EMPTYGZSHA2 stupid/source/Sources.gz
 $EMPTYBZ2SHA2 stupid/source/Sources.bz2
 $FAKESUPERSHA2 stupid/source/Sources.super
 $(sha2andsize dists/test2/stupid/source/Release) stupid/source/Release
 $EMPTYSHA2 ugly/binary-${FAKEARCHITECTURE}/Packages
 $EMPTYGZSHA2 ugly/binary-${FAKEARCHITECTURE}/Packages.gz
 $EMPTYBZ2SHA2 ugly/binary-${FAKEARCHITECTURE}/Packages.bz2
 $FAKESUPERSHA2 ugly/binary-${FAKEARCHITECTURE}/Packages.super
 $(sha2andsize dists/test2/ugly/binary-${FAKEARCHITECTURE}/Release) ugly/binary-${FAKEARCHITECTURE}/Release
 $EMPTYSHA2 ugly/binary-coal/Packages
 $EMPTYGZSHA2 ugly/binary-coal/Packages.gz
 $EMPTYBZ2SHA2 ugly/binary-coal/Packages.bz2
 $FAKESUPERSHA2 ugly/binary-coal/Packages.super
 $(sha2andsize dists/test2/ugly/binary-coal/Release) ugly/binary-coal/Release
 $EMPTYSHA2 ugly/source/Sources
 $EMPTYGZSHA2 ugly/source/Sources.gz
 $EMPTYBZ2SHA2 ugly/source/Sources.bz2
 $FAKESUPERSHA2 ugly/source/Sources.super
 $(sha2andsize dists/test2/ugly/source/Release) ugly/source/Release
END
printf '%%g/^Date:/s/Date: .*/Date: normalized/\n%%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq\n' | ed -s dists/test1/Release
printf '%%g/^Date:/s/Date: .*/Date: normalized/\n%%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq\n' | ed -s dists/test2/Release
dodiff dists/test1/Release.expected dists/test1/Release || exit 1
dodiff dists/test2/Release.expected dists/test2/Release || exit 1

PACKAGE=simple EPOCH="" VERSION=1 REVISION="" SECTION="stupid/base" genpackage.sh
checknolog log1
if test -n "$TESTNEWFILESDB" ; then
	dodo test ! -f db/files.db
fi
testrun - -b . include test1 test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/stupid"
-v2*=Created directory "./pool/stupid/s"
-v2*=Created directory "./pool/stupid/s/simple"
-e1*=db: 'pool/stupid/s/simple/simple-addons_1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple-addons_1_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/s/simple/simple_1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/s/simple/simple_1.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1.dsc' added to checksums.db(pool).
-e1*=db: 'pool/stupid/s/simple/simple_1_source+${FAKEARCHITECTURE}+all.changes' added to files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1_source+${FAKEARCHITECTURE}+all.changes' added to checksums.db(pool).
-d1*=db: 'simple-addons' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'simple' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'simple' added to packages.db(test1|stupid|source).
-d1*=db: 'simple' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
echo returned: $?
checklog log1 << EOF
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} simple-addons 1
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} simple 1
DATESTR add test1 dsc stupid source simple 1
EOF

PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=0.9-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/base" genpackage.sh
testrun - -b . include test1 test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./pool/ugly"
-v2*=Created directory "./pool/ugly/b"
-v2*=Created directory "./pool/ugly/b/bloat+-0a9z.app"
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' added to checksums.db(pool).
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|source).
-d1*=db: 'bloat+-0a9z.app' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
EOF
echo returned: $?
checklog log1 <<EOF
DATESTR add test1 deb ugly ${FAKEARCHITECTURE} bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 deb ugly ${FAKEARCHITECTURE} bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 dsc ugly source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF

testrun - -b . -Tdsc remove test1 simple 3<<EOF
stdout
-v1*=removing 'simple' from 'test1|stupid|source'...
-d1*=db: 'simple' removed from packages.db(test1|stupid|source).
=[tracking_get test1 simple 1]
=[tracking_get found test1 simple 1]
=[tracking_save test1 simple 1]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 dsc stupid source simple 1
EOF
testrun - -b . -Tdeb remove test1 bloat+-0a9z.app 3<<EOF
stdout
-v1*=removing 'bloat+-0a9z.app' from 'test1|ugly|${FAKEARCHITECTURE}'...
-d1*=db: 'bloat+-0a9z.app' removed from packages.db(test1|ugly|${FAKEARCHITECTURE}).
=[tracking_get test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_get found test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_save test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb ugly ${FAKEARCHITECTURE} bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -A source remove test1 bloat+-0a9z.app 3<<EOF
stdout
-v1*=removing 'bloat+-0a9z.app' from 'test1|ugly|source'...
-d1*=db: 'bloat+-0a9z.app' removed from packages.db(test1|ugly|source).
=[tracking_get test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_get found test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_save test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 dsc ugly source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -A ${FAKEARCHITECTURE} remove test1 simple 3<<EOF
stdout
-v1*=removing 'simple' from 'test1|stupid|${FAKEARCHITECTURE}'...
-d1*=db: 'simple' removed from packages.db(test1|stupid|${FAKEARCHITECTURE}).
=[tracking_get test1 simple 1]
=[tracking_get found test1 simple 1]
=[tracking_save test1 simple 1]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb stupid ${FAKEARCHITECTURE} simple 1
EOF
testrun - -b . -C ugly remove test1 bloat+-0a9z.app-addons 3<<EOF
stdout
-v1*=removing 'bloat+-0a9z.app-addons' from 'test1|ugly|${FAKEARCHITECTURE}'...
-d1*=db: 'bloat+-0a9z.app-addons' removed from packages.db(test1|ugly|${FAKEARCHITECTURE}).
=[tracking_get test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_get found test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_save test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb ugly ${FAKEARCHITECTURE} bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -C stupid remove test1 simple-addons 3<<EOF
stdout
-v1*=removing 'simple-addons' from 'test1|stupid|${FAKEARCHITECTURE}'...
-d1*=db: 'simple-addons' removed from packages.db(test1|stupid|${FAKEARCHITECTURE}).
=[tracking_get test1 simple 1]
=[tracking_get found test1 simple 1]
=[tracking_save test1 simple 1]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb stupid ${FAKEARCHITECTURE} simple-addons 1
EOF
CURDATE="`TZ=GMT LC_ALL=C date +'%a, %d %b %Y %H:%M:%S +0000'`"
printf '%%g/^Date:/s/Date: .*/Date: normalized/\n%%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq\n' | ed -s dists/test1/Release

dodiff dists/test1/Release.expected dists/test1/Release || exit 1

cat > conf/srcoverride <<END
simple Section ugly/games
simple Priority optional
simple Maintainer simple.source.maintainer
bloat+-0a9z.app Section stupid/X11
bloat+-0a9z.app Priority optional
bloat+-0a9z.app X-addition totally-unsupported
bloat+-0a9z.app Maintainer bloat.source.maintainer
END
cat > conf/binoverride <<END
simple Maintainer simple.maintainer
simple Section ugly/base
simple Priority optional
simple-addons Section ugly/addons
simple-addons Priority optional
simple-addons Maintainer simple.add.maintainer
bloat+-0a9z.app Maintainer bloat.maintainer
bloat+-0a9z.app Section stupid/base
bloat+-0a9z.app Priority optional
bloat+-0a9z.app-addons Section stupid/addons
bloat+-0a9z.app-addons Maintainer bloat.add.maintainer
bloat+-0a9z.app-addons Priority optional
END

testrun - -b . -Tdsc -A source includedsc test2 simple_1.dsc 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=simple_1.dsc: component guessed as 'ugly'
stdout
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'change'
-v2*=Created directory "./pool/ugly/s"
-v2*=Created directory "./pool/ugly/s/simple"
-e1*=db: 'pool/ugly/s/simple/simple_1.dsc' added to files.db(md5sums).
-d1*=db: 'pool/ugly/s/simple/simple_1.dsc' added to checksums.db(pool).
-e1*=db: 'pool/ugly/s/simple/simple_1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/ugly/s/simple/simple_1.tar.gz' added to checksums.db(pool).
-d1*=db: 'simple' added to packages.db(test2|ugly|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
-v6*=  replacing './dists/test2/ugly/source/Sources' (uncompressed,gzipped,script: bzip.example,testhook)
EOF
checklog log2 <<EOF
DATESTR add test2 dsc ugly source simple 1
EOF
testrun - -b . -Tdsc -A source includedsc test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc: component guessed as 'stupid'
stdout
-v2*=Created directory "./pool/stupid/b"
-v2*=Created directory "./pool/stupid/b/bloat+-0a9z.app"
-e1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' added to checksums.db(pool).
-e1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' added to checksums.db(pool).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test2|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*=  replacing './dists/test2/stupid/source/Sources' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'change'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2 <<EOF
DATESTR add test2 dsc stupid source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -Tdeb -A ${FAKEARCHITECTURE} includedeb test2 simple_1_${FAKEARCHITECTURE}.deb 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=simple_1_${FAKEARCHITECTURE}.deb: component guessed as 'ugly'
stdout
-e1*=db: 'pool/ugly/s/simple/simple_1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/ugly/s/simple/simple_1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'simple' added to packages.db(test2|ugly|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'change'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2  <<EOF
DATESTR add test2 deb ugly ${FAKEARCHITECTURE} simple 1
EOF
testrun - -b . -Tdeb -A coal includedeb test2 simple-addons_1_all.deb 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=simple-addons_1_all.deb: component guessed as 'ugly'
stdout
-e1*=db: 'pool/ugly/s/simple/simple-addons_1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/ugly/s/simple/simple-addons_1_all.deb' added to checksums.db(pool).
-d1*=db: 'simple-addons' added to packages.db(test2|ugly|coal).
-v0=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*=  replacing './dists/test2/ugly/binary-coal/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|ugly|source'...
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'change'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2  <<EOF
DATESTR add test2 deb ugly coal simple-addons 1
EOF
testrun - -b . -Tdeb -A ${FAKEARCHITECTURE} includedeb test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb: component guessed as 'stupid'
stdout
-e1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test2|stupid|${FAKEARCHITECTURE}).
-v0=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'change'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2 <<EOF
DATESTR add test2 deb stupid ${FAKEARCHITECTURE} bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -Tdeb -A coal includedeb test2 bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb: component guessed as 'stupid'
stdout
-e1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' added to checksums.db(pool).
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test2|stupid|coal).
-v0=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*=  replacing './dists/test2/stupid/binary-coal/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|stupid|source'...
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'change'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2 <<EOF
DATESTR add test2 deb stupid coal bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
find dists/test2/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^\(Package\|Maintainer\|Section\|Priority\): ' | sort > results
cat >results.expected <<END
dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages.gz:Maintainer: bloat.maintainer
dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages.gz:Package: bloat+-0a9z.app
dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages.gz:Priority: optional
dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages.gz:Section: stupid/base
dists/test2/stupid/binary-coal/Packages.gz:Maintainer: bloat.add.maintainer
dists/test2/stupid/binary-coal/Packages.gz:Package: bloat+-0a9z.app-addons
dists/test2/stupid/binary-coal/Packages.gz:Priority: optional
dists/test2/stupid/binary-coal/Packages.gz:Section: stupid/addons
dists/test2/stupid/source/Sources.gz:Maintainer: bloat.source.maintainer
dists/test2/stupid/source/Sources.gz:Package: bloat+-0a9z.app
dists/test2/stupid/source/Sources.gz:Priority: optional
dists/test2/stupid/source/Sources.gz:Section: stupid/X11
dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages.gz:Maintainer: simple.maintainer
dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages.gz:Package: simple
dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages.gz:Priority: optional
dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages.gz:Section: ugly/base
dists/test2/ugly/binary-coal/Packages.gz:Maintainer: simple.add.maintainer
dists/test2/ugly/binary-coal/Packages.gz:Package: simple-addons
dists/test2/ugly/binary-coal/Packages.gz:Priority: optional
dists/test2/ugly/binary-coal/Packages.gz:Section: ugly/addons
dists/test2/ugly/source/Sources.gz:Maintainer: simple.source.maintainer
dists/test2/ugly/source/Sources.gz:Package: simple
dists/test2/ugly/source/Sources.gz:Priority: optional
dists/test2/ugly/source/Sources.gz:Section: ugly/games
END
dodiff results.expected results
rm results
testout "" -b . listfilter test2 'Source(==simple)|(!Source,Package(==simple))'
ls -la results
cat > results.expected << END
test2|ugly|${FAKEARCHITECTURE}: simple 1
test2|ugly|coal: simple-addons 1
test2|ugly|source: simple 1
END
dodiff results.expected results
testout "" -b . listfilter test2 'Source(==bloat+-0a9z.app)|(!Source,Package(==bloat+-0a9z.app))'
cat > results.expected << END
test2|stupid|${FAKEARCHITECTURE}: bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
test2|stupid|coal: bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
test2|stupid|source: bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
END
dodiff results.expected results

cat >conf/updates <<END
Name: Test2toTest1
Method: copy:$WORKDIR
Suite: test2
Architectures: coal>${FAKEARCHITECTURE} ${FAKEARCHITECTURE} source
FilterFormula: Priority(==optional),Package(>=alpha),Package(<=zeta)
FilterList: error list
ListHook: /bin/cp
END

cat >conf/list <<END
simple-addons		install
bloat+-0a9z.app 	install
simple			install
bloat+-0a9z.app-addons	install
END

cp dists/test2/Release dists/test2/Release.safe
ed -s dists/test2/Release <<EOF
g/stupid.source.Sources/s/^ ................................ / ffffffffffffffffffffffffffffffff /
w
q
EOF

testrun - -b . update test1 3<<EOF
returns 254
stderr
*=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/Release'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/ugly/source/Sources.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/ugly/source/Sources.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/ugly/binary-coal/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/ugly/binary-coal/Packages.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
*=Wrong checksum during receive of 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz':
*=md5 expected: ffffffffffffffffffffffffffffffff, got: $(md5 dists/test2/stupid/source/Sources.gz)
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/binary-coal/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/binary-coal/Packages.gz'
-v0*=There have been errors!
stdout
-v2*=Created directory "./lists"
EOF

cp dists/test2/Release.safe dists/test2/Release
ed -s dists/test2/Release <<EOF
g/stupid.source.Sources/s/^ ........................................ / 1111111111111111111111111111111111111111 /
w
q
EOF

testrun - -b . update test1 3<<EOF
returns 254
stderr
*=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/Release'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
*=Wrong checksum during receive of 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz':
*=sha1 expected: 1111111111111111111111111111111111111111, got: $(sha1 dists/test2/stupid/source/Sources.gz)
-v0*=There have been errors!
stdout
EOF

cp dists/test2/Release.safe dists/test2/Release
ed -s dists/test2/Release <<EOF
g/stupid.source.Sources/s/^ ................................................................ / 9999999999999999999999999999999999999999999999999999999999999999 /
w
q
EOF

testrun - -b . update test1 3<<EOF
returns 254
stderr
*=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/Release'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
*=Wrong checksum during receive of 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz':
*=sha256 expected: 9999999999999999999999999999999999999999999999999999999999999999, got: $(sha256 dists/test2/stupid/source/Sources.gz)
-v0*=There have been errors!
stdout
EOF

cp conf/updates conf/updates.safe
cat >> conf/updates <<EOF
IgnoreHashes: sha2
EOF

testrun - -b . update test1 3<<EOF
returns 248
stderr
*=Error parsing config file ./conf/updates, line 8, column 15:
*=Unknown flag in IgnoreHashes header.(allowed values: sha1 and sha256)
*=To ignore unknown fields use --ignore=unknownfield
-v0*=There have been errors!
stdout
EOF

cp conf/updates.safe conf/updates
cat >> conf/updates <<EOF
IgnoreHashes: sha1
EOF

testrun - -b . update test1 3<<EOF
returns 254
stderr
*=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/Release'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
*=Wrong checksum during receive of 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz':
*=sha256 expected: 9999999999999999999999999999999999999999999999999999999999999999, got: $(sha256 dists/test2/stupid/source/Sources.gz)
-v0*=There have been errors!
stdout
EOF

cp conf/updates.safe conf/updates
cat >> conf/updates <<EOF
IgnoreHashes: sha256
EOF

testrun - -b . update test1 3<<EOF
stderr
*=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/Release'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_dsc_ugly_source' './lists/test1_Test2toTest1_dsc_ugly_source_changed'
-v6*=Listhook successfully returned!
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_ugly_${FAKEARCHITECTURE}' './lists/test1_Test2toTest1_deb_ugly_${FAKEARCHITECTURE}_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_ugly_coal' './lists/test1_Test2toTest1_deb_ugly_coal_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_dsc_stupid_source' './lists/test1_Test2toTest1_dsc_stupid_source_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_stupid_${FAKEARCHITECTURE}' './lists/test1_Test2toTest1_deb_stupid_${FAKEARCHITECTURE}_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_stupid_coal' './lists/test1_Test2toTest1_deb_stupid_coal_changed'
stdout
-v0*=Calculating packages to get...
-v3*=  processing updates for 'test1|ugly|source'
-v5*=  reading './lists/test1_Test2toTest1_dsc_ugly_source_changed'
-v3*=  processing updates for 'test1|ugly|${FAKEARCHITECTURE}'
-v5*=  reading './lists/test1_Test2toTest1_deb_ugly_${FAKEARCHITECTURE}_changed'
-v5*=  reading './lists/test1_Test2toTest1_deb_ugly_coal_changed'
-v3*=  processing updates for 'test1|stupid|source'
-v5*=  reading './lists/test1_Test2toTest1_dsc_stupid_source_changed'
-v3*=  processing updates for 'test1|stupid|${FAKEARCHITECTURE}'
-v5*=  reading './lists/test1_Test2toTest1_deb_stupid_${FAKEARCHITECTURE}_changed'
-v5*=  reading './lists/test1_Test2toTest1_deb_stupid_coal_changed'
-v0*=Getting packages...
-v1=Freeing some memory...
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'simple' added to packages.db(test1|ugly|source).
-d1*=db: 'simple' added to packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'simple-addons' added to packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|stupid|source).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
EOF

mv dists/test2/Release.safe dists/test2/Release
mv conf/updates.safe conf/updates

checklog log1 <<EOF
DATESTR add test1 dsc ugly source simple 1
DATESTR add test1 deb ugly ${FAKEARCHITECTURE} simple 1
DATESTR add test1 deb ugly ${FAKEARCHITECTURE} simple-addons 1
DATESTR add test1 dsc stupid source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
checknolog log1
checknolog log2
testrun - -b . update test1 3<<EOF
=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/Release'
*=Nothing to do found. (Use --noskipold to force processing)
EOF
checklog log1 < /dev/null
checknolog log2
testrun - --nolistsdownload -b . update test1 3<<EOF
-v0*=Ignoring --skipold because of --nolistsdownload
=WARNING: Single-Instance not yet supported!
=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
-v0*=Warning: As --nolistsdownload is given, index files are NOT checked.
-v6*=Called /bin/cp './lists/test1_Test2toTest1_dsc_ugly_source' './lists/test1_Test2toTest1_dsc_ugly_source_changed'
-v6*=Listhook successfully returned!
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_ugly_${FAKEARCHITECTURE}' './lists/test1_Test2toTest1_deb_ugly_${FAKEARCHITECTURE}_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_ugly_coal' './lists/test1_Test2toTest1_deb_ugly_coal_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_dsc_stupid_source' './lists/test1_Test2toTest1_dsc_stupid_source_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_stupid_${FAKEARCHITECTURE}' './lists/test1_Test2toTest1_deb_stupid_${FAKEARCHITECTURE}_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_stupid_coal' './lists/test1_Test2toTest1_deb_stupid_coal_changed'
stdout
-v0*=Calculating packages to get...
-v3*=  processing updates for 'test1|ugly|source'
-v5*=  reading './lists/test1_Test2toTest1_dsc_ugly_source_changed'
-v3*=  processing updates for 'test1|ugly|${FAKEARCHITECTURE}'
-v5*=  reading './lists/test1_Test2toTest1_deb_ugly_${FAKEARCHITECTURE}_changed'
-v5*=  reading './lists/test1_Test2toTest1_deb_ugly_coal_changed'
-v3*=  processing updates for 'test1|stupid|source'
-v5*=  reading './lists/test1_Test2toTest1_dsc_stupid_source_changed'
-v3*=  processing updates for 'test1|stupid|${FAKEARCHITECTURE}'
-v5*=  reading './lists/test1_Test2toTest1_deb_stupid_${FAKEARCHITECTURE}_changed'
-v5*=  reading './lists/test1_Test2toTest1_deb_stupid_coal_changed'
-v0*=Getting packages...
-v1=Freeing some memory...
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
EOF
checklog log1 < /dev/null
checknolog log2

find dists/test2/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' | sed -e 's/test2/test1/' -e "s/coal/${FAKEARCHITECTURE}/" | sort > test2
find dists/test1/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' | sort > test1
dodiff test2 test1

testrun - -b . check test1 test2 3<<EOF
stdout
-v1*=Checking test2...
-x1*=Checking packages in 'test2|stupid|${FAKEARCHITECTURE}'...
-x1*=Checking packages in 'test2|stupid|coal'...
-x1*=Checking packages in 'test2|stupid|source'...
-x1*=Checking packages in 'test2|ugly|${FAKEARCHITECTURE}'...
-x1*=Checking packages in 'test2|ugly|coal'...
-x1*=Checking packages in 'test2|ugly|source'...
-v1*=Checking test1...
-x1*=Checking packages in 'test1|stupid|${FAKEARCHITECTURE}'...
-x1*=Checking packages in 'test1|stupid|source'...
-x1*=Checking packages in 'test1|ugly|${FAKEARCHITECTURE}'...
-x1*=Checking packages in 'test1|ugly|source'...
EOF
testrun "" -b . checkpool
testrun - -b . rereference test1 test2 3<<EOF
stdout
-v1*=Referencing test2...
-v2=Rereferencing test2|stupid|${FAKEARCHITECTURE}...
-v2=Rereferencing test2|stupid|coal...
-v2=Rereferencing test2|stupid|source...
-v2=Rereferencing test2|ugly|${FAKEARCHITECTURE}...
-v2=Rereferencing test2|ugly|coal...
-v2=Rereferencing test2|ugly|source...
-v3*=Unlocking depencies of test2|stupid|${FAKEARCHITECTURE}...
-v3*=Referencing test2|stupid|${FAKEARCHITECTURE}...
-v3*=Unlocking depencies of test2|stupid|coal...
-v3*=Referencing test2|stupid|coal...
-v3*=Unlocking depencies of test2|stupid|source...
-v3*=Referencing test2|stupid|source...
-v3*=Unlocking depencies of test2|ugly|${FAKEARCHITECTURE}...
-v3*=Referencing test2|ugly|${FAKEARCHITECTURE}...
-v3*=Unlocking depencies of test2|ugly|coal...
-v3*=Referencing test2|ugly|coal...
-v3*=Unlocking depencies of test2|ugly|source...
-v3*=Referencing test2|ugly|source...
-v1*=Referencing test1...
-v2=Rereferencing test1|stupid|${FAKEARCHITECTURE}...
-v2=Rereferencing test1|stupid|source...
-v2=Rereferencing test1|ugly|${FAKEARCHITECTURE}...
-v2=Rereferencing test1|ugly|source...
-v3*=Unlocking depencies of test1|stupid|${FAKEARCHITECTURE}...
-v3*=Referencing test1|stupid|${FAKEARCHITECTURE}...
-v3*=Unlocking depencies of test1|stupid|source...
-v3*=Referencing test1|stupid|source...
-v3*=Unlocking depencies of test1|ugly|${FAKEARCHITECTURE}...
-v3*=Referencing test1|ugly|${FAKEARCHITECTURE}...
-v3*=Unlocking depencies of test1|ugly|source...
-v3*=Referencing test1|ugly|source...
EOF
testrun - -b . check test1 test2 3<<EOF
stdout
-v1*=Checking test1...
-x1*=Checking packages in 'test2|stupid|${FAKEARCHITECTURE}'...
-x1*=Checking packages in 'test2|stupid|coal'...
-x1*=Checking packages in 'test2|stupid|source'...
-x1*=Checking packages in 'test2|ugly|${FAKEARCHITECTURE}'...
-x1*=Checking packages in 'test2|ugly|coal'...
-x1*=Checking packages in 'test2|ugly|source'...
-v1*=Checking test2...
-x1*=Checking packages in 'test1|stupid|${FAKEARCHITECTURE}'...
-x1*=Checking packages in 'test1|stupid|source'...
-x1*=Checking packages in 'test1|ugly|${FAKEARCHITECTURE}'...
-x1*=Checking packages in 'test1|ugly|source'...
EOF

testout "" -b . dumptracks
cat >results.expected <<END
Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple-addons_1_all.deb a 0
 pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb b 0
 pool/stupid/s/simple/simple_1.dsc s 0
 pool/stupid/s/simple/simple_1.tar.gz s 0
 pool/stupid/s/simple/simple_1_source+${FAKEARCHITECTURE}+all.changes c 0

END
dodiff results.expected results

testout "" -b . dumpunreferenced
dodiff results.empty results
testrun - -b . removealltracks test2 test1 3<<EOF
stdout
stderr
*=Error: Requested removing of all tracks of distribution 'test1',
*=which still has tracking enabled. Use --delete to delete anyway.
-v0*=There have been errors!
returns 255
EOF
testrun - --delete -b . removealltracks test2 test1 3<<EOF
stdout
-v0*=Deleting all tracks for test2...
-v0*=Deleting all tracks for test1...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/stupid/s/simple/simple-addons_1_all.deb
-e1*=db: 'pool/stupid/s/simple/simple-addons_1_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple-addons_1_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1.dsc
-e1*=db: 'pool/stupid/s/simple/simple_1.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1.tar.gz
-e1*=db: 'pool/stupid/s/simple/simple_1.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1_source+${FAKEARCHITECTURE}+all.changes
-e1*=db: 'pool/stupid/s/simple/simple_1_source+${FAKEARCHITECTURE}+all.changes' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1_source+${FAKEARCHITECTURE}+all.changes' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/stupid/s/simple
-v1*=removed now empty directory ./pool/stupid/s
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/ugly/b/bloat+-0a9z.app
-v1*=removed now empty directory ./pool/ugly/b
EOF
echo returned: $?
testrun - -b . include test1 test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./pool/ugly/b"
-v2*=Created directory "./pool/ugly/b/bloat+-0a9z.app"
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' added to checksums.db(pool).
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|source).
-d1*=db: 'bloat+-0a9z.app' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
EOF
checklog log1 <<EOF
DATESTR add test1 deb ugly ${FAKEARCHITECTURE} bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 deb ugly ${FAKEARCHITECTURE} bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 dsc ugly source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
echo returned: $?
OUTPUT=test2.changes PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=9.0-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/extra" genpackage.sh
testrun - -b . include test1 test2.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc' added to checksums.db(pool).
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' added to files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' added to checksums.db(pool).
-d1*=db: 'bloat+-0a9z.app-addons' removed from packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app' removed from packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'bloat+-0a9z.app' removed from packages.db(test1|ugly|source).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|source).
-d1*=db: 'bloat+-0a9z.app' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
-v0*=Deleting files no longer referenced...
EOF
echo returned: $?
checklog log1 <<EOF
DATESTR replace test1 deb ugly ${FAKEARCHITECTURE} bloat+-0a9z.app-addons 99:9.0-A:Z+a:z-0+aA.9zZ 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR replace test1 deb ugly ${FAKEARCHITECTURE} bloat+-0a9z.app 99:9.0-A:Z+a:z-0+aA.9zZ 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR replace test1 dsc ugly source bloat+-0a9z.app 99:9.0-A:Z+a:z-0+aA.9zZ 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -S sectiontest -P prioritytest includedeb test1 simple_1_${FAKEARCHITECTURE}.deb 3<<EOF
stderr
-v1*=simple_1_${FAKEARCHITECTURE}.deb: component guessed as 'stupid'
stdout
-v2*=Created directory "./pool/stupid/s"
-v2*=Created directory "./pool/stupid/s/simple"
-e1*=db: 'pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-d1*=db: 'simple' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'simple' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
echo returned: $?
dodo zgrep '^Section: sectiontest' dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages.gz
dodo zgrep '^Priority: prioritytest' dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages.gz
checklog log1 <<EOF
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} simple 1
EOF
testrun - -b . -S sectiontest -P prioritytest includedsc test1 simple_1.dsc 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1*=simple_1.dsc: component guessed as 'stupid'
stdout
-e1*=db: 'pool/stupid/s/simple/simple_1.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1.dsc' added to checksums.db(pool).
-e1*=db: 'pool/stupid/s/simple/simple_1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1.tar.gz' added to checksums.db(pool).
-d1*=db: 'simple' added to packages.db(test1|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
echo returned: $?
dodo zgrep '^Section: sectiontest' dists/test1/stupid/source/Sources.gz
dodo zgrep '^Priority: prioritytest' dists/test1/stupid/source/Sources.gz
checklog log1 <<EOF
DATESTR add test1 dsc stupid source simple 1
EOF

testout "" -b . dumptracks
cat >results.expected <<END
Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

END
dodiff results.expected results
testout "" -b . dumpunreferenced
dodiff results.empty results

echo "now testing .orig.tar.gz handling"
tar -czf test_1.orig.tar.gz test.changes
PACKAGE=test EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh -sd
testrun - -b . include test1 test.changes 3<<EOF
returns 249
stderr
-v0=Data seems not to be signed trying to use directly...
*=Unable to find pool/stupid/t/test/test_1.orig.tar.gz needed by test_1-2.dsc!
*=Perhaps you forgot to give dpkg-buildpackage the -sa option,
*= or you could try --ignore=missingfile to guess possible files to use.
-v0*=There have been errors!
stdout
-v2*=Created directory "./pool/stupid/t"
-v2*=Created directory "./pool/stupid/t/test"
-e1*=db: 'pool/stupid/t/test/test-addons_1-2_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test-addons_1-2_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/test/test_1-2.diff.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2.diff.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/test/test_1-2.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2.dsc' added to checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test-addons_1-2_all.deb
-e1*=db: 'pool/stupid/t/test/test-addons_1-2_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test-addons_1-2_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2.diff.gz
-e1*=db: 'pool/stupid/t/test/test_1-2.diff.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2.diff.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2.dsc
-e1*=db: 'pool/stupid/t/test/test_1-2.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2.dsc' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/stupid/t/test
-v1*=removed now empty directory ./pool/stupid/t
EOF
checknolog log1
checknolog log2
testrun - -b . --ignore=missingfile include test1 test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
*=Unable to find pool/stupid/t/test/test_1.orig.tar.gz!
*=Perhaps you forgot to give dpkg-buildpackage the -sa option.
*=--ignore=missingfile was given, searching for file...
stdout
-v2*=Created directory "./pool/stupid/t"
-v2*=Created directory "./pool/stupid/t/test"
-e1*=db: 'pool/stupid/t/test/test-addons_1-2_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test-addons_1-2_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/test/test_1-2.diff.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2.diff.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/test/test_1-2.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2.dsc' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/test/test_1.orig.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1.orig.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/test/test_1-2_source+${FAKEARCHITECTURE}+all.changes' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2_source+${FAKEARCHITECTURE}+all.changes' added to checksums.db(pool).
-d1*=db: 'test-addons' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'test' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'test' added to packages.db(test1|stupid|source).
-d1*=db: 'test' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
checklog log1 <<EOF
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} test-addons 1-2
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} test 1-2
DATESTR add test1 dsc stupid source test 1-2
EOF
dodo zgrep test_1-2.dsc dists/test1/stupid/source/Sources.gz

tar -czf testb_2.orig.tar.gz test.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-2" SECTION="stupid/base" genpackage.sh -sa
testrun - -b . include test1 test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./pool/stupid/t/testb"
-e1*=db: 'pool/stupid/t/testb/testb-addons_2-2_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb-addons_2-2_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_2-2_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-2_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_2-2.diff.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-2.diff.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_2-2.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-2.dsc' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_2.orig.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2.orig.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_1:2-2_source+${FAKEARCHITECTURE}+all.changes' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_1:2-2_source+${FAKEARCHITECTURE}+all.changes' added to checksums.db(pool).
-d1*=db: 'testb-addons' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'testb' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'testb' added to packages.db(test1|stupid|source).
-d1*=db: 'testb' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
checklog log1 <<EOF
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} testb-addons 1:2-2
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} testb 1:2-2
DATESTR add test1 dsc stupid source testb 1:2-2
EOF
dodo zgrep testb_2-2.dsc dists/test1/stupid/source/Sources.gz
rm test2.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-3" SECTION="stupid/base" OUTPUT="test2.changes" genpackage.sh -sd
testrun - -b . include test1 test2.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-e1*=db: 'pool/stupid/t/testb/testb-addons_2-3_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb-addons_2-3_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_2-3.diff.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-3.diff.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_2-3.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-3.dsc' added to checksums.db(pool).
-e1*=db: 'pool/stupid/t/testb/testb_1:2-3_source+${FAKEARCHITECTURE}+all.changes' added to files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_1:2-3_source+${FAKEARCHITECTURE}+all.changes' added to checksums.db(pool).
-d1*=db: 'testb-addons' removed from packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'testb-addons' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'testb' removed from packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'testb' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'testb' removed from packages.db(test1|stupid|source).
-d1*=db: 'testb' added to packages.db(test1|stupid|source).
-d1*=db: 'testb' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR replace test1 deb stupid ${FAKEARCHITECTURE} testb-addons 1:2-3 1:2-2
DATESTR replace test1 deb stupid ${FAKEARCHITECTURE} testb 1:2-3 1:2-2
DATESTR replace test1 dsc stupid source testb 1:2-3 1:2-2
EOF
dodo zgrep testb_2-3.dsc dists/test1/stupid/source/Sources.gz

testout "" -b . dumpunreferenced
dodiff results.empty results

echo "now testing some error messages:"
PACKAGE=4test EPOCH="1:" VERSION=b.1 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun -  -b . include test1 test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./pool/stupid/4"
-v2*=Created directory "./pool/stupid/4/4test"
-e1*=db: 'pool/stupid/4/4test/4test-addons_b.1-1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test-addons_b.1-1_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/4/4test/4test_b.1-1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_b.1-1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/4/4test/4test_b.1-1.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_b.1-1.dsc' added to checksums.db(pool).
-e1*=db: 'pool/stupid/4/4test/4test_1:b.1-1_source+${FAKEARCHITECTURE}+all.changes' added to files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_1:b.1-1_source+${FAKEARCHITECTURE}+all.changes' added to checksums.db(pool).
-d1*=db: '4test-addons' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: '4test' added to packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: '4test' added to packages.db(test1|stupid|source).
-d1*=db: '4test' added to tracking.db(test1).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
checklog log1 <<EOF
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} 4test-addons 1:b.1-1
DATESTR add test1 deb stupid ${FAKEARCHITECTURE} 4test 1:b.1-1
DATESTR add test1 dsc stupid source 4test 1:b.1-1
EOF

cat >includeerror.rules <<EOF
returns 255
stderr
-v0*=There have been errors!
*=Error: Too few arguments for command 'include'!
*=Syntax: reprepro [--delete] include <distribution> <.changes-file>
EOF
testrun includeerror -b . include unknown 3<<EOF
testrun includeerror -b . include unknown test.changes test2.changes
testrun - -b . include unknown test.changes 3<<EOF
stderr
-v0*=There have been errors!
*=No distribution definition of 'unknown' found in './conf/distributions'!
returns 249
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results

testout "" -b . dumptracks
# TODO: check those if they are really expected...
cat > results.expected <<EOF
Distribution: test1
Source: 4test
Version: 1:b.1-1
Files:
 pool/stupid/4/4test/4test-addons_b.1-1_all.deb a 1
 pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/4/4test/4test_b.1-1.dsc s 1
 pool/stupid/4/4test/4test_b.1-1.tar.gz s 1
 pool/stupid/4/4test/4test_1:b.1-1_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

Distribution: test1
Source: test
Version: 1-2
Files:
 pool/stupid/t/test/test-addons_1-2_all.deb a 1
 pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/t/test/test_1-2.dsc s 1
 pool/stupid/t/test/test_1.orig.tar.gz s 1
 pool/stupid/t/test/test_1-2.diff.gz s 1
 pool/stupid/t/test/test_1-2_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: testb
Version: 1:2-2
Files:
 pool/stupid/t/testb/testb-addons_2-2_all.deb a 0
 pool/stupid/t/testb/testb_2-2_${FAKEARCHITECTURE}.deb b 0
 pool/stupid/t/testb/testb_2-2.dsc s 0
 pool/stupid/t/testb/testb_2.orig.tar.gz s 0
 pool/stupid/t/testb/testb_2-2.diff.gz s 0
 pool/stupid/t/testb/testb_1:2-2_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: testb
Version: 1:2-3
Files:
 pool/stupid/t/testb/testb-addons_2-3_all.deb a 1
 pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/t/testb/testb_2-3.dsc s 1
 pool/stupid/t/testb/testb_2.orig.tar.gz s 1
 pool/stupid/t/testb/testb_2-3.diff.gz s 1
 pool/stupid/t/testb/testb_1:2-3_source+${FAKEARCHITECTURE}+all.changes c 0

EOF
dodiff results.expected results
testrun -  -b . tidytracks 3<<EOF
stdout
-v0*=Looking for old tracks in test1...
EOF
testout "" -b . dumptracks
dodiff results.expected results
sed -i -e 's/^Tracking: keep/Tracking: all/' conf/distributions
testrun -  -b . tidytracks 3<<EOF
stdout
-v0*=Looking for old tracks in test1...
-d1*=db: 'testb' '1:2-2' removed from tracking.db(test1).
-d1*=db: 'bloat+-0a9z.app' '99:0.9-A:Z+a:z-0+aA.9zZ' removed from tracking.db(test1).
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb-addons_2-2_all.deb
-e1*=db: 'pool/stupid/t/testb/testb-addons_2-2_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb-addons_2-2_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-2_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/t/testb/testb_2-2_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-2_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-2.dsc
-e1*=db: 'pool/stupid/t/testb/testb_2-2.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-2.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-2.diff.gz
-e1*=db: 'pool/stupid/t/testb/testb_2-2.diff.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-2.diff.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_1:2-2_source+${FAKEARCHITECTURE}+all.changes
-e1*=db: 'pool/stupid/t/testb/testb_1:2-2_source+${FAKEARCHITECTURE}+all.changes' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_1:2-2_source+${FAKEARCHITECTURE}+all.changes' removed from checksums.db(pool).
EOF
cp db/tracking.db db/saved2tracking.db
cp db/references.db db/saved2references.db
testout "" -b . dumpunreferenced
dodiff results.empty results
testout "" -b . dumptracks
cat > results.expected <<EOF
Distribution: test1
Source: 4test
Version: 1:b.1-1
Files:
 pool/stupid/4/4test/4test-addons_b.1-1_all.deb a 1
 pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/4/4test/4test_b.1-1.dsc s 1
 pool/stupid/4/4test/4test_b.1-1.tar.gz s 1
 pool/stupid/4/4test/4test_1:b.1-1_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

Distribution: test1
Source: test
Version: 1-2
Files:
 pool/stupid/t/test/test-addons_1-2_all.deb a 1
 pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/t/test/test_1-2.dsc s 1
 pool/stupid/t/test/test_1.orig.tar.gz s 1
 pool/stupid/t/test/test_1-2.diff.gz s 1
 pool/stupid/t/test/test_1-2_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: testb
Version: 1:2-3
Files:
 pool/stupid/t/testb/testb-addons_2-3_all.deb a 1
 pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/t/testb/testb_2-3.dsc s 1
 pool/stupid/t/testb/testb_2.orig.tar.gz s 1
 pool/stupid/t/testb/testb_2-3.diff.gz s 1
 pool/stupid/t/testb/testb_1:2-3_source+${FAKEARCHITECTURE}+all.changes c 0

EOF
dodiff results.expected results
sed -i -e 's/^Tracking: all/Tracking: minimal/' conf/distributions
testrun -  -b . tidytracks 3<<EOF
stdout
-v0*=Looking for old tracks in test1...
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
testout "" -b . dumptracks
dodiff results.expected results
sed -i -e 's/^Tracking: minimal includechanges/Tracking: minimal/' conf/distributions
testrun -  -b . tidytracks 3<<EOF
stdout
-v0*=Looking for old tracks in test1...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/stupid/4/4test/4test_1:b.1-1_source+${FAKEARCHITECTURE}+all.changes
-e1*=db: 'pool/stupid/4/4test/4test_1:b.1-1_source+${FAKEARCHITECTURE}+all.changes' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_1:b.1-1_source+${FAKEARCHITECTURE}+all.changes' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2_source+${FAKEARCHITECTURE}+all.changes
-e1*=db: 'pool/stupid/t/test/test_1-2_source+${FAKEARCHITECTURE}+all.changes' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2_source+${FAKEARCHITECTURE}+all.changes' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_1:2-3_source+${FAKEARCHITECTURE}+all.changes
-e1*=db: 'pool/stupid/t/testb/testb_1:2-3_source+${FAKEARCHITECTURE}+all.changes' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_1:2-3_source+${FAKEARCHITECTURE}+all.changes' removed from checksums.db(pool).
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
testout "" -b . dumptracks
cat > results.expected <<EOF
Distribution: test1
Source: 4test
Version: 1:b.1-1
Files:
 pool/stupid/4/4test/4test-addons_b.1-1_all.deb a 1
 pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/4/4test/4test_b.1-1.dsc s 1
 pool/stupid/4/4test/4test_b.1-1.tar.gz s 1

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

Distribution: test1
Source: test
Version: 1-2
Files:
 pool/stupid/t/test/test-addons_1-2_all.deb a 1
 pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/t/test/test_1-2.dsc s 1
 pool/stupid/t/test/test_1.orig.tar.gz s 1
 pool/stupid/t/test/test_1-2.diff.gz s 1

Distribution: test1
Source: testb
Version: 1:2-3
Files:
 pool/stupid/t/testb/testb-addons_2-3_all.deb a 1
 pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/t/testb/testb_2-3.dsc s 1
 pool/stupid/t/testb/testb_2.orig.tar.gz s 1
 pool/stupid/t/testb/testb_2-3.diff.gz s 1

EOF
dodiff results.expected results
testrun -  -b . tidytracks 3<<EOF
stdout
-v0*=Looking for old tracks in test1...
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
# Earlier update rules made this tracking data outdated.
# so copy it, so it can be replayed so that also outdated data
# is tested to be handled correctly.
mv db/tracking.db db/savedtracking.db
mv db/references.db db/savedreferences.db
# Try this with .changes files still listed
mv db/saved2tracking.db db/tracking.db
mv db/saved2references.db db/references.db
sed -i -e 's/^Tracking: minimal/Tracking: minimal includechanges/' conf/distributions
testrun -  -b . retrack 3<<EOF
stdout
-v1*=Chasing test1...
-d1*=db: 'bloat+-0a9z.app' added to tracking.db(test1).
-x1*=  Tracking test1|stupid|${FAKEARCHITECTURE}...
-x1*=  Tracking test1|stupid|source...
-x1*=  Tracking test1|ugly|${FAKEARCHITECTURE}...
-x1*=  Tracking test1|ugly|source...
EOF
testout "" -b . dumptracks
cat > results.expected <<EOF
Distribution: test1
Source: 4test
Version: 1:b.1-1
Files:
 pool/stupid/4/4test/4test-addons_b.1-1_all.deb a 1
 pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/4/4test/4test_b.1-1.dsc s 1
 pool/stupid/4/4test/4test_b.1-1.tar.gz s 1
 pool/stupid/4/4test/4test_1:b.1-1_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 1

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1
 pool/ugly/s/simple/simple_1_${FAKEARCHITECTURE}.deb b 1
 pool/ugly/s/simple/simple-addons_1_all.deb a 1
 pool/ugly/s/simple/simple_1.dsc s 1
 pool/ugly/s/simple/simple_1.tar.gz s 1

Distribution: test1
Source: test
Version: 1-2
Files:
 pool/stupid/t/test/test-addons_1-2_all.deb a 1
 pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/t/test/test_1-2.dsc s 1
 pool/stupid/t/test/test_1.orig.tar.gz s 1
 pool/stupid/t/test/test_1-2.diff.gz s 1
 pool/stupid/t/test/test_1-2_source+${FAKEARCHITECTURE}+all.changes c 0

Distribution: test1
Source: testb
Version: 1:2-3
Files:
 pool/stupid/t/testb/testb-addons_2-3_all.deb a 1
 pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb b 1
 pool/stupid/t/testb/testb_2-3.dsc s 1
 pool/stupid/t/testb/testb_2.orig.tar.gz s 1
 pool/stupid/t/testb/testb_2-3.diff.gz s 1
 pool/stupid/t/testb/testb_1:2-3_source+${FAKEARCHITECTURE}+all.changes c 0

EOF
dodiff results.expected results

testout "" -b . dumpunreferenced
dodiff results.empty results
testout ""  -b . dumpreferences
cp results results.expected
testrun - -b . rereference 3<<EOF
stdout
-v1*=Referencing test1...
-v2*=Unlocking depencies of test1|stupid|${FAKEARCHITECTURE}...
=Rereferencing test1|stupid|${FAKEARCHITECTURE}...
-v2*=Referencing test1|stupid|${FAKEARCHITECTURE}...
-v2*=Unlocking depencies of test1|stupid|source...
=Rereferencing test1|stupid|source...
-v2*=Referencing test1|stupid|source...
-v2*=Unlocking depencies of test1|ugly|${FAKEARCHITECTURE}...
=Rereferencing test1|ugly|${FAKEARCHITECTURE}...
-v2*=Referencing test1|ugly|${FAKEARCHITECTURE}...
-v2*=Unlocking depencies of test1|ugly|source...
=Rereferencing test1|ugly|source...
-v2*=Referencing test1|ugly|source...
-v1*=Referencing test2...
-v2*=Unlocking depencies of test2|stupid|${FAKEARCHITECTURE}...
=Rereferencing test2|stupid|${FAKEARCHITECTURE}...
-v2*=Referencing test2|stupid|${FAKEARCHITECTURE}...
-v2*=Unlocking depencies of test2|stupid|coal...
=Rereferencing test2|stupid|coal...
-v2*=Referencing test2|stupid|coal...
-v2*=Unlocking depencies of test2|stupid|source...
=Rereferencing test2|stupid|source...
-v2*=Referencing test2|stupid|source...
-v2*=Unlocking depencies of test2|ugly|${FAKEARCHITECTURE}...
=Rereferencing test2|ugly|${FAKEARCHITECTURE}...
-v2*=Referencing test2|ugly|${FAKEARCHITECTURE}...
-v2*=Unlocking depencies of test2|ugly|coal...
=Rereferencing test2|ugly|coal...
-v2*=Referencing test2|ugly|coal...
-v2*=Unlocking depencies of test2|ugly|source...
=Rereferencing test2|ugly|source...
-v2*=Referencing test2|ugly|source...
EOF
testout ""  -b . dumpreferences
dodiff results results.expected
rm db/references.db
testrun - -b . rereference 3<<EOF
stdout
-v1*=Referencing test1...
-v2*=Unlocking depencies of test1|stupid|${FAKEARCHITECTURE}...
=Rereferencing test1|stupid|${FAKEARCHITECTURE}...
-v2*=Referencing test1|stupid|${FAKEARCHITECTURE}...
-v2*=Unlocking depencies of test1|stupid|source...
=Rereferencing test1|stupid|source...
-v2*=Referencing test1|stupid|source...
-v2*=Unlocking depencies of test1|ugly|${FAKEARCHITECTURE}...
=Rereferencing test1|ugly|${FAKEARCHITECTURE}...
-v2*=Referencing test1|ugly|${FAKEARCHITECTURE}...
-v2*=Unlocking depencies of test1|ugly|source...
=Rereferencing test1|ugly|source...
-v2*=Referencing test1|ugly|source...
-v1*=Referencing test2...
-v2*=Unlocking depencies of test2|stupid|${FAKEARCHITECTURE}...
=Rereferencing test2|stupid|${FAKEARCHITECTURE}...
-v2*=Referencing test2|stupid|${FAKEARCHITECTURE}...
-v2*=Unlocking depencies of test2|stupid|coal...
=Rereferencing test2|stupid|coal...
-v2*=Referencing test2|stupid|coal...
-v2*=Unlocking depencies of test2|stupid|source...
=Rereferencing test2|stupid|source...
-v2*=Referencing test2|stupid|source...
-v2*=Unlocking depencies of test2|ugly|${FAKEARCHITECTURE}...
=Rereferencing test2|ugly|${FAKEARCHITECTURE}...
-v2*=Referencing test2|ugly|${FAKEARCHITECTURE}...
-v2*=Unlocking depencies of test2|ugly|coal...
=Rereferencing test2|ugly|coal...
-v2*=Referencing test2|ugly|coal...
-v2*=Unlocking depencies of test2|ugly|source...
=Rereferencing test2|ugly|source...
-v2*=Referencing test2|ugly|source...
EOF
testout ""  -b . dumpreferences
dodiff results results.expected
testout ""  -b . dumpreferences
dodiff results.expected results

sed -i -e 's/^Tracking: minimal/Tracking: keep includechanges/' conf/distributions
mv db/savedtracking.db db/tracking.db
mv db/savedreferences.db db/references.db

mkdir conf2
testrun - -b . --confdir conf2 update 3<<EOF
returns 254
stderr
*=Error opening config file 'conf2/distributions': No such file or directory(2)
=(Have you forgotten to specify a basedir by -b?
=To only set the conf/ dir use --confdir)
-v0*=There have been errors!
EOF
touch conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
returns 249
stderr
*=No distribution definitions found in conf2/distributions!
-v0*=There have been errors!
EOF
echo 'Codename: foo' > conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
stderr
*=Error parsing config file conf2/distributions, line 2:
*=Required field 'Architectures' expected (since line 1).
-v0*=There have been errors!
returns 249
EOF
echo "Architectures: ${FAKEARCHITECTURE} fingers" >> conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
*=Error parsing config file conf2/distributions, line 3:
*=Required field 'Components' expected (since line 1).
-v0*=There have been errors!
returns 249
EOF
echo 'Components: unneeded bloated i386' >> conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
*=Error: packages database contains unused 'test1|stupid|${FAKEARCHITECTURE}' database.
*=This either means you removed a distribution, component or architecture from
*=the distributions config file without calling clearvanished, or your config
*=does not belong to this database.
*=To ignore use --ignore=undefinedtarget.
-v0*=There have been errors!
returns 255
EOF
testrun - -b . --confdir conf2 --ignore=undefinedtarget update 3<<EOF
*=Error: packages database contains unused 'test1|stupid|${FAKEARCHITECTURE}' database.
*=This either means you removed a distribution, component or architecture from
*=the distributions config file without calling clearvanished, or your config
*=does not belong to this database.
*=Ignoring as --ignore=undefinedtarget given.
*=Error: packages database contains unused 'test1|ugly|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test1|ugly|source' database.
*=Error: packages database contains unused 'test1|stupid|source' database.
*=Error: packages database contains unused 'test2|stupid|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test2|stupid|coal' database.
*=Error: packages database contains unused 'test2|stupid|source' database.
*=Error: packages database contains unused 'test2|ugly|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test2|ugly|coal' database.
*=Error: packages database contains unused 'test2|ugly|source' database.
*=Error: tracking database contains unused 'test1' database.
*=This either means you removed a distribution from the distributions config
*=file without calling clearvanished (or at least removealltracks), you
*=experienced a bug in retrack in versions < 3.0.0, you found a new bug or your
*=config does not belong to this database.
*=To ignore use --ignore=undefinedtracking.
-v0*=There have been errors!
returns 255
EOF
testrun - -b . --confdir conf2 --ignore=undefinedtarget --ignore=undefinedtracking update 3<<EOF
*=Error: packages database contains unused 'test1|stupid|${FAKEARCHITECTURE}' database.
*=This either means you removed a distribution, component or architecture from
*=the distributions config file without calling clearvanished, or your config
*=does not belong to this database.
*=Ignoring as --ignore=undefinedtarget given.
*=Error: tracking database contains unused 'test1' database.
*=This either means you removed a distribution from the distributions config
*=file without calling clearvanished (or at least removealltracks), you
*=experienced a bug in retrack in versions < 3.0.0, you found a new bug or your
*=config does not belong to this database.
*=Ignoring as --ignore=undefinedtracking given.
*=Error: packages database contains unused 'test1|ugly|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test1|ugly|source' database.
*=Error: packages database contains unused 'test1|stupid|source' database.
*=Error: packages database contains unused 'test2|stupid|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test2|stupid|coal' database.
*=Error: packages database contains unused 'test2|stupid|source' database.
*=Error: packages database contains unused 'test2|ugly|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test2|ugly|coal' database.
*=Error: packages database contains unused 'test2|ugly|source' database.
*=Error opening config file 'conf2/updates': No such file or directory(2)
-v0*=There have been errors!
returns 254
EOF
touch conf2/updates
testrun - -b . --confdir conf2 --ignore=undefinedtarget --ignore=undefinedtracking --noskipold update 3<<EOF
stderr
*=Error: packages database contains unused 'test1|stupid|${FAKEARCHITECTURE}' database.
*=This either means you removed a distribution, component or architecture from
*=the distributions config file without calling clearvanished, or your config
*=does not belong to this database.
*=Ignoring as --ignore=undefinedtarget given.
*=Error: packages database contains unused 'test1|ugly|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test1|ugly|source' database.
*=Error: packages database contains unused 'test1|stupid|source' database.
*=Error: packages database contains unused 'test2|stupid|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test2|stupid|coal' database.
*=Error: packages database contains unused 'test2|stupid|source' database.
*=Error: packages database contains unused 'test2|ugly|${FAKEARCHITECTURE}' database.
*=Error: packages database contains unused 'test2|ugly|coal' database.
*=Error: packages database contains unused 'test2|ugly|source' database.
*=Error: tracking database contains unused 'test1' database.
*=This either means you removed a distribution from the distributions config
*=file without calling clearvanished (or at least removealltracks), you
*=experienced a bug in retrack in versions < 3.0.0, you found a new bug or your
*=config does not belong to this database.
*=Ignoring as --ignore=undefinedtracking given.
stdout
-v2=Created directory "./lists"
-v0*=Calculating packages to get...
-v0*=Getting packages...
-v1=Freeing some memory...
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
EOF
testrun - -b . clearvanished 3<<EOF
stdout
*=Deleting vanished identifier 'foo|bloated|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'foo|bloated|fingers'.
*=Deleting vanished identifier 'foo|i386|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'foo|i386|fingers'.
*=Deleting vanished identifier 'foo|unneeded|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'foo|unneeded|fingers'.
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
echo "Format: 2.0" > broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Date' field!
=To Ignore use --ignore=missingfield.
-v0*=There have been errors!
returns 255
EOF
echo "Date: today" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Source' field
-v0*=There have been errors!
returns 255
EOF
echo "Source: nowhere" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Binary' field
-v0*=There have been errors!
returns 255
EOF
echo "Binary: phantom" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Architecture' field
-v0*=There have been errors!
returns 255
EOF
echo "Architecture: brain" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=In 'broken.changes': Missing 'Version' field
-v0*=There have been errors!
returns 255
EOF
echo "Version: old" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=In 'broken.changes': Missing 'Distribution' field
-v0*=There have been errors!
returns 255
EOF
echo "Distribution: old" >> broken.changes
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=In 'broken.changes': Missing 'Urgency' field!
=To Ignore use --ignore=missingfield.
-v0*=There have been errors!
returns 255
EOF
echo "Distribution: old" >> broken.changes
testrun - -b . --ignore=missingfield include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=In 'broken.changes': Missing 'Urgency' field!
*=In 'broken.changes': Missing 'Maintainer' field!
=Ignoring as --ignore=missingfield given.
*=In 'broken.changes': Missing 'Files' field!
-v0*=There have been errors!
returns 255
EOF
echo "Files:" >> broken.changes
testrun - -b . --ignore=missingfield include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=In 'broken.changes': Missing 'Urgency' field!
*=In 'broken.changes': Missing 'Maintainer' field!
*=broken.changes: Not enough files in .changes!
=Ignoring as --ignore=missingfield given.
-v0*=There have been errors!
returns 255
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
echo " $EMPTYMD5 section priority filename_version.tar.gz" >> broken.changes
testrun - -b . --ignore=missingfield include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
=In 'broken.changes': Missing 'Urgency' field!
=Ignoring as --ignore=missingfield given.
=In 'broken.changes': Missing 'Maintainer' field!
*=Warning: File 'filename_version.tar.gz' looks like source but does not start with 'nowhere_'!
=I hope you know what you do.
# grr, this message has really to improve...
=Warning: Package version 'version.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=.changes put in a distribution not listed within it!
=To ignore use --ignore=wrongdistribution.
-v0*=There have been errors!
returns 255
EOF
testrun - -b . --ignore=unusedarch --ignore=surprisingarch --ignore=wrongdistribution --ignore=missingfield include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
=In 'broken.changes': Missing 'Urgency' field!
=Ignoring as --ignore=missingfield given.
=In 'broken.changes': Missing 'Maintainer' field!
=Warning: File 'filename_version.tar.gz' looks like source but does not start with 'nowhere_'!
=I hope you know what you do.
*=.changes put in a distribution not listed within it!
*=Ignoring as --ignore=wrongdistribution given.
*=Architecture header lists architecture 'brain', but no files for it!
*=Ignoring as --ignore=unusedarch given.
*='filename_version.tar.gz' looks like architecture 'source', but this is not listed in the Architecture-Header!
*=Ignoring as --ignore=surprisingarch given.
*=Cannot find file './filename_version.tar.gz' needed by 'broken.changes'!
-v0*=There have been errors!
returns 249
EOF
touch filename_version.tar.gz
testrun - -b . --ignore=unusedarch --ignore=surprisingarch --ignore=wrongdistribution --ignore=missingfield include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
=In 'broken.changes': Missing 'Urgency' field!
=Ignoring as --ignore=missingfield given.
=In 'broken.changes': Missing 'Maintainer' field!
=Warning: File 'filename_version.tar.gz' looks like source but does not start with 'nowhere_'!
=I hope you know what you do.
*=.changes put in a distribution not listed within it!
*=Ignoring as --ignore=wrongdistribution given.
*=Architecture header lists architecture 'brain', but no files for it!
*=Ignoring as --ignore=unusedarch given.
*='filename_version.tar.gz' looks like architecture 'source', but this is not listed in the Architecture-Header!
*=Ignoring as --ignore=surprisingarch given.
*=Warning: File 'pool/stupid/n/nowhere/filename_version.tar.gz' was listed in the .changes
*= but seems unused. Checking for references...
= indeed unused, deleting it...
stdout
-v2*=Created directory "./pool/stupid/n"
-v2*=Created directory "./pool/stupid/n/nowhere"
-e1*=db: 'pool/stupid/n/nowhere/filename_version.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/n/nowhere/filename_version.tar.gz' added to checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/n/nowhere/filename_version.tar.gz
-e1*=db: 'pool/stupid/n/nowhere/filename_version.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/n/nowhere/filename_version.tar.gz' removed from checksums.db(pool).
-v2*=removed now empty directory ./pool/stupid/n/nowhere
-v2*=removed now empty directory ./pool/stupid/n
EOF
mkdir -p pool/stupid/n/nowhere
dodo test ! -f pool/stupid/n/nowhere/filename_version.tar.gz
cp filename_version.tar.gz pool/stupid/n/nowhere/filename_version.tar.gz
testrun - -b . _detect pool/stupid/n/nowhere/filename_version.tar.gz 3<<EOF
stdout
-e1*=db: 'pool/stupid/n/nowhere/filename_version.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/n/nowhere/filename_version.tar.gz' added to checksums.db(pool).
EOF
testout "" -b . dumpunreferenced
cat >results.expected <<EOF
pool/stupid/n/nowhere/filename_version.tar.gz
EOF
dodiff results.expected results
testrun - -b . deleteunreferenced 3<<EOF
stdout
-v1*=deleting and forgetting pool/stupid/n/nowhere/filename_version.tar.gz
-e1*=db: 'pool/stupid/n/nowhere/filename_version.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/n/nowhere/filename_version.tar.gz' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/stupid/n/nowhere
-v1*=removed now empty directory ./pool/stupid/n
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
testout "" -b . dumpreferences
# first remove file, then try to remove the package
testrun - -b . _forget pool/ugly/s/simple/simple_1_${FAKEARCHITECTURE}.deb 3<<EOF
stdout
-e1*=db: 'pool/ugly/s/simple/simple_1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/s/simple/simple_1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
EOF
testrun - -b . remove test1 simple 3<<EOF
# ???
=Warning: tracking database of test1 missed files for simple_1.
stdout
-v1*=removing 'simple' from 'test1|stupid|${FAKEARCHITECTURE}'...
-v1*=removing 'simple' from 'test1|stupid|source'...
-v1*=removing 'simple' from 'test1|ugly|${FAKEARCHITECTURE}'...
-v1*=removing 'simple' from 'test1|ugly|source'...
-d1*=db: 'simple' removed from packages.db(test1|stupid|${FAKEARCHITECTURE}).
-d1*=db: 'simple' removed from packages.db(test1|stupid|source).
-d1*=db: 'simple' removed from packages.db(test1|ugly|${FAKEARCHITECTURE}).
-d1*=db: 'simple' removed from packages.db(test1|ugly|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test1/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb stupid ${FAKEARCHITECTURE} simple 1
DATESTR remove test1 dsc stupid source simple 1
DATESTR remove test1 deb ugly ${FAKEARCHITECTURE} simple 1
DATESTR remove test1 dsc ugly source simple 1
EOF
testrun - -b . remove test2 simple 3<<EOF
*=To be forgotten filekey 'pool/ugly/s/simple/simple_1_${FAKEARCHITECTURE}.deb' was not known.
-v0*=There have been errors!
stdout
-v1=removing 'simple' from 'test2|ugly|${FAKEARCHITECTURE}'...
-d1*=db: 'simple' removed from packages.db(test2|ugly|${FAKEARCHITECTURE}).
-v1=removing 'simple' from 'test2|ugly|source'...
-d1*=db: 'simple' removed from packages.db(test2|ugly|source).
-v0=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test2/ugly/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
-v6*=  replacing './dists/test2/ugly/source/Sources' (uncompressed,gzipped,script: bzip.example,testhook)
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'change'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'change'
-v0=Deleting files no longer referenced...
-v1=deleting and forgetting pool/ugly/s/simple/simple_1_${FAKEARCHITECTURE}.deb
-v1=deleting and forgetting pool/ugly/s/simple/simple_1.dsc
-d1=db: 'pool/ugly/s/simple/simple_1.dsc' removed from checksums.db(pool).
-e1=db: 'pool/ugly/s/simple/simple_1.dsc' removed from files.db(md5sums).
-v1=deleting and forgetting pool/ugly/s/simple/simple_1.tar.gz
-d1=db: 'pool/ugly/s/simple/simple_1.tar.gz' removed from checksums.db(pool).
-e1=db: 'pool/ugly/s/simple/simple_1.tar.gz' removed from files.db(md5sums).
returns 249
EOF
checklog log2 <<EOF
DATESTR remove test2 deb ugly ${FAKEARCHITECTURE} simple 1
DATESTR remove test2 dsc ugly source simple 1
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results

cat > broken.changes <<EOF
Format: -1.0
Date: yesterday
Source: differently
Version: 0another
Architecture: source ${FAKEARCHITECTURE}
Urgency: super-hyper-duper-important
Maintainer: still me <guess@who>
Description: missing
Changes: missing
Binary: none and nothing
Distribution: test2
Files: 
 `md5sum 4test_b.1-1.dsc| cut -d" " -f 1` `stat -c%s 4test_b.1-1.dsc` a b differently_0another.dsc
 `md5sum 4test_b.1-1_${FAKEARCHITECTURE}.deb| cut -d" " -f 1` `stat -c%s 4test_b.1-1_${FAKEARCHITECTURE}.deb` a b 4test_b.1-1_${FAKEARCHITECTURE}.deb
EOF
#todo: make it work without this..
cp 4test_b.1-1.dsc differently_0another.dsc
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
=Warning: Package version 'b.1-1_${FAKEARCHITECTURE}.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=I don't know what to do having a .dsc without a .diff.gz or .tar.gz in 'broken.changes'!
-v0*=There have been errors!
returns 255
EOF
cat >> broken.changes <<EOF
 `md5sum 4test_b.1-1.tar.gz| cut -d" " -f 1` `stat -c%s 4test_b.1-1.tar.gz` a b 4test_b.1-1.tar.gz
EOF
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: File '4test_b.1-1.tar.gz' looks like source but does not start with 'differently_'!
=I hope you know what you do.
*='./pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' has packagename '4test' not listed in the .changes file!
*=To ignore use --ignore=surprisingbinary.
-v0*=There have been errors!
stdout
-v2*=Created directory "./pool/stupid/d"
-v2*=Created directory "./pool/stupid/d/differently"
-e1*=db: 'pool/stupid/d/differently/4test_b.1-1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/4test_b.1-1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/d/differently/differently_0another.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/differently_0another.dsc' added to checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/d/differently/4test_b.1-1.tar.gz
-e1*=db: 'pool/stupid/d/differently/4test_b.1-1.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/4test_b.1-1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/d/differently/differently_0another.dsc
-e1*=db: 'pool/stupid/d/differently/differently_0another.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/differently_0another.dsc' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/stupid/d/differently
-v1*=removed now empty directory ./pool/stupid/d
returns 255
EOF
testrun - -b . --ignore=surprisingbinary include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: File '4test_b.1-1.tar.gz' looks like source but does not start with 'differently_'!
=I hope you know what you do.
*='./pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' has packagename '4test' not listed in the .changes file!
*=Ignoring as --ignore=surprisingbinary given.
*='./pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' lists source package '4test', but .changes says it is 'differently'!
-v0*=There have been errors!
stdout
-v2*=Created directory "./pool/stupid/d"
-v2*=Created directory "./pool/stupid/d/differently"
-e1*=db: 'pool/stupid/d/differently/4test_b.1-1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/4test_b.1-1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/stupid/d/differently/differently_0another.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/differently_0another.dsc' added to checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/d/differently/4test_b.1-1.tar.gz
-e1*=db: 'pool/stupid/d/differently/4test_b.1-1.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/4test_b.1-1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/4test_b.1-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/d/differently/differently_0another.dsc
-e1*=db: 'pool/stupid/d/differently/differently_0another.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/d/differently/differently_0another.dsc' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/stupid/d/differently
-v1*=removed now empty directory ./pool/stupid/d
returns 255
EOF
cat > broken.changes <<EOF
Format: -1.0
Date: yesterday
Source: 4test
Version: 0orso
Architecture: source ${FAKEARCHITECTURE}
Urgency: super-hyper-duper-important
Maintainer: still me <guess@who>
Description: missing
Changes: missing
Binary: 4test
Distribution: test2
Files: 
 `md5sum 4test_b.1-1.dsc| cut -d" " -f 1` `stat -c%s 4test_b.1-1.dsc` a b 4test_0orso.dsc
 `md5sum 4test_b.1-1_${FAKEARCHITECTURE}.deb| cut -d" " -f 1` `stat -c%s 4test_b.1-1_${FAKEARCHITECTURE}.deb` a b 4test_b.1-1_${FAKEARCHITECTURE}.deb
 `md5sum 4test_b.1-1.tar.gz| cut -d" " -f 1` `stat -c%s 4test_b.1-1.tar.gz` a b 4test_b.1-1.tar.gz
EOF
cp 4test_b.1-1.dsc 4test_0orso.dsc
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1_${FAKEARCHITECTURE}.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*='./pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=To ignore use --ignore=wrongsourceversion.
-v0*=There have been errors!
stdout
-e1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' added to checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/4/4test/4test_0orso.dsc
-e1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' removed from checksums.db(pool).
returns 255
EOF
testrun - -b . --ignore=wrongsourceversion include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*='./pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=Ignoring as --ignore=wrongsourceversion given.
*='4test_0orso.dsc' says it is version '1:b.1-1', while .changes file said it is '0orso'
*=To ignore use --ignore=wrongversion.
-v0*=There have been errors!
stdout
-e1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' added to checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/4/4test/4test_0orso.dsc
-e1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' removed from checksums.db(pool).
returns 255
EOF
checknolog log1
checknolog log2
testrun - -b . --ignore=wrongsourceversion --ignore=wrongversion include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*='./pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=Ignoring as --ignore=wrongsourceversion given.
*='4test_0orso.dsc' says it is version '1:b.1-1', while .changes file said it is '0orso'
*=Ignoring as --ignore=wrongversion given.
stdout
-e1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' added to files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' added to checksums.db(pool).
-d1*=db: '4test' added to packages.db(test2|stupid|${FAKEARCHITECTURE}).
-d1*=db: '4test' added to packages.db(test2|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*=  replacing './dists/test2/stupid/source/Sources' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'change'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'change'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2 <<EOF
DATESTR add test2 deb stupid ${FAKEARCHITECTURE} 4test 1:b.1-1
DATESTR add test2 dsc stupid source 4test 1:b.1-1
EOF
testrun - -b . remove test2 4test 3<<EOF
stdout
-v1*=removing '4test' from 'test2|stupid|${FAKEARCHITECTURE}'...
-d1*=db: '4test' removed from packages.db(test2|stupid|${FAKEARCHITECTURE}).
-v1*=removing '4test' from 'test2|stupid|source'...
-d1*=db: '4test' removed from packages.db(test2|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/test2/stupid/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*=  replacing './dists/test2/stupid/source/Sources' (uncompressed,gzipped,script: bzip.example,testhook)
-v6*= looking for changes in 'test2|ugly|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/stupid/4/4test/4test_0orso.dsc
-e1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_0orso.dsc' removed from checksums.db(pool).
*=testhook got 4: './dists/test2' 'stupid/binary-${FAKEARCHITECTURE}/Packages.new' 'stupid/binary-${FAKEARCHITECTURE}/Packages' 'change'
*=testhook got 4: './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'change'
*=testhook got 4: './dists/test2' 'ugly/binary-${FAKEARCHITECTURE}/Packages.new' 'ugly/binary-${FAKEARCHITECTURE}/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
*=testhook got 4: './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2 <<EOF
DATESTR remove test2 deb stupid ${FAKEARCHITECTURE} 4test 1:b.1-1
DATESTR remove test2 dsc stupid source 4test 1:b.1-1
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results

checknolog log1
checknolog log2

testout "" -b . dumptracks
# TODO: check here for what should be here,
# check the othe stuff, too
#dodiff results.empty results
cat > conf/distributions <<EOF
Codename: X
Architectures: none
Components: test
EOF
testrun - -b . --delete clearvanished 3<<EOF
stderr
-v4*=Strange, 'X|test|none' does not appear in packages.db yet.
stdout
*=Deleting vanished identifier 'test1|stupid|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'test1|stupid|source'.
*=Deleting vanished identifier 'test1|ugly|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'test1|ugly|source'.
*=Deleting vanished identifier 'test2|stupid|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'test2|stupid|coal'.
*=Deleting vanished identifier 'test2|stupid|source'.
*=Deleting vanished identifier 'test2|ugly|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'test2|ugly|coal'.
*=Deleting vanished identifier 'test2|ugly|source'.
*=Deleting tracking data for vanished distribution 'test1'.
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/stupid/4/4test/4test-addons_b.1-1_all.deb
-e1*=db: 'pool/stupid/4/4test/4test-addons_b.1-1_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test-addons_b.1-1_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_b.1-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/4/4test/4test_b.1-1.dsc
-e1*=db: 'pool/stupid/4/4test/4test_b.1-1.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_b.1-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/4/4test/4test_b.1-1.tar.gz
-e1*=db: 'pool/stupid/4/4test/4test_b.1-1.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/4/4test/4test_b.1-1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz
-e1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
-e1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
-e1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz
-e1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1.dsc
-e1*=db: 'pool/stupid/s/simple/simple_1.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1.tar.gz
-e1*=db: 'pool/stupid/s/simple/simple_1.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/s/simple/simple_1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test-addons_1-2_all.deb
-e1*=db: 'pool/stupid/t/test/test-addons_1-2_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test-addons_1-2_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2.dsc
-e1*=db: 'pool/stupid/t/test/test_1-2.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test_1.orig.tar.gz
-e1*=db: 'pool/stupid/t/test/test_1.orig.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1.orig.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2.diff.gz
-e1*=db: 'pool/stupid/t/test/test_1-2.diff.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/test/test_1-2.diff.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb-addons_2-3_all.deb
-e1*=db: 'pool/stupid/t/testb/testb-addons_2-3_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb-addons_2-3_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-3_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-3.dsc
-e1*=db: 'pool/stupid/t/testb/testb_2-3.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-3.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2.orig.tar.gz
-e1*=db: 'pool/stupid/t/testb/testb_2.orig.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2.orig.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-3.diff.gz
-e1*=db: 'pool/stupid/t/testb/testb_2-3.diff.gz' removed from files.db(md5sums).
-d1*=db: 'pool/stupid/t/testb/testb_2-3.diff.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/ugly/s/simple/simple-addons_1_all.deb
-e1*=db: 'pool/ugly/s/simple/simple-addons_1_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/ugly/s/simple/simple-addons_1_all.deb' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/stupid/4/4test
-v1*=removed now empty directory ./pool/stupid/4
-v1*=removed now empty directory ./pool/stupid/b/bloat+-0a9z.app
-v1*=removed now empty directory ./pool/stupid/b
-v1*=removed now empty directory ./pool/stupid/s/simple
-v1*=removed now empty directory ./pool/stupid/s
-v1*=removed now empty directory ./pool/stupid/t/testb
-v1*=removed now empty directory ./pool/stupid/t/test
-v1*=removed now empty directory ./pool/stupid/t
-v1*=removed now empty directory ./pool/stupid
-v1*=removed now empty directory ./pool/ugly/b/bloat+-0a9z.app
-v1*=removed now empty directory ./pool/ugly/b
-v1*=removed now empty directory ./pool/ugly/s/simple
-v1*=removed now empty directory ./pool/ugly/s
-v1*=removed now empty directory ./pool/ugly
-v1*=removed now empty directory ./pool
EOF

for tracking in true false ; do
if $tracking ; then
echo "this is the test variant with tracking on"
else
echo "this is the test variant with tracking off"
fi
testout "" -b . dumptracks
dodiff results.empty results
testout "" -b . dumpunreferenced
dodiff results.empty results

checknolog logfile
checknolog log1
checknolog log2

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

rm -r dists
checknolog logab
testout "" -b . dumptracks a
dodiff results.empty results
testout "" -b . dumpunreferenced
dodiff results.empty results
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
-e1*=db: 'pool/all/a/aa/aa-addons_1-1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa-addons_1-1_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-1.dsc' added to files.db(md5sums).
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
-e1*=db: 'pool/all/a/aa/aa-addons_1-2_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa-addons_1-2_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-2.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-2.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-2.dsc' added to files.db(md5sums).
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
-e1*=db: 'pool/all/a/aa/aa_1-1.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-1.tar.gz
-e1*=db: 'pool/all/a/aa/aa_1-1.tar.gz' removed from files.db(md5sums).
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
-e1*=db: 'pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-1_all.deb
-e1*=db: 'pool/all/a/aa/aa-addons_1-1_all.deb' removed from files.db(md5sums).
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
-e1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/all/a/aa/aa_1-3.dsc' added to files.db(md5sums).
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
-e1*=db: 'pool/all/a/aa/aa_1-2.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-2.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-2.tar.gz
-e1*=db: 'pool/all/a/aa/aa_1-2.tar.gz' removed from files.db(md5sums).
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
-e1*=db: 'pool/all/a/ab/ab-addons_2-1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab-addons_2-1_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/ab/ab_2-1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_2-1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/ab/ab_2-1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_2-1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/all/a/ab/ab_2-1.dsc' added to files.db(md5sums).
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
-e1*=db: 'pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-2_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-2_all.deb
-e1*=db: 'pool/all/a/aa/aa-addons_1-2_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa-addons_1-2_all.deb' removed from checksums.db(pool).
EOF
checklog logab << EOF
DATESTR replace b deb all ${FAKEARCHITECTURE} aa 1-3 1-2
DATESTR replace b deb all ${FAKEARCHITECTURE} aa-addons 1-3 1-2
DATESTR add b deb all ${FAKEARCHITECTURE} ab 2-1
DATESTR add b deb all ${FAKEARCHITECTURE} ab-addons 2-1
EOF
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
-e1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
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
-v0*=Deleting files no longer referenced...
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
-e1*=db: 'pool/all/a/ab/ab_3-1.dsc' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' added to checksums.db(pool).
-e1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' added to checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.diff.gz
-e1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
-e1*=db: 'pool/all/a/ab/ab_3-1.dsc' removed from files.db(md5sums).
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
*=Warning: File 'pool/all/a/ab/ab_3-1.diff.gz' was listed in the .changes
*= but seems unused. Checking for references...
*= indeed unused, deleting it...
stdout
-e1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' added to checksums.db(pool).
-e1*=db: 'pool/all/a/ab/ab_3-1.dsc' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' added to checksums.db(pool).
-d1*=db: 'ab' removed from packages.db(a|all|source).
-d1*=db: 'ab' added to packages.db(a|all|source).
-t1*=db: 'ab' '2-1' removed from tracking.db(a).
-v2*=deleting and forgetting pool/all/a/ab/ab_3-1.diff.gz
-d1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from checksums.db(pool).
-e1*=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from files.db(md5sums).
-v5*=Deleting 'broken.changes'.
-v0*=Exporting indices...
-v6*= looking for changes in 'a|all|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'a|all|source'...
-v6*=  replacing './dists/a/all/source/Sources' (gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/ab/ab_2-1.dsc
-e1*=db: 'pool/all/a/ab/ab_2-1.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_2-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_2-1.tar.gz
-e1*=db: 'pool/all/a/ab/ab_2-1.tar.gz' removed from files.db(md5sums).
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
-e1=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from files.db(md5sums).
-d1=db: 'pool/all/a/ab/ab_3-1.diff.gz' removed from checksums.db(pool).
EOF

DISTRI=b PACKAGE=ac EPOCH="" VERSION=1 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun - -b . -A ${FAKEARCHITECTURE} --delete --delete --ignore=missingfile include b test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v2*=Skipping 'ac_1-1.dsc' as not for architecture '${FAKEARCHITECTURE}'.
-v2*=Skipping 'ac_1-1.tar.gz' as not for architecture '${FAKEARCHITECTURE}'.
-v3*=Placing 'ac-addons_1-1_all.deb' only in architecture '${FAKEARCHITECTURE}' as requested.
stdout
-v2*=Created directory "./pool/all/a/ac"
-e1*=db: 'pool/all/a/ac/ac-addons_1-1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/all/a/ac/ac-addons_1-1_all.deb' added to checksums.db(pool).
-e1*=db: 'pool/all/a/ac/ac_1-1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
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
Suite: a
ListHook: /bin/cp
END
testrun - -b . predelete b 3<<EOF
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/a/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/a/Release'
-v6*=aptmethod start 'copy:$WORKDIR/dists/a/all/binary-${FAKEARCHITECTURE}/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/a/all/binary-${FAKEARCHITECTURE}/Packages.gz'
-v6*=Called /bin/cp './lists/b_froma_deb_all_${FAKEARCHITECTURE}' './lists/b_froma_deb_all_${FAKEARCHITECTURE}_changed'
-v6*=Listhook successfully returned!
stdout
-v0*=Removing obsolete or to be replaced packages...
-v3*=  processing updates for 'b|all|${FAKEARCHITECTURE}'
-v5*=  marking everything to be deleted
-v5*=  reading './lists/b_froma_deb_all_${FAKEARCHITECTURE}_changed'
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
-e1*=db: 'pool/all/a/ab/ab_2-1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_2-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_2-1_all.deb
-e1*=db: 'pool/all/a/ab/ab-addons_2-1_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab-addons_2-1_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ac/ac_1-1_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/all/a/ac/ac_1-1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ac/ac_1-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ac/ac-addons_1-1_all.deb
-e1*=db: 'pool/all/a/ac/ac-addons_1-1_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ac/ac-addons_1-1_all.deb' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/all/a/ac
EOF
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
testrun - --keepunreferenced --dbdir db2 -b . removesrc a unknown 3<<EOF
stderr
-t1*=Nothing about source package 'unknown' found in the tracking data of 'a'!
-t1*=This either means nothing from this source in this version is there,
-t1*=or the tracking information might be out of date.
stdout
EOF
testrun - --keepunreferenced --dbdir db2 -b . removesrc a ab 3-1 3<<EOF
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
testrun - --keepunreferenced --dbdir db2 -b . removesrc a ab 3<<EOF
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
testrun - --keepunreferenced --dbdir db2 -b . removefilter b "Version (== 1-3), Package (>> aa)" 3<<EOF
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
if $tracking ; then
testrun - -b . --delete removealltracks a 3<<EOF
stdout
-v0*=Deleting all tracks for a...
-v0=Deleting files no longer referenced...
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
#-v4*=Strange, 'X|test|none' does not appear in packages.db yet.
stdout
*=Deleting vanished identifier 'a|all|${FAKEARCHITECTURE}'.
*=Deleting vanished identifier 'a|all|source'.
*=Deleting vanished identifier 'b|all|${FAKEARCHITECTURE}'.
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-3_all.deb
-e1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.dsc
-e1*=db: 'pool/all/a/aa/aa_1-3.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-3.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.tar.gz
-e1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/all/a/aa
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_3-1_all.deb
-e1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
-e1*=db: 'pool/all/a/ab/ab_3-1.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.tar.gz
-e1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/all/a/ab
-v1*=removed now empty directory ./pool/all/a
-v1*=removed now empty directory ./pool/all
-v1*=removed now empty directory ./pool
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
-e1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa-addons_1-3_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.dsc
-e1*=db: 'pool/all/a/aa/aa_1-3.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-3.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.tar.gz
-e1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-3.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/aa/aa_1-3_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/all/a/aa
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_3-1_all.deb
-e1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab-addons_3-1_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
-e1*=db: 'pool/all/a/ab/ab_3-1.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.tar.gz
-e1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb
-e1*=db: 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' removed from files.db(md5sums).
-d1*=db: 'pool/all/a/ab/ab_3-1_${FAKEARCHITECTURE}.deb' removed from checksums.db(pool).
-v1*=removed now empty directory ./pool/all/a/ab
-v1*=removed now empty directory ./pool/all/a
-v1*=removed now empty directory ./pool/all
-v1*=removed now empty directory ./pool
EOF
fi
checknolog logab
done
set +v +x
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
