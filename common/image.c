#include <assert.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <X11/extensions/Xrender.h>

#include <png.h>
#include <jpeglib.h>
#include <gif_lib.h>

#include "common.h"

extern Display* display;
extern Window window;

struct cnt_image
{
  int width;
  int height;
  Picture picture;

  cnt_data_callback data_callback;
  void* data_callback_arg;

  unsigned char* data;

  size_t data_consumed;
  int done;
  int started;

  union
  {
    struct
    {
      png_structp png;
      png_infop info;
    } png;

    struct
    {
      struct jpeg_decompress_struct cinfo;
      struct jpeg_source_mgr srcmgr;
    } jpeg;
  };
};

static int is_jpeg(const char* data, size_t size)
{
  if(size < 10)
    return 0;

  return !memcmp(data + 6, "JFIF", 4);
}

static int is_gif(const char* data, size_t size)
{
  if(size < 4)
    return 0;

  return !memcmp(data, "GIF8", 4);
}

static int is_xpm(const char* data, size_t size)
{
  if(size < 10)
    return 0;

  return !memcmp(data, "/* XPM */\n", 10);
}

static void png_error_cb(png_structp png, png_const_charp message)
{
  struct cnt_image* img = png_get_progressive_ptr(png);

  if(img)
    img->done = 1;

  fprintf(stderr, "PNG Error: %s\n", message);
}

static void png_warning_cb(png_structp png, png_const_charp message)
{
  fprintf(stderr, "PNG Warning: %s\n", message);
}

static void png_info_cb(png_structp png, png_infop info)
{
  struct cnt_image* img = png_get_progressive_ptr(png);
  png_uint_32 width;
  png_uint_32 height;
  int bit_depth, color_type;

  png_get_IHDR(png, info, &width, &height, &bit_depth, &color_type, 0, 0, 0);

  png_set_bgr(png);

  if(color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);

  if(png_get_valid(png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png);

  if(bit_depth == 16)
    png_set_strip_16(png);
  else if(bit_depth < 8)
    png_set_packing(png);

  if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);

  if(!(color_type & PNG_COLOR_MASK_ALPHA))
    png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);

  png_set_interlace_handling(png);

  png_read_update_info(png, info);

  img->width = width;
  img->height = height;
  img->data = malloc(width * height * 4);
  memset(img->data, 0, width * height * 4);
}

static void png_row_cb(png_structp png, png_bytep new_row, png_uint_32 row_num, int pass)
{
  struct cnt_image* img;

  if(!new_row)
       return;

  img = png_get_progressive_ptr(png);

  png_progressive_combine_row(png, img->data + row_num * img->width * 4, new_row);
}

static void png_end_cb(png_structp png, png_infop cinfo)
{
  struct cnt_image* img = png_get_progressive_ptr(png);

  img->done = 1;
}

static void jpeg_init_source(j_decompress_ptr cinfo)
{
  return;
}

static boolean jpeg_fill_input_buffer(j_decompress_ptr cinfo)
{
  return 0;
}

static void jpeg_skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
  assert((size_t) num_bytes <= cinfo->src->bytes_in_buffer);

  cinfo->src->next_input_byte += num_bytes;
  cinfo->src->bytes_in_buffer -= num_bytes;
}

static void jpeg_term_source(j_decompress_ptr cinfo)
{
  return;
}

struct data_handle
{
  char* data;
  size_t data_size;
  size_t offset;
};

static int gif_read(GifFileType* gif, GifByteType* data, int length)
{
  struct data_handle* h = gif->UserData;

  if(length + h->offset > h->data_size)
    length = h->data_size - h->offset;

  memcpy(data, h->data + h->offset, length);
  h->offset += length;

  return length;
}

