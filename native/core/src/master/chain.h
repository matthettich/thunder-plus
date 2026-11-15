// THE MASTER CHAIN (N1, unit U12): what the page puts between the master's MASTER VOL and its OUT fader (index.html rtRoute, pass()):
//
//   out (MASTER VOL) -> [master pump FX: U18] -> [mastering: EQ biquads -> the thunder-master worklet (suppressor, compressor, limiter)]
//                    -> [safety limiter] -> mo (OUT) -> the device
//
//   mastering  S.mst.on (OFF by default: freshMst). The EQ is native biquads (master/eq.h), only while mst.on and eq.on; the worklet is made only when a
//              stage needs it (mstNeedsWk: sup, comp or lim on); freshMst has the compressor and the limiter ON and the suppressor off, the EQ flat, so a
//              song that only switches mastering on gets the EQ (flat) -> compressor -> limiter. Auto master / the pump mastering effects are U18, not this.
//   safety     S.limiter (ON by default, `limiter:true` in a song): the worklet's limiter alone with SAFE_LIM (3 ms look-ahead, true peak, -1 dB ceiling,
//              120 ms release, no drive), and it is bypassed when the mastering chain has its own limiter on (rtRoute's mLim = mst.on && mst.lim.on).
//              Chrome's DynamicsCompressorNode + soft-clip WaveShaper (mkLimiter / softClip) is only the page's stand-in until the worklet has loaded
//              and where worklets are missing: NOT ported (nothing in a render uses it: pass() waits for mwReady).
//   OUT        the mixer's `mo` lane: after this chain (the Mixer's master hook is called at exactly this point, then OUT).
//   DC / soft clip: none in the chain (the soft clip is the stand-in's); samples outside (-64, 64) and NaN are zeroed at the worklet's input only.
//
// Latency (what pass() trims): mastering's (suppressor 2560 + limiter look) + the safety limiter's look, in samples: latency().
#ifndef THUNDER_MASTER_CHAIN_H
#define THUNDER_MASTER_CHAIN_H
#include <memory>

#include "master/eq.h"
#include "master/master.h"
#include "mix/mix.h"
#include "rt/json.h"

namespace thunder {

struct MasterChainConfig {
  bool mstOn = false;                 // S.mst.on
  bool eqOn = true; int nBands = 0; EqBand bands[kEqMaxBands];
  MasterSet set;                      // mstSet(M): sup, comp and lim as the song has them (all three present)
  bool safety = false;                // S.limiter
  MasterChainConfig();
};
// normMst() for the song's `mst` (a Missing / non-object gives freshMst) and the song's limiter flag. Clamps to MST_RANGES / the band limits as the page does.
MasterChainConfig master_chain_from_song(Json mst, bool limiter);
// freshMst's defaults (mst off, flat EQ with the six default bands, comp + lim on, sup off), safety as given
MasterChainConfig master_chain_fresh(bool safety);

class MasterChain {
 public:
  MasterChain(double sampleRate, int maxBlock);   // allocates (control thread)
  // rtRoute / pass(): the chain for this configuration. Allocation-free; the processors' states are kept (a render configures once, before its first block).
  void configure(const MasterChainConfig& c);
  void process(float* L, float* R, int frames);   // in place
  bool mstActive() const { return mstOn_; }        // mastering is in the path (mst.on)
  bool eqActive() const { return eq_.nodes() > 0; }
  bool workletActive() const { return wkOn_; }     // the thunder-master worklet exists (a stage needs it)
  bool safetyActive() const { return safeOn_; }    // the safety limiter is in the path (S.limiter and not replaced by mastering's limiter)
  int latency() const;                             // mstLatency + safeLatency, samples
  int latencyMastering() const;
  int latencySafety() const;
  MasterEq& eq() { return eq_; }
  MasterProcessor& mastering() { return *mst_; }
  MasterProcessor& safety() { return *safe_; }
  // the Mixer's master hook (Mixer::setMasterFx(&MasterChain::hook, chain))
  static void hook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds);
 private:
  double sr_;
  MasterEq eq_; std::unique_ptr<MasterProcessor> mst_, safe_;
  bool mstOn_ = false, wkOn_ = false, safeOn_ = false;
};

}  // namespace thunder
#endif
