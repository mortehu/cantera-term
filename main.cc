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

#include <algorithm>
#include <memory>
#include <unordered_map>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include "array.h"
#include "draw.h"
#include "font.h"
#include "glyph.h"
#include "terminal.h"
#include "tree.h"
#include "x11.h"

extern char **environ;

FONT_Data *font;
unsigned int palette[16];
int home_fd;

namespace {

int print_version;
int print_help;

struct option long_options[] = { { "version", no_argument, &print_version, 1 },
                                 { "help", no_argument, &print_help, 1 },
                                 { 0, 0, 0, 0 } };

std::unique_ptr<tree> config;
bool hidden;

unsigned int scroll_extra;

const char *font_name;
unsigned int font_size, font_weight;

int done;
const char *session_path;

const struct {
  uint16_t index;
  uint16_t and_mask;
  uint16_t or_mask;
} ansi_helper[] = {
  { 0, 0, ATTR_DEFAULT }, { 1, 0xffff ^ ATTR_BOLD, ATTR_BOLD },
  { 2, 0xffff ^ ATTR_BOLD, 0 }, { 3, 0xffff ^ ATTR_STANDOUT, ATTR_STANDOUT },
  { 4, 0xffff ^ ATTR_UNDERLINE, ATTR_UNDERLINE },
  { 5, 0xffff ^ ATTR_BLINK, ATTR_BLINK }, /* 7 = reverse video */
  { 8, 0, 0 },
  { 22, (uint16_t)(~ATTR_BOLD & ~ATTR_STANDOUT & ~ATTR_UNDERLINE), 0 },
  { 23, 0xffff ^ ATTR_STANDOUT, 0 }, { 24, 0xffff ^ ATTR_UNDERLINE, 0 },
  { 25, 0xffff ^ ATTR_BLINK, 0 },         /* 27 = no reverse */
  { 30, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_BLACK) },
  { 31, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_RED) },
  { 32, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_GREEN) },
  { 33, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_YELLOW) },
  { 34, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_BLUE) },
  { 35, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_MAGENTA) },
  { 36, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_CYAN) },
  { 37, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_WHITE) },
  { 39, 0xffff ^ FG(ATTR_WHITE), FG_DEFAULT },
  { 40, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_BLACK) },
  { 41, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_RED) },
  { 42, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_GREEN) },
  { 43, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_YELLOW) },
  { 44, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_BLUE) },
  { 45, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_MAGENTA) },
  { 46, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_CYAN) },
  { 47, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_WHITE) },
  { 49, 0xffff ^ BG(ATTR_WHITE), BG_DEFAULT }
};

Terminal terminal;

unsigned char *select_text = NULL;
size_t select_alloc, select_length;

unsigned char *clipboard_text = NULL;
size_t clipboard_length;

bool clear;
pthread_mutex_t clear_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t clear_cond = PTHREAD_COND_INITIALIZER;

}

static void *memset16(void *s, int w, size_t n) {
  uint16_t *o = (uint16_t *)s;

  assert(!(n & 1));

  n >>= 1;

  while (n--)
    *o++ = w;

  return s;
}

static void term_LoadGlyph(wchar_t character) {
  struct FONT_Glyph *glyph;

  if (!(glyph = FONT_GlyphForCharacter(font, character)))
    fprintf(stderr, "Failed to get glyph for '%d'", character);

  GLYPH_Add(character, glyph);

  free(glyph);
}

Terminal::Terminal()
    : chars(),
      attr(),
      curchars(),
      curattrs(),
      scroll_line(),
      cur_scroll_line(),
      curscreen(),
      curattr(),
      reverse(),
      history_size(),
      fontsize(),
      storedcursorx(),
      storedcursory(),
      scrolltop(),
      scrollbottom(),
      cursorx(),
      cursory(),
      escape(),
      param(),
      appcursor(),
      hide_cursor(),
      insertmode(),
      select_begin(-1),
      select_end(-1),
      focused(),
      history_scroll(),
      use_alt_charset_(),
      nch_(),
      savedx_(),
      savedy_() {
  pthread_mutex_init(&bufferLock, 0);
}

void Terminal::Init(char *const *args, unsigned int width,
                    unsigned int height) {
  char *c;

  size.ws_col = width / FONT_SpaceWidth(font);
  size.ws_row = height / FONT_LineHeight(font);
  size.ws_xpixel = width;
  size.ws_ypixel = height;

  history_size = size.ws_row + scroll_extra;

  if (-1 == (pid_ = forkpty(&fd, 0, 0, &size)))
    err(EX_OSERR, "forkpty() failed");

  if (!pid_) {
    /* In child process */

    execve(args[0], args, environ);

    fprintf(stderr, "Failed to execute '%s'", args[0]);

    _exit(EXIT_FAILURE);
  }

  fcntl(fd, F_SETFL, O_NDELAY);

  buffer = (char *)calloc(2 * size.ws_col * history_size,
                          sizeof(wchar_t) + sizeof(uint16_t));
  c = buffer;
  chars[0] = (wchar_t *)c;
  c += size.ws_col * history_size * sizeof(wchar_t);
  attr[0] = (uint16_t *)c;
  c += size.ws_col * history_size * sizeof(uint16_t);
  chars[1] = (wchar_t *)c;
  c += size.ws_col * history_size * sizeof(wchar_t);
  attr[1] = (uint16_t *)c;
  curattr = 0x07;
  scrollbottom = size.ws_row;
  std::fill(chars[0], chars[0] + size.ws_col * history_size, 0);
  std::fill(attr[0], attr[0] + size.ws_col * history_size, curattr);
  std::fill(attr[1], attr[1] + size.ws_col * history_size, curattr);
  scroll_line[0] = 0;
  scroll_line[1] = 0;

  ClearSelection();

  SetScreen(0);
}

