#!/bin/sh
#
# Select only debs needed for a D-I netinstall cd

IN="$1"
OUT="$2"

DIR=`dirname "$IN"`
FILE=`basename "$IN"`
CODENAME=`echo $FILE | cut -d"_" -f1`
COMPONENT=`echo $FILE | cut -d"_" -f4`
ARCH=`echo $FILE | cut -d"_" -f5`

echo "### $IN"
echo "# Source: $IN"
echo "# Debs:   $DIR/$FILE.debs"
echo "# Out:    $OUT"
echo

# generate list of packages needed
DEBCDDIR="/usr/share/debian-cd"
export ARCH CODENAME DEBCDDIR DIR
make -f "$DEBCDDIR/Makefile"                          \
     BDIR='$(DIR)'                                    \
     INSTALLER_CD='2'                                 \
     TASK='$(DEBCDDIR)/tasks/debian-installer+kernel' \
     BASEDIR='$(DEBCDDIR)'                            \
     forcenonusoncd1='0'                              \
     VERBOSE_MAKE='yes'                               \
     "$DIR/list"

# hotfix abi name for sparc kernel
sed -e 's/-1-/-2-/' "$DIR/list" > "$DIR/$FILE.debs"
rm -f "$DIR/list"

# filter only needed packages
grep-dctrl `cat "$DIR/$FILE.debs" | while read P; do echo -n " -o -X -P $P"; done | cut -b 5-` "$IN" >"$OUT"

# cleanup
rm -f "$DIR/$FILE.debs"
