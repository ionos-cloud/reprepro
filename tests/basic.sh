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

setUp() {
	create_repo
}

tearDown() {
	check_db
}

test_empty() {
	$REPREPRO -b $REPO export
	call $REPREPRO -b $REPO list buster
	assertEquals "" "$($REPREPRO -b $REPO list buster)"
}

test_list() {
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=1.0 REVISION=-1 ../genpackage.sh)
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_1.0-1_${ARCH}.deb
	assertEquals "buster|main|$ARCH: hello 1.0-1" "$($REPREPRO -b $REPO list buster)"
}

test_ls() {
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster EPOCH="1:" VERSION=2.5 REVISION=-3 ../genpackage.sh)
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.5-3_${ARCH}.deb
	assertEquals "hello | 1:2.5-3 | buster | $ARCH" "$($REPREPRO -b $REPO ls hello)"
}

test_copy() {
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster EPOCH="1:" VERSION=2.5 REVISION=-3 ../genpackage.sh)
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.5-3_${ARCH}.deb
	assertEquals "hello | 1:2.5-3 | buster | $ARCH" "$($REPREPRO -b $REPO ls hello)"
	add_distro bullseye
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster hello
	assertEquals "bullseye|main|$ARCH: hello 1:2.5-3" "$($REPREPRO -b $REPO list bullseye)"
}

test_copy_existing() {
	add_distro bullseye
	(cd $PKGS && PACKAGE=sl SECTION=main DISTRI=buster EPOCH="" VERSION=3.03 REVISION=-1 ../genpackage.sh)
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/sl_3.03-1_${ARCH}.deb
	assertEquals "sl | 3.03-1 | buster | $ARCH" "$($REPREPRO -b $REPO ls sl)"
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster sl
	assertEquals "\
sl | 3.03-1 |   buster | $ARCH
sl | 3.03-1 | bullseye | $ARCH" "$($REPREPRO -b $REPO ls sl)"
	call $REPREPRO $VERBOSE_ARGS -b $REPO copy bullseye buster sl
	assertEquals "\
sl | 3.03-1 |   buster | $ARCH
sl | 3.03-1 | bullseye | $ARCH" "$($REPREPRO -b $REPO ls sl)"
}

test_include_changes() {
	(cd $PKGS && PACKAGE=sl SECTION=main DISTRI=buster EPOCH="" VERSION=3.03 REVISION=-1 ../genpackage.sh)
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main include buster $PKGS/sl_3.03-1_${ARCH}.changes
	assertEquals "\
buster|main|$ARCH: sl 3.03-1
buster|main|$ARCH: sl-addons 3.03-1
buster|main|source: sl 3.03-1" "$($REPREPRO -b $REPO list buster)"
}

test_include_old() {
	# Test including an old package version. Expected output:
	# Skipping inclusion of 'hello' '2.9-1' in 'buster|main|$ARCH', as it has already '2.9-2'.
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=2.9 REVISION=-1 ../genpackage.sh)
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=2.9 REVISION=-2 ../genpackage.sh)
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-2_${ARCH}.deb
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-1_${ARCH}.deb
	assertEquals "buster|main|$ARCH: hello 2.9-2" "$($REPREPRO -b $REPO list buster)"
}

test_limit() {
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=2.9 REVISION=-1 ../genpackage.sh)
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=2.9 REVISION=-2 ../genpackage.sh)
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-1_${ARCH}.deb
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main includedeb buster $PKGS/hello_2.9-2_${ARCH}.deb
	assertEquals "buster|main|$ARCH: hello 2.9-2" "$($REPREPRO -b $REPO list buster)"
}

