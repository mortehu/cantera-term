bin_PROGRAMS = cantera-wm
man1_MANS = doc/cantera-wm.1

CONVERT = @CONVERT@

AM_CFLAGS = -Wall -g -pthread -std=c99
AM_CPPFLAGS = -I/usr/include/freetype2 -I/usr/include/libxml2 -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE

XLDFLAGS =  -L/usr/X11R6/lib -lX11 -lfreetype -lXrender -lpng

cantera_wm_SOURCES = font.c font.h globals.h main.c menu.c common.c common.h menu.h utf8.c
cantera_wm_LDFLAGS = $(XLDFLAGS) -lutil -lm -lXdamage -lXinerama -lXpm

EXTRA_DIST = cantera-wm.desktop

install-data-local:
	install -m 0755 -d $(DESTDIR)$(datadir)/xsessions
	install -m 0644 potty.desktop $(DESTDIR)$(datadir)/xsessions/potty.desktop
