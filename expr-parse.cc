#include "expr-parse.h"

#include "base/string.h"

namespace expression {

bool ParseContext::FindAndEval(const std::string& input,
                               std::string::size_type* offset,
                               std::string* result,
                               uint16_t flags) {
  ParseContext context;

  // Loop over every suffix either from the beginning or after a space.
  for (std::string::size_type i = 0; i < input.length(); ++i) {
    if (isspace(input[i])) continue;

    // Avoid interpreting "apple" as the expression "e".
    if (i > 0 && isalpha(input[i - 1])) continue;

    std::string suffix = input.substr(i);

    std::unique_ptr<Expression> expr(context.ParseExpression(suffix));

    if (expr && expr->ToString(result)) {
      if ((flags & kIgnoreTrivial) && expr->IsTrivial()) {
        result->clear();
        return false;
      }

      *offset = i;

      return true;
    }
  }

  return false;
}

void ParseContext::SetError(const yy::ExpressionParser::location_type& l,
                            const std::string& error) {
  error_ =
      StringPrintf("%d:%d: %s", l.begin.line, l.begin.column, error.c_str());
}

}  // namespace expression
