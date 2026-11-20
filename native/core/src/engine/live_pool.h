// THE LIVE POOL (LIVE-ROUTE step 1): the live engines (bass, acid, mach; vox and the rest in later stages) as note players of a song render. A pad is declared live with addPad (control thread: the nodes
// are made there, none at a note); the pool holds one node per (track the pad may play on, pad). A node is a LiveShell (the U26 rule: a note event at its exact sample, control blocks counted from the last
// note event) plus the pool's bookkeeping: a TIMELINE of booked note events (cancelFrom: a note takes back what is booked at or after it, as the page's AudioParam cancel does), the RATE lane as an
// automation list in seconds (folded like the sampler's, never a dropped event: P5), and the level and pan the page's GainNode / StereoPanner give a node.
//
// L4 (RUN AHEAD): the shell's clock runs lat() samples ahead of the mixer's: in quantum [q0, q0 + 128) the shell is given the events of [q0 + lat, q0 + lat + 128) and returns the engine's samples for
// [q0, q0 + 128), so a note sounds on its own sample. A node's first call has lat() samples of pre-roll. An event for a time already inside the look-ahead is applied at the shell's clock and counted (liveLate).
// L5 (a note-off always wins): the timeline holds kTimeline events a node; when it is full a NoteOn is dropped (liveQueueDropped); a NoteOff discards what is booked at or after it (liveForcedOff) and is kept.
//
// REAL-TIME RULES: addPad allocates; note() and render() allocate nothing.
#ifndef THUNDER_ENGINE_LIVE_POOL_H
#define THUNDER_ENGINE_LIVE_POOL_H
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/live_shell.h"
#include "rt/auto.h"
#include "rt/sampletime.h"
#include "seq/seq.h"
#include "voice/voice.h"

namespace thunder {

enum class LiveKind : uint8_t { None = 0, Bass = 1, Vox = 2, Acid = 3, Mach = 4 };
constexpr int kLivePads = 256;

struct LivePadConfig {
  LiveKind kind = LiveKind::None;
  BassP bass; VoxP vox; AcidP acid; MachP mach;
  uint64_t trackMask = 0;   // bit t: the pad may play on track t (0 .. 32); 0 = all 33
};

// A decision trace for tests (LIVE-ROUTE L10(f)): what the pool books on a node, in the order it books it. Off by default (no cost, no allocation); a test gives it a vector with room.
struct LiveTraceRec { int track = 0, pad = 0, j = 0; char what = 0; double t = 0, v = 0; int64_t T = 0; bool leg = false, acc = false, same = false; };   // what: 'n' note-on (v = syllable), 'o' note-off, 'c' cancelFrom

struct LiveTEv { int64_t T = 0; LiveEvType type = LiveEvType::NoteOn; bool leg = false, acc = false, same = false; double v = 0; };   // one booked event of a node's timeline (the events not yet handed to the shell)
// A read-only view of a node, for tests (the decision-log gate, LIVE-ROUTE L10(f)).
struct LiveDebugNode { const LiveTEv* tl = nullptr; int ntl = 0; const Auto* rate = nullptr; const Auto* g = nullptr; const Auto* pn = nullptr; double sylN = 0, lastOn = -1; bool inT = false, curLive = false, curStop = false; double stopAt = 0; int curCol = 0; };

struct LiveStats { uint32_t played = 0, noNode = 0, late = 0, queueDropped = 0, forcedOff = 0, failed = 0; };

class LiveEng {   // one engine as the pool uses it (a LiveShell of one of the four cores behind a small interface)
 public:
  virtual ~LiveEng() = default;
  virtual void process(float* L, float* R, uint32_t n, const LiveEvent* ev, uint32_t nev) = 0;
  virtual void seek(uint64_t clock) = 0;
  virtual uint64_t clock() const = 0;
  virtual uint32_t latency() const = 0;
  virtual bool failed() const = 0;
  virtual void setRateFeed(const Auto* a, double sr) = 0;
  virtual size_t bytes() const = 0;
};

class LivePool {
 public:
  static constexpr int kTimeline = 256, kAutoRate = 128, kAutoGP = 32, kQuantum = 128;
  explicit LivePool(double sampleRate);
  ~LivePool();
  LivePool(const LivePool&) = delete;
  LivePool& operator=(const LivePool&) = delete;

