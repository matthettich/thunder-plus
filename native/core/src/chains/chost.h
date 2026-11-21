// C1: THE CHAINS HOST (offline): the song's chains (one chains::ChainsNode per CHN track that is heard, the bus tracks included) plugged into the mixer.
// The engine (chains.h, U31) is the page's `thunder-chains` worklet; this is what the page does AROUND it (index.html routeTrack / chainsNode / busSync / pass()):
// which tracks get a node, the node's bus map, the song's transport, the events that reach a node. docs/port/CHAINS-HOST-DESIGN-DRAFT.md (sections 4 and 9, the lead's rulings) is the design,
// docs/port/C1G-NOTES.md and native/tools/chains-song-dump.mjs the golden it must equal bit for bit (ctest chains_song).
//
// ZERO LATENCY (ruling D1): ChainsNode::process runs per 128-frame quantum inside the mixer's strip (TRIM -> node -> [track effects] -> fader -> pan); nothing is reported to the PDC (the page counts none).
//
// THREAD RULES. plan() runs on the control thread and does everything that allocates (the nodes, the prepared patches, the bus plan, the first call of every lazily made table); it returns a ChainsPlan.
// commit(), event() and hook() run on the audio thread, between blocks, and allocate nothing, take no lock and free nothing: commit stores pointers and copies fixed-size maps, swaps a prepared patch
// in, then sets the mixer's hooks and, LAST, its bus plan; what it replaces (old patches, retired nodes) is left in the plan for the control thread to free by deleting it.
//
// EVENTS (SongRenderer::dispatch -> event()): `chc` (the tracker's BYP b1..b15) and `tempo` are kept in a fixed time-sorted array (kChainsHostEvents; a full array drops the newest and counts it) and applied
// at the START of the first quantum a hook runs in whose index is >= the event's quantum, to the nodes, before any node of that quantum runs. TWO RULES (the page's), not one:
//   tempo: quantum k = ceil(t / q) with q = 128.0 / sr, in double (index.html:23229 `Math.ceil(ev.t / q) * q`, ac.suspend(at)); for a t that is exactly a quantum start this gives k + 1 for some k (case f/byp-fuzz-quantum-start).
//   chc:   quantum k = ceil(ceil(t * sr) / 128), t * sr in double: what real Chrome does for a k-rate AudioParam (measured by the lead: docs/port/C1G-NOTES.md, "Chrome k-rate check"). NOT the tempo rule.
// The song's transport (tempo, lpb, ticks, playing) is applied to a fresh node after its patch is loaded and RE-APPLIED after every patch swap: a saved patch's own transport must not win (case j).
#ifndef THUNDER_CHAINS_CHOST_H
#define THUNDER_CHAINS_CHOST_H
#include <cstdint>
#include <vector>

#include "chains/chains.h"
#include "mix/mix.h"
#include "rt/json.h"
#include "seq/seq.h"

