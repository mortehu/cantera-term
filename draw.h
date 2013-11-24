#ifndef DRAW_H_
#define DRAW_H_ 1

#include "terminal.h"

#ifdef __cplusplus
extern "C" {
#endif

void init_gl_30(void);

void draw_gl_30(struct terminal *t);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !DRAW_H_ */
