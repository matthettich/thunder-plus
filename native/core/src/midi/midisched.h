// U35: MIDI OUT SCHEDULING. index.html's "MIDI out and MIDI clock" section (msend, moEv, moNote, moPlay, moMod, moCmd, moRamp, moEcho, moClock,
// moLfo, moStart, moRelease, moStop, clkJoin, panic) as one C++ object, with the owner's fixes of 19 Nov (docs/port/U35-NOTES.md "Rulings"):
// B1 silent note ends the mono note, B2 forward-integrated clock, B3 Gear delay on the transport, B4 per-track ownership + per-pitch count,
// B5/B6 STOP clears the CC and used-channel tables, B7 late ticks / late notes dropped and counted, B8 Stop at the stop time, B9 no off for an
// unsent on. B10 (no RPN, no bank select) is kept as the web has it.
//
// SHAPE. The host (the live core) calls, on the AUDIO THREAD only: onEvent / onStep for every sequencer event, book() once per block; start() / stop() /
// release() / panic() / cableChanged() / portToggled() / live*() / setters between blocks. NOTHING here allocates, locks or does I/O after the
// constructor (fixed arrays; counted drops when one is full). The output is a stream of MidiMsg records pushed on an SPSC ring that another thread
// drains (drain()); a message's `frame` is the core's audio frame counter at the engine rate, EXCLUDING the device's output latency (the shell adds
// it) and INCLUDING the core's own FX latency (setFxLatency, read from the mixer each block, no cache) minus the port's Gear delay.
// A MidiMsg with MIDI_F_CLEAR is a STOP marker: the shell drops everything it still has queued for that port whose frame is after the marker's.
//
// THE OUTBOX. A message more than `lead` seconds ahead of the block's `now` waits in a time-sorted array (stable for equal times) and is released
// when it is within `lead`: the web's MOB rule (60 ms). Messages are given their per-pitch ownership in TIME order as they are released.
// STOP drops what waits. Lateness (B7): a note the host reports `late` (see seq_note_late, shared with Players) and a clock tick more than
// kClockLateSec behind `now` are dropped and counted, never sent in a burst.
#ifndef THUNDER_MIDI_MIDISCHED_H
#define THUNDER_MIDI_MIDISCHED_H
#include <cmath>
#include <cstdint>
#include <cstring>

#include "rt/spsc.h"
#include "seq/seq.h"
#include "song/song.h"

namespace thunder {

constexpr int kMidiPorts = 4;                 // A..D
constexpr int kMidiTracks = 34;               // the 32 tracks, the master column (never MIDI), and index 32+1 for live keys
constexpr int kMidiKeysTrack = 33;            // live keys (the page's track NT, which no song track uses)
constexpr int kMidiHeldCap = 128;             // held notes per track (an ARP can book 96 steps); counted when exceeded
constexpr int kMidiPendCap = 4096;            // the outbox
constexpr int kMidiRingCap = 8192;            // output records
constexpr int kMidiCmdCap = 256;              // commands from other threads
constexpr double kNoteLateSec = 0.15;         // index.html:23827: a note more than this behind `now` is dropped (pad and MIDI)
constexpr double kClockLateSec = 0.020;       // a clock tick more than this behind `now` is dropped (B7)
constexpr double kClockAheadSec = 0.080;      // ticks, LFO and glides are booked this far ahead (moClock, moLfo, moRamp)
constexpr double kEchoAheadSec = 0.100;       // moEcho
constexpr double kDefaultLeadSec = 0.060;     // MOB_AHEAD
constexpr double kLfoDt = 0.01;               // LFO_DT

// THE ONE LATENESS TEST (B7). Players drops a pad note for which this is true; MidiSched drops the MIDI note of the same event.
inline bool seq_note_late(double eventT, double now) { return eventT < now - kNoteLateSec; }

enum MidiKind : uint8_t { MK_EV = 0, MK_CLOCK, MK_LFO, MK_RAMP, MK_ECHO, MK_RELEASE, MK_STOP, MK_START, MK_JOIN, MK_PANIC, MK_LIVE, MK_OTHER, MK_COUNT };
enum : uint8_t { MIDI_F_CLEAR = 1 };

struct MidiMsg {                 // 16 bytes
  int64_t frame;                 // the core's audio frame the message is for (see above)
  uint8_t port, len, flags, kind;
  uint8_t b[3];
  uint8_t cable;                 // the port's cable id (setPort) when the message was made: the shell's CLEAR works per cable, and a cable change must not re-route queued messages
};
static_assert(sizeof(MidiMsg) == 16, "MidiMsg is 16 bytes");

struct MidiStats {               // all counted since construction; read with stats()
  uint64_t sent = 0, ringFull = 0, pendFull = 0, heldFull = 0, echoFull = 0, glideFull = 0, pendNoteFull = 0, rampKeyFull = 0;
  uint64_t lateTicks = 0, lateNotes = 0, lateMsgs = 0;           // B7
  uint64_t collisions = 0;                                      // B4: a note-on for a pitch another TRACK holds on that port + channel (a shell can warn)
  uint64_t sameTrackRetrig = 0, offsSuppressed = 0, onsSilent = 0;   // an on onto a pitch the same track holds; offs not sent (another holder / never on); B1 notes
  uint64_t ticks = 0, starts = 0, stops = 0, badEvents = 0, badArgs = 0, cmdFull = 0;
  uint64_t maxPend = 0;
};

class MidiSched {
 public:
  explicit MidiSched(double sampleRate);   // control thread: allocates everything
  ~MidiSched();
  MidiSched(const MidiSched&) = delete;
  MidiSched& operator=(const MidiSched&) = delete;

