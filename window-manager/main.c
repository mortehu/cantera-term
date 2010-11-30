#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <sched.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>
#include <wchar.h>

#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>

#include "array.h"
#include "common.h"
#include "font.h"
#include "globals.h"
#include "menu.h"
#include "tree.h"

#define PARTIAL_REPAINT 1

extern char** environ;

struct tree* config;
int inotify_fd = -1;

int xskips[] = { 1, 1 };
int yskips[] = { 1, 1 };
int font_sizes[] = { 12, 36 };

unsigned int palette[] =
{
  /* ANSI colors */
  0xff000000, 0xff1818c2, 0xff18c218, 0xff18c2c2,
  0xffc21818, 0xffc218c2, 0xffc2c218, 0xffc2c2c2,
  0xff686868, 0xff7474ff, 0xff54ff54, 0xff54ffff,
  0xffff5454, 0xffff54ff, 0xffffff54, 0xffffffff,

  0xffdddddd, /* 16 = lighter gray */
  0xff9090ff, /* 17 = lighter blue */
  0xff484848, /* 18 = darker gray */
  0x3f000000, /* 19 = semitransparent black */
};

Cursor menu_cursor;
Cursor shell_cursor;
Cursor other_cursor;

XRenderColor xrpalette[sizeof(palette) / sizeof(palette[0])];
Picture picpalette[sizeof(palette) / sizeof(palette[0])];

struct screen* screens;
int screen_count = 0;

int terminal_list_width;
int terminal_list_height;
Window terminal_list_popup = 0;

void run_command(int fd, const char* command, const char* arg);
void init_ximage(XImage* image, int width, int height, void* data);

struct window*
new_window(Window window, XCreateWindowEvent* cwe);

void
window_gone(Window xwindow);

Window window;

struct window
{
  Window xwindow;
  int x, y;
  unsigned int width, height;

  Window transient_for;
  terminal* desktop;
  struct screen* screen;
};

struct
{
  ARRAY_MEMBERS(struct window);
} windows;

struct window*
find_window(Window window)
{
  size_t i;

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      if(ARRAY_GET(&windows, i).xwindow == window)
        return &ARRAY_GET(&windows, i);
    }

  return 0;
}

Window
find_xwindow(terminal* t)
{
  const struct window* w;
  size_t i;

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      w = &ARRAY_GET(&windows, i);

      if(w->desktop == t)
        return w->xwindow;
    }

  return 0;
}

Display* display;
int screenidx;
Screen* screen;
Visual* visual;
XVisualInfo visual_template;
XVisualInfo* visual_info;
Window root_window;
XWindowAttributes root_window_attr;
Atom xa_potty_play;
Atom xa_utf8_string;
Atom xa_compound_text;
Atom xa_targets;
Atom xa_net_wm_icon;
Atom xa_net_wm_pid;
Atom xa_wm_state;
Atom xa_wm_transient_for;
Atom xa_wm_protocols;
Atom xa_wm_delete_window;
XRenderPictFormat* xrenderpictformat;
XRenderPictFormat* argb32pictformat;
XRenderPictFormat* a8pictformat;
GlyphSet alpha_glyphs[2];
XIM xim = 0;
XIC xic;
int xfd;
int ctrl_pressed = 0;
int mod1_pressed = 0;
int super_pressed = 0;
int shift_pressed = 0;
int button1_pressed = 0;

struct screen* current_screen;

#define my_isprint(c) (isprint((c)) || ((c) >= 0x80))

void
clear()
{
  unsigned int i;

  for(i = 0; i < screen_count; ++i)
    {
      if(screens[i].at->mode == mode_menu)
        {
          XClearArea(display, screens[i].window, 0, 0,
                     screens[i].width, screens[i].height, True);
        }
    }
}

static void
swap_terminals(unsigned int a, unsigned int b)
{
  terminal* term_a;
  terminal* term_b;
  terminal tmp;
  unsigned int i;

  term_a = &current_screen->terminals[a];
  term_b = &current_screen->terminals[b];

  tmp = *term_a;
  *term_a = *term_b;
  *term_b = tmp;

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      struct window* w;

      w = &ARRAY_GET(&windows, i);

      if(w->screen != current_screen)
        continue;

      if(w->desktop == term_a)
        w->desktop = term_b;
      else if(w->desktop == term_b)
        w->desktop = term_a;
    }
}

static void
history_reset(struct screen *s, int terminal)
{
  s->history_size = 1;
  s->history[0] = terminal;
}

static void
history_add(struct screen *s, int terminal)
{
  if (s->history_size == TERMINAL_COUNT)
    {
      --s->history_size;
      memmove (s->history, s->history + 1, s->history_size * sizeof(*s->history));
    }

  s->history[s->history_size++] = terminal;
}

static void paint(Window window, int x, int y, int width, int height)
{
  unsigned int i;

  for(i = 0; i < screen_count; ++i)
    {
      if(screens[i].window == window)
          menu_draw(&screens[i]);

      XRenderComposite(display, PictOpSrc,
                       screens[i].root_buffer,
                       None,
                       screens[i].root_picture,
                       x, y,
                       0, 0,
                       x, y, width, height);
    }
}

int get_int_property(Window window, Atom property, int* result)
{
  Atom type;
  int format;
  unsigned long nitems;
  unsigned long bytes_after;
  uint32_t* prop;

  if(Success != XGetWindowProperty(display, window, property, 0, 4, False,
                                   AnyPropertyType, &type, &format, &nitems,
                                   &bytes_after, (unsigned char**) &prop))
    return -1;

  if(!prop)
    return -1;

  *result = *prop;

  XFree(prop);

  return 0;
}

