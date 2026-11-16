// U23: the pwm layer (pwmOsc, pwmDefault): a sample played the way old hardware did it (a clock reading it with no smoothing; 1-bit PWM, NES DPCM or plain PCM at 2 to 8 bits).
// With no sample loaded it plays a built-in metallic cymbal: eng_pwm_default builds it once, as pwmDefault does.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
using l23::mn; using l23::mx;

namespace {
struct PwmDefault {
  static constexpr size_t kLen = 48000;   // Math.round(32000 * 1.5)
  float d[kLen];
  PwmDefault() {
    const double sr = 32000; const size_t n = kLen;
    double F[6] = {205.3, 304.4, 369.6, 522.7, 540, 800}; for (int i = 0; i < 6; i++) F[i] = F[i] * 2.7;
    double ph[6] = {0, 0, 0, 0, 0, 0}; double seed = 90901, hp = 0, prev = 0, pk = 0;
    for (size_t i = 0; i < n; i++) {
      double m = 0; for (int k = 0; k < 6; k++) { ph[k] += F[k] / sr; ph[k] -= std::floor(ph[k]); m += ph[k] < 0.5 ? 1 : -1; }
      seed = (double)js_to_uint32(seed * 1103515245.0 + 12345.0);   // (seed * 1103515245 + 12345) >>> 0: the product exceeds 2^53 and is rounded in doubles, as in the JS
      const double w = seed / 2147483648.0 - 1, x = m / 6 * 0.55 + w * 0.6;
      hp = 0.9 * (hp + x - prev); prev = x;
      const double t = (double)i / sr, y = hp * (js_exp(-t * 2.4) * 0.8 + js_exp(-t * 18) * 0.6) * mn(1, t / 0.0015);
      d[i] = (float)y; if (std::fabs(y) > pk) pk = std::fabs(y);
    }
    for (size_t i = 0; i < n; i++) d[i] = (float)((double)d[i] * (0.95 / pk));
  }
};
const PwmDefault& pwmDefault() { static const PwmDefault t; return t; }
}  // namespace

const float* eng_pwm_default(size_t* len, double* sr) { const PwmDefault& t = pwmDefault(); if (len) *len = PwmDefault::kLen; if (sr) *sr = 32000; return t.d; }

void PwmOsc::init(const EngLayer& L, double A, double B) {
  const EngU23& U = L.u23; double ssr;
  if (U.smp && U.smpLen) { d = U.smp; len = U.smpLen; ssr = U.smpSr; } else { d = eng_pwm_default(&len, &ssr); }
  k = ssr / kBase; clk = 2 * js_pow(16, A) * 1000;   // pwmClock(A) * 1000
  md = mn(8, std::floor(B * 9));                       // pwmMode
  q = md >= 2 ? js_pow(2, md - 1) : 0;
  pos = 0; acc = 1; held = 0; cnt = 64; duty = 0.5;
}
double PwmOsc::s(double inc) {
  if (pos >= (double)len - 1) return 0;
  acc += clk * inc / kBase;
  if (acc >= 1) {
    acc -= std::floor(acc); const double x = mx(-1, mn(1, l23::at(d, len, (double)js_to_int32(pos))));
    if (md == 0) duty = (x + 1) * 0.5;
    else if (md == 1) { cnt = (x + 1) * 63.5 > cnt ? mn(127, cnt + 2) : mx(0, cnt - 2); held = cnt / 63.5 - 1; }
    else held = mx(-q, mn(q - 1, js_round(x * q))) / q;
  }
  pos += inc * k;
  return md == 0 ? (acc < duty ? 0.8 : -0.8) : held;
}
}  // namespace thunder
