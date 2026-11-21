// See players.h. The code here is SongRenderer::dispatch / capPoly / takeAll as they were in render.cpp (N1 / U09 and the SoundFont route), moved.
#include "render/players.h"

#include "rt/num.h"

namespace thunder {

Players::Players(const RenderPads* pads, double sampleRate, int maxVoices) : pads_(pads), pool_(sampleRate, maxVoices), sfpool_(sampleRate), live_(sampleRate) {}

void Players::setTime(double t0, int64_t frame0) { pool_.setTime(t0); sfpool_.setFrame((uint64_t)frame0); live_.setTime(t0); }
void Players::render(float* const* inL, float* const* inR, int frames, double t0, int64_t frame0) {
  pool_.render(inL, inR, frames, t0);
  sfpool_.render(inL, inR, frames, (uint64_t)frame0);   // adds into the same inputs
  live_.render(inL, inR, frames, frame0);
}

bool Players::dispatch(const SeqEvent& e, const Seq& seq, const Song& song) {
  switch (e.type) {
    case SEV_STP: pool_.stp(e); sfpool_.stp(e); live_.stp(e); return true;
    case SEV_OFF: if (e.track >= 0) { pool_.off(e); sfpool_.off(e); live_.off(e); } return true;
    case SEV_MOD: if (e.track >= 0) { const double* arp = seq.arp(e); pool_.mod(e, arp); sfpool_.mod(e, arp); live_.mod(e, arp); } return true;
    case SEV_NOTE: note(e, seq, song); return true;
    case SEV_TEMPO: live_.tempo(e.t, e.v); return false;   // (the live bass nodes follow it; the event still counts as ignored for the renderer, as before)
    default: return false;
  }
}

void Players::note(const SeqEvent& e, const Seq& seq, const Song& song) {
  const RenderPads& pads_ = *this->pads_;
  if (e.track < 0) return;
  st_.notes++;
  { const int ii = to_int_sat(e.ii, -1, 1 << 20); /* N1 C3: NaN is "none" */ if (ii >= 0 && ii < kNI && song.insts[ii].mo == 1) { st_.notesMidiOnly++; return; } }   // MIDI gear only: nothing sounds here
  const int pad = to_int_sat(e.pad, -1, 1 << 20);   // (N1 C3: a part without `pad` gives NaN: no pad, what x86 gave and what the JS's kit.voices[NaN] is)
  if (pad < 0 || pad >= kRenderPads) { st_.notesNoPad++; return; }
  // D2: which player. A live-engine or SoundFont pad is not the sampler's: its notes are silent here (counted), but they still take the track over (a live note does: liveNote / sfNote).
  if (pads_.route[pad] == PadRoute::Live) {
    st_.notesLive++;
    const int ck = pads_.kit[pad].choke; const uint64_t sr = ++serial_; pool_.stamp(sr); sfpool_.stamp(sr); live_.stamp(sr);
    bool polyVox = false;
    if (live_.has(pad) && live_.note(e, pad, seq.bpm(), seq.arp(e), ck, &polyVox)) {   // a pad with live params plays; one without stays silent (counted)
      st_.notesLivePlayed++;
      // liveNote: polyVox takes nothing over; every other live note is the MONO branch whatever ev.poly says (P2): the sampler's and SoundFont voices of the track give way, and the choke group
      if (!polyVox) { SeqEvent m = e; m.poly = false; takeAll(m, pad); }
    } else { takeAll(e, pad); live_.take(e, ck); }   // no node: `this.take(ev)` (a poly event takes nothing over of its own track, the choke group still)
    return;
  }
  if (pads_.route[pad] == PadRoute::SoundFont) {
    st_.notesSf++;
    const sf::SfPad& sp = pads_.sf[pad];
    if (!sp.font || !sp.pcm) { takeAll(e, pad); live_.take(e, pads_.kit[pad].choke); return; }   // no preset prepared: silent (the page would prepare it and play nothing now, without taking over; this renderer takes over, as it did before the font input)
    // core/core-processor.js sfNote: no zone for the key / velocity: returns before the takeover. Otherwise the track's sampler voices give way (the SfPool does its own).
    if (!sfpool_.covers(e, sp)) return;
    capPoly(e);
    live_.take(e, pads_.kit[pad].choke);
    pool_.take(e, &pads_.kit[pad]);
    if (sfpool_.note(e, seq.arp(e), sp, pads_.kit[pad])) st_.notesSfPlayed++;
    return;
  }
  if (!pads_.buf[pad].data) { st_.notesNoPad++; return; }   // a slot the kit doesn't have is silent
  VoiceBuf b = pads_.buf[pad];   // (a copy of the small descriptor: the buffer itself is not copied)
  // D3, varRec: an instrument with slc > 1 or sync plays the pad as it is (no variation, no shaping). The JS: `S.insts[ev.ii]`, a whole index.
  const int ii = (e.ii == e.ii && e.ii >= 0 && e.ii < kNI && e.ii == (double)(int)e.ii) ? (int)e.ii : -1;
  bool useVar = false;
  if (ii >= 0 && (song.insts[ii].slc > 1 || song.insts[ii].sync)) { b.noShape = true; if (e.has & (SH_VK | SH_TW)) st_.notesUnshaped++; }
  else if (e.has & SH_VK) {   // D2: MUT / GEN: the variation the host rendered, by instrument, kind, number and pad; none: silent (never the base pad)
    const RenderVar* v = ii >= 0 ? pads_.findVar(ii, e.vkKind, e.vkNum, pad) : nullptr;
    if (!v) { st_.notesVarMissing++; takeAll(e, pad); live_.take(e, pads_.kit[pad].choke); return; }
    b = v->buf; useVar = true;
  }
  if (VoicePool::shapedReverse(e, b)) { st_.notesShapedRev++; takeAll(e, pad); live_.take(e, pads_.kit[pad].choke); return; }   // (the page plays a reversed + shaped note: not this player's)
  if (VoicePool::playable(e, b)) { capPoly(e); live_.take(e, pads_.kit[pad].choke); }   // (a note the pool refuses touches nothing)
  sfpool_.take(e, &pads_.kit[pad]);   // the sampler's note takes the track's SoundFont voices over too (VoicePool::note does the sampler's own)
  if (e.has & SH_ST) st_.notesStretched++;   // sync 2 / 3: the page plays the stretched pad; here the pad unstretched at `rate` (counted)
  if (pool_.note(e, seq.arp(e), b, pads_.kit[pad], seq.bpm())) { st_.notesPlayed++; if (useVar) st_.notesVar++; }
}

// startNote / sfNote: `else while (T.length >= POLY) kill(T.shift(), ev.t, 0.006)` on ONE list per track, after the dead voices are removed. Here the list is split in two (the sampler's, the SoundFont's), each in start order, so the oldest is the one
// with the smaller serial. Serials come from one counter (the pools stamp their next voice); the pools' own caps then find room and do nothing. Allocates nothing.
void Players::capPoly(const SeqEvent& e) {
  const uint64_t s = ++serial_; pool_.stamp(s); sfpool_.stamp(s); live_.stamp(s);
  if (!e.poly || e.track < 0 || e.track >= kVoiceTracks) return;
  int a = pool_.liveOnTrack(e.track, e.t), b = sfpool_.liveOnTrack(e.track, e.t), c = live_.liveOnTrack(e.track, e.t);   // P3: a live voice is in the list too
  while (a + b + c >= kVoicePoly) {
    uint64_t sa = 0, sb = 0, sc = 0; const bool ha = pool_.oldestOnTrack(e.track, sa), hb = sfpool_.oldestOnTrack(e.track, sb), hc = live_.oldestOnTrack(e.track, sc);
    if (ha && (!hb || sa < sb) && (!hc || sa < sc)) { pool_.releaseOldest(e.track, e.t); a--; }
    else if (hb && (!hc || sb < sc)) { sfpool_.releaseOldest(e.track, e.t); b--; }
    else if (hc) { live_.releaseOldest(e.track, e.t); c--; }
    else break;
  }
}

void Players::stopAll(double at) { pool_.stopAll(at); sfpool_.stopAll(at); live_.stopAll(at); }

void Players::takeAll(const SeqEvent& e, int pad) { pool_.take(e, &pads_->kit[pad]); sfpool_.take(e, &pads_->kit[pad]); }

}  // namespace thunder
