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

extern unsigned int X11_window_width;
extern unsigned int X11_window_height;

extern Atom prop_paste;
extern Atom xa_utf8_string;
extern Atom xa_compound_text;
extern Atom xa_targets;
extern Atom xa_net_wm_icon;
extern Atom xa_net_wm_pid;
extern Atom xa_wm_state;
extern Atom xa_wm_transient_for;
extern Atom xa_wm_protocols;
extern Atom xa_wm_delete_window;

void
X11_Setup (void);

void
X11_handle_configure (void);

#endif /* !X11_H_ */
