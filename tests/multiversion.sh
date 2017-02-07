#!/bin/sh
set -u

# Copyright (C) 2017, Benjamin Drung <benjamin.drung@profitbricks.com>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

. "${0%/*}/shunit2-helper-functions.sh"

oneTimeSetUp() {
	for revision in 1 2 2+deb8u1 10; do
		(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster EPOCH="" VERSION=2.9 REVISION=-$revision ../genpackage.sh)
	done
}

setUp() {
	create_repo
}

four_hellos() {
	for revision in 1 2 2+deb8u1 10; do
		call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-${revision}_${ARCH}.deb
	done
}

test_ls() {
	(cd $PKGS && PACKAGE=kvm SECTION=main DISTRI=buster VERSION=1.2.1 REVISION=-8 ../genpackage.sh)
	(cd $PKGS && PACKAGE=kvm SECTION=main DISTRI=buster VERSION=1.2.1 REVISION=-9 ../genpackage.sh)
	(cd $PKGS && PACKAGE=appdirs SECTION=main DISTRI=buster VERSION=1.3.0 REVISION=-1 ../genpackage.sh)
	for package in hello_2.9-1 kvm_1.2.1-8 kvm_1.2.1-9 appdirs_1.3.0-1; do
		call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/${package}_${ARCH}.deb
	done
	assertEquals "\
kvm | 1.2.1-9 | buster | $ARCH
kvm | 1.2.1-8 | buster | $ARCH" "$($REPREPRO -b $REPO ls kvm)"
	assertEquals "\
buster|main|$ARCH: kvm 1.2.1-9
buster|main|$ARCH: kvm 1.2.1-8" "$($REPREPRO -b $REPO list buster kvm)"
}

test_sorting() {
	four_hellos
	assertEquals "\
buster|main|$ARCH: hello 2.9-10
buster|main|$ARCH: hello 2.9-2+deb8u1
buster|main|$ARCH: hello 2.9-2
buster|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	assertEquals "\
hello |       2.9-10 | buster | $ARCH
hello | 2.9-2+deb8u1 | buster | $ARCH
hello |        2.9-2 | buster | $ARCH
hello |        2.9-1 | buster | $ARCH" "$($REPREPRO -b $REPO ls hello)"
}

test_include_twice() {
	for revision in 1 2; do
		call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-${revision}_${ARCH}.deb
	done
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-1_${ARCH}.deb
	assertEquals "\
hello | 2.9-2 | buster | $ARCH
hello | 2.9-1 | buster | $ARCH" "$($REPREPRO -b $REPO ls hello)"
}

test_copy_latest() {
	four_hellos
	add_distro bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster hello hello
	assertEquals "bullseye|main|$ARCH: hello 2.9-10" "$($REPREPRO -b $REPO list bullseye)"
}

test_copy_specific() {
	four_hellos
	add_distro bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster hello=2.9-10 hello=2.9-1 hello=2.9-10
	assertEquals "\
bullseye|main|$ARCH: hello 2.9-10
bullseye|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list bullseye)"
}

test_remove_latest() {
	four_hellos
	add_distro bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster hello=2.9-10 hello=2.9-1 hello=2.9-10
	call $REPREPRO $VERBOSE_ARGS -b $REPO remove bullseye hello
	assertEquals "\
hello |       2.9-10 |   buster | $ARCH
hello | 2.9-2+deb8u1 |   buster | $ARCH
hello |        2.9-2 |   buster | $ARCH
hello |        2.9-1 |   buster | $ARCH
hello |        2.9-1 | bullseye | $ARCH" "$($REPREPRO -b $REPO ls hello)"
}

test_remove_specific() {
	four_hellos
	call $REPREPRO $VERBOSE_ARGS -b $REPO remove buster hello=2.9-2+deb8u1 hellox hello=2.9-2+deb8u1
	assertEquals "\
buster|main|$ARCH: hello 2.9-10
buster|main|$ARCH: hello 2.9-2
buster|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
}

