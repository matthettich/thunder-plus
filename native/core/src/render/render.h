// THE OFFLINE SONG RENDERER (N1, unit U09): the port's first whole path, sequencer -> sampler voices -> mixer, one block at a time.
// It runs the blocks as the web app's audio-thread core does (core/core-processor.js process1): at the top of each block the sequencer is
// stepped while its next step is due within LEAD (0.25 s) of the block's start, at most three steps a block, every event handed on at
// once (a note is booked at its own time, max(ev.t, the block's start): on the same sample as the web app's players book it), then the
// voices sound into the tracks' inputs and the mixer mixes. No effects, chains, mastering, live engines or SoundFonts yet: a pad plays
// the rendered buffer the host hands in (a live-engine pad its render, as the page does where the live module is not loaded).
//
// REAL-TIME RULES: the constructor allocates; block() allocates nothing, takes no lock and does no I/O (native/tests/rt_harness.cpp
// runs it under the allocation guard). The Song, the vars and the pad buffers must outlive the renderer.
#ifndef THUNDER_RENDER_RENDER_H
#define THUNDER_RENDER_RENDER_H
#include <cstdint>
#include <memory>
#include <vector>

#include "master/chain.h"
#include "meter/meter.h"
#include "mix/mix.h"
#include "mix/pdc.h"
#include "seq/seq.h"
#include "sf/sf_live.h"
#include "fx/fxbank.h"
#include "song/song.h"
#include "voice/voice.h"

