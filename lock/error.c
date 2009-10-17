#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <time.h>
#include <sys/timeb.h>

#include "error.h"

#ifndef NDEBUG
void info(const char* format, ...)
{
  va_list args;
#ifndef PSP
  struct timeb tp;
  char timestr[32];
  char* buf;

  ftime(&tp);

  strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", localtime(&tp.time));

  va_start(args, format);
  vasprintf(&buf, format, args);

#if WIN32
  fprintf(stderr, "%s.%03d: %s\n", timestr, tp.millitm, buf);
#else
  fprintf(stderr, "\033[1;37m%s.%03d\033[00m: %s\n", timestr, tp.millitm, buf);
#endif
#endif

  free(buf);
}
#endif

void fatal_error(const char* format, ...)
{
  va_list args;
#ifndef PSP
  struct timeb tp;
  char timestr[32];
  char* buf;

  ftime(&tp);

  strftime(timestr, sizeof(timestr), "%Y-%m-%dT%H:%M:%S", localtime(&tp.time));

  va_start(args, format);
  vasprintf(&buf, format, args);

#if WIN32
  fprintf(stderr, "%s.%03d: %s\n", timestr, tp.millitm, buf);
#else
  fprintf(stderr, "\033[1;37m%s.%03d\033[00m: \033[1;31m%s\033[00m\n", timestr, tp.millitm, buf);
#endif
#endif

  free(buf);

#if PSP
  sceKernelExitGame();
#else
  exit(EXIT_FAILURE);
#endif
}
