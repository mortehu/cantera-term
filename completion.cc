#include "completion.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace {

std::wstring CurrentLinePrefix(const Terminal::State& state) {
  std::wstring result;
  for (size_t x = 0; x < state.cursor_x; ++x)
    result.push_back(state.chars[state.cursor_y * state.width + x]);

  return result;
}

class NullCompletion : public Completion {
 public:
  void Train(const Terminal::State& state, char ch) override {}

  std::vector<char> Predict(const Terminal::State& state) override {
    return std::vector<char>();
  }
};

class BasicCompletion : public Completion {
 public:
  void Train(const Terminal::State& state, char ch) override {
    auto prefix = CurrentLinePrefix(state);

    if (lut_.size() >= kMaxSize) {
      lut_.clear();
      fprintf(stderr, "Ran out of space\n");
    }

    lut_.emplace(std::move(prefix), ch);
  }

  std::vector<char> Predict(const Terminal::State& state) override {
    auto prefix = CurrentLinePrefix(state);

    std::vector<char> result;

    for (;;) {
      const auto i = lut_.find(prefix);
      if (i == lut_.end()) break;

      const auto prediction = i->second;

      result.push_back(prediction);
      prefix.push_back(prediction);
    }

    return result;
  }

 private:
  static const size_t kMaxSize = 100000;

  std::unordered_map<std::wstring, char> lut_;
};

}  // namespace

Completion::~Completion() {}

std::unique_ptr<Completion> Completion::CreateNullCompletion() {
  return std::make_unique<NullCompletion>();
}

std::unique_ptr<Completion> Completion::CreateBasicCompletion() {
  return std::make_unique<BasicCompletion>();
}
