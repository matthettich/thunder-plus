// U09: see render.h. The block is core/core-processor.js process1 (steps 1 and 2) with the page's mixer in the core (mix/mix.h).
#include "render/render.h"

#include <algorithm>

#include "rt/num.h"

namespace thunder {

SongRenderer::SongRenderer(const Song& song, const SeqVars& vars, const RenderPads& pads, double sampleRate, int maxBlock, const SeqOpts& opts, int maxVoices)
    : S_(song), pads_(pads), sr_(sampleRate), maxBlock_(maxBlock), seq_(new Seq(song, vars, SeqMode::Song, opts)), pool_(sampleRate, maxVoices), mix_(sampleRate, kRenderQuantum),
      evs_(new SeqEvent[kRenderEvents]), step_(new SeqStep), scratch_(new float[(size_t)maxBlock * 2]()), q_(new float[(size_t)kRenderQuantum * 2]()) {
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
    case SEV_FXC: case SEV_FXP: if (fx_) fx_->event(e, mix_.now(), mix_); return;
    default: st_.eventsIgnored++; return;   // chc (chains), eng, the MIDI events, tempo: no route yet (counted)
  }
  if (e.track < 0) return;
  st_.notes++;
  { const int ii = to_int_sat(e.ii, -1, 1 << 20); /* N1 C3: NaN is "none" */ if (ii >= 0 && ii < kNI && S_.insts[ii].mo == 1) { st_.notesMidiOnly++; return; } }   // MIDI gear only: nothing sounds here
  const int pad = to_int_sat(e.pad, -1, 1 << 20);   // (N1 C3: a part without `pad` gives NaN: no pad, what x86 gave and what the JS's kit.voices[NaN] is)
  if (pad < 0 || pad >= kRenderPads) { st_.notesNoPad++; return; }
  // D2: which player. A live-engine or SoundFont pad is not the sampler's: its notes are silent here (counted), but they still take the track over (a live note does: liveNote / sfNote).
  if (pads_.route[pad] == PadRoute::Live) { st_.notesLive++; pool_.take(e, &pads_.kit[pad]); return; }
  if (pads_.route[pad] == PadRoute::SoundFont) { st_.notesSf++; pool_.take(e, &pads_.kit[pad]); return; }
  if (!pads_.buf[pad].data) { st_.notesNoPad++; return; }   // a slot the kit doesn't have is silent
  VoiceBuf b = pads_.buf[pad];   // (a copy of the small descriptor: the buffer itself is not copied)
  // D3, varRec: an instrument with slc > 1 or sync plays the pad as it is (no variation, no shaping). The JS: `S.insts[ev.ii]`, a whole index.
  const int ii = (e.ii == e.ii && e.ii >= 0 && e.ii < kNI && e.ii == (double)(int)e.ii) ? (int)e.ii : -1;
  bool useVar = false;
  if (ii >= 0 && (S_.insts[ii].slc > 1 || S_.insts[ii].sync)) { b.noShape = true; if (e.has & (SH_VK | SH_TW)) st_.notesUnshaped++; }
  else if (e.has & SH_VK) {   // D2: MUT / GEN: the variation the host rendered, by instrument, kind, number and pad; none: silent (never the base pad)
    const RenderVar* v = ii >= 0 ? pads_.findVar(ii, e.vkKind, e.vkNum, pad) : nullptr;
    if (!v) { st_.notesVarMissing++; pool_.take(e, &pads_.kit[pad]); return; }
    b = v->buf; useVar = true;
  }
  if (VoicePool::shapedReverse(e, b)) { st_.notesShapedRev++; pool_.take(e, &pads_.kit[pad]); return; }   // (the page plays a reversed + shaped note: not this player's)
  if (pool_.note(e, seq_->arp(e), b, pads_.kit[pad], seq_->bpm())) { st_.notesPlayed++; if (useVar) st_.notesVar++; }
}

void SongRenderer::setPlan(const PdcPlan& plan, PdcMode mode, int64_t extraLatencyFrames) {
  plan_ = plan; extra_ = extraLatencyFrames < 0 ? 0 : extraLatencyFrames;
  mix_.setTime((double)qFrame_ / sr_);
  PdcApply a; a.mode = mode; a.live = false; pdc_apply(mix_, plan, a);
}

