#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>
#include <utmp.h>
#include <wchar.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include "base/string.h"
#include "command.h"
#include "draw.h"
#include "expr-parse.h"
#include "font.h"
#include "glyph.h"
#include "terminal.h"
#include "tree.h"
#include "x11.h"

namespace {

int print_version;
int print_help;

struct option long_options[] = {{"version", no_argument, &print_version, 1},
                                {"help", no_argument, &print_help, 1},
                                {0, 0, 0, 0}};

std::unique_ptr<tree> config;
bool hidden;

unsigned int scroll_extra;

const char* font_name;
unsigned int font_size, font_weight;
FONT_Data* font;

unsigned int palette[16];

int done;
int home_fd;

std::unique_ptr<Terminal> terminal;

std::string primary_selection;
std::string clipboard_text;

std::string last_expression, expression_result;
std::string::size_type expression_offset;

bool clear;
std::mutex clear_mutex;
std::condition_variable clear_cond;

pid_t pid;
int terminal_fd;

std::mutex buffer_mutex;

// Last pressed key.
KeySym prev_key_sym = 0;

std::unordered_map<KeyInfo, void (*)(XKeyEvent* event)> key_callbacks;

Terminal::State draw_state;

}  // namespace

static void LoadGlyph(wchar_t character) {
  FONT_Glyph* glyph;

  if (!(glyph = FONT_GlyphForCharacter(font, character)))
    fprintf(stderr, "Failed to get glyph for '%d'", character);

  GLYPH_Add(character, glyph);

  free(glyph);
}

static void CreateLineArtGlyphs(void) {
  FONT_Glyph* glyph =
      FONT_GlyphWithSize(FONT_SpaceWidth(font), FONT_LineHeight(font));
  unsigned int ascent = FONT_Ascent(font);
  glyph->x = 0;
  glyph->y = ascent;
  glyph->xOffset = glyph->width;
  glyph->yOffset = 0;

  unsigned int mid_x = glyph->width / 2;
  unsigned int mid_y = glyph->height / 2;
  unsigned int x, y;

#define CREATE_GLYPH(code, expr)                             \
  do {                                                       \
    for (y = 0; y < glyph->height; ++y) {                    \
      for (x = 0; x < glyph->width; ++x) {                   \
        unsigned char color = (expr);                        \
        glyph->data[(y * glyph->width + x) * 4 + 0] = color; \
        glyph->data[(y * glyph->width + x) * 4 + 1] = color; \
        glyph->data[(y * glyph->width + x) * 4 + 2] = color; \
        glyph->data[(y * glyph->width + x) * 4 + 3] = 255;   \
      }                                                      \
    }                                                        \
    GLYPH_Add(code, glyph);                                  \
  } while (0)

  CREATE_GLYPH(0x2500, (y == mid_y) * 255);  // '─'
  CREATE_GLYPH(0x2502, (x == mid_x) * 255);  // '│'
  CREATE_GLYPH(
      0x250c,
      ((y == mid_y && x >= mid_x) || (x == mid_x && y >= mid_y)) * 255);  // '┌'
  CREATE_GLYPH(
      0x2510,
      ((y == mid_y && x <= mid_x) || (x == mid_x && y >= mid_y)) * 255);  // '┐'
  CREATE_GLYPH(
      0x2514,
      ((y == mid_y && x >= mid_x) || (x == mid_x && y <= mid_y)) * 255);  // '└'
  CREATE_GLYPH(
      0x2518,
      ((y == mid_y && x <= mid_x) || (x == mid_x && y <= mid_y)) * 255);  // '┘'
  CREATE_GLYPH(0x251c,
               ((y == mid_y && x >= mid_x) || x == mid_x) * 255);  // '├'
  CREATE_GLYPH(0x2524,
               ((y == mid_y && x <= mid_x) || x == mid_x) * 255);  // '┤'
  CREATE_GLYPH(0x2592, 192);                                       // '▒'

#undef CREATE_GLYPH

  free(glyph);
}

