#ifndef GLOBALS_H_
#define GLOBALS_H_ 1

#include <stdint.h>

#include <sys/time.h>
#include <pty.h>

#include <X11/extensions/Xdamage.h>

enum mode
{
  mode_menu,
  mode_x11
};

#define MAX_QUERIES 16

struct transient
{
  Window window;
  int x, y;
  int width, height;
  struct transient* next;
};

typedef struct
{
  Window window;
  struct transient* transients;
  Damage damage;
  Picture picture;
  XserverRegion region;
  Picture thumbnail;
  int xscreen;

  enum mode mode;
  enum mode return_mode;

  pid_t pid;
  int fd;

  int dirty;
  struct timeval next_update;
} terminal;

extern int xskips[];
extern int yskips[];
extern int font_sizes[];

extern GlyphSet alpha_glyphs[2];
extern XRenderPictFormat* xrenderpictformat;
extern XRenderPictFormat* a8pictformat;
extern Picture root_buffer;
extern XWindowAttributes root_window_attr;
extern int window_width;
extern int window_height;

extern XRenderColor xrpalette[];
extern Picture picpalette[];

extern int active_terminal;
extern int last_set_terminal;
extern terminal* at;

extern terminal terminals[];

void init_session(terminal* t, char* const* args);

int launch(const char* command);

size_t utf8_to_ucs(wchar_t* result, const char* input, size_t avail);

#endif /* GLOBALS_H_ */
