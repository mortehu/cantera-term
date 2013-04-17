#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "font.h"
#include "glyph.h"
#include "opengl.h"
#include "terminal.h"
#include "x11.h"

struct draw_Color
{
  uint8_t r, g, b;
};

struct draw_SolidVertex
{
  int16_t x, y;
  struct draw_Color color;
};

static struct draw_SolidVertex *solidVertices;
static size_t solidVertexCount, solidVertexAlloc;

struct draw_TexturedVertex
{
  int16_t x, y;
  uint16_t u, v;
  struct draw_Color color;
};

static struct draw_TexturedVertex *texturedVertices;
static size_t texturedVertexCount, texturedVertexAlloc;

#define ARRAY_GROW_IF(array, count, alloc, increment)                    \
  do                                                                     \
    {                                                                    \
      if (count + increment > alloc)                                     \
        {                                                                \
          void *newArray;                                                \
          size_t newAlloc;                                               \
                                                                         \
          newAlloc = alloc * 3 / 2 + 4096 / sizeof (*array);             \
                                                                         \
          if (!(newArray = realloc (array, newAlloc * sizeof (*array)))) \
            goto failed;                                                 \
                                                                         \
          array = newArray;                                              \
          alloc = newAlloc;                                              \
        }                                                                \
    }                                                                    \
  while (0)

static void
draw_AddSolidQuad (unsigned int x, unsigned int y,
                   unsigned int width, unsigned int height,
                   uint32_t uint_color)
{
  unsigned int x2, y2;
  struct draw_Color color;

  if (uint_color == 0xff000000)
    return;

  color.r = uint_color >> 16;
  color.g = uint_color >> 8;
  color.b = uint_color;

  x2 = x + width;
  y2 = y + height;

  if (solidVertexCount >= 4
      && solidVertices[solidVertexCount - 1].color.r == color.r
      && solidVertices[solidVertexCount - 1].color.g == color.g
      && solidVertices[solidVertexCount - 1].color.b == color.b
      && solidVertices[solidVertexCount - 1].x == x
      && solidVertices[solidVertexCount - 1].y == y
      && solidVertices[solidVertexCount - 2].y == y2)
    {
      solidVertices[solidVertexCount - 1].x = x2;
      solidVertices[solidVertexCount - 2].x = x2;

      return;
    }

  ARRAY_GROW_IF (solidVertices, solidVertexCount, solidVertexAlloc, 4);

#define ADD_VERTEX(x_, y_, color_)                                       \
  do                                                                     \
    {                                                                    \
      solidVertices[solidVertexCount].x = x_;                            \
      solidVertices[solidVertexCount].y = y_;                            \
      solidVertices[solidVertexCount].color = color_;                    \
      ++solidVertexCount;                                                \
    }                                                                    \
  while (0)

  ADD_VERTEX (x,  y,  color);
  ADD_VERTEX (x,  y2, color);
  ADD_VERTEX (x2, y2, color);
  ADD_VERTEX (x2, y,  color);

#undef ADD_VERTEX

failed:

  ;
}

static void
draw_AddTexturedQuad (unsigned int x, unsigned int y,
                      unsigned int width, unsigned int height,
                      unsigned int u, unsigned int v,
                      uint32_t uint_color)
{
  struct draw_Color color;

  color.r = uint_color >> 16;
  color.g = uint_color >> 8;
  color.b = uint_color;

  ARRAY_GROW_IF (texturedVertices, texturedVertexCount,
                 texturedVertexAlloc, 4);

#define ADD_VERTEX(x_, y_, u_, v_, color_)                               \
  do                                                                     \
    {                                                                    \
      texturedVertices[texturedVertexCount].x = x_;                      \
      texturedVertices[texturedVertexCount].y = y_;                      \
      texturedVertices[texturedVertexCount].u = u_;                      \
      texturedVertices[texturedVertexCount].v = v_;                      \
      texturedVertices[texturedVertexCount].color = color_;              \
      ++texturedVertexCount;                                             \
    }                                                                    \
  while (0)

  ADD_VERTEX (x,         y,          u,         v,          color);
  ADD_VERTEX (x,         y + height, u,         v + height, color);
  ADD_VERTEX (x + width, y + height, u + width, v + height, color);
  ADD_VERTEX (x + width, y,          u + width, v,          color);

#undef ADD_VERTEX

failed:

  ;
}