namespace thunder {

constexpr int kRenderPads = 256;       // pad slots a kit can have (a pad number is one byte)
constexpr int kRenderQuantum = 128;    // frames a song is rendered in (the web core's block), whatever the host's block
constexpr int kRenderEvents = kSeqStepEvents;   // one sequencer step's events, sized for the worst case (seq.h; more are counted: stats().seqDropped)

// Which player a pad's notes are for (the kit entry's `live` / `sf` of coreKitV()). Sampler: the rendered pad (or a variation of it). Live: a live engine
// (bass, vox, acid, mach, xeng, pump synth, waves): its player is not wired into this renderer yet, so its notes are SILENT and counted (stats().notesLive).
// SoundFont: the sf player (sf/sf_live.h, one SfPool next to the VoicePool): a pad with a prepared preset (RenderPads::sf) plays through it; a pad without one is SILENT and counted (notesSf).
enum class PadRoute : uint8_t { Sampler = 0, Live = 1, SoundFont = 2 };

// A MUT / GEN variation the host rendered (the page's `vbuf`): mono, keyed as the core's varBufs are, by instrument, kind ('g' GEN / 'm' MUT), number and pad.
struct RenderVar {
  int ii = 0; char kind = 0; int num = 0, pad = 0;
  std::vector<float> data; VoiceBuf buf;   // buf.data points into data
};

struct RenderPads {
  VoiceBuf buf[kRenderPads];           // data == nullptr: the kit has no buffer for the pad (its notes are silent, as `kit.voices[pad]` missing)
  VoiceKit kit[kRenderPads];
  PadRoute route[kRenderPads] = {};    // Sampler unless the host says otherwise
  sf::SfPad sf[kRenderPads];           // the prepared SoundFont of a pad (font == nullptr: none): borrowed, the host (the C API) keeps the font, the preset and the PCM alive for the renderer's life
  SeqChop chop;                        // per pad: the loop length and slice points applyChop reads (seq/chop.h); the renderer hands &chop to its Seq
  std::vector<RenderVar> var;          // sorted by (ii, kind, num, pad); read-only while a renderer runs
  static bool varLess(const RenderVar& a, int ii, char kind, int num, int pad) {
    if (a.ii != ii) return a.ii < ii;
    if (a.kind != kind) return a.kind < kind;
    if (a.num != num) return a.num < num;
    return a.pad < pad; }
  RenderVar* findVar(int ii, char kind, int num, int pad) {
    size_t lo = 0, hi = var.size();
    while (lo < hi) { const size_t m = (lo + hi) / 2; if (varLess(var[m], ii, kind, num, pad)) lo = m + 1; else hi = m; }
    return lo < var.size() && var[lo].ii == ii && var[lo].kind == kind && var[lo].num == num && var[lo].pad == pad ? &var[lo] : nullptr; }
  const RenderVar* findVar(int ii, char kind, int num, int pad) const { return const_cast<RenderPads*>(this)->findVar(ii, kind, num, pad); }
  // Control thread. Adds or replaces (frames == 0 removes). `mono` is copied. Returns false when the allocation failed (throws only if exceptions are on).
  void setVar(int ii, char kind, int num, int pad, const float* mono, int frames, double sr) {
    size_t lo = 0, hi = var.size();
    while (lo < hi) { const size_t m = (lo + hi) / 2; if (varLess(var[m], ii, kind, num, pad)) lo = m + 1; else hi = m; }
    const bool have = lo < var.size() && var[lo].ii == ii && var[lo].kind == kind && var[lo].num == num && var[lo].pad == pad;
    if (frames <= 0) { if (have) var.erase(var.begin() + (std::ptrdiff_t)lo); return; }
    std::vector<float> d(mono, mono + frames);
    if (!have) { RenderVar v; v.ii = ii; v.kind = kind; v.num = num; v.pad = pad; var.insert(var.begin() + (std::ptrdiff_t)lo, std::move(v)); }
    RenderVar& v = var[lo]; v.data.swap(d); v.buf = VoiceBuf(); v.buf.data = v.data.data(); v.buf.len = frames; v.buf.sr = sr;
  }
  // The bytes the variations of pad `pad` (any instrument) hold, and dropping them (the page drops a pad's variations whenever the pad's buffer changes:
  // core/core-processor.js:337 `case 'buf'` -> varDrop(d.pad), :239). Control thread. dropVarsOfPad returns the bytes freed; it allocates nothing.
  size_t varBytesOfPad(int pad) const { size_t b = 0; for (const RenderVar& v : var) if (v.pad == pad) b += v.data.size() * sizeof(float); return b; }
  size_t dropVarsOfPad(int pad) {
    size_t b = 0, w = 0;
    for (size_t i = 0; i < var.size(); i++) { if (var[i].pad == pad) { b += var[i].data.size() * sizeof(float); continue; } if (w != i) var[w] = std::move(var[i]); w++; }
    var.erase(var.begin() + (std::ptrdiff_t)w, var.end());   // (order kept: still sorted)
    return b; }
};

struct RenderStats {
  int64_t frames = 0;
  int steps = 0, events = 0, notes = 0, notesPlayed = 0, notesNoPad = 0, notesMidiOnly = 0, notesShapedRev = 0, mixEvents = 0, beats = 0;
  // D2 / D3 routing: notesLive / notesSf: notes of a live-engine / SoundFont pad (silent: no player wired); notesVar: notes that played a MUT / GEN variation;
  // notesVarMissing: a MUT / GEN note whose variation the host did not hand in (silent); notesUnshaped: notes of a sliced / synced instrument that had a MUT / GEN / CUT / RES / DEC and played the pad as it is (varRec);
  // notesSf counts every note of a SoundFont-route pad; notesSfPlayed those that sounded through the SfPool (a prepared font, a zone for the key, a free voice; NOT counted in notesPlayed);
  // eventsIgnored: events this renderer has no route for (eng, chc, tempo, the MIDI events)
  int notesLive = 0, notesSf = 0, notesSfPlayed = 0, notesVar = 0, notesVarMissing = 0, notesUnshaped = 0, eventsIgnored = 0;
  int peakVoices = 0, peakVoiceLane = 0, peakMixLane = 0, peakStepEvents = 0;
  uint32_t voiceDropped = 0, voiceOverflow = 0, voiceAutoDropped = 0, mixAutoDropped = 0;
  uint64_t seqDropped = 0;
  // appended with the SoundFont route's review (verify-242 F6): the SfPool's own counters (voiceDropped / voiceOverflow / voiceAutoDropped are the SAMPLER pool's) and the peak of both pools' voices together
  uint32_t sfVoiceDropped = 0, sfVoiceOverflow = 0, sfVoiceAutoDropped = 0; int peakVoicesAll = 0;
  // chop (CHOP-NOTES.md): notesStretched = notes with ev.st (sync 2 / 3) played UNSTRETCHED at `rate` (the stretch port does not exist yet); seqChopped = notes chopped; seqChopSkipped = a sliced / synced
  // instrument's note whose pad had no chop data handed in (unchopped); seqChopNoHits = SMODE on without handed-in hits (grid used)
  int notesStretched = 0; uint64_t seqChopped = 0, seqChopSkipped = 0, seqChopNoHits = 0;
};

class SongRenderer {
 public:
  // Control thread. opts: makeSeq's options (a render: row = the song's RENDER FROM, once = true).
  SongRenderer(const Song& song, const SeqVars& vars, const RenderPads& pads, double sampleRate, int maxBlock, const SeqOpts& opts, int maxVoices = 64);
  ~SongRenderer();
  SongRenderer(const SongRenderer&) = delete;
  SongRenderer& operator=(const SongRenderer&) = delete;

