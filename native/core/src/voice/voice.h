// THE SAMPLER VOICE PLAYER (N1, unit U08): the core's player of rendered pads, ported from core/core-processor.js (also at the end
// of core/seq-core.js, build 616): `Rd`, `stBal`, startNote / take / kill / relTc / live1 / pitchMods / modVoice, the `off`, `mod` and
// `stp` handlers of ev(), stopAll, and process1's per-voice loop with its fast path (constBlk). THE JS IS THE SPECIFICATION: every
// statement is ported in the same arithmetic order (doubles throughout; the outputs and the pads are float32 as the JS's Float32Array
// are, so a sum is rounded to float32 where the JS rounds it), quirks included. Suspected bugs are listed in the unit's report, not fixed.
// The goldens: native/tools/voice-dump.mjs (the JS in a Node `vm`) and ctest `voice` (tests/test_voice.cpp).
//
// WHAT IS NOT HERE (marked STUB in voice.cpp where the JS touches it): SoundFont voices (sfNote, sfRun, zones), MUT / GEN variation
// buffers (varBufs, varRec: the host hands in the buffer to play), live engines (liveNote / liveStart, `v.live`), the mixer lanes
// and outputs (U09), stretched SYNC notes (`ev.st`), the WATCHDOG, the beat clock.
//
// REAL-TIME RULES. The constructor allocates everything (the voices, their automation lists, the shaping rings of CUT / RES / DEC
// notes); the entry points and render() allocate nothing, take no lock and do no I/O. A pad is a pointer + length + sample rate owned
// elsewhere: it must stay valid until the voices that play it have ended (the host keeps a buffer until its voices are gone).
// Counters (never silently lost): dropped() notes refused because every voice was busy (note() then returns false, so the host can let
// the page play it), tOverflow() notes a track's list could not hold, autoDropped() automation events refused by a full list.
#ifndef THUNDER_VOICE_VOICE_H
#define THUNDER_VOICE_VOICE_H
#include <cmath>
#include <cstdint>
#include <memory>

#include "rt/auto.h"
#include "seq/seq.h"

namespace thunder {

constexpr int kVoiceTracks = kSeqTracks;   // outputs 0..31 and the master column's (stp reaches it; the JS's `k <= NT`)
constexpr int kVoicePoly = 4;              // POLY: a poly track holds at most four voices; the oldest lets go
constexpr int kVoiceTrackCap = 8;          // per-track list (never more than kVoicePoly + 1 in the JS's flow)
constexpr int kRdRing = 16384;             // RD_RING

// DEFERRED FREE OF A PAD'S DATA (U37 P1, render/pads_snap.h). A voice that took a pad's data from a PadsSnap records which buffer it took: `epoch` (the buffer's own number, > 0; 0 = not tracked: the
// by-reference pads of SongRenderer / the C API, whose owner outlives the renderer) and `slot` (0..kEpochSlots-1: the sampler's audio of pad p is slot p, the SoundFont data of pad p slot 256 + p).
// The pools report the smallest epoch per slot over their voices (epochMins) so the control side knows what it may free. Nothing here changes a sample of any render.
constexpr int kEpochSlots = 512;
constexpr uint64_t kEpochNone = ~(uint64_t)0;   // "no voice uses this slot"

// A rendered pad as the sampler reads it (the JS's coreBufs entry {data, data2, sr}, or a variation buffer).
struct VoiceBuf {
  const float* data = nullptr;
  const float* data2 = nullptr;   // the right channel of a stereo pad (same length), or null
  int len = 0;                    // data.length
  double sr = 0;                  // the pad's sample rate
  bool noShape = false;           // the host's varRec returned "the pad as it is" (a sliced / synced instrument): the event's tw is not applied
  uint64_t epoch = 0; int slot = -1;   // U37 P1: which PadBuf this is (see kEpochSlots); the voice records them. 0 / -1: not tracked
};
// The vibrato curve of pitchMods (an automation Curve): base * 2 ** (st * sin(2 pi hz d min(1, max(0, (x - t0) / dd))) / 12), p = {base, st, hz, d, t0, dd}. Shared with the live pool (LIVE-ROUTE B4).
double voice_vib_curve(const double* p, double x);

// kv[pad]: what the sampler reads of the page's kit message ({choke, syn, rel}; live / sf pads are not this player's)
struct VoiceKit { int choke = 0; bool syn = false; double rel = 0; };

class VoicePool {
 public:
  explicit VoicePool(double sampleRate, int maxVoices = 64);   // allocates (control thread)
  ~VoicePool();
  VoicePool(const VoicePool&) = delete;
  VoicePool& operator=(const VoicePool&) = delete;

