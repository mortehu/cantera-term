#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include "gui.h"

#define FOURCC_YUV12_PLANAR 0x32315659

struct gui_font
{
  XftFont *xft_font;
  unsigned int space_width;
};

static int x11_connected;

static int repaint_socket[2];
static int repaint_queued;
static pthread_mutex_t repaint_lock = PTHREAD_MUTEX_INITIALIZER;

Display *GUI_display;
int GUI_screenidx;
Screen *GUI_screen;
Visual *GUI_visual;
XVisualInfo GUI_visual_template;
XVisualInfo *GUI_visual_info;

static XRenderPictFormat *xrenderpictformat;
static XRenderPictFormat *argb32pictformat;
static XRenderPictFormat *a8pictformat;
static XIM xim;

static Atom xa_wm_delete_window;
static Atom xa_clipboard;
static Atom xa_utf8_string;
static Atom xa_prop_paste;

static struct gui_instance **instances;
static size_t instance_count;
static size_t instance_alloc;

static struct gui_instance *
find_instance(Window w)
{
  size_t i;

  for(i = 0; i < instance_count; ++i)
    {
      if(instances[i]->window == w)
        return instances[i];
    }

  return 0;
}

static void
x11_connect ()
{
  char *c;
  int nitems;

  if (x11_connected)
    return;

  XInitThreads ();

  GUI_display = XOpenDisplay (0);

  if (!GUI_display)
    errx (EXIT_FAILURE, "Failed to open GUI_display");

  XSynchronize (GUI_display, True);

  GUI_screenidx = DefaultScreen (GUI_display);
  GUI_screen = DefaultScreenOfDisplay (GUI_display);
  GUI_visual = DefaultVisual (GUI_display, GUI_screenidx);
  GUI_visual_info = XGetVisualInfo (GUI_display, VisualNoMask, &GUI_visual_template, &nitems);

  xa_wm_delete_window = XInternAtom (GUI_display, "WM_DELETE_WINDOW", False);
  xa_clipboard = XInternAtom (GUI_display, "CLIPBOARD", False);
  xa_utf8_string = XInternAtom (GUI_display, "UTF8_STRING", False);
  xa_prop_paste = XInternAtom (GUI_display, "BRA_PASTE", False);

  xim = 0;

  if ((c = XSetLocaleModifiers ("")) && *c)
    xim = XOpenIM (GUI_display, 0, 0, 0);

  if (!xim && (c = XSetLocaleModifiers ("@im=none")) && *c)
    xim = XOpenIM (GUI_display, 0, 0, 0);

  if (!xim)
    errx (EXIT_FAILURE, "Failed to open X Input Method");

  xrenderpictformat = XRenderFindVisualFormat (GUI_display, GUI_visual);

  if (!xrenderpictformat)
    errx (EXIT_FAILURE, "XRenderFindVisualFormat failed");

  argb32pictformat = XRenderFindStandardFormat (GUI_display, PictStandardARGB32);

  a8pictformat = XRenderFindStandardFormat (GUI_display, PictStandardA8);

  if (!a8pictformat)
    errx (EXIT_FAILURE, "XrenderFindStandardFormat failed for PictStandardA8");

  XSynchronize (GUI_display, False);

  pipe (repaint_socket);

  x11_connected = 1;
}

static void
configure_window (struct gui_instance *gi)
{
  if (gi->back_buffer)
    XRenderFreePicture (GUI_display, gi->back_buffer);

  if (gi->fontdraw)
    XftDrawDestroy (gi->fontdraw);

  if (gi->pmap)
    XFreePixmap (GUI_display, gi->pmap);

  if (gi->gc)
    XFreeGC (GUI_display, gi->gc);

  gi->gc = XCreateGC (GUI_display, gi->window, 0, 0);

  gi->pmap = XCreatePixmap (GUI_display, gi->window, gi->width, gi->height, GUI_visual_info->depth);

  gi->fontdraw = XftDrawCreate (GUI_display, gi->pmap, GUI_visual, DefaultColormap (GUI_display, GUI_screenidx));

  gi->back_buffer
    = XRenderCreatePicture (GUI_display, gi->pmap, xrenderpictformat, 0, 0);

  if (!gi->back_buffer)
    errx (EXIT_FAILURE, "Failed to create back buffer for X window (XRenderCreatePicture)");
}

