// See live_pool.h. The JS: core/core-processor.js liveNote / liveStart / kill (the mono branch), the page's StereoPanner / GainNode after the node (__tcPost).
#include "engine/live_pool.h"
#include "voice/voice.h"

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
  uint32_t shellDropped() const override { return sh_.dropped(); }
  uint32_t shellForced() const override { return sh_.forced(); }
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
  for (auto& v : byTrack_) v.clear();
  for (Node* n : active_) byTrack_[n->track].push_back(n);
}
LiveStats LivePool::stats() const { LiveStats s = st_; for (const Node* n : active_) { s.queueDropped += n->eng->shellDropped(); s.forcedOff += n->eng->shellForced(); } return s; }
uint32_t LivePool::failedNodes() const { uint32_t k = 0; for (const Node* n : active_) if (n->eng->failed()) k++; return k; }

// ---- the timeline ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void LivePool::cancelFrom(Node& n, int64_t T) {   // AudioParam.cancelScheduledValues(t) on gate / trig / leg: what is booked at or after T goes (a note at the same sample takes back a booked off: D4)
  int w = 0; for (int i = 0; i < n.ntl; i++) if (n.tl[i].T < T || (n.tl[i].type != LiveEvType::NoteOn && n.tl[i].type != LiveEvType::NoteOff)) n.tl[w++] = n.tl[i];   // (gate, trig, leg: the note events; tempo and wob stay)
  n.ntl = w; rec(n, 'c', 0, 0, T);
}
void LivePool::book(Node& n, const TEv& e) {
  if (n.ntl >= kTimeline) {
    if (e.type != LiveEvType::NoteOff && e.type != LiveEvType::End) { st_.queueDropped++; return; }   // L5: a NoteOn (and a Bpm / Wob event) is dropped and counted
    // a note-off always wins: it discards what is booked at or after it and is kept
    st_.forcedOff++;
    int w = 0; for (int i = 0; i < n.ntl; i++) { if (n.tl[i].T < e.T) n.tl[w++] = n.tl[i]; else st_.queueDropped++; }
    n.ntl = w;
    if (n.ntl >= kTimeline) {   // (all of it is earlier: the latest rate / tempo / wobble event gives way, else the latest event; counted)
      int v = n.ntl - 1; for (int i = n.ntl - 1; i >= 0; i--) if (n.tl[i].type != LiveEvType::NoteOn && n.tl[i].type != LiveEvType::NoteOff) { v = i; break; }
      for (int i = v; i + 1 < n.ntl; i++) n.tl[i] = n.tl[i + 1];
      n.ntl--; st_.queueDropped++;
    }
  }
  int pos = n.ntl; while (pos > 0 && n.tl[pos - 1].T > e.T) { n.tl[pos] = n.tl[pos - 1]; pos--; }
  n.tl[pos] = e; n.ntl++;
}

// F4 (the pool's entry): what reaches a core. A field the JS would have thrown on or an AudioParam cannot hold (NaN, Inf) is refused: a feature's flag is cleared (the bend, the vibrato, `mul`, cut, rel, wob, arp, a gain / pan step), the rest of the event stands;
// a magnitude is clamped to +-1e6 (gain, bendMul, mul, vibSpd, vibDep); each refusal is counted (LiveStats::refused). The time of an off / stp / mod must be finite, else the event is refused whole. rate, gain, pan and the slide of a NOTE are
// refused whole (the JS throws in liveStart). Clamps the JS has stay where they were (rate 0..256 in the shell, bpm 10..999, wob 0..64).
static inline double mag(double v) { return v < -1e6 ? -1e6 : v > 1e6 ? 1e6 : v; }
void LivePool::sanitize(SeqEvent& e, const double* arp) {
  const auto f = [](double v) { return std::isfinite(v); };
  auto drop = [&](uint32_t bit) { e.has &= ~bit; st_.refused++; };
  if ((e.has & SH_GAIN) && !f(e.gain)) drop(SH_GAIN);
  if ((e.has & SH_PAN) && !f(e.pan)) drop(SH_PAN);
  if ((e.has & SH_MUL) && !f(e.mul)) drop(SH_MUL);
  if ((e.has & SH_BEND) && (!f(e.bendMul) || !f(e.bendT1))) drop(SH_BEND);
  if ((e.has & SH_VIB) && (!f(e.vibSpd) || !f(e.vibDep) || !f(e.vibT1))) drop(SH_VIB);
  if ((e.has & SH_CUT) && !f(e.cut)) drop(SH_CUT);
  if ((e.has & SH_REL) && !f(e.rel)) drop(SH_REL);
  if ((e.has & SH_WOB) && !f(e.wob)) drop(SH_WOB);
  if (e.has & SH_ARP) { bool ok = arp && e.arpN >= 0 && e.arpN <= 4096; for (int q = 0; ok && q < 2 * e.arpN; q++) if (!f(arp[q])) ok = false; if (!ok) drop(SH_ARP); }
  if (f(e.gain)) e.gain = mag(e.gain);
  if (f(e.mul)) e.mul = mag(e.mul);
  if (f(e.bendMul)) e.bendMul = mag(e.bendMul);
  if (f(e.vibSpd)) e.vibSpd = mag(e.vibSpd);
  if (f(e.vibDep)) e.vibDep = mag(e.vibDep);
}

