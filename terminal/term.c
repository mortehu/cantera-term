#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <pty.h>
#include <sched.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmp.h>
#include <wchar.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>
#include <X11/keysym.h>

#include "common.h"
#include "font.h"
#include "tree.h"

#define PARTIAL_REPAINT 1

struct tree* config = 0;

unsigned int scroll_extra;
const char* font_name;

extern char** environ;

int xskips[] = { 1, 1 };
int yskips[] = { 1, 1 };
int font_sizes[] = { 12, 36 };

unsigned int palette[16];

struct terminal
{
  Window window;

  pid_t pid;
  int fd;

  char* buffer;
  wchar_t* chars[2];
  uint16_t* attr[2];
  wchar_t* curchars;
  uint16_t* curattrs;
  int offset[2];
  int* curoffset;
  int curscreen;
  int curattr;
  int reverse;
  struct winsize size;
  unsigned int history_size;
  int fontsize;
  int xskip;
  int yskip;
  int storedcursorx[2];
  int storedcursory[2];
  int scrolltop;
  int scrollbottom;
  int cursorx;
  int cursory;
  int escape;
  int param[8];
  int savedx;
  int savedy;
  int appcursor;
  int insertmode;
  unsigned int ch, nch;

  unsigned int history_scroll;

  int document;
  unsigned char* doc_buf;
  size_t doc_buf_size;
  size_t doc_buf_payload;
  size_t doc_buf_alloc;

  struct
  {
    struct cnt_image* image;
    Picture pic;
    size_t width, height;
    int char_x, char_y;
    int rows, cols;
    int screen;
  } images[256];

  size_t image_count;
};

static int done;
static const char* session_path;

XRenderColor xrpalette[sizeof(palette) / sizeof(palette[0])];
Picture picpalette[sizeof(palette) / sizeof(palette[0])];
Picture picgradients[256];

int window_width, window_height;

static void normalize_offset();

#define ATTR_BLINK     0x0008
#define ATTR_HIGHLIGHT 0x0008
#define ATTR_BOLD      0x0008
#define ATTR_STANDOUT  0x0008
#define ATTR_UNDERLINE 0x0800
#define ATTR_BLACK     0x0000
#define ATTR_BLUE      0x0001
#define ATTR_GREEN     0x0002
#define ATTR_RED       0x0004
#define ATTR_CYAN      (ATTR_BLUE | ATTR_GREEN)
#define ATTR_MAGENTA   (ATTR_BLUE | ATTR_RED)
#define ATTR_YELLOW    (ATTR_GREEN | ATTR_RED)
#define ATTR_WHITE     (ATTR_BLUE | ATTR_GREEN | ATTR_RED)
#define FG(color)      color
#define BG(color)      ((color) << 4)
#define FG_DEFAULT     FG(ATTR_WHITE)
#define BG_DEFAULT     BG(ATTR_BLACK)
#define ATTR_DEFAULT   (FG_DEFAULT | BG_DEFAULT)
#define REVERSE(color) ((((color) & 0x70) >> 4) | (((color) & 0x07) << 4) | ((color) & 0x88))

const struct
{
  uint16_t index;
  uint16_t and_mask;
  uint16_t or_mask;
} ansi_helper[] =
{
  {  0, 0,               ATTR_DEFAULT },
  {  1, ~ATTR_BOLD,      ATTR_BOLD },
  {  2, ~ATTR_BOLD,      0 },
  {  3, ~ATTR_STANDOUT,  ATTR_STANDOUT },
  {  4, ~ATTR_UNDERLINE, ATTR_UNDERLINE },
  {  5, (uint16_t) ~ATTR_BLINK,     ATTR_BLINK },
  /* 7 = reverse video */
  {  8, 0,               0 },
  { 22, ~ATTR_BOLD & ~ATTR_STANDOUT & ~ATTR_UNDERLINE, 0 },
  { 23, ~ATTR_STANDOUT,  0 },
  { 24, ~ATTR_UNDERLINE, 0 },
  { 25, (uint16_t) ~ATTR_BLINK,     0 },
  /* 27 = no reverse */
  { 30, ~FG(ATTR_WHITE), FG(ATTR_BLACK) },
  { 31, ~FG(ATTR_WHITE), FG(ATTR_RED) },
  { 32, ~FG(ATTR_WHITE), FG(ATTR_GREEN) },
  { 33, ~FG(ATTR_WHITE), FG(ATTR_YELLOW) },
  { 34, ~FG(ATTR_WHITE), FG(ATTR_BLUE) },
  { 35, ~FG(ATTR_WHITE), FG(ATTR_MAGENTA) },
  { 36, ~FG(ATTR_WHITE), FG(ATTR_CYAN) },
  { 37, ~FG(ATTR_WHITE), FG(ATTR_WHITE) },
  { 39, ~FG(ATTR_WHITE), FG_DEFAULT },
  { 40, ~BG(ATTR_WHITE), BG(ATTR_BLACK) },
  { 41, ~BG(ATTR_WHITE), BG(ATTR_RED) },
  { 42, ~BG(ATTR_WHITE), BG(ATTR_GREEN) },
  { 43, ~BG(ATTR_WHITE), BG(ATTR_YELLOW) },
  { 44, ~BG(ATTR_WHITE), BG(ATTR_BLUE) },
  { 45, ~BG(ATTR_WHITE), BG(ATTR_MAGENTA) },
  { 46, ~BG(ATTR_WHITE), BG(ATTR_CYAN) },
  { 47, ~BG(ATTR_WHITE), BG(ATTR_WHITE) },
  { 49, ~BG(ATTR_WHITE), BG_DEFAULT }
};

struct terminal terminal;
Display* display;
int damage_eventbase;
int damage_errorbase;
int screenidx;
Screen* screen;
Visual* visual;
XVisualInfo visual_template;
XVisualInfo* visual_info;
XSetWindowAttributes window_attr;
Window window;
Atom prop_paste;
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
int ctrl_pressed = 0;
int mod1_pressed = 0;
int super_pressed = 0;
int shift_pressed = 0;
int button1_pressed = 0;
struct timeval lastpaint = { 0, 0 };
wchar_t* screenchars;
uint16_t* screenattrs;
int select_begin = -1;
int select_end = -1;
unsigned char* select_text = 0;
unsigned long select_alloc = 0;
unsigned long select_length;
Picture root_picture;
Picture root_buffer;

#define my_isprint(c) (isprint((c)) || ((c) >= 0x80))

void *memset16(void *s, int w, size_t n)
{
  uint16_t* o = s;

  assert(!(n & 1));

  n >>= 1;

  while(n--)
    *o++ = w;

  return s;
}

static void setscreen(int screen)
{
  terminal.storedcursorx[terminal.curscreen] = terminal.cursorx;
  terminal.storedcursory[terminal.curscreen] = terminal.cursory;

  terminal.curscreen = screen;
  terminal.curchars = terminal.chars[screen];
  terminal.curattrs = terminal.attr[screen];
  terminal.cursorx = terminal.storedcursorx[screen];
  terminal.cursory = terminal.storedcursory[screen];
  terminal.curoffset = &terminal.offset[screen];
}

static void insert_chars(int count)
{
  int k;
  int size;

  size = terminal.size.ws_col * terminal.history_size;

  for(k = terminal.size.ws_col; k-- > terminal.cursorx + count; )
  {
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curchars[(terminal.cursory * terminal.size.ws_col + k - count + *terminal.curoffset) % size];
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k - count + *terminal.curoffset) % size];
  }

  while(k >= terminal.cursorx)
  {
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 'X';
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
    --k;
  }
}

static void addchar(int ch)
{
  int size;

  size = terminal.size.ws_col * terminal.history_size;

  if(ch < 32)
    return;

  if(ch == 0x7f || ch >= 65536)
  {
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = 0;
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = terminal.curattr;

    return;
  }

  int width = wcwidth(ch);

  if(!width)
    return;

  if(width > 1)
  {
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = ch;
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + 1 + *terminal.curoffset) % size] = 0xffff;
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] =
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + 1 + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
    terminal.cursorx += width;

    if(terminal.cursorx > terminal.size.ws_col)
      terminal.cursorx = terminal.size.ws_col;
  }
  else
  {
    if(terminal.insertmode)
      insert_chars(1);

    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = ch;
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
    ++terminal.cursorx;
  }
}

