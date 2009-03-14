#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#include <X11/extensions/Xrender.h>
#include <X11/xpm.h>

#include <png.h>

#include "common.h"

void run_command(int fd, const char* command, const char* arg)
{
  char path[4096];
  sprintf(path, ".potty/commands/%s", command);

  if(-1 == access(path, X_OK))
    sprintf(path, PKGDATADIR "/commands/%s", command);

  if(-1 == access(path, X_OK))
    return;

  if(!fork())
  {
    char* args[3];

    if(fd != -1)
      dup2(fd, 1);

    args[0] = path;
    args[1] = (char*) arg;
    args[2] = 0;

    execve(args[0], args, environ);

    exit(EXIT_FAILURE);
  }
}

void init_ximage(XImage* image, int width, int height, void* data)
{
    memset(image, 0, sizeof(XImage));
    image->width = width;
    image->height = height;
    image->format = ZPixmap;
    image->data = (char*) data;
#if __BYTE_ORDER == __LITTLE_ENDIAN
    image->byte_order = LSBFirst;
    image->bitmap_bit_order = LSBFirst;
#else
    image->byte_order = MSBFirst;
    image->bitmap_bit_order = MSBFirst;
#endif
    image->bitmap_unit = 32;
    image->bitmap_pad = 32;
    image->depth = 32;
    image->bytes_per_line = width * 4;
    image->bits_per_pixel = 32;
}

static int xpm_load(const char* path, struct picture* pic)
{
  Picture image, mask = None, result = None;
  Pixmap result_pixmap;
  Pixmap image_pixmap = 0;
  Pixmap mask_pixmap = 0;
  XpmAttributes attr;
  XRenderColor clear;

  memset(&attr, 0, sizeof(attr));

  if(-1 == XReadPixmapFile(display, window, (char*) path, &image_pixmap, &mask_pixmap, &attr))
    return -1;

  result_pixmap = XCreatePixmap(display, window, attr.width, attr.height, 32);
  result = XRenderCreatePicture(display, result_pixmap, XRenderFindStandardFormat(display, PictStandardARGB32), 0, 0);
  XFreePixmap(display, result_pixmap);

  image = XRenderCreatePicture(display, image_pixmap, XRenderFindStandardFormat(display, PictStandardRGB24), 0, 0);
  XFreePixmap(display, image_pixmap);

  if(mask_pixmap)
  {
    mask = XRenderCreatePicture(display, mask_pixmap, XRenderFindStandardFormat(display, PictStandardA1), 0, 0);
    XFreePixmap(display, mask_pixmap);
  }

  memset(&clear, 0, sizeof(clear));
  XRenderFillRectangle(display, PictOpSrc, result, &clear, 0, 0, attr.width, attr.height);

  XRenderComposite(display, PictOpOver, image, mask, result, 0, 0, 0, 0, 0, 0, attr.width, attr.height);

  pic->pic = result;
  pic->width = attr.width;
  pic->height = attr.height;

  return 0;
}

static int png_load(const char* path, struct picture* pic)
{
  Picture result = None;
  FILE* f;
  png_bytep* rowPointers;
  png_structp png;
  png_infop pnginfo;
  png_uint_32 width, height;
  unsigned char* data;
  unsigned int row;
  int bit_depth, pixel_format, interlace_type;

  f = fopen(path, "rb");

  if(!f)
  {
    char buf[256];

    fprintf(stderr, "Failed to open '%s': %s (cwd=%s)\n", path, strerror(errno), getcwd(buf, sizeof(buf)));

    return -1;
  }

  png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if(png == NULL)
  {
    fprintf(stderr, "png_create_read_struct failed\n");

    return -1;
  }

  pnginfo = png_create_info_struct(png);

  if(pnginfo == NULL)
  {
    fprintf(stderr, "png_create_info_struct failed\n");

    return -1;
  }

  if(setjmp(png_jmpbuf(png)))
  {
    fprintf(stderr, "PNG decoding failed (%s)\n", path);

    return -1;
  }

  png_init_io(png, f);

  png_read_info(png, pnginfo);

  png_get_IHDR(png, pnginfo, &width, &height, &bit_depth, &pixel_format,
               &interlace_type, int_p_NULL, int_p_NULL);

  if(bit_depth != 8)
  {
    fprintf(stderr, "Unsupported bit depth %d in %s\n", bit_depth, path);

    return -1;
  }

  switch(pixel_format)
  {
  case PNG_COLOR_TYPE_RGB_ALPHA:

    break;

  default:

    fprintf(stderr, "Unsupported pixel format in %s\n", path);

    return -1;
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

  {
    int i, j;
    unsigned char* pixel;
    unsigned char tmp;

    for(j = 0; j < height; ++j)
    {
      for(i = 0; i < width; ++i)
      {
        pixel = data + (j * width + i) * 4;

        if(!pixel[3])
        {
          pixel[0] = 0;
          pixel[1] = 0;
          pixel[2] = 0;
        }
        else
        {
          tmp = pixel[0];
          pixel[0] = pixel[2];
          pixel[2] = tmp;
        }
      }
    }
  }

  {
    XImage temp_image;
    XGCValues xgc;
    GC tmp_gc;

    init_ximage(&temp_image, width, height, (void*) data);

    Pixmap temp_pixmap = XCreatePixmap(display, window, width, height, 32);

    xgc.function = GXclear;
    tmp_gc = XCreateGC(display, temp_pixmap, GCFunction, &xgc);
    XFillRectangle(display, temp_pixmap, tmp_gc, 0, 0, width, height);
    XFreeGC(display, tmp_gc);

    tmp_gc = XCreateGC(display, temp_pixmap, 0, 0);
    XPutImage(display, temp_pixmap, tmp_gc, &temp_image, 0, 0, 0, 0, width, height);
    XFreeGC(display, tmp_gc);

    result = XRenderCreatePicture(display, temp_pixmap, XRenderFindStandardFormat(display, PictStandardARGB32), 0, 0);

    XFreePixmap(display, temp_pixmap);
  }

  free(data);

  pic->pic = result;
  pic->width = width;
  pic->height = height;

  return 0;
}


int image_load(const char* path, struct picture* pic)
{
  const char* ext = strrchr(path, '.');

  if(!ext)
    return -1;

  if(!strcmp(ext, ".png"))
    return png_load(path, pic);

  if(!strcmp(ext, ".xpm"))
    return xpm_load(path, pic);

  return -1;
}
