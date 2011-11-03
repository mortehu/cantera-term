#ifndef GUI_H_
#define GUI_H_ 1

#include <X11/Xlib.h>
#include <X11/keysymdef.h>

#include <X11/Xft/Xft.h>

#include <X11/extensions/Xrender.h>

#define GUI_FONT_BOLD 0x0001

struct gui_instance;
struct gui_image;
struct gui_font;

#define GUI_OVERRIDE_REDIRECT 0x0001

struct gui_definition
{
  unsigned int flags;

  void (*init)(struct gui_instance *gi);

  /* This function is called when the user presses Alt-F4 or similar */
  void (*destroy)(struct gui_instance *gi);

  void (*paint)(struct gui_instance *gi, unsigned int x, unsigned int y, unsigned int width, unsigned int height);

  void (*key_pressed)(struct gui_instance *gi, unsigned int key, const wchar_t *text, unsigned int modmask);
  void (*key_released)(struct gui_instance *gi, unsigned int key);

  void (*mouse_moved)(struct gui_instance *gi, int x, int y);
  void (*button_pressed)(struct gui_instance *gi, unsigned int button, unsigned int modmask);
  void (*button_released)(struct gui_instance *gi, unsigned int button, unsigned int modmask);

  void (*x11_event)(void *ctx, XEvent *event);

  void (*paste)(struct gui_instance *gi, const char *text, size_t length);
};

enum gui_clipboard
{
  GUI_PRIMARY_SELECTION = 0,
  GUI_SECONDARY_SELECTION = 1,
  GUI_CLIPBOARD = 2,
  GUI_CLIPBOARD_COUNT = 3
};

struct gui_instance
{
  struct gui_definition definition;

  XSetWindowAttributes window_attr;
  Window window;
  XIC xic;

  Picture back_buffer;
  Picture front_buffer;

  GC gc;
  Pixmap pmap;

  XftDraw *fontdraw;

  int repaint_waiting;

  unsigned int width, height;

  Time last_event;

  struct
    {
      unsigned char *data;
      size_t length;
    }
  clipboards[GUI_CLIPBOARD_COUNT];
};

/**
 * Updates `buffer' and `size' with all data currently available.
 *
 * Returns 1 if transfer is complete, 0 otherwise.
 *
 * If transfer is not complete, the caller shall free the returned buffer.
 * This is because a background thread might reallocated the primary buffer, so
 * a copy has to be made for the caller.
 *
 * If the transfer is complete, the caller shall not free the returned buffer.
 * This is to avoid needless copying.
 */
typedef int (*gui_data_callback)(void **buffer, size_t *size, void *ctx);

/**
 * Loads an entire file into a buffer, and returns a handle usable by
 * gui_file_callback.
 *
 * Call free to destroy this handle.
 */
void *
gui_file_callback_init(const char *path);

/**
 * Returns the contents of a buffer loaded with gui_file_callback_init.
 *
 * Always returns 1 (transfer complete).
 */
int
gui_file_callback(void **buffer, size_t *size, void *ctx);

struct gui_instance *
gui_instance(struct gui_definition *definition);

void
gui_destroy(struct gui_instance *gi);

/* Thread-safe */
void
gui_repaint();

void
gui_draw_quad(struct gui_instance *gi, int x, int y, unsigned int width,
              unsigned int height, unsigned int color);

/**
 * Allocates a gui_image.
 *
 * You need to use this instead of malloc() to preserve binary compatibility.
 */
struct gui_image*
gui_image_alloc ();

/**
 * Frees a gui_image and all associated data.
 */
void
gui_image_free (struct gui_image** img);

/**
 * Set data source for image.
 *
 * You need to call this before gui_image_load.
 */
void
gui_image_set_data_callback (struct gui_image* image,
                             gui_data_callback data_callback, void* ctx);

/**
 * Load an image, progressively.
 *
 * This function either return None or the same Picture handle on each call.
 * If called multiple times, it will progressively update the Picture until all
 * data has arrived.
 */
int
gui_image_load (struct gui_image* image);

struct gui_image*
gui_image_load_simple (struct gui_instance *gi, const char *path);

void
gui_draw_image(struct gui_instance *gi, int x, int y,
               struct gui_image *image);

struct gui_font*
gui_font_load(const char *name, unsigned int size, int flags);

unsigned int
gui_font_line_height(struct gui_font *font);

void
gui_text_clip(struct gui_instance *gi, int x, int y,
              unsigned int width, unsigned int height);

unsigned int
gui_text_width(struct gui_font *font, const char *text, size_t length);

unsigned int
gui_wtext_width(struct gui_font *font, const wchar_t *text, size_t length);

void
gui_draw_text(struct gui_instance *gi, struct gui_font *font,
              int x, int y, const char *text, unsigned int color,
              int alignment);

void
gui_draw_text_length(struct gui_instance *gi, struct gui_font *font,
                     int x, int y, const char *text, size_t length,
                     unsigned int color);

void
gui_draw_wtext_length(struct gui_instance *gi, struct gui_font *font,
                      int x, int y, const wchar_t *text, size_t length,
                      unsigned int color);

void
gui_main_loop();

/****************************************************************************/

Display *GUI_display;
int GUI_screenidx;
Screen *GUI_screen;
Visual *GUI_visual;
XVisualInfo GUI_visual_template;
XVisualInfo *GUI_visual_info;

#endif /* !GUI_H_ */
