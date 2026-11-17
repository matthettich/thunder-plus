// Shared helpers of the MODFX modules: what the top of chains-worklet.js defines (clamp, sinT, rng, soft, rvMix, the Float32Array store) as the JS has it.
#ifndef THUNDER_MODFX_UTIL_H
#define THUNDER_MODFX_UTIL_H
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>

#include "modfx/modfx.h"
#include "rt/jsmath.h"

namespace thunder {
namespace modfx {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kPi = 3.141592653589793;     // Math.PI
const double kSqrt2 = 1.4142135623730951;  // Math.SQRT2

// clamp = (v, a, b) => v < a ? a : v > b ? b : v   (a NaN stays a NaN)
inline double clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
// Math.max / Math.min of two numbers: NaN if either is NaN, +0 over -0 for max and -0 over +0 for min
inline double jmax(double a, double b) { if (a != a || b != b) return kNaN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double jmin(double a, double b) { if (a != a || b != b) return kNaN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }
inline int32_t i32(double x) { return js_to_int32(x); }          // x | 0
inline double jsign(double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : x; }   // Math.sign (NaN, +-0 as they are)
inline bool truthy(double x) { return x != 0 && x == x; }       // a Number in `if (x)`
// A JS `%` on doubles
inline double jmod(double a, double b) { return std::fmod(a, b); }
// A Float32Array read: an index outside the array is undefined in the JS and every sum with it NaN.
inline double rdf(const float* b, int N, double i) { return i >= 0 && i < N && i == std::floor(i) ? (double)b[(int)i] : kNaN; }

// PitchCorrect's missed-read counter: the JS's `++st.miss` is a Number that just keeps counting (`> 2` is all that is asked of it); an int here would overflow (UB) after 2^31 control blocks
// (about 66 days on). It saturates at a cap far above 2: voiced comes out the same, the counter has no other reader. (Hardening, 17 Nov, B1-7.)
constexpr int kMissCap = 1 << 20;
inline int missInc(int m) { return m < kMissCap ? m + 1 : m; }

// ---- sinT: the 4096-point float32 table with linear interpolation -----------------------------------------------------------------------
constexpr int kTS = 4096;
extern const float* const kSin;   // kSin[0..kTS]
inline double sinT(double p) {
  p -= std::floor(p);
  const double x = p * kTS; const int32_t i = i32(x);
  if (i >= kTS) return kNaN;   // p - floor(p) == 1 (a tiny negative p rounds to it): the JS reads SIN[4097], undefined, and its sum is NaN; never a read past the table's 4097 entries
  return (double)kSin[i] + ((double)kSin[i + 1] - (double)kSin[i]) * (x - i);
}

// rng(seed): xorshift32, returns s / 4294967296
class Rng {
 public:
  Rng() = default;
  explicit Rng(uint32_t seed) : s_(seed ? seed : 1) {}
  double operator()() { s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5; return (double)s_ / 4294967296.0; }
 private:
  uint32_t s_ = 1;
};

// dbLin = d => Math.pow(10, d / 20)
inline double dbLin(double d) { return js_pow(10, d / 20); }

// REVERB MIX: rvMix(m, o) -> [dry, wet]
inline void rvMix(double m, double* o) { const double a = (m < 0 ? 0 : m > 1 ? 1 : m) * kPi / 2, s = js_sin(a); o[0] = js_cos(a); o[1] = s * s; }

// `restarted(st, ctx)`: the transport start counter changed (the first call always: st.sn is undefined)
struct Restart { bool seen = false; double sn = 0;
  bool operator()(const Ctx& c) { if (!seen || sn != c.startN) { seen = true; sn = c.startN; return true; } return false; } };

// A Float32Array element: reads give the double the JS sees, a store rounds to float32 (so `float + float` in float arithmetic cannot happen by mistake).
struct FRef {
  float* p;
  operator double() const { return (double)*p; }
  FRef& operator=(double v) { *p = (float)v; return *this; }
  FRef& operator=(const FRef& o) { *p = *o.p; return *this; }
  FRef& operator+=(double v) { *p = (float)((double)*p + v); return *this; }
};
struct In { const float* p; double operator[](int i) const { return (double)p[i]; } };
struct Out { float* p; FRef operator[](int i) const { return FRef{p + i}; } void fill(int n, double v) const { for (int i = 0; i < n; i++) p[i] = (float)v; } };
inline In inp(const IO& io, int k) { return In{io.in[k]}; }
inline Out outp(const IO& io, int k) { return Out{io.out[k]}; }
// the right input: In R if patched, else In (inR(io))
inline In inR(const IO& io, int l, int r) { return In{io.linked[r] ? io.in[r] : io.in[l]}; }

// a Float32Array / Float64Array / Int32Array owned by a module, made once, zero-filled
template <class T> struct ArrT { using Ref = T&; using CRef = const T&; static Ref ref(T* p) { return *p; } static CRef cref(const T* p) { return *p; } };
template <> struct ArrT<float> { using Ref = FRef; using CRef = double; static Ref ref(float* p) { return FRef{p}; } static CRef cref(const float* p) { return (double)*p; } };
template <class T> struct Arr {
  std::unique_ptr<T[]> d; int n = 0;
  Arr() = default;
  explicit Arr(int n_) : d(new T[(size_t)(n_ > 0 ? n_ : 0) + 1]()), n(n_) {}
  void init(int n_) { d.reset(new T[(size_t)(n_ > 0 ? n_ : 0) + 1]()); n = n_; }
  typename ArrT<T>::Ref operator[](int i) { return ArrT<T>::ref(d.get() + i); }
  typename ArrT<T>::CRef operator[](int i) const { return ArrT<T>::cref(d.get() + i); }
  T* get() { return d.get(); }
  void fill(T v) { for (int i = 0; i < n; i++) d[(size_t)i] = v; }
};
using F32 = Arr<float>;

}  // namespace modfx
}  // namespace thunder
#endif
