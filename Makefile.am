SUBDIRS = docs tests

EXTRA_DIST = autogen.sh

bin_PROGRAMS = reprepro

AM_CPPFLAGS = -D_GNU_SOURCE=1 -DPKGDATADIR=\"$(pkgdatadir)\" -Wall

reprepro_SOURCES = guesscomponent.c files.c md5.c md5sum.c dirs.c chunks.c reference.c packages.c binaries.c sources.c names.c dpkgversions.c release.c mprintf.c updates.c strlist.c signature.c distribution.c extractcontrol.c checkindeb.c checkindsc.c checkin.c copyfile.c upgradelist.c target.c aptmethod.c downloadcache.c main.c override.c terms.c ignore.c filterlist.c exports.c tracking.c
noinst_HEADERS = guesscomponent.h md5.h md5sum.h dirs.h files.h chunks.h reference.h packages.h binaries.h sources.h names.h release.h error.h mprintf.h updates.h strlist.h signature.h distribution.h extractcontrol.h checkindeb.h checkindsc.h copyfile.h upgradelist.h target.h aptmethod.h downloadcache.h override.h terms.h ignore.h filterlist.h dpkgversions.h checkin.h exports.h globals.h tracking.h

MAINTAINERCLEANFILES = Makefile.in configure install-sh stamp-h.in aclocal.m4 config.h.in mkinstalldirs
