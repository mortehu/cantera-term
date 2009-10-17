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

struct menu_item
{
  struct picture icon;
  char* icon_basename;
  char* command;
  wchar_t* name;
  wchar_t* shortcut;
  int flags;
};

static struct menu_item* menu_items;
static size_t menu_item_count;
static size_t menu_items_alloc;
static pthread_mutex_t menu_mutex = PTHREAD_MUTEX_INITIALIZER;

static int offset;
static int cursor;
static int page_height = 1;

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

static int range_str_eq(const char* lhs, const char* rhs_begin, const char* rhs_end)
{
  return (rhs_end - rhs_begin == strlen(lhs))
      && (!memcmp(lhs, rhs_begin, rhs_end - rhs_begin));
}

static void desktop_parse(const char* path)
{
  int fd, res;
  char* data;
  const char* end;
  const char* ch;
  off_t size;

  fd = open(path, O_RDONLY);

  if(fd == -1)
  {
    fprintf(stderr, "Failed to open '%s' for reading: %s\n", path, strerror(errno));

    return;
  }

  size = lseek(fd, 0, SEEK_END);

  if(size == -1)
  {
    fprintf(stderr, "Failed to get file size of '%s': %s\n", path, strerror(errno));

    return;
  }

  if(size < 16384)
  {
    data = alloca(size);

    res = pread(fd, data, size, 0);

    if(res == -1)
    {
      fprintf(stderr, "Failed to read %zu bytes from '%s': %s\n", size, path, strerror(errno));

      close(fd);

      return;
    }
    else if(res < size)
    {
      fprintf(stderr, "Got %d bytes reading '%s', wanted %zu\n", res, path, size);

      close(fd);

      return;
    }
  }
  else
  {
    data = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);

    if(data == MAP_FAILED)
    {
      fprintf(stderr, "Failed to mmap '%s': %s\n", path, strerror(errno));

      return;
    }
  }

  ch = data;
  end = data + size;

  char* generic_name = 0;
  char* name = 0;
  char* exec = 0;
  char* icon = 0;
  char* terminal = 0;

  const char* section_begin = 0;
  const char* section_end = 0;

  while(ch != end)
  {
    const char* key_begin;
    const char* key_end;
    const char* value_begin;
    const char* value_end;

    while(ch != end && isspace(*ch))
      ++ch;

    if(ch == end)
      break;

    if(*ch == '[')
    {
      if(++ch == end)
        goto done;

      section_begin = ch++;

      while(ch != end && *ch != ']')
        ++ch;

      if(ch == end)
        goto done;

      section_end = ch++;

      continue;
    }

    key_begin = ch++;

    while(ch != end && *ch != '=' && !isspace(*ch))
      ++ch;

    if(ch == end)
      goto done;

    key_end = ch;

    while(ch != end && isspace(*ch))
      ++ch;

    if(ch == end || *ch != '=')
      goto done;

    ++ch;

    while(ch != end && isspace(*ch))
      ++ch;

    if(ch == end)
      goto done;

    value_begin = ch++;

    while(ch != end && *ch != '\r' && *ch != '\n')
      ++ch;

    if(ch == end)
      goto done;

    value_end = ch;

    if(range_str_eq("Desktop Entry", section_begin, section_end))
    {
      if(range_str_eq("GenericName", key_begin, key_end))
        generic_name = strndupa(value_begin, value_end - value_begin);
      else if(range_str_eq("Name", key_begin, key_end))
        name = strndupa(value_begin, value_end - value_begin);
      else if(range_str_eq("Exec", key_begin, key_end))
        exec = strndupa(value_begin, value_end - value_begin);
      else if(range_str_eq("Icon", key_begin, key_end))
        icon = strndupa(value_begin, value_end - value_begin);
      else if(range_str_eq("Terminal", key_begin, key_end))
        terminal = strndupa(value_begin, value_end - value_begin);
    }
  }

  if(name && exec)
  {
    size_t i;

    if(menu_items_alloc == menu_item_count)
    {
      void* new_ptr;

      menu_items_alloc = 32 + menu_items_alloc * 3 / 2;

      pthread_mutex_lock(&menu_mutex);
      new_ptr = realloc(menu_items, menu_items_alloc * sizeof(struct menu_item));

      if(!new_ptr)
      {
        fprintf(stderr, "Allocating %zu bytes of memory failed: %s\n", menu_items_alloc * sizeof(struct menu_item), strerror(errno));

        pthread_mutex_unlock(&menu_mutex);

        goto done;
      }

      menu_items = new_ptr;

      pthread_mutex_unlock(&menu_mutex);
    }

    i = menu_item_count;

    memset(&menu_items[i], 0, sizeof(struct menu_item));

    if(icon)
      menu_items[i].icon_basename = strdup(icon);
    menu_items[i].command = strdup(exec);
    menu_items[i].name = malloc((strlen(name) + 1) * sizeof(wchar_t));
    utf8_to_ucs(menu_items[i].name, name, strlen(name) + 1);

    if(terminal && (terminal[0] == 't' || terminal[0] == '1'))
      menu_items[i].flags |= FLAG_TERMINAL;


    ++menu_item_count;
  }

