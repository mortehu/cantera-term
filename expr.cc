#include "expr.h"

#include <cassert>

#include <mpreal.h>

#include "base/string.h"

namespace expression {

Expression* Expression::CreateNumeric(const std::string& v) {
  Expression* result = new Expression(kNumeric);
  if (v == "pi")
    result->numeric_ = "3.1415926535897932384626433832795";
  else if (v == "phi")
    result->numeric_ = "1.61803398874989484820458683436563811";
  else if (v == "e")
    result->numeric_ = "2.71828182845904523536028747135";
  else
    result->numeric_ = v;
  return result;
}

bool Expression::ToString(std::string* result) const {
  mpfr::mpreal value = Eval();

  if (!mpfr::isfinite(value)) return false;

  *result = value.toString();

  return true;
}

mpfr::mpreal Expression::Eval() const {
  switch (type_) {
    case kNumeric:
      return numeric_;
    case kMinus:
      return -lhs_->Eval();
    case kAdd:
      return lhs_->Eval() + rhs_->Eval();
    case kSubtract:
      return lhs_->Eval() - rhs_->Eval();
    case kMultiply:
      return lhs_->Eval() * rhs_->Eval();
    case kDivide:
      return lhs_->Eval() / rhs_->Eval();
    case kExponentiate:
      return mpfr::pow(lhs_->Eval(), rhs_->Eval());
  }

  return mpfr::mpreal();
}

}  // namespace expression