static void update_image(struct cnt_image* img, void* data, size_t data_size, int transfer_done)
{
  if(data_size > img->data_consumed)
  {
    if(!png_sig_cmp(data, 0, data_size))
    {
      png_process_data(img->png.png, img->png.info,
          data + img->data_consumed,
          data_size - img->data_consumed);
    }
    else if(is_jpeg(data, data_size))
    {
      if(transfer_done)
      {
        int x, y;
        unsigned char* row;
        struct jpeg_error_mgr error_manager;

        img->jpeg.cinfo.err = jpeg_std_error(&error_manager);

        jpeg_create_decompress(&img->jpeg.cinfo);

        img->jpeg.srcmgr.next_input_byte = data;
        img->jpeg.srcmgr.bytes_in_buffer = data_size;
        img->jpeg.srcmgr.init_source = jpeg_init_source;
        img->jpeg.srcmgr.fill_input_buffer = jpeg_fill_input_buffer;
        img->jpeg.srcmgr.skip_input_data = jpeg_skip_input_data;
        img->jpeg.srcmgr.resync_to_restart = jpeg_resync_to_restart;
        img->jpeg.srcmgr.term_source = jpeg_term_source;

        img->jpeg.cinfo.src = &img->jpeg.srcmgr;

        jpeg_read_header(&img->jpeg.cinfo, TRUE);

        jpeg_start_decompress(&img->jpeg.cinfo);

        assert(img->jpeg.cinfo.output_components == 3);

        img->width = img->jpeg.cinfo.output_width;
        img->height = img->jpeg.cinfo.output_height;
        img->data = malloc(img->width * img->height * 4);

        row = alloca(img->width * 3);

        while(img->jpeg.cinfo.output_scanline < img->jpeg.cinfo.output_height)
        {
          y = img->jpeg.cinfo.output_scanline;
          jpeg_read_scanlines(&img->jpeg.cinfo, (JSAMPLE**) &row, 1);

          for(x = 0; x < img->width; ++x)
          {
            img->data[(y * img->width + x) * 4 + 0] = row[x * 3 + 2];
            img->data[(y * img->width + x) * 4 + 1] = row[x * 3 + 1];
            img->data[(y * img->width + x) * 4 + 2] = row[x * 3 + 0];
            img->data[(y * img->width + x) * 4 + 3] = 0xFF;
          }
        }

        img->done = 1;
      }
    }
    else if(is_gif(data, data_size))
    {
      if(transfer_done)
      {
        struct data_handle dh;
        GifFileType* gif;
        int gif_done = 0;
        int transparency = -1;

        dh.data = data;
        dh.data_size = data_size;
        dh.offset = 0;

        gif = DGifOpen(&dh, gif_read);

        while(!gif_done)
        {
          GifRecordType gif_type;

          DGifGetRecordType(gif, &gif_type);

          switch(gif_type)
          {
          case IMAGE_DESC_RECORD_TYPE:

            {
              int x, y, i, pass;
              unsigned char* row;

              DGifGetImageDesc(gif);

              img->width = gif->SWidth;
              img->height = gif->SHeight;
              img->data = malloc(img->width * img->height * 4);

              row = alloca(img->width);

              y = 0;
              pass = 0;

              for(i = 0; i < img->height; ++i)
              {
                if(GIF_ERROR == DGifGetLine(gif, row, img->width))
                  break;

                for(x = 0; x < img->width; ++x)
                {
                  int color = row[x];

                  if(gif->SColorMap)
                  {
                    img->data[(y * img->width + x) * 4] =     gif->SColorMap->Colors[color].Blue;
                    img->data[(y * img->width + x) * 4 + 1] = gif->SColorMap->Colors[color].Green;
                    img->data[(y * img->width + x) * 4 + 2] = gif->SColorMap->Colors[color].Red;
                  }
                  else
                  {
                    img->data[(y * img->width + x) * 4] =     gif->Image.ColorMap->Colors[color].Blue;
                    img->data[(y * img->width + x) * 4 + 1] = gif->Image.ColorMap->Colors[color].Green;
                    img->data[(y * img->width + x) * 4 + 2] = gif->Image.ColorMap->Colors[color].Red;
                  }

                  if(transparency != -1)
                    img->data[(y * img->width + x) * 4 + 3] = (color == transparency) ? 0 : 255;
                  else
                    img->data[(y * img->width + x) * 4 + 3] = 255;
                }

                if(gif->Image.Interlace)
                {
                  static const int offsets[] = { 0, 4, 2, 1 };
                  static const int skips[] = { 8, 8, 4, 2 };

                  y += skips[pass];

                  if(y >= img->height)
                  {
                    if(++pass == 4)
                      break;

                    y = offsets[pass];
                  }
                }
                else
                  ++y;
              }

              DGifCloseFile(gif);
              gif_done = 1;

              break;
            }

          case EXTENSION_RECORD_TYPE:

            {
              int extension_code;
              GifByteType* extension;

              if(GIF_ERROR == DGifGetExtension(gif, &extension_code, &extension))
              {
                gif_done = 1;
                DGifCloseFile(gif);

                break;
              }

              while(extension)
              {
                if(extension_code == 0xF9)
                {
                  if(extension[1] & 1)
                    transparency = extension[4];
                }

                if(GIF_ERROR == DGifGetExtensionNext(gif, &extension))
                {
                  gif_done = 1;
                  DGifCloseFile(gif);

                  break;
                }
              }
            }

            break;

          case TERMINATE_RECORD_TYPE:

            gif_done = 1;
            DGifCloseFile(gif);

            break;

          default:

            gif_done = 1;
            DGifCloseFile(gif);

            break;
          }
        }

        img->done = 1;
      }
    }
    else if(is_xpm(data, data_size))
    {
      if(transfer_done)
      {
        Picture image, mask = None;
        Pixmap result_pixmap;
        Pixmap image_pixmap = 0;
        Pixmap mask_pixmap = 0;
        XpmAttributes attr;
        XRenderColor clear;

        memset(&attr, 0, sizeof(attr));

        if(-1 == XpmCreatePixmapFromBuffer(display, window, data, &image_pixmap, &mask_pixmap, &attr))
          return;

        result_pixmap = XCreatePixmap(display, window, attr.width, attr.height, 32);
        img->picture = XRenderCreatePicture(display, result_pixmap, XRenderFindStandardFormat(display, PictStandardARGB32), 0, 0);
        XFreePixmap(display, result_pixmap);

        image = XRenderCreatePicture(display, image_pixmap, XRenderFindStandardFormat(display, PictStandardRGB24), 0, 0);
        XFreePixmap(display, image_pixmap);

        if(mask_pixmap)
        {
          mask = XRenderCreatePicture(display, mask_pixmap, XRenderFindStandardFormat(display, PictStandardA1), 0, 0);
          XFreePixmap(display, mask_pixmap);
        }

        memset(&clear, 0, sizeof(clear));
        XRenderFillRectangle(display, PictOpSrc, img->picture, &clear, 0, 0, attr.width, attr.height);

        XRenderComposite(display, PictOpOver, image, mask, img->picture, 0, 0, 0, 0, 0, 0, attr.width, attr.height);

        img->width = attr.width;
        img->height = attr.height;
        img->done = 1;
      }
    }

    img->data_consumed = data_size;
  }

  if(img->data)
  {
    if(img->width && img->height)
    {
      XImage temp_image;
      Pixmap temp_pixmap;
      GC temp_gc;
      XGCValues xgc;
      size_t i;

      /* PictOpOver acts as src=one dest=one instead of src=alpha dest=1-alpha
       * on pixels with alpha equal to zero, so we need to clear these pixels.
       * Yes, this is weird. */
      for(i = 0; i < img->width * img->height * 4; i += 4)
      {
        if(!img->data[i + 3])
        {
          img->data[i + 0] = 0;
          img->data[i + 1] = 0;
          img->data[i + 2] = 0;
        }
      }


      memset(&temp_image, 0, sizeof(XImage));
      temp_image.width = img->width;
      temp_image.height = img->height;
      temp_image.format = ZPixmap;
      temp_image.data = (char*) img->data;
#if __BYTE_ORDER == __LITTLE_ENDIAN
      temp_image.byte_order = LSBFirst;
#else
      temp_image.byte_order = MSBFirst;
#endif
      temp_image.bitmap_bit_order = MSBFirst;
      temp_image.bitmap_unit = 32;
      temp_image.bitmap_pad = 32;
      temp_image.depth = 32;
      temp_image.bits_per_pixel = 32;

      XInitImage(&temp_image);

      if(img->picture != None)
        XRenderFreePicture(display, img->picture);

      temp_pixmap = XCreatePixmap(display, window, img->width, img->height, 32);

      temp_gc = XCreateGC(display, temp_pixmap, 0, 0);
      XPutImage(display, temp_pixmap, temp_gc, &temp_image, 0, 0, 0, 0, img->width, img->height);
      XFreeGC(display, temp_gc);

      img->picture = XRenderCreatePicture(display, temp_pixmap, XRenderFindStandardFormat(display, PictStandardARGB32), 0, 0);

      XFreePixmap(display, temp_pixmap);
    }

    if(img->done)
    {
      free(img->data);
      img->data = 0;
    }
  }
}