done:

  if(size >= 16384)
    munmap((void*) data, size);
  close(fd);
}

static void desktop_recursive_scan(const char* path)
{
  char path_buf[4096];
  struct dirent** dirents;
  struct dirent* dent;
  int i, entcount, path_length;

  path_length = strlen(path);

  entcount = scandir(path, &dirents, 0, alphasort);

  if(entcount <= 0)
  {
    if(entcount == 0)
      free(dirents);

    return;
  }

  for(i = 0; i < entcount; ++i)
  {
    int filename_length, fullname_length;

    dent = dirents[i];

    if(dent->d_name[0] == '.')
      continue;

    filename_length = strlen(dent->d_name);

    if(path_length + filename_length + 2 > sizeof(path_buf))
    {
      fprintf(stderr, "File name `%s/%s' too long\n", path, dent->d_name);

      continue;
    }

    strcpy(path_buf, path);

    if(path_buf[path_length - 1] == '/')
    {
      fullname_length = path_length + filename_length + 1;
      strcpy(path_buf + path_length, dent->d_name);
    }
    else
    {
      fullname_length = path_length + filename_length + 2;
      path_buf[path_length] = '/';
      strcpy(path_buf + path_length + 1, dent->d_name);
    }

    if(dent->d_type != DT_REG)
    {
      desktop_recursive_scan(path_buf);

      continue;
    }

    if(filename_length > 8 && !strcmp(dent->d_name + filename_length - 8, ".desktop"))
      desktop_parse(path_buf);
  }
}

