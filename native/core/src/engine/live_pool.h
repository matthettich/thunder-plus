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
  bool has(int pad) const { return pad >= 0 && pad < kLivePads && pads_[pad].kind != LiveKind::None; }
  size_t padBytes(int pad) const { return pad >= 0 && pad < kLivePads ? pads_[pad].bytes : 0; }

  // The quantum's start (seconds): `now` in the JS (currentTime). Call before this quantum's events.
  void setTime(double t0) { now_ = t0; }
  // A note event on a live pad (the JS's liveNote / liveStart, the mono branch). true: a node took it; false: none for the track (counted noNode).
  bool note(const SeqEvent& e, int pad);
  // Adds the nodes' samples for [frame0, frame0 + frames) into the mixer inputs; frames <= kQuantum (one quantum).
  void render(float* const* inL, float* const* inR, int frames, int64_t frame0);

  const LiveStats& stats() const { return st_; }
  uint32_t failedNodes() const;

 private:
  struct TEv { int64_t T = 0; LiveEvType type = LiveEvType::NoteOn; bool leg = false, acc = false, same = false; double v = 0; };
  struct Node {
    int pad = 0, track = 0; LiveKind kind = LiveKind::None;
    std::unique_ptr<LiveEng> eng;
    TEv tl[kTimeline]; int ntl = 0;
    AutoN<kAutoRate> rate{1}; AutoN<kAutoGP> g{1}, pn{0};
    double lastOn = -1; bool curLive = false, curStop = false; double stopAt = 0;
    bool started = false;
    Node() = default;
  };
  struct PadSlot { LiveKind kind = LiveKind::None; std::vector<std::unique_ptr<Node>> nodes; Node* byTrack[kVoiceTracks] = {}; size_t bytes = 0; };

  void rebuildActive();
  void startNote(Node& n, const SeqEvent& e);
  void kill(Node& n, double at);
  void cancelFrom(Node& n, int64_t T);
  void book(Node& n, const TEv& e);
  void runNode(Node& n, int64_t q0, int frames, float* const* inL, float* const* inR);
  void deliver(Node& n, uint32_t count, LiveEvent* out, uint32_t* nout);

  double sr_; double now_ = 0;
  PadSlot pads_[kLivePads];
  std::vector<Node*> active_;
  std::unique_ptr<float[]> scr_;     // two scratch blocks of kQuantum
  LiveStats st_;
};

}  // namespace thunder
#endif