static void paint(int x, int y, int width, int height)
{
  int row, col, i, selbegin, selend;
  int minx = x;
  int miny = y;
  int maxx = x + width;
  int maxy = y + height;
  unsigned int size;
  int in_selection = 0;

  size = terminal.history_size * terminal.size.ws_col;

  if(terminal.image_count)
  {
    XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[0], 0, 0, window_width, window_height);
    memset(screenchars, 0xff, cols * rows * sizeof(wchar_t));
  }

  {
    if(select_begin < select_end)
    {
      selbegin = select_begin;
      selend = select_end;
    }
    else
    {
      selbegin = select_end;
      selend = select_begin;
    }

    selbegin = (selbegin + terminal.history_scroll * terminal.size.ws_col) % size;
    selend = (selend + terminal.history_scroll * terminal.size.ws_col) % size;

    for(row = 0; row < terminal.size.ws_row; ++row)
    {
      size_t pos = ((row + terminal.history_size - terminal.history_scroll) * terminal.size.ws_col + (*terminal.curoffset)) % size;
      wchar_t* screenline = &screenchars[row * terminal.size.ws_col];
      uint16_t* screenattrline = &screenattrs[row * terminal.size.ws_col];
      const wchar_t* line = &terminal.curchars[pos];
      const uint16_t* attrline = &terminal.curattrs[pos];
      int start = 0, end, x = 0;

      while(start < terminal.size.ws_col)
      {
        int width, height;
        int printable;
        int update;
        int attr = attrline[start];
        int localattr = -1;

        if(row == terminal.cursory + terminal.history_scroll && start == terminal.cursorx)
        {
          attr = REVERSE(attr);

          if(!attr)
            attr = BG(ATTR_WHITE);
        }

        printable = (line[start] != 0);

        if(row * terminal.size.ws_col + start == selbegin)
          in_selection = 1;

        if(row * terminal.size.ws_col + start == selend)
          in_selection = 0;

        if(in_selection)
          {
            if(line[start] != screenline[start] && !button1_pressed)
              {
                in_selection = 0;
                select_begin = -1;
                select_end = -1;

                XClearArea(display, window, 0, 0, window_width, window_height, True);
              }
            else
              attr = REVERSE(attr);
          }

#if PARTIAL_REPAINT
        update = (line[start] != screenline[start]) || (attr != screenattrline[start]);
#else
        update = 1;
#endif

        end = start + 1;

        while(end < terminal.size.ws_col)
        {
          localattr = attrline[end];

          if(row * terminal.size.ws_col + end >= selbegin
          && row * terminal.size.ws_col + end < selend)
          {
            if(line[end] != screenline[end] && !button1_pressed)
            {
              selbegin = select_begin = -1;
              selend = select_end = -1;
            }
            else
              localattr = REVERSE(localattr);
          }

          if(localattr != attr)
            break;

          if(row == terminal.cursory && end == terminal.cursorx)
            break;

          if((line[end] != 0) != printable)
            break;

#if PARTIAL_REPAINT
          if(update)
          {
            if(line[end] == screenline[end] && attr == screenattrline[end])
              break;
          }
          else
          {
            if(line[end] != screenline[end] || attr != screenattrline[end])
              break;
          }
#endif

          ++end;
        }

        width = (end - start) * terminal.xskip;
        height = terminal.yskip;

        for(i = start; i < end; ++i)
        {
          screenline[i] = line[i];
          screenattrline[i] = attr;
        }

        if(!printable)
        {
          if(update || !PARTIAL_REPAINT)
          {
            if(x < minx) minx =x;
            if(row * terminal.yskip < miny) miny = row * terminal.yskip;
            if(x + width > maxx) maxx = x + width;
            if(row * terminal.yskip + height > maxy) maxy = row * terminal.yskip + height;
            XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[(attr >> 4) & 0x07], x, row * terminal.yskip, width, height);

            if(attr & ATTR_UNDERLINE)
            {
              XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[attr & 0x0F], x, (row + 1) * terminal.yskip - 1, width, 1);
            }
          }

          x += width;
          start = end;

          continue;
        }

        if(update || !PARTIAL_REPAINT)
        {
          if(x < minx) minx =x;
          if(row * terminal.yskip < miny) miny = row * terminal.yskip;
          if(x + width > maxx) maxx = x + width;
          if(row * terminal.yskip + height > maxy) maxy = row * terminal.yskip + height;
          XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[(attr >> 4) & 0x07], x, row * terminal.yskip, width, height);

          drawtext(root_buffer, &line[start], end - start, x, row * terminal.yskip, attr & 0x0F, SMALL);

          if(attr & ATTR_UNDERLINE)
          {
            XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[attr & 0x0F], x, (row + 1) * terminal.yskip - 1, width, 1);
          }
        }

        x += width;

        start = end;
      }
    }

    for(i = 0; i < terminal.image_count; )
    {
      if(terminal.images[i].screen != terminal.curscreen)
      {
        ++i;

        continue;
      }

      if(terminal.images[i].char_y + terminal.images[i].rows < 0)
      {
        --terminal.image_count;
        cnt_image_free(&terminal.images[i].image);
        memmove(&terminal.images[i], &terminal.images[terminal.image_count], sizeof(terminal.images[0]));
      }
      else
      {
        XRenderComposite(display, PictOpSrc, terminal.images[i].pic, None, root_buffer,
                         0, 0,
                         0, 0,
                         terminal.images[i].char_x * xskips[SMALL],
                         terminal.images[i].char_y * yskips[SMALL],
                         terminal.images[i].width, terminal.images[i].height);

        for(row = terminal.images[i].char_y; row < terminal.images[i].char_y + rows; ++row)
        {
          for(col = terminal.images[i].char_x; col < terminal.images[i].char_x + cols; ++col)
          {
            if(row >= 0 && col >= 0 && row < terminal.size.ws_row && col < terminal.size.ws_col)
              screenchars[row * terminal.size.ws_col + col] = 0xffff;
          }
        }

        ++i;
      }
    }

    i = terminal.size.ws_row * terminal.yskip;

    if(i < window_height)
    {
      XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[0],
                           0, i, window_width, window_height - i);
    }

    i = terminal.size.ws_col * terminal.xskip;

    if(i < window_width)
    {
      XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[0],
                           i, 0,
                           window_width - i, window_height);
    }
  }

  if(root_buffer != root_picture)
  {
    XRenderComposite(display, PictOpSrc, root_buffer, None, root_picture,
                     x, y, 0, 0, x, y, width, height);
  }
}

static void normalize_offset()
{
  int size = terminal.size.ws_col * terminal.history_size;
  int i;

  if(!*terminal.curoffset)
    return;

  for(i = 0; i < 2; ++i)
  {
    int offset = terminal.offset[i];
    wchar_t* tmpchars;
    uint16_t* tmpattrs;

    assert(offset >= 0);
    assert(offset < size);

    tmpchars = malloc(sizeof(wchar_t) * offset);
    tmpattrs = malloc(sizeof(uint16_t) * offset);

    memcpy(tmpchars, terminal.chars[i], sizeof(*tmpchars) * offset);
    memcpy(tmpattrs, terminal.attr[i], sizeof(*tmpattrs) * offset);

    memmove(terminal.chars[i], terminal.chars[i] + offset, sizeof(*tmpchars) * (size - offset));
    memmove(terminal.attr[i], terminal.attr[i] + offset, sizeof(*tmpattrs) * (size - offset));

    memmove(terminal.chars[i] + (size - offset), tmpchars, sizeof(*tmpchars) * offset);
    memmove(terminal.attr[i] + (size - offset), tmpattrs, sizeof(*tmpattrs) * offset);

    terminal.offset[i] = 0;

    free(tmpattrs);
    free(tmpchars);
  }
}

