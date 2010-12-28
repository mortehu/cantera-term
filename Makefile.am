noinst_LIBRARIES = libcommon.a
bin_PROGRAMS = cantera-term cantera-wm cantera-lock
man1_MANS = terminal/doc/cantera-term.1 window-manager/doc/cantera-wm.1
pkgdata_DATA = lock/gfx/fonts/vera-sans-mono.dat lock/gfx/fonts/vera-sans-mono_0.png

CONVERT = @CONVERT@

XLDFLAGS =  -L/usr/X11R6/lib -lX11 -lXrender -lXft -lXpm -lpng -ljpeg -lgif
AM_CFLAGS = -g -Wall -pthread
AM_CPPFLAGS = -I$(srcdir)/common -I/usr/include/freetype2 -I/usr/include/libxml2 -DBINDIR=\"$(bindir)\" -DPKGDATADIR=\"$(pkgdatadir)\" -D_BSD_SOURCE -D_GNU_SOURCE -D_XOPEN_SOURCE -D_REENTRANT -D_FILE_OFFSET_BITS=64

libcommon_a_SOURCES = common/image.c common/file.c common/common.h common/tree.c common/tree.h common/arena.c common/arena.h common/io.c common/io.h

cantera_term_SOURCES = terminal/font.h terminal/term.c terminal/font.c terminal/globals.h
cantera_term_LDFLAGS = $(XLDFLAGS) -lutil -lm
cantera_term_LDADD = libcommon.a

cantera_wm_SOURCES = \
  window-manager/font.h \
  window-manager/globals.h \
  window-manager/main.c \
  window-manager/menu.c \
  window-manager/menu.h \
  window-manager/utf8.c \
  window-manager/wm-font.c \
  window-manager/wm-window-type.c \
  window-manager/wm-window-type.h
cantera_wm_LDFLAGS = -L/usr/X11R6/lib $(XLDFLAGS) -lutil -lm -lXinerama -rdynamic -lXfixes -ldl
cantera_wm_LDADD = libcommon.a

cantera_lock_SOURCES = \
  lock/draw.h lock/draw.c \
  lock/error.h lock/error.c \
  lock/font.h lock/lock-font.c \
  lock/hashmap.h lock/hashmap.c \
  lock/util.h lock/util.c \
  lock/texture.h lock/texture.c \
  lock/vector.h lock/vector.c \
  lock/var.h lock/var.c \
  lock/input.h lock/main_x11.c \
  lock/lock.c
cantera_lock_LDADD = -lX11 -lGL -lGLU -lm -lXxf86vm -lpng -lXinerama -lcrypt

EXTRA_DIST = terminal/cantera-term.desktop terminal/doc/cantera-term.1 window-manager/cantera-wm.desktop window-manager/doc/cantera-wm.1 cantera-lock/gfx/fonts/vera-sans-mono.dat cantera-lock/gfx/fonts/vera-sans-mono_0.png

install-data-local:
	install -m 0755 -d $(DESTDIR)$(datadir)/applications
	install -m 0644 $(srcdir)/terminal/cantera-term.desktop $(DESTDIR)$(datadir)/applications/cantera-term.desktop
	install -m 0755 -d $(DESTDIR)$(datadir)/xsessions
	install -m 0644 $(srcdir)/window-manager/cantera-wm.desktop $(DESTDIR)$(datadir)/xsessions/cantera-wm.desktop

