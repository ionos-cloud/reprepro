#!/bin/bash

set -e

testrun() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" -C $TESTOPTIONS "$REPREPRO" $VERBOSITY "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -r -C $TESTOPTIONS "$REPREPRO" $VERBOSITY "$@"
else
	"$TESTTOOL" -r -C $TESTOPTIONS "$REPREPRO" $VERBOSITY "$@" 3<"$rules".rules
fi
}
testout() {
rules=$1
shift
if test "x$rules" = "x" ; then
	"$TESTTOOL" -o results $TESTOPTIONS "$REPREPRO" $VERBOSITY "$@"
elif test "x$rules" = "x-" ; then
	"$TESTTOOL" -o results -r $TESTOPTIONS "$REPREPRO" $VERBOSITY "$@"
else
	"$TESTTOOL" -o results -r $TESTOPTIONS "$REPREPRO" $VERBOSITY "$@" 3<"$rules".rules
fi
}
dogrep() {
echo grep -q "$@"
grep -q "$@"
}
dongrep() {
echo "!grep" -q "$@"
! grep -q "$@"
}
dodiff() {
echo diff -u "$@"
diff -u "$@"
}
dodo() {
echo "$@"
"$@"
}

WORKDIR="`pwd`/testdir"
USE_VALGRIND=""
VALGRIND_SUP=""

if [ "x$1" == "x--delete" ] ; then
	rm -r "$WORKDIR" || true
	shift
fi
if [ "x$1" == "x--valgrind" ] ; then
	USE_VALGRIND=1
	shift
fi
if [ "x$1" == "x--valgrind-supp" ] ; then
	USE_VALGRIND=1
	shift
	VALGRIND_SUP="$1"
	shift
fi

mkdir "$WORKDIR"
cd "$WORKDIR"

if [ "1" -gt "$#" ] || [ "3" -lt "$#" ] ; then
	echo "Syntax: test.sh <src-dir> [<testtool-binary>] [<reprepro-binary>]" >&2
	exit 1
fi
SRCDIR="$1"
if [ -z "$TESTOPTIONS" ] ; then
	if [ -z "$USE_VALGRIND" ] ; then
		TESTOPTIONS="-e -a"
	elif [ -z "$VALGRIND_SUP" ] ; then
		TESTOPTIONS="-e -a --debug --leak-check=full --suppressions=$SRCDIR/valgrind.supp"
	else
		TESTOPTIONS="-e -a --debug --leak-check=full --suppressions=$VALGRIND_SUP"
	fi
fi
#TESTOPTIONS="-D v=-1 $TESTOPTIONS"
#VERBOSITY="-s"
#TESTOPTIONS="-D v=0 $TESTOPTIONS"
#VERBOSITY=""
#TESTOPTIONS="-D v=1 $TESTOPTIONS"
#VERBOSITY="-v"
#TESTOPTIONS="-D v=2 $TESTOPTIONS"
#VERBOSITY="-vv"
#TESTOPTIONS="-D v=3 $TESTOPTIONS"
#VERBOSITY="-vvv"
#TESTOPTIONS="-D v=4 $TESTOPTIONS"
#VERBOSITY="-vvvv"
#TESTOPTIONS="-D v=5 $TESTOPTIONS"
#VERBOSITY="-vvvvv"
TESTOPTIONS="-D v=6 $TESTOPTIONS"
VERBOSITY="-vvvvvv"
if [ "2" -le "$#" ] ; then
	TESTTOOL="$2"
else
	TESTTOOL=testtool
fi
if [ "3" -le "$#" ] ; then
	REPREPRO="$3"
else
	REPREPRO="$SRCDIR/reprepro"
fi
TESTOPTIONS="-D x=0 -D d=1 $TESTOPTIONS"
VERBOSITY="--verbosedb $VERBOSITY"
TESTS="$SRCDIR/tests"
UPDATETYPE=update
export PATH="$TESTS:$PATH"
if ! [ -x "$REPREPRO" ] ; then
	echo "Could not find $REPREPRO!" >&2
	exit 1
fi
TESTTOOLVERSION="`$TESTTOOL --version`"
case $TESTTOOLVERSION in
	"testtool version "*) ;;
	*) echo "Failed to get version of testtool($TESTTOOL)"
	   exit 1
	   ;;
esac
touch results.empty

dodo test ! -d db
testrun - -b . _versioncompare 0 1 3<<EOF
stdout
*='0' is smaller than '1'.
EOF
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
# touch conf/updates
# dodo test ! -d db
# testrun - -b . checkupdate test 3<<EOF
# stderr
# *=Nothing to do found. (Use --noskipold to force processing)
# stdout
# -v2*=Created directory "./db"
# -v2=Created directory "./lists"
# -v2*=Removed empty directory "./db"
# EOF
# dodo test ! -d db
# mkdir d
# testrun - -b . --dbdir d/ab/c//x checkupdate test 3<<EOF
# stderr
# *=Nothing to do found. (Use --noskipold to force processing)
# stdout
# -v2*=Created directory "d/ab"
# -v2*=Created directory "d/ab/c"
# -v2*=Created directory "d/ab/c//x"
# -v2=Created directory "./lists"
# -v2*=Removed empty directory "d/ab/c//x"
# -v2*=Removed empty directory "d/ab/c"
# -v2*=Removed empty directory "d/ab"
# EOF
# rm -r -f lists
rm -r conf
dodo test ! -d d/ab
mkdir -p conf
cat > conf/options <<CONFEND
export changed
CONFEND
cat > conf/distributions <<CONFEND
Codename: A
Architectures: abacus calculator
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
Architectures: abacus calculator
Components: dog cat
Log: logfile
 -A
CONFEND
testrun - -b . export 3<<EOF
return 255
*=Log notifier option -A misses an argument in ./conf/distributions, line 5, column 3
-v0*=There have been errors!
EOF
cat > conf/distributions <<CONFEND
Codename: A
Architectures: abacus calculator
Components: dog cat
Log: logfile
 -A=abacus
CONFEND
testrun - -b . export 3<<EOF
return 255
*=Error parsing config file ./conf/distributions, line 5, column 10:
*=Unexpected end of line: name of notifier script missing!
-v0*=There have been errors!
EOF
cat > conf/distributions <<CONFEND
Codename: A
Architectures: abacus calculator
Components: dog cat
Log: logfile
 -A=abacus --architecture=coal
CONFEND
testrun - -b . export 3<<EOF
return 255
*=Repeated notifier option --architecture in ./conf/distributions, line 5, column 12!
-v0*=There have been errors!
EOF
cat > conf/distributions <<CONFEND
Codename: A
Architectures: abacus calculator
Components: dog cat
Log: logfile
 -A=nonexistant -C=nocomponent --type=none --withcontrol noscript.sh

Codename: B
Architectures: abacus source
Components: dog cat
Contents: 1
Log: logfile
CONFEND
testrun - -b . export 3<<EOF
stdout
-v2*=Created directory "./db"
-v1*=Exporting B...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/B"
-v2*=Created directory "./dists/B/dog"
-v2*=Created directory "./dists/B/dog/binary-abacus"
-v6*= exporting 'B|dog|abacus'...
-v6*=  creating './dists/B/dog/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/B/dog/source"
-v6*= exporting 'B|dog|source'...
-v6*=  creating './dists/B/dog/source/Sources' (gzipped)
-v2*=Created directory "./dists/B/cat"
-v2*=Created directory "./dists/B/cat/binary-abacus"
-v6*= exporting 'B|cat|abacus'...
-v2*=Created directory "./dists/B/cat/source"
-v6*=  creating './dists/B/cat/binary-abacus/Packages' (uncompressed,gzipped)
-v6*= exporting 'B|cat|source'...
-v6*=  creating './dists/B/cat/source/Sources' (gzipped)
-v1*= generating Contents-abacus...
-v1*=Exporting A...
-v2*=Created directory "./dists/A"
-v2*=Created directory "./dists/A/dog"
-v2*=Created directory "./dists/A/dog/binary-abacus"
-v6*= exporting 'A|dog|abacus'...
-v6*=  creating './dists/A/dog/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/dog/binary-calculator"
-v6*= exporting 'A|dog|calculator'...
-v6*=  creating './dists/A/dog/binary-calculator/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/cat"
-v2*=Created directory "./dists/A/cat/binary-abacus"
-v6*= exporting 'A|cat|abacus'...
-v6*=  creating './dists/A/cat/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/A/cat/binary-calculator"
-v6*= exporting 'A|cat|calculator'...
-v6*=  creating './dists/A/cat/binary-calculator/Packages' (uncompressed,gzipped)
EOF
find dists -type f | LC_ALL=C sort -f > results
cat > results.expected <<END
dists/A/cat/binary-abacus/Packages
dists/A/cat/binary-abacus/Packages.gz
dists/A/cat/binary-abacus/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-abacus/Packages
dists/A/dog/binary-abacus/Packages.gz
dists/A/dog/binary-abacus/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-abacus/Packages
dists/B/cat/binary-abacus/Packages.gz
dists/B/cat/binary-abacus/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-abacus.gz
dists/B/dog/binary-abacus/Packages
dists/B/dog/binary-abacus/Packages.gz
dists/B/dog/binary-abacus/Release
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
*=Unknown flag in Permit-header. (but not within the rule we are intrested in.)
*=Warning: ignored error parsing config file ./conf/incoming, line 6, column 23:
*=Unknown flag in Cleanup-header. (but not within the rule we are intrested in.)
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
*=There ia neither a 'Allow' nor a 'Default' definition in rule 'default'
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
DSCMD5S="$(md5sum i/bird_1.dsc | cut -d' ' -f1) $(stat -c '%s' i/bird_1.dsc)"
TARMD5S="$(md5sum i/bird_1.tar.gz | cut -d' ' -f1) $(stat -c '%s' i/bird_1.tar.gz)"
DEBMD5="$(md5sum i/bird_1_abacus.deb | cut -d' ' -f1)"
DEBSIZE="$(stat -c '%s' i/bird_1_abacus.deb)"
DEBAMD5="$(md5sum i/bird-addons_1_all.deb | cut -d' ' -f1)"
DEBASIZE="$(stat -c '%s' i/bird-addons_1_all.deb)"
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
testrun - -b . processincoming default 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v9*=Adding reference to 'pool/dog/b/bird/bird_1.dsc' by 'B|dog|source'
-v9*=Adding reference to 'pool/dog/b/bird/bird_1.tar.gz' by 'B|dog|source'
-v9*=Adding reference to 'pool/dog/b/bird/bird_1_abacus.deb' by 'B|dog|abacus'
-v9*=Adding reference to 'pool/dog/b/bird/bird-addons_1_all.deb' by 'B|dog|abacus'
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/dog"
-v2*=Created directory "./pool/dog/b"
-v2*=Created directory "./pool/dog/b/bird"
-v2*=Created directory "./logs"
-d1*=db: 'bird' added to packages.db(B|dog|source).
-d1*=db: 'bird' added to packages.db(B|dog|abacus).
-d1*=db: 'bird-addons' added to packages.db(B|dog|abacus).
-v3*=deleting './i/bird_1.dsc'...
-v3*=deleting './i/bird_1.tar.gz'...
-v3*=deleting './i/bird_1_abacus.deb'...
-v3*=deleting './i/bird-addons_1_all.deb'...
-v3*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|abacus'...
-v6*=  replacing './dists/B/dog/binary-abacus/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|abacus'...
-v6*= looking for changes in 'B|cat|source'...
-v1*= generating Contents-abacus...
-v4*=Reading filelist for pool/dog/b/bird/bird_1_abacus.deb
-v4*=Reading filelist for pool/dog/b/bird/bird-addons_1_all.deb
EOF
LOGDATE="$(date +'%Y-%m-%d %H:')"
echo normalizing logfile: DATESTR is "$LOGDATE??:??"
sed -i -e 's/^'"$LOGDATE"'[0-9][0-9]:[0-9][0-9] /DATESTR /g' logs/logfile
cat > results.log.expected <<EOF
DATESTR add B dsc dog source bird 1
DATESTR add B deb dog abacus bird 1
DATESTR add B deb dog abacus bird-addons 1
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
gunzip -c dists/B/Contents-abacus.gz > results
dodiff results.expected results
cat > results.expected <<EOF
Package: bird
Version: 1
Architecture: abacus
Installed-Size: 20
Maintainer: me <guess@who>
Priority: superfluous
Section: tasty
Filename: pool/dog/b/bird/bird_1_abacus.deb
Size: $DEBSIZE
MD5sum: $DEBMD5
Description: bla
 blub

Package: bird-addons
Version: 1
Architecture: all
Installed-Size: 24
Maintainer: me <guess@who>
Source: bird
Priority: superfluous
Section: tasty
Filename: pool/dog/b/bird/bird-addons_1_all.deb
Size: $DEBASIZE
MD5sum: $DEBAMD5
Description: bla
 blub

EOF
dodiff results.expected dists/B/dog/binary-abacus/Packages
cat > results.expected <<EOF
Package: bird
Format: 1.0
Version: 1
Binary: bird, bird-addons
Maintainer: me <guess@who>
Architecture: any
Standards-Version: 0.0
Priority: superfluous
Section: tasty
Directory: pool/dog/b/bird
Files: 
 $DSCMD5S bird_1.dsc
 $TARMD5S bird_1.tar.gz

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
-d1*=db: 'bird' added to packages.db(B|cat|source).
-d1*=db: 'bird' added to packages.db(B|cat|abacus).
-d1*=db: 'bird-addons' added to packages.db(B|cat|abacus).
-v7*=db: pool/cat/b/bird/bird_1.dsc: file added.
-v7*=db: pool/cat/b/bird/bird_1.tar.gz: file added.
-v7*=db: pool/cat/b/bird/bird_1_abacus.deb: file added.
-v7*=db: pool/cat/b/bird/bird-addons_1_all.deb: file added.
-v3*=deleting './i/bird_1.dsc'...
-v3*=deleting './i/bird_1.tar.gz'...
-v3*=deleting './i/bird_1_abacus.deb'...
-v3*=deleting './i/bird-addons_1_all.deb'...
-v3*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v6*= looking for changes in 'B|cat|abacus'...
-v6*= looking for changes in 'B|cat|source'...
-v6*= looking for changes in 'B|dog|abacus'...
-v6*=  replacing './dists/B/cat/binary-abacus/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/cat/source/Sources' (gzipped)
-v1*= generating Contents-abacus...
-v4*=Reading filelist for pool/cat/b/bird/bird_1_abacus.deb
-v4*=Reading filelist for pool/cat/b/bird/bird-addons_1_all.deb
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
DATESTR add B deb dog abacus bird 1
DATESTR add B deb dog abacus bird-addons 1
DATESTR add B dsc cat source bird 1
DATESTR add B deb cat abacus bird 1
DATESTR add B deb cat abacus bird-addons 1
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
gunzip -c dists/B/Contents-abacus.gz > results
dodiff results.expected results
cat > results.expected <<EOF
Package: bird
Version: 1
Architecture: abacus
Installed-Size: 20
Maintainer: me <guess@who>
Task: lunch
Priority: hungry
Section: cat/tasty
Filename: pool/cat/b/bird/bird_1_abacus.deb
Size: $DEBSIZE
MD5sum: $DEBMD5
Description: bla
 blub

