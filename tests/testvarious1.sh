#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
	source $(dirname $0)/test.inc
fi

mkdir -p conf
cat > conf/distributions <<EOF

#

Codename:		test	
Architectures:
# This is an comment
 	a
Components:
 	c

#
#

EOF
touch conf/updates
dodo test ! -d db
testrun - -b . checkupdate test 3<<EOF
stderr
*=Nothing to do found. (Use --noskipold to force processing)
stdout
-v2*=Created directory "./db"
-v2=Created directory "./lists"
#-v2*=Removed empty directory "./db"
EOF
#dodo test ! -d db
rm -r -f lists
rm -r -f db conf
dodo test ! -d d/ab
mkdir -p conf
cat > conf/options <<CONFEND
export changed
CONFEND
cat > conf/distributions <<CONFEND
Codename: A
Architectures: ${FAKEARCHITECTURE} calculator
Components: dog cat
Log: logfile
 --bla
CONFEND
testrun - -b . export 3<<EOF
return 255
stdout
stderr
*=Unknown Log notifier option in ./conf/distributions, line 5, column 2: '--bla'
-v0*=There have been errors!
EOF
cat > conf/distributions <<CONFEND
Codename: A
Architectures: ${FAKEARCHITECTURE} calculator
Components: dog cat
Log: logfile
 -A
CONFEND
testrun - -b . export 3<<EOF
return 255
*=Log notifier option -A misses an argument in ./conf/distributions, line 5, column 4
-v0*=There have been errors!
EOF
cat > conf/distributions <<CONFEND
Codename: A
Architectures: ${FAKEARCHITECTURE} calculator
Components: dog cat
Log: logfile
 -A=${FAKEARCHITECTURE}
CONFEND
testrun - -b . export 3<<EOF
return 255
*=Error parsing config file ./conf/distributions, line 5, column $(( 5 + $FALEN )):
*=Unexpected end of line: name of notifier script missing!
-v0*=There have been errors!
EOF
cat > conf/distributions <<CONFEND
Codename: A
Architectures: ${FAKEARCHITECTURE} calculator
Components: dog cat
Log: logfile
 -A=${FAKEARCHITECTURE} --architecture=coal
CONFEND
testrun - -b . export 3<<EOF
return 255
*=Repeated notifier option --architecture in ./conf/distributions, line 5, column $(( 6 + $FALEN))!
-v0*=There have been errors!
EOF
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
find dists -type f | LC_ALL=C sort -f > results
cat > results.expected <<END
dists/A/cat/binary-${FAKEARCHITECTURE}/Packages
dists/A/cat/binary-${FAKEARCHITECTURE}/Packages.gz
dists/A/cat/binary-${FAKEARCHITECTURE}/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-${FAKEARCHITECTURE}/Packages
dists/A/dog/binary-${FAKEARCHITECTURE}/Packages.gz
dists/A/dog/binary-${FAKEARCHITECTURE}/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-${FAKEARCHITECTURE}/Packages
dists/B/cat/binary-${FAKEARCHITECTURE}/Packages.gz
dists/B/cat/binary-${FAKEARCHITECTURE}/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-${FAKEARCHITECTURE}.gz
dists/B/dog/binary-${FAKEARCHITECTURE}/Packages
dists/B/dog/binary-${FAKEARCHITECTURE}/Packages.gz
dists/B/dog/binary-${FAKEARCHITECTURE}/Release
dists/B/dog/source/Release
dists/B/dog/source/Sources.gz
dists/B/Release
END
dodiff results.expected results
testrun - -b . processincoming default 3<<EOF
returns 254
stderr
*=Error opening config file './conf/incoming': No such file or directory(2)
-v0*=There have been errors!
stdout
EOF
if test -n "$TESTNEWFILESDB" ; then
	dodo test ! -f db/files.db
fi
touch conf/incoming
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
*=No definition for 'default' found in './conf/incoming'!
-v0*=There have been errors!
stdout
EOF
cat > conf/incoming <<EOF
Name: bla
Tempdir: bla
Incomingdir: bla
EOF
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
*=No definition for 'default' found in './conf/incoming'!
-v0*=There have been errors!
stdout
EOF
cat > conf/incoming <<EOF
Name: bla
Tempdir: bla
Incomingdir: bla

# a comment
#

Name: default

Name: blub
EOF
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
*=Error parsing config file ./conf/incoming, line 9:
*=Required field 'TempDir' expected (since line 8).
-v0*=There have been errors!
EOF
cat > conf/incoming <<EOF
Name: bla
Tempdir: bla
Incomingdir: bla

# a comment
#

Name: default
TempDir: temp

Name: blub
EOF
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
*=Error parsing config file ./conf/incoming, line 10:
*=Required field 'IncomingDir' expected (since line 8).
-v0*=There have been errors!
EOF
cat > conf/incoming <<EOF
# commentary
Name: bla
Tempdir: bla
Incomingdir: bla
Permit: unused_files bla older_version
Cleanup: unused_files bla on_deny

# a comment
#

Name: default
TempDir: temp

Name: blub
EOF
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
*=Warning: ignored error parsing config file ./conf/incoming, line 5, column 22:
*=Unknown flag in Permit header. (but not within the rule we are intrested in.)
*=Warning: ignored error parsing config file ./conf/incoming, line 6, column 23:
*=Unknown flag in Cleanup header. (but not within the rule we are intrested in.)
*=Error parsing config file ./conf/incoming, line 13:
*=Required field 'IncomingDir' expected (since line 11).
-v0*=There have been errors!
EOF
cat > conf/incoming <<EOF
Name: bla
TempDir: bla
IncomingDir: bla