void SongRenderer::setMasterChain(const MasterChainConfig& cfg) {
  if (!mchain_) mchain_.reset(new MasterChain(sr_, maxBlock_));
  mchain_->configure(cfg); mix_.setMasterFx(&SongRenderer::masterHook, this);
}
void SongRenderer::masterHook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds) {
  SongRenderer* r = (SongRenderer*)user;   // MASTER VOL -> master effects -> [mastering -> safety limiter] -> OUT
  // The master node hears the beat clock like a track node (page: setMfx calls clockTo(mfx)); without it master Flex / Step gate / Retrigger see a stopped clock.
  if (r->fx_ && r->fx_->host(kSongNT).slotCount() > 0) r->fx_->host(kSongNT).process(L, R, frames, t0, r->mix_.beat(kSongNT), frames);
  if (r->mchain_) MasterChain::hook(r->mchain_.get(), track, L, R, frames, t0, feeds, nFeeds);
}
void SongRenderer::setEffects(bool on) {
  if (!on) { fx_.reset(); for (int t = 0; t < kSongNT; t++) mix_.setFx(t, nullptr, nullptr); mix_.setMasterFx(mchain_ ? &SongRenderer::masterHook : nullptr, mchain_ ? this : nullptr); return; }
  fx_.reset(new FxBank(sr_)); fx_->load(S_, S_.bpm);
  fx_->attach(mix_, false);
  mix_.setMasterFx((mchain_ || fx_->host(kSongNT).slotCount() > 0) ? &SongRenderer::masterHook : nullptr, this);
}
void SongRenderer::renderTrimmed(float* outL, float* outR, int64_t N) {
  int64_t skip = latencyFrames(), done = 0; float* sl = scratch_.get(); float* sr = sl + maxBlock_;
  while (skip > 0) { const int n = (int)std::min<int64_t>(skip, maxBlock_); block(sl, sr, n); skip -= n; }
  while (done < N) { const int n = (int)std::min<int64_t>(N - done, maxBlock_); block(outL + done, outR + done, n); done += n; }
}

void SongRenderer::block(float* outL, float* outR, int N) {
  while (N > 0) {
    if (qRead_ >= kRenderQuantum) { quantum(); qRead_ = 0; }
    const int n = std::min(N, kRenderQuantum - qRead_);
    std::copy(q_.get() + qRead_, q_.get() + qRead_ + n, outL);
    std::copy(q_.get() + kRenderQuantum + qRead_, q_.get() + kRenderQuantum + qRead_ + n, outR);
    outL += n; outR += n; qRead_ += n; N -= n; frame_ += n;
  }
}

void SongRenderer::quantum() {
  const int N = kRenderQuantum;
  const double t0 = (double)qFrame_ / sr_;
  pool_.setTime(t0); mix_.setTime(t0);
  // 1. step the song up to LEAD ahead (at most 3 steps a quantum, as the web core's block)
  int n = 0;
  while (seq_->alive() && seq_->time() < t0 + lead && n++ < 3) {
    const int k = seq_->step(evs_.get(), kRenderEvents, step_.get());
    st_.steps++; if (k > st_.peakStepEvents) st_.peakStepEvents = k;
    for (int i = 0; i < k; i++) dispatch(evs_[i]);
  }
  // 2. the voices into their tracks' inputs, then the mixer
  pool_.render(mix_.inputsL(), mix_.inputsR(), N, t0);
  float* const qL = q_.get(); float* const qR = q_.get() + kRenderQuantum;
  mix_.render(qL, qR, N, t0);
  if (meters_) { meters_feed(*meters_, mix_, qL, qR, N); meters_->advance(N, t0 + (double)N / sr_); }
  qFrame_ += N;
}

RenderStats SongRenderer::stats() const {
  RenderStats s = st_; s.frames = frame_;
  s.peakVoices = pool_.peakVoices(); s.peakVoiceLane = pool_.peakEvents(); s.peakMixLane = mix_.peakEvents();
  s.voiceDropped = pool_.dropped(); s.voiceOverflow = pool_.tOverflow(); s.voiceAutoDropped = pool_.autoDropped(); s.mixAutoDropped = mix_.autoDropped(); s.seqDropped = seq_->dropped();
  return s;
}

}  // namespace thunder