static void UpdateSelection(Time time) {
  primary_selection = terminal->GetSelection();

  XSetSelectionOwner(X11_display, XA_PRIMARY, X11_window, time);

  if (X11_window != XGetSelectionOwner(X11_display, XA_PRIMARY)) {
    /* We did not get the selection */
    terminal->ClearSelection();
    primary_selection.clear();
  }
}

static void send_selection(XSelectionRequestEvent* request, const char* text,
                           size_t length) {
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
    const Atom targets[] = {XA_STRING, xa_utf8_string};

    XChangeProperty(X11_display, request->requestor, request->property, XA_ATOM,
                    32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(targets),
                    sizeof(targets) / sizeof(targets[0]));
  } else if (request->target == XA_STRING ||
             request->target == xa_utf8_string) {
    ret = XChangeProperty(X11_display, request->requestor, request->property,
                          request->target, 8, PropModeReplace,
                          reinterpret_cast<const unsigned char*>(text), length);

    if (ret != BadAlloc && ret != BadAtom && ret != BadValue &&
        ret != BadWindow)
      response.property = request->property;
  } else {
    fprintf(stderr, "Unknown selection request target: %s\n",
            XGetAtomName(X11_display, request->target));
  }

  XSendEvent(request->display, request->requestor, False, NoEventMask,
             (XEvent*)&response);
}

static void paste(Atom selection, Time time) {
  XConvertSelection(X11_display, selection, xa_utf8_string, selection,
                    X11_window, time);
}

void X11_handle_configure(void) {
  static unsigned int configured_width, configured_height;
  /* Resize event -- create new buffers and copy+clip old data */

  if (X11_window_width == configured_width &&
      X11_window_height == configured_height)
    return;
  configured_width = X11_window_width;
  configured_height = X11_window_height;

  glViewport(0, 0, X11_window_width, X11_window_height);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0f, X11_window_width, X11_window_height, 0.0f, 0.0f, 1.0f);

  {
    const auto line_height = FONT_LineHeight(font);
    std::lock_guard<std::mutex> buffer_lock(buffer_mutex);
    terminal->Resize(X11_window_width, X11_window_height,
                    FONT_SpaceWidth(font), line_height);
  }

  ioctl(terminal_fd, TIOCSWINSZ, &terminal->Size());
}

void X11ClearThread() {
  for (;;) {
    std::unique_lock<std::mutex> lock(clear_mutex);
    clear_cond.wait(lock, [] { return clear; });
    clear = false;

    XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
    XFlush(X11_display);
  }
}

void X11_Clear(void) {
  if (hidden) return;

  std::lock_guard<std::mutex> lock(clear_mutex);
  clear = true;
  clear_cond.notify_one();
}

static void TTYReadThread() {
  unsigned char buf[4096];
  ssize_t result;
  size_t fill = 0;
  struct pollfd pfd;

  pfd.fd = terminal_fd;
  pfd.events = POLLIN | POLLRDHUP;

  for (;;) {
    if (-1 == poll(&pfd, 1, -1)) {
      if (errno == EINTR) continue;

      break;
    }

    if (pfd.revents & POLLRDHUP) break;

    // Read until EAGAIN/EWOULDBLOCK.
    while (0 < (result = read(terminal_fd, buf + fill, sizeof(buf) - fill))) {
      fill += result;
      if (fill == sizeof(buf)) break;
    }

    if (result == -1 && errno != EAGAIN && errno != EWOULDBLOCK) break;

    if (!buffer_mutex.try_lock()) {
      // We couldn't get the lock straight away.  If the buffer is not full,
      // and we can get more data within 1 ms, go ahead and read that.
      if (fill < sizeof(buf) && 0 < poll(&pfd, 1, 1)) continue;

      // No new data available, go ahead and paint.
      buffer_mutex.lock();
    }

    terminal->ProcessData(buf, fill);
    fill = 0;
    buffer_mutex.unlock();

    X11_Clear();
  }

  done = 1;

  X11_Clear();
}