void
gui_main_loop ()
{
  XEvent event;
  struct gui_instance *gi;
  int xfd = ConnectionNumber (GUI_display);

  for (;;)
    {
      fd_set readset;
      int maxfd;

      if (XPending (GUI_display))
        goto pending;

      FD_ZERO (&readset);

      FD_SET (xfd, &readset);
      maxfd = xfd;

      FD_SET (repaint_socket[0], &readset);

      if (repaint_socket[0] > maxfd)
        maxfd = repaint_socket[0];

      if (-1 == select (maxfd + 1, &readset, 0, 0, 0))
        {
          if (errno == EINTR)
            continue;

          errx (EXIT_FAILURE, "select failed: %s", strerror (errno));
        }

      if (FD_ISSET (repaint_socket[0], &readset))
        {
          char c;

          pthread_mutex_lock (&repaint_lock);
          repaint_queued = 0;
          pthread_mutex_unlock (&repaint_lock);

          read (repaint_socket[0], &c, 1);

          if (!instances[0]->repaint_waiting)
            {
              instances[0]->repaint_waiting = 1;

              XClearArea (GUI_display, instances[0]->window, 0, 0, 0, 0, True);
              XFlush (GUI_display);
            }
        }

pending:
      while (XPending (GUI_display))
        {
          XNextEvent (GUI_display, &event);

          if (0 == (gi = find_instance (event.xany.window)))
            continue;

          if (gi->definition.x11_event)
            gi->definition.x11_event (gi, &event);

          switch (event.type)
            {
            case KeyPress:

              gi->last_event = event.xkey.time;

              if (!XFilterEvent (&event, gi->window) && gi->definition.key_pressed)
                {
                  wchar_t text[32];
                  Status status;
                  KeySym key_sym;
                  int len;

                  len = XwcLookupString (gi->xic, &event.xkey, text, sizeof (text) / sizeof (text[0])- 1, &key_sym, &status);
                  text[len] = 0;

                  gi->definition.key_pressed (gi, key_sym, text, event.xkey.state);
                }

              break;

            case KeyRelease:

              gi->last_event = event.xkey.time;

              if (gi->definition.key_released)
                {
                  KeySym key_sym;

                  key_sym = XLookupKeysym (&event.xkey, 0);

                  gi->definition.key_released (gi, key_sym);
                }

              break;


            case MotionNotify:

              gi->last_event = event.xmotion.time;

              if (gi->definition.mouse_moved)
                gi->definition.mouse_moved (gi, event.xmotion.x,
                                           event.xmotion.y);

              break;

            case ButtonPress:

              gi->last_event = event.xbutton.time;

              if (gi->definition.button_pressed)
                gi->definition.button_pressed (gi, event.xbutton.button - 1,
                                              event.xbutton.state);

              break;

            case ButtonRelease:

              gi->last_event = event.xbutton.time;

              if (gi->definition.button_released)
                gi->definition.button_released (gi, event.xbutton.button - 1,
                                              event.xbutton.state);


              break;

            case ConfigureNotify:

              while (XCheckTypedWindowEvent (GUI_display, gi->window, ConfigureNotify, &event))
                {
                  /* Do nothing */
                }

              if (gi->width == event.xconfigure.width && gi->height == event.xconfigure.height)
                break;

              gi->width = event.xconfigure.width;
              gi->height = event.xconfigure.height;

              configure_window (gi);

              XClearArea (GUI_display, gi->window, 0, 0, 0, 0, True);

              break;

            case NoExpose:

              break;

            case Expose:

                {
                  int minx = event.xexpose.x;
                  int miny = event.xexpose.y;
                  int maxx = minx + event.xexpose.width;
                  int maxy = miny + event.xexpose.height;

                  while (XCheckTypedWindowEvent (GUI_display, gi->window, Expose, &event))
                    {
                      if (event.xexpose.x < minx) minx = event.xexpose.x;
                      if (event.xexpose.y < miny) miny = event.xexpose.y;
                      if (event.xexpose.x + event.xexpose.width > maxx) maxx = event.xexpose.x + event.xexpose.width;
                      if (event.xexpose.y + event.xexpose.height > maxx) maxx = event.xexpose.y + event.xexpose.height;
                    }

                  gi->repaint_waiting = 0;

                  if (!gi->back_buffer)
                    configure_window (gi);

                  gi->definition.paint (gi, minx, miny, maxx - minx, maxy - miny);

                  XRenderComposite (GUI_display, PictOpSrc, gi->back_buffer, None,
                                   gi->front_buffer, minx, miny, 0, 0, minx, miny,
                                   maxx - minx, maxy - miny);
                }

              break;

            case ClientMessage:

              if (event.xclient.data.l[0] == xa_wm_delete_window)
                gi->definition.destroy (gi);

              break;

            case SelectionRequest:

                {
                  XSelectionRequestEvent* request;
                  XSelectionEvent response;
                  enum gui_clipboard clipboard;
                  int ret;

                  request = &event.xselectionrequest;

                  if (request->selection == XA_PRIMARY)
                    clipboard = GUI_PRIMARY_SELECTION;
                  else if (request->selection == XA_SECONDARY)
                    clipboard = GUI_SECONDARY_SELECTION;
                  else if (request->selection == xa_clipboard)
                    clipboard = GUI_CLIPBOARD;
                  else
                    break;

                  if (!gi->clipboards[clipboard].length)
                    break;

                  if (request->target != XA_STRING
                      && request->target != xa_utf8_string)
                    break;

                  if (request->property == None)
                    request->property = request->target;

                  response.type = SelectionNotify;
                  response.send_event = True;
                  response.display = GUI_display;
                  response.requestor = request->requestor;
                  response.selection = request->selection;
                  response.target = request->target;
                  response.property = None;
                  response.time = request->time;

                  ret = XChangeProperty (GUI_display, request->requestor,
                                        request->property, request->target,
                                        8, PropModeReplace,
                                        gi->clipboards[clipboard].data,
                                        gi->clipboards[clipboard].length);

                  if (ret != BadAlloc && ret != BadAtom
                      && ret != BadValue && ret != BadWindow)
                    response.property = request->property;

                  XSendEvent (request->display, request->requestor, False,
                             NoEventMask, (XEvent*) &response);
                }

              break;

            case SelectionNotify:

                {
                  unsigned char* prop;
                  unsigned long nitems, bytes_after;
                  Atom type;
                  int format, result;

                  result = XGetWindowProperty (GUI_display, gi->window,
                                              xa_prop_paste, 0, 0, False,
                                              AnyPropertyType, &type, &format,
                                              &nitems, &bytes_after, &prop);

                  if (result != Success)
                    break;

                  XFree (prop);

                  result = XGetWindowProperty (GUI_display, gi->window,
                                              xa_prop_paste, 0, bytes_after,
                                              False, AnyPropertyType, &type,
                                              &format, &nitems, &bytes_after,
                                              &prop);

                  if (result != Success)
                    break;

                  if (gi->definition.paste
                      && type == xa_utf8_string && format == 8)
                    {
                      gi->definition.paste (gi, (const char*) prop, nitems);
                    }

                  XFree (prop);
                }

              break;
            }
        }
    }
}


