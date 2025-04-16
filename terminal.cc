#include "terminal.h"

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <algorithm>
#include <memory>

#include <fcntl.h>
#include <unistd.h>

namespace {

const struct {
  int index;
  uint16_t and_mask;
  uint16_t or_mask;
} kANSIHelper[] = {
    {0, 0, ATTR_DEFAULT},
    {1, 0xffff ^ ATTR_BOLD, ATTR_BOLD},
    {2, 0xffff ^ ATTR_BOLD, 0},
    {3, 0xffff ^ ATTR_STANDOUT, ATTR_STANDOUT},
    {4, 0xffff ^ ATTR_UNDERLINE, ATTR_UNDERLINE},
    {5, 0xffff ^ ATTR_BLINK, ATTR_BLINK}, /* 7 = reverse video */
    {8, 0, 0},
    {22, (uint16_t)(~ATTR_BOLD & ~ATTR_STANDOUT & ~ATTR_UNDERLINE), 0},
    {23, 0xffff ^ ATTR_STANDOUT, 0},
    {24, 0xffff ^ ATTR_UNDERLINE, 0},
    {25, 0xffff ^ ATTR_BLINK, 0}, /* 27 = no reverse */
    {30, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_BLACK)},
    {31, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_RED)},
    {32, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_GREEN)},
    {33, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_YELLOW)},
    {34, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_BLUE)},
    {35, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_MAGENTA)},
    {36, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_CYAN)},
    {37, 0xffff ^ FG(ATTR_WHITE), FG(ATTR_WHITE)},
    {39, 0xffff ^ FG(ATTR_WHITE), FG_DEFAULT},
    {40, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_BLACK)},
    {41, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_RED)},
    {42, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_GREEN)},
    {43, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_YELLOW)},
    {44, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_BLUE)},
    {45, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_MAGENTA)},
    {46, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_CYAN)},
    {47, 0xffff ^ BG(ATTR_WHITE), BG(ATTR_WHITE)},
    {49, 0xffff ^ BG(ATTR_WHITE), BG_DEFAULT}};

static const Terminal::Attr kDefaultAttr(Terminal::Color(127, 127, 127),
                                         Terminal::Color(0, 0, 0));

static const Terminal::Color kDefaultColors[256] = {
    0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080,
    0xc0c0c0, 0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff,
    0x00ffff, 0xffffff, 0x000000, 0x00005f, 0x000087, 0x0000af, 0x0000d7,
    0x0000ff, 0x005f00, 0x005f5f, 0x005f87, 0x005faf, 0x005fd7, 0x005fff,
    0x008700, 0x00875f, 0x008787, 0x0087af, 0x0087d7, 0x0087ff, 0x00af00,
    0x00af5f, 0x00af87, 0x00afaf, 0x00afd7, 0x00afff, 0x00d700, 0x00d75f,
    0x00d787, 0x00d7af, 0x00d7d7, 0x00d7ff, 0x00ff00, 0x00ff5f, 0x00ff87,
    0x00ffaf, 0x00ffd7, 0x00ffff, 0x5f0000, 0x5f005f, 0x5f0087, 0x5f00af,
    0x5f00d7, 0x5f00ff, 0x5f5f00, 0x5f5f5f, 0x5f5f87, 0x5f5faf, 0x5f5fd7,
    0x5f5fff, 0x5f8700, 0x5f875f, 0x5f8787, 0x5f87af, 0x5f87d7, 0x5f87ff,
    0x5faf00, 0x5faf5f, 0x5faf87, 0x5fafaf, 0x5fafd7, 0x5fafff, 0x5fd700,
    0x5fd75f, 0x5fd787, 0x5fd7af, 0x5fd7d7, 0x5fd7ff, 0x5fff00, 0x5fff5f,
    0x5fff87, 0x5fffaf, 0x5fffd7, 0x5fffff, 0x870000, 0x87005f, 0x870087,
    0x8700af, 0x8700d7, 0x8700ff, 0x875f00, 0x875f5f, 0x875f87, 0x875faf,
    0x875fd7, 0x875fff, 0x878700, 0x87875f, 0x878787, 0x8787af, 0x8787d7,
    0x8787ff, 0x87af00, 0x87af5f, 0x87af87, 0x87afaf, 0x87afd7, 0x87afff,
    0x87d700, 0x87d75f, 0x87d787, 0x87d7af, 0x87d7d7, 0x87d7ff, 0x87ff00,
    0x87ff5f, 0x87ff87, 0x87ffaf, 0x87ffd7, 0x87ffff, 0xaf0000, 0xaf005f,
    0xaf0087, 0xaf00af, 0xaf00d7, 0xaf00ff, 0xaf5f00, 0xaf5f5f, 0xaf5f87,
    0xaf5faf, 0xaf5fd7, 0xaf5fff, 0xaf8700, 0xaf875f, 0xaf8787, 0xaf87af,
    0xaf87d7, 0xaf87ff, 0xafaf00, 0xafaf5f, 0xafaf87, 0xafafaf, 0xafafd7,
    0xafafff, 0xafd700, 0xafd75f, 0xafd787, 0xafd7af, 0xafd7d7, 0xafd7ff,
    0xafff00, 0xafff5f, 0xafff87, 0xafffaf, 0xafffd7, 0xafffff, 0xd70000,
    0xd7005f, 0xd70087, 0xd700af, 0xd700d7, 0xd700ff, 0xd75f00, 0xd75f5f,
    0xd75f87, 0xd75faf, 0xd75fd7, 0xd75fff, 0xd78700, 0xd7875f, 0xd78787,
    0xd787af, 0xd787d7, 0xd787ff, 0xd7af00, 0xd7af5f, 0xd7af87, 0xd7afaf,
    0xd7afd7, 0xd7afff, 0xd7d700, 0xd7d75f, 0xd7d787, 0xd7d7af, 0xd7d7d7,
    0xd7d7ff, 0xd7ff00, 0xd7ff5f, 0xd7ff87, 0xd7ffaf, 0xd7ffd7, 0xd7ffff,
    0xff0000, 0xff005f, 0xff0087, 0xff00af, 0xff00d7, 0xff00ff, 0xff5f00,
    0xff5f5f, 0xff5f87, 0xff5faf, 0xff5fd7, 0xff5fff, 0xff8700, 0xff875f,
    0xff8787, 0xff87af, 0xff87d7, 0xff87ff, 0xffaf00, 0xffaf5f, 0xffaf87,
    0xffafaf, 0xffafd7, 0xffafff, 0xffd700, 0xffd75f, 0xffd787, 0xffd7af,
    0xffd7d7, 0xffd7ff, 0xffff00, 0xffff5f, 0xffff87, 0xffffaf, 0xffffd7,
    0xffffff, 0x080808, 0x121212, 0x1c1c1c, 0x262626, 0x303030, 0x3a3a3a,
    0x444444, 0x4e4e4e, 0x585858, 0x606060, 0x666666, 0x767676, 0x808080,
    0x8a8a8a, 0x949494, 0x9e9e9e, 0xa8a8a8, 0xb2b2b2, 0xbcbcbc, 0xc6c6c6,
    0xd0d0d0, 0xdadada, 0xe4e4e4, 0xeeeeee};

}  // namespace