static void WriteToTTY(const void* data, size_t len) {
  size_t off = 0;
  ssize_t result;

  while (off < len) {
    result = write(terminal_fd, reinterpret_cast<const char*>(data) + off,
                   len - off);

    if (result < 0) {
      done = 1;

      break;
    }

    off += result;
  }
}

static void WriteStringToTTY(const char* string) {
  WriteToTTY(string, strlen(string));
}

static bool WaitForDeadChildren() {
  pid_t child_pid;
  int status;

  while (0 < (child_pid = waitpid(-1, &status, WNOHANG))) {
    if (child_pid == pid)
      return true;
  }

  return false;
}

void HandleKeyPress(KeySym key_sym, const char* text, size_t len,
                    unsigned int modifier_mask, XEvent* event,
                    bool& history_scroll_reset) {
  if (key_sym == XK_Control_L || key_sym == XK_Control_R)
    modifier_mask |= ControlMask, history_scroll_reset = false;

  if (key_sym == XK_Alt_L || key_sym == XK_Alt_R || key_sym == XK_Shift_L ||
      key_sym == XK_Shift_R)
    history_scroll_reset = false;

  /* Hack for keyboards with no menu key; remap two consecutive
   * taps of R-Control to Menu */
  if (key_sym == XK_Control_R && prev_key_sym == XK_Control_R) {
    key_sym = XK_Menu;
    modifier_mask &= ~ControlMask;
  }

  if ((modifier_mask & ShiftMask) && key_sym == XK_Up) {
    history_scroll_reset = false;

    if (terminal->history_scroll < scroll_extra) {
      ++terminal->history_scroll;
      XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
    }
  } else if ((modifier_mask & ShiftMask) && key_sym == XK_Down) {
    history_scroll_reset = false;

    if (terminal->history_scroll) {
      --terminal->history_scroll;
      XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
    }
  } else if ((modifier_mask & ShiftMask) && key_sym == XK_Page_Up) {
    history_scroll_reset = false;

    terminal->history_scroll += terminal->Size().ws_row;

    if (terminal->history_scroll > scroll_extra)
      terminal->history_scroll = scroll_extra;

    XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
  } else if ((modifier_mask & ShiftMask) && key_sym == XK_Page_Down) {
    history_scroll_reset = false;

    if (terminal->history_scroll > terminal->Size().ws_row)
      terminal->history_scroll -= terminal->Size().ws_row;
    else
      terminal->history_scroll = 0;

    XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
  } else if ((modifier_mask & ShiftMask) && key_sym == XK_Home) {
    history_scroll_reset = false;

    if (terminal->history_scroll != scroll_extra) {
      terminal->history_scroll = scroll_extra;

      XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
    }
  } else {
    auto handler = key_callbacks.find(
        KeyInfo(key_sym, modifier_mask & (ControlMask | ShiftMask)));

    if (handler != key_callbacks.end()) {
      handler->second(&event->xkey);
    } else if (len) {
      if ((modifier_mask & Mod1Mask)) WriteStringToTTY("\033");

      if (len == 1 && (text[0] == ('S' & 0x3f) || text[0] == ('Q' && 0x3f)))
        history_scroll_reset = false;

      WriteToTTY(text, len);
    }
  }
}

