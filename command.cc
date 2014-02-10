#include "command.h"

#include <fcntl.h>

Command::Command(int home_fd, const char* command)
    : path_(".cantera/commands/"),
      home_fd_(home_fd),
      stdin_(-1),
      stdout_(-1),
      stderr_(-1) {
  path_.append(command);
  args_.push_back(path_);
}

pid_t Command::Run() {
  int command_fd;
  pid_t child;

  if (-1 == (command_fd = openat(home_fd_, path_.c_str(), O_RDONLY))) return -1;

  if (-1 == (child = fork())) return -1;

  if (!child) {
    std::vector<const char*> c_args;
    for (const std::string& arg : args_)
      c_args.push_back(arg.c_str());
    c_args.push_back(nullptr);

    std::vector<const char*> c_environ;
    for (char** env = environ; *env; ++env)
      c_environ.push_back(*env);
    for (const std::string& env : environ_)
      c_environ.push_back(env.c_str());
    c_environ.push_back(nullptr);

    if (stdin_ != -1) dup2(stdin_, STDIN_FILENO);
    if (stdout_ != -1) dup2(stdout_, STDOUT_FILENO);
    if (stderr_ != -1) dup2(stderr_, STDERR_FILENO);

    fexecve(command_fd, const_cast<char* const*>(c_args.data()),
            const_cast<char* const*>(c_environ.data()));

    _exit(EXIT_FAILURE);
  }

  close(command_fd);

  return child;
}