struct gui_instance *
gui_instance (struct gui_definition *definition)
{
  XWindowAttributes root_window_attr;
  struct gui_instance *result;
  XRenderPictureAttributes pa;
  unsigned int attr_mask;

  result = malloc (sizeof (*result));
  memset (result, 0, sizeof (*result));

  result->definition = *definition;

  x11_connect (result);

  XGetWindowAttributes(GUI_display, RootWindow(GUI_display, DefaultScreen(GUI_display)), &root_window_attr);

  memset (&result->window_attr, 0, sizeof (result->window_attr));
  result->window_attr.cursor = XCreateFontCursor (GUI_display, XC_left_ptr);

  result->window_attr.colormap = DefaultColormap (GUI_display, GUI_screenidx);
  result->window_attr.event_mask = KeyPressMask | KeyReleaseMask |
    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
    StructureNotifyMask | ExposureMask;

  attr_mask = CWColormap | CWEventMask | CWCursor;

  if (definition->flags & GUI_OVERRIDE_REDIRECT)
    {
      result->window_attr.override_redirect = True;
      attr_mask |= CWOverrideRedirect;
    }

  result->width = root_window_attr.width;
  result->height = root_window_attr.height;

  result->last_event = CurrentTime;

  result->window
    = XCreateWindow (GUI_display, RootWindow (GUI_display, GUI_screenidx),
                     0, 0, result->width, result->height, 0,
                     GUI_visual_info->depth, InputOutput, GUI_visual,
                     attr_mask,
                     &result->window_attr);

  if (!result->window)
    errx (EXIT_FAILURE, "Failed to create X window (XCreateWindow)");

  if (instance_count == instance_alloc)
    {
      instance_alloc = instance_alloc * 2 + 1;
      instances = realloc (instances, sizeof(*instances) * instance_alloc);

      if(!instances)
        errx(EXIT_FAILURE, "realloc failed: %s", strerror(errno));
    }

  instances[instance_count++] = result;

  if(definition->init)
    definition->init(result);

  result->xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing |
                          XIMStatusNothing, XNClientWindow, result->window,
                          XNFocusWindow, result->window, NULL);

  if(!result->xic)
    errx(EXIT_FAILURE, "Failed to create X Input Context (XCreateIC)");

  XMapRaised(GUI_display, result->window);

  if (definition->flags & GUI_OVERRIDE_REDIRECT)
    {
      XGrabPointer(GUI_display, result->window, False,
                   None, GrabModeAsync,
                   GrabModeAsync, result->window, None, CurrentTime);
      XGrabKeyboard(GUI_display, RootWindow(GUI_display, DefaultScreen(GUI_display)),
                    True, GrabModeAsync, GrabModeAsync, CurrentTime);
      XSetInputFocus(GUI_display, result->window, RevertToParent, CurrentTime);
    }


  memset(&pa, 0, sizeof(pa));
  pa.subwindow_mode = IncludeInferiors;

  result->front_buffer = XRenderCreatePicture(GUI_display, result->window, xrenderpictformat, CPSubwindowMode, &pa);

  if(!result->front_buffer)
    errx(EXIT_FAILURE, "Failed to create front buffer for X window (XRenderCreatePicture)");

  return result;
}

