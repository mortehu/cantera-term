#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#include "jpc.h"

struct JPC_char
{
  unsigned int attr;
  wchar_t ch;
};

static int JPC_width, JPC_height;
static int JPC_curx, JPC_cury;
static int JPC_cursorx, JPC_cursory;
static unsigned int JPC_curattr;
static struct JPC_char* JPC_screen;
static struct JPC_char* JPC_canvas;

static struct termios JPC_orig_termios;
static struct termios JPC_termios;

static void JPC_setattr(unsigned int attr);

void jpc_resize()
{
  struct winsize winsize;

  if(-1 == ioctl(STDIN_FILENO, TIOCGWINSZ, &winsize))
  {
    JPC_width = 80;
    JPC_height = 24;
  }
  else
  {
    if(winsize.ws_col == JPC_width && winsize.ws_row == JPC_height)
      return;

    JPC_width = winsize.ws_col;
    JPC_height = winsize.ws_row;
  }

  free(JPC_screen);
  free(JPC_canvas);

  JPC_screen = calloc(sizeof(struct JPC_char), JPC_width * JPC_height);
  JPC_canvas = calloc(sizeof(struct JPC_char), JPC_width * JPC_height);

  printf("\033[2J");
}

void jpc_exit()
{
  if(JPC_screen)
  {
    tcsetattr(0, TCSANOW, &JPC_orig_termios);

    free(JPC_screen);
    free(JPC_canvas);
    JPC_screen = 0;
    JPC_canvas = 0;

    printf("\033[?1049l\033[00m");
    fflush(stdout);
  }
}

void jpc_init()
{
  if(JPC_screen)
    return;

  tcgetattr(0, &JPC_orig_termios);
  JPC_termios = JPC_orig_termios;
  JPC_termios.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
  tcsetattr(0, TCSANOW, &JPC_termios);

  jpc_resize();

  JPC_curx = 0;
  JPC_cury = 0;
  JPC_curattr = JPC_FG_WHITE | JPC_BG_BLACK;

  printf("\033[?1049h\033[0;0H\033[00;37;40m");

  atexit(jpc_exit);
}

static void JPC_moveto(int x, int y)
{
  if(JPC_curx != x || JPC_cury != y)
  {
    printf("\033[%zu;%zuH", y + 1, x + 1);
    JPC_curx = x;
    JPC_cury = y;
  }
}

static void JPC_setattr(unsigned int attr)
{
  char command[16];
  int first = 1;

  if(attr == JPC_curattr)
    return;

  strcpy(command, "\033[");

  if((attr ^ JPC_curattr) & 0x0f)
  {
    switch(attr & 0x0f)
    {
    case JPC_FG_BLACK: strcat(command, "30"); break;
    case JPC_FG_RED: strcat(command, "31"); break;
    case JPC_FG_GREEN: strcat(command, "32"); break;
    case JPC_FG_YELLOW: strcat(command, "33"); break;
    case JPC_FG_BLUE: strcat(command, "34"); break;
    case JPC_FG_MAGENTA: strcat(command, "35"); break;
    case JPC_FG_CYAN: strcat(command, "36"); break;
    case JPC_FG_WHITE: strcat(command, "37"); break;
    }

    first = 0;
  }

  if((attr ^ JPC_curattr) & 0xf0)
  {
    if(!first)
      strcat(command, ";");

    switch(attr & 0xf0)
    {
    case JPC_BG_BLACK: strcat(command, "40"); break;
    case JPC_BG_RED: strcat(command, "41"); break;
    case JPC_BG_GREEN: strcat(command, "42"); break;
    case JPC_BG_YELLOW: strcat(command, "43"); break;
    case JPC_BG_BLUE: strcat(command, "44"); break;
    case JPC_BG_MAGENTA: strcat(command, "45"); break;
    case JPC_BG_CYAN: strcat(command, "46"); break;
    case JPC_BG_WHITE: strcat(command, "47"); break;
    }

    first = 0;
  }

  if((attr ^ JPC_curattr) & JPC_STANDOUT)
  {
    if(!first)
      strcat(command, ";");

    if(attr & JPC_STANDOUT)
      strcat(command, "1");
    else
      strcat(command, "22");

    first = 0;
  }

  if((attr ^ JPC_curattr) & JPC_UNDERLINE)
  {
    if(!first)
      strcat(command, ";");

    if(attr & JPC_UNDERLINE)
      strcat(command, "4");
    else
      strcat(command, "24");
  }

  strcat(command, "m");

  printf(command);

  JPC_curattr = attr;
}

