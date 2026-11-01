// ...Thunder's own effects (original code, no third-party DSP): comb resonator, transient shaper, trance gate,
// three-band "OTT-style" compressor and frequency shifter. Each processes stereo in place, n <= 128.
#include <math.h>
#include <string.h>
#include "tfx.h"

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float coefMs(float ms, float sr) { return 1.f - expf(-1.f / (sr * ms * 0.001f)); }

// ---- 25 Comb resonator. p: 0 frequency Hz, 1 feedback (−0.99–0.99; below 0 rings an octave down, hollow),
//      2 damping, 3 stereo spread (cents between sides), 4 mix
struct CombFx : Fx {
  static const int N = 4096;
  float buf[2][N]; int w = 0; float lp[2] = {0, 0};
  CombFx() { memset(buf, 0, sizeof(buf)); }
  void process(float* L, float* R, int n) override {
    const float hz = clampf(p[0], 20.f, 8000.f), fb = clampf(p[1], -0.99f, 0.99f), damp = clampf(p[2], 0.f, 0.99f), m = p[4];
    float d[2]; d[0] = sr / hz; d[1] = sr / (hz * powf(2.f, p[3] / 1200.f));
    float* io[2] = {L, R};
    for (int i = 0; i < n; ++i) {
      for (int c = 0; c < 2; ++c) {
        const float dl = clampf(d[c], 2.f, N - 2.f); float rp = w - dl; if (rp < 0) rp += N;
        const int k = (int)rp; const float f = rp - k; const float y = buf[c][k] + (buf[c][(k + 1) & (N - 1)] - buf[c][k]) * f;
        lp[c] += (y - lp[c]) * (1.f - damp);
        const float x = io[c][i];
        buf[c][w] = x + lp[c] * fb;
        io[c][i] = x + (y * 0.5f - x) * m;
      }
      w = (w + 1) & (N - 1);
    }
  }
};
Fx* make_comb(float) { return new CombFx(); }

// ---- 26 Transient shaper. p: 0 attack (−1–1), 1 sustain (−1–1), 2 output (gain, 1 = unity), 3 mix
struct TransientFx : Fx {
  float ef = 0, es = 0, g = 1;
  void process(float* L, float* R, int n) override {
    const float af = coefMs(1.f, sr), rf = coefMs(30.f, sr), as = coefMs(25.f, sr), rs = coefMs(250.f, sr), gs = coefMs(2.f, sr);
    const float att = clampf(p[0], -1, 1), sus = clampf(p[1], -1, 1), out = p[2], m = p[3];
    for (int i = 0; i < n; ++i) {
      const float x = fabsf(L[i]) + fabsf(R[i]);
      ef += (x - ef) * (x > ef ? af : rf); es += (x - es) * (x > es ? as : rs);
      // log ratio of the fast follower to the slow one: above 0 while a hit starts, below 0 in its tail
      const float d = logf((ef + 1e-5f) / (es + 1e-5f));
      // up to about +10 dB of extra punch (or tail), and down to −26 dB
      const float tgt = d > 0 ? expf(clampf(att * d * 1.6f, -3.f, 1.15f)) : expf(clampf(-sus * d * 1.6f, -3.f, 1.15f));
      g += (tgt - g) * gs;
      const float k = g * out;
      L[i] += (L[i] * k - L[i]) * m; R[i] += (R[i] * k - R[i]) * m;
    }
  }
};
Fx* make_transient(float) { return new TransientFx(); }

// ---- 27 Trance gate. p: 0 steps per second (from the tempo), 1 gate length (0–1 of a step), 2 depth,
//      3 smoothing ms, 4 pattern (16 bits, step 1 = bit 0), 5 pattern length (1–16), 6 restart counter, 7 restart delay s
struct GateFx : Fx {
  double ph = 0; int step = 0; float g = 0, lastReset = -1; long wait = 0;
  void process(float* L, float* R, int n) override {
    if (p[6] != lastReset) { lastReset = p[6]; ph = 0; step = 0; wait = (long)(clampf(p[7], 0, 1) * sr); }
    const double inc = clampf(p[0], 0.05f, 64.f) / sr; const float len = clampf(p[1], 0.02f, 1.f), dep = clampf(p[2], 0, 1);
    const unsigned pat = (unsigned)p[4]; const int plen = (int)clampf(p[5], 1, 16); const float k = coefMs(clampf(p[3], 0.3f, 50.f), sr);
    for (int i = 0; i < n; ++i) {
      bool open = true;
      if (wait > 0) --wait;
      else {
        open = ((pat >> step) & 1u) && ph < len;
        ph += inc; if (ph >= 1.0) { ph -= 1.0; step = (step + 1) % plen; }
      }
      g += ((open ? 1.f : 1.f - dep) - g) * k;
      L[i] *= g; R[i] *= g;
    }
  }
};
Fx* make_gate(float) { return new GateFx(); }