Package: bird-addons
Version: 1
Architecture: all
Installed-Size: 24
Maintainer: me <guess@who>
Source: bird
Priority: superfluous
Section: cat/ugly
Filename: pool/cat/b/bird/bird-addons_1_all.deb
Size: $DEBASIZE
MD5sum: $DEBAMD5
Description: bla
 blub

EOF
dodiff results.expected dists/B/cat/binary-abacus/Packages
cat > results.expected <<EOF
Package: bird
Format: 1.0
Version: 1
Binary: bird, bird-addons
Maintainer: me <guess@who>
Architecture: any
Standards-Version: 0.0
Homepage: gopher://tree
Priority: hurry
Section: cat/nest
Directory: pool/cat/b/bird
Files: 
 $DSCMD5S bird_1.dsc
 $TARMD5S bird_1.tar.gz

EOF
BIRDDSCMD5S="$DSCMD5S"
BIRDTARMD5S="$TARMD5S"
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
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_coal.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_coal.deb)"
cat > i/test.changes <<EOF
EOF
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Could only find spaces within 'test.changes'!
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
echo " md5sum size - - ../ööü_v_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
-v0=Data seems not to be signed trying to use directly...
*=In 'test.changes': file '../ööü_v_all.deb' not found in the incoming dir!
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " md5sum size - - \300\257.\300\257_v_funny.deb" >> i/test.changes
touch "$(echo -e 'i/\300\257.\300\257_v_funny.deb')"
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*='test.changes' lists architecture 'funny' not found in distribution 'A'!
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " md5sum size - - \300\257.\300\257_v_all.deb" >> i/test.changes
mv "$(echo -e 'i/\300\257.\300\257_v_funny.deb')" "$(echo -e 'i/\300\257.\300\257_v_all.deb')"
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
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " md5sum size - - debfilename_debfileversion~2_coal.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*='coal' is not listed in the Architecture header of 'test.changes' but file 'debfilename_debfileversion~2_coal.deb' looks like it!
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " md5sum size - - debfilename_debfileversion~2_all.deb" >> i/test.changes
mv i/debfilename_debfileversion~2_coal.deb i/debfilename_debfileversion~2_all.deb
# // TODO: that should be ERROR: instead of WARNING:
testrun - -b . processincoming default 3<<EOF
returns 254
stderr
-v0=Data seems not to be signed trying to use directly...
*=WARNING: './i/debfilename_debfileversion~2_all.deb' has md5sum '$DEBMD5S', while 'md5sum size' was expected.
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
# TODO: these will hopefully change to not divulge the place of the temp dir some day...
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Cannot find Maintainer-header in control file of ./temp/debfilename_debfileversion~2_all.deb!
-v0*=There have been errors!
EOF
echo "Maintainer: noone <me@nowhere>" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Cannot find Description-header in control file of ./temp/debfilename_debfileversion~2_all.deb!
-v0*=There have been errors!
EOF
echo ...
echo "Description: test-package" >> pkg/DEBIAN/control
echo " a package to test reprepro" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Cannot find Architecture-header in control file of ./temp/debfilename_debfileversion~2_all.deb!
-v0*=There have been errors!
EOF
echo "Architecture: coal" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/debfilename_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/debfilename_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/debfilename_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - debfilename_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Name part of filename ('debfilename') and name within the file ('indebname') do not match for 'debfilename_debfileversion~2_all.deb' in 'test.changes'!
-v0*=There have been errors!
EOF
mv i/debfilename_debfileversion~2_all.deb i/indebname_debfileversion~2_all.deb
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
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
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Source-header 'sourceinchanges' of 'test.changes' and source name 'sourceindeb' within the file 'indebname_debfileversion~2_all.deb' do not match!
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
*=Name 'indebname' of binary 'indebname_debfileversion~2_all.deb' is not listed in Binaries-header of 'test.changes'!
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
echo "Section: test" >> pkg/DEBIAN/control
dpkg-deb -b pkg i/indebname_debfileversion~2_all.deb
DEBMD5S="$(md5sum i/indebname_debfileversion~2_all.deb | cut -d' ' -f1) $(stat -c '%s' i/indebname_debfileversion~2_all.deb)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S - - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No section found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S test - indebname_debfileversion~2_all.deb" >> i/test.changes
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
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S test - indebname_debfileversion~2_all.deb" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No priority found for 'indebname' ('indebname_debfileversion~2_all.deb' in 'test.changes')!
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo -e " $DEBMD5S section priority indebname_debfileversion~2_all.deb" >> i/test.changes
checknolog logfile
testrun - -b . processincoming default 3<<EOF
returns 0
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'sourceversionindeb' does not start with a digit, violating 'should'-directive in policy 5.6.11
stdout
-v2*=Created directory "./pool/dog/s"
-v2*=Created directory "./pool/dog/s/sourceindeb"
-d1*=db: 'indebname' added to packages.db(A|dog|abacus).
-d1*=db: 'indebname' added to packages.db(A|dog|calculator).
-v3*=deleting './i/indebname_debfileversion~2_all.deb'...
-v3*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v6*= looking for changes in 'A|cat|abacus'...
-v6*=  replacing './dists/A/dog/binary-abacus/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'A|cat|calculator'...
-v6*=  replacing './dists/A/dog/binary-calculator/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'A|dog|abacus'...
-v6*= looking for changes in 'A|dog|calculator'...
EOF
checklog logfile <<EOF
DATESTR add A deb dog abacus indebname 1:versionindeb~1
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
 md5sum size - - dscfilename_fileversion~.dsc
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
*=WARNING: './i/dscfilename_fileversion~.dsc' has md5sum 'd41d8cd98f00b204e9800998ecf8427e 0', while 'md5sum size' was expected.
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Could only find spaces within './temp/dscfilename_fileversion~.dsc'!
-v0*=There have been errors!
EOF
echo "Dummyheader:" > i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Missing 'Source'-header in ./temp/dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Source: nameindsc" > i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Cannot find 'Format'-header in ./temp/dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Format: 1.0" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Cannot find 'Maintainer'-header in ./temp/dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Maintainer: guess who <me@nowhere>" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Cannot find 'Standards-Version'-header in ./temp/dscfilename_fileversion~.dsc!
*=Missing 'Version'-header in ./temp/dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Standards-Version: 0" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Missing 'Version'-header in ./temp/dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Version: versionindsc" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Missing 'Files'-header in ./temp/dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo "Files:  " >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
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
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S - - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=Source-header 'sourceinchanges' of 'test.changes' and name 'dscfilename' within the file 'dscfilename_fileversion~.dsc' do not match!
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
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy - dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=No priority found for 'dscfilename' ('dscfilename_fileversion~.dsc' in 'test.changes')!
-v0*=There have been errors!
EOF
echo -e "g/^Format:/d\nw\nq\n" | ed -s i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy can't-live-without dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=Cannot find 'Format'-header in ./temp/dscfilename_fileversion~.dsc!
-v0*=There have been errors!
EOF
echo -e "1i\nFormat: 1.0\n.\nw\nq\n" | ed -s i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
OLDDSCFILENAMEMD5S="$DSCMD5S"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy can't-live-without dscfilename_fileversion~.dsc" >> i/test.changes
checknolog logfile
testrun - -b . processincoming default 3<<EOF
returns 0
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
stdout
-d1*=db: 'dscfilename' added to packages.db(B|dog|source).
-v2*=Created directory "./pool/dog/d"
-v2*=Created directory "./pool/dog/d/dscfilename"
-v3*=deleting './i/dscfilename_fileversion~.dsc'...
-v3*=deleting './i/test.changes'...
-v0=Exporting indices...
-v6*= looking for changes in 'B|dog|abacus'...
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|abacus'...
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
*=Error in parsing size or missing space afterwards!
-v0*=There have been errors!
EOF
sed -i "s/ sizeindsc / 666 /" i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy unneeded dscfilename_fileversion~.dsc" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'versionindsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=file 'strangefile' is needed for 'dscfilename_fileversion~.dsc', not yet registered in the pool and not found in 'test.changes'
-v0*=There have been errors!
EOF
echo " md5suminchanges 666 - - strangefile" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
*=No underscore in filename in 'md5suminchanges 666 - - strangefile'!
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " md5suminchanges 666 - - strangefile_xyz" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 249
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown filetype: 'md5suminchanges 666 - - strangefile_xyz', assuming to be source format...
*=In 'test.changes': file 'strangefile_xyz' not found in the incoming dir!
-v0*=There have been errors!
EOF
mv i/strangefile i/strangefile_xyz
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown filetype: 'md5suminchanges 666 - - strangefile_xyz', assuming to be source format...
*=file 'strangefile' is needed for 'dscfilename_fileversion~.dsc', not yet registered in the pool and not found in 'test.changes'
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " md5sumindsc 666 - - strangefile_xyz" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 254
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown filetype: 'md5sumindsc 666 - - strangefile_xyz', assuming to be source format...
*=WARNING: './i/strangefile_xyz' has md5sum '31a1096ff883d52f0c1f39e652d6336f 33', while 'md5sumindsc 666' was expected.
-v0*=There have been errors!
EOF
echo -e '$d\nw\nq\n' | ed -s i/dscfilename_fileversion~.dsc
echo " 31a1096ff883d52f0c1f39e652d6336f 33 strangefile_xyz" >> i/dscfilename_fileversion~.dsc
DSCMD5S="$(md5sum i/dscfilename_fileversion~.dsc | cut -d' ' -f1) $(stat -c '%s' i/dscfilename_fileversion~.dsc)"
DSCFILENAMEMD5S="$DSCMD5S"
echo -e '$-1,$d\nw\nq\n' | ed -s i/test.changes
echo " $DSCMD5S dummy unneeded dscfilename_fileversion~.dsc" >> i/test.changes
echo " 33a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz" >> i/test.changes
testrun - -b . processincoming default 3<<EOF
returns 255
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown filetype: '33a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz', assuming to be source format...
*=file 'strangefile_xyz' is listed with md5sum '33a1096ff883d52f0c1f39e652d6336f 33' in 'test.changes' but with md5sum '31a1096ff883d52f0c1f39e652d6336f 33' in 'dscfilename_fileversion~.dsc'!
-v0*=There have been errors!
EOF
find pool -type f | LC_ALL=C sort -f > results
cat > results.expected <<EOF
pool/cat/b/bird/bird-addons_1_all.deb
pool/cat/b/bird/bird_1.dsc
pool/cat/b/bird/bird_1.tar.gz
pool/cat/b/bird/bird_1_abacus.deb
pool/dog/b/bird/bird-addons_1_all.deb
pool/dog/b/bird/bird_1.dsc
pool/dog/b/bird/bird_1.tar.gz
pool/dog/b/bird/bird_1_abacus.deb
pool/dog/d/dscfilename/dscfilename_versionindsc.dsc
pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb
EOF
dodiff results.expected results
find dists -type f | LC_ALL=C sort -f > results
cat > results.expected <<EOF
dists/A/cat/binary-abacus/Packages
dists/A/cat/binary-abacus/Packages.gz
dists/A/cat/binary-abacus/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-abacus/Packages
dists/A/dog/binary-abacus/Packages.gz
dists/A/dog/binary-abacus/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-abacus/Packages
dists/B/cat/binary-abacus/Packages.gz
dists/B/cat/binary-abacus/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-abacus.gz
dists/B/dog/binary-abacus/Packages
dists/B/dog/binary-abacus/Packages.gz
dists/B/dog/binary-abacus/Release
dists/B/dog/source/Release
dists/B/dog/source/Sources.gz
dists/B/Release
EOF
dodiff results.expected results
gunzip -c dists/B/dog/source/Sources.gz > results
cat > results.expected <<EOF
Package: bird
Format: 1.0
Version: 1
Binary: bird, bird-addons
Maintainer: me <guess@who>
Architecture: any
Standards-Version: 0.0
Priority: superfluous
Section: tasty
Directory: pool/dog/b/bird
Files: 
 $BIRDDSCMD5S bird_1.dsc
 $BIRDTARMD5S bird_1.tar.gz

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

EOF
dodiff results.expected results
testout "" -b . dumpunreferenced
dodiff results.empty results
echo -e '$d\nw\nq\n' | ed -s i/test.changes
echo " 31a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz" >> i/test.changes
checknolog logfile
testrun - -b . processincoming default 3<<EOF
returns 0
stderr
-v0=Data seems not to be signed trying to use directly...
=Unknown filetype: '31a1096ff883d52f0c1f39e652d6336f 33 - - strangefile_xyz', assuming to be source format...
stdout
-d1*=db: 'dscfilename' removed from packages.db(B|dog|source).
-d1*=db: 'dscfilename' added to packages.db(B|dog|source).
-v3*=deleting './i/dscfilename_fileversion~.dsc'...
-v3*=deleting './i/test.changes'...
-v3*=deleting './i/strangefile_xyz'...
-v0*=Exporting indices...
-v6*= looking for changes in 'B|dog|abacus'...
-v6*= looking for changes in 'B|dog|source'...
-v6*=  replacing './dists/B/dog/source/Sources' (gzipped)
-v6*= looking for changes in 'B|cat|abacus'...
-v6*= looking for changes in 'B|cat|source'...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/dog/d/dscfilename/dscfilename_versionindsc.dsc
EOF
checklog logfile <<EOF
DATESTR replace B dsc dog source dscfilename 1:newversion~ versionindsc
EOF

find pool -type f | LC_ALL=C sort -f > results
cat > results.expected <<EOF
pool/cat/b/bird/bird-addons_1_all.deb
pool/cat/b/bird/bird_1.dsc
pool/cat/b/bird/bird_1.tar.gz
pool/cat/b/bird/bird_1_abacus.deb
pool/dog/b/bird/bird-addons_1_all.deb
pool/dog/b/bird/bird_1.dsc
pool/dog/b/bird/bird_1.tar.gz
pool/dog/b/bird/bird_1_abacus.deb
pool/dog/d/dscfilename/dscfilename_newversion~.dsc
pool/dog/d/dscfilename/strangefile_xyz
pool/dog/s/sourceindeb/indebname_versionindeb~1_all.deb
EOF
dodiff results.expected results
find dists -type f | LC_ALL=C sort -f > results
cat > results.expected <<EOF
dists/A/cat/binary-abacus/Packages
dists/A/cat/binary-abacus/Packages.gz
dists/A/cat/binary-abacus/Release
dists/A/cat/binary-calculator/Packages
dists/A/cat/binary-calculator/Packages.gz
dists/A/cat/binary-calculator/Release
dists/A/dog/binary-abacus/Packages
dists/A/dog/binary-abacus/Packages.gz
dists/A/dog/binary-abacus/Release
dists/A/dog/binary-calculator/Packages
dists/A/dog/binary-calculator/Packages.gz
dists/A/dog/binary-calculator/Release
dists/A/Release
dists/B/cat/binary-abacus/Packages
dists/B/cat/binary-abacus/Packages.gz
dists/B/cat/binary-abacus/Release
dists/B/cat/source/Release
dists/B/cat/source/Sources.gz
dists/B/Contents-abacus.gz
dists/B/dog/binary-abacus/Packages
dists/B/dog/binary-abacus/Packages.gz
dists/B/dog/binary-abacus/Release
dists/B/dog/source/Release
dists/B/dog/source/Sources.gz
dists/B/Release
EOF
dodiff results.expected results
gunzip -c dists/B/dog/source/Sources.gz > results
cat > results.expected <<EOF
Package: bird
Format: 1.0
Version: 1
Binary: bird, bird-addons
Maintainer: me <guess@who>
Architecture: any
Standards-Version: 0.0
Priority: superfluous
Section: tasty
Directory: pool/dog/b/bird
Files: 
 $BIRDDSCMD5S bird_1.dsc
 $BIRDTARMD5S bird_1.tar.gz

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

