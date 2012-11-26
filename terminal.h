#ifndef TERMINAL_H_
#define TERMINAL_H_ 1

#include <sys/ioctl.h>
#include <sys/types.h>

#include "font.h"

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

struct terminal
{
  pid_t pid;
  int fd;

  pthread_mutex_t bufferLock;

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

  int select_begin;
  int select_end;

  int focused;

  unsigned int history_scroll;
};

extern struct FONT_Data *font;
extern unsigned int palette[16];

void term_clear_selection (void);

void term_process_data(const unsigned char* buf, size_t count);

#endif /* !TERMINAL_H_ */