void Terminal::Resize(unsigned int width, unsigned int height) {
  int cols = std::max(width / FONT_SpaceWidth(font), 1U);
  int rows = std::max(height / FONT_LineHeight(font), 1U);

  int oldcols = size.ws_col;
  int oldrows = size.ws_row;

  size.ws_xpixel = width;
  size.ws_ypixel = height;
  size.ws_col = cols;
  size.ws_row = rows;
  history_size = rows + scroll_extra;

  if (cols != oldcols || rows != oldrows) {
    wchar_t *oldchars[2] = { chars[0], chars[1] };
    uint16_t *oldattr[2] = { attr[0], attr[1] };

    char *oldbuffer = buffer;
    buffer = (char *)calloc(2 * cols * history_size,
                            (sizeof(wchar_t) + sizeof(uint16_t)));

    char *c;
    c = buffer;
    chars[0] = (wchar_t *)c;
    c += cols * history_size * sizeof(wchar_t);
    attr[0] = (uint16_t *)c;
    c += cols * history_size * sizeof(uint16_t);
    chars[1] = (wchar_t *)c;
    c += cols * history_size * sizeof(wchar_t);
    attr[1] = (uint16_t *)c;
    scrollbottom = rows;

    int srcoff = 0;
    int minrows;

    if (rows > oldrows) {
      minrows = oldrows;
    } else {
      if (cursory >= rows) srcoff = cursory - rows + 1;
      minrows = rows;
    }

    int mincols = (cols < oldcols) ? cols : oldcols;

    for (int i = 0; i < minrows; ++i) {
      memcpy(&chars[0][i * cols], &oldchars[0][(i + srcoff) * oldcols],
             mincols * sizeof(wchar_t));
      memcpy(&attr[0][i * cols], &oldattr[0][(i + srcoff) * oldcols],
             mincols * sizeof(attr[0][0]));
    }

    for (int i = 0; i < minrows; ++i) {
      memcpy(&chars[1][i * cols], &oldchars[1][(i + srcoff) * oldcols],
             mincols * sizeof(wchar_t));
      memcpy(&attr[1][i * cols], &oldattr[1][(i + srcoff) * oldcols],
             mincols * sizeof(attr[1][0]));
    }

    free(oldbuffer);

    curchars = chars[curscreen];
    curattrs = attr[curscreen];

    cursory = cursory - srcoff;
    storedcursory[1 - curscreen] += rows - oldrows;
#define CLIP_CURSOR(val, max) \
  {                           \
    if (val < 0)              \
      val = 0;                \
    else if (val >= max)      \
      val = max - 1;          \
  }
    CLIP_CURSOR(cursorx, cols);
    CLIP_CURSOR(cursory, rows);
    CLIP_CURSOR(storedcursorx[1 - curscreen], cols);
    CLIP_CURSOR(storedcursory[1 - curscreen], rows);
#undef CLIP_CURSOR

    ioctl(fd, TIOCSWINSZ, &size);
  }
}

