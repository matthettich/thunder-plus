// THE MIXER (N1, unit U09): the page's Web Audio strip (index.html makePlayer, "GAIN STAGING") with the core's mix lanes
// (core/core-processor.js mixEv / mixOut / mixFill) as one C++ object, and busPlan / chainsFeed / audible as plain functions.
// THE CODE IS THE SPECIFICATION (docs/GAIN-STAGING.md is older than the bus plan). Per track t of 0..NT (NT: the preview bus, every level 1):
//
//   voices -> inp (TRIM) -> [chains hook: dry 0 / wet 1] -> [track FX hook] -> ins -> g (VOL x audible) -> cg (TVL / TVA) -> p (pan) -> dl -> master sum
//                                                                               ins -> pre (audible ? 1 : 0) -> a PRE send's bus
//                                                                                                              p -> a POST send's bus
//   (U10, LATENCY COMPENSATION: three DelayNodes read the pan's output p and the `pre` pair: dl on the way to the master sum, fdl on p's feed
//    into the buses it feeds, fdlP on `pre`'s. Their delay is pdcPlan's d and f, all 0 until setDelays() says otherwise; see mix/pdc.h.)
//   master sum -> out (MASTER VOL + MVOL) -> [master FX hook: effects, mastering, limiter] -> mo (OUT + MOUT) -> the output
//
// Every node's arithmetic is Chrome's, in float32 as its nodes are: a gain is sample x gain (both float32); the StereoPanner's
// equal-power laws (mono input and stereo input differ, see mix.cpp); a summing point adds in track order; a param with something
// plugged into it is its own value + the plugged signal (the pan's sum clamped to -1..1). Every level is an `Auto` lane read per
// sample (a constant block takes the fast path, same values). The reference: native/tools/mix-dump.mjs (a JS model of this strip,
// checked against real Web Audio by native/tools/mix-webaudio-check.mjs), ctest `mix` and `busplan`.
//
// TWO KINDS OF LANE, as in the web app with the audio-thread sequencer:
//   - the mixer's own (inp, g, pre, pan, out, mo): moved by mix.set, the page's law (applyMix, live: an approach with a 12 ms time
//     constant; MASTER VOL jumps, as rtRoute's `.value =` does). mute / solo re-aim g and pre of every track whose audible() changed.
//   - the song's (mg x, mp +, mm +, mo +): moved by the sequencer's events with mixEv's laws (4 ms), back to nothing at songLanesReset().
//
// BUSES. busPlan (a pure function, below) says which tracks feed which bus track from the song's chains patch. A bus EXISTS only
// while its track runs through chains (the page's busSync: `b.chn`): here, while the bus track has a chain hook. Its sources are
// handed to that hook (PRE sources from `pre`, the others from after their pan), and the pure ones leave the master sum. With no
// chain host (until the chains unit lands) no bus exists and every track plays on its own, exactly as the page with CHN off.
//
// REAL-TIME RULES. The constructor allocates; event(), set(), setPlan(), setChain / setFx / setMasterFx, songLanesReset() and
// render() allocate nothing, take no lock and do no I/O (call them on the audio thread, between blocks). bus_plan() and
// chains_feed() read JSON and allocate: control thread.
#ifndef THUNDER_MIX_MIX_H
#define THUNDER_MIX_MIX_H
#include <cstdint>
#include <memory>

#include "mix/beat.h"
#include "mix/delay.h"
#include "rt/auto.h"
#include "rt/json.h"
#include "seq/seq.h"
#include "song/song.h"