static void scroll(int fromcursor)
{
  size_t i;
  int first, length;

  for(i = 0; i < terminal.image_count; ++i)
  {
    if(terminal.images[i].screen == terminal.curscreen)
      --terminal.images[i].char_y;
  }

  if(!fromcursor && terminal.scrolltop == 0 && terminal.scrollbottom == terminal.size.ws_row)
  {
    size_t clear_offset;

    clear_offset = *terminal.curoffset + terminal.size.ws_row * terminal.size.ws_col;
    clear_offset %= (terminal.size.ws_col * terminal.history_size);

    memset(terminal.curchars + clear_offset, 0, sizeof(*terminal.curchars) * terminal.size.ws_col);
    memset16(terminal.curattrs + clear_offset, terminal.curattr, sizeof(*terminal.curattrs) * terminal.size.ws_col);

    *terminal.curoffset += terminal.size.ws_col;
    *terminal.curoffset %= terminal.size.ws_col * terminal.history_size;

    return;
  }

  normalize_offset();

  if(fromcursor)
  {
    first = terminal.cursory * terminal.size.ws_col;
    length = (terminal.scrollbottom - terminal.cursory - 1) * terminal.size.ws_col;
  }
  else
  {
    first = terminal.scrolltop * terminal.size.ws_col;
    length = (terminal.scrollbottom - terminal.scrolltop - 1) * terminal.size.ws_col;
  }

  memmove(terminal.curchars + first, terminal.curchars + first + terminal.size.ws_col, length * sizeof(wchar_t));
  memset(terminal.curchars + first + length, 0, terminal.size.ws_col * sizeof(wchar_t));

  memmove(terminal.curattrs + first, terminal.curattrs + first + terminal.size.ws_col, sizeof(uint16_t) * length);
  memset16(terminal.curattrs + first + length, terminal.curattr, sizeof(uint16_t) * terminal.size.ws_col);
}

static void rscroll(int fromcursor)
{
  int first, length;

  normalize_offset();

  if(fromcursor)
  {
    first = terminal.cursory * terminal.size.ws_col;
    length = (terminal.scrollbottom - terminal.cursory - 1) * terminal.size.ws_col;
  }
  else
  {
    first = terminal.scrolltop * terminal.size.ws_col;
    length = (terminal.scrollbottom - terminal.scrolltop - 1) * terminal.size.ws_col;
  }

  memmove(terminal.curchars + first + terminal.size.ws_col, terminal.curchars + first, length * sizeof(wchar_t));
  memset(terminal.curchars + first, 0, terminal.size.ws_col * sizeof(wchar_t));

  memmove(terminal.curattrs + first + terminal.size.ws_col, terminal.curattrs + first, sizeof(uint16_t) * length);
  memset16(terminal.curattrs + first, terminal.curattr, sizeof(uint16_t) * terminal.size.ws_col);
}

enum range_type
{
  range_word_or_url,
  range_parenthesis,
  range_line
};

static int find_range(int range, int* begin, int* end)
{
  int i, ch;
  unsigned int offset, size;

  size = terminal.history_size * terminal.size.ws_col;
  offset = *terminal.curoffset;

  if(range == range_word_or_url)
  {
    i = *begin;

    while(i)
    {
      if(!(i % terminal.size.ws_col))
        break;

      ch = terminal.curchars[(offset + i - 1) % size];

      if(ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch))
        break;

      --i;
    }

    *begin = i;

    i = *end;

    while((i % terminal.size.ws_col) < terminal.size.ws_col)
    {
      ch = terminal.curchars[(offset + i) % size];

      if(ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch))
        break;

      ++i;
    }

    *end = i;

    if(*begin == *end)
      return 0;

    return 1;
  }
  else if(range == range_parenthesis)
  {
    int paren_level = 0;
    i = *begin;

    while(i > 0)
    {
      ch = terminal.curchars[(offset + i) % size];

      if((!ch || ((i + 1) % terminal.size.ws_col == 0) || isspace(ch)) && !paren_level)
      {
        ++i;

        break;
      }

      if(ch == ')')
        ++paren_level;

      if(ch == '(')
      {
        if(!paren_level)
          break;

        --paren_level;
      }

      --i;
    }

    *begin = i;

    if(*end > i + 1)
    {
      if(terminal.curchars[(offset + *end - 1) % size] == '=')
        --*end;
    }

    return 1;
  }
  else
    return 0;
}

void init_session(char* const* args)
{
  memset(&terminal, 0, sizeof(terminal));

  terminal.xskip = xskips[terminal.fontsize];
  terminal.yskip = yskips[terminal.fontsize];
  terminal.size.ws_xpixel = window_width;
  terminal.size.ws_ypixel = window_height;
  terminal.size.ws_col = window_width / terminal.xskip;
  terminal.size.ws_row = window_height / terminal.yskip;
  terminal.history_size = terminal.size.ws_row + scroll_extra;

  terminal.pid = forkpty(&terminal.fd, 0, 0, &terminal.size);

  if(terminal.pid)
  {
    char* c;

    terminal.buffer = calloc(2 * terminal.size.ws_col * terminal.history_size, sizeof(wchar_t) + sizeof(uint16_t));
    c = terminal.buffer;
    terminal.chars[0] = (wchar_t*) c; c += terminal.size.ws_col * terminal.history_size * sizeof(wchar_t);
    terminal.attr[0] = (uint16_t*) c; c += terminal.size.ws_col * terminal.history_size * sizeof(uint16_t);
    terminal.chars[1] = (wchar_t*) c; c += terminal.size.ws_col * terminal.history_size * sizeof(wchar_t);
    terminal.attr[1] = (uint16_t*) c;
    terminal.curattr = 0x07;
    terminal.scrollbottom = terminal.size.ws_row;
    memset(terminal.chars[0], 0, terminal.size.ws_col * terminal.history_size * sizeof(wchar_t));
    memset16(terminal.attr[0], terminal.curattr, sizeof(uint16_t) * terminal.size.ws_col * terminal.history_size);
    memset16(terminal.attr[1], terminal.curattr, sizeof(uint16_t) * terminal.size.ws_col * terminal.history_size);
    terminal.offset[0] = 0;
    terminal.offset[1] = 0;

    setscreen(0);
  }
  else
  {
    terminal.xskip = xskips[terminal.fontsize];
    terminal.yskip = yskips[terminal.fontsize];
    execve(args[0], args, environ);

    exit(EXIT_FAILURE);
  }
}

static void x11_connect(const char* display_name)
{
  int i;
  int nitems;
  char* c;
  long pid;

  pid = getpid();

  XInitThreads();

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

  window_attr.colormap = DefaultColormap(display, 0);
  window_attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | ExposureMask;

  window = XCreateWindow(display, RootWindow(display, screenidx), 0, 0, window_width, window_height, 0, visual_info->depth, InputOutput, visual, CWColormap | CWEventMask | CWCursor, &window_attr);

  prop_paste = XInternAtom(display, "CANTERA_PASTE", False);
  xa_utf8_string = XInternAtom(display, "UTF8_STRING", False);
  xa_compound_text = XInternAtom(display, "COMPOUND_TEXT", False);
  xa_targets = XInternAtom(display, "TARGETS", False);
  xa_wm_state = XInternAtom(display, "WM_STATE", False);
  xa_net_wm_icon = XInternAtom(display, "_NET_WM_ICON", False);
  xa_net_wm_pid = XInternAtom(display, "_NET_WM_PID", False);
  xa_wm_transient_for = XInternAtom(display, "WM_TRANSIENT_FOR", False);
  xa_wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
  xa_wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);

  XChangeProperty(display, window, xa_net_wm_pid, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *) &pid, 1);

  XStoreName(display, window, "cantera-term");
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

  cols = window_width / xskips[0];
  rows = window_height / yskips[0];

  screenchars = calloc(cols * rows, sizeof(*screenchars));
  screenattrs = calloc(cols * rows, sizeof(*screenattrs));
  memset(screenchars, 0xff, cols * rows * sizeof(*screenchars));

  XSynchronize(display, False);

  XClearArea(display, window, 0, 0, window_width, window_height, True);
}