void Terminal::ProcessData(const unsigned char *buf, size_t count) {
  const unsigned char *end;
  int k;

  // TODO(mortehu): Make sure cursor does not leave screen.
  end = buf + count;

  while (cursory >= size.ws_row) {
    Scroll(false);
    --cursory;
  }

  // Redundant, optimized character processing code for the typical case.
  if (!escape && !insertmode && !nch_) {
    unsigned int attr, offset;

    attr = reverse ? REVERSE(curattr) : curattr;

    offset =
        (*cur_scroll_line + cursory) % history_size * size.ws_col + cursorx;

    for (; buf != end; ++buf) {
      if (*buf >= ' ' && *buf <= '~') {
        if (cursorx == size.ws_col) {
          if (++cursory >= size.ws_row) {
            Scroll(false);
            --cursory;
          }

          cursorx = 0;

          offset = (*cur_scroll_line + cursory) % history_size * size.ws_col;
        }

        curchars[offset] = *buf;
        curattrs[offset] = attr;
        ++cursorx;
        ++offset;
      } else if (*buf == '\r') {
        cursorx = 0;
        offset = (*cur_scroll_line + cursory) % history_size * size.ws_col;
      } else if (*buf == '\n') {
        ++cursory;

        if (cursory == scrollbottom || cursory >= size.ws_row) {
          Scroll(false);
          --cursory;
        }

        offset =
            (*cur_scroll_line + cursory) % history_size * size.ws_col + cursorx;
      } else
        break;
    }
  }

  for (; buf != end; ++buf) {
    switch (escape) {
      case 0:

        switch (*buf) {
          case '\033':

            escape = 1;
            memset(param, 0, sizeof(param));

            break;

          case '\b':

            if (cursorx > 0) --cursorx;

            break;

          case '\t':

            if (cursorx < size.ws_col - 8)
              cursorx = (cursorx + 8) & ~7;
            else
              cursorx = size.ws_col - 1;

            break;

          case '\n':

            ++cursory;

            while (cursory == scrollbottom || cursory >= size.ws_row) {
              Scroll(false);
              --cursory;
            }

            break;

          case '\r':

            cursorx = 0;

            break;

          case '\177':

            if (cursory < size.ws_row)
              terminal.curchars[(*cur_scroll_line + cursory) % history_size *
                                    size.ws_col + cursorx] = 0;

            break;

          case('O' & 0x3F) : /* ^O = default character set */

            break;

          case('N' & 0x3F) : /* ^N = alternate character set */

            break;

          default:

            assert(cursorx >= 0 && cursorx <= size.ws_col);
            assert(cursory >= 0 && cursory < size.ws_row);

            if (cursorx == size.ws_col) {
              ++cursory;
              cursorx = 0;
            }

            while (cursory >= size.ws_row) {
              Scroll(false);
              --cursory;
            }

            if (nch_) {
              if ((*buf & 0xC0) != 0x80) {
                nch_ = 0;
                AddChar(*buf);
              } else {
                ch_ <<= 6;
                ch_ |= *buf & 0x3F;

                if (0 == --nch_) {
                  AddChar(ch_);
                }
              }
            } else {
              if ((*buf & 0x80) == 0) {
                AddChar(*buf);
              } else if ((*buf & 0xE0) == 0xC0) {
                ch_ = *buf & 0x1F;
                nch_ = 1;
              } else if ((*buf & 0xF0) == 0xE0) {
                ch_ = *buf & 0x0F;
                nch_ = 2;
              } else if ((*buf & 0xF8) == 0xF0) {
                ch_ = *buf & 0x03;
                nch_ = 3;
              } else if ((*buf & 0xFC) == 0xF8) {
                ch_ = *buf & 0x01;
                nch_ = 4;
              }
            }
        }

        break;

      case 1:

        switch (*buf) {
          case 'D':

            ++cursory;

            while (cursory == scrollbottom || cursory >= size.ws_row) {
              Scroll(false);
              --cursory;
            }

            break;

          case 'E':

            escape = 0;
            cursorx = 0;
            ++cursory;

            while (cursory == scrollbottom || cursory >= size.ws_row) {
              Scroll(false);
              --cursory;
            }

            break;

          case '[':

            escape = 2;
            memset(param, 0, sizeof(param));

            break;

          case '%':

            escape = 2;
            param[0] = -1;

            break;

          case ']':

            escape = 2;
            param[0] = -2;

            break;

          case '(':

            escape = 2;
            param[0] = -4;

            break;

          case '#':

            escape = 2;
            param[0] = -5;

            break;

          case 'M':

            if (cursorx == 0 && cursory == scrolltop)
              ReverseScroll(false);
            else if (cursory)
              --cursory;

            escape = 0;

            break;

          default:

            escape = 0;
        }

        break;

      default:

        if (param[0] == -1)
          escape = 0;
        else if (param[0] == -2) {
          /* Handle ESC ] Ps ; Pt BEL */
          if (escape == 2) {
            if (*buf >= '0' && *buf <= '9') {
              param[1] *= 10;
              param[1] += *buf - '0';
            } else
              ++escape;
          } else {
            if (*buf != '\007') {
              /* XXX: Store text */
            } else
              escape = 0;
          }
        } else if (param[0] == -4) {
          switch (*buf) {
            case '0':
              use_alt_charset_[curscreen] = true;
              break;
            case 'B':
              use_alt_charset_[curscreen] = false;
              break;
          }

          escape = 0;
        } else if (param[0] == -5) {
          escape = 0;
        } else if (escape == 2 && *buf == '?') {
          param[0] = -3;
          ++escape;
        } else if (escape == 2 && *buf == '>') {
          param[0] = -4;
          ++escape;
        } else if (*buf == ';') {
          if (escape < (int) sizeof(param) + 1)
            param[++escape - 2] = 0;
          else
            param[(sizeof(param) / sizeof(param[0])) - 1] = 0;
        } else if (*buf >= '0' && *buf <= '9') {
          param[escape - 2] *= 10;
          param[escape - 2] += *buf - '0';
        } else if (param[0] == -3) {
          if (*buf == 'h') {
            for (k = 1; k < escape - 1; ++k) {
              switch (param[k]) {
                case 1:

                  appcursor = 1;

                  break;

                case 25:

                  hide_cursor = 0;

                  break;

                case 1049:

                  if (curscreen != 1) {
                    memset(chars[1], 0,
                           size.ws_col * history_size * sizeof(wchar_t));
                    memset(attr[1], 0x07,
                           size.ws_col * history_size * sizeof(uint16_t));
                    SetScreen(1);
                  }

                  break;
              }
            }
          } else if (*buf == 'l') {
            for (k = 1; k < escape - 1; ++k) {
              switch (param[k]) {
                case 1:

                  appcursor = 0;

                  break;

                case 25:

                  hide_cursor = 1;

                  break;

                case 1049:

                  if (curscreen != 0) SetScreen(0);

                  break;
              }
            }
          }

          escape = 0;
        } else {
          switch (*buf) {
            case '@':

              if (!param[0]) param[0] = 1;

              InsertChars(param[0]);

              break;

            case 'A':

              if (!param[0]) param[0] = 1;

              cursory -= (param[0] < cursory) ? param[0] : cursory;

              break;

            case 'B':

              if (!param[0]) param[0] = 1;

              cursory =
                  (param[0] + cursory < size.ws_row) ? (param[0] + cursory)
                                                     : (size.ws_row - 1);

              break;

            case 'C':

              if (!param[0]) param[0] = 1;

              cursorx =
                  (param[0] + cursorx < size.ws_col) ? (param[0] + cursorx)
                                                     : (size.ws_col - 1);

              break;

            case 'D':

              if (!param[0]) param[0] = 1;

              cursorx -= (param[0] < cursorx) ? param[0] : cursorx;

              break;

            case 'E':

              cursorx = 0;
              ++cursory;

              while (cursory == scrollbottom || cursory >= size.ws_row) {
                Scroll(false);
                --cursory;
              }

              break;

            case 'F':

              cursorx = 0;

              if (cursory == scrolltop)
                ReverseScroll(false);
              else if (cursory)
                --cursory;

              escape = 0;

              break;

            case 'G':

              if (param[0] > 0) --param[0];

              cursorx = (param[0] < size.ws_col) ? param[0] : (size.ws_col - 1);

              break;

            case 'H':
            case 'f':

              if (param[0] > 0) --param[0];

              if (param[1] > 0) --param[1];

              cursory = (param[0] < size.ws_row) ? param[0] : (size.ws_row - 1);
              cursorx = (param[1] < size.ws_col) ? param[1] : (size.ws_col - 1);

              break;

            case 'J':

              if (param[0] == 0) {
                /* Clear from cursor to end */

                NormalizeHistoryBuffer();

                int count = size.ws_col * (size.ws_row - cursory - 1) +
                            (size.ws_col - cursorx);
                memset(&curchars[cursory * size.ws_col + cursorx], 0,
                       count * sizeof(wchar_t));
                memset16(&curattrs[cursory * size.ws_col + cursorx], curattr,
                         count * sizeof(uint16_t));
              } else if (param[0] == 1) {
                /* Clear from start to cursor */

                NormalizeHistoryBuffer();

                int count = (size.ws_col * cursory + cursorx);
                memset(curchars, 0, count * sizeof(wchar_t));
                memset16(curattrs, curattr, count * sizeof(uint16_t));
              } else if (param[0] == 2) {
                for (size_t i = 0; i < size.ws_row; ++i)
                  ClearLine((i + size.ws_row) % history_size);
              }

              break;

            case 'K': {
              size_t line_offset =
                  (*cur_scroll_line + cursory) % history_size * size.ws_col;
              size_t begin, end;

              switch (param[0]) {
                case 0:
                  /* Clear from cursor to end */
                  begin = cursorx;
                  end = size.ws_col;
                  break;
                case 1:
                  /* Clear from start to cursor */
                  begin = 0;
                  end = cursorx + 1;
                  break;
                default:
                case 2:
                  /* Clear entire line */
                  begin = 0;
                  end = size.ws_col;
              }

              for (size_t x = begin; x < end; ++x) {
                curchars[line_offset + x] = 0;
                curattrs[line_offset + x] =
                    reverse ? REVERSE(curattr) : curattr;
              }
            } break;

            case 'L':

              if (!param[0])
                param[0] = 1;
              else if (param[0] > size.ws_row)
                param[0] = size.ws_row;

              while (param[0]--)
                ReverseScroll(true);

              break;

            case 'M':

              if (!param[0])
                param[0] = 1;
              else if (param[0] > size.ws_row)
                param[0] = size.ws_row;

              while (param[0]--)
                Scroll(true);

              break;

            case 'P':

              /* Delete character at cursor */

              NormalizeHistoryBuffer();

              if (!param[0])
                param[0] = 1;
              else if (param[0] > size.ws_col)
                param[0] = size.ws_col;

              while (param[0]--) {
                memmove(&curchars[cursory * size.ws_col + cursorx],
                        &curchars[cursory * size.ws_col + cursorx + 1],
                        (size.ws_col - cursorx - 1) * sizeof(wchar_t));
                memmove(&curattrs[cursory * size.ws_col + cursorx],
                        &curattrs[cursory * size.ws_col + cursorx + 1],
                        (size.ws_col - cursorx - 1) * sizeof(uint16_t));
                curchars[(cursory + 1) * size.ws_col - 1] = 0;
                curattrs[(cursory + 1) * size.ws_col - 1] =
                    reverse ? REVERSE(curattr) : curattr;
              }

              break;

            case 'S':

              if (!param[0]) param[0] = 1;

              while (param[0]--)
                Scroll(false);

              break;

            case 'T':

              if (!param[0]) param[0] = 1;

              while (param[0]--)
                ReverseScroll(false);

              break;

            case 'X':

              if (param[0] <= 0) param[0] = 1;

              for (k = cursorx; k < cursorx + param[0] && k < size.ws_col;
                   ++k) {
                curchars[(cursory + *cur_scroll_line) % history_size *
                             size.ws_col + k] = 0;
                curattrs[(cursory + *cur_scroll_line) % history_size *
                             size.ws_col + k] =
                    reverse ? REVERSE(curattr) : curattr;
              }

              break;

            case 'd':

              if (param[0] > 0)
                --param[0];
              else
                param[0] = 0;

              cursory = (param[0] < size.ws_row) ? param[0] : (size.ws_row - 1);

              break;

            case 'h':

              for (k = 0; k < escape - 1; ++k) {
                switch (param[k]) {
                  case 4:

                    insertmode = 1;

                    break;
                }
              }

              break;

            case 'l':

              for (k = 0; k < escape - 1; ++k) {
                switch (param[k]) {
                  case 4:

                    insertmode = 0;

                    break;
                }
              }

              break;

            case 'm':

              for (k = 0; k < escape - 1; ++k) {
                switch (param[k]) {
                  case 7:

                    reverse = 1;

                    break;

                  case 27:

                    reverse = 0;

                    break;

                  case 0:

                    reverse = 0;

                  default:

                    for (size_t l = 0;
                         l < sizeof(ansi_helper) / sizeof(ansi_helper[0]);
                         ++l) {
                      if (ansi_helper[l].index == param[k]) {
                        curattr &= ansi_helper[l].and_mask;
                        curattr |= ansi_helper[l].or_mask;

                        break;
                      }
                    }

                    break;
                }
              }

              break;

            case 'r':

              if (param[0] < param[1]) {
                --param[0];

                if (param[1] > size.ws_row) param[1] = size.ws_row;

                if (param[0] < 0) param[0] = 0;

                scrolltop = param[0];
                scrollbottom = param[1];
              } else {
                scrolltop = 0;
                scrollbottom = size.ws_row;
              }

              break;

            case 's':

              savedx_ = cursorx;
              savedy_ = cursory;

              break;

            case 'u':

              cursorx = savedx_;
              cursory = savedy_;

              break;
          }

          escape = 0;
        }
    }
  }
}

