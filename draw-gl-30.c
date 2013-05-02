#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>

#include "font.h"
#include "glyph.h"
#include "opengl.h"
#include "terminal.h"
#include "x11.h"

struct draw_Color
{
  uint8_t r, g, b;
};

struct draw_TexturedVertex
{
  int16_t x, y;
  uint16_t u, v;
  struct draw_Color color;
};

struct draw_Shader
{
  GLuint handle;
  GLuint color_attribute;
  GLuint vertex_position_attribute;
  GLuint texture_coord_attribute;
};

static struct draw_Shader shader;

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

static void
draw_AddSolidQuad (unsigned int x, unsigned int y,
                   unsigned int width, unsigned int height,
                   uint32_t uint_color)
{
  struct draw_Color color;

  if (!(uint_color & 0xffffff))
    return;

  color.r = uint_color >> 16;
  color.g = uint_color >> 8;
  color.b = uint_color;

  ARRAY_GROW_IF (texturedVertices, texturedVertexCount,
                 texturedVertexAlloc, 4);

  ADD_VERTEX (x,         y,          0, 0, color);
  ADD_VERTEX (x,         y + height, 0, 0, color);
  ADD_VERTEX (x + width, y + height, 0, 0, color);
  ADD_VERTEX (x + width, y,          0, 0, color);

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

  ADD_VERTEX (x,         y,          u,         v,          color);
  ADD_VERTEX (x,         y + height, u,         v + height, color);
  ADD_VERTEX (x + width, y + height, u + width, v + height, color);
  ADD_VERTEX (x + width, y,          u + width, v,          color);

failed:

  ;
}

static void
draw_FlushQuads (void)
{
  glBindTexture (GL_TEXTURE_2D, GLYPH_Texture ());
  glEnable (GL_BLEND);
  glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  glVertexAttribPointer (shader.color_attribute, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof (*texturedVertices), &texturedVertices[0].color.r);
  glVertexAttribPointer (shader.vertex_position_attribute, 2, GL_SHORT, GL_FALSE, sizeof (*texturedVertices), &texturedVertices[0].x);
  glVertexAttribPointer (shader.texture_coord_attribute, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof (*texturedVertices), &texturedVertices[0].u);
  glDrawArrays (GL_QUADS, 0, texturedVertexCount);

  texturedVertexCount = 0;
}

/**
 * string: the shader source code
 * type: GL_VERTEX_SHADER or GL_FRAGMENT_SHADER
 */
static GLuint
load_shader (const char *error_context, const char *string, GLenum type)
{
  GLuint result;
  GLint string_length, compile_status;

  result = glCreateShader (type);

  string_length = strlen (string);
  glShaderSource (result, 1, &string, &string_length);
  glCompileShader (result);

  glGetShaderiv (result, GL_COMPILE_STATUS, &compile_status);

  if (compile_status != GL_TRUE)
    {
      GLchar log[1024];
      GLsizei logLength;
      glGetShaderInfoLog (result, sizeof (log), &logLength, log);

      errx (EXIT_FAILURE, "%s: glCompileShader failed: %.*s",
            error_context, (int) logLength, log);
    }

  return result;
}

static struct draw_Shader
load_program (const char *vertex_shader_source,
              const char *fragment_shader_source)
{
  GLuint vertex_shader, fragment_shader;
  struct draw_Shader result;
  GLint link_status;

  vertex_shader = load_shader ("vertex", vertex_shader_source, GL_VERTEX_SHADER);
  fragment_shader = load_shader ("fragment", fragment_shader_source, GL_FRAGMENT_SHADER);

  result.handle = glCreateProgram ();
  glAttachShader (result.handle, vertex_shader);
  glAttachShader (result.handle, fragment_shader);
  glLinkProgram (result.handle);

  glGetProgramiv (result.handle, GL_LINK_STATUS, &link_status);

  if (link_status != GL_TRUE)
    {
      GLchar log[1024];
      GLsizei logLength;
      glGetProgramInfoLog (result.handle, sizeof (log), &logLength, log);

      errx (EXIT_FAILURE, "glLinkProgram failed: %.*s",
            (int) logLength, log);
    }

  glUseProgram (result.handle);

  result.vertex_position_attribute = glGetAttribLocation (result.handle, "attr_VertexPosition");
  result.texture_coord_attribute = glGetAttribLocation (result.handle, "attr_TextureCoord");
  result.color_attribute = glGetAttribLocation (result.handle, "attr_Color");

  return result;
}