namespace thunder {

constexpr int kMixTracks = kSongNT + 1;   // 0..NT-1 and the preview bus (the voices' outputs: kVoiceTracks)
constexpr int kMixFeedMax = 32;           // busSync: src.slice(0, 32)

// ---- the gain laws (index.html / core/seq-core.js, with js_pow for Math.pow) ----------------------------------------------------
double mix_curve(double v);        // curve(): (clamp(v, 0, 255) / 255) ^ 2: master VOL and OUT (FF = 0 dB)
double mix_track_gain(double v);   // trackGain(): E0 = 0 dB, below it (v / 224) ^ 2, above it up to +6 dB at FF in even dB
double mix_trim_gain(double v);    // trimGain(): 80 = 0 dB, 00 = -18 dB, FF = +18 dB in even dB (v rounded)
double mix_pan(double v);          // clamp((v - 128) / 127, -1, 1): 00 left, 80 centre, FF right

// ---- busPlan ----------------------------------------------------------------------------------------------------------------------
// The JS's {feeds, targets, pure, pre, loop}. feeds[b]: the sources of bus b, ascending, as the JS list has them (a track with two
// Track Ins that both reach the bus is in the list twice, as in the JS: ported as is); feedMask / targets / pure / pre: bit t = track t.
struct BusPlan {
  uint8_t feeds[kSongNT][kMixFeedMax];   // the first kMixFeedMax of each list (what busSync wires)
  int nFeeds[kSongNT];                   // how many of them (the JS list's length, capped at kMixFeedMax)
  int feedLen[kSongNT];                  // the JS list's full length
  uint32_t feedMask[kSongNT], targets[kSongNT];
  uint32_t pure = 0, pre = 0;
  bool loop = false;
  BusPlan();
};
uint32_t chains_tracks(Json chainPatch);                       // chainsTracks(): the tracks that have a Chain Out
uint32_t chains_feed(Json chainPatch, const bool* chainOn);    // chainsFeed(t) for every t: CHN on and a Chain Out
BusPlan bus_plan(Json chainPatch, const bool* chainOn);        // busPlan()
// audible(t) for every t: solo wins, then mute, then "nobody is soloed"; soloing a bus keeps its sources, soloing a source its bus
uint32_t mix_audible(const BusPlan& plan, uint32_t mute, uint32_t solo);

// ---- hooks (U14: the track effects host; the chains host; the master chain) -------------------------------------------------------
struct MixFeed { int track; bool pre; const float* L; const float* R; };
// In place on the track's pair. A chain hook also gets the bus's sources (nFeeds 0 when the track is not a bus).
typedef void (*MixInsertFn)(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds);

// mix.set (docs/port/CORE-BRIDGE.md section 3): {tr, k, i?, v, ramp?}
enum class MixKey : uint8_t { Vol, Trim, Pan, Mute, Solo, Send, Out };
struct MixSet {
  int tr = 0;             // 0..NT-1 a track; NT (kSongNT) the master: Vol is MASTER, Out is OUT
  MixKey k = MixKey::Vol;
  int i = 0;              // (Send: which send; the web app has no send level, see set())
  double v = 0;           // 00..FF; Mute / Solo: 0 or 1
  double ramp = -1;       // frames. < 0 (not given): the page's law; 0: at once; > 0: linear over that many frames
  double at = -1;         // seconds on the core's timeline; < 0 or in the past: now (the time given to setTime)
};

class Mixer {
 public:
  // maxDelaySec: the longest delay any delay point can hold (a ring each: 2 x 4 bytes x sr x maxDelaySec for the post side of every track and
  // the pre side again; the JS holds 0.99 s, which is 38 MB at 48 kHz). A longer plan is cut to it and counted (pdcClamped()).
  Mixer(double sampleRate, int maxBlock, double maxDelaySec = kPdcMaxSec);   // allocates (control thread)
  ~Mixer();
  Mixer(const Mixer&) = delete;
  Mixer& operator=(const Mixer&) = delete;

