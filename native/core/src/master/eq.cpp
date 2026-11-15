// U12: the master EQ as Chromium's BiquadFilterNode. See eq.h.
#include "master/eq.h"

#include <algorithm>
#include <cmath>

#include "rt/jsmath.h"

namespace thunder {
namespace {
const double kPi = 3.141592653589793;
inline double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// Biquad::SetNormalizedCoefficients: a multiplication by 1 / a0
inline BqCoef norm(double b0, double b1, double b2, double a0, double a1, double a2) {
  const double inv = 1 / a0; BqCoef c; c.b0 = b0 * inv; c.b1 = b1 * inv; c.b2 = b2 * inv; c.a1 = a1 * inv; c.a2 = a2 * inv; return c;
}
const double kButter12[] = {0.7071}, kButter24[] = {0.5412, 1.3066}, kButter48[] = {0.5098, 0.6013, 0.9000, 2.5629};
}  // namespace

BqCoef bq_coef(BqType type, double sr, float fHz, float qv, float gainDb) {
  const double nyquist = sr / 2, fr = (double)fHz / nyquist, q = qv, gain = gainDb;
  switch (type) {
    case BqType::Lowpass: case BqType::Highpass: {
      const double cutoff = clamp01(fr); double resonance = q;
      if (cutoff == 1 || cutoff == 0) {
        if (type == BqType::Lowpass) return norm(cutoff, 0, 0, 1, 0, 0);   // lowpass: 1 at cutoff 1 (everything passes), 0 at cutoff 0 (nothing)
        return norm(1 - cutoff, 0, 0, 1, 0, 0);                            // highpass: the opposite
      }
      resonance = (double)(float)js_pow(10, resonance / 20);   // (Chromium holds it as a float: measured in Chrome 153)
      const double theta = kPi * cutoff, alpha = std::sin(theta) / (2 * resonance), cosw = std::cos(theta);
      if (type == BqType::Lowpass) { const double beta = (1 - cosw) / 2; return norm(beta, 2 * beta, beta, 1 + alpha, -2 * cosw, 1 - alpha); }
      const double beta = (1 + cosw) / 2; return norm(beta, -2 * beta, beta, 1 + alpha, -2 * cosw, 1 - alpha);
    }
    case BqType::Peaking: {
      const double frequency = clamp01(fr), qq = std::max(0.0, q), a = (double)(float)js_pow(10.0, gain / 40);   // (A is a float in Chromium: measured in Chrome 153)
      if (frequency > 0 && frequency < 1) {
        if (qq > 0) {
          const double w0 = kPi * frequency, alpha = std::sin(w0) / (2 * qq), k = std::cos(w0);
          return norm(1 + alpha * a, -2 * k, 1 - alpha * a, 1 + alpha / a, -2 * k, 1 - alpha / a);
        }
        return norm(a * a, 0, 0, 1, 0, 0);   // Q = 0: the limit is A^2
      }
      return norm(1, 0, 0, 1, 0, 0);
    }
    case BqType::Lowshelf: {
      const double frequency = clamp01(fr), a = (double)(float)js_pow(10.0, gain / 40);   // (A is a float in Chromium: measured in Chrome 153)
      if (frequency == 1) return norm(a * a, 0, 0, 1, 0, 0);
      if (frequency > 0) {
        const double w0 = kPi * frequency, s = 1, alpha = 0.5 * std::sin(w0) * std::sqrt((a + 1 / a) * (1 / s - 1) + 2), k = std::cos(w0), k2 = 2 * std::sqrt(a) * alpha;
        const double ap1 = a + 1, am1 = a - 1;
        return norm(a * (ap1 - am1 * k + k2), 2 * a * (am1 - ap1 * k), a * (ap1 - am1 * k - k2), ap1 + am1 * k + k2, -2 * (am1 + ap1 * k), ap1 + am1 * k - k2);
      }
      return norm(1, 0, 0, 1, 0, 0);
    }
    case BqType::Highshelf: {
      const double frequency = clamp01(fr), a = (double)(float)js_pow(10.0, gain / 40);   // (A is a float in Chromium: measured in Chrome 153)
      if (frequency == 1) return norm(1, 0, 0, 1, 0, 0);
      if (frequency > 0) {
        const double w0 = kPi * frequency, s = 1, alpha = 0.5 * std::sin(w0) * std::sqrt((a + 1 / a) * (1 / s - 1) + 2), k = std::cos(w0), k2 = 2 * std::sqrt(a) * alpha;
        const double ap1 = a + 1, am1 = a - 1;
        return norm(a * (ap1 + am1 * k + k2), -2 * a * (am1 + ap1 * k), a * (ap1 + am1 * k - k2), ap1 - am1 * k + k2, 2 * (am1 - ap1 * k), ap1 - am1 * k - k2);
      }
      return norm(a * a, 0, 0, 1, 0, 0);
    }
  }
  return BqCoef();
}

void MasterEq::configure(double sr, bool eqOn, const EqBand* bands, int n) {
  n_ = 0; if (!eqOn) return;
  for (int i = 0; i < n && i < kEqMaxBands; i++) {
    const EqBand& b = bands[i]; if (!b.on) continue;
    if (b.type == EqType::LowCut || b.type == EqType::HighCut) {
      const double* Q = b.slope == 12 ? kButter12 : b.slope == 48 ? kButter48 : kButter24;   // BUTTER[b.slope] || BUTTER[24]
      const int nq = b.slope == 12 ? 1 : b.slope == 48 ? 4 : 2;
      for (int k = 0; k < nq && n_ < kEqMaxNodes; k++) {
        Node& nd = node_[n_++]; nd = Node(); nd.type = b.type == EqType::LowCut ? BqType::Highpass : BqType::Lowpass;
        nd.f = (float)b.f; nd.q = (float)(20 * std::log10(Q[k])); nd.g = 0;   // (n.Q.value = 20 * Math.log10(q); the gain stays at its default 0)
        nd.c = bq_coef(nd.type, sr, nd.f, nd.q, nd.g);
      }
    } else if (n_ < kEqMaxNodes) {
      Node& nd = node_[n_++]; nd = Node(); nd.type = b.type == EqType::Bell ? BqType::Peaking : b.type == EqType::LowShelf ? BqType::Lowshelf : BqType::Highshelf;
      nd.f = (float)b.f; nd.g = (float)b.g; nd.q = (float)b.q; nd.c = bq_coef(nd.type, sr, nd.f, nd.q, nd.g);
    }
  }
}

void MasterEq::reset() { for (int i = 0; i < n_; i++) { Node& nd = node_[i]; for (int c = 0; c < 2; c++) nd.x1[c] = nd.x2[c] = nd.y1[c] = nd.y2[c] = 0; } }

void MasterEq::process(float* L, float* R, int frames) {
  for (int i = 0; i < n_; i++) {
    Node& nd = node_[i]; const BqCoef c = nd.c;
    for (int ch = 0; ch < 2; ch++) {
      float* d = ch ? R : L; double x1 = nd.x1[ch], x2 = nd.x2[ch], y1 = nd.y1[ch], y2 = nd.y2[ch];
      for (int k = 0; k < frames; k++) {
        const double x = d[k], y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        d[k] = (float)y; x2 = x1; x1 = x; y2 = y1; y1 = (double)d[k];   // (the float32 output is what feeds back)
      }
      nd.x1[ch] = x1; nd.x2[ch] = x2; nd.y1[ch] = y1; nd.y2[ch] = y2;
    }
  }
}

}  // namespace thunder