// D7 (cancelAndHoldAtTime, index.html bassNote): the lane keeps the value it has at t, and a ramp that is still running is cut THERE (it goes on gliding up to t); the core's Auto::hold would drop the ramp's end event and
// leave the lane at the ramp's start value until a step at t.
static void holdRamp(Auto& a, double t) {
  const double v = a.at(t); bool ramp = false;
  for (int i = 0; i < a.size(); i++) { const AutoEv& e = a.event(i); if (e.t >= t) { ramp = e.k == 1; break; } }
  a.cancel(t); if (ramp) a.lin(v, t); else a.set(v, t);
}

// ---- notes -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
bool LivePool::note(const SeqEvent& e0, int pad, double bpm, const double* arp, int choke, bool* poly) {
  if (poly) *poly = false;
  if (!has(pad)) return false;
  PadSlot& ps = pads_[pad];
  Node* n = e0.track >= 0 && e0.track < kVoiceTracks ? ps.byTrack[e0.track] : nullptr;
  if (!n) { st_.noNode++; return false; }
  if (!std::isfinite(e0.t) || !std::isfinite(e0.rate) || !std::isfinite(e0.gain) || !std::isfinite(e0.pan) || ((e0.has & SH_SLIDE) && (!std::isfinite(e0.slideFrom) || !std::isfinite(e0.slideDur)))) { st_.refused++; return false; }   // (the JS would throw inside liveStart: nothing is booked)
  SeqEvent e = e0; sanitize(e, arp);
  if (ps.kind == LiveKind::Vox && e.poly) { if (poly) *poly = true; curChoke_ = choke; return polyVox(e, ps, bpm, arp); }
  // the mono branch (liveNote): every voice of the track but this node's own gives way (its gate goes low at the note), the list is cleared, then the choke group of the other tracks
  for (Node* o : byTrack_[e.track]) { if (o != n && o->inT) kill(*o, e.t); o->inT = false; }
  chokeKill(e.track, choke, e.t);
  curChoke_ = choke;
  startNote(*n, e, bpm, arp);
  st_.played++;
  return true;
}

// polyVox (core-processor.js liveNote, the `ev.poly && lk.vox` branch): four nodes a track; the syllable of the note (notes within 4 ms share one), the node it takes: the same column still sounding, else a free one, else the one
// that started longest ago (ties: the first); a note that would take the node that started at the very same time with none free is consumed and silent.
bool LivePool::polyVox(const SeqEvent& e, PadSlot& ps, double bpm, const double* arp) {
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
  startNote(*pick, e, bpm, arp, kk, true);
  st_.played++;
  return true;
}

