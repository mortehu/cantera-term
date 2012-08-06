#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
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
#include <sysexits.h>
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

#include "tree.h"
#include "x11.h"

#define PARTIAL_REPAINT 1

static int print_version;
static int print_help;
static unsigned int parent_window;
static int pty_fd = -1;
static const char *title = "cantera-term";

static struct option long_options[] =
{
  { "width",    required_argument, 0,              'w' },
  { "height",   required_argument, 0,              'h' },
  { "title",    required_argument, 0,              'T' },
  { "command",  required_argument, 0,              'e' },
  { "into",     required_argument, 0,              'i' },
  { "pty-fd",   required_argument, 0,              'p' },
  { "version",        no_argument, &print_version, 1 },
  { "help",           no_argument, &print_help,    1 },
  { 0, 0, 0, 0 }
};

struct tree* config = 0;

unsigned int scroll_extra;
const char* font_name;

extern char** environ;

static unsigned int palette[16];

struct terminal
{
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
  int alt_charset[2];
  unsigned int ch, nch;

  unsigned int history_scroll;
};

/* Alternate characters, from 0x41 to 0x7E, inclusive */
static unsigned short alt_charset[62] =
{
    0x2191, 0x2193, 0x2192, 0x2190, 0x2588, 0x259a, 0x2603, // 41-47 hi mr. snowman!
    0,      0,      0,      0,      0,      0,      0,      0, // 48-4f
    0,      0,      0,      0,      0,      0,      0,      0, // 50-57
    0,      0,      0,      0,      0,      0,      0, 0x0020, // 58-5f
    0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1, // 60-67
    0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba, // 68-6f
    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c, // 70-77
    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,         // 78-7e
};

static int done;
static const char* session_path;
static int focused;

unsigned int window_width = 800, window_height = 600;

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
int damage_eventbase;
int damage_errorbase;
int screenidx;
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
int cols;
int rows;
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

int temp_switch_screen = 0;

#define my_isprint(c) (isprint((c)) || ((c) >= 0x80))

void *memset16(void *s, int w, size_t n)
{
  uint16_t* o = s;

  assert(!(n & 1));

  n >>= 1;

  while (n--)
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

  for (k = terminal.size.ws_col; k-- > terminal.cursorx + count; )
  {
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curchars[(terminal.cursory * terminal.size.ws_col + k - count + *terminal.curoffset) % size];
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k - count + *terminal.curoffset) % size];
  }

  while (k >= terminal.cursorx)
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

  if (terminal.alt_charset[terminal.curscreen])
    {
      if (ch >= 0x41 && ch <= 0x7e)
        ch = alt_charset[ch - 0x41];
    }

  if (ch < 32)
    return;

  if (ch == 0x7f || ch >= 65536)
  {
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = 0;
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = terminal.curattr;

    return;
  }

  int width = wcwidth(ch);

  if (!width)
    return;

  if (width > 1)
  {
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = ch;
    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + 1 + *terminal.curoffset) % size] = 0xffff;
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] =
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + 1 + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
    terminal.cursorx += width;

    if (terminal.cursorx > terminal.size.ws_col)
      terminal.cursorx = terminal.size.ws_col;
  }
  else
  {
    if (terminal.insertmode)
      insert_chars(1);

    terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = ch;
    terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
    ++terminal.cursorx;
  }
}

static void
paint(int x, int y, int width, int height)
{
  glClear (GL_COLOR_BUFFER_BIT);

  int row, i, selbegin, selend;
  int minx = x;
  int miny = y;
  int maxx = x + width;
  int maxy = y + height;
  unsigned int size;
  int in_selection = 0;

  size = terminal.history_size * terminal.size.ws_col;

  const wchar_t* curchars;
  const uint16_t* curattrs;
  int cursorx, cursory;
  int curoffset;

  if (temp_switch_screen)
    {
      curchars = terminal.chars[1 - terminal.curscreen];
      curattrs = terminal.attr[1 - terminal.curscreen];
      cursorx = terminal.storedcursorx[1 - terminal.curscreen];
      cursory = terminal.storedcursory[1 - terminal.curscreen];
      curoffset = terminal.offset[1 - terminal.curscreen];
    }
  else
    {
      curchars = terminal.curchars;
      curattrs = terminal.curattrs;
      cursorx = terminal.cursorx;
      cursory = terminal.cursory;
      curoffset = *terminal.curoffset;
    }

  if (select_begin < select_end)
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

  glBegin (GL_QUADS);

  for (row = 0; row < terminal.size.ws_row; ++row)
    {
      size_t pos = ((row + terminal.history_size - terminal.history_scroll) * terminal.size.ws_col + curoffset) % size;
      wchar_t* screenline = &screenchars[row * terminal.size.ws_col];
      uint16_t* screenattrline = &screenattrs[row * terminal.size.ws_col];
      const wchar_t* line = &curchars[pos];
      const uint16_t* attrline = &curattrs[pos];
      int start = 0, end, x = 0;

      while (start < terminal.size.ws_col)
        {
          int width, height;
          int printable;
          int attr = attrline[start];
          int localattr = -1;

          if (focused
              && row == cursory + terminal.history_scroll
              && start == cursorx)
            {
              attr = REVERSE(attr);

              if (!attr)
                attr = BG(ATTR_WHITE);
            }

          printable = (line[start] != 0);

          if (row * terminal.size.ws_col + start == selbegin)
            in_selection = 1;

          if (row * terminal.size.ws_col + start == selend)
            in_selection = 0;

          if (in_selection)
            {
              if (line[start] != screenline[start] && !button1_pressed)
                {
                  in_selection = 0;
                  select_begin = -1;
                  select_end = -1;
                }
              else
                attr = REVERSE(attr);
            }

          end = start + 1;

          while (end < terminal.size.ws_col)
            {
              localattr = attrline[end];

              if (row * terminal.size.ws_col + end >= selbegin
                  && row * terminal.size.ws_col + end < selend)
                {
                  if (line[end] != screenline[end] && !button1_pressed)
                    {
                      selbegin = select_begin = -1;
                      selend = select_end = -1;
                    }
                  else
                    localattr = REVERSE(localattr);
                }

              if (localattr != attr)
                break;

              if (row == cursory && end == cursorx)
                break;

              if ((line[end] != 0) != printable)
                break;

              ++end;
            }

          width = (end - start) * terminal.xskip;
          height = terminal.yskip;

          for (i = start; i < end; ++i)
            {
              screenline[i] = line[i];
              screenattrline[i] = attr;
            }

          if (x < minx) minx =x;
          if (row * terminal.yskip < miny) miny = row * terminal.yskip;
          if (x + width > maxx) maxx = x + width;
          if (row * terminal.yskip + height > maxy) maxy = row * terminal.yskip + height;

          /* color: (attr >> 4) & 7 */

          unsigned int color;
          color = palette[(attr >> 4) & 7];
          glColor3ub (color >> 16, color >> 8, color);

          glVertex2f (x,         row * terminal.yskip);
          glVertex2f (x,         row * terminal.yskip + height);
          glVertex2f (x + width, row * terminal.yskip + height);
          glVertex2f (x + width, row * terminal.yskip);

          color = palette[attr & 0x0f];
          glColor3ub (color >> 16, color >> 8, color);

          if (printable)
            {
              glVertex2f (x,         row * terminal.yskip);
              glVertex2f (x,         row * terminal.yskip + height);
              glVertex2f (x + width, row * terminal.yskip + height);
              glVertex2f (x + width, row * terminal.yskip);
            }
#if 0
          drawtext(root_buffer, &line[start], end - start, x, row * terminal.yskip, attr & 0x0F, SMALL);
#endif

          if (attr & ATTR_UNDERLINE)
            {
              glVertex2f (x,         (row + 1) * terminal.yskip - 1);
              glVertex2f (x,         (row + 1) * terminal.yskip);
              glVertex2f (x + width, (row + 1) * terminal.yskip);
              glVertex2f (x + width, (row + 1) * terminal.yskip - 1);
            }

          x += width;

          start = end;
        }
    }

  glEnd ();

  glXSwapBuffers (X11_display, X11_window);
}

