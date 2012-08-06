#ifndef X11_H_
#define X11_H_ 1

#include <X11/Xlib.h>
#include <GL/glx.h>

extern Display*     X11_display;
extern Window       X11_window;
extern XVisualInfo* X11_visual;
extern XIM          X11_xim;
extern XIC          X11_xic;
extern GLXContext   X11_glx_context;

void
X11_Setup (void);

#endif /* !X11_H_ */
