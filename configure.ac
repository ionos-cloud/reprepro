dnl
dnl Process this file with autoconf to produce a configure script
dnl

AC_INIT(main.c)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(reprepro,0.2precvs)

AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_INSTALL

AC_CHECK_LIB(z,gzopen,,[AC_MSG_ERROR(["no zlib found"])],)
AC_CHECK_LIB(db3,db_create,,[AC_MSG_ERROR(["no libdb3 found"])],)
AC_CHECK_LIB(gpgme,gpgme_check_version,,[AC_MSG_ERROR(["no libgpgme found"])],)
AC_CHECK_HEADER(gpgme.h,,[AC_MSG_ERROR(["no gpgme.h found"])])
CHECK_ENUM(GPGME_SIG_STAT_GOOD_EXP,[gpgme.h],[CPPFLAGS="$CPPFLAGS -DHASGPGMEGOODEXP"],[],)

AC_ARG_ENABLE(debug, [  --enable-debug          Activate some switches, that make debugging easier],
[
        debug=yes
        CPPFLAGS="$CPPFLAGS -Wall -g"
],
[debug=no])

AM_CONDITIONAL(DEBUG, test "x$debug" = "xyes")

dnl
dnl Create makefiles
dnl

AC_OUTPUT([Makefile docs/Makefile tests/Makefile])