static int first_available_terminal(struct screen* screen)
{
  int i;

  if(screen->at->mode == mode_menu)
    return screen->active_terminal;

  for(i = 0; i < TERMINAL_COUNT; ++i)
  {
    if(screen->terminals[i].mode == mode_menu)
      return i;
  }

  return -1;
}

static void set_map_state(Window window, int state)
{
  unsigned long data[2];
  data[0] = state;
  data[1] = None;

  XChangeProperty(display, window, xa_wm_state, xa_wm_state, 32, PropModeReplace, (unsigned char*) data, 2);
}

static void grab_thumbnail(struct window* w)
{
  Atom type;
  int format;
  int result;
  unsigned long nitems;
  unsigned long bytes_after;
  unsigned char* prop;

  int thumb_width, thumb_height;

  if(!w->screen)
    return;

  menu_thumbnail_dimensions(w->screen, &thumb_width, &thumb_height, 0);

  if(w->desktop->thumbnail)
    return;

  result = XGetWindowProperty(display, w->xwindow, xa_net_wm_icon, 0, 0, False,
                              AnyPropertyType, &type, &format, &nitems, &bytes_after,
                              &prop);

  if(result != Success)
    {
      fprintf(stderr, "XGetWindowProperty failed\n");

      return;
    }

  if(prop)
    XFree(prop);

  result = XGetWindowProperty(display, w->xwindow, xa_net_wm_icon, 0, bytes_after, False,
                              AnyPropertyType, &type, &format, &nitems, &bytes_after,
                              &prop);

  if(prop && format == 32)
    {
      unsigned long* buf = (unsigned long*) prop;
      unsigned int width = buf[0];
      unsigned int height = buf[1];
      unsigned int i;

      union
        {
          uint32_t rgba;
          struct
            {
              unsigned char r, g, b, a;
            } c;
        } *colors;

      colors = calloc(sizeof(*colors), width * height);

      for(i = 0; i < width * height; ++i)
        {
          colors[i].rgba = buf[i + 2];

          colors[i].c.r = (colors[i].c.r * colors[i].c.a) / 255;
          colors[i].c.g = (colors[i].c.g * colors[i].c.a) / 255;
          colors[i].c.b = (colors[i].c.b * colors[i].c.a) / 255;
        }

      XImage temp_image;
      init_ximage(&temp_image, width, height, colors);

      Pixmap temp_pixmap = XCreatePixmap(display, w->screen->window, thumb_width, thumb_height, format);

      GC tmp_gc = XCreateGC(display, temp_pixmap, 0, 0);
      XFillRectangle(display, temp_pixmap, tmp_gc, 0, 0, thumb_width, thumb_height);
      XPutImage(display, temp_pixmap, tmp_gc, &temp_image, 0, 0,
                thumb_width / 2 - width / 2, thumb_height / 2 - height / 2, width, height);
      XFreeGC(display, tmp_gc);

      w->desktop->thumbnail
        = XRenderCreatePicture(display, temp_pixmap,
                               XRenderFindStandardFormat(display, PictStandardARGB32),
                               0, 0);

      XFreePixmap(display, temp_pixmap);

      XFree(prop);

      free(colors);
    }
}

static void paint_terminal_list_popup();

static void create_terminal_list_popup()
{
  XSetWindowAttributes window_attr;
  int thumb_width, thumb_height, thumb_margin;

  if(current_screen->at->mode == mode_menu || terminal_list_popup)
    return;

  menu_thumbnail_dimensions(current_screen, &thumb_width, &thumb_height,
                            &thumb_margin);

  terminal_list_width = current_screen->width;
  terminal_list_height = 2 * thumb_height + 3 * thumb_margin + yskips[SMALL];

  window_attr.override_redirect = True;

  terminal_list_popup
    = XCreateWindow(display, RootWindow(display, screenidx),
                    current_screen->x_org,
                    current_screen->y_org + current_screen->height - terminal_list_height,
                    terminal_list_width, terminal_list_height,
                    0, visual_info->depth, InputOutput, visual, CWOverrideRedirect, &window_attr);

  XMapWindow(display, terminal_list_popup);

  paint_terminal_list_popup();
}

static void paint_terminal_list_popup()
{
  Pixmap pmap;
  Picture windowpic;
  Picture buffer;

  windowpic = XRenderCreatePicture(display, terminal_list_popup, xrenderpictformat, 0, 0);

  pmap = XCreatePixmap(display, terminal_list_popup, terminal_list_width, terminal_list_height, visual_info->depth);
  buffer = XRenderCreatePicture(display, pmap, xrenderpictformat, 0, 0);

  XFreePixmap(display, pmap);

  XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[0], 0, 0, terminal_list_width, terminal_list_height);

  menu_draw_desktops(current_screen, buffer, terminal_list_height);

  XRenderComposite(display, PictOpSrc, buffer, None, windowpic, 0, 0, 0, 0, 0, 0, terminal_list_width, terminal_list_height);

  XRenderFreePicture(display, buffer);
  XRenderFreePicture(display, windowpic);
}

static void destroy_terminal_list_popup()
{
  if(!terminal_list_popup)
    return;

  XDestroyWindow(display, terminal_list_popup);
  terminal_list_popup = 0;
}