  // The song's mixer values, at once (a render's applyMix; a song load). The plan is not touched.
  void load(const Song& S);
  void reset();                                            // every level as a new song has it (vol E0, trim 80, pan 80, master / OUT FF)
  void setTrack(int t, double vol, double trim, double pan, bool mute, bool solo);   // one track's values, at once (call apply() after the last)
  void setMaster(double master, double mout);              // at once
  void apply();                                            // applyMix, live false: every lane to its value now, at once
  void setPlan(const BusPlan& plan);                       // busSync
  void setChain(int t, MixInsertFn fn, void* user);        // the track runs through chains (null: it does not)
  void setFx(int t, MixInsertFn fn, void* user);           // the track's effects (U14)
  void setMasterFx(MixInsertFn fn, void* user);            // between MASTER VOL and OUT: master effects, mastering, the limiter
  void setMono(int t, bool mono) { if (t >= 0 && t < kMixTracks) mono_[t] = mono; }   // a one-channel input (the panner's mono law): inL only is read. The sampler's voices are stereo.

  // U10: pdcSync's `set` on every delay point at time now (setTime): d[t] / f[t] seconds for t in 0..kMixTracks-1 (a PdcPlan's d and f; f is
  // both fdl and fdlP). glide (live): each point ramps linearly from its value now over `ramp` s, else it takes the value at once (a render).
  // idle: every delay is 0 (LATENCY COMP AUTO while no song plays). A delay already at the target is left alone. Allocates nothing.
  struct DelaySet { bool glide = false; bool idle = false; double ramp = 0.05; };
  void setDelays(const double* d, const double* f, const DelaySet& s);
  // The beat clock's readers (n._cd): the effects node of track t (a setFx hook) or of the master (kSongNT: setMasterFx) hears the clock through
  // a delay of c[t] seconds (a PdcPlan's c), set with the same rules. A reader is fed only while its node has a hook.
  void setClockDelays(const double* c, const DelaySet& s);
  double clockDelaySec(int t) const;    // n._cd._pv: the delay now in force in seconds (0 if none); the effect commands of FXO / FLX / GAT / RTG wait this long (index.html 22741)
  uint32_t pdcClamped() const;       // delay requests above the cap (cut to it)
  double delayNow(int t, int point) const;   // the target of a delay point in seconds (point 0 dl, 1 fdl, 2 fdlP); for tests
  double maxDelaySec() const { return capSec_; }

  void setTime(double now) { now_ = now; }                 // currentTime: the start of the block about to be rendered
  bool event(const SeqEvent& e);                           // mixEv: tvl, tva, tpn, mvol, mout. true: taken
  bool set(const MixSet& c);                               // false: not a thing this mixer has (a bad track, Send)
  void songLanesReset();
  // THE BEAT CLOCK (mix/beat.h): 'beat' events (event()) ramp it; stop it with clockStop at PLAY / STOP; render() fills it each block.
  void clockStop(double at) { clk_.stop(at); }
  BeatClock& clock() { return clk_; }
  // after render() and valid until the next: the core's lane (beat + 1 playing, 0 stopped); clkBus (the lane - 1: the beat playing, -1 stopped);
  // and what the effects node of track t (kSongNT: the master) hears: clkBus, delayed by its c. Callable from a track's / the master's hook.
  const float* beatLane() const { return clkLane_.get(); }
  const float* beatBus() const { return clkBus_.get(); }
  const float* beat(int t) const { return t >= 0 && t < kMixTracks && clkRd_[t] ? clkRd_[t] : clkBus_.get(); }                                   // mixReset (PLAY and STOP): the song's lanes are gone, the mixer's own stay

  // The tracks' inputs for this block: the voice pool adds into them (VoicePool::render(inputsL(), inputsR(), ...)). render() clears them.
  float* const* inputsL() { return inPL_; }
  float* const* inputsR() { return inPR_; }
  // One block (frames <= maxBlock) starting at t0 seconds: reads the inputs, writes the stereo output, clears the inputs.
  void render(float* outL, float* outR, int frames, double t0);

  // what a track sends on after its pan, and before its fader (valid after render(), for meters and tests)
  const float* postL(int t) const { return post_.get() + (size_t)(t * 2) * (size_t)maxBlock_; }
  const float* postR(int t) const { return post_.get() + (size_t)(t * 2 + 1) * (size_t)maxBlock_; }