void jpc_paint()
{
  int x, y, i, j;
  int c, cwidth;

  for(y = 0, i = 0; y < JPC_height; ++y)
  {
    for(x = 0; x < JPC_width; ++x, ++i)
    {
      if(memcmp(&JPC_screen[i], &JPC_canvas[i], sizeof(struct JPC_char)))
      {
        JPC_moveto(x, y);

        if(JPC_canvas[i].attr != JPC_curattr)
          JPC_setattr(JPC_canvas[i].attr);

        if(JPC_canvas[i].ch)
        {
          c = JPC_canvas[i].ch;
          cwidth = wcwidth(JPC_canvas[i].ch);

          if(c < 0x80)
            putchar(c);
          else if(c < 0x800)
          {
            putchar(0xc0 | (c >> 6));
            putchar(0x80 | (c & 0x3f));
          }
          else if(c < 0x10000)
          {
            putchar(0xe0 | (c >> 12));
            putchar(0x80 | ((c >> 6) & 0x3f));
            putchar(0x80 | (c & 0x3f));
          }
          else if(c < 0x200000)
          {
            putchar(0xf0 | (c >> 18));
            putchar(0x80 | ((c >> 12) & 0x3f));
            putchar(0x80 | ((c >> 6) & 0x3f));
            putchar(0x80 | (c & 0x3f));
          }
          else if(c < 0x4000000)
          {
            putchar(0xf8 | (c >> 24));
            putchar(0x80 | ((c >> 18) & 0x3f));
            putchar(0x80 | ((c >> 12) & 0x3f));
            putchar(0x80 | ((c >> 6) & 0x3f));
            putchar(0x80 | (c & 0x3f));
          }
          else
          {
            putchar(0xfc | (c >> 30));
            putchar(0x80 | ((c >> 24) & 0x3f));
            putchar(0x80 | ((c >> 18) & 0x3f));
            putchar(0x80 | ((c >> 12) & 0x3f));
            putchar(0x80 | ((c >> 6) & 0x3f));
            putchar(0x80 | (c & 0x3f));
          }

          for(j = 1; j < cwidth; ++j)
          {
            JPC_screen[y * JPC_width + x + j].attr = JPC_canvas[i].attr;
            JPC_screen[y * JPC_width + x + j].ch = 0;
          }

          JPC_curx += cwidth;
        }
        else
        {
          putchar(' ');
          ++JPC_curx;
        }

        if(JPC_curx >= JPC_width)
        {
          JPC_curx = 0;
          ++JPC_cury;
        }

        JPC_screen[i] = JPC_canvas[i];
      }
    }
  }

  JPC_moveto(JPC_cursorx, JPC_cursory);

  fflush(stdout);
}

void jpc_full_repaint()
{
  if(!JPC_screen)
    return;

  memset(JPC_screen, 0, sizeof(struct JPC_char) * JPC_width * JPC_height);

  JPC_curx = 0;
  JPC_cury = 0;
  JPC_curattr = JPC_FG_WHITE | JPC_BG_BLACK;

  printf("\033[?1049h\033[0;0H\033[00;37;40m");

  jpc_paint();
}

void jpc_clear()
{
  if(!JPC_screen)
    return;

  memset(JPC_canvas, 0, sizeof(struct JPC_char) * JPC_width * JPC_height);
}

void jpc_addstring(unsigned int attr, int x, int y, const wchar_t* text)
{
  int cwidth, i;

  if(!JPC_screen)
    return;

  if(y < 0 || y >= JPC_height)
    return;

  while(x < 0 && *text)
    x += wcwidth(*text++);

  if(x < 0)
    return;

  while(x < JPC_width && *text)
  {
    cwidth = wcwidth(*text);

    if(cwidth)
    {
      JPC_canvas[y * JPC_width + x].attr = attr;
      JPC_canvas[y * JPC_width + x].ch = *text;

      for(i = 1; i < cwidth; ++i)
      {
        JPC_canvas[y * JPC_width + x].attr = attr;
        JPC_canvas[y * JPC_width + x].ch = 0;
      }

      x += cwidth;
    }

    ++text;
  }

  JPC_cursorx = x;
  JPC_cursory = y;
}

