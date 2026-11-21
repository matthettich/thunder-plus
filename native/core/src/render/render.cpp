// U09: see render.h. The block is core/core-processor.js process1 (steps 1 and 2) with the page's mixer in the core (mix/mix.h).
#include "render/render.h"

#include <algorithm>

#include "midi/midi_host.h"
#include "render/players.h"

namespace thunder {

static SeqOpts withChop(SeqOpts o, const RenderPads& pads) { o.chop = &pads.chop; return o; }

SongRenderer::SongRenderer(const Song& song, const SeqVars& vars, const RenderPads& pads, double sampleRate, int maxBlock, const SeqOpts& opts, int maxVoices)
    : S_(song), pads_(pads), sr_(sampleRate), maxBlock_(maxBlock), seq_(new Seq(song, vars, SeqMode::Song, withChop(opts, pads))), players_(new Players(&pads, sampleRate, maxVoices)), mix_(sampleRate, kRenderQuantum),
      evs_(new SeqEvent[kRenderEvents]), step_(new SeqStep), scratch_(new float[(size_t)maxBlock * 2]()), q_(new float[(size_t)kRenderQuantum * 2]()) {
  // the page's mixer as a render sets it (applyMix, live false) and the bus plan (no bus exists until a chains host is plugged in)
  mix_.setPlan(bus_plan(song.chainPatch, song.chainOn));
  mix_.load(song);
}
SongRenderer::~SongRenderer() = default;
int SongRenderer::voices() const { return players_->voices(); }
VoicePool& SongRenderer::pool() { return players_->pool(); }
sf::SfPool& SongRenderer::sfPool() { return players_->sfPool(); }
Players& SongRenderer::players() { return *players_; }

// ThunderCore.ev(), for what this path has: the mixer's events, the beat clock, the effects'; the sampler's / SoundFont's off / mod / stp / note are the Players'
void SongRenderer::dispatch(const SeqEvent& e) {
  st_.events++;
  switch (e.type) {
    case SEV_TVL: case SEV_TVA: case SEV_TPN: case SEV_MVOL: case SEV_MOUT: if (mix_.event(e)) st_.mixEvents++; return;
    case SEV_BEAT: if (mix_.event(e)) st_.beats++; return;   // the beat clock (U10)
    case SEV_FXC: case SEV_FXP: if (fx_) fx_->event(e, mix_.now(), mix_); return;
    default: break;
  }
  if (!players_->dispatch(e, *seq_, S_)) st_.eventsIgnored++;   // chc (chains), eng, the MIDI events, tempo: no route yet (counted)
}

void SongRenderer::setPlan(const PdcPlan& plan, PdcMode mode, int64_t extraLatencyFrames) {
  plan_ = plan; extra_ = extraLatencyFrames < 0 ? 0 : extraLatencyFrames;
  mix_.setTime((double)qFrame_ / sr_);
  PdcApply a; a.mode = mode; a.live = false; pdc_apply(mix_, plan, a);
}

bool SongRenderer::setStem(int track) {
  if (track < 0 || track >= kSongNT || midi_) return false;   // (a MIDI hook would read the stem's forced mask as the audible mask)
  const uint32_t aud = (uint32_t)mix_.audibleMask();   // mute / solo as the song has them (not yet forced)
  uint32_t m = 1u << track;
  // a bus's stem plays its audible sources with it, and theirs through any depth (busClosure: a walk over the feeds that adds a track only when it is audible, so a muted middle bus
  // does not pull in what only feeds it); from the patch's plan, as the page, whether or not a chains host has made the bus yet
  for (bool more = true; more;) { more = false; for (int b = 0; b < kSongNT; b++) if ((m >> b & 1u) && (mix_.plan().feedMask[b] & aud & ~m)) { m |= mix_.plan().feedMask[b] & aud; more = true; } }
  stem_ = track; mchain_.reset(); mix_.setMasterFx(nullptr, nullptr);
  mix_.setHeard(m);
  return true;
}
void SongRenderer::setMasterChain(const MasterChainConfig& cfg) {
  if (stem_ >= 0) return;   // a stem has no master chain
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
  if (stem_ >= 0) {   // a stem: no master effects (the master hook stays off); effects only on the tracks that are heard (pass(): `for (const t of keep) pl.setFx(...)`, index.html:24256)
    const uint32_t heard = (uint32_t)mix_.audibleMask(); for (int t = 0; t < kSongNT; t++) if (!(heard >> t & 1u)) mix_.setFx(t, nullptr, nullptr);
    return;
  }
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
  players_->setTime(t0, qFrame_); mix_.setTime(t0);
  // U36 PR B, MIDI OUT (null hook: nothing here runs). A. before the steps: the posted commands, this block's figures. latencyFrames() is read every block, no cache (the web's moFxLat, A6); the clamp keeps midi_begin's
  // argument check (0..2 s) from refusing a block over an absurd plan. The live engines run ahead by their latency and add none (LIVE-ROUTE-DESIGN L4).
  const bool midi = midi_ && midi_begin(midi_, t0, (double)kRenderQuantum / sr_, std::min(2.0, std::max(0.0, (double)latencyFrames() / sr_)), mix_.audibleMask()) >= 0;
  // 1. step the song up to LEAD ahead (at most 3 steps a quantum, as the web core's block)
  int n = 0;
  while (seq_->alive() && seq_->time() < t0 + lead && n++ < 3) {
    const int k = seq_->step(evs_.get(), kRenderEvents, step_.get());
    st_.steps++; if (k > st_.peakStepEvents) st_.peakStepEvents = k;
    if (midi) midi_sched(midi_).onStep(evs_.get(), k, *step_, *seq_, t0);   // B. the whole step first, then its events are dispatched (the order midi_drive.h pins)
    for (int i = 0; i < k; i++) { dispatch(evs_[i]); players_->samplePeak(); }
  }
  if (midi) midi_end(midi_, t0, seq_->time(), seq_->bpm(), seq_->alive());   // C. book the outbox, the clock, the LFOs and publish
  if (!liveReleased_ && !seq_->alive()) { liveReleased_ = true; players_->songOver(seq_->time()); }   // C4: live voices are held until the song's end (the page cuts there; an unknown-length render must be able to end)
  // 2. the voices into their tracks' inputs, then the mixer
  players_->render(mix_.inputsL(), mix_.inputsR(), N, t0, qFrame_);
  if (tap_) tap_(tapUser_, mix_.inputsL(), mix_.inputsR(), N, qFrame_);
  float* const qL = q_.get(); float* const qR = q_.get() + kRenderQuantum;
  mix_.render(qL, qR, N, t0);
  if (meters_) { meters_feed(*meters_, mix_, qL, qR, N); meters_->advance(N, t0 + (double)N / sr_); }
  qFrame_ += N;
}

RenderStats SongRenderer::stats() const {
  RenderStats s = st_; s.frames = frame_;
  const PlayerStats& p = players_->stats(); const VoicePool& pool_ = players_->pool(); const sf::SfPool& sfpool_ = players_->sfPool();
  s.notes = p.notes; s.notesPlayed = p.notesPlayed; s.notesNoPad = p.notesNoPad; s.notesMidiOnly = p.notesMidiOnly; s.notesShapedRev = p.notesShapedRev;
  s.notesLive = p.notesLive; s.notesSf = p.notesSf; s.notesSfPlayed = p.notesSfPlayed; s.notesVar = p.notesVar; s.notesVarMissing = p.notesVarMissing; s.notesUnshaped = p.notesUnshaped; s.notesStretched = p.notesStretched;
  s.notesLivePlayed = p.notesLivePlayed; { const LiveStats l = players_->live().stats(); s.liveNoNode = l.noNode; s.liveLate = l.late; s.liveQueueDropped = l.queueDropped; s.liveForcedOff = l.forcedOff; s.liveFailed = players_->live().failedNodes(); s.liveRefused = l.refused; }
  s.peakVoices = pool_.peakVoices(); s.peakVoiceLane = pool_.peakEvents(); s.peakMixLane = mix_.peakEvents();
  s.voiceDropped = pool_.dropped(); s.voiceOverflow = pool_.tOverflow(); s.voiceAutoDropped = pool_.autoDropped(); s.mixAutoDropped = mix_.autoDropped(); s.seqDropped = seq_->dropped();
  s.seqChopped = seq_->chopped(); s.seqChopSkipped = seq_->chopSkipped(); s.seqChopNoHits = seq_->chopNoHits(); s.sfVoiceDropped = sfpool_.dropped(); s.sfVoiceOverflow = sfpool_.tOverflow(); s.sfVoiceAutoDropped = sfpool_.autoDropped(); s.peakVoicesAll = std::max(p.peakAll, pool_.peakVoices());
  return s;
}

}  // namespace thunder
