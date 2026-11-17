// U24: the FM layers (index.html makeOsc's fm2 / fmfb / fmmetal / fmcross and fmOsc with FM_MODELS / FM_ALGS4 / FM_ALGS6). See layer_u24.h.
#include "engine/layer_u24.h"

#include "rt/jsmath.h"

namespace thunder {
namespace {
// fmRatio(a) = RATIOS[Math.round(a * (RATIOS.length - 1))]: an index outside the table is undefined (NaN in the arithmetic)
inline double fmRatio(double a) { const double i = js_round(a * 23); return (i >= 0 && i <= 23) ? kRatios[(int)i] : (double)NAN; }

struct Alg { int n; int nm[6]; int m[6][2]; int nc; int cars[6]; };   // mods[k] = m[k][0..nm[k]), cars
#define NOM {0, 0}
const Alg kAlgs4[8] = {
  {4, {0, 1, 1, 1}, {NOM, {0, 0}, {1, 0}, {2, 0}}, 1, {3}},            // 1: chain 0->1->2->3
  {4, {0, 1, 1, 1}, {NOM, {0, 0}, {1, 0}, {1, 0}}, 1, {3}},            // 2: two mods into the last
  {4, {0, 1, 2, 1}, {NOM, {0, 0}, {0, 1}, {2, 0}}, 1, {3}},            // 3
  {4, {0, 1, 1, 1}, {NOM, {0, 0}, {1, 0}, {2, 0}}, 2, {2, 3}},         // 4: two carriers
  {4, {0, 1, 1, 1}, {NOM, {0, 0}, {1, 0}, {2, 0}}, 3, {0, 1, 3}},      // 5
  {4, {0, 0, 1, 1}, {NOM, NOM, {0, 0}, {1, 0}}, 2, {2, 3}},            // 6: two pairs
  {4, {0, 0, 1, 2}, {NOM, NOM, {0, 0}, {0, 1}}, 2, {2, 3}},            // 7
  {4, {0, 0, 0, 0}, {NOM, NOM, NOM, NOM}, 4, {0, 1, 2, 3}},            // 8: additive
};
const Alg kAlgs6[5] = {
  {6, {0, 1, 1, 1, 1, 1}, {NOM, {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}}, 1, {5}},           // long chain
  {6, {0, 1, 1, 1, 1, 1}, {NOM, {0, 0}, {1, 0}, {1, 0}, {3, 0}, {4, 0}}, 1, {5}},
  {6, {0, 1, 1, 1, 1, 1}, {NOM, {0, 0}, {0, 0}, {2, 0}, {3, 0}, {4, 0}}, 2, {3, 5}},
  {6, {0, 0, 1, 1, 1, 1}, {NOM, NOM, {0, 0}, {1, 0}, {3, 0}, {4, 0}}, 2, {1, 5}},
  {6, {0, 0, 0, 0, 0, 0}, {NOM, NOM, NOM, NOM, NOM, NOM}, 6, {0, 1, 2, 3, 4, 5}},           // additive
};
const double kR4[4] = {1, 2, 3, 4}, kR6[6] = {1, 1, 2, 3, 4, 5}, kRGen[4] = {1, 2, 4, 8}, kRSb[4] = {1, 2, 4, 6}, kRArc[4] = {1, 1.007, 2, 3.01};
const Alg* algsOf(LType t, int* count) { if (t == LType::Fm6) { *count = 5; return kAlgs6; } *count = 8; return kAlgs4; }
}  // namespace

// ---- fm2 / fmfb / fmmetal / fmcross ------------------------------------------------------------------------------------------------------------------------
void U24Fm::init(int k, double a, double b) {
  kind = k; A = a; B = b; p = p1 = p2 = pa = pb = 0; I = 0; fb = 0;
  switch (k) {
    case 0: ratio = fmRatio(a); I0 = b * b * 12; break;
    case 1: break;
    case 2: r1 = 1.4142 + a * 2.4; r2 = 1.6818 + a * 1.3; r3 = 2.613 + a * 3.1; break;
    default: ratio = fmRatio(a); break;
  }
}
void U24Fm::blk(double env) {
  switch (kind) {
    case 0: I = I0 * (0.15 + 0.85 * env) * 0.159155; break;
    case 1: fb = A * 1.8 * (1 - B + B * env) * 0.159155; break;
    case 2: I = B * B * 9 * (0.2 + 0.8 * env) * 0.159155; break;
    default: I = B * 1.8 * (0.2 + 0.8 * env) * 0.159155; break;
  }
}
double U24Fm::s(double inc) {
  switch (kind) {
    case 0: { const double y = eng_sinT(p + I * eng_sinT(p * ratio)); p += inc; return y; }
    case 1: { const double y = eng_sinT(p + fb * (p1 + p2) * 0.5); p2 = p1; p1 = y; p += inc; return y; }
    case 2: { const double y = 0.5 * (eng_sinT(p + I * eng_sinT(p * r1)) + eng_sinT(p * r2 + I * eng_sinT(p * r3))); p += inc; return y; }
    default: { const double a = eng_sinT(p + I * pb), b = eng_sinT(p * ratio + I * pa); pa = a; pb = b; p += inc; return (a + b) * 0.5; }
  }
}

// ---- fmOsc: fm4 / fm6 / genesis / sb / arcade ----------------------------------------------------------------------------------------------------------------
// FM_MODELS: fm4 {n 4, ratios 1 2 3 4, idx 6, fbMax 1.2}, fm6 {6, 1 1 2 3 4 5, 5, 1.2, FM_ALGS6}, genesis {4, 1 2 4 8, 6, 1.8, q 511}, sb {4, 1 2 4 6, 7, 1.0, 'opl'}, arcade {4, 1 1.007 2 3.01, 8, 2.2, 'fold'}
void U24FmOp::init(LType t, double A, double B) {
  int na = 0; const Alg* algs = algsOf(t, &na); (void)algs;
  n = t == LType::Fm6 ? 6 : 4; wf = 0; q = 0;
  switch (t) {
    case LType::Fm4: ratios = kR4; idx = 6; fbMax = 1.2; break;
    case LType::Fm6: ratios = kR6; idx = 5; fbMax = 1.2; break;
    case LType::Genesis: ratios = kRGen; idx = 6; fbMax = 1.8; q = 511; break;
    case LType::Sb: ratios = kRSb; idx = 7; fbMax = 1.0; wf = 1; break;
    default: ratios = kRArc; idx = 8; fbMax = 2.2; wf = 2; break;   // arcade
  }
  const double ai = u24_max(0, u24_min(na - 1, js_round(A * (na - 1))));   // NaN: the JS would throw on algs[NaN].order; 0 here (u24_layer_ok refuses it)
  alg = ai == ai ? (int)ai : 0;
  fb = B * fbMax * 0.159155; base = idx; I = 0; f1 = f2 = 0;
  for (int k = 0; k < 6; k++) ph[k] = o[k] = 0;
}
void U24FmOp::blk(double env) { I = base * (0.15 + 0.85 * ((env != 0 && env == env) ? env : 0)) * 0.159155; }   // base * (0.15 + 0.85 * (env || 0)) * 0.159155
double U24FmOp::s(double inc) {
  const Alg& al = (n == 6 ? kAlgs6 : kAlgs4)[alg];
  for (int k = 0; k < al.n; k++) {
    double sum = 0; const int nm = al.nm[k];
    if (nm) { for (int j = 0; j < nm; j++) sum += o[al.m[k][j]]; sum *= I / nm; }
    double p = ph[k] + sum;
    if (k == 0 && (fb != 0 && fb == fb)) p += fb * (f1 + f2) * 0.5;
    double y;
    if (q) y = js_round(eng_sinT(p) * q) / q;
    else if (wf == 1) y = (k & 1) ? std::fabs(eng_sinT(p)) * 2 - 1 : eng_sinT(p);
    else if (wf == 2) y = eng_sinT(eng_sinT(p) * 0.8);
    else y = eng_sinT(p);
    if (k == 0) { f2 = f1; f1 = y; }
    o[k] = y; ph[k] += inc * ratios[k];
  }
  double out = 0; for (int c = 0; c < al.nc; c++) out += o[al.cars[c]];
  return out / al.nc;
}

}  // namespace thunder