static void normalize_offset()
{
  int size = terminal.size.ws_col * terminal.history_size;
  int i;

  if (!*terminal.curoffset)
    return;

  for (i = 0; i < 2; ++i)
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
  int first, length;

  if (!fromcursor && terminal.scrolltop == 0 && terminal.scrollbottom == terminal.size.ws_row)
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

  if (fromcursor)
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

  if (fromcursor)
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

  if (range == range_word_or_url)
  {
    i = *begin;

    while (i)
    {
      if (!(i % terminal.size.ws_col))
        break;

      ch = terminal.curchars[(offset + i - 1) % size];

      if (ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch))
        break;

      --i;
    }

    *begin = i;

    i = *end;

    while ((i % terminal.size.ws_col) < terminal.size.ws_col)
    {
      ch = terminal.curchars[(offset + i) % size];

      if (ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch))
        break;

      ++i;
    }

    *end = i;

    if (*begin == *end)
      return 0;

    return 1;
  }
  else if (range == range_parenthesis)
  {
    int paren_level = 0;
    i = *begin;

    while (i > 0)
    {
      ch = terminal.curchars[(offset + i) % size];

      if ((!ch || ((i + 1) % terminal.size.ws_col == 0) || isspace(ch)) && !paren_level)
      {
        ++i;

        break;
      }

      if (ch == ')')
        ++paren_level;

      if (ch == '(')
      {
        if (!paren_level)
          break;

        --paren_level;
      }

      --i;
    }

    *begin = i;

    if (*end > i + 1)
    {
      if (terminal.curchars[(offset + *end - 1) % size] == '=')
        --*end;
    }

    return 1;
  }
  else
    return 0;
}

