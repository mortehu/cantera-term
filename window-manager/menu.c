#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include <X11/extensions/Xrender.h>

#include "common.h"
#include "font.h"
#include "globals.h"

static struct picture background;

#define FLAG_TERMINAL 0x0001

struct picture
{
  struct cnt_image* image;
  Picture pic;
  size_t width;
  size_t height;
};

static wchar_t query[256];

static void* config;

void menu_draw_desktops(Picture buffer, int height);

static void drawtext_bar(Picture target, const wchar_t* text, size_t len, int x, int y)
{
  if(!len)
    return;

  XRenderFillRectangle(display, PictOpOver, target, &xrpalette[19],
                       x - 2, y, len * xskips[SMALL] + 4, yskips[SMALL]);
  drawtext(target, text, len, x, y, 15, SMALL);
}

static void* loading_thread_entry(void* arg)
{
  struct stat st_config, st_config_so;

  if(0 == access(".cantera/applications/", X_OK | R_OK))
    desktop_recursive_scan(".cantera/applications/");
  else
  {
    desktop_recursive_scan("/usr/share/applications/");
    desktop_recursive_scan("/home/larsod/share/applications/");
    desktop_recursive_scan("/usr/local/share/applications/");
  }

  if(0 == stat(".cantera/config.c", &st_config))
  {
    char config_so_path[64];
    struct utsname un;

    uname(&un);

    sprintf(config_so_path, ".cantera/config-%s.so", un.machine);

    if(-1 == stat(config_so_path, &st_config_so)
    || st_config_so.st_mtime < st_config.st_mtime)
    {
      char cmd[128];

      fprintf(stderr, "Compiling configuration...\n");

      sprintf(cmd, "gcc -x c -fPIC -std=c99 .cantera/config.c -shared -o %s", config_so_path);
      system(cmd);
    }

    config = dlopen(config_so_path, RTLD_NOW | RTLD_LOCAL);

    if(!config)
      fprintf(stderr, "Failed to load '%s': %s\n", config_so_path, dlerror());
    else
    {
      void (*init_handler)() = dlsym(config, "init");

      if(init_handler)
        init_handler();
    }
  }

  return 0;
}

static int image_load(const char* path, struct picture* img)
{
  void* readctx;

  /* XXX: Implement cache */

  readctx = cnt_file_callback_init(path);

  if(!readctx)
    return -1;

  img->image = cnt_image_alloc();
  cnt_image_set_data_callback(img->image, cnt_file_callback, readctx);
  img->pic = cnt_image_load(&img->width, &img->height, img->image);

  free(readctx);

  return 0;
}

void menu_init()
{
  pthread_t loading_thread;

  image_load(".cantera/background.png", &background);

  pthread_create(&loading_thread, 0, loading_thread_entry, 0);
  pthread_detach(loading_thread);
}

void menu_thumbnail_dimensions(int* width, int* height, int* margin)
{
  int tmp_margin = 10;
  *width = (window_width - tmp_margin * 17) / 12;
  *height = window_height * *width / window_width;

  if(margin)
    *margin = tmp_margin;
}

void menu_draw()
{
  wchar_t buf[260];
  int thumb_width, thumb_height, thumb_margin;
  menu_thumbnail_dimensions(&thumb_width, &thumb_height, &thumb_margin);

  XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[0],
                       0, 0, window_width, window_height);

  if(background.pic)
  {
    XRenderComposite(display, PictOpSrc, background.pic, None, root_buffer,
                     0, 0, 0, 0, 0, 0, window_width, window_height);
  }

  int margin_y = window_height * 75 / 1000;

  int y = margin_y;

  swprintf(buf, sizeof(buf), L"Query: %ls", query);
  y = window_height - 2 * thumb_height - 2 * thumb_margin - yskips[SMALL] - yskips[LARGE] - 15;
  drawtext(root_buffer, buf, wcslen(buf), thumb_margin + 1, y + 1, 0, LARGE);
  drawtext(root_buffer, buf, wcslen(buf), thumb_margin, y, 15, LARGE);

  menu_draw_desktops(root_buffer, window_height);
}

