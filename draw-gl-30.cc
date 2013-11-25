#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <err.h>
#include <pthread.h>

#include "font.h"
#include "glyph.h"
#include "opengl.h"
#include "terminal.h"
#include "x11.h"

struct draw_Color {
  uint8_t r, g, b;
};

struct draw_TexturedVertex {
  draw_TexturedVertex() {}

  draw_TexturedVertex(int x, int y, int u, int v, draw_Color color)
      : x(x), y(y), u(u), v(v), color(color) {}

  int16_t x, y;
  uint16_t u, v;
  draw_Color color;
};

struct draw_Shader {
  GLuint handle;
  GLuint color_attribute;
  GLuint vertex_position_attribute;
  GLuint texture_coord_attribute;
};

static struct draw_Shader shader;

static std::vector<draw_TexturedVertex> texturedVertices;

static void draw_AddSolidQuad(unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height,
                              uint32_t uint_color) {
  struct draw_Color color;

  if (!(uint_color & 0xffffff)) return;

  color.r = uint_color >> 16;
  color.g = uint_color >> 8;
  color.b = uint_color;

  texturedVertices.emplace_back(x, y, 0, 0, color);
  texturedVertices.emplace_back(x, y + height, 0, 0, color);
  texturedVertices.emplace_back(x + width, y + height, 0, 0, color);
  texturedVertices.emplace_back(x + width, y, 0, 0, color);
}

static void draw_AddTexturedQuad(unsigned int x, unsigned int y,
                                 unsigned int width, unsigned int height,
                                 unsigned int u, unsigned int v,
                                 uint32_t uint_color) {
  struct draw_Color color;

  color.r = uint_color >> 16;
  color.g = uint_color >> 8;
  color.b = uint_color;

  texturedVertices.emplace_back(x, y, u, v, color);
  texturedVertices.emplace_back(x, y + height, u, v + height, color);
  texturedVertices.emplace_back(x + width, y + height, u + width, v + height,
                                color);
  texturedVertices.emplace_back(x + width, y, u + width, v, color);
}

static void draw_FlushQuads(void) {
  glBindTexture(GL_TEXTURE_2D, GLYPH_Texture());
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  glVertexAttribPointer(shader.color_attribute, 3, GL_UNSIGNED_BYTE, GL_TRUE,
                        sizeof(texturedVertices[0]),
                        &texturedVertices[0].color.r);
  glVertexAttribPointer(shader.vertex_position_attribute, 2, GL_SHORT, GL_FALSE,
                        sizeof(texturedVertices[0]), &texturedVertices[0].x);
  glVertexAttribPointer(shader.texture_coord_attribute, 2, GL_UNSIGNED_SHORT,
                        GL_FALSE, sizeof(texturedVertices[0]),
                        &texturedVertices[0].u);
  glDrawArrays(GL_QUADS, 0, texturedVertices.size());

  texturedVertices.clear();
}

/**
 * string: the shader source code
 * type: GL_VERTEX_SHADER or GL_FRAGMENT_SHADER
 */
static GLuint load_shader(const char *error_context, const char *string,
                          GLenum type) {
  GLuint result;
  GLint string_length, compile_status;

  result = glCreateShader(type);

  string_length = strlen(string);
  glShaderSource(result, 1, &string, &string_length);
  glCompileShader(result);

  glGetShaderiv(result, GL_COMPILE_STATUS, &compile_status);

  if (compile_status != GL_TRUE) {
    GLchar log[1024];
    GLsizei logLength;
    glGetShaderInfoLog(result, sizeof(log), &logLength, log);

    errx(EXIT_FAILURE, "%s: glCompileShader failed: %.*s", error_context,
         (int) logLength, log);
  }

  return result;
}

static struct draw_Shader load_program(const char *vertex_shader_source,
                                       const char *fragment_shader_source) {
  GLuint vertex_shader, fragment_shader;
  struct draw_Shader result;
  GLint link_status;

  vertex_shader = load_shader("vertex", vertex_shader_source, GL_VERTEX_SHADER);
  fragment_shader =
      load_shader("fragment", fragment_shader_source, GL_FRAGMENT_SHADER);

  result.handle = glCreateProgram();
  glAttachShader(result.handle, vertex_shader);
  glAttachShader(result.handle, fragment_shader);
  glLinkProgram(result.handle);

  glGetProgramiv(result.handle, GL_LINK_STATUS, &link_status);

  if (link_status != GL_TRUE) {
    GLchar log[1024];
    GLsizei logLength;
    glGetProgramInfoLog(result.handle, sizeof(log), &logLength, log);

    errx(EXIT_FAILURE, "glLinkProgram failed: %.*s", (int) logLength, log);
  }

  glUseProgram(result.handle);

  result.vertex_position_attribute =
      glGetAttribLocation(result.handle, "attr_VertexPosition");
  result.texture_coord_attribute =
      glGetAttribLocation(result.handle, "attr_TextureCoord");
  result.color_attribute = glGetAttribLocation(result.handle, "attr_Color");

  return result;
}

