// C1: see chost.h.
#include "chains/chost.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <new>

namespace thunder {

using chains::ChainsNode;

ChainsPlan::~ChainsPlan() {
  assert(settled_ || noop_);   // a plan destroyed while in flight leaves the host's inFlight_ set (chost.h, protocol 3)
  for (int i = 0; i < nTrk_; i++) { delete trk_[i].fresh; if (trk_[i].patch) chains::chains_free_patch(trk_[i].patch); }
  for (int i = 0; i < nOldNode_; i++) delete oldNode_[i];
  for (int i = 0; i < nOldPatch_; i++) if (oldPatch_[i]) chains::chains_free_patch(oldPatch_[i]);
}

ChainsHost::ChainsHost(double sampleRate, size_t budgetBytes) : sr_(sampleRate), q_(128.0 / sampleRate), budget_(budgetBytes ? budgetBytes : kChainsHostDefaultBudget) {}
ChainsHost::~ChainsHost() { for (int t = 0; t < kSongNT; t++) delete node_[t]; }

// pass() (index.html:25909-25913, busStemPlan :25816-25820, busClosure :25823-25827)
uint32_t ChainsHost::keepMask(const BusPlan& plan, uint32_t audible, uint32_t noteTracks) {
  uint32_t keep = audible & noteTracks;
  for (bool more = true; more;) { more = false; for (int b = 0; b < kSongNT; b++) if (!(keep >> b & 1u) && (audible >> b & 1u) && (plan.feedMask[b] & keep)) { keep |= 1u << b; more = true; } }   // busStemPlan
  uint32_t k2 = keep;
  for (bool more = true; more;) { more = false; for (int b = 0; b < kSongNT; b++) if ((k2 >> b & 1u) && (plan.feedMask[b] & audible & ~k2)) { k2 |= plan.feedMask[b] & audible; more = true; } }   // busClosure
  return k2;
}

// the bytes of one node's patch from the document alone (the engine's own figures, cmem.inc, per module): what the engine would hold once the patch is built
static double docBytes(Json patch, double sr) {
  Json mj = patch.get("modules"); double b = 0; if (!mj.isArr()) return 0;
  for (size_t i = 0; i < mj.size(); i++) { Json m = mj.at(i); if (!m.isObj()) continue; Json t = m.get("type"); if (t.isStr() && chains::cinfo(t.asStr())) b += chains::chains_module_bytes(t.asStr(), sr); }
  return b;
}

// ---- control thread --------------------------------------------------------------------------------------------------------------------
ChainsPlan* ChainsHost::plan(Json chainPatch, const bool* chainOn, uint32_t heard, const ChainsSongTransport& song) {
#if defined(__cpp_exceptions)
  try { return planImpl(chainPatch, chainOn, heard, song); } catch (const std::bad_alloc&) { return nullptr; }   // everything planImpl made is freed by its unique_ptr; the control view and inFlight_ are touched only at its very end
#else
  return planImpl(chainPatch, chainOn, heard, song);
#endif
}
ChainsPlan* ChainsHost::planImpl(Json chainPatch, const bool* chainOn, uint32_t heard, const ChainsSongTransport& song) {
  if (inFlight_) { st_.busyRefused++; return nullptr; }
  // the first call of the module tables makes static data (cdef / cinfo / the registry): do it here, so the audio thread never meets a first call
  (void)chains::cinfo_count(); (void)chains::chainsTable(); (void)chains::cdef("trackIn"); (void)chains::cdef("chainOut"); (void)chains::cdef("smixer");
  std::unique_ptr<ChainsPlan> pl(new (std::nothrow) ChainsPlan); if (!pl) return nullptr;
  ChainsPlan& P = *pl; P.song_ = song; P.newSong_ = haveLast_ && !(song == lastSong_);
  P.bus_ = bus_plan(chainPatch, chainOn);
  const uint32_t feedMask = chains_feed(chainPatch, chainOn);
  const bool okPatch = chainPatch.isObj() && chainPatch.str("format") == "chains.patch";   // routeTrack: wantChn = chainsFeed(t) && patch format
  uint32_t want = 0; for (int t = 0; t < kSongNT; t++) if (okPatch && (feedMask >> t & 1u) && (heard >> t & 1u)) want |= 1u << t;
  for (int t = 0; t < kSongNT; t++) if (ctlNode_[t]) P.nodeMask_ |= 1u << t;   // (a no-op plan leaves this)
  // D7: the memory budget, before anything is built
  const double pb = docBytes(chainPatch, sr_); P.patchBytes_ = (size_t)pb;
  { double total = 0; for (int t = 0; t < kSongNT; t++) if (want >> t & 1u) total += (ctlNode_[t] ? 2 * pb : pb) + (double)kChainsNodeOverhead; P.bytes_ = (size_t)total;
    if (total > (double)budget_) { P.overBudget_ = P.noop_ = true; st_.overBudget++; st_.refused++; return pl.release(); } }
  bool refused = false;
  {
    for (int t = 0; t < kSongNT && !refused; t++) {
      ChainsPlan::Trk* k = nullptr;
      if (want >> t & 1u) {
        if (ctlNode_[t]) {
          k = &P.trk_[P.nTrk_++]; k->track = t; k->node = ctlNode_[t];
          k->patch = ctlNode_[t]->engine().preparePatch(chainPatch);   // refused (null): the whole plan is
          if (!k->patch) refused = true;
        } else {
          chains::EngineOptions o; o.transport.tempo = song.tempo; o.transport.lpb = song.lpb; o.transport.ticks = song.ticks; o.transport.playing = false;   // processorOptions.transport (index.html:21137)
          std::unique_ptr<ChainsNode> n(new ChainsNode(sr_, t, o));
          if (!n->engine().valid() || !n->engine().loadPatch(chainPatch)) { refused = true; break; }
          k = &P.trk_[P.nTrk_++]; k->track = t; k->node = n.get(); k->fresh = n.release();
        }
        if (k && P.bus_.nFeeds[t] > 0) { k->nBus = P.bus_.nFeeds[t] < kMixFeedMax ? P.bus_.nFeeds[t] : kMixFeedMax; for (int j = 0; j < k->nBus; j++) k->busMap[j] = P.bus_.feeds[t][j]; }
      } else if (ctlNode_[t]) { k = &P.trk_[P.nTrk_++]; k->track = t; k->remove = true; }
    }
  }
  if (!refused) P.nodeMask_ = want;
  if (refused) {   // one document, refused: nothing of this plan is used (the destructor frees what it made)
    for (int i = 0; i < P.nTrk_; i++) { delete P.trk_[i].fresh; P.trk_[i].fresh = nullptr; if (P.trk_[i].patch) chains::chains_free_patch(P.trk_[i].patch); P.trk_[i].patch = nullptr; }
    P.nTrk_ = 0; P.noop_ = true; P.refused_ = 1; st_.refused++; P.nodeMask_ = 0; for (int t = 0; t < kSongNT; t++) if (ctlNode_[t]) P.nodeMask_ |= 1u << t;
    return pl.release();
  }
  inFlight_ = true;
  return pl.release();
}
void ChainsHost::settle(ChainsPlan* p) {
  if (!p || p->noop_ || p->settled_) return;
  p->settled_ = true; inFlight_ = false;
  if (!p->committed_) return;   // abandoned: the control view stays what it was
  for (int i = 0; i < p->nTrk_; i++) { const ChainsPlan::Trk& k = p->trk_[i]; ctlNode_[k.track] = k.remove ? nullptr : k.node; }
  lastSong_ = p->song_; haveLast_ = true;
}

// ---- audio thread ----------------------------------------------------------------------------------------------------------------------
void ChainsHost::applyTransport(ChainsNode& n) {   // the page's {tempo, lpb, ticks, playing: true} message: Object.assign over the engine's transport (startN + 1 when it starts playing)
  chains::ChainsEngine& e = n.engine(); waves::Transport t = e.transport();
  t.tempo = tp_.tempo; t.lpb = tp_.lpb; t.ticks = tp_.ticks; t.playing = true;
  e.setTransport(t);
}
void ChainsHost::commit(ChainsPlan* p, Mixer& mix) {
  if (!p || p->noop_ || p->committed_) return;
  p->committed_ = true; p->nOldNode_ = 0; p->nOldPatch_ = 0;
  const bool moved = !tpSet_ || p->newSong_;
  if (moved) { tp_ = p->song_; tpSet_ = true; }
  for (int i = 0; i < p->nTrk_; i++) {
    ChainsPlan::Trk& k = p->trk_[i]; const int t = k.track;
    if (k.remove) { if (node_[t]) p->oldNode_[p->nOldNode_++] = node_[t]; node_[t] = nullptr; continue; }
    if (k.fresh) { if (node_[t]) p->oldNode_[p->nOldNode_++] = node_[t]; node_[t] = k.fresh; k.fresh = nullptr; applyTransport(*node_[t]); }
    else if (k.patch && node_[t]) { p->oldPatch_[p->nOldPatch_++] = node_[t]->engine().swapPatch(k.patch); k.patch = nullptr; applyTransport(*node_[t]); }   // swapPatch applied the SAVED transport: the song's goes over it again
    if (node_[t]) { int bm[kMixFeedMax]; for (int j = 0; j < k.nBus; j++) bm[j] = k.busMap[j]; node_[t]->engine().setBusMap(bm, k.nBus); }
  }
  nNodes_ = 0; for (int t = 0; t < kSongNT; t++) if (node_[t]) { nNodes_++; if (moved) applyTransport(*node_[t]); }
  for (int t = 0; t < kSongNT; t++) mix.setChain(t, node_[t] ? &ChainsHost::hook : nullptr, node_[t] ? this : nullptr);
  mix.setPlan(p->bus_);   // last: a bus never exists before its node
}

// the two due-frame rules (chost.h)
int64_t ChainsHost::tempoDue(double t) const { if (!(t > 0)) return 0; const double k = std::ceil(t / q_) * 128.0; return k >= 9e15 ? (int64_t)9e15 : (int64_t)k; }
int64_t ChainsHost::chcDue(double t) const { if (!(t > 0)) return 0; const double f = std::ceil(t * sr_); const double d = std::ceil(f / 128.0) * 128.0; return d >= 9e15 ? (int64_t)9e15 : (int64_t)d; }

bool ChainsHost::put(const Ev& e) {
  if (nEv_ >= kChainsHostEvents) { st_.droppedEvents++; return false; }
  int i = nEv_; while (i > 0 && ev_[i - 1].due > e.due) { ev_[i] = ev_[i - 1]; i--; }   // stable: after the events due at the same frame
  ev_[i] = e; nEv_++; return true;
}
bool ChainsHost::event(const SeqEvent& e) {
  Ev v; v.track = 0; v.p = 0; v.v = e.v; v.tks = e.tks;
  if (e.type == SEV_CHC) {
    if (!(e.t == e.t) || e.track < 0 || e.track >= kSongNT || e.p < 1 || e.p > chains::kBypSwitches || !std::isfinite(e.v)) { st_.badEvents++; return false; }
    if (!node_[e.track]) { st_.chcNoNode++; return false; }   // no node for the track: nothing to switch (and nothing queued: the queue is for events that will be applied)
    v.type = SEV_CHC; v.track = (int8_t)e.track; v.p = (int8_t)e.p; v.due = chcDue(e.t);
  } else if (e.type == SEV_TEMPO) {
    if (!(e.t == e.t) || !std::isfinite(e.v)) { st_.badEvents++; return false; }
    if (nNodes_ == 0) { tp_.tempo = e.v; if (e.tks > 0) tp_.ticks = e.tks; return true; }   // nothing to apply it to: the host's transport (a node made later starts from it)
    v.type = SEV_TEMPO; v.due = tempoDue(e.t);
  } else return false;
  return put(v);
}
void ChainsHost::beginBlock(int64_t frame) {
  int n = 0;
  while (n < nEv_ && ev_[n].due <= frame) {
    const Ev& e = ev_[n++];
    if (e.type == SEV_CHC) { if (node_[e.track]) { node_[e.track]->engine().setBypassSwitch(e.p, e.v); st_.eventsApplied++; } else st_.chcNoNode++; }   // (the node went away after the event was queued: counted, not applied)
    else {
      st_.eventsApplied++; tp_.tempo = e.v; if (e.tks > 0) tp_.ticks = e.tks;
      for (int t = 0; t < kSongNT; t++) if (node_[t]) { chains::ChainsEngine& g = node_[t]->engine(); waves::Transport tr = g.transport(); tr.tempo = e.v; if (e.tks > 0) tr.ticks = e.tks; g.setTransport(tr); }
    }
  }
  if (n) { std::memmove(ev_, ev_ + n, sizeof(Ev) * (size_t)(nEv_ - n)); nEv_ -= n; }
}

void ChainsHost::hook(void* user, int track, float* L, float* R, int frames, double, const MixFeed* feeds, int nFeeds) {
  ChainsHost* h = static_cast<ChainsHost*>(user);
  if (track < 0 || track >= kSongNT) return;
  ChainsNode* nd = h->node_[track]; if (!nd) return;
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
