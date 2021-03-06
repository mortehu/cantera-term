#include <err.h>
#include <stdlib.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <GL/glew.h>
#include <GL/glxew.h>
#include <GL/glx.h>

#include "x11.h"

Display* X11_display = 0;
Window X11_window;
XVisualInfo* X11_visual;
XIM X11_xim;
XIC X11_xic;
GLXContext X11_glx_context;

unsigned int X11_window_width = 800;
unsigned int X11_window_height = 600;

Atom xa_utf8_string;
Atom xa_clipboard;
Atom xa_targets;

static Bool x11_WaitForMapNotify(Display* X11_display, XEvent* event,
                                 char* arg) {
  return (event->type == MapNotify) && (event->xmap.window == (Window) arg);
}

void X11_Setup(void) {
  int attributes[] = { GLX_RGBA, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                       GLX_BLUE_SIZE, 8, GLX_DOUBLEBUFFER, None };

  XWMHints* wmhints;
  Colormap color_map;
  XSetWindowAttributes attr;
  XEvent event;
  char* p;

  XInitThreads();

  const char* display_name = getenv("DISPLAY");

  if (!display_name)
    err(EXIT_FAILURE, "DISPLAY environment variable not set");

  X11_display = XOpenDisplay(display_name);

  if (!X11_display)
    errx(EXIT_FAILURE, "Failed to open X11_display %s", display_name);

  XSynchronize(X11_display, True);

  if (!glXQueryExtension(X11_display, 0, 0))
    errx(EXIT_FAILURE, "No GLX extension present");

  if (!(X11_visual = glXChooseVisual(X11_display, DefaultScreen(X11_display),
                                     attributes)))
    errx(EXIT_FAILURE, "glXChooseVisual failed");

  if (!(X11_glx_context =
            glXCreateContext(X11_display, X11_visual, 0, GL_TRUE)))
    errx(EXIT_FAILURE, "Failed creating OpenGL context");

  color_map =
      XCreateColormap(X11_display, RootWindow(X11_display, X11_visual->screen),
                      X11_visual->visual, AllocNone);

  attr.backing_store = NotUseful;
  attr.border_pixel = 0;
  attr.colormap = color_map;
  attr.event_mask =
      ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
      KeyPressMask | KeyReleaseMask | FocusChangeMask | EnterWindowMask |
      LeaveWindowMask | StructureNotifyMask;

  X11_window = XCreateWindow(
      X11_display, RootWindow(X11_display, X11_visual->screen), 0, 0,
      X11_window_width, X11_window_height, 0, X11_visual->depth, InputOutput,
      X11_visual->visual,
      CWBackingStore | CWBorderPixel | CWColormap | CWEventMask, &attr);

  if (!(wmhints = XAllocWMHints())) errx(EXIT_FAILURE, "XAllocWMHints failed");

  wmhints->input = True;
  wmhints->flags = InputHint;

  XSetWMHints(X11_display, X11_window, wmhints);

  XFree(wmhints);

  XMapRaised(X11_display, X11_window);

  if ((p = XSetLocaleModifiers("")) && *p)
    X11_xim = XOpenIM(X11_display, 0, 0, 0);

  if (!X11_xim && (p = XSetLocaleModifiers("@im=none")) && *p)
    X11_xim = XOpenIM(X11_display, 0, 0, 0);

  if (!X11_xim) errx(EXIT_FAILURE, "Failed to open X Input Method");

  X11_xic =
      XCreateIC(X11_xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                XNClientWindow, X11_window, XNFocusWindow, X11_window, NULL);

  if (!X11_xic) errx(EXIT_FAILURE, "Failed to create X Input Context");

  XIfEvent(X11_display, &event, x11_WaitForMapNotify, (char*)X11_window);

  if (!glXMakeCurrent(X11_display, X11_window, X11_glx_context))
    errx(EXIT_FAILURE, "glXMakeCurrent returned false");

  xa_utf8_string = XInternAtom(X11_display, "UTF8_STRING", False);
  xa_clipboard = XInternAtom(X11_display, "CLIPBOARD", False);
  xa_targets = XInternAtom(X11_display, "TARGETS", False);

  XSynchronize(X11_display, False);
}
