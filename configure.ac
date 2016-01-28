AC_INIT(cantera-term,31)

AC_CANONICAL_TARGET
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

AM_MAINTAINER_MODE([enable])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_MACRO_DIR([m4])

AM_PROG_AR
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LEX
AC_PROG_MAKE_SET
AC_PROG_YACC

LT_INIT

PKG_CHECK_MODULES([PACKAGES],
                  [fontconfig >= 2.8.0
                   freetype2 >= 9.20
                   glew
                   x11])

AC_SUBST(PACKAGES_CFLAGS)
AC_SUBST(PACKAGES_LIBS)

AC_LANG_PUSH([C++])
AX_CXX_COMPILE_STDCXX_14([noext])
AC_LANG_POP([C++])

AC_CONFIG_HEADERS([config.h])

AC_OUTPUT([Makefile])
