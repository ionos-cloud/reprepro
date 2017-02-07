#!/bin/sh

REPO=testrepo
PKGS=testpkgs
ARCH=${ARCH:-$(dpkg-architecture -qDEB_HOST_ARCH)}
REPREPRO=../reprepro

call() {
	echo "I: Calling $@"
	"$@" || fail "Command '$@' failed with exit code $?."
}

create_repo() {
	rm -rf $REPO $PKGS
	mkdir -p $REPO/conf
	cat > $REPO/conf/distributions <<EOF
Codename: buster
Architectures: $ARCH source
Components: main non-free
EOF
	mkdir -p $PKGS
	$REPREPRO -b $REPO export
}

add_repo() {
	local name="$1"
	cat >> $REPO/conf/distributions <<EOF

Codename: $name
Architectures: $ARCH source
Components: main non-free
EOF
}