  uint32_t autoDropped() const;      // automation events a full lane refused (must stay 0)
  int peakEvents() const { return peakEv_; }   // the longest lane seen
  uint32_t audibleMask() const { return audible_; }
  uint32_t routedMask() const { return routed_; }   // the pure sources playing only through a bus that exists
  uint32_t busMask() const { return busOn_; }       // the buses that exist

  static constexpr int kAutoUi = 16;     // a mixer lane: mix.set moves (each a few events; folded every block)
  static constexpr int kAutoSong = 64;   // a song lane: the events the sequencer books in the next LEAD

 private:
  struct Want { double gv = 1, pv = 0, tg = 1, prv = 1; };
  struct Hook { MixInsertFn fn = nullptr; void* user = nullptr; };
  struct Strip {
    AutoN<kAutoUi> inp{1}, g{1}, pre{1}, pan{0};
    AutoN<kAutoSong> mg{1}, mp{0};
    bool hasMg = false, hasMp = false;
    Want last;
  };
  Want want(int t) const;
  void move(Auto& A, double v, double at, double ramp);
  void refresh(double at, double ramp);
  void rebuild();
  bool lane(float* C, Auto& A, int N, double t0);   // fills C with the lane's block values; true: one value for the whole block
  void track(int t, int N, double t0);
  struct Dly { DelayRing post, pre; DelayParam dl, fdl, fdlP; };
  // reads a delay point for the block just pushed: returns the input pair itself when the delay is 0 for the whole block, else fills oL / oR
  void tap(const DelayRing& r, DelayParam& p, const float*& L, const float*& R, float* oL, float* oR, int N, double t0);
  void note(const Auto& A) { if (A.size() > peakEv_) peakEv_ = A.size(); }

  double sr_, dt_;
  int maxBlock_;
  double now_ = 0;
  std::unique_ptr<Strip[]> S_;
  AutoN<kAutoUi> out_{1}, mo_{1};
  AutoN<kAutoSong> mm_{0}, mo2_{0};
  bool hasMm_ = false, hasMo_ = false;
  // the page's values (S.mix, S.master, S.mout)
  double vol_[kSongNT], trim_[kSongNT], pan_[kSongNT], master_ = 255, mout_ = 255;
  uint32_t mute_ = 0, solo_ = 0, audible_ = 0xffffffffu;
  bool mono_[kMixTracks];
  BusPlan plan_;
  Hook chn_[kMixTracks], fx_[kMixTracks], mfx_;
  uint32_t busOn_ = 0, routed_ = 0, preSrc_ = 0;   // buses that exist; sources off the master; sources some bus hears PRE
  int order_[kMixTracks];
  std::unique_ptr<float[]> in_, post_, preb_, c_;   // inputs, after-pan pairs, pre pairs (maxBlock each), the lanes' block values
  std::unique_ptr<double[]> d_;
  double capSec_;
  BeatClock clk_;
  struct Rd { DelayRing ring; DelayParam cd; };
  std::unique_ptr<Rd[]> rd_;
  std::unique_ptr<float[]> clkLane_, clkBus_, clkOut_;
  const float* clkRd_[kMixTracks];
  void clockBlock(int N, double t0);
  std::unique_ptr<Dly[]> dly_;
  std::unique_ptr<float[]> fd_, sc_;               // the delayed feeds (post, pre) of every track; a scratch pair for the master sum
  const float *fpL_[kMixTracks], *fpR_[kMixTracks], *feL_[kMixTracks], *feR_[kMixTracks];   // what a bus hears of each source (this block)
  uint32_t fdlSrc_ = 0;                            // sources some existing bus hears POST (preSrc_ are heard PRE)
  float* inPL_[kMixTracks]; float* inPR_[kMixTracks];
  int peakEv_ = 0;
};

}  // namespace thunder
#endif
