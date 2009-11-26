#include <math.h>
#include <wchar.h>

#include <X11/extensions/Xrender.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "common.h"
#include "globals.h"
#include "font.h"

#ifndef TERMINAL
#define SIZE_COUNT 2
#else
#define SIZE_COUNT 1
#endif

FT_Library ft_library;
FT_Face ft_faces[SIZE_COUNT][1];

extern const char* font_name;

int ft_facecount = 0;

unsigned char loaded_glyphs[SIZE_COUNT][65536 / CHAR_BIT];

#define hasglyph(size,n) (((n) < 65536) && (loaded_glyphs[size][(n) >> 3] & (1 << (n & 7))))

void loadglyph(int size, unsigned int n)
{
  int i, x, y;
  int stride;
  int result;
  FT_UInt idx;
  Glyph glyph;
  XGlyphInfo glyphinfo;
  unsigned char alpha_image[1024];
  FT_GlyphSlot slot = 0;

  if(n >= 65536)
    return;

  loaded_glyphs[size][n >> 3] |= 1 << (n & 7);

  for(i = 0; i < ft_facecount; ++i)
  {
    idx = FT_Get_Char_Index(ft_faces[size][i], n);

    if(!idx)
      continue;

    result = FT_Load_Glyph(ft_faces[size][i], idx, FT_LOAD_RENDER);

    if(result)
      continue;

    slot = ft_faces[size][i]->glyph;

    break;
  }

  glyph = (Glyph) n;

  if(i == ft_facecount)
  {
    /* Glyph not found */

    glyphinfo.width = xskips[size] - 4;
    glyphinfo.height = yskips[size] - 4;
    glyphinfo.x = -(xskips[size] / 2 - glyphinfo.width / 2);
    glyphinfo.y = -(yskips[size] / 2 - glyphinfo.height / 2);
    glyphinfo.xOff = xskips[size];
    glyphinfo.yOff = 0;

    stride = (glyphinfo.width + 3) & ~3;

    for(y = 0; y < glyphinfo.height; ++y)
    {
      for(x = 0; x < glyphinfo.width; ++x)
      {
        if(y == 0 || x == 0 || y == glyphinfo.height - 1 || x == glyphinfo.width - 1)
          alpha_image[y * stride + x] = 255;
        else
          alpha_image[y * stride + x] = 0;
      }
    }
  }
  else
  {
    glyphinfo.width = slot->bitmap.width;
    glyphinfo.height = slot->bitmap.rows;
    glyphinfo.x = -slot->metrics.horiBearingX >> 6;
    glyphinfo.y = (slot->metrics.horiBearingY >> 6) - font_sizes[size] - (ft_faces[size][i]->descender >> 8) - 1;
    glyphinfo.xOff = slot->advance.x >> 6;

    int skip = xskips[size];

    if(wcwidth(n) < 2)
      glyphinfo.xOff = xskips[size];
    else
    {
      skip = 2 * xskips[size];
      glyphinfo.xOff = skip;
    }

    glyphinfo.yOff = 0;

    stride = (glyphinfo.width + 3) & ~3;

    for(y = 0; y < glyphinfo.height; ++y)
    {
      int effy = -glyphinfo.y + y;

      for(x = 0; x < glyphinfo.width; ++x)
      {
        int effx = -glyphinfo.x + x < skip;

        if(effx >= 0 && effx < skip && effy >= 0 && effy < yskips[size])
          alpha_image[y * stride + x] = slot->bitmap.buffer[y * glyphinfo.width + x];
        else
          alpha_image[y * stride + x] = 0;
      }
    }
  }

  XRenderAddGlyphs(display, alpha_glyphs[size], &glyph, &glyphinfo, 1,
                   (const char*) alpha_image, stride * glyphinfo.height);
}

void drawtext(Picture dest, const wchar_t* text, size_t length, int x, int y, int color, int size)
{
  size_t i;

  for(i = 0; i < length; ++i)
  {
    if(text[i] < 0 || text[i] >= 65536)
      continue;

    if(!hasglyph(size, text[i]))
      loadglyph(size, text[i]);
  }

  if(sizeof(wchar_t) == sizeof(unsigned short))
  {
    XRenderCompositeString16(display, PictOpOver, picpalette[color], dest, a8pictformat, alpha_glyphs[size], 0, 0, x, y, (const unsigned short*) text, length);
  }
  else if(sizeof(wchar_t) == sizeof(unsigned int))
  {
    XRenderCompositeString32(display, PictOpOver, picpalette[color], dest, a8pictformat, alpha_glyphs[size], 0, 0, x, y, (const unsigned int*) text, length);
  }
  else
    abort();
}

void font_init()
{
  int result;
  int i, j;

  result = FT_Init_FreeType(&ft_library);

  if(result)
  {
    fprintf(stderr, "Error initializing FreeType: %d\n", result);

    exit(EXIT_FAILURE);
  }

  /*
  for(i = 0; i < sizeof(font_names) / sizeof(font_names[0]); ++i)
  */
  {
    for(j = 0; j < SIZE_COUNT; ++j)
    {
      result = FT_New_Face(ft_library, font_name, 0, &ft_faces[j][ft_facecount]);

      if(result == FT_Err_Unknown_File_Format)
      {
        fprintf(stderr, "Error opening font '%s': Unsupported file format.\n",
                font_name);

        break;
      }

      if(result)
      {
        fprintf(stderr, "Error opening font '%s': %d\n", font_name, result);

        break;
      }

      result = FT_Set_Pixel_Sizes(ft_faces[j][ft_facecount], 0, font_sizes[j]);

      if(result)
      {
        fprintf(stderr, "Error setting size (%d pixels): %d\n", font_sizes[j], result);

        break;
      }

      if(ft_facecount == 0)
      {
        xskips[j] = round(ft_faces[j][ft_facecount]->size->metrics.max_advance / 64.0);
        yskips[j] = round(ft_faces[j][ft_facecount]->size->metrics.height / 64.0);
      }
    }

    if(j == SIZE_COUNT)
      ++ft_facecount;
  }

  if(!ft_facecount)
  {
    fprintf(stderr, "No font faces found.\n");

    exit(EXIT_FAILURE);
  }
}

void font_free()
{
  int i, j;

  memset(loaded_glyphs, 0, sizeof(loaded_glyphs));

  for(i = 0; i < ft_facecount; ++i)
  {
    for(j = 0; j < SIZE_COUNT; ++j)
    {
      FT_Done_Face(ft_faces[j][i]);
    }
  }

  ft_facecount = 0;

  FT_Done_FreeType(ft_library);
}
