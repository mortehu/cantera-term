#ifndef DRAW_H_
#define DRAW_H_ 1

struct sprite
{
  int texture;
  float u0, v0;
  float u1, v1;

  unsigned int width, height;
};

#define DRAW_SPRITE_DYNAMIC 0x0001

void draw_bind_texture(int texture);

void draw_set_color(unsigned int color);

void draw_quad(int texture, float x, float y, float width, float height);

void draw_quad_st(int texture, float x, float y, float width, float height,
                  float s0, float t0, float s1, float t1);

void draw_add_sprite(struct sprite* target, const void* data,
                     unsigned int width, unsigned int height,
                     int flags);

void draw_discard_dynamic_sprites();

void draw_sprite(struct sprite* sprite, float x, float y);

void draw_flush();

#endif /* !DRAW_H_ */
