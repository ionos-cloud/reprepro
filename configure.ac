dnl
dnl Process this file with autoconf to produce a configure script
dnl

AC_INIT(reprepro, 4.13.0, brlink@debian.org)
AC_CONFIG_SRCDIR(main.c)
AC_CONFIG_AUX_DIR(ac)
AM_INIT_AUTOMAKE([-Wall -Werror -Wno-portability])
AM_CONFIG_HEADER(config.h)

if test "${CFLAGS+set}" != set ; then
	CFLAGS="-Wall -O2 -g -Wmissing-prototypes -Wstrict-prototypes -Wshadow"
fi

AM_MAINTAINER_MODE
AC_GNU_SOURCE

AC_PROG_CC_C99
AC_PROG_INSTALL
AC_SYS_LARGEFILE

AC_C_BIGENDIAN()
AC_HEADER_STDBOOL
AC_CHECK_FUNCS([closefrom strndup dprintf tdestroy])
found_mktemp=no
AC_CHECK_FUNCS([mkostemp mkstemp],[found_mktemp=yes ; break],)
if test "$found_mktemp" = "no" ; then
	AC_MSG_ERROR([Missing mkstemp or mkostemp])
fi
AC_CHECK_FUNC([vasprintf],,[AC_MSG_ERROR([Could not find vasprintf implementation!])])

DBLIBS=""
# the only way to find out which is compileable is to look into db.h:

AC_CHECK_HEADER(db.h,,[AC_MSG_ERROR(["no db.h found"])])

AC_CHECK_LIB(db, db_create, [DBLIBS="-ldb $DBLIBS"
	],[AC_MSG_ERROR(["no libdb found"])],[$DBLIBS])
AC_SUBST([DBLIBS])

AC_CHECK_LIB(z,gzopen,,[AC_MSG_ERROR(["no zlib found"])],)

AC_ARG_WITH(libgpgme,
[  --with-libgpgme=path|yes|no	Give path to prefix libgpgme was installed with],[dnl
	case "$withval" in
	no)
	;;
	yes)
	AC_CHECK_HEADER(gpgme.h,,[AC_MSG_ERROR(["no gpgme.h found"])])
	AC_CHECK_LIB(gpg-error,gpg_strsource,,[AC_MSG_ERROR(["no libgpg-error found"])],)
	AC_CHECK_LIB(gpgme,gpgme_get_protocol_name,,[AC_MSG_ERROR(["no libgpgme found (need at least 0.4.1)"])],)
	;;
	*)
	CPPFLAGS="$CPPFLAGS -I$withval/include"
	LIBS="$LIBS -L$withval/lib"
	AC_CHECK_HEADER(gpgme.h,,[AC_MSG_ERROR(["no gpgme.h found"])])
	AC_CHECK_LIB(gpg-error,gpg_strsource,,[AC_MSG_ERROR(["no libgpg-error found"])],)
	AC_CHECK_LIB(gpgme,gpgme_get_protocol_name,,[AC_MSG_ERROR(["no libgpgme found (need at least 0.4.1)"])],)
	;;
	esac
],[dnl default is to behave like yes (for libgpgme only)
	AC_CHECK_HEADER(gpgme.h,,[AC_MSG_ERROR(["no gpgme.h found (to disable run with --without-libgpgme)"])])
	AC_CHECK_LIB(gpg-error,gpg_strsource,,[AC_MSG_ERROR(["no libgpg-error found (to disable run with --without-libgpgme)"])],)
	AC_CHECK_LIB(gpgme,gpgme_get_protocol_name,,[AC_MSG_ERROR(["did not find libgpgme versoion 0.4.1 or later (to disable run with --without-libgpgme)"])],)
])

AC_ARG_WITH(libbz2,
[  --with-libbz2=path|yes|no	Give path to prefix libbz2 was installed with],[dnl
	case "$withval" in
	no)
	;;
	yes)
	AC_CHECK_LIB(bz2,BZ2_bzCompressInit,,[AC_MSG_ERROR(["no libbz2 found, despite being told to use it"])],)
	;;
	*)
	AC_CHECK_LIB(bz2,BZ2_bzCompressInit,[dnl
		AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIBBZ2))
		LIBS="$LIBS -L$withval/lib -lbz2"
		CPPFLAGS="$CPPFLAGS -I$withval/include"
	],[AC_MSG_ERROR(["no libbz2 found, despite being told to use it"])],[-L$withval/lib])
	;;
	esac
],[dnl without --with-libbz2 we look for it but not finding it is no error:
	AC_CHECK_LIB(bz2,BZ2_bzCompressInit,,[AC_MSG_WARN(["no libbz2 found, compiling without"])],)
])

ARCHIVELIBS=""
ARCHIVECPP=""
AH_TEMPLATE([HAVE_LIBARCHIVE],[Defined if libarchive is available])
AC_ARG_WITH(libarchive,
[  --with-libarchive=path|yes|no  Give path to prefix libarchive was installed with],[dnl
	case "$withval" in
	no)
	;;
	yes)
	AC_CHECK_LIB(archive,archive_read_new,[dnl
		AC_CHECK_HEADER(archive.h,[dnl
			AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIBARCHIVE),1)
			ARCHIVELIBS="-larchive"
		],[AC_MSG_ERROR([Could not find archive.h])])
	],[AC_MSG_ERROR([Could not find libarchive])])
	;;
	*)
	AC_CHECK_LIB(archive,archive_read_new,[dnl
		mysave_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="-I$withval/include $CPPFLAGS"
		AC_CHECK_HEADER(archive.h,[dnl
			AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIBARCHIVE),1)
			ARCHIVELIBS="-L$withval/lib -larchive"
			ARCHIVECPP="-I$withval/include"
		],[AC_MSG_ERROR([Could not find archive.h])])
		CPPFLAGS="$mysave_CPPFLAGS"
	],[AC_MSG_ERROR([Could not find libarchive])],[-L$withval/lib])
	;;
	esac
],[dnl without --with-libarchive we look for it but not finding it is no error:
	AC_CHECK_LIB(archive,archive_read_new,[dnl
		AC_CHECK_HEADER(archive.h,[dnl
			AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIBARCHIVE),1)
			ARCHIVELIBS="-larchive"
		],)
	],)
])
AC_ARG_WITH(static-libarchive,
[  --with-static-libarchive=.a-file  static libarchive library to be linked against],
[	case "$withval" in
	no|yes) AC_MSG_ERROR([--with-static-libarchive needs an .a file as parameter])
	;;
	*)
	AC_CHECK_LIB(c,archive_read_new,[dnl
		mysave_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$ARCHIVECPP $CPPFLAGS"
		AC_CHECK_HEADER(archive.h,[dnl
			AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIBARCHIVE))
			ARCHIVELIBS="$withval"
		],[AC_MSG_ERROR([Could not find archive.h])])
		CPPFLAGS="$mysave_CPPFLAGS"
	],[AC_MSG_ERROR([Error linking against $withval])],[$withval])
	esac
])
AM_CONDITIONAL([HAVE_LIBARCHIVE],[test -n "$ARCHIVELIBS"])
AC_SUBST([ARCHIVELIBS])
AC_SUBST([ARCHIVECPP])

dnl
dnl Create makefiles
dnl

AC_CONFIG_FILES([Makefile docs/Makefile tests/Makefile])
AC_OUTPUT
