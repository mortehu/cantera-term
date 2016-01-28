#ifndef COMPLETION_H_
#define COMPLETION_H_ 1

#include <memory>
#include <vector>

#include "terminal.h"

class Completion {
public:
  static std::unique_ptr<Completion> CreateNullCompletion();

  static std::unique_ptr<Completion> CreateBasicCompletion();

  virtual ~Completion();

  virtual void Train(const Terminal::State &state, char ch) = 0;

  virtual std::vector<char> Predict(const Terminal::State &state) = 0;
};

#endif // !COMPLETION_H_
