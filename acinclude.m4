# Check for an enum, which seem to be forgotten in autoconf,
# as this can neighter be checked with cpp, nor is it a symbol
m4_define([CHECK_ENUM],
[AS_VAR_PUSHDEF([check_Enum], [cv_check_enum_$1])dnl
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
])# 
