#!/bin/sh
#set -u

. bash_unit.inc

setup() {
	create_repo
}

five_hellos() {
	for revision in 1 0bd1 0bd1a 0bd10 2; do
		(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster EPOCH="" VERSION=2.8 REVISION=-$revision ../genpackage.sh)
		call $REPREPRO -b $REPO -V -C main includedeb buster $PKGS/hello_2.8-${revision}_${ARCH}.deb
	done
}

test_ls() {
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=1.0 REVISION=-1 ../genpackage.sh)
	(cd $PKGS && PACKAGE=kvm SECTION=main DISTRI=buster VERSION=1.2.1 REVISION=-8 ../genpackage.sh)
	(cd $PKGS && PACKAGE=kvm SECTION=main DISTRI=buster VERSION=1.2.1 REVISION=-9 ../genpackage.sh)
	(cd $PKGS && PACKAGE=appdirs SECTION=main DISTRI=buster VERSION=1.3.0 REVISION=-1 ../genpackage.sh)
	for package in hello_1.0-1 kvm_1.2.1-8 kvm_1.2.1-9 appdirs_1.3.0-1; do
		call $REPREPRO -b $REPO -V -C main includedeb buster $PKGS/${package}_${ARCH}.deb
	done
	assert_equals "\
kvm | 1.2.1-9 | buster | $ARCH
kvm | 1.2.1-8 | buster | $ARCH" "$($REPREPRO -b $REPO ls kvm)"
	assert_equals "\
buster|main|amd64: kvm 1.2.1-9
buster|main|amd64: kvm 1.2.1-8" "$($REPREPRO -b $REPO list buster kvm)"
}

test_sorting() {
	five_hellos
	assert_equals "\
buster|main|amd64: hello 2.8-2
buster|main|amd64: hello 2.8-1
buster|main|amd64: hello 2.8-0bd10
buster|main|amd64: hello 2.8-0bd1a
buster|main|amd64: hello 2.8-0bd1" "$($REPREPRO -b $REPO list buster)"
	assert_equals "\
hello |     2.8-2 | buster | amd64
hello |     2.8-1 | buster | amd64
hello | 2.8-0bd10 | buster | amd64
hello | 2.8-0bd1a | buster | amd64
hello |  2.8-0bd1 | buster | amd64" "$($REPREPRO -b $REPO ls hello)"
}

test_include_twice() {
	for revision in 1 2; do
		(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster EPOCH="" VERSION=2.8 REVISION=-$revision ../genpackage.sh)
		call $REPREPRO -b $REPO -V -C main includedeb buster $PKGS/hello_2.8-${revision}_${ARCH}.deb
	done
	call $REPREPRO -b $REPO -V -C main includedeb buster $PKGS/hello_2.8-1_${ARCH}.deb
	assert_equals "\
hello | 2.8-2 | buster | amd64
hello | 2.8-1 | buster | amd64" "$($REPREPRO -b $REPO ls hello)"
}

test_copy_latest() {
	five_hellos
	add_repo bullseye
	call $REPREPRO -b $REPO -V copy bullseye buster hello hello
	assert_equals "bullseye|main|amd64: hello 2.8-2" "$($REPREPRO -b $REPO list bullseye)"
}

test_copy_specific() {
	five_hellos
	add_repo bullseye
	call $REPREPRO -b $REPO -V copy bullseye buster hello=2.8-0bd10 hello=2.8-1 hello=2.8-0bd10
	assert_equals "\
bullseye|main|amd64: hello 2.8-1
bullseye|main|amd64: hello 2.8-0bd10" "$($REPREPRO -b $REPO list bullseye)"
}

test_remove_latest() {
	five_hellos
	add_repo bullseye
	call $REPREPRO -b $REPO -V copy bullseye buster hello=2.8-0bd10 hello=2.8-1 hello=2.8-0bd10
	call $REPREPRO -b $REPO -V remove bullseye hello
	assert_equals "\
hello |     2.8-2 |   buster | amd64
hello |     2.8-1 |   buster | amd64
hello | 2.8-0bd10 |   buster | amd64
hello | 2.8-0bd1a |   buster | amd64
hello |  2.8-0bd1 |   buster | amd64
hello | 2.8-0bd10 | bullseye | amd64" "$($REPREPRO -b $REPO ls hello)"
}