void Terminal::SetScreen(int screen) {
  storedcursorx[curscreen] = cursorx;
  storedcursory[curscreen] = cursory;

  curscreen = screen;
  curchars = chars[screen];
  curattrs = attr[screen];
  cursorx = storedcursorx[screen];
  cursory = storedcursory[screen];
  cur_scroll_line = &scroll_line[screen];
}

void Terminal::InsertChars(size_t count) {
  size_t line_offset =
      (*cur_scroll_line + cursory) % history_size * size.ws_col;
  size_t k = size.ws_col;

  // TODO(mortehu): Is this right?
  while (--k > cursorx + count) {
    curchars[line_offset + k] = curchars[line_offset + k - count];
    curattrs[line_offset + k] = curattrs[line_offset + k - count];
  }

  for (; k >= static_cast<size_t>(cursorx); --k) {
    curchars[line_offset + k] = 'X';
    curattrs[line_offset + k] = curattr;
  }
}

void Terminal::AddChar(int ch) {
  // Alternate characters, from 0x41 to 0x7E, inclusive.
  static const int kAltCharset[62] = {
    0x2191, 0x2193, 0x2192, 0x2190, 0x2588, 0x259a, 0x2603, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0020, 0x25c6, 0x2592, 0x2409, 0x240c, 0x240d,
    0x240a, 0x00b0, 0x00b1, 0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514,
    0x253c, 0x23ba, 0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534,
    0x252c, 0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7,
  };

  if (use_alt_charset_[curscreen]) {
    if (ch >= 0x41 && ch <= 0x7e) ch = kAltCharset[ch - 0x41];
  }

  if (ch < 32) return;

  size_t offset =
      (*cur_scroll_line + cursory) % history_size * size.ws_col + cursorx;

  if (ch == 0x7f || ch >= 65536) {
    curchars[offset] = 0;
    curattrs[offset] = reverse ? REVERSE(curattr) : curattr;
    return;
  }

  if (!GLYPH_IsLoaded(ch)) term_LoadGlyph(ch);

  if (insertmode) terminal.InsertChars(1);

  curchars[offset] = ch;
  curattrs[offset] = reverse ? REVERSE(curattr) : curattr;
  ++cursorx;
}

void Terminal::NormalizeHistoryBuffer() {
  size_t history_buffer_size = size.ws_col * history_size;

  for (size_t i = 0; i < 2; ++i) {
    if (!scroll_line[i]) continue;

    assert(scroll_line[i] > 0);
    assert(scroll_line[0] < history_size);

    size_t buffer_offset = cur_scroll_line[i] * size.ws_col;

    std::unique_ptr<wchar_t[]> tmpchars(new wchar_t[buffer_offset]);
    std::unique_ptr<uint16_t[]> tmpattrs(new uint16_t[buffer_offset]);

    memcpy(&tmpchars[0], chars[i], sizeof(tmpchars[0]) * buffer_offset);
    memcpy(&tmpattrs[0], attr[i], sizeof(tmpattrs[0]) * buffer_offset);

    memmove(chars[i], chars[i] + buffer_offset,
            sizeof(tmpchars[0]) * (history_buffer_size - buffer_offset));
    memmove(attr[i], attr[i] + buffer_offset,
            sizeof(tmpattrs[0]) * (history_buffer_size - buffer_offset));

    memmove(chars[i] + (history_buffer_size - buffer_offset), &tmpchars[0],
            sizeof(tmpchars[0]) * buffer_offset);
    memmove(attr[i] + (history_buffer_size - buffer_offset), &tmpattrs[0],
            sizeof(tmpattrs[0]) * buffer_offset);

    scroll_line[i] = 0;
  }
}

void Terminal::ClearLine(size_t line) {
  size_t offset = line * size.ws_col;

  std::fill(curchars + offset, curchars + offset + size.ws_col, 0);
  std::fill(curattrs + offset, curattrs + offset + size.ws_col,
            reverse ? REVERSE(curattr) : curattr);
}

void Terminal::Scroll(bool fromcursor) {
  terminal.ClearSelection();

  if (!fromcursor && scrolltop == 0 && scrollbottom == size.ws_row) {
    ClearLine((*cur_scroll_line + size.ws_row) % history_size);
    *cur_scroll_line = (*cur_scroll_line + 1) % history_size;

    return;
  }

  NormalizeHistoryBuffer();

  size_t first, length;

  if (fromcursor) {
    first = cursory;
    length = (scrollbottom - cursory - 1);
  } else {
    first = scrolltop;
    length = (scrollbottom - scrolltop - 1);
  }

  memmove(curchars + first, curchars + first + size.ws_col,
          length * sizeof(wchar_t));
  memset(curchars + first + length, 0, size.ws_col * sizeof(wchar_t));

  memmove(curattrs + first, curattrs + first + size.ws_col,
          sizeof(uint16_t) * length);
  memset16(curattrs + first + length, curattr, sizeof(uint16_t) * size.ws_col);
}

void Terminal::ReverseScroll(bool fromcursor) {
  terminal.ClearSelection();

  NormalizeHistoryBuffer();

  int first, length;

  if (fromcursor) {
    first = cursory * size.ws_col;
    length = (scrollbottom - cursory - 1) * size.ws_col;
  } else {
    first = scrolltop * size.ws_col;
    length = (scrollbottom - scrolltop - 1) * size.ws_col;
  }

  memmove(curchars + first + size.ws_col, curchars + first,
          length * sizeof(wchar_t));
  memset(curchars + first, 0, size.ws_col * sizeof(wchar_t));

  memmove(curattrs + first + size.ws_col, curattrs + first,
          sizeof(uint16_t) * length);
  memset16(curattrs + first, curattr, sizeof(uint16_t) * size.ws_col);
}

