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

  Terminal terminal;
  terminal.Init(800, 500, 10, 20, 0);

  for (size_t i = 0; i < 100000; ++i) {
    switch (rand() & 3) {
      case 0: {
        size_t index =
            rand() % (sizeof(escape_codes) / sizeof(escape_codes[0]));
        terminal.ProcessData(escape_codes[index], strlen(escape_codes[index]));
      } break;
      case 1: {
        char buffer[64];
        strcpy(buffer, "\033[");
        if (rand() & 1) {
          sprintf(&buffer[2], "%u%c", rand() % 4096, rand());
        } else {
          sprintf(&buffer[2], "%c", rand());
        }
        terminal.ProcessData(buffer, strlen(buffer));
      } break;
      case 2: {
        char buffer[64];
        for (size_t j = 0; j < sizeof(buffer); ++j)
          buffer[j] = rand();

        terminal.ProcessData(buffer, sizeof(buffer));
      }
    }
  }

  return EXIT_SUCCESS;
}
