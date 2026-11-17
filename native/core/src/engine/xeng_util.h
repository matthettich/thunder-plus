// U28: KeysUtil (bank / add / finish / rnd) and the helpers every XENG engine uses. Internal to core/src/engine/xeng_*.cpp.
#ifndef THUNDER_ENGINE_XENG_UTIL_H
#define THUNDER_ENGINE_XENG_UTIL_H
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/layers.h"
#include "engine/xeng.h"
#include "rt/jsmath.h"

namespace thunder {
namespace xe {
using l23::mx;   // Math.max(a, b): NaN wins, +0 beats -0
using l23::mn;   // Math.min(a, b)
constexpr double kPi = 3.141592653589793;
inline double c01(double v) { return mx(0, mn(1, v)); }     // Math.max(0, Math.min(1, v))
inline double orz(double x) { return (x != 0 && x == x) ? x : 0; }   // x || 0
inline double orv(double x, double d) { return (x != 0 && x == x) ? x : d; }   // x || d
inline int32_t i32(double x) { return js_to_int32(x); }     // x | 0
// A table index from a rounded and clamped parameter: the parameters are finite (normX), so the clamp is exact; NaN (not reachable from a song) is the low end, never a cast of NaN.
inline int cidx(double v, int lo, int hi) { if (!(v == v)) return lo; return v < lo ? lo : v > hi ? hi : (int)v; }
inline bool truthy(double x) { return x != 0 && x == x; }   // JS truthiness of a Number
constexpr size_t kRefuse = (size_t)-1;   // an engine's answer for input the JS cannot render either (its Float32Array would throw RangeError): the voice is refused (kEngUnsupported)
constexpr double kMaxLine = 4194304;     // the longest delay line a voice may ask for, in floats (16 MB): the JS would throw above 2^32 and run out of memory far below; 4 M is a note of 0.012 Hz at 48 kHz
inline double jabs(double x) { return std::fabs(x); }
// `new Float32Array(N)` size check shared by every engine: N must be representable; (size_t)N is returned, 0 if not
inline bool okN(double N) { return N >= 1 && N < 4e9; }

// KeysUtil.rnd(seed): s = (s * 1664525 + 1013904223) >>> 0; s / 4294967296 (the product is below 2^53: exact in doubles, so uint32 arithmetic gives the same bits)
struct KeysRnd { uint32_t s; explicit KeysRnd(double seed) : s(js_to_uint32(seed)) {} double operator()() { s = s * 1664525u + 1013904223u; return (double)s / 4294967296.0; } };

// KeysUtil.bank / add: rotating phasors, arrays of doubles (JS Arrays, not typed arrays: no float32 rounding)
constexpr int kBankMax = 256;   // partials count is clamped to 256, the piano makes at most 64 partials x 3 strings
struct KeysBank {
  double sr, c[kBankMax], s[kBankMax], wc[kBankMax], ws[kBankMax], a[kBankMax]; int n = 0;
  explicit KeysBank(double sr_) : sr(sr_) {}
  int add(double f, double phase) { const double w = 2 * kPi * f / sr; c[n] = js_cos(phase); s[n] = js_sin(phase); wc[n] = js_cos(w); ws[n] = js_sin(w); a[n] = 0; return n++; }
};

// The tail every engine ends with: end = last sample with |x| >= 1e-4 (at least 1), a fade of round(sr * fade) samples into the new end. Returns the new length.
size_t trim_fade(float* out, size_t N, double sr, double fade);
// KeysUtil.finish(out, p, sr, tailFade): peak-normalise to 0.8 * level, drive (tanh), then trim_fade. Returns the length.
size_t finish(float* out, size_t N, double level, double drive, double sr, double tailFade);
// The peak / level / drive pass alone (the engines that mix their own tail in between: Modal, Swarm, Drift use the same lines inline)
inline double peak_of(const float* out, size_t N) { double pk = 0; for (size_t i = 0; i < N; i++) pk = mx(pk, jabs((double)out[i])); return pk; }
void level_drive(float* out, size_t N, double level, double drive, double scale);   // lv = Math.pow(10, (level || 0) / 20) * scale, then the tanh drive: scale is (pk > 0 ? 0.8 / pk : 1), Tone's is 0.5

// ---- U29 -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
inline double cl3(double v, double a, double b) { return v < a ? a : v > b ? b : v; }   // the engines' `cl = (v, a, b) => v < a ? a : v > b ? b : v` (NaN passes through)
// A typed array read: a read outside the array is `undefined` in the JS and the arithmetic round it gives NaN.
inline double tabget(const float* T, int n, int32_t i) { return (i >= 0 && i < n) ? (double)T[i] : std::nan(""); }
inline double dget(const double* T, int n, double i) { return (i == i && i >= 0 && i < n) ? T[(int)i] : std::nan(""); }
inline double fget(const float* T, int n, double i) { return (i == i && i >= 0 && i < n) ? (double)T[(int)i] : std::nan(""); }

// The scratch arena of the second set: zeroed, 16-aligned blocks that stay allocated for the next render of this thread (reset() makes them free again), so the same render a second time allocates nothing.
// A block is never moved: pointers stay valid until the next reset(). A take above kArenaMax bytes gives nullptr (the engine then refuses the voice).
constexpr size_t kArenaMax = (size_t)1 << 31;
class XArena {
 public:
  XArena() = default; XArena(const XArena&) = delete; XArena& operator=(const XArena&) = delete;
  ~XArena() { release(); }
  void reset();
  void release();
  void* take(size_t bytes);
  template <class T> T* get(size_t n) { if (n > kArenaMax / sizeof(T)) return nullptr; return static_cast<T*>(take(n * sizeof(T))); }
 private:
  struct Blk { char* p; size_t cap, used; };
  Blk* blk_ = nullptr; size_t n_ = 0, cap_ = 0;
};
XArena& xarena();   // the calling thread's

// SpecUtil (index.html): the in-place radix-2 FFT (re / im of length n, a power of two; inv = the e^{+i} kernel, unscaled; twiddles cached per thread), and its base64 reader.
void spec_fft(double* re, double* im, int n, bool inv);
// SpecUtil.unb64: the string with everything outside A-Za-z0-9+/ removed, decoded; the bytes are taken from `ar`, their count in *n (nullptr from the arena when it is out of room)
uint8_t* spec_unb64(XArena& ar, const std::string& s, size_t* n);
}  // namespace xe
}  // namespace thunder
#endif
