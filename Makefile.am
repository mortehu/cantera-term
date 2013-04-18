bin_PROGRAMS = cantera-term
man1_MANS = doc/cantera-term.1

CONVERT = @CONVERT@

AM_CFLAGS = -g -Wall -pthread $(PACKAGES_CFLAGS)
AM_CPPFLAGS = -I/usr/include/freetype2 -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE -D_REENTRANT -D_FILE_OFFSET_BITS=64

cantera_term_SOURCES = term.c globals.h tree.c tree.h arena.c arena.h io.c io.h x11.c x11.h font.c font.h glyph.c glyph.h draw-gl-12.c
cantera_term_LDADD = $(PACKAGES_LIBS) -lutil -lm -lGL

EXTRA_DIST = doc/cantera-term.1

install-data-local:
	$(INSTALL) -m 0755 -d $(DESTDIR)$(datadir)/applications
	$(INSTALL_DATA) $(srcdir)/cantera-term.desktop $(DESTDIR)$(datadir)/applications/cantera-term.desktop