void
gui_destroy(struct gui_instance *gi)
{
  if(gi->gc)
    XFreeGC(GUI_display, gi->gc);

  if(gi->front_buffer)
    XRenderFreePicture(GUI_display, gi->front_buffer);

  if(gi->back_buffer)
    XRenderFreePicture(GUI_display, gi->back_buffer);

  if(gi->xic)
    XDestroyIC(gi->xic);

  if(gi->window)
    XDestroyWindow(GUI_display, gi->window);
}

void
gui_repaint(struct gui_instance *gi)
{
  char c = 1;

  pthread_mutex_lock (&repaint_lock);

  if (repaint_queued)
    {
      pthread_mutex_unlock (&repaint_lock);

      return;
    }

  write(repaint_socket[1], &c, 1);

  repaint_queued = 1;

  pthread_mutex_unlock (&repaint_lock);
}

void
gui_draw_quad(struct gui_instance *gi, int x, int y, unsigned int width,
              unsigned int height, unsigned int color)
{
  XRenderColor c;

  c.alpha = ((color & 0xff000000) >> 24) * 0x0101;
  c.red = ((color & 0xff0000) >> 16) * 0x0101;
  c.green = ((color & 0x00ff00) >> 8) * 0x0101;
  c.blue = (color & 0x0000ff) * 0x0101;

  XRenderFillRectangle(GUI_display, PictOpSrc, gi->back_buffer, &c, x, y, width, height);
}

struct gui_font *
gui_font_load(const char *name, unsigned int size, int flags)
{
  struct gui_font *result;
  XGlyphInfo extents;
  XftFont *xft_font;
  char *xft_font_desc;

  xft_font_desc = alloca(strlen(name) + 64);

  sprintf(xft_font_desc, "%s:pixelsize=%d", name, size);

  if(flags & GUI_FONT_BOLD)
    strcat(xft_font_desc, ":bold");

  if(0 == (xft_font = XftFontOpenName(GUI_display, GUI_screenidx, xft_font_desc)))
    return 0;

  result = malloc(sizeof(*result));

  result->xft_font = xft_font;

  XftTextExtentsUtf8(GUI_display, xft_font, (const unsigned char *) " ", 1, &extents);
  result->space_width = extents.xOff;

  return result;
}

