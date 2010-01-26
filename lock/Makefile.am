bin_PROGRAMS = cantera-lock
pkgdata_DATA = gfx/fonts/vera-sans-mono.dat gfx/fonts/vera-sans-mono_0.png

AM_CFLAGS = -g -std=c99 -Wall -D_GNU_SOURCE -pthread
AM_CPPFLAGS = -I/usr/include/freetype2 -DPKGDATADIR=\"$(pkgdatadir)\"

cantera_lock_SOURCES = \
  draw.h draw.c \
  error.h error.c \
  font.h font.c \
  hashmap.h hashmap.c \
  util.h util.c \
  texture.h texture.c \
  vector.h vector.c \
  var.h var.c \
  input.h main_x11.c \
  lock.c

EXTRA_DIST = gfx/fonts/vera-sans-mono.dat gfx/fonts/vera-sans-mono_0.png

cantera_lock_LDADD = -lX11 -lGL -lGLU -lm -lXxf86vm -lpng -lXinerama -lcrypt

install-exec-hook:
	chown root $(bindir)/cantera-lock
	chmod u+s $(bindir)/cantera-lock
