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

#include "master/chain.h"
#include "meter/meter.h"
#include "mix/mix.h"
#include "mix/pdc.h"
#include "seq/seq.h"
#include "song/song.h"
#include "voice/voice.h"

namespace thunder {

constexpr int kRenderPads = 256;       // pad slots a kit can have (a pad number is one byte)
constexpr int kRenderEvents = 1024;    // one sequencer step's events (more are counted: stats().seqDropped)

struct RenderPads {
  VoiceBuf buf[kRenderPads];           // data == nullptr: the kit has no buffer for the pad (its notes are silent, as `kit.voices[pad]` missing)
  VoiceKit kit[kRenderPads];
};

struct RenderStats {
  int64_t frames = 0;
  int steps = 0, events = 0, notes = 0, notesPlayed = 0, notesNoPad = 0, notesMidiOnly = 0, notesShapedRev = 0, mixEvents = 0, beats = 0;
  int peakVoices = 0, peakVoiceLane = 0, peakMixLane = 0, peakStepEvents = 0;
  uint32_t voiceDropped = 0, voiceOverflow = 0, voiceAutoDropped = 0, mixAutoDropped = 0;
  uint64_t seqDropped = 0;
};

class SongRenderer {
 public:
  // Control thread. opts: makeSeq's options (a render: row = the song's RENDER FROM, once = true).
  SongRenderer(const Song& song, const SeqVars& vars, const RenderPads& pads, double sampleRate, int maxBlock, const SeqOpts& opts, int maxVoices = 64);
  ~SongRenderer();
  SongRenderer(const SongRenderer&) = delete;
  SongRenderer& operator=(const SongRenderer&) = delete;

  void block(float* outL, float* outR, int frames);     // the next `frames` (<= maxBlock) samples

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
  // METERS (U13): every block, after the mixer, the tracks' post-pan pairs and the master output are fed to `bank` and its clock advanced (null: none). The bank outlives the renderer.
  void setMeters(MeterBank* bank) { meters_ = bank; }
  // N frames of the song with the latency trimmed off the start, as pass() returns them: renders latencyFrames() + N and drops the first
  // latencyFrames(). Allocates nothing (a scratch block made at construction).
  void renderTrimmed(float* outL, float* outR, int64_t N);
  bool songOver() const { return !seq_->alive(); }       // the sequencer has reached the end
  int voices() const { return pool_.active(); }          // voices still sounding
  double time() const { return (double)frame_ / sr_; }
  int64_t frame() const { return frame_; }
  Mixer& mixer() { return mix_; }
  VoicePool& pool() { return pool_; }
  RenderStats stats() const;

  double lead = 0.25;                                    // LEAD (the core's `lead` command: 0.05..2 s)

 private:
  void dispatch(const SeqEvent& e);
  const Song& S_; const RenderPads& pads_;
  double sr_; int maxBlock_;
  std::unique_ptr<Seq> seq_;
  VoicePool pool_;
  Mixer mix_;
  std::unique_ptr<SeqEvent[]> evs_;
  std::unique_ptr<SeqStep> step_;
  int64_t frame_ = 0;
  PdcPlan plan_; int64_t extra_ = 0;
  std::unique_ptr<float[]> scratch_;
  std::unique_ptr<MasterChain> mchain_;
  MeterBank* meters_ = nullptr;
  RenderStats st_;
};

}  // namespace thunder
#endif
