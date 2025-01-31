#ifndef EXPR_H_
#define EXPR_H_ 1

#include <cstdio>
#include <forward_list>
#include <memory>
#include <string>

#include "mpreal.h"

namespace expression {

class Expression {
 public:
  enum ExpressionType {
    kInvalid,

    // Constants
    kNumeric,
    kString,
    kTime,
    kInterval,

    // Operators
    kMinus,
    kAdd,
    kSubtract,
    kMultiply,
    kDivide,
    kModulus,
    kExponentiate,

    // Other functions
    kCos,
    kSin,
    kLog,
    kHex,
  };

  typedef std::forward_list<Expression*> List;

  Expression(ExpressionType type) : type_(type) {}
  Expression(ExpressionType type, Expression* lhs, Expression* rhs = nullptr)
      : type_(type), lhs_(lhs), rhs_(rhs) {}

  static Expression* CreateNumeric(const std::string& v);
  static Expression* CreateTime(const std::string& v);

  ExpressionType Type() const { return type_; }

  bool ToString(std::string* result) const;

  bool IsTrivial() const;

 private:
  struct Value {
    Value() : type(kInvalid) {}
    Value(const std::string& v) : type(kString), string(v) {}
    Value(const mpfr::mpreal& v) : type(kNumeric), numeric(v) {}
    Value(ExpressionType type, const mpfr::mpreal& v) : type(type), numeric(v) {}

    enum ExpressionType type;
    std::string string;
    mpfr::mpreal numeric;
  };

  Value Eval() const;

  ExpressionType type_;

  // For kNumeric.
  mpfr::mpreal numeric_;

  // For kString and kTime.
  std::string string_;

  // First argument.
  std::unique_ptr<Expression> lhs_;

  // Second argument.
  std::unique_ptr<Expression> rhs_;
};

}  // namespace expression

#endif  // !EXPR_H_
