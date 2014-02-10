// Routines for communicating between terminal emulator and shell.
//
// When the terminal emulator starts the internal bash instance, it is passed a
// Unix socket file descriptor for sending and receiving messages.  This allows
// fast and robust inter-operation.

#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <err.h>
#include <readline/readline.h>
#include <unistd.h>

#include "base/string.h"
#include "terminal.h"
#include "x11.h"

extern "C" {

// Declarations from within Bash.

#define PATH_CHECKDOTDOT	0x0001
#define PATH_CHECKEXISTS	0x0002
#define PATH_HARDPATH		0x0004
#define PATH_NOALLOC		0x0008

extern char* get_string_value(const char*);
extern char* bash_dequote_text(const char* text);
extern char* sh_canonpath(char*, int);
extern char* full_pathname(char*);
extern char* polite_directory_format(char*);

}  // extern "C"

namespace {

Terminal* terminal;
int socket_fd;

class Message : public std::map<std::string, std::string> {
 public:
  void Serialize(std::vector<uint8_t>* output) {
    output->clear();
    output->push_back(0);
    output->push_back(0);
    output->push_back(0);
    output->push_back(0);

    for (const auto& kv : *this) {
      WriteSize(kv.first.length(), output);
      output->insert(output->end(), kv.first.begin(), kv.first.end());
      WriteSize(kv.second.length(), output);
      output->insert(output->end(), kv.second.begin(), kv.second.end());
    }

    uint32_t total_size = static_cast<uint32_t>(output->size());
    (*output)[0] = total_size >> 16;
    (*output)[1] = total_size >> 24;
    (*output)[2] = total_size >> 8;
    (*output)[3] = total_size;
  }

  void Send(int fd) {
    std::vector<uint8_t> buffer;
    Serialize(&buffer);

    size_t offset = 0;

    while (offset < buffer.size()) {
      ssize_t ret = write(fd, &buffer[offset], buffer.size() - offset);
      if (ret == -1) err(EXIT_FAILURE, "Writing to command socket failed");
      offset += ret;
    }
  }

  void Deserialize(const std::vector<uint8_t>& input, size_t size) {
    size_t i = 4;

    while (i < size) {
      size_t key_size = ReadSize(&i, input);
      std::string key(reinterpret_cast<const char*>(&input[i]), key_size);
      i += key_size;

      size_t value_size = ReadSize(&i, input);
      std::string value(reinterpret_cast<const char*>(&input[i]), value_size);
      i += value_size;

      (*this)[key] = value;
    }

    assert(i == size);
  }

 private:
  void WriteSize(size_t size, std::vector<uint8_t>* output) {
    if (size > 0x3fff) output->push_back(0x80 | (size >> 14));
    if (size > 0x7f) output->push_back(0x80 | (size >> 7));
    output->push_back(size & 0x7f);
  }

  size_t ReadSize(size_t* i, const std::vector<uint8_t>& input) {
    size_t result = 0;
    do {
      result = (result << 7) | input[*i];
    } while (input[(*i)++] & 0x80);
    return result;
  }
};

int ReadlineHook() {
  static std::string line_buffer;
  Message msg;

  if (line_buffer == rl_line_buffer) return 0;
  line_buffer = rl_line_buffer;

  if (line_buffer == "cd" || HasPrefix(line_buffer, "cd ")) {
    std::string path = line_buffer.substr(2);

    while (!path.empty() && isspace(path[0]))
      path.erase(path.begin());

    char* dequoted_path = bash_dequote_text(path.c_str());
    if (!dequoted_path) goto done;

    char* expanded_path;
    if (!*dequoted_path)
      expanded_path = get_string_value("HOME");
    else if (!strcmp(dequoted_path, "-"))
      expanded_path = get_string_value("OLDPWD");
    else
      expanded_path = dequoted_path;
    if (!expanded_path) { free(dequoted_path); goto done; }

    char* canonical_path = sh_canonpath(full_pathname(expanded_path), PATH_CHECKDOTDOT | PATH_CHECKEXISTS);
    free(dequoted_path);
    if (!canonical_path) goto done;

    char* polite_path = polite_directory_format(canonical_path);
    if (polite_path)
      msg["cursor-hint"] = polite_path;

    free(canonical_path);
  }

done:
  msg.Send(socket_fd);

  return 0;
}

void ReaderThread(int fd) {
  std::vector<uint8_t> buffer;
  uint8_t tmp[4096];
  ssize_t ret;

  while (0 < (ret = read(fd, tmp, sizeof(tmp)))) {
    buffer.insert(buffer.end(), tmp, tmp + ret);

    if (buffer.size() < 4) continue;

    size_t message_size =
        (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
    if (message_size > buffer.size()) continue;

    Message message;
    message.Deserialize(buffer, message_size);
    buffer.erase(buffer.begin(), buffer.begin() + message_size);

    auto cursor_hint = message.find("cursor-hint");
    if (cursor_hint != message.end())
      terminal->SetCursorHint(cursor_hint->second);
    else
      terminal->ClearCursorHint();

    X11_Clear();
#if 0
    for (const auto& kv : message) {
      fprintf(stderr, "%s -> %s\n", kv.first.c_str(), kv.second.c_str());
    }
#endif
  }
}

}  // namespace

void SetupBashServer(int fd) {
  std::thread(ReaderThread, fd).detach();
  socket_fd = fd;
  rl_event_hook = ReadlineHook;
}

void SetupBashClient(Terminal* arg_terminal, int fd) {
  std::thread(ReaderThread, fd).detach();
  terminal = arg_terminal;
  socket_fd = fd;
}
