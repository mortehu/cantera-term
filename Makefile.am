bin_PROGRAMS = cantera-term
lib_LTLIBRARIES = libcantera-bash.la
noinst_LTLIBRARIES = libcommon.la libexpression.la
check_PROGRAMS = expression-test fuzz-test
man1_MANS = doc/cantera-term.1

ACLOCAL_AMFLAGS = -I m4

CONVERT = @CONVERT@

BUILT_SOURCES = expression-parser.hh

AM_CFLAGS = -g -Wall -pthread -fPIC $(PACKAGES_CFLAGS)
AM_CXXFLAGS = $(AM_CFLAGS)
AM_CPPFLAGS = -I/usr/include/freetype2
AM_YFLAGS = --defines=expression-parser.hh

cantera_term_SOURCES = \
  draw-gl-30.cc \
  base/file.cc \
  base/file.h \
  bash.cc \
  bash.h \
  command.cc \
  command.h \
  font.c \
  font.h \
  glyph.c \
  glyph.h \
  main.cc \
  terminal.cc \
  terminal.h \
  tree.cc \
  tree.h \
  x11.c \
  x11.h
cantera_term_LDADD = $(PACKAGES_LIBS) -lutil -lm -lGL libexpression.la libcommon.la

libexpression_la_SOURCES = \
  expression-lexer.ll \
  expression-parser.yy \
  expr-parse.cc \
  expr-parse.h \
  expr.cc \
  expr.h
libexpression_la_LDFLAGS = -lmpfr

libcantera_bash_la_SOURCES = bash-builtin.cc
libcantera_bash_la_CPPFLAGS = -I/usr/include/bash
libcantera_bash_la_LIBADD = libcommon.la
libcantera_bash_la_LDFLAGS = -lpthread

libcommon_la_SOURCES = \
  base/string.cc \
  base/string.h \
  message.cc \
  message.h

expression_test_SOURCES = expression-test.cc
expression_test_LDADD = libexpression.la libcommon.la

fuzz_test_SOURCES = fuzz-test.cc terminal.h terminal.cc

TESTS = expression-test fuzz-test

EXTRA_DIST = doc/cantera-term.1

install-data-local:
	$(INSTALL) -m 0755 -d $(DESTDIR)$(datadir)/applications
	$(INSTALL_DATA) $(srcdir)/cantera-term.desktop $(DESTDIR)$(datadir)/applications/cantera-term.desktop
