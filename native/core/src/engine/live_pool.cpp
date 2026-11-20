// See live_pool.h. The JS: core/core-processor.js liveNote / liveStart / kill (the mono branch), the page's StereoPanner / GainNode after the node (__tcPost).
#include "engine/live_pool.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace thunder {

namespace {
constexpr double kPi = 3.14159265358979323846;

template <class SH>
class LiveEngT final : public LiveEng {
 public:
  explicit LiveEngT(double sr) : sh_(sr) {}
  SH& shell() { return sh_; }
  void process(float* L, float* R, uint32_t n, const LiveEvent* ev, uint32_t nev) override { sh_.process(L, R, n, ev, nev); }
  void seek(uint64_t c) override { sh_.seek(c); }
  uint64_t clock() const override { return sh_.clock(); }
  uint32_t latency() const override { return SH::latency(); }
  bool failed() const override { return sh_.failed(); }
  void setRateFeed(const Auto* a, double sr) override { sh_.setRateFeed(a, sr); }
  size_t bytes() const override { return sizeof(*this); }
 private:
  SH sh_;
};
}  // namespace

LivePool::LivePool(double sampleRate) : sr_(sampleRate), scr_(new float[2 * kQuantum]()) {}
LivePool::~LivePool() = default;

bool LivePool::addPad(int pad, const LivePadConfig& cfg) {
  if (pad < 0 || pad >= kLivePads) return false;
  if (cfg.kind != LiveKind::Bass && cfg.kind != LiveKind::Vox && cfg.kind != LiveKind::Acid && cfg.kind != LiveKind::Mach) return false;
  PadSlot fresh; fresh.kind = cfg.kind;
  if (cfg.kind == LiveKind::Vox) { fresh.poly.assign((size_t)kVoiceTracks * kVoicePoly, nullptr); fresh.vg.assign((size_t)kVoiceTracks, VoxG()); }
  auto make = [&](int t, int j) -> Node* {
    std::unique_ptr<Node> n(new Node());
    n->pad = pad; n->track = t; n->kind = cfg.kind; n->j = j;
    bool ok = true;
    switch (cfg.kind) {
      case LiveKind::Bass: { auto* e = new LiveEngT<LiveBass>(sr_); n->eng.reset(e); e->shell().core().set(cfg.bass); break; }
      case LiveKind::Vox: { auto* e = new LiveEngT<LiveVox>(sr_); n->eng.reset(e); ok = e->shell().core().set(cfg.vox); break; }   // (false: the lyrics could not be parsed)
      case LiveKind::Acid: { auto* e = new LiveEngT<LiveAcid>(sr_); n->eng.reset(e); e->shell().core().set(cfg.acid); break; }
      default: { auto* e = new LiveEngT<LiveMach>(sr_); n->eng.reset(e); e->shell().core().set(cfg.mach); break; }
    }
    if (!ok) return nullptr;
    n->eng->setRateFeed(&n->rate, sr_);
    fresh.bytes += sizeof(Node) + n->eng->bytes();
    Node* raw = n.get(); fresh.nodes.push_back(std::move(n)); return raw;
  };
  for (int t = 0; t < kVoiceTracks; t++) {
    if (cfg.trackMask && !(cfg.trackMask >> t & 1)) continue;
    Node* m = make(t, 0); if (!m) return false;
    fresh.byTrack[t] = m;
    if (cfg.kind == LiveKind::Vox) for (int j = 1; j <= kVoicePoly; j++) { Node* p = make(t, j); if (!p) return false; fresh.poly[(size_t)t * kVoicePoly + (size_t)(j - 1)] = p; }
  }
  pads_[pad] = std::move(fresh);   // (the nodes are made first: a throw above leaves the pad as it was)
  rebuildActive();
  return true;
}
void LivePool::removePad(int pad) { if (pad < 0 || pad >= kLivePads) return; pads_[pad] = PadSlot(); rebuildActive(); }
void LivePool::rebuildActive() {
  active_.clear(); size_t n = 0; for (const PadSlot& p : pads_) n += p.nodes.size();
  active_.reserve(n);
  for (PadSlot& p : pads_) for (auto& nd : p.nodes) active_.push_back(nd.get());
}
uint32_t LivePool::failedNodes() const { uint32_t k = 0; for (const Node* n : active_) if (n->eng->failed()) k++; return k; }

