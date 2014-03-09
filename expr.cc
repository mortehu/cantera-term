#include "expr.h"

#include <cassert>


#include "base/string.h"
#include "3rd_party/mpfrc++/mpreal.h"

namespace expression {

const mp_prec_t kPrecision = 160;  // Bits

Expression* Expression::CreateNumeric(const std::string& v) {
  Expression* result = new Expression(kNumeric);
  if (!v.compare(0, 2, "0x")) {
    result->numeric_ = mpfr::mpreal(v.substr(2), kPrecision, 16);
  } else if (v == "pi") {
    result->numeric_ = "3.1415926535897932384626433832795";
  } else if (v == "phi") {
    result->numeric_ = "1.61803398874989484820458683436563811";
  } else if (v == "e") {
    result->numeric_ = "2.71828182845904523536028747135";
  } else {
    result->numeric_ = mpfr::mpreal(v, kPrecision, 10);
  }
  return result;
}

bool Expression::ToString(std::string* result) const {
  Value value = Eval();

  switch (value.type) {
    case kNumeric:
      if (!mpfr::isfinite(value.numeric)) return false;
      *result = value.numeric.toString();
      break;

    case kString:
      *result = value.string;
      break;

    default:
      return false;
  }

  return true;
}

Expression::Value Expression::Eval() const {
  Value lhs, rhs;
  if (lhs_) lhs = lhs_->Eval();
  if (rhs_) rhs = rhs_->Eval();

  switch (type_) {
    case kInvalid:
      return Value();
    case kNumeric:
      return numeric_;
    case kString:
      return string_;
    case kMinus:
      if (lhs.type != kNumeric) return Value();
      return -lhs.numeric;
    case kAdd:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return Value();
      return lhs.numeric + rhs.numeric;
    case kSubtract:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return Value();
      return lhs.numeric - rhs.numeric;
    case kMultiply:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return Value();
      return lhs.numeric * rhs.numeric;
    case kDivide:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return Value();
      return lhs.numeric / rhs.numeric;
    case kModulus:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return Value();
      return mpfr::fmod(lhs.numeric, rhs.numeric);
    case kExponentiate:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return Value();
      return mpfr::pow(lhs.numeric, rhs.numeric);
    case kCos:
      if (lhs.type != kNumeric) return Value();
      return mpfr::cos(lhs.numeric);
    case kSin:
      if (lhs.type != kNumeric) return Value();
      return mpfr::sin(lhs.numeric);
    case kLog:
      if (lhs.type != kNumeric) return Value();
      return mpfr::log(lhs.numeric);
    case kHex:
      if (lhs.type != kNumeric) return Value();
      return StringPrintf("0x%lx", lhs.numeric.toULong());
  }

  return Value();
}

}  // namespace expression