bool Terminal::FindRange(RangeType range_type, int *begin, int *end) const {
  size_t history_buffer_size = size.ws_col * history_size;
  size_t offset = *cur_scroll_line * size.ws_col;

  int i, ch;

  switch (range_type) {
    case Terminal::kRangeWordOrURL:
      i = *begin;

      while (i) {
        if (!(i % size.ws_col)) break;

        ch = curchars[(offset + i - 1) % history_buffer_size];

        if (ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch)) break;

        --i;
      }

      *begin = i;

      i = *end;

      while ((i % size.ws_col) < size.ws_col) {
        ch = curchars[(offset + i) % history_buffer_size];

        if (ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch)) break;

        ++i;
      }

      *end = i;

      return *begin != *end;

    case Terminal::kRangeParenthesis: {
      int paren_level = 0;
      i = *begin;

      while (i > 0) {
        ch = curchars[(offset + i) % history_buffer_size];

        if ((!ch || ((i + 1) % size.ws_col == 0) || isspace(ch)) &&
            !paren_level) {
          ++i;

          break;
        }

        if (ch == ')') ++paren_level;

        if (ch == '(') {
          if (!paren_level) break;

          --paren_level;
        }

        --i;
      }

      *begin = i;

      if (*end > i + 1 &&
          curchars[(offset + *end - 1) % history_buffer_size] == '=')
        --*end;

      return true;
    }
  }

  return false;
}

void Terminal::ClearSelection() {
  select_begin = -1;
  select_end = -1;
}

void Terminal::UpdateSelection(Time time) {
  int i;
  unsigned int offset;

  if (select_begin == select_end) return;

  size_t history_buffer_size = size.ws_col * history_size;
  offset = *cur_scroll_line * size.ws_col;

  if (select_text) {
    free(select_text);
    select_text = 0;
  }

  if (select_begin > select_end) {
    i = select_begin;
    select_begin = select_end;
    select_end = i;
  }

  select_alloc = select_end - select_begin + 1;
  select_text = (unsigned char *)calloc(select_alloc, 1);
  select_length = 0;

  size_t last_graph = 0;
  size_t last_graph_col = 0;
  i = select_begin;

  while (i != select_end) {
    int ch = curchars[(i + offset) % history_buffer_size];
    size_t width = size.ws_col;

    if (ch == 0 || ch == 0xffff) ch = ' ';

    if (select_length + 4 > select_alloc) {
      select_alloc *= 2;
      select_text = (unsigned char *)realloc(select_text, select_alloc);
    }

    if (i > select_begin && (i % width) == 0) {
      select_length = last_graph;
      if (last_graph_col != (width - 1)) select_text[select_length++] = '\n';
      last_graph = select_length;
    }

    if (ch < 0x80) {
      select_text[select_length++] = ch;
    } else if (ch < 0x800) {
      select_text[select_length++] = 0xC0 | (ch >> 6);
      select_text[select_length++] = 0x80 | (ch & 0x3F);
    } else if (ch < 0x10000) {
      select_text[select_length++] = 0xE0 | (ch >> 12);
      select_text[select_length++] = 0x80 | ((ch >> 6) & 0x3F);
      select_text[select_length++] = 0x80 | (ch & 0x3f);
    }

    if (ch != ' ') {
      last_graph = select_length;
      last_graph_col = i % width;
    }

    ++i;
  }

  select_length = last_graph;
  select_text[select_length] = 0;

  XSetSelectionOwner(X11_display, XA_PRIMARY, X11_window, time);

  if (X11_window != XGetSelectionOwner(X11_display, XA_PRIMARY)) {
    /* We did not get the selection */

    select_begin = select_end;
    free(select_text);
    select_text = 0;
  }
}

void Terminal::SaveSession() {
  if (!session_path) return;

  if (cursorx) terminal.ProcessData((const unsigned char *)"\r\n", 2);

  int fd = open(session_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  if (fd == -1) return;

  NormalizeHistoryBuffer();

  size_t history_buffer_size = size.ws_col * history_size;

  write(fd, &size, sizeof(size));
  write(fd, &cursorx, sizeof(cursorx));
  write(fd, &cursory, sizeof(cursory));
  write(fd, chars[0], history_buffer_size * sizeof(*chars[0]));
  write(fd, attr[0], history_buffer_size * sizeof(*attr[0]));

  close(fd);
}

static void sighandler(int signal) {
  static int first = 1;

  fprintf(stderr, "Got signal %d\n", signal);

  if (first) {
    first = 0;
    terminal.SaveSession();
  }

  exit(EXIT_SUCCESS);
}

static void send_selection(XSelectionRequestEvent *request,
                           const unsigned char *text, size_t length) {
  XSelectionEvent response;
  int ret;

  response.type = SelectionNotify;
  response.send_event = True;
  response.display = X11_display;
  response.requestor = request->requestor;
  response.selection = request->selection;
  response.target = request->target;
  response.property = None;
  response.time = request->time;

  if (request->target == xa_targets) {
    const Atom targets[] = { XA_STRING, xa_utf8_string };

    XChangeProperty(X11_display, request->requestor, request->property, XA_ATOM,
                    32, PropModeReplace, (const unsigned char *)targets,
                    ARRAY_SIZE(targets));
  } else if (request->target == XA_STRING ||
             request->target == xa_utf8_string) {
    ret = XChangeProperty(X11_display, request->requestor, request->property,
                          request->target, 8, PropModeReplace, text, length);

    if (ret != BadAlloc && ret != BadAtom && ret != BadValue &&
        ret != BadWindow)
      response.property = request->property;
  } else {
    fprintf(stderr, "Unknown selection request target: %s\n",
            XGetAtomName(X11_display, request->target));
  }

  XSendEvent(request->display, request->requestor, False, NoEventMask,
             (XEvent *)&response);
}

static void paste(Atom selection, Time time) {
  XConvertSelection(X11_display, selection, xa_utf8_string, selection,
                    X11_window, time);
}

void Terminal::Write(const char *data, size_t len) {
  size_t off = 0;
  ssize_t result;

  ClearSelection();

  while (off < len) {
    result = write(fd, data + off, len - off);

    if (result < 0) {
      done = 1;

      break;
    }

    off += result;
  }
}

void X11_handle_configure(void) {
  /* Resize event -- create new buffers and copy+clip old data */

  terminal.NormalizeHistoryBuffer();

  glViewport(0, 0, X11_window_width, X11_window_height);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0f, X11_window_width, X11_window_height, 0.0f, 0.0f, 1.0f);

  terminal.Resize(X11_window_width, X11_window_height);
}

void run_command(int fd, const char *command, const char *arg) {
  char path[4096];
  int command_fd;

  sprintf(path, ".cantera/commands/%s", command);

  if (-1 == (command_fd = openat(home_fd, path, O_RDONLY))) {
    sprintf(path, PKGDATADIR "/commands/%s", command);

    if (-1 == (command_fd = openat(home_fd, path, O_RDONLY))) return;
  }

  if (!fork()) {
    char *args[3];

    if (fd != -1) dup2(fd, 1);

    args[0] = path;
    args[1] = (char *)arg;
    args[2] = 0;

    fexecve(command_fd, args, environ);

    exit(EXIT_FAILURE);
  }

  close(command_fd);
}

