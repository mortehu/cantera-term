#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <err.h>
#include <sysexits.h>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "font.h"

struct FONT_Data {
  FT_Face *faces;
  size_t faceCount;

  unsigned int spaceWidth;
};

static FT_Library ft_library;

static FT_GlyphSlot font_FreeTypeGlyphForCharacter(struct FONT_Data *font,
                                                   wint_t character,
                                                   FT_Face *face,
                                                   unsigned int loadFlags);

void FONT_Init(void) {
  int status;

  if (0 != (status = FT_Init_FreeType(&ft_library)))
    errx(EXIT_FAILURE, "Failed to initialize FreeType with status %d", status);
}

int FONT_PathsForFont(char ***paths, const char *name, unsigned int size,
                      unsigned int weight) {
  FcPattern *pattern;
  FcCharSet *charSet;
  FcFontSet *fontSet;
  FcResult fcResult;
  unsigned int i, result = 0;

  if (!FcInit()) return -1;

  pattern = FcNameParse((FcChar8 *)name);

  FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double) size);
  FcPatternAddInteger(pattern, FC_WEIGHT, weight);

  FcConfigSubstitute(0, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);

  fontSet = FcFontSort(0, pattern, FcTrue, &charSet, &fcResult);

  FcPatternDestroy(pattern);

  if (!fontSet) return -1;

  *paths = calloc(fontSet->nfont, sizeof(**paths));

  for (i = 0; i < fontSet->nfont; ++i) {
    FcChar8 *path = 0;

    FcPatternGetString(fontSet->fonts[i], FC_FILE, 0, &path);

    if (path) (*paths)[result++] = strdup((const char *)path);
  }

  FcFontSetDestroy(fontSet);

  return result;
}

struct FONT_Data *FONT_Load(const char *name, unsigned int size,
                            unsigned int weight) {
  struct FONT_Data *result;
  FT_GlyphSlot tmpGlyph;
  char **paths;
  int i, pathCount;
  int ok = 0;

  pathCount = FONT_PathsForFont(&paths, name, size, weight);

  if (pathCount <= 0) return NULL;

  result = calloc(1, sizeof(*result));

  if (!(result->faces = calloc(pathCount, sizeof(*result->faces)))) goto fail;

  result->faceCount = 0;

  for (i = 0; i < pathCount; ++i) {
    FT_Face face;
    int ret;

    if (0 != (ret = FT_New_Face(ft_library, paths[i], 0, &face))) {
      fprintf(stderr, "FT_New_Face on %s failed with code %d\n", paths[i], ret);

      continue;
    }

    if (0 != (ret = FT_Set_Pixel_Sizes(face, 0, size))) {
      FT_Done_Face(face);

      fprintf(stderr, "FT_New_Face on %s failed with code %d\n", paths[i], ret);

      continue;
    }

    result->faces[result->faceCount++] = face;
  }

  if (!result->faceCount) {
    fprintf(stderr, "Failed to load any font faces for `%s'\n", name);

    goto fail;
  }

  tmpGlyph = font_FreeTypeGlyphForCharacter(result, ' ', NULL, 0);
  result->spaceWidth = tmpGlyph->advance.x >> 6;

  ok = 1;

fail:

  if (!ok) {
    free(result);
    result = NULL;
  }

  for (i = 0; i < pathCount; ++i)
    free(paths[i]);
  free(paths);

  return result;
}

void FONT_Free(struct FONT_Data *font) {
  int i;

  for (i = 0; i < font->faceCount; ++i)
    FT_Done_Face(font->faces[i]);

  free(font->faces);
}

unsigned int FONT_Ascent(struct FONT_Data *font) {
  if (!font->faceCount) return 0.0;

  return font->faces[0]->size->metrics.ascender >> 6;
}

unsigned int FONT_Descent(struct FONT_Data *font) {
  if (!font->faceCount) return 0.0;

  return -font->faces[0]->size->metrics.descender >> 6;
}

unsigned int FONT_LineHeight(struct FONT_Data *font) {
  if (!font->faceCount) return 0.0;

  return font->faces[0]->size->metrics.height >> 6;
}

unsigned int FONT_SpaceWidth(struct FONT_Data *font) {
  return font->spaceWidth;
}

struct FONT_Glyph *FONT_GlyphForCharacter(struct FONT_Data *font,
                                          wint_t character) {
  struct FONT_Glyph *result;
  FT_GlyphSlot glyph;
  FT_Face face;
  unsigned int y, x, i;

  if (!(glyph = font_FreeTypeGlyphForCharacter(font, character, &face, 0)))
    return NULL;

  assert(!(glyph->bitmap.width % 3));

  glyph->bitmap.width /= 3;

  if (!(result = FONT_GlyphWithSize(glyph->bitmap.width, glyph->bitmap.rows)))
    return NULL;

  result->x = -glyph->bitmap_left;
  result->y = glyph->bitmap_top;
  result->xOffset = (glyph->advance.x + 32) >> 6;
  result->yOffset = (glyph->advance.y + 32) >> 6;

  for (y = 0, i = 0; y < result->height; ++y) {
    for (x = 0; x < result->width; ++x, i += 4) {
      result->data[i + 0] =
          glyph->bitmap.buffer[y * glyph->bitmap.pitch + x * 3 + 0];
      result->data[i + 1] =
          glyph->bitmap.buffer[y * glyph->bitmap.pitch + x * 3 + 1];
      result->data[i + 2] =
          glyph->bitmap.buffer[y * glyph->bitmap.pitch + x * 3 + 2];
      result->data[i + 3] =
          (result->data[i] + result->data[i + 1] + result->data[i + 2]) / 3;
    }
  }

  return result;
}

struct FONT_Glyph *FONT_GlyphWithSize(unsigned int width, unsigned int height) {
  struct FONT_Glyph *result;

  result = calloc(1, offsetof(struct FONT_Glyph, data) + width * height * 4);
  result->width = width;
  result->height = height;

  return result;
}

static FT_GlyphSlot font_FreeTypeGlyphForCharacter(struct FONT_Data *font,
                                                   wint_t character,
                                                   FT_Face *face,
                                                   unsigned int loadFlags) {
  int faceIndex;

  for (faceIndex = 0; faceIndex < font->faceCount; ++faceIndex) {
    FT_Face currentFace;

    currentFace = font->faces[faceIndex];

    if (!FT_Load_Char(currentFace, character, loadFlags)) {
      FT_Render_Glyph(currentFace->glyph, FT_RENDER_MODE_LCD);

      if (face) *face = currentFace;

      return currentFace->glyph;
    }
  }

  return 0;
}
