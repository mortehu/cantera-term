#ifndef MENU_H_
#define MENU_H_ 1

void menu_init();
void menu_init_ximage(XImage* image, int width, int height, void* data);
void menu_thumbnail_dimensions(struct screen* screen, int* width, int* height, int* margin);
void menu_draw_desktops(struct screen* screen, Picture buffer, int height);
void menu_draw(struct screen* screen);

#endif /* !MENU_H_ */
