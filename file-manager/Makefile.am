bin_PROGRAMS = cantera-fm

man1_MANS = doc/cantera-fm.1

AM_CFLAGS = -Wall -g -pthread -std=c99
AM_CPPFLAGS = -I../common -I/usr/include/freetype2 -I/usr/include/libxml2 -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE

XLDFLAGS =  -L/usr/X11R6/lib -lX11 -lfreetype -lXrender -lpng -ljpeg -lgif

cantera_fm_SOURCES = main.c
cantera_fm_LDFLAGS = $(XLDFLAGS) -lutil -lXpm
cantera_fm_LDADD = ../common/libcommon.a

EXTRA_DIST = cantera-fm.desktop

install-data-local:
	install -m 0755 -d $(DESTDIR)$(datadir)/applications
	install -m 0644 cantera-fm.desktop $(DESTDIR)$(datadir)/applications/cantera-fm.desktop
