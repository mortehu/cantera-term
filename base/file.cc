#include "base/file.h"

#include <climits>
#include <cstdlib>
#include <cstring>

#include <err.h>
#include <unistd.h>

std::string TemporaryDirectory(const char* base_name) {
  char path[PATH_MAX];
  strcpy(path, "/tmp/");
  strcat(path, base_name);
  strcat(path, ".XXXXXX");

  if (!mkdtemp(path))
    err(EXIT_FAILURE, "Failed to make temporary directory '%s'", base_name);

  return path;
}
