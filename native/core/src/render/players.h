// THE PLAYERS (N1, LIVE-ROUTE step 0): the note players of a song render behind one class. Dispatch of a note-class event (note / off / mod / stp) to the
// player its pad is routed to, the pools (the sampler's VoicePool, the SoundFont pool), the track's voice list they share (the poly cap of four by serial,
// the takeover, the choke), the routing stats and the voice count. This is core/core-processor.js ThunderCore.ev() for the events that start, move or
// stop voices; SongRenderer keeps what is not a player's: the mixer's events, the beat clock, the effects, the sequencer and the quantum loop.
// A move of code only: every render is bit-identical to the one before. Live engines (LivePool) join here later.
//
// REAL-TIME RULES: the constructor allocates; dispatch() / render() allocate nothing and take no lock.
#ifndef THUNDER_RENDER_PLAYERS_H
#define THUNDER_RENDER_PLAYERS_H
#include <atomic>
#include <cstdint>

#include "engine/live_pool.h"
#include "render/render.h"

namespace thunder {

// The note-routing counters (RenderStats's fields of the same names are copied from here).
struct PlayerStats {
  int notes = 0, notesPlayed = 0, notesNoPad = 0, notesMidiOnly = 0, notesShapedRev = 0;
  int notesLive = 0, notesSf = 0, notesSfPlayed = 0, notesVar = 0, notesVarMissing = 0, notesUnshaped = 0, notesStretched = 0;
  int notesLivePlayed = 0;                              // notes of a live pad a node sounded (a subset of notesLive)
  int notesLate = 0;                                    // setDropLate: notes dropped for lateness (counted in `notes`, in no route's counter)
  int peakAll = 0;   // the most voices of all pools sounding together (sampled after every event: samplePeak())
};

class Players {
 public:
  // Control thread. Neither the Song nor the pads are held by reference (a live host adopts a new Song snapshot while playing and the old one is freed: P1):
  // dispatch() takes the Song it should read, and the pads are read through a pointer the owner may re-point between quanta (setPads). The pads must outlive their use.
  Players(const RenderPads* pads, double sampleRate, int maxVoices);
  void setPads(const RenderPads* pads) { pads_ = pads; }
  // LATENESS (B7, U36 PR B). Off by default: every render is bit-identical to before. On (the live host turns it on): a NOTE whose time is more than kNoteLateSec (0.15 s, midisched.h seq_note_late) behind the
  // quantum's start t0 (setTime) is not played, by any route (sampler, SoundFont, live engine), and counts in stats().notesLate. A late MONO note (not e.poly) still lets go of the track's previous voices and its
  // choke group (MidiSched's rule); a late poly note touches nothing. Off / mod / stp and every other event are never dropped for lateness.
  // THIS IS NOT THE WEB'S RULE, ON PURPOSE. The web's own core PLAYS a late sampler, SoundFont or live-engine note at once (core-processor.js: sampler startNote at max(ev.t, currentTime) :591, SoundFont :447, live
  // liveNote :694); index.html coreMsg drops `e.t < currentTime - 0.15` (:25325) only for notes the core did not play, because `if (e.core) continue` (:25315) comes first. And the web's gear path (moEv :18153, before
  // that test) sends a late OUTPUT 2 note. Natively ONE rule covers pad and gear (the lead's ruling, U36-SPEC 2.5): after a stall or a seek of more than 0.15 s the pad and the gear agree and there is no burst of
  // late hits; the web plays them in a burst. Audible difference: only when the sequencer lags the audio clock by more than 0.15 s. A late mono note ends the previous voice in both. A MIDI-only instrument
  // (OUTPUT 1) sounds no pad and is not counted here, as before.
  void setDropLate(bool on) { dropLate_.store(on, std::memory_order_relaxed); }

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
  // U37 P1: the smallest epoch of the pad data each slot's live or booked voices started on (both pools; the live engines hold no pad data: a node owns its parameters). mins: kEpochSlots entries, kEpochNone where no voice. Audio thread, allocation-free.
  void epochMins(uint64_t* mins) const { pool_.epochMins(mins); sfpool_.epochMins(mins); }
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
  std::atomic<bool> dropLate_{false}; double t0_ = 0;                // setDropLate; the quantum's start (setTime)
  uint64_t serial_ = 0;                                  // voice serials (age across the pools)
};

}  // namespace thunder
#endif