test_older_version() {
	cat >> $REPO/conf/incoming <<EOF
Name: buster-upload
IncomingDir: incoming
TempDir: tmp
Allow: buster
Permit: older_version
EOF
	echo "Limit: 3" >> $REPO/conf/distributions
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=2.9 REVISION=-1 ../genpackage.sh)
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=2.9 REVISION=-2 ../genpackage.sh)
	mkdir -p "$REPO/incoming"
	cp "$PKGS/hello_2.9-2_${ARCH}.changes" "$PKGS/hello-addons_2.9-2_all.deb" "$PKGS/hello_2.9-2_${ARCH}.deb" "$PKGS/hello_2.9-2.dsc" "$PKGS/hello_2.9.orig.tar.gz" "$PKGS/hello_2.9-2.debian.tar.xz" "$REPO/incoming"
	call $REPREPRO $VERBOSE_ARGS -b $REPO processincoming buster-upload hello_2.9-2_${ARCH}.changes
	assertEquals "hello | 2.9-2 | buster | $ARCH, source" "$($REPREPRO -b $REPO ls hello)"
	cp "$PKGS/hello_2.9-1_${ARCH}.changes" "$PKGS/hello-addons_2.9-1_all.deb" "$PKGS/hello_2.9-1_${ARCH}.deb" "$PKGS/hello_2.9-1.dsc" "$PKGS/hello_2.9.orig.tar.gz" "$PKGS/hello_2.9-1.debian.tar.xz" "$REPO/incoming"
	call $REPREPRO $VERBOSE_ARGS -b $REPO processincoming buster-upload hello_2.9-1_${ARCH}.changes
	assertEquals "\
hello | 2.9-2 | buster | $ARCH, source
hello | 2.9-1 | buster | $ARCH, source" "$($REPREPRO -b $REPO ls hello)"
}

test_too_old_version() {
	# Allow only one version per package in the archive
	# Test if uploading an older version will not replace the newer version
	# in the archive.
	cat >> $REPO/conf/incoming <<EOF
Name: buster-upload
IncomingDir: incoming
TempDir: tmp
Allow: buster
Permit: older_version
EOF
	echo "Limit: 1" >> $REPO/conf/distributions
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=2.9 REVISION=-1 ../genpackage.sh)
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=2.9 REVISION=-2 ../genpackage.sh)
	mkdir -p "$REPO/incoming"
	cp "$PKGS/hello_2.9-2_${ARCH}.changes" "$PKGS/hello-addons_2.9-2_all.deb" "$PKGS/hello_2.9-2_${ARCH}.deb" "$PKGS/hello_2.9-2.dsc" "$PKGS/hello_2.9.orig.tar.gz" "$PKGS/hello_2.9-2.debian.tar.xz" "$REPO/incoming"
	call $REPREPRO $VERBOSE_ARGS -b $REPO processincoming buster-upload hello_2.9-2_${ARCH}.changes
	assertEquals "hello | 2.9-2 | buster | $ARCH, source" "$($REPREPRO -b $REPO ls hello)"
	cp "$PKGS/hello_2.9-1_${ARCH}.changes" "$PKGS/hello-addons_2.9-1_all.deb" "$PKGS/hello_2.9-1_${ARCH}.deb" "$PKGS/hello_2.9-1.dsc" "$PKGS/hello_2.9.orig.tar.gz" "$PKGS/hello_2.9-1.debian.tar.xz" "$REPO/incoming"
	call $REPREPRO $VERBOSE_ARGS -b $REPO processincoming buster-upload hello_2.9-1_${ARCH}.changes
	assertEquals "hello | 2.9-2 | buster | $ARCH, source" "$($REPREPRO -b $REPO ls hello)"
}

test_remove() {
	(cd $PKGS && PACKAGE=sl SECTION=main DISTRI=buster EPOCH="" VERSION=3.03 REVISION=-1 ../genpackage.sh)
	call $REPREPRO $VERBOSE_ARGS -b $REPO -C main include buster $PKGS/sl_3.03-1_${ARCH}.changes
	assertEquals "\
buster|main|$ARCH: sl 3.03-1
buster|main|$ARCH: sl-addons 3.03-1
buster|main|source: sl 3.03-1" "$($REPREPRO -b $REPO list buster)"
	call $REPREPRO $VERBOSE_ARGS -b $REPO remove buster sl
	assertEquals "buster|main|$ARCH: sl-addons 3.03-1" "$($REPREPRO -b $REPO list buster)"
}

test_listcodenames() {
	assertEquals "buster" "$($REPREPRO -b $REPO _listcodenames)"
	add_distro bullseye
	assertEquals "\
buster
bullseye" "$($REPREPRO -b $REPO _listcodenames)"
}

. shunit2
