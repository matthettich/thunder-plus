// U32: THE LIVE SOUNDFONT NOTE (core/core-processor.js `sfNote` + `sfRun`, and index.html's `sfNote` it copies): one note of a SoundFont pad = up to four zones (a layered preset, a stereo pair counts as one), each its own
// looping sample with the font's volume envelope, the font's low-pass swept by its modulation envelope, and its own pan, into one voice the track treats like any other (mono takeover, poly of four, OFF, pitch commands, chokes).
// Ported line by line, in the arithmetic order of the JS (doubles; the track outputs are float32 sums, as the JS's Float32Arrays). The seam is VoicePool's (voice/voice.h): the same SeqEvent, VoiceKit, take / off / mod / stp / stopAll /
// render, so the wiring (U34 / U37) can hold one SfPool next to the VoicePool and hand each pad's notes to the right one (see U32-NOTES.md "Conditions for the wiring").
//
// REAL-TIME RULES. The constructor allocates everything; note(), off(), mod(), stp(), stopAll(), take() and render() allocate nothing, take no lock and do no I/O. A pad is borrowed: its Font, Preset and PresetPcm must stay valid until the
// voices that play it have ended (the host prepares the preset BEFORE the pad plays, off the audio thread, and keeps it until its voices are gone). Counters: dropped() notes refused because every voice was busy, tOverflow(), autoDropped().
//
// THE RULE FOR TIME (the live rules of the port): a voice's samples are timed on the HOST'S ABSOLUTE FRAME COUNT, in the web copy's 128-frame quanta: the time of frame n is (n - n % 128) / sr + (n % 128) * (1 / sr), which is exactly
// the JS's `currentTime + i * dt` when the host's blocks are the 128-frame quanta, and is the same number for every host block size. Automation is folded (Auto::prune) only on a quantum edge. So the sound does not depend on the host's block
// size: ONE block of any length, 128, 64, 17 or 1 frames give the same float32 bits (ctest `sf_live`). Events must be handed in before their time (the engine's lead): an event whose time has passed is taken at the current frame, as the JS
// takes it at the current block's start (`Math.max(ev.t, currentTime)`), and that does depend on block size.
#ifndef THUNDER_SF_SF_LIVE_H
#define THUNDER_SF_SF_LIVE_H
#include <cstdint>
#include <memory>

#include "rt/auto.h"
#include "seq/seq.h"
#include "sf/sf.h"
#include "voice/voice.h"

namespace thunder {
namespace sf {

// A SoundFont pad as the core holds it once the page has sent it (the JS's sfPads entry {P, font, pr, smp}).
struct SfPad {
  const Font* font = nullptr; const Preset* pr = nullptr; const PresetPcm* pcm = nullptr;
  PadParams P;   // bank, prog (informational), trans, velo, atk, rel, level, len
};

class SfPool {
 public:
  explicit SfPool(double sampleRate, int maxVoices = 32);   // allocates (control thread)
  ~SfPool();
  SfPool(const SfPool&) = delete;
  SfPool& operator=(const SfPool&) = delete;

  // currentTime for the events that follow (and for render, which sets it itself): the host's frame count
  void setFrame(uint64_t frame) { now_ = timeOf(frame); }
  double timeOf(uint64_t frame) const;

  // sfNote(ev). `kv`: the pad's kit entry (choke, syn). Returns false if nothing sounds (no zone, no sample, no free voice, a bad event).
  bool note(const SeqEvent& e, const double* arp, const SfPad& pad, const VoiceKit& kv);
  void take(const SeqEvent& e, const VoiceKit* kit);   // a note the SAMPLER plays: this track's SoundFont voices give way (mono takeover, chokes)
  void off(const SeqEvent& e);
  void mod(const SeqEvent& e, const double* arp);
  void stp(const SeqEvent& e);
  void stopAll(double at);

  // One host block of `frames` starting at the host's absolute frame `frame0`: adds every voice into its track's output (null outL: the track has no output and its voices are left alone; null outR: mono).
  void render(float* const* outL, float* const* outR, int frames, uint64_t frame0);
  void render(float* L, float* R, int frames, uint64_t frame0);   // every track onto one pair

  int active() const { return nAlive_; }
  int capacity() const { return cap_; }
  uint32_t dropped() const { return dropped_; }
  uint32_t tOverflow() const { return tOver_; }
  uint32_t refused() const { return refused_; }
  uint32_t autoDropped() const;
  static constexpr int kZones = 4;
  static constexpr int kAutoR = 128, kAutoGFP = 32, kAutoZ = 24;

 private:
  struct Flt { AutoN<kAutoZ> fr{0}; double q = 0, b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, w1 = 0, w2 = 0; int k = 0; };
  struct Zn {
    const float *L = nullptr, *R = nullptr; int n = 0; double sr = 0, ls = 0, le = 0; bool lp = false, done = false, hasFlt = false, hasZp = false; double zp = 0, pos = 0, rate = 0;
    AutoN<kAutoZ> r{0}, env{0}; Flt flt;
  };
  struct Voice {
    Zn z[kZones]; int nz = 0; bool st = false;
    AutoN<kAutoR> r{0}; AutoN<kAutoGFP> g{0}, f{1}, pn{0};
    double base = 0, rate = 0, rel = 0, end = 0, t0 = 0, stopAt = 0; int choke = 0, col = 0, track = 0; bool ended = false, hasStop = false;
  };
  double relTc(const Voice& v) const { return v.rel / 4.6; }   // syn is always true for these voices
  bool live1(const Voice& v, double at) const { return !v.ended && !(v.hasStop && v.stopAt <= at); }
  void kill(int vi, double at, double tc = 0.004, bool stop = false);
  void pitchMods(Voice& v, double t, double base, const SeqEvent& e, const double* arp);
  void tErase(int track, int k);
  void tRemove(int vi);
  void aliveErase(int ai);
  void run(Voice& v, float* L, float* R, int N, uint64_t frame0);

  double sr_, dt_;
  int cap_;
  double now_ = 0;
  std::unique_ptr<Voice[]> V_;
  std::unique_ptr<int[]> alive_, free_;
  int nAlive_ = 0, nFree_ = 0;
  int T_[kVoiceTracks][kVoiceTrackCap]; int nT_[kVoiceTracks];
  float sL_[128], sR_[128];
  uint32_t dropped_ = 0, tOver_ = 0, refused_ = 0;
};

}  // namespace sf
}  // namespace thunder
#endif
