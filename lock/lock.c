#include <err.h>
#include <math.h>
#include <pthread.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <GL/gl.h>
#include <X11/extensions/Xinerama.h>

#include "gui.h"

struct gui_instance *gui;
struct gui_font *font;

static time_t begin_lock;
static time_t hide_hud = 0;

char* user_name;
char* host_name;
char* password_hash;

XineramaScreenInfo* screens;
int screen_count;

static char pass[1024];

static const char* hash_for_password(const char* password, const char* salt)
{
  return crypt(password, salt);
}

void
lock_paint(struct gui_instance *gi, unsigned int x, unsigned int y, unsigned int width, unsigned int height)
{
  int i, j;
  char* buf;
  time_t now;

  gui_draw_quad(gi, 0, 0, width, height, 0x000000);

  now = time (0);

  if(now < hide_hud)
    {
      for(i = 0; i < screen_count; ++i)
        {
          char date[24];
          struct tm* lt;

          lt = localtime(&begin_lock);
          strftime(date, sizeof(date), "%Y-%m-%d %H:%M %Z", lt);

          x = screens[i].x_org;
          y = screens[i].y_org;

          asprintf(&buf, "%s at %s", user_name, host_name);
          gui_draw_text(gi, font, x, y, buf, 0xffffffff, 0);
          free(buf);

          y += 25.0f;
          asprintf(&buf, "Locked since %s", date);
          gui_draw_text(gi, font, x + 20, y + 20, buf, 0xffcccccc, 0);
          free(buf);

          unsigned int diff = (now - begin_lock);

          if(diff < 120)
            asprintf(&buf, "%u seconds ago", diff);
          else if(diff < 3600)
            asprintf(&buf, "%u:%02u minutes ago", diff / 60, diff % 60);
          else
            asprintf(&buf, "%u:%02u hours ago", diff / 3600, (diff / 60) % 60);

          y += 25.0f;
          gui_draw_text(gi, font, x + 20, y + 20, buf, 0xffcccccc, 0);
          free(buf);

          buf = malloc(strlen(pass) + sizeof("Password: "));
          strcpy(buf, "Password: ");
          for(j = 0; pass[j]; ++j)
            strcat(buf, "*");

          y += 25.0f;
          gui_draw_text(gi, font, x + 20, y + 20, buf, 0xffcccccc, 0);
          free(buf);
        }
    }
}

static void *
repaint_thread (void *arg)
{
  for (;;)
    {
      sleep (1);

      gui_repaint ();
    }

  return 0;
}

static void
lock_init(struct gui_instance *gi)
{
  XWindowAttributes root_window_attr;
  pthread_t th;
  int i;

  pthread_create (&th, 0, repaint_thread, 0);
  pthread_detach (th);

  XGetWindowAttributes(GUI_display, RootWindow(GUI_display, DefaultScreen(GUI_display)), &root_window_attr);

  font = gui_font_load ("Bitstream Vera Sans Mono", 18, 0);

  if(XineramaQueryExtension(GUI_display, &i, &i))
    {
      if(XineramaIsActive(GUI_display))
        screens = XineramaQueryScreens(GUI_display, &screen_count);
    }

  if(!screen_count)
    {
      screen_count = 1;
      screens = malloc(sizeof(XineramaScreenInfo) * 1);
      screens[0].x_org = 0;
      screens[0].y_org = 0;
      screens[0].width = root_window_attr.width;
      screens[0].height = root_window_attr.height;
    }
}

void
lock_key_pressed (struct gui_instance *gi, unsigned int key, const wchar_t *text, unsigned int modmask)
{
  char ctext[32];
  size_t ctextlen;

  switch (key)
    {
    case XK_BackSpace:

      if(pass[0])
        pass[strlen(pass) - 1] = 0;

      break;

    default:

      ctextlen = wcstombs(ctext, text, sizeof (ctext));

      if (ctextlen == sizeof (ctext))
        return;

      switch(ctext[0])
        {
        case '\b':

          if(pass[0])
            pass[strlen(pass) - 1] = 0;

          break;

        case 'U' & 0x3F:

          pass[0] = 0;

          break;

        default:

          if(strlen(pass) + strlen(ctext) < sizeof(pass) - 1)
            strcat(pass, ctext);
        }
    }

  if(!strcmp(password_hash, hash_for_password(pass, password_hash)))
    exit(0);

  hide_hud = time (0) + 20;

  gui_repaint();
}

static char*
get_user_name()
{
  char* result = 0;
  uid_t euid;
  struct passwd* pwent;

  euid = getuid();

  while(0 != (pwent = getpwent()))
  {
    if(pwent->pw_uid == euid)
    {
      result = strdup(pwent->pw_name);

      break;
    }
  }

  endpwent();

  if (*result == '+')
    ++result;

  return result;
}

static char*
get_host_name()
{
  static char host_name[32];

  gethostname(host_name, sizeof(host_name));
  host_name[sizeof(host_name) - 1] = 0;

  return host_name;
}

void
get_password_hash()
{
  struct passwd* p;
  struct spwd* s;

  p = getpwnam(user_name);

  if(!p)
    errx(EXIT_FAILURE, "Unable to get password for '%s'", user_name);

  password_hash = p->pw_passwd;

  if(!strcmp(password_hash, "x"))
    {
      s = getspnam(user_name);

      if(!s)
	errx(EXIT_FAILURE, "Unable to get password for '%s' from shadow file", user_name);

      password_hash = s->sp_pwdp;
    }
}

int
main (int argc, char **argv)
{
  struct gui_definition def;

  begin_lock = time(0);

  hide_hud = begin_lock + 60;

  user_name = get_user_name();
  host_name = get_host_name();

  get_password_hash();

  memset(&def, 0, sizeof(def));

  def.flags = GUI_OVERRIDE_REDIRECT;
  def.init = lock_init;
  def.paint = lock_paint;
  def.key_pressed = lock_key_pressed;

  gui = gui_instance(&def);

  gui_main_loop ();

  return EXIT_SUCCESS;
}