test_removefilter() {
	(cd $PKGS && PACKAGE=kvm SECTION=main DISTRI=buster VERSION=1.2.1 REVISION=-8 ../genpackage.sh)
	(cd $PKGS && PACKAGE=kvm SECTION=main DISTRI=buster VERSION=1.2.1 REVISION=-9 ../genpackage.sh)
	for package in hello_2.9-1 kvm_1.2.1-8 kvm_1.2.1-9; do
		call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/${package}_${ARCH}.deb
	done
	assertEquals "\
buster|main|$ARCH: hello 2.9-1
buster|main|$ARCH: kvm 1.2.1-9
buster|main|$ARCH: kvm 1.2.1-8" "$($REPREPRO -b $REPO list buster)"

	add_distro bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster kvm
	assertEquals "bullseye|main|$ARCH: kvm 1.2.1-9" "$($REPREPRO -b $REPO list bullseye)"

	call $REPREPRO $VERBOSE_ARGS -b $REPO removefilter buster "Package (= kvm)"
	assertEquals "buster|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	assertTrue "kvm_1.2.1-8_$ARCH.deb is still in the pool!" "test ! -e $REPO/pool/main/k/kvm/kvm_1.2.1-8_$ARCH.deb"
	assertTrue "kvm_1.2.1-9_$ARCH.deb is missing from the pool!" "test -e $REPO/pool/main/k/kvm/kvm_1.2.1-9_$ARCH.deb"

	call $REPREPRO $VERBOSE_ARGS -b $REPO removefilter bullseye "Package (= kvm)"
	assertEquals "buster|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	assertTrue "kvm_1.2.1-9_$ARCH.deb is still in the pool!" "test ! -e $REPO/pool/main/k/kvm/kvm_1.2.1-9_$ARCH.deb"
}

test_readd_distribution() {
	# Test case for https://github.com/profitbricks/reprepro/issues/1
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-1_${ARCH}.deb

	# Add distribution
	cp $REPO/conf/distributions $REPO/conf/distributions.backup
	add_distro bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb bullseye $PKGS/hello_2.9-2_${ARCH}.deb

	# Remove distribution
	mv $REPO/conf/distributions.backup $REPO/conf/distributions
	call $REPREPRO $VERBOSE_ARGS -b $REPO --delete clearvanished

	# Re-add distribution again
	echo "I: Re-adding bullseye..."
	add_distro bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb bullseye $PKGS/hello_2.9-10_${ARCH}.deb
	assertEquals "bullseye|main|$ARCH: hello 2.9-10" "$($REPREPRO -b $REPO list bullseye)"
}

test_limit3() {
	echo "Limit: 3" >> $REPO/conf/distributions
	for revision in 1 2 2+deb8u1; do
		call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-${revision}_${ARCH}.deb
	done
	assertEquals "\
buster|main|${ARCH}: hello 2.9-2+deb8u1
buster|main|${ARCH}: hello 2.9-2
buster|main|${ARCH}: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-10_${ARCH}.deb
	assertEquals "\
buster|main|${ARCH}: hello 2.9-10
buster|main|${ARCH}: hello 2.9-2+deb8u1
buster|main|${ARCH}: hello 2.9-2" "$($REPREPRO -b $REPO list buster)"
}

test_reduce_limit() {
	for revision in 1 2 2+deb8u1; do
		call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-${revision}_${ARCH}.deb
	done
	assertEquals "\
buster|main|${ARCH}: hello 2.9-2+deb8u1
buster|main|${ARCH}: hello 2.9-2
buster|main|${ARCH}: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	echo "Limit: 1" >> $REPO/conf/distributions
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-10_${ARCH}.deb
	assertEquals "buster|main|${ARCH}: hello 2.9-10" "$($REPREPRO -b $REPO list buster)"
	assertEquals "\
Distribution: buster
Source: hello
Version: 2.9-10
Files:
 pool/main/h/hello/hello_2.9-10_$ARCH.deb b 1" "$($REPREPRO -b $REPO dumptracks)"
}

test_limit_old() {
	for revision in 1 2 10; do
		call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-${revision}_${ARCH}.deb
	done
	assertEquals "\
buster|main|${ARCH}: hello 2.9-10
buster|main|${ARCH}: hello 2.9-2
buster|main|${ARCH}: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	echo "Limit: 2" >> $REPO/conf/distributions
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-2+deb8u1_${ARCH}.deb
	assertEquals "\
buster|main|${ARCH}: hello 2.9-10
buster|main|${ARCH}: hello 2.9-2+deb8u1" "$($REPREPRO -b $REPO list buster)"
}

. shunit2