Name: default
TempDir: temp
IncomingDir:		i

Name: blub
TempDir: blub
IncomingDir: blub
EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
*=There is neither an 'Allow' nor a 'Default' definition in rule 'default'
*=(starting at line 5, ending at line 8 of ./conf/incoming)!
*=Aborting as nothing would be let in.
-v0*=There have been errors!
EOF
cat > conf/incoming <<EOF
Name: bla
TempDir: bla
IncomingDir: blub

Name: default
TempDir: temp
IncomingDir:		i	
Allow: A B

Name: blub
TempDir: bla
IncomingDir: blub
EOF
testrun - -b . processincoming default 3<<EOF
returns 254
stderr
*=Cannot scan './i': No such file or directory
-v0*=There have been errors!
stdout
-v2*=Created directory "./temp"
EOF
mkdir i
testrun "" -b . processincoming default
(cd i ; PACKAGE=bird EPOCH="" VERSION=1 REVISION="" SECTION="tasty" genpackage.sh)
echo returned: $?
DSCMD5S="$(mdandsize i/bird_1.dsc)"
TARMD5S="$(mdandsize i/bird_1.tar.gz)"
DSCSHA1S="$(sha1andsize i/bird_1.dsc)"
TARSHA1S="$(sha1andsize i/bird_1.tar.gz)"
DSCSHA2S="$(sha2andsize i/bird_1.dsc)"
TARSHA2S="$(sha2andsize i/bird_1.tar.gz)"
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=No distribution found for 'test.changes'!
-v0*=There have been errors!
stdout
EOF
sed -i -e 's/test1/A/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*='test.changes' lists architecture 'source' not found in distribution 'A'!
-v0*=There have been errors!
stdout
EOF
sed -i -e 's/Distribution: A/Distribution: B/' i/test.changes
cp -a i i2
function checknolog() {
	dodo test ! -f logs/"$1"
}
checknolog logfile
if test -n "$TESTNEWFILESDB" ; then
	dodo test ! -f db/files.db
fi
testrun - -b . processincoming default 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v9*=Adding reference to 'pool/dog/b/bird/bird_1.dsc' by 'B|dog|source'
-v9*=Adding reference to 'pool/dog/b/bird/bird_1.tar.gz' by 'B|dog|source'
-v9*=Adding reference to 'pool/dog/b/bird/bird_1_${FAKEARCHITECTURE}.deb' by 'B|dog|${FAKEARCHITECTURE}'
-v9*=Adding reference to 'pool/dog/b/bird/bird-addons_1_all.deb' by 'B|dog|${FAKEARCHITECTURE}'
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/dog"
-v2*=Created directory "./pool/dog/b"
-v2*=Created directory "./pool/dog/b/bird"
-e1*=db: 'pool/dog/b/bird/bird_1.dsc' added to files.db(md5sums).
-d1*=db: 'pool/dog/b/bird/bird_1.dsc' added to checksums.db(pool).
-e1*=db: 'pool/dog/b/bird/bird_1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/dog/b/bird/bird_1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/dog/b/bird/bird_1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/dog/b/bird/bird_1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/dog/b/bird/bird-addons_1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/dog/b/bird/bird-addons_1_all.deb' added to checksums.db(pool).
-v2*=Created directory "./logs"
-d1*=db: 'bird' added to packages.db(B|dog|source).
-d1*=db: 'bird' added to packages.db(B|dog|${FAKEARCHITECTURE}).
-d1*=db: 'bird-addons' added to packages.db(B|dog|${FAKEARCHITECTURE}).
-v3*=deleting './i/bird_1.dsc'...
-v3*=deleting './i/bird_1.tar.gz'...
-v3*=deleting './i/bird_1_${FAKEARCHITECTURE}.deb'...
-v3*=deleting './i/bird-addons_1_all.deb'...
-v3*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/B/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-${FAKEARCHITECTURE}...
-v4*=Reading filelist for pool/dog/b/bird/bird_1_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/dog/b/bird/bird_1_${FAKEARCHITECTURE}.deb' added to contents.cache.db(compressedfilelists).
-v4*=Reading filelist for pool/dog/b/bird/bird-addons_1_all.deb
-d1*=db: 'pool/dog/b/bird/bird-addons_1_all.deb' added to contents.cache.db(compressedfilelists).
EOF
LOGDATE="$(date +'%Y-%m-%d %H:')"
echo normalizing logfile: DATESTR is "$LOGDATE??:??"
sed -i -e 's/^'"$LOGDATE"'[0-9][0-9]:[0-9][0-9] /DATESTR /g' logs/logfile
cat > results.log.expected <<EOF
DATESTR add B dsc dog source bird 1
DATESTR add B deb dog ${FAKEARCHITECTURE} bird 1
DATESTR add B deb dog ${FAKEARCHITECTURE} bird-addons 1
EOF
dodiff results.log.expected logs/logfile
find temp -type f > results
dodiff results.empty results
find i -type f > results
dodiff results.empty results
cat > results.expected <<EOF
FILE                                                    LOCATION
x	    tasty/bird,tasty/bird-addons
a/1	    tasty/bird,tasty/bird-addons
dir/another	    tasty/bird,tasty/bird-addons
dir/file	    tasty/bird,tasty/bird-addons
dir/subdir/file	    tasty/bird,tasty/bird-addons
EOF
gunzip -c dists/B/Contents-${FAKEARCHITECTURE}.gz > results
dodiff results.expected results
printindexpart pool/dog/b/bird/bird_1_${FAKEARCHITECTURE}.deb > results.expected
printindexpart pool/dog/b/bird/bird-addons_1_all.deb >> results.expected
dodiff results.expected dists/B/dog/binary-${FAKEARCHITECTURE}/Packages
withoutchecksums pool/dog/b/bird/bird_1.dsc > results.expected
ed -s results.expected <<EOF
H
/^Source: / m 0
s/^Source: /Package: /
/^Files: / kf
'f i
Priority: superfluous
Section: tasty
Directory: pool/dog/b/bird
.
'f a
 $DSCMD5S bird_1.dsc
