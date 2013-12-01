#include <assert.h>
#include <stdlib.h>

#include "terminal.h"

static const char* escape_codes[] = {
  "\033E", "\033D", "\033M", "\033H", "\033Z", "\0337", "\0338", "\033[s",
  "\033[u", "\033c", "\033g", "\0336p", "\0337p", "\033=", "\033>", "\033#8",
  "\033^", "\033!", "\033k", "\033P", "\033_", "\033]0;title\007",
  "\033]83;cmd\007", "\016", "\017", "\033n", "\033o", "\033N", "\033O",
  "\033(0", "\033(B", "\033)0", "\033)B", "\033*0", "\033*B", "\033+0",
  "\033+B",
};

int main(int argc, char** argv) {
  srand(time(NULL));

  Terminal terminal_noscroll, terminal_scroll;
  terminal_noscroll.Init(800, 500, 10, 20, 0);
  terminal_scroll.Init(800, 500, 10, 20, 1001);

  for (size_t i = 0; i < 10000; ++i) {
    char buffer[64];
    switch (rand() & 3) {
      case 0: {
        size_t index =
            rand() % (sizeof(escape_codes) / sizeof(escape_codes[0]));
        strcpy(buffer, escape_codes[index]);
      } break;
      case 1: {
        strcpy(buffer, "\033[");
        if (rand() & 1) {
          sprintf(&buffer[2], "%u%c", rand() % 4096, rand());
        } else {
          sprintf(&buffer[2], "%c", rand());
        }
      } break;
      case 2: {
        for (size_t j = 0; j < sizeof(buffer) - 1; ++j)
          buffer[j] = rand();
        buffer[sizeof(buffer) - 1] = 0;
      }
    }
    terminal_noscroll.ProcessData(buffer, strlen(buffer));
    terminal_scroll.ProcessData(buffer, strlen(buffer));

    Terminal::State state_noscroll, state_scroll;
    terminal_noscroll.GetState(&state_noscroll);
    terminal_scroll.GetState(&state_scroll);

    assert(state_noscroll.width == state_scroll.width);
    assert(state_noscroll.height == state_scroll.height);
    assert(state_noscroll.cursor_x == state_scroll.cursor_x);
    assert(state_noscroll.cursor_y == state_scroll.cursor_y);
    assert(state_noscroll.cursor_hidden == state_scroll.cursor_hidden);
    assert(state_noscroll.focused == state_scroll.focused);
    assert(!memcmp(&state_noscroll.chars[0], &state_scroll.chars[0],
                   sizeof(state_noscroll.chars[0]) * state_noscroll.width *
                       state_noscroll.height));
    assert(!memcmp(&state_noscroll.attrs[0], &state_scroll.attrs[0],
                   sizeof(state_noscroll.attrs[0]) * state_noscroll.width *
                       state_noscroll.height));
  }

  return EXIT_SUCCESS;
}