Terminal::Terminal(std::function<void(const void*, size_t)>&& write_function)
    : reverse(),
      history_size(),
      scrolltop(),
      scrollbottom(),
      escape(),
      param(),
      appcursor(),
      hide_cursor(),
      insertmode(),
      select_begin(-1),
      select_end(-1),
      focused(),
      history_scroll(),
      write_function_(std::move(write_function)),
      nch_(),
      savedx_(),
      savedy_() {}

void Terminal::Init(unsigned int width, unsigned int height,
                    unsigned int space_width, unsigned int line_height,
                    size_t scroll_extra) {
  ansi_attribute_ = ATTR_WHITE;
  attribute_.fg = ansi_colors_[7];
  attribute_.bg = ansi_colors_[0];
  attribute_.extra = 0;

  size_.ws_col = width / space_width;
  size_.ws_row = height / line_height;
  size_.ws_xpixel = width;
  size_.ws_ypixel = height;

  history_size = size_.ws_row + scroll_extra;

  for (size_t i = 0; i < 2; ++i) {
    size_t n = size_.ws_col * history_size;
    screens_[i].chars.reset(new CharacterType[n]);
    screens_[i].attr .reset(new Attr[n]);

    std::fill(&screens_[i].chars[0],
              &screens_[i].chars[n], L' ');
    std::fill(&screens_[i].attr[0],
              &screens_[i].attr[n], EffectiveAttribute());
  }

  scrollbottom = size_.ws_row;

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
    for (size_t i = 0; i < 2; ++i) {
      std::unique_ptr<wchar_t[]> oldchars = std::move(screens_[i].chars);
      std::unique_ptr<Attr[]> oldattr = std::move(screens_[i].attr);

      size_t n = size_.ws_col * history_size;
      screens_[i].chars.reset(new wchar_t[n]);
      screens_[i].attr .reset(new Attr[n]);

      std::fill(&screens_[i].chars[0],
                &screens_[i].chars[n], L' ');
      std::fill(&screens_[i].attr[0],
                &screens_[i].attr[n], EffectiveAttribute());

      scrollbottom = rows;

      int srcoff = 0;
      int minrows;

      if (rows > oldrows) {
        minrows = oldrows;
      } else {
        if (screens_[i].cursor_y >= rows)
          srcoff = current_screen_->cursor_y - rows + 1;
        minrows = rows;
      }

      int mincols = (cols < oldcols) ? cols : oldcols;

      for (int row = 0; row < minrows; ++row) {
        memcpy(&screens_[i].chars[row * cols],
               &oldchars[(row + srcoff) * oldcols],
               mincols * sizeof(CharacterType));
        memcpy(&screens_[i].attr[row * cols],
               &oldattr[(row + srcoff) * oldcols], mincols * sizeof(Attr));
      }

      screens_[i].cursor_y -= srcoff;

      screens_[i].cursor_x =
          std::max(std::min(screens_[i].cursor_x, rows - 1), 0);
      screens_[i].cursor_y =
          std::max(std::min(screens_[i].cursor_y, cols - 1), 0);
    }
  }
}

