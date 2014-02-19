#include <cstdlib>

#include "expr-parse.h"

namespace {

void TestExpression(const std::string& input, const std::string& expected) {
  expression::ParseContext parse_context;

  std::unique_ptr<expression::Expression> expression(
      parse_context.ParseExpression(input));

  if (!expression) {
    fprintf(stderr, "Failed to parse \"%s\": %s\n", input.c_str(),
            parse_context.Error().c_str());

    exit(EXIT_FAILURE);
  }

  std::string result;

  if (!expression->ToString(&result)) {
    fprintf(stderr, "Failed to evaluate \"%s\": %s\n", input.c_str(),
            parse_context.Error().c_str());

    exit(EXIT_FAILURE);
  }

  if (result != expected) {
    fprintf(stderr,
            "Unexpected result for \"%s\".  Expected \"%s\", got \"%s\"\n",
            input.c_str(), expected.c_str(), result.c_str());
    exit(EXIT_FAILURE);
  }
}

void TestAutoExpression(const std::string& input, const std::string& expected) {
  std::string result;
  std::string::size_type offset;

  if (!expression::ParseContext::FindAndEval(input, &offset, &result)) {
    if (expected.empty()) return;
    fprintf(stderr, "Did not find an expression in \"%s\"", input.c_str());
    exit(EXIT_FAILURE);
  }

  if (result != expected) {
    fprintf(stderr,
            "Unexpected result for \"%s\".  Expected \"%s\", got \"%s\"\n",
            input.c_str(), expected.c_str(), result.c_str());
    exit(EXIT_FAILURE);
  }
}

}  // namespace

int main(int argc, char** argv) {
  TestExpression("1", "1");
  TestExpression("-1", "-1");

  TestExpression("1+2", "3");
  TestExpression("1 + 2", "3");
  TestExpression(" 1  +  2 ", "3");

  TestExpression("2*10", "20");
  TestExpression("2/10", "0.2");
  TestExpression("2^10", "1024");
  TestExpression("2**10", "1024");

  TestExpression("1+2*3", "7");
  TestExpression("1+(2*3)", "7");
  TestExpression("(1+2)*3", "9");
  TestExpression("(((1)+(2))*(3))", "9");

  TestExpression("pi/pi", "1");

  TestExpression("e**pi-pi", "19.9990999791895");

  TestAutoExpression("[foo@bar:~/xyz/2]$ 2*10", "20");

  return EXIT_SUCCESS;
}
