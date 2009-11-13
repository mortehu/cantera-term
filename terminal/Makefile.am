bin_PROGRAMS = cantera-term

man1_MANS = doc/cantera-term.1

AM_CFLAGS = -Wall -g -pthread -std=c99
AM_CPPFLAGS = -I$(srcdir)/../common -I/usr/include/freetype2 -I/usr/include/libxml2 -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE

XLDFLAGS =  -L/usr/X11R6/lib -lX11 -lfreetype -lXrender -lpng -ljpeg -lgif

cantera_term_SOURCES = font.h term.c font.c globals.h
cantera_term_LDFLAGS = $(XLDFLAGS) -lutil -lXpm -lm
cantera_term_LDADD = ../common/libcommon.a

EXTRA_DIST = cantera-term.desktop doc/cantera-term.1

install-data-local:
	install -m 0755 -d $(DESTDIR)$(datadir)/applications
	install -m 0644 $(srcdir)/cantera-term.desktop $(DESTDIR)$(datadir)/applications/cantera-term.desktop
