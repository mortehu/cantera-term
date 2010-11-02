/**
 * Font drawing functions
 * Copyright (C) 2008,2009  Morten Hustveit
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "error.h"
#include "font.h"
#include "texture.h"

#define MAX_FONT_COUNT 16
#define MAX_FONT_TEXTURE_COUNT 64

struct FONT_glyph_info
{
  int character;
  int size;
  int bitmapidx;
  int top;
  int left;
  int xskip;
  int image_width;
  int image_height;
  float s1;
  float t1;
  float s2;
  float t2;
};

struct FONT_font
{
  struct FONT_glyph_info* glyphs;
  int glyph_count;
};

static struct FONT_font FONT_fonts[MAX_FONT_COUNT];
static int FONT_font_count;

#if !WORDS_BIGENDIAN
static void
swap32(void* p)
{
  unsigned int* u = p;

  *u = (*u >> 24)
    | ((*u & 0xff0000) >> 8)
    | ((*u & 0x00ff00) << 8)
    | (*u << 24);
}
#endif

int
font_load(const char* name)
{
  char fullpath[4096];
  FILE* f;
  int i, file_size, result;

  strcpy(fullpath, PKGDATADIR);
  strcat(fullpath, "/");
  strcat(fullpath, name);
  strcat(fullpath, ".dat");

  f = fopen(fullpath, "rb");

  if(!f)
    fatal_error("Failed to open font '%s' for reading: %s", fullpath, strerror(errno));

  fseek(f, 0, SEEK_END);
  file_size = ftell(f);
  rewind(f);

  if(!file_size || (file_size % sizeof(struct FONT_glyph_info)))
    fatal_error("Error in '%s': expected to be multiple of %d bytes, was %d",
                sizeof(struct FONT_glyph_info), (int) file_size);

  result = FONT_font_count++;
  FONT_fonts[result].glyph_count = file_size / sizeof(struct FONT_glyph_info);
  FONT_fonts[result].glyphs = malloc(file_size);

  if(file_size != fread(FONT_fonts[result].glyphs, 1, file_size, f))
    fatal_error("Failed read of size %d from '%s'", (int) file_size, fullpath);

  fclose(f);

  for(i = 0; i < FONT_fonts[result].glyph_count; ++i)
    {
#if !WORDS_BIGENDIAN
      swap32(&FONT_fonts[result].glyphs[i].character);
      swap32(&FONT_fonts[result].glyphs[i].size);
      swap32(&FONT_fonts[result].glyphs[i].bitmapidx);
      swap32(&FONT_fonts[result].glyphs[i].top);
      swap32(&FONT_fonts[result].glyphs[i].left);
      swap32(&FONT_fonts[result].glyphs[i].xskip);
      swap32(&FONT_fonts[result].glyphs[i].image_width);
      swap32(&FONT_fonts[result].glyphs[i].image_height);
      swap32(&FONT_fonts[result].glyphs[i].s1);
      swap32(&FONT_fonts[result].glyphs[i].t1);
      swap32(&FONT_fonts[result].glyphs[i].s2);
      swap32(&FONT_fonts[result].glyphs[i].t2);
#endif

      sprintf(fullpath, PKGDATADIR "/%s_%d.png", name, FONT_fonts[result].glyphs[i].bitmapidx);

      FONT_fonts[result].glyphs[i].bitmapidx = texture_load(fullpath);
    }

  return result;
}

static int
FONT_glyph(int font, int size, int ch)
{
  int first, last, len, half, middle;

  assert(font < FONT_font_count);

  first = 0;
  len = last = FONT_fonts[font].glyph_count;


  while(len > 0)
    {
      half = len >> 1;
      middle = first + half;

      if(FONT_fonts[font].glyphs[middle].size < size
         || (FONT_fonts[font].glyphs[middle].size == size && FONT_fonts[font].glyphs[middle].character < ch))
        {
          first = middle + 1;
          len = len - half - 1;
        }
      else
        len = half;
    }

  if(FONT_fonts[font].glyphs[first].size != size
     || FONT_fonts[font].glyphs[first].character != ch)
    {
      if(isprint(ch))
        fatal_error("Character %c (%d) of size %d not found in font %d", ch, ch, size, font);
      else
        fatal_error("Character %d of size %d not found in font %d", ch, size,font);
    }

  return first;
}

int
font_string_width(int font, int size, const char* string)
{
  int result = 0;

  assert(font < FONT_font_count);

  while(*string)
    {
      result += FONT_fonts[font].glyphs[FONT_glyph(font, size, *string)].xskip;
      ++string;
    }

  return result;
}

int
font_draw(int font, int size, const char* string, float x, float y, int alignment)
{
  const struct FONT_glyph_info* glyph;
  int offset = 0;

  if(alignment > 0)
    {
      int width = font_string_width(font, size, string);

      offset -= alignment * width / 2;
    }

  while(*string)
    {
      glyph = &FONT_fonts[font].glyphs[FONT_glyph(font, size, *string)];

      draw_quad_st(glyph->bitmapidx,
                   x + offset + glyph->left, y - glyph->top,
                   glyph->image_width, glyph->image_height,
                   glyph->s1, glyph->t1,
                   glyph->s2, glyph->t2);

      offset += glyph->xskip;

      ++string;
    }

  return offset;
}
