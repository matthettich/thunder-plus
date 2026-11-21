// C1: THE CHAINS HOST (offline): the song's chains (one chains::ChainsNode per CHN track that is heard, the bus tracks included) plugged into the mixer.
// The engine (chains.h, U31) is the page's `thunder-chains` worklet; this is what the page does AROUND it (index.html routeTrack / chainsNode / busSync / pass()):
// which tracks get a node, the node's bus map, the song's transport, the events that reach a node. docs/port/CHAINS-HOST-DESIGN-DRAFT.md (sections 4 and 9, the lead's rulings) is the design,
// docs/port/C1G-NOTES.md and native/tools/chains-song-dump.mjs the golden it must equal bit for bit (ctest chains_song).
//
// ZERO LATENCY (ruling D1): ChainsNode::process runs per 128-frame quantum inside the mixer's strip (TRIM -> node -> [track effects] -> fader -> pan); nothing is reported to the PDC (the page counts none).
//
// THREAD RULES. plan(), settle() and the destructors run on the control thread and do everything that allocates or frees (the nodes, the prepared patches, the bus plan, the first call of every lazily made table).
// commit(), beginBlock(), event() and hook() run on the audio thread, between blocks, and allocate nothing, take no lock and free nothing: commit stores pointers and copies fixed-size maps, swaps a prepared patch
// in, then sets the mixer's hooks and, LAST, its bus plan; what it replaces (old patches, retired nodes) is left in the plan for the control thread to free by deleting it. commit() must run in the audio callback
// (or before the first block), never concurrently with Mixer::render.
//
// OWNERSHIP PROTOCOL (one plan in flight):
//   1. p = plan(...): reads the control view (`ctlNode_`: the nodes of the last SETTLED plan), changes nothing of it, makes fresh nodes / prepared patches that only the plan owns. A second plan() while one is
//      in flight (not yet settled) returns null and counts (busyRefused). Out of memory: null, and everything it made is freed (the view was never touched).
//   2. commit(p, mixer): audio side. The nodes the plan made are now the host's, what it replaced is in the plan (retiredPatches() / retiredNodes()). A second commit of the same plan does nothing. A plan that is
//      refused as a whole (noop(): the patch was refused or the memory budget would be passed) commits as nothing.
//   3. settle(p): control side, ONCE when the plan's fate is known, committed or not: a committed plan's nodes become the control view; an abandoned one leaves it as it was. Then delete p (frees what it still
//      owns: nodes / patches it never gave away, and what commit retired). A plan must be settled before the next plan() (noop plans need no settle). Deleting a plan WITHOUT settle() leaves inFlight_ set (no plan
//      can be made again): ~ChainsPlan asserts against it.
//   THREADED HOST (C2): the plan must come BACK from the audio side through a ring (release on push, acquire on pop) before the control side reads committed_ / the nodes in settle() or deletes the plan;
//      until then the audio thread owns it.
//
// EVENTS (SongRenderer::dispatch -> event()): `chc` (the tracker's BYP b1..b15) and `tempo`. A tempo event moves the host's transport at once when no node exists; otherwise it and every chc wait in a fixed time-sorted
// array (kChainsHostEvents, sized for the worst real tracker row; a full array drops the newest and counts it). Each event stores the FRAME it is due at, and beginBlock(frame) (called once per block BEFORE the
// mixer renders it, by whoever owns the block clock: SongRenderer passes its own frame counter, a live host its own) applies every event due at or before that frame to the nodes, before any node of the block runs.
// TWO RULES (the page's), not one:
//   tempo: k = ceil(t / q) with q = 128.0 / sr, in double (index.html:23229 `Math.ceil(ev.t / q) * q`, ac.suspend(at)); due at frame k * 128 (for some quantum starts this is k + 1: case f/byp-fuzz-quantum-start).
//   chc:   f = ceil(t * sr), t * sr in double; due at ceil(f / 128) * 128: what real Chrome does for a k-rate AudioParam (measured: docs/port/C1G-NOTES.md, "Chrome k-rate check"). NOT the tempo rule.
// The 128 grid is the page's ABSOLUTE grid of the render: both rules are exact for a host whose blocks start on multiples of 128; a host with another block size or phase gets "the first block that starts at or
// after the due frame" (one block of lateness at most), never an early event.
// The song's transport (tempo, lpb, ticks, playing) is applied to a fresh node after its patch is loaded and RE-APPLIED after every patch swap: a saved patch's own transport must not win (case j). A plan made for
// a song whose transport differs from the previous plan's (the plan says so: newSong) replaces the host's; otherwise the host's (moved by tempo events) stays.
// MEMORY (ruling D7): one TOTAL budget per host. plan() adds up, BEFORE building anything, the bytes the engine says each node's patch needs (chains_module_bytes: cmem.inc) over the nodes the plan would have, counting
// the second set that exists during a swap, and refuses the whole plan over the budget (noop, overBudget()).
#ifndef THUNDER_CHAINS_CHOST_H
#define THUNDER_CHAINS_CHOST_H
#include <cstdint>
#include <vector>

