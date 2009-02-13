#!/bin/bash

set -e
if [ "x$TESTINCSETUP" != "xissetup" ] ; then
	source $(dirname $0)/test.inc
fi

# first create a fake package with logs and byhand files:
mkdir documentation-9876AD
cd documentation-9876AD
mkdir debian
cat > debian/rules <<'EOF'
#!/usr/bin/make
tmp = $(CURDIR)/debian/tmp
binary-indep:
	install -m 755 -d $(tmp)/DEBIAN $(tmp)/usr/share/doc/documentation
	echo "I have told you so" > $(tmp)/usr/share/doc/documentation/NEWS
	gzip -c9 debian/changelog > $(tmp)/usr/share/doc/documentation/changelog.gz
	chown -R root.root $(tmp) && chmod -R go=rX $(tmp)
	dpkg-gencontrol -isp
	dpkg --build $(tmp) ..
	echo "I forgot" >> ../manifesto.txt
	echo "What?" >> ../history.txt
	dpkg-distaddfile manifesto.txt byhand -
	dpkg-distaddfile history.txt byhand -

.PHONY: clean binary-arch binary-indep binary build build-indep buil-arch
EOF
chmod a+x debian/rules
cat > debian/changelog <<EOF
documentation (9876AD) test; urgency=low

  * everything fixed

 -- Sky.NET <nowhere@example.com>  Sat, 15 Jan 9876 17:12:05 +2700
EOF

cat > debian/control <<EOF
Source: documentation
Section: doc
Priority: standard
Maintainer: Sky.NET <nowhere@example.com>
Standards-Version: Aleph_17

Package: documentation
Architecture: all
Description: documentation
 documentation
EOF

cd ..
dpkg-source -b documentation-9876AD ""
cd documentation-9876AD

fakeroot make -f debian/rules binary-indep > ../documentation_9876AD_coal+all.log
dpkg-genchanges > ../test.changes
cd ..
rm -r documentation-9876AD

ed -s test.changes <<EOF
/^Files: /a
 $(mdandsize documentation_9876AD_coal+all.log) - - documentation_9876AD_coal+all.log
.
w
q
EOF

mkdir conf
# first check files are properly ingored:
cat > conf/distributions <<EOF
Codename: test
Architectures: coal source
Components: main
EOF

testrun - include test test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
*=Ignoring byhand file: 'manifesto.txt'!
*=Ignoring byhand file: 'history.txt'!
*=Ignoring log file: 'documentation_9876AD_coal+all.log'!
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/d"
-v2*=Created directory "./pool/main/d/documentation"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' added to checksums.db(pool).
-d1*=db: 'documentation' added to packages.db(test|main|coal).
-d1*=db: 'documentation' added to packages.db(test|main|source).
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test"
-v2*=Created directory "./dists/test/main"
-v2*=Created directory "./dists/test/main/binary-coal"
-v6*= looking for changes in 'test|main|coal'...
-v6*=  creating './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/test/main/source"
-v6*= looking for changes in 'test|main|source'...
-v6*=  creating './dists/test/main/source/Sources' (gzipped)
EOF
rm -r pool dists db

# now include the byhand file:
cat > conf/distributions <<EOF
Codename: test
Architectures: coal source
Components: main
Tracking: minimal includebyhand
EOF

testrun - include test test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
*=Ignoring log file: 'documentation_9876AD_coal+all.log'!
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/d"
-v2*=Created directory "./pool/main/d/documentation"
-v2*=Created directory "./pool/main/d/documentation/documentation_9876AD_byhand"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/history.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' added to checksums.db(pool).
-d1*=db: 'documentation' added to packages.db(test|main|coal).
-d1*=db: 'documentation' added to packages.db(test|main|source).
-d1*=db: 'documentation' added to tracking.db(test).
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test"
-v2*=Created directory "./dists/test/main"
-v2*=Created directory "./dists/test/main/binary-coal"
-v6*= looking for changes in 'test|main|coal'...
-v6*=  creating './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/test/main/source"
-v6*= looking for changes in 'test|main|source'...
-v6*=  creating './dists/test/main/source/Sources' (gzipped)
EOF
cat >results.expected <<EOF
Distribution: test
Source: documentation
Version: 9876AD
Files:
 pool/main/d/documentation/documentation_9876AD_byhand/history.txt x 0
 pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt x 0
 pool/main/d/documentation/documentation_9876AD_all.deb a 1
 pool/main/d/documentation/documentation_9876AD.dsc s 1
 pool/main/d/documentation/documentation_9876AD.tar.gz s 1

