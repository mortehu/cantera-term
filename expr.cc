#include "expr.h"

#include <mpreal.h>
#include <cassert>

#include "base/string.h"

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

Expression* Expression::CreateTime(const std::string& v) {
  Expression* result = new Expression(kTime);
  if (v == "now") {
    result->numeric_ = time(nullptr);
  } else if (v.size() == 10 || v.size() == 16 || v.size() == 19) {
    tm tmTime;
    memset(&tmTime, 0, sizeof(tmTime));
    strptime(v.c_str(), "%Y-%m-%d %H:%M:%S", &tmTime);
    result->numeric_ = mktime(&tmTime);
  }
  else if (v.size() == 5 || v.size() == 8)
  {
    const auto now = time(nullptr);
    tm tmTime;
    memset(&tmTime, 0, sizeof(tmTime));
    localtime_r(&now, &tmTime);
    strptime(v.c_str(), "%H:%M:%S", &tmTime);
    result->numeric_ = mktime(&tmTime);
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

    case kTime: {
      if (!mpfr::isint(value.numeric)) return false;
      time_t ttTime = value.numeric.toLong();
      tm tmTime;
      localtime_r(&ttTime, &tmTime);
      char buffer[64];
      if (0 == strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmTime))
        return false;
      result->assign(buffer);
    } break;

    case kInterval: {
      if (!mpfr::isint(value.numeric)) return false;
      std::ostringstream buf;

      auto seconds = value.numeric.toLong();
      if (seconds < 0) buf << '-';
      seconds = std::abs(seconds);
      auto minutes = seconds / 60;
      seconds %= 60;
      auto hours = minutes / 60;
      minutes %= 60;
      auto days = hours / 24;
      hours %= 24;
      auto weeks = days / 7;
      days %= 7;

      bool first = true;
      if (weeks) {
        buf << weeks << " weeks";
        first = false;
      }
      if (days) {
        if (!first) buf << ", ";
        buf << days << " days";
        first = false;
      }
      if (hours) {
        if (!first) buf << ", ";
        buf << hours << " hours";
        first = false;
      }
      if (minutes) {
        if (!first) buf << ", ";
        buf << minutes << " minutes";
        first = false;
      }
      if (seconds) {
        if (!first) buf << ", ";
        buf << seconds << " seconds";
        first = false;
      }
      if (first) buf << '0';

      result->assign(buf.str());
    } break;

    default:
      return false;
  }

  return true;
}

bool Expression::IsTrivial() const {
  switch (type_) {
    case kInvalid:
    case kNumeric:
    case kString:
      return true;

    default:
      return false;
  }
}

Expression::Value Expression::Eval() const {
  Value lhs, rhs;
  if (lhs_) lhs = lhs_->Eval();
  if (rhs_) rhs = rhs_->Eval();

  switch (type_) {
    case kInvalid:
      return {};
    case kNumeric:
    case kTime:
    case kInterval:
      return {type_, numeric_};
    case kString:
      return string_;
    case kMinus:
      if (lhs.type != kNumeric) return {};
      return -lhs.numeric;
    case kAdd:
      if ((lhs.type == kTime || lhs.type == kNumeric) && rhs.type == kNumeric)
        return {lhs.type, lhs.numeric + rhs.numeric};
      return {};
    case kSubtract:
      if (lhs.type == kTime && rhs.type == kTime)
        return {kInterval, lhs.numeric - rhs.numeric};
      if ((lhs.type == kTime || lhs.type == kNumeric) && rhs.type == kNumeric)
        return {lhs.type, lhs.numeric - rhs.numeric};
      return {};
    case kMultiply:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return {};
      return lhs.numeric * rhs.numeric;
    case kDivide:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return {};
      return lhs.numeric / rhs.numeric;
    case kModulus:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return {};
      return mpfr::fmod(lhs.numeric, rhs.numeric);
    case kExponentiate:
      if (lhs.type != kNumeric || rhs.type != kNumeric) return {};
      return mpfr::pow(lhs.numeric, rhs.numeric);
    case kCos:
      if (lhs.type != kNumeric) return {};
      return mpfr::cos(lhs.numeric);
    case kSin:
      if (lhs.type != kNumeric) return {};
      return mpfr::sin(lhs.numeric);
    case kLog:
      if (lhs.type != kNumeric) return {};
      return mpfr::log(lhs.numeric);
    case kHex:
      if (lhs.type != kNumeric) return {};
      return StringPrintf("0x%lx", lhs.numeric.toULong());
  }

  return {};
}

}  // namespace expression