static void
set_focus(struct screen* screen, terminal* t, Time when)
{
  Window focus;

  focus = screen->window;

  if(t->mode == mode_x11)
  {
    struct window* w;
    size_t i;

    for(i = 0; i < ARRAY_COUNT(&windows); ++i)
      {
        w = &ARRAY_GET(&windows, i);

        if(!w->transient_for && w->desktop == t)
          {
            focus = w->xwindow;
            set_map_state(focus, 1);
            XMapRaised(display, w->xwindow);

            break;
          }
      }

    for(i = 0; i < ARRAY_COUNT(&windows); ++i)
      {
        w = &ARRAY_GET(&windows, i);

        if(w->transient_for && w->desktop == t)
          {
            focus = w->xwindow;
            set_map_state(focus, 1);
            XMapRaised(display, w->xwindow);
          }
      }
  }

  if(screen == current_screen)
    XSetInputFocus(display, focus, RevertToPointerRoot, when);
}

static void
set_active_terminal(struct screen* screen, unsigned int terminal_index, Time when)
{
  Window tmp_window;
  int i;

  if(terminal_index == screen->active_terminal)
    return;

  set_focus(screen, &screen->terminals[terminal_index], when);

  for(i = 0; i < ARRAY_COUNT(&windows); ++i)
    {
      if(ARRAY_GET(&windows, i).desktop == screen->at)
        {
          tmp_window = ARRAY_GET(&windows, i).xwindow;
          XUnmapWindow(display, tmp_window);
          set_map_state(tmp_window, 0);
        }
    }

  screen->active_terminal = terminal_index;
  screen->at = &screen->terminals[terminal_index];

  if(screen->at->mode == mode_menu)
    destroy_terminal_list_popup();
}

pid_t launch(const char* command, Time when)
{
  pid_t pid = fork();

  if(pid == -1)
    return 0;

  if(!pid)
  {
    char* args[4];
    char buf[32];

    setsid();

    sprintf(buf, "%llu", (unsigned long long int) when);
    setenv("DESKTOP_START_ID", buf, 1);

    sprintf(buf, ".cantera/bash-history-%02d", current_screen->active_terminal);
    setenv("HISTFILE", buf, 1);

    sprintf(buf, ".cantera/session-%02d", current_screen->active_terminal);
    setenv("SESSION_PATH", buf, 1);

    args[0] = "/bin/sh";
    args[1] = "-c";
    asprintf(&args[2], "exec %s", command);
    args[3] = 0;

    execve(args[0], args, environ);

    exit(EXIT_FAILURE);
  }

  if(current_screen->at->mode == mode_menu)
    current_screen->at->pid = pid;

  return pid;
}

static void grab_keys()
{
  static const int global_modifiers[] = { 0, LockMask, LockMask | Mod2Mask, Mod2Mask };
  int i, f, gmod;

  XGrabKey(display, 129, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* music */
  XGrabKey(display, 160, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* mute */
  XGrabKey(display, 161, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* calculator */
  XGrabKey(display, 174, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* volume down */
  XGrabKey(display, 176, AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync); /* volume up */

  XGrabKey(display, XKeysymToKeycode(display, XK_Alt_L), Mod4Mask, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Alt_R), Mod4Mask, root_window, False, GrabModeAsync, GrabModeAsync);

  XGrabKey(display, XKeysymToKeycode(display, XK_Super_L), AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Super_R), AnyModifier, root_window, False, GrabModeAsync, GrabModeAsync);

  for(i = 0; i < sizeof(global_modifiers) / sizeof(global_modifiers[0]); ++i)
  {
    gmod = global_modifiers[i];

    for(f = 0; f < 9; ++f)
      XGrabKey(display, XKeysymToKeycode(display, XK_1 + f), Mod4Mask, root_window, False, GrabModeAsync, GrabModeAsync);

    XGrabKey(display, XKeysymToKeycode(display, XK_Left), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Right), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Up), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Down), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);

    for(f = 0; f < 12; ++f)
    {
      XGrabKey(display, XKeysymToKeycode(display, XK_F1 + f), ControlMask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
      XGrabKey(display, XKeysymToKeycode(display, XK_F1 + f), Mod4Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    }

    XGrabKey(display, XKeysymToKeycode(display, XK_Escape), ControlMask | Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_F4), Mod1Mask | gmod, root_window, False, GrabModeAsync, GrabModeAsync);
  }
}

static int xerror_handler(Display* display, XErrorEvent* error)
{
  char errorbuf[512];

  XGetErrorText(display, error->error_code, errorbuf, sizeof(errorbuf));

  if(error->error_code == BadWindow)
    window_gone(error->resourceid);

  fprintf(stderr, "X error: %s (request: %d, minor: %d)  ID: %08X\n", errorbuf,
          error->request_code, error->minor_code, (unsigned int) error->resourceid);

  return 0;
}

static int xerror_discarder(Display* display, XErrorEvent* error)
{
  return 0;
}

static int x11_connected = 0;

static void
create_menu_cursor()
{
  Pixmap mask = XCreatePixmap(display, XRootWindow(display, 0), 1, 1, 1);

  XGCValues xgc;

  xgc.function = GXclear;

  GC gc = XCreateGC(display, mask, GCFunction, &xgc);

  XFillRectangle(display, mask, gc, 0, 0, 1, 1);

  XColor color;

  color.pixel = 0;
  color.red = 0;
  color.flags = 4;

  menu_cursor = XCreatePixmapCursor(display, mask, mask, &color, &color, 0, 0);

  XFreePixmap(display, mask);

  XFreeGC(display, gc);
}

