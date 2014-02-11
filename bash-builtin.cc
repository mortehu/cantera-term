#include <thread>

#include <readline/readline.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "base/string.h"
#include "bash.h"
#include "message.h"

extern "C" {

#include <bash/config.h>
#include <bash/bashline.h>
#include <bash/bashtypes.h>
#include <bash/builtins.h>
#include <bash/command.h>
#include <bash/shell.h>
}

namespace {

int fd;

void TokenizeCommand(const std::string& command,
                     std::vector<std::string>* result) {
  WORD_LIST* words =
      split_at_delims(const_cast<char*>(command.c_str()), command.length(),
                      const_cast<char*>(" "), 0, 0, nullptr, nullptr);

  for (WORD_LIST* p = words; p; p = p->next) {
    result->push_back(p->word->word);
  }
}

bool DequoteText(const std::string& input, std::string* result) {
  *result = input;

  return true;
}

bool GetValue(const char* name, std::string* result) {
  const char* str = get_string_value(name);
  if (!str) return false;
  *result = str;
  return true;
}

int ReadlineHook() {
  static std::string line_buffer;
  Message msg;
  std::vector<std::string> command;

  if (!rl_line_buffer) rl_line_buffer = const_cast<char*>("");
  if (line_buffer == rl_line_buffer) return 0;
  line_buffer = rl_line_buffer;

  TokenizeCommand(line_buffer, &command);

  if (command.empty()) goto done;

  if (command[0] == "cd") {
    std::string path = (command.size() > 1) ? command[1] : std::string();

    if (!DequoteText(path, &path)) goto done;

    std::string expanded_path;
    if (path.empty()) {
      if (!GetValue("HOME", &expanded_path)) goto done;
    } else if (path == "-") {
      if (!GetValue("OLDPWD", &expanded_path)) goto done;
    } else {
      expanded_path = path;
    }

    std::unique_ptr<char[]> tmp(new char[expanded_path.length() + 1]);
    strcpy(tmp.get(), expanded_path.c_str());

    char* canonical_path = sh_canonpath(full_pathname(tmp.get()),
                                        PATH_CHECKDOTDOT | PATH_CHECKEXISTS);
    if (!canonical_path) goto done;

    msg["cursor-hint"] = canonical_path;
  }

done:
  msg.Send(fd);

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
  }

  close(fd);
}

__attribute__((constructor)) void Setup() {
  char* socket_path = getenv("CANTERA_TERM_COMMAND_SOCKET");
  if (!socket_path) {
    fprintf(stderr, "CANTERA_TERM_COMMAND_SOCKET not set\n");
    return;
  }

  fd = socket(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (-1 == fd) {
    fprintf(stderr, "Failed to create UNIX socket: %s\n", strerror(errno));
    return;
  }

  struct sockaddr_un unixaddr;
  memset(&unixaddr, 0, sizeof(unixaddr));
  unixaddr.sun_family = AF_UNIX;
  strcpy(unixaddr.sun_path, socket_path);

  if (-1 == connect(fd, (struct sockaddr*)&unixaddr, sizeof(unixaddr))) {
    fprintf(stderr,
            "Failed to connect to cantera-term command socket '%s': %s\n",
            socket_path, strerror(errno));
    return;
  }
  std::thread(ReaderThread, fd).detach();

  rl_event_hook = ReadlineHook;
}

}  // namespace

int cantera_builtin(WORD_LIST* list) { return (EXECUTION_SUCCESS); }

static char* cantera_doc[] = {
    const_cast<char*>("Cantera Terminal builtin command."),
    nullptr};

struct builtin cantera_struct = {const_cast<char*>("cantera"), cantera_builtin,
                                 BUILTIN_ENABLED,              cantera_doc,
                                 const_cast<char*>("cantera"), nullptr};
