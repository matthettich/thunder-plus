// U28: DriftCore (XENG drift): warm analog pads: two oscillator lines per voice (saw / pulse with PWM), a high-pass and a low-pass, an amp, then a tape stage (wow, flutter, saturation, a BBD chorus, age, dropouts, hiss).
// index.html DriftCore.render, line for line. The chorus delay line is a Float32Array of ceil(sr * 0.03) + 4 in the JS: a stack buffer here when it fits (sr up to 273 kHz), else a std::vector (allocates).
#include <cstring>
#include <vector>

#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
struct Path { double hz, a, b, ph; };
struct Vc { double pa, pb, ps, det; Path dp, fd; double pwmPh, pwmK, l1, l2, h1, h2, la1, la2, la3, lk, ha1, ha2, ha3, hk, dtA, dtB, w; };
inline double blep(double t, double dt) { return t < dt ? (t /= dt, t + t - t * t - 1) : t > 1 - dt ? (t = (t - 1) / dt, t * t + t + t + 1) : 0; }
inline double pulse(double ph, double w, double dt) { double q = ph - w; if (q < 0) q += 1; return (ph < w ? 1 : -1) + blep(ph, dt) - blep(q, dt) - (2 * w - 1); }
}

size_t xeng_drift(const DriftP& p, double sr, double hold, float* out, size_t cap) {
  const double atk = mx(0.5, p.atk) / 1000, dec = mx(1, p.dec) / 1000, rel = mx(1, p.rel) / 1000, sus = c01(p.sus);
  const double fat = mx(0.5, p.fatk) / 1000, fdc = mx(5, p.fdec) / 1000, fsu = c01(p.fsus);
  const double h = mx(atk + 0.02, orv(hold, 1));
  const double len = mn(32, h + rel * 1.1 + 0.08), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  KeysRnd rnd(0x0d21f7e5);
  const double f0 = 65.406 * js_pow(2, js_round(p.oct) + orz(p.tune) / 12);
  const int nv = cidx(js_round(p.voices), 1, 4); const double spreadC = p.spread * p.spread * 25, driftC = p.drift * p.drift * 30;
  auto path = [&](double hz) { Path q; q.hz = hz * (0.6 + rnd() * 0.8); q.a = rnd() * 2 - 1; q.b = rnd() * 2 - 1; q.ph = rnd(); return q; };
  bool bad = false;   // a path faster than 1e4 wraps per control step: the JS's `while (q.ph >= 1)` would not end within any time a render can take (at 2^53 it never ends): refused
  auto step = [&](Path& q, double dt) { if (!(q.hz * dt <= 1e4)) { bad = true; return 0.0; } q.ph += q.hz * dt; while (q.ph >= 1) { q.ph -= 1; q.a = q.b; q.b = rnd() * 2 - 1; } const double m = (1 - js_cos(kPi * q.ph)) * 0.5; return q.a + (q.b - q.a) * m; };
  Vc V[4];
  for (int j = 0; j < nv; j++) {
    const double pos = nv > 1 ? (double)j / (nv - 1) * 2 - 1 : 0; Vc& v = V[j]; std::memset((void*)&v, 0, sizeof v);
    v.pa = rnd(); v.pb = rnd(); v.ps = rnd(); v.det = pos * spreadC + (rnd() - 0.5) * spreadC * 0.3; v.dp = path(0.22); v.fd = path(0.1); v.pwmPh = rnd(); v.pwmK = 0.85 + rnd() * 0.3; v.lk = 2; v.hk = 2; v.w = 0.5;
  }
  Path wowP = path(mx(0.02, p.wowRate) * 0.45); const double wowC = p.wow * p.wow * 35, flutC = p.flutter * p.flutter * 9; Path fpath = path(9);
  double wph = rnd(), fph1 = rnd(), fph2 = rnd();
  const double bRatio = js_pow(2, (js_round(p.bSemi) + p.detune / 100) / 12), kf = js_pow(f0 / 261.6, p.key);
  const double ah = h > atk ? sus + (1 - sus) * js_exp(-6.9 * (h - atk) / dec) : h / atk;
  auto env = [&](double t) { return t < atk ? t / atk : t < h ? sus + (1 - sus) * js_exp(-6.9 * (t - atk) / dec) : ah * js_exp(-6.9 * (t - h) / rel); };
  auto fv = [&](double t) { return t < fat ? t / fat : fsu + (1 - fsu) * js_exp(-6.9 * (t - fat) / fdc); };
  const double fh = fv(h);
  auto fenv = [&](double t) { return t < h ? fv(t) : fh * js_exp(-6.9 * (t - h) / rel); };
  const double vd = mx(0, p.vibDelay) / 1000, vibC = mx(0, p.vib);
  struct Svf { double a1, a2, a3, k; };
  auto svf = [&](double fc, double res) { const double g = js_tan(kPi * mx(20, mn(sr * 0.45, fc)) / sr), k = 2 - 1.96 * c01(res), a1 = 1 / (1 + g * (g + k)); return Svf{a1, g * a1, g * g * a1, k}; };
  const size_t CR = 32; const double cdt = (double)CR / sr, sdt = 1 / sr, norm = 1 / std::sqrt((double)nv);
  const double aSaw = p.aSaw, aPul = p.aPulse, bSaw = p.bSaw, bPul = p.bPulse, sub = p.sub * 0.6, nz = p.noise * 0.5, ring = p.ring * 0.8;
  for (size_t i0 = 0; i0 < N; i0 += CR) {
    const double t0 = (double)i0 * sdt;
    const double wow = (0.6 * js_sin(2 * kPi * wph) + 0.4 * step(wowP, cdt)) * wowC; wph += p.wowRate * cdt;
    const double flut = (0.5 * js_sin(2 * kPi * fph1) + 0.3 * js_sin(2 * kPi * fph2) + 0.2 * step(fpath, cdt)) * flutC; fph1 += 7.3 * cdt; fph2 += 11.9 * cdt;
    const double vib = truthy(vibC) ? vibC * mn(1, mx(0, (t0 - vd) / 0.5)) * js_sin(2 * kPi * p.vibRate * t0) : 0;
    const double common = wow + flut + vib, fc = p.cut * kf * js_pow(2, p.fenv * 5 * fenv(t0));
    for (int j = 0; j < nv; j++) {
      Vc& v = V[j];
      v.dtA = f0 * js_pow(2, (v.det + step(v.dp, cdt) * driftC + common) / 1200) * sdt; v.dtB = v.dtA * bRatio;
      v.pwmPh += p.pwmRate * v.pwmK * cdt; v.w = mx(0.04, mn(0.96, p.pw + p.pwm * 0.45 * js_sin(2 * kPi * v.pwmPh)));
      const Svf L = svf(fc * js_pow(2, step(v.fd, cdt) * 0.3 * p.drift), p.res), H = svf(p.hp, p.hpRes);
      v.la1 = L.a1; v.la2 = L.a2; v.la3 = L.a3; v.lk = L.k; v.ha1 = H.a1; v.ha2 = H.a2; v.ha3 = H.a3; v.hk = H.k;
    }
    if (bad) return kRefuse;   // right after this control block's steps (it was at the next block's start: a voice of 32 samples or fewer, one control block, was rendered instead of refused)
    const size_t i1 = (size_t)mn((double)N, (double)(i0 + CR));
    for (size_t i = i0; i < i1; i++) {
      double s = 0;
      for (int j = 0; j < nv; j++) {
        Vc& v = V[j]; const double dA = v.dtA, dB = v.dtB;
        double pa = v.pa + dA; if (pa >= 1) pa -= 1; v.pa = pa;
        double pb = v.pb + dB; if (pb >= 1) pb -= 1; v.pb = pb;
        const double sA = 2 * pa - 1 - blep(pa, dA), sB = 2 * pb - 1 - blep(pb, dB), qB = pulse(pb, v.w, dB);
        double x = aSaw * sA + bSaw * sB + bPul * qB;
        if (truthy(aPul)) x += aPul * pulse(pa, v.w, dA);
        if (truthy(sub)) { double ps = v.ps + dA * 0.5; if (ps >= 1) ps -= 1; v.ps = ps; x += sub * pulse(ps, 0.5, dA * 0.5); }
        if (truthy(ring)) x += ring * sA * qB;
        if (truthy(nz)) x += nz * (rnd() * 2 - 1);
        { const double v3 = x - v.h2, v1 = v.ha1 * v.h1 + v.ha2 * v3, v2 = v.h2 + v.ha2 * v.h1 + v.ha3 * v3; v.h1 = 2 * v1 - v.h1; v.h2 = 2 * v2 - v.h2; x = x - v.hk * v1 - v2; }
        { const double v3 = x - v.l2, v1 = v.la1 * v.l1 + v.la2 * v3, v2 = v.l2 + v.la2 * v.l1 + v.la3 * v3; v.l1 = 2 * v1 - v.l1; v.l2 = 2 * v2 - v.l2; x = v2; }
        s += x;
      }
      out[i] = (float)(s * norm * env((double)i * sdt));
    }
  }
  // ---- the tape stage ----
  double pk = peak_of(out, N);
  const double pre = pk > 0 ? 0.9 / pk : 1, sg = 1 + p.sat * 3, sn = 1 / js_tanh(sg); const bool satOn = p.sat > 0.001;
  const double lpA = 1 - js_exp(-2 * kPi * 18000 * js_pow(2, -p.age * 2.8) / sr), hpA = 1 - js_exp(-2 * kPi * (15 + p.age * 120) / sr);
  double dips[32][3]; int ndips = 0;
  if (p.age > 0.05) for (double t = 0.5 + rnd() * 2; t < len; t += 1.2 + rnd() * 3) { const double a = 0.04 + rnd() * 0.08, b = p.age * (0.25 + rnd() * 0.25); if (ndips < 32) { dips[ndips][0] = t; dips[ndips][1] = a; dips[ndips][2] = b; ndips++; } }
  const double cw = c01(p.chorus) * 0.5; const double dlNd = std::ceil(sr * 0.03) + 4; if (!(dlNd >= 5 && dlNd <= kMaxLine)) return kRefuse; const size_t dlN = (size_t)dlNd, cap0 = 8192;
  float dlS[8192]; std::vector<float> dlV; float* dl = dlS;
  if (dlN > cap0) { dlV.assign(dlN, 0.0f); dl = dlV.data(); } else std::memset(dlS, 0, dlN * sizeof(float));
  const double cBase = 0.008 * sr, cDep = 0.0025 * sr;
  double l1 = 0, l2 = 0, hpS = 0, c1 = 0, c2 = 0; size_t w = 0, di = 0; c1 = rnd(); c2 = rnd();
  for (size_t i = 0; i < N; i++) {
    const double t = (double)i * sdt; double x = (double)out[i] * pre;
    if (satOn) x = js_tanh(x * sg) * sn;
    l1 += (x - l1) * lpA; l2 += (l1 - l2) * lpA; hpS += (l2 - hpS) * hpA; x = l2 - hpS;
    while ((int)di < ndips && t > dips[di][0] + dips[di][1]) di++;
    if ((int)di < ndips && t > dips[di][0]) { const double q = (t - dips[di][0]) / dips[di][1]; x *= 1 - dips[di][2] * 0.5 * (1 - js_cos(2 * kPi * q)); }
    if (truthy(cw)) {
      dl[w] = (float)x; c1 += 0.45 * sdt; c2 += 0.17 * sdt; if (c1 >= 1) c1 -= 1; if (c2 >= 1) c2 -= 1;
      const double m = (c1 < 0.5 ? 4 * c1 - 1 : 3 - 4 * c1) * 0.7 + js_sin(2 * kPi * c2) * 0.3;
      double rp = (double)w - (cBase + cDep * m); if (rp < 0) rp += (double)dlN; const int32_t r0i = js_to_int32(rp); const double r0 = (double)r0i, fr = rp - r0; const size_t r0u = r0i >= 0 && (size_t)r0i < dlN ? (size_t)r0i : 0, r1 = r0u + 1 < dlN ? r0u + 1 : 0;
      x = x * (1 - cw * 0.4) + ((double)dl[r0u] + ((double)dl[r1] - (double)dl[r0u]) * fr) * cw;
      if (++w >= dlN) w = 0;
    }
    out[i] = (float)x;
  }
  pk = peak_of(out, N);
  const double lv = js_pow(10, orz(p.level) / 20) * (pk > 0 ? 0.8 / pk : 1), dr = orz(p.drive) > 0.001 ? 1 + p.drive * p.drive * 10 : 0, dn = dr != 0 ? 1 / js_tanh(dr) : 1;
  const double hz = p.hiss > 0.001 ? 0.8 * js_pow(10, (-62 + p.hiss * 34) / 20) * 3 : 0; double hn = 0;
  for (size_t i = 0; i < N; i++) {
    double y = (double)out[i] * lv; if (dr != 0) y = js_tanh(y * dr) * dn;
    if (truthy(hz)) { hn += ((rnd() * 2 - 1) - hn) * 0.35; y += hn * hz * std::sqrt(mx(0, env((double)i * sdt))); }
    out[i] = (float)y;
  }
  return trim_fade(out, N, sr, 0.003);
}

}  // namespace thunder