namespace thunder {

constexpr int kChainsHostEvents = 256;   // chc / tempo events waiting for their quantum (counted drops, as FxBank::kPend)

// The song's transport the nodes get (tempo, lpb, ticks); `playing` is true from sample 0 in an offline render (index.html:25862).
struct ChainsSongTransport { double tempo = 120, lpb = 4, ticks = 6; };

struct ChainsHostStats {
  uint64_t blocks = 0;            // node blocks run by the hook
  uint64_t failedBlocks = 0;      // blocks in which the engine failed: the track's output is silence (the buses are cleared, as the worklet's catch does)
  uint64_t droppedEvents = 0;     // events that found the array full
  uint64_t badEvents = 0;         // events with a track / switch / time / value that cannot be applied (dropped)
  uint64_t eventsApplied = 0;
  uint64_t feedMismatch = 0;      // a feed the mixer handed over whose track is not the bus map's j-th (cannot happen with a plan from bus_plan)
  uint64_t chcNoNode = 0;         // chc events for a track that has no node when they fall due (the page: no node, nothing)
};

// What plan() makes and commit() adopts. The control thread owns it before commit and again after (delete it: it frees what commit retired and anything it never adopted).
class ChainsPlan {
 public:
  ChainsPlan() = default;
  ChainsPlan(const ChainsPlan&) = delete; ChainsPlan& operator=(const ChainsPlan&) = delete;
  ~ChainsPlan();
  uint32_t nodeMask() const { return nodeMask_; }       // the tracks that have a node after this plan
  int refused() const { return refused_; }               // patches (fresh or new) the engine refused. The patch is ONE document for every node, so one refusal refuses the whole plan: it is a no-op (commit changes nothing: the playing patches, bus maps and
                                                         // bus plan stay; with nothing playing yet the tracks play dry and no bus exists)
  bool noop() const { return noop_; }
  const BusPlan& bus() const { return bus_; }
  int retiredPatches() const { return nOldPatch_; }      // after commit(): the patches it swapped out (the control thread frees them with the plan)
  int retiredNodes() const { return nOldNode_; }          // after commit(): the nodes it replaced or removed
  int nodes() const { int n = 0; for (uint32_t m = nodeMask_; m; m &= m - 1) n++; return n; }
  std::vector<int> busMap(int t) const { std::vector<int> v; for (int i = 0; i < nTrk_; i++) if (trk_[i].track == t && !trk_[i].remove) for (int j = 0; j < trk_[i].nBus; j++) v.push_back(trk_[i].busMap[j]); return v; }   // the bus map of a track this plan keeps (control side, tests)
 private:
  friend class ChainsHost;
  struct Trk { int track = 0; chains::ChainsNode* fresh = nullptr; waves::PatchImpl* patch = nullptr; bool remove = false; int nBus = 0; uint8_t busMap[kMixFeedMax] = {}; };
  Trk trk_[kSongNT]; int nTrk_ = 0;
  BusPlan bus_; uint32_t nodeMask_ = 0; int refused_ = 0; bool noop_ = false; ChainsSongTransport song_;
  chains::ChainsNode* oldNode_[kSongNT] = {}; int nOldNode_ = 0;       // filled by commit()
  waves::PatchImpl* oldPatch_[kSongNT] = {}; int nOldPatch_ = 0;
};

class ChainsHost {
 public:
  explicit ChainsHost(double sampleRate);   // control thread
  ~ChainsHost();                             // deletes the nodes it adopted (nothing may be running)
  ChainsHost(const ChainsHost&) = delete; ChainsHost& operator=(const ChainsHost&) = delete;

  // CONTROL THREAD. chainOn: kSongNT flags; heard: bit t = the track is heard (SongRenderer::heardMask(): in a stem only those, as pass() makes chains for `keep` only). A track gets a node when it is heard, its CHN flag is on
  // and the patch has a Chain Out for it (chains_feed). A track the control side already knows a node for keeps it and gets the new patch prepared for a swap; a refused patch leaves the playing one. Returns null only when
  // memory ran out.
  ChainsPlan* plan(Json chainPatch, const bool* chainOn, uint32_t heard, const ChainsSongTransport& song);

  // AUDIO THREAD (or the control thread before the first block). Nodes and maps first, the mixer's hooks, the bus plan last. The plan keeps what was replaced.
  void commit(ChainsPlan* p, Mixer& mix);

  // AUDIO THREAD: a SEV_CHC / SEV_TEMPO event (anything else is refused). true: kept for its quantum.
  bool event(const SeqEvent& e);

  // The mixer's hook (Mixer::setChain): the bus's feeds -> NodeIn.feeds in bus-map order, ChainsNode::process in place on the track's pair.
  static void hook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds);

  ChainsHostStats stats() const { return st_; }
  chains::ChainsNode* node(int t) { return t >= 0 && t < kSongNT ? node_[t] : nullptr; }   // (tests)
  int pendingEvents() const { return nEv_; }
  // the quantum an event of each kind falls due in (public so the tests can pin the two rules)
  int64_t tempoQuantum(double t) const;
  int64_t chcQuantum(double t) const;

 private:
  struct Ev { int64_t k; uint8_t type; int8_t track; int8_t p; double v, tks; };
  bool put(const Ev& e);
  void applyDue(int64_t qi);
  void applyTransport(chains::ChainsNode& n);
  double sr_, q_;                                  // q_ = 128.0 / sr in double, as the page's `q`
  chains::ChainsNode* node_[kSongNT] = {};         // audio side's view
  chains::ChainsNode* ctlNode_[kSongNT] = {};      // control side's view (what plan() has decided; plans are committed in order)
  ChainsSongTransport tp_; bool tpSet_ = false;    // the song's transport as it is now (tempo events move it)
  Ev ev_[kChainsHostEvents]; int nEv_ = 0;
  int64_t appliedQ_ = -1;
  ChainsHostStats st_;
};

}  // namespace thunder
#endif