test_remove_specific() {
	five_hellos
	call $REPREPRO -b $REPO -V remove buster hello=2.8-0bd1a hellox hello=2.8-0bd1a
	assert_equals "\
buster|main|amd64: hello 2.8-2
buster|main|amd64: hello 2.8-1
buster|main|amd64: hello 2.8-0bd10
buster|main|amd64: hello 2.8-0bd1" "$($REPREPRO -b $REPO list buster)"
}

test_removefilter() {
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=1.0 REVISION=-1 ../genpackage.sh)
	(cd $PKGS && PACKAGE=kvm SECTION=main DISTRI=buster VERSION=1.2.1 REVISION=-8 ../genpackage.sh)
	(cd $PKGS && PACKAGE=kvm SECTION=main DISTRI=buster VERSION=1.2.1 REVISION=-9 ../genpackage.sh)
	for package in hello_1.0-1 kvm_1.2.1-8 kvm_1.2.1-9; do
		call $REPREPRO -b $REPO -V -C main includedeb buster $PKGS/${package}_${ARCH}.deb
	done
	assert_equals "\
buster|main|amd64: hello 1.0-1
buster|main|amd64: kvm 1.2.1-9
buster|main|amd64: kvm 1.2.1-8" "$($REPREPRO -b $REPO list buster)"

	add_repo bullseye
	call $REPREPRO -b $REPO -V copy bullseye buster kvm
	assert_equals "bullseye|main|amd64: kvm 1.2.1-9" "$($REPREPRO -b $REPO list bullseye)"

	echo "I: Calling $REPREPRO -b $REPO -VVV removefilter buster \"Package (= kvm)\""
	call $REPREPRO -b $REPO -V removefilter buster "Package (= kvm)"
	assert_equals "buster|main|amd64: hello 1.0-1" "$($REPREPRO -b $REPO list buster)"
	assert "test ! -e $REPO/pool/main/k/kvm/kvm_1.2.1-8_amd64.deb" "kvm_1.2.1-8_amd64.deb is still in the pool!"
	assert "test -e $REPO/pool/main/k/kvm/kvm_1.2.1-9_amd64.deb" "kvm_1.2.1-9_amd64.deb is missing from the pool!"

	echo "I: Calling $REPREPRO -b $REPO -VVV removefilter bullseye \"Package (= kvm)\""
	call $REPREPRO -b $REPO -V removefilter bullseye "Package (= kvm)"
	assert_equals "buster|main|amd64: hello 1.0-1" "$($REPREPRO -b $REPO list buster)"
	assert "test ! -e $REPO/pool/main/k/kvm/kvm_1.2.1-9_amd64.deb" "kvm_1.2.1-9_amd64.deb is still in the pool!"
}

test_readd_distribution() {
	# Test case for https://github.com/profitbricks/reprepro/issues/1
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=buster VERSION=1.0 REVISION=-1 ../genpackage.sh)
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=bullseye VERSION=1.3 REVISION=-1 ../genpackage.sh)
	(cd $PKGS && PACKAGE=hello SECTION=main DISTRI=bullseye VERSION=1.4 REVISION=-1 ../genpackage.sh)
	call $REPREPRO -b $REPO -V -C main includedeb buster $PKGS/hello_1.0-1_${ARCH}.deb

	# Add distribution
	cp $REPO/conf/distributions $REPO/conf/distributions.backup
	add_repo bullseye
	call $REPREPRO -b $REPO -V -C main includedeb bullseye $PKGS/hello_1.3-1_${ARCH}.deb

	# Remove distribution
	mv $REPO/conf/distributions.backup $REPO/conf/distributions
	call $REPREPRO -b $REPO -V --delete clearvanished

	# Re-add distribution again
	echo "I: Re-adding bullseye..."
	add_repo bullseye
	call $REPREPRO -b $REPO -V -C main includedeb bullseye $PKGS/hello_1.4-1_${ARCH}.deb
	assert_equals "bullseye|main|$ARCH: hello 1.4-1" "$($REPREPRO -b $REPO list bullseye)"
}