  // The next `frames` samples, any count (N1 review B1/C8). The song is rendered in fixed quanta of kRenderQuantum (128) frames on one
  // global grid, as the web core's 128-frame blocks are, and handed out from a small buffer: the samples are the same whatever block
  // size the host asks for (1, 480, 4096...), and a fast song is stepped as often at 4096 frames as at 128 (the old "three steps a block"
  // made a 4096-frame callback play a 300 BPM song at 0.44 speed).
  void block(float* outL, float* outR, int frames);

  // LATENCY COMPENSATION (pass()): the plan is set in force at once (a render's pdcSync) and everything plays plan.T later, so the render is
  // `N + latencyFrames()` long and the start is trimmed. extraLatencyFrames: what pass() adds for the master side (mastering, the master's pump
  // effects, the limiter's look-ahead: U11 / U12 / U18 say how much). Call before the first block.
  void setPlan(const PdcPlan& plan, PdcMode mode, int64_t extraLatencyFrames = 0);
  int64_t latencyFrames() const { return extra_ + (mchain_ ? mchain_->latency() : 0) + pdc_trim_frames(plan_, sr_); }
  // THE MASTER CHAIN (U12): between MASTER VOL and OUT, as the page routes it: [mastering: EQ, suppressor / compressor / limiter] -> [the safety limiter].
  // OFF by default (no chain: what U09 / U10 rendered). master_chain_from_song(song.mst, song.limiter) is the song's own. Its latency (mastering's
  // suppressor + limiter look-ahead, the safety limiter's look-ahead) is part of latencyFrames() and renderTrimmed() trims it, as pass() does.
  // Control thread, before the first block (the processors start from rest).
  void setMasterChain(const MasterChainConfig& cfg);
  MasterChain* masterChain() { return mchain_.get(); }
  // A STEM render (U34b, renderOut's pass(evs, {t}, N, sr, getB, false, false), index.html:24351): track `track` (0..kSongNT-1) alone is heard (plus, when it is a bus of the song's chains patch, its audible
  // sources: pass's k2, :24228) whatever mute / solo say; every track still plays (chokes, takeovers). No master effects, no master chain (mastering, safety limiter) and no master
  // latency: setMasterChain / the master hook are refused from here on and a chain already set is dropped. MASTER VOL and OUT stay. Control thread, before the first block,
  // and before setEffects / setMasterChain (which then leave the master alone). Returns false for a bad track.
  bool setStem(int track);
  // A chains host, once there is one, must set a chain hook (Mixer::setChain) only on the tracks in heardMask() of a stem render, as pass() does (`routeTrack` for `keep` only, :24258):
  // a bus that is not heard does not exist there, so a pure source heard alone plays on its own, not through it.
  int stem() const { return stem_; }                     // -1: the mix
  uint32_t heardMask() const { return mix_.audibleMask(); }
  // TRACK AND MASTER EFFECTS (U14): the song's tfx effects (S.tfx / S.mfx, the 34 wasm effects) in the mixer's setFx / setMasterFx hooks, their FXO / FLX / GAT /
  // RTG / FXV events dispatched to them. Effects of the other kinds (U15 to U18) are left out. Control thread, before the first block.
  void setEffects(bool on);
  FxBank* fxBank() { return fx_.get(); }
  // METERS (U13): every block, after the mixer, the tracks' post-pan pairs and the master output are fed to `bank` and its clock advanced (null: none). The bank outlives the renderer.
  void setMeters(MeterBank* bank) { meters_ = bank; }
  // N frames of the song with the latency trimmed off the start, as pass() returns them: renders latencyFrames() + N and drops the first
  // latencyFrames(). Allocates nothing (a scratch block made at construction).
  void renderTrimmed(float* outL, float* outR, int64_t N);
  bool songOver() const { return !seq_->alive(); }       // the sequencer has reached the end
  int voices() const { return pool_.active() + sfpool_.active(); }   // voices still sounding (the sampler's and the SoundFont pool's)
  double time() const { return (double)frame_ / sr_; }
  int64_t frame() const { return frame_; }
  Mixer& mixer() { return mix_; }
  VoicePool& pool() { return pool_; }
  sf::SfPool& sfPool() { return sfpool_; }
  // A tap on the mixer's per-track inputs (the sampler's and the SoundFont pool's voices summed), called each quantum just before the mixer runs; for tests. Null: none.
  typedef void (*InputTap)(void* user, const float* const* inL, const float* const* inR, int frames, int64_t frame0);
  void setInputTap(InputTap fn, void* user) { tap_ = fn; tapUser_ = user; }
  RenderStats stats() const;

