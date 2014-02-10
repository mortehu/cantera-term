#ifndef COMMAND_H_
#define COMMAND_H_

#include <string>
#include <vector>

#include <unistd.h>

class Command {
 public:
  Command(int home_fd, const char* command);

  Command& AddArg(const char* arg) {
    args_.push_back(arg);
    return *this;
  }
  Command& AddArg(const std::string& arg) {
    args_.push_back(arg);
    return *this;
  }

  Command& SetStdin(int fd) {
    stdin_ = fd;
    return *this;
  }
  Command& SetStdout(int fd) {
    stdout_ = fd;
    return *this;
  }
  Command& SetStderr(int fd) {
    stderr_ = fd;
    return *this;
  }

  Command& PutEnv(const char* string) {
    environ_.push_back(string);
    return *this;
  }

  pid_t Run();

 private:
  std::string path_;
  int home_fd_;

  std::vector<std::string> args_;
  std::vector<std::string> environ_;

  int stdin_;
  int stdout_;
  int stderr_;
};

#endif /* !COMMAND_H_ */