  // currentTime: the start of the block being processed (render() sets it too; call before this block's events, as process1 steps the
  // sequencer before it sounds the voices)
  void setTime(double t) { now_ = t; }

  // The events (the JS's ev() for the ones that touch the sampler). `arp`: the sequencer's pairs for e (Seq::arp(e)), or null.
  // note(): startNote for a note the core plays. false: not started (no free voice: dropped() counts it; or a bad track).
  // Use wantsShaping() first to know the page must play a reversed + shaped note (varRec returns null for it).
  bool note(const SeqEvent& e, const double* arp, const VoiceBuf& buf, const VoiceKit& kit, double bpm);
  static bool shapedReverse(const SeqEvent& e, const VoiceBuf& buf) { return (e.has & SH_TW) && e.rev && !buf.noShape; }   // varRec: null (the page plays it)
  void take(const SeqEvent& e, const VoiceKit* kit);   // a note the page plays: this track's voices still give way (mono takeover, chokes)
  void off(const SeqEvent& e);                          // type 'off'
  void mod(const SeqEvent& e, const double* arp);       // type 'mod'
  void stp(const SeqEvent& e);                          // type 'stp'
  void stopAll(double at);                              // stopAll(at)
  // RESET TRACK (web #251, the core's message {op:'stp', track}, seq-core.js:1229): ONE track's voices let go as STOP lets them go: at = max(currentTime, t); for 0 <= track < NT every voice of the track gets
  // kill(v, at, 0.004, stop = TRUE) (a voice booked to start at or after `at` never sounds) and the list is cleared. Unlike stp() (the sequencer's STP command: stop = false, the master column too).
  void stopTrack(int track, double t);

  // THE POLY CAP ACROSS TWO POOLS (verify-242 F3). The JS keeps ONE voice list per track for the sampler's and the SoundFont's voices (core/core-processor.js :442-443 and :582-583:
  // `while (T.length >= POLY) kill(T.shift())`), so the cap of four is shared. A renderer that keeps a second pool (sf::SfPool, same four calls) does the cap itself, oldest first
  // across both, before it calls note() (whose own cap then finds room): stamp() gives the next voice its serial (the renderer's counter, so serials compare across pools),
  // liveOnTrack() drops this track's ended voices as note() would and returns how many are left, oldestOnTrack() the serial of the oldest, releaseOldest() lets it go (T.shift() + kill 0.006).
  void stamp(uint64_t serial) { stamp_ = serial; }
  int liveOnTrack(int track, double t);
  bool oldestOnTrack(int track, uint64_t& serial) const;
  void releaseOldest(int track, double t);
  static bool playable(const SeqEvent& e, const VoiceBuf& c) { return e.rate > 0 && std::isfinite(e.rate) && std::isfinite(e.t) && std::isfinite(e.gain) && c.sr > 0 && std::isfinite(c.sr) && c.len >= 2; }   // note() refuses what this refuses

  // One block: adds every live voice into its track's output. outL[track] / outR[track]: that track's two channels (null outL: the
  // track has no output, its voices are left as they are, as the JS's `continue`; null outR: mono, both sums go to outL).
  void render(float* const* outL, float* const* outR, int frames, double t0);
  void render(float* L, float* R, int frames, double t0);   // every track onto one pair (a convenience for tests and a mixer-less host)

  // U09, for native/tools/render-compare.sh's reading of a difference ONLY (off by default: the core's own behaviour, U08's goldens).
  // On: a voice is timed as the PAGE player's AudioBufferSourceNode is in a Web Audio render, where the web app's two players differ:
  //   1. the start is ceil(t sr - 1e-6) / sr (index.html startNote's st0, build 608); the core starts on the first sample at or after t,
  //      so a note a hair above a sample (60.00000000000129 s) starts one sample later in the core;
  //   2. playbackRate is a k-rate param: read once per 128-frame render quantum (a slide or bend moves in steps). A slide's ramp is the
  //      first thing ever booked on its param (startNote sets no .value on a slide): Chrome then starts the ramp at the first quantum
  //      boundary at or after the note's time, and until that boundary the note plays at the node's own rate, 1. A bend's ramp (after
  //      `.value = rate`) starts at the note's time. Both measured against Chrome 153 on ELECTRO's 6 slides and 3 bends. The core reads
  //      its rate lane per sample, from the note's time. (Arpeggios and vibrato are not timed the page's way here: no test song has them.)
  void setPageTiming(bool on) { pageTiming_ = on; }