.
$ a
Checksums-Sha1: 
 $DSCSHA1S bird_1.dsc
 $TARSHA1S bird_1.tar.gz
Checksums-Sha256: 
 $DSCSHA2S bird_1.dsc
 $TARSHA2S bird_1.tar.gz

.
w
q
EOF
gunzip -c dists/B/dog/source/Sources.gz > results
dodiff results.expected results

echo "DebOverride: debo" >> conf/distributions
echo "DscOverride: dsco" >> conf/distributions
echo "bird Section cat/tasty" > conf/debo
echo "bird Priority hungry" >> conf/debo
echo "bird Task lunch" >> conf/debo
echo "bird-addons Section cat/ugly" >> conf/debo
echo "bird Section cat/nest" > conf/dsco
echo "bird Priority hurry" >> conf/dsco
echo "bird Homepage gopher://tree" >> conf/dsco

mv i2/* i/
rmdir i2
testrun - -b . processincoming default 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./pool/cat"
-v2*=Created directory "./pool/cat/b"
-v2*=Created directory "./pool/cat/b/bird"
-e1*=db: 'pool/cat/b/bird/bird_1.dsc' added to files.db(md5sums).
-d1*=db: 'pool/cat/b/bird/bird_1.dsc' added to checksums.db(pool).
-e1*=db: 'pool/cat/b/bird/bird_1.tar.gz' added to files.db(md5sums).
-d1*=db: 'pool/cat/b/bird/bird_1.tar.gz' added to checksums.db(pool).
-e1*=db: 'pool/cat/b/bird/bird_1_${FAKEARCHITECTURE}.deb' added to files.db(md5sums).
-d1*=db: 'pool/cat/b/bird/bird_1_${FAKEARCHITECTURE}.deb' added to checksums.db(pool).
-e1*=db: 'pool/cat/b/bird/bird-addons_1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/cat/b/bird/bird-addons_1_all.deb' added to checksums.db(pool).
-d1*=db: 'bird' added to packages.db(B|cat|source).
-d1*=db: 'bird' added to packages.db(B|cat|${FAKEARCHITECTURE}).
-d1*=db: 'bird-addons' added to packages.db(B|cat|${FAKEARCHITECTURE}).
-v3*=deleting './i/bird_1.dsc'...
-v3*=deleting './i/bird_1.tar.gz'...
-v3*=deleting './i/bird_1_${FAKEARCHITECTURE}.deb'...
-v3*=deleting './i/bird-addons_1_all.deb'...
-v3*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/B/cat/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/cat/source/Sources' (gzipped)
-v1*= generating Contents-${FAKEARCHITECTURE}...
-v4*=Reading filelist for pool/cat/b/bird/bird_1_${FAKEARCHITECTURE}.deb
-d1*=db: 'pool/cat/b/bird/bird_1_${FAKEARCHITECTURE}.deb' added to contents.cache.db(compressedfilelists).
-v4*=Reading filelist for pool/cat/b/bird/bird-addons_1_all.deb
-d1*=db: 'pool/cat/b/bird/bird-addons_1_all.deb' added to contents.cache.db(compressedfilelists).
EOF
function checklog() {
	cat > results.log.expected
	LOGDATE="$(date +'%Y-%m-%d %H:')"
	echo normalizing "$1": DATESTR is "$LOGDATE??:??"
	sed -i -e 's/^'"$LOGDATE"'[0-9][0-9]:[0-9][0-9] /DATESTR /g' logs/"$1"
	dodiff results.log.expected logs/"$1"
	rm logs/"$1"

}
checklog logfile <<EOF
DATESTR add B dsc dog source bird 1
DATESTR add B deb dog ${FAKEARCHITECTURE} bird 1
DATESTR add B deb dog ${FAKEARCHITECTURE} bird-addons 1
DATESTR add B dsc cat source bird 1
DATESTR add B deb cat ${FAKEARCHITECTURE} bird 1
DATESTR add B deb cat ${FAKEARCHITECTURE} bird-addons 1
EOF
find temp -type f > results
dodiff results.empty results
find i -type f > results
dodiff results.empty results
cat > results.expected <<EOF
FILE                                                    LOCATION
x	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
a/1	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
dir/another	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
dir/file	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
dir/subdir/file	    tasty/bird,tasty/bird-addons,cat/tasty/bird,cat/ugly/bird-addons
EOF
gunzip -c dists/B/Contents-${FAKEARCHITECTURE}.gz > results
dodiff results.expected results
printindexpart pool/cat/b/bird/bird_1_${FAKEARCHITECTURE}.deb > results.expected
printindexpart pool/cat/b/bird/bird-addons_1_all.deb >> results.expected
ed -s results.expected <<EOF
H
/^Priority: / s/^Priority: superfluous$/Priority: hungry/
i
Task: lunch
.
/^Section: / s/^Section: tasty$/Section: cat\/tasty/
/^Section: tasty/ s/^Section: tasty$/Section: cat\/ugly/
w
q
EOF
dodiff results.expected dists/B/cat/binary-${FAKEARCHITECTURE}/Packages
withoutchecksums pool/cat/b/bird/bird_1.dsc > results.expected
ed -s results.expected <<EOF
H
/^Source: / m 0
s/^Source: /Package: /
/^Files: / kf
'f i
Homepage: gopher://tree
Priority: hurry
Section: cat/nest
Directory: pool/cat/b/bird
.
'f a
 $DSCMD5S bird_1.dsc
.
$ a
Checksums-Sha1: 
 $DSCSHA1S bird_1.dsc
 $TARSHA1S bird_1.tar.gz
Checksums-Sha256: 
 $DSCSHA2S bird_1.dsc
 $TARSHA2S bird_1.tar.gz

.
w
q
EOF
BIRDDSCMD5S="$DSCMD5S"
BIRDTARMD5S="$TARMD5S"
BIRDDSCSHA1S="$DSCSHA1S"
BIRDTARSHA1S="$TARSHA1S"
BIRDDSCSHA2S="$DSCSHA2S"
BIRDTARSHA2S="$TARSHA2S"
gunzip -c dists/B/cat/source/Sources.gz > results
dodiff results.expected results

# now missing: checking what all can go wrong in a .changes or .dsc file...
mkdir pkg
mkdir pkg/a
touch pkg/a/b
mkdir pkg/DEBIAN
cat > pkg/DEBIAN/control <<EOF
Package: indebname
Version: 1:versionindeb~1
Source: sourceindeb (sourceversionindeb)
EOF
dpkg-deb -b pkg i/debfilename_debfileversion~2_coal.deb
DEBMD5="$(md5sum i/debfilename_debfileversion~2_coal.deb | cut -d' ' -f1)"
DEBSIZE="$(stat -c '%s' i/debfilename_debfileversion~2_coal.deb)"
DEBMD5S="$DEBMD5 $DEBSIZE"
cat > i/test.changes <<EOF
EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Unexpected empty file 'test.changes'!
-v0*=There have been errors!
EOF
echo > i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Could only find spaces within 'test.changes'!
-v0*=There have been errors!
EOF
cat > i/test.changes <<EOF
-chunk: 1


EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly....
*=First non-space character is a '-' but there is no empty line in
*='test.changes'.
*=Unable to extract any data from it!
-v0*=There have been errors!
EOF
cat > i/test.changes <<EOF
-chunk: 1

EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly....
*=First non-space character is a '-' but there is no empty line in
*='test.changes'.
*=Unable to extract any data from it!
-v0*=There have been errors!
EOF
cat > i/test.changes <<EOF
chunk: 1

chunk: 2
EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0*=There have been errors!
*=Error parsing 'test.changes': Seems not to be signed but has spurious empty line.
EOF
cat > i/test.changes <<EOF
-----BEGIN FAKE GPG SIGNED MAIL
type: funny

This is some content
-----BEGIN FAKE SIGNATURE
Hahaha!
-----END FAKE SIGNATURE
EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly....
-v0=Cannot check signatures from 'test.changes' as compiled without support for libgpgme!
-v0=Extracting the content manually without looking at the signature...
*=In 'test.changes': Missing 'Source' field!
-v0*=There have been errors!
EOF
cat > i/test.changes <<EOF
-----BEGIN FAKE GPG SIGNED MAIL
type: funny

This is some content

-----BEGIN FAKE SIGNATURE
Hahaha!
-----END FAKE SIGNATURE
EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly....
-v0=Cannot check signatures from 'test.changes' as compiled without support for libgpgme!
-v0=Extracting the content manually without looking at the signature...
*=In 'test.changes': Missing 'Source' field!
-v0*=There have been errors!
EOF
cat > i/test.changes <<EOF
Dummyfield: test
EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Source' field!
-v0*=There have been errors!
EOF
echo "Source: sourceinchanges" > i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Binary' field!
-v0*=There have been errors!
EOF
echo "Binary: binaryinchanges" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Architecture' field!
-v0*=There have been errors!
EOF
echo "Architecture: funny" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Version' field!
-v0*=There have been errors!
EOF
echo "Version: 999:versioninchanges-0~" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Distribution' field!
-v0*=There have been errors!
EOF
echo "Distribution: A" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': Missing 'Files' field!
-v0*=There have been errors!
EOF
echo "Files:" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': Empty 'Files' section!
-v0*=There have been errors!
EOF
# as it does not look for the file, but scanned the directory
# and looked for it, there is no problem here, though it might
# look like one
echo " ffff 666 - - ../ööü_v_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': file '../ööü_v_all.deb' not found in the incoming dir!
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
printf ' ffff 666 - - \300\257.\300\257_v_funny.deb\n' >> i/test.changes
touch "$(printf 'i/\300\257.\300\257_v_funny.deb')"
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*='test.changes' lists architecture 'funny' not found in distribution 'A'!
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
printf ' ffff 666 - - \300\257.\300\257_v_all.deb\n' >> i/test.changes
mv "$(printf 'i/\300\257.\300\257_v_funny.deb')" "$(printf 'i/\300\257.\300\257_v_all.deb')"
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*='all' is not listed in the Architecture header of 'test.changes' but file 'À¯.À¯_v_all.deb' looks like it!
-v0*=There have been errors!
EOF
sed -i -e 's/funny/all/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Invalid filename 'À¯.À¯_v_all.deb' listed in 'test.changes': contains 8-bit characters
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " ffff 1 - - debfilename_debfileversion~2_coal.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*='coal' is not listed in the Architecture header of 'test.changes' but file 'debfilename_debfileversion~2_coal.deb' looks like it!
-v0*=There have been errors!
EOF
mv i/debfilename_debfileversion~2_coal.deb i/debfilename_debfileversion~2_all.deb
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " md5sum size - - debfilename_debfileversion~2_all.deb" >> i/test.changes
# TODO: this error message has to be improved:
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Malformed md5 hash in 'md5sum size - - debfilename_debfileversion~2_all.deb'!
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " ffff 666 - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 254
stderr
-v0=Data seems not to be signed trying to use directly...
*=ERROR: File 'debfilename_debfileversion~2_all.deb' does not match expectations:
*=md5 expected: ffff, got: $DEBMD5
*=size expected: 666, got: $DEBSIZE
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
# TODO: these will hopefully change to not divulge the place of the temp dir some day...
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=No Maintainer field in ./temp/debfilename_debfileversion~2_all.deb's control file!
-v0*=There have been errors!
EOF
echo "Maintainer: noone <me@nowhere>" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=No Description field in ./temp/debfilename_debfileversion~2_all.deb's control file!
-v0*=There have been errors!
EOF
echo ...
echo "Description: test-package" >> pkg/DEBIAN/control
echo " a package to test reprepro" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=No Architecture field in ./temp/debfilename_debfileversion~2_all.deb's control file!
-v0*=There have been errors!
EOF
echo "Architecture: coal" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Name part of filename ('debfilename') and name within the file ('indebname') do not match for 'debfilename_debfileversion~2_all.deb' in 'test.changes'!
-v0*=There have been errors!
EOF
mv i/debfilename_debfileversion~2_all.deb i/indebname_debfileversion~2_all.deb
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Architecture 'coal' of 'indebname_debfileversion~2_all.deb' does not match 'all' specified in 'test.changes'!
-v0*=There have been errors!
EOF
sed -i -e "s/^Architecture: coal/Architecture: all/" pkg/DEBIAN/control
dpkg-deb -b pkg i/indebname_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/indebname_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/indebname_debfileversion~2_all.deb)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Source header 'sourceinchanges' of 'test.changes' and source name 'sourceindeb' within the file 'indebname_debfileversion~2_all.deb' do not match!
-v0*=There have been errors!
EOF
sed -i -e 's/sourceinchanges/sourceindeb/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Source version '999:versioninchanges-0~' of 'test.changes' and source version 'sourceversionindeb' within the file 'indebname_debfileversion~2_all.deb' do not match!
-v0*=There have been errors!
EOF
sed -i -e 's/999:versioninchanges-0~/sourceversionindeb/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Name 'indebname' of binary 'indebname_debfileversion~2_all.deb' is not listed in Binaries header of 'test.changes'!
-v0*=There have been errors!
EOF
sed -i -e 's/binaryinchanges/indebname/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No section found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
-v0*=There have been errors!
EOF
echo "Section: sectiontest" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/indebname_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/indebname_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/indebname_debfileversion~2_all.deb)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No section found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S test - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No priority found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
-v0*=There have been errors!
EOF
echo "Priority: survival" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/indebname_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/indebname_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/indebname_debfileversion~2_all.deb)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S test - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No priority found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DEBMD5S section priority indebname_debfileversion~2_all.deb" >> i/test.changes
checknolog logfile
testrun - -b . processincoming default 3<<EOF
returns 0
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
stdout
-v2*=Created directory "./pool/dog/s"
-v2*=Created directory "./pool/dog/s/sourceindeb"
-e1*=db: 'pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb' added to files.db(md5sums).
-d1*=db: 'pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb' added to checksums.db(pool).
-d1*=db: 'indebname' added to packages.db(A|dog|${FAKEARCHITECTURE}).
-d1*=db: 'indebname' added to packages.db(A|dog|calculator).
-v3*=deleting './i/indebname_debfileversion~2_all.deb'...
-v3*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v6*= looking for changes in 'A|cat|${FAKEARCHITECTURE}'...
-v6*=  replacing './dists/A/dog/binary-${FAKEARCHITECTURE}/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'A|cat|calculator'...
-v6*=  replacing './dists/A/dog/binary-calculator/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'A|dog|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'A|dog|calculator'...
EOF
checklog logfile <<EOF
DATESTR add A deb dog ${FAKEARCHITECTURE} indebname 1:versionindeb~1
DATESTR add A deb dog calculator indebname 1:versionindeb~1
EOF
find pool/dog/s -type f > results
echo "pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb" > results.expected
dodiff results.expected results

touch i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
cat > i/test.changes <<EOF
Source: sourceinchanges
Binary: nothing
Architecture: all
Version: 1:versioninchanges
Distribution: A
Files: 
 ffff 666 - - dscfilename_fileversion~.dsc
EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*='source' is not listed in the Architecture header of 'test.changes' but file 'dscfilename_fileversion~.dsc' looks like it!
-v0*=There have been errors!
EOF
sed -i -e 's/^Architecture: all$/Architecture: source/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*='test.changes' lists architecture 'source' not found in distribution 'A'!
-v0*=There have been errors!
EOF
sed -i -e 's/^Distribution: A$/Distribution: B/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 254
stderr
-v0=Data seems not to be signed trying to use directly...
*=ERROR: File 'dscfilename_fileversion~.dsc' does not match expectations:
*=md5 expected: ffff, got: $EMPTYMD5ONLY
*=size expected: 666, got: 0
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Unexpected empty file 'dscfilename_fileversion~.dsc'!
-v0*=There have been errors!
EOF
#*=Could only find spaces within './temp/dscfilename_fileversion~.dsc'!
echo "Dummyheader:" > i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Missing 'Source' field in dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Source: nameindsc" > i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Cannot find 'Format' field in dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Format: 1.0" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Cannot find 'Maintainer' field in dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Maintainer: guess who <me@nowhere>" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Missing 'Version' field in dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Standards-Version: 0" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Missing 'Version' field in dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Version: versionindsc" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Missing 'Files' field in 'dscfilename_fileversion~.dsc'!
-v0*=There have been errors!
EOF
echo "Files:  " >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Name part of filename ('dscfilename') and name within the file ('nameindsc') do not match for 'dscfilename_fileversion~.dsc' in 'test.changes'!
-v0*=There have been errors!
EOF
sed -i 's/^Source: nameindsc$/Source: dscfilename/g' i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Source header 'sourceinchanges' of 'test.changes' and name 'dscfilename' within the file 'dscfilename_fileversion~.dsc' do not match!
-v0*=There have been errors!
EOF
sed -i 's/^Source: sourceinchanges$/Source: dscfilename/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Source version '1:versioninchanges' of 'test.changes' and version 'versionindsc' within the file 'dscfilename_fileversion~.dsc' do not match!
-v0*=There have been errors!
EOF
sed -i 's/^Version: 1:versioninchanges$/Version: versionindsc/' i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No section found for 'dscfilename' ('dscfilename_fileversion~.dsc' in 'test.changes')!
-v0*=There have been errors!
EOF
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No priority found for 'dscfilename' ('dscfilename_fileversion~.dsc' in 'test.changes')!
-v0*=There have been errors!
EOF
printf "g/^Format:/d\nw\nq\n" | ed -s i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy can't-live-without dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Cannot find 'Format' field in dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
printf "1i\nFormat: 1.0\n.\nw\nq\n" | ed -s i/dscfilename_fileversion~.dsc
DSCMD5S="$(mdandsize i/dscfilename_fileversion~.dsc )"
OLDDSCFILENAMEMD5S="$DSCMD5S"
OLDDSCFILENAMESHA1S="$(sha1andsize i/dscfilename_fileversion~.dsc)"
OLDDSCFILENAMESHA2S="$(sha2andsize i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy can't-live-without dscfilename_fileversion~.dsc" >> i/test.changes
checknolog logfile
testrun - -b . processincoming default 3<<EOF
returns 0
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
stdout
-v2*=Created directory "./pool/dog/d"
-v2*=Created directory "./pool/dog/d/dscfilename"
-e1*=db: 'pool/dog/d/dscfilename/dscfilename_versionindsc.dsc' added to files.db(md5sums).
-d1*=db: 'pool/dog/d/dscfilename/dscfilename_versionindsc.dsc' added to checksums.db(pool).
-d1*=db: 'dscfilename' added to packages.db(B|dog|source).
-v3*=deleting './i/dscfilename_fileversion~.dsc'...
-v3*=deleting './i/test.changes'...
-v0=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
EOF
checklog logfile <<EOF
DATESTR add B dsc dog source dscfilename versionindsc
EOF
# TODO: check Sources.gz
cat >i/strangefile <<EOF
just a line to make it non-empty
EOF
cat >i/dscfilename_fileversion~.dsc <<EOF
Format: 1.0
Source: dscfilename
Maintainer: guess who <me@nowhere>
Standards-Version: 0
Version: 1:newversion~
Files: 
 md5sumindsc sizeindsc strangefile
EOF
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
cat >i/test.changes <<EOF
Source: dscfilename
Binary: nothing
Architecture: source
Version: 1:newversion~
Distribution: B
Files: 
 $DSCMD5S dummy can't-live-without dscfilename_fileversion~.dsc
EOF
# this is a stupid error message, needs to get some context
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Error parsing md5 checksum line ' md5sumindsc sizeindsc strangefile' within 'dscfilename_fileversion~.dsc'
-v0*=There have been errors!
EOF
sed -i "s/ md5sumindsc / dddddddddddddddddddddddddddddddd /" i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy unneeded dscfilename_fileversion~.dsc" >> i/test.changes
# this is a stupid error message, needs to get some context
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Error parsing md5 checksum line ' dddddddddddddddddddddddddddddddd sizeindsc strangefile' within 'dscfilename_fileversion~.dsc'
-v0*=There have been errors!
EOF
sed -i "s/ sizeindsc / 666 /" i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy unneeded dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=file 'strangefile' is needed for 'dscfilename_fileversion~.dsc', not yet registered in the pool and not found in 'test.changes'
-v0*=There have been errors!
EOF
echo " 11111111111111111111111111111111 666 - - strangefile" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=No underscore found in file name in '11111111111111111111111111111111 666 - - strangefile'!
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " 11111111111111111111111111111111 666 - - strangefile_xyz" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown file type: '11111111111111111111111111111111 666 - - strangefile_xyz', assuming source format...
*=In 'test.changes': file 'strangefile_xyz' not found in the incoming dir!
-v0*=There have been errors!
EOF
mv i/strangefile i/strangefile_xyz
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown file type: '11111111111111111111111111111111 666 - - strangefile_xyz', assuming source format...
*=file 'strangefile' is needed for 'dscfilename_fileversion~.dsc', not yet registered in the pool and not found in 'test.changes'
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " dddddddddddddddddddddddddddddddd 666 - - strangefile_xyz" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 254
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown file type: 'dddddddddddddddddddddddddddddddd 666 - - strangefile_xyz', assuming source format...
*=ERROR: File 'strangefile_xyz' does not match expectations:
*=md5 expected: dddddddddddddddddddddddddddddddd, got: 31a1096ff883d52f0c1f39e652d6336f
*=size expected: 666, got: 33
-v0*=There have been errors!
EOF
printf '$d\nw\nq\n' | ed -s i/dscfilename_fileversion~.dsc
echo " 31a1096ff883d52f0c1f39e652d6336f 33 strangefile_xyz" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(mdandsize i/dscfilename_fileversion~.dsc)"
DSCSHA1S="$(sha1andsize i/dscfilename_fileversion~.dsc)"
DSCSHA2S="$(sha2andsize i/dscfilename_fileversion~.dsc)"
DSCFILENAMEMD5S="$DSCMD5S"
DSCFILENAMESHA1S="$DSCSHA1S"
DSCFILENAMESHA2S="$DSCSHA2S"
printf '$-1,$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy unneeded dscfilename_fileversion~.dsc" >> i/test.changes
echo " 33a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown file type: '33a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz', assuming source format...
*=file 'strangefile_xyz' has conflicting checksums listed in 'test.changes' and 'dscfilename_fileversion~.dsc'!
-v0*=There have been errors!
EOF
find pool -type f | LC_ALL=C sort -f > results
cat > results.expected <<EOF
pool/cat/b/bird/bird-addons_1_all.deb
pool/cat/b/bird/bird_1.dsc
pool/cat/b/bird/bird_1.tar.gz
pool/cat/b/bird/bird_1_${FAKEARCHITECTURE}.deb
pool/dog/b/bird/bird-addons_1_all.deb
pool/dog/b/bird/bird_1.dsc
pool/dog/b/bird/bird_1.tar.gz
pool/dog/b/bird/bird_1_${FAKEARCHITECTURE}.deb
pool/dog/d/dscfilename/dscfilename_versionindsc.dsc
pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb
EOF
dodiff results.expected results
find dists -type f | LC_ALL=C sort -f > results
cat > results.expected <<EOF
dists/A/cat/binary-${FAKEARCHITECTURE}/Packages
dists/A/cat/binary-${FAKEARCHITECTURE}/Packages.gz
dists/A/cat/binary-${FAKEARCHITECTURE}/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-${FAKEARCHITECTURE}/Packages
dists/A/dog/binary-${FAKEARCHITECTURE}/Packages.gz
dists/A/dog/binary-${FAKEARCHITECTURE}/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-${FAKEARCHITECTURE}/Packages
dists/B/cat/binary-${FAKEARCHITECTURE}/Packages.gz
dists/B/cat/binary-${FAKEARCHITECTURE}/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-${FAKEARCHITECTURE}.gz
dists/B/dog/binary-${FAKEARCHITECTURE}/Packages
dists/B/dog/binary-${FAKEARCHITECTURE}/Packages.gz
dists/B/dog/binary-${FAKEARCHITECTURE}/Release
dists/B/dog/source/Release
dists/B/dog/source/Sources.gz
dists/B/Release
EOF
dodiff results.expected results
gunzip -c dists/B/dog/source/Sources.gz > results
withoutchecksums pool/dog/b/bird/bird_1.dsc >bird.preprocessed
ed -s bird.preprocessed <<EOF
H
/^Source: / m 0
s/^Source: /Package: /
/^Files: / kf
'f i
Priority: superfluous
Section: tasty
Directory: pool/dog/b/bird
.
'f a
 $BIRDDSCMD5S bird_1.dsc
.
$ a
Checksums-Sha1: 
 $BIRDDSCSHA1S bird_1.dsc
 $BIRDTARSHA1S bird_1.tar.gz
Checksums-Sha256: 
 $BIRDDSCSHA2S bird_1.dsc
 $BIRDTARSHA2S bird_1.tar.gz

.
w
q
EOF
cat bird.preprocessed - > results.expected <<EOF
Package: dscfilename
Format: 1.0
Maintainer: guess who <me@nowhere>
Standards-Version: 0
Version: versionindsc
Priority: can't-live-without
Section: dummy
Directory: pool/dog/d/dscfilename
Files: 
 $OLDDSCFILENAMEMD5S dscfilename_versionindsc.dsc
Checksums-Sha1: 
 $OLDDSCFILENAMESHA1S dscfilename_versionindsc.dsc
Checksums-Sha256: 
 $OLDDSCFILENAMESHA2S dscfilename_versionindsc.dsc

EOF
dodiff results.expected results
testout "" -b . dumpunreferenced
dodiff results.empty results
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
testrun - -b . _removereferences s=A=now 3<<EOF
EOF
testrun - -b . _removereferences s=B=now 3<<EOF
EOF
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
rm -r dists/A.snapshot
rm -r dists/B.snapshot
mv tmp/Contents-${FAKEARCHITECTURE}.gz dists/B/
##
printf '$d\nw\nq\n' | ed -s i/test.changes
echo " 31a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz" >> i/test.changes
checknolog logfile
testrun - -b . processincoming default 3<<EOF
returns 0
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown file type: '31a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz', assuming source format...
stdout
-e1*=db: 'pool/dog/d/dscfilename/dscfilename_newversion~.dsc' added to files.db(md5sums).
-d1*=db: 'pool/dog/d/dscfilename/dscfilename_newversion~.dsc' added to checksums.db(pool).
-e1*=db: 'pool/dog/d/dscfilename/strangefile_xyz' added to files.db(md5sums).
-d1*=db: 'pool/dog/d/dscfilename/strangefile_xyz' added to checksums.db(pool).
-d1*=db: 'dscfilename' removed from packages.db(B|dog|source).
-d1*=db: 'dscfilename' added to packages.db(B|dog|source).
-v3*=deleting './i/dscfilename_fileversion~.dsc'...
-v3*=deleting './i/test.changes'...
-v3*=deleting './i/strangefile_xyz'...
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|${FAKEARCHITECTURE}'...
-v6*= looking for changes in 'B|cat|source'...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/dog/d/dscfilename/dscfilename_versionindsc.dsc
-e1*=db: 'pool/dog/d/dscfilename/dscfilename_versionindsc.dsc' removed from files.db(md5sums).
-d1*=db: 'pool/dog/d/dscfilename/dscfilename_versionindsc.dsc' removed from checksums.db(pool).
EOF
checklog logfile <<EOF
DATESTR replace B dsc dog source dscfilename 1:newversion~ versionindsc
EOF

find pool -type f | LC_ALL=C sort -f > results
cat > results.expected <<EOF
pool/cat/b/bird/bird-addons_1_all.deb
pool/cat/b/bird/bird_1.dsc
pool/cat/b/bird/bird_1.tar.gz
pool/cat/b/bird/bird_1_${FAKEARCHITECTURE}.deb
pool/dog/b/bird/bird-addons_1_all.deb
pool/dog/b/bird/bird_1.dsc
pool/dog/b/bird/bird_1.tar.gz
pool/dog/b/bird/bird_1_${FAKEARCHITECTURE}.deb
pool/dog/d/dscfilename/dscfilename_newversion~.dsc
pool/dog/d/dscfilename/strangefile_xyz
pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb
EOF
dodiff results.expected results
find dists -type f | LC_ALL=C sort -f > results
cat > results.expected <<EOF
dists/A/cat/binary-${FAKEARCHITECTURE}/Packages
dists/A/cat/binary-${FAKEARCHITECTURE}/Packages.gz
dists/A/cat/binary-${FAKEARCHITECTURE}/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-${FAKEARCHITECTURE}/Packages
dists/A/dog/binary-${FAKEARCHITECTURE}/Packages.gz
dists/A/dog/binary-${FAKEARCHITECTURE}/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-${FAKEARCHITECTURE}/Packages
dists/B/cat/binary-${FAKEARCHITECTURE}/Packages.gz
dists/B/cat/binary-${FAKEARCHITECTURE}/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-${FAKEARCHITECTURE}.gz
dists/B/dog/binary-${FAKEARCHITECTURE}/Packages
dists/B/dog/binary-${FAKEARCHITECTURE}/Packages.gz
dists/B/dog/binary-${FAKEARCHITECTURE}/Release
dists/B/dog/source/Release
dists/B/dog/source/Sources.gz
dists/B/Release
EOF
dodiff results.expected results
gunzip -c dists/B/dog/source/Sources.gz > results
cat bird.preprocessed - > results.expected <<EOF
Package: dscfilename
Format: 1.0
Maintainer: guess who <me@nowhere>
Standards-Version: 0
Version: 1:newversion~
Priority: unneeded
Section: dummy
Directory: pool/dog/d/dscfilename
Files: 
 $DSCFILENAMEMD5S dscfilename_newversion~.dsc
 31a1096ff883d52f0c1f39e652d6336f 33 strangefile_xyz
Checksums-Sha1: 
 $DSCFILENAMESHA1S dscfilename_newversion~.dsc
 4453da6ca46859b207c5b55af6213ff8369cd383 33 strangefile_xyz
Checksums-Sha256: 
 $DSCFILENAMESHA2S dscfilename_newversion~.dsc
 c40fcf711220c0ce210159d43b22f1f59274819bf3575e11cc0057ed1988a575 33 strangefile_xyz

EOF
dodiff results.expected results

testout "" -b . dumpunreferenced
dodiff results.empty results

rm -r conf db pool dists i pkg
testsuccess