static void composite_init()
{
#if 0
  XRenderPictureAttributes pa;
  int major, minor;

  if(!XCompositeQueryExtension(display, &i, &i))
    return;

  XCompositeQueryVersion(display, &major, &minor);

  if(!(major > 0 || minor >= 2))
    return;

  if(!XDamageQueryExtension(display, &damage_eventbase, &damage_errorbase))
    return;

  pa.subwindow_mode = IncludeInferiors;

  XCompositeRedirectSubwindows(display, root_window, CompositeRedirectManual);
#endif
}

static void x11_connect(const char* display_name)
{
  XSetWindowAttributes window_attr;
  XineramaScreenInfo* xinerama_screens;
  int i;
  int nitems;
  char* c;

  fprintf(stderr, "Connecting to %s\n", display_name);

  if(x11_connected)
    return;

  display = XOpenDisplay(display_name);

  if(!display)
  {
    fprintf(stderr, "Failed to open display %s\n", display_name);

    return;
  }

  XSynchronize(display, True);

  root_window = RootWindow(display, screenidx);

  window_attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask | EnterWindowMask;
  XChangeWindowAttributes(display, root_window, CWEventMask, &window_attr);

  XGetWindowAttributes(display, root_window, &root_window_attr);

  shell_cursor = XCreateFontCursor(display, XC_left_ptr);
  other_cursor = XCreateFontCursor(display, XC_left_ptr);

  XSetErrorHandler(xerror_handler);
  //XSetIOErrorHandler(xioerror_handler);

  screenidx = DefaultScreen(display);
  screen = DefaultScreenOfDisplay(display);
  visual = DefaultVisual(display, screenidx);
  visual_info = XGetVisualInfo(display, VisualNoMask, &visual_template, &nitems);

  xrenderpictformat = XRenderFindVisualFormat(display, visual);

  if(!xrenderpictformat)
  {
    fprintf(stderr, "XRenderFindVisualFormat failed.\n");

    return;
  }

  create_menu_cursor();

  if(XineramaQueryExtension(display, &i, &i))
  {
    if(XineramaIsActive(display))
      xinerama_screens = XineramaQueryScreens(display, &screen_count);
  }

  if(!screen_count)
  {
    screen_count = 1;
    xinerama_screens = malloc(sizeof(*screens) * 1);
    xinerama_screens[0].x_org = 0;
    xinerama_screens[0].y_org = 0;
    xinerama_screens[0].width = root_window_attr.width;
    xinerama_screens[0].height = root_window_attr.height;
  }

  screens = calloc(sizeof(*screens), screen_count);
  current_screen = screens;

  for(i = 0; i < screen_count; ++i)
    {
      XRenderPictureAttributes pa;
      Pixmap pmap;

      pa.subwindow_mode = IncludeInferiors;

      screens[i].width = xinerama_screens[i].width;
      screens[i].height = xinerama_screens[i].height;
      screens[i].x_org = xinerama_screens[i].x_org;
      screens[i].y_org = xinerama_screens[i].y_org;

      memset(&window_attr, 0, sizeof(window_attr));

      window_attr.colormap = DefaultColormap(display, 0);
      window_attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask;
      window_attr.override_redirect = True;

      window_attr.cursor = menu_cursor;

      screens[i].window
        = XCreateWindow(display, root_window,
                        screens[i].x_org, screens[i].y_org,
                        screens[i].width, screens[i].height, 0,
                        visual_info->depth, InputOutput, visual,
                        CWOverrideRedirect | CWColormap | CWEventMask |
                        CWCursor, &window_attr);

      XMapWindow(display, screens[i].window);

      screens[i].root_picture = XRenderCreatePicture(display, screens[i].window, xrenderpictformat, CPSubwindowMode, &pa);

      pmap = XCreatePixmap(display, screens[i].window, screens[i].width, screens[i].height, visual_info->depth);
      screens[i].root_buffer = XRenderCreatePicture(display, pmap, xrenderpictformat, 0, 0);

      if(screens[i].root_buffer == None)
        {
          fprintf(stderr, "Failed to create root buffer\n");

          return;
        }

      XFreePixmap(display, pmap);

      screens[i].active_terminal = 0;
      screens[i].at = &screens[i].terminals[0];
    }

  window = screens[0].window;

  grab_keys();

  xa_potty_play = XInternAtom(display, "POTTY_PLAY", False);
  xa_utf8_string = XInternAtom(display, "UTF8_STRING", False);
  xa_compound_text = XInternAtom(display, "COMPOUND_TEXT", False);
  xa_targets = XInternAtom(display, "TARGETS", False);
  xa_wm_state = XInternAtom(display, "WM_STATE", False);
  xa_net_wm_icon = XInternAtom(display, "_NET_WM_ICON", False);
  xa_net_wm_pid = XInternAtom(display, "_NET_WM_PID", False);
  xa_wm_transient_for = XInternAtom(display, "WM_TRANSIENT_FOR", False);
  xa_wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
  xa_wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);

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

  /* XXX: Used to be `window'  Root okay? */
  xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                  XNClientWindow, root_window, XNFocusWindow, root_window, NULL);

  if(!xic)
  {
    fprintf(stderr, "Failed to create X Input Context\n");

    return;
  }
  a8pictformat = XRenderFindStandardFormat(display, PictStandardA8);

  for(i = 0; i < sizeof(palette) / sizeof(palette[0]); ++i)
  {
    xrpalette[i].alpha = ((palette[i] & 0xff000000) >> 24) * 0x0101;
    xrpalette[i].red = ((palette[i] & 0xff0000) >> 16) * 0x0101;
    xrpalette[i].green = ((palette[i] & 0x00ff00) >> 8) * 0x0101;
    xrpalette[i].blue = (palette[i] & 0x0000ff) * 0x0101;

    picpalette[i] = XRenderCreateSolidFill(display, &xrpalette[i]);
  }

  alpha_glyphs[0] = XRenderCreateGlyphSet(display, a8pictformat);
  alpha_glyphs[1] = XRenderCreateGlyphSet(display, a8pictformat);

  if(!alpha_glyphs[0] || !alpha_glyphs[1])
  {
    fprintf(stderr, "XRenderCreateGlyphSet failed.\n");

    return;
  }

  composite_init();

  font_init();

  menu_init();

  xfd = ConnectionNumber(display);

  XSynchronize(display, False);

  x11_connected = 1;
}