static void *x11_clear_thread_entry(void *arg) {
  for (;;) {
    pthread_mutex_lock(&clear_mutex);
    while (!clear)
      pthread_cond_wait(&clear_cond, &clear_mutex);
    clear = false;
    pthread_mutex_unlock(&clear_mutex);

    XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
    XFlush(X11_display);
  }

  return NULL;
}

static void x11_clear(void) {
  if (hidden) return;

  pthread_mutex_lock(&clear_mutex);
  clear = true;
  pthread_cond_signal(&clear_cond);
  pthread_mutex_unlock(&clear_mutex);
}

static void *tty_read_thread_entry(void *arg) {
  unsigned char buf[4096];
  ssize_t result;
  size_t fill = 0;
  struct pollfd pfd;

  pfd.fd = terminal.fd;
  pfd.events = POLLIN | POLLRDHUP;

  for (;;) {
    if (-1 == poll(&pfd, 1, -1)) {
      if (errno == EINTR) continue;

      break;
    }

    if (pfd.revents & POLLRDHUP) break;

    // Read until EAGAIN/EWOULDBLOCK.
    while (0 < (result = read(terminal.fd, buf + fill, sizeof(buf) - fill))) {
      fill += result;
      if (fill == sizeof(buf)) break;
    }

    if (result == -1 && errno != EAGAIN && errno != EWOULDBLOCK) break;

    if (0 != pthread_mutex_trylock(&terminal.bufferLock)) {
      // We couldn't get the lock straight away.  If the buffer is not full,
      // and we can get more data within 1 ms, go ahead and read that.
      if (fill < sizeof(buf) && 0 < poll(&pfd, 1, 1)) continue;

      // No new data available, go ahead and paint.
      pthread_mutex_lock(&terminal.bufferLock);
    }

    terminal.ProcessData(buf, fill);
    fill = 0;
    pthread_mutex_unlock(&terminal.bufferLock);

    x11_clear();
  }

  terminal.SaveSession();

  done = 1;

  x11_clear();

  return NULL;
}

void Terminal::WaitForDeadChildren(void) {
  pid_t pid;
  int status;

  while (0 < (pid = waitpid(-1, &status, WNOHANG))) {
    if (pid == terminal.pid_) {
      terminal.SaveSession();

      exit(EXIT_SUCCESS);
    }
  }
}

struct KeyInfo : std::pair<unsigned int, unsigned int> {
  typedef std::pair<unsigned int, unsigned int> super;

  KeyInfo(unsigned int symbol) : super(symbol, 0) {}

  KeyInfo(unsigned int symbol, unsigned int mask)
      : super(symbol, mask & (ControlMask | Mod1Mask | ShiftMask)) {}
};

namespace std {
template <> struct hash<KeyInfo> {
  size_t operator()(const KeyInfo &k) const {
    return (k.first << 16) | k.second;
  }
};
}

