#ifndef BASH_H_
#define BASH_H_ 1

class Terminal;

void SetupBashServer(int fd);
void SetupBashClient(Terminal* terminal, int fd);

#endif  // !BASH_H_
