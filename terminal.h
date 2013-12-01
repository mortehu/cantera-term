#ifndef TERMINAL_H_
#define TERMINAL_H_ 1

#include <memory>
#include <string.h>
#include <string>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <X11/X.h>

#define ATTR_BLINK 0x0008
#define ATTR_HIGHLIGHT 0x0008
#define ATTR_BOLD 0x0008
#define ATTR_STANDOUT 0x0008
#define ATTR_UNDERLINE 0x0800
#define ATTR_BLACK 0x0000
#define ATTR_BLUE 0x0001
#define ATTR_GREEN 0x0002
#define ATTR_RED 0x0004
#define ATTR_CYAN (ATTR_BLUE | ATTR_GREEN)
#define ATTR_MAGENTA (ATTR_BLUE | ATTR_RED)
#define ATTR_YELLOW (ATTR_GREEN | ATTR_RED)
#define ATTR_WHITE (ATTR_BLUE | ATTR_GREEN | ATTR_RED)
#define FG(color) color
#define BG(color) ((color) << 4)
#define FG_DEFAULT FG(ATTR_WHITE)
#define BG_DEFAULT BG(ATTR_BLACK)
#define ATTR_DEFAULT (FG_DEFAULT | BG_DEFAULT)
#define REVERSE(color) \
  ((((color) & 0x70) >> 4) | (((color) & 0x07) << 4) | ((color) & 0x88))

class Terminal {
 public:
  struct State {
    size_t width, height;
    std::unique_ptr<wchar_t[]> chars;
    std::unique_ptr<uint16_t[]> attrs;
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

  void Init(unsigned int width, unsigned int height, unsigned int space_width,
            unsigned int line_height, size_t scroll_extra);
  void Resize(unsigned int width, unsigned int height, unsigned int space_width,
              unsigned int line_height);

  void ProcessData(const void* buf, size_t count);
  void GetState(State* state) const;

  void Select(RangeType range_type);
  bool FindRange(RangeType range_type, int* begin, int* end) const;
  void ClearSelection();
  std::string GetSelection();

  void SaveSession(const char* session_path);
  void RestoreSession(int fd);

  const winsize& Size() const { return size_; }

  char* buffer;
  std::unique_ptr<wchar_t[]> chars[2];
  std::unique_ptr<uint16_t[]> attr[2];
  wchar_t* curchars;
  uint16_t* curattrs;
  size_t scroll_line[2];
  size_t* cur_scroll_line;
  int curscreen;
  int curattr;
  int reverse;
  size_t history_size;
  int fontsize;
  int storedcursorx[2];
  int storedcursory[2];
  int scrolltop;
  int scrollbottom;
  int cursorx;
  int cursory;
  int escape;
  int param[8];
  int appcursor;
  int hide_cursor;
  int insertmode;

  int select_begin;
  int select_end;

  int focused;

  unsigned int history_scroll;

 private:
  void NormalizeHistoryBuffer();
  void Scroll(bool fromcursor);
  void ReverseScroll(bool fromcursor);

  void SetScreen(int screen);
  void InsertChars(size_t count);
  void AddChar(int ch);
  void ClearLineWithAttr(size_t line, int ch, uint16_t attr);

  void ClearLine(size_t line) {
    ClearLineWithAttr(line, ' ', EffectiveAttribute());
  }

  uint16_t EffectiveAttribute() const {
    return reverse ? REVERSE(curattr) : curattr;
  }

  struct winsize size_;

  bool use_alt_charset_[2];
  unsigned int ch_, nch_;

  int savedx_, savedy_;
};

#endif /* !TERMINAL_H_ */