void LivePool::startNote(Node& n, const SeqEvent& e, double bpm, const double* arp, double fk, bool hasFk) {
  const double t = std::isfinite(e.t) ? std::max(e.t, now_) : now_;   // Math.max(ev.t, currentTime)
  const int64_t T = sample_at_or_after(t, sr_);
  const bool leg = (e.has & SH_SLIDE) && e.slideDur > 0;
  cancelFrom(n, T);                                                    // gate, trig, leg
  // the rate: hold, then the slide or the step (the list is folded like the sampler's rather than a point dropped: P5)
  if (n.rate.size() + 3 > n.rate.capacity()) { n.rate.prune(now_); if (n.rate.size() + 3 > n.rate.capacity()) { n.rate.prune(t); n.rate.cancel(t); } }
  holdRamp(n.rate, t);   // D7: the page's cancelAndHoldAtTime
  if (leg) { n.rate.set(e.slideFrom, t); n.rate.lin(e.rate, t + e.slideDur); } else n.rate.set(e.rate, t);
  if (n.kind == LiveKind::Bass && std::isfinite(bpm) && bpm != n.lastBpm) { bookCtl(n, LiveEvType::Bpm, t, bpm); n.lastBpm = bpm; }   // P4: the bpm lane follows the tempo (SEV_TEMPO comes after its step's notes, never before the first)
  TEv on; on.T = T; on.type = LiveEvType::NoteOn; on.leg = leg; on.acc = n.kind != LiveKind::Bass && n.kind != LiveKind::Vox && e.acc;
  if (n.kind == LiveKind::Vox) { on.v = hasFk ? fk : leg ? std::max(0.0, n.sylN - 1) : n.sylN++; on.same = leg; }   // syl lane: the node's own counter, or polyVox's; `same` lane: the slide
  book(n, on); rec(n, 'n', t, on.v, T, leg, on.acc, on.same);
  if (n.g.size() + 2 > n.g.capacity()) n.g.prune(now_);
  if (n.pn.size() + 2 > n.pn.capacity()) n.pn.prune(now_);
  n.g.set(e.gain, t); n.pn.set(std::max(-1.0, std::min(1.0, (e.pan - 128) / 127)), t);
  if (n.kind == LiveKind::Bass && (e.has & SH_WOB) && std::isfinite(e.wob)) bookCtl(n, LiveEvType::Wob, t, e.wob);
  n.serial = nextSerial_; n.choke = curChoke_; n.done = false; n.quiet = 0;
  n.lastOn = t; n.curLive = true; n.curStop = false; n.curCol = e.col; n.base = e.rate; n.vrate = e.rate; n.inT = true; n.used = true;
  if (!leg) pitchMods(n, t, e.rate, e, arp);
  if (e.has & SH_CUT) kill(n, e.cut);
  if (e.has & SH_REL) kill(n, e.rel);
}

void LivePool::bookCtl(Node& n, LiveEvType type, double t, double v) {
  TEv e; e.T = sample_at_or_after(std::max(t, now_), sr_); e.type = type; e.v = v; book(n, e);
}

// pitchMods (core-processor.js :525-545) on the node's rate lane. An ARP row books arpN + 1 points: the list is folded before it would overflow, and only if that is not enough the new arpeggio replaces what is booked from its first step on (P5; as the sampler's).
void LivePool::pitchMods(Node& n, double t, double base, const SeqEvent& e, const double* arp) {
  AutoN<kAutoRate>& pr = n.rate;
  if (e.has & SH_ARP) {
    if (pr.size() + e.arpN + 1 > pr.capacity()) {
      const double t1 = e.arpN ? std::max(arp[0], t) : t;
      pr.prune(now_);
      if (pr.size() + e.arpN + 1 > pr.capacity()) { pr.prune(t1); pr.cancel(t1); }
    }
    for (int q = 0; q < e.arpN; q++) pr.set(base * arp[2 * q + 1], std::max(arp[2 * q], t));
    if (e.arpN) pr.set(base, arp[2 * (e.arpN - 1)] + (e.arpN > 1 ? arp[2] - arp[0] : 0.02));
  } else if (e.has & SH_BEND) {
    if (pr.size() + 2 > pr.capacity()) pr.prune(now_);
    pr.set(base, t); pr.lin(base * e.bendMul, e.bendT1); n.vrate = base * e.bendMul;
  } else if ((e.has & SH_VIB) && e.vibDep != 0 && e.vibDep == e.vibDep) {
    const double d = std::max(0.02, e.vibT1 - t), hz = 1 + e.vibSpd * 1.2, st = e.vibDep / 15 * 1.5, t0 = t + 0.0005, dd = d - 0.001;
    if (pr.size() + 2 > pr.capacity()) pr.prune(now_);
    pr.set(base, t);
    Curve cv; cv.fn = voice_vib_curve; cv.p[0] = base; cv.p[1] = st; cv.p[2] = hz; cv.p[3] = d; cv.p[4] = t0; cv.p[5] = dd;
    pr.curve(cv, t0, dd);
  }
}