void
draw_FlushQuads (void)
{
  static const float uvScale = 1.0 / GLYPH_ATLAS_SIZE;

  glBindTexture (GL_TEXTURE_2D, GLYPH_Texture ());
  glEnable (GL_BLEND);
  glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  if (solidVertexCount)
    {
      glColor3f (1.0f, 1.0f, 1.0f);
      glTexCoord2f (0.0f, 0.0f);

      glPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);

      glColorPointer (3, GL_UNSIGNED_BYTE, sizeof (*solidVertices), &solidVertices[0].color.r);
      glVertexPointer (2, GL_SHORT, sizeof (*solidVertices), &solidVertices[0].x);

      glEnableClientState (GL_COLOR_ARRAY);
      glEnableClientState (GL_VERTEX_ARRAY);

      glDrawArrays (GL_QUADS, 0, solidVertexCount);

      glPopClientAttrib ();
    }

  glMatrixMode (GL_TEXTURE);
  glPushMatrix ();
  glLoadIdentity ();
  glScalef (uvScale, uvScale, 1.0f);

  glPushClientAttrib (GL_CLIENT_VERTEX_ARRAY_BIT);

  glColorPointer (3, GL_UNSIGNED_BYTE, sizeof (*texturedVertices), &texturedVertices[0].color.r);
  glTexCoordPointer (2, GL_SHORT, sizeof (*texturedVertices), &texturedVertices[0].u);
  glVertexPointer (2, GL_SHORT, sizeof (*texturedVertices), &texturedVertices[0].x);

  glEnableClientState (GL_COLOR_ARRAY);
  glEnableClientState (GL_TEXTURE_COORD_ARRAY);
  glEnableClientState (GL_VERTEX_ARRAY);

  glDrawArrays (GL_QUADS, 0, texturedVertexCount);

  glPopClientAttrib ();

  glPopMatrix ();

  solidVertexCount = 0;
  texturedVertexCount = 0;
}

void
draw_gl_12 (struct terminal *t)
{
#if 1
  unsigned int ascent, descent, spaceWidth, lineHeight;
  int y, row, selbegin, selend;
  unsigned int size;
  int in_selection = 0;

  size = t->history_size * t->size.ws_col;

  const wchar_t* curchars;
  const uint16_t* curattrs;
  int cursorx, cursory;
  int curoffset;

  ascent = FONT_Ascent (font);
  descent = FONT_Descent (font);
  lineHeight = FONT_LineHeight (font);
  spaceWidth = FONT_SpaceWidth (font);

  y = ascent;

  pthread_mutex_lock (&t->bufferLock);

  curchars = t->curchars;
  curattrs = t->curattrs;
  cursorx = t->cursorx;
  cursory = t->cursory;
  curoffset = *t->curoffset;

  if (t->select_begin < t->select_end)
    {
      selbegin = t->select_begin;
      selend = t->select_end;
    }
  else
    {
      selbegin = t->select_end;
      selend = t->select_begin;
    }

  selbegin = (selbegin + t->history_scroll * t->size.ws_col) % size;
  selend = (selend + t->history_scroll * t->size.ws_col) % size;

  for (row = 0; row < t->size.ws_row; ++row)
    {
      size_t pos = ((row + t->history_size - t->history_scroll) * t->size.ws_col + curoffset) % size;
      const wchar_t* line = &curchars[pos];
      const uint16_t* attrline = &curattrs[pos];
      int x = 0, col;

      for (col = 0; col < t->size.ws_col; ++col)
        {
          int printable;
          int attr = attrline[col];
          int xOffset = spaceWidth;

          if (t->focused
              && row == cursory + t->history_scroll
              && col == cursorx)
            {
              attr = REVERSE(attr);

              if (!attr)
                attr = BG(ATTR_WHITE);
            }

          printable = (line[col] != 0);

          if (row * t->size.ws_col + col == selbegin)
            in_selection = 1;

          if (row * t->size.ws_col + col == selend)
            in_selection = 0;

          if (in_selection)
            attr = REVERSE(attr);

          /* color: (attr >> 4) & 7 */

          if (printable)
            {
              struct FONT_Glyph glyph;
              uint16_t u, v;

              GLYPH_Get (line[col], &glyph, &u, &v);

              draw_AddTexturedQuad (x - glyph.x, y - glyph.y,
                                    glyph.width, glyph.height,
                                    u, v,
                                    palette[attr & 0x0f]);

              if (glyph.xOffset > spaceWidth)
                xOffset = glyph.xOffset;
            }

          draw_AddSolidQuad (x, y - ascent, xOffset, lineHeight, palette[(attr >> 4) & 7]);

          if (attr & ATTR_UNDERLINE)
            draw_AddSolidQuad (x, y + descent, xOffset, 1, palette[attr & 0x0f]);

          x += xOffset;
        }

      y += lineHeight;
    }

  pthread_mutex_unlock (&t->bufferLock);

  GLYPH_UpdateTexture ();

  draw_FlushQuads ();
#else
  GLYPH_UpdateTexture ();

  glBindTexture (GL_TEXTURE_2D, GLYPH_Texture ());

  glBegin (GL_QUADS);

  glTexCoord2f (0, 0);
  glVertex2f (0, 0);

  glTexCoord2f (0, 1);
  glVertex2f (0, GLYPH_ATLAS_SIZE);

  glTexCoord2f (1, 1);
  glVertex2f (GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE);

  glTexCoord2f (1, 0);
  glVertex2f (GLYPH_ATLAS_SIZE, 0);

  glEnd ();
#endif
  glXSwapBuffers (X11_display, X11_window);

  glClear (GL_COLOR_BUFFER_BIT);
}
