// U09: see render.h. The block is core/core-processor.js process1 (steps 1 and 2) with the page's mixer in the core (mix/mix.h).
#include "render/render.h"

#include <algorithm>

namespace thunder {

SongRenderer::SongRenderer(const Song& song, const SeqVars& vars, const RenderPads& pads, double sampleRate, int maxBlock, const SeqOpts& opts, int maxVoices)
    : S_(song), pads_(pads), sr_(sampleRate), maxBlock_(maxBlock), seq_(new Seq(song, vars, SeqMode::Song, opts)), pool_(sampleRate, maxVoices), mix_(sampleRate, maxBlock),
      evs_(new SeqEvent[kRenderEvents]), step_(new SeqStep), scratch_(new float[(size_t)maxBlock * 2]()) {
  // the page's mixer as a render sets it (applyMix, live false) and the bus plan (no bus exists until a chains host is plugged in)
  mix_.setPlan(bus_plan(song.chainPatch, song.chainOn));
  mix_.load(song);
}
SongRenderer::~SongRenderer() = default;

// ThunderCore.ev(), for what this path has: the mixer's events, the sampler's off / mod / stp / note
void SongRenderer::dispatch(const SeqEvent& e) {
  st_.events++;
  switch (e.type) {
    case SEV_STP: pool_.stp(e); return;
    case SEV_TVL: case SEV_TVA: case SEV_TPN: case SEV_MVOL: case SEV_MOUT: if (mix_.event(e)) st_.mixEvents++; return;
    case SEV_BEAT: if (mix_.event(e)) st_.beats++; return;   // the beat clock (U10)
    case SEV_OFF: if (e.track >= 0) pool_.off(e); return;
    case SEV_MOD: if (e.track >= 0) pool_.mod(e, seq_->arp(e)); return;
    case SEV_NOTE: break;
    default: return;   // fxp / fxc / chc (U14), eng, the MIDI events, tempo
  }
  if (e.track < 0) return;
  st_.notes++;
  { const int ii = (int)e.ii; if (ii >= 0 && ii < kNI && S_.insts[ii].mo == 1) { st_.notesMidiOnly++; return; } }   // MIDI gear only: nothing sounds here
  const int pad = (int)e.pad;
  if (pad < 0 || pad >= kRenderPads || !pads_.buf[pad].data) { st_.notesNoPad++; return; }   // a slot the kit doesn't have is silent
  const VoiceBuf& b = pads_.buf[pad];
  if (VoicePool::shapedReverse(e, b)) { st_.notesShapedRev++; pool_.take(e, &pads_.kit[pad]); return; }   // (the page plays a reversed + shaped note: not this player's)
  if (pool_.note(e, seq_->arp(e), b, pads_.kit[pad], seq_->bpm())) st_.notesPlayed++;
}

void SongRenderer::setPlan(const PdcPlan& plan, PdcMode mode, int64_t extraLatencyFrames) {
  plan_ = plan; extra_ = extraLatencyFrames < 0 ? 0 : extraLatencyFrames;
  mix_.setTime((double)frame_ / sr_);
  PdcApply a; a.mode = mode; a.live = false; pdc_apply(mix_, plan, a);
}

void SongRenderer::setMasterChain(const MasterChainConfig& cfg) {
  if (!mchain_) mchain_.reset(new MasterChain(sr_, maxBlock_));
  mchain_->configure(cfg); mix_.setMasterFx(&MasterChain::hook, mchain_.get());
}
void SongRenderer::renderTrimmed(float* outL, float* outR, int64_t N) {
  int64_t skip = latencyFrames(), done = 0; float* sl = scratch_.get(); float* sr = sl + maxBlock_;
  while (skip > 0) { const int n = (int)std::min<int64_t>(skip, maxBlock_); block(sl, sr, n); skip -= n; }
  while (done < N) { const int n = (int)std::min<int64_t>(N - done, maxBlock_); block(outL + done, outR + done, n); done += n; }
}

void SongRenderer::block(float* outL, float* outR, int N) {
  if (N > maxBlock_) N = maxBlock_;
  const double t0 = (double)frame_ / sr_;
  pool_.setTime(t0); mix_.setTime(t0);
  // 1. step the song up to LEAD ahead (at most 3 steps a block)
  int n = 0;
  while (seq_->alive() && seq_->time() < t0 + lead && n++ < 3) {
    const int k = seq_->step(evs_.get(), kRenderEvents, step_.get());
    st_.steps++; if (k > st_.peakStepEvents) st_.peakStepEvents = k;
    for (int i = 0; i < k; i++) dispatch(evs_[i]);
  }
  // 2. the voices into their tracks' inputs, then the mixer
  pool_.render(mix_.inputsL(), mix_.inputsR(), N, t0);
  mix_.render(outL, outR, N, t0);
  frame_ += N;
}

RenderStats SongRenderer::stats() const {
  RenderStats s = st_; s.frames = frame_;
  s.peakVoices = pool_.peakVoices(); s.peakVoiceLane = pool_.peakEvents(); s.peakMixLane = mix_.peakEvents();
  s.voiceDropped = pool_.dropped(); s.voiceOverflow = pool_.tOverflow(); s.voiceAutoDropped = pool_.autoDropped(); s.mixAutoDropped = mix_.autoDropped(); s.seqDropped = seq_->dropped();
  return s;
}

}  // namespace thunder