  // Control thread. Makes (or replaces) the pad's nodes. false: the kind is not built yet or an allocation failed (std::bad_alloc propagates where exceptions are on).
  bool addPad(int pad, const LivePadConfig& cfg);
  void removePad(int pad);
  void setTrace(std::vector<LiveTraceRec>* v) { trace_ = v; }
  bool has(int pad) const { return pad >= 0 && pad < kLivePads && pads_[pad].kind != LiveKind::None; }
  size_t padBytes(int pad) const { return pad >= 0 && pad < kLivePads ? pads_[pad].bytes : 0; }

  // The quantum's start (seconds): `now` in the JS (currentTime). Call before this quantum's events.
  void setTime(double t0) { now_ = t0; }
  // A note event on a live pad (the JS's liveNote / liveStart, the mono branch). true: a node took it; false: none for the track (counted noNode).
  // `bpm`: the sequencer's tempo now (P4: a bass node's bpm lane is kept current from it at each note); `arp`: Seq::arp(e), valid only for this call.
  // `choke`: the pad's kit choke group (the voice keeps it). `*poly` (if given) is set when the note went the polyVox way: such a note takes nothing over and chokes nothing (the JS's polyVox branch has no takeover); every other live note is the MONO branch
  // (it takes the track over, whatever ev.poly says: P2) and the caller gives the track's sampler / SoundFont voices the same takeover.
  bool note(const SeqEvent& e, int pad, double bpm, const double* arp, int choke = 0, bool* poly = nullptr);
  // The track's ONE voice list (L6), the live part of it: the JS's take() for a note another player plays or none (sweep the voices that have let go, kill all of the track's live voices for a non-poly note, the choke group of the other tracks),
  // and the poly cap's view of it (P3: a live voice is in T, so a later sampler / SoundFont poly note can shift it out; a live note has no cap loop of its own).
  void take(const SeqEvent& e, int choke);
  int liveOnTrack(int track, double t) const;                 // the track's live voices still in T at t (core's live1)
  bool oldestOnTrack(int track, uint64_t& serial) const;      // the oldest of them (by serial)
  void releaseOldest(int track, double t);                    // T.shift(): its gate goes low at t, it leaves T
  void stamp(uint64_t serial) { nextSerial_ = serial; }       // the serial the next live note's voice gets (one counter for all pools)
  int active() const;                                         // C4: live voices sounding: from the note-on until the gate is off and the node has been quiet (< 1e-5) for 4096 samples, or 4 s since the off
  void releaseHeld(double at);                                // C4: the song is over: every live voice still held lets go at `at`
  // The events that reach a track's live voices (the JS's off / mod / stp handlers of ev(), the live branch of kill / modVoice): `off` (all of the track's voices, or the columns of e.cols), `mod`, `stp` (e.flag: every track),
  // SEV_TEMPO (every bass node's bpm), and stopAll(at) (every node's gate low at `at`, booked notes taken back, syllable counters and voxG reset: for a live host).
  void off(const SeqEvent& e);
  void mod(const SeqEvent& e, const double* arp);
  void stp(const SeqEvent& e);
  void tempo(double t, double bpm);
  void stopAll(double at);
  // For tests: the value of a node's rate lane at time t (seconds); NaN when there is no such node.
  double debugRateAt(int track, int pad, int j, double t) const;
  bool debugNode(int track, int pad, int j, LiveDebugNode* out) const;
  bool debugVoxG(int track, int pad, double* sylN, double* lastT, double* lastK) const;
  // Adds the nodes' samples for [frame0, frame0 + frames) into the mixer inputs; frames <= kQuantum (one quantum).
  void render(float* const* inL, float* const* inR, int frames, int64_t frame0);

