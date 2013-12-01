bin_PROGRAMS = cantera-term
check_PROGRAMS = fuzz
man1_MANS = doc/cantera-term.1

ACLOCAL_AMFLAGS = -I m4

CONVERT = @CONVERT@

AM_CFLAGS = -g -Wall -pthread $(PACKAGES_CFLAGS)
AM_CXXFLAGS = $(AM_CFLAGS)
AM_CPPFLAGS = -I/usr/include/freetype2

cantera_term_SOURCES = main.cc terminal.h terminal.cc tree.cc tree.h x11.c x11.h font.c font.h glyph.c glyph.h draw-gl-30.cc
cantera_term_LDADD = $(PACKAGES_LIBS) -lutil -lm -lGL

fuzz_SOURCES = fuzz.cc terminal.h terminal.cc

TESTS = fuzz

EXTRA_DIST = doc/cantera-term.1

install-data-local:
	$(INSTALL) -m 0755 -d $(DESTDIR)$(datadir)/applications
	$(INSTALL_DATA) $(srcdir)/cantera-term.desktop $(DESTDIR)$(datadir)/applications/cantera-term.desktop
