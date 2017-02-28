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
		mkdir -p "$PKGS"
		(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster EPOCH="" VERSION=2.9 REVISION=-$revision ../genpackage.sh)
	done
}

setUp() {
	create_repo
	echo "Limit: -1" >> $REPO/conf/distributions
}

tearDown() {
	check_db
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
	add_distro bullseye "Limit: -1"
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster hello hello
	assertEquals "bullseye|main|$ARCH: hello 2.9-10" "$($REPREPRO -b $REPO list bullseye)"
}

test_copy_specific() {
	four_hellos
	add_distro bullseye "Limit: -1"
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster hello=2.9-10 hello=2.9-1 hello=2.9-10
	assertEquals "\
bullseye|main|$ARCH: hello 2.9-10
bullseye|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list bullseye)"
}

test_remove_latest() {
	four_hellos
	add_distro bullseye "Limit: -1"
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

	add_distro bullseye "Limit: -1"
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
	add_distro bullseye "Limit: -1"
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb bullseye $PKGS/hello_2.9-2_${ARCH}.deb

	# Remove distribution
	mv $REPO/conf/distributions.backup $REPO/conf/distributions
	call $REPREPRO $VERBOSE_ARGS -b $REPO --delete clearvanished

	# Re-add distribution again
	echo "I: Re-adding bullseye..."
	add_distro bullseye "Limit: -1"
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb bullseye $PKGS/hello_2.9-10_${ARCH}.deb
	assertEquals "bullseye|main|$ARCH: hello 2.9-10" "$($REPREPRO -b $REPO list bullseye)"
}

test_limit3() {
	sed -i 's/^Limit: .*$/Limit: 3/' $REPO/conf/distributions
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
	sed -i 's/^Limit: .*$/Limit: 1/' $REPO/conf/distributions
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-10_${ARCH}.deb
	assertEquals "buster|main|${ARCH}: hello 2.9-10" "$($REPREPRO -b $REPO list buster)"
	assertEquals "\
Distribution: buster
Source: hello
Version: 2.9-10
Files:
 pool/main/h/hello/hello_2.9-10_$ARCH.deb b 1" "$($REPREPRO -b $REPO dumptracks)"
}

test_reduce_limit_archive() {
	clear_distro
	add_distro buster-archive "Limit: 7"
	add_distro buster "Limit: -1\nArchive: buster-archive"
	for revision in 1 2; do
		call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-${revision}_${ARCH}.deb
	done
	assertEquals "\
buster|main|${ARCH}: hello 2.9-2
buster|main|${ARCH}: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	sed -i 's/^Limit: -1$/Limit: 1/' $REPO/conf/distributions
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-10_${ARCH}.deb
	assertEquals "\
hello |  2.9-2 | buster-archive | $ARCH
hello |  2.9-1 | buster-archive | $ARCH
hello | 2.9-10 |         buster | $ARCH" "$($REPREPRO -b $REPO ls hello)"
	assertEquals "\
Distribution: buster-archive
Source: hello
Version: 2.9-1
Files:
 pool/main/h/hello/hello_2.9-1_$ARCH.deb b 1

Distribution: buster-archive
Source: hello
Version: 2.9-2
Files:
 pool/main/h/hello/hello_2.9-2_$ARCH.deb b 1

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
	sed -i 's/^Limit: .*$/Limit: 2/' $REPO/conf/distributions
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-2+deb8u1_${ARCH}.deb
	assertEquals "\
buster|main|${ARCH}: hello 2.9-10
buster|main|${ARCH}: hello 2.9-2+deb8u1" "$($REPREPRO -b $REPO list buster)"
}

test_update_packages() {
	# Test case for https://github.com/profitbricks/reprepro/issues/6
	local upstream_repo
	upstream_repo="${0%/*}/upstreamrepo"

	four_hellos
	rm -rf "$upstream_repo"
	mv "$REPO" "$upstream_repo"

	mkdir -p "$REPO/conf"
	cat > "$REPO/conf/distributions" <<EOF
Origin: Icinga2
Label: Icinga2
Suite: icinga-stretch
Codename: icinga-stretch
Description: Icinga2 packages for Debian Stretch
Architectures: $ARCH
Components: main
Update: icinga-stretch
Log: icinga2.log
Limit: -1
EOF
	cat > "$REPO/conf/updates" <<EOF
Name: icinga-stretch
Method: file://$(readlink -f $upstream_repo)
Suite: buster
Components: main
Architectures: $ARCH
VerifyRelease: blindtrust
GetInRelease: no
EOF
	call $REPREPRO $VERBOSE_ARGS -b $REPO --noskipold update
	assertEquals "icinga-stretch|main|$ARCH: hello 2.9-10" "$($REPREPRO -b $REPO list icinga-stretch)"

	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb icinga-stretch $PKGS/hello_2.9-2_${ARCH}.deb
	call $REPREPRO $VERBOSE_ARGS -b $REPO --noskipold update
	assertEquals "\
icinga-stretch|main|$ARCH: hello 2.9-10
icinga-stretch|main|$ARCH: hello 2.9-2" "$($REPREPRO -b $REPO list icinga-stretch)"
}

test_includedsc_sources() {
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedsc buster $PKGS/hello_2.9-1.dsc
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedsc buster $PKGS/hello_2.9-2.dsc
	assertEquals "\
buster|main|source: hello 2.9-2
buster|main|source: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"

	call $REPREPRO $VERBOSE_ARGS -b $REPO removesrc buster hello 2.9-1
	assertEquals "buster|main|source: hello 2.9-2" "$($REPREPRO -b $REPO list buster)"
}

test_database_upgrade() {
	# Test case for https://github.com/profitbricks/reprepro/issues/8
	rm -rf "$REPO"
	cp -r "${0%/*}/old-database" "$REPO"
	call $REPREPRO $VERBOSE_ARGS -b $REPO export
	assertEquals "\
bullseye|main|amd64
bullseye|main|i386
bullseye|main|source
bullseye|non-free|amd64
bullseye|non-free|i386
bullseye|non-free|source" "$(db_dump "$REPO/db/packages.db" | sed -n 's/^database=//p')"
}

test_move_specific() {
	four_hellos
	add_distro bullseye
	$REPREPRO -b $REPO export bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO move bullseye buster hello=2.9-2
	assertEquals "\
buster|main|$ARCH: hello 2.9-10
buster|main|$ARCH: hello 2.9-2+deb8u1
buster|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	assertEquals "bullseye|main|$ARCH: hello 2.9-2" "$($REPREPRO -b $REPO list bullseye)"
}

test_movesrc_specific() {
	four_hellos
	add_distro bullseye
	$REPREPRO -b $REPO export bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO movesrc bullseye buster hello 2.9-2
	assertEquals "\
buster|main|$ARCH: hello 2.9-10
buster|main|$ARCH: hello 2.9-2+deb8u1
buster|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	assertEquals "bullseye|main|$ARCH: hello 2.9-2" "$($REPREPRO -b $REPO list bullseye)"
}

test_movefilter_specific() {
	four_hellos
	add_distro bullseye "Limit: -1"
	$REPREPRO -b $REPO export bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO movefilter bullseye buster 'Package (= hello), $Version (>> 2.9-2)'
	assertEquals "\
buster|main|$ARCH: hello 2.9-2
buster|main|$ARCH: hello 2.9-1" "$($REPREPRO -b $REPO list buster)"
	assertEquals "\
bullseye|main|$ARCH: hello 2.9-10
bullseye|main|$ARCH: hello 2.9-2+deb8u1" "$($REPREPRO -b $REPO list bullseye)"
}

. shunit2
