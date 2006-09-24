SUBDIRS = docs tests

EXTRA_DIST = autogen.sh

bin_PROGRAMS = reprepro changestool

if HAVE_LIBARCHIVE
ARCHIVE_USED = ar.c debfile.c
ARCHIVE_UNUSED = extractcontrol.c 
else
ARCHIVE_USED = extractcontrol.c
ARCHIVE_UNUSED = ar.c debfile.c
endif

AM_CPPFLAGS = -D_GNU_SOURCE=1 -Wall $(ARCHIVECPP) $(DBCPPFLAGS)
reprepro_LDADD = $(ARCHIVELIBS) $(DBLIBS)
changestool_LDADD = $(ARCHIVELIBS)

reprepro_SOURCES = guesscomponent.c files.c md5.c md5sum.c dirs.c chunks.c reference.c packages.c binaries.c sources.c names.c dpkgversions.c release.c mprintf.c updates.c strlist.c signature.c distribution.c checkindeb.c checkindsc.c checkin.c copyfile.c upgradelist.c target.c aptmethod.c downloadcache.c main.c override.c terms.c ignore.c filterlist.c exports.c tracking.c optionsfile.c readrelease.c donefile.c pull.c contents.c filelist.c $(ARCHIVE_USED)
EXTRA_reprepro_SOURCE = $(ARCHIVE_UNUSED)

changestool_SOURCES = tool.c strlist.c md5sum.c md5.c mprintf.c chunks.c signature.c dirs.c ignore.c names.c copyfile.c $(ARCHIVE_USED)

noinst_HEADERS = guesscomponent.h md5.h md5sum.h dirs.h files.h chunks.h reference.h packages.h binaries.h sources.h names.h release.h error.h mprintf.h updates.h strlist.h signature.h distribution.h debfile.h checkindeb.h checkindsc.h copyfile.h upgradelist.h target.h aptmethod.h downloadcache.h override.h terms.h ignore.h filterlist.h dpkgversions.h checkin.h exports.h globals.h tracking.h trackingt.h optionsfile.h readrelease.h donefile.h pull.h ar.h filelist.h contents.h

MAINTAINERCLEANFILES = Makefile.in configure install-sh stamp-h.in aclocal.m4 config.h.in mkinstalldirs config.guess config.sub missing

maintainer-clean-local:
	-rm -rf autom4te.cache

# Some things for my private laziness
strictbooleancheck:
	/home/brl/gcc/b/gcc/cc1 -DHAVE_CONFIG_H -I/home/brl/gcc/b/gcc/include -I/usr/include -I.  -D_GNU_SOURCE=1 -Wall -DAVOID_CHECKPROBLEMS=1 -g -W -O2 *.c

SPLINT=splint
SPLITFLAGSFORVIM= -linelen 10000 -locindentspaces 0
SPLINTFLAGS= +posixlib $(SPLITFLAGSFORVIM)

splint:
	$(SPLINT) $(DEFS) $(INCLUDES) -D_GNU_SOURCE=1 $(SPLINTFLAGS) *.c
