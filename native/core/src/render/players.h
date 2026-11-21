// THE PLAYERS (N1, LIVE-ROUTE step 0): the note players of a song render behind one class. Dispatch of a note-class event (note / off / mod / stp) to the
// player its pad is routed to, the pools (the sampler's VoicePool, the SoundFont pool), the track's voice list they share (the poly cap of four by serial,
// the takeover, the choke), the routing stats and the voice count. This is core/core-processor.js ThunderCore.ev() for the events that start, move or
// stop voices; SongRenderer keeps what is not a player's: the mixer's events, the beat clock, the effects, the sequencer and the quantum loop.
// A move of code only: every render is bit-identical to the one before. Live engines (LivePool) join here later.
//
// REAL-TIME RULES: the constructor allocates; dispatch() / render() allocate nothing and take no lock.
#ifndef THUNDER_RENDER_PLAYERS_H
#define THUNDER_RENDER_PLAYERS_H
#include <cstdint>

#include "engine/live_pool.h"
#include "render/render.h"

namespace thunder {

// The note-routing counters (RenderStats's fields of the same names are copied from here).
struct PlayerStats {
  int notes = 0, notesPlayed = 0, notesNoPad = 0, notesMidiOnly = 0, notesShapedRev = 0;
  int notesLive = 0, notesSf = 0, notesSfPlayed = 0, notesVar = 0, notesVarMissing = 0, notesUnshaped = 0, notesStretched = 0;
  int notesLivePlayed = 0;                              // notes of a live pad a node sounded (a subset of notesLive)
  int peakAll = 0;   // the most voices of all pools sounding together (sampled after every event: samplePeak())
};

class Players {
 public:
  // Control thread. Neither the Song nor the pads are held by reference (a live host adopts a new Song snapshot while playing and the old one is freed: P1):
  // dispatch() takes the Song it should read, and the pads are read through a pointer the owner may re-point between quanta (setPads). The pads must outlive their use.
  Players(const RenderPads* pads, double sampleRate, int maxVoices);
  void setPads(const RenderPads* pads) { pads_ = pads; }

  // A note, off, mod or stp event (any other type: returns false, nothing done). `seq` gives the arp table and the bpm the voices read.
  bool dispatch(const SeqEvent& e, const Seq& seq, const Song& song);
  // RESET TRACK (web #251: the core's message {op:'stp', track, t}): this track's voices in BOTH pools let go as STOP lets them go (a voice booked to start later never sounds). Not the sequencer's STP
  // command (that is dispatch(SEV_STP), stop = false). Control / audio thread between blocks, as dispatch(); allocation-free. The pump NOTES FROM routing (the page's pmNotes) has no native counterpart yet.
  void stopTrack(int track, double t) { pool_.stopTrack(track, t); sfpool_.stopTrack(track, t); live_.stopTrack(track, t); }
  // The quantum's clocks, then its voices added into the mixer's per-track inputs.
  void setTime(double t0, int64_t frame0);
  void render(float* const* inL, float* const* inR, int frames, double t0, int64_t frame0);

  void stopAll(double at);                               // every voice of every pool lets go at `at` (for a live host)
  int voices() const { return pool_.active() + sfpool_.active() + live_.active(); }   // voices still sounding (the sampler's, the SoundFont pool's, the live engines': C4)
  void songOver(double at) { live_.releaseHeld(at); }    // C4: the sequencer has ended: live voices still held let go at `at`
  void samplePeak() { const int v = voices(); if (v > st_.peakAll) st_.peakAll = v; }
  const PlayerStats& stats() const { return st_; }
  VoicePool& pool() { return pool_; }
  const VoicePool& pool() const { return pool_; }
  sf::SfPool& sfPool() { return sfpool_; }
  const sf::SfPool& sfPool() const { return sfpool_; }
  LivePool& live() { return live_; }                     // the live engines' pool (declare a pad with live().addPad before the first block)
  const LivePool& live() const { return live_; }

 private:
  void note(const SeqEvent& e, const Seq& seq, const Song& song);
  void capPoly(const SeqEvent& e);                       // a poly note: the track's cap of four is shared by both pools, oldest first (core-processor.js :442-443, :582-583: one list)
  void takeAll(const SeqEvent& e, int pad);              // a note the sampler does not play: the track's voices in BOTH pools give way (one voice list in the JS)
  const RenderPads* pads_;
  VoicePool pool_;
  sf::SfPool sfpool_;
  LivePool live_;
  PlayerStats st_;
  uint64_t serial_ = 0;                                  // voice serials (age across the pools)
};

}  // namespace thunder
#endif
