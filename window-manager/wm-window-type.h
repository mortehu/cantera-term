#ifndef WM_WINDOW_TYPE_H_
#define WM_WINDOW_TYPE_H_ 1

#include <X11/Xlib.h>

enum wm_window_type
{
  wm_window_type_unknown = 0,
  wm_window_type_desktop,
  wm_window_type_dock,
  wm_window_type_toolbar,
  wm_window_type_menu,
  wm_window_type_utility,
  wm_window_type_splash,
  wm_window_type_dialog,
  wm_window_type_normal
};

void
wm_window_type_init (Display *display);

enum wm_window_type
wm_window_type_get (Display *display, Window window);

#endif /* !WM_WINDOW_TYPE_H_ */