void SetupKeyCallbacks() {
#define MAP_KEY_TO_STRING(keysym, string)                  \
  key_callbacks[keysym] = [](XKeyEvent* event) {           \
    if (event->state & Mod1Mask) WriteStringToTTY("\033"); \
    WriteStringToTTY((string));                            \
  };

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
  MAP_KEY_TO_STRING(XK_Home, terminal->appcursor ? "\033OH" : "\033[H");
  MAP_KEY_TO_STRING(XK_End, terminal->appcursor ? "\033OF" : "\033[F");
  MAP_KEY_TO_STRING(XK_Page_Up, "\033[5~");
  MAP_KEY_TO_STRING(XK_Page_Down, "\033[6~");
  MAP_KEY_TO_STRING(XK_Up, terminal->appcursor ? "\033OA" : "\033[A");
  MAP_KEY_TO_STRING(XK_Down, terminal->appcursor ? "\033OB" : "\033[B");
  MAP_KEY_TO_STRING(XK_Right, terminal->appcursor ? "\033OC" : "\033[C");
  MAP_KEY_TO_STRING(XK_Left, terminal->appcursor ? "\033OD" : "\033[D");

#undef MAP_KEY_TO_STRING

  /* Map Ctrl-Left/Right to Home/End */
  key_callbacks[KeyInfo(XK_Left, ControlMask)] = key_callbacks[XK_Home];
  key_callbacks[KeyInfo(XK_Right, ControlMask)] = key_callbacks[XK_End];

  key_callbacks[XK_Menu] = [](XKeyEvent* event) {
    if (!expression_result.empty() && expression_result != last_expression) {
      std::string text(last_expression.size() - expression_offset, '\b');
      text.insert(text.end(), expression_result.begin(),
                  expression_result.end());
      WriteToTTY(text.data(), text.length());
    }
  };

  /* Clipboard handling */
  key_callbacks[KeyInfo(XK_C, ControlMask | ShiftMask)] = [](XKeyEvent* event) {
    if (!primary_selection.empty()) {
      clipboard_text = primary_selection;

      XSetSelectionOwner(X11_display, xa_clipboard, X11_window, event->time);
    }
  };
  key_callbacks[KeyInfo(XK_Insert, ControlMask | ShiftMask)] =
      key_callbacks[KeyInfo(XK_V, ControlMask | ShiftMask)] =
          [](XKeyEvent* event) { paste(xa_clipboard, event->time); };
  key_callbacks[KeyInfo(XK_Insert, ShiftMask)] = [](XKeyEvent* event) {
    paste(XA_PRIMARY, event->time);
  };

  /* Suppress output from some keys */
  key_callbacks[XK_Shift_L] = key_callbacks[XK_Shift_R] =
      key_callbacks[XK_ISO_Prev_Group] =
          key_callbacks[XK_ISO_Next_Group] = [](XKeyEvent* event) {};
}

