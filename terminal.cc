#include "terminal.h"

#include <assert.h>
#include <ctype.h>
#include <memory>
#include <stdint.h>

#include <fcntl.h>
#include <unistd.h>

namespace {

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

}  // namespace

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
      savedy_() {}

void Terminal::Init(unsigned int width, unsigned int height,
                    unsigned int space_width, unsigned int line_height,
                    size_t scroll_extra) {
  size_.ws_col = width / space_width;
  size_.ws_row = height / line_height;
  size_.ws_xpixel = width;
  size_.ws_ypixel = height;

  history_size = size_.ws_row + scroll_extra;

  chars[0].reset(new wchar_t[size_.ws_col * history_size]);
  chars[1].reset(new wchar_t[size_.ws_col * history_size]);
  attr[0].reset(new uint16_t[size_.ws_col * history_size]);
  attr[1].reset(new uint16_t[size_.ws_col * history_size]);

  curattr = 0x07;
  scrollbottom = size_.ws_row;
  std::fill(&chars[0][0], &chars[0][size_.ws_col * history_size], ' ');
  std::fill(&attr[0][0], &attr[0][size_.ws_col * history_size],
            EffectiveAttribute());
  scroll_line[0] = 0;
  scroll_line[1] = 0;

  ClearSelection();

  SetScreen(0);
}

void Terminal::Resize(unsigned int width, unsigned int height,
                      unsigned int space_width, unsigned int line_height) {
  if (width == size_.ws_xpixel && height == size_.ws_ypixel) return;

  NormalizeHistoryBuffer();

  int cols = std::max(width / space_width, 1U);
  int rows = std::max(height / line_height, 1U);

  int oldcols = size_.ws_col;
  int oldrows = size_.ws_row;

  size_.ws_xpixel = width;
  size_.ws_ypixel = height;
  size_.ws_col = cols;
  size_.ws_row = rows;
  history_size += rows - oldrows;

  if (cols != oldcols || rows != oldrows) {
    std::unique_ptr<wchar_t[]> oldchars[2] = { std::move(chars[0]),
                                               std::move(chars[1]) };
    std::unique_ptr<uint16_t[]> oldattr[2] = { std::move(attr[0]),
                                               std::move(attr[1]) };

    chars[0].reset(new wchar_t[size_.ws_col * history_size]);
    chars[1].reset(new wchar_t[size_.ws_col * history_size]);
    attr[0].reset(new uint16_t[size_.ws_col * history_size]);
    attr[1].reset(new uint16_t[size_.ws_col * history_size]);

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

    curchars = chars[curscreen].get();
    curattrs = attr[curscreen].get();

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
  }
}