void init_session(char* const* args)
{
  char* c;
  int stderr_backup;

  memset(&terminal, 0, sizeof(terminal));

  terminal.xskip = 8;
  terminal.yskip = 8;
  terminal.size.ws_xpixel = window_width;
  terminal.size.ws_ypixel = window_height;
  terminal.size.ws_col = window_width / terminal.xskip;
  terminal.size.ws_row = window_height / terminal.yskip;
  terminal.history_size = terminal.size.ws_row + scroll_extra;

  if (pty_fd != -1)
    {
      terminal.fd = pty_fd;
      terminal.pid = getppid ();
    }
  else
    {
      stderr_backup = dup (2);
      terminal.pid = forkpty(&terminal.fd, 0, 0, &terminal.size);

      if (terminal.pid == -1)
	err (EX_OSERR, "forkpty() failed");

      if (!terminal.pid)
        {
          terminal.xskip = 8;
          terminal.yskip = 8;

          if (-1 == execve(args[0], args, environ))
	  {
	    dup2 (stderr_backup, 2);
	    err (EXIT_FAILURE, "Failed to execute '%s'", args[0]);
	  }
        }
    }

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

static void save_session()
{
  int fd;
  size_t size;

  if (!session_path)
    return;

  fd = open(session_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  if (fd == -1)
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

  if (first)
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

  if (select_begin == select_end)
    return;

  size = terminal.size.ws_col * terminal.history_size;
  offset = *terminal.curoffset;

  if (select_text)
  {
    free(select_text);
    select_text = 0;
  }

  if (select_begin > select_end)
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

  while (i != select_end)
  {
    int ch = terminal.curchars[(i + offset) % size];
    int width = terminal.size.ws_col;

    if (ch == 0 || ch == 0xffff)
      ch = ' ';

    if (select_length + 4 > select_alloc)
    {
      select_alloc *= 2;
      select_text = realloc(select_text, select_alloc);
    }

    if (i > select_begin && (i % width) == 0)
    {
      select_length = last_graph;
      if (last_graph_col != (width - 1))
        select_text[select_length++] = '\n';
      last_graph = select_length;
    }

    if (ch < 0x80)
    {
      select_text[select_length++] = ch;
    }
    else if (ch < 0x800)
    {
      select_text[select_length++] = 0xC0 | (ch >> 6);
      select_text[select_length++] = 0x80 | (ch & 0x3F);
    }
    else if (ch < 0x10000)
    {
      select_text[select_length++] = 0xE0 | (ch >> 12);
      select_text[select_length++] = 0x80 | ((ch >> 6) & 0x3F);
      select_text[select_length++] = 0x80 | (ch & 0x3f);
    }

    if (ch != ' ')
    {
      last_graph = select_length;
      last_graph_col = i % width;
    }

    ++i;
  }

  select_length = last_graph;
  select_text[select_length] = 0;

  XSetSelectionOwner (X11_display, XA_PRIMARY, X11_window, time);

  if (X11_window != XGetSelectionOwner (X11_display, XA_PRIMARY))
  {
    select_begin = select_end;
    free(select_text);
    select_text = 0;
  }
}

static void paste(Time time)
{
  Window selowner;

  selowner = XGetSelectionOwner(X11_display, XA_PRIMARY);

  if (selowner == None)
    return;

  XDeleteProperty(X11_display, X11_window, prop_paste);

  XConvertSelection(X11_display, XA_PRIMARY, xa_utf8_string, prop_paste, X11_window, time);
}

static void term_process_data(unsigned char* buf, int count)
{
  int j, k, l;

  int size = terminal.size.ws_col * terminal.history_size;

  /* XXX: Make sure cursor does not leave screen */

  j = 0;

  /* Redundant character processing code for the typical case */
  if (!terminal.escape && !terminal.insertmode && !terminal.nch)
  {
    for (; j < count; ++j)
    {
      if (buf[j] >= ' ' && buf[j] <= '~')
      {
        if (terminal.cursorx == terminal.size.ws_col)
        {
          ++terminal.cursory;
          terminal.cursorx = 0;
        }

        while (terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }

        terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = buf[j];
        terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
        ++terminal.cursorx;
      }
      else if (buf[j] == '\r')
        terminal.cursorx = 0;
      else if (buf[j] == '\n')
      {
        ++terminal.cursory;

        while (terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }
      }
      else
        break;
    }
  }

  for (; j < count; ++j)
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

        if (terminal.cursorx > 0)
          --terminal.cursorx;

        break;

      case '\t':

        if (terminal.cursorx < terminal.size.ws_col - 8)
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

        while (terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }

        break;

      case '\r':

        terminal.cursorx = 0;

        break;

      case '\177':

        if (terminal.cursory < terminal.size.ws_row)
          terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = 0;

        break;

      case ('O' & 0x3F): /* ^O = default character set */

        break;

      case ('N' & 0x3F): /* ^N = alternate character set */

        break;

      default:

        assert(terminal.cursorx >= 0 && terminal.cursorx <= terminal.size.ws_col);
        assert(terminal.cursory >= 0 && terminal.cursory < terminal.size.ws_row);

        if (terminal.cursorx == terminal.size.ws_col)
        {
          ++terminal.cursory;
          terminal.cursorx = 0;
        }

        while (terminal.cursory >= terminal.size.ws_row)
        {
          scroll(0);
          --terminal.cursory;
        }

        if (terminal.nch)
        {
          if ((buf[j] & 0xC0) != 0x80)
          {
            terminal.nch = 0;
            addchar(buf[j]);
          }
          else
          {
            terminal.ch <<= 6;
            terminal.ch |= buf[j] & 0x3F;

            if (0 == --terminal.nch)
            {
              addchar(terminal.ch);
            }
          }
        }
        else
        {
          if ((buf[j] & 0x80) == 0)
          {
            addchar(buf[j]);
          }
          else if ((buf[j] & 0xE0) == 0xC0)
          {
            terminal.ch = buf[j] & 0x1F;
            terminal.nch = 1;
          }
          else if ((buf[j] & 0xF0) == 0xE0)
          {
            terminal.ch = buf[j] & 0x0F;
            terminal.nch = 2;
          }
          else if ((buf[j] & 0xF8) == 0xF0)
          {
            terminal.ch = buf[j] & 0x03;
            terminal.nch = 3;
          }
          else if ((buf[j] & 0xFC) == 0xF8)
          {
            terminal.ch = buf[j] & 0x01;
            terminal.nch = 4;
          }
        }
      }

      break;

    case 1:

      switch(buf[j])
        {
        case 'D':

          ++terminal.cursory;

          while (terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
            {
              scroll(0);
              --terminal.cursory;
            }

          break;

        case 'E':

          terminal.escape = 0;
          terminal.cursorx = 0;
          ++terminal.cursory;

          while (terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
            {
              scroll(0);
              --terminal.cursory;
            }

          break;

        case '[':

          terminal.escape = 2;
          memset(terminal.param, 0, sizeof(terminal.param));

          break;

        case '%':

          terminal.escape = 2;
          terminal.param[0] = -1;

          break;

        case ']':

          terminal.escape = 2;
          terminal.param[0] = -2;

          break;

        case '(':

          terminal.escape = 2;
          terminal.param[0] = -4;

          break;

        case '#':

          terminal.escape = 2;
          terminal.param[0] = -5;

          break;

        case 'M':

          if (terminal.cursorx == 0 && terminal.cursory == terminal.scrolltop)
            rscroll(0);
          else if (terminal.cursory)
            --terminal.cursory;

          terminal.escape = 0;

          break;

        default:

          terminal.escape = 0;
        }

      break;

    default:

      if (terminal.param[0] == -1)
        {
          terminal.escape = 0;
        }
      else if (terminal.param[0] == -2)
        {
          /* Handle ESC ] Ps ; Pt BEL */
          if (terminal.escape == 2)
            {
              if (buf[j] >= '0' && buf[j] <= '9')
                {
                  terminal.param[1] *= 10;
                  terminal.param[1] += buf[j] - '0';
                }
              else
                ++terminal.escape;
            }
          else
            {
              if (buf[j] != '\007')
                {
                  /* XXX: Store text */
                }
              else
                terminal.escape = 0;
            }
        }
      else if (terminal.param[0] == -4)
        {
          switch(buf[j])
            {
            case '0':

              terminal.alt_charset[terminal.curscreen] = 1;

              break;

            case 'B':

              terminal.alt_charset[terminal.curscreen] = 0;

              break;
            }

          terminal.escape = 0;
        }
      else if (terminal.param[0] == -5)
        {
          terminal.escape = 0;
        }
      else if (terminal.escape == 2 && buf[j] == '?')
        {
          terminal.param[0] = -3;
          ++terminal.escape;
        }
      else if (terminal.escape == 2 && buf[j] == '>')
        {
          terminal.param[0] = -4;
          ++terminal.escape;
        }
      else if (buf[j] == ';')
        {
          if (terminal.escape < sizeof(terminal.param) + 1)
            terminal.param[++terminal.escape - 2] = 0;
          else
            terminal.param[(sizeof(terminal.param) / sizeof(terminal.param[0])) - 1] = 0;
        }
      else if (buf[j] >= '0' && buf[j] <= '9')
        {
          terminal.param[terminal.escape - 2] *= 10;
          terminal.param[terminal.escape - 2] += buf[j] - '0';
        }
      else if (terminal.param[0] == -3)
        {
          if (buf[j] == 'h')
            {
              for (k = 1; k < terminal.escape - 1; ++k)
                {
                  switch(terminal.param[k])
                    {
                    case 1:

                      terminal.appcursor = 1;

                      break;

                    case 1049:

                      if (terminal.curscreen != 1)
                        {
                          memset(terminal.chars[1], 0, terminal.size.ws_col * terminal.history_size * sizeof(wchar_t));
                          memset(terminal.attr[1], 0x07, terminal.size.ws_col * terminal.history_size * sizeof(uint16_t));
                          setscreen(1);
                        }

                      break;
                    }
                }
            }
          else if (buf[j] == 'l')
            {
              for (k = 1; k < terminal.escape - 1; ++k)
                {
                  switch(terminal.param[k])
                    {
                    case 1:

                      terminal.appcursor = 0;

                      break;

                    case 1049:

                      if (terminal.curscreen != 0)
                        setscreen(0);

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

              if (!terminal.param[0])
                terminal.param[0] = 1;

              insert_chars(terminal.param[0]);

              break;

            case 'A':

              if (!terminal.param[0])
                terminal.param[0] = 1;

              terminal.cursory -= (terminal.param[0] < terminal.cursory) ? terminal.param[0] : terminal.cursory;

              break;

            case 'B':

              if (!terminal.param[0])
                terminal.param[0] = 1;

              terminal.cursory = (terminal.param[0] + terminal.cursory < terminal.size.ws_row) ? (terminal.param[0] + terminal.cursory) : (terminal.size.ws_row - 1);

              break;

            case 'C':

              if (!terminal.param[0])
                terminal.param[0] = 1;

              terminal.cursorx = (terminal.param[0] + terminal.cursorx < terminal.size.ws_col) ? (terminal.param[0] + terminal.cursorx) : (terminal.size.ws_col - 1);

              break;

            case 'D':

              if (!terminal.param[0])
                terminal.param[0] = 1;

              terminal.cursorx -= (terminal.param[0] < terminal.cursorx) ? terminal.param[0] : terminal.cursorx;

              break;

            case 'E':

              terminal.cursorx = 0;
              ++terminal.cursory;

              while (terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
                {
                  scroll(0);
                  --terminal.cursory;
                }

              break;

            case 'F':

              terminal.cursorx = 0;

              if (terminal.cursory == terminal.scrolltop)
                rscroll(0);
              else if (terminal.cursory)
                --terminal.cursory;

              terminal.escape = 0;

              break;

            case 'G':

              if (terminal.param[0] > 0)
                --terminal.param[0];

              terminal.cursorx = (terminal.param[0] < terminal.size.ws_col) ? terminal.param[0] : (terminal.size.ws_col - 1);

              break;

            case 'H':
            case 'f':

              if (terminal.param[0] > 0)
                --terminal.param[0];

              if (terminal.param[1] > 0)
                --terminal.param[1];

              terminal.cursory = (terminal.param[0] < terminal.size.ws_row) ? terminal.param[0] : (terminal.size.ws_row - 1);
              terminal.cursorx = (terminal.param[1] < terminal.size.ws_col) ? terminal.param[1] : (terminal.size.ws_col - 1);

              break;

            case 'J':

              if (terminal.param[0] == 0)
                {
                  /* Clear from cursor to end */

                  normalize_offset();

                  int count = terminal.size.ws_col * (terminal.size.ws_row - terminal.cursory - 1) + (terminal.size.ws_col - terminal.cursorx);
                  memset(&terminal.curchars[terminal.cursory * terminal.size.ws_col + terminal.cursorx], 0, count * sizeof(wchar_t));
                  memset16(&terminal.curattrs[terminal.cursory * terminal.size.ws_col + terminal.cursorx], terminal.curattr, count * sizeof(uint16_t));
                }
              else if (terminal.param[0] == 1)
                {
                  /* Clear from start to cursor */

                  normalize_offset();

                  int count = (terminal.size.ws_col * terminal.cursory + terminal.cursorx);
                  memset(terminal.curchars, 0, count * sizeof(wchar_t));
                  memset16(terminal.curattrs, terminal.curattr, count * sizeof(uint16_t));
                }
              else if (terminal.param[0] == 2)
                {
                  size_t screen_size, history_size;

                  screen_size = terminal.size.ws_col * terminal.size.ws_row;
                  history_size = terminal.size.ws_col * terminal.history_size;

                  if (*terminal.curoffset + screen_size > history_size)
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
                }

              break;

            case 'K':

              if (!terminal.param[0])
                {
                  /* Clear from cursor to end */

                  for (k = terminal.cursorx; k < terminal.size.ws_col; ++k)
                    {
                      terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 0;
                      terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
                      terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
                    }
                }
              else if (terminal.param[0] == 1)
                {
                  /* Clear from start to cursor */

                  for (k = 0; k <= terminal.cursorx; ++k)
                    {
                      terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 0;
                      terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
                      terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
                    }
                }
              else if (terminal.param[0] == 2)
                {
                  /* Clear entire line */

                  for (k = 0; k < terminal.size.ws_col; ++k)
                    {
                      terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 0;
                      terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
                      terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
                    }
                }

              break;

            case 'L':

              if (!terminal.param[0])
                terminal.param[0] = 1;
              else if (terminal.param[0] > terminal.size.ws_row)
                terminal.param[0] = terminal.size.ws_row;

              while (terminal.param[0]--)
                rscroll(1);

              break;

            case 'M':

              if (!terminal.param[0])
                terminal.param[0] = 1;
              else if (terminal.param[0] > terminal.size.ws_row)
                terminal.param[0] = terminal.size.ws_row;

              while (terminal.param[0]--)
                scroll(1);

              break;

            case 'P':

              /* Delete character at cursor */

              normalize_offset();

              if (!terminal.param[0])
                terminal.param[0] = 1;
              else if (terminal.param[0] > terminal.size.ws_col)
                terminal.param[0] = terminal.size.ws_col;

              while (terminal.param[0]--)
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

              if (!terminal.param[0])
                terminal.param[0] = 1;

              while (terminal.param[0]--)
                scroll(0);

              break;

            case 'T':

              if (!terminal.param[0])
                terminal.param[0] = 1;

              while (terminal.param[0]--)
                rscroll(0);

              break;

            case 'X':

              if (terminal.param[0] <= 0)
                terminal.param[0] = 1;

              for (k = terminal.cursorx; k < terminal.cursorx + terminal.param[0] && k < terminal.size.ws_col; ++k)
                {
                  terminal.curchars[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = 0;
                  terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.curattr;
                  terminal.curattrs[(terminal.cursory * terminal.size.ws_col + k + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
                }

              break;

            case 'd':

              if (terminal.param[0] > 0)
                --terminal.param[0];
              else
                terminal.param[0] = 0;

              terminal.cursory = (terminal.param[0] < terminal.size.ws_row) ? terminal.param[0] : (terminal.size.ws_row - 1);

              break;

            case 'h':

              for (k = 0; k < terminal.escape - 1; ++k)
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

              for (k = 0; k < terminal.escape - 1; ++k)
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

              for (k = 0; k < terminal.escape - 1; ++k)
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

                      for (l = 0; l < sizeof(ansi_helper) / sizeof(ansi_helper[0]); ++l)
                        {
                          if (ansi_helper[l].index == terminal.param[k])
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

              if (terminal.param[0] < terminal.param[1])
                {
                  --terminal.param[0];

                  if (terminal.param[1] > terminal.size.ws_row)
                    terminal.param[1] = terminal.size.ws_row;

                  if (terminal.param[0] < 0)
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

  XClearArea (X11_display, X11_window, 0, 0, 0, 0, True);
  XFlush(X11_display);
}

void term_read()
{
  unsigned char buf[4096];
  int result, more, fill = 0;

  /* This loop is designed for single-core computers, in case the data source is
   * doing a series of small writes */
  do
  {
    result = read(terminal.fd, buf + fill, sizeof(buf) - fill);

    if (result == -1)
      {
        save_session();

        exit(EXIT_SUCCESS);
      }

    fill += result;

    if (fill < sizeof(buf))
    {
      sched_yield();

      ioctl(terminal.fd, FIONREAD, &more);
    }
    else
      more = 0;
  }
  while (more);

  term_process_data(buf, fill);
}

void term_write(const char* data, size_t len)
{
  size_t off = 0;
  ssize_t result;

  while (off < len)
  {
    result = write(terminal.fd, data + off, len - off);

    if (result < 0)
      exit(EXIT_FAILURE);

    off += result;
  }
}

void term_strwrite(const char* data)
{
  term_write(data, strlen(data));
}

void x11_handle_configure(XConfigureEvent *config)
{
  int i;

  /* Resize event -- create new buffers and copy+clip old data */

  normalize_offset();

  window_width = config->width;
  window_height = config->height;

  glViewport (0, 0, window_width, window_height);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0.0f, window_width, window_height, 0.0f, 0.0f, 1.0f);

  cols = window_width / terminal.xskip;
  rows = window_height / terminal.yskip;

  if (!cols)
    cols = 1;

  if (!rows)
    rows = 1;

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

      if (rows > oldrows)
        minrows = oldrows;
      else
        {
          if (terminal.cursory >= rows)
            srcoff = terminal.cursory - rows + 1;
          minrows = rows;
        }

      int mincols = (cols < oldcols) ? cols : oldcols;

      for (i = 0; i < minrows; ++i)
        {
          memcpy(&terminal.chars[0][i * cols], &oldchars[0][(i + srcoff) * oldcols], mincols * sizeof(wchar_t));
          memcpy(&terminal.attr[0][i * cols], &oldattr[0][(i + srcoff) * oldcols], mincols * sizeof(terminal.attr[0][0]));
        }

      for (i = 0; i < minrows; ++i)
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
}

void run_command(int fd, const char* command, const char* arg)
{
  char path[4096];
  sprintf(path, ".cantera/commands/%s", command);

  if (-1 == access(path, X_OK))
    sprintf(path, PKGDATADIR "/commands/%s", command);

  if (-1 == access(path, X_OK))
    return;

  if (!fork())
  {
    char* args[3];

    if (fd != -1)
      dup2(fd, 1);

    args[0] = path;
    args[1] = (char*) arg;
    args[2] = 0;

    execve(args[0], args, environ);

    exit(EXIT_FAILURE);
  }
}

int x11_process_events()
{
  int result;
  int xfd;

  xfd = ConnectionNumber(X11_display);

  while (!done)
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
    if (terminal.fd > maxfd)
      maxfd = terminal.fd;

    if (-1 == select(maxfd + 1, &readset, 0, 0, 0))
    {
      if (errno == EINTR)
        continue;

      fprintf(stderr, "select failed: %s\n", strerror(errno));

      return -1;
    }

    if (FD_ISSET(terminal.fd, &readset))
      term_read();

    while (0 < (pid = waitpid(-1, &status, WNOHANG)))
    {
      if (pid == terminal.pid)
        {
          save_session();

          return 0;
        }
    }

    while (XPending(X11_display))
    {
      XNextEvent(X11_display, &event);

      switch(event.type)
      {
      case KeyPress:

        /* if (!XFilterEvent(&event, window)) */
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

          len = Xutf8LookupString(X11_xic, &event.xkey, text, sizeof(text) - 1, &key_sym, &status);

          if (!text[0])
            len = 0;

          if (key_sym == XK_Control_L || key_sym == XK_Control_R)
            ctrl_pressed = 1, history_scroll_reset = 0;

          if (key_sym == XK_Super_L || key_sym == XK_Super_R)
            super_pressed = 1, history_scroll_reset = 0;

          if (key_sym == XK_Alt_L || key_sym == XK_Alt_R
             || key_sym == XK_Shift_L || key_sym == XK_Shift_R)
            history_scroll_reset = 0;

          if (temp_switch_screen)
            {
              XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);

              temp_switch_screen = 0;
            }

          if (event.xkey.keycode == 161 || key_sym == XK_Menu)
          {
            normalize_offset();

            if (shift_pressed)
            {
              select_end = terminal.cursory * terminal.size.ws_col + terminal.cursorx;

              if (select_end == 0)
              {
                select_begin = 0;
                select_end = 1;
              }
              else
                select_begin = select_end - 1;

              find_range(range_parenthesis, &select_begin, &select_end);

              update_selection(CurrentTime);

              XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
            }
            else
            {
              if (select_text)
                run_command(terminal.fd, "calculate", (const char*) select_text);
            }
          }

	  if (key_sym == XK_Insert && shift_pressed)
	  {
	    paste(event.xkey.time);
	  }
	  else if (key_sym >= XK_F1 && key_sym <= XK_F4)
	  {
	    char buf[4];
	    buf[0] = '\033';
	    buf[1] = 'O';
	    buf[2] = 'P' + key_sym - XK_F1;
	    buf[3] = 0;

	    term_strwrite(buf);
	  }
	  else if (key_sym >= XK_F5 && key_sym <= XK_F12)
	  {
	    static int off[] = { 15, 17, 18, 19, 20, 21, 23, 24 };

	    char buf[6];
	    buf[0] = '\033';
	    buf[1] = '[';
	    buf[2] = '0' + off[key_sym - XK_F5] / 10;
	    buf[3] = '0' + off[key_sym - XK_F5] % 10;
	    buf[4] = '~';
	    buf[5] = 0;

	    term_strwrite(buf);
	  }
	  else if (key_sym == XK_Up)
	  {
	    if (shift_pressed)
	      {
		history_scroll_reset = 0;

		if (terminal.history_scroll < scroll_extra)
		  {
		    ++terminal.history_scroll;
		    XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
		  }
	      }
	    else if (terminal.appcursor)
	      term_strwrite("\033OA");
	    else
	      term_strwrite("\033[A");
	  }
	  else if (key_sym == XK_Down)
	  {
	    if (shift_pressed)
	      {
		history_scroll_reset = 0;

		if (terminal.history_scroll)
		  {
		    --terminal.history_scroll;
		    XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
		  }
	      }
	    else if (terminal.appcursor)
	      term_strwrite("\033OB");
	    else
	      term_strwrite("\033[B");
	  }
	  else if (key_sym == XK_Right)
	  {
	    if (ctrl_pressed)
	      term_strwrite(terminal.appcursor ? "\033OF" : "\033[F");
	    else if (terminal.appcursor)
	      term_strwrite("\033OC");
	    else
	      term_strwrite("\033[C");
	  }
	  else if (key_sym == XK_Left)
	  {
	    if (ctrl_pressed)
	      term_strwrite(terminal.appcursor ? "\033OH" : "\033[H");
	    else if (terminal.appcursor)
	      term_strwrite("\033OD");
	    else
	      term_strwrite("\033[D");
	  }
	  else if (key_sym == XK_Insert)
	  {
	    term_strwrite("\033[2~");
	  }
	  else if (key_sym == XK_Delete)
	  {
	    term_strwrite("\033[3~");
	  }
	  else if (key_sym == XK_Page_Up)
	  {
	    if (shift_pressed)
	      {
		history_scroll_reset = 0;

		terminal.history_scroll += terminal.size.ws_row;

		if (terminal.history_scroll > scroll_extra)
		  terminal.history_scroll = scroll_extra;

		XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
	      }
	    else
	      term_strwrite("\033[5~");
	  }
	  else if (key_sym == XK_Page_Down)
	  {
	    if (shift_pressed)
	      {
		history_scroll_reset = 0;

		if (terminal.history_scroll > terminal.size.ws_row)
		  terminal.history_scroll -= terminal.size.ws_row;
		else
		  terminal.history_scroll = 0;

		XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
	      }
	    else
	      term_strwrite("\033[6~");
	  }
	  else if (key_sym == XK_Home)
	  {
	    if (shift_pressed)
	      {
		history_scroll_reset = 0;

		if (terminal.history_scroll != scroll_extra)
		  {
		    terminal.history_scroll = scroll_extra;

		    XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
		  }
	      }
	    else if (terminal.appcursor)
	      term_strwrite("\033OH");
	    else
	      term_strwrite("\033[H");
	  }
	  else if (key_sym == XK_End)
	  {
	    if (terminal.appcursor)
	      term_strwrite("\033OF");
	    else
	      term_strwrite("\033[F");
	  }
	  else if (key_sym == XK_space)
	  {
	    /*
	       if (mod1_pressed)
	       term_strwrite("\033");

	     */
	    if (mod1_pressed)
	      {
		temp_switch_screen = 1;

		XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
	      }
	    else
	      term_strwrite(" ");
	  }
          else if (key_sym == XK_Alt_L)
          {
            /* Hack for leaping in vim */
            normalize_offset();

            if (terminal.curchars[0] == 32 && (terminal.curattrs[0] == 31 || terminal.curattrs[0] == 2063))
              term_write("\033?", 2);
          }
          else if (key_sym == XK_Alt_R)
          {
            /* Hack for leaping in vim */

            normalize_offset();

            if (terminal.curchars[0] == 32 && (terminal.curattrs[0] == 31 || terminal.curattrs[0] == 2063))
              term_write("\033/", 2);
          }
	  else if (key_sym == XK_Shift_L || key_sym == XK_Shift_R
	       || key_sym == XK_ISO_Prev_Group || key_sym == XK_ISO_Next_Group)
	  {
	    /* Do not generate characters on shift key, or gus'
	     * special shift keys */
	  }
	  else if (len)
	  {
	    if (mod1_pressed)
	      term_strwrite("\033");

	    term_write((const char*) text, len);
	  }

	  if (history_scroll_reset && terminal.history_scroll)
	  {
	    terminal.history_scroll = 0;
	    XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
	  }
        }

        break;

      case KeyRelease:

        {
          ctrl_pressed = (event.xkey.state & ControlMask);
          mod1_pressed = (event.xkey.state & Mod1Mask);
          super_pressed = (event.xkey.state & Mod4Mask);
          shift_pressed = (event.xkey.state & ShiftMask);
        }

        break;

      case MotionNotify:

        ctrl_pressed = (event.xkey.state & ControlMask);

	if (event.xbutton.state & Button1Mask)
	{
	  int x, y, new_select_end;
	  unsigned int size;

	  size = terminal.history_size * terminal.size.ws_col;

	  x = event.xbutton.x / terminal.xskip;
	  y = event.xbutton.y / terminal.yskip;

	  new_select_end = y * terminal.size.ws_col + x;

	  if (terminal.history_scroll)
	    new_select_end += size - (terminal.history_scroll * terminal.size.ws_col);

	  if (ctrl_pressed)
	  {
	    find_range(range_word_or_url, &select_begin, &new_select_end);
	  }

	  if (new_select_end != select_end)
	  {
	    select_end = new_select_end;

	    XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
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

            if (terminal.history_scroll)
              select_begin += size - (terminal.history_scroll * terminal.size.ws_col);

            select_end = select_begin;

            if (ctrl_pressed)
            {
              find_range(range_word_or_url, &select_begin, &select_end);
            }

            XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
          }

          break;

        case 2: /* Middle button */

          paste(event.xbutton.time);

          break;

        case 4: /* Up */

          if (terminal.history_scroll < scroll_extra)
            {
              ++terminal.history_scroll;
              XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
            }

          break;

        case 5: /* Down */

          if (terminal.history_scroll)
            {
              --terminal.history_scroll;
              XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);
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

            if (select_text && (event.xkey.state & Mod1Mask))
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

          if (request->property == None)
            request->property = request->target;

          response.type = SelectionNotify;
          response.send_event = True;
          response.display = X11_display;
          response.requestor = request->requestor;
          response.selection = request->selection;
          response.target = request->target;
          response.property = None;
          response.time = request->time;

          /* fprintf(stderr, "Wanting select_text %s\n", XGetAtomName(display, response.target)); */

          if (select_text)
          {
            if (request->target == XA_STRING
            || request->target == xa_utf8_string)
            {
              result = XChangeProperty(X11_display, request->requestor, request->property,
                                       request->target, 8, PropModeReplace, select_text, select_length);

              if (result != BadAlloc && result != BadAtom && result != BadValue && result != BadWindow)
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

          result = XGetWindowProperty(X11_display, X11_window, prop_paste, 0, 0, False, AnyPropertyType,
                                      &type, &format, &nitems, &bytes_after, &prop);

          if (result != Success)
            break;

          XFree(prop);

          result = XGetWindowProperty(X11_display, X11_window, prop_paste, 0, bytes_after, False, AnyPropertyType,
                                      &type, &format, &nitems, &bytes_after, &prop);

          if (result != Success)
            break;

          if (type != xa_utf8_string || format != 8)
            break;

          term_write((char*) prop, nitems);

          XFree(prop);
        }

        break;

      case ConfigureNotify:

        {
          /* Skip to last ConfigureNotify event */
          while (XCheckTypedWindowEvent(X11_display, X11_window, ConfigureNotify, &event))
          {
            /* Do nothing */
          }

          if (window_width == event.xconfigure.width && window_height == event.xconfigure.height)
            break;

          x11_handle_configure(&event.xconfigure);
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

          while (XCheckTypedWindowEvent(X11_display, X11_window, Expose, &event))
          {
            if (event.xexpose.x < minx) minx = event.xexpose.x;
            if (event.xexpose.y < miny) miny = event.xexpose.y;
            if (event.xexpose.x + event.xexpose.width > maxx) maxx = event.xexpose.x + event.xexpose.width;
            if (event.xexpose.y + event.xexpose.height > maxy) maxy = event.xexpose.y + event.xexpose.height;
          }

          paint(minx, miny, maxx - minx, maxy - miny);
        }

        break;

      case FocusIn:

        focused = 1;
        XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);

        break;

      case FocusOut:

        focused = 0;
        XClearArea(X11_display, X11_window, 0, 0, window_width, window_height, True);

        break;
      }
    }
  }

  return 0;
}

int main(int argc, char** argv)
{
  char* args[16];
  int i;
  int session_fd;
  char* palette_str;
  char* token;

  for (i = 1; i < argc; ++i)
    if (!strcmp (argv[i], "-e"))
      argv[i] = "--";

  setlocale(LC_ALL, "en_US.UTF-8");

  while ((i = getopt_long (argc, argv, "T:", long_options, 0)) != -1)
  {
    switch (i)
    {
    case 0:

      break;

    case 'T':

      title = optarg;

      break;

    case 'i':

      parent_window = strtol (optarg, 0, 0);

      break;

    case 'p':

      pty_fd = strtol (optarg, 0, 0);

      break;


    case 'w':

      window_width = strtol (optarg, 0, 0);

      break;

    case 'h':

      window_height = strtol (optarg, 0, 0);

      break;

    case '?':

      fprintf (stderr, "Try `%s --help' for more information.\n", argv[0]);

      return EXIT_FAILURE;
    }
  }

  if (print_help)
    {
      printf ("Usage: %s [OPTION]...\n"
             "\n"
             "  -T, --title=TITLE          set window title to TITLE\n"
             "  -e, --command=COMMAND      execute COMMAND instead of /bin/bash\n"
             "      --help     display this help and exit\n"
             "      --version  display version information\n"
             "\n"
             "Report bugs to <morten@rashbox.org>\n", argv[0]);

      return EXIT_SUCCESS;
    }

  if (print_version)
    {
      fprintf (stdout, "%s\n", PACKAGE_STRING);

      return EXIT_SUCCESS;
    }

  if (!getenv("DISPLAY"))
  {
    fprintf(stderr, "DISPLAY variable not set.\n");

    return EXIT_FAILURE;
  }

  session_path = getenv("SESSION_PATH");

  if (session_path)
    unsetenv("SESSION_PATH");

  chdir(getenv("HOME"));

  mkdir(".cantera", 0777);
  mkdir(".cantera/commands", 0777);
  mkdir(".cantera/file-commands", 0777);
  mkdir(".cantera/filemanager", 0777);

  config = tree_load_cfg(".cantera/config");

  palette_str = strdup(tree_get_string_default(config, "terminal.palette", "000000 1818c2 18c218 18c2c2 c21818 c218c2 c2c218 c2c2c2 686868 7474ff 54ff54 54ffff ff5454 ff54ff ffff54 ffffff"));

  for (i = 0, token = strtok(palette_str, " "); token;
      ++i, token = strtok(0, " "))
    {
      if (palette[i] < 16)
        palette[i] = 0xff000000 | strtol(token, 0, 16);
    }

  scroll_extra = tree_get_integer_default(config, "terminal.history-size", 1000);
  font_name = tree_get_string_default(config, "terminal.font", "/usr/share/fonts/truetype/msttcorefonts/Andale_Mono.ttf");
  /*font_sizes[0] = tree_get_integer_default(config, "terminal.font-size", 12);*/

  signal(SIGTERM, sighandler);
  signal(SIGIO, sighandler);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

  setenv("TERM", "xterm", 1);

  if (session_path)
    {
      session_fd = open(session_path, O_RDONLY);

      if (session_fd != -1)
        {
          struct winsize ws;

          if (sizeof(ws) == read(session_fd, &ws, sizeof(ws)))
            {
              window_width = ws.ws_xpixel;
              window_height = ws.ws_ypixel;
            }
        }
    }
  else
    session_fd = -1;

  X11_Setup ();

  if (optind < argc)
  {
    for (i = 0; i < argc - optind && i + 1 < sizeof(args) / sizeof(args[0]); ++i)
      args[i] = argv[optind + i];

    args[i] = 0;
  }
  else
  {
    args[0] = "/bin/bash";
    args[1] = 0;
  }

  init_session(args);

  if (session_fd != -1)
    {
      size_t size;

      size = terminal.size.ws_col * terminal.history_size;

      read(session_fd, &terminal.cursorx, sizeof(terminal.cursorx));
      read(session_fd, &terminal.cursory, sizeof(terminal.cursory));

      if (terminal.cursorx >= terminal.size.ws_col
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

          if (terminal.cursory >= terminal.size.ws_row)
            terminal.cursory = terminal.size.ws_row - 1;
          terminal.cursorx = 0;
        }

      close(session_fd);
      unlink(session_path);
    }

  if (-1 == x11_process_events())
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}

// vim: ts=2 sw=2 et sts=2
