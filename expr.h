#ifndef EXPR_H_
#define EXPR_H_ 1

#include <cstdio>
#include <forward_list>
#include <memory>
#include <string>

#include <mpreal.h>

namespace expression {

class Expression {
 public:
  enum ExpressionType {
    kNumeric,
    kMinus,
    kAdd,
    kSubtract,
    kMultiply,
    kDivide,
    kExponentiate,
  };

  typedef std::forward_list<Expression*> List;

  Expression(ExpressionType type) : type_(type) {}
  Expression(ExpressionType type, Expression* lhs, Expression* rhs)
      : type_(type), lhs_(lhs), rhs_(rhs) {}

  static Expression* CreateNumeric(const std::string& v);

  ExpressionType Type() const { return type_; }

  bool ToString(std::string* result) const;

 private:
  mpfr::mpreal Eval() const;

  ExpressionType type_;

  // For kNumeric.
  mpfr::mpreal numeric_;

  // First argument.
  std::unique_ptr<Expression> lhs_;

  // Second argument.
  std::unique_ptr<Expression> rhs_;
};

}  // namespace expression

#endif  // !EXPR_H_
