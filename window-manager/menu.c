#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
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
#include "menu.h"
#include "tree.h"

static struct picture background;
extern struct tree* config;

#define FLAG_TERMINAL 0x0001

struct picture
{
  struct cnt_image* image;
  Picture pic;
  size_t width;
  size_t height;
};

static void drawtext_bar(Picture target, const wchar_t* text, size_t len, int x, int y)
{
  if(!len)
    return;

  XRenderFillRectangle(display, PictOpOver, target, &xrpalette[19],
                       x - 2, y, len * xskips[SMALL] + 4, yskips[SMALL]);
  drawtext(target, text, len, x, y, 15, SMALL);
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
  char** init_commands;
  size_t i, init_command_count;

  init_command_count = tree_get_strings(config, "menu.init", &init_commands);

  for(i = 0; i < init_command_count; ++i)
    system(init_commands[i]);

  free(init_commands);

  image_load(".cantera/background.png", &background);
}

void menu_thumbnail_dimensions(struct screen* screen, int* width, int* height, int* margin)
{
  int tmp_margin = 10;
  int tmp_width;

  tmp_width = (screen->width - tmp_margin * 17) / 12;

  if (width)
    *width = tmp_width;

  if (height)
    *height = screen->height * tmp_width / screen->width;

  if(margin)
    *margin = tmp_margin;
}

void menu_draw(struct screen* screen)
{
  wchar_t buf[260];
  int thumb_width, thumb_height, thumb_margin;

  menu_thumbnail_dimensions(screen, &thumb_width, &thumb_height, &thumb_margin);

  XRenderFillRectangle(display, PictOpSrc, screen->root_buffer, &xrpalette[0],
                       0, 0, screen->width, screen->height);

  if(background.pic)
  {
    XRenderComposite(display, PictOpSrc, background.pic, None, screen->root_buffer,
                     0, 0, 0, 0, 0, 0, screen->width, screen->height);
  }

  int margin_y = screen->height * 75 / 1000;

  int y = margin_y;

  swprintf(buf, sizeof(buf), L"Query: %ls", screen->query);
  y = screen->height - 2 * thumb_height - 2 * thumb_margin - yskips[SMALL] - yskips[LARGE] - 15;
  drawtext(screen->root_buffer, buf, wcslen(buf), thumb_margin + 1, y + 1, 0, LARGE);
  drawtext(screen->root_buffer, buf, wcslen(buf), thumb_margin, y, 15, LARGE);

  menu_draw_desktops(screen, screen->root_buffer, screen->height);
}

long read_proc_int (const char *path)
{
  FILE *f;
  long result;

  f = fopen (path, "r");

  if (!f)
    return -1;

  fscanf (f, "%ld", &result);
  fclose (f);

  return result;
}

void menu_draw_desktops(struct screen* screen, Picture buffer, int height)
{
  int thumb_width, thumb_height, thumb_margin;
  int i, x = 0, y;
  time_t ttnow;
  struct tm* tmnow;
  wchar_t wbuf[256];
  char buf[256];

  long charge_now = -1, charge_full = -1;

  menu_thumbnail_dimensions(screen, &thumb_width, &thumb_height, &thumb_margin);

  charge_now =  read_proc_int ("/sys/class/power_supply/BAT1/charge_now");
  charge_full = read_proc_int ("/sys/class/power_supply/BAT1/charge_full");

  ttnow = time(0);
  tmnow = localtime(&ttnow);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmnow);
  swprintf(wbuf, sizeof(wbuf), L"%s", buf);

  if (charge_now >= 0 && charge_full >= 0)
  {
    swprintf (wcschr (wbuf, 0), sizeof(wbuf),
              L"  Battery: %.2f%%",
              100.0 * charge_now / charge_full);
  }

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

    if(i == screen->active_terminal
       && current_screen == screen)
      border_color = 12;
    else if(screen->terminals[i].mode == mode_menu)
      border_color = 8;

    XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[border_color], x - 1, y - 1, 1, thumb_height + 2);
    XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[border_color], x + thumb_width, y - 1, 1, thumb_height + 2);
    XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[border_color], x - 1, y - 1, thumb_width + 2, 1);
    XRenderFillRectangle(display, PictOpSrc, buffer, &xrpalette[border_color], x - 1, y + thumb_height, thumb_width + 2, 1);

    if(!screen->terminals[i].thumbnail)
      XRenderFillRectangle(display, PictOpOver, buffer, &xrpalette[19],
                           x, y, thumb_width, thumb_height);
    else
      XRenderComposite(display, PictOpSrc, screen->terminals[i].thumbnail, None, buffer, 0, 0, 0, 0, x, y, thumb_width, thumb_height);
  }
}

void menu_keypress(struct screen* screen, int key_sym, const char* text, int textlen, Time time)
{
  switch(key_sym)
  {
  case XK_Escape:

    screen->query[0] = 0;

    break;

  case XK_Return:

      {
        char command[4096];

        wcstombs(command, screen->query, sizeof(command));

        launch(command, time);

        screen->query[0] = 0;
      }

    break;

  case XK_BackSpace:

    if(wcslen(screen->query))
      screen->query[wcslen(screen->query) - 1] = 0;

    break;

  default:

    return;
  }

  XClearArea(display, screen->window, 0, 0, screen->width, screen->height, True);
}

int menu_handle_char(struct screen* screen, int ch)
{
  switch(ch)
  {
  case ('U' & 0x3F):

    screen->query[0] = 0;

    break;

  default:

    if(isgraph(ch) || ch == ' ')
    {
      if(ch == ' ' && !screen->query[0])
        return 0;

      size_t querylen = wcslen(screen->query);

      if(querylen < sizeof(screen->query) / sizeof(screen->query[0]) - 1)
      {
        screen->query[querylen++] = ch;
        screen->query[querylen] = 0;
      }

      break;
    }

    return -1;
  }

  XClearArea(display, screen->window, 0, 0, screen->width, screen->height, True);

  return 0;
}
