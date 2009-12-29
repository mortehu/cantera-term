/**
 * X11 Input Device Detection and Window Creation
 * Copyright (C) 2006  Morten Hustveit
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pwd.h>
#include <shadow.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/xf86vmode.h>
#include <X11/extensions/Xinerama.h>
#include <X11/keysym.h>

#include <GL/glx.h>

#include "error.h"
#include "input.h"
#include "var.h"

#define NDEBUG 1

/* AltiVec */
#undef pixel

var main_variables[] =
{
  { "fullscreen",   var_archive, var_float, 0, 1.0f },
  { "r_enable",     var_archive, var_float, 0, 1.0f },
  { "r_waitvblank", var_archive, var_float, 0, 0.0f },
  { "width",        var_archive, var_float, 0, 0.0f },
  { "height",       var_archive, var_float, 0, 0.0f },
};

extern void game_process_frame(float width, float height, double delta_time);
extern void game_init();

int program_argc;
char** program_argv;

static Display*     display = 0;
static Window       window;
static XVisualInfo* visual;
static XIM          xim;
static XIC          xic;

XineramaScreenInfo* screens;
int screen_count = 0;

static int restore;
static XF86VidModeModeInfo** mode_info;

static char* get_user_name();
static char* get_host_name();
static Bool wait_for_map_notify(Display*, XEvent* event, char* arg);

static device_state device_states[2];

static void exithandler(void)
{
  if(restore)
    XF86VidModeSwitchToMode(display, visual->screen, mode_info[0]);

  XFlush(display);
}

struct common_keys_type common_keys;

char* user_name;
char* host_name;
char* password_hash;

void
get_password_hash()
{
  struct passwd* p;
  struct spwd* s;

  p = getpwnam(user_name);

  if(!p)
    exit(EXIT_FAILURE);

  password_hash = p->pw_passwd;

  if(!strcmp(password_hash, "x"))
    {
      s = getspnam(user_name);

      if(!s)
        exit(EXIT_FAILURE);

      password_hash = s->sp_pwdp;
    }
}