  double lead = 0.25;                                    // LEAD (the core's `lead` command: 0.05..2 s)

 private:
  void dispatch(const SeqEvent& e);
  void capPoly(const SeqEvent& e);                       // a poly note: the track's cap of four is shared by both pools, oldest first (core-processor.js :442-443, :582-583: one list)
  void takeAll(const SeqEvent& e, int pad);              // a note the sampler does not play: the track's voices in BOTH pools give way (one voice list in the JS)
  void quantum();                                        // renders the next kRenderQuantum frames into q_
  const Song& S_; const RenderPads& pads_;
  double sr_; int maxBlock_;
  std::unique_ptr<Seq> seq_;
  VoicePool pool_;
  sf::SfPool sfpool_;
  Mixer mix_;
  std::unique_ptr<SeqEvent[]> evs_;
  std::unique_ptr<SeqStep> step_;
  int64_t frame_ = 0;                                    // frames handed to the host
  int64_t qFrame_ = 0; int qRead_ = kRenderQuantum;      // frames rendered into q_; how many of the last quantum the host has had
  std::unique_ptr<float[]> q_;                           // the last quantum: left then right
  PdcPlan plan_; int64_t extra_ = 0;
  std::unique_ptr<float[]> scratch_;
  std::unique_ptr<MasterChain> mchain_;
  std::unique_ptr<FxBank> fx_;
  static void masterHook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds);
  MeterBank* meters_ = nullptr;
  int stem_ = -1;
  RenderStats st_;
  uint64_t serial_ = 0; int peakAll_ = 0;                // voice serials (age across the pools) and the most voices of both pools sounding together
  InputTap tap_ = nullptr; void* tapUser_ = nullptr;
};

}  // namespace thunder
#endif
