// U27: BlipVoice and renderBlip (the pad's Blip engine: a minimal electronic drum), index.html line for line (NOT chains-worklet.js's BlipDrum copy that modfx/mod_misc.cpp has: that one differs, e.g. Math.max(2, decay)).
// Doubles everywhere except the Float32Array `out` it adds into; every Math.* is a js_* function. Allocation: none.
#include <cmath>
#include <cstring>

#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

namespace {
constexpr double kMaxBlip = 33554432;   // 2^25 floats: the longest render (12 s at 2.7 MHz); the JS would allocate whatever the rate says

struct BlipVoice {
  double sr; const XBlipP* p;
  bool on = false; double t = 0, ph = 0, mph = 0, rph = 0, lp = 0, bp = 0, rep = 0, rg = 1, v0 = (double)NAN, ke = (double)NAN, env = (double)NAN; uint32_t rnd = 7777;
  BlipVoice(double sr_, const XBlipP* p_) : sr(sr_), p(p_) {}
  void trig(double vel) { on = true; t = 0; ph = 0; mph = 0; rep = 0; rg = vel; v0 = rg; }
  // Adds (does not write) samples [a, b) of the voice into out, times gain g. out has `len` samples (a write past them is ignored).
  void render(float* out, size_t len, double a, double b, double g) {
    if (!on) return;
    const XBlipP& q0 = *p; const double inv = 1 / sr, kd = js_exp(-inv * 1000 / mx(1, q0.kickT)), ad = js_exp(-inv * 1000 * 6.9 / mx(1, q0.decay));
    const double atk = mx(1, q0.atk / 1000 * sr), fmI = q0.fm * 8, dr = q0.drive * 8, dn = truthy(dr) ? 1 / js_tanh(dr) : 1;
    const double filt = mx(-1, mn(1, q0.filt)), fc = filt < 0 ? 20000 * js_pow(0.004, -filt) : 20 * js_pow(400, filt);
    const double q = 2 - 1.95 * mx(0, mn(1, q0.res)), gT = mx(5, q0.grainT) / 1000 * sr, rc = std::sqrt(q / 2);   // rc: resonance doesn't jump the level
    // the filter is only stable for f^2 + 2fq < 4, so f stops just under that (the JS's own fix of 4 Nov)
    const double f = mn(2 * js_sin(kPi * mn(0.45, fc / sr)), 0.98 * (std::sqrt(q * q + 4) - q));
    const double grN = (double)i32(q0.grain);
    for (double i = a; i < b; i++) {
      const double tt = t;
      // grain: the hit starts again every Grain time, each repeat quieter
      if (q0.grain >= 1 && rep < grN && tt >= gT * (rep + 1)) { rep++; rg = v0 * js_pow(0.72, rep); ke = 1; env = 0; ph = 0; }
      if (tt == 0) { ke = 1; env = 0; }
      const double tl = tt - rep * gT;
      // envelope: linear attack, then exponential decay
      env = tl < atk ? tl / atk : env * ad;
      ke *= kd;
      const double kk = q0.kick * 5 * ke, hz = kk > 1e-4 ? q0.pitch * js_exp(kk * 0.6931471805599453) : q0.pitch;
      // FM: a sine at fmR x pitch modulates the phase
      ph += hz * inv; if (ph >= 1) ph -= std::floor(ph);
      double pm = ph;
      if (fmI > 0) { mph += hz * q0.fmR * inv; if (mph >= 1) mph -= std::floor(mph); pm += fmI * js_sin(2 * kPi * mph) * env * 0.159; }
      const double x0 = pm - std::floor(pm), u = x0 < 0.5 ? 2 * x0 : 2 * x0 - 1;
      double y = (x0 < 0.5 ? 4 : -4) * u * (1 - u);   // parabolic arcs: a sine with a little more edge
      // ring mod: depth from tremolo to metal
      if (q0.ring > 0) { rph += q0.ringF * inv; if (rph >= 1) rph -= std::floor(rph); y *= 1 - q0.ring + q0.ring * js_sin(2 * kPi * rph); }
      // noise and click
      rnd = rnd * 1664525u + 1013904223u; const double nz = (double)rnd / 2147483648.0 - 1;
      y = y * (1 - q0.noise * 0.7) + nz * q0.noise;
      if (tl < sr * 0.0015) y += q0.click * (1 - tl / (sr * 0.0015)) * (nz * 0.5 + 0.8);
      y *= env * rg;
      if (truthy(dr)) y = js_tanh(y * dr) * dn;
      // bipolar state-variable filter
      if (filt != 0) { const double hp = y - lp - q * bp; bp += f * hp; lp += f * bp; if (!(jabs(lp) < 1e3)) { lp = bp = 0; } y = (filt < 0 ? lp : hp) * rc; }
      if (i >= 0 && i < (double)len && i == std::floor(i)) { const size_t k = (size_t)i; out[k] = (float)((double)out[k] + y * g * 0.8); }   // out[i] += y * g * 0.8
      t++;
      if (env < 1e-5 && tl > atk && (q0.grain < 1 || rep >= grN)) { on = false; break; }
    }
  }
};
}  // namespace

// renderBlip(V, sr): normBlip(V.blip) (done by the caller: the fields are finite), pitch moved by the voice's Tune, Drive added to the blip's own, one hit, Level.
size_t xeng_blip(const XBlipP& P, double sr, double tune, double vdrive, double level, float* out, size_t cap) {
  XBlipP b = P; b.pitch *= js_pow(2, orz(tune) / 12); b.drive = mn(1, b.drive + orz(vdrive));
  const double n = mn(sr * 12, std::ceil(((b.atk + b.decay * 1.6 + (b.grain >= 1 ? (double)i32(b.grain) * b.grainT : 0)) / 1000 + 0.05) * sr));
  const double tN = (n == n) ? std::trunc(n) : 0;   // new Float32Array(n): truncated, NaN gives 0, below -1 throws RangeError
  if (!(tN >= 1 && tN <= kMaxBlip)) return kRefuse;   // (an empty sound is refused too)
  const size_t len = (size_t)tN;
  if (!out) return len;
  if (len > cap) return 0;
  std::memset(out, 0, len * sizeof(float));
  BlipVoice v(sr, &b); v.trig(1);
  v.render(out, len, 0, n, js_pow(10, orz(level) / 20));
  // the JS's tail: `end = n; while (end > 1 && Math.abs(out[end - 1]) < 1e-4) end--` (a fractional n reads undefined and stops), slice, a fade of round(sr * 0.002)
  size_t end = len; if (n == std::floor(n)) { while (end > 1 && jabs((double)out[end - 1]) < 1e-4) end--; }
  const size_t rl = end > 1 ? end : 1; const double flr = js_round(sr * 0.002); const size_t fl = (double)rl < flr ? rl : (size_t)(flr > 0 ? flr : 0);
  for (size_t i = 0; i < fl; i++) out[rl - 1 - i] = (float)((double)out[rl - 1 - i] * ((double)i / (double)fl));
  return rl;
}

}  // namespace thunder