static void save_session()
{
  int fd;
  size_t size;

  if(!session_path)
    return;

  fd = open(session_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  if(fd == -1)
    return;

  normalize_offset();

  size = terminal.history_size * terminal.size.ws_col;

  write(fd, &terminal.size, sizeof(terminal.size));
  write(fd, &terminal.cursorx, sizeof(terminal.cursorx));
  write(fd, &terminal.cursory, sizeof(terminal.cursory));
  write(fd, terminal.chars[0], size * sizeof(*terminal.chars[0]));
  write(fd, terminal.attr[0], size * sizeof(*terminal.attr[0]));

  close(fd);
}

static void sighandler(int signal)
{
  static int first = 1;

  fprintf(stderr, "Got signal %d\n", signal);

  if(first)
    {
      first = 0;
      save_session();
    }

  exit(EXIT_SUCCESS);
}

static void update_selection(Time time)
{
  int i;
  unsigned int size, offset;

  if(select_begin == select_end)
    return;

  size = terminal.size.ws_col * terminal.history_size;
  offset = *terminal.curoffset;

  if(select_text)
  {
    free(select_text);
    select_text = 0;
  }

  if(select_begin > select_end)
  {
    i = select_begin;
    select_begin = select_end;
    select_end = i;
  }

  select_alloc = select_end - select_begin + 1;
  select_text = calloc(select_alloc, 1);
  select_length = 0;

  size_t last_graph = 0;
  size_t last_graph_col = 0;
  i = select_begin;

  while(i != select_end)
  {
    int ch = terminal.curchars[(i + offset) % size];
    int width = terminal.size.ws_col;

    if(ch == 0 || ch == 0xffff)
      ch = ' ';

    if(select_length + 4 > select_alloc)
    {
      select_alloc *= 2;
      select_text = realloc(select_text, select_alloc);
    }

    if(i > select_begin && (i % width) == 0)
    {
      select_length = last_graph;
      if(last_graph_col != (width - 1))
        select_text[select_length++] = '\n';
      last_graph = select_length;
    }

    if(ch < 0x80)
    {
      select_text[select_length++] = ch;
    }
    else if(ch < 0x800)
    {
      select_text[select_length++] = 0xC0 | (ch >> 6);
      select_text[select_length++] = 0x80 | (ch & 0x3F);
    }
    else if(ch < 0x10000)
    {
      select_text[select_length++] = 0xE0 | (ch >> 12);
      select_text[select_length++] = 0x80 | ((ch >> 6) & 0x3F);
      select_text[select_length++] = 0x80 | (ch & 0x3f);
    }

    if(ch != ' ')
    {
      last_graph = select_length;
      last_graph_col = i % width;
    }

    ++i;
  }

  select_length = last_graph;
  select_text[select_length] = 0;

  XSetSelectionOwner(display, XA_PRIMARY, window, time);

  if(window != XGetSelectionOwner(display, XA_PRIMARY))
  {
    select_begin = select_end;
    free(select_text);
    select_text = 0;
  }
}

static void paste(Time time)
{
  Window selowner;

  selowner = XGetSelectionOwner(display, XA_PRIMARY);

  if(selowner == None)
    return;

  XDeleteProperty(display, window, prop_paste);

  XConvertSelection(display, XA_PRIMARY, xa_utf8_string, prop_paste, window, time);
}

static void append_doc_data(void* data, size_t size);

static void process_data(unsigned char* buf, int count)
{
  int j, k, l;

  int size = terminal.size.ws_col * terminal.history_size;

  /* XXX: Make sure cursor does not leave screen */

  j = 0;

  /* Redundant character processing code for the typical case */
  if(!terminal.escape && !terminal.insertmode && !terminal.nch)
  {
    for(; j < count; ++j)
    {
      if(buf[j] >= ' ' && buf[j] <= '~')
      {
        if(terminal.cursorx == terminal.size.ws_col)
        {
          ++terminal.cursory;
          terminal.cursorx = 0;
        }

        while(terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }

        terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = buf[j];
        terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
        ++terminal.cursorx;
      }
      else if(buf[j] == '\r')
        terminal.cursorx = 0;
      else if(buf[j] == '\n')
      {
        ++terminal.cursory;

        while(terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }
      }
      else
        break;
    }
  }

  for(; j < count; ++j)
  {
    switch(terminal.escape)
    {
    case 0:

      switch(buf[j])
      {
      case '\033':

        terminal.escape = 1;
        memset(terminal.param, 0, sizeof(terminal.param));

        break;

      case '\b':

        if(terminal.cursorx > 0)
          --terminal.cursorx;

        break;

      case '\t':

        if(terminal.cursorx < terminal.size.ws_col - 8)
        {
          terminal.cursorx = (terminal.cursorx + 8) & ~7;
        }
        else
        {
          terminal.cursorx = terminal.size.ws_col - 1;
        }

        break;

      case '\n':

        ++terminal.cursory;

        while(terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }

        break;

      case '\r':

        terminal.cursorx = 0;

        break;

      case '\177':

        if(terminal.cursory < terminal.size.ws_row)
          terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = 0;

        break;

      case ('O' & 0x3F): /* ^O = default character set */

        break;

      case ('N' & 0x3F): /* ^N = alternate character set */

        break;

      default:

        assert(terminal.cursorx >= 0 && terminal.cursorx <= terminal.size.ws_col);
        assert(terminal.cursory >= 0 && terminal.cursory < terminal.size.ws_row);

        if(terminal.cursorx == terminal.size.ws_col)
        {
          ++terminal.cursory;
          terminal.cursorx = 0;
        }

        while(terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }

        if(terminal.nch)
        {
          if((buf[j] & 0xC0) != 0x80)
          {
            terminal.nch = 0;
            addchar(buf[j]);
          }
          else
          {
            terminal.ch <<= 6;
            terminal.ch |= buf[j] & 0x3F;

            if(0 == --terminal.nch)
            {
              addchar(terminal.ch);
            }
          }
        }
        else
        {
          if((buf[j] & 0x80) == 0)
          {
            addchar(buf[j]);
          }
          else if((buf[j] & 0xE0) == 0xC0)
          {
            terminal.ch = buf[j] & 0x1F;
            terminal.nch = 1;
          }
          else if((buf[j] & 0xF0) == 0xE0)
          {
            terminal.ch = buf[j] & 0x0F;
            terminal.nch = 2;
          }
          else if((buf[j] & 0xF8) == 0xF0)
          {
            terminal.ch = buf[j] & 0x03;
            terminal.nch = 3;
          }
          else if((buf[j] & 0xFC) == 0xF8)
          {
            terminal.ch = buf[j] & 0x01;
            terminal.nch = 4;
          }
        }
      }

      break;

    case 1:

      if(buf[j] == '[')
      {
        terminal.escape = 2;

        memset(terminal.param, 0, sizeof(terminal.param));
      }
      else if(buf[j] == '%')
      {
        terminal.escape = 2;
        terminal.param[0] = -1;
      }
      else if(buf[j] == ']')
      {
        terminal.escape = 2;
        terminal.param[0] = -2;
      }
      else if(buf[j] == '(')
      {
        terminal.escape = 2;
        terminal.param[0] = -4;
      }
      else if(buf[j] == 'M')
      {
        if(terminal.cursorx == 0 && terminal.cursory == terminal.scrolltop)
        {
          rscroll(0);
        }
        else if(terminal.cursory)
        {
          --terminal.cursory;
        }

        terminal.escape = 0;
      }
      else
      {
        terminal.escape = 0;
      }

      break;

    default:

      if(terminal.param[0] == -1)
      {
        terminal.escape = 0;
      }
      else if(terminal.param[0] == -2)
      {
        /* Handle ESC ] Ps ; Pt BEL */
        if(terminal.escape == 2)
        {
          if(buf[j] >= '0' && buf[j] <= '9')
          {
            terminal.param[1] *= 10;
            terminal.param[1] += buf[j] - '0';
          }
          else
            ++terminal.escape;
        }
        else
        {
          if(buf[j] != '\007')
          {
            /* XXX: Store text */
          }
          else
            terminal.escape = 0;
        }
      }
      else if(terminal.param[0] == -4)
      {
        terminal.escape = 0;
      }
      else if(terminal.escape == 2 && buf[j] == '?')
      {
        terminal.param[0] = -3;
        ++terminal.escape;
      }
      else if(terminal.escape == 2 && buf[j] == '>')
      {
        terminal.param[0] = -4;
        ++terminal.escape;
      }
      else if(buf[j] == ';')
      {
        if(terminal.escape < sizeof(terminal.param) + 1)
          terminal.param[++terminal.escape - 2] = 0;
        else
          terminal.param[(sizeof(terminal.param) / sizeof(terminal.param[0])) - 1] = 0;
      }
      else if(buf[j] >= '0' && buf[j] <= '9')
      {
        terminal.param[terminal.escape - 2] *= 10;
        terminal.param[terminal.escape - 2] += buf[j] - '0';
      }
      else if(terminal.param[0] == -3)
      {
        if(buf[j] == 'h')
        {
          for(k = 1; k < terminal.escape - 1; ++k)
          {
            switch(terminal.param[k])
            {
            case 1:

              terminal.appcursor = 1;

              break;

            case 1049:

              if(terminal.curscreen != 1)
              {
                memset(terminal.chars[1], 0, terminal.size.ws_col * terminal.history_size * sizeof(wchar_t));
                memset(terminal.attr[1], 0x07, terminal.size.ws_col * terminal.history_size * sizeof(uint16_t));
                setscreen(1);
              }

              break;

            case 0xD0C:

              terminal.document = 1;
              terminal.escape = 0;

              if(j + 1 < count)
                append_doc_data(&buf[j + 1], count - j - 1);

              return;
            }
          }
        }
        else if(buf[j] == 'l')
        {
          for(k = 1; k < terminal.escape - 1; ++k)
          {
            switch(terminal.param[k])
            {
            case 1:

              terminal.appcursor = 0;

              break;

            case 1049:

              if(terminal.curscreen != 0)
                setscreen(0);

              break;

            case 0xD0C:

              free(terminal.doc_buf);
              terminal.doc_buf = 0;
              terminal.doc_buf_size = 0;
              terminal.doc_buf_payload = 0;
              terminal.doc_buf_alloc = 0;
              terminal.document = 0;

              break;
            }
          }
        }

        terminal.escape = 0;
      }
      else
      {
        switch(buf[j])
        {
          case '@':

            if(!terminal.param[0])
              terminal.param[0] = 1;

            insert_chars(terminal.param[0]);

            break;

          case 'A':

            if(!terminal.param[0])
              terminal.param[0] = 1;

            terminal.cursory -= (terminal.param[0] < terminal.cursory) ? terminal.param[0] : terminal.cursory;

            break;

          case 'B':

            if(!terminal.param[0])
              terminal.param[0] = 1;

            terminal.cursory = (terminal.param[0] + terminal.cursory < terminal.size.ws_row) ? (terminal.param[0] + terminal.cursory) : (terminal.size.ws_row - 1);

            break;

          case 'C':

            if(!terminal.param[0])
              terminal.param[0] = 1;

            terminal.cursorx = (terminal.param[0] + terminal.cursorx < terminal.size.ws_col) ? (terminal.param[0] + terminal.cursorx) : (terminal.size.ws_col - 1);

            break;

          case 'D':

            if(!terminal.param[0])
              terminal.param[0] = 1;

            terminal.cursorx -= (terminal.param[0] < terminal.cursorx) ? terminal.param[0] : terminal.cursorx;

            break;

          case 'G':

            if(terminal.param[0] > 0)
              --terminal.param[0];

            terminal.cursorx = (terminal.param[0] < terminal.size.ws_col) ? terminal.param[0] : (terminal.size.ws_col - 1);

            break;

          case 'H':
          case 'f':

            if(terminal.param[0] > 0)
              --terminal.param[0];

            if(terminal.param[1] > 0)
              --terminal.param[1];

            terminal.cursory = (terminal.param[0] < terminal.size.ws_row) ? terminal.param[0] : (terminal.size.ws_row - 1);
            terminal.cursorx = (terminal.param[1] < terminal.size.ws_col) ? terminal.param[1] : (terminal.size.ws_col - 1);

            break;

          case 'J':

            if(terminal.param[0] == 0)
            {
              /* Clear from cursor to end */

              normalize_offset();

              int count = terminal.size.ws_col * (terminal.size.ws_row - terminal.cursory - 1) + (terminal.size.ws_col - terminal.cursorx);
              memset(&terminal.curchars[terminal.cursory * terminal.size.ws_col + terminal.cursorx], 0, count * sizeof(wchar_t));
              memset16(&terminal.curattrs[terminal.cursory * terminal.size.ws_col + terminal.cursorx], terminal.curattr, count * sizeof(uint16_t));

              for(k = 0; k < terminal.image_count; )
              {
                if(terminal.images[k].screen == terminal.curscreen
                && terminal.images[k].char_y + terminal.images[k].rows > terminal.cursory)
                {
                  --terminal.image_count;
                  cnt_image_free(&terminal.images[k].image);
                  memmove(&terminal.images[k], &terminal.images[terminal.image_count], sizeof(terminal.images[0]));
                }
                else
                  ++k;
              }
            }
            else if(terminal.param[0] == 1)
            {
              /* Clear from start to cursor */

              normalize_offset();

              int count = (terminal.size.ws_col * terminal.cursory + terminal.cursorx);
              memset(terminal.curchars, 0, count * sizeof(wchar_t));
              memset16(terminal.curattrs, terminal.curattr, count * sizeof(uint16_t));

              for(k = 0; k < terminal.image_count; )
              {
                if(terminal.images[k].screen == terminal.curscreen
                && terminal.images[k].char_y <= terminal.cursory)
                {
                  --terminal.image_count;
                  cnt_image_free(&terminal.images[k].image);
                  memmove(&terminal.images[k], &terminal.images[terminal.image_count], sizeof(terminal.images[0]));
                }
                else
                  ++k;
              }
            }
            else if(terminal.param[0] == 2)
            {
              size_t screen_size, history_size;

              screen_size = terminal.size.ws_col * terminal.size.ws_row;
              history_size = terminal.size.ws_col * terminal.history_size;

              if(*terminal.curoffset + screen_size > history_size)
                {
                  memset(terminal.curchars + *terminal.curoffset, 0, (history_size - *terminal.curoffset) * sizeof(wchar_t));
                  memset(terminal.curchars, 0, (screen_size + *terminal.curoffset - history_size) * sizeof(wchar_t));

                  memset16(terminal.curattrs + *terminal.curoffset, 0x07, (history_size - *terminal.curoffset) * sizeof(uint16_t));
                  memset16(terminal.curattrs, 0x07, (screen_size + *terminal.curoffset - history_size) * sizeof(uint16_t));
                }
              else
                {
                  memset(terminal.curchars + *terminal.curoffset, 0, screen_size * sizeof(wchar_t));
                  memset16(terminal.curattrs + *terminal.curoffset, 0x07, screen_size * sizeof(uint16_t));
                }

              terminal.cursory = 0;
              terminal.cursorx = 0;

              for(k = 0; k < terminal.image_count; )
                {
                  if(terminal.images[k].screen == terminal.curscreen)
                    {
                      --terminal.image_count;
                      cnt_image_free(&terminal.images[k].image);
                      memmove(&terminal.images[k], &terminal.images[terminal.image_count], sizeof(terminal.images[0]));
                    }
                  else
                    ++k;
                }
            }

            break;

          case 'K':

            if(!terminal.param[0])
            {
              /* Clear from cursor to end */

              for(k = terminal.cursorx; k < terminal.size.ws_col; ++k)
              {
                terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 0;
                terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
                terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
              }
            }
            else if(terminal.param[0] == 1)
            {
              /* Clear from start to cursor */

              for(k = 0; k <= terminal.cursorx; ++k)
              {
                terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 0;
                terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
                terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
              }
            }
            else if(terminal.param[0] == 2)
            {
              /* Clear entire line */

              for(k = 0; k < terminal.size.ws_col; ++k)
              {
                terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 0;
                terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
                terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
              }
            }

            break;

          case 'L':

            if(!terminal.param[0])
              terminal.param[0] = 1;
            else if(terminal.param[0] > terminal.size.ws_row)
              terminal.param[0] = terminal.size.ws_row;

            while(terminal.param[0]--)
              rscroll(1);

            break;

          case 'M':

            if(!terminal.param[0])
              terminal.param[0] = 1;
            else if(terminal.param[0] > terminal.size.ws_row)
              terminal.param[0] = terminal.size.ws_row;

            while(terminal.param[0]--)
              scroll(1);

            break;

          case 'P':

            /* Delete character at cursor */

            normalize_offset();

            if(!terminal.param[0])
              terminal.param[0] = 1;
            else if(terminal.param[0] > terminal.size.ws_col)
              terminal.param[0] = terminal.size.ws_col;

            while(terminal.param[0]--)
            {
              memmove(&terminal.curchars[terminal.cursory * terminal.size.ws_col + terminal.cursorx],
                  &terminal.curchars[terminal.cursory * terminal.size.ws_col + terminal.cursorx + 1], (terminal.size.ws_col - terminal.cursorx - 1) * sizeof(wchar_t));
              memmove(&terminal.curattrs[terminal.cursory * terminal.size.ws_col + terminal.cursorx],
                  &terminal.curattrs[terminal.cursory * terminal.size.ws_col + terminal.cursorx + 1], (terminal.size.ws_col - terminal.cursorx - 1) * sizeof(uint16_t));
              terminal.curchars[(terminal.cursory + 1) * terminal.size.ws_col - 1] = 0;
              terminal.curattrs[(terminal.cursory + 1) * terminal.size.ws_col - 1] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
            }

            break;

          case 'S':

            if(!terminal.param[0])
              terminal.param[0] = 1;

            while(terminal.param[0]--)
              scroll(0);

            break;

          case 'T':

            if(!terminal.param[0])
              terminal.param[0] = 1;

            while(terminal.param[0]--)
              rscroll(0);

            break;

          case 'X':

            if(terminal.param[0] <= 0)
              terminal.param[0] = 1;

            for(k = terminal.cursorx; k < terminal.cursorx + terminal.param[0] && k < terminal.size.ws_col; ++k)
            {
              terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 0;
              terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
              terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
            }

            break;

          case 'd':

            if(terminal.param[0] > 0)
              --terminal.param[0];
            else
              terminal.param[0] = 0;

            terminal.cursory = (terminal.param[0] < terminal.size.ws_row) ? terminal.param[0] : (terminal.size.ws_row - 1);

            break;

          case 'h':

            for(k = 0; k < terminal.escape - 1; ++k)
            {
              switch(terminal.param[k])
              {
                case 4:

                  terminal.insertmode = 1;

                  break;
              }
            }

            break;

          case 'l':

            for(k = 0; k < terminal.escape - 1; ++k)
            {
              switch(terminal.param[k])
              {
                case 4:

                  terminal.insertmode = 0;

                  break;
              }
            }

            break;

          case 'm':

            for(k = 0; k < terminal.escape - 1; ++k)
            {
              switch(terminal.param[k])
              {
                case 7:

                  terminal.reverse = 1;

                  break;

                case 27:

                  terminal.reverse = 0;

                  break;

                case 0:

                  terminal.reverse = 0;

                default:

                  for(l = 0; l < sizeof(ansi_helper) / sizeof(ansi_helper[0]); ++l)
                  {
                    if(ansi_helper[l].index == terminal.param[k])
                    {
                      terminal.curattr &= ansi_helper[l].and_mask;
                      terminal.curattr |= ansi_helper[l].or_mask;

                      break;
                    }
                  }

                  break;
              }
            }

            break;

          case 'r':

            if(terminal.param[0] < terminal.param[1])
            {
              --terminal.param[0];

              if(terminal.param[1] > terminal.size.ws_row)
                terminal.param[1] = terminal.size.ws_row;

              if(terminal.param[0] < 0)
                terminal.param[0] = 0;

              terminal.scrolltop = terminal.param[0];
              terminal.scrollbottom = terminal.param[1];
            }
            else
            {
              terminal.scrolltop = 0;
              terminal.scrollbottom = terminal.size.ws_row;
            }

            break;

          case 's':

            terminal.savedx = terminal.cursorx;
            terminal.savedy = terminal.cursory;

            break;

          case 'u':

            terminal.cursorx = terminal.savedx;
            terminal.cursory = terminal.savedy;

            break;
        }

        terminal.escape = 0;
      }
    }
  }

  XLockDisplay(display);
  XClearArea(display, window, 0, 0, window_width, window_height, True);
  XFlush(display);
  XUnlockDisplay(display);
}

static int doc_data_callback(void** buffer, size_t* size, void* ctx)
{
  struct terminal* term = ctx;

  *buffer = term->doc_buf;
  *size = term->doc_buf_payload;

  return 1;
}

static void append_doc_data(void* data, size_t size)
{
  unsigned char* end;

  if(terminal.doc_buf_size + size > terminal.doc_buf_alloc)
  {
    terminal.doc_buf_alloc = terminal.doc_buf_alloc * 3 / 2;
    terminal.doc_buf_alloc += (size + 0xffff) & ~0xffff;
    terminal.doc_buf = realloc(terminal.doc_buf, terminal.doc_buf_alloc);
  }

  memcpy(terminal.doc_buf + terminal.doc_buf_size, data, size);
  terminal.doc_buf_size += size;

  if(0 != (end = memmem(terminal.doc_buf, terminal.doc_buf_size, "\033[?3340l", 8)))
  {
    struct cnt_image* image;
    Picture pic;
    size_t width, height;

    unsigned char* old_buf;
    size_t old_buf_size;
    size_t old_buf_payload;

    terminal.doc_buf_payload = end - terminal.doc_buf;

    image = cnt_image_alloc();
    cnt_image_set_data_callback(image, doc_data_callback, &terminal);

    pic = cnt_image_load(&width, &height, image);

    old_buf = terminal.doc_buf;
    old_buf_size = terminal.doc_buf_size;
    old_buf_payload = terminal.doc_buf_payload;

    terminal.doc_buf = 0;
    terminal.doc_buf_size = 0;
    terminal.doc_buf_payload = 0;
    terminal.doc_buf_alloc = 0;
    terminal.document = 0;

    if(pic && terminal.image_count < sizeof(terminal.images) / sizeof(terminal.images[0]))
    {
      size_t i, lines;

      i = terminal.image_count++;

      terminal.images[i].image = image;
      terminal.images[i].pic = pic;
      terminal.images[i].width = width;
      terminal.images[i].height = height;
      terminal.images[i].char_x = terminal.cursorx;
      terminal.images[i].char_y = terminal.cursory;
      terminal.images[i].screen = terminal.curscreen;
      terminal.images[i].rows = (height + yskips[SMALL] - 1) / yskips[SMALL];
      terminal.images[i].cols = (width + xskips[SMALL] - 1) / xskips[SMALL];
      lines = terminal.images[i].rows;

      while(lines--)
      {
        terminal.cursorx = 0;
        ++terminal.cursory;

        while(terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }
      }
    }
    else
    {
      static const char* message = "[Unrecognized document]\r\n";
      process_data((unsigned char*) message, strlen(message));

      cnt_image_free(&image);
    }

    process_data(end, old_buf_size - old_buf_payload);

    free(old_buf);
  }
}

void read_data()
{
  unsigned char buf[4096];
  int result, more, fill = 0;

  /* This loop is designed for single-core computers, in case the data source is
   * doing a series of small writes */
  do
  {
    result = read(terminal.fd, buf + fill, sizeof(buf) - fill);

    if(result == -1)
      {
        save_session();

        exit(EXIT_SUCCESS);
      }

    fill += result;

    if(fill < sizeof(buf))
    {
      sched_yield();

      ioctl(terminal.fd, FIONREAD, &more);
    }
    else
      more = 0;
  }
  while(more);

  if(terminal.document)
    append_doc_data(buf, fill);
  else
    process_data(buf, fill);
}

void term_writen(const char* data, size_t len)
{
  size_t off = 0;
  ssize_t result;

  while(off < len)
  {
    result = write(terminal.fd, data + off, len - off);

    if(result < 0)
      exit(EXIT_FAILURE);

    off += result;
  }
}

void term_write(const char* data)
{
  term_writen(data, strlen(data));
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

int main(int argc, char** argv)
{
  char* args[2];
  int i;
  int xfd;
  int result;
  int session_fd;
  char* palette_str;
  char* token;

  setlocale(LC_ALL, "en_US.UTF-8");

  if(!getenv("DISPLAY"))
  {
    fprintf(stderr, "DISPLAY variable not set.\n");

    return EXIT_FAILURE;
  }

  session_path = getenv("SESSION_PATH");

  if(session_path)
    unsetenv("SESSION_PATH");

  chdir(getenv("HOME"));

  mkdir(".cantera", 0777);
  mkdir(".cantera/commands", 0777);
  mkdir(".cantera/file-commands", 0777);
  mkdir(".cantera/filemanager", 0777);

  config = tree_load_cfg(".cantera/config");

  palette_str = strdup(tree_get_string_default(config, "terminal.palette", "000000 1818c2 18c218 18c2c2 c21818 c218c2 c2c218 c2c2c2 686868 7474ff 54ff54 54ffff ff5454 ff54ff ffff54 ffffff"));

  for(i = 0, token = strtok(palette_str, " "); token;
      ++i, token = strtok(0, " "))
    {
      if(palette[i] < 16)
        palette[i] = 0xff000000 | strtol(token, 0, 16);
    }
  scroll_extra = tree_get_integer_default(config, "terminal.history-size", 1000);
  font_name = tree_get_string_default(config, "terminal.font", "/usr/share/fonts/truetype/msttcorefonts/Andale_Mono.ttf");
  font_sizes[0] = tree_get_integer_default(config, "terminal.font-size", 12);

  signal(SIGTERM, sighandler);
  signal(SIGIO, sighandler);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

  setenv("TERM", "xterm", 1);

  window_width = 800;
  window_height = 600;

  if(session_path)
    {
      session_fd = open(session_path, O_RDONLY);

      if(session_fd != -1)
        {
          struct winsize ws;

          if(sizeof(ws) == read(session_fd, &ws, sizeof(ws)))
            {
              window_width = ws.ws_xpixel;
              window_height = ws.ws_ypixel;
            }
        }
    }
  else
    session_fd = -1;

  x11_connect(getenv("DISPLAY"));

  args[0] = "/bin/bash";
  args[1] = 0;

  init_session(args);

  if(session_fd != -1)
    {
      size_t size;

      size = terminal.size.ws_col * terminal.history_size;

      read(session_fd, &terminal.cursorx, sizeof(terminal.cursorx));
      read(session_fd, &terminal.cursory, sizeof(terminal.cursory));

      if(terminal.cursorx >= terminal.size.ws_col
         || terminal.cursory >= terminal.size.ws_row
         || terminal.cursorx < 0
         || terminal.cursory < 0)
        {
          terminal.cursorx = 0;
          terminal.cursory = 0;
        }
      else
        {
          read(session_fd, terminal.chars[0], size * sizeof(*terminal.chars[0]));
          read(session_fd, terminal.attr[0], size * sizeof(*terminal.attr[0]));

          terminal.cursorx = 0;
          ++terminal.cursory;

          while(terminal.cursory >= terminal.size.ws_row)
            {
              scroll(0);
              --terminal.cursory;
            }
        }

      close(session_fd);
      unlink(session_path);
    }

  xfd = ConnectionNumber(display);

  while(!done)
  {
    XEvent event;
    pid_t pid;
    int status;
    fd_set readset;
    int maxfd;

    FD_ZERO(&readset);
    FD_SET(xfd, &readset);
    maxfd = xfd;
    FD_SET(terminal.fd, &readset);
    if(terminal.fd > maxfd)
      maxfd = terminal.fd;

    if(-1 == select(maxfd + 1, &readset, 0, 0, 0))
    {
      if(errno == EINTR)
        continue;

      fprintf(stderr, "select failed: %s\n", strerror(errno));

      return EXIT_FAILURE;
    }

    if(FD_ISSET(terminal.fd, &readset))
      read_data();

    while(0 < (pid = waitpid(-1, &status, WNOHANG)))
    {
      if(pid == terminal.pid)
        {
          save_session();

          return EXIT_SUCCESS;
        }
    }

    while(XPending(display))
    {
      XNextEvent(display, &event);

      switch(event.type)
      {
      case KeyPress:

        /* if(!XFilterEvent(&event, window)) */
        {
          char text[32];
          Status status;
          KeySym key_sym;
          int len;
          int history_scroll_reset = 1;

          ctrl_pressed = (event.xkey.state & ControlMask);
          mod1_pressed = (event.xkey.state & Mod1Mask);
          super_pressed = (event.xkey.state & Mod4Mask);
          shift_pressed = (event.xkey.state & ShiftMask);

          len = Xutf8LookupString(xic, &event.xkey, text, sizeof(text) - 1, &key_sym, &status);

          if(!text[0])
            len = 0;

          if(key_sym == XK_Control_L || key_sym == XK_Control_R)
            ctrl_pressed = 1, history_scroll_reset = 0;

          if(key_sym == XK_Super_L || key_sym == XK_Super_R)
            super_pressed = 1, history_scroll_reset = 0;

          if(key_sym == XK_Alt_L || key_sym == XK_Alt_R
             || key_sym == XK_Shift_L || key_sym == XK_Shift_R)
            history_scroll_reset = 0;

          if(event.xkey.keycode == 161 || key_sym == XK_Menu)
          {
            normalize_offset();

            if(shift_pressed)
            {
              select_end = terminal.cursory * terminal.size.ws_col + terminal.cursorx;

              if(select_end == 0)
              {
                select_begin = 0;
                select_end = 1;
              }
              else
                select_begin = select_end - 1;

              find_range(range_parenthesis, &select_begin, &select_end);

              update_selection(CurrentTime);

              XClearArea(display, window, 0, 0, window_width, window_height, True);
            }
            else
            {
              if(select_text)
                run_command(terminal.fd, "calculate", (const char*) select_text);
            }
          }

          {
            if (key_sym == XK_Insert && shift_pressed)
            {
              paste(event.xkey.time);
            }
            else if(key_sym >= XK_F1 && key_sym <= XK_F4)
            {
              char buf[4];
              buf[0] = '\033';
              buf[1] = 'O';
              buf[2] = 'P' + key_sym - XK_F1;
              buf[3] = 0;

              term_write(buf);
            }
            else if(key_sym >= XK_F5 && key_sym <= XK_F12)
            {
              static int off[] = { 15, 17, 18, 19, 20, 21, 23, 24 };

              char buf[6];
              buf[0] = '\033';
              buf[1] = '[';
              buf[2] = '0' + off[key_sym - XK_F5] / 10;
              buf[3] = '0' + off[key_sym - XK_F5] % 10;
              buf[4] = '~';
              buf[5] = 0;

              term_write(buf);
            }
            else if(key_sym == XK_Up)
            {
              if(shift_pressed)
                {
                  history_scroll_reset = 0;

                  if(terminal.history_scroll < scroll_extra)
                    {
                      ++terminal.history_scroll;
                      XClearArea(display, window, 0, 0, window_width, window_height, True);
                    }
                }
              else if(terminal.appcursor)
                term_write("\033OA");
              else
                term_write("\033[A");
            }
            else if(key_sym == XK_Down)
            {
              if(shift_pressed)
                {
                  history_scroll_reset = 0;

                  if(terminal.history_scroll)
                    {
                      --terminal.history_scroll;
                      XClearArea(display, window, 0, 0, window_width, window_height, True);
                    }
                }
              else if(terminal.appcursor)
                term_write("\033OB");
              else
                term_write("\033[B");
            }
            else if(key_sym == XK_Right)
            {
              if(terminal.appcursor)
                term_write("\033OC");
              else
                term_write("\033[C");
            }
            else if(key_sym == XK_Left)
            {
              if(terminal.appcursor)
                term_write("\033OD");
              else
                term_write("\033[D");
            }
            else if(key_sym == XK_Insert)
            {
              term_write("\033[2~");
            }
            else if(key_sym == XK_Delete)
            {
              term_write("\033[3~");
            }
            else if(key_sym == XK_Page_Up)
            {
              if(shift_pressed)
                {
                  history_scroll_reset = 0;

                  terminal.history_scroll += terminal.size.ws_row;

                  if(terminal.history_scroll > scroll_extra)
                    terminal.history_scroll = scroll_extra;

                  XClearArea(display, window, 0, 0, window_width, window_height, True);
                }
              else
                term_write("\033[5~");
            }
            else if(key_sym == XK_Page_Down)
            {
              if(shift_pressed)
                {
                  history_scroll_reset = 0;

                  if(terminal.history_scroll > terminal.size.ws_row)
                    terminal.history_scroll -= terminal.size.ws_row;
                  else
                    terminal.history_scroll = 0;

                  XClearArea(display, window, 0, 0, window_width, window_height, True);
                }
              else
                term_write("\033[6~");
            }
            else if(key_sym == XK_Home)
            {
              if(shift_pressed)
                {
                  history_scroll_reset = 0;

                  if(terminal.history_scroll != scroll_extra)
                    {
                      terminal.history_scroll = scroll_extra;

                      XClearArea(display, window, 0, 0, window_width, window_height, True);
                    }
                }
              else if(terminal.appcursor)
                term_write("\033OH");
              else
                term_write("\033[H");
            }
            else if(key_sym == XK_End)
            {
              if(terminal.appcursor)
                term_write("\033OF");
              else
                term_write("\033[F");
            }
            else if(key_sym == XK_space)
            {
              if(mod1_pressed)
                term_write("\033");

              term_write(" ");
            }
            else if(key_sym == XK_Shift_L || key_sym == XK_Shift_R
                 || key_sym == XK_ISO_Prev_Group || key_sym == XK_ISO_Next_Group)
            {
              /* Do not generate characters on shift key, or gus'
               * special shift keys */
            }
            else if(len)
            {
              if(mod1_pressed)
                term_write("\033");

              term_writen((const char*) text, len);
            }
          }

          if(history_scroll_reset && terminal.history_scroll)
            {
              terminal.history_scroll = 0;
              XClearArea(display, window, 0, 0, window_width, window_height, True);
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
        }

        break;

      case MotionNotify:

        ctrl_pressed = (event.xkey.state & ControlMask);

        {
          if(event.xbutton.state & Button1Mask)
          {
            int x, y, new_select_end;
            unsigned int size;

            size = terminal.history_size * terminal.size.ws_col;

            x = event.xbutton.x / terminal.xskip;
            y = event.xbutton.y / terminal.yskip;

            new_select_end = y * terminal.size.ws_col + x;

            if(terminal.history_scroll)
              new_select_end += size - (terminal.history_scroll * terminal.size.ws_col);

            if(ctrl_pressed)
            {
              find_range(range_word_or_url, &select_begin, &new_select_end);
            }

            if(new_select_end != select_end)
            {
              select_end = new_select_end;

              XClearArea(display, window, 0, 0, window_width, window_height, True);
            }
          }
        }

        break;

      case ButtonPress:

        ctrl_pressed = (event.xkey.state & ControlMask);
        mod1_pressed = (event.xkey.state & Mod1Mask);
        shift_pressed = (event.xkey.state & ShiftMask);

        switch(event.xbutton.button)
        {
        case 1: /* Left button */

          {
            int x, y;
            unsigned int size;

            size = terminal.history_size * terminal.size.ws_col;

            button1_pressed = 1;

            x = event.xbutton.x / terminal.xskip;
            y = event.xbutton.y / terminal.yskip;

            select_begin = y * terminal.size.ws_col + x;

            if(terminal.history_scroll)
              select_begin += size - (terminal.history_scroll * terminal.size.ws_col);

            select_end = select_begin;

            if(ctrl_pressed)
            {
              find_range(range_word_or_url, &select_begin, &select_end);
            }

            XClearArea(display, window, 0, 0, window_width, window_height, True);
          }

          break;

        case 2: /* Middle button */

          paste(event.xbutton.time);

          break;

        case 4: /* Up */

          if(terminal.history_scroll < scroll_extra)
            {
              ++terminal.history_scroll;
              XClearArea(display, window, 0, 0, window_width, window_height, True);
            }

          break;

        case 5: /* Down */

          if(terminal.history_scroll)
            {
              --terminal.history_scroll;
              XClearArea(display, window, 0, 0, window_width, window_height, True);
            }

          break;
        }

        break;

      case ButtonRelease:

        switch(event.xbutton.button)
        {
        case 1: /* Left button */

          {
            button1_pressed = 0;

            update_selection(event.xbutton.time);

            if(select_text && (event.xkey.state & Mod1Mask))
              run_command(terminal.fd, "open-url", (const char*) select_text);

            break;
          }
        }

        break;

      case SelectionRequest:

        {
          XSelectionRequestEvent* request = &event.xselectionrequest;
          XSelectionEvent response;

          /* XXX: Check time */

          if(request->property == None)
            request->property = request->target;

          response.type = SelectionNotify;
          response.send_event = True;
          response.display = display;
          response.requestor = request->requestor;
          response.selection = request->selection;
          response.target = request->target;
          response.property = None;
          response.time = request->time;

          /* fprintf(stderr, "Wanting select_text %s\n", XGetAtomName(display, response.target)); */

          if(select_text)
          {
            if(request->target == XA_STRING
            || request->target == xa_utf8_string)
            {
              result = XChangeProperty(display, request->requestor, request->property,
                                       request->target, 8, PropModeReplace, select_text, select_length);

              if(result != BadAlloc && result != BadAtom && result != BadValue && result != BadWindow)
                response.property = request->property;
            }
          }

          XSendEvent(request->display, request->requestor, False, NoEventMask,
                     (XEvent*) &response);
        }

        break;

      case SelectionNotify:

        {
          Atom type;
          int format;
          unsigned long nitems;
          unsigned long bytes_after;
          unsigned char* prop;

          result = XGetWindowProperty(display, window, prop_paste, 0, 0, False, AnyPropertyType,
                                      &type, &format, &nitems, &bytes_after, &prop);

          if(result != Success)
            break;

          XFree(prop);

          result = XGetWindowProperty(display, window, prop_paste, 0, bytes_after, False, AnyPropertyType,
                                      &type, &format, &nitems, &bytes_after, &prop);

          if(result != Success)
            break;

          if(type != xa_utf8_string || format != 8)
            break;

          term_writen((char*) prop, nitems);

          XFree(prop);
        }

        break;

      case ConfigureNotify:

        {
          /* Skip to last ConfigureNotify event */
          while(XCheckTypedWindowEvent(display, window, ConfigureNotify, &event))
          {
            /* Do nothing */
          }

          if(window_width == event.xconfigure.width && window_height == event.xconfigure.height)
            break;

          normalize_offset();

          window_width = event.xconfigure.width;
          window_height = event.xconfigure.height;

          cols = window_width / terminal.xskip;
          rows = window_height / terminal.yskip;

          int oldcols = terminal.size.ws_col;
          int oldrows = terminal.size.ws_row;

          terminal.size.ws_xpixel = window_width;
          terminal.size.ws_ypixel = window_height;
          terminal.size.ws_col = cols;
          terminal.size.ws_row = rows;
          terminal.history_size = rows + scroll_extra;

          if (cols != oldcols || rows != oldrows)
          {
            wchar_t* oldchars[2] = {terminal.chars[0], terminal.chars[1]};
            uint16_t* oldattr[2] = {terminal.attr[0], terminal.attr[1]};

            char *oldbuffer = terminal.buffer;
            terminal.buffer = calloc(2 * cols * terminal.history_size, (sizeof(wchar_t) + sizeof(uint16_t)));

            char* c;
            c = terminal.buffer;
            terminal.chars[0] = (wchar_t*) c; c += cols * terminal.history_size * sizeof(wchar_t);
            terminal.attr[0] = (uint16_t*) c; c += cols * terminal.history_size * sizeof(uint16_t);
            terminal.chars[1] = (wchar_t*) c; c += cols * terminal.history_size * sizeof(wchar_t);
            terminal.attr[1] = (uint16_t*) c;
            terminal.scrollbottom = rows;

            int srcoff = 0;
            int minrows;

            if(rows > oldrows)
              minrows = oldrows;
            else
            {
              if(terminal.cursory >= rows)
                srcoff = terminal.cursory - rows + 1;
              minrows = rows;
            }

            int mincols = (cols < oldcols) ? cols : oldcols;

            for(i = 0; i < minrows; ++i)
            {
              memcpy(&terminal.chars[0][i * cols], &oldchars[0][(i + srcoff) * oldcols], mincols * sizeof(wchar_t));
              memcpy(&terminal.attr[0][i * cols], &oldattr[0][(i + srcoff) * oldcols], mincols * sizeof(terminal.attr[0][0]));
            }

            for(i = 0; i < minrows; ++i)
            {
              memcpy(&terminal.chars[1][i * cols], &oldchars[1][(i + srcoff) * oldcols], mincols * sizeof(wchar_t));
              memcpy(&terminal.attr[1][i * cols], &oldattr[1][(i + srcoff) * oldcols], mincols * sizeof(terminal.attr[1][0]));
            }

            free(oldbuffer);

            terminal.curchars = terminal.chars[terminal.curscreen];
            terminal.curattrs = terminal.attr[terminal.curscreen];

            free(screenchars);
            free(screenattrs);
            screenchars = calloc(sizeof(*screenchars), cols * rows);
            screenattrs = calloc(sizeof(*screenattrs), cols * rows);

            terminal.cursory = terminal.cursory - srcoff;
            terminal.storedcursory[1 - terminal.curscreen] += rows - oldrows;
#define CLIP_CURSOR(val, max) { if (val < 0) val = 0; else if (val >= max) val = max - 1; }
            CLIP_CURSOR(terminal.cursorx, cols);
            CLIP_CURSOR(terminal.cursory, rows);
            CLIP_CURSOR(terminal.storedcursorx[1 - terminal.curscreen], cols);
            CLIP_CURSOR(terminal.storedcursory[1 - terminal.curscreen], rows);
#undef CLIP_CURSOR

            ioctl(terminal.fd, TIOCSWINSZ, &terminal.size);
          }

          Pixmap pmap;

          XRenderFreePicture(display, root_buffer);

          pmap = XCreatePixmap(display, window, window_width, window_height, visual_info->depth);
          root_buffer = XRenderCreatePicture(display, pmap, xrenderpictformat, 0, 0);

          if(root_buffer == None)
          {
            fprintf(stderr, "Failed to create root buffer\n");

            return EXIT_FAILURE;
          }

          XFreePixmap(display, pmap);

          XRenderFillRectangle(display, PictOpSrc, root_buffer, &xrpalette[0], 0, 0, window_width, window_height);
          memset(screenchars, 0xff, cols * rows * sizeof(*screenchars));
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