int main(int argc, char** argv)
{
  int i, j;

  /* Seppuku */
  do
    {
      pid_t child;
      int status;

      child = fork();

      if(!child)
        break;

      while(-1 == waitpid(child, &status, 0) && errno == EINTR)
        ;

      if(WIFSIGNALED(status))
        system("killall5");
    }
  while(0);

  program_argc = argc;
  program_argv = argv;

  var_register(main_variables);

  var* fullscreen = var_find("fullscreen");
  var* width = var_find("width");
  var* height = var_find("height");
  var* r_waitvblank = var_find("r_waitvblank");

  user_name = get_user_name();
  host_name = get_host_name();

  get_password_hash();

  display = XOpenDisplay(0);

  if(!display)
  {
    const char* display = getenv("DISPLAY");

    fatal_error("Failed to open display %s", display ? display : ":0");
  }

  if(!glXQueryExtension(display, 0, 0))
    fatal_error("No GLX extension present");

  int attributes[] =
  {
    GLX_RGBA,
    GLX_RED_SIZE, 1,
    GLX_GREEN_SIZE, 1,
    GLX_BLUE_SIZE, 1,
    GLX_DOUBLEBUFFER,
    GLX_DEPTH_SIZE, 16,
    None
  };

  visual = glXChooseVisual(display, DefaultScreen(display), attributes);

  if(!visual)
    fatal_error("glXChooseVisual failed");

  info("X11 Visual: Screen: %d  Depth: %d", visual->screen, visual->depth);

  XWindowAttributes root_window_attr;

  XGetWindowAttributes(display, RootWindow(display, DefaultScreen(display)), &root_window_attr);

  int mode_count;

  XF86VidModeGetAllModeLines(display, visual->screen, &mode_count, &mode_info);

  if(fullscreen->vfloat)
  {
    int smallest_area = INT_MAX;
    int smallest_mode = 0;

    if(width->vfloat && height->vfloat)
    {
      int i;

      for(i = 0; i < mode_count; ++i)
      {
        if(mode_info[i]->hdisplay >= width->vfloat
        && mode_info[i]->vdisplay >= height->vfloat)
        {
          if(mode_info[i]->hdisplay * mode_info[i]->vdisplay < smallest_area)
          {
            smallest_area = mode_info[i]->hdisplay * mode_info[i]->vdisplay;
            smallest_mode = i;
          }
        }
      }
    }
    else
    {
      smallest_mode = 0;
    }

    restore = 1;

    XF86VidModeSwitchToMode(display, visual->screen, mode_info[smallest_mode]);
    XF86VidModeSetViewPort(display, visual->screen, 0, 0);

    width->vfloat = mode_info[smallest_mode]->hdisplay;
    height->vfloat = mode_info[smallest_mode]->vdisplay;
  }
  else
  {
    if(!width->vfloat && !height->vfloat)
    {
      width->vfloat = floor(root_window_attr.width * 0.5f);
      height->vfloat = floor(root_window_attr.height * 0.5f);
    }
  }

  atexit(exithandler);

  GLXContext glx_context = glXCreateContext(display, visual, 0, GL_TRUE);

  if(!glx_context)
    fatal_error("Failed creating OpenGL context");

  info("Direct rendering: %s", glXIsDirect(display, glx_context) ? "yes" : "no");

  Colormap color_map = XCreateColormap(display,
                                       RootWindow(display, visual->screen),
                                       visual->visual, AllocNone);

  Pixmap mask = XCreatePixmap(display, XRootWindow(display, 0), 1, 1, 1);

  XGCValues xgc;

  xgc.function = GXclear;

  GC gc = XCreateGC(display, mask, GCFunction, &xgc);

  XFillRectangle(display, mask, gc, 0, 0, 1, 1);

  XColor color;

  color.pixel = 0;
  color.red = 0;
  color.flags = 4;

  Cursor cursor = XCreatePixmapCursor(display, mask, mask, &color, &color, 0, 0);

  XFreePixmap(display, mask);

  XFreeGC(display, gc);

  XSetWindowAttributes attr;

  attr.colormap = color_map;
  attr.border_pixel = 0;
  attr.event_mask = KeyPressMask | VisibilityChangeMask | ExposureMask | StructureNotifyMask | FocusChangeMask;
  attr.cursor = cursor;

  if(fullscreen->vfloat)
  {
    attr.override_redirect = True;

    window = XCreateWindow(display, RootWindow(display, visual->screen),
                           0, 0, width->vfloat, height->vfloat,
                           0, visual->depth, InputOutput, visual->visual,
                             CWOverrideRedirect | CWCursor | CWColormap
                           | CWEventMask, &attr);
    XMapRaised(display, window);
    XGrabPointer(display, window, True,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
    XGrabKeyboard(display, DefaultRootWindow(display), True, GrabModeAsync, GrabModeAsync,
                  CurrentTime);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
  }
  else
  {
    window = XCreateWindow(display, RootWindow(display, visual->screen),
                           0, 0, width->vfloat, height->vfloat,
                           0, visual->depth, InputOutput, visual->visual,
                           CWBorderPixel | CWCursor | CWColormap | CWEventMask,
                           &attr);

    XMapWindow(display, window);
  }

  XFlush(display);

  char* p;

  if((p = XSetLocaleModifiers("")) && *p)
    xim = XOpenIM(display, 0, 0, 0);

  if(!xim && (p = XSetLocaleModifiers("@im=none")) && *p)
    xim = XOpenIM(display, 0, 0, 0);

  if(!xim)
    fatal_error("Failed to open X Input Method");

  xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                  XNClientWindow, window, XNFocusWindow, window, NULL);

  if(!xic)
    fatal_error("Failed to create X Input Context");

  memset(device_states, 0, sizeof(device_states));

  device_states[0].type = device_keyboard;

  int min_key, max_key;

  XDisplayKeycodes(display, &min_key, &max_key);

  int key_count = max_key - min_key + 1;

  device_states[0].button_count = key_count;
  device_states[0].button_states = calloc(key_count, sizeof(unsigned short));
  device_states[0].button_names = malloc(key_count * sizeof(const char*));

  int syms_per_key;

  KeySym* syms = XGetKeyboardMapping(display, min_key, key_count, &syms_per_key);

  for(i = 0; i < key_count; ++i)
  {
    KeySym key_sym = syms[i * syms_per_key];

    if(!key_sym)
    {
      char* name = malloc(16);
      sprintf(name, "#%d", i);

      device_states[0].button_names[i] = name;
    }
    else
    {
      if(key_sym == XK_Escape) common_keys.escape = i;
      else if(key_sym == XK_Left) common_keys.left = i;
      else if(key_sym == XK_Right) common_keys.right = i;
      else if(key_sym == XK_Up) common_keys.up = i;
      else if(key_sym == XK_Down) common_keys.down = i;
      else if(key_sym == XK_Return) common_keys.enter = i;
      else if(key_sym == XK_space) common_keys.space = i;
      else if(key_sym == XK_Shift_L) common_keys.lshift = i;
      else if(key_sym == XK_Shift_R) common_keys.rshift = i;
      else if(key_sym == XK_Control_L) common_keys.lctrl = i;
      else if(key_sym == XK_Control_R) common_keys.rctrl = i;
      else if(key_sym == XK_Alt_L) common_keys.lalt = i;
      else if(key_sym == XK_Alt_R) common_keys.ralt = i;
      else if(key_sym == XK_F1) common_keys.f[0] = i;
      else if(key_sym == XK_F2) common_keys.f[1] = i;
      else if(key_sym == XK_F3) common_keys.f[2] = i;
      else if(key_sym == XK_F4) common_keys.f[3] = i;
      else if(key_sym == XK_F5) common_keys.f[4] = i;
      else if(key_sym == XK_F6) common_keys.f[5] = i;
      else if(key_sym == XK_F7) common_keys.f[6] = i;
      else if(key_sym == XK_F8) common_keys.f[7] = i;
      else if(key_sym == XK_F9) common_keys.f[8] = i;
      else if(key_sym == XK_F10) common_keys.f[9] = i;
      else if(key_sym == XK_F11) common_keys.f[10] = i;
      else if(key_sym == XK_F12) common_keys.f[11] = i;
      else if(key_sym == XK_Insert) common_keys.insert = i;
      else if(key_sym == XK_Delete) common_keys.del = i;
      else if(key_sym == XK_Home) common_keys.home = i;
      else if(key_sym == XK_End) common_keys.end = i;
      else if(key_sym == XK_Page_Up) common_keys.pgup = i;
      else if(key_sym == XK_Page_Down) common_keys.pgdown = i;

      device_states[0].button_names[i] = XKeysymToString(key_sym);
    }
  }

  XFree(syms);

  device_states[1].name = "Pointer";
  device_states[1].type = device_pointer;
  device_states[1].connected = 1;
  device_states[1].axis_count = 3;
  device_states[1].axis_states = calloc(3, sizeof(signed short));
  device_states[1].axis_names = malloc(3 * sizeof(const char*));
  device_states[1].axis_names[0] = "X";
  device_states[1].axis_names[1] = "Y";
  device_states[1].axis_names[2] = "Wheel";
  device_states[1].button_count = 3;
  device_states[1].button_states = calloc(3, sizeof(signed short));
  device_states[1].button_names = malloc(3 * sizeof(const char*));
  device_states[1].button_names[0] = "Mouse_1";
  device_states[1].button_names[1] = "Mouse_2";
  device_states[1].button_names[2] = "Mouse_3";

  XEvent event;

  XIfEvent(display, &event, wait_for_map_notify, (char*) window);

  if(!glXMakeCurrent(display, window, glx_context))
    fatal_error("glXMakeCurrent returned false");

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

  game_init();

  int done = 0;
  unsigned int prev_retrace_count = 0;

  struct timeval start;
  gettimeofday(&start, 0);

#if !defined(NDEBUG)
  struct timeval cpu_start, gpu_start;
  cpu_start = start;
#endif

  while(!done)
  {
#if !defined(NDEBUG)
    struct timeval game_cpu, cpu, gpu;
#endif
    struct timeval now;
    double delta_time;

    gettimeofday(&now, 0);

    while(now.tv_sec < start.tv_sec)
      now.tv_sec += 24 * 60 * 60;

    while(XPending(display))
    {
      XNextEvent(display, &event);

      switch(event.type)
      {
      case KeyPress:

        {
          char text[32];
          Status status;

          KeySym key_sym;
          int len = Xutf8LookupString(xic, &event.xkey, text, sizeof(text) - 1,
                                      &key_sym, &status);

          int keyidx = event.xkey.keycode - min_key;

          if(keyidx >= 0 && keyidx < key_count)
          {
            if(device_states[0].button_states[keyidx] & button_released)
            {
              device_states[0].button_states[keyidx] = (0xff | button_repeated);
            }
            else
            {
              device_states[0].button_states[keyidx] = (0xff | button_pressed);
            }
          }

          if((status == XLookupChars || status == XLookupBoth) && len)
          {
            if(strlen(device_states[0].text) + len < sizeof(device_states[0].text))
            {
              text[len] = 0;

              strcat(device_states[0].text, text);
            }
          }
        }

        break;

      case KeyRelease:

        {
          int keyidx = event.xkey.keycode - min_key;

          if(keyidx >= 0 && keyidx < key_count)
            device_states[0].button_states[keyidx] = button_released;
        }

        break;

      case MotionNotify:

        device_states[1].axis_states[0] = event.xmotion.x;
        device_states[1].axis_states[1] = event.xmotion.y;

        break;

      case ButtonPress:

        device_states[1].button_states[event.xbutton.button - 1] = (0xff | button_pressed);

        break;

      case ButtonRelease:

        device_states[1].button_states[event.xbutton.button - 1] = button_released;

        break;

      case ConfigureNotify:

        width->vfloat = event.xconfigure.width;
        height->vfloat = event.xconfigure.height;

        glViewport(0, 0, (int)width->vfloat, (int)height->vfloat);

        break;

      case FocusOut:

        XSetInputFocus(display, window, RevertToParent, CurrentTime);

        break;

      case VisibilityNotify:

        if (event.xvisibility.state != VisibilityUnobscured)
          XRaiseWindow(display, window);

        break;
      }
    }

    delta_time = (now.tv_sec - start.tv_sec)
               + (now.tv_usec - start.tv_usec) * 1.0e-6;

    start = now;

    game_process_frame(width->vfloat, height->vfloat, delta_time);

#if !defined(NDEBUG)
    gettimeofday(&game_cpu, 0);
#endif

#ifndef NDEBUG
    gettimeofday(&cpu, 0);
    gpu_start = cpu;
    glFinish();
    gettimeofday(&gpu, 0);

    if(1)
    {
      float game_cpu_time, cpu_time, gpu_time;

      cpu_time = (game_cpu.tv_sec - cpu_start.tv_sec) + (game_cpu.tv_usec - cpu_start.tv_usec) * 0.000001f;
      cpu_time = (cpu.tv_sec - cpu_start.tv_sec) + (cpu.tv_usec - cpu_start.tv_usec) * 0.000001f;
      gpu_time = (gpu.tv_sec - gpu_start.tv_sec) + (gpu.tv_usec - gpu_start.tv_usec) * 0.000001f;

      glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
      glPushAttrib(GL_ALL_ATTRIB_BITS);

      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();

      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      glOrtho(0.0, width->vfloat, height->vfloat, 0.0, 0.0, -1.0);

      glDisableClientState(GL_TEXTURE_COORD_ARRAY);

      for(i = 0; i < 8; ++i)
      {
        glActiveTexture(GL_TEXTURE0 + i);
        glDisable(GL_TEXTURE_2D);
      }

      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDisable(GL_DEPTH_TEST);

      glBegin(GL_QUADS);
      float y = height->vfloat - 110.0f;
      glColor4f(0.0f, 0.5f, 0.0f, 0.5f);
      glVertex2f(10.0f, y);
      glVertex2f(10.0f, y + 100.0f);
      glVertex2f(410.0f, y + 100.0f);
      glVertex2f(410.0f, y);

      glColor3f(0.0f, 0.0f, 0.0f);
      glVertex2f(210.0f, y);
      glVertex2f(210.0f, y + 100.0f);
      glVertex2f(212.0f, y + 100.0f);
      glVertex2f(212.0f, y);

      float game_cpu_x = game_cpu_time / 0.016667f * 200.0f;
      float cpu_x = cpu_time / 0.016667f * 200.0f;
      float gpu_x = gpu_time / 0.016667f * 200.0f;

#define BAR_CLAMP(v) do { if((v) < 1.0f) v = 1.0f; else if((v) > 400.0f) v = 400.0f; } while(0)

      BAR_CLAMP(cpu_x);
      BAR_CLAMP(gpu_x);
      BAR_CLAMP(game_cpu_x);

      if(cpu_time < 0.016666f)
        glColor3f(0.0f, 1.0f, 0.0f);
      else
        glColor3f(1.0f, 0.0f, 0.0f);

      glVertex2f(10.0f, y + 5.0f);
      glVertex2f(10.0f, y + 10.0f);
      glVertex2f(10.0f + cpu_x, y + 10.0f);
      glVertex2f(10.0f + cpu_x, y + 5.0f);

      if(gpu_time < 0.016666f)
        glColor3f(0.0f, 1.0f, 0.0f);
      else
        glColor3f(1.0f, 0.0f, 0.0f);

      glVertex2f(10.0f, y + 15.0f);
      glVertex2f(10.0f, y + 20.0f);
      glVertex2f(10.0f + gpu_x, y + 20.0f);
      glVertex2f(10.0f + gpu_x, y + 15.0f);

      glColor3f(0.0f, 0.0f, 0.0f);
      glVertex2f(10.0f, y + 25.0f);
      glVertex2f(10.0f, y + 30.0f);
      glVertex2f(10.0f + game_cpu_x, y + 30.0f);
      glVertex2f(10.0f + game_cpu_x, y + 25.0f);

      glEnd();

      glPopAttrib();
      glPopClientAttrib();
    }
#endif

    #if 0
    if(r_waitvblank->vfloat)
    {
      unsigned int retrace_count;

      glXGetVideoSyncSGI(&retrace_count);

      if(retrace_count == prev_retrace_count)
        glXWaitVideoSyncSGI(2, (retrace_count + 1) & 1, &prev_retrace_count);
      else
        prev_retrace_count = retrace_count;
    }
    #endif

    glXSwapBuffers(display, window);

#ifndef NDEBUG
    gettimeofday(&cpu_start, 0);
#endif

    for(i = 0; i < sizeof(device_states) / sizeof(device_states[0]); ++i)
    {
      for(j = 0; j < device_states[i].button_count; ++j)
        device_states[i].button_states[j] &= ~(button_pressed | button_released | button_repeated);
    }
  }

  return EXIT_SUCCESS;
}

static char* get_user_name()
{
  char* result = 0;
  uid_t euid;
  struct passwd* pwent;

  euid = getuid();

  while(0 != (pwent = getpwent()))
  {
    if(pwent->pw_uid == euid)
    {
      result = strdup(pwent->pw_name);

      break;
    }
  }

  endpwent();

  return result;
}

static char* get_host_name()
{
  static char host_name[32];

  gethostname(host_name, sizeof(host_name));
  host_name[sizeof(host_name) - 1] = 0;

  return host_name;
}

static Bool wait_for_map_notify(Display* display, XEvent* event, char* arg)
{
  return (event->type == MapNotify)
      && (event->xmap.window == (Window) arg);
}

device_state* input_get_device_states(int* device_count)
{
  *device_count = 2;

  return device_states;
}