  // ---- configuration (audio thread, between blocks; the C API reaches them through its command ring) ----------------------------
  void setSong(const Song* s) { S_ = s; }                       // the instruments' MIDI fields; must outlive its use
  void setPort(int p, bool connected, bool clockOn, double gearMs, int cable);   // gear delay clamped -50..200 ms; `cable` identifies the physical cable (equal = one clock stream)
  void setLead(double sec);                                      // outbox look-ahead (0.005..0.5), default 0.060
  void setFxLatency(double sec);                                 // mastering + master pump FX + safety limiter + PDC, read each block (no cache); 0..2 s
  void setAudible(uint64_t mask) { audible_ = mask; }            // bit t set = track t is heard (mix mute / solo / bus rule is the mixer's: mix_audible); tracks >= 32 always
  void setSampleRate(double sr);

  // ---- transport (audio thread) ---------------------------------------------------------------------------------------------------
  void start(int spp /* Song Position Pointer in 16ths, 0 = from the top */, double now);   // moStart
  // The sequencer's first beat event sends Start (or SPP + Continue); the rest is driven by onStep / book.
  void stop(double now);                                          // moStop (STOP, song end): release + clock Stop
  void release(double now, bool playing);                         // moRelease (playing: OUTPUT / PORT / CH edit while the song runs: no clear, the outbox stays)
  void panic(double now);                                         // F8: release + CC 64, 120, 123 on every channel of every connected port
  void cableChanged(int p, bool connected, int cable, double now);   // a port's cable changed while playing (the page's `change` handler)
  void portToggled(int p, bool clockOn, double now);              // a port's clock box ticked / unticked (clkPortToggled)

  // ---- events (audio thread) -----------------------------------------------------------------------------------------------------
  // One sequencer event. `late`: the host's lateness verdict for a note (seq_note_late(e.t, now)); `bpm` the sequencer's tempo (ECHO's spacing);
  // `arp` = seq.arp(e) for an event with SH_ARP. BEAT events must come from onStep (they carry the tempo map).
  void onEvent(const SeqEvent& e, const double* arp, double bpm, bool late, double now);
  // One played step: every event, then the ARP returns due by the step, with the tempo-map anchor (the beat event's t and b, the tempo after the step).
  void onStep(const SeqEvent* evs, int n, const SeqStep& st, const Seq& seq, double now);
  // Once per block, after the step loop: the clock, the LFOs, the CSL glides, the echoes, the outbox. seqTime = the next step's time; bpm = the sequencer's.
  void book(double now, double seqTime, double bpm, bool seqAlive);

  // ---- live ---------------------------------------------------------------------------------------------------------------------
  // A held key on a MIDI instrument: the page's preview() (note = the tracker note, C-4 = 48; vol 0..255, < 0 = full). Out at now + Gear delay + 1 ms.
  int liveNoteOn(int ii, int note, int vol, int cap, double now);   // returns a handle (>= 0) or -1
  void liveNoteOff(int handle, double now);
  void liveRaw(int port, const uint8_t* b, int len, double extraSec, double now);   // thru: kbCC / kbPass / test (CC 64 is remembered for the stop)

  // ---- output ---------------------------------------------------------------------------------------------------------------------
  // Consumer thread. Returns how many records were written.
  int64_t drain(MidiMsg* out, int64_t cap);
  const MidiStats& stats() const { return st_; }

  // ---- introspection for tests ------------------------------------------------------------------------------------------------------
  int heldCount(int track) const { return track >= 0 && track < kMidiTracks ? nHeld_[track] : 0; }
  int pendCount() const { return nPend_; }
  int pitchCount(int p, int ch, int n) const { return cnt_[(p * 16 + ch) * 128 + n]; }
  bool clockOn() const { return clk_.on; }

 private:
  struct Held { uint8_t p, ch, n, base, vel, rng; double fine, t, end; int32_t col; uint32_t nid; };
  struct PendRet { uint8_t p, ch, n, base, vel, rng; double fine, t, end; int32_t col; uint32_t nid; };   // an ARP's return
  struct PendMsg { double at; uint32_t ord; uint8_t port, len, kind, track; uint8_t b[3]; uint8_t cable; };
  struct EchoN { double t, off, len; uint8_t p, ch, n, vel; bool on; };
  struct Glide { int32_t key; uint8_t p, ch, cc; double a, b, t0, t1, at; int last; bool cut; };
  struct Lfo { int v = -1; double t = 0; bool hasT = false; double trig[64]; int nTrig = 0; };
  struct Anchor { double t, b, bpm; };
  struct Bend { bool has; int v; double t; };
  struct Seg { double t; int d; };