EOF
testout - dumptracks test 3<<EOF
EOF
dodiff results.expected results
testrun - retrack 3<<EOF
stdout
-v1*=Chasing test...
EOF
testout - dumptracks test 3<<EOF
EOF
dodiff results.expected results
testrun - _listchecksums 3<<EOF
stdout
*=pool/main/d/documentation/documentation_9876AD.dsc $(fullchecksum documentation_9876AD.dsc)
*=pool/main/d/documentation/documentation_9876AD.tar.gz $(fullchecksum documentation_9876AD.tar.gz)
*=pool/main/d/documentation/documentation_9876AD_all.deb $(fullchecksum documentation_9876AD_all.deb)
*=pool/main/d/documentation/documentation_9876AD_byhand/history.txt $(fullchecksum history.txt)

*=pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt $(fullchecksum manifesto.txt)
EOF

testrun - remove test documentation 3<<EOF
stdout
-v1*=removing 'documentation' from 'test|main|coal'...
-d1*=db: 'documentation' removed from packages.db(test|main|coal).
-v1*=removing 'documentation' from 'test|main|source'...
-d1*=db: 'documentation' removed from packages.db(test|main|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|coal'...
-v6*=  replacing './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|source'...
-v6*=  replacing './dists/test/main/source/Sources' (gzipped)
-d1*=db: 'documentation' '9876AD' removed from tracking.db(test).
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD.dsc
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD.tar.gz
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD_all.deb
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD_byhand/history.txt
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/history.txt' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt' removed from checksums.db(pool).
-v2*=removed now empty directory ./pool/main/d/documentation/documentation_9876AD_byhand
-v2*=removed now empty directory ./pool/main/d/documentation
-v2*=removed now empty directory ./pool/main/d
-v2*=removed now empty directory ./pool/main
-v2*=removed now empty directory ./pool
EOF
dodo test ! -e pool
rm -r dists db

# now include the log file, too:
cat > conf/distributions <<EOF
Codename: test
Architectures: coal source
Components: main
Tracking: minimal includebyhand includelogs
EOF

testrun - include test test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/d"
-v2*=Created directory "./pool/main/d/documentation"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_coal+all.log' added to checksums.db(pool).
-v2*=Created directory "./pool/main/d/documentation/documentation_9876AD_byhand"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/history.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' added to checksums.db(pool).
-d1*=db: 'documentation' added to packages.db(test|main|coal).
-d1*=db: 'documentation' added to packages.db(test|main|source).
-d1*=db: 'documentation' added to tracking.db(test).
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test"
-v2*=Created directory "./dists/test/main"
-v2*=Created directory "./dists/test/main/binary-coal"
-v6*= looking for changes in 'test|main|coal'...
-v6*=  creating './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/test/main/source"
-v6*= looking for changes in 'test|main|source'...
-v6*=  creating './dists/test/main/source/Sources' (gzipped)
EOF
cat >results.expected <<EOF
Distribution: test
Source: documentation
Version: 9876AD
Files:
 pool/main/d/documentation/documentation_9876AD_byhand/history.txt x 0
 pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt x 0
 pool/main/d/documentation/documentation_9876AD_all.deb a 1
 pool/main/d/documentation/documentation_9876AD.dsc s 1
 pool/main/d/documentation/documentation_9876AD.tar.gz s 1
 pool/main/d/documentation/documentation_9876AD_coal+all.log l 0

EOF

testout - dumptracks test 3<<EOF
EOF
dodiff results.expected results

rm -r dists db pool

# and now everything at once, too:
cat > conf/distributions <<EOF
Codename: test
Architectures: coal source
Components: main
Tracking: minimal includebyhand includelogs includechanges
EOF

testrun - include test test.changes 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/d"
-v2*=Created directory "./pool/main/d/documentation"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_source+all.changes' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_coal+all.log' added to checksums.db(pool).
-v2*=Created directory "./pool/main/d/documentation/documentation_9876AD_byhand"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/history.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' added to checksums.db(pool).
-d1*=db: 'documentation' added to packages.db(test|main|coal).
-d1*=db: 'documentation' added to packages.db(test|main|source).
-d1*=db: 'documentation' added to tracking.db(test).
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test"
-v2*=Created directory "./dists/test/main"
-v2*=Created directory "./dists/test/main/binary-coal"
-v6*= looking for changes in 'test|main|coal'...
-v6*=  creating './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/test/main/source"
-v6*= looking for changes in 'test|main|source'...
-v6*=  creating './dists/test/main/source/Sources' (gzipped)
EOF
cat >results.expected <<EOF
Distribution: test
Source: documentation
Version: 9876AD
Files:
 pool/main/d/documentation/documentation_9876AD_byhand/history.txt x 0
 pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt x 0
 pool/main/d/documentation/documentation_9876AD_all.deb a 1
 pool/main/d/documentation/documentation_9876AD.dsc s 1
 pool/main/d/documentation/documentation_9876AD.tar.gz s 1
 pool/main/d/documentation/documentation_9876AD_coal+all.log l 0
 pool/main/d/documentation/documentation_9876AD_source+all.changes c 0

EOF
testout - dumptracks test 3<<EOF
EOF
dodiff results.expected results
testrun - _listchecksums 3<<EOF
stdout
*=pool/main/d/documentation/documentation_9876AD_coal+all.log $(fullchecksum documentation_9876AD_coal+all.log)
*=pool/main/d/documentation/documentation_9876AD_source+all.changes $(fullchecksum test.changes)
*=pool/main/d/documentation/documentation_9876AD.dsc $(fullchecksum documentation_9876AD.dsc)
*=pool/main/d/documentation/documentation_9876AD.tar.gz $(fullchecksum documentation_9876AD.tar.gz)
*=pool/main/d/documentation/documentation_9876AD_all.deb $(fullchecksum documentation_9876AD_all.deb)
*=pool/main/d/documentation/documentation_9876AD_byhand/history.txt $(fullchecksum history.txt)

*=pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt $(fullchecksum manifesto.txt)
EOF

testrun - remove test documentation 3<<EOF
stdout
-v1*=removing 'documentation' from 'test|main|coal'...
-d1*=db: 'documentation' removed from packages.db(test|main|coal).
-v1*=removing 'documentation' from 'test|main|source'...
-d1*=db: 'documentation' removed from packages.db(test|main|source).
-v0*=Exporting indices...
-v6*= looking for changes in 'test|main|coal'...
-v6*=  replacing './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v6*= looking for changes in 'test|main|source'...
-v6*=  replacing './dists/test/main/source/Sources' (gzipped)
-d1*=db: 'documentation' '9876AD' removed from tracking.db(test).
-v0*=Deleting files no longer referenced...
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD_source+all.changes
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_source+all.changes' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD_coal+all.log
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_coal+all.log' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD.dsc
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD.tar.gz
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD_all.deb
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD_byhand/history.txt
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/history.txt' removed from checksums.db(pool).
-v1*=deleting and forgetting pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt' removed from checksums.db(pool).
-v2*=removed now empty directory ./pool/main/d/documentation/documentation_9876AD_byhand
-v2*=removed now empty directory ./pool/main/d/documentation
-v2*=removed now empty directory ./pool/main/d
-v2*=removed now empty directory ./pool/main
-v2*=removed now empty directory ./pool
EOF
dodo test ! -e pool
rm -r dists db

mkdir i j tmp
mv *.txt documentation_9876AD* test.changes j/
cp j/* i/
cat > conf/incoming <<EOF
Name: foo
IncomingDir: i
TempDir: tmp
Default: test
EOF

testrun - processincoming foo 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/d"
-v2*=Created directory "./pool/main/d/documentation"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_source+all.changes' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_coal+all.log' added to checksums.db(pool).
-v2*=Created directory "./pool/main/d/documentation/documentation_9876AD_byhand"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/history.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' added to checksums.db(pool).
-d1*=db: 'documentation' added to packages.db(test|main|coal).
-d1*=db: 'documentation' added to packages.db(test|main|source).
-d1*=db: 'documentation' added to tracking.db(test).
-v1*=deleting './i/documentation_9876AD_all.deb'...
-v1*=deleting './i/documentation_9876AD_coal+all.log'...
-v1*=deleting './i/documentation_9876AD.tar.gz'...
-v1*=deleting './i/history.txt'...
-v1*=deleting './i/manifesto.txt'...
-v1*=deleting './i/documentation_9876AD.dsc'...
-v1*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test"
-v2*=Created directory "./dists/test/main"
-v2*=Created directory "./dists/test/main/binary-coal"
-v6*= looking for changes in 'test|main|coal'...
-v6*=  creating './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/test/main/source"
-v6*= looking for changes in 'test|main|source'...
-v6*=  creating './dists/test/main/source/Sources' (gzipped)
EOF
cat >results.expected <<EOF
Distribution: test
Source: documentation
Version: 9876AD
Files:
 pool/main/d/documentation/documentation_9876AD.dsc s 1
 pool/main/d/documentation/documentation_9876AD.tar.gz s 1
 pool/main/d/documentation/documentation_9876AD_all.deb a 1
 pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt x 0
 pool/main/d/documentation/documentation_9876AD_byhand/history.txt x 0
 pool/main/d/documentation/documentation_9876AD_coal+all.log l 0
 pool/main/d/documentation/documentation_9876AD_source+all.changes c 0

EOF

testout - dumptracks test 3<<EOF
EOF
dodiff results.expected results

rm -r db pool dists

cp j/* i/
ed -s conf/distributions <<EOF
g/^Tracking: /s/include[^ ]*//g
w
q
EOF

testrun - processincoming foo 3<<EOF
stdout
-v2*=Created directory "./db"
stderr
=Data seems not to be signed trying to use directly...
*=Error: 'test.changes' contains unused file 'documentation_9876AD_coal+all.log'!
*=(Do Permit: unused_files to conf/incoming to ignore and
*= additionaly Cleanup: unused_files to delete them)
*=Alternatively, you can also add a LogDir: for 'foo' into conf/incoming
*=then files like that will be stored there.
-v0*=There have been errors!
returns 255
EOF

cat >> conf/incoming <<EOF
Logdir: log
EOF

testrun - processincoming foo 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/d"
-v2*=Created directory "./pool/main/d/documentation"
-v2*=Created directory "log"
-v2*=Created directory "log/documentation_9876AD_source+all.0000000"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' added to checksums.db(pool).
-d1*=db: 'documentation' added to packages.db(test|main|coal).
-d1*=db: 'documentation' added to packages.db(test|main|source).
-d1*=db: 'documentation' added to tracking.db(test).
-v1*=deleting './i/documentation_9876AD_all.deb'...
-v1*=deleting './i/documentation_9876AD_coal+all.log'...
-v1*=deleting './i/documentation_9876AD.tar.gz'...
-v1*=deleting './i/history.txt'...
-v1*=deleting './i/manifesto.txt'...
-v1*=deleting './i/documentation_9876AD.dsc'...
-v1*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test"
-v2*=Created directory "./dists/test/main"
-v2*=Created directory "./dists/test/main/binary-coal"
-v6*= looking for changes in 'test|main|coal'...
-v6*=  creating './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/test/main/source"
-v6*= looking for changes in 'test|main|source'...
-v6*=  creating './dists/test/main/source/Sources' (gzipped)
EOF

ls log/documentation_9876AD_source+all.0000000 | sort > results
cat > results.expected <<EOF
documentation_9876AD_coal+all.log
history.txt
manifesto.txt
test.changes
EOF
dodiff results.expected results

rm -r db pool dists

cp j/* i/
ed -s conf/distributions <<EOF
g/^Tracking: /d
a
Tracking: all includechanges includelogs includebyhand
.
w
q
EOF

testrun - processincoming foo 3<<EOF
stderr
=Data seems not to be signed trying to use directly...
stdout
-v2*=Created directory "./db"
-v2*=Created directory "./pool"
-v2*=Created directory "./pool/main"
-v2*=Created directory "./pool/main/d"
-v2*=Created directory "./pool/main/d/documentation"
-v2*=Created directory "log/documentation_9876AD_source+all.0000001"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_source+all.changes' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_coal+all.log' added to checksums.db(pool).
-v2*=Created directory "./pool/main/d/documentation/documentation_9876AD_byhand"
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/history.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_byhand/manifesto.txt' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD_all.deb' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.tar.gz' added to checksums.db(pool).
-d1*=db: 'pool/main/d/documentation/documentation_9876AD.dsc' added to checksums.db(pool).
-d1*=db: 'documentation' added to packages.db(test|main|coal).
-d1*=db: 'documentation' added to packages.db(test|main|source).
-d1*=db: 'documentation' added to tracking.db(test).
-v1*=deleting './i/documentation_9876AD_all.deb'...
-v1*=deleting './i/documentation_9876AD_coal+all.log'...
-v1*=deleting './i/documentation_9876AD.tar.gz'...
-v1*=deleting './i/history.txt'...
-v1*=deleting './i/manifesto.txt'...
-v1*=deleting './i/documentation_9876AD.dsc'...
-v1*=deleting './i/test.changes'...
-v0*=Exporting indices...
-v2*=Created directory "./dists"
-v2*=Created directory "./dists/test"
-v2*=Created directory "./dists/test/main"
-v2*=Created directory "./dists/test/main/binary-coal"
-v6*= looking for changes in 'test|main|coal'...
-v6*=  creating './dists/test/main/binary-coal/Packages' (uncompressed,gzipped)
-v2*=Created directory "./dists/test/main/source"
-v6*= looking for changes in 'test|main|source'...
-v6*=  creating './dists/test/main/source/Sources' (gzipped)
EOF

ls log/documentation_9876AD_source+all.0000001 | sort > results
cat > results.expected <<EOF
documentation_9876AD_coal+all.log
test.changes
EOF
dodiff results.expected results

# TODO: check for multiple distributions
# some storing some not, and when the handling script is implemented

rm -r db pool dists
rm -r i j tmp conf results results.expected log
testsuccess
