#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>
#include <vector>

#include <err.h>
#include <fcntl.h>
#include <sysexits.h>
#include <unistd.h>

#include "array.h"
#include "tree.h"

extern int home_fd;

struct tree_node {
  std::string path, value;
};

struct tree {
  std::string name;
  std::vector<tree_node> nodes;
};

void tree_destroy(struct tree* t) { delete t; }

void tree_create_node(struct tree* t, const char* path, const char* value) {
  tree_node new_node;

  new_node.path = path;
  new_node.value = value;

  t->nodes.push_back(new_node);
}

long long int tree_get_integer_default(const struct tree* t, const char* path,
                                       long long int def) {
  char* tmp;
  long long int result;
  size_t i;

  for (i = 0; i < t->nodes.size(); ++i) {
    if (t->nodes[i].path == path) {
      result = strtoll(t->nodes[i].value.c_str(), &tmp, 0);

      if (*tmp) {
        fprintf(stderr, "%s: expected integer value in '%s', found '%s'\n",
                t->name.c_str(), path, t->nodes[i].value.c_str());

        return def;
      }

      return result;
    }
  }

  return def;
}

const char* tree_get_string_default(const struct tree* t, const char* path,
                                    const char* def) {
  size_t i;

  for (i = 0; i < t->nodes.size(); ++i) {
    if (t->nodes[i].path == path) return t->nodes[i].value.c_str();
  }

  return def;
}

static int is_symbol_char(int ch) {
  return isalnum(ch) || ch == '-' || ch == '_' || ch == '!';
}

static void read_all(int fd, void* buf, size_t total, const char* path) {
  char* cbuf = (char*)buf;
  size_t offset = 0;
  int ret;

  while (offset < total) {
    ret = read(fd, cbuf, total - offset);

    if (ret == -1) err(EXIT_FAILURE, "%s: read error", path);

    if (ret == 0)
      errx(EXIT_FAILURE,
           "%s: file was truncated while reading (read returned 0)", path);

    offset += ret;
  }
}

struct tree* tree_load_cfg(const char* path) {
  struct tree* result;
  char* data;
  off_t size;
  int fd;

  char symbol[4096];
  size_t symbol_len = 0;

  size_t section_stack[32];
  size_t section_stackp = 0;
  int expecting_symbol = 1;

  char* c;
  int lineno = 1;

  result = new tree;
  result->name = path;

  if (-1 == (fd = openat(home_fd, path, O_RDONLY))) {
    if (errno == ENOENT) return result;

    err(EX_NOINPUT, "%s: open failed", path);
  }

  if (-1 == (size = lseek(fd, 0, SEEK_END)))
    err(EX_OSERR, "%s: failed to seek to end of file", path);

  if (-1 == lseek(fd, 0, SEEK_SET))
    err(EX_OSERR, "%s: failed to seek to start of file", path);

  data = new char[size + 1];

  read_all(fd, data, size, path);
  data[size] = 0;

  close(fd);

  c = data;

  while (*c) {
    while (isspace(*c)) {
      if (*c++ == '\n') ++lineno;
    }

    if (!*c) break;

    if (*c == '#') {
      while (*c && *c != '\n')
        ++c;

      ++lineno;

      continue;
    }

    if (*c == '}') {
      if (!section_stackp)
        errx(EX_DATAERR, "%s:%d: unexpected '}'", path, lineno);

      if (!--section_stackp)
        symbol_len = 0;
      else
        symbol_len = section_stack[section_stackp - 1];

      ++c;

      continue;
    }

    if (expecting_symbol) {
      if (!is_symbol_char(*c)) {
        if (isprint(*c))
          errx(EX_DATAERR, "%s:%d: unexpected '%c' while looking for symbol",
               path, lineno, *c);
        else
          errx(EX_DATAERR, "%s:%d: unexpected 0x%02x while looking for symbol",
               path, lineno, *c);
      }

      if (symbol_len) {
        if (symbol_len + 1 == ARRAY_SIZE(symbol))
          errx(EX_DATAERR, "%s:%d: symbol stack overflow", path, lineno);

        symbol[symbol_len++] = '.';
      }

      while (is_symbol_char(*c)) {
        if (symbol_len + 1 == ARRAY_SIZE(symbol))
          errx(EX_DATAERR, "%s:%d: symbol stack overflow", path, lineno);

        symbol[symbol_len++] = *c++;
      }

      if (isspace(*c)) {
        *c++ = 0;
        while (isspace(*c))
          ++c;
      }

      switch (*c) {
        case 0:

          errx(EX_DATAERR, "%s:%d: unexpected end-of-file after symbol", path,
               lineno);

        case '.':

          expecting_symbol = 1;
          *c++ = 0;

          break;

        case '{':

          if (section_stackp == ARRAY_SIZE(section_stack))
            errx(EX_DATAERR, "%s:%d: too many nested sections", path, lineno);

          section_stack[section_stackp++] = symbol_len;
          expecting_symbol = 1;
          *c++ = 0;

          break;

        case '}':

          errx(EX_DATAERR, "%s:%d: unexpected '%c' after symbol", path, lineno,
               *c);

        default:

          expecting_symbol = 0;
      }
    } else /* !expecting_symbol */
        {
      char* value = c;

      if (*c == '"') {
        char* o;

        o = value = ++c;

        for (;;) {
          if (!*c) {
            errx(EX_DATAERR, "%s:%d: unexpected end-of-file in "
                             "string",
                 path, lineno);
          }

          if (*c == '\\') {
            if (!*(c + 1))
              errx(EX_DATAERR, "%s:%d: unexpected end-of-file in "
                               "string",
                   path, lineno);

            ++c;
            *o++ = *c++;
          } else if (*c == '"')
            break;
          else
            *o++ = *c++;
        }

        *o = 0;
        ++c;
      } else {
        while (*c && !isspace(*c))
          ++c;

        if (*c) *c++ = 0;
      }

      symbol[symbol_len] = 0;

      tree_create_node(result, symbol, value);

      if (section_stackp)
        symbol_len = section_stack[section_stackp - 1];
      else
        symbol_len = 0;

      expecting_symbol = 1;
    }
  }

  delete[] data;

  return result;
}
