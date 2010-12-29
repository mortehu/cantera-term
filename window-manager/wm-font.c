#include <math.h>
#include <wchar.h>

#include <X11/extensions/Xrender.h>
#include <X11/Xft/Xft.h>

#include "common.h"
#include "globals.h"
#include "font.h"

#define SIZE_COUNT 2

char const* font_name = "Andale Mono";

static XftDraw *fontdraw;
static XftFont *font_faces[SIZE_COUNT];

void drawtext(Picture dest, const wchar_t* text, size_t length, int x, int y, int color, int size)
{
  y += font_faces[size]->ascent;

  if(sizeof(wchar_t) == sizeof(unsigned short))
  {
    XftTextRender16 (display, PictOpOver, picpalette[color], font_faces[size],
                     dest, 0, 0, x, y, (const unsigned short *) text, length);
  }
  else if(sizeof(wchar_t) == sizeof(unsigned int))
  {
    XftTextRender32 (display, PictOpOver, picpalette[color], font_faces[size],
                     dest, 0, 0, x, y, (const unsigned int *) text, length);
  }
  else
    abort();
}

static XftFont *
font_load(const char* name, unsigned int size, int flags)
{
  char* xft_font_desc;

  xft_font_desc = alloca(strlen(name) + 64);

  sprintf(xft_font_desc, "%s:pixelsize=%d", name, size);

  if(flags & FONT_BOLD)
    strcat(xft_font_desc, ":bold");

  return XftFontOpenName(display, 0, xft_font_desc);
}

void font_init(Drawable drawable, Visual *visual, Colormap colormap)
{
  FT_UInt init_glyphs[256];
  int i;

  for (i = 0; i < sizeof(init_glyphs) / sizeof(init_glyphs[0]); ++i)
    init_glyphs[i] = i;

  fontdraw = XftDrawCreate(display, drawable, visual, colormap);

  for (i = 0; i < SIZE_COUNT; ++i)
    {
      font_faces[i] = font_load (font_name, font_sizes[i], 0);

      if (!font_faces[i])
        {
          fprintf(stderr, "Error loading font '%s' size %d\n", font_name, font_sizes[i]);

          exit(EXIT_FAILURE);
        }

      xskips[i] = ceil(font_faces[i]->max_advance_width);
      yskips[i] = round(font_faces[i]->height);

      XftFontLoadGlyphs (display, font_faces[i], 0, init_glyphs, sizeof(init_glyphs) / sizeof(init_glyphs[0]));
    }
}

void font_free()
{
  int i;

  for (i = 0; i < SIZE_COUNT; ++i)
    XftFontClose (display, font_faces[i]);

  XftDrawDestroy (fontdraw);
}
