#!/bin/sh
set -e
basedir=$(mktemp -d)
cd $basedir
mkdir -p conf
cat <<EOF >> conf/distributions
Origin: Ubuntu
Label: Ubuntu
Suite: impish
Version: 21.10
Codename: impish
Architectures: amd64 arm64 armhf i386 ppc64el riscv64 s390x
Components: main
Description: Ubuntu Impish 21.10
EOF
reprepro -b $basedir createsymlinks
apt-get download hello
reprepro -b $basedir includedeb impish hello*.deb
