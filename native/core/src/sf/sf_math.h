// U32: the few JS number helpers the SoundFont player needs beyond rt/jsmath.h.
#ifndef THUNDER_SF_SF_MATH_H
#define THUNDER_SF_SF_MATH_H
#include <cmath>

namespace thunder {
namespace sf {
double sf_log10(double x);   // Math.log10: V8 12.4's ieee754::log10 (fdlibm's old e_log10 on top of ieee754::log), bit for bit (checked against Node in ctest `sf`)
// Math.max / Math.min of two numbers: NaN wins, and +0 beats -0 for max (-0 beats +0 for min)
inline double jmax(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double jmin(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }
inline double jclamp(double v, double a, double b) { return jmax(a, jmin(b, v)); }   // the page's clamp(v, a, b)
inline double orv(double x, double d) { return (x == 0 || x != x) ? d : x; }          // x || d
inline double jsign(double x) { return x > 0 ? 1 : x < 0 ? -1 : x; }                  // Math.sign
}  // namespace sf
}  // namespace thunder
#endif
