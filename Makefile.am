bin_PROGRAMS = cantera-term
lib_LTLIBRARIES =
noinst_LTLIBRARIES = libcommon.la libexpression.la
check_PROGRAMS = expression-test fuzz-test
man1_MANS = doc/cantera-term.1

# Required for Bison to work correctly
AUTOMAKE_OPTIONS = 1.14

ACLOCAL_AMFLAGS = -I m4

CONVERT = @CONVERT@

BUILT_SOURCES = expression-parser.hh location.hh position.hh stack.hh

AM_CFLAGS = -g -Wall -pthread -fPIC $(PACKAGES_CFLAGS)
AM_CXXFLAGS = $(AM_CFLAGS)
AM_CPPFLAGS = -I/usr/include/freetype2
AM_YFLAGS = --defines=expression-parser.hh

cantera_term_SOURCES = \
  $(BUILT_SOURCES) \
  3rd_party/mpfrc++/mpreal.h \
  base/file.cc \
  base/file.h \
  command.cc \
  command.h \
  completion.cc \
  draw-gl-30.cc \
  draw.h \
  font.c \
  font.h \
  glyph.c \
  glyph.h \
  main.cc \
  opengl.h \
  terminal.cc \
  terminal.h \
  tree.cc \
  tree.h \
  x11.c \
  x11.h
cantera_term_LDADD = $(PACKAGES_LIBS) -lutil -lm -lGL libexpression.la libcommon.la
cantera_term_LDFLAGS = -z relro

libexpression_la_SOURCES = \
  expression-lexer.ll \
  expression-parser.yy \
  expr-parse.cc \
  expr-parse.h \
  expr.cc \
  expr.h
libexpression_la_LDFLAGS = -lmpfr

libcommon_la_SOURCES = \
  base/string.cc \
  base/string.h \
  message.cc \
  message.h

expression_test_SOURCES = expression-test.cc
expression_test_LDADD = libexpression.la libcommon.la

fuzz_test_SOURCES = fuzz-test.cc terminal.h terminal.cc

TESTS = expression-test fuzz-test lint-debian-package.sh

EXTRA_DIST = doc/cantera-term.1 cantera-term.desktop

expression-parser.hh: expression-parser.cc

install-data-local:
	$(INSTALL) -m 0755 -d $(DESTDIR)$(datadir)/applications
	$(INSTALL_DATA) $(srcdir)/cantera-term.desktop $(DESTDIR)$(datadir)/applications/cantera-term.desktop

# Test target name assumed by Travis CI
test: check