int x11_process_events() {
  std::unordered_map<KeyInfo, void(*)(XKeyEvent * event)> key_callbacks;
  KeySym prev_key_sym = 0;
  XEvent event;
  int result;

#define MAP_KEY_TO_STRING(keysym, string)                      \
  key_callbacks[keysym] = [](XKeyEvent * event) {              \
    if (event->state & Mod1Mask) terminal.WriteString("\033"); \
    terminal.WriteString((string));                            \
  };                                                           \

  MAP_KEY_TO_STRING(XK_F1, "\033OP");
  MAP_KEY_TO_STRING(XK_F2, "\033OQ");
  MAP_KEY_TO_STRING(XK_F3, "\033OR");
  MAP_KEY_TO_STRING(XK_F4, "\033OS");
  MAP_KEY_TO_STRING(XK_F5, "\033[15~");
  MAP_KEY_TO_STRING(XK_F6, "\033[17~");
  MAP_KEY_TO_STRING(XK_F7, "\033[18~");
  MAP_KEY_TO_STRING(XK_F8, "\033[19~");
  MAP_KEY_TO_STRING(XK_F9, "\033[20~");
  MAP_KEY_TO_STRING(XK_F10, "\033[21~");
  MAP_KEY_TO_STRING(XK_F11, "\033[23~");
  MAP_KEY_TO_STRING(XK_F12, "\033[24~");
  MAP_KEY_TO_STRING(XK_Insert, "\033[2~");
  MAP_KEY_TO_STRING(XK_Delete, "\033[3~");
  MAP_KEY_TO_STRING(XK_Home, terminal.appcursor ? "\033OH" : "\033[H");
  MAP_KEY_TO_STRING(XK_End, terminal.appcursor ? "\033OF" : "\033[F");
  MAP_KEY_TO_STRING(XK_Page_Up, "\033[5~");
  MAP_KEY_TO_STRING(XK_Page_Down, "\033[6~");
  MAP_KEY_TO_STRING(XK_Up, terminal.appcursor ? "\033OA" : "\033[A");
  MAP_KEY_TO_STRING(XK_Down, terminal.appcursor ? "\033OB" : "\033[B");
  MAP_KEY_TO_STRING(XK_Right, terminal.appcursor ? "\033OC" : "\033[C");
  MAP_KEY_TO_STRING(XK_Left, terminal.appcursor ? "\033OD" : "\033[D");

#undef MAP_KEY_TO_STRING

  /* Map Ctrl-Left/Right to Home/End */
  key_callbacks[KeyInfo(XK_Left, ControlMask)] = key_callbacks[XK_Home];
  key_callbacks[KeyInfo(XK_Right, ControlMask)] = key_callbacks[XK_End];

  /* Inline calculator */
  key_callbacks[KeyInfo(XK_Menu, ShiftMask)] = [](XKeyEvent * event) {
    terminal.NormalizeHistoryBuffer();

    terminal.select_end =
        terminal.cursory * terminal.size.ws_col + terminal.cursorx;

    if (terminal.select_end == 0) {
      terminal.select_begin = 0;
      terminal.select_end = 1;
    } else
      terminal.select_begin = terminal.select_end - 1;

    terminal.FindRange(Terminal::kRangeParenthesis, &terminal.select_begin,
                       &terminal.select_end);

    terminal.UpdateSelection(CurrentTime);

    XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
  };
  key_callbacks[XK_Menu] = [](XKeyEvent * event) {
    if (select_text)
      run_command(terminal.fd, "calculate", (const char *)select_text);
  };

  /* Clipboard handling */
  key_callbacks[KeyInfo(XK_C, ControlMask | ShiftMask)] =
      [](XKeyEvent * event) {
    if (select_text) {
      free(clipboard_text);
      clipboard_length = select_length;

      if (!(clipboard_text = (unsigned char *)malloc(select_length))) return;

      memcpy(clipboard_text, select_text, clipboard_length);

      XSetSelectionOwner(X11_display, xa_clipboard, X11_window, event->time);
    }
  };
  key_callbacks[KeyInfo(XK_Insert, ControlMask | ShiftMask)] =
      key_callbacks[KeyInfo(XK_V, ControlMask | ShiftMask)] =
          [](XKeyEvent * event) {
    paste(xa_clipboard, event->time);
  };
  key_callbacks[KeyInfo(XK_Insert, ShiftMask)] = [](XKeyEvent * event) {
    paste(XA_PRIMARY, event->time);
  };

  /* Suppress output from some keys */
  key_callbacks[XK_Shift_L] = key_callbacks[XK_Shift_R] =
      key_callbacks[XK_ISO_Prev_Group] = key_callbacks[XK_ISO_Next_Group] =
          [](XKeyEvent * event) {};

  while (!done) {
    XNextEvent(X11_display, &event);

    if (XFilterEvent(&event, X11_window)) continue;

    terminal.WaitForDeadChildren();

    switch (event.type) {
      case KeyPress:

        /* Filter synthetic events, to make stealthy key logging more difficult
         */
        if (event.xkey.send_event) break;

        {
          char text[32];
          Status status;
          KeySym key_sym;
          int len;
          int history_scroll_reset = 1;
          unsigned int modifier_mask = event.xkey.state;

          len = Xutf8LookupString(X11_xic, &event.xkey, text, sizeof(text) - 1,
                                  &key_sym, &status);

          if (!text[0]) len = 0;

          if (key_sym == XK_Control_L || key_sym == XK_Control_R)
            modifier_mask |= ControlMask, history_scroll_reset = 0;

          if (key_sym == XK_Alt_L || key_sym == XK_Alt_R ||
              key_sym == XK_Shift_L || key_sym == XK_Shift_R)
            history_scroll_reset = 0;

          /* Hack for keyboards with no menu key; remap two consecutive
           * taps of R-Control to Menu */
          if (key_sym == XK_Control_R && prev_key_sym == XK_Control_R) {
            key_sym = XK_Menu;
            modifier_mask &= ~ControlMask;
          }

          if ((modifier_mask & ShiftMask) && key_sym == XK_Up) {
            history_scroll_reset = 0;

            if (terminal.history_scroll < scroll_extra) {
              ++terminal.history_scroll;
              XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
            }
          } else if ((modifier_mask & ShiftMask) && key_sym == XK_Down) {
            history_scroll_reset = 0;

            if (terminal.history_scroll) {
              --terminal.history_scroll;
              XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
            }
          } else if ((modifier_mask & ShiftMask) && key_sym == XK_Page_Up) {
            history_scroll_reset = 0;

            terminal.history_scroll += terminal.size.ws_row;

            if (terminal.history_scroll > scroll_extra)
              terminal.history_scroll = scroll_extra;

            XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
          } else if ((modifier_mask & ShiftMask) && key_sym == XK_Page_Down) {
            history_scroll_reset = 0;

            if (terminal.history_scroll > terminal.size.ws_row)
              terminal.history_scroll -= terminal.size.ws_row;
            else
              terminal.history_scroll = 0;

            XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
          } else if ((modifier_mask & ShiftMask) && key_sym == XK_Home) {
            history_scroll_reset = 0;

            if (terminal.history_scroll != scroll_extra) {
              terminal.history_scroll = scroll_extra;

              XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
            }
          } else {
            auto handler = key_callbacks.find(
                KeyInfo(key_sym, modifier_mask & (ControlMask | ShiftMask)));

            if (handler != key_callbacks.end())
              handler->second(&event.xkey);
            else if (len) {
              if ((modifier_mask & Mod1Mask)) terminal.WriteString("\033");

              terminal.Write(reinterpret_cast<const char *>(text), len);
            }
          }

          if (history_scroll_reset && terminal.history_scroll) {
            terminal.history_scroll = 0;
            XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
          }

          prev_key_sym = key_sym;
        }

        break;

      case MotionNotify:

        if (event.xbutton.state & Button1Mask) {
          int x, y, new_select_end;
          unsigned int size;

          size = terminal.history_size * terminal.size.ws_col;

          x = event.xbutton.x / FONT_SpaceWidth(font);
          y = event.xbutton.y / FONT_LineHeight(font);

          new_select_end = y * terminal.size.ws_col + x;

          if (terminal.history_scroll)
            new_select_end +=
                size - (terminal.history_scroll * terminal.size.ws_col);

          if (event.xbutton.state & ControlMask)
            terminal.FindRange(Terminal::kRangeWordOrURL,
                               &terminal.select_begin, &new_select_end);

          if (new_select_end != terminal.select_end) {
            terminal.select_end = new_select_end;

            XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
          }
        }

        break;

      case ButtonPress:

        XSetInputFocus(X11_display, X11_window, RevertToPointerRoot,
                       event.xkey.time);

        switch (event.xbutton.button) {
          case 1: /* Left button */
                  {
            int x, y;
            unsigned int size;

            size = terminal.history_size * terminal.size.ws_col;

            x = event.xbutton.x / FONT_SpaceWidth(font);
            y = event.xbutton.y / FONT_LineHeight(font);

            terminal.select_begin = y * terminal.size.ws_col + x;

            if (terminal.history_scroll)
              terminal.select_begin +=
                  size - (terminal.history_scroll * terminal.size.ws_col);

            terminal.select_end = terminal.select_begin;

            if (event.xbutton.state & ControlMask)
              terminal.FindRange(Terminal::kRangeWordOrURL,
                                 &terminal.select_begin, &terminal.select_end);

            XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
          } break;

          case 2: /* Middle button */

            paste(XA_PRIMARY, event.xbutton.time);

            break;

          case 4: /* Up */

            if (terminal.history_scroll < scroll_extra) {
              ++terminal.history_scroll;
              XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
            }

            break;

          case 5: /* Down */

            if (terminal.history_scroll) {
              --terminal.history_scroll;
              XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
            }

            break;
        }

        break;

      case ButtonRelease:

        /* Left button */
        if (event.xbutton.button == 1) {
          terminal.UpdateSelection(event.xbutton.time);

          if (select_text && (event.xkey.state & Mod1Mask))
            run_command(terminal.fd, "open-url", (const char *)select_text);
        }

        break;

      case SelectionRequest: {
        XSelectionRequestEvent *request = &event.xselectionrequest;

        if (request->property == None) request->property = request->target;

        if (request->selection == XA_PRIMARY) {
          if (select_text) send_selection(request, select_text, select_length);
        } else if (request->selection == xa_clipboard) {
          if (clipboard_text)
            send_selection(request, clipboard_text, clipboard_length);
        }
      } break;

      case SelectionNotify: {
        Atom selection;
        Atom type;
        int format;
        unsigned long nitems;
        unsigned long bytes_after;
        unsigned char *prop;

        selection = event.xselection.selection;

        result = XGetWindowProperty(X11_display, X11_window, selection, 0, 0,
                                    False, AnyPropertyType, &type, &format,
                                    &nitems, &bytes_after, &prop);

        if (result != Success) break;

        XFree(prop);

        result = XGetWindowProperty(X11_display, X11_window, selection, 0,
                                    bytes_after, False, AnyPropertyType, &type,
                                    &format, &nitems, &bytes_after, &prop);

        if (result != Success) break;

        if (type != xa_utf8_string || format != 8) break;

        terminal.Write(reinterpret_cast<const char *>(prop), nitems);

        XFree(prop);
      } break;

      case SelectionClear:

        if (event.xselectionclear.selection == XA_PRIMARY)
          terminal.ClearSelection();

        break;

      case MapNotify:

        hidden = false;

        X11_handle_configure();

        break;

      case UnmapNotify:

        hidden = true;

        break;

      case ConfigureNotify: {
        /* Skip to last ConfigureNotify event */
        while (XCheckTypedWindowEvent(X11_display, X11_window, ConfigureNotify,
                                      &event))
          ; /* Do nothing */

        X11_window_width = event.xconfigure.width;
        X11_window_height = event.xconfigure.height;

        X11_handle_configure();
      } break;

      case Expose:

        /* Skip to last Expose event */
        while (XCheckTypedWindowEvent(X11_display, X11_window, Expose, &event))
          ; /* Do nothing */

        draw_gl_30(&terminal);

        break;

      case EnterNotify: {
        const XEnterWindowEvent *ewe;

        ewe = (XEnterWindowEvent *)&event;

        if (!ewe->focus || ewe->detail == NotifyInferior) break;

        /* Fall through to FocusIn */
      }

      case FocusIn:

        terminal.focused = 1;
        XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);

        break;

      case LeaveNotify: {
        const XLeaveWindowEvent *lwe;

        lwe = (XEnterWindowEvent *)&event;

        if (!lwe->focus || lwe->detail == NotifyInferior) break;

        /* Fall through to FocusOut */
      }

      case FocusOut:

        terminal.focused = 0;
        XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);

        prev_key_sym = 0;

        break;
    }
  }

  return 0;
}

