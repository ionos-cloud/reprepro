
bin_PROGRAMS = mirrorer

AM_CPPFLAGS = -D_GNU_SOURCE=1 -DPKGDATADIR=\"$(pkgdatadir)\" -Wall

mirrorer_SOURCES = guesscomponent.c files.c md5.c md5sum.c dirs.c chunks.c reference.c packages.c binaries.c sources.c names.c dpkgversions.c release.c mprintf.c updates.c strlist.c signature.c distribution.c extractcontrol.c checkindeb.c checkindsc.c checkin.c copyfile.c strtupel.c upgradelist.c target.c aptmethod.c downloadcache.c main.c
noinst_HEADERS = guesscomponent.h md5.h md5sum.h dirs.h files.h chunks.h reference.h packages.h binaries.h sources.h names.h release.h error.h mprintf.h updates.h strlist.h signature.h distribution.h extractcontrol.h checkindeb.h checkindsc.h copyfile.h strtupel.h upgradelist.h target.h aptmethod.h downloadcache.h

MAINTAINERCLEANFILES = Makefile.in configure install-sh stamp-h.in aclocal.m4 config.h.in mkinstalldirs
