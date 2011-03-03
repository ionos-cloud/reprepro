dnl CHECK_ENUM and GET_DEFINE autoconf macros are
dnl Copyright 2004,2006 Bernhard R. Link
dnl and hereby in the public domain
# Check for an enum, which seem to be forgotten in autoconf,
# as this can neighter be checked with cpp, nor is it a symbol
m4_define([CHECK_ENUM],
[AS_VAR_PUSHDEF([check_Enum], [rr_cv_check_enum_$1])dnl
AC_CACHE_CHECK([for $1 in $2], check_Enum,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT([$5])
@%:@include <$2>],
[ if( $1 == 0 ) 
	return 0;
])],
		[AS_VAR_SET(check_Enum, yes)],
		[AS_VAR_SET(check_Enum, no)])])
AS_IF([test AS_VAR_GET(check_Enum) = yes], [$3], [$4])[]dnl
AS_VAR_POPDEF([check_Enum])dnl
])dnl
# extract the value of a #define from a header
m4_define([GET_DEFINE],
[AC_LANG_PREPROC_REQUIRE()dnl
AS_VAR_PUSHDEF(get_Define, [rr_cv_get_define_$1])dnl
AC_CACHE_CHECK([for $1], get_Define,
[dnl
	m4_ifvaln([$2],[dnl
		echo "#include <$2>" > conftest.$ac_ext
		echo "$1" >> conftest.$ac_ext
	],[dnl
		echo "$1" > conftest.$ac_ext
	])
	if _AC_EVAL_STDERR([$ac_cpp conftest.$ac_ext >conftest.out]) >/dev/null; then
		if test -s conftest.err; then
			AS_VAR_SET(get_Define, $1)
		else
			AS_VAR_SET(get_Define, "$(tail -1 conftest.out)")
		fi
	else
		AS_VAR_SET(get_Define, $1)
	fi
	rm -f conftest.err conftest.out conftest.$ac_ext
])
TMP_GET_DEFINE=AS_VAR_GET(get_Define)
TMP_GET_DEFINE=${TMP_GET_DEFINE% }
TMP_GET_DEFINE=${TMP_GET_DEFINE% }
AS_IF([test "$TMP_GET_DEFINE" = $1], [$3], [$1="$TMP_GET_DEFINE"])[]dnl
AS_VAR_POPDEF([get_Define])dnl
])dnl GET_DEFINE
