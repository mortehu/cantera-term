#include <stdio.h>
#include <string.h>

#include "glyph.h"

static GLuint glyph_texture;

struct glyph_Data
{
  uint16_t width, height;
  int16_t  x, y;
  int16_t  xOffset, yOffset;
  int16_t  u, v;
};

static struct glyph_Data glyphs[65536]; /* 1 MB */
static uint32_t loadedGlyphs[65536 / 32];
static unsigned int top[GLYPH_ATLAS_SIZE];

void
GLYPH_Init (void)
{
  glGenTextures (1,  &glyph_texture);
  glBindTexture (GL_TEXTURE_2D, glyph_texture);

  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
}

GLuint
GLYPH_Texture (void)
{
  return glyph_texture;
}

void
GLYPH_Add (unsigned int code, struct FONT_Glyph *glyph)
{
  if (code >= sizeof (glyphs) / sizeof (glyphs[0]))
    return;

  loadedGlyphs[code >> 5] |= (1 << (code & 31));

  if (glyph->width && glyph->height)
    {
      unsigned int best_u, best_v, u, k, v_max;

      best_u = GLYPH_ATLAS_SIZE;
      best_v = GLYPH_ATLAS_SIZE;

      for(u = 0; u < GLYPH_ATLAS_SIZE - glyph->width + 1; ++u)
        {
          v_max = top[u];

          for(k = 1; k < glyph->width && v_max < best_v; ++k)
            {
              if (top[u + k] > v_max)
                v_max = top[u + k];
            }

          if (v_max < best_v)
            {
              best_v = v_max;
              best_u = u;
            }
        }

      if (best_u == GLYPH_ATLAS_SIZE)
        {
          fprintf (stderr, "No room for glyph of size %ux%u\n", glyph->width, glyph->height);

          return;
        }

      glyphs[code].u = best_u;
      glyphs[code].v = best_v;

      glBindTexture (GL_TEXTURE_2D, glyph_texture);
      glTexSubImage2D (GL_TEXTURE_2D, 0, best_u, best_v, glyph->width, glyph->height,
                       GL_RGBA, GL_UNSIGNED_BYTE, glyph->data);

      for (k = 0; k < glyph->width; ++k)
        top[best_u + k] = best_v + glyph->height;
    }

  glyphs[code].width = glyph->width;
  glyphs[code].height = glyph->height;
  glyphs[code].x = glyph->x;
  glyphs[code].y = glyph->y;
  glyphs[code].xOffset = glyph->xOffset;
  glyphs[code].yOffset = glyph->yOffset;
}

int
GLYPH_IsLoaded (unsigned int code)
{
  if (code >= sizeof (glyphs) / sizeof (glyphs[0]))
    return 1;

  return (loadedGlyphs[code >> 5] & (1 << (code & 31)));
}

void
GLYPH_Get (unsigned int code, struct FONT_Glyph *glyph,
           uint16_t *u, uint16_t *v)
{
  if (code >= sizeof (glyphs) / sizeof (glyphs[0]))
    {
      memset (glyph, 0, sizeof (*glyph));
      *u = 0.0f;
      *v = 0.0f;

      return;
    }

  glyph->width = glyphs[code].width;
  glyph->height = glyphs[code].height;
  glyph->x = glyphs[code].x;
  glyph->y = glyphs[code].y;
  glyph->xOffset = glyphs[code].xOffset;
  glyph->yOffset = glyphs[code].yOffset;

  *u = glyphs[code].u;
  *v = glyphs[code].v;
}