// modVoice for a live voice: gain and pan on the node's level, `mul` and the bends on its rate, cut / rel as kills; D6 (the page's modVoice): a `wob` moves the bass node's wobble at the row's time.
void LivePool::modVoice(Node& n, const SeqEvent& e, const double* arp) {
  if (e.has & SH_GAIN) { if (n.g.size() + 2 > n.g.capacity()) n.g.prune(now_); n.g.set(e.gain, e.t); }
  if (e.has & SH_PAN) { if (n.pn.size() + 2 > n.pn.capacity()) n.pn.prune(now_); n.pn.set(std::max(-1.0, std::min(1.0, (e.pan - 128) / 127)), e.t); }
  if (e.has & SH_MUL) { n.vrate = n.base * e.mul; if (n.rate.size() + 2 > n.rate.capacity()) n.rate.prune(now_); n.rate.set(n.vrate, e.t); }
  pitchMods(n, e.t, n.vrate, e, arp);
  if (e.has & SH_CUT) kill(n, e.cut);
  if (e.has & SH_REL) kill(n, e.rel);
  if (n.kind == LiveKind::Bass && (e.has & SH_WOB) && std::isfinite(e.wob)) bookCtl(n, LiveEvType::Wob, e.t, e.wob);
}

void LivePool::mod(const SeqEvent& e0, const double* arp) {
  if (e0.track < 0 || e0.track >= kVoiceTracks) return;
  if (!std::isfinite(e0.t)) { st_.refused++; return; }
  SeqEvent e = e0; sanitize(e, arp);
  for (Node* n : byTrack_[e.track]) if (n->inT && n->curLive && !(n->curStop && n->stopAt <= e.t)) modVoice(*n, e, arp);   // live1
}
void LivePool::off(const SeqEvent& e) {
  if (e.track < 0 || e.track >= kVoiceTracks) return;
  if (!std::isfinite(e.t)) { st_.refused++; return; }
  for (Node* n : byTrack_[e.track]) {
    if (!n->inT) continue;
    if ((e.has & SH_COLS) && !(n->curCol >= 0 && n->curCol < 8 && ((e.cols >> n->curCol) & 1))) continue;
    kill(*n, e.t); n->inT = false;
  }
}
void LivePool::stp(const SeqEvent& e) {
  if (!std::isfinite(e.t)) { st_.refused++; return; }
  for (int k = 0; k < kVoiceTracks; k++) if (e.flag || k == e.track) for (Node* n : byTrack_[k]) if (n->inT) { kill(*n, e.t); n->inT = false; }
}
void LivePool::tempo(double t, double bpm) {
  if (!std::isfinite(bpm) || !std::isfinite(t)) { st_.refused++; return; }
  for (Node* n : active_) if (n->kind == LiveKind::Bass) { bookCtl(*n, LiveEvType::Bpm, t, bpm); n->lastBpm = bpm; }
}
void LivePool::stopAll(double at) {   // stopAll(at): every node's voice lets go at `at`; what is booked at or after it is taken back, the gate goes low; the syllable counters and voxG start again
  const double t = std::max(at, now_); const int64_t T = sample_at_or_after(t, sr_);
  for (Node* n : active_) {
    if (n->curLive && !(n->curStop && n->stopAt <= at)) { n->curStop = true; n->stopAt = at; n->stopT = T; }
    if (n->used) { cancelFrom(*n, T); TEv off; off.T = T; off.type = LiveEvType::NoteOff; book(*n, off); rec(*n, 'o', t, 0, T); }
    n->sylN = 0; n->inT = false;
  }
  for (PadSlot& p : pads_) for (VoxG& g : p.vg) g = VoxG();
}
double LivePool::debugRateAt(int track, int pad, int j, double t) const {
  if (pad < 0 || pad >= kLivePads || track < 0 || track >= kVoiceTracks) return std::nan("");
  const PadSlot& ps = pads_[pad]; const Node* n = j == 0 ? ps.byTrack[track] : (ps.poly.empty() ? nullptr : ps.poly[(size_t)track * kVoicePoly + (size_t)(j - 1)]);
  return n ? n->rate.at(t) : std::nan("");
}