#include "chains/chains.h"
#include "mix/mix.h"
#include "rt/json.h"
#include "seq/seq.h"

namespace thunder {

constexpr int kChainsHostEvents = 4096;   // events waiting for their block: 32 tracks x 30 (BYP 0Y: 15 switches and their 15 restores) = 960 a row, a few rows inside the sequencer's look-ahead
constexpr size_t kChainsHostDefaultBudget = (size_t)1536 << 20;   // 1.5 GiB for all the nodes of a host (D7); a phone host passes less
constexpr size_t kChainsNodeOverhead = 300 << 10;                 // what a node holds besides its patch: 128 tracks of input and bus buffers, the sends (about 280 KB), rounded up

// The song's transport the nodes get (tempo, lpb, ticks); `playing` is true from sample 0 in an offline render (index.html:25862).
struct ChainsSongTransport { double tempo = 120, lpb = 4, ticks = 6; bool operator==(const ChainsSongTransport& o) const { return tempo == o.tempo && lpb == o.lpb && ticks == o.ticks; } };

struct ChainsHostStats {
  uint64_t blocks = 0;            // node blocks run by the hook
  uint64_t failedBlocks = 0;      // blocks in which the engine failed: the track's output is silence (the buses are cleared, as the worklet's catch does)
  uint64_t droppedEvents = 0;     // events that found the array full
  uint64_t badEvents = 0;         // events with a track / switch / time / value that cannot be applied (dropped)
  uint64_t eventsApplied = 0;
  uint64_t feedMismatch = 0;      // a feed the mixer handed over whose track is not the bus map's j-th (cannot happen with a plan from bus_plan)
  uint64_t chcNoNode = 0;         // chc events for a track that has no node (the page: no node, nothing): dropped when they arrive
  uint64_t refused = 0;           // plans refused whole (the patch, or no memory)   (control side)
  uint64_t overBudget = 0;        // plans refused for the memory budget              (control side)
  uint64_t busyRefused = 0;       // plan() calls refused because a plan was in flight (control side)
};

// What plan() makes and commit() adopts. See the ownership protocol above.
class ChainsPlan {
 public:
  ChainsPlan() = default;
  ChainsPlan(const ChainsPlan&) = delete; ChainsPlan& operator=(const ChainsPlan&) = delete;
  ~ChainsPlan();
  uint32_t nodeMask() const { return nodeMask_; }       // the tracks that have a node after this plan (for a no-op plan: before it)
  int refused() const { return refused_; }               // 1 when the engine refused the patch. The patch is ONE document for every node, so one refusal refuses the whole plan: it is a no-op
  bool overBudget() const { return overBudget_; }        // the plan would pass the host's memory budget: refused whole, a no-op
  bool noop() const { return noop_; }                    // commit() changes nothing
  bool committed() const { return committed_; }
  size_t bytes() const { return bytes_; }                // what the nodes of the plan need in all (during a swap: the second patch included)
  size_t patchBytes() const { return patchBytes_; }      // what one node's patch needs (cmem.inc)
  const BusPlan& bus() const { return bus_; }
  int retiredPatches() const { return nOldPatch_; }      // after commit(): the patches it swapped out (the control thread frees them with the plan)
  int retiredNodes() const { return nOldNode_; }         // after commit(): the nodes it replaced or removed
  int nodes() const { int n = 0; for (uint32_t m = nodeMask_; m; m &= m - 1) n++; return n; }
  std::vector<int> busMap(int t) const { std::vector<int> v; for (int i = 0; i < nTrk_; i++) if (trk_[i].track == t && !trk_[i].remove) for (int j = 0; j < trk_[i].nBus; j++) v.push_back(trk_[i].busMap[j]); return v; }   // the bus map of a track this plan keeps (control side, tests)
 private:
  friend class ChainsHost;
  struct Trk { int track = 0; chains::ChainsNode* node = nullptr; /* the node after this plan (not owned) */ chains::ChainsNode* fresh = nullptr; /* owned until commit */ waves::PatchImpl* patch = nullptr; bool remove = false; int nBus = 0; uint8_t busMap[kMixFeedMax] = {}; };
  Trk trk_[kSongNT]; int nTrk_ = 0;
  BusPlan bus_; uint32_t nodeMask_ = 0; int refused_ = 0; bool overBudget_ = false, noop_ = false, committed_ = false, settled_ = false, newSong_ = false; size_t bytes_ = 0, patchBytes_ = 0; ChainsSongTransport song_;
  chains::ChainsNode* oldNode_[kSongNT] = {}; int nOldNode_ = 0;       // filled by commit()
  waves::PatchImpl* oldPatch_[kSongNT] = {}; int nOldPatch_ = 0;
};

class ChainsHost {
 public:
  explicit ChainsHost(double sampleRate, size_t budgetBytes = kChainsHostDefaultBudget);   // control thread
  ~ChainsHost();                             // deletes the nodes it adopted (nothing may be running)
  ChainsHost(const ChainsHost&) = delete; ChainsHost& operator=(const ChainsHost&) = delete;

