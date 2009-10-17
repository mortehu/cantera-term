bin_PROGRAMS = cantera-wm
man1_MANS = doc/cantera-wm.1
include_HEADERS = cantera-config.h

CONVERT = @CONVERT@

AM_CFLAGS = -Wall -g -pthread -std=c99
AM_CPPFLAGS = -I$(srcdir)/../common -I/usr/include/freetype2 -I/usr/include/libxml2 -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE

cantera_wm_SOURCES = font.c font.h globals.h main.c menu.c menu.h utf8.c
cantera_wm_LDFLAGS = -L/usr/X11R6/lib -lX11 -lfreetype -lXrender -lpng -lutil -lm -lXdamage -lXinerama -lXpm -ljpeg -lgif -rdynamic
cantera_wm_LDADD = ../common/libcommon.a

EXTRA_DIST = cantera-wm.desktop doc/cantera-wm.1

install-data-local:
	install -m 0755 -d $(DESTDIR)$(datadir)/xsessions
	install -m 0644 $(srcdir)/cantera-wm.desktop $(DESTDIR)$(datadir)/xsessions/cantera-wm.desktop
