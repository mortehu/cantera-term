#ifndef MESSAGE_H_
#define MESSAGE_H_ 1

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Message : public std::map<std::string, std::string> {
 public:
  void Serialize(std::vector<uint8_t>* output);

  void Send(int fd);

  void Deserialize(const std::vector<uint8_t>& input, size_t size);

 private:
  void WriteSize(size_t size, std::vector<uint8_t>* output);

  size_t ReadSize(size_t* i, const std::vector<uint8_t>& input);
};

#endif  // !MESSAGE_H_
