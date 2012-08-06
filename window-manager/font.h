#ifndef FONT_H_
#define FONT_H_ 1

void font_init();
void font_free();
void loadglyph(int size, unsigned int n);
void drawtext(Picture dest, const wchar_t* text, size_t length, int x, int y, int color, int size);

#define SMALL 0
#define LARGE 1

#endif /* FONT_H_ */