  const LiveStats& stats() const { return st_; }
  uint32_t failedNodes() const;

 private:
  using TEv = LiveTEv;
  struct Node {
    int pad = 0, track = 0; LiveKind kind = LiveKind::None;
    std::unique_ptr<LiveEng> eng;
    TEv tl[kTimeline]; int ntl = 0;
    AutoN<kAutoRate> rate{0}; AutoN<kAutoGP> g{1}, pn{0};
    int j = 0;                          // 0: the mono node; 1 .. 4: a polyVox node
    double lastOn = -1; bool curLive = false, curStop = false; double stopAt = 0; int curCol = 0;
    double sylN = 0;                    // the mono node's syllable counter (the JS's L.sylN)
    double base = 1, vrate = 1;         // the note's rate and the rate a `mul` / bend left (v.base, v.rate)
    bool inT = false;                   // the voice is in its track's voice list (off / stp take it out; mod reaches it only there)
    bool used = false;                  // a note was ever booked on this node (stopAll acts on used nodes)
    double lastBpm = -1;                // the bass node's bpm lane as last set
    bool started = false;
    uint64_t serial = 0;                // the voice's age in the track's list (the poly cap)
    int choke = 0;                      // the voice's choke group (the kit's, at its note-on)
    int64_t stopT = 0;                  // the sample the gate went low at (C4)
    int64_t quiet = 0; bool done = false;   // C4: samples of output under 1e-5 since the off; the voice has ended (counts no more)
    Node() = default;
  };
  struct VoxG { double sylN = 0, lastT = -9, lastK = 0; };   // polyVox's syllable counter of a (track, pad): notes within 4 ms share one syllable
  struct PadSlot {
    LiveKind kind = LiveKind::None; std::vector<std::unique_ptr<Node>> nodes; Node* byTrack[kVoiceTracks] = {}; size_t bytes = 0;
    std::vector<Node*> poly;            // vox: kVoicePoly nodes a track (index track * kVoicePoly + j - 1), null where the mask has none
    std::vector<VoxG> vg;               // vox: per track
  };

  void rebuildActive();
  void startNote(Node& n, const SeqEvent& e, double bpm, const double* arp, double fk = -1, bool hasFk = false);
  void pitchMods(Node& n, double t, double base, const SeqEvent& e, const double* arp);
  void modVoice(Node& n, const SeqEvent& e, const double* arp);
  void bookCtl(Node& n, LiveEvType type, double t, double v);   // a Bpm / Wob event at the sample of time t
  bool polyVox(const SeqEvent& e, PadSlot& ps, double bpm, const double* arp);
  void kill(Node& n, double at);
  void chokeKill(int track, int ck, double t);
  int curChoke_ = 0;                 // the choke group of the note being started
  void cancelFrom(Node& n, int64_t T);
  void book(Node& n, const TEv& e);
  void runNode(Node& n, int64_t q0, int frames, float* const* inL, float* const* inR);
  void deliver(Node& n, uint32_t count, LiveEvent* out, uint32_t* nout);

  double sr_; double now_ = 0; uint64_t nextSerial_ = 0;
  PadSlot pads_[kLivePads];
  std::vector<Node*> active_;
  std::vector<Node*> byTrack_[kVoiceTracks];   // every node of a track, pad order (the track's live voices)
  std::unique_ptr<float[]> scr_;     // two scratch blocks of kQuantum
  LiveStats st_; std::vector<LiveTraceRec>* trace_ = nullptr;
  void rec(const Node& n, char what, double t, double v, int64_t T, bool leg = false, bool acc = false, bool same = false) { if (trace_) { LiveTraceRec r; r.track = n.track; r.pad = n.pad; r.j = n.j; r.what = what; r.t = t; r.v = v; r.T = T; r.leg = leg; r.acc = acc; r.same = same; trace_->push_back(r); } }
};

}  // namespace thunder
#endif
