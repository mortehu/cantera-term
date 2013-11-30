#ifndef DRAW_H_
#define DRAW_H_ 1

#include <wchar.h>

#include "terminal.h"

void init_gl_30(void);

void draw_gl_30(const Terminal::State& state);

#endif /* !DRAW_H_ */
