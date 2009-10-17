/**
 * 2D drawing routines
 * Copyright (C) 2008  Morten Hustveit
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include "error.h"
#include "draw.h"
#include "texture.h"

#define MAX_BATCH_SIZE 1024

struct DRAW_quad
{
  int texture;
  uint32_t color;
  float x, y;
  float width, height;
  float u0, v0;
  float u1, v1;
};

static struct DRAW_quad DRAW_quads[MAX_BATCH_SIZE];
static size_t DRAW_quad_count;

struct DRAW_sprite_atlas
{
  GLuint texture;

  uint16_t* bottom;
};

static struct DRAW_sprite_atlas* DRAW_atlases;
static size_t DRAW_atlas_count;

static GLint DRAW_atlas_size = 0;

static int DRAW_current_texture;
static unsigned int DRAW_current_color = 0xffffffff;

void draw_bind_texture(int texture)
{
  if(texture != DRAW_current_texture)
  {
    glBindTexture(GL_TEXTURE_2D, texture);

    DRAW_current_texture = texture;
  }
}

void draw_set_color(unsigned int color)
{
  DRAW_current_color = color;
}

void draw_quad(int texture, float x, float y, float width, float height)
{
  size_t i;

  if(DRAW_quad_count == MAX_BATCH_SIZE)
    draw_flush();

  i = DRAW_quad_count++;
  DRAW_quads[i].texture = texture;
  DRAW_quads[i].color = DRAW_current_color;
  DRAW_quads[i].x = x;
  DRAW_quads[i].y = y;
  DRAW_quads[i].width = width;
  DRAW_quads[i].height = height;
  DRAW_quads[i].u0 = 0.0f;
  DRAW_quads[i].v0 = 0.0f;
  DRAW_quads[i].u1 = 1.0f;
  DRAW_quads[i].v1 = 1.0f;
}

void draw_quad_st(int texture, float x, float y, float width, float height,
                  float s0, float t0, float s1, float t1)
{
  size_t i;

  if(DRAW_quad_count == MAX_BATCH_SIZE)
    draw_flush();

  i = DRAW_quad_count++;
  DRAW_quads[i].texture = texture;
  DRAW_quads[i].color = DRAW_current_color;
  DRAW_quads[i].x = x;
  DRAW_quads[i].y = y;
  DRAW_quads[i].width = width;
  DRAW_quads[i].height = height;
  DRAW_quads[i].u0 = s0;
  DRAW_quads[i].v0 = t0;
  DRAW_quads[i].u1 = s1;
  DRAW_quads[i].v1 = t1;
}

void DRAW_alloc_atlas()
{
  size_t i = DRAW_atlas_count++;

  DRAW_atlases = realloc(DRAW_atlases, sizeof(struct DRAW_sprite_atlas) * DRAW_atlas_count);

  glGenTextures(1, &DRAW_atlases[i].texture);

  if(!DRAW_atlas_size)
  {
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &DRAW_atlas_size);

    info("Max texture size: %d", DRAW_atlas_size);
  }

  draw_bind_texture(DRAW_atlases[i].texture);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DRAW_atlas_size, DRAW_atlas_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

  DRAW_atlases[i].bottom = calloc(sizeof(uint16_t), DRAW_atlas_size);
}

/**
 * bottom + quadratic search: 2.2s
 */

void draw_add_sprite(struct sprite* target, const void* data,
                     unsigned int width, unsigned int height, int flags)
{
  struct DRAW_sprite_atlas* atlas = 0;
  unsigned int j, k;
  unsigned int x = 0, y = 0;

  unsigned int best_max = DRAW_atlas_size;
  unsigned int best_max_x = 0;

  if(DRAW_atlas_count)
  {
    atlas = &DRAW_atlases[DRAW_atlas_count - 1];

    for(j = 0; j < DRAW_atlas_size - width + 1; ++j)
    {
      unsigned int max = atlas->bottom[j];

      for(k = 1; k < width && max < best_max; ++k)
      {
        if(atlas->bottom[j + k] > max)
          max = atlas->bottom[j + k];
      }

      if(max < best_max)
      {
        best_max = max;
        best_max_x = j;
      }
    }
  }

  if(best_max + height <= DRAW_atlas_size)
  {
    x = best_max_x;
    y = best_max;

    assert(x + width <= DRAW_atlas_size);
  }
  else
  {
    DRAW_alloc_atlas();

    atlas = &DRAW_atlases[DRAW_atlas_count - 1];
    x = 0;
    y = 0;
  }

  for(j = 0; j < width; ++j)
    atlas->bottom[x + j] = y + height;

  target->texture = atlas->texture;
  target->width = width;
  target->height = height;
  target->u0 = (float) x / DRAW_atlas_size;
  target->v0 = (float) y / DRAW_atlas_size;
  target->u1 = (float) (x + width) / DRAW_atlas_size;
  target->v1 = (float) (y + height) / DRAW_atlas_size;

  draw_bind_texture(target->texture);

  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

void draw_sprite(struct sprite* sprite, float x, float y)
{
  size_t i;

  if(DRAW_quad_count == MAX_BATCH_SIZE)
    draw_flush();

  i = DRAW_quad_count++;
  DRAW_quads[i].texture = sprite->texture;
  DRAW_quads[i].color = DRAW_current_color;
  DRAW_quads[i].x = x;
  DRAW_quads[i].y = y;
  DRAW_quads[i].width = sprite->width;
  DRAW_quads[i].height = sprite->height;
  DRAW_quads[i].u0 = sprite->u0;
  DRAW_quads[i].v0 = sprite->v0;
  DRAW_quads[i].u1 = sprite->u1;
  DRAW_quads[i].v1 = sprite->v1;
}

void draw_flush()
{
  size_t i;

  for(i = 0; i < DRAW_quad_count; ++i)
  {
    struct DRAW_quad* quad = &DRAW_quads[i];

    if(DRAW_current_texture != quad->texture)
    {
      if(i)
      {
        glEnd();
        glBindTexture(GL_TEXTURE_2D, quad->texture);
        glBegin(GL_QUADS);
      }
      else
        glBindTexture(GL_TEXTURE_2D, quad->texture);

      DRAW_current_texture = quad->texture;
    }

    if(!i)
      glBegin(GL_QUADS);

    glColor4ub(quad->color >> 16,
               quad->color >> 8,
               quad->color,
               quad->color >> 24);

    glTexCoord2f(quad->u0, quad->v0); glVertex2f(quad->x, quad->y);
    glTexCoord2f(quad->u0, quad->v1); glVertex2f(quad->x, quad->y + quad->height);
    glTexCoord2f(quad->u1, quad->v1); glVertex2f(quad->x + quad->width, quad->y + quad->height);
    glTexCoord2f(quad->u1, quad->v0); glVertex2f(quad->x + quad->width, quad->y);
  }

  glEnd();

  DRAW_quad_count = 0;
}
