#ifndef BASH_H_
#define BASH_H_ 1

#include <cstdint>
#include <map>
#include <vector>

class Terminal;

class Bash {
 public:
  void Setup(Terminal* terminal);
  void Start();

 private:
  struct Client {
    std::vector<uint8_t> buffer;
  };

  void ReaderThread();
  void ProcessClient(int fd);

  std::map<int, Client> clients;

  int epoll_fd_;
  int listen_fd_;
};

#endif  // !BASH_H_