void Terminal::ProcessData(const void* buf, size_t count) {
  const unsigned char* begin = reinterpret_cast<const unsigned char*>(buf);
  const unsigned char* end = begin + count;

  // Redundant, optimized character processing code for the typical case.
  if (!escape && !insertmode && !nch_ && !current_screen_->use_alt_charset) {
    Attr attr = EffectiveAttribute();
    size_t offset = (current_screen_->scroll_line + current_screen_->cursor_y) %
                        history_size * size_.ws_col +
                    current_screen_->cursor_x;

    for (; begin != end; ++begin) {
      if (*begin >= ' ' && *begin <= '~') {
        if (current_screen_->cursor_x == size_.ws_col) {
          if (++current_screen_->cursor_y >= size_.ws_row) {
            Scroll(false);
            --current_screen_->cursor_y;
          }

          current_screen_->cursor_x = 0;

          offset = (current_screen_->scroll_line + current_screen_->cursor_y) %
                   history_size * size_.ws_col;
        }

        current_screen_->chars[offset] = *begin;
        current_screen_->attr[offset] = attr;
        ++current_screen_->cursor_x;
        ++offset;
      } else if (*begin == '\r') {
        current_screen_->cursor_x = 0;
        offset = (current_screen_->scroll_line + current_screen_->cursor_y) %
                 history_size * size_.ws_col;
      } else if (*begin == '\n') {
        ++current_screen_->cursor_y;

        if (current_screen_->cursor_y == scrollbottom ||
            current_screen_->cursor_y >= size_.ws_row) {
          Scroll(false);
          --current_screen_->cursor_y;
        }

        offset = (current_screen_->scroll_line + current_screen_->cursor_y) %
                     history_size * size_.ws_col +
                 current_screen_->cursor_x;
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

          case '\\':

            escape = 0;

            break;

          case '\b':

            if (current_screen_->cursor_x > 0) --current_screen_->cursor_x;

            break;

          case '\t': {
            unsigned int next_tab = (current_screen_->cursor_x + 8) & ~7;
            auto tab_stop =
                tab_stops_.lower_bound(current_screen_->cursor_x + 1);

            if (tab_stop != tab_stops_.end() && *tab_stop < next_tab)
              current_screen_->cursor_x = *tab_stop;
            else
              current_screen_->cursor_x = next_tab;

            if (current_screen_->cursor_x >= size_.ws_col)
              current_screen_->cursor_x = size_.ws_col - 1;
          } break;

          case '\n':

            ++current_screen_->cursor_y;

            if (current_screen_->cursor_y == scrollbottom ||
                current_screen_->cursor_y >= size_.ws_row) {
              Scroll(false);
              --current_screen_->cursor_y;
            }

            break;

          case '\r':

            current_screen_->cursor_x = 0;

            break;

          case '\177':

            if (current_screen_->cursor_y < size_.ws_row &&
                current_screen_->cursor_x < size_.ws_col)
              current_screen_->chars[(current_screen_->scroll_line +
                                      current_screen_->cursor_y) %
                                         history_size * size_.ws_col +
                                     current_screen_->cursor_x] = 0;

            break;

          case ('O' & 0x3F): /* ^O = default character set */

            break;

          case ('N' & 0x3F): /* ^N = alternate character set */

            break;

          default:

            assert(current_screen_->cursor_x >= 0 &&
                   current_screen_->cursor_x <= size_.ws_col);
            assert(current_screen_->cursor_y >= 0 &&
                   current_screen_->cursor_y < size_.ws_row);

            if (current_screen_->cursor_x == size_.ws_col) {
              ++current_screen_->cursor_y;
              current_screen_->cursor_x = 0;
            }

            // TODO(mortehu): Check if we need to test against scrollbottom as
            // well.
            if (current_screen_->cursor_y >= size_.ws_row) {
              Scroll(false);
              --current_screen_->cursor_y;
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
          case '\\':

            escape = 0;

            break;

          case '7':

            escape = 0;
            savedx_ = current_screen_->cursor_x;
            savedy_ = current_screen_->cursor_y;

            break;

          case '8':

            escape = 0;
            current_screen_->cursor_x = savedx_;
            current_screen_->cursor_y = savedy_;

            break;

          case 'D':

            escape = 0;
            ++current_screen_->cursor_y;

            if (current_screen_->cursor_y == scrollbottom ||
                current_screen_->cursor_y >= size_.ws_row) {
              Scroll(false);
              --current_screen_->cursor_y;
            }

            break;

          case 'E':

            escape = 0;
            current_screen_->cursor_x = 0;
            ++current_screen_->cursor_y;

            if (current_screen_->cursor_y == scrollbottom ||
                current_screen_->cursor_y >= size_.ws_row) {
              Scroll(false);
              --current_screen_->cursor_y;
            }

            break;

          case 'H':

            escape = 0;
            tab_stops_.insert(current_screen_->cursor_x);

            break;

          case 'c':

            escape = 0;
            tab_stops_.clear();
            current_screen_->cursor_x = 0;
            current_screen_->cursor_y = 0;
            for (size_t i = 0; i < size_.ws_row; ++i)
              ClearLine((current_screen_->scroll_line +
                         current_screen_->cursor_y + i) %
                        history_size);
            current_screen_->use_alt_charset = false;

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

            if (current_screen_->cursor_x == 0 &&
                current_screen_->cursor_y == scrolltop)
              ReverseScroll(false);
            else if (current_screen_->cursor_y)
              --current_screen_->cursor_y;

            escape = 0;

            break;

          default:

            escape = 0;
        }

        break;

      default:

        if (*begin == '\\') {
          escape = 0;
        } else if (param[0] == -1) {
          escape = 0;
        } else if (param[0] == -2) {
          /* Handle ESC ] Ps ; Pt BEL */

          if (*begin == '\007') {
            escape = 0;
            break;
          }

          if (escape == 2) {
            if (*begin >= '0' && *begin <= '9') {
              param[1] *= 10;
              param[1] += *begin - '0';
            } else if (*begin == ';') {
              ++escape;
            }
          } else {
            /* XXX: Store text */
          }
        } else if (param[0] == -4) {
          switch (*begin) {
            case '0':
              current_screen_->use_alt_charset = true;
              break;
            case 'B':
              current_screen_->use_alt_charset = false;
              break;
          }

          escape = 0;
        } else if (param[0] == -5) {
          escape = 0;
          switch (*begin) {
            case '8':
              for (size_t i = 0; i < size_.ws_row; ++i)
                ClearLineWithAttr(
                    (current_screen_->scroll_line + i) % history_size, 'E',
                    kDefaultAttr);
              break;
          }
        } else if (escape == 2 && *begin == '?') {
          param[0] = -3;
          ++escape;
        } else if (escape == 2 && *begin == '>') {
          param[0] = -4;
          ++escape;
        } else if (*begin == ';') {
          if (escape < (int)sizeof(param) + 1)
            param[++escape - 2] = 0;
          else
            param[(sizeof(param) / sizeof(param[0])) - 1] = 0;
        } else if (*begin >= '0' && *begin <= '9') {
          param[escape - 2] *= 10;
          param[escape - 2] += *begin - '0';
        } else if (param[0] == -3) {
          if (*begin == 'h') {
            for (int k = 1; k < escape - 1; ++k) {
              switch (param[k]) {
                case 1:
                  appcursor = true;
                  break;
                case 25:
                  hide_cursor = false;
                  break;
                case 1049:
                  if (current_screen_ != &screens_[1]) {
                    std::fill(&screens_[1].chars[0],
                              &screens_[1].chars[size_.ws_col * history_size],
                              0);
                    std::fill(&screens_[1].attr[0],
                              &screens_[1].attr[size_.ws_col * history_size],
                              kDefaultAttr);
                    SetScreen(1);
                  }
                  break;
                case 2004:
                  bracketed_paste = true;
                  break;
              }
            }
          } else if (*begin == 'l') {
            for (int k = 1; k < escape - 1; ++k) {
              switch (param[k]) {
                case 1:
                  appcursor = false;
                  break;
                case 25:
                  hide_cursor = true;
                  break;
                case 1049:
                  SetScreen(0);
                  break;
                case 2004:
                  bracketed_paste = false;
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

              current_screen_->cursor_y -=
                  (param[0] < current_screen_->cursor_y)
                      ? param[0]
                      : current_screen_->cursor_y;

              break;

            case 'B':

              if (!param[0]) param[0] = 1;

              current_screen_->cursor_y =
                  (param[0] + current_screen_->cursor_y < size_.ws_row)
                      ? (param[0] + current_screen_->cursor_y)
                      : (size_.ws_row - 1);

              break;

            case 'C':

              if (!param[0]) param[0] = 1;

              current_screen_->cursor_x =
                  (param[0] + current_screen_->cursor_x < size_.ws_col)
                      ? (param[0] + current_screen_->cursor_x)
                      : (size_.ws_col - 1);

              break;

            case 'D':

              if (!param[0]) param[0] = 1;

              current_screen_->cursor_x -=
                  (param[0] < current_screen_->cursor_x)
                      ? param[0]
                      : current_screen_->cursor_x;

              break;

            case 'E':

              current_screen_->cursor_x = 0;
              ++current_screen_->cursor_y;

              if (current_screen_->cursor_y == scrollbottom ||
                  current_screen_->cursor_y >= size_.ws_row) {
                Scroll(false);
                --current_screen_->cursor_y;
              }

              break;

            case 'F':

              current_screen_->cursor_x = 0;

              if (current_screen_->cursor_y == scrolltop)
                ReverseScroll(false);
              else if (current_screen_->cursor_y)
                --current_screen_->cursor_y;

              escape = 0;

              break;

            case 'G':

              if (param[0] > 0) --param[0];

              current_screen_->cursor_x =
                  (param[0] < size_.ws_col) ? param[0] : (size_.ws_col - 1);

              break;

            case 'H':
            case 'f':

              if (param[0] > 0) --param[0];

              if (param[1] > 0) --param[1];

              current_screen_->cursor_y =
                  (param[0] < size_.ws_row) ? param[0] : (size_.ws_row - 1);
              current_screen_->cursor_x =
                  (param[1] < size_.ws_col) ? param[1] : (size_.ws_col - 1);

              break;

            case 'J': {
              size_t begin = current_screen_->scroll_line;
              size_t end = current_screen_->scroll_line + size_.ws_row;
              bool fall_through = true;

              switch (param[0]) {
                case 0:
                  begin = current_screen_->scroll_line +
                          current_screen_->cursor_y + 1;
                  break;
                case 1:
                  end =
                      current_screen_->scroll_line + current_screen_->cursor_y;
                  break;
                default:
                case 2:
                  fall_through = false;
                  break;
              }

              for (size_t i = begin; i < end; ++i) ClearLine(i % history_size);

              if (!fall_through) break;
            }

            case 'K': {
              size_t line_offset =
                  (current_screen_->scroll_line + current_screen_->cursor_y) %
                  history_size * size_.ws_col;
              size_t begin, end;

              switch (param[0]) {
                case 0:
                  /* Clear from cursor to end */
                  begin = current_screen_->cursor_x;
                  end = size_.ws_col;
                  break;
                case 1:
                  /* Clear from start to cursor */
                  begin = 0;
                  end = current_screen_->cursor_x + 1;
                  break;
                default:
                case 2:
                  /* Clear entire line */
                  begin = 0;
                  end = size_.ws_col;
              }

              Attr attr = EffectiveAttribute();

              for (size_t x = begin; x < end; ++x) {
                current_screen_->chars[line_offset + x] = ' ';
                current_screen_->attr[line_offset + x] = attr;
              }
            } break;

            case 'L':

              if (!param[0])
                param[0] = 1;
              else if (param[0] > size_.ws_row)
                param[0] = size_.ws_row;

              while (param[0]--) ReverseScroll(true);

              break;

            case 'M':

              if (!param[0])
                param[0] = 1;
              else if (param[0] > size_.ws_row)
                param[0] = size_.ws_row;

              while (param[0]--) Scroll(true);

              break;

            case 'P': {
              /* Delete character at cursor */
              if (!param[0]) param[0] = 1;
              if (current_screen_->cursor_x + param[0] > size_.ws_col)
                param[0] = size_.ws_col - current_screen_->cursor_x;

              DeleteChars(param[0]);
            } break;

            case 'S':

              param[0] = std::max(
                  1, std::min(static_cast<int>(size_.ws_row), param[0]));

              while (param[0]--) Scroll(false);

              break;

            case 'T':

              param[0] = std::max(
                  1, std::min(static_cast<int>(size_.ws_row), param[0]));

              while (param[0]--) ReverseScroll(false);

              break;

            case 'X': {
              if (param[0] <= 0) param[0] = 1;

              Attr attr = EffectiveAttribute();

              for (int k = current_screen_->cursor_x;
                   k < current_screen_->cursor_x + param[0] && k < size_.ws_col;
                   ++k) {
                current_screen_->chars[(current_screen_->cursor_y +
                                        current_screen_->scroll_line) %
                                           history_size * size_.ws_col +
                                       k] = 0;
                current_screen_->attr[(current_screen_->cursor_y +
                                       current_screen_->scroll_line) %
                                          history_size * size_.ws_col +
                                      k] = attr;
              }

            } break;

            case 'c':
              if (!param[0]) {
                // Terminal attributes requested.
                write_function_("\033[?1;0c", 7);
              }
              break;

            case 'd':

              if (param[0] > 0)
                --param[0];
              else
                param[0] = 0;

              current_screen_->cursor_y =
                  (param[0] < size_.ws_row) ? param[0] : (size_.ws_row - 1);

              break;

            case 'h':

              for (int k = 0; k < escape - 1; ++k) {
                switch (param[k]) {
                  case 4:
                    insertmode = true;
                    break;
                }
              }

              break;

            case 'l':

              for (int k = 0; k < escape - 1; ++k) {
                switch (param[k]) {
                  case 4:
                    insertmode = false;
                    break;
                }
              }

              break;

            case 'm': {
              for (int k = 0; k + 1 < escape;) {
                int code = param[k++];

                switch (code) {
                  case 7:
                    reverse = true;
                    break;
                  case 27:
                    reverse = false;
                    break;

                  case 38: {
                    // Extended foreground color codes.

                    if (k + 1 >= escape) break;

                    switch (param[k++]) {
                      case 2:  // RGB
                        if (k + 3 >= escape) break;
                        attribute_.fg =
                            Color(param[k], param[k + 1], param[k + 2]);
                        k += 3;
                        break;
                      case 5:  // Indexed
                        if (param[k] >= 0 && param[k] <= 255)
                          attribute_.fg = kDefaultColors[param[k++]];
                        break;
                    }

                  } break;

                  case 48: {
                    // Extended background color codes.

                    if (k + 1 >= escape) break;

                    switch (param[k++]) {
                      case 2:  // RGB
                        if (k + 3 >= escape) break;
                        attribute_.bg =
                            Color(param[k], param[k + 1], param[k + 2]);
                        k += 3;
                        break;
                      case 5:  // Indexed
                        if (param[k] >= 0 && param[k] <= 255)
                          attribute_.bg = kDefaultColors[param[k++]];
                        break;
                    }

                  } break;

                  case 0:
                    reverse = false;

                  // Fall through.

                  default: {
                    for (size_t l = 0;
                         l < sizeof(kANSIHelper) / sizeof(kANSIHelper[0]);
                         ++l) {
                      if (kANSIHelper[l].index == code) {
                        ansi_attribute_ &= kANSIHelper[l].and_mask;
                        ansi_attribute_ |= kANSIHelper[l].or_mask;
                        break;
                      }
                    }

                    unsigned fg_color_index = (ansi_attribute_ >> 8) & 7;
                    if (ansi_attribute_ &
                        (ATTR_HIGHLIGHT | ATTR_STANDOUT | ATTR_BOLD))
                      fg_color_index += 8;

                    attribute_.fg = ansi_colors_[fg_color_index];
                    attribute_.bg = ansi_colors_[(ansi_attribute_ >> 12) & 7];
                    attribute_.extra =
                        ansi_attribute_ & (ATTR_BLINK | ATTR_UNDERLINE);
                  } break;
                }
              }
            } break;

            case 'r':

              if (param[0] < param[1]) {
                --param[0];

                if (param[1] > size_.ws_row) param[1] = size_.ws_row;

                if (param[0] < 0) param[0] = 0;

                if (param[0] >= param[1]) break;

                scrolltop = param[0];
                scrollbottom = param[1];
              } else {
                scrolltop = 0;
                scrollbottom = size_.ws_row;
              }

              break;

            case 's':

              savedx_ = current_screen_->cursor_x;
              savedy_ = current_screen_->cursor_y;

              break;

            case 'u':

              current_screen_->cursor_x = savedx_;
              current_screen_->cursor_y = savedy_;

              break;
          }

          escape = 0;
        }
    }
  }
}

void Terminal::GetState(State* state) const {
  state->width = size_.ws_col;
  state->height = size_.ws_row;
  state->chars.resize(size_.ws_col * size_.ws_row);
  state->attr.resize(size_.ws_col * size_.ws_row);

  for (size_t row = 0, offset = (history_size - history_scroll +
                                 current_screen_->scroll_line) *
                                size_.ws_col;
       row < size_.ws_row; ++row, offset += size_.ws_col) {
    offset %= history_size * size_.ws_col;
    std::copy(&current_screen_->chars[offset],
              &current_screen_->chars[offset + size_.ws_col],
              &state->chars[row * size_.ws_col]);
    std::copy(&current_screen_->attr[offset],
              &current_screen_->attr[offset + size_.ws_col],
              &state->attr[row * size_.ws_col]);
  }

  state->cursor_x = std::min(current_screen_->cursor_x, size_.ws_col - 1);
  state->cursor_y = current_screen_->cursor_y + history_scroll;

  size_t selbegin, selend;
  if (select_begin < select_end) {
    selbegin = select_begin;
    selend = select_end;
  } else {
    selbegin = select_end;
    selend = select_begin;
  }

  state->selection_begin = (selbegin + history_scroll * size_.ws_col) %
                           (history_size * size_.ws_col);
  state->selection_end =
      (selend + history_scroll * size_.ws_col) % (history_size * size_.ws_col);

  state->cursor_hidden = hide_cursor;
  state->focused = focused;

  if (!hide_cursor) state->cursor_hint = cursor_hint_;
}

void Terminal::InsertChars(size_t count) {
  size_t line_offset =
      (current_screen_->scroll_line + current_screen_->cursor_y) %
      history_size * size_.ws_col;
  size_t k = size_.ws_col;

  while (k > current_screen_->cursor_x + count) {
    --k;
    current_screen_->chars[line_offset + k] =
        current_screen_->chars[line_offset + k - count];
    current_screen_->attr[line_offset + k] =
        current_screen_->attr[line_offset + k - count];
  }

  Attr attr = EffectiveAttribute();

  while (k-- > static_cast<size_t>(current_screen_->cursor_x)) {
    current_screen_->chars[line_offset + k] = ' ';
    current_screen_->attr[line_offset + k] = attr;
  }
}

void Terminal::DeleteChars(size_t count) {
  size_t line_offset =
      (current_screen_->scroll_line + current_screen_->cursor_y) %
      history_size * size_.ws_col;
  size_t k = current_screen_->cursor_x;

  for (; k + count < size_.ws_col; ++k) {
    current_screen_->chars[line_offset + k] =
        current_screen_->chars[line_offset + k + count];
    current_screen_->attr[line_offset + k] =
        current_screen_->attr[line_offset + k + count];
  }

  Attr attr = EffectiveAttribute();

  for (; k < size_.ws_col; ++k) {
    current_screen_->chars[line_offset + k] = ' ';
    current_screen_->attr[line_offset + k] = attr;
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

  if (current_screen_->use_alt_charset && ch >= 0x41 && ch <= 0x7e)
    ch = kAltCharset[ch - 0x41];

  if (ch < 32) return;

  size_t offset = (current_screen_->scroll_line + current_screen_->cursor_y) %
                      history_size * size_.ws_col +
                  current_screen_->cursor_x;

  if (ch == 0x7f || ch >= 65536) {
    current_screen_->chars[offset] = ' ';
    current_screen_->attr[offset] = EffectiveAttribute();
    return;
  }

  if (insertmode) InsertChars(1);

  current_screen_->chars[offset] = ch;
  current_screen_->attr[offset] = EffectiveAttribute();
  ++current_screen_->cursor_x;
}

void Terminal::NormalizeHistoryBuffer() {
  size_t history_buffer_size = size_.ws_col * history_size;

  for (size_t i = 0; i < 2; ++i) {
    if (!screens_[i].scroll_line) continue;

    assert(screens_[i].scroll_line > 0);
    assert(screens_[i].scroll_line < history_size);

    size_t buffer_offset = screens_[i].scroll_line * size_.ws_col;

    std::unique_ptr<CharacterType[]> tmpchars(new CharacterType[buffer_offset]);
    std::unique_ptr<Attr[]> tmpattrs(new Attr[buffer_offset]);

    memcpy(&tmpchars[0], &screens_[i].chars[0],
           sizeof(CharacterType) * buffer_offset);
    memcpy(&tmpattrs[0], &screens_[i].attr[0], sizeof(Attr) * buffer_offset);

    memmove(&screens_[i].chars[0], &screens_[i].chars[buffer_offset],
            sizeof(CharacterType) * (history_buffer_size - buffer_offset));
    memmove(&screens_[i].attr[0], &screens_[i].attr[buffer_offset],
            sizeof(Attr) * (history_buffer_size - buffer_offset));

    memcpy(&screens_[i].chars[history_buffer_size - buffer_offset],
           &tmpchars[0], sizeof(CharacterType) * buffer_offset);
    memcpy(&screens_[i].attr[history_buffer_size - buffer_offset], &tmpattrs[0],
           sizeof(Attr) * buffer_offset);

    screens_[i].scroll_line = 0;
  }
}

void Terminal::ClearLineWithAttr(size_t line, int ch, const Attr& attr) {
  size_t offset = line * size_.ws_col;

  std::fill(&current_screen_->chars[offset],
            &current_screen_->chars[offset + size_.ws_col], ch);
  std::fill(&current_screen_->attr[offset],
            &current_screen_->attr[offset + size_.ws_col], attr);
}

void Terminal::Scroll(bool fromcursor) {
  if (!fromcursor && scrolltop == 0 && scrollbottom == size_.ws_row) {
    ClearLine((current_screen_->scroll_line + size_.ws_row) % history_size);
    current_screen_->scroll_line =
        (current_screen_->scroll_line + 1) % history_size;

    return;
  }

  NormalizeHistoryBuffer();

  size_t first, length;

  if (fromcursor) {
    // TODO(mortehu): See what other terminals do in this case.
    if (current_screen_->cursor_y >= scrollbottom) return;
    first = current_screen_->cursor_y;
    length = (scrollbottom - current_screen_->cursor_y - 1);
  } else {
    first = scrolltop;
    length = (scrollbottom - scrolltop - 1);
  }

  memmove(&current_screen_->chars[first * size_.ws_col],
          &current_screen_->chars[(first + 1) * size_.ws_col],
          length * size_.ws_col * sizeof(CharacterType));
  std::fill(&current_screen_->chars[(first + length) * size_.ws_col],
            &current_screen_->chars[(first + length + 1) * size_.ws_col], ' ');

  memmove(&current_screen_->attr[first * size_.ws_col],
          &current_screen_->attr[(first + 1) * size_.ws_col],
          length * size_.ws_col * sizeof(Attr));
  std::fill(&current_screen_->attr[(first + length) * size_.ws_col],
            &current_screen_->attr[(first + length + 1) * size_.ws_col],
            EffectiveAttribute());
}

void Terminal::ReverseScroll(bool fromcursor) {
  NormalizeHistoryBuffer();

  size_t first, length;

  if (fromcursor) {
    if (current_screen_->cursor_y + 1 >= scrollbottom) return;
    first = current_screen_->cursor_y;
    length = (scrollbottom - 1 - current_screen_->cursor_y);
  } else {
    if (scrolltop + 1 >= scrollbottom) return;
    first = scrolltop;
    length = (scrollbottom - scrolltop - 1);
  }

  memmove(&current_screen_->chars[(first + 1) * size_.ws_col],
          &current_screen_->chars[first * size_.ws_col],
          length * size_.ws_col * sizeof(CharacterType));
  std::fill(&current_screen_->chars[first * size_.ws_col],
            &current_screen_->chars[(first + 1) * size_.ws_col], ' ');

  memmove(&current_screen_->attr[(first + 1) * size_.ws_col],
          &current_screen_->attr[first * size_.ws_col],
          length * size_.ws_col * sizeof(Attr));
  std::fill(&current_screen_->attr[first * size_.ws_col],
            &current_screen_->attr[(first + 1) * size_.ws_col],
            EffectiveAttribute());
}

void Terminal::Select(RangeType range_type) {
  NormalizeHistoryBuffer();

  select_end =
      current_screen_->cursor_y * size_.ws_col + current_screen_->cursor_x;

  if (select_end == 0) {
    select_begin = 0;
    select_end = 1;
  } else
    select_begin = select_end - 1;

  FindRange(range_type, &select_begin, &select_end);
}

bool Terminal::FindRange(RangeType range_type, size_t* begin,
                         size_t* end) const {
  size_t history_buffer_size = size_.ws_col * history_size;
  size_t offset = current_screen_->scroll_line * size_.ws_col;

  size_t i;
  int ch;

  switch (range_type) {
    case Terminal::kRangeLine: {
      i = *begin;
      *begin -= (*begin % size_.ws_col); // Start of the line

      i = *end;
      *end = ((*end / size_.ws_col) + 1) * size_.ws_col; // End of the line (next line start)

      // No need to check for empty range for line selection,
      // as selecting a line will always select at least the line itself.
      return true;
    }

    case Terminal::kRangeWordOrURL:
      i = *begin;

      while (i) {
        if (!(i % size_.ws_col)) break;

        ch = current_screen_->chars[(offset + i - 1) % history_buffer_size];

        if (ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch)) break;

        --i;
      }

      *begin = i;

      i = *end;

      while ((i % size_.ws_col) < size_.ws_col) {
        ch = current_screen_->chars[(offset + i) % history_buffer_size];

        if (ch <= 32 || ch == 0x7f || strchr("\'\"()[]{}<>,`", ch)) break;

        ++i;
      }

      *end = i;

      return *begin != *end;

    case Terminal::kRangeParenthesis: {
      int paren_level = 0;
      i = *begin;

      while (i > 0) {
        ch = current_screen_->chars[(offset + i) % history_buffer_size];

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
          current_screen_->chars[(offset + *end - 1) % history_buffer_size] ==
              '=')
        --*end;

      return true;
    }
  }

  return false;
}

std::string Terminal::GetTextInRange(size_t begin, size_t end) const {
  if (begin == end) return std::string();

  size_t history_buffer_size = size_.ws_col * history_size;
  size_t offset = current_screen_->scroll_line * size_.ws_col;

  if (begin > end) std::swap(begin, end);

  size_t last_graph = 0;
  size_t last_graph_col = 0;
  size_t i = begin;

  std::string result;

  while (i != end) {
    int ch = current_screen_->chars[(i + offset) % history_buffer_size];
    size_t width = size_.ws_col;

    if (ch == 0 || ch == 0xffff) ch = ' ';

    if (i > begin && (i % width) == 0) {
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

std::string Terminal::GetCurrentLine(bool to_cursor) const {
  size_t begin = current_screen_->cursor_y * size_.ws_col;
  size_t end = begin + (to_cursor ? current_screen_->cursor_x : size_.ws_col);

  return GetTextInRange(begin, end);
}
