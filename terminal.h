#ifndef TERMINAL_H_
#define TERMINAL_H_ 1

#include <memory>
#include <set>
#include <string.h>
#include <string>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <X11/X.h>

#define ATTR_BLINK 0x0001
#define ATTR_HIGHLIGHT 0x0002
#define ATTR_BOLD 0x0004
#define ATTR_STANDOUT 0x0008
#define ATTR_UNDERLINE 0x0010
#define ATTR_BLACK 0x0000
#define ATTR_BLUE 0x0100
#define ATTR_GREEN 0x0200
#define ATTR_RED 0x0400
#define ATTR_CYAN (ATTR_BLUE | ATTR_GREEN)
#define ATTR_MAGENTA (ATTR_BLUE | ATTR_RED)
#define ATTR_YELLOW (ATTR_GREEN | ATTR_RED)
#define ATTR_WHITE (ATTR_BLUE | ATTR_GREEN | ATTR_RED)
#define FG(color) color
#define BG(color) ((color) << 4)
#define FG_DEFAULT FG(ATTR_WHITE)
#define BG_DEFAULT BG(ATTR_BLACK)
#define ATTR_DEFAULT (FG_DEFAULT | BG_DEFAULT)

class Terminal {
 public:
  struct Color {
    Color() {}

    Color(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}

    uint8_t r, g, b;
  };

  struct Attr {
    Attr() {}
    Attr(const Color& fg, const Color& bg, uint8_t extra = 0)
        : fg(fg), bg(bg), extra(extra) {}

    Attr Reverse() const {
      Attr result;
      result.fg = bg;
      result.bg = fg;
      result.extra = extra;
      return result;
    }

    Color fg, bg;
    uint8_t extra;
  };

  struct State {
    size_t width, height;
    std::unique_ptr<wchar_t[]> chars;
    std::unique_ptr<Attr[]> attrs;
    size_t cursor_x, cursor_y;
    size_t selection_begin, selection_end;
    bool cursor_hidden;
    bool focused;
  };

  enum RangeType {
    kRangeWordOrURL,
    kRangeParenthesis,
  };

  Terminal();

  void SetANSIColor(unsigned int index, const Color& color) {
    ansi_colors_[index] = color;
  }

  void Init(unsigned int width, unsigned int height, unsigned int space_width,
            unsigned int line_height, size_t scroll_extra);
  void Resize(unsigned int width, unsigned int height, unsigned int space_width,
              unsigned int line_height);

  void ProcessData(const void* buf, size_t count);
  void GetState(State* state) const;
  std::string GetTextInRange(size_t begin, size_t end) const;

  void Select(RangeType range_type);
  bool FindRange(RangeType range_type, size_t* begin, size_t* end) const;

  void ClearSelection() {
    select_begin = 0;
    select_end = 0;
  }

  std::string GetSelection() const {
    return GetTextInRange(select_begin, select_end);
  }

  void SaveSession(const char* session_path);
  void RestoreSession(int fd);

  const winsize& Size() const { return size_; }

  std::unique_ptr<wchar_t[]> chars[2];
  std::unique_ptr<Attr[]> attr[2];
  wchar_t* curchars;
  Attr* curattrs;
  size_t scroll_line[2];
  size_t* cur_scroll_line;
  int curscreen;
  bool reverse;
  size_t history_size;
  int storedcursorx[2];
  int storedcursory[2];
  int scrolltop;
  int scrollbottom;
  int cursorx;
  int cursory;
  int escape;
  int param[8];
  bool appcursor;
  bool hide_cursor;
  bool insertmode;

  size_t select_begin;
  size_t select_end;

  bool focused;

  unsigned int history_scroll;

 private:
  void NormalizeHistoryBuffer();
  void Scroll(bool fromcursor);
  void ReverseScroll(bool fromcursor);

  void SetScreen(int screen);
  void InsertChars(size_t count);
  void DeleteChars(size_t count);
  void AddChar(int ch);
  void ClearLineWithAttr(size_t line, int ch, const Attr& attr);

  void ClearLine(size_t line) {
    ClearLineWithAttr(line, ' ', EffectiveAttribute());
  }

  Attr EffectiveAttribute() const {
    return reverse ? attribute_.Reverse() : attribute_;
  }

  struct winsize size_;

  Color ansi_colors_[16];
  unsigned int ansi_attribute_;
  Attr attribute_;

  bool use_alt_charset_[2];
  unsigned int ch_, nch_;

  int savedx_, savedy_;

  std::set<unsigned int> tab_stops_;
};

#endif /* !TERMINAL_H_ */
