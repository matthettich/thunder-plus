// Math functions that give the same bits as the web copy on every platform (U05, U07).
// V8 computes Math.exp and Math.pow with its port of fdlibm, not the C library's, and C libraries differ from each other in the
// last bit (glibc, Apple, MSVC, bionic). The sequencer's goldens are compared exactly, so the core must not depend on the
// platform's exp or pow: js_exp and js_pow are fdlibm's algorithms. (cos, sin, tan and log joined them with U15: fdlibm.cpp.)
// The core is built with floating-point contraction off (no fused multiply-add): see core/CMakeLists.txt.
#ifndef THUNDER_RT_JSMATH_H
#define THUNDER_RT_JSMATH_H
#include <cmath>
#include <cstdint>

namespace thunder {
double js_exp(double x);
double js_pow(double x, double y);   // Math.pow: fdlibm's e_pow (V8's ieee754::pow), bit for bit (U07; proven in ctest `runtime`)
// Math.cos / sin / tan / log: fdlibm's, as V8 12.4 (Node 22) computes them, bit for bit (U15; core/src/rt/fdlibm.cpp, proven in ctest `libm`).
// (Newer V8 builds moved some of these to the LLVM libc; the reference of this port is Node 22, see the note in fdlibm.cpp.)
double js_cos(double x);
double js_sin(double x);
double js_tan(double x);
double js_log(double x);
double js_tanh(double x);   // Math.tanh: fdlibm's (U16), V8 12.4 bit for bit
double js_log2(double x);   // Math.log2: V8 12.4's ieee754::log2 (U19), bit for bit
double js_atan2(double y, double x);   // Math.atan2: V8 12.4's ieee754::atan2 (U20), bit for bit
double js_hypot3(double a, double b, double c);   // Math.hypot(a, b, c): V8 12.4's builtin (scaled, Kahan-summed; U20)

// Math.round: the nearest integer, ties towards +Infinity; -0 for -0.5 <= x < 0 (and for -0); NaN and the infinities as they are.
// (Not floor(x + 0.5): that is wrong for 0.49999999999999994 and for odd integers at and above 2^52.)
inline double js_round(double x) {
  if (!(x == x) || std::isinf(x)) return x;
  const double f = std::floor(x);
  if (x - f >= 0.5) return f + 1 == 0 ? std::copysign(0.0, x) : f + 1;   // (-0.5 <= x < 0: -0)
  return f;                                                               // (floor(-0) is -0)
}
// ToInt32 / ToUint32 (`x | 0`, `x >> n`, `x >>> 0`): modulo 2^32 of the truncated value; NaN and the infinities give 0.
inline uint32_t js_to_uint32(double x) {
  if (!(x == x) || std::isinf(x)) return 0;
  if (x >= -2147483648.0 && x <= 4294967295.0) return (uint32_t)(int64_t)x;   // (truncates; the common case)
  double m = std::fmod(std::trunc(x), 4294967296.0); if (m < 0) m += 4294967296.0;
  return (uint32_t)m;
}
inline int32_t js_to_int32(double x) { return (int32_t)js_to_uint32(x); }
// Math.imul on values already brought to 32 bits.
inline uint32_t js_imul(uint32_t a, uint32_t b) { return a * b; }

// Number.prototype.toString (radix 10) / what JSON.stringify writes for a finite number: the shortest digits that read back as
// the same double, laid out as ECMAScript's Number::toString does ("0.000001", "1e-7", "1e+21", "-0" as "0"). NaN gives "NaN",
// the infinities "Infinity" / "-Infinity" (JSON.stringify writes null for those: the caller's business).
// Writes at most 25 characters and a 0 into buf; returns the length.
int js_number_to_string(double v, char* buf);
}

#endif
