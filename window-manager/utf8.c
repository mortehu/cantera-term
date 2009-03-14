#include <wchar.h>
#include <stdlib.h>

size_t utf8_to_ucs(wchar_t* result, const char* input, size_t avail)
{
  const wchar_t* begin = result;
  const wchar_t* end = result + avail;

  if(begin == end)
    return 0;

  while(*input)
  {
    int ch = (unsigned char) *input++;

    if((ch & 0x80) == 0x00)
    {
      *result++ = ch;

      if(result == end)
        break;

      continue;
    }

    int n; /* Number of following bytes */

    if((ch & 0xE0) == 0xC0)
    {
      ch &= 0x1F;

      n = 1;
    }
    else if((ch & 0xF0) == 0xE0)
    {
      ch &= 0x0F;

      n = 2;
    }
    else if((ch & 0xF8) == 0xF0)
    {
      ch &= 0x07;

      n = 3;
    }
    else if((ch & 0xFC) == 0xF8)
    {
      ch &= 0x03;

      n = 4;
    }
    else if((ch & 0xFE) == 0xFC)
    {
      ch &= 0x01;

      n = 5;
    }
    else
    {
      ch = '?';

      n = 0;
    }

    while(n--)
    {
      if(!*input)
      {
        ch = '?';

        break;
      }

      int b = (unsigned char) *input;

      if((b & 0xC0) != 0x80)
      {
        ch = '?';

        break;
      }

      ch <<= 6;
      ch |= (b & 0x3F);

      ++input;
    }

    *result++ = ch;
  }

  if(result == end)
    *(result - 1) = 0;
  else
    *result = 0;

  return result - begin;
}
