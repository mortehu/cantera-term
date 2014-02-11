#include "message.h"

#include <cassert>

#include <err.h>
#include <unistd.h>

void Message::Serialize(std::vector<uint8_t>* output) {
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

void Message::Send(int fd) {
  std::vector<uint8_t> buffer;
  Serialize(&buffer);

  size_t offset = 0;

  while (offset < buffer.size()) {
    ssize_t ret = write(fd, &buffer[offset], buffer.size() - offset);
    if (ret == -1) err(EXIT_FAILURE, "Writing to command socket failed");
    offset += ret;
  }
}

void Message::Deserialize(const std::vector<uint8_t>& input, size_t size) {
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

void Message::WriteSize(size_t size, std::vector<uint8_t>* output) {
  if (size > 0x3fff) output->push_back(0x80 | (size >> 14));
  if (size > 0x7f) output->push_back(0x80 | (size >> 7));
  output->push_back(size & 0x7f);
}

size_t Message::ReadSize(size_t* i, const std::vector<uint8_t>& input) {
  size_t result = 0;
  do {
    result = (result << 7) | input[*i];
  } while (input[(*i)++] & 0x80);
  return result;
}