void
init_gl_30 (void)
{
  static const char *vertex_shader_source =
    "attribute vec2 attr_VertexPosition;\n"
    "attribute vec2 attr_TextureCoord;\n"
    "attribute vec3 attr_Color;\n"
    "uniform vec2 uniform_WindowSize;\n"
    "varying vec2 var_TextureCoord;\n"
    "varying vec3 var_Color;\n"
    "void main (void)\n"
    "{\n"
    "  gl_Position = vec4(-1.0 + (attr_VertexPosition.x * uniform_WindowSize.x) * 2.0,\n"
    "                      1.0 - (attr_VertexPosition.y * uniform_WindowSize.y) * 2.0, 0.0, 1.0);\n"
    "  var_TextureCoord = attr_TextureCoord;\n"
    "  var_Color = attr_Color;\n"
    "}";

  static const char *fragment_shader_source =
    "varying vec2 var_TextureCoord;\n"
    "varying vec3 var_Color;\n"
    "uniform sampler2D uniform_Sampler;\n"
    "uniform float uniform_TextureScale;\n"
    "void main (void)\n"
    "{\n"
    "  gl_FragColor = vec4(var_Color, 1.0) * texture2D(uniform_Sampler, vec2 (var_TextureCoord.s * uniform_TextureScale, var_TextureCoord.t * uniform_TextureScale));\n"
    "}";

  glewInit ();

  shader = load_program (vertex_shader_source, fragment_shader_source);

  glUniform1i (glGetUniformLocation (shader.handle, "uniform_Sampler"), 0);
  glUniform1f (glGetUniformLocation (shader.handle, "uniform_TextureScale"), 1.0f / GLYPH_ATLAS_SIZE);
  glEnableVertexAttribArray (shader.vertex_position_attribute);
  glEnableVertexAttribArray (shader.texture_coord_attribute);
  glEnableVertexAttribArray (shader.color_attribute);
}

void
draw_gl_30 (struct terminal *t)
{
  unsigned int ascent, descent, spaceWidth, lineHeight;
  int y, row, selbegin, selend;
  unsigned int size;
  int in_selection = 0;

  size = t->history_size * t->size.ws_col;

  const wchar_t* curchars;
  const uint16_t* curattrs;
  int cursorx, cursory;
  int curoffset;

  glUniform2f (glGetUniformLocation (shader.handle, "uniform_WindowSize"),
               1.0f / X11_window_width,
               1.0f / X11_window_height);

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

              if (glyph.xOffset > spaceWidth)
                xOffset = glyph.xOffset;

              draw_AddSolidQuad (x, y - ascent, xOffset, lineHeight, palette[(attr >> 4) & 7]);

              if (attr & ATTR_UNDERLINE)
                draw_AddSolidQuad (x, y + descent, xOffset, 1, palette[attr & 0x0f]);

              draw_AddTexturedQuad (x - glyph.x, y - glyph.y,
                                    glyph.width, glyph.height,
                                    u, v,
                                    palette[attr & 0x0f]);
            }
          else
            {
              draw_AddSolidQuad (x, y - ascent, xOffset, lineHeight, palette[(attr >> 4) & 7]);

              if (attr & ATTR_UNDERLINE)
                draw_AddSolidQuad (x, y + descent, xOffset, 1, palette[attr & 0x0f]);
            }

          x += xOffset;
        }

      y += lineHeight;
    }

  pthread_mutex_unlock (&t->bufferLock);

  GLYPH_UpdateTexture ();

  draw_FlushQuads ();

  glXSwapBuffers (X11_display, X11_window);

  glClear (GL_COLOR_BUFFER_BIT);
}
