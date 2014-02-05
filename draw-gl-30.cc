#include "draw.h"

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

struct draw_TexturedVertex {
  draw_TexturedVertex() {}

  draw_TexturedVertex(int x, int y, int u, int v, const Terminal::Color &color)
      : x(x), y(y), u(u), v(v), color(color) {}

  int16_t x, y;
  uint16_t u, v;
  Terminal::Color color;
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
                              const Terminal::Color &color) {
  if (!color.r && !color.g && !color.b) return;

  texturedVertices.emplace_back(x, y, 0, 0, color);
  texturedVertices.emplace_back(x, y + height, 0, 0, color);
  texturedVertices.emplace_back(x + width, y + height, 0, 0, color);
  texturedVertices.emplace_back(x + width, y, 0, 0, color);
}

static void draw_AddTexturedQuad(unsigned int x, unsigned int y,
                                 unsigned int width, unsigned int height,
                                 unsigned int u, unsigned int v,
                                 const Terminal::Color &color) {
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
         (int)logLength, log);
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

    errx(EXIT_FAILURE, "glLinkProgram failed: %.*s", (int)logLength, log);
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

void draw_gl_30(const Terminal::State &state, const FONT_Data *font) {
  // Step 2: Submit the GL commands.
  glUniform2f(glGetUniformLocation(shader.handle, "uniform_RcpWindowSize"),
              1.0f / X11_window_width, 1.0f / X11_window_height);

  unsigned int ascent = FONT_Ascent(font);
  unsigned int descent = FONT_Descent(font);
  unsigned int lineHeight = FONT_LineHeight(font);
  unsigned int spaceWidth = FONT_SpaceWidth(font);

  bool in_selection = (state.selection_begin > state.width * state.height &&
                       state.selection_end < state.width * state.height);
  int y = ascent;

  for (size_t row = 0; row < state.height; ++row) {
    const wchar_t *line = &state.chars[row * state.width];
    const Terminal::Attr *attrline = &state.attrs[row * state.width];
    int x = 0;

    for (size_t col = 0; col < state.width; ++col) {
      Terminal::Attr attr = attrline[col];
      int xOffset = spaceWidth;

      if (!state.cursor_hidden && row == state.cursor_y &&
          col == state.cursor_x) {
        attr.fg = Terminal::Color(0, 0, 0);
        if (state.focused) {
          attr.bg = Terminal::Color(255, 255, 255);
        } else {
          attr.bg = Terminal::Color(127, 127, 127);
        }
      }

      // `selection_begin' might be greater than `selection_end' if our history
      // window straddles the end of the history buffer.
      if (row * state.width + col == state.selection_begin) in_selection = true;
      if (row * state.width + col == state.selection_end) in_selection = false;

      if (in_selection) std::swap(attr.fg, attr.bg);

      int character = line[col];

      if (character > ' ') {
        struct FONT_Glyph glyph;
        uint16_t u, v;

        if (!GLYPH_IsLoaded(character)) {
          struct FONT_Glyph *glyph;

          if (!(glyph = FONT_GlyphForCharacter(font, character)))
            fprintf(stderr, "Failed to get glyph for '%d'", character);

          GLYPH_Add(character, glyph);

          free(glyph);
        }

        GLYPH_Get(character, &glyph, &u, &v);

        if (glyph.xOffset > 0 &&
            static_cast<unsigned int>(glyph.xOffset) > spaceWidth)
          xOffset = glyph.xOffset;

        draw_AddSolidQuad(x, y - ascent, xOffset, lineHeight, attr.bg);

        if (attr.extra & ATTR_UNDERLINE)
          draw_AddSolidQuad(x, y + descent, xOffset, 1, attr.fg);

        draw_AddTexturedQuad(x - glyph.x, y - glyph.y, glyph.width,
                             glyph.height, u, v, attr.fg);
      } else {
        draw_AddSolidQuad(x, y - ascent, xOffset, lineHeight, attr.bg);

        if (attr.extra & ATTR_UNDERLINE)
          draw_AddSolidQuad(x, y + descent, xOffset, 1, attr.fg);
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
