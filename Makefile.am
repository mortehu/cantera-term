bin_PROGRAMS = cantera-term
man1_MANS = doc/cantera-term.1

CONVERT = @CONVERT@

AM_CFLAGS = -g -Wall -pthread $(DBUS_CFLAGS)
AM_CPPFLAGS = -I$(srcdir)/common -I/usr/include/freetype2 -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE -D_REENTRANT -D_FILE_OFFSET_BITS=64

cantera_term_SOURCES = font.h term.c font.c globals.h tree.c tree.h arena.c arena.h io.c io.h x11.c x11.h
cantera_term_LDFLAGS = -L/usr/X11R6/lib -lX11 -lfreetype -lXpm -lpng -ljpeg -lgif -lutil -lm -lGL

EXTRA_DIST = cantera-term.desktop doc/cantera-term.1

install-data-local:
	install -m 0755 -d $(DESTDIR)$(datadir)/applications
	install -m 0644 $(srcdir)/terminal/cantera-term.desktop $(DESTDIR)$(datadir)/applications/cantera-term.desktop