// ---- the timeline ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void LivePool::cancelFrom(Node& n, int64_t T) {   // AudioParam.cancelScheduledValues(t) on gate / trig / leg: what is booked at or after T goes (a note at the same sample takes back a booked off: D4)
  int w = 0; for (int i = 0; i < n.ntl; i++) if (n.tl[i].T < T) n.tl[w++] = n.tl[i];
  n.ntl = w; rec(n, 'c', 0, 0, T);
}
void LivePool::book(Node& n, const TEv& e) {
  if (n.ntl >= kTimeline) {
    if (e.type != LiveEvType::NoteOff) { st_.queueDropped++; return; }   // L5: a NoteOn is dropped and counted
    // a note-off always wins: it discards what is booked at or after it and is kept
    st_.forcedOff++;
    int w = 0; for (int i = 0; i < n.ntl; i++) { if (n.tl[i].T < e.T) n.tl[w++] = n.tl[i]; else st_.queueDropped++; }
    n.ntl = w;
    if (n.ntl >= kTimeline) { n.ntl--; st_.queueDropped++; }   // (all of it is earlier: the latest booked event gives way)
  }
  int pos = n.ntl; while (pos > 0 && n.tl[pos - 1].T > e.T) { n.tl[pos] = n.tl[pos - 1]; pos--; }
  n.tl[pos] = e; n.ntl++;
}

// ---- notes -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
bool LivePool::note(const SeqEvent& e, int pad) {
  if (!has(pad)) return false;
  PadSlot& ps = pads_[pad];
  Node* n = e.track >= 0 && e.track < kVoiceTracks ? ps.byTrack[e.track] : nullptr;
  if (!n) { st_.noNode++; return false; }
  if (!std::isfinite(e.rate) || !std::isfinite(e.gain) || !std::isfinite(e.pan) || ((e.has & SH_SLIDE) && (!std::isfinite(e.slideFrom) || !std::isfinite(e.slideDur)))) return false;   // (the JS would throw inside liveStart: nothing is booked)
  if (ps.kind == LiveKind::Vox && e.poly) return polyVox(e, ps);
  startNote(*n, e);
  st_.played++;
  return true;
}

// polyVox (core-processor.js liveNote, the `ev.poly && lk.vox` branch): four nodes a track; the syllable of the note (notes within 4 ms share one), the node it takes: the same column still sounding, else a free one, else the one
// that started longest ago (ties: the first); a note that would take the node that started at the very same time with none free is consumed and silent.
bool LivePool::polyVox(const SeqEvent& e, PadSlot& ps) {
  Node* nodes[kVoicePoly]; for (int j = 0; j < kVoicePoly; j++) { nodes[j] = ps.poly[(size_t)e.track * kVoicePoly + (size_t)j]; if (!nodes[j]) { st_.noNode++; return false; } }
  VoxG& G = ps.vg[(size_t)e.track];
  const bool leg = (e.has & SH_SLIDE) && e.slideDur > 0;
  double kk;
  if (std::fabs(e.t - G.lastT) < 0.004) kk = G.lastK; else { kk = leg ? std::max(0.0, G.sylN - 1) : G.sylN++; G.lastT = e.t; G.lastK = kk; }
  Node* free_[kVoicePoly]; int nf = 0;
  for (int j = 0; j < kVoicePoly; j++) if (!nodes[j]->curLive || nodes[j]->curStop) free_[nf++] = nodes[j];
  Node* pick = nullptr;
  for (int j = 0; j < kVoicePoly && !pick; j++) { const Node* b = nodes[j]; if (b->curLive && (!b->curStop || b->stopAt > e.t - 1e-6) && b->curCol == e.col && b->lastOn < e.t - 0.004) pick = nodes[j]; }   // sameCol
  if (!pick) { Node** src = nf ? free_ : nodes; const int cnt = nf ? nf : kVoicePoly; pick = src[0]; for (int j = 1; j < cnt; j++) if (!(pick->lastOn <= src[j]->lastOn)) pick = src[j]; }
  if (pick->lastOn >= e.t - 1e-6 && std::fabs(pick->lastOn - e.t) < 0.004 && nf == 0) return true;
  startNote(*pick, e, kk, true);
  st_.played++;
  return true;
}

void LivePool::startNote(Node& n, const SeqEvent& e, double fk, bool hasFk) {
  const double t = std::isfinite(e.t) ? std::max(e.t, now_) : now_;   // Math.max(ev.t, currentTime)
  const int64_t T = sample_at_or_after(t, sr_);
  const bool leg = (e.has & SH_SLIDE) && e.slideDur > 0;
  cancelFrom(n, T);                                                    // gate, trig, leg
  // the rate: hold, then the slide or the step (the list is folded like the sampler's rather than a point dropped: P5)
  if (n.rate.size() + 3 > n.rate.capacity()) { n.rate.prune(now_); if (n.rate.size() + 3 > n.rate.capacity()) { n.rate.prune(t); n.rate.cancel(t); } }
  n.rate.hold(t);
  if (leg) { n.rate.set(e.slideFrom, t); n.rate.lin(e.rate, t + e.slideDur); } else n.rate.set(e.rate, t);
  TEv on; on.T = T; on.type = LiveEvType::NoteOn; on.leg = leg; on.acc = n.kind != LiveKind::Bass && n.kind != LiveKind::Vox && e.acc;
  if (n.kind == LiveKind::Vox) { on.v = hasFk ? fk : leg ? std::max(0.0, n.sylN - 1) : n.sylN++; on.same = leg; }   // syl lane: the node's own counter, or polyVox's; `same` lane: the slide
  book(n, on); rec(n, 'n', t, on.v, T, leg, on.acc, on.same);
  if (n.g.size() + 2 > n.g.capacity()) n.g.prune(now_);
  if (n.pn.size() + 2 > n.pn.capacity()) n.pn.prune(now_);
  n.g.set(e.gain, t); n.pn.set(std::max(-1.0, std::min(1.0, (e.pan - 128) / 127)), t);
  n.lastOn = t; n.curLive = true; n.curStop = false; n.curCol = e.col;
  if (e.has & SH_CUT) kill(n, e.cut);
  if (e.has & SH_REL) kill(n, e.rel);
}

