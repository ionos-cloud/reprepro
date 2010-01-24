#!/bin/sh
# This is an example script for a byhandhook.
# Add to you conf/distributions something like
##ByhandHooks:
## * * * copybyhand.sh
# and copy this script as copybyhand.sh in your conf/
# directory (or give the full path), and processincoming
# will copy all byhand/raw files to dists/codename/extra/*

set -e

if [ $# != 5 ] ; then
	echo "to be called by reprepro as byhandhook" >&2
	exit 1
fi
if [ -z "$REPREPRO_DIST_DIR" ] ; then
	echo "to be called by reprepro as byhandhook" >&2
	exit 1
fi

codename="$1"
section="$2"
priority="$3"
basefilename="$4"
fullfilename="$5"

mkdir -p "$REPREPRO_DIST_DIR/$codename/extra"
install -T -- "$fullfilename" "$REPREPRO_DIST_DIR/$codename/extra/$basefilename"
