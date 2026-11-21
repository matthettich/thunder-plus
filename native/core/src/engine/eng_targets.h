// ENGINE TARGETS on a live node (LIVE-ROUTE 1b): the twelve k-rate overrides of the page's live nodes (cCut cRes cDec cAtk cRel: the commands' bytes, -1 = none; oCut oRes oEnv oAtk oDec oSus oRel: signed offsets) and what
// the node's engOn() does with them: the effective values are swapped into the core's parameter object for one control block and put back after (index.html:22597-22621, the same arithmetic in the three processors; the target
// tables differ per kind). The values are read where the shell reads the rate lane: at a control block's first sample.
#ifndef THUNDER_ENGINE_ENG_TARGETS_H
#define THUNDER_ENGINE_ENG_TARGETS_H
#include <cmath>

#include "engine/bass_core.h"
#include "engine/vox_core.h"
#include "engine/xeng_live.h"
#include "rt/auto.h"
#include "rt/jsmath.h"

namespace thunder {

// the twelve lanes in the order of the page's engOn: v[0..4] = cut res dec atk rel (-1 = none), v[5..11] = cut res env atk dec sus rel (offsets)
struct EngFeed { const Auto* a[12] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}; bool on = false; };
inline void engRead(const EngFeed& f, double t, double v[12]) {   // eg(A, d): the lane's value at t, the default where it is not a number; a param's range clamps it
  for (int i = 0; i < 12; i++) {
    const double d = i < 5 ? -1 : 0, lo = i < 5 ? -1 : -255; double x = f.a[i] ? f.a[i]->at(t) : d;
    if (!(x == x)) { x = d; }
    v[i] = x < lo ? lo : x > 255 ? 255 : x;
  }
}

// one kind's table (the page's efm()): per target k (cut res env atk dec sus rel): the core's parameter, its range, the fourth flag (vox `bright`)
template <class P> struct EngMap { double P::*m[7]; double lo[7], hi[7]; bool fourth0; };
inline const EngMap<BassP>& engMapBass() { static const EngMap<BassP> M = {{&BassP::cut, &BassP::res, &BassP::fenv, &BassP::atk, &BassP::dec, &BassP::sus, &BassP::rel}, {30, 0, -6, 0.5, 1, 0, 1}, {18000, 1, 6, 2000, 4000, 1, 4000}, false}; return M; }
inline const EngMap<VoxP>& engMapVox() { static const EngMap<VoxP> M = {{&VoxP::bright, nullptr, nullptr, &VoxP::atk, nullptr, nullptr, &VoxP::rel}, {0, 0, 0, 0.5, 0, 0, 1}, {1, 0, 0, 400, 0, 0, 3000}, true}; return M; }
inline const EngMap<AcidP>& engMapAcid() { static const EngMap<AcidP> M = {{&AcidP::cut, &AcidP::res, &AcidP::envAmt, nullptr, &AcidP::ampDec, &AcidP::sus, &AcidP::rel}, {40, 0, 0, 0, 1, 0, 1}, {6000, 1.05, 1, 0, 4000, 1, 1000}, false}; return M; }
inline const EngMap<MachP>& engMapMach() { static const EngMap<MachP> M = {{&MachP::lp, &MachP::res, &MachP::fenv, &MachP::atk, &MachP::dec, &MachP::sus, &MachP::rel}, {60, 0, 0, 0.5, 1, 0, 1}, {16000, 1, 1, 2000, 4000, 1, 4000}, false}; return M; }

static inline double engClamp(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
// engOn: the swap-in. true: something was swapped (sv holds the stored values for engSwapOut)
template <class P> bool engSwapIn(P& p, const EngMap<P>& M, const double v[12], double sv[7]) {
  if (!(v[0] >= 0 || v[1] >= 0 || v[2] >= 0 || v[3] >= 0 || v[4] >= 0 || v[5] != 0 || v[6] != 0 || v[7] != 0 || v[8] != 0 || v[9] != 0 || v[10] != 0 || v[11] != 0)) return false;
  static const int CI[7] = {0, 1, -1, 3, 2, -1, 4};
  for (int k = 0; k < 7; k++) if (M.m[k]) sv[k] = p.*(M.m[k]);
  const double K = 18000.0 / 30, D = 4 / 0.008;
  for (int k = 0; k < 7; k++) {
    if (!M.m[k]) continue;
    const int ci = CI[k]; const double c = ci < 0 ? -1 : v[ci], o = v[5 + k];
    if (c < 0 && o == 0) continue;
    const double own = sv[k], lo = M.lo[k], hi = M.hi[k]; double e;
    if (k == 0 && !M.fourth0) e = engClamp((c < 0 ? own : 30 * js_pow(K, c / 255)) * js_pow(K, o / 255), lo, hi);
    else if (k == 0 || k == 1 || k == 5) { const double b = c < 0 ? (own - lo) / (hi - lo) : c / 255; e = lo + engClamp(b + o / 255, 0, 1) * (hi - lo); }
    else if (k == 2) e = engClamp(own + o / 255 * (hi - lo), lo, hi);
    else { const double b = c < 0 ? own / 1000 : 0.008 * js_pow(D, std::fmin(c, 254) / 254); e = engClamp(b * js_pow(D, o / 254), lo / 1000, hi / 1000) * 1000; }
    p.*(M.m[k]) = e;
  }
  return true;
}
template <class P> void engSwapOut(P& p, const EngMap<P>& M, const double sv[7]) { for (int k = 0; k < 7; k++) if (M.m[k]) p.*(M.m[k]) = sv[k]; }

}  // namespace thunder
#endif
