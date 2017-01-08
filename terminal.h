#ifndef TERMINAL_H_
#define TERMINAL_H_ 1

#include <string.h>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

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

struct KeyInfo : std::pair<unsigned int, unsigned int> {
  typedef std::pair<unsigned int, unsigned int> super;

  KeyInfo(unsigned int symbol) : super(symbol, 0) {}

  KeyInfo(unsigned int symbol, unsigned int mask)
      : super(symbol, mask & (ControlMask | Mod1Mask | ShiftMask)) {}
};

class Terminal {
 public:
  enum RangeType {
    kRangeWordOrURL,
    kRangeParenthesis,
  };

  typedef wchar_t CharacterType;

  struct Color {
    Color() : r(), g(), b() {}

    Color(uint32_t rgb) : r(rgb >> 16), g(rgb >> 8), b(rgb) {}

    Color(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}

    uint8_t r, g, b;
  };

  struct Attr {
    Attr() : fg(), bg(), extra() {}
    Attr(const Color& fg, const Color& bg, uint8_t extra = 0)
        : fg(fg), bg(bg), extra(extra) {}

    Attr Reverse() const { return Attr(bg, fg, extra); }

    Color fg, bg;
    uint8_t extra;
  };

  struct Screen {
    Screen() : scroll_line(), cursor_x(), cursor_y(), use_alt_charset() {}

    std::unique_ptr<CharacterType[]> chars;
    std::unique_ptr<Attr[]> attr;
    size_t scroll_line;
    int cursor_x, cursor_y;
    bool use_alt_charset;
  };

  struct State {
    size_t width, height;
    std::vector<CharacterType> chars;
    std::vector<Attr> attr;
    size_t cursor_x, cursor_y;
    size_t selection_begin, selection_end;
    bool cursor_hidden;
    bool focused;
    std::string cursor_hint;

    // Next predicted keystrokes.
    std::vector<char> completion_hint;
  };

  Terminal(std::function<void(const void*, size_t)>&& write_function);

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

  std::string GetSelection() const {
    return GetTextInRange(select_begin, select_end);
  }

  std::string GetCurrentLine(bool to_cursor = false) const;

  void Select(RangeType range_type);
  bool FindRange(RangeType range_type, size_t* begin, size_t* end) const;

  void ClearSelection() {
    select_begin = 0;
    select_end = 0;
  }

  void SetCursorHint(const std::string& hint) { cursor_hint_ = hint; }
  void ClearCursorHint() { cursor_hint_.clear(); }

  const winsize& Size() const { return size_; }

  bool reverse;
  size_t history_size;
  int scrolltop;
  int scrollbottom;
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

  void SetScreen(int screen) { current_screen_ = &screens_[screen]; }

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

  std::function<void(const void*, size_t)> write_function_;

  struct winsize size_;

  Screen screens_[2];
  Screen* current_screen_;

  Color ansi_colors_[256];
  unsigned int ansi_attribute_;
  Attr attribute_;

  unsigned int ch_, nch_;

  int savedx_, savedy_;

  std::string cursor_hint_;

  std::set<unsigned int> tab_stops_;
};

namespace std {

template <>
struct hash<KeyInfo> {
  size_t operator()(const KeyInfo& k) const {
    return (k.first << 16) | k.second;
  }
};

}  // namespace std

#endif /* !TERMINAL_H_ */
