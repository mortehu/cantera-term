#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrender.h>
#include <X11/keysym.h>

#include "common.h"

Display* display;
Window window;
int screenidx;
Screen* screen;
Visual* visual;
XVisualInfo visual_template;
XVisualInfo* visual_info;
XSetWindowAttributes window_attr;
XRenderPictFormat* xrenderpictformat;
XIM xim = 0;
XIC xic;
int ctrl_pressed = 0;
int mod1_pressed = 0;
int super_pressed = 0;
int shift_pressed = 0;
Picture window_picture;
Picture window_buffer;

int window_width, window_height;

static void x11_connect(const char* display_name)
{
  int nitems;
  char* c;

  display = XOpenDisplay(display_name);

  if(!display)
  {
    fprintf(stderr, "Failed to open display %s\n", display_name);

    exit(EXIT_FAILURE);
  }

  XSynchronize(display, True);

  screenidx = DefaultScreen(display);
  screen = DefaultScreenOfDisplay(display);
  visual = DefaultVisual(display, screenidx);
  visual_info = XGetVisualInfo(display, VisualNoMask, &visual_template, &nitems);

  memset(&window_attr, 0, sizeof(window_attr));
  window_attr.cursor = XCreateFontCursor(display, XC_left_ptr);
  window_width = 800;
  window_height = 600;

  window_attr.colormap = DefaultColormap(display, 0);
  window_attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | ExposureMask;

  window = XCreateWindow(display, RootWindow(display, screenidx), 0, 0, window_width, window_height, 0, visual_info->depth, InputOutput, visual, CWColormap | CWEventMask | CWCursor, &window_attr);

  XStoreName(display, window, "cantera-fm");
  XMapWindow(display, window);

  xim = 0;

  if((c = XSetLocaleModifiers("")) && *c)
    xim = XOpenIM(display, 0, 0, 0);

  if(!xim && (c = XSetLocaleModifiers("@im=none")) && *c)
    xim = XOpenIM(display, 0, 0, 0);

  if(!xim)
  {
    fprintf(stderr, "Failed to open X Input Method\n");

    return;
  }

  xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                  XNClientWindow, window, XNFocusWindow, window, NULL);

  if(!xic)
  {
    fprintf(stderr, "Failed to create X Input Context\n");

    return;
  }

  xrenderpictformat = XRenderFindVisualFormat(display, visual);

  if(!xrenderpictformat)
  {
    fprintf(stderr, "XRenderFindVisualFormat failed.\n");

    return;
  }

  XRenderPictureAttributes pa;
  pa.subwindow_mode = IncludeInferiors;

  window_picture = XRenderCreatePicture(display, window, xrenderpictformat, CPSubwindowMode, &pa);

  {
    Pixmap pmap;

    pmap = XCreatePixmap(display, window, window_width, window_height, visual_info->depth);
    window_buffer = XRenderCreatePicture(display, pmap, xrenderpictformat, 0, 0);

    if(window_buffer == None)
    {
      fprintf(stderr, "Failed to create root buffer\n");

      return;
    }

    XFreePixmap(display, pmap);
  }

  XSynchronize(display, False);
}

static void paint(int x, int y, int width, int height)
{
  XRenderColor black;

  black.red = 0;
  black.green = 0;
  black.blue = 0xffff;
  black.alpha = 0xffff;

  XRenderFillRectangle(display, PictOpSrc, window_buffer, &black, 0, 0, window_width, window_height);

  if(window_buffer != window_picture)
  {
    XRenderComposite(display, PictOpSrc, window_buffer, None, window_picture,
                     x, y,
                     0, 0,
                     x, y, width, height);
  }
}

int main(int argc, char** argv)
{
  int xfd;

  if(!getenv("DISPLAY"))
  {
    fprintf(stderr, "DISPLAY variable not set.\n");

    return EXIT_FAILURE;
  }

  chdir(getenv("HOME"));

  mkdir(".cantera", 0777);
  mkdir(".cantera/commands", 0777);
  mkdir(".cantera/file-commands", 0777);
  mkdir(".cantera/filemanager", 0777);

  x11_connect(getenv("DISPLAY"));

  xfd = ConnectionNumber(display);

  for(;;)
  {
    XEvent event;
    fd_set readset;
    int maxfd;

    FD_ZERO(&readset);
    FD_SET(xfd, &readset);
    maxfd = xfd;

    if(-1 == select(maxfd + 1, &readset, 0, 0, 0))
    {
      if(errno == EINTR)
        continue;

      fprintf(stderr, "select failed: %s\n", strerror(errno));

      return EXIT_FAILURE;
    }

    while(XPending(display))
    {
      XNextEvent(display, &event);

      switch(event.type)
      {
      case KeyPress:

        if(!XFilterEvent(&event, window))
        {
          char text[32];
          Status status;
          KeySym key_sym;
          int len;

          ctrl_pressed = (event.xkey.state & ControlMask);
          mod1_pressed = (event.xkey.state & Mod1Mask);
          super_pressed = (event.xkey.state & Mod4Mask);
          shift_pressed = (event.xkey.state & ShiftMask);

          len = Xutf8LookupString(xic, &event.xkey, text, sizeof(text) - 1, &key_sym, &status);

          if(!text[0])
            len = 0;
        }

        break;

      case KeyRelease:

        {
          KeySym key_sym;

          key_sym = XLookupKeysym(&event.xkey, 0);

          ctrl_pressed = (event.xkey.state & ControlMask);
          mod1_pressed = (event.xkey.state & Mod1Mask);
          super_pressed = (event.xkey.state & Mod4Mask);
          shift_pressed = (event.xkey.state & ShiftMask);
        }

        break;

      case ConfigureNotify:

        {
          Pixmap pmap;

          window_width = event.xconfigure.width;
          window_height = event.xconfigure.height;

          XRenderFreePicture(display, window_buffer);

          pmap = XCreatePixmap(display, window, window_width, window_height, visual_info->depth);
          window_buffer = XRenderCreatePicture(display, pmap, xrenderpictformat, 0, 0);

          if(window_buffer == None)
          {
            fprintf(stderr, "Failed to create root buffer\n");

            return EXIT_FAILURE;
          }

          XFreePixmap(display, pmap);

          XClearArea(display, window, 0, 0, window_width, window_height, True);
        }

        break;

      case NoExpose:

        break;

      case Expose:

        {
          int minx = event.xexpose.x;
          int miny = event.xexpose.y;
          int maxx = minx + event.xexpose.width;
          int maxy = miny + event.xexpose.height;

          while(XCheckTypedWindowEvent(display, window, Expose, &event))
          {
            if(event.xexpose.x < minx) minx = event.xexpose.x;
            if(event.xexpose.y < miny) miny = event.xexpose.y;
            if(event.xexpose.x + event.xexpose.width > maxx) maxx = event.xexpose.x + event.xexpose.width;
            if(event.xexpose.y + event.xexpose.height > maxy) maxy = event.xexpose.y + event.xexpose.height;
          }

          paint(minx, miny, maxx - minx, maxy - miny);
        }

        break;
      }
    }
  }

  return EXIT_SUCCESS;
}

// vim: ts=2 sw=2 et sts=2