  // The heard set of an offline mix (pass(): index.html:25909-25913 keepT = audible tracks that have notes, busStemPlan adds the buses a kept track feeds, busClosure (:25823) the audible sources of every kept bus).
  // audible: Mixer::audibleMask(); noteTracks: bit t = the song plays a note on track t. (A stem's heard set is the stem's own: SongRenderer::setStem.)
  static uint32_t keepMask(const BusPlan& plan, uint32_t audible, uint32_t noteTracks);

  // CONTROL THREAD. chainOn: kSongNT flags; heard: bit t = the track is heard (keepMask(), or a stem's heardMask(): pass() makes chains for `keep` only). A track gets a node when it is heard, its CHN flag is on
  // and the patch has a Chain Out for it (chains_feed). A track that has a node keeps it and gets the new patch prepared for a swap. Null: a plan is in flight (busyRefused), or memory ran out.
  ChainsPlan* plan(Json chainPatch, const bool* chainOn, uint32_t heard, const ChainsSongTransport& song);
  void settle(ChainsPlan* p);                // CONTROL THREAD: the plan's fate is known (see the protocol)

  // AUDIO THREAD (or the control thread before the first block). Nodes and maps first, the mixer's hooks, the bus plan last. The plan keeps what was replaced. A second commit of a plan does nothing.
  void commit(ChainsPlan* p, Mixer& mix);

  // AUDIO THREAD: a SEV_CHC / SEV_TEMPO event (anything else is refused). true: kept (or, a tempo with no node, applied to the host's transport).
  bool event(const SeqEvent& e);
  // AUDIO THREAD: the block that starts at `frame` is about to render: apply every event due at or before it (see EVENTS).
  void beginBlock(int64_t frame);

  // The mixer's hook (Mixer::setChain): the bus's feeds -> NodeIn.feeds in bus-map order, ChainsNode::process in place on the track's pair.
  static void hook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds);

  ChainsHostStats stats() const { return st_; }
  const ChainsSongTransport& transport() const { return tp_; }
  chains::ChainsNode* node(int t) { return t >= 0 && t < kSongNT ? node_[t] : nullptr; }   // (tests)
  int pendingEvents() const { return nEv_; }
  size_t budget() const { return budget_; }
  // the frame an event of each kind falls due at (public so the tests can pin the two rules)
  int64_t tempoDue(double t) const;
  int64_t chcDue(double t) const;

 private:
  struct Ev { int64_t due; uint8_t type; int8_t track; int8_t p; double v, tks; };
  ChainsPlan* planImpl(Json chainPatch, const bool* chainOn, uint32_t heard, const ChainsSongTransport& song);
  bool put(const Ev& e);
  void applyTransport(chains::ChainsNode& n);
  double sr_, q_;                                  // q_ = 128.0 / sr in double, as the page's `q`
  size_t budget_;
  chains::ChainsNode* node_[kSongNT] = {};         // audio side's view
  chains::ChainsNode* ctlNode_[kSongNT] = {};      // control side's view: the nodes of the last settled plan
  ChainsSongTransport tp_; bool tpSet_ = false;    // the song's transport as it is now (tempo events move it)
  ChainsSongTransport lastSong_; bool haveLast_ = false;   // control side: the song transport of the last settled plan
  int nNodes_ = 0;                                 // audio side: nodes now
  bool inFlight_ = false;                          // control side
  Ev ev_[kChainsHostEvents]; int nEv_ = 0;
  ChainsHostStats st_;
};

}  // namespace thunder
#endif
