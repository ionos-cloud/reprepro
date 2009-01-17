#!/bin/bash

set -e

source $(dirname $0)/test.inc

runtest atoms
runtest trackingcorruption
runtest layeredupdate
runtest layeredupdate2
runtest uncompress
runtest check
runtest flat
runtest subcomponents
runtest snapshotcopyrestore
runtest various1
runtest various2
runtest various3

set +v +x
echo
echo "If the script is still running to show this,"
echo "all tested cases seem to work. (Though writing some tests more can never harm)."
exit 0