static int done;

static void sighandler(int signal)
{
  fprintf(stderr, "Got signal %d\n", signal);

  exit(EXIT_SUCCESS);
}

static void
enter_menu_mode(struct screen* screen, terminal* t)
{
  /* XXX: Free first (2010-10-15: what does this mean?) */

  t->mode = mode_menu;

  if(t->thumbnail)
  {
    XRenderFreePicture(display, t->thumbnail);
    t->thumbnail = 0;
  }

  if(t == screen->at)
    destroy_terminal_list_popup();
}

int
find_pid(pid_t pid, struct terminal** term, struct screen** screen)
{
  unsigned int screen_index;
  unsigned int term_index;

  for(screen_index = 0; screen_index < screen_count; ++screen_index)
    {
      for(term_index = 0; term_index < TERMINAL_COUNT; ++term_index)
        {
          if(screens[screen_index].terminals[term_index].pid == pid)
            {
              *term = &screens[screen_index].terminals[term_index];
              *screen = &screens[screen_index];

              return 0;
            }
        }
    }

  return -1;
}

static void
connect_transient(struct window* w)
{
  if(w->transient_for)
    return;

  XSync(display, False);
  XSetErrorHandler(xerror_discarder);
  XGetTransientForHint(display, w->xwindow, &w->transient_for);
  XSync(display, False);
  XSetErrorHandler(xerror_handler);

  if(w->transient_for)
    {
      struct window* parent;

      parent = find_window(w->transient_for);

      if(parent)
        {
          w->screen = parent->screen;
          w->desktop = parent->desktop;
        }
    }
}

void
window_gone(Window xwindow)
{
  struct screen* screen;
  terminal* desktop;
  struct window* w;
  size_t i, j;

  w = find_window(xwindow);

  if(!w)
    return;

  screen = w->screen;
  desktop = w->desktop;

  ARRAY_REMOVE_PTR(&windows, w);

  if (screen && screen->history_size > 1 && !w->transient_for)
    {
      i = desktop - screen->terminals;

      assert (i >= 0 && i < TERMINAL_COUNT);

      if (i == screen->history[screen->history_size - 1])
        {
          --screen->history_size;
          set_active_terminal(screen, screen->history[screen->history_size - 1], CurrentTime);
        }

      /* If desktop is inside the history stack, remove it */

      for (j = 0; screen->history_size > 1 && j < screen->history_size; )
        {
          if (screen->history[j] == i)
            {
              --screen->history_size;
              memmove(screen->history + j, screen->history + j + 1, sizeof(*screen->history) * (screen->history_size - j));
            }
          else
            ++j;
        }
    }

  if(desktop)
    {
      for(i = 0; i < ARRAY_COUNT(&windows); ++i)
        if(ARRAY_GET(&windows, i).desktop == desktop)
          return;

      if(i == ARRAY_COUNT(&windows))
        enter_menu_mode(screen, desktop);

      if(screen->at == desktop)
        {
          set_focus(screen, desktop, CurrentTime);
          clear();
        }
    }
}

struct window*
new_window(Window window, XCreateWindowEvent* cwe)
{
  struct window new_window;
  unsigned int i;

  for(i = 0; i < screen_count; ++i)
    {
      if(window == screens[i].window)
        break;
    }

  if(i != screen_count)
    return 0;

  memset(&new_window, 0, sizeof(new_window));

  new_window.xwindow = window;
  new_window.x = cwe->x;
  new_window.y = cwe->y;
  new_window.width = cwe->width;
  new_window.height = cwe->height;

  ARRAY_ADD(&windows, new_window);

  return &ARRAY_GET(&windows, ARRAY_COUNT(&windows) - 1);
}

extern struct tree* config;

