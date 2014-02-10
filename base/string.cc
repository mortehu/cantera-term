#include "string.h"

#include <err.h>
#include <stdarg.h>
#include <sysexits.h>

std::string StringPrintf(const char* format, ...) {
  va_list args;
  char* buf;

  va_start(args, format);

  if (-1 == vasprintf(&buf, format, args)) err(EXIT_FAILURE, "asprintf failed");

  std::string result(buf);
  free(buf);

  return result;
}

bool HasPrefix(const std::string& haystack, const std::string& needle) {
  if (haystack.size() < needle.length()) return false;
  return !haystack.compare(0, needle.length(), needle);
}