void LivePool::kill(Node& n, double at) {   // kill(v, at) for a live voice: the gate goes low at `at`; a stop before a booked later note takes that note back
  if (!n.curLive || (n.curStop && n.stopAt <= at)) return;
  n.curStop = true; n.stopAt = at;
  const double t = std::max(at, now_); const int64_t T = sample_at_or_after(t, sr_);
  if (t < n.lastOn - 1e-6) cancelFrom(n, T);
  TEv off; off.T = T; off.type = LiveEvType::NoteOff; book(n, off); rec(n, 'o', t, 0, T);
}

// ---- rendering -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void LivePool::deliver(Node& n, uint32_t count, LiveEvent* out, uint32_t* nout) {
  const int64_t clk = (int64_t)n.eng->clock(), end = clk + (int64_t)count;
  int k = 0; uint32_t m = 0;
  while (k < n.ntl && n.tl[k].T < end) {
    const TEv& e = n.tl[k++]; LiveEvent& o = out[m++];
    o = LiveEvent(); o.type = e.type; o.leg = e.leg; o.acc = e.acc; o.same = e.same; o.v = e.v;
    if (e.T < clk) { st_.late++; o.at = 0; } else o.at = (uint32_t)(e.T - clk);
  }
  if (k) { std::memmove(n.tl, n.tl + k, (size_t)(n.ntl - k) * sizeof(TEv)); n.ntl -= k; }
  *nout = m;
}

void LivePool::runNode(Node& n, int64_t q0, int frames, float* const* inL, float* const* inR) {
  LiveEng& eng = *n.eng; const uint32_t lat = eng.latency();
  float* sL = scr_.get(); float* sR = scr_.get() + kQuantum;
  LiveEvent ev[kTimeline + 1]; uint32_t nev = 0;
  if (!n.started) {   // the first call: the shell starts at this quantum and runs lat samples of pre-roll, so that it is lat ahead of the mixer
    eng.seek((uint64_t)q0); n.started = true;
    deliver(n, lat, ev, &nev); eng.process(sL, sR, lat, ev, nev);
  }
  deliver(n, (uint32_t)frames, ev, &nev);
  eng.process(sL, sR, (uint32_t)frames, ev, nev);   // the engine's samples for [q0, q0 + frames)
  // the page's GainNode and StereoPanner after the node (__tcPost), then into the track's input
  float* oL = inL ? inL[n.track] : nullptr; float* oR = inR ? inR[n.track] : nullptr; if (!oL) return;
  const double dt = 1 / sr_, tb0 = (double)q0 / sr_, tb1 = tb0 + frames * dt;
  n.g.prune(tb0); n.pn.prune(tb0); { const int64_t keep = (int64_t)eng.clock() - 2 * (int64_t)LiveBass::kBlk; if (keep > 0) n.rate.prune((double)keep / sr_); }   // (the shell's next control block starts at or after clock - kBlk)
  double gc = 0, pc = 0; const bool gok = n.g.constBlk(tb0, tb1, &gc), pok = n.pn.constBlk(tb0, tb1, &pc);
  for (int i = 0; i < frames; i++) {
    const double t = tb0 + i * dt, g = gok ? gc : n.g.at(t), p = pok ? pc : n.pn.at(t);
    const double l = (double)sL[i] * g, r = (double)sR[i] * g;
    double a, b;
    if (p <= 0) { const double x = (p + 1) * kPi / 2; a = l + r * std::cos(x); b = r * std::sin(x); }
    else { const double x = p * kPi / 2; a = l * std::cos(x); b = r + l * std::sin(x); }
    oL[i] = (float)((double)oL[i] + (double)(float)a);
    if (oR) oR[i] = (float)((double)oR[i] + (double)(float)b); else oL[i] = (float)((double)oL[i] + (double)(float)b);
  }
}

void LivePool::render(float* const* inL, float* const* inR, int frames, int64_t frame0) {
  if (frames <= 0 || frames > kQuantum) return;
  for (Node* n : active_) runNode(*n, frame0, frames, inL, inR);
}

}  // namespace thunder
