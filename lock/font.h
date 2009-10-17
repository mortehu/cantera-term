#ifndef FONT_H_
#define FONT_H_ 1

#define FONT_ALIGN_LEFT   0
#define FONT_ALIGN_CENTER 1
#define FONT_ALIGN_RIGHT  2

/**
 * Loads a font and return a handle.
 *
 * If the font is already loaded, return the previously generated handle.
 *
 * Terminates if the font does not exist.
 */
int font_load(const char* name);

/**
 * Returns the pixel width of a string with a given font.
 */
int font_string_width(int font, int size, const char* string);

/**
 * Draws the specified UTF-8 string.
 *
 * The y coordinate is taken as the baseline of the text (the bottom of most
 * capital letters).
 *
 * Returns the width of the string.
 */
int font_draw(int font, int size, const char* string, float x, float y, int alignment);

#endif /* !FONT_H_ */
