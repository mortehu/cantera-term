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
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>

#include "common.h"
#include "font.h"
#include "globals.h"
#include "menu.h"

#define PARTIAL_REPAINT 1

extern char** environ;

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
Picture picgradients[256];

#ifndef TERMINAL
XineramaScreenInfo* screens;
Window* screen_windows;
terminal** screen_terms;
int screen_count = 0;

int terminal_list_width;
int terminal_list_height;
Window terminal_list_popup = 0;

Window player_window = 0;
int player_window_width = 640;
int player_window_height = 20;
#endif

int window_width, window_height;

struct inferior
{
  Window window;
  Damage damage;
  int x, y;
  int width, height;
};

struct inferior inferiors[128];
int inferior_count = 0;

void run_command(int fd, const char* command, const char* arg);
void init_ximage(XImage* image, int width, int height, void* data);

void add_inferior(Window window)
{
  int i = inferior_count++;

  XWindowAttributes attr;

  XGetWindowAttributes(display, window, &attr);

  inferiors[i].window = window;
  inferiors[i].damage = XDamageCreate(display, window, XDamageReportNonEmpty);
  inferiors[i].x = attr.x;
  inferiors[i].y = attr.y;
  inferiors[i].width = attr.width;
  inferiors[i].height = attr.height;
}

void remove_inferior(int i)
{
  XDamageDestroy(display, inferiors[i].damage);
  memmove(&inferiors[i], &inferiors[inferior_count - 1], sizeof(struct inferior));
  --inferior_count;
}

Display* display;
int damage_eventbase;
int damage_errorbase;
int screenidx;
Screen* screen;
Visual* visual;
XVisualInfo visual_template;
XVisualInfo* visual_info;
Window root_window;
XWindowAttributes root_window_attr;
XSetWindowAttributes window_attr;
Window window;
Atom prop_paste;
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
GC gc;
XRenderPictFormat* xrenderpictformat;
XRenderPictFormat* argb32pictformat;
XRenderPictFormat* a8pictformat;
GlyphSet alpha_glyphs[2];
int cols;
int rows;
XIM xim = 0;
XIC xic;
#define TERMINAL_COUNT 24
terminal terminals[TERMINAL_COUNT];
int xfd;
int active_terminal = 0;
terminal* at = &terminals[0];
int last_set_terminal = 0;
int ctrl_pressed = 0;
int mod1_pressed = 0;
int super_pressed = 0;
int shift_pressed = 0;
int button1_pressed = 0;
struct timeval lastpaint = { 0, 0 };
unsigned char* paste_buffer = 0;
unsigned long paste_offset;
unsigned long paste_length;
int paste_terminal;
int paste_is_x;
Picture root_picture;
Picture root_buffer;

static void mark_terminal_dirty();

#define my_isprint(c) (isprint((c)) || ((c) >= 0x80))

static void swap_terminals(int a, int b)
{
  terminal tmp;

  tmp = terminals[a];
  terminals[a] = terminals[b];
  terminals[b] = tmp;
}

static void clear_screen()
{
  XRenderFillRectangle(display, PictOpSrc, root_picture, &xrpalette[0],
                       0, 0, window_width, window_height);
}

static void paint(int x, int y, int width, int height)
{
  int minx = x;
  int miny = y;
  int maxx = x + width;
  int maxy = y + height;

  XFixesSetPictureClipRegion(display, root_buffer, 0, 0, None);

  if(at->mode == mode_menu)
    menu_draw();
  else if(at->mode == mode_x11)
  {
    XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[0],
        0, 0, window_width, window_height);
  }

  if(root_buffer != root_picture)
  {
    XRenderComposite(display, PictOpSrc, root_buffer, None, root_picture,
                     minx, miny,
                     0, 0,
                     minx, miny, maxx - minx, maxy - miny);
  }
}

