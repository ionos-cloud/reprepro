dnl
dnl Process this file with autoconf to produce a configure script
dnl

AC_INIT(main.c)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(reprepro,2.2.2)

AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_INSTALL
AC_SYS_LARGEFILE


DBLIBS=""
DBCPPFLAGS=""
AH_TEMPLATE([LIBDB_VERSION],[variant of libdb used])
AC_ARG_WITH(libdb,
[  --with-libdb=path|4.4|4.3|3 Give prefix of libdb-4.3 or older version to use],[dnl
	case "$withval" in
	no)
	AC_MSG_ERROR(["Cannot compile without libdb"]);
	;;
	3)
	DB_WANT_VERSION=3
	;;
	yes|4.3)
	DB_WANT_VERSION=4.3
	;;
	4.4)
	DB_WANT_VERSION=4.4
	;;
	?|?.?)
	AC_MSG_ERROR([Unknown --with-libdb argument $withval])
	;;
	*)
	DB_WANT_VERSION=any
	DBLIBS="-L$withval/lib"
	DBCPPFLAGS="-I$withval/include"
	;;
	esac
],[DB_WANT_VERSION=any])

# the only way to find out which is compileable is to look into db.h:

mysave_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $DBCPPFLAGS"
AC_CHECK_HEADER(db.h,,[AC_MSG_ERROR(["no db.h found"])])
GET_DEFINE(DB_VERSION_MAJOR,db.h,[AC_MSG_ERROR("Unable to retrieve DB_VERSION_MAJOR from db.h")])
GET_DEFINE(DB_VERSION_MINOR,db.h,[AC_MSG_ERROR("Unable to retrieve DB_VERSION_MINOR from db.h")])
CPPFLAGS="$mysave_CPPFLAGS"

# now make sure what we found is what we want

if test "x$DB_VERSION_MAJOR" = "x3" ; then
	if test "$DB_WANT_VERSION" = "any" ; then
		DB_WANT_VERSION=3
	elif test "$DB_WANT_VERSION" != "3" ; then
		AC_MSG_ERROR([Found libdb major version $DB_VERSION_MAJOR.$DB_VERSION_MINOR, need $DB_WANT_VERSION])
	fi
elif test "x$DB_VERSION_MAJOR" = "x4" -a "x$DB_VERSION_MINOR" = "x3" ; then
	if test "$DB_WANT_VERSION" = "any" ; then
		DB_WANT_VERSION=4.3
	elif test "$DB_WANT_VERSION" != "4.3" ; then
		AC_MSG_ERROR([Found libdb version $DB_VERSION_MAJOR.$DB_VERSION_MINOR, need $DB_WANT_VERSION])
	fi
elif test "x$DB_VERSION_MAJOR" = "x4" -a "x$DB_VERSION_MINOR" = "x4" ; then
	if test "$DB_WANT_VERSION" = "any" ; then
		DB_WANT_VERSION=4.4
	elif test "$DB_WANT_VERSION" != "4.4" ; then
		AC_MSG_ERROR([Found libdb major version $DB_VERSION_MAJOR.$DB_VERSION_MINOR, need $DB_WANT_VERSION])
	fi
else
	AC_MSG_ERROR([Unsupported libdb major $DB_VERSION_MAJOR minor $DB_VERSION_MINOR
Only 4.3 (stronly recommended) or 3.2 or 4.4 are supported])
fi

# now we have a header with the expected version, check for the library:
case "$DB_WANT_VERSION" in
4.4)
	AC_CHECK_LIB(db-4.4,db_create,[dnl
			AC_DEFINE_UNQUOTED(AS_TR_CPP(LIBDB_VERSION),44)
			DBLIBS="-ldb-4.4 $DBLIBS"
	],[AC_MSG_ERROR(["no libdb-4.4 found"])],[$DBLIBS])
;;
4.3)
	AC_CHECK_LIB(db-4.3,db_create,[dnl
			AC_DEFINE_UNQUOTED(AS_TR_CPP(LIBDB_VERSION),43)
			DBLIBS="-ldb-4.3 $DBLIBS"
	],[AC_MSG_ERROR(["no libdb-4.3 found"])],[$DBLIBS])
;;
3)
	AC_CHECK_LIB(db3,db_create,[dnl
			AC_DEFINE_UNQUOTED(AS_TR_CPP(LIBDB_VERSION),3)
			DBLIBS="-ldb3 $DBLIBS"
	],[AC_MSG_ERROR(["no libdb3 found"])],[$DBLIBS])
;;
*)
	AC_MSG_ERROR([Internal error: DB_WANT_VERSION=$DB_WANT_VERSION])
esac
AC_SUBST([DBLIBS])
AC_SUBST([DBCPPFLAGS])

AC_CHECK_LIB(z,gzopen,,[AC_MSG_ERROR(["no zlib found"])],)
AC_CHECK_LIB(gpg-error,gpg_strsource,,[AC_MSG_ERROR(["no libgpg-error found"])],)
AC_CHECK_LIB(gpgme,gpgme_get_protocol_name,,[AC_MSG_ERROR(["no libgpgme found (need at least 0.4.1)"])],)
AC_CHECK_HEADER(gpgme.h,,[AC_MSG_ERROR(["no gpgme.h found"])])

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

AC_OUTPUT([Makefile docs/Makefile tests/Makefile])