// ---- 28 Multiband (OTT-style upward + downward compression in three bands).
//      p: 0 depth, 1 time (0 fast – 1 slow), 2 low gain dB, 3 mid gain dB, 4 high gain dB, 5 output dB
struct Svf { float ic1 = 0, ic2 = 0; void lh(float x, float g, float k, float& lo, float& hi) {
  const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
  const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
  ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lo = v2; hi = x - k * v1 - v2; } };
struct OttFx : Fx {
  Svf a[2][2], b[2][2]; float env[3] = {0, 0, 0}, gn[3] = {1, 1, 1};
  void process(float* L, float* R, int n) override {
    const float k = 1.41421356f, g1 = tanf(3.14159265f * 120.f / sr), g2 = tanf(3.14159265f * 2500.f / sr);
    const float dep = clampf(p[0], 0, 1), t = clampf(p[1], 0, 1), atk = coefMs(1.f + t * 20.f, sr), rel = coefMs(40.f + t * 400.f, sr), gs = coefMs(3.f, sr);
    float bg[3]; for (int j = 0; j < 3; ++j) bg[j] = powf(10.f, clampf(p[2 + j], -24, 24) / 20.f);
    const float og = powf(10.f, clampf(p[5], -24, 24) / 20.f);
    float* io[2] = {L, R};
    for (int i = 0; i < n; ++i) {
      float band[2][3];
      for (int c = 0; c < 2; ++c) {
        float lo, hi, lo2, hi2, x = io[c][i];
        // complementary splits (low = two 12 dB sections, the rest is what is left), so the three bands sum back exactly
        a[c][0].lh(x, g1, k, lo, hi); a[c][1].lh(lo, g1, k, lo2, hi2); band[c][0] = lo2;
        float hlo, hhi, hlo2, hhi2; b[c][0].lh(x - lo2, g2, k, hlo, hhi); b[c][1].lh(hlo, g2, k, hlo2, hhi2);
        band[c][1] = hlo2; band[c][2] = x - lo2 - hlo2;
      }
      float y[2] = {0, 0};
      for (int j = 0; j < 3; ++j) {
        const float lv = fabsf(band[0][j]) + fabsf(band[1][j]);
        env[j] += (lv - env[j]) * (lv > env[j] ? atk : rel);
        const float db = 20.f * log10f(env[j] + 1e-6f), T = -20.f;
        // below the target: raise it (upward, ratio ~3:1 from −60 dB); above: press it down (~4:1)
        float gd = db < T ? (db > -60.f ? (T - db) * 0.66f : 0.f) : -(db - T) * 0.75f;
        gd = clampf(gd * dep, -24.f, 18.f);
        gn[j] += (powf(10.f, gd / 20.f) - gn[j]) * gs;
        for (int c = 0; c < 2; ++c) y[c] += band[c][j] * gn[j] * bg[j];
      }
      L[i] = y[0] * og; R[i] = y[1] * og;
    }
  }
};
Fx* make_ott(float) { return new OttFx(); }

// ---- 29 Frequency shifter (single-sideband): every partial moves by the same number of Hz, so it goes
//      inharmonic and metallic. p: 0 shift Hz (−2000–2000), 1 feedback, 2 mix
struct Ap { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; float run(float x, float a) { const float y = a * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
struct ShiftFx : Fx {
  // a 90° phase-difference pair of allpass chains (the well-known 8-coefficient IIR Hilbert network)
  Ap pa[2][4], pb[2][4]; float d1[2] = {0, 0}, fbs[2] = {0, 0}; double ph = 0;
  void process(float* L, float* R, int n) override {
    static const float A[4] = {0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f, 0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f};
    static const float B[4] = {0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f, 0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f};
    const double inc = clampf(p[0], -2000, 2000) / sr; const float fb = clampf(p[1], 0, 0.9f), m = p[2];
    float* io[2] = {L, R};
    for (int i = 0; i < n; ++i) {
      const float c = cosf(6.2831853f * (float)ph), s = sinf(6.2831853f * (float)ph);
      for (int ch = 0; ch < 2; ++ch) {
        const float x = io[ch][i] + fbs[ch] * fb;
        float u = x, v = x; for (int k = 0; k < 4; ++k) { u = pa[ch][k].run(u, A[k]); v = pb[ch][k].run(v, B[k]); }
        const float re = d1[ch]; d1[ch] = u;    // the first chain is a sample behind the second
        const float y = re * c + v * s;
        fbs[ch] = y;
        io[ch][i] += (y - io[ch][i]) * m;
      }
      ph += inc; ph -= floor(ph);
    }
  }
};
Fx* make_shift(float) { return new ShiftFx(); }
