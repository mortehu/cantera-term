bin_PROGRAMS = cantera-term
man1_MANS = doc/cantera-term.1

CONVERT = @CONVERT@

AM_CFLAGS = -g -Wall -pthread $(PACKAGES_CFLAGS)
AM_CPPFLAGS = -I$(srcdir)/common -I/usr/include/freetype2 -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE -D_REENTRANT -D_FILE_OFFSET_BITS=64

cantera_term_SOURCES = term.c globals.h tree.c tree.h arena.c arena.h io.c io.h x11.c x11.h font.c font.h glyph.c glyph.h draw-gl-12.c
cantera_term_LDFLAGS = -L/usr/X11R6/lib -lX11 -lutil -lm -lGL $(PACKAGES_LIBS)

EXTRA_DIST = doc/cantera-term.1

install-data-local:
	install -m 0755 -d $(DESTDIR)$(datadir)/applications