int x11_process_events() {
  XEvent event;
  int result;

  SetupKeyCallbacks();

  while (!done && !WaitForDeadChildren()) {
    XNextEvent(X11_display, &event);

    if (XFilterEvent(&event, X11_window)) continue;

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
          bool history_scroll_reset = true;
          unsigned int modifier_mask = event.xkey.state;

          len = Xutf8LookupString(X11_xic, &event.xkey, text, sizeof(text) - 1,
                                  &key_sym, &status);

          if (!text[0]) len = 0;

          HandleKeyPress(key_sym, text, len, modifier_mask, &event,
                         history_scroll_reset);

          if (history_scroll_reset && terminal->history_scroll) {
            terminal->history_scroll = 0;
            XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
          }

          prev_key_sym = key_sym;
        }

        break;

      case MotionNotify:

        if (event.xbutton.state & Button1Mask) {
          int x, y;
          unsigned int size;

          size = terminal->history_size * terminal->Size().ws_col;

          x = std::max(0,
                       std::min(terminal->Size().ws_col - 1,
                                event.xbutton.x /
                                    static_cast<int>(FONT_SpaceWidth(font))));
          y = std::max(0,
                       std::min(terminal->Size().ws_row - 1,
                                event.xbutton.y /
                                    static_cast<int>(FONT_LineHeight(font))));

          size_t new_select_end = y * terminal->Size().ws_col + x;

          if (terminal->history_scroll)
            new_select_end +=
                size - (terminal->history_scroll * terminal->Size().ws_col);

          if (event.xbutton.state & ControlMask)
            terminal->FindRange(Terminal::kRangeWordOrURL,
                               &terminal->select_begin, &new_select_end);

          if (new_select_end != terminal->select_end) {
            terminal->select_end = new_select_end;

            XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
          }
        }

        break;

      case ButtonPress:

        XSetInputFocus(X11_display, X11_window, RevertToParent,
                       event.xkey.time);

        switch (event.xbutton.button) {
          case 1: {
            // Left button.
            primary_selection.clear();

            size_t size = terminal->history_size * terminal->Size().ws_col;

            int x =
                std::min(static_cast<unsigned int>(terminal->Size().ws_col) - 1,
                         std::max(0U, event.xbutton.x / FONT_SpaceWidth(font)));
            int y =
                std::min(static_cast<unsigned int>(terminal->Size().ws_row) - 1,
                         std::max(0U, event.xbutton.y / FONT_LineHeight(font)));

            terminal->select_begin = y * terminal->Size().ws_col + x;

            if (terminal->history_scroll) {
              terminal->select_begin +=
                  size - (terminal->history_scroll * terminal->Size().ws_col);
            }

            terminal->select_end = terminal->select_begin;

            if (event.xbutton.state & ControlMask) {
              terminal->FindRange(Terminal::kRangeWordOrURL,
                                 &terminal->select_begin, &terminal->select_end);
            } else if (event.xbutton.state & ShiftMask) {
              terminal->FindRange(Terminal::kRangeLine,
                                 &terminal->select_begin, &terminal->select_end);
            }

            XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
          } break;

          case 2: /* Middle button */

            paste(XA_PRIMARY, event.xbutton.time);

            break;

          case 4: /* Up */

            if (terminal->history_scroll < scroll_extra) {
              ++terminal->history_scroll;
              XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
            }

            break;

          case 5: /* Down */

            if (terminal->history_scroll) {
              --terminal->history_scroll;
              XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);
            }

            break;
        }

        break;

      case ButtonRelease:

        /* Left button */
        if (event.xbutton.button == 1) {
          UpdateSelection(event.xbutton.time);

          if (!primary_selection.empty() && (event.xkey.state & Mod1Mask))
            Command(home_fd, "open-url").AddArg(primary_selection).Run();
        }

        break;

      case SelectionRequest: {
        XSelectionRequestEvent* request = &event.xselectionrequest;

        if (request->property == None) request->property = request->target;

        if (request->selection == XA_PRIMARY) {
          if (!primary_selection.empty())
            send_selection(request, primary_selection.data(),
                           primary_selection.size());
        } else if (request->selection == xa_clipboard) {
          if (!clipboard_text.empty())
            send_selection(request, clipboard_text.data(),
                           clipboard_text.size());
        }
      } break;

      case SelectionNotify: {
        Atom selection;
        Atom type;
        int format;
        unsigned long nitems;
        unsigned long bytes_after;
        unsigned char* prop;

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

        /* Remove trailing newlines.  */
        while (nitems > 0 && prop[nitems - 1] == '\n') --nitems;

        WriteToTTY(prop, nitems);

        XFree(prop);
      } break;

      case SelectionClear:

        if (event.xselectionclear.selection == XA_PRIMARY)
          terminal->ClearSelection();

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

      case Expose: {
        /* Skip to last Expose event */
        while (XCheckTypedWindowEvent(X11_display, X11_window, Expose, &event))
          ; /* Do nothing */

        std::string new_expression;

        {
          std::lock_guard<std::mutex> buffer_lock(buffer_mutex);
          if (!primary_selection.empty() &&
              primary_selection != terminal->GetSelection())
            terminal->ClearSelection();

          terminal->GetState(&draw_state);

          new_expression = terminal->GetCurrentLine(true);
        }

        if (new_expression != last_expression) {
          // TODO(mortehu): Move processing to a separate thread.
          expression_result.clear();
          expression::ParseContext::FindAndEval(
              new_expression, &expression_offset, &expression_result,
              expression::ParseContext::kIgnoreTrivial,
              &draw_state);
          last_expression = new_expression;
        } else if (new_expression.empty()) {
          last_expression.clear();
          expression_result.clear();
        }

        if (!expression_result.empty())
          draw_state.cursor_hint = expression_result;

        draw_gl_30(draw_state, font);
      } break;

      case EnterNotify: {
        const XEnterWindowEvent* ewe;

        ewe = (XEnterWindowEvent*)&event;

        if (!ewe->focus || ewe->detail == NotifyInferior) break;

        /* Fall through to FocusIn */
      }

      case FocusIn:

        terminal->focused = true;
        XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);

        break;

      case LeaveNotify: {
        const XLeaveWindowEvent* lwe;

        lwe = (XEnterWindowEvent*)&event;

        if (!lwe->focus || lwe->detail == NotifyInferior) break;

        /* Fall through to FocusOut */
      }

      case FocusOut:

        terminal->focused = false;
        XClearArea(X11_display, X11_window, 0, 0, 0, 0, True);

        prev_key_sym = 0;

        break;
    }
  }

  return 0;
}

