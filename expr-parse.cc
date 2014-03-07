#include "expr-parse.h"

#include "base/string.h"

namespace expression {

bool ParseContext::FindAndEval(const std::string& input,
                               std::string::size_type* offset,
                               std::string* result) {
  ParseContext context;

  for (std::string::size_type i = 0; i + 1 < input.length(); ++i) {
    if (isspace(input[i])) continue;

    std::string suffix = input.substr(i);

    std::unique_ptr<Expression> expr(context.ParseExpression(suffix));

    if (expr && expr->ToString(result)) {
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
