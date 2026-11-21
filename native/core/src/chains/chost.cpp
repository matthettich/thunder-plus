// C1: see chost.h.
#include "chains/chost.h"

#include <cmath>
#include <cstring>
#include <new>

namespace thunder {

using chains::ChainsNode;

ChainsPlan::~ChainsPlan() {
  for (int i = 0; i < nTrk_; i++) { delete trk_[i].fresh; if (trk_[i].patch) chains::chains_free_patch(trk_[i].patch); }
  for (int i = 0; i < nOldNode_; i++) delete oldNode_[i];
  for (int i = 0; i < nOldPatch_; i++) if (oldPatch_[i]) chains::chains_free_patch(oldPatch_[i]);
}

ChainsHost::ChainsHost(double sampleRate) : sr_(sampleRate), q_(128.0 / sampleRate) {}
ChainsHost::~ChainsHost() { for (int t = 0; t < kSongNT; t++) delete node_[t]; }

// ---- control thread --------------------------------------------------------------------------------------------------------------------
ChainsPlan* ChainsHost::plan(Json chainPatch, const bool* chainOn, uint32_t heard, const ChainsSongTransport& song) {
  // the first call of the module tables makes static data (cdef / cinfo / the registry): do it here, so the audio thread never meets a first call
  (void)chains::cinfo_count(); (void)chains::chainsTable(); (void)chains::cdef("trackIn"); (void)chains::cdef("chainOut"); (void)chains::cdef("smixer");
  std::unique_ptr<ChainsPlan> pl(new (std::nothrow) ChainsPlan); if (!pl) return nullptr;
  ChainsPlan& P = *pl; P.song_ = song;
  ChainsNode* saved[kSongNT]; for (int t = 0; t < kSongNT; t++) saved[t] = ctlNode_[t];
  P.bus_ = bus_plan(chainPatch, chainOn);
  const uint32_t feedMask = chains_feed(chainPatch, chainOn);
  const bool okPatch = chainPatch.isObj() && chainPatch.str("format") == "chains.patch";   // routeTrack: wantChn = chainsFeed(t) && patch format
  for (int t = 0; t < kSongNT; t++) {
    const bool want = okPatch && (feedMask >> t & 1u) && (heard >> t & 1u);
    ChainsPlan::Trk* k = nullptr;
    if (want) {
      if (ctlNode_[t]) {
        k = &P.trk_[P.nTrk_++]; k->track = t;
        k->patch = ctlNode_[t]->engine().preparePatch(chainPatch);   // refused (null): the playing patch stays
        if (!k->patch) P.refused_++;
      } else {
        ChainsNode* n = nullptr;
        try {
          chains::EngineOptions o; o.transport.tempo = song.tempo; o.transport.lpb = song.lpb; o.transport.ticks = song.ticks; o.transport.playing = false;   // processorOptions.transport (index.html:21137)
          n = new ChainsNode(sr_, t, o);
          if (!n->engine().valid() || !n->engine().loadPatch(chainPatch)) { delete n; n = nullptr; P.refused_++; }
        } catch (const std::bad_alloc&) { delete n; return nullptr; }
        if (!n) continue;   // no node: the track plays dry and no bus exists
        k = &P.trk_[P.nTrk_++]; k->track = t; k->fresh = n; ctlNode_[t] = n;
      }
      P.nodeMask_ |= 1u << t;
      if (P.bus_.nFeeds[t] > 0) { k->nBus = P.bus_.nFeeds[t] < kMixFeedMax ? P.bus_.nFeeds[t] : kMixFeedMax; for (int j = 0; j < k->nBus; j++) k->busMap[j] = P.bus_.feeds[t][j]; }
    } else if (ctlNode_[t]) {
      k = &P.trk_[P.nTrk_++]; k->track = t; k->remove = true; ctlNode_[t] = nullptr;
    }
  }
  if (P.refused_) {   // one document, refused: nothing of this plan is used; the control side's view is what it was (the plan's destructor frees what it made)
    for (int t = 0; t < kSongNT; t++) ctlNode_[t] = saved[t];
    for (int i = 0; i < P.nTrk_; i++) { delete P.trk_[i].fresh; P.trk_[i].fresh = nullptr; if (P.trk_[i].patch) chains::chains_free_patch(P.trk_[i].patch); P.trk_[i].patch = nullptr; }
    P.nTrk_ = 0; P.noop_ = true; P.nodeMask_ = 0; for (int t = 0; t < kSongNT; t++) if (saved[t]) P.nodeMask_ |= 1u << t;
  }
  return pl.release();
}

// ---- audio thread ----------------------------------------------------------------------------------------------------------------------
void ChainsHost::applyTransport(ChainsNode& n) {   // the page's {tempo, lpb, ticks, playing: true} message: Object.assign over the engine's transport (startN + 1 when it starts playing)
  chains::ChainsEngine& e = n.engine(); waves::Transport t = e.transport();
  t.tempo = tp_.tempo; t.lpb = tp_.lpb; t.ticks = tp_.ticks; t.playing = true;
  e.setTransport(t);
}
void ChainsHost::commit(ChainsPlan* p, Mixer& mix) {
  if (!p || p->noop_) return;
  if (!tpSet_) { tp_ = p->song_; tpSet_ = true; }
  p->nOldNode_ = 0; p->nOldPatch_ = 0;
  for (int i = 0; i < p->nTrk_; i++) {
    ChainsPlan::Trk& k = p->trk_[i]; const int t = k.track;
    if (k.remove) { if (node_[t]) p->oldNode_[p->nOldNode_++] = node_[t]; node_[t] = nullptr; continue; }
    if (k.fresh) { if (node_[t]) p->oldNode_[p->nOldNode_++] = node_[t]; node_[t] = k.fresh; k.fresh = nullptr; applyTransport(*node_[t]); }
    else if (k.patch) { p->oldPatch_[p->nOldPatch_++] = node_[t]->engine().swapPatch(k.patch); k.patch = nullptr; applyTransport(*node_[t]); }   // swapPatch applied the SAVED transport: the song's goes over it again
    if (node_[t]) { int bm[kMixFeedMax]; for (int j = 0; j < k.nBus; j++) bm[j] = k.busMap[j]; node_[t]->engine().setBusMap(bm, k.nBus); }
  }
  for (int t = 0; t < kSongNT; t++) mix.setChain(t, node_[t] ? &ChainsHost::hook : nullptr, node_[t] ? this : nullptr);
  mix.setPlan(p->bus_);   // last: a bus never exists before its node
}

// the two quantum rules (chost.h)
int64_t ChainsHost::tempoQuantum(double t) const { if (!(t > 0)) return 0; const double k = std::ceil(t / q_); return k >= 9e15 ? (int64_t)9e15 : (int64_t)k; }
int64_t ChainsHost::chcQuantum(double t) const { if (!(t > 0)) return 0; const double f = std::ceil(t * sr_); const double k = std::ceil(f / 128.0); return k >= 9e15 ? (int64_t)9e15 : (int64_t)k; }

bool ChainsHost::put(const Ev& e) {
  if (nEv_ >= kChainsHostEvents) { st_.droppedEvents++; return false; }
  int i = nEv_; while (i > 0 && ev_[i - 1].k > e.k) { ev_[i] = ev_[i - 1]; i--; }   // stable: after the events of the same quantum
  ev_[i] = e; nEv_++; return true;
}
bool ChainsHost::event(const SeqEvent& e) {
  Ev v; v.track = 0; v.p = 0; v.v = e.v; v.tks = e.tks;
  if (e.type == SEV_CHC) {
    if (!(e.t == e.t) || e.track < 0 || e.track >= kSongNT || e.p < 1 || e.p > chains::kBypSwitches || !std::isfinite(e.v)) { st_.badEvents++; return false; }
    v.type = SEV_CHC; v.track = (int8_t)e.track; v.p = (int8_t)e.p; v.k = chcQuantum(e.t);
  } else if (e.type == SEV_TEMPO) {
    if (!(e.t == e.t) || !std::isfinite(e.v)) { st_.badEvents++; return false; }
    v.type = SEV_TEMPO; v.k = tempoQuantum(e.t);
  } else return false;
  return put(v);
}
void ChainsHost::applyDue(int64_t qi) {
  if (qi <= appliedQ_) return;
  appliedQ_ = qi;
  int n = 0;
  while (n < nEv_ && ev_[n].k <= qi) {
    const Ev& e = ev_[n++]; st_.eventsApplied++;
    if (e.type == SEV_CHC) { if (node_[e.track]) node_[e.track]->engine().setBypassSwitch(e.p, e.v); else st_.chcNoNode++; }
    else {
      tp_.tempo = e.v; if (e.tks > 0) tp_.ticks = e.tks;
      for (int t = 0; t < kSongNT; t++) if (node_[t]) { chains::ChainsEngine& g = node_[t]->engine(); waves::Transport tr = g.transport(); tr.tempo = e.v; if (e.tks > 0) tr.ticks = e.tks; g.setTransport(tr); }
    }
  }
  if (n) { for (int i = n; i < nEv_; i++) ev_[i - n] = ev_[i]; nEv_ -= n; }
}

void ChainsHost::hook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds) {
  ChainsHost* h = static_cast<ChainsHost*>(user);
  if (track < 0 || track >= kSongNT) return;
  ChainsNode* nd = h->node_[track]; if (!nd) return;
  h->applyDue((int64_t)std::floor(t0 * h->sr_ / 128.0 + 0.5));   // the quantum this call is in (t0 = k * 128 / sr)
  const std::vector<int>& bm = nd->engine().busMap();
  const float* fp[2 * kMixFeedMax]; int nf = nFeeds < kMixFeedMax ? nFeeds : kMixFeedMax; if (nf < 0) nf = 0;
  for (int j = 0; j < nf; j++) { fp[2 * j] = feeds[j].L; fp[2 * j + 1] = feeds[j].R; if (j >= (int)bm.size() || bm[(size_t)j] != feeds[j].track) h->st_.feedMismatch++; }
  chains::NodeIn in; in.inL = L; in.inR = R; in.feeds = nf ? fp : nullptr; in.nFeeds = nf;
  chains::NodeOut out; out.L = L; out.R = R;   // in place: p1 copies the input into its own buffers first and writes the output last
  const uint64_t f0 = nd->engine().stats().failedBlocks;
  nd->process(in, out, frames);
  h->st_.blocks++;
  if (nd->engine().stats().failedBlocks != f0) h->st_.failedBlocks++;   // the engine cleared its buses: the output above is silence
}

}  // namespace thunder
