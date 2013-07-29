#ifndef GLYPH_H_
#define GLYPH_H_ 1

#include "opengl.h"
#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GLYPH_ATLAS_SIZE 512

void
GLYPH_Init (void);

GLuint
GLYPH_Texture (void);

void
GLYPH_Add (unsigned int code, struct FONT_Glyph *glyph);

int
GLYPH_IsLoaded (unsigned int code);

void
GLYPH_Get (unsigned int code, struct FONT_Glyph *glyph,
           uint16_t *u, uint16_t *v);

void
GLYPH_UpdateTexture (void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLYPH_H_ */