bool LivePool::debugNode(int track, int pad, int j, LiveDebugNode* o) const {
  if (pad < 0 || pad >= kLivePads || track < 0 || track >= kVoiceTracks) return false;
  const PadSlot& ps = pads_[pad]; const Node* n = j == 0 ? ps.byTrack[track] : (ps.poly.empty() ? nullptr : ps.poly[(size_t)track * kVoicePoly + (size_t)(j - 1)]);
  if (!n) return false;
  o->tl = n->tl; o->ntl = n->ntl; o->rate = &n->rate; o->g = &n->g; o->pn = &n->pn; o->sylN = n->sylN; o->lastOn = n->lastOn; o->inT = n->inT; o->curLive = n->curLive; o->curStop = n->curStop; o->stopAt = n->stopAt; o->curCol = n->curCol;
  return true;
}
bool LivePool::debugVoxG(int track, int pad, double* sylN, double* lastT, double* lastK) const {
  if (pad < 0 || pad >= kLivePads || track < 0 || track >= kVoiceTracks || pads_[pad].vg.empty()) return false;
  const VoxG& g = pads_[pad].vg[(size_t)track]; *sylN = g.sylN; *lastT = g.lastT; *lastK = g.lastK; return true;
}

// ---- the track's one voice list (the live part) ----------------------------------------------------------------------------------------------------------------------------------------------
static inline bool live1(bool inT, bool curLive, bool curStop, double stopAt, double t) { return inT && curLive && !(curStop && stopAt <= t); }
void LivePool::chokeKill(int track, int ck, double t) {   // for (k != track) for (v of T(k)) if (v.choke === ck) kill(v, t)
  if (!ck) return;
  for (int k = 0; k < kVoiceTracks; k++) if (k != track) for (Node* o : byTrack_[k]) if (o->inT && o->choke == ck) kill(*o, t);
}
void LivePool::take(const SeqEvent& e, int choke) {
  if (e.track < 0 || e.track >= kVoiceTracks) return;
  for (Node* o : byTrack_[e.track]) if (o->inT && !live1(o->inT, o->curLive, o->curStop, o->stopAt, e.t)) o->inT = false;   // (the sweep)
  if (!e.poly) for (Node* o : byTrack_[e.track]) if (o->inT) { kill(*o, e.t); o->inT = false; }
  chokeKill(e.track, choke, e.t);
}
int LivePool::liveOnTrack(int track, double t) const {
  int c = 0; if (track >= 0 && track < kVoiceTracks) for (const Node* o : byTrack_[track]) if (live1(o->inT, o->curLive, o->curStop, o->stopAt, t)) c++;
  return c;
}
bool LivePool::oldestOnTrack(int track, uint64_t& serial) const {
  bool h = false; if (track >= 0 && track < kVoiceTracks) for (const Node* o : byTrack_[track]) if (o->inT && (!h || o->serial < serial)) { serial = o->serial; h = true; }
  return h;
}
void LivePool::releaseOldest(int track, double t) {
  Node* b = nullptr; if (track >= 0 && track < kVoiceTracks) for (Node* o : byTrack_[track]) if (o->inT && (!b || o->serial < b->serial)) b = o;
  if (b) { kill(*b, t); b->inT = false; }
}
void LivePool::bookEnd(int64_t T) { for (Node* n : active_) { TEv e; e.T = T; e.type = LiveEvType::End; book(*n, e); } }
int LivePool::active() const { int c = 0; for (const Node* n : active_) if (n->curLive && !n->done) c++; return c; }
void LivePool::releaseHeld(double at) { for (Node* n : active_) if (n->curLive && !n->curStop) kill(*n, at); }

void LivePool::kill(Node& n, double at) {   // kill(v, at) for a live voice: the gate goes low at `at`; a stop before a booked later note takes that note back
  if (!n.curLive || (n.curStop && n.stopAt <= at)) return;
  n.curStop = true; n.stopAt = at;
  const double t = std::max(at, now_); const int64_t T = sample_at_or_after(t, sr_); n.stopT = T;
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
    if (e.T < clk) { if (e.type == LiveEvType::NoteOn) st_.late++; o.at = 0; } else o.at = (uint32_t)(e.T - clk);
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
  if (tap_) tap_(tapUser_, n.track, n.pad, n.j, sL, sR, frames, q0);
  if (n.curLive && !n.done && n.curStop) {   // C4: the voice ends when its gate is off and the output stays under 1e-5 for 4096 samples, or 4 s after the off
    const int64_t end = q0 + frames;
    if (end > n.stopT) {
      const int from = (int)std::max<int64_t>(0, n.stopT - q0); float pk = 0; for (int i = from; i < frames; i++) pk = std::max(pk, std::max(std::fabs(sL[i]), std::fabs(sR[i])));
      if (pk < 1e-5f) n.quiet += frames - from; else n.quiet = 0;
      if (n.quiet >= 4096 || (double)(end - n.stopT) >= 4 * sr_) n.done = true;
    }
  }
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
