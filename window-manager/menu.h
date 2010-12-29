#ifndef MENU_H_
#define MENU_H_ 1

void menu_init();
void menu_init_ximage(XImage* image, int width, int height, void* data);
void menu_thumbnail_dimensions(struct screen* screen, int* width, int* height, int* margin);
void menu_draw_desktops (struct screen* screen);
void menu_draw(struct screen* screen);
void menu_keypress(struct screen* screen, int key_sym, const char* text, int textlen, Time time);
int menu_handle_char(struct screen* screen, int ch);

#endif /* !MENU_H_ */