  // port state
  bool conn_[kMidiPorts] = {false, false, false, false}, clkOn_[kMidiPorts] = {false, false, false, false};
  double gear_[kMidiPorts] = {0, 0, 0, 0};
  int cable_[kMidiPorts] = {-1, -2, -3, -4};
  double sr_, lead_ = kDefaultLeadSec, fxLat_ = 0, now_ = 0;
  const Song* S_ = nullptr;
  uint64_t audible_ = ~uint64_t(0);
  MidiStats st_;

  // the output
  Spsc<MidiMsg, kMidiRingCap> ring_;
  PendMsg* pend_; int nPend_ = 0; uint32_t ord_ = 0;
  // ownership (B4)
  uint8_t* cnt_;              // [port][ch][pitch] holders of a sounding note
  int8_t* owner_;             // [port][ch][pitch] the last track that sent a note-on (-1 none)
  int64_t* onFrame_;          // frame of the last note-on sent
  // the page's `mx`
  Held* held_;                // [track][kMidiHeldCap]
  int nHeld_[kMidiTracks] = {0};
  PendRet* pendRet_;          // [track][8]
  int nPendRet_[kMidiTracks] = {0};
  uint64_t used_ = 0;         // port * 16 + channel bits
  Bend bend_[kMidiPorts * 16];
  int16_t* ccv_;              // [port][ch][cc] last value sent, -1 none
  Glide glide_[64]; int nGlide_ = 0;
  EchoN echo_[512]; int nEcho_ = 0;
  struct RampKey { int64_t k; }; RampKey rampKeys_[256]; int nRampKeys_ = 0;
  Lfo lfo_[kNI];
  uint32_t nidN_ = 0;
  // tempo map (the beat events) and the clock (B2)
  Anchor anc_[64]; int nAnc_ = 0;     // oldest first
  bool haveT0_ = false; double t0_ = 0; int spp_ = 0; bool playing_ = false;
  struct Clk { bool on = false; int64_t k = 0; bool hasLast = false; double last = 0; } clk_;
  int64_t lastTickFrame_[kMidiPorts][8]; int lastTickN_[kMidiPorts] = {0, 0, 0, 0};
  struct SusTo { bool on = false; uint8_t p = 0, ch = 0; } sus_;
  struct KeyH { bool used = false; int ii = 0, note = 0; uint32_t nid = 0; int port = 0; } keys_[32];
  int32_t keyCol_ = 1000;
  // command ring (not used by the C++ API; the C API fills it)
  friend struct MidiSchedAccess;

  // helpers (the page's functions)
  double latSec(int p) const { return gear_[p] * 0.001; }
  int64_t frameOf(double sec) const { return (int64_t)std::llround(sec * sr_); }
  void send(const uint8_t* b, int len, double t, bool hasT, int port, uint8_t kind, int track);
  void emit(const PendMsg& m);
  void push(int port, const uint8_t* b, int len, int64_t frame, uint8_t kind, uint8_t flags, int cable = -1000);
  void flush(double now);
  void pendInsert(const PendMsg& m);
  void clkPorts(int* ps, int& n) const;
  void sendClk(const uint8_t* b, int len, double t, uint8_t kind);
  bool audibleT(int t) const { return t >= 32 || (t >= 0 && ((audible_ >> t) & 1)); }

  void offOne(Held& h, double at);
  void moOff(int t, double at);
  void moOffCols(int t, uint32_t cols, double at);
  void offNote(int t, uint32_t nid, double at);
  uint32_t moPlay(int t, int p, int ch, int n, int vel, int rng, double fine, const Seg* segs, int nSegs, double end, uint32_t nid, bool hasNid, int32_t col);
  uint32_t moNote(const SeqEvent& e, const SongInst& I, const double* arp, double bpm, int cap);
  void moMod(const SeqEvent& e, const double* arp);
  void moCmd(const SeqEvent& e);
  void pendAt(int t, double at);
  void pendFlush(int t, double at);
  void pendPlay(int t, const PendRet& x);
  void bendTo(int p, int ch, double st, int rng, double t, bool hasT);
  double bendNow(int p, int ch, int rng) const;
  bool rampOnce(int p, int ch, double t0);
  void bendRamp(int p, int ch, double a, double b, int rng, double t0, double t1);
  void bendVib(int p, int ch, double c, double spd, double dep, int rng, double t0, double t1);
  void moRamp(double now);
  void moEcho(double now);
  void moClock(double now, double seqTime, double bpm);
  void moLfo(double now, double seqTime, double bpm);
  void lfoTrig(int ii, double t);
  void addAnchor(double t, double b, double bpm);
  double beatAt(double t, double bpm) const;
  double timeOfBeat(double beat, double seqTime, double bpm, bool& ok) const;
  void joinPort(int p, bool all, double now);
  void releaseAll(double now, bool playing, bool stopping = false);
  int clampPort(int p) const { return p < 0 ? 0 : p >= kMidiPorts ? kMidiPorts - 1 : p; }
};

}  // namespace thunder
#endif
