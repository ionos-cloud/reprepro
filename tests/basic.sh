#!/bin/sh
#set -u

. bash_unit.inc

setup() {
	create_repo
}

test_empty() {
	$REPREPRO -b $REPO export
	call $REPREPRO -b $REPO list buster
	assert_equals "" "$($REPREPRO -b $REPO list buster)"
}

test_list() {
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=1.0 REVISION=-1 ../genpackage.sh)
	call $REPREPRO -b $REPO -V -C main includedeb buster $PKGS/hello_1.0-1_${ARCH}.deb
	assert_equals "buster|main|$ARCH: hello 1.0-1" "$($REPREPRO -b $REPO list buster)"
}

test_ls() {
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster EPOCH="1:" VERSION=2.5 REVISION=-3 ../genpackage.sh)
	call $REPREPRO -b $REPO -V -C main includedeb buster $PKGS/hello_2.5-3_${ARCH}.deb
	assert_equals "hello | 1:2.5-3 | buster | $ARCH" "$($REPREPRO -b $REPO ls hello)"
}

test_include_changes() {
	(cd $PKGS && PACKAGE=sl SECTION=main DISTRI=buster EPOCH="" VERSION=3.03 REVISION=-1 ../genpackage.sh)
	call $REPREPRO -b $REPO -V -C main include buster $PKGS/test.changes
	assert_equals "\
buster|main|amd64: sl 3.03-1
buster|main|amd64: sl-addons 3.03-1
buster|main|source: sl 3.03-1" "$($REPREPRO -b $REPO list buster)"
}
