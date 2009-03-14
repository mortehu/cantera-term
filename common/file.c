#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"

struct cnt_file_args
{
  off_t size;
  char data[0];
};

void* cnt_file_callback_init(const char* path)
{
  int fd;
  off_t size, offset;
  struct cnt_file_args* result;

  fd = open(path, O_RDONLY);

  if(!fd)
    return 0;

  size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  if(size == -1)
    return 0;

  result = malloc(sizeof(struct cnt_file_args) + size);

  if(!result)
  {
    close(fd);

    return 0;
  }

  result->size = size;

  offset = 0;

  while(offset < size)
  {
    int res;

    res = read(fd, result->data + offset, size - offset);

    if(res <= 0)
    {
      close(fd);
      free(result);

      return 0;
    }

    offset += res;
  }

  close(fd);

  return result;
}

int cnt_file_callback(void** buffer, size_t* size, void* ctx)
{
  struct cnt_file_args* args = ctx;

  *size = args->size;
  *buffer = args->data;

  return 1;
}
