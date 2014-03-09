AC_INIT(cantera-term,30)

AC_CANONICAL_TARGET
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_MACRO_DIR([m4])

AM_PROG_AR
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LEX
AC_PROG_MAKE_SET
AC_PROG_YACC

AC_DEFINE(USE_BASH, [], [Build bash plugin])
AC_CHECK_HEADER([bash/config.h],
                [AC_DEFINE(USE_BASH, 1)],
                [AC_DEFINE(USE_BASH, 0)])
AM_CONDITIONAL(USE_BASH, [test "x$ac_cv_header_bash_config_h" = xyes])

LT_INIT

PKG_CHECK_MODULES([PACKAGES], [x11 freetype2 >= 9.20 fontconfig >= 2.8.0 libpng >= 1.2 glew])

AC_SUBST(PACKAGES_CFLAGS)
AC_SUBST(PACKAGES_LIBS)

AC_LANG_PUSH([C++])
AX_CXX_COMPILE_STDCXX_11([noext])
AC_LANG_POP([C++])

AC_CONFIG_HEADERS([config.h])

AC_OUTPUT([Makefile])
