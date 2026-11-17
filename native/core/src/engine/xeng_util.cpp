// U28: KeysUtil.finish and the shared tail of the XENG engines (see xeng_util.h).
#include "engine/xeng_util.h"

namespace thunder {
namespace xe {

void level_drive(float* out, size_t N, double level, double drive, double scale) {
  const double lv = js_pow(10, orz(level) / 20) * scale, dr = orz(drive) > 0.001 ? 1 + drive * drive * 10 : 0, dn = dr != 0 ? 1 / js_tanh(dr) : 1;
  for (size_t i = 0; i < N; i++) { double y = (double)out[i] * lv; if (dr != 0) y = js_tanh(y * dr) * dn; out[i] = (float)y; }
}

size_t trim_fade(float* out, size_t N, double sr, double fade) {
  size_t end = N; while (end > 1 && jabs((double)out[end - 1]) < 1e-4) end--;
  const size_t rl = end > 1 ? end : 1;   // Math.max(end, 1)
  const double flr = js_round(sr * fade); const size_t fl = (double)rl < flr ? rl : (size_t)(flr > 0 ? flr : 0);
  for (size_t i = 0; i < fl; i++) out[rl - 1 - i] = (float)((double)out[rl - 1 - i] * ((double)i / (double)fl));
  return rl;
}

size_t finish(float* out, size_t N, double level, double drive, double sr, double tailFade) {
  const double pk = peak_of(out, N);
  level_drive(out, N, level, drive, pk > 0 ? 0.8 / pk : 1);
  return trim_fade(out, N, sr, tailFade);
}

}  // namespace xe
}  // namespace thunder

// ---- U29: the scratch arena, SpecUtil ------------------------------------------------------------------------------------------------------------------------------------------------------
#include <cstdlib>
#include <cstring>
#include <new>

namespace thunder {
namespace xe {

void XArena::release() { for (size_t i = 0; i < n_; i++) std::free(blk_[i].p); std::free(blk_); blk_ = nullptr; n_ = cap_ = 0; }
void XArena::reset() { for (size_t i = 0; i < n_; i++) blk_[i].used = 0; }
void* XArena::take(size_t bytes) {
  bytes = (bytes + 15) & ~(size_t)15; if (bytes == 0) bytes = 16;
  if (bytes > kArenaMax) return nullptr;
  for (size_t i = 0; i < n_; i++) if (blk_[i].cap - blk_[i].used >= bytes) { void* p = blk_[i].p + blk_[i].used; blk_[i].used += bytes; std::memset(p, 0, bytes); return p; }
  if (n_ == cap_) { const size_t nc = cap_ ? cap_ * 2 : 16; Blk* nb = static_cast<Blk*>(std::malloc(nc * sizeof(Blk))); if (!nb) return nullptr; if (n_) std::memcpy(nb, blk_, n_ * sizeof(Blk)); std::free(blk_); blk_ = nb; cap_ = nc; }
  const size_t cap = bytes > ((size_t)1 << 20) ? bytes : ((size_t)1 << 20);
  char* p = static_cast<char*>(std::aligned_alloc(16, cap)); if (!p) return nullptr;   // (cap is a multiple of 16)
  blk_[n_++] = Blk{p, cap, bytes}; std::memset(p, 0, bytes); return p;
}
XArena& xarena() { static thread_local XArena a; return a; }

namespace {
struct Tw { double* co = nullptr; double* si = nullptr; int n = 0; ~Tw() { std::free(co); std::free(si); } };
}
void spec_fft(double* re, double* im, int n, bool inv) {
  if (n < 2 || (n & (n - 1)) != 0 || n > (1 << 20)) return;   // (every call passes a power of two)
  int lg = 0; while ((1 << lg) < n) lg++;
  static thread_local Tw tw[21];
  Tw& T = tw[lg];
  if (!T.co) {   // SpecUtil.tw(n): co[k] = Math.cos(2 * Math.PI * k / n), si[k] = Math.sin(...)
    const int h = n >> 1; T.co = static_cast<double*>(std::malloc((size_t)h * sizeof(double))); T.si = static_cast<double*>(std::malloc((size_t)h * sizeof(double))); if (!T.co || !T.si) { std::free(T.co); std::free(T.si); T.co = T.si = nullptr; return; }
    for (int k = 0; k < h; k++) { T.co[k] = js_cos(2 * kPi * k / n); T.si[k] = js_sin(2 * kPi * k / n); }
  }
  const double* co = T.co; const double* si = T.si; const double sg = inv ? 1 : -1;
  for (int i = 1, j = 0; i < n; i++) {
    int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b;
    if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const int half = len >> 1, step = n / len;
    for (int i = 0; i < n; i += len) {
      for (int k = 0, t = 0; k < half; k++, t += step) {
        const double wr = co[t], wi = sg * si[t]; const int a = i + k, b = a + half; const double br = re[b], bi = im[b];
        const double xr = br * wr - bi * wi, xi = br * wi + bi * wr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
      }
    }
  }
}

uint8_t* spec_unb64(XArena& ar, const std::string& str, size_t* nOut) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int8_t M[256]; std::memset(M, -1, sizeof M); for (int i = 0; i < 64; i++) M[(unsigned char)T[i]] = (int8_t)i;
  size_t len = 0; for (unsigned char c : str) if (M[c] >= 0) len++;
  char* s = ar.get<char>(len ? len : 1); if (!s) return nullptr;
  { size_t k = 0; for (unsigned char c : str) if (M[c] >= 0) s[k++] = (char)c; }
  const size_t n = len * 3 / 4; uint8_t* out = ar.get<uint8_t>(n ? n : 1); if (!out) return nullptr; size_t o = 0;
  for (size_t i = 0; i < len; i += 4) {
    const int a = M[(unsigned char)s[i]], b = i + 1 < len ? M[(unsigned char)s[i + 1]] : 0, c = i + 2 < len ? M[(unsigned char)s[i + 2]] : -1, d = i + 3 < len ? M[(unsigned char)s[i + 3]] : -1;
    const int32_t v = ((a & 63) << 18) | ((b & 63) << 12) | ((c < 0 ? 0 : c) << 6) | (d < 0 ? 0 : d);
    if (o < n) out[o++] = (uint8_t)((v >> 16) & 255);
    if (c >= 0 && o < n) out[o++] = (uint8_t)((v >> 8) & 255);
    if (d >= 0 && o < n) out[o++] = (uint8_t)(v & 255);
  }
  *nOut = o; return out;
}

}  // namespace xe
}  // namespace thunder
