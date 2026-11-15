// LATENCY COMPENSATION (N1, unit U10): index.html's pdcPlan as a pure function, plus the delays that carry it out.
// THE CODE IS THE SPECIFICATION (pdcPlan, pdcModeOf, pumpSlotLat / pumpChainLatency, makePlayer's pdcSync). The plan, in five lines:
//   1. a track's own latency L(t) = the sum of its pump effect slots' latency (samples) / sample rate. A slot counts when it is ON, and also
//      when it is OFF if the effect has a bypass of its own (build 603: the plugin's bypass still delays; METER has none, so adds nothing OFF).
//      Only track slots (S.tfx) are read: the master's slots and mastering never enter the plan, and neither do chains modules.
//   2. a track's ARRIVAL at its pan: at(t) = L(t) + (a bus: the latest arrival of the tracks that feed it).
//   3. d(t), the delay on the track's direct way to the master sum: T - at(t), where T is the latest arrival over every track not "routed"
//      (a pure source heard only through its bus); 0 for a routed one. The preview bus (index kSongNT) is a track here: d = T - 0.
//   4. f(q), the delay on q's feed into the buses it feeds (POST feed after the pan; PRE feed after `pre`): max over the buses it feeds
//      of (latest arrival among that bus's sources - at(q)). A send (heard directly and through a bus) gets d and f separately.
//   5. c(t), how late a node's INPUT is = at(t) - L(t) (a bus: the latest of its sources; the master: T): the delay of the beat clock the
//      node's tempo-locked effects hear. Only entries above 1e-9 s exist; the rest read 0.
// Everything is in SECONDS as doubles, exactly the JS's arithmetic; the mixer turns a delay into samples. Mode Off: the plan is all zeros.
// Auto and On give the same plan; the difference is when it is in force (PdcApply below: Auto live is in force only while a song plays).
#ifndef THUNDER_MIX_PDC_H
#define THUNDER_MIX_PDC_H
#include <cstdint>

#include "mix/mix.h"

namespace thunder {

enum class PdcMode : uint8_t { Off, Auto, On };
// pdcModeOf(): aset.pdcMode 'on' | 'off' | 'auto' (nullptr / anything else: unset), else a setting saved before build 616: aset.pdc true -> on, else auto
PdcMode pdc_mode_of(const char* pdcMode, bool legacyPdc);

// One pump effect slot of a track, as the caller (the effects host, U14 / U18) reports it. latency: the slot's `pump_latency` in samples
// (pumpLatencyOf). bypass: the effect has a bypass of its own (pumpIdx[...].bypass != null: every pump effect but METER).
struct PdcSlot { int track; int slot; bool on; bool bypass; double latency; };

struct PdcPlan {
  bool on = false;                // false: mode Off (the JS's `none`)
  double T = 0;                   // the latest arrival: everything plays this much later (seconds); a render trims it
  double d[kMixTracks], f[kMixTracks], c[kMixTracks];   // seconds, per track 0..NT-1 and the preview bus / master (index kSongNT); 0 where the JS map has no entry
  PdcPlan();
};

// pdcPlan(sr). Control thread (allocates). Slots of tracks outside 0..NT-1 are ignored.
PdcPlan pdc_plan(const BusPlan& plan, const PdcSlot* slots, int nSlots, double sampleRate, PdcMode mode);
// What pass() adds to the render's length and trims off the start: Math.round(T * sr)
int64_t pdc_trim_frames(const PdcPlan& p, double sampleRate);

}  // namespace thunder
#endif
