#ifndef FONT_H_
#define FONT_H_ 1

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FONT_Data;

struct FONT_Glyph
{
  uint16_t width, height;
  int16_t  x, y;
  int16_t  xOffset, yOffset;

  uint8_t data[1];
};

/************************************************************************/

void
FONT_Init (void);

int
FONT_PathsForFont (char ***paths, const char *name, unsigned int size, unsigned int weight);

struct FONT_Data *
FONT_Load (const char *name, unsigned int size, unsigned int weight);

void
FONT_Free (struct FONT_Data *font);

unsigned int
FONT_Ascent (struct FONT_Data *font);

unsigned int
FONT_Descent (struct FONT_Data *font);

unsigned int
FONT_LineHeight (struct FONT_Data *font);

unsigned int
FONT_SpaceWidth (struct FONT_Data *font);

struct FONT_Glyph *
FONT_GlyphForCharacter (struct FONT_Data *font, wint_t character);

struct FONT_Glyph *
FONT_GlyphWithSize (unsigned int width, unsigned int height);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !FONT_H_ */