unsigned int
gui_font_line_height(struct gui_font *font)
{
  return font->xft_font->ascent + font->xft_font->descent;
}

void
gui_text_clip(struct gui_instance *gi, int x, int y,
              unsigned int width, unsigned int height)
{
  XRectangle cliprect;
  cliprect.x = x;
  cliprect.y = y;
  cliprect.width = width;
  cliprect.height = height;

  XftDrawSetClipRectangles(gi->fontdraw, 0, 0, &cliprect, 1);
  XRenderSetPictureClipRectangles(GUI_display, gi->back_buffer, 0, 0, &cliprect, 1);
}

unsigned int
gui_text_width (struct gui_font *font, const char *text, size_t length)
{
  XGlyphInfo extents;

  XftTextExtentsUtf8(GUI_display, font->xft_font, (const unsigned char *) text,
                     length, &extents);

  return extents.xOff;
}

unsigned int
gui_wtext_width(struct gui_font *font, const wchar_t *text, size_t length)
{
  XGlyphInfo extents;

  if (sizeof (wchar_t) == 4)
    {
      XftTextExtents32 (GUI_display, font->xft_font, (const unsigned int *) text,
                        length, &extents);
    }
  else if (sizeof (wchar_t) == 2)
    {
      XftTextExtents16 (GUI_display, font->xft_font, (const unsigned short *) text,
                        length, &extents);
    }
  else
    assert (!"Unexpected size of wchar_t");

  return extents.xOff;
}

void
gui_draw_text(struct gui_instance *gi, struct gui_font *font,
              int x, int y, const char *text, unsigned int color,
              int alignment)
{
  XftColor xft_color;
  size_t length;

  length = strlen(text);

  if(alignment)
    x -= gui_text_width(font, text, length) * alignment / 2;

  xft_color.pixel = color | 0xff000000;
  xft_color.color.red = (color >> 16) * 0x0101;
  xft_color.color.green = ((color >> 8) & 0xff) * 0x0101;
  xft_color.color.blue = (color & 0xff) * 0x0101;
  xft_color.color.alpha = 0xffff;

  XftDrawStringUtf8(gi->fontdraw, &xft_color, font->xft_font,
                    x, y + font->xft_font->ascent,
                    (const unsigned char *) text, length);
}

void
gui_draw_text_length(struct gui_instance *gi, struct gui_font *font,
                     int x, int y, const char *text, size_t length,
                     unsigned int color)
{
  XftColor xft_color;

  xft_color.pixel = color | 0xff000000;
  xft_color.color.red = (color >> 16) * 0x0101;
  xft_color.color.green = ((color >> 8) & 0xff) * 0x0101;
  xft_color.color.blue = (color & 0xff) * 0x0101;
  xft_color.color.alpha = 0xffff;

  XftDrawStringUtf8(gi->fontdraw, &xft_color, font->xft_font,
                    x, y + font->xft_font->ascent,
                    (const unsigned char *) text, length);
}

void
gui_draw_wtext_length(struct gui_instance *gi, struct gui_font *font,
                      int x, int y, const wchar_t *text, size_t length,
                      unsigned int color)
{
  XftColor xft_color;

  xft_color.pixel = color | 0xff000000;
  xft_color.color.red = (color >> 16) * 0x0101;
  xft_color.color.green = ((color >> 8) & 0xff) * 0x0101;
  xft_color.color.blue = (color & 0xff) * 0x0101;
  xft_color.color.alpha = 0xffff;

  if (sizeof (wchar_t) == 4)
    {
      XftDrawString32 (gi->fontdraw, &xft_color, font->xft_font,
                       x, y + font->xft_font->ascent,
                       (const unsigned int *) text, length);
    }
  else if (sizeof (wchar_t) == 2)
    {
      XftDrawString16 (gi->fontdraw, &xft_color, font->xft_font,
                       x, y + font->xft_font->ascent,
                       (const unsigned short *) text, length);
    }
  else
    assert (!"Unexpected size of wchar_t");
}