  // U37 P1. For every voice that is alive (sounding OR booked to start later: both are in the alive list from note() on) and was started on a tracked buffer: mins[slot] = min(mins[slot], its epoch).
  // `mins` has kEpochSlots entries, set to kEpochNone by the caller. Audio thread, once a quantum; allocates nothing.
  void epochMins(uint64_t* mins) const { for (int ai = 0; ai < nAlive_; ai++) { const Voice& v = V_[(size_t)alive_[ai]]; if (v.eslot >= 0 && v.eslot < kEpochSlots && v.ep < mins[v.eslot]) mins[v.eslot] = v.ep; } }

  int active() const { return nAlive_; }
  int capacity() const { return cap_; }
  uint32_t dropped() const { return dropped_; }
  uint32_t tOverflow() const { return tOver_; }
  uint32_t refused() const { return refused_; }   // notes note() refused: a rate / time / gain not finite, a bad pad (N1 review A1)
  uint32_t autoDropped() const;
  int peakVoices() const { return peakV_; }
  int peakEvents() const { return peakEv_; }   // the longest automation list seen (r, g, f or pn) after an event; size kAutoR / kAutoGFP from it

  static constexpr int kAutoR = 128;     // the rate list: an arpeggio books up to 96 pairs + its end point + bends and mods
  static constexpr int kAutoGFP = 32;    // gain, fade, pan

 private:
  struct Rd {
    const float *src = nullptr, *src2 = nullptr; bool rev = false, tw = false, f = false; int len = 0, N = 0, hi = 0, lo = 0;
    double tau = 0, a1 = 0, a2 = 0, a3 = 0, l1 = 0, l2 = 0, kd = 1, e = 1; float* ring = nullptr;
    void init(const float* s, const float* s2, int length, double sr, bool hasTw, bool hasCut, double cut, double res, bool hasDec, double dec, bool reverse, double start);
    double raw(int c, int i) const { const float* D = c ? src2 : src; if (!D || i < 0 || i >= len) return 0; return D[rev ? len - 1 - i : i]; }
    double at(int c, int i);
  };
  struct Trm { double hz = 0, d = 0, t = 0; };
  struct Voice {
    AutoN<kAutoR> r{0}; AutoN<kAutoGFP> g{0}, f{1}, pn{0};
    const float *data = nullptr, *data2 = nullptr; int len = 0; double sr = 0;
    bool hasTrm = false, hasApn = false; Trm trm, apn;
    double base = 0, rate = 0; int choke = 0, col = 0, track = 0; uint64_t ser = 0;
    bool ended = false, hasStop = false; double stopAt = 0;
    bool syn = false; double rel = 0, end = 0, pos = 0, t0 = 0;
    bool hasRd = false; Rd rd;
    uint64_t ep = 0; int eslot = -1;   // the buffer this voice started on (VoiceBuf::epoch / slot), for epochMins
  };

  double relTc(const Voice& v) const { return v.syn ? v.rel / 4.6 : 0.004; }
  bool live1(const Voice& v, double at) const { return !v.ended && !(v.hasStop && v.stopAt <= at); }
  void kill(int vi, double at, double tc = 0.004, bool stop = false);
  void pitchMods(Voice& v, double t, double base, const SeqEvent& e, const double* arp);
  void modVoice(int vi, const SeqEvent& e, const double* arp);
  void tErase(int track, int k);
  void tRemove(int vi);
  void aliveErase(int ai);
  void track(const Voice& v);
  void run(Voice& v, float* L, float* R, int N, double tb0, double tb1);

  double sr_;
  bool pageTiming_ = false;
  int cap_;
  double now_ = 0;
  std::unique_ptr<Voice[]> V_;
  std::unique_ptr<float[]> rings_;
  std::unique_ptr<int[]> alive_, free_;
  int nAlive_ = 0, nFree_ = 0;
  int T_[kVoiceTracks][kVoiceTrackCap]; int nT_[kVoiceTracks];
  uint32_t dropped_ = 0, tOver_ = 0, refused_ = 0; int peakV_ = 0, peakEv_ = 0; uint64_t stamp_ = 0;
};

}  // namespace thunder
#endif