EOF
dodiff results.expected results

testout "" -b . dumpunreferenced
dodiff results.empty results

#echo "preliminary finish due to testing"
#exit 0
rm -r conf db pool dists i pkg
#echo "preliminary finish due to testing"
#exit 0
###############################################################################

mkdir -p conf
cat > conf/options <<CONFEND
export changed
CONFEND
cat > conf/distributions <<CONFEND
Codename: test1
Architectures: abacus source
Components: stupid ugly
Update: Test2toTest1
DebIndices: Packages Release . .gz .bz2
UDebIndices: Packages .gz .bz2
DscIndices: Sources Release .gz .bz2
Tracking: keep includechanges includebyhand
Log: log1

Codename: test2
Architectures: abacus coal source
Components: stupid ugly
Origin: Brain
Label: Only a test
Suite: broken
Version: 9999999.02
DebIndices: Packages Release . .gz $SRCDIR/docs/bzip.example
UDebIndices: Packages .gz
DscIndices: Sources Release . .gz $SRCDIR/docs/bzip.example
Description: test with all fields set
DebOverride: binoverride
DscOverride: srcoverride
Log: log2
CONFEND

set -v
checknolog logfile
testrun - -b . export 3<<EOF
stdout
-v2*=Created directory "./db"
-v1*=Exporting test2...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test2"
-v2*=Created directory "./dists/test2/stupid"
-v2*=Created directory "./dists/test2/stupid/binary-abacus"
-v6*= exporting 'test2|stupid|abacus'...
-v6*=  creating './dists/test2/stupid/binary-abacus/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-abacus/Packages.new' 'stupid/binary-abacus/Packages' 'new'
-v11*=Exporthook successfully returned!
-v2*=Created directory "./dists/test2/stupid/binary-coal"
-v6*= exporting 'test2|stupid|coal'...
-v6*=  creating './dists/test2/stupid/binary-coal/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'new'
-v2*=Created directory "./dists/test2/stupid/source"
-v6*= exporting 'test2|stupid|source'...
-v6*=  creating './dists/test2/stupid/source/Sources' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'new'
-v2*=Created directory "./dists/test2/ugly"
-v2*=Created directory "./dists/test2/ugly/binary-abacus"
-v6*= exporting 'test2|ugly|abacus'...
-v6*=  creating './dists/test2/ugly/binary-abacus/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-abacus/Packages.new' 'ugly/binary-abacus/Packages' 'new'
-v2*=Created directory "./dists/test2/ugly/binary-coal"
-v6*= exporting 'test2|ugly|coal'...
-v6*=  creating './dists/test2/ugly/binary-coal/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'new'
-v2*=Created directory "./dists/test2/ugly/source"
-v6*= exporting 'test2|ugly|source'...
-v6*=  creating './dists/test2/ugly/source/Sources' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'new'
-v1*=Exporting test1...
-v2*=Created directory "./dists/test1"
-v2*=Created directory "./dists/test1/stupid"
-v2*=Created directory "./dists/test1/stupid/binary-abacus"
-v6*= exporting 'test1|stupid|abacus'...
-v6*=  creating './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v2*=Created directory "./dists/test1/stupid/source"
-v6*= exporting 'test1|stupid|source'...
-v6*=  creating './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v2*=Created directory "./dists/test1/ugly"
-v2*=Created directory "./dists/test1/ugly/binary-abacus"
-v6*= exporting 'test1|ugly|abacus'...
-v6*=  creating './dists/test1/ugly/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v2*=Created directory "./dists/test1/ugly/source"
-v6*= exporting 'test1|ugly|source'...
-v6*=  creating './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
EOF
test -f dists/test1/Release
test -f dists/test2/Release

EMPTYGZMD5SUM=7029066c27ac6f5ef18d660d5741979a
EMPTYBZ2MD5SUM=4059d198768f9f8dc9372dc1c54bc3c3
cat > dists/test1/Release.expected <<END
Codename: test1
Date: normalized
Architectures: abacus
Components: stupid ugly
MD5Sum:
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-abacus/Packages
 $EMPTYGZMD5SUM 20 stupid/binary-abacus/Packages.gz
 $EMPTYBZ2MD5SUM 14 stupid/binary-abacus/Packages.bz2
 d9f0fad5d54ad09dd4ecee86c73b64d4 39 stupid/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/source/Sources
 $EMPTYGZMD5SUM 20 stupid/source/Sources.gz
 $EMPTYBZ2MD5SUM 14 stupid/source/Sources.bz2
 e38c7da133734e1fd68a7e344b94fe96 39 stupid/source/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-abacus/Packages
 $EMPTYGZMD5SUM 20 ugly/binary-abacus/Packages.gz
 $EMPTYBZ2MD5SUM 14 ugly/binary-abacus/Packages.bz2
 236fcd9339b1813393819d464e37c7c6 37 ugly/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/source/Sources
 $EMPTYGZMD5SUM 20 ugly/source/Sources.gz
 $EMPTYBZ2MD5SUM 14 ugly/source/Sources.bz2
 ed4ee9aa5d080f67926816133872fd02 37 ugly/source/Release
END
cat > dists/test2/Release.expected <<END
Origin: Brain
Label: Only a test
Suite: broken
Codename: test2
Version: 9999999.02
Date: normalized
Architectures: abacus coal
Components: stupid ugly
Description: test with all fields set
MD5Sum:
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-abacus/Packages
 $EMPTYGZMD5SUM 20 stupid/binary-abacus/Packages.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/binary-abacus/Packages.bz2
 e142c47c1be0c32cd120138066b73c73 146 stupid/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/binary-coal/Packages
 $EMPTYGZMD5SUM 20 stupid/binary-coal/Packages.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/binary-coal/Packages.bz2
 10ae2f283e1abdd3facfac6ed664035d 144 stupid/binary-coal/Release
 d41d8cd98f00b204e9800998ecf8427e 0 stupid/source/Sources
 $EMPTYGZMD5SUM 20 stupid/source/Sources.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 stupid/source/Sources.bz2
 b923b3eb1141e41f0b8bb74297ac8a36 146 stupid/source/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-abacus/Packages
 $EMPTYGZMD5SUM 20 ugly/binary-abacus/Packages.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/binary-abacus/Packages.bz2
 22eb57e60d3c621b8bd8461eae218b16 144 ugly/binary-abacus/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/binary-coal/Packages
 $EMPTYGZMD5SUM 20 ugly/binary-coal/Packages.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/binary-coal/Packages.bz2
 7a05de3b706d08ed06779d0ec2e234e9 142 ugly/binary-coal/Release
 d41d8cd98f00b204e9800998ecf8427e 0 ugly/source/Sources
 $EMPTYGZMD5SUM 20 ugly/source/Sources.gz
 4059d198768f9f8dc9372dc1c54bc3c3 14 ugly/source/Sources.bz2
 e73a8a85315766763a41ad4dc6744bf5 144 ugly/source/Release
END
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\n%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq' | ed -s dists/test1/Release
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\n%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq' | ed -s dists/test2/Release
dodiff dists/test1/Release.expected dists/test1/Release || exit 1
dodiff dists/test2/Release.expected dists/test2/Release || exit 1

