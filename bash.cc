// Routines for communicating between terminal emulator and shell.
//
// When the terminal emulator starts the internal bash instance, it is passed a
// Unix socket file descriptor for sending and receiving messages.  This allows
// fast and robust inter-operation.

#include "bash.h"

#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <err.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/file.h"
#include "base/string.h"
#include "terminal.h"
#include "message.h"
#include "x11.h"

namespace {

Terminal* terminal;

}  // namespace

void Bash::Setup(Terminal* arg_terminal) {
  std::string path = TemporaryDirectory("cantera-term");
  path += "/command_socket";

  listen_fd_ = socket(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (-1 == listen_fd_) err(EXIT_FAILURE, "Failed to create UNIX socket");

  struct sockaddr_un unixaddr;
  memset(&unixaddr, 0, sizeof(unixaddr));
  unixaddr.sun_family = AF_UNIX;
  strcpy(unixaddr.sun_path, path.c_str());

  if (-1 == bind(listen_fd_, (struct sockaddr*)&unixaddr, sizeof(unixaddr)))
    err(EXIT_FAILURE, "Failed to bind UNIX socket to address");

  if (-1 == listen(listen_fd_, SOMAXCONN))
    err(EXIT_FAILURE, "Failed to listen on UNIX socket");

  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);

  if (-1 == epoll_fd_) err(EXIT_FAILURE, "Failed to create epoll descriptor");

  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = EPOLLIN;
  ev.data.fd = listen_fd_;

  if (-1 == epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev))
    err(EXIT_FAILURE, "Failed to add UNIX socket to epoll");

  terminal = arg_terminal;

  setenv("CANTERA_TERM_COMMAND_SOCKET", path.c_str(), 1);
}

void Bash::Start() {
  std::thread(&Bash::ReaderThread, this).detach();
}

void Bash::ReaderThread() {
  for (;;) {
    struct epoll_event ev;
    int nfds;

    if (-1 == (nfds = epoll_wait(epoll_fd_, &ev, 1, -1))) {
      if (errno == EINTR) continue;

      break;
    }

    if (!nfds) continue;

    if (ev.data.fd == listen_fd_) {
      int fd = accept(listen_fd_, NULL, NULL);

      if (-1 == fd) continue;

      ev.events = EPOLLIN;
      ev.data.fd = fd;

      if (-1 == epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev)) close(fd);
    } else {
      ProcessClient(ev.data.fd);
    }
  }
}

void Bash::ProcessClient(int fd) {
  auto client_iterator = clients.find(fd);

  char tmp[4096];
  ssize_t ret = read(fd, tmp, sizeof(tmp));

  if (ret <= 0) {
    if (client_iterator != clients.end())
      clients.erase(client_iterator);
    close(fd);
    return;
  }

  if (client_iterator == clients.end())
    client_iterator = clients.insert(std::make_pair(fd, Client())).first;

  Client* client = &client_iterator->second;

  client->buffer.insert(client->buffer.end(), tmp, tmp + ret);

  if (client->buffer.size() < 4) return;

  size_t message_size = (client->buffer[0] << 24) | (client->buffer[1] << 16) |
                        (client->buffer[2] << 8) | client->buffer[3];
  if (message_size > client->buffer.size()) return;

  Message message;
  message.Deserialize(client->buffer, message_size);
  client->buffer.erase(client->buffer.begin(),
                       client->buffer.begin() + message_size);

  for (const auto& kv : message)
    fprintf(stderr, "%s: %s\n", kv.first.c_str(), kv.second.c_str());

  auto cursor_hint = message.find("cursor-hint");
  if (cursor_hint != message.end())
    terminal->SetCursorHint(cursor_hint->second);
  else
    terminal->ClearCursorHint();

  X11_Clear();
}
