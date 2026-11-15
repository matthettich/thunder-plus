// Math functions that give the same bits as the web copy on every platform (U05).
// V8 computes Math.exp with its port of fdlibm's exp, not the C library's, and C libraries differ from each other in the last
// bit (glibc, Apple, MSVC, bionic). The sequencer's goldens are compared exactly, so the core must not depend on the
// platform's exp: js_exp is fdlibm's algorithm. (pow and sin join it when the unit that needs them exactly lands.)
// The core is built with floating-point contraction off (no fused multiply-add): see core/CMakeLists.txt.
#ifndef THUNDER_RT_JSMATH_H
#define THUNDER_RT_JSMATH_H

namespace thunder {
double js_exp(double x);
}

#endif
