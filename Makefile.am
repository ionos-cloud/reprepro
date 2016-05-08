SUBDIRS = docs tests

EXTRA_DIST = autogen.sh

bin_PROGRAMS = reprepro changestool rredtool

if HAVE_LIBARCHIVE
ARCHIVE_USED = ar.c debfile.c
ARCHIVE_CONTENTS = debfilecontents.c
ARCHIVE_UNUSED = extractcontrol.c
else
ARCHIVE_USED = extractcontrol.c
ARCHIVE_CONTENTS =
ARCHIVE_UNUSED = ar.c debfile.c debfilecontents.c
endif

AM_CPPFLAGS = $(ARCHIVECPP) $(DBCPPFLAGS)
reprepro_LDADD = $(ARCHIVELIBS) $(DBLIBS)
changestool_LDADD = $(ARCHIVELIBS)

reprepro_SOURCES = outhook.c descriptions.c sizes.c sourcecheck.c byhandhook.c archallflood.c needbuild.c globmatch.c printlistformat.c diffindex.c rredpatch.c pool.c atoms.c uncompression.c remoterepository.c indexfile.c copypackages.c sourceextraction.c checksums.c readtextfile.c filecntl.c sha1.c sha256.c configparser.c database.c freespace.c hooks.c log.c changes.c incoming.c uploaderslist.c guesscomponent.c files.c md5.c dirs.c chunks.c reference.c binaries.c sources.c checks.c names.c dpkgversions.c release.c mprintf.c updates.c strlist.c signature_check.c signedfile.c signature.c distribution.c checkindeb.c checkindsc.c checkin.c upgradelist.c target.c aptmethod.c downloadcache.c main.c override.c terms.c termdecide.c ignore.c filterlist.c exports.c tracking.c optionsfile.c donefile.c pull.c contents.c filelist.c $(ARCHIVE_USED) $(ARCHIVE_CONTENTS)
EXTRA_reprepro_SOURCE = $(ARCHIVE_UNUSED)

changestool_SOURCES = uncompression.c sourceextraction.c readtextfile.c filecntl.c tool.c chunkedit.c strlist.c checksums.c sha1.c sha256.c md5.c mprintf.c chunks.c signature.c dirs.c names.c $(ARCHIVE_USED)

rredtool_SOURCES = rredtool.c rredpatch.c mprintf.c filecntl.c sha1.c

noinst_HEADERS = outhook.h descriptions.h sizes.h sourcecheck.h byhandhook.h archallflood.h needbuild.h globmatch.h printlistformat.h pool.h atoms.h uncompression.h remoterepository.h copypackages.h sourceextraction.h checksums.h readtextfile.h filecntl.h sha1.h sha256.h configparser.h database_p.h database.h freespace.h hooks.h log.h changes.h incoming.h guesscomponent.h md5.h dirs.h files.h chunks.h reference.h binaries.h sources.h checks.h names.h release.h error.h mprintf.h updates.h strlist.h signature.h signature_p.h distribution.h debfile.h checkindeb.h checkindsc.h upgradelist.h target.h aptmethod.h downloadcache.h override.h terms.h termdecide.h ignore.h filterlist.h dpkgversions.h checkin.h exports.h globals.h tracking.h trackingt.h optionsfile.h donefile.h pull.h ar.h filelist.h contents.h chunkedit.h uploaderslist.h indexfile.h rredpatch.h diffindex.h package.h

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in $(srcdir)/configure $(srcdir)/stamp-h.in $(srcdir)/aclocal.m4 $(srcdir)/config.h.in

clean-local:
	-rm -rf autom4te.cache $(srcdir)/autom4te.cache

maintainer-clean-local:
	-rm -rf $(srcdir)/ac

# Some things for my private laziness
strictbooleancheck:
	/home/brl/gcc/b/gcc/cc1 -DHAVE_CONFIG_H -I/home/brl/gcc/b/gcc/include -I/usr/include -I. -Wall -DAVOID_CHECKPROBLEMS=1 -g -W -O2 *.c

SPLINT=splint
SPLITFLAGSFORVIM= -linelen 10000 -locindentspaces 0
SPLINTFLAGS= +posixlib -booltype bool -numabstractcast -fixedformalarray -enumint +enumindex +charint $(SPLITFLAGSFORVIM) $(EXTRASPLINTFLAGS)

splint:
	$(SPLINT) -DSPLINT=1 $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) -D_GNU_SOURCE=1 $(SPLINTFLAGS) $(foreach file,$(reprepro_SOURCES),$(srcdir)/$(file))
