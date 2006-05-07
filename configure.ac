dnl
dnl Process this file with autoconf to produce a configure script
dnl

AC_INIT(main.c)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(reprepro,0.8.2-cvs)

AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_INSTALL

AC_CHECK_LIB(z,gzopen,,[AC_MSG_ERROR(["no zlib found"])],)
AC_CHECK_LIB(db3,db_create,,[AC_MSG_ERROR(["no libdb3 found"])],)
AC_CHECK_LIB(gpgme,gpgme_check_version,,[AC_MSG_ERROR(["no libgpgme found"])],)
AC_CHECK_HEADER(gpgme.h,,[AC_MSG_ERROR(["no gpgme.h found"])])
AH_TEMPLATE([HASGPGMEGOODEXP],[Defined if libgpgme contains GPGME_SIG_STAT_GOOD_EXP])
CHECK_ENUM(GPGME_SIG_STAT_GOOD_EXP,[gpgme.h],[AC_DEFINE_UNQUOTED(AS_TR_CPP(HASGPGMEGOODEXP))],[],)

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
			AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIBARCHIVE))
			ARCHIVELIBS="-larchive"
		],[AC_MSG_ERROR([Could not find archive.h])])
	],[AC_MSG_ERROR([Could not find libarchive])])
	;;
	*)
	AC_CHECK_LIB(archive,archive_read_new,[dnl
		mysave_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="-I$withval/include $CPPFLAGS"
		AC_CHECK_HEADER(archive.h,[dnl
			AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIBARCHIVE))
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
			AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIBARCHIVE))
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

AC_OUTPUT([Makefile docs/Makefile tests/Makefile])


