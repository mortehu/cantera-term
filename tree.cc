#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <memory>
#include <string>
#include <vector>

#include <err.h>
#include <fcntl.h>
#include <sysexits.h>
#include <unistd.h>

#include "terminal.h"
#include "tree.h"

void tree_create_node(tree* t, const std::string& path,
                      const std::string& value) {
  t->nodes.emplace_back();
  tree::node& new_node = t->nodes.back();

  new_node.path = path;
  new_node.value = value;
}

long long int tree_get_integer_default(const tree* t, const char* path,
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

const char* tree_get_string_default(const tree* t, const char* path,
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

tree* tree_load_cfg(int home_fd, const char* path) {
  off_t size;
  int fd;

  std::string symbol;

  std::vector<size_t> section_stack;
  int expecting_symbol = 1;

  char* c;
  int lineno = 1;

  std::unique_ptr<tree> result(new tree);
  result->name = path;

  if (-1 == (fd = openat(home_fd, path, O_RDONLY))) {
    if (errno == ENOENT) return result.release();

    err(EX_NOINPUT, "%s: open failed", path);
  }

  if (-1 == (size = lseek(fd, 0, SEEK_END)))
    err(EX_OSERR, "%s: failed to seek to end of file", path);

  if (-1 == lseek(fd, 0, SEEK_SET))
    err(EX_OSERR, "%s: failed to seek to start of file", path);

  std::unique_ptr<char[]> data(new char[size + 1]);

  read_all(fd, &data[0], size, path);
  data[size] = 0;

  close(fd);

  c = &data[0];

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
      if (section_stack.empty())
        errx(EX_DATAERR, "%s:%d: unexpected '}'", path, lineno);

      section_stack.pop_back();
      if (section_stack.empty())
        symbol.clear();
      else
        symbol.resize(section_stack.back());

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

      if (!symbol.empty()) symbol.push_back('.');

      while (is_symbol_char(*c))
        symbol.push_back(*c++);

      if (isspace(*c)) {
        *c++ = 0;
        while (*c && isspace(*c))
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

          section_stack.push_back(symbol.length());
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
            if (!*++c)
              errx(EX_DATAERR, "%s:%d: unexpected end-of-file in "
                               "string",
                   path, lineno);

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

      tree_create_node(result.get(), symbol, value);

      if (!section_stack.empty())
        symbol.resize(section_stack.back());
      else
        symbol.clear();

      expecting_symbol = 1;
    }
  }

  return result.release();
}