PACKAGE=simple EPOCH="" VERSION=1 REVISION="" SECTION="stupid/base" genpackage.sh
checknolog log1
testrun - -b . include test1 test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/stupid"
-v2*=Created directory "./pool/stupid/s"
-v2*=Created directory "./pool/stupid/s/simple"
=[tracking_get test1 simple 1]
=[tracking_new test1 simple 1]
-d1*=db: 'simple-addons' added to packages.db(test1|stupid|abacus).
-d1*=db: 'simple' added to packages.db(test1|stupid|abacus).
-d1*=db: 'simple' added to packages.db(test1|stupid|source).
=[tracking_save test1 simple 1]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
echo returned: $?
checklog log1 << EOF
DATESTR add test1 deb stupid abacus simple-addons 1
DATESTR add test1 deb stupid abacus simple 1
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
=[tracking_get test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_new test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|source).
=[tracking_save test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*=  replacing './dists/test1/ugly/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
EOF
echo returned: $?
checklog log1 <<EOF
DATESTR add test1 deb ugly abacus bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 deb ugly abacus bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
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
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 dsc stupid source simple 1
EOF
testrun - -b . -Tdeb remove test1 bloat+-0a9z.app 3<<EOF
stdout
-v1*=removing 'bloat+-0a9z.app' from 'test1|ugly|abacus'...
-d1*=db: 'bloat+-0a9z.app' removed from packages.db(test1|ugly|abacus).
=[tracking_get test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_get found test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_save test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*=  replacing './dists/test1/ugly/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb ugly abacus bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -A source remove test1 bloat+-0a9z.app 3<<EOF
stdout
-v1*=removing 'bloat+-0a9z.app' from 'test1|ugly|source'...
-d1*=db: 'bloat+-0a9z.app' removed from packages.db(test1|ugly|source).
=[tracking_get test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_get found test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_save test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 dsc ugly source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -A abacus remove test1 simple 3<<EOF
stdout
-v1*=removing 'simple' from 'test1|stupid|abacus'...
-d1*=db: 'simple' removed from packages.db(test1|stupid|abacus).
=[tracking_get test1 simple 1]
=[tracking_get found test1 simple 1]
=[tracking_save test1 simple 1]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb stupid abacus simple 1
EOF
testrun - -b . -C ugly remove test1 bloat+-0a9z.app-addons 3<<EOF
stdout
-v1*=removing 'bloat+-0a9z.app-addons' from 'test1|ugly|abacus'...
-d1*=db: 'bloat+-0a9z.app-addons' removed from packages.db(test1|ugly|abacus).
=[tracking_get test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_get found test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
=[tracking_save test1 bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*=  replacing './dists/test1/ugly/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb ugly abacus bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -C stupid remove test1 simple-addons 3<<EOF
stdout
-v1*=removing 'simple-addons' from 'test1|stupid|abacus'...
-d1*=db: 'simple-addons' removed from packages.db(test1|stupid|abacus).
=[tracking_get test1 simple 1]
=[tracking_get found test1 simple 1]
=[tracking_save test1 simple 1]
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb stupid abacus simple-addons 1
EOF
CURDATE="`TZ=GMT LC_ALL=C date +'%a, %d %b %Y %H:%M:%S +0000'`"
echo -e '%g/^Date:/s/Date: .*/Date: normalized/\n%g/gz$/s/^ 163be0a88c70ca629fd516dbaadad96a / 7029066c27ac6f5ef18d660d5741979a /\nw\nq' | ed -s dists/test1/Release

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
-v2*=Created directory "./pool/ugly/s"
-v2*=Created directory "./pool/ugly/s/simple"
-d1*=db: 'simple' added to packages.db(test2|ugly|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-abacus/Packages.new' 'stupid/binary-abacus/Packages' 'old'
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|stupid|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
-v6*= looking for changes in 'test2|ugly|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-abacus/Packages.new' 'ugly/binary-abacus/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'change'
-v6*=  replacing './dists/test2/ugly/source/Sources' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
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
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test2|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-abacus/Packages.new' 'stupid/binary-abacus/Packages' 'old'
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|stupid|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'change'
-v6*=  replacing './dists/test2/stupid/source/Sources' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v6*= looking for changes in 'test2|ugly|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-abacus/Packages.new' 'ugly/binary-abacus/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2 <<EOF
DATESTR add test2 dsc stupid source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -Tdeb -A abacus includedeb test2 simple_1_abacus.deb 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=simple_1_abacus.deb: component guessed as 'ugly'
stdout
-d1*=db: 'simple' added to packages.db(test2|ugly|abacus).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-abacus/Packages.new' 'stupid/binary-abacus/Packages' 'old'
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|stupid|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
-v6*= looking for changes in 'test2|ugly|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-abacus/Packages.new' 'ugly/binary-abacus/Packages' 'change'
-v6*=  replacing './dists/test2/ugly/binary-abacus/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v6*= looking for changes in 'test2|ugly|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2  <<EOF
DATESTR add test2 deb ugly abacus simple 1
EOF
testrun - -b . -Tdeb -A coal includedeb test2 simple-addons_1_all.deb 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=simple-addons_1_all.deb: component guessed as 'ugly'
stdout
-d1*=db: 'simple-addons' added to packages.db(test2|ugly|coal).
-v0=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-abacus/Packages.new' 'stupid/binary-abacus/Packages' 'old'
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|stupid|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
-v6*= looking for changes in 'test2|ugly|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-abacus/Packages.new' 'ugly/binary-abacus/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*=  replacing './dists/test2/ugly/binary-coal/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'change'
-v6*= looking for changes in 'test2|ugly|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2  <<EOF
DATESTR add test2 deb ugly coal simple-addons 1
EOF
testrun - -b . -Tdeb -A abacus includedeb test2 bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb: component guessed as 'stupid'
stdout
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test2|stupid|abacus).
-v0=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v6*=  replacing './dists/test2/stupid/binary-abacus/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-abacus/Packages.new' 'stupid/binary-abacus/Packages' 'change'
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|stupid|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
-v6*= looking for changes in 'test2|ugly|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-abacus/Packages.new' 'ugly/binary-abacus/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2 <<EOF
DATESTR add test2 deb stupid abacus bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -Tdeb -A coal includedeb test2 bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1=bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb: component guessed as 'stupid'
stdout
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test2|stupid|coal).
-v0=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-abacus/Packages.new' 'stupid/binary-abacus/Packages' 'old'
-v11*=Exporthook successfully returned!
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*=  replacing './dists/test2/stupid/binary-coal/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/binary-coal/Packages.new' 'stupid/binary-coal/Packages' 'change'
-v6*= looking for changes in 'test2|stupid|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'stupid/source/Sources.new' 'stupid/source/Sources' 'old'
-v6*= looking for changes in 'test2|ugly|abacus'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-abacus/Packages.new' 'ugly/binary-abacus/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|coal'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/binary-coal/Packages.new' 'ugly/binary-coal/Packages' 'old'
-v6*= looking for changes in 'test2|ugly|source'...
-v11*=Called $SRCDIR/docs/bzip.example './dists/test2' 'ugly/source/Sources.new' 'ugly/source/Sources' 'old'
EOF
checklog log2 <<EOF
DATESTR add test2 deb stupid coal bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
find dists/test2/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^\(Package\|Maintainer\|Section\|Priority\): ' | sort > results
cat >results.expected <<END
dists/test2/stupid/binary-abacus/Packages.gz:Maintainer: bloat.maintainer
dists/test2/stupid/binary-abacus/Packages.gz:Package: bloat+-0a9z.app
dists/test2/stupid/binary-abacus/Packages.gz:Priority: optional
dists/test2/stupid/binary-abacus/Packages.gz:Section: stupid/base
dists/test2/stupid/binary-coal/Packages.gz:Maintainer: bloat.add.maintainer
dists/test2/stupid/binary-coal/Packages.gz:Package: bloat+-0a9z.app-addons
dists/test2/stupid/binary-coal/Packages.gz:Priority: optional
dists/test2/stupid/binary-coal/Packages.gz:Section: stupid/addons
dists/test2/stupid/source/Sources.gz:Maintainer: bloat.source.maintainer
dists/test2/stupid/source/Sources.gz:Package: bloat+-0a9z.app
dists/test2/stupid/source/Sources.gz:Priority: optional
dists/test2/stupid/source/Sources.gz:Section: stupid/X11
dists/test2/ugly/binary-abacus/Packages.gz:Maintainer: simple.maintainer
dists/test2/ugly/binary-abacus/Packages.gz:Package: simple
dists/test2/ugly/binary-abacus/Packages.gz:Priority: optional
dists/test2/ugly/binary-abacus/Packages.gz:Section: ugly/base
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
test2|ugly|abacus: simple 1
test2|ugly|coal: simple-addons 1
test2|ugly|source: simple 1
END
dodiff results.expected results
testout "" -b . listfilter test2 'Source(==bloat+-0a9z.app)|(!Source,Package(==bloat+-0a9z.app))'
cat > results.expected << END
test2|stupid|abacus: bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
test2|stupid|coal: bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
test2|stupid|source: bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
END
dodiff results.expected results

cat >conf/updates <<END
Name: Test2toTest1
Method: copy:$WORKDIR
Suite: test2
Architectures: coal>abacus abacus source
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

testrun - -b . $UPDATETYPE test1 3<<EOF
stderr
*=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/Release'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/ugly/source/Sources.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/ugly/source/Sources.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/ugly/binary-abacus/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/ugly/binary-abacus/Packages.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/ugly/binary-coal/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/ugly/binary-coal/Packages.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/source/Sources.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/binary-abacus/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/binary-abacus/Packages.gz'
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/stupid/binary-coal/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/stupid/binary-coal/Packages.gz'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_dsc_ugly_source' './lists/test1_Test2toTest1_dsc_ugly_source_changed'
-v6*=Listhook successfully returned!
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_ugly_abacus' './lists/test1_Test2toTest1_deb_ugly_abacus_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_ugly_coal' './lists/test1_Test2toTest1_deb_ugly_coal_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_dsc_stupid_source' './lists/test1_Test2toTest1_dsc_stupid_source_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_stupid_abacus' './lists/test1_Test2toTest1_deb_stupid_abacus_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_stupid_coal' './lists/test1_Test2toTest1_deb_stupid_coal_changed'
stdout
-v2*=Created directory "./lists"
-v0*=Calculating packages to get...
-v3*=  processing updates for 'test1|ugly|source'
-v5*=  reading './lists/test1_Test2toTest1_dsc_ugly_source_changed'
-v3*=  processing updates for 'test1|ugly|abacus'
-v5*=  reading './lists/test1_Test2toTest1_deb_ugly_abacus_changed'
-v5*=  reading './lists/test1_Test2toTest1_deb_ugly_coal_changed'
-v3*=  processing updates for 'test1|stupid|source'
-v5*=  reading './lists/test1_Test2toTest1_dsc_stupid_source_changed'
-v3*=  processing updates for 'test1|stupid|abacus'
-v5*=  reading './lists/test1_Test2toTest1_deb_stupid_abacus_changed'
-v5*=  reading './lists/test1_Test2toTest1_deb_stupid_coal_changed'
-v0*=Getting packages...
-v1=Freeing some memory...
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'simple' added to packages.db(test1|ugly|source).
-d1*=db: 'simple' added to packages.db(test1|ugly|abacus).
-d1*=db: 'simple-addons' added to packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|stupid|source).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|stupid|abacus).
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test1|stupid|abacus).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*=  replacing './dists/test1/ugly/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
EOF
checklog log1 <<EOF
DATESTR add test1 dsc ugly source simple 1
DATESTR add test1 deb ugly abacus simple 1
DATESTR add test1 deb ugly abacus simple-addons 1
DATESTR add test1 dsc stupid source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 deb stupid abacus bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 deb stupid abacus bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
checknolog log1
checknolog log2
testrun - -b . $UPDATETYPE test1 3<<EOF
=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
=WARNING: Single-Instance not yet supported!
-v6*=aptmethod start 'copy:$WORKDIR/dists/test2/Release'
-v1*=aptmethod got 'copy:$WORKDIR/dists/test2/Release'
*=Nothing to do found. (Use --noskipold to force processing)
EOF
checklog log1 < /dev/null
checknolog log2
testrun - --nolistsdownload -b . $UPDATETYPE test1 3<<EOF
-v0*=Ignoring --skipold because of --nolistsdownload
=WARNING: Single-Instance not yet supported!
=WARNING: Updating does not update trackingdata. Trackingdata of test1 will be outdated!
-v0*=Warning: As --nolistsdownload is given, index files are NOT checked.
-v6*=Called /bin/cp './lists/test1_Test2toTest1_dsc_ugly_source' './lists/test1_Test2toTest1_dsc_ugly_source_changed'
-v6*=Listhook successfully returned!
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_ugly_abacus' './lists/test1_Test2toTest1_deb_ugly_abacus_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_ugly_coal' './lists/test1_Test2toTest1_deb_ugly_coal_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_dsc_stupid_source' './lists/test1_Test2toTest1_dsc_stupid_source_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_stupid_abacus' './lists/test1_Test2toTest1_deb_stupid_abacus_changed'
-v6*=Called /bin/cp './lists/test1_Test2toTest1_deb_stupid_coal' './lists/test1_Test2toTest1_deb_stupid_coal_changed'
stdout
-v0*=Calculating packages to get...
-v3*=  processing updates for 'test1|ugly|source'
-v5*=  reading './lists/test1_Test2toTest1_dsc_ugly_source_changed'
-v3*=  processing updates for 'test1|ugly|abacus'
-v5*=  reading './lists/test1_Test2toTest1_deb_ugly_abacus_changed'
-v5*=  reading './lists/test1_Test2toTest1_deb_ugly_coal_changed'
-v3*=  processing updates for 'test1|stupid|source'
-v5*=  reading './lists/test1_Test2toTest1_dsc_stupid_source_changed'
-v3*=  processing updates for 'test1|stupid|abacus'
-v5*=  reading './lists/test1_Test2toTest1_deb_stupid_abacus_changed'
-v5*=  reading './lists/test1_Test2toTest1_deb_stupid_coal_changed'
-v0*=Getting packages...
-v1=Freeing some memory...
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
EOF
checklog log1 < /dev/null
checknolog log2

find dists/test2/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' | sed -e 's/test2/test1/' -e 's/coal/abacus/' | sort > test2
find dists/test1/ \( -name "Packages.gz" -o -name "Sources.gz" \) -print0 | xargs -0 zgrep '^Package: ' | sort > test1
dodiff test2 test1

testrun - -b . check test1 test2 3<<EOF
stdout
-v1*=Checking test2...
-x1*=Checking packages in 'test2|stupid|abacus'...
-x1*=Checking packages in 'test2|stupid|coal'...
-x1*=Checking packages in 'test2|stupid|source'...
-x1*=Checking packages in 'test2|ugly|abacus'...
-x1*=Checking packages in 'test2|ugly|coal'...
-x1*=Checking packages in 'test2|ugly|source'...
-v1*=Checking test1...
-x1*=Checking packages in 'test1|stupid|abacus'...
-x1*=Checking packages in 'test1|stupid|source'...
-x1*=Checking packages in 'test1|ugly|abacus'...
-x1*=Checking packages in 'test1|ugly|source'...
EOF
testrun "" -b . checkpool
testrun - -b . rereference test1 test2 3<<EOF
stdout
-v1*=Referencing test2...
-v2=Rereferencing test2|stupid|abacus...
-v2=Rereferencing test2|stupid|coal...
-v2=Rereferencing test2|stupid|source...
-v2=Rereferencing test2|ugly|abacus...
-v2=Rereferencing test2|ugly|coal...
-v2=Rereferencing test2|ugly|source...
-v3*=Unlocking depencies of test2|stupid|abacus...
-v3*=Referencing test2|stupid|abacus...
-v3*=Unlocking depencies of test2|stupid|coal...
-v3*=Referencing test2|stupid|coal...
-v3*=Unlocking depencies of test2|stupid|source...
-v3*=Referencing test2|stupid|source...
-v3*=Unlocking depencies of test2|ugly|abacus...
-v3*=Referencing test2|ugly|abacus...
-v3*=Unlocking depencies of test2|ugly|coal...
-v3*=Referencing test2|ugly|coal...
-v3*=Unlocking depencies of test2|ugly|source...
-v3*=Referencing test2|ugly|source...
-v1*=Referencing test1...
-v2=Rereferencing test1|stupid|abacus...
-v2=Rereferencing test1|stupid|source...
-v2=Rereferencing test1|ugly|abacus...
-v2=Rereferencing test1|ugly|source...
-v3*=Unlocking depencies of test1|stupid|abacus...
-v3*=Referencing test1|stupid|abacus...
-v3*=Unlocking depencies of test1|stupid|source...
-v3*=Referencing test1|stupid|source...
-v3*=Unlocking depencies of test1|ugly|abacus...
-v3*=Referencing test1|ugly|abacus...
-v3*=Unlocking depencies of test1|ugly|source...
-v3*=Referencing test1|ugly|source...
EOF
testrun - -b . check test1 test2 3<<EOF
stdout
-v1*=Checking test1...
-x1*=Checking packages in 'test2|stupid|abacus'...
-x1*=Checking packages in 'test2|stupid|coal'...
-x1*=Checking packages in 'test2|stupid|source'...
-x1*=Checking packages in 'test2|ugly|abacus'...
-x1*=Checking packages in 'test2|ugly|coal'...
-x1*=Checking packages in 'test2|ugly|source'...
-v1*=Checking test2...
-x1*=Checking packages in 'test1|stupid|abacus'...
-x1*=Checking packages in 'test1|stupid|source'...
-x1*=Checking packages in 'test1|ugly|abacus'...
-x1*=Checking packages in 'test1|ugly|source'...
EOF

testout "" -b . dumptracks
cat >results.expected <<END
Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple-addons_1_all.deb a 0
 pool/stupid/s/simple/simple_1_abacus.deb b 0
 pool/stupid/s/simple/simple_1.dsc s 0
 pool/stupid/s/simple/simple_1.tar.gz s 0
 pool/stupid/s/simple/simple_1_source+abacus+all.changes c 0

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
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1.dsc
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1.tar.gz
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1_abacus.deb
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1_source+abacus+all.changes
-v1*=removed now empty directory ./pool/stupid/s/simple
-v1*=removed now empty directory ./pool/stupid/s
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes
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
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*=  replacing './dists/test1/ugly/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
EOF
checklog log1 <<EOF
DATESTR add test1 deb ugly abacus bloat+-0a9z.app-addons 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 deb ugly abacus bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR add test1 dsc ugly source bloat+-0a9z.app 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
echo returned: $?
OUTPUT=test2.changes PACKAGE=bloat+-0a9z.app EPOCH=99: VERSION=9.0-A:Z+a:z REVISION=-0+aA.9zZ SECTION="ugly/extra" genpackage.sh
testrun - -b . include test1 test2.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'bloat+-0a9z.app-addons' removed from packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app-addons' added to packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app' removed from packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|abacus).
-d1*=db: 'bloat+-0a9z.app' removed from packages.db(test1|ugly|source).
-d1*=db: 'bloat+-0a9z.app' added to packages.db(test1|ugly|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*=  replacing './dists/test1/ugly/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
-v0*=Deleting files no longer referenced...
EOF
echo returned: $?
checklog log1 <<EOF
DATESTR replace test1 deb ugly abacus bloat+-0a9z.app-addons 99:9.0-A:Z+a:z-0+aA.9zZ 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR replace test1 deb ugly abacus bloat+-0a9z.app 99:9.0-A:Z+a:z-0+aA.9zZ 99:0.9-A:Z+a:z-0+aA.9zZ
DATESTR replace test1 dsc ugly source bloat+-0a9z.app 99:9.0-A:Z+a:z-0+aA.9zZ 99:0.9-A:Z+a:z-0+aA.9zZ
EOF
testrun - -b . -S test -P test includedeb test1 simple_1_abacus.deb 3<<EOF
stderr
-v1*=simple_1_abacus.deb: component guessed as 'stupid'
stdout
-v2*=Created directory "./pool/stupid/s"
-v2*=Created directory "./pool/stupid/s/simple"
-d1*=db: 'simple' added to packages.db(test1|stupid|abacus).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
echo returned: $?
checklog log1 <<EOF
DATESTR add test1 deb stupid abacus simple 1
EOF
testrun - -b . -S test -P test includedsc test1 simple_1.dsc 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v1*=simple_1.dsc: component guessed as 'stupid'
stdout
-d1*=db: 'simple' added to packages.db(test1|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
echo returned: $?
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
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_abacus.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_abacus.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

END
dodiff results.expected results
testout "" -b . dumpunreferenced
dodiff results.empty results

echo "now testing .orig.tar.gz handling"
tar -czf test_1.orig.tar.gz test.changes
PACKAGE=test EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh -si
testrun - -b . include test1 test.changes 3<<EOF
returns 249
stderr
-v0=Data seems not to be signed trying to use directly...
*=Unable to find ./pool/stupid/t/test/test_1.orig.tar.gz!
*=Perhaps you forgot to give dpkg-buildpackage the -sa option,
*= or you cound try --ignore=missingfile
-v0*=There have been errors!
stdout
-v2*=Created directory "./pool/stupid/t"
-v2*=Created directory "./pool/stupid/t/test"
-v1*=deleting and forgetting pool/stupid/t/test/test-addons_1-2_all.deb
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2_abacus.deb
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2.diff.gz
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2.dsc
-v1*=removed now empty directory ./pool/stupid/t/test
-v1*=removed now empty directory ./pool/stupid/t
EOF
checknolog log1
checknolog log2
testrun - -b . --ignore=missingfile include test1 test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
*=Unable to find ./pool/stupid/t/test/test_1.orig.tar.gz!
*=Looking around if it is elsewhere as --ignore=missingfile given.
stdout
-v2*=Created directory "./pool/stupid/t"
-v2*=Created directory "./pool/stupid/t/test"
-d1*=db: 'test-addons' added to packages.db(test1|stupid|abacus).
-d1*=db: 'test' added to packages.db(test1|stupid|abacus).
-d1*=db: 'test' added to packages.db(test1|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
checklog log1 <<EOF
DATESTR add test1 deb stupid abacus test-addons 1-2
DATESTR add test1 deb stupid abacus test 1-2
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
-d1*=db: 'testb-addons' added to packages.db(test1|stupid|abacus).
-d1*=db: 'testb' added to packages.db(test1|stupid|abacus).
-d1*=db: 'testb' added to packages.db(test1|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
checklog log1 <<EOF
DATESTR add test1 deb stupid abacus testb-addons 1:2-2
DATESTR add test1 deb stupid abacus testb 1:2-2
DATESTR add test1 dsc stupid source testb 1:2-2
EOF
dodo zgrep testb_2-2.dsc dists/test1/stupid/source/Sources.gz
rm test2.changes
PACKAGE=testb EPOCH="1:" VERSION=2 REVISION="-3" SECTION="stupid/base" OUTPUT="test2.changes" genpackage.sh -sd
testrun - -b . include test1 test2.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'testb-addons' removed from packages.db(test1|stupid|abacus).
-d1*=db: 'testb-addons' added to packages.db(test1|stupid|abacus).
-d1*=db: 'testb' removed from packages.db(test1|stupid|abacus).
-d1*=db: 'testb' added to packages.db(test1|stupid|abacus).
-d1*=db: 'testb' removed from packages.db(test1|stupid|source).
-d1*=db: 'testb' added to packages.db(test1|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR replace test1 deb stupid abacus testb-addons 1:2-3 1:2-2
DATESTR replace test1 deb stupid abacus testb 1:2-3 1:2-2
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
-d1*=db: '4test-addons' added to packages.db(test1|stupid|abacus).
-d1*=db: '4test' added to packages.db(test1|stupid|abacus).
-d1*=db: '4test' added to packages.db(test1|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*= looking for changes in 'test1|ugly|source'...
EOF
checklog log1 <<EOF
DATESTR add test1 deb stupid abacus 4test-addons 1:b.1-1
DATESTR add test1 deb stupid abacus 4test 1:b.1-1
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
 pool/stupid/4/4test/4test_b.1-1_abacus.deb b 1
 pool/stupid/4/4test/4test_b.1-1.dsc s 1
 pool/stupid/4/4test/4test_b.1-1.tar.gz s 1
 pool/stupid/4/4test/4test_1:b.1-1_source+abacus+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb b 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 0
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_abacus.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_abacus.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

Distribution: test1
Source: test
Version: 1-2
Files:
 pool/stupid/t/test/test-addons_1-2_all.deb a 1
 pool/stupid/t/test/test_1-2_abacus.deb b 1
 pool/stupid/t/test/test_1-2.dsc s 1
 pool/stupid/t/test/test_1.orig.tar.gz s 1
 pool/stupid/t/test/test_1-2.diff.gz s 1
 pool/stupid/t/test/test_1-2_source+abacus+all.changes c 0

Distribution: test1
Source: testb
Version: 1:2-2
Files:
 pool/stupid/t/testb/testb-addons_2-2_all.deb a 0
 pool/stupid/t/testb/testb_2-2_abacus.deb b 0
 pool/stupid/t/testb/testb_2-2.dsc s 0
 pool/stupid/t/testb/testb_2.orig.tar.gz s 0
 pool/stupid/t/testb/testb_2-2.diff.gz s 0
 pool/stupid/t/testb/testb_1:2-2_source+abacus+all.changes c 0

Distribution: test1
Source: testb
Version: 1:2-3
Files:
 pool/stupid/t/testb/testb-addons_2-3_all.deb a 1
 pool/stupid/t/testb/testb_2-3_abacus.deb b 1
 pool/stupid/t/testb/testb_2-3.dsc s 1
 pool/stupid/t/testb/testb_2.orig.tar.gz s 1
 pool/stupid/t/testb/testb_2-3.diff.gz s 1
 pool/stupid/t/testb/testb_1:2-3_source+abacus+all.changes c 0

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
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:0.9-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes
-v1*=deleting and forgetting pool/stupid/t/testb/testb-addons_2-2_all.deb
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-2_abacus.deb
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-2.dsc
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-2.diff.gz
-v1*=deleting and forgetting pool/stupid/t/testb/testb_1:2-2_source+abacus+all.changes
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
 pool/stupid/4/4test/4test_b.1-1_abacus.deb b 1
 pool/stupid/4/4test/4test_b.1-1.dsc s 1
 pool/stupid/4/4test/4test_b.1-1.tar.gz s 1
 pool/stupid/4/4test/4test_1:b.1-1_source+abacus+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_abacus.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_abacus.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

Distribution: test1
Source: test
Version: 1-2
Files:
 pool/stupid/t/test/test-addons_1-2_all.deb a 1
 pool/stupid/t/test/test_1-2_abacus.deb b 1
 pool/stupid/t/test/test_1-2.dsc s 1
 pool/stupid/t/test/test_1.orig.tar.gz s 1
 pool/stupid/t/test/test_1-2.diff.gz s 1
 pool/stupid/t/test/test_1-2_source+abacus+all.changes c 0

Distribution: test1
Source: testb
Version: 1:2-3
Files:
 pool/stupid/t/testb/testb-addons_2-3_all.deb a 1
 pool/stupid/t/testb/testb_2-3_abacus.deb b 1
 pool/stupid/t/testb/testb_2-3.dsc s 1
 pool/stupid/t/testb/testb_2.orig.tar.gz s 1
 pool/stupid/t/testb/testb_2-3.diff.gz s 1
 pool/stupid/t/testb/testb_1:2-3_source+abacus+all.changes c 0

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
-v1*=deleting and forgetting pool/stupid/4/4test/4test_1:b.1-1_source+abacus+all.changes
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2_source+abacus+all.changes
-v1*=deleting and forgetting pool/stupid/t/testb/testb_1:2-3_source+abacus+all.changes
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
 pool/stupid/4/4test/4test_b.1-1_abacus.deb b 1
 pool/stupid/4/4test/4test_b.1-1.dsc s 1
 pool/stupid/4/4test/4test_b.1-1.tar.gz s 1

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_abacus.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_abacus.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1

Distribution: test1
Source: test
Version: 1-2
Files:
 pool/stupid/t/test/test-addons_1-2_all.deb a 1
 pool/stupid/t/test/test_1-2_abacus.deb b 1
 pool/stupid/t/test/test_1-2.dsc s 1
 pool/stupid/t/test/test_1.orig.tar.gz s 1
 pool/stupid/t/test/test_1-2.diff.gz s 1

Distribution: test1
Source: testb
Version: 1:2-3
Files:
 pool/stupid/t/testb/testb-addons_2-3_all.deb a 1
 pool/stupid/t/testb/testb_2-3_abacus.deb b 1
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
-x1*=  Tracking test1|stupid|abacus...
-x1*=  Tracking test1|stupid|source...
-x1*=  Tracking test1|ugly|abacus...
-x1*=  Tracking test1|ugly|source...
EOF
testout "" -b . dumptracks
cat > results.expected <<EOF
Distribution: test1
Source: 4test
Version: 1:b.1-1
Files:
 pool/stupid/4/4test/4test-addons_b.1-1_all.deb a 1
 pool/stupid/4/4test/4test_b.1-1_abacus.deb b 1
 pool/stupid/4/4test/4test_b.1-1.dsc s 1
 pool/stupid/4/4test/4test_b.1-1.tar.gz s 1
 pool/stupid/4/4test/4test_1:b.1-1_source+abacus+all.changes c 0

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:0.9-A:Z+a:z-0+aA.9zZ
Files:
 pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb b 1
 pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz s 1

Distribution: test1
Source: bloat+-0a9z.app
Version: 99:9.0-A:Z+a:z-0+aA.9zZ
Files:
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb a 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_abacus.deb b 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz s 1
 pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_99:9.0-A:Z+a:z-0+aA.9zZ_source+abacus+all.changes c 0

Distribution: test1
Source: simple
Version: 1
Files:
 pool/stupid/s/simple/simple_1_abacus.deb b 1
 pool/stupid/s/simple/simple_1.dsc s 1
 pool/stupid/s/simple/simple_1.tar.gz s 1
 pool/ugly/s/simple/simple_1_abacus.deb b 1
 pool/ugly/s/simple/simple-addons_1_all.deb a 1
 pool/ugly/s/simple/simple_1.dsc s 1
 pool/ugly/s/simple/simple_1.tar.gz s 1

Distribution: test1
Source: test
Version: 1-2
Files:
 pool/stupid/t/test/test-addons_1-2_all.deb a 1
 pool/stupid/t/test/test_1-2_abacus.deb b 1
 pool/stupid/t/test/test_1-2.dsc s 1
 pool/stupid/t/test/test_1.orig.tar.gz s 1
 pool/stupid/t/test/test_1-2.diff.gz s 1
 pool/stupid/t/test/test_1-2_source+abacus+all.changes c 0

Distribution: test1
Source: testb
Version: 1:2-3
Files:
 pool/stupid/t/testb/testb-addons_2-3_all.deb a 1
 pool/stupid/t/testb/testb_2-3_abacus.deb b 1
 pool/stupid/t/testb/testb_2-3.dsc s 1
 pool/stupid/t/testb/testb_2.orig.tar.gz s 1
 pool/stupid/t/testb/testb_2-3.diff.gz s 1
 pool/stupid/t/testb/testb_1:2-3_source+abacus+all.changes c 0

EOF
dodiff results.expected results

testout "" -b . dumpunreferenced
dodiff results.empty results
testout ""  -b . dumpreferences
cp results results.expected
testrun - -b . rereference 3<<EOF
stdout
-v1*=Referencing test1...
-v2*=Unlocking depencies of test1|stupid|abacus...
=Rereferencing test1|stupid|abacus...
-v2*=Referencing test1|stupid|abacus...
-v2*=Unlocking depencies of test1|stupid|source...
=Rereferencing test1|stupid|source...
-v2*=Referencing test1|stupid|source...
-v2*=Unlocking depencies of test1|ugly|abacus...
=Rereferencing test1|ugly|abacus...
-v2*=Referencing test1|ugly|abacus...
-v2*=Unlocking depencies of test1|ugly|source...
=Rereferencing test1|ugly|source...
-v2*=Referencing test1|ugly|source...
-v1*=Referencing test2...
-v2*=Unlocking depencies of test2|stupid|abacus...
=Rereferencing test2|stupid|abacus...
-v2*=Referencing test2|stupid|abacus...
-v2*=Unlocking depencies of test2|stupid|coal...
=Rereferencing test2|stupid|coal...
-v2*=Referencing test2|stupid|coal...
-v2*=Unlocking depencies of test2|stupid|source...
=Rereferencing test2|stupid|source...
-v2*=Referencing test2|stupid|source...
-v2*=Unlocking depencies of test2|ugly|abacus...
=Rereferencing test2|ugly|abacus...
-v2*=Referencing test2|ugly|abacus...
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
-v2*=Unlocking depencies of test1|stupid|abacus...
=Rereferencing test1|stupid|abacus...
-v2*=Referencing test1|stupid|abacus...
-v2*=Unlocking depencies of test1|stupid|source...
=Rereferencing test1|stupid|source...
-v2*=Referencing test1|stupid|source...
-v2*=Unlocking depencies of test1|ugly|abacus...
=Rereferencing test1|ugly|abacus...
-v2*=Referencing test1|ugly|abacus...
-v2*=Unlocking depencies of test1|ugly|source...
=Rereferencing test1|ugly|source...
-v2*=Referencing test1|ugly|source...
-v1*=Referencing test2...
-v2*=Unlocking depencies of test2|stupid|abacus...
=Rereferencing test2|stupid|abacus...
-v2*=Referencing test2|stupid|abacus...
-v2*=Unlocking depencies of test2|stupid|coal...
=Rereferencing test2|stupid|coal...
-v2*=Referencing test2|stupid|coal...
-v2*=Unlocking depencies of test2|stupid|source...
=Rereferencing test2|stupid|source...
-v2*=Referencing test2|stupid|source...
-v2*=Unlocking depencies of test2|ugly|abacus...
=Rereferencing test2|ugly|abacus...
-v2*=Referencing test2|ugly|abacus...
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
echo -e 'Codename: foo' > conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
stderr
*=Error parsing config file conf2/distributions, line 2:
*=Required field 'Architectures' expected (since line 1).
-v0*=There have been errors!
returns 249
EOF
echo -e 'Architectures: abacus fingers' >> conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
*=Error parsing config file conf2/distributions, line 3:
*=Required field 'Components' expected (since line 1).
-v0*=There have been errors!
returns 249
EOF
echo -e 'Components: unneeded bloated i386' >> conf2/distributions
testrun - -b . --confdir conf2 update 3<<EOF
*=Error: packages database contains unused 'test1|stupid|abacus' database.
*=This either means you removed a distribution, component or architecture from
*=the distributions config file without calling clearvanished, or your config
*=does not belong to this database.
*=To ignore use --ignore=undefinedtarget.
-v0*=There have been errors!
returns 255
EOF
testrun - -b . --confdir conf2 --ignore=undefinedtarget update 3<<EOF
*=Error: packages database contains unused 'test1|stupid|abacus' database.
*=This either means you removed a distribution, component or architecture from
*=the distributions config file without calling clearvanished, or your config
*=does not belong to this database.
*=Ignoring as --ignore=undefinedtarget given.
*=Error: packages database contains unused 'test1|ugly|abacus' database.
*=Error: packages database contains unused 'test1|ugly|source' database.
*=Error: packages database contains unused 'test1|stupid|source' database.
*=Error: packages database contains unused 'test2|stupid|abacus' database.
*=Error: packages database contains unused 'test2|stupid|coal' database.
*=Error: packages database contains unused 'test2|stupid|source' database.
*=Error: packages database contains unused 'test2|ugly|abacus' database.
*=Error: packages database contains unused 'test2|ugly|coal' database.
*=Error: packages database contains unused 'test2|ugly|source' database.
*=Error: tracking database contains unused 'test1' database.
*=This either means you removed a distribution from the distributions config
*=file without calling clearvanished (or at least removealltracks), you were
*=bitten by a bug in retrack in versions < 3.0.0, you found a new bug or your
*=config does not belong to this database.
*=To ignore use --ignore=undefinedtracking.
-v0*=There have been errors!
returns 255
EOF
testrun - -b . --confdir conf2 --ignore=undefinedtarget --ignore=undefinedtracking update 3<<EOF
*=Error: packages database contains unused 'test1|stupid|abacus' database.
*=This either means you removed a distribution, component or architecture from
*=the distributions config file without calling clearvanished, or your config
*=does not belong to this database.
*=Ignoring as --ignore=undefinedtarget given.
*=Error: tracking database contains unused 'test1' database.
*=This either means you removed a distribution from the distributions config
*=file without calling clearvanished (or at least removealltracks), you were
*=bitten by a bug in retrack in versions < 3.0.0, you found a new bug or your
*=config does not belong to this database.
*=Ignoring as --ignore=undefinedtracking given.
*=Error: packages database contains unused 'test1|ugly|abacus' database.
*=Error: packages database contains unused 'test1|ugly|source' database.
*=Error: packages database contains unused 'test1|stupid|source' database.
*=Error: packages database contains unused 'test2|stupid|abacus' database.
*=Error: packages database contains unused 'test2|stupid|coal' database.
*=Error: packages database contains unused 'test2|stupid|source' database.
*=Error: packages database contains unused 'test2|ugly|abacus' database.
*=Error: packages database contains unused 'test2|ugly|coal' database.
*=Error: packages database contains unused 'test2|ugly|source' database.
*=Error opening config file 'conf2/updates': No such file or directory(2)
-v0*=There have been errors!
returns 254
EOF
touch conf2/updates
testrun - -b . --confdir conf2 --ignore=undefinedtarget --ignore=undefinedtracking --noskipold update 3<<EOF
stderr
*=Error: packages database contains unused 'test1|stupid|abacus' database.
*=This either means you removed a distribution, component or architecture from
*=the distributions config file without calling clearvanished, or your config
*=does not belong to this database.
*=Ignoring as --ignore=undefinedtarget given.
*=Error: packages database contains unused 'test1|ugly|abacus' database.
*=Error: packages database contains unused 'test1|ugly|source' database.
*=Error: packages database contains unused 'test1|stupid|source' database.
*=Error: packages database contains unused 'test2|stupid|abacus' database.
*=Error: packages database contains unused 'test2|stupid|coal' database.
*=Error: packages database contains unused 'test2|stupid|source' database.
*=Error: packages database contains unused 'test2|ugly|abacus' database.
*=Error: packages database contains unused 'test2|ugly|coal' database.
*=Error: packages database contains unused 'test2|ugly|source' database.
*=Error: tracking database contains unused 'test1' database.
*=This either means you removed a distribution from the distributions config
*=file without calling clearvanished (or at least removealltracks), you were
*=bitten by a bug in retrack in versions < 3.0.0, you found a new bug or your
*=config does not belong to this database.
*=Ignoring as --ignore=undefinedtracking given.
stdout
-v2=Created directory "./lists"
-v0*=Calculating packages to get...
-v0*=Getting packages...
-v1=Freeing some memory...
-v1*=Shutting down aptmethods...
-v0*=Installing (and possibly deleting) packages...
-v0*=Exporting indices...
-v2*=Created directory "./dists/foo"
-v2*=Created directory "./dists/foo/unneeded"
-v2*=Created directory "./dists/foo/unneeded/binary-abacus"
-v6*= looking for changes in 'foo|unneeded|abacus'...
-v6*=  creating './dists/foo/unneeded/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/foo/unneeded/binary-fingers"
-v6*= looking for changes in 'foo|unneeded|fingers'...
-v6*=  creating './dists/foo/unneeded/binary-fingers/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/foo/bloated"
-v2*=Created directory "./dists/foo/bloated/binary-abacus"
-v6*= looking for changes in 'foo|bloated|abacus'...
-v6*=  creating './dists/foo/bloated/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/foo/bloated/binary-fingers"
-v6*= looking for changes in 'foo|bloated|fingers'...
-v6*=  creating './dists/foo/bloated/binary-fingers/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/foo/i386"
-v2*=Created directory "./dists/foo/i386/binary-abacus"
-v6*= looking for changes in 'foo|i386|abacus'...
-v6*=  creating './dists/foo/i386/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/foo/i386/binary-fingers"
-v6*= looking for changes in 'foo|i386|fingers'...
-v6*=  creating './dists/foo/i386/binary-fingers/Packages' (uncompressed,gzipped)
EOF
testrun - -b . clearvanished 3<<EOF
stdout
*=Deleting vanished identifier 'foo|bloated|abacus'.
*=Deleting vanished identifier 'foo|bloated|fingers'.
*=Deleting vanished identifier 'foo|i386|abacus'.
*=Deleting vanished identifier 'foo|i386|fingers'.
*=Deleting vanished identifier 'foo|unneeded|abacus'.
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
*=In 'broken.changes': Missing 'Description' field!
*=In 'broken.changes': Missing 'Changes' field!
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
*=In 'broken.changes': Missing 'Description' field!
*=In 'broken.changes': Missing 'Changes' field!
*=broken.changes: Not enough files in .changes!
=Ignoring as --ignore=missingfield given.
-v0*=There have been errors!
returns 255
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
echo " d41d8cd98f00b204e9800998ecf8427e 0 section priority filename_version.tar.gz" >> broken.changes
testrun - -b . --ignore=missingfield include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'old' does not start with a digit, violating 'should'-directive in policy 5.6.11
=In 'broken.changes': Missing 'Urgency' field!
=Ignoring as --ignore=missingfield given.
=In 'broken.changes': Missing 'Maintainer' field!
=In 'broken.changes': Missing 'Description' field!
=In 'broken.changes': Missing 'Changes' field!
*=Warning: Strange file 'filename_version.tar.gz'!
=Looks like source but does not start with 'nowhere_' as I would have guessed!
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
=In 'broken.changes': Missing 'Description' field!
=In 'broken.changes': Missing 'Changes' field!
=Warning: Strange file 'filename_version.tar.gz'!
=Looks like source but does not start with 'nowhere_' as I would have guessed!
=I hope you know what you do.
# again
=Warning: Package version 'version.tar.gz' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=.changes put in a distribution not listed within it!
*=Ignoring as --ignore=wrongdistribution given.
*=Architecture-header lists architecture 'brain', but no files for this!
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
=In 'broken.changes': Missing 'Description' field!
=In 'broken.changes': Missing 'Changes' field!
=Warning: Strange file 'filename_version.tar.gz'!
*=Looks like source but does not start with 'nowhere_' as I would have guessed!
=I hope you know what you do.
*=.changes put in a distribution not listed within it!
*=Ignoring as --ignore=wrongdistribution given.
*=Architecture-header lists architecture 'brain', but no files for this!
*=Ignoring as --ignore=unusedarch given.
*='filename_version.tar.gz' looks like architecture 'source', but this is not listed in the Architecture-Header!
*=Ignoring as --ignore=surprisingarch given.
stdout
-v2*=Created directory "./pool/stupid/n"
-v2*=Created directory "./pool/stupid/n/nowhere"
EOF
testout "" -b . dumpunreferenced
cat >results.expected <<EOF
pool/stupid/n/nowhere/filename_version.tar.gz
EOF
dodiff results.expected results
testrun - -b . deleteunreferenced 3<<EOF
stdout
-v1*=deleting and forgetting pool/stupid/n/nowhere/filename_version.tar.gz
-v1*=removed now empty directory ./pool/stupid/n/nowhere
-v1*=removed now empty directory ./pool/stupid/n
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
testout "" -b . dumpreferences
# first remove file, then try to remove the package
testrun "" -b . _forget pool/ugly/s/simple/simple_1_abacus.deb
testrun - -b . remove test1 simple 3<<EOF
# ???
=Warning: tracking database of test1 missed files for simple_1.
stdout
-v1*=removing 'simple' from 'test1|stupid|abacus'...
-v1*=removing 'simple' from 'test1|stupid|source'...
-v1*=removing 'simple' from 'test1|ugly|abacus'...
-v1*=removing 'simple' from 'test1|ugly|source'...
-d1*=db: 'simple' removed from packages.db(test1|stupid|abacus).
-d1*=db: 'simple' removed from packages.db(test1|stupid|source).
-d1*=db: 'simple' removed from packages.db(test1|ugly|abacus).
-d1*=db: 'simple' removed from packages.db(test1|ugly|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test1|stupid|abacus'...
-v6*=  replacing './dists/test1/stupid/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|stupid|source'...
-v6*=  replacing './dists/test1/stupid/source/Sources' (gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|abacus'...
-v6*=  replacing './dists/test1/ugly/binary-abacus/Packages' (uncompressed,gzipped,bzip2ed)
-v6*= looking for changes in 'test1|ugly|source'...
-v6*=  replacing './dists/test1/ugly/source/Sources' (gzipped,bzip2ed)
-v0*=Deleting files no longer referenced...
EOF
checklog log1 <<EOF
DATESTR remove test1 deb stupid abacus simple 1
DATESTR remove test1 dsc stupid source simple 1
DATESTR remove test1 deb ugly abacus simple 1
DATESTR remove test1 dsc ugly source simple 1
EOF
testrun - -b . remove test2 simple 3<<EOF
*=To be forgotten filekey 'pool/ugly/s/simple/simple_1_abacus.deb' was not known.
-v0*=There have been errors!
stdout
-v1=removing 'simple' from 'test2|ugly|abacus'...
-d1*=db: 'simple' removed from packages.db(test2|ugly|abacus).
-v1=removing 'simple' from 'test2|ugly|source'...
-d1*=db: 'simple' removed from packages.db(test2|ugly|source).
-v0=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*= looking for changes in 'test2|ugly|abacus'...
-v6*=  replacing './dists/test2/ugly/binary-abacus/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
-v6*=  replacing './dists/test2/ugly/source/Sources' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v0=Deleting files no longer referenced...
-v1=deleting and forgetting pool/ugly/s/simple/simple_1_abacus.deb
-v1=deleting and forgetting pool/ugly/s/simple/simple_1.dsc
-v1=deleting and forgetting pool/ugly/s/simple/simple_1.tar.gz
returns 249
EOF
checklog log2 <<EOF
DATESTR remove test2 deb ugly abacus simple 1
DATESTR remove test2 dsc ugly source simple 1
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results

cat > broken.changes <<EOF
Format: -1.0
Date: yesterday
Source: differently
Version: 0another
Architecture: source abacus
Urgency: super-hyper-duper-important
Maintainer: still me <guess@who>
Description: missing
Changes: missing
Binary: none and nothing
Distribution: test2
Files: 
 `md5sum 4test_b.1-1.dsc| cut -d" " -f 1` `stat -c%s 4test_b.1-1.dsc` a b differently_0another.dsc
 `md5sum 4test_b.1-1_abacus.deb| cut -d" " -f 1` `stat -c%s 4test_b.1-1_abacus.deb` a b 4test_b.1-1_abacus.deb
EOF
#todo: make it work without this..
cp 4test_b.1-1.dsc differently_0another.dsc
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1.dsc' does not start with a digit, violating 'should'-directive in policy 5.6.11
=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*=I don't know what to do having a .dsc without a .diff.gz or .tar.gz in 'broken.changes'!
-v0*=There have been errors!
returns 255
EOF
cat >> broken.changes <<EOF
 `md5sum 4test_b.1-1.tar.gz| cut -d" " -f 1` `stat -c%s 4test_b.1-1.tar.gz` a b 4test_b.1-1.tar.gz
EOF
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: Strange file '4test_b.1-1.tar.gz'!
*=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
*='./pool/stupid/d/differently/4test_b.1-1_abacus.deb' has packagename '4test' not listed in the .changes file!
*=To ignore use --ignore=surprisingbinary.
-v0*=There have been errors!
stdout
-v2*=Created directory "./pool/stupid/d"
-v2*=Created directory "./pool/stupid/d/differently"
-v1*=deleting and forgetting pool/stupid/d/differently/4test_b.1-1.tar.gz
-v1*=deleting and forgetting pool/stupid/d/differently/4test_b.1-1_abacus.deb
-v1*=deleting and forgetting pool/stupid/d/differently/differently_0another.dsc
-v1*=removed now empty directory ./pool/stupid/d/differently
-v1*=removed now empty directory ./pool/stupid/d
returns 255
EOF
testrun - -b . --ignore=surprisingbinary include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: Strange file '4test_b.1-1.tar.gz'!
*=Looks like source but does not start with 'differently_' as I would have guessed!
=I hope you know what you do.
*='./pool/stupid/d/differently/4test_b.1-1_abacus.deb' has packagename '4test' not listed in the .changes file!
*=Ignoring as --ignore=surprisingbinary given.
*='./pool/stupid/d/differently/4test_b.1-1_abacus.deb' lists source package '4test', but .changes says it is 'differently'!
-v0*=There have been errors!
stdout
-v2*=Created directory "./pool/stupid/d"
-v2*=Created directory "./pool/stupid/d/differently"
-v1*=deleting and forgetting pool/stupid/d/differently/4test_b.1-1.tar.gz
-v1*=deleting and forgetting pool/stupid/d/differently/4test_b.1-1_abacus.deb
-v1*=deleting and forgetting pool/stupid/d/differently/differently_0another.dsc
-v1*=removed now empty directory ./pool/stupid/d/differently
-v1*=removed now empty directory ./pool/stupid/d
returns 255
EOF
cat > broken.changes <<EOF
Format: -1.0
Date: yesterday
Source: 4test
Version: 0orso
Architecture: source abacus
Urgency: super-hyper-duper-important
Maintainer: still me <guess@who>
Description: missing
Changes: missing
Binary: 4test
Distribution: test2
Files: 
 `md5sum 4test_b.1-1.dsc| cut -d" " -f 1` `stat -c%s 4test_b.1-1.dsc` a b 4test_0orso.dsc
 `md5sum 4test_b.1-1_abacus.deb| cut -d" " -f 1` `stat -c%s 4test_b.1-1_abacus.deb` a b 4test_b.1-1_abacus.deb
 `md5sum 4test_b.1-1.tar.gz| cut -d" " -f 1` `stat -c%s 4test_b.1-1.tar.gz` a b 4test_b.1-1.tar.gz
EOF
cp 4test_b.1-1.dsc 4test_0orso.dsc
testrun - -b . include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
=Warning: Package version 'b.1-1_abacus.deb' does not start with a digit, violating 'should'-directive in policy 5.6.11
*='./pool/stupid/4/4test/4test_b.1-1_abacus.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=To ignore use --ignore=wrongsourceversion.
-v0*=There have been errors!
stdout
-v1*=deleting and forgetting pool/stupid/4/4test/4test_0orso.dsc
returns 255
EOF
testrun - -b . --ignore=wrongsourceversion include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*='./pool/stupid/4/4test/4test_b.1-1_abacus.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=Ignoring as --ignore=wrongsourceversion given.
*='4test_0orso.dsc' says it is version '1:b.1-1', while .changes file said it is '0orso'
*=To ignore use --ignore=wrongversion.
-v0*=There have been errors!
stdout
-v1*=deleting and forgetting pool/stupid/4/4test/4test_0orso.dsc
returns 255
EOF
checknolog log1
checknolog log2
testrun - -b . --ignore=wrongsourceversion --ignore=wrongversion include test2 broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*='./pool/stupid/4/4test/4test_b.1-1_abacus.deb' lists source version '1:b.1-1', but .changes says it is '0orso'!
*=Ignoring as --ignore=wrongsourceversion given.
*='4test_0orso.dsc' says it is version '1:b.1-1', while .changes file said it is '0orso'
*=Ignoring as --ignore=wrongversion given.
stdout
-d1*=db: '4test' added to packages.db(test2|stupid|abacus).
-d1*=db: '4test' added to packages.db(test2|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v6*=  replacing './dists/test2/stupid/binary-abacus/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*=  replacing './dists/test2/stupid/source/Sources' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v6*= looking for changes in 'test2|ugly|abacus'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
EOF
checklog log2 <<EOF
DATESTR add test2 deb stupid abacus 4test 1:b.1-1
DATESTR add test2 dsc stupid source 4test 1:b.1-1
EOF
testrun - -b . remove test2 4test 3<<EOF
stdout
-v1*=removing '4test' from 'test2|stupid|abacus'...
-d1*=db: '4test' removed from packages.db(test2|stupid|abacus).
-v1*=removing '4test' from 'test2|stupid|source'...
-d1*=db: '4test' removed from packages.db(test2|stupid|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test2|stupid|abacus'...
-v6*=  replacing './dists/test2/stupid/binary-abacus/Packages' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v6*= looking for changes in 'test2|stupid|coal'...
-v6*= looking for changes in 'test2|stupid|source'...
-v6*=  replacing './dists/test2/stupid/source/Sources' (uncompressed,gzipped,script: $SRCDIR/docs/bzip.example)
-v6*= looking for changes in 'test2|ugly|abacus'...
-v6*= looking for changes in 'test2|ugly|coal'...
-v6*= looking for changes in 'test2|ugly|source'...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/stupid/4/4test/4test_0orso.dsc
EOF
checklog log2 <<EOF
DATESTR remove test2 deb stupid abacus 4test 1:b.1-1
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
*=Deleting vanished identifier 'test1|stupid|abacus'.
*=Deleting vanished identifier 'test1|stupid|source'.
*=Deleting vanished identifier 'test1|ugly|abacus'.
*=Deleting vanished identifier 'test1|ugly|source'.
*=Deleting vanished identifier 'test2|stupid|abacus'.
*=Deleting vanished identifier 'test2|stupid|coal'.
*=Deleting vanished identifier 'test2|stupid|source'.
*=Deleting vanished identifier 'test2|ugly|abacus'.
*=Deleting vanished identifier 'test2|ugly|coal'.
*=Deleting vanished identifier 'test2|ugly|source'.
*=Deleting tracking data for vanished distribution 'test1'.
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/stupid/4/4test/4test-addons_b.1-1_all.deb
-v1*=deleting and forgetting pool/stupid/4/4test/4test_b.1-1_abacus.deb
-v1*=deleting and forgetting pool/stupid/4/4test/4test_b.1-1.dsc
-v1*=deleting and forgetting pool/stupid/4/4test/4test_b.1-1.tar.gz
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_9.0-A:Z+a:z-0+aA.9zZ_all.deb
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ_abacus.deb
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.dsc
-v1*=deleting and forgetting pool/ugly/b/bloat+-0a9z.app/bloat+-0a9z.app_9.0-A:Z+a:z-0+aA.9zZ.tar.gz
-v1*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app-addons_0.9-A:Z+a:z-0+aA.9zZ_all.deb
-v1*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ_abacus.deb
-v1*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.dsc
-v1*=deleting and forgetting pool/stupid/b/bloat+-0a9z.app/bloat+-0a9z.app_0.9-A:Z+a:z-0+aA.9zZ.tar.gz
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1_abacus.deb
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1.dsc
-v1*=deleting and forgetting pool/stupid/s/simple/simple_1.tar.gz
-v1*=deleting and forgetting pool/stupid/t/test/test-addons_1-2_all.deb
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2_abacus.deb
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2.dsc
-v1*=deleting and forgetting pool/stupid/t/test/test_1.orig.tar.gz
-v1*=deleting and forgetting pool/stupid/t/test/test_1-2.diff.gz
-v1*=deleting and forgetting pool/stupid/t/testb/testb-addons_2-3_all.deb
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-3_abacus.deb
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-3.dsc
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2.orig.tar.gz
-v1*=deleting and forgetting pool/stupid/t/testb/testb_2-3.diff.gz
-v1*=deleting and forgetting pool/ugly/s/simple/simple-addons_1_all.deb
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
Architectures: abacus source
Components: all
Tracking: minimal
Log: logab

Codename: b
Architectures: abacus
Components: all
Pull: froma
Log: logab
EOF
else
cat >> conf/distributions <<EOF

Codename: a
Architectures: abacus source
Components: all
Log: logab

Codename: b
Architectures: abacus
Components: all
Pull: froma
Log: logab
EOF
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
-v3*=  pulling into 'b|all|abacus'
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
-v3*=  pulling into 'b|all|abacus'
-v5*=  looking what to get from 'a|all|abacus'
-v0*=Installing (and possibly deleting) packages...
EOF
checklog logab < /dev/null
test ! -d dists/a
test ! -d dists/b
testrun - -b . --export=normal pull b 3<<EOF
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|abacus'
-v5*=  looking what to get from 'a|all|abacus'
-v0*=Installing (and possibly deleting) packages...
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/b"
-v2*=Created directory "./dists/b/all"
-v2*=Created directory "./dists/b/all/binary-abacus"
-v6*= looking for changes in 'b|all|abacus'...
-v6*=  creating './dists/b/all/binary-abacus/Packages' (uncompressed,gzipped)
EOF
checklog logab < /dev/null
test ! -d dists/a
test -d dists/b
testrun - -b . --export=normal pull a b 3<<EOF
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|abacus'
-v5*=  looking what to get from 'a|all|abacus'
-v0*=Installing (and possibly deleting) packages...
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|abacus'...
-v2*=Created directory "./dists/a"
-v2*=Created directory "./dists/a/all"
-v2*=Created directory "./dists/a/all/binary-abacus"
-v6*= looking for changes in 'a|all|abacus'...
-v6*=  creating './dists/a/all/binary-abacus/Packages' (uncompressed,gzipped)
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
*=Warning: database 'a|all|abacus' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
stdout
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/all"
-v2*=Created directory "./pool/all/a"
-v2*=Created directory "./pool/all/a/aa"
-d1*=db: 'aa-addons' added to packages.db(a|all|abacus).
-d1*=db: 'aa' added to packages.db(a|all|abacus).
-d1*=db: 'aa' added to packages.db(a|all|source).
-v5*=Deleting 'test.changes'.
EOF
checklog logab << EOF
DATESTR add a deb all abacus aa-addons 1-1
DATESTR add a deb all abacus aa 1-1
DATESTR add a dsc all source aa 1-1
EOF
test ! -d dists/a
test ! -d dists/b
test ! -f test.changes
test ! -f aa_1-1_abacus.deb
test ! -f aa_1-1.dsc
test ! -f aa_1-1.tar.gz
test ! -f aa-addons_1-1_all.deb
test -f pool/all/a/aa/aa-addons_1-1_all.deb
test -f pool/all/a/aa/aa_1-1_abacus.deb
test -f pool/all/a/aa/aa_1-1.dsc
test -f pool/all/a/aa/aa_1-1.tar.gz
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-1
Files:
 pool/all/a/aa/aa-addons_1-1_all.deb a 1
 pool/all/a/aa/aa_1-1_abacus.deb b 1
 pool/all/a/aa/aa_1-1.dsc s 1
 pool/all/a/aa/aa_1-1.tar.gz s 1

END
if $tracking; then dodiff results.expected results ; else dodiff results.empty results ; fi
testrun - -b . export a 3<<EOF
stdout
-v1*=Exporting a...
-v2*=Created directory "./dists/a"
-v2*=Created directory "./dists/a/all"
-v2*=Created directory "./dists/a/all/binary-abacus"
-v6*= exporting 'a|all|abacus'...
-v6*=  creating './dists/a/all/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/a/all/source"
-v6*= exporting 'a|all|source'...
-v6*=  creating './dists/a/all/source/Sources' (gzipped)
EOF
checknolog logab
dogrep "Version: 1-1" dists/a/all/binary-abacus/Packages
rm -r dists/a
testrun - -b . --export=changed pull a b 3<<EOF
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|abacus'
-v5*=  looking what to get from 'a|all|abacus'
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'aa' added to packages.db(b|all|abacus).
-d1*=db: 'aa-addons' added to packages.db(b|all|abacus).
-v0*=Exporting indices...
-v2*=Created directory "./dists/b"
-v2*=Created directory "./dists/b/all"
-v2*=Created directory "./dists/b/all/binary-abacus"
-v6*= looking for changes in 'b|all|abacus'...
-v6*=  creating './dists/b/all/binary-abacus/Packages' (uncompressed,gzipped)
EOF
checklog logab << EOF
DATESTR add b deb all abacus aa 1-1
DATESTR add b deb all abacus aa-addons 1-1
EOF
test ! -d dists/a
test -d dists/b
dogrep "Version: 1-1" dists/b/all/binary-abacus/Packages
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-2" SECTION="stupid/base" genpackage.sh
testrun - -b . --export=changed --delete include a test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'aa-addons' removed from packages.db(a|all|abacus).
-d1*=db: 'aa-addons' added to packages.db(a|all|abacus).
-d1*=db: 'aa' removed from packages.db(a|all|abacus).
-d1*=db: 'aa' added to packages.db(a|all|abacus).
-d1*=db: 'aa' removed from packages.db(a|all|source).
-d1*=db: 'aa' added to packages.db(a|all|source).
-v0*=Exporting indices...
-v2*=Created directory "./dists/a"
-v2*=Created directory "./dists/a/all"
-v2*=Created directory "./dists/a/all/binary-abacus"
-v6*= looking for changes in 'a|all|abacus'...
-v6*=  creating './dists/a/all/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/a/all/source"
-v6*= looking for changes in 'a|all|source'...
-v6*=  creating './dists/a/all/source/Sources' (gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa_1-1.dsc
-v1*=deleting and forgetting pool/all/a/aa/aa_1-1.tar.gz
EOF
checklog logab << EOF
DATESTR replace a deb all abacus aa-addons 1-2 1-1
DATESTR replace a deb all abacus aa 1-2 1-1
DATESTR replace a dsc all source aa 1-2 1-1
EOF
test -f test.changes
test ! -f aa_1-2_abacus.deb
test ! -f aa_1-2.dsc
test ! -f aa_1-2.tar.gz
test ! -f aa-addons_1-2_all.deb
test -d dists/a
dogrep "Version: 1-2" dists/a/all/binary-abacus/Packages
dogrep "Version: 1-1" dists/b/all/binary-abacus/Packages
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-2
Files:
 pool/all/a/aa/aa-addons_1-2_all.deb a 1
 pool/all/a/aa/aa_1-2_abacus.deb b 1
 pool/all/a/aa/aa_1-2.dsc s 1
 pool/all/a/aa/aa_1-2.tar.gz s 1

END
if $tracking; then dodiff results.expected results ; else dodiff results.empty results ; fi
rm -r dists/a dists/b
testrun - -b . --export=changed pull a b 3<<EOF
stderr
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|abacus'
-v5*=  looking what to get from 'a|all|abacus'
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'aa' removed from packages.db(b|all|abacus).
-d1*=db: 'aa' added to packages.db(b|all|abacus).
-d1*=db: 'aa-addons' removed from packages.db(b|all|abacus).
-d1*=db: 'aa-addons' added to packages.db(b|all|abacus).
-v0=Exporting indices...
-v2*=Created directory "./dists/b"
-v2*=Created directory "./dists/b/all"
-v2*=Created directory "./dists/b/all/binary-abacus"
-v6*= looking for changes in 'b|all|abacus'...
-v6*=  creating './dists/b/all/binary-abacus/Packages' (uncompressed,gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa_1-1_abacus.deb
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-1_all.deb
EOF
checklog logab << EOF
DATESTR replace b deb all abacus aa 1-2 1-1
DATESTR replace b deb all abacus aa-addons 1-2 1-1
EOF
test ! -d dists/a
test -d dists/b
dogrep "Version: 1-2" dists/b/all/binary-abacus/Packages
DISTRI=a PACKAGE=aa EPOCH="" VERSION=1 REVISION="-3" SECTION="stupid/base" genpackage.sh
testrun - -b . --export=never include a test.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Warning: database 'a|all|abacus' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
*=Changes will only be visible after the next 'export'!
stdout
-d1*=db: 'aa-addons' removed from packages.db(a|all|abacus).
-d1*=db: 'aa-addons' added to packages.db(a|all|abacus).
-d1*=db: 'aa' removed from packages.db(a|all|abacus).
-d1*=db: 'aa' added to packages.db(a|all|abacus).
-d1*=db: 'aa' removed from packages.db(a|all|source).
-d1*=db: 'aa' added to packages.db(a|all|source).
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa_1-2.dsc
-v1*=deleting and forgetting pool/all/a/aa/aa_1-2.tar.gz
EOF
checklog logab << EOF
DATESTR replace a deb all abacus aa-addons 1-3 1-2
DATESTR replace a deb all abacus aa 1-3 1-2
DATESTR replace a dsc all source aa 1-3 1-2
EOF
test -f test.changes
test -f aa_1-3_abacus.deb
test -f aa_1-3.dsc
test -f aa_1-3.tar.gz
test -f aa-addons_1-3_all.deb
test ! -f pool/all/a/aa/aa_1-2.dsc
test -f pool/all/a/aa/aa_1-2_abacus.deb # still in b
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-3
Files:
 pool/all/a/aa/aa-addons_1-3_all.deb a 1
 pool/all/a/aa/aa_1-3_abacus.deb b 1
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
*=Warning: database 'a|all|abacus' was modified but no index file was exported.
*=Warning: database 'a|all|source' was modified but no index file was exported.
=Changes will only be visible after the next 'export'!
stdout
-v2*=Created directory "./pool/all/a/ab"
-d1*=db: 'ab-addons' added to packages.db(a|all|abacus).
-d1*=db: 'ab' added to packages.db(a|all|abacus).
-d1*=db: 'ab' added to packages.db(a|all|source).
-v5*=Deleting 'test.changes'.
EOF
checklog logab << EOF
DATESTR add a deb all abacus ab-addons 2-1
DATESTR add a deb all abacus ab 2-1
DATESTR add a dsc all source ab 2-1
EOF
testrun - -b . --export=changed pull b 3<<EOF
stderr
stdout
-v0*=Calculating packages to pull...
-v3*=  pulling into 'b|all|abacus'
-v5*=  looking what to get from 'a|all|abacus'
-v0*=Installing (and possibly deleting) packages...
-d1*=db: 'aa' removed from packages.db(b|all|abacus).
-d1*=db: 'aa' added to packages.db(b|all|abacus).
-d1*=db: 'aa-addons' removed from packages.db(b|all|abacus).
-d1*=db: 'aa-addons' added to packages.db(b|all|abacus).
-d1*=db: 'ab' added to packages.db(b|all|abacus).
-d1*=db: 'ab-addons' added to packages.db(b|all|abacus).
-v0=Exporting indices...
-v6*= looking for changes in 'b|all|abacus'...
-v6*=  replacing './dists/b/all/binary-abacus/Packages' (uncompressed,gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa_1-2_abacus.deb
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-2_all.deb
EOF
checklog logab << EOF
DATESTR replace b deb all abacus aa 1-3 1-2
DATESTR replace b deb all abacus aa-addons 1-3 1-2
DATESTR add b deb all abacus ab 2-1
DATESTR add b deb all abacus ab-addons 2-1
EOF
dogrep "Version: 1-3" dists/b/all/binary-abacus/Packages
dogrep "Version: 2-1" dists/b/all/binary-abacus/Packages
test ! -f pool/all/a/aa/aa_1-2_abacus.deb
test -f pool/all/a/aa/aa_1-3_abacus.deb
DISTRI=a PACKAGE=ab EPOCH="" VERSION=3 REVISION="-1" SECTION="stupid/base" genpackage.sh
grep -v '\.tar\.gz' test.changes > broken.changes
testrun - -b . --delete --delete include a broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=I don't know what to do having a .dsc without a .diff.gz or .tar.gz in 'broken.changes'!
-v0*=There have been errors!
returns 255
EOF
checknolog logab
echo ' d41d8cd98f00b204e9800998ecf8427e 0 stupid/base superfluous ab_3-1.diff.gz' >> broken.changes
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
test -f ab_3-1_abacus.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test ! -f pool/all/a/ab/ab-addons_3-1_all.deb
test ! -f pool/all/a/ab/ab_3-1_abacus.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
touch ab_3-1.diff.gz
testrun - -b . --delete -T deb include a broken.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
stdout
-d1*=db: 'ab-addons' removed from packages.db(a|all|abacus).
-d1*=db: 'ab-addons' added to packages.db(a|all|abacus).
-d1*=db: 'ab' removed from packages.db(a|all|abacus).
-d1*=db: 'ab' added to packages.db(a|all|abacus).
-v0*=Exporting indices...
-v2*=Created directory "./dists/a"
-v2*=Created directory "./dists/a/all"
-v2*=Created directory "./dists/a/all/binary-abacus"
-v6*= looking for changes in 'a|all|abacus'...
-v6*=  creating './dists/a/all/binary-abacus/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/a/all/source"
-v6*= looking for changes in 'a|all|source'...
-v6*=  creating './dists/a/all/source/Sources' (gzipped)
-v0*=Deleting files no longer referenced...
EOF
checklog logab <<EOF
DATESTR replace a deb all abacus ab-addons 3-1 2-1
DATESTR replace a deb all abacus ab 3-1 2-1
EOF
testout "" -b . dumpunreferenced
dodiff results.empty results
test -f broken.changes
test -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
testout "" -b . dumptracks a
cat >results.expected <<END
Distribution: a
Source: aa
Version: 1-3
Files:
 pool/all/a/aa/aa-addons_1-3_all.deb a 1
 pool/all/a/aa/aa_1-3_abacus.deb b 1
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
 pool/all/a/ab/ab_3-1_abacus.deb b 1

END
if $tracking; then dodiff results.expected results ; else dodiff results.empty results ; fi
testout "" -b . dumpunreferenced
dodiff results.empty results
testrun - -b . --delete --delete include a broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Unable to find ./pool/all/a/ab/ab_3-1.tar.gz!
=Perhaps you forgot to give dpkg-buildpackage the -sa option,
= or you cound try --ignore=missingfile
-v0*=There have been errors!
stdout
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.diff.gz
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
returns 249
EOF
test -f broken.changes
test -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test -f ab_3-1.dsc
test ! -f pool/all/a/ab/ab_3-1.diff.gz
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test ! -f pool/all/a/ab/ab_3-1.dsc
cat broken.changes
testrun - -b . -T dsc --delete --delete --ignore=missingfile include a broken.changes 3<<EOF
-v0=Data seems not to be signed trying to use directly...
*=Unable to find ./pool/all/a/ab/ab_3-1.tar.gz!
*=Looking around if it is elsewhere as --ignore=missingfile given.
stdout
-d1*=db: 'ab' removed from packages.db(a|all|source).
-d1*=db: 'ab' added to packages.db(a|all|source).
-v5*=Deleting 'broken.changes'.
-v0*=Exporting indices...
-v6*= looking for changes in 'a|all|abacus'...
-v6*= looking for changes in 'a|all|source'...
-v6*=  replacing './dists/a/all/source/Sources' (gzipped)
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/ab/ab_2-1.dsc
-v1*=deleting and forgetting pool/all/a/ab/ab_2-1.tar.gz
EOF
checklog logab <<EOF
DATESTR replace a dsc all source ab 3-1 2-1
EOF
test ! -f broken.changes
test ! -f ab_3-1.diff.gz
test ! -f ab-addons_3-1_all.deb
test ! -f ab_3-1_abacus.deb
test ! -f ab_3-1.dsc
# test ! -f pool/all/a/ab/ab_3-1.diff.gz # decide later (TODO: let reprepro check for those)
test -f pool/all/a/ab/ab-addons_3-1_all.deb
test -f pool/all/a/ab/ab_3-1_abacus.deb
test -f pool/all/a/ab/ab_3-1.dsc
testout "" -b . dumpunreferenced
cat > results.expected << EOF
pool/all/a/ab/ab_3-1.diff.gz
EOF
dodiff results.empty results || dodiff results.expected results
testrun - -b . deleteunreferenced 3<<EOF
stdout
-v1=deleting and forgetting pool/all/a/ab/ab_3-1.diff.gz
EOF

DISTRI=b PACKAGE=ac EPOCH="" VERSION=1 REVISION="-1" SECTION="stupid/base" genpackage.sh
testrun - -b . -A abacus --delete --delete --ignore=missingfile include b test.changes 3<<EOF
stderr
-v0=Data seems not to be signed trying to use directly...
-v2*=Skipping 'ac_1-1.dsc' as not for architecture 'abacus'.
-v2*=Skipping 'ac_1-1.tar.gz' as not for architecture 'abacus'.
-v3*=Placing 'ac-addons_1-1_all.deb' only in architecture 'abacus' as requested.
stdout
-v2*=Created directory "./pool/all/a/ac"
-d1*=db: 'ac-addons' added to packages.db(b|all|abacus).
-d1*=db: 'ac' added to packages.db(b|all|abacus).
-v5*=Deleting 'test.changes'.
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|abacus'...
-v6*=  replacing './dists/b/all/binary-abacus/Packages' (uncompressed,gzipped)
EOF
checklog logab <<EOF
DATESTR add b deb all abacus ac-addons 1-1
DATESTR add b deb all abacus ac 1-1
EOF
dogrep '^Package: aa$' dists/b/all/binary-abacus/Packages
dogrep '^Package: aa-addons$' dists/b/all/binary-abacus/Packages
dogrep '^Package: ab$' dists/b/all/binary-abacus/Packages
dogrep '^Package: ab-addons$' dists/b/all/binary-abacus/Packages
dogrep '^Package: ac$' dists/b/all/binary-abacus/Packages
dogrep '^Package: ac-addons$' dists/b/all/binary-abacus/Packages
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
-v6*=aptmethod start 'copy:$WORKDIR/dists/a/all/binary-abacus/Packages.gz'
-v1*=aptmethod got 'copy:$WORKDIR/dists/a/all/binary-abacus/Packages.gz'
-v6*=Called /bin/cp './lists/b_froma_deb_all_abacus' './lists/b_froma_deb_all_abacus_changed'
-v6*=Listhook successfully returned!
stdout
-v0*=Removing obsolete or to be replaced packages...
-v3*=  processing updates for 'b|all|abacus'
-v5*=  marking everything to be deleted
-v5*=  reading './lists/b_froma_deb_all_abacus_changed'
-d1*=db: 'ac-addons' removed from packages.db(b|all|abacus).
-v1*=removing 'ab' from 'b|all|abacus'...
-d1*=db: 'ab' removed from packages.db(b|all|abacus).
-v1*=removing 'ab-addons' from 'b|all|abacus'...
-d1*=db: 'ab-addons' removed from packages.db(b|all|abacus).
-v1*=removing 'ac' from 'b|all|abacus'...
-d1*=db: 'ac' removed from packages.db(b|all|abacus).
-v1*=removing 'ac-addons' from 'b|all|abacus'...
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|abacus'...
-v6*=  replacing './dists/b/all/binary-abacus/Packages' (uncompressed,gzipped)
-v1*=Shutting down aptmethods...
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/ab/ab_2-1_abacus.deb
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_2-1_all.deb
-v1*=deleting and forgetting pool/all/a/ac/ac_1-1_abacus.deb
-v1*=deleting and forgetting pool/all/a/ac/ac-addons_1-1_all.deb
-v1*=removed now empty directory ./pool/all/a/ac
EOF
checklog logab <<EOF
DATESTR remove b deb all abacus ab 2-1
DATESTR remove b deb all abacus ab-addons 2-1
DATESTR remove b deb all abacus ac 1-1
DATESTR remove b deb all abacus ac-addons 1-1
EOF
dogrep '^Package: aa$' dists/b/all/binary-abacus/Packages
dogrep '^Package: aa-addons$' dists/b/all/binary-abacus/Packages
dongrep '^Package: ab$' dists/b/all/binary-abacus/Packages
dongrep '^Package: ab-addons$' dists/b/all/binary-abacus/Packages
dongrep '^Package: ac$' dists/b/all/binary-abacus/Packages
dongrep '^Package: ac-addons$' dists/b/all/binary-abacus/Packages
test ! -f pool/all/a/ac/ac-addons_1-1_all.deb
test ! -f pool/all/a/ab/ab_2-1_abacus.deb
test -f pool/all/a/aa/aa_1-3_abacus.deb
testrun - -b . copy b a ab ac 3<<EOF
stdout
-v9*=Adding reference to 'pool/all/a/ab/ab_3-1_abacus.deb' by 'b|all|abacus'
-v1*=Moving 'ab' from 'a|all|abacus' to 'b|all|abacus'.
-v3*=Not looking into 'a|all|source' as no matching target in 'b'!
-v3*=No instance of 'ab' found in 'a|all|source'!
-v3*=No instance of 'ac' found in 'a|all|abacus'!
-v3*=No instance of 'ac' found in 'a|all|source'!
-v1*=Looking for 'ab' in 'a' to be copied to 'b'...
-d1*=db: 'ab' added to packages.db(b|all|abacus).
-v1*=Looking for 'ac' in 'a' to be copied to 'b'...
-v0*=Exporting indices...
-v6*= looking for changes in 'b|all|abacus'...
-v6*=  replacing './dists/b/all/binary-abacus/Packages' (uncompressed,gzipped)
EOF
checklog logab <<EOF
DATESTR add b deb all abacus ab 3-1
EOF
if $tracking ; then
testout "" -b . dumptracks
cat > results.expected <<EOF
Distribution: a
Source: aa
Version: 1-3
Files:
 pool/all/a/aa/aa-addons_1-3_all.deb a 1
 pool/all/a/aa/aa_1-3_abacus.deb b 1
 pool/all/a/aa/aa_1-3.dsc s 1
 pool/all/a/aa/aa_1-3.tar.gz s 1

Distribution: a
Source: ab
Version: 3-1
Files:
 pool/all/a/ab/ab-addons_3-1_all.deb a 1
 pool/all/a/ab/ab_3-1_abacus.deb b 1
 pool/all/a/ab/ab_3-1.dsc s 1
 pool/all/a/ab/ab_3-1.tar.gz s 1

EOF
dodiff results.expected results
testout "" -b . dumpreferences
cat > results.expected <<EOF
a aa 1-3 pool/all/a/aa/aa-addons_1-3_all.deb
a|all|abacus pool/all/a/aa/aa-addons_1-3_all.deb
b|all|abacus pool/all/a/aa/aa-addons_1-3_all.deb
a aa 1-3 pool/all/a/aa/aa_1-3.dsc
a|all|source pool/all/a/aa/aa_1-3.dsc
a aa 1-3 pool/all/a/aa/aa_1-3.tar.gz
a|all|source pool/all/a/aa/aa_1-3.tar.gz
a aa 1-3 pool/all/a/aa/aa_1-3_abacus.deb
a|all|abacus pool/all/a/aa/aa_1-3_abacus.deb
b|all|abacus pool/all/a/aa/aa_1-3_abacus.deb
a ab 3-1 pool/all/a/ab/ab-addons_3-1_all.deb
a|all|abacus pool/all/a/ab/ab-addons_3-1_all.deb
a ab 3-1 pool/all/a/ab/ab_3-1.dsc
a|all|source pool/all/a/ab/ab_3-1.dsc
a ab 3-1 pool/all/a/ab/ab_3-1.tar.gz
a|all|source pool/all/a/ab/ab_3-1.tar.gz
a ab 3-1 pool/all/a/ab/ab_3-1_abacus.deb
a|all|abacus pool/all/a/ab/ab_3-1_abacus.deb
b|all|abacus pool/all/a/ab/ab_3-1_abacus.deb
EOF
dodiff results.expected results
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
a|all|abacus pool/all/a/aa/aa-addons_1-3_all.deb
b|all|abacus pool/all/a/aa/aa-addons_1-3_all.deb
a|all|source pool/all/a/aa/aa_1-3.dsc
a|all|source pool/all/a/aa/aa_1-3.tar.gz
a|all|abacus pool/all/a/aa/aa_1-3_abacus.deb
b|all|abacus pool/all/a/aa/aa_1-3_abacus.deb
a|all|abacus pool/all/a/ab/ab-addons_3-1_all.deb
a|all|source pool/all/a/ab/ab_3-1.dsc
a|all|source pool/all/a/ab/ab_3-1.tar.gz
a|all|abacus pool/all/a/ab/ab_3-1_abacus.deb
b|all|abacus pool/all/a/ab/ab_3-1_abacus.deb
EOF
dodiff results.expected results
cat > conf/distributions <<EOF
Codename: X
Architectures: none
Components: test
EOF
if $tracking ; then
testrun - -b . --delete clearvanished 3<<EOF
-v4*=Strange, 'X|test|none' does not appear in packages.db yet.
stdout
*=Deleting vanished identifier 'a|all|abacus'.
*=Deleting vanished identifier 'a|all|source'.
*=Deleting vanished identifier 'b|all|abacus'.
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-3_all.deb
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.dsc
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.tar.gz
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3_abacus.deb
-v1*=removed now empty directory ./pool/all/a/aa
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_3-1_all.deb
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.tar.gz
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1_abacus.deb
-v1*=removed now empty directory ./pool/all/a/ab
-v1*=removed now empty directory ./pool/all/a
-v1*=removed now empty directory ./pool/all
-v1*=removed now empty directory ./pool
EOF
else
testrun - -b . --delete clearvanished 3<<EOF
-v4*=Strange, 'X|test|none' does not appear in packages.db yet.
stdout
*=Deleting vanished identifier 'a|all|abacus'.
*=Deleting vanished identifier 'a|all|source'.
*=Deleting vanished identifier 'b|all|abacus'.
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/all/a/aa/aa-addons_1-3_all.deb
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.dsc
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3.tar.gz
-v1*=deleting and forgetting pool/all/a/aa/aa_1-3_abacus.deb
-v1*=removed now empty directory ./pool/all/a/aa
-v1*=deleting and forgetting pool/all/a/ab/ab-addons_3-1_all.deb
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.dsc
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1.tar.gz
-v1*=deleting and forgetting pool/all/a/ab/ab_3-1_abacus.deb
-v1*=removed now empty directory ./pool/all/a/ab
-v1*=removed now empty directory ./pool/all/a
-v1*=removed now empty directory ./pool/all
-v1*=removed now empty directory ./pool
EOF
fi
done
set +v +x
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
