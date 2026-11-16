// Text to double, the same on every platform: the vendored fast_float (third_party/PINS.md), never strtod (locale, platform rounding)
// and never std::from_chars for double (Android's NDK libc++ has none). Header only; the includer needs third_party on its include path.
#ifndef THUNDER_RT_DBL_H
#define THUNDER_RT_DBL_H
#include <cmath>
#include <cstddef>
#include <string_view>

#include "fast_float/fast_float.h"

namespace thunder {

// The decimal grammar [+-]digits[.digits][e[+-]digits] (also ".5" and "5."), the WHOLE range or false. A '+' is allowed here (fast_float
// takes none), but not before a '-'. Too big gives +-infinity, too small the correctly rounded subnormal or 0, as strtod does.
inline bool parse_double(const char* b, const char* e, double* out) {
  if (b < e && *b == '+') { b++; if (b < e && (*b == '-' || *b == '+')) return false; }
  if (b >= e) return false;
  double v = 0;
  const auto r = fast_float::from_chars(b, e, v);
  if (r.ptr != e || (r.ec != std::errc() && r.ec != std::errc::result_out_of_range)) return false;
  *out = v;
  return true;
}
inline bool parse_double(std::string_view s, double* out) { return parse_double(s.data(), s.data() + s.size(), out); }

// JavaScript's Number(string) (StringToNumber): trims white space, "" is 0, "Infinity", 0x / 0o / 0b integers, else the decimal grammar;
// NaN when it is none of these.
inline double js_string_to_number(std::string_view s) {
  auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
  while (!s.empty() && ws(s.front())) s.remove_prefix(1);
  while (!s.empty() && ws(s.back())) s.remove_suffix(1);
  if (s.empty()) return 0;
  if (s == "Infinity" || s == "+Infinity") return HUGE_VAL;
  if (s == "-Infinity") return -HUGE_VAL;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X' || s[1] == 'o' || s[1] == 'O' || s[1] == 'b' || s[1] == 'B')) {
    const int base = (s[1] == 'x' || s[1] == 'X') ? 16 : (s[1] == 'o' || s[1] == 'O') ? 8 : 2;
    double v = 0;
    for (size_t i = 2; i < s.size(); i++) {
      const char c = s[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
      if (d >= base) return std::nan("");
      v = v * base + d;
    }
    return v;
  }
  double v;
  return parse_double(s, &v) ? v : std::nan("");
}

}  // namespace thunder
#endif
