#ifndef GLOBALS_H_
#define GLOBALS_H_ 1

#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xdamage.h>

#include <stdint.h>

#include <sys/time.h>
#include <pty.h>

#include "wm-window-type.h"

enum mode
{
  mode_menu,
  mode_x11
};

#define MAX_QUERIES 16

struct screen;
typedef struct terminal terminal;

#define WINDOW_IS_MAPPED          0x0001
#define WINDOW_WANT_UNMAPPED      0x0002
#define WINDOW_DIRTY              0x0004
#define WINDOW_UNMANAGED          0x0008

struct window
{
  Window xwindow;
  Picture xpicture;
  Damage xdamage;
  XserverRegion damage_region;

  int x, y;
  unsigned int width, height;

  enum wm_window_type type;

  int flags;

  Window transient_for;
  terminal* desktop;
  struct screen* screen;
};

struct terminal
{
  Picture thumbnail;

  enum mode mode;
  enum mode return_mode;

  pid_t pid;
  Time startup;
  int fd;

  struct timeval next_update;
};

#define TERMINAL_COUNT 24

struct screen
{
  wchar_t query[256];

  unsigned int x_org, y_org;
  unsigned int width, height;
  Window window;
  Picture root_buffer;
  Picture root_picture;
  terminal terminals[TERMINAL_COUNT];

  struct window *background;

  int history[TERMINAL_COUNT];
  int history_size;

  int active_terminal;
  terminal* at;
};

extern struct screen* current_screen;

extern int xskips[];
extern int yskips[];
extern int font_sizes[];

extern GlyphSet alpha_glyphs[2];
extern XRenderPictFormat* xrenderpictformat;
extern XRenderPictFormat* a8pictformat;
extern XWindowAttributes root_window_attr;
extern Display* display;

extern XRenderColor xrpalette[];
extern Picture picpalette[];

void init_session(terminal* t, char* const* args);

int launch(const char* command, Time time);

size_t utf8_to_ucs(wchar_t* result, const char* input, size_t avail);

#endif /* GLOBALS_H_ */
