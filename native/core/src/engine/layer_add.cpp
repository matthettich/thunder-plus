// U24: the additive layers (index.html makeOsc's addsaw / addodd / membrane / bell / bar / chord and addOsc). See layer_u24.h.
#include "engine/layer_u24.h"

#include "rt/jsmath.h"

namespace thunder {
namespace {
// `1 + Math.round(a * 31)` etc. as a count of loop iterations (`for (k = 0; k < n; k++)`): NaN and anything below 1 run no iteration
inline int loopCount(double n) { return (n == n && n >= 1) ? (n > 1e6 ? 1000000 : (int)n) : 0; }
// ARRAY.slice(0, n): the length of the result for a 12- or 6-element table
inline int sliceLen(double n, int len) { if (n != n) return 0; const double e = std::trunc(n); if (e < 0) { const double v = len + e; return v < 0 ? 0 : (int)v; } return e > len ? len : (int)e; }
inline double chordIdx(double a) { return js_round(a * 13); }   // CHORDS.length - 1
}  // namespace

bool u24_add_ok(const EngLayer& L) {
  switch (L.type) {
    case LType::Addsaw: case LType::Addodd: return loopCount(1 + js_round(L.a * 31)) <= kU24AddMax;
    case LType::Chord: { const double i = chordIdx(L.a); return i >= 0 && i <= 13; }   // chordOf(a)[1] throws in the JS for an undefined chord
    default: return true;
  }
}

void U24Add::init(LType t, double A, double B) {
  n = 0; live = 0; ph = 0;
  switch (t) {
    case LType::Addsaw: case LType::Addodd: {
      n = loopCount(1 + js_round(A * 31)); const bool odd = t == LType::Addodd;
      for (int k = 0; k < n; k++) { const double r = odd ? 2 * k + 1 : k + 1; rs[k] = r; am[k] = js_pow(r, -(0.4 + B * 1.6)); dm[k] = k * 3; }
      break; }
    case LType::Membrane: {
      n = sliceLen(2 + js_round(A * 10), 12);
      for (int k = 0; k < n; k++) { rs[k] = kMem[k]; am[k] = 1 / (1 + k * 0.7); dm[k] = B * B * 50 * k; }
      break; }
    case LType::Bell: {
      n = sliceLen(2 + js_round(A * 10), 12);
      for (int k = 0; k < n; k++) { rs[k] = kBell[k]; am[k] = kBellA[k]; dm[k] = (0.3 + B * B * 14) * kBell[k]; }
      break; }
    case LType::Bar: {
      n = 6; const double sx = 0.12 + A * 0.85;
      for (int k = 0; k < 6; k++) { rs[k] = kBar[k]; am[k] = js_pow(sx, k); dm[k] = (0.5 + B * B * 12) * kBar[k]; }
      break; }
    default: {   // chord
      const int ci = (int)chordIdx(A); const EngChord& C = kChords[ci];
      for (int j = 0; j < C.n; j++) { const double r = js_pow(2, C.semis[j] / 12.0);
        rs[n] = r; rs[n + 1] = r * 2; rs[n + 2] = r * 3; am[n] = 1; am[n + 1] = B * 0.5; am[n + 2] = B * 0.33; dm[n] = 0; dm[n + 1] = 2; dm[n + 2] = 4; n += 3; }
      break; }
  }
  double sum = 0, mx = 0; for (int k = 0; k < n; k++) { const double a = am[k]; sum += a; if (a > mx) mx = a; }
  norm = 1 / u24_max(0.5 * sum + 0.5 * mx, 1e-6);
  live = n; for (int k = 0; k < n; k++) g[k] = 0;
}
void U24Add::blk(double t, double, double inc) {
  live = 0;
  for (int k = 0; k < n; k++) { if (rs[k] * inc > 0.45) { g[k] = 0; continue; } g[k] = am[k] * norm * js_exp(-t * dm[k]); live = k + 1; }
}
double U24Add::s(double inc) {
  double y = 0;
  for (int k = 0; k < live; k++) { const double gk = g[k]; if (gk > 1e-5) y += gk * eng_sinT(ph * rs[k]); }
  ph += inc; return y;
}

}  // namespace thunder
