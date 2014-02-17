#ifndef EXPR_PARSE_H_
#define EXPR_PARSE_H_ 1

#include <cstdio>
#include <memory>
#include <string>

#include "expr.h"
#include "expression-parser.hh"

namespace expression {

class ParseContext {
 public:
  // Finds the longest suffix of `input` that is a valid expression, evaluates
  // its, and stores the value in `result`.  Returns true on success, and false
  // on error.
  static bool FindAndEval(const std::string& input,
                          std::string::size_type* offset, std::string* result);

  void SetError(const yy::ExpressionParser::location_type& l,
                const std::string& error);

  const std::string& Error() const { return error_; }

  void* Scanner() { return scanner_; }

  Expression* ParseExpression(const std::string& expression);

 private:
  friend class yy::ExpressionParser;

  void* scanner_;
  std::string error_;

  std::unique_ptr<Expression> expression_;
};

}  // namespace expression

#define YY_DECL \
  yy::ExpressionParser::symbol_type yylex(expression::ParseContext* context)

YY_DECL;

#endif  // !EXPR_PARSE_H_
