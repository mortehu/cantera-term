#ifdef  WIN32
#include <windows.h>
#endif

#include <png.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <GL/gl.h>

#include "error.h"
#include "draw.h"
#include "hashmap.h"
#include "texture.h"
#include "var.h"

#define MAX_TEXTURE_HANDLES 1024

struct TEXTURE_handle
{
  char* path;
  int width, height;
  GLuint handle;
};

static struct TEXTURE_handle TEXTURE_handles[MAX_TEXTURE_HANDLES];
static int TEXTURE_handle_count;
static struct hashmap* TEXTURE_hashmap;

#ifndef png_jmpbuf
#  define png_jmpbuf(png) ((png)->jmpbuf)
#endif

int texture_load(const char* path)
{
  FILE* f;
  png_bytep* rowPointers;
  png_structp png;
  png_infop pnginfo;
  png_uint_32 width, height;
  unsigned char* data;
  unsigned int row;
  int bit_depth, pixel_format, interlace_type;
  int i;

  if(!var_find("r_enable")->vfloat)
    return 0;

  if(!TEXTURE_hashmap)
    TEXTURE_hashmap = hashmap_create("textures");

  if(hashmap_has_key(TEXTURE_hashmap, path))
  {
    i = hashmap_get(TEXTURE_hashmap, path);

    return TEXTURE_handles[i].handle;
  }

  if(TEXTURE_handle_count == MAX_TEXTURE_HANDLES)
    fatal_error("Maximum number of textures (%d) reached", MAX_TEXTURE_HANDLES);

  f = fopen(path, "rb");

  if(!f)
    fatal_error("Failed to open '%s': %s", path, strerror(errno));

  png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if(png == NULL)
    fatal_error("png_create_read_struct failed");

  pnginfo = png_create_info_struct(png);

  if(pnginfo == NULL)
    fatal_error("png_create_info_struct failed");

  if(setjmp(png_jmpbuf(png)))
    fatal_error("PNG decoding failed (%s)", path);

  png_init_io(png, f);

  png_read_info(png, pnginfo);

  png_get_IHDR(png, pnginfo, &width, &height, &bit_depth, &pixel_format,
               &interlace_type, int_p_NULL, int_p_NULL);

  if(bit_depth != 8)
    fatal_error("Unsupported bit depth %d in %s", bit_depth, path);

  switch(pixel_format)
  {
  case PNG_COLOR_TYPE_RGB_ALPHA:

    break;

  default:

    fatal_error("Unsupported pixel format in %s", path);
  }

  data = malloc(height * width * 4);

  rowPointers = malloc(sizeof(png_bytep) * height);

  for(row = 0; row < height; ++row)
    rowPointers[row] = data + row * width * 4;

  png_read_image(png, rowPointers);

  free(rowPointers);

  png_read_end(png, pnginfo);

  png_destroy_read_struct(&png, &pnginfo, png_infopp_NULL);

  fclose(f);

  glGenTextures(1,  &TEXTURE_handles[TEXTURE_handle_count].handle);
  draw_bind_texture(TEXTURE_handles[TEXTURE_handle_count].handle);
  glTexImage2D(GL_TEXTURE_2D, 0, 4, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  TEXTURE_handles[TEXTURE_handle_count].path = strdup(path);
  TEXTURE_handles[TEXTURE_handle_count].width = width;
  TEXTURE_handles[TEXTURE_handle_count].height = height;

  hashmap_insert(TEXTURE_hashmap, path, TEXTURE_handle_count);

  free(data);

  return TEXTURE_handles[TEXTURE_handle_count++].handle;
}

void texture_size(int handle, int* width, int* height)
{
  int i;

  for(i = 0; i < TEXTURE_handle_count; ++i)
    if(TEXTURE_handles[i].handle == handle)
      break;

  assert(i < TEXTURE_handle_count);

  if(width)
    *width = TEXTURE_handles[i].width;

  if(height)
    *height = TEXTURE_handles[i].height;
}