void jpc_addstring_utf8(unsigned int attr, int x, int y, const unsigned char* text)
{
  int cwidth, i;

  if(!JPC_screen)
    return;

  if(y < 0 || y >= JPC_height)
    return;

  while(x < 0 && *text)
    x += wcwidth(*text++);

  if(x < 0)
    return;

  while(x < JPC_width && *text)
  {
    int ch = 0;
    int n;

    /* Check for invalid UTF-8 */
    if((*text & 0x80) == 0x80)
      return;

    ch = *text++;

    if(ch & 0x80)
    {
      if ((ch & 0xE0) == 0xC0)
      {
        ch &= 0x1F;

        n = 1;
      }
      else if ((ch & 0xF0) == 0xE0)
      {
        ch &= 0x0F;

        n = 2;
      }
      else if ((ch & 0xF8) == 0xF0)
      {
        ch &= 0x07;

        n = 3;
      }
      else if ((ch & 0xFC) == 0xF8)
      {
        ch &= 0x03;

        n = 4;
      }
      else if ((ch & 0xFE) == 0xFC)
      {
        ch &= 0x01;

        n = 5;
      }
      else
        return;

      while(n--)
      {
        if(!*text)
          return;

        int b = (unsigned char) *text;

        if((b & 0xC0) != 0x80)
          return;

        ch <<= 6;
        ch |= (b & 0x3F);

        ++text;
      }
    }

    cwidth = wcwidth(ch);

    if(cwidth)
    {
      JPC_canvas[y * JPC_width + x].attr = attr;
      JPC_canvas[y * JPC_width + x].ch = ch;

      for(i = 1; i < cwidth; ++i)
      {
        JPC_canvas[y * JPC_width + x].attr = attr;
        JPC_canvas[y * JPC_width + x].ch = 0;
      }

      x += cwidth;
    }
  }

  JPC_cursorx = x;
  JPC_cursory = y;
}

void jpc_disable()
{
  printf("\033[?1049l");
  fflush(stdout);
  tcsetattr(0, TCSANOW, &JPC_orig_termios);
}

void jpc_enable()
{
  tcsetattr(0, TCSANOW, &JPC_termios);

  JPC_curx = 0;
  JPC_cury = 0;
  JPC_curattr = JPC_FG_WHITE | JPC_BG_BLACK;

  printf("\033[?1049h\033[0;0H\033[00;37;40m");

  jpc_full_repaint();
}

int my_getchar()
{
  int c = getchar();
  //fprintf(stderr, "Got %02X\n", c);
  return c;
}

wchar_t jpc_getc()
{
  static wchar_t queue[4];
  static wchar_t queuelen;
  int result;

  /* XXX: this could easily be nicer */

  if(queuelen)
  {
    result = queue[0];
    --queuelen;
    memmove(&queue[0], &queue[1], queuelen * sizeof(wchar_t));

    return result;
  }

  result = my_getchar();

  /* XXX: Support UTF-8 */

  switch(result)
  {
  case '\033':

    queue[queuelen++] = my_getchar();

    if(queue[0] == 'O' || queue[0] == '[')
    {
      int arg = 0;
      int ch;

      for(;;)
      {
        ch = my_getchar();

        if(!isdigit(ch))
          break;

        arg *= 10;
        arg += ch - '0';
      }

      /* Common for O and [ */
      switch(ch)
      {
      case 'H': return JPC_KEY_HOME;
      case 'F': return JPC_KEY_END;
      }

      if(queue[0] == 'O')
      {
        switch(ch)
        {
        case 'P': return JPC_KEY_F(1);
        case 'Q': return JPC_KEY_F(2);
        case 'R': return JPC_KEY_F(3);
        case 'S': return JPC_KEY_F(4);
        }
      }
      else /* queue[0] == '[' */
      {
        switch(ch)
        {
        case 'A': return JPC_KEY_UP;
        case 'B': return JPC_KEY_DOWN;
        case 'C': return JPC_KEY_RIGHT;
        case 'D': return JPC_KEY_LEFT;
        case '~':

          switch(arg)
          {
          case 5: return JPC_KEY_PPAGE;
          case 6: return JPC_KEY_NPAGE;
          case 15: return JPC_KEY_F(5);
          case 17: return JPC_KEY_F(6);
          case 18: return JPC_KEY_F(7);
          case 19: return JPC_KEY_F(8);
          case 20: return JPC_KEY_F(9);
          case 21: return JPC_KEY_F(10);
          case 23: return JPC_KEY_F(11);
          case 24: return JPC_KEY_F(12);
          }
        }
      }
    }

    return '\033';

  case '\n': return '\r';
  }

  return result;
}

void jpc_get_size(int* width, int* height)
{
  if(width)
    *width = JPC_width;

  if(height)
    *height = JPC_height;
}