struct cnt_image* cnt_image_alloc()
{
  struct cnt_image* result;

  result = malloc(sizeof(struct cnt_image));
  memset(result, 0, sizeof(struct cnt_image));

  return result;
}

void cnt_image_free(struct cnt_image** ptr)
{
  struct cnt_image* img = *ptr;

  if(img->picture != None)
    XRenderFreePicture(display, img->picture);
  if(img->data)
    free(img->data);
  free(*ptr);
  *ptr = 0;
}

void cnt_image_set_data_callback(struct cnt_image* image, cnt_data_callback data_callback, void* ctx)
{
  image->data_callback = data_callback;
  image->data_callback_arg = ctx;
}

unsigned long cnt_image_load(size_t* width, size_t* height, struct cnt_image* image)
{
  Picture result = None;
  int transfer_done;
  void* data = 0;
  size_t data_size = 0;

  *width = image->width;
  *height = image->height;

  if(image->done)
    return image->picture;

  if(image->started)
  {
    transfer_done = image->data_callback(&data, &data_size, image->data_callback_arg);

    update_image(image, data, data_size, transfer_done);

    if(!transfer_done)
      free(data);
    else
      image->done = 1;

    *width = image->width;
    *height = image->height;

    return image->picture;
  }

  transfer_done = image->data_callback( &data, &data_size, image->data_callback_arg);

  if(!data_size)
    return None;

  if(!png_sig_cmp(data, 0, data_size))
  {
    image->started = 1;
    image->png.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, png_error_cb, png_warning_cb);
    image->png.info = png_create_info_struct(image->png.png);

    png_set_progressive_read_fn(image->png.png, image, png_info_cb, png_row_cb, png_end_cb);

    update_image(image, data, data_size, transfer_done);

    if(transfer_done)
      image->done = 1;

    *width = image->width;
    *height = image->height;
    result = image->picture;
  }
  else if(is_jpeg(data, data_size) || is_gif(data, data_size) || is_xpm(data, data_size))
  {
    image->started = 1;

    update_image(image, data, data_size, transfer_done);

    if(transfer_done)
      image->done = 1;

    *width = image->width;
    *height = image->height;
    result = image->picture;
  }

  if(!transfer_done)
    free(data);

  return result;
}
