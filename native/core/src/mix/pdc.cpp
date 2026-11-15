// U10: see pdc.h. pdc_plan is index.html's pdcPlan line for line (the same double arithmetic, so the same bits).
#include "mix/pdc.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

#include "rt/num.h"

namespace thunder {

PdcPlan::PdcPlan() { for (int t = 0; t < kMixTracks; t++) d[t] = f[t] = c[t] = 0; }

PdcMode pdc_mode_of(const char* m, bool legacyPdc) {
  if (m) { const std::string_view s(m); if (s == "on") return PdcMode::On; if (s == "off") return PdcMode::Off; if (s == "auto") return PdcMode::Auto; }
  return legacyPdc ? PdcMode::On : PdcMode::Auto;
}

PdcPlan pdc_plan(const BusPlan& P, const PdcSlot* slots, int nSlots, double sr, PdcMode mode) {
  PdcPlan out;
  if (mode == PdcMode::Off) return out;
  const int NT = kSongNT;
  // L(t) = pumpChainLatency(S.tfx[t], sr) / sr: the slots in the chain's order, the counted ones summed
  std::vector<PdcSlot> sl; for (int i = 0; i < nSlots; i++) if (slots[i].track >= 0 && slots[i].track < NT) sl.push_back(slots[i]);
  std::stable_sort(sl.begin(), sl.end(), [](const PdcSlot& a, const PdcSlot& b) { return a.track != b.track ? a.track < b.track : a.slot < b.slot; });
  double sum[kMixTracks]; for (int t = 0; t < kMixTracks; t++) sum[t] = 0;
  for (const PdcSlot& s : sl) sum[s.track] = sum[s.track] + ((s.on || s.bypass) ? s.latency : 0.0);
  double Lt[kMixTracks]; for (int t = 0; t < kMixTracks; t++) Lt[t] = t < NT ? sum[t] / sr : 0;
  // at: arrival at the pan (memoised; `busy` is the JS's loop guard, which the bus plan never needs)
  double at[kMixTracks]; bool have[kMixTracks], busy[kMixTracks]; for (int t = 0; t < kMixTracks; t++) { at[t] = 0; have[t] = busy[t] = false; }
  struct Rec { static double atP(int t, const BusPlan& P, const double* L, double* at, bool* have, bool* busy) {
      if (have[t]) return at[t];
      double v = L[t]; if (busy[t]) return v; busy[t] = true;
      if (t < kSongNT && P.feedMask[t]) { double m = 0; for (int q = 0; q < kSongNT; q++) if (P.feedMask[t] >> q & 1u) m = std::max(m, atP(q, P, L, at, have, busy)); v += m; }
      busy[t] = false; at[t] = v; have[t] = true; return v; } };
  for (int t = 0; t <= NT; t++) Rec::atP(t, P, Lt, at, have, busy);
  bool hasF[kMixTracks]; for (int t = 0; t < kMixTracks; t++) hasF[t] = false;
  uint32_t routed = 0;
  for (int b = 0; b < NT; b++) { if (!P.feedMask[b]) continue;
    double m = 0; for (int q = 0; q < NT; q++) if (P.feedMask[b] >> q & 1u) m = std::max(m, at[q]);
    for (int q = 0; q < NT; q++) if (P.feedMask[b] >> q & 1u) { out.f[q] = std::max(hasF[q] ? out.f[q] : 0.0, m - at[q]); hasF[q] = true; if (P.pure >> q & 1u) routed |= 1u << q; } }
  double T = 0; for (int t = 0; t <= NT; t++) if (t == NT || !(routed >> t & 1u)) T = std::max(T, at[t]);
  for (int t = 0; t <= NT; t++) out.d[t] = (t < NT && (routed >> t & 1u)) ? 0.0 : T - at[t];
  for (int t = 0; t < NT; t++) { const double v = at[t] - Lt[t]; if (v > 1e-9) out.c[t] = v; }
  if (T > 1e-9) out.c[NT] = T;
  out.on = true; out.T = T;
  return out;
}

int64_t pdc_trim_frames(const PdcPlan& p, double sr) { return to_i64_sat(std::floor(p.T * sr + 0.5), -(1LL << 40), 1LL << 40); }   // Math.round

Mixer::DelaySet pdc_delay_set(const PdcApply& a) {
  Mixer::DelaySet s; s.glide = a.live; s.idle = a.live && !a.playing && a.mode == PdcMode::Auto; s.ramp = a.now ? 0.01 : 0.05; return s;
}
void pdc_apply(Mixer& mix, const PdcPlan& plan, const PdcApply& a) { const Mixer::DelaySet s = pdc_delay_set(a); mix.setDelays(plan.d, plan.f, s); mix.setClockDelays(plan.c, s); }

}  // namespace thunder