void init_gl_30(void) {
  static const char *vertex_shader_source =
      "attribute vec2 attr_VertexPosition;\n"
      "attribute vec2 attr_TextureCoord;\n"
      "attribute vec3 attr_Color;\n"
      "uniform vec2 uniform_RcpWindowSize;\n"
      "varying vec2 var_TextureCoord;\n"
      "varying vec3 var_Color;\n"
      "void main (void)\n"
      "{\n"
      "  gl_Position = vec4(-1.0 + (attr_VertexPosition.x * "
      "uniform_RcpWindowSize.x) * 2.0,\n"
      "                      1.0 - (attr_VertexPosition.y * "
      "uniform_RcpWindowSize.y) * 2.0, 0.0, 1.0);\n"
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
      "  gl_FragColor = vec4(var_Color, 1.0) * texture2D(uniform_Sampler, vec2 "
      "(var_TextureCoord.s * uniform_TextureScale, var_TextureCoord.t * "
      "uniform_TextureScale));\n"
      "}";

  glewInit();

  shader = load_program(vertex_shader_source, fragment_shader_source);

  glUniform1i(glGetUniformLocation(shader.handle, "uniform_Sampler"), 0);
  glUniform1f(glGetUniformLocation(shader.handle, "uniform_TextureScale"),
              1.0f / GLYPH_ATLAS_SIZE);
  glEnableVertexAttribArray(shader.vertex_position_attribute);
  glEnableVertexAttribArray(shader.texture_coord_attribute);
  glEnableVertexAttribArray(shader.color_attribute);

  glEnable(GL_TEXTURE_2D);
}

void draw_gl_30(struct terminal *t) {
  // Step 1: Clone the data we need for drawing under a mutex lock.
  pthread_mutex_lock(&t->bufferLock);

  size_t width = t->size.ws_col;
  size_t height = t->size.ws_row;
  size_t history_size = t->history_size * width;

  std::unique_ptr<wchar_t[]> curchars(new wchar_t[width * height]);
  std::unique_ptr<uint16_t[]> curattrs(new uint16_t[width * height]);

  for (size_t row = 0, offset = (t->history_size - t->history_scroll) * width +
                                *t->curoffset;
       row < height; ++row, offset += width) {
    offset %= history_size;
    std::copy(&t->curchars[offset], &t->curchars[offset + width],
              &curchars[row * width]);
    std::copy(&t->curattrs[offset], &t->curattrs[offset + width],
              &curattrs[row * width]);
  }

  size_t cursorx = t->cursorx;
  size_t cursory = t->cursory + t->history_scroll;

  size_t selbegin, selend;
  if (t->select_begin < t->select_end) {
    selbegin = t->select_begin;
    selend = t->select_end;
  } else {
    selbegin = t->select_end;
    selend = t->select_begin;
  }

  selbegin = (selbegin + t->history_scroll * width) % history_size;
  selend = (selend + t->history_scroll * width) % history_size;

  bool hide_cursor = t->hide_cursor;
  bool focused = t->focused;

  pthread_mutex_unlock(&t->bufferLock);

  // Step 2: Submit the GL commands.
  glUniform2f(glGetUniformLocation(shader.handle, "uniform_RcpWindowSize"),
              1.0f / X11_window_width, 1.0f / X11_window_height);

  unsigned int ascent = FONT_Ascent(font);
  unsigned int descent = FONT_Descent(font);
  unsigned int lineHeight = FONT_LineHeight(font);
  unsigned int spaceWidth = FONT_SpaceWidth(font);

  // TODO(mortehu): Handle case when selection starts above top of screen.
  bool in_selection = false;

  int y = ascent;

  for (size_t row = 0; row < height; ++row) {
    const wchar_t *line = &curchars[row * width];
    const uint16_t *attrline = &curattrs[row * width];
    int x = 0;

    for (size_t col = 0; col < width; ++col) {
      int printable;
      unsigned int attr = attrline[col];
      int xOffset = spaceWidth;
      unsigned int color = palette[attr & 0xF];
      unsigned int background_color = palette[(attr >> 4) & 7];

      if (!hide_cursor && row == cursory && col == cursorx) {
        if (focused) {
          color = 0xff000000;
          background_color = 0xffffffff;
        } else {
          color = 0xff000000;
          background_color = 0xff7f7f7f;
        }
      }

      printable = (line[col] != 0);

      /* `selbegin' might be greater than `selend' if our history window
       * straddles the end of the history buffer.  */
      if (row * width + col == selbegin) in_selection = true;
      if (row * width + col == selend) in_selection = false;

      if (in_selection) {
        unsigned int tmp;
        tmp = color;
        color = background_color;
        background_color = tmp;
      }

      if (printable) {
        struct FONT_Glyph glyph;
        uint16_t u, v;

        GLYPH_Get(line[col], &glyph, &u, &v);

        if (glyph.xOffset > 0 &&
            static_cast<unsigned int>(glyph.xOffset) > spaceWidth)
          xOffset = glyph.xOffset;

        draw_AddSolidQuad(x, y - ascent, xOffset, lineHeight, background_color);

        if (attr & ATTR_UNDERLINE)
          draw_AddSolidQuad(x, y + descent, xOffset, 1, color);

        draw_AddTexturedQuad(x - glyph.x, y - glyph.y, glyph.width,
                             glyph.height, u, v, color);
      } else {
        draw_AddSolidQuad(x, y - ascent, xOffset, lineHeight, background_color);

        if (attr & ATTR_UNDERLINE)
          draw_AddSolidQuad(x, y + descent, xOffset, 1, color);
      }

      x += xOffset;
    }

    y += lineHeight;
  }

  GLYPH_UpdateTexture();

  draw_FlushQuads();

  glXSwapBuffers(X11_display, X11_window);

  glClear(GL_COLOR_BUFFER_BIT);
}