int main(int argc, char **argv) {
  pthread_t tty_read_thread, x11_clear_thread;
  char *args[16];
  int i, session_fd;
  const char *home;
  char *palette_str, *token;

  setlocale(LC_ALL, "en_US.UTF-8");

  while ((i = getopt_long(argc, argv, "T:", long_options, 0)) != -1) {
    switch (i) {
      case 0:
        break;

      case '?':

        fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);

        return EXIT_FAILURE;
    }
  }

  if (print_help) {
    printf("Usage: %s [OPTION]...\n"
           "\n"
           "      --help     display this help and exit\n"
           "      --version  display version information\n"
           "\n"
           "Report bugs to <morten.hustveit@gmail.com>\n",
           argv[0]);

    return EXIT_SUCCESS;
  }

  if (print_version) {
    fprintf(stdout, "%s\n", PACKAGE_STRING);

    return EXIT_SUCCESS;
  }

  session_path = getenv("SESSION_PATH");

  if (session_path) unsetenv("SESSION_PATH");

  if (!(home = getenv("HOME")))
    errx(EXIT_FAILURE, "HOME environment variable missing");

  if (-1 == (home_fd = open(home, O_RDONLY)))
    err(EXIT_FAILURE, "Failed to open HOME directory");

  mkdirat(home_fd, ".cantera", 0777);
  mkdirat(home_fd, ".cantera/commands", 0777);

  config.reset(tree_load_cfg(".cantera/config"));

  palette_str = strdup(tree_get_string_default(
      config.get(), "terminal.palette",
      "000000 1818c2 18c218 18c2c2 c21818 c218c2 c2c218 c2c2c2 686868 7474ff "
      "54ff54 54ffff ff5454 ff54ff ffff54 ffffff"));

  for (i = 0, token = strtok(palette_str, " "); i < 16 && token;
       ++i, token = strtok(0, " ")) {
    palette[i] = 0xff000000 | strtol(token, NULL, 16);
  }

  scroll_extra =
      tree_get_integer_default(config.get(), "terminal.history-size", 1000);
  font_name =
      tree_get_string_default(config.get(), "terminal.font", "Andale Mono");
  font_size = tree_get_integer_default(config.get(), "terminal.font-size", 12);
  font_weight =
      tree_get_integer_default(config.get(), "terminal.font-weight", 200);

  signal(SIGTERM, sighandler);
  signal(SIGIO, sighandler);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

  setenv("TERM", "xterm", 1);

  if (session_path) {
    session_fd = open(session_path, O_RDONLY);

    if (session_fd != -1) {
      struct winsize ws;

      if (sizeof(ws) == read(session_fd, &ws, sizeof(ws))) {
        X11_window_width = ws.ws_xpixel;
        X11_window_height = ws.ws_ypixel;
      }
    }
  } else
    session_fd = -1;

  X11_Setup();

  FONT_Init();
  GLYPH_Init();

  if (!(font = FONT_Load(font_name, font_size, font_weight)))
    errx(EXIT_FAILURE, "Failed to load font `%s' of size %u, weight %u",
         font_name, font_size, font_weight);

  /* Preload the most important glyphs, which will be uploaded to OpenGL in a
   * single batch */

  /* ASCII */
  for (i = ' '; i <= '~'; ++i)
    term_LoadGlyph(i);

  /* ISO-8859-1 */
  for (i = 0xa1; i <= 0xff; ++i)
    term_LoadGlyph(i);

  if (optind < argc) {
    if (argc - optind + 1 > (int) ARRAY_SIZE(args))
      errx(EXIT_FAILURE, "Too many arguments");

    for (i = optind; i < argc; ++i)
      args[i - optind] = argv[i];

    args[i - optind] = 0;
  } else {
    args[0] = (char *)"/bin/bash";
    args[1] = 0;
  }

  terminal.Init(args, X11_window_width, X11_window_height);

  X11_handle_configure();

  init_gl_30();

  if (session_fd != -1) {
    size_t size;

    size = terminal.size.ws_col * terminal.history_size;

    read(session_fd, &terminal.cursorx, sizeof(terminal.cursorx));
    read(session_fd, &terminal.cursory, sizeof(terminal.cursory));

    if (terminal.cursorx >= terminal.size.ws_col ||
        terminal.cursory >= terminal.size.ws_row || terminal.cursorx < 0 ||
        terminal.cursory < 0) {
      terminal.cursorx = 0;
      terminal.cursory = 0;
    } else {
      read(session_fd, terminal.chars[0], size * sizeof(*terminal.chars[0]));
      read(session_fd, terminal.attr[0], size * sizeof(*terminal.attr[0]));

      if (terminal.cursory >= terminal.size.ws_row)
        terminal.cursory = terminal.size.ws_row - 1;
      terminal.cursorx = 0;
    }

    close(session_fd);
    unlink(session_path);
  }

  pthread_create(&tty_read_thread, 0, tty_read_thread_entry, 0);
  pthread_detach(tty_read_thread);

  pthread_create(&x11_clear_thread, 0, x11_clear_thread_entry, 0);
  pthread_detach(x11_clear_thread);

  if (-1 == x11_process_events()) return EXIT_FAILURE;

  return EXIT_SUCCESS;
}

// vim: ts=2 sw=2 et sts=2
