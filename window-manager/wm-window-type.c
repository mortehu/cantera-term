#include <X11/Xatom.h>
#include <stdio.h>

#include "wm-window-type.h"

static Atom xa_net_wm_window_type;

static Atom xa_net_wm_window_type_desktop;
static Atom xa_net_wm_window_type_dock;
static Atom xa_net_wm_window_type_toolbar;
static Atom xa_net_wm_window_type_menu;
static Atom xa_net_wm_window_type_utility;
static Atom xa_net_wm_window_type_splash;
static Atom xa_net_wm_window_type_dialog;
static Atom xa_net_wm_window_type_normal;

void
wm_window_type_init (Display *display)
{
  xa_net_wm_window_type =         XInternAtom (display, "_NET_WM_WINDOW_TYPE", False);

  xa_net_wm_window_type_desktop = XInternAtom (display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
  xa_net_wm_window_type_dock =    XInternAtom (display, "_NET_WM_WINDOW_TYPE_DOCK", False);
  xa_net_wm_window_type_toolbar = XInternAtom (display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
  xa_net_wm_window_type_menu =    XInternAtom (display, "_NET_WM_WINDOW_TYPE_MENU", False);
  xa_net_wm_window_type_utility = XInternAtom (display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
  xa_net_wm_window_type_splash =  XInternAtom (display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
  xa_net_wm_window_type_dialog =  XInternAtom (display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  xa_net_wm_window_type_normal =  XInternAtom (display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
}

enum wm_window_type
wm_window_type_get (Display *display, Window window)
{
  enum wm_window_type result = wm_window_type_normal;

  Atom type;
  int format;
  unsigned long nitems;
  unsigned long bytes_after;
  unsigned long* prop;

  if(Success != XGetWindowProperty(display, window, xa_net_wm_window_type, 0, 1024, False,
				   XA_ATOM, &type, &format, &nitems,
				   &bytes_after, (unsigned char**) &prop))
    {
      return wm_window_type_normal;
    }

  if(!prop)
    return wm_window_type_normal;

  if (*prop == xa_net_wm_window_type_desktop)
    result = wm_window_type_desktop;
  else if (*prop == xa_net_wm_window_type_dock)
    result = wm_window_type_dock;
  else if (*prop == xa_net_wm_window_type_toolbar)
    result = wm_window_type_toolbar;
  else if (*prop == xa_net_wm_window_type_menu)
    result = wm_window_type_menu;
  else if (*prop == xa_net_wm_window_type_utility)
    result = wm_window_type_utility;
  else if (*prop == xa_net_wm_window_type_splash)
    result = wm_window_type_splash;
  else if (*prop == xa_net_wm_window_type_dialog)
    result = wm_window_type_dialog;
  else if (*prop == xa_net_wm_window_type_normal)
    result = wm_window_type_normal;

  XFree(prop);

  return result;
}