int menu_item_cmp(const void* vlhs, const void* vrhs)
{
  const struct menu_item* lhs = vlhs;
  const struct menu_item* rhs = vrhs;

  return wcscasecmp(lhs->name, rhs->name);
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

  qsort(menu_items, menu_item_count, sizeof(struct menu_item), menu_item_cmp);

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

static void load_icon(struct menu_item* m)
{
  static const char* resolutions[] = { "48x48", "32x32", "16x16" };
  static const char* packs[] = { "crystalsvg", "hicolor" };
  char path_buf[4096];

  size_t i, j;

  assert(m->icon_basename);

  if(m->icon_basename[0] == '/')
  {
    if(0 == access(m->icon_basename, R_OK) && 0 == image_load(m->icon_basename, &m->icon))
      goto done;
  }
  else
  {
    snprintf(path_buf, sizeof(path_buf), "/usr/share/pixmaps/%s", m->icon_basename);
    path_buf[sizeof(path_buf) - 1] = 0;

    if(0 == access(path_buf, R_OK) && 0 == image_load(path_buf, &m->icon))
      goto done;

    snprintf(path_buf, sizeof(path_buf), "/usr/share/pixmaps/%s.xpm", m->icon_basename);
    path_buf[sizeof(path_buf) - 1] = 0;

    if(0 == access(path_buf, R_OK) && 0 == image_load(path_buf, &m->icon))
      goto done;

    for(j = 0; j < sizeof(resolutions) / sizeof(resolutions[0]); ++j)
    {
      for(i = 0; i < sizeof(packs) / sizeof(packs[0]); ++i)
      {
        snprintf(path_buf, sizeof(path_buf), "/usr/share/icons/%s/%s/apps/%s.png", packs[i], resolutions[j], m->icon_basename);
        path_buf[sizeof(path_buf) - 1] = 0;

        if(0 == access(path_buf, R_OK) && 0 == image_load(path_buf, &m->icon))
          goto done;
      }
    }
  }

done:

  free(m->icon_basename);
  m->icon_basename = 0;
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

static int filter(int n)
{
  int j;
  size_t querylen = wcslen(query);

  if(query[0])
  {
    for(j = 0; menu_items[n].name[j]; ++j)
    {
      if(!wcsncasecmp(query, &menu_items[n].name[j], querylen))
        return 0;;
    }

    return 1;
  }

  return 0;
}

void menu_draw()
{
  wchar_t buf[260];
  int thumb_width, thumb_height, thumb_margin;
  int i, j, n;

  menu_thumbnail_dimensions(&thumb_width, &thumb_height, &thumb_margin);

  XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[0],
                       0, 0, window_width, window_height);

  if(background.pic)
  {
    XRenderComposite(display, PictOpSrc, background.pic, None, root_buffer,
                     0, 0, 0, 0, 0, 0, window_width, window_height);
  }

  int margin_x = window_width * 75 / 1000;
  int margin_y = window_height * 75 / 1000;

  int y = margin_y;
  int x = margin_x;

  pthread_mutex_lock(&menu_mutex);

  for(i = offset, n = 0; i < menu_item_count; ++i)
  {
    size_t querylen = wcslen(query);

    if(y > window_height - 2 * thumb_height - 2 * thumb_margin - yskips[SMALL] - 100)
      break;

    if(filter(i))
      continue;

    if(!menu_items[i].icon.pic && menu_items[i].icon_basename)
      load_icon(&menu_items[i]);

    if(menu_items[i].icon.pic)
    {
      if(menu_items[i].icon.width != 48 || menu_items[i].icon.height != 48)
      {
        XTransform xform;

        memset(&xform, 0, sizeof(xform));
        xform.matrix[0][0] = XDoubleToFixed(menu_items[i].icon.width / 48.0);
        xform.matrix[1][1] = XDoubleToFixed(menu_items[i].icon.height / 48.0);
        xform.matrix[2][2] = XDoubleToFixed(1.0);

        XRenderSetPictureTransform(display, menu_items[i].icon.pic, &xform);
        XRenderComposite(display, PictOpOver, menu_items[i].icon.pic, None, root_buffer,
                         0, 0, 0, 0,
                         x, y + 8, 48, 48);
      }
      else
      {
        XRenderComposite(display, PictOpOver, menu_items[i].icon.pic, None, root_buffer,
                         0, 0, 0, 0, x, y + 8, 48, 48);
      }
    }

    drawtext(root_buffer, menu_items[i].name, wcslen(menu_items[i].name), x + 56 + 1, y + 1, 0, LARGE);

    if(query[0])
    {
      for(j = 0; menu_items[i].name[j]; )
      {
        if(!wcsncasecmp(query, &menu_items[i].name[j], querylen))
        {
          drawtext(root_buffer, menu_items[i].name + j, querylen, x + 56 + j * xskips[LARGE], y, 14, LARGE);
          j += querylen;
        }
        else
        {
          drawtext(root_buffer, menu_items[i].name + j, 1, x + 56 + j * xskips[LARGE], y,
                   (n == cursor) ? 10 : 15, LARGE);
          ++j;
        }
      }
    }
    else
    {
      drawtext(root_buffer, menu_items[i].name, wcslen(menu_items[i].name), x + 56, y,
               (n == cursor) ? 10 : 15, LARGE);
    }

    if(menu_items[i].shortcut)
    {
      drawtext_bar(root_buffer, menu_items[i].shortcut, wcslen(menu_items[i].shortcut), x + 58, y + yskips[LARGE] + 1);
    }
    else if(n < 10)
    {
      if(n == cursor)
        swprintf(buf, sizeof(buf), L"Press Enter to launch `%s`",  menu_items[i].command);
      else
        swprintf(buf, sizeof(buf), L"Press Super+%d to launch `%s`", (n + 1) % 10, menu_items[i].command);

      if(menu_items[i].flags & FLAG_TERMINAL)
        wcscat(buf, L" in a terminal");

      drawtext_bar(root_buffer, buf, wcslen(buf), x + 58, y + yskips[LARGE] + 1);
    }

    y += 80;
    ++n;
  }

  pthread_mutex_unlock(&menu_mutex);

  swprintf(buf, sizeof(buf), L"Query: %ls", query);
  y = window_height - 2 * thumb_height - 2 * thumb_margin - yskips[SMALL] - yskips[LARGE] - 15;
  drawtext(root_buffer, buf, wcslen(buf), thumb_margin + 1, y + 1, 0, LARGE);
  drawtext(root_buffer, buf, wcslen(buf), thumb_margin, y, 15, LARGE);

  page_height = n;

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

static void launch_item(int n)
{
  char command[4096];
  const char* in;
  char* out;
  int i;

  if(n < 0 || n >= menu_item_count)
    return;

  for(i = 0; i < menu_item_count; ++i)
  {
    if(filter(i))
      continue;

    if(!n--)
    {
      in = menu_items[i].command;
      out = command;

      while(*in)
      {
        if(*in == '%')
        {
          in += 2;
        }
        else
          *out++ = *in++;
      }

      *out = 0;

      launch(command);
    }
  }
}

void menu_keypress(int key_sym, const char* text, int textlen)
{
  switch(key_sym)
  {
  case XK_Down:

    if(cursor < page_height - 1)
      ++cursor;
    else
      ++offset;

    break;

  case XK_Up:

    if(cursor > 0)
      --cursor;
    else
      --offset;

    break;

  case XK_Page_Down:

    offset += page_height;

    break;

  case XK_Page_Up:

    if(offset > page_height)
      offset -= page_height;
    else if(offset)
      offset = 0;
    else
      cursor = 0;

    break;

  case XK_Home:

    offset = 0;

    break;

  case XK_End:

    offset = menu_item_count;

    break;

  case XK_Escape:

    query[0] = 0;

    break;

  case XK_Return:

    launch_item(offset + cursor);

    break;

  case XK_BackSpace:

    if(wcslen(query))
      query[wcslen(query) - 1] = 0;

    break;

  default:

    return;
  }

  if(offset < 0)
    offset = 0;

  if(offset >= menu_item_count - page_height)
  {
    if(menu_item_count > page_height)
      offset = menu_item_count - page_height;
    else
      offset = 0;
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
    offset = 0;
    cursor = 0;

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

      offset = 0;
      cursor = 0;

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
