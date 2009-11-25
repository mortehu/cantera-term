#ifndef MENU_H_
#define MENU_H_ 1

void menu_init();
void menu_init_ximage(XImage* image, int width, int height, void* data);
void menu_thumbnail_dimensions(int* width, int* height, int* margin);
void menu_draw_desktops(Picture buffer, int height);
void menu_draw();
void menu_keypress(int key_sym, const char* text, int textlen, Time time);
int menu_handle_char(int ch);

#endif /* !MENU_H_ */
