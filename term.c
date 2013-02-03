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
#include <pthread.h>
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
#include <X11/extensions/Xrender.h>
#include <X11/keysym.h>

#include "draw.h"
#include "font.h"
#include "glyph.h"
#include "terminal.h"
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
unsigned int font_size, font_weight;
struct FONT_Data *font;

extern char** environ;

unsigned int palette[16];

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

static void normalize_offset();

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
int screenidx;
int cols;
int rows;
int ctrl_pressed = 0;
int mod1_pressed = 0;
int super_pressed = 0;
int shift_pressed = 0;
int button1_pressed = 0;
struct timeval lastpaint = { 0, 0 };
unsigned char* select_text = 0;
unsigned long select_alloc = 0;
unsigned long select_length;

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

static void
term_LoadGlyph (wchar_t character)
{
  struct FONT_Glyph *glyph;

  if (!(glyph = FONT_GlyphForCharacter (font, character)))
    fprintf (stderr, "Failed to get glyph for '%d'", character);

  GLYPH_Add (character, glyph);

  free (glyph);
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

  if (!GLYPH_IsLoaded (ch))
    term_LoadGlyph (ch);

  if (terminal.insertmode)
    insert_chars(1);

  terminal.curchars[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = ch;
  terminal.curattrs[(terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size] = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;
  ++terminal.cursorx;
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

  term_clear_selection ();

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

  term_clear_selection ();

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

void term_clear_selection (void)
{
  terminal.select_begin = -1;
  terminal.select_end = -1;
}

void init_session(char* const* args)
{
  char* c;
  int stderr_backup;

  memset(&terminal, 0, sizeof(terminal));

  terminal.size.ws_xpixel = X11_window_width;
  terminal.size.ws_ypixel = X11_window_height;
  terminal.size.ws_col = X11_window_width / FONT_SpaceWidth (font);
  terminal.size.ws_row = X11_window_height / FONT_LineHeight (font);
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
          if (-1 == execve(args[0], args, environ))
	  {
	    dup2 (stderr_backup, 2);
	    err (EXIT_FAILURE, "Failed to execute '%s'", args[0]);
	  }
        }
    }

  fcntl (terminal.fd, F_SETFL, O_NDELAY);

  pthread_mutex_init (&terminal.bufferLock, 0);

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

  term_clear_selection ();

  setscreen(0);
}

static void save_session()
{
  int fd;
  size_t size;

  if (!session_path)
    return;

  if (terminal.cursorx)
    term_process_data((const unsigned char *) "\r\n", 2);

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

  if (terminal.select_begin == terminal.select_end)
    return;

  size = terminal.size.ws_col * terminal.history_size;
  offset = *terminal.curoffset;

  if (select_text)
  {
    free(select_text);
    select_text = 0;
  }

  if (terminal.select_begin > terminal.select_end)
  {
    i = terminal.select_begin;
    terminal.select_begin = terminal.select_end;
    terminal.select_end = i;
  }

  select_alloc = terminal.select_end - terminal.select_begin + 1;
  select_text = calloc(select_alloc, 1);
  select_length = 0;

  size_t last_graph = 0;
  size_t last_graph_col = 0;
  i = terminal.select_begin;

  while (i != terminal.select_end)
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

    if (i > terminal.select_begin && (i % width) == 0)
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
    terminal.select_begin = terminal.select_end;
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

void term_process_data(const unsigned char* buf, size_t count)
{
  const unsigned char *end;
  int k, l;

  int size = terminal.size.ws_col * terminal.history_size;

  /* XXX: Make sure cursor does not leave screen */

  end = buf + count;

  while (terminal.cursory >= terminal.size.ws_row)
    {
      scroll(0);
      --terminal.cursory;
    }

  /* Redundant character processing code for the typical case */
  if (!terminal.escape && !terminal.insertmode && !terminal.nch)
    {
      unsigned int attr, offset;

      attr = terminal.reverse ? REVERSE(terminal.curattr) : terminal.curattr;

      offset = (terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size;

      for (; buf != end; ++buf)
        {
          if (*buf >= ' ' && *buf <= '~')
            {
              if (terminal.cursorx == terminal.size.ws_col)
                {
                  if (++terminal.cursory >= terminal.size.ws_row)
                    {
                      scroll(0);
                      --terminal.cursory;
                    }

                  terminal.cursorx = 0;

                  offset = (terminal.cursory * terminal.size.ws_col + *terminal.curoffset) % size;
                }

              terminal.curchars[offset] = *buf;
              terminal.curattrs[offset] = attr;
              ++terminal.cursorx;
              ++offset;
            }
          else if (*buf == '\r')
            {
              terminal.cursorx = 0;
              offset = (terminal.cursory * terminal.size.ws_col + *terminal.curoffset) % size;
            }
          else if (*buf == '\n')
            {
              ++terminal.cursory;

              if (terminal.cursory == terminal.scrollbottom || terminal.cursory >= terminal.size.ws_row)
                {
                  scroll(0);
                  --terminal.cursory;
                }

              offset = (terminal.cursory * terminal.size.ws_col + terminal.cursorx + *terminal.curoffset) % size;
            }
          else
            break;
        }
    }

  for (; buf != end; ++buf)
    {
      switch(terminal.escape)
        {
        case 0:

          switch(*buf)
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
                  if ((*buf & 0xC0) != 0x80)
                    {
                      terminal.nch = 0;
                      addchar(*buf);
                    }
                  else
                    {
                      terminal.ch <<= 6;
                      terminal.ch |= *buf & 0x3F;

                      if (0 == --terminal.nch)
                        {
                          addchar(terminal.ch);
                        }
                    }
                }
              else
                {
                  if ((*buf & 0x80) == 0)
                    {
                      addchar(*buf);
                    }
                  else if ((*buf & 0xE0) == 0xC0)
                    {
                      terminal.ch = *buf & 0x1F;
                      terminal.nch = 1;
                    }
                  else if ((*buf & 0xF0) == 0xE0)
                    {
                      terminal.ch = *buf & 0x0F;
                      terminal.nch = 2;
                    }
                  else if ((*buf & 0xF8) == 0xF0)
                    {
                      terminal.ch = *buf & 0x03;
                      terminal.nch = 3;
                    }
                  else if ((*buf & 0xFC) == 0xF8)
                    {
                      terminal.ch = *buf & 0x01;
                      terminal.nch = 4;
                    }
                }
            }

          break;

        case 1:

          switch(*buf)
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
                  if (*buf >= '0' && *buf <= '9')
                    {
                      terminal.param[1] *= 10;
                      terminal.param[1] += *buf - '0';
                    }
                  else
                    ++terminal.escape;
                }
              else
                {
                  if (*buf != '\007')
                    {
                      /* XXX: Store text */
                    }
                  else
                    terminal.escape = 0;
                }
            }
          else if (terminal.param[0] == -4)
            {
              switch(*buf)
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
          else if (terminal.escape == 2 && *buf == '?')
            {
              terminal.param[0] = -3;
              ++terminal.escape;
            }
          else if (terminal.escape == 2 && *buf == '>')
            {
              terminal.param[0] = -4;
              ++terminal.escape;
            }
          else if (*buf == ';')
            {
              if (terminal.escape < sizeof(terminal.param) + 1)
                terminal.param[++terminal.escape - 2] = 0;
              else
                terminal.param[(sizeof(terminal.param) / sizeof(terminal.param[0])) - 1] = 0;
            }
          else if (*buf >= '0' && *buf <= '9')
            {
              terminal.param[terminal.escape - 2] *= 10;
              terminal.param[terminal.escape - 2] += *buf - '0';
            }
          else if (terminal.param[0] == -3)
            {
              if (*buf == 'h')
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
              else if (*buf == 'l')
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
              switch(*buf)
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
}

void term_write(const char* data, size_t len)
{
  size_t off = 0;
  ssize_t result;

  term_clear_selection ();

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

void X11_handle_configure (void)
{
  int i;

  /* Resize event -- create new buffers and copy+clip old data */

  normalize_offset();

  glViewport (0, 0, X11_window_width, X11_window_height);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glOrtho (0.0f, X11_window_width, X11_window_height, 0.0f, 0.0f, 1.0f);

  cols = X11_window_width / FONT_SpaceWidth (font);
  rows = X11_window_height / FONT_LineHeight (font);

  if (!cols)
    cols = 1;

  if (!rows)
    rows = 1;

  int oldcols = terminal.size.ws_col;
  int oldrows = terminal.size.ws_row;

  terminal.size.ws_xpixel = X11_window_width;
  terminal.size.ws_ypixel = X11_window_height;
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

static void *
tty_read_thread_entry (void *arg)
{
  unsigned char buf[4096];
  int result, fill = 0;
  struct pollfd pfd;

  pfd.fd = terminal.fd;
  pfd.events = POLLIN | POLLRDHUP;

  while (-1 != poll (&pfd, 1, -1))
    {
      while (0 < (result = read(terminal.fd, buf + fill, sizeof (buf) - fill)))
        {
          fill += result;

          if (fill == sizeof (buf))
            break;
        }

      if (result == -1 && errno != EAGAIN)
        break;

      if (0 != pthread_mutex_trylock (&terminal.bufferLock))
        {
          if (fill < sizeof (buf))
            {
              if (0 < poll (&pfd, 1, 1))
                continue;
            }

          pthread_mutex_lock (&terminal.bufferLock);
        }

      term_process_data(buf, fill);
      fill = 0;
      pthread_mutex_unlock (&terminal.bufferLock);

      XClearArea (X11_display, X11_window, 0, 0, 0, 0, True);
      XFlush(X11_display);
    }

  save_session();

  exit(EXIT_SUCCESS);

  return NULL;
}

static void
wait_for_dead_children (void)
{
  pid_t pid;
  int status;

  while (0 < (pid = waitpid(-1, &status, WNOHANG)))
    {
      if (pid == terminal.pid)
        {
          save_session();

          return exit (EXIT_SUCCESS);
        }
    }
}

int x11_process_events()
{
  XEvent event;
  int result;

  while(!done && 0 == XNextEvent(X11_display, &event))
    {
      wait_for_dead_children ();

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

              if (event.xkey.keycode == 161 || key_sym == XK_Menu)
                {
                  normalize_offset();

                  if (shift_pressed)
                    {
                      terminal.select_end = terminal.cursory * terminal.size.ws_col + terminal.cursorx;

                      if (terminal.select_end == 0)
                        {
                          terminal.select_begin = 0;
                          terminal.select_end = 1;
                        }
                      else
                        terminal.select_begin = terminal.select_end - 1;

                      find_range(range_parenthesis, &terminal.select_begin, &terminal.select_end);

                      update_selection(CurrentTime);

                      XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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
                          XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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
                          XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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

                      XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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

                      XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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

                          XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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
                  if (mod1_pressed)
                    term_strwrite("\033");

                  term_strwrite(" ");
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
                  XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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

              x = event.xbutton.x / FONT_SpaceWidth (font);
              y = event.xbutton.y / FONT_LineHeight (font);

              new_select_end = y * terminal.size.ws_col + x;

              if (terminal.history_scroll)
                new_select_end += size - (terminal.history_scroll * terminal.size.ws_col);

              if (ctrl_pressed)
                {
                  find_range(range_word_or_url, &terminal.select_begin, &new_select_end);
                }

              if (new_select_end != terminal.select_end)
                {
                  terminal.select_end = new_select_end;

                  XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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

                  x = event.xbutton.x / FONT_SpaceWidth (font);
                  y = event.xbutton.y / FONT_LineHeight (font);

                  terminal.select_begin = y * terminal.size.ws_col + x;

                  if (terminal.history_scroll)
                    terminal.select_begin += size - (terminal.history_scroll * terminal.size.ws_col);

                  terminal.select_end = terminal.select_begin;

                  if (ctrl_pressed)
                    {
                      find_range(range_word_or_url, &terminal.select_begin, &terminal.select_end);
                    }

                  XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
                }

              break;

            case 2: /* Middle button */

              paste(event.xbutton.time);

              break;

            case 4: /* Up */

              if (terminal.history_scroll < scroll_extra)
                {
                  ++terminal.history_scroll;
                  XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
                }

              break;

            case 5: /* Down */

              if (terminal.history_scroll)
                {
                  --terminal.history_scroll;
                  XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);
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

        case MapNotify:

          X11_handle_configure ();

          break;

        case ConfigureNotify:

            {
              /* Skip to last ConfigureNotify event */
              while (XCheckTypedWindowEvent(X11_display, X11_window, ConfigureNotify, &event))
                {
                  /* Do nothing */
                }

              X11_window_width = event.xconfigure.width;
              X11_window_height = event.xconfigure.height;

              X11_handle_configure ();
            }

          break;

        case NoExpose:

          break;

        case Expose:

          while (XCheckTypedWindowEvent(X11_display, X11_window, Expose, &event))
            ;

          draw_gl_12 (&terminal);

          break;

        case FocusIn:

          terminal.focused = 1;
          XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);

          break;

        case FocusOut:

          terminal.focused = 0;
          XClearArea(X11_display, X11_window, 0, 0, X11_window_width, X11_window_height, True);

          break;
        }
    }

  return 0;
}

int main(int argc, char** argv)
{
  pthread_t tty_read_thread;
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

          X11_window_width = strtol (optarg, 0, 0);

          break;

        case 'h':

          X11_window_height = strtol (optarg, 0, 0);

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
  font_name = tree_get_string_default(config, "terminal.font", "Andale Mono");
  font_size = tree_get_integer_default(config, "terminal.font-size", 12);
  font_weight = tree_get_integer_default(config, "terminal.font-weight", 200);

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
              X11_window_width = ws.ws_xpixel;
              X11_window_height = ws.ws_ypixel;
            }
        }
    }
  else
    session_fd = -1;

  X11_Setup ();

  glEnable (GL_TEXTURE_2D);

  FONT_Init ();
  GLYPH_Init ();

  if (!(font = FONT_Load (font_name, font_size, font_weight)))
    errx (EXIT_FAILURE, "Failed to load font `%s' of size %u, weight %u", font_name, font_size, font_weight);

  /* ASCII */
  for (i = ' '; i <= '~'; ++i)
    term_LoadGlyph (i);

  /* ISO-8859-1 */
  for (i = 0xa1; i <= 0xff; ++i)
    term_LoadGlyph (i);

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

  X11_handle_configure ();

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

  pthread_create (&tty_read_thread, 0, tty_read_thread_entry, 0);
  pthread_detach (tty_read_thread);

  if (-1 == x11_process_events())
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}

// vim: ts=2 sw=2 et sts=2
