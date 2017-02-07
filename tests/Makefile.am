EXTRA_DIST = \
brokenuncompressor.sh \
genpackage.sh \
test.inc \
test.sh \
atoms.test \
buildinfo.test \
buildneeding.test \
check.test \
copy.test \
descriptions.test \
diffgeneration.test \
easyupdate.test \
export.test \
exporthooks.test \
flat.test \
flood.test \
includeasc.test \
includeextra.test \
layeredupdate.test \
layeredupdate2.test \
listcodenames.test \
morgue.test \
onlysmalldeletes.test \
override.test \
packagediff.test \
signatures.test \
signed.test \
snapshotcopyrestore.test \
srcfilterlist.test \
subcomponents.test \
template.test \
trackingcorruption.test \
uncompress.test \
updatecorners.test \
updatepullreject.test \
uploaders.test \
various1.test \
various2.test \
various3.test \
verify.test \
wrongarch.test \
evil.key \
expired.key \
expiredwithsubkey.key \
expiredwithsubkey-working.key \
good.key \
revoked.key \
revoked.pkey \
withsubkeys.key \
withsubkeys-works.key \
basic.sh \
multiversion.sh \
shunit2-helper-functions.sh
MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

check:
	./basic.sh
	./multiversion.sh

clean-local:
	rm -rf testrepo testpkgs