void menu_draw_desktops(Picture buffer, int height)
{
  int thumb_width, thumb_height, thumb_margin;
  int i, x = 0, y;
  time_t ttnow;
  struct tm* tmnow;
  wchar_t wbuf[256];
  char buf[256];

  menu_thumbnail_dimensions(&thumb_width, &thumb_height, &thumb_margin);

  ttnow = time(0);
  tmnow = localtime(&ttnow);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmnow);
  swprintf(wbuf, sizeof(wbuf), L"%s", buf);

  x = thumb_margin;

  drawtext_bar(buffer, wbuf, wcslen(wbuf), x, height - yskips[SMALL] - 4);
  x += (wcslen(wbuf) + 2) * xskips[SMALL];

  swprintf(wbuf, sizeof(wbuf), L"%s", PACKAGE_STRING);
  drawtext_bar(buffer, wbuf, wcslen(wbuf), x, height - yskips[SMALL] - 4);

  for(i = 0; i < 24; ++i)
  {
    x = thumb_margin + (i % 12) * (thumb_width + thumb_margin);

    if((i % 12) > 7)
      x += 4 * thumb_margin;
    else if((i % 12) > 3)
      x += 2 * thumb_margin;

    if(i < 12)
      y = height - 2 * thumb_height - 2 * thumb_margin - yskips[SMALL];
    else
      y = height - thumb_height - thumb_margin - yskips[SMALL];

    int border_color = 7;

    if(i == active_terminal)
      border_color = 12;
    else if(terminals[i].mode == mode_menu)
      border_color = 8;

    XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[border_color], x - 1, y - 1, 1, thumb_height + 2);
    XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[border_color], x + thumb_width, y - 1, 1, thumb_height + 2);
    XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[border_color], x - 1, y - 1, thumb_width + 2, 1);
    XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[border_color], x - 1, y + thumb_height, thumb_width + 2, 1);

    if(!terminals[i].thumbnail)
    {
      XRenderFillRectangle(display, PictOpOver, buffer, &xrpalette[19],
                           x, y, thumb_width, thumb_height);
    }
    else
    {
      XRenderComposite(display, PictOpSrc, terminals[i].thumbnail, None, buffer, 0, 0, 0, 0, x, y, thumb_width, thumb_height);
    }
  }
}

void menu_keypress(int key_sym, const char* text, int textlen)
{
  switch(key_sym)
  {
  case XK_Escape:

    query[0] = 0;

    break;

  case XK_Return:

      {
        char command[4096];

        wcstombs(command, query, sizeof(command));

        launch(command);

        query[0] = 0;
      }

    break;

  case XK_BackSpace:

    if(wcslen(query))
      query[wcslen(query) - 1] = 0;

    break;

  default:

    return;
  }

  XClearArea(display, window, 0, 0, window_width, window_height, True);
}

void menu_keyrelease(int key_sym)
{
}

int menu_handle_char(int ch)
{
  switch(ch)
  {
  case ('U' & 0x3F):

    query[0] = 0;

    break;

  default:

    if(isgraph(ch) || ch == ' ')
    {
      if(ch == ' ' && !query[0])
        return 0;

      size_t querylen = wcslen(query);

      if(querylen < sizeof(query) / sizeof(query[0]) - 1)
      {
        query[querylen++] = ch;
        query[querylen] = 0;
      }

      break;
    }

    return -1;
  }

  XClearArea(display, window, 0, 0, window_width, window_height, True);

  return 0;
}

int menu_handle_hotkey(int ch)
{
  void (*handler)(int ch) = dlsym(config, "handle_hotkey");

  if(handler)
    handler(ch);

  return 0;
}