void Terminal::ProcessData(const void *buf, size_t count) {
  int k;

  const unsigned char *begin = reinterpret_cast<const unsigned char *>(buf);
  const unsigned char *end = begin + count;

  // TODO(mortehu): Make sure cursor does not leave screen.
  while (cursory >= size_.ws_row) {
    Scroll(false);
    --cursory;
  }

  // Redundant, optimized character processing code for the typical case.
  if (!escape && !insertmode && !nch_) {
    uint16_t attr = EffectiveAttribute();
    size_t offset =
        (*cur_scroll_line + cursory) % history_size * size_.ws_col + cursorx;

    for (; begin != end; ++begin) {
      if (*begin >= ' ' && *begin <= '~') {
        if (cursorx == size_.ws_col) {
          if (++cursory >= size_.ws_row) {
            Scroll(false);
            --cursory;
          }

          cursorx = 0;

          offset = (*cur_scroll_line + cursory) % history_size * size_.ws_col;
        }

        curchars[offset] = *begin;
        curattrs[offset] = attr;
        ++cursorx;
        ++offset;
      } else if (*begin == '\r') {
        cursorx = 0;
        offset = (*cur_scroll_line + cursory) % history_size * size_.ws_col;
      } else if (*begin == '\n') {
        ++cursory;

        if (cursory == scrollbottom || cursory >= size_.ws_row) {
          Scroll(false);
          --cursory;
        }

        offset = (*cur_scroll_line + cursory) % history_size * size_.ws_col +
                 cursorx;
      } else {
        break;
      }
    }
  }

  for (; begin != end; ++begin) {
    switch (escape) {
      case 0:

        switch (*begin) {
          case '\033':

            escape = 1;
            memset(param, 0, sizeof(param));

            break;

          case '\b':

            if (cursorx > 0) --cursorx;

            break;

          case '\t':

            if (cursorx < size_.ws_col - 8)
              cursorx = (cursorx + 8) & ~7;
            else
              cursorx = size_.ws_col - 1;

            break;

          case '\n':

            ++cursory;

            while (cursory == scrollbottom || cursory >= size_.ws_row) {
              Scroll(false);
              --cursory;
            }

            break;

          case '\r':

            cursorx = 0;

            break;

          case '\177':

            if (cursory < size_.ws_row)
              curchars[(*cur_scroll_line + cursory) % history_size *
                           size_.ws_col + cursorx] = 0;

            break;

          case('O' & 0x3F) : /* ^O = default character set */

            break;

          case('N' & 0x3F) : /* ^N = alternate character set */

            break;

          default:

            assert(cursorx >= 0 && cursorx <= size_.ws_col);
            assert(cursory >= 0 && cursory < size_.ws_row);

            if (cursorx == size_.ws_col) {
              ++cursory;
              cursorx = 0;
            }

            while (cursory >= size_.ws_row) {
              Scroll(false);
              --cursory;
            }

            if (nch_) {
              if ((*begin & 0xC0) != 0x80) {
                nch_ = 0;
                AddChar(*begin);
              } else {
                ch_ <<= 6;
                ch_ |= *begin & 0x3F;

                if (0 == --nch_) {
                  AddChar(ch_);
                }
              }
            } else {
              if ((*begin & 0x80) == 0) {
                AddChar(*begin);
              } else if ((*begin & 0xE0) == 0xC0) {
                ch_ = *begin & 0x1F;
                nch_ = 1;
              } else if ((*begin & 0xF0) == 0xE0) {
                ch_ = *begin & 0x0F;
                nch_ = 2;
              } else if ((*begin & 0xF8) == 0xF0) {
                ch_ = *begin & 0x03;
                nch_ = 3;
              } else if ((*begin & 0xFC) == 0xF8) {
                ch_ = *begin & 0x01;
                nch_ = 4;
              }
            }
        }

        break;

      case 1:

        switch (*begin) {
          case 'D':

            ++cursory;

            while (cursory == scrollbottom || cursory >= size_.ws_row) {
              Scroll(false);
              --cursory;
            }

            break;

          case 'E':

            escape = 0;
            cursorx = 0;
            ++cursory;

            while (cursory == scrollbottom || cursory >= size_.ws_row) {
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
            if (*begin >= '0' && *begin <= '9') {
              param[1] *= 10;
              param[1] += *begin - '0';
            } else
              ++escape;
          } else {
            if (*begin != '\007') {
              /* XXX: Store text */
            } else
              escape = 0;
          }
        } else if (param[0] == -4) {
          switch (*begin) {
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
        } else if (escape == 2 && *begin == '?') {
          param[0] = -3;
          ++escape;
        } else if (escape == 2 && *begin == '>') {
          param[0] = -4;
          ++escape;
        } else if (*begin == ';') {
          if (escape < (int) sizeof(param) + 1)
            param[++escape - 2] = 0;
          else
            param[(sizeof(param) / sizeof(param[0])) - 1] = 0;
        } else if (*begin >= '0' && *begin <= '9') {
          param[escape - 2] *= 10;
          param[escape - 2] += *begin - '0';
        } else if (param[0] == -3) {
          if (*begin == 'h') {
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
                    std::fill(&chars[1][0],
                              &chars[1][size_.ws_col * history_size], 0);
                    std::fill(&attr[1][0],
                              &attr[1][size_.ws_col * history_size], 0x07);
                    SetScreen(1);
                  }

                  break;
              }
            }
          } else if (*begin == 'l') {
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
          switch (*begin) {
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
                  (param[0] + cursory < size_.ws_row) ? (param[0] + cursory)
                                                      : (size_.ws_row - 1);

              break;

            case 'C':

              if (!param[0]) param[0] = 1;

              cursorx =
                  (param[0] + cursorx < size_.ws_col) ? (param[0] + cursorx)
                                                      : (size_.ws_col - 1);

              break;

            case 'D':

              if (!param[0]) param[0] = 1;

              cursorx -= (param[0] < cursorx) ? param[0] : cursorx;

              break;

            case 'E':

              cursorx = 0;
              ++cursory;

              while (cursory == scrollbottom || cursory >= size_.ws_row) {
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

              cursorx =
                  (param[0] < size_.ws_col) ? param[0] : (size_.ws_col - 1);

              break;

            case 'H':
            case 'f':

              if (param[0] > 0) --param[0];

              if (param[1] > 0) --param[1];

              cursory =
                  (param[0] < size_.ws_row) ? param[0] : (size_.ws_row - 1);
              cursorx =
                  (param[1] < size_.ws_col) ? param[1] : (size_.ws_col - 1);

              break;

            case 'J':

              if (param[0] == 0) {
                /* Clear from cursor to end */

                NormalizeHistoryBuffer();

                int count = size_.ws_col * (size_.ws_row - cursory - 1) +
                            (size_.ws_col - cursorx);
                memset(&curchars[cursory * size_.ws_col + cursorx], 0,
                       count * sizeof(wchar_t));
                std::fill(&curattrs[cursory * size_.ws_col + cursorx],
                          &curattrs[cursory * size_.ws_col + cursorx + count],
                          EffectiveAttribute());
              } else if (param[0] == 1) {
                /* Clear from start to cursor */

                NormalizeHistoryBuffer();

                int count = (size_.ws_col * cursory + cursorx);
                memset(curchars, 0, count * sizeof(wchar_t));
                std::fill(curchars, curchars + count, EffectiveAttribute());
              } else if (param[0] == 2) {
                for (size_t i = 0; i < size_.ws_row; ++i)
                  ClearLine((i + size_.ws_row) % history_size);
              }

              break;

            case 'K': {
              size_t line_offset =
                  (*cur_scroll_line + cursory) % history_size * size_.ws_col;
              size_t begin, end;

              switch (param[0]) {
                case 0:
                  /* Clear from cursor to end */
                  begin = cursorx;
                  end = size_.ws_col;
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
                  end = size_.ws_col;
              }

              uint16_t attr = EffectiveAttribute();

              for (size_t x = begin; x < end; ++x) {
                curchars[line_offset + x] = 0;
                curattrs[line_offset + x] = attr;
              }
            } break;

            case 'L':

              if (!param[0])
                param[0] = 1;
              else if (param[0] > size_.ws_row)
                param[0] = size_.ws_row;

              while (param[0]--)
                ReverseScroll(true);

              break;

            case 'M':

              if (!param[0])
                param[0] = 1;
              else if (param[0] > size_.ws_row)
                param[0] = size_.ws_row;

              while (param[0]--)
                Scroll(true);

              break;

            case 'P': {

              /* Delete character at cursor */

              NormalizeHistoryBuffer();

              if (!param[0])
                param[0] = 1;
              if (cursorx + param[0] > size_.ws_col)
                param[0] = size_.ws_col - cursorx;

              std::copy(&curchars[cursory * size_.ws_col + cursorx + param[0]],
                        &curchars[(cursory + 1) * size_.ws_col],
                        &curchars[cursory * size_.ws_col + cursorx]);
              std::copy(&curattrs[cursory * size_.ws_col + cursorx + param[0]],
                        &curattrs[(cursory + 1) * size_.ws_col],
                        &curattrs[cursory * size_.ws_col + cursorx]);

              std::fill(&curchars[(cursory + 1) * size_.ws_col - param[0]],
                        &curchars[(cursory + 1) * size_.ws_col], ' ');
              std::fill(&curattrs[(cursory + 1) * size_.ws_col - param[0]],
                        &curattrs[(cursory + 1) * size_.ws_col],
                        EffectiveAttribute());
            } break;

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

            case 'X': {

              if (param[0] <= 0) param[0] = 1;

              uint16_t attr = EffectiveAttribute();

              for (k = cursorx; k < cursorx + param[0] && k < size_.ws_col;
                   ++k) {
                curchars[(cursory + *cur_scroll_line) % history_size *
                             size_.ws_col + k] = 0;
                curattrs[(cursory + *cur_scroll_line) % history_size *
                             size_.ws_col + k] = attr;
              }

            } break;

            case 'd':

              if (param[0] > 0)
                --param[0];
              else
                param[0] = 0;

              cursory =
                  (param[0] < size_.ws_row) ? param[0] : (size_.ws_row - 1);

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

                if (param[1] > size_.ws_row) param[1] = size_.ws_row;

                if (param[0] < 0) param[0] = 0;

                scrolltop = param[0];
                scrollbottom = param[1];
              } else {
                scrolltop = 0;
                scrollbottom = size_.ws_row;
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

void Terminal::GetState(State *state) const {
  state->width = size_.ws_col;
  state->height = size_.ws_row;
  state->chars.reset(new wchar_t[size_.ws_col * size_.ws_row]);
  state->attrs.reset(new uint16_t[size_.ws_col * size_.ws_row]);

  for (size_t row = 0,
              offset = (history_size - history_scroll + *cur_scroll_line) *
                       size_.ws_col;
       row < size_.ws_row; ++row, offset += size_.ws_col) {
    offset %= history_size * size_.ws_col;
    std::copy(&curchars[offset], &curchars[offset + size_.ws_col],
              &state->chars[row * size_.ws_col]);
    std::copy(&curattrs[offset], &curattrs[offset + size_.ws_col],
              &state->attrs[row * size_.ws_col]);
  }

  state->cursor_x = cursorx;
  state->cursor_y = cursory + history_scroll;

  size_t selbegin, selend;
  if (select_begin < select_end) {
    selbegin = select_begin;
    selend = select_end;
  } else {
    selbegin = select_end;
    selend = select_begin;
  }

  state->selection_begin =
      (selbegin + history_scroll * size_.ws_col) % history_size;
  state->selection_end =
      (selend + history_scroll * size_.ws_col) % history_size;

  state->cursor_hidden = hide_cursor;
  state->focused = focused;
}

void Terminal::SetScreen(int screen) {
  storedcursorx[curscreen] = cursorx;
  storedcursory[curscreen] = cursory;

  curscreen = screen;
  curchars = chars[screen].get();
  curattrs = attr[screen].get();
  cursorx = storedcursorx[screen];
  cursory = storedcursory[screen];
  cur_scroll_line = &scroll_line[screen];
}

void Terminal::InsertChars(size_t count) {
  size_t line_offset =
      (*cur_scroll_line + cursory) % history_size * size_.ws_col;
  size_t k = size_.ws_col;

  while (k > cursorx + count) {
    --k;
    curchars[line_offset + k] = curchars[line_offset + k - count];
    curattrs[line_offset + k] = curattrs[line_offset + k - count];
  }

  uint16_t attr = EffectiveAttribute();

  while (k-- > static_cast<size_t>(cursorx)) {
    curchars[line_offset + k] = ' ';
    curattrs[line_offset + k] = attr;
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
      (*cur_scroll_line + cursory) % history_size * size_.ws_col + cursorx;

  if (ch == 0x7f || ch >= 65536) {
    curchars[offset] = 0;
    curattrs[offset] = EffectiveAttribute();
    return;
  }

  if (insertmode) InsertChars(1);

  curchars[offset] = ch;
  curattrs[offset] = EffectiveAttribute();
  ++cursorx;
}

void Terminal::NormalizeHistoryBuffer() {
  size_t history_buffer_size = size_.ws_col * history_size;

  for (size_t i = 0; i < 2; ++i) {
    if (!scroll_line[i]) continue;

    assert(scroll_line[i] > 0);
    assert(scroll_line[0] < history_size);

    size_t buffer_offset = cur_scroll_line[i] * size_.ws_col;

    std::unique_ptr<wchar_t[]> tmpchars(new wchar_t[buffer_offset]);
    std::unique_ptr<uint16_t[]> tmpattrs(new uint16_t[buffer_offset]);

    memcpy(&tmpchars[0], &chars[i][0], sizeof(tmpchars[0]) * buffer_offset);
    memcpy(&tmpattrs[0], &attr[i][0], sizeof(tmpattrs[0]) * buffer_offset);

    memmove(&chars[i][0], &chars[i][buffer_offset],
            sizeof(tmpchars[0]) * (history_buffer_size - buffer_offset));
    memmove(&attr[i][0], &attr[i][buffer_offset],
            sizeof(tmpattrs[0]) * (history_buffer_size - buffer_offset));

    memcpy(&chars[i][history_buffer_size - buffer_offset], &tmpchars[0],
           sizeof(tmpchars[0]) * buffer_offset);
    memcpy(&attr[i][history_buffer_size - buffer_offset], &tmpattrs[0],
           sizeof(tmpattrs[0]) * buffer_offset);

    scroll_line[i] = 0;
  }
}

void Terminal::ClearLine(size_t line) {
  size_t offset = line * size_.ws_col;

  std::fill(curchars + offset, curchars + offset + size_.ws_col, 0);
  std::fill(curattrs + offset, curattrs + offset + size_.ws_col,
            EffectiveAttribute());
}

void Terminal::Scroll(bool fromcursor) {
  ClearSelection();

  if (!fromcursor && scrolltop == 0 && scrollbottom == size_.ws_row) {
    ClearLine((*cur_scroll_line + size_.ws_row) % history_size);
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

  memmove(curchars + first, curchars + first + size_.ws_col,
          length * sizeof(wchar_t));
  memset(curchars + first + length, 0, size_.ws_col * sizeof(wchar_t));

  memmove(curattrs + first, curattrs + first + size_.ws_col,
          sizeof(uint16_t) * length);
  std::fill(curattrs + first + length, curattrs + first + length + size_.ws_col,
            EffectiveAttribute());
}

void Terminal::ReverseScroll(bool fromcursor) {
  ClearSelection();

  NormalizeHistoryBuffer();

  int first, length;

  if (fromcursor) {
    first = cursory * size_.ws_col;
    length = (scrollbottom - cursory - 1) * size_.ws_col;
  } else {
    first = scrolltop * size_.ws_col;
    length = (scrollbottom - scrolltop - 1) * size_.ws_col;
  }

  memmove(curchars + first + size_.ws_col, curchars + first,
          length * sizeof(wchar_t));
  memset(curchars + first, 0, size_.ws_col * sizeof(wchar_t));

  memmove(curattrs + first + size_.ws_col, curattrs + first,
          sizeof(uint16_t) * length);
  std::fill(curattrs + first, curattrs + first + size_.ws_col,
            EffectiveAttribute());
}

void Terminal::Select(RangeType range_type) {
  NormalizeHistoryBuffer();

  select_end = cursory * size_.ws_col + cursorx;

  if (select_end == 0) {
    select_begin = 0;
    select_end = 1;
  } else
    select_begin = select_end - 1;

  FindRange(range_type, &select_begin, &select_end);
}

bool Terminal::FindRange(RangeType range_type, int *begin, int *end) const {
  size_t history_buffer_size = size_.ws_col * history_size;
  size_t offset = *cur_scroll_line * size_.ws_col;

  int i, ch;

  switch (range_type) {
    case Terminal::kRangeWordOrURL:
      i = *begin;

      while (i) {
        if (!(i % size_.ws_col)) break;

        ch = curchars[(offset + i - 1) % history_buffer_size];

        if (ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch)) break;

        --i;
      }

      *begin = i;

      i = *end;

      while ((i % size_.ws_col) < size_.ws_col) {
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

        if ((!ch || ((i + 1) % size_.ws_col == 0) || isspace(ch)) &&
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

std::string Terminal::GetSelection() {
  int i;
  unsigned int offset;

  if (select_begin == select_end) return std::string();

  size_t history_buffer_size = size_.ws_col * history_size;
  offset = *cur_scroll_line * size_.ws_col;

  if (select_begin > select_end) {
    i = select_begin;
    select_begin = select_end;
    select_end = i;
  }

  size_t last_graph = 0;
  size_t last_graph_col = 0;
  i = select_begin;

  std::string result;

  while (i != select_end) {
    int ch = curchars[(i + offset) % history_buffer_size];
    size_t width = size_.ws_col;

    if (ch == 0 || ch == 0xffff) ch = ' ';

    if (i > select_begin && (i % width) == 0) {
      result.resize(last_graph);
      if (last_graph_col != (width - 1)) result.push_back('\n');
      last_graph = result.size();
    }

    if (ch < 0x80) {
      result.push_back(ch);
    } else if (ch < 0x800) {
      result.push_back(0xC0 | (ch >> 6));
      result.push_back(0x80 | (ch & 0x3F));
    } else if (ch < 0x10000) {
      result.push_back(0xE0 | (ch >> 12));
      result.push_back(0x80 | ((ch >> 6) & 0x3F));
      result.push_back(0x80 | (ch & 0x3f));
    }

    if (ch != ' ') {
      last_graph = result.size();
      last_graph_col = i % width;
    }

    ++i;
  }

  if (last_graph < result.size()) result.resize(last_graph);

  return result;
}

void Terminal::SaveSession(const char *session_path) {
  if (cursorx) ProcessData((const unsigned char *)"\r\n", 2);

  int session_fd = open(session_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  if (session_fd == -1) return;

  NormalizeHistoryBuffer();

  size_t history_buffer_size = size_.ws_col * history_size;

  write(session_fd, &size_, sizeof(size_));
  write(session_fd, &cursorx, sizeof(cursorx));
  write(session_fd, &cursory, sizeof(cursory));
  write(session_fd, chars[0].get(), history_buffer_size * sizeof(chars[0][0]));
  write(session_fd, attr[0].get(), history_buffer_size * sizeof(attr[0][0]));

  close(session_fd);
}