static void StartSubprocess(int argc, char** argv) {
  std::vector<std::string> command_line;

  if (optind < argc) {
    for (int i = optind; i < argc; ++i) command_line.push_back(argv[i]);
  } else {
    command_line.push_back("/bin/bash");
  }

  std::vector<const char*> c_command_line;
  for (const std::string& str : command_line)
    c_command_line.push_back(str.c_str());
  c_command_line.push_back(nullptr);

  if (-1 == (pid = forkpty(&terminal_fd, nullptr, nullptr, &terminal->Size())))
    err(EX_OSERR, "forkpty() failed");

  if (!pid) {
    /* In child process */
    execvpe(c_command_line[0], const_cast<char* const*>(&c_command_line[0]),
            environ);

    fprintf(stderr, "Failed to execute '%s'", c_command_line[0]);

    _exit(EXIT_FAILURE);
  }
}

int main(int argc, char** argv) {
  int i;
  const char* home;
  char *palette_str, *token;

  setlocale(LC_ALL, "en_US.UTF-8");

  while ((i = getopt_long(argc, argv, "", long_options, 0)) != -1) {
    switch (i) {
      case 0:
        break;

      case '?':

        fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);

        return EXIT_FAILURE;
    }
  }

  if (print_help) {
    printf(
        "Usage: %s [OPTION]... [COMMAND [ARGUMENT]...]\n"
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

  if (!(home = getenv("HOME")))
    errx(EXIT_FAILURE, "HOME environment variable missing");

  if (-1 == (home_fd = open(home, O_RDONLY | O_CLOEXEC)))
    err(EXIT_FAILURE, "Failed to open HOME directory");

  mkdirat(home_fd, ".cantera", 0777);
  mkdirat(home_fd, ".cantera/commands", 0777);

  config.reset(tree_load_cfg(home_fd, ".cantera/config"));

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

  X11_window_width = tree_get_integer_default(config.get(), "terminal.width", 800);
  X11_window_height = tree_get_integer_default(config.get(), "terminal.height", 600);

  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

  setenv("TERM", "xterm", 1);

  X11_Setup();

  FONT_Init();
  GLYPH_Init();

  if (!(font = FONT_Load(font_name, font_size, font_weight)))
    errx(EXIT_FAILURE, "Failed to load font `%s' of size %u, weight %u",
         font_name, font_size, font_weight);

  // Preload the most important glyphs, which will be uploaded to OpenGL in a
  // single batch.
  for (i = '!'; i <= '~'; ++i) LoadGlyph(i);
  for (i = 0xa1; i <= 0xff; ++i) LoadGlyph(i);
  CreateLineArtGlyphs();

  terminal.reset(new Terminal(WriteToTTY));

  for (i = 0; i < 16; ++i) {
    terminal->SetANSIColor(
        i, Terminal::Color(palette[i] >> 16, palette[i] >> 8, palette[i]));
  }

  terminal->Init(X11_window_width, X11_window_height, FONT_SpaceWidth(font),
                 FONT_LineHeight(font), scroll_extra);

  StartSubprocess(argc, argv);

  fcntl(terminal_fd, F_SETFL, O_NDELAY);

  X11_handle_configure();

  init_gl_30();

  std::thread(TTYReadThread).detach();
  std::thread(X11ClearThread).detach();

  if (-1 == x11_process_events()) return EXIT_FAILURE;

  _Exit(EXIT_SUCCESS);
}