int main(int argc, char** argv)
{
  int i, j;
  int result;

  setlocale(LC_ALL, "en_US.UTF-8");

  if(!getenv("DISPLAY"))
  {
    fprintf(stderr, "DISPLAY variable not set.\n");

    return EXIT_FAILURE;
  }

  chdir(getenv("HOME"));

  config = tree_load_cfg(".cantera/config");

  inotify_fd = inotify_init1(IN_CLOEXEC);

  if(inotify_fd != -1)
    {
      if(-1 == inotify_add_watch(inotify_fd, ".cantera", IN_ALL_EVENTS | IN_ONLYDIR))
        fprintf(stderr, "inotify_add_watch failed: %s\n", strerror(errno));
    }

  mkdir(".cantera", 0777);
  mkdir(".cantera/commands", 0777);
  mkdir(".cantera/file-commands", 0777);
  mkdir(".cantera/filemanager", 0777);

  signal(SIGTERM, sighandler);
  signal(SIGIO, sighandler);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

  setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin:/usr/games:~/bin", 1);
  setenv("TERM", "xterm", 1);

  x11_connect(getenv("DISPLAY"));

  if(!x11_connected)
    return EXIT_FAILURE;

  for(j = 0; j < screen_count; ++j)
    {
      for(i = 0; i < TERMINAL_COUNT; ++i)
        {
          char buf[32];
          const char* command;

          screens[j].active_terminal = i;
          screens[j].at = &screens[j].terminals[i];

          if(i < 24)
            {
              if(!j)
                {
                  sprintf(buf,
                          "auto-launch.%s-f%d", (i < 12) ? "ctrl" : "super",
                          (i % 12) + 1);
                }
              else
                {
                  sprintf(buf,
                          "auto-launch.%s-f%d.%u", (i < 12) ? "ctrl" : "super",
                          (i % 12) + 1, j);
                }

              if(0 != (command = tree_get_string_default(config, buf, 0)))
                launch(command, 0);
            }

          screens[j].at->mode = mode_menu;
          screens[j].at->return_mode = mode_menu;
        }

      screens[j].active_terminal = 0;
      screens[j].at = &screens[j].terminals[0];

      history_reset(&screens[j], 0);
    }

  set_focus(current_screen, current_screen->at, CurrentTime);

  while(!done)
  {
    pid_t pid;
    int status;
    int maxfd = xfd;
    fd_set readset;
    fd_set writeset;
    struct timeval timeout;

    while(0 < (pid = waitpid(-1, &status, WNOHANG)))
      ;

    if(x11_connected && XPending(display))
      goto process_events;

    FD_ZERO(&readset);
    FD_ZERO(&writeset);

    if(x11_connected)
    {
      FD_SET(xfd, &readset);

      if(xfd > maxfd)
        maxfd = xfd;
    }

    if(inotify_fd != -1)
      {
        FD_SET(inotify_fd, &readset);

        if(inotify_fd > maxfd)
          maxfd = inotify_fd;
      }

    gettimeofday(&timeout, 0);
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000000 - timeout.tv_usec;

    result = select(maxfd + 1, &readset, &writeset, 0, &timeout);

    if(result < 0)
    {
      FD_ZERO(&writeset);
      FD_ZERO(&readset);
    }

    if(result == 0)
      clear();

    if(inotify_fd != -1 && FD_ISSET(inotify_fd, &readset))
      {
        struct inotify_event* ev;
        char* buf;
        size_t size;
        int available = 0;

        result = ioctl(inotify_fd, FIONREAD, &available);

        if(available > 0)
          {
            buf = malloc(available);

            result = read(inotify_fd, buf, available);

            if(result < 0)
              {
                fprintf(stderr, "Read error from inotify file descriptor: %s\n",
                        strerror(errno));

                close(inotify_fd);

                inotify_fd = -1;
              }
            else if(result < sizeof(struct inotify_event))
              {
                fprintf(stderr, "Short read from inotify\n");

                close(inotify_fd);

                inotify_fd =-1;
              }
            else
              {
                ev = (struct inotify_event*) buf;

                while(available)
                  {
                    size = sizeof(struct inotify_event) + ev->len;

                    if(size > available)
                      {
                        fprintf(stderr, "Corrupt data in inotify stream\n");

                        close(inotify_fd);

                        inotify_fd = -1;

                        break;
                      }

                    if(!strcmp(ev->name, "config")
                       && (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)))
                      {
                        tree_destroy(config);
                        config = tree_load_cfg(".cantera/config");
                      }

                    available -= size;
                    ev = (struct inotify_event*) ((char*) ev + size);
                  }
              }

            free(buf);
          }
      }

    if(x11_connected && FD_ISSET(xfd, &readset))
    {
      XEvent event;

process_events:

      while(XPending(display))
      {
        XNextEvent(display, &event);

        switch(event.type)
        {
        case KeyPress:

          if(!XFilterEvent(&event, event.xkey.window))
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

            if(key_sym == XK_Control_L || key_sym == XK_Control_R)
              ctrl_pressed = 1;

            if(key_sym == XK_Super_L || key_sym == XK_Super_R)
              super_pressed = 1;

            if(key_sym == XK_Alt_L || key_sym == XK_Alt_R)
              mod1_pressed = 1;

            /* Logitech cordless keyboard (S510) */

            /* right side keys */
            if(event.xkey.keycode == 129)
              run_command(-1, "music", 0);
            else if(event.xkey.keycode == 162)
              run_command(-1, "play_pause", 0);
            else if(event.xkey.keycode == 164)
              run_command(-1, "stop", 0);
            else if(event.xkey.keycode == 153)
              run_command(-1, "next", 0);
            else if(event.xkey.keycode == 144)
              run_command(-1, "previous", 0);
            else if(event.xkey.keycode == 176)
              run_command(-1, "increase-sound-volume", 0);
            else if(event.xkey.keycode == 174)
              run_command(-1, "decrease-sound-volume", 0);
            else if(event.xkey.keycode == 160)
              run_command(-1, "toggle-mute", 0);
            /* left side keys */
            else if(event.xkey.keycode == 223)
              run_command(-1, "standby", 0);
            else if(event.xkey.keycode == 130)
              run_command(-1, "home", 0);
            /* other */
            else if(ctrl_pressed && event.xkey.keycode == 110)
              run_command(-1, "coffee", 0);
            else if(key_sym >= 'a' && key_sym <= 'z' && super_pressed)
              {
                char key[10];
                const char* command;

                sprintf(key, "hotkey.%c", (int) key_sym);

                command = tree_get_string_default(config, key, 0);

                if(command)
                  launch(command, event.xkey.time);
              }
            else if((super_pressed ^ ctrl_pressed) && key_sym >= XK_F1 && key_sym <= XK_F12)
            {
              unsigned int new_terminal;

              new_terminal = key_sym - XK_F1;

              if(super_pressed)
                new_terminal += 12;

              if(new_terminal != current_screen->active_terminal)
              {
                  history_reset(current_screen, new_terminal);
                  set_active_terminal(current_screen, new_terminal, event.xkey.time);
              }
            }
            else if((key_sym == XK_q || key_sym == XK_Q) && (ctrl_pressed && mod1_pressed))
            {
              exit(EXIT_SUCCESS);
            }
            else if(super_pressed && (mod1_pressed ^ ctrl_pressed))
            {
              int new_terminal;
              int direction = 0;

              if(key_sym == XK_Right)
                direction = 1;
              else if(key_sym == XK_Left)
                direction = -1;
              else if(key_sym == XK_Down)
                direction = TERMINAL_COUNT / 2;
              else if(key_sym == XK_Up)
                direction = -TERMINAL_COUNT / 2;

              if(direction)
              {
                new_terminal = (TERMINAL_COUNT + current_screen->active_terminal + direction) % TERMINAL_COUNT;

                if(ctrl_pressed)
                {
                  swap_terminals(new_terminal, current_screen->active_terminal);

                  current_screen->history[current_screen->history_size - 1] = new_terminal;

                  current_screen->active_terminal = new_terminal;
                  current_screen->at = &current_screen->terminals[current_screen->active_terminal];
                }
                else
                {
                    history_reset(current_screen, new_terminal);
                  set_active_terminal(current_screen, new_terminal, event.xkey.time);
                }
              }

              create_terminal_list_popup();
            }
            else if(super_pressed && key_sym >= XK_1 && key_sym <= XK_9)
            {
              unsigned int screen = key_sym - XK_1;

              if(screen >= screen_count)
                break;

              current_screen = &screens[screen];

              set_focus(current_screen, current_screen->at, event.xkey.time);
            }
            else if(ctrl_pressed && mod1_pressed && (key_sym == XK_Escape))
            {
              launch("xkill", event.xkey.time);
            }
            else if(mod1_pressed && key_sym == XK_F4)
            {
              switch(current_screen->at->mode)
              {
              case mode_x11:

                {
                  XClientMessageEvent cme;
                  Window tmp_window;

                  if(0 != (tmp_window = find_xwindow(current_screen->at)))
                    {
                      cme.type = ClientMessage;
                      cme.send_event = True;
                      cme.display = display;
                      cme.window = tmp_window;
                      cme.message_type = xa_wm_protocols;
                      cme.format = 32;
                      cme.data.l[0] = xa_wm_delete_window;
                      cme.data.l[1] = event.xkey.time;

                      XSendEvent(display, tmp_window, False, 0, (XEvent*) &cme);
                    }
                }

                break;

              default:;
              }
            }
            else if(current_screen->at->mode == mode_x11)
            {
            }
            else if(current_screen->at->mode == mode_menu)
            {
              if(0 != menu_handle_char(current_screen, text[0]))
                menu_keypress(current_screen, key_sym, text, len, event.xkey.time);
            }
          }

          clear();

          break;

        case KeyRelease:

          {
            KeySym key_sym;

            key_sym = XLookupKeysym(&event.xkey, 0);

            ctrl_pressed = (event.xkey.state & ControlMask);
            mod1_pressed = (event.xkey.state & Mod1Mask);
            super_pressed = (event.xkey.state & Mod4Mask);
            shift_pressed = (event.xkey.state & ShiftMask);

            if(key_sym == XK_Control_L || key_sym == XK_Control_R)
              ctrl_pressed = 0;

            if(key_sym == XK_Super_L || key_sym == XK_Super_R)
              super_pressed = 0;

            if(key_sym == XK_Alt_L || key_sym == XK_Alt_R)
              mod1_pressed = 0;

            if(!super_pressed || !(mod1_pressed ^ ctrl_pressed))
              destroy_terminal_list_popup();
          }

          break;

        case DestroyNotify:

          window_gone(event.xdestroywindow.window);

          break;

        case UnmapNotify:

            {
              struct window* w;
              XEvent destroy_event;

              /* Window is probably destroyed, so we check that first */
              while(XCheckTypedWindowEvent(display, root_window, DestroyNotify,
                                           &destroy_event))
                {
                  window_gone(destroy_event.xdestroywindow.window);
                }

              w = find_window(event.xunmap.window);

              if(!w)
                break;

              if(w->screen && w->desktop == w->screen->at)
                set_focus(w->screen, w->screen->at, CurrentTime);
            }

          break;

        case ConfigureNotify:

            {
              struct window* w;

              w = find_window(event.xconfigure.window);

              if(!w)
                break;

              w->x = event.xconfigure.x;
              w->y = event.xconfigure.y;
              w->width = event.xconfigure.width;
              w->height = event.xconfigure.height;
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

            while(XCheckTypedWindowEvent(display, event.xexpose.window, Expose, &event))
            {
              if(event.xexpose.x < minx) minx = event.xexpose.x;
              if(event.xexpose.y < miny) miny = event.xexpose.y;
              if(event.xexpose.x + event.xexpose.width > maxx) maxx = event.xexpose.x + event.xexpose.width;
              if(event.xexpose.y + event.xexpose.height > maxy) maxy = event.xexpose.y + event.xexpose.height;
            }

            paint(event.xexpose.window, minx, miny, maxx - minx, maxy - miny);
          }

          break;

        case CreateNotify:

          {
            if(event.xcreatewindow.override_redirect)
              break;

            new_window(event.xcreatewindow.window, &event.xcreatewindow);
          }

          break;

        case ConfigureRequest:

            {
              XWindowChanges wc;
              XConfigureEvent ce;
              XConfigureRequestEvent* request = &event.xconfigurerequest;
              int mask;
              struct window* w;

              mask = request->value_mask;

              memset(&wc, 0, sizeof(wc));

              w = find_window(request->window);

              if(!w)
                break;

              XWindowAttributes attr;
              XGetWindowAttributes(display, w->xwindow, &attr);

              connect_transient(w);

              mask = request->value_mask;
              wc.sibling = request->above;
              wc.stack_mode = request->detail;
              wc.x = request->x;
              wc.y = request->y;
              wc.width = request->width;
              wc.height = request->height;

              if(!(mask & CWX))
                wc.x = w->x;

              if(!(mask & CWY))
                wc.y = w->y;

              if(!(mask & CWWidth))
                wc.width = w->width;

              if(!(mask & CWHeight))
                wc.height = w->height;

              if(screen_count == 1)
                w->screen = &screens[0];

              if(!w->transient_for && w->screen)
                {
                  wc.x = w->screen->x_org;
                  wc.y = w->screen->y_org;
                  wc.width = w->screen->width;
                  wc.height = w->screen->height;
                }

              mask |= CWX | CWY | CWWidth | CWHeight;

              XConfigureWindow(display, request->window, mask, &wc);

              memset(&ce, 0, sizeof(ce));
              ce.type = ConfigureNotify;
              ce.display = display;
              ce.event = request->window;
              ce.window = request->window;

              ce.x = wc.x;
              ce.y = wc.y;
              ce.width = wc.width;
              ce.height = wc.height;

              XSendEvent(display, request->window, False, StructureNotifyMask, (XEvent*) &ce);

              w->x = wc.x;
              w->y = wc.y;
              w->width = wc.width;
              w->height = wc.height;
            }

          break;

        case MapRequest:

          {
            struct window* w;
            int pid;

            w = find_window(event.xmaprequest.window);

            if(!w)
              break;

            connect_transient(w);

            if(!w->desktop
               && !w->transient_for
               && 0 == get_int_property(w->xwindow, xa_net_wm_pid, &pid))
              {
                pid = getsid(pid);

                if(0 == find_pid(pid, &w->desktop, &w->screen))
                  {
                    if(w->desktop->mode != mode_menu)
                      {
                        w->screen = 0;
                        w->desktop = 0;
                      }
                    else
                      {
                        w->desktop->mode = mode_x11;
                        clear();
                      }
                  }
              }

            if(!w->desktop)
            {
              unsigned int new_terminal;

              new_terminal = first_available_terminal(current_screen);

              w->screen = current_screen;
              w->desktop = &current_screen->terminals[new_terminal];

              memset(w->desktop, 0, sizeof(*w->desktop));
              w->desktop->mode = mode_x11;

              history_add(current_screen, new_terminal);
              set_active_terminal(current_screen, new_terminal, CurrentTime);
            }

            if(!w->transient_for
               && (w->x != w->screen->x_org
                   || w->y != w->screen->y_org
                   || w->width != w->screen->width
                   || w->height != w->screen->height))
              {
                XWindowChanges wc;

                w->x = wc.x = w->screen->x_org;
                w->y = wc.y = w->screen->y_org;
                w->width = wc.width = w->screen->width;
                w->height = wc.height = w->screen->height;

                XConfigureWindow(display, w->xwindow, CWX | CWY | CWWidth | CWHeight, &wc);
              }

            grab_thumbnail(w);

            if(w->desktop == w->screen->at)
            {
              set_map_state(w->xwindow, 1);
              XMapRaised(display, w->xwindow);
              XSetInputFocus(display, w->xwindow, RevertToPointerRoot, CurrentTime);
            }
          }

          break;
        }
      }
    }

    if(x11_connected && terminal_list_popup)
      paint_terminal_list_popup();
  }

  return EXIT_SUCCESS;
}

void run_command(int fd, const char* command, const char* arg)
{
  char path[4096];
  sprintf(path, ".cantera/commands/%s", command);

  if(-1 == access(path, X_OK))
    sprintf(path, PKGDATADIR "/commands/%s", command);

  if(-1 == access(path, X_OK))
    return;

  if(!fork())
  {
    char* args[3];

    if(fd != -1)
      dup2(fd, 1);

    args[0] = path;
    args[1] = (char*) arg;
    args[2] = 0;

    execve(args[0], args, environ);

    exit(EXIT_FAILURE);
  }
}

void init_ximage(XImage* image, int width, int height, void* data)
{
  memset(image, 0, sizeof(XImage));
  image->width = width;
  image->height = height;
  image->format = ZPixmap;
  image->data = (char*) data;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  image->byte_order = LSBFirst;
  image->bitmap_bit_order = LSBFirst;
#else
  image->byte_order = MSBFirst;
  image->bitmap_bit_order = MSBFirst;
#endif
  image->bitmap_unit = 32;
  image->bitmap_pad = 32;
  image->depth = 32;
  image->bytes_per_line = width * 4;
  image->bits_per_pixel = 32;
}

// vim: ts=2 sw=2 et sts=2