static int first_available_terminal()
{
  int i;

  if(at->mode == mode_menu)
    return active_terminal;

  for(i = 0; i < TERMINAL_COUNT; ++i)
  {
    if(terminals[i].mode == mode_menu)
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

static void mark_terminal_dirty()
{
  at->dirty = 2;
}

static void grab_thumbnail()
{
  Pixmap pmap;
  int thumb_width, thumb_height;

  menu_thumbnail_dimensions(&thumb_width, &thumb_height, 0);

  if(at->mode == mode_menu)
  {
    if(at->thumbnail)
    {
      XRenderFreePicture(display, at->thumbnail);
      at->thumbnail = 0;
    }

    return;
  }
  else if(at->mode == mode_x11)
  {
    Atom type;
    int format;
    int result;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char* prop;

    if(at->thumbnail)
      return;

    result = XGetWindowProperty(display, at->window, xa_net_wm_icon, 0, 0, False,
                                AnyPropertyType, &type, &format, &nitems, &bytes_after,
                                &prop);

    if(result != Success)
    {
      fprintf(stderr, "No success\n");
      return;
    }

    if(prop)
      XFree(prop);

    result = XGetWindowProperty(display, at->window, xa_net_wm_icon, 0, bytes_after, False,
                                AnyPropertyType, &type, &format, &nitems, &bytes_after,
                                &prop);

    if(prop)
    {
      unsigned int* buf = (unsigned int*) prop;
      unsigned int width = buf[0];
      unsigned int height = buf[1];

      XImage temp_image;
      init_ximage(&temp_image, width, height, (char*) prop + 8);

      Pixmap temp_pixmap = XCreatePixmap(display, window, thumb_width, thumb_height, 32);

      GC tmp_gc = XCreateGC(display, temp_pixmap, 0, 0);
      XFillRectangle(display, temp_pixmap, tmp_gc, 0, 0, thumb_width, thumb_height);
      XPutImage(display, temp_pixmap, tmp_gc, &temp_image, 0, 0,
                thumb_width / 2 - width / 2, thumb_height / 2 - height / 2, width, height);
      XFreeGC(display, tmp_gc);

      at->thumbnail = XRenderCreatePicture(display, temp_pixmap, XRenderFindStandardFormat(display, PictStandardARGB32), 0, 0);

      XFreePixmap(display, temp_pixmap);

      XFree(prop);
    }

    return;
  }

  if(!at->thumbnail)
  {
    pmap = XCreatePixmap(display, root_window, window_width / 16, window_height / 16, xrenderpictformat->depth);

    at->thumbnail = XRenderCreatePicture(display, pmap, xrenderpictformat, 0, 0);

    XFreePixmap(display, pmap);
  }

  XTransform xform =
  {
    {
      { XDoubleToFixed(1.0), XDoubleToFixed(0.0), XDoubleToFixed(0.0) },
      { XDoubleToFixed(0.0), XDoubleToFixed(1.0), XDoubleToFixed(0.0) },
      { XDoubleToFixed(0.0), XDoubleToFixed(0.0), XDoubleToFixed(1.0 / 16.0) }
    }
  };

  XRenderSetPictureTransform(display, root_buffer, &xform);
  XRenderSetPictureFilter(display, root_buffer, FilterBilinear, 0, 0);
  XRenderComposite(display, PictOpSrc, root_buffer, None, at->thumbnail, 0, 0, 0, 0, 0, 0, window_width, window_height);

  xform.matrix[2][2] = XDoubleToFixed(1.0);
  XRenderSetPictureTransform(display, root_buffer, &xform);
  XRenderSetPictureFilter(display, root_buffer, FilterNearest, 0, 0);
}

static void paint_terminal_list_popup();

static void create_terminal_list_popup()
{
  int thumb_width, thumb_height, thumb_margin;
  if(at->mode == mode_menu || terminal_list_popup)
    return;

  grab_thumbnail();

  menu_thumbnail_dimensions(&thumb_width, &thumb_height, &thumb_margin);

  terminal_list_width = window_width;
  terminal_list_height = 2 * thumb_height + 3 * thumb_margin + yskips[SMALL];

  window_attr.override_redirect = True;

  terminal_list_popup
    = XCreateWindow(display, RootWindow(display, screenidx),
                    screens[0].x_org,
                    screens[0].y_org + window_height - terminal_list_height,
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

  menu_draw_desktops(buffer, terminal_list_height);

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

static void set_focus(terminal* t)
{
  if(t->mode == mode_x11)
  {
    struct transient* trans = t->transients;

    Window focus = t->window;
    clear_screen();
    XMapWindow(display, t->window);
    set_map_state(t->window, 1);

    while(trans)
    {
      XMapRaised(display, trans->window);
      focus = trans->window;
      trans = trans->next;
    }

    XSetInputFocus(display, focus, RevertToPointerRoot, CurrentTime);
  }
  else
  {
    XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);

    XSetWindowAttributes window_attr;
    if(t->mode == mode_menu)
      window_attr.cursor = menu_cursor;
    else
      window_attr.cursor = other_cursor;

    XChangeWindowAttributes(display, window, CWCursor, &window_attr);
  }
}

static void configure_player_window()
{
  XWindowChanges wc;

  const int margin_y = window_height * 75 / 1000;
  const int thumb_height = window_height / 16;
  const int thumb_margin = 6;

  terminal_list_width = window_width;
  terminal_list_height = 2 * thumb_height + 5 * thumb_margin + 2 * yskips[SMALL];

  wc.x = screens[0].x_org + window_width - player_window_width - window_width * 75 / 1000;
  wc.y = screens[0].y_org + window_height - terminal_list_height - margin_y + thumb_margin - player_window_height;
  wc.width = player_window_width;
  wc.height = player_window_height;

  XConfigureWindow(display, player_window, CWX | CWY | CWWidth | CWHeight, &wc);
}

static void show_player_window()
{
  if(player_window)
  {
    configure_player_window();

    XMapWindow(display, player_window);
  }
}

static void set_active_terminal(int terminal)
{
  int i;

  if(terminal != active_terminal && !super_pressed)
    grab_thumbnail();

  set_focus(&terminals[terminal]);

  if(terminals[terminal].mode != mode_menu)
  {
    if(player_window)
      XUnmapWindow(display, player_window);
  }

  if(terminal == active_terminal)
    return;

  if(at->window)
  {
    for(i = 1; i < screen_count; ++i)
    {
      if(screen_windows[i] == at->window)
        break;
    }

    if(i == screen_count)
    {
      struct transient* t = at->transients;

      XUnmapWindow(display, at->window);
      set_map_state(at->window, 0);

      while(t)
      {
        XUnmapWindow(display, t->window);
        set_map_state(t->window, 0);

        t = t->next;
      }
    }

    XUnmapWindow(display, at->window);
    set_map_state(at->window, 0);
  }

  active_terminal = terminal;
  at = &terminals[active_terminal];

  if(at->mode == mode_menu)
  {
    destroy_terminal_list_popup();
    show_player_window();
  }

  at->dirty = 1;
}

int launch(const char* command)
{
  pid_t pid = fork();

  if(pid == -1)
    return 0;

  if(!pid)
  {
    char* args[5];
    char histfile[32];

    sprintf(histfile, ".potty/bash-history-%02d", active_terminal);
    setenv("HISTFILE", histfile, 1);

    args[0] = "/bin/sh";
    args[1] = "-c";
    args[2] = "exec";
    args[3] = (char*) command;
    args[4] = 0;

    execve(args[0], args, environ);
  }

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

  /*
  XGrabKey(display, XKeysymToKeycode(display, XK_Control_L), Mod1Mask, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Control_R), Mod1Mask, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Control_L), Mod2Mask, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Control_R), Mod2Mask, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Alt_L), ControlMask | Mod1Mask, root_window, False, GrabModeAsync, GrabModeAsync);
  XGrabKey(display, XKeysymToKeycode(display, XK_Alt_R), ControlMask | Mod1Mask, root_window, False, GrabModeAsync, GrabModeAsync);
  */
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

  fprintf(stderr, "X error: %s (request: %d, minor: %d)  ID: %08X\n", errorbuf,
          error->request_code, error->minor_code, (unsigned int) error->resourceid);

  // abort();

  return 0;
}

static int xerror_discarder(Display* display, XErrorEvent* error)
{
  return 0;
}

static int x11_connected = 0;

static void x11_connect(const char* display_name)
{
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

  if(XineramaQueryExtension(display, &i, &i))
  {
    if(XineramaIsActive(display))
      screens = XineramaQueryScreens(display, &screen_count);
  }

  if(!screen_count)
  {
    screen_count = 1;
    screens = malloc(sizeof(XineramaScreenInfo) * 1);
    screens[0].x_org = 0;
    screens[0].y_org = 0;
    screens[0].width = root_window_attr.width;
    screens[0].height = root_window_attr.height;
  }

  screen_windows = malloc(sizeof(Window) * screen_count);
  screen_terms = malloc(sizeof(terminal*) * screen_count);
  memset(screen_windows, 0, sizeof(Window) * screen_count);
  memset(screen_terms, 0, sizeof(terminal*) * screen_count);

  screenidx = DefaultScreen(display);
  screen = DefaultScreenOfDisplay(display);
  visual = DefaultVisual(display, screenidx);
  visual_info = XGetVisualInfo(display, VisualNoMask, &visual_template, &nitems);

  memset(&window_attr, 0, sizeof(window_attr));
  window_width = screens[0].width;
  window_height = screens[0].height;

  window_attr.colormap = DefaultColormap(display, 0);
  window_attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask;
  window_attr.override_redirect = True;

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

  window_attr.cursor = menu_cursor;

  window = XCreateWindow(display, root_window, screens[0].x_org, screens[0].y_org, window_width, window_height, 0, visual_info->depth, InputOutput, visual, CWOverrideRedirect | CWColormap | CWEventMask | CWCursor, &window_attr);
  XMapWindow(display, window);

  grab_keys();

  prop_paste = XInternAtom(display, "POTTY_PASTE", False);
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

  a8pictformat = XRenderFindStandardFormat(display, PictStandardA8);

  gc = XCreateGC(display, window, 0, 0);

  for(i = 0; i < sizeof(palette) / sizeof(palette[0]); ++i)
  {
    Pixmap pmap;
    XRenderPictureAttributes attr;

    xrpalette[i].alpha = ((palette[i] & 0xff000000) >> 24) * 0x0101;
    xrpalette[i].red = ((palette[i] & 0xff0000) >> 16) * 0x0101;
    xrpalette[i].green = ((palette[i] & 0x00ff00) >> 8) * 0x0101;
    xrpalette[i].blue = (palette[i] & 0x0000ff) * 0x0101;

    pmap = XCreatePixmap(display, window, 1, 1, xrenderpictformat->depth);

    memset(&attr, 0, sizeof(attr));
    attr.repeat = True;

    picpalette[i] = XRenderCreatePicture(display, pmap, xrenderpictformat, CPRepeat, &attr);

    XFreePixmap(display, pmap);

    XRenderFillRectangle(display, PictOpSrc, picpalette[i],
                         &xrpalette[i], 0, 0, 1, 1);
  }

  for(i = 0; i < 256; ++i)
  {
    Pixmap pmap;
    XRenderPictureAttributes attr;

    XRenderColor color;
    color.alpha = i * 0x0101;
    color.red = 0xffff;
    color.green = 0xffff;
    color.blue = 0xffff;

    pmap = XCreatePixmap(display, window, 1, 1, a8pictformat->depth);

    memset(&attr, 0, sizeof(attr));
    attr.repeat = True;

    picgradients[i] = XRenderCreatePicture(display, pmap, a8pictformat, CPRepeat, &attr);

    XFreePixmap(display, pmap);

    XRenderFillRectangle(display, PictOpSrc, picgradients[i], &color, 0, 0, 1, 1);
  }

  alpha_glyphs[0] = XRenderCreateGlyphSet(display, a8pictformat);
  alpha_glyphs[1] = XRenderCreateGlyphSet(display, a8pictformat);

  if(!alpha_glyphs[0] || !alpha_glyphs[1])
  {
    fprintf(stderr, "XRenderCreateGlyphSet failed.\n");

    return;
  }

  font_init();

  XRenderPictureAttributes pa;
  pa.subwindow_mode = IncludeInferiors;

  root_picture = XRenderCreatePicture(display, window, xrenderpictformat, CPSubwindowMode, &pa);

  {
    Pixmap pmap;

    pmap = XCreatePixmap(display, window, window_width, window_height, visual_info->depth);
    root_buffer = XRenderCreatePicture(display, pmap, xrenderpictformat, 0, 0);

    if(root_buffer == None)
    {
      fprintf(stderr, "Failed to create root buffer\n");

      return;
    }

    XFreePixmap(display, pmap);
  }

  menu_init();

  cols = window_width / xskips[0];
  rows = window_height / yskips[0];

  xfd = ConnectionNumber(display);

  XSynchronize(display, False);

  clear_screen();
  mark_terminal_dirty();

  x11_connected = 1;
}

static int done;

static void sighandler(int signal)
{
  fprintf(stderr, "Got signal %d\n", signal);

  exit(EXIT_SUCCESS);
}

static void mark_dirty(int termidx, int x, int y, int width, int height, const char* reason)
{
  terminal* t = &terminals[termidx];
  XRectangle rect = { x, y, width, height };

  if(!t->region)
  {
    t->region = XFixesCreateRegion(display, 0, 0);

    XFixesSetRegion(display, t->region, &rect, 1);
  }
  else
  {
    XserverRegion tmpregion = XFixesCreateRegion(display, 0, 0);
    XFixesSetRegion(display, tmpregion, &rect, 1);
    XFixesUnionRegion(display, t->region, t->region, tmpregion);
    XFixesDestroyRegion(display, tmpregion);
  }

  t->dirty |= 1;
}

static void enter_menu_mode(int termidx)
{
  terminal* t = &terminals[termidx];
  struct transient* transient = t->transients;

  while(transient)
  {
    struct transient* next = transient->next;
    free(transient);
    transient = next;
  }

  /* XXX: Free first */
  t->mode = mode_menu;
  t->window = 0;
  t->transients = 0;
  t->damage = 0;
  t->picture = 0;
  t->region = 0;
  t->dirty |= 1;

  if(t->picture)
  {
    XRenderFreePicture(display, t->picture);
    XDamageDestroy(display, t->damage);
    t->picture = 0;
  }

  if(t->thumbnail)
  {
    XRenderFreePicture(display, t->thumbnail);
    t->thumbnail = 0;
  }

  if(t == at)
  {
    show_player_window();
    destroy_terminal_list_popup();
  }
}

static int find_window(Window w, terminal** term, struct transient** trans)
{
  int i;

  for(i = 0; i < TERMINAL_COUNT; ++i)
  {
    if(terminals[i].window == w)
    {
      *term = &terminals[i];
      if(trans)
        *trans = 0;

      return 0;
    }

    if(!trans)
      continue;

    struct transient* t = terminals[i].transients;

    while(t)
    {
      if(t->window == w)
      {
        *term = &terminals[i];
        *trans = t;

        return 0;
      }

      t = t->next;
    }
  }

  *term = 0;
  if(trans)
    *trans = 0;

  return -1;
}

static void add_transient(terminal* term, Window window, struct transient** trans)
{
  struct transient* next = term->transients;
  term->transients = malloc(sizeof(struct transient));
  term->transients->window = window;
  term->transients->next = next;
  term->transients->x = -1;
  term->transients->y = -1;
  term->transients->width = -1;
  term->transients->height = -1;

  if(trans)
    *trans = term->transients;
}

static void get_transient_for(Window w, Window* transient_for)
{
  XSync(display, False);
  XSetErrorHandler(xerror_discarder);
  XGetTransientForHint(display, w, transient_for);
  XSync(display, False);
  XSetErrorHandler(xerror_handler);
}

#ifndef TERMINAL
int is_player_window(Window w)
{
  unsigned char* prop;
  unsigned long nitems, bytes_after;
  Atom type;
  int result, format;

  if(player_window)
    return w == player_window;

  result = XGetWindowProperty(display, w, xa_potty_play, 0, 0, False,
      AnyPropertyType, &type, &format, &nitems, &bytes_after,
      &prop);

  if(result != Success)
    return 0;

  if(!prop)
    return 0;

  XFree(prop);

  player_window = w;

  return 1;
}
#endif

int main(int argc, char** argv)
{
  int i;
  int result;
  struct timeval now;

  setlocale(LC_ALL, "en_US.UTF-8");

  if(!getenv("DISPLAY"))
  {
    fprintf(stderr, "DISPLAY variable not set.\n");

    return EXIT_FAILURE;
  }

  chdir(getenv("HOME"));

  mkdir(".potty", 0777);
  mkdir(".potty/commands", 0777);
  mkdir(".potty/file-commands", 0777);
  mkdir(".potty/filemanager", 0777);

  signal(SIGTERM, sighandler);
  signal(SIGIO, sighandler);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

#ifndef TERMINAL
  setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin:/usr/games:~/bin", 1);
#endif
  setenv("TERM", "xterm", 1);

  x11_connect(getenv("DISPLAY"));

  if(!x11_connected)
    return EXIT_FAILURE;

#ifndef TERMINAL
  for(i = 0; i < TERMINAL_COUNT; ++i)
  {
    terminals[i].mode = mode_menu;
    terminals[i].return_mode = mode_menu;
  }

#else
  {
    char* args[2];

    args[0] = "/bin/bash";
    args[1] = 0;

    init_session(at, args);
  }
#endif

  while(!done)
  {
    pid_t pid;
    int status;
    int maxfd = xfd;
    fd_set readset;
    fd_set writeset;

    while(0 < (pid = waitpid(-1, &status, WNOHANG)))
    {
#ifndef TERMINAL
      for(i = 0; i < TERMINAL_COUNT; ++i)
      {
        if(terminals[i].pid == pid)
        {
          if(terminals[i].return_mode == mode_menu)
          {
            enter_menu_mode(i);
          }

          terminals[i].mode = terminals[i].return_mode;
          terminals[i].return_mode = mode_menu;

          terminals[i].pid = 0;

          if(i == active_terminal)
            set_active_terminal(last_set_terminal);

          break;
        }
      }
#else
      if(pid == terminals[0].pid)
        return EXIT_SUCCESS;
#endif
    }

    FD_ZERO(&readset);
    FD_ZERO(&writeset);

    if(x11_connected)
    {
      FD_SET(xfd, &readset);

      if(xfd > maxfd)
        maxfd = xfd;
    }

    if(!x11_connected)
    {
      result = select(maxfd + 1, &readset, &writeset, 0, 0);
    }
    else if(at->dirty)
    {
      struct timeval tv;

      tv.tv_sec = 0;
      tv.tv_usec = 10000;

      result = select(maxfd + 1, &readset, &writeset, 0, &tv);
    }
    else
    {
      struct timeval tv;

      if(at->next_update.tv_sec)
      {
        int64_t timediff;
        gettimeofday(&now, 0);

        timediff = (at->next_update.tv_sec - now.tv_sec) * 1000000
                 + (at->next_update.tv_usec - now.tv_usec);

        if(timediff > 0)
        {
          tv.tv_sec = timediff / 1000000;
          tv.tv_usec = timediff % 1000000;
        }
        else
        {
          tv.tv_sec = 0;
          tv.tv_usec = 0;
        }
      }
      else
      {
        tv.tv_sec = 1;
        tv.tv_usec = 0;
      }

      result = select(maxfd + 1, &readset, &writeset, 0, &tv);
    }

    if(result < 0)
    {
      FD_ZERO(&writeset);
      FD_ZERO(&readset);
    }

    if(x11_connected && FD_ISSET(xfd, &readset))
    {
      XEvent event;

      while(XPending(display))
      {
        XNextEvent(display, &event);

#ifndef TERMINAL
        if(event.type == damage_eventbase + XDamageNotify)
        {
          XDamageNotifyEvent* e = (XDamageNotifyEvent*) &event;

          for(i = 0; i < inferior_count; ++i)
            if(inferiors[i].window == e->drawable)
              break;

          if(e->drawable == at->window || i != inferior_count)
          {
            if(!at->region)
            {
              at->region = XFixesCreateRegion(display, 0, 0);

              XDamageSubtract(display, e->damage, None, at->region);
            }
            else
            {
              XserverRegion tmpregion = XFixesCreateRegion(display, 0, 0);
              XDamageSubtract(display, e->damage, None, tmpregion);
              XFixesUnionRegion(display, at->region, at->region, tmpregion);
              XFixesDestroyRegion(display, tmpregion);
            }

            at->dirty |= 1;
          }
          else if(e->drawable == window && at->mode != mode_x11)
          {
            at->dirty |= 1;
          }
          else
          {
            XDamageSubtract(display, e->damage, None, None);
          }

          continue;
        }
#endif

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
              menu_handle_char(key_sym);
            else if((super_pressed ^ ctrl_pressed) && key_sym >= XK_F1 && key_sym <= XK_F12)
            {
              int new_terminal;

              new_terminal = key_sym - XK_F1;

              if(super_pressed)
                new_terminal += 12;

              last_set_terminal = new_terminal;

              if(new_terminal != active_terminal)
                set_active_terminal(new_terminal);
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
                new_terminal = (TERMINAL_COUNT + active_terminal + direction) % TERMINAL_COUNT;

                if(ctrl_pressed)
                {
                  swap_terminals(new_terminal, active_terminal);

                  last_set_terminal = active_terminal = new_terminal;
                  at = &terminals[active_terminal];

				  at->dirty = 1;
                }
                else
                {
                  last_set_terminal = new_terminal;
                  set_active_terminal(new_terminal);
                }
              }

              create_terminal_list_popup();
            }
            else if(super_pressed && key_sym >= XK_1 && key_sym <= XK_9)
            {
              int i;
              int screen = key_sym - XK_1;

              if(at->window && screen < screen_count && screen_windows[screen] != at->window)
              {
                XWindowChanges wc;

                if(screen_windows[screen])
                {
                  wc.x = screens[0].x_org;
                  wc.y = screens[0].y_org;
                  wc.width = screens[0].width;
                  wc.height = screens[0].height;

                  screen_terms[screen]->xscreen = 0;
                  XConfigureWindow(display, screen_windows[screen], CWX | CWY | CWWidth | CWHeight, &wc);
                  XUnmapWindow(display, screen_windows[screen]);
                }

                for(i = 0; i < screen_count; ++i)
                {
                  if(screen_windows[i] == at->window)
                  {
                    screen_windows[i] = 0;
                    screen_terms[i] = 0;
                  }
                }

                at->xscreen = screen;

                wc.x = screens[screen].x_org;
                wc.y = screens[screen].y_org;
                wc.width = screens[screen].width;
                wc.height = screens[screen].height;

                XConfigureWindow(display, at->window, CWX | CWY | CWWidth | CWHeight, &wc);

                mark_terminal_dirty();

                screen_windows[screen] = at->window;
                screen_terms[screen] = at;
              }
            }
            else if(ctrl_pressed && mod1_pressed && (key_sym == XK_Escape))
            {
              launch("exec xkill");
            }
            else if(mod1_pressed && key_sym == XK_F4)
            {
              switch(at->mode)
              {
              case mode_x11:

                {
                  XClientMessageEvent cme;

                  cme.type = ClientMessage;
                  cme.send_event = True;
                  cme.display = display;
                  cme.window = at->window;
                  cme.message_type = xa_wm_protocols;
                  cme.format = 32;
                  cme.data.l[0] = xa_wm_delete_window;
                  cme.data.l[1] = event.xkey.time;

                  XSendEvent(display, at->window, False, 0, (XEvent*) &cme);
                }

                break;

              default:;
              }
            }
            else if(at->mode == mode_x11)
            {
            }
            else if(at->mode == mode_menu)
            {
              if(0 != menu_handle_char(text[0]))
              {
                menu_keypress(key_sym, text, len);
                at->dirty |= 1;
              }
            }
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

			if(key_sym == XK_Control_L || key_sym == XK_Control_R)
              ctrl_pressed = 0;

			if(key_sym == XK_Super_L || key_sym == XK_Super_R)
              super_pressed = 0;

			if(key_sym == XK_Alt_L || key_sym == XK_Alt_R)
              mod1_pressed = 0;

            if(!super_pressed || !(mod1_pressed ^ ctrl_pressed))
              destroy_terminal_list_popup();

            if(at->mode == mode_menu)
            {
              menu_keyrelease(key_sym);
              at->dirty |= 1;
            }
          }

          break;

        case DestroyNotify:

          {
            terminal* term;
            struct transient* trans;

            if(event.xdestroywindow.window == player_window)
            {
              player_window = 0;

              break;
            }

            if(-1 == find_window(event.xdestroywindow.window, &term, &trans))
              break;

            if(!trans)
            {
              enter_menu_mode(term - terminals);
            }
            else
            {
              if(term->transients == trans)
              {
                term->transients = trans->next;
              }
              else
              {
                struct transient* prev = term->transients;

                while(prev->next != trans)
                  prev = prev->next;

                prev->next = trans->next;
              }

              free(trans);
            }
          }

          break;

        case UnmapNotify:

          if(event.xunmap.window == at->window)
          {
            enter_menu_mode(active_terminal);

            /* Return to main window after a pop-up */
            set_active_terminal(last_set_terminal);
          }
          else
          {
            for(i = 0; i < inferior_count; ++i)
            {
              if(event.xunmap.window == inferiors[i].window)
                break;
            }

            if(i == inferior_count)
              break;

            mark_dirty(active_terminal, inferiors[i].x, inferiors[i].y,
                       inferiors[i].width, inferiors[i].height, "unmap");

            remove_inferior(i);
          }

          break;

        case ConfigureNotify:

          for(i = 0; i < inferior_count; ++i)
          {
            if(event.xconfigure.window == inferiors[i].window)
            {
              mark_dirty(active_terminal, inferiors[i].x, inferiors[i].y,
                         inferiors[i].width, inferiors[i].height, "resize");

              inferiors[i].x = event.xconfigure.x;
              inferiors[i].y = event.xconfigure.y;
              inferiors[i].width = event.xconfigure.width;
              inferiors[i].height = event.xconfigure.height;

              break;
            }
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

        case CreateNotify:

          {
            XWindowChanges wc;
            XCreateWindowEvent* cwe = &event.xcreatewindow;
            Window transient_for = 0;

            get_transient_for(cwe->window, &transient_for);

            if(transient_for)
              break;

            if(!cwe->override_redirect && (cwe->x != 0 || cwe->y != 0 || cwe->width != window_width || cwe->height != window_height))
            {
              wc.x = 0;
              wc.y = 0;
              wc.width = window_width;
              wc.height = window_height;

              XConfigureWindow(display, cwe->window, CWX | CWY | CWWidth | CWHeight, &wc);
            }
          }

          break;

        case ConfigureRequest:

          {
            XWindowChanges wc;
            XConfigureEvent ce;
            XConfigureRequestEvent* request = &event.xconfigurerequest;
            Window transient_for = 0;
            terminal* term;
            struct transient* trans;
            int mask;

            if(is_player_window(event.xconfigurerequest.window))
            {
              player_window_width = event.xconfigurerequest.width;
              player_window_height = event.xconfigurerequest.height;

              if(at->mode == mode_menu)
                configure_player_window();

              break;
            }

            memset(&wc, 0, sizeof(wc));

            find_window(request->window, &term, &trans);

            if(trans)
              transient_for = term->window;
            else
            {
              get_transient_for(request->window, &transient_for);

              if(transient_for)
              {
                find_window(transient_for, &term, 0);

                if(term)
                  add_transient(term, request->window, &trans);
              }
            }

            if(trans)
            {
              int xscreen = 0;

              if(term)
                xscreen = term->xscreen;

              mask = request->value_mask;
              wc.sibling = request->above;
              wc.stack_mode = request->detail;

              if(mask & CWX) trans->x = wc.x = request->x;
              if(mask & CWY) trans->y = wc.y = request->y;
              if(mask & CWWidth) trans->width = wc.width = request->width;
              if(mask & CWHeight) trans->height = wc.height = request->height;

              if(trans->x == -1)
              {
                mask |= CWX;
                wc.x = trans->x = screens[xscreen].x_org;
              }

              if(trans->y == -1)
              {
                mask |= CWY;
                wc.y = trans->y = screens[xscreen].y_org;
              }

              if(trans->width == -1)
              {
                mask |= CWWidth;
                wc.width = trans->width = screens[xscreen].x_org + screens[xscreen].width - trans->x;
              }

              if(trans->height == -1)
              {
                mask |= CWHeight;
                wc.height = trans->height = screens[xscreen].y_org + screens[xscreen].height - trans->y;
              }
            }
            else
            {
              int xscreen = 0;
              mask = request->value_mask | CWX | CWY | CWWidth | CWHeight;

              if(term)
                xscreen = term->xscreen;

              wc.x = screens[xscreen].x_org;
              wc.y = screens[xscreen].y_org;
              wc.width = screens[xscreen].width;
              wc.height = screens[xscreen].height;
              wc.sibling = request->above;
              wc.stack_mode = request->detail;
            }

            XConfigureWindow(display, request->window, mask, &wc);

            memset(&ce, 0, sizeof(ce));
            ce.type = ConfigureNotify;
            ce.display = display;
            ce.event = request->window;
            ce.window = request->window;

            if(trans)
            {
              ce.x = trans->x;
              ce.y = trans->y;
              ce.width = trans->width;
              ce.height = trans->height;
            }
            else
            {
              ce.x = wc.x;
              ce.y = wc.y;
              ce.width = wc.width;
              ce.height = wc.height;
            }

            XSendEvent(display, request->window, False, StructureNotifyMask, (XEvent*) &ce);
          }

          break;

        case MapRequest:

          {
            terminal* term;
            struct transient* trans;

            if(is_player_window(event.xmaprequest.window))
              break;

            if(player_window)
              XUnmapWindow(display, player_window);

            find_window(event.xmaprequest.window, &term, &trans);

            if(!term)
            {
              trans = 0;

              term = &terminals[first_available_terminal()];

              term->mode = mode_x11;
              term->window = event.xmaprequest.window;
            }
            else if(!trans)
            {
              Window transient_for = 0;

              get_transient_for(event.xmaprequest.window, &transient_for);

              add_transient(term, event.xmaprequest.window, 0);
            }

            if(!trans || term != at)
              set_active_terminal(term - terminals);
            else if(trans)
            {
              XMapRaised(display, event.xmaprequest.window);
              XSetInputFocus(display, event.xmaprequest.window, RevertToPointerRoot, CurrentTime);
            }
          }

          break;
        }
      }
    }

    if(x11_connected && terminal_list_popup)
      paint_terminal_list_popup();

    gettimeofday(&now, 0);

    if(at->next_update.tv_sec)
    {
      if(now.tv_sec > at->next_update.tv_sec
      || (   now.tv_sec == at->next_update.tv_sec
          && now.tv_usec >= at->next_update.tv_usec))
      {
        at->dirty |= 1;
        at->next_update.tv_sec = 0;
        at->next_update.tv_usec = 0;
      }
    }

    if(!at->next_update.tv_sec)
    {
      if(at->mode == mode_menu)
      {
        at->next_update.tv_sec = now.tv_sec + 1;
        at->next_update.tv_usec = 0;
      }
    }

    if(x11_connected)
    {
      if(at->dirty)
      {
        at->dirty = 0;

        XClearArea(display, window, 0, 0, window_width, window_height, True);
      }

      XFlush(display);
    }
  }

  return EXIT_SUCCESS;
}

void run_command(int fd, const char* command, const char* arg)
{
  char path[4096];
  sprintf(path, ".potty/commands/%s", command);

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
