// U37 P4 (ctest `live_midi`): MIDI in the live host (docs/port/U37-P4-NOTES.md).
//   1  done-test    the record stream of LiveHost (F_MIDI: the scheduler's Start / Stop called DIRECTLY by the audio side right after midi_begin, the 0.3 s song-end Stop from its own timer) is BYTE-EQUAL
//                   (every field of every thunder_midi_msg, frame included, in order, CLEAR markers included) to the POSTED path (SongRenderer + thunder_midi_start / _stop posted before the block, the host's
//                   0.3 s rule on the control side) for the 18 ledger songs of test_midi_glue (+ start-midway and loop). Same Song, same SeqOpts (the live play's), the Seq started 0.08 s after the play.
//   2  state machine  HELD keeps the clock running and a later stop stops it; stop from IDLE repeats the MIDI stop; play over play; stop then play in one quantum keeps the order; the ENDED timer.
//   3  lead         midiLead range (0.005 .. 1.2), E_STATE without F_MIDI, and a 1.2 s lead books further ahead than the default.
//   4  plumbing     song_publish hands the song to the scheduler, spp from `row` (midi_spp_for), a host without F_MIDI has no scheduler, counters.
// Light on purpose (the sanitizer CI job has died of heavy tests): THUNDER_SANITIZE_BUILD (CMake, as test_fx / test_live_fx) or the compilers' own macros run a reduced matrix.
#include <algorithm>

#include "live_common.h"
#include "fx/fxbank.h"
#include "master/chain.h"
#include "midi/midi_spp.h"
#include "mix/pdc.h"
#include "render/render.h"
#include "midi_drive.h"
#include "thunder/midi.h"

#if defined(THUNDER_SANITIZE_BUILD) || defined(__SANITIZE_ADDRESS__)
#define LM_REDUCED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define LM_REDUCED 1
#endif
#endif
#ifndef LM_REDUCED
#define LM_REDUCED 0
#endif

using namespace lt;
typedef thunder_midi_msg Rec;

static std::string hexRec(const Rec& r) { char b[160]; std::snprintf(b, sizeof b, "{frame %lld port %d len %d flags %d kind %d bytes %02x %02x %02x cable %d}", (long long)r.frame, r.port, r.len, r.flags, r.kind, r.b[0], r.b[1], r.b[2], r.cable); return b; }
static int firstDiff(const std::vector<Rec>& a, const std::vector<Rec>& b) { const size_t n = std::min(a.size(), b.size()); for (size_t i = 0; i < n; i++) if (std::memcmp(&a[i], &b[i], sizeof(Rec)) != 0) return (int)i; return a.size() == b.size() ? -1 : (int)n; }

// ---- the scheduler's ports as the ledger case has them ----------------------------------------------------------------------------------------------------------------------------------------------------
static void setPorts(thunder_midi* h, const mdrive::Case& C) {
  for (int p = 0; p < 4; p++) { int cab = 100 + p; if (!C.ports[(size_t)p].empty()) { cab = p; for (int q = 0; q < p; q++) if (C.ports[(size_t)q] == C.ports[(size_t)p]) { cab = q; break; } } thunder_midi_set_port(h, p, !C.ports[(size_t)p].empty(), C.clk[(size_t)p] != 0, C.gear[(size_t)p], cab); }
}
static void drainInto(thunder_midi* h, std::vector<Rec>& out) { Rec buf[512]; for (;;) { const int64_t n = thunder_midi_drain(h, buf, 512); if (n <= 0) break; out.insert(out.end(), buf, buf + n); } }

// ---- THE POSTED PATH: SongRenderer with the hook, thunder_midi_start / _stop posted before the block (what render.h says the host owes) ---------------------------------------------------------------------
// `stopAtQ`: the explicit STOP (posted before that quantum); the 0.3 s song-end stop is the host's rule from the web's frame(). Returns the records of `quanta` quanta.
// verify-p4 finding 7: the explicit STOP really stops the reference: from that quantum on the renderer is not stepped any more (the sequencer is gone, as in the host) and the scheduler is only booked
// (thunder_midi_process: the same begin / end, no step) with the sequencer's last time. Counters of the scheduler are returned in `ms` (the done-test compares starts / stops / ticks / sent too).
static bool postedPath(const mdrive::Case& C, int quanta, int stopAtQ, std::vector<Rec>& out, int* songEndStopQ = nullptr, thunder_midi_stats* ms = nullptr) {
  using namespace mdrive;
  Song song; JsonError je; if (!song.load(C.songText.data(), C.songText.size(), &je, /*pageS*/ true)) return false;
  SeqVars vars; RenderPads pads;
  SeqOpts so; so.once = false; so.live = false; so.sim = false; so.row = C.row; so.randomSeed = 0x2545F491u; if (C.hasLoop) { so.hasLoop = true; so.loopY0 = C.y0; so.loopY1 = C.y1; }
  SongRenderer R(song, vars, pads, SR, 4096, so, 16);
  if (!R.setSeqStart(0.08)) return false;
  thunder_midi* h = thunder_midi_create(SR, nullptr, 0); if (!h) return false;
  struct G { thunder_midi* h; ~G() { thunder_midi_free(h); } } guard{h};
  if (thunder_midi_set_song(h, (const uint8_t*)C.songText.data(), C.songText.size()) != 0) return false;
  setPorts(h, C);
  if (!R.setMidi(h)) return false;
  thunder_midi_start(h, C.row > 0 ? sppFor(song, vars, C.row) : 0);
  std::vector<float> L(128), Rr(128); bool stopped = false, halted = false; double lastSeqT = 0;
  for (int q = 0; q < quanta; q++) {
    if (q == stopAtQ) { thunder_midi_stop(h); halted = true; }
    if (!halted && !stopped && R.songOver() && (double)q * 128 / SR > R.seqTime() + 0.3) { stopped = true; thunder_midi_stop(h); if (songEndStopQ) *songEndStopQ = q; }
    if (halted) { if (thunder_midi_process(h, (double)q * 128 / SR, 128.0 / SR, lastSeqT, 120, 0) < 0) return false; }
    else { R.block(L.data(), Rr.data(), 128); lastSeqT = R.seqTime(); }
    drainInto(h, out);
  }
  if (ms) { std::memset(ms, 0, sizeof *ms); ms->size = sizeof *ms; thunder_midi_stats_get(h, ms); }
  return true;
}

// ---- THE LIVE HOST ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
struct LiveRun {
  Host H; thunder_midi* mh = nullptr; std::vector<Rec> out; int64_t frame = 0; std::vector<float> a, b;
  explicit LiveRun(const thunder_live_opts& o) : H(o) { a.resize(8192); b.resize(8192); if (H.h) mh = H->midi(); }
  LiveHost* operator->() { return H.h; }
  void drain() { if (H.h) { Rec buf[512]; for (;;) { const int64_t n = H->midiDrain(buf, 512); if (n <= 0) break; out.insert(out.end(), buf, buf + n); } } }
  // `n` frames in calls of `cs`; the records are drained after every call (the worklet's rule)
  int run(int64_t n, int cs) { int64_t done = 0; while (done < n) { const int c = (int)std::min<int64_t>(cs, n - done); const int rc = H->process(a.data(), b.data(), c, frame, 0); if (rc) return rc; frame += c; done += c; drain(); } return 0; }
  thunder_live_play_opts po(const mdrive::Case& C) { thunder_live_play_opts p = playOpts(-1); p.row = C.row; if (C.hasLoop) { p.has_loop = 1; p.loop_y0 = C.y0; p.loop_y1 = C.y1; } return p; }
};
static bool livePath(const mdrive::Case& C, int quanta, int stopAtQ, int cs, std::vector<Rec>& out, thunder_live_stats* st = nullptr, std::vector<std::vector<uint8_t>>* evs = nullptr, thunder_midi_stats* ms = nullptr) {
  LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h || !L.mh) return false;
  if (L.H.song(C.songText) != 0) return false;
  setPorts(L.mh, C);
  thunder_live_play_opts p = L.po(C); if (L->play(p) != 0) return false;
  if (cs % 128 != 0 && stopAtQ >= 0) return false;   // (the explicit stop is placed on a quantum boundary: calls of a multiple of 128)
  int q = 0;
  const int step = cs / 128 > 0 ? cs / 128 : 1;
  while (q < quanta) {
    if (stopAtQ >= q && stopAtQ < q + step) { if (L.run((int64_t)(stopAtQ - q) * 128, cs)) return false; q = stopAtQ; if (L->stop(-1) != 0) return false; }
    const int upto = std::min(quanta, stopAtQ > q ? stopAtQ : quanta);
    if (L.run((int64_t)(upto - q) * 128, cs)) return false;
    q = upto;
  }
  out = L.out;
  if (st) *st = stats(L.H.h);
  if (ms) { std::memset(ms, 0, sizeof *ms); ms->size = sizeof *ms; thunder_midi_stats_get(L.mh, ms); }
  if (evs) *evs = L.H.poll();
  return true;
}

// ---- 1: the done-test -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void doneTest() {
  // the 18 songs of test_midi_glue's reference (cases 9 / glue_vs_runBlocks) and the two with a start row / a loop
  const char* all[] = {"two-ports", "mono-notes", "tempo-clock", "echo", "lfo-cc", "bend-fine", "cc-ins", "cc-program", "chan-share", "column-switch", "modes-mixed", "muted-solo", "note-len", "overlap-poly", "slide-arp", "song-end", "velocity", "fx-latency", "start-midway", "loop"};
  const int nAll = (int)(sizeof all / sizeof *all);
  // the sanitizer runs a reduced matrix: the song that ends (the timer's stop), one with clock and notes, one with the start row, one loop
  const char* reduced[] = {"song-end", "tempo-clock", "start-midway", "loop", "mono-notes"};
  const char** list = LM_REDUCED ? reduced : all; const int n = LM_REDUCED ? (int)(sizeof reduced / sizeof *reduced) : nAll;
  int songs = 0; long records = 0; int timerStops = 0, countedStarts = 0;
  for (int i = 0; i < n; i++) {
    mdrive::Case C; if (!mdrive::loadCase(THUNDER_ROOT, list[i], C)) { CHECK(false, "no ledger case %s", list[i]); continue; }
    C.actions.clear();
    // run the song for its ledger length, stop it explicitly 100 ms before the end of the run (a song that ends before has stopped by its own timer: the explicit stop then runs from IDLE), then 30 quanta more
    const int endQ = (int)((int64_t)C.endMs * 48 / 128) + 30, stopQ = std::max(8, (int)((int64_t)C.endMs * 48 / 128) - 40);
    std::vector<Rec> ref, got; int songEndStopQ = -1; thunder_midi_stats rms, gms;
    if (!postedPath(C, endQ, stopQ, ref, &songEndStopQ, &rms)) { CHECK(false, "%s: the posted path does not run", list[i]); continue; }
    thunder_live_stats st; std::memset(&st, 0, sizeof st);
    if (!livePath(C, endQ, stopQ, 128, got, &st, nullptr, &gms)) { CHECK(false, "%s: the live path does not run", list[i]); continue; }
    const int d = firstDiff(ref, got);
    CHECK(d < 0 && !ref.empty(), "%s: the live host's MIDI stream is NOT byte-equal to the posted path (%zu vs %zu records; first difference at %d: posted %s | live %s)", list[i], ref.size(), got.size(), d,
          d >= 0 && (size_t)d < ref.size() ? hexRec(ref[(size_t)d]).c_str() : "(none)", d >= 0 && (size_t)d < got.size() ? hexRec(got[(size_t)d]).c_str() : "(none)");
    CHECK(st.midi_begin_failed == 0 && st.midi_may_hang == 0 && st.seq_dropped == 0, "%s: no midi_begin failure (%u), no hung note (%u), no dropped event", list[i], st.midi_begin_failed, st.midi_may_hang);
    // (c) the scheduler's counters are equal too. NOTE (mutation 'Start both before and after midi_begin'): that mutant is EQUIVALENT - start() reads nothing but its arguments and the port table, and the second call overwrites everything the first set -
    //     so no stream or counter can tell it; the Start BEFORE midi_begin ONLY is killed by the byte comparison and by the 'ports configured after play' check in plumbing().
    CHECK(gms.starts == rms.starts && gms.stops == rms.stops && gms.ticks == rms.ticks && gms.sent == rms.sent,
          "%s: scheduler counters: starts %llu/%llu (posted/live), stops %llu/%llu, ticks %llu/%llu, sent %llu/%llu", list[i], (unsigned long long)rms.starts, (unsigned long long)gms.starts, (unsigned long long)rms.stops, (unsigned long long)gms.stops, (unsigned long long)rms.ticks, (unsigned long long)gms.ticks, (unsigned long long)rms.sent, (unsigned long long)gms.sent);
    if (songEndStopQ >= 0) timerStops++;
    if (rms.starts > 0) countedStarts++;
    records += (long)got.size(); songs++;
    std::printf("  %-14s %5zu records %s%s\n", list[i], got.size(), d < 0 ? "BYTE-EQUAL" : "DIFFERENT", songEndStopQ >= 0 ? " (the song ended: the 0.3 s stop ran)" : "");
  }
  std::printf("1 done-test: %d songs, %ld records byte-equal to the posted path; %d of them ended by themselves and stopped by the 0.3 s timer\n", songs, records, timerStops);
  CHECK(countedStarts >= 1, "at least one song has a clock port, whose Start the scheduler COUNTS (stats.starts): the counter check can see a doubled Start");
  CHECK(timerStops >= 1, "at least one song ended and stopped by the timer (the 0.3 s rule is exercised)");
  // the same with another call size (the quantum structure is the host's, not the caller's): three songs, calls of 4 and of 32 quanta
  if (!LM_REDUCED) for (const char* nm : {"song-end", "tempo-clock", "echo"}) for (int cs : {512, 4096}) {
    mdrive::Case C; if (!mdrive::loadCase(THUNDER_ROOT, nm, C)) continue; C.actions.clear();
    const int endQ = (int)((int64_t)C.endMs * 48 / 128) + 32, stopQ = std::max(32, (int)((int64_t)C.endMs * 48 / 128) - 32) / 32 * 32;
    std::vector<Rec> ref, got; if (!postedPath(C, endQ, stopQ, ref) || !livePath(C, endQ / 32 * 32, stopQ, cs, got)) { CHECK(false, "%s: call size %d does not run", nm, cs); continue; }
    // (the live run is cut at a multiple of 32 quanta: compare the common prefix of the records whose frame lies before that cut)
    const int64_t cut = (int64_t)(endQ / 32 * 32) * 128; std::vector<Rec> r2; for (const Rec& r : ref) if (r.frame < cut - 2 * 48000) r2.push_back(r);
    std::vector<Rec> g2; for (const Rec& r : got) if (r.frame < cut - 2 * 48000) g2.push_back(r);
    const int d = firstDiff(r2, g2); CHECK(d < 0 && !r2.empty(), "%s at call size %d: the stream differs from the posted path at record %d (%zu vs %zu)", nm, cs, d, r2.size(), g2.size());
  }
}

// ---- 1b: a STOP WHILE PLAYING (verify-p4 findings 7, 8) -----------------------------------------------------------------------------------------------------------------------------------------------------
// The stop falls INSIDE the song (quantum 20 is before the sequencer's first step, 61 and 150 are in it), the reference really stops (postedPath), the streams are byte-equal. lfo-cc is the song whose reference used to keep
// emitting CCs after the Stop.
static void stopWhilePlaying() {
  const char* names[] = {"lfo-cc", "tempo-clock", "two-ports", "echo", "slide-arp", "mono-notes"};
  const char* redNames[] = {"lfo-cc", "echo"};
  const char** L = LM_REDUCED ? redNames : names; const int n = LM_REDUCED ? 2 : (int)(sizeof names / sizeof *names);
  const int stops[] = {20, 61, 150};
  int cases = 0; long recs = 0;
  for (int i = 0; i < n; i++) for (int sq : stops) {
    mdrive::Case C; if (!mdrive::loadCase(THUNDER_ROOT, L[i], C)) { CHECK(false, "no case %s", L[i]); continue; } C.actions.clear();
    const int endQ = sq + 60; std::vector<Rec> ref, got; thunder_midi_stats rms, gms;
    if (!postedPath(C, endQ, sq, ref, nullptr, &rms) || !livePath(C, endQ, sq, 128, got, nullptr, nullptr, &gms)) { CHECK(false, "%s stop at %d: does not run", L[i], sq); continue; }
    const int d = firstDiff(ref, got);
    CHECK(d < 0 && !ref.empty(), "%s: a stop at quantum %d (while playing) differs from the posted path at record %d (%zu vs %zu: posted %s | live %s)", L[i], sq, d, ref.size(), got.size(),
          d >= 0 && (size_t)d < ref.size() ? hexRec(ref[(size_t)d]).c_str() : "(none)", d >= 0 && (size_t)d < got.size() ? hexRec(got[(size_t)d]).c_str() : "(none)");
    CHECK(gms.starts == rms.starts && gms.stops == rms.stops && gms.sent == rms.sent, "%s stop %d: counters differ", L[i], sq);
    cases++; recs += (long)got.size();
  }
  std::printf("1b stop while playing: %d cases, %ld records byte-equal to a posted path that really stops\n", cases, recs);
}

// ---- 1c: NON-ZERO FX LATENCY (verify-p4 finding 8): ELECTRO with every instrument OUTPUT 2 on port A (clock on), effects + master chain + PDC, against SongRenderer with the same setup ----------------------------------------------------
static void fxLatency() {
  std::string text = electro(); for (size_t at = 0; (at = text.find("\"mo\":0", at)) != std::string::npos; at += 6) text.replace(at, 6, "\"mo\":2");
  const int NQ = LM_REDUCED ? 260 : 600, stopQ = LM_REDUCED ? 200 : 450;
  for (int pdc : {1, 2}) {
    if (LM_REDUCED && pdc == 2) break;
    std::vector<Rec> ref, got; int64_t latRef = 0;
    { Song song; JsonError je; if (!song.load(text.data(), text.size(), &je, true)) { CHECK(false, "ELECTRO does not load"); return; }
      SeqVars vars; RenderPads pads; SeqOpts so; so.once = false; so.live = false; so.randomSeed = 0x2545F491u;
      SongRenderer R(song, vars, pads, SR, 4096, so, 64); R.setSeqStart(0.08); R.setEffects(true); R.setMasterChain(master_chain_from_song(song.mst, song.limiter));
      { const PdcMode mode = pdc == 2 ? PdcMode::On : PdcMode::Auto; std::vector<PdcSlot> slots(1024); const int ns = R.fxBank() ? R.fxBank()->pdcSlots(slots.data(), (int)slots.size()) : 0; R.setPlan(pdc_plan(bus_plan(song.chainPatch, song.chainOn), slots.data(), ns, SR, mode), mode, 0); }
      thunder_midi* h = thunder_midi_create(SR, nullptr, 0); thunder_midi_set_song(h, (const uint8_t*)text.data(), text.size()); thunder_midi_set_port(h, 0, 1, 1, 0, 0); R.setMidi(h); thunder_midi_start(h, 0);
      std::vector<float> a(128), b(128); bool halted = false; double lastT = 0;
      for (int q = 0; q < NQ; q++) { if (q == stopQ) { thunder_midi_stop(h); halted = true; } if (halted) thunder_midi_process(h, (double)q * 128 / SR, 128.0 / SR, lastT, 120, 0); else { R.block(a.data(), b.data(), 128); lastT = R.seqTime(); } drainInto(h, ref); }
      latRef = R.latencyFrames(); thunder_midi_free(h); }
    { LiveRun L(opts(THUNDER_LIVE_F_MIDI | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER, 4096)); if (!L.H.h) return;
      CHECK(L.H.song(text) == 0, "song"); thunder_midi_set_port(L.mh, 0, 1, 1, 0, 0);
      thunder_live_play_opts p = playOpts(-1); p.pdc_mode = pdc; CHECK(L->play(p) == 0, "play");
      CHECK(L.run((int64_t)stopQ * 128, 128) == 0, "run"); const int latLive = stats(L.H.h).latency_frames; CHECK(L->stop(-1) == 0, "stop"); CHECK(L.run((int64_t)(NQ - stopQ) * 128, 128) == 0, "run after");
      got = L.out;
      CHECK(latRef > 0 && latLive == (int)latRef, "FX latency in force: renderer %lld frames, live %d (non-zero)", (long long)latRef, latLive); }
    const int d = firstDiff(ref, got);
    CHECK(d < 0 && ref.size() > 20, "ELECTRO with effects, master and PDC mode %d (latency %lld frames): the MIDI stream differs from SongRenderer's at record %d (%zu vs %zu: %s | %s)", pdc, (long long)latRef, d, ref.size(), got.size(),
          d >= 0 && (size_t)d < ref.size() ? hexRec(ref[(size_t)d]).c_str() : "(none)", d >= 0 && (size_t)d < got.size() ? hexRec(got[(size_t)d]).c_str() : "(none)");
    std::printf("1c fx latency (PDC mode %d, %lld frames): %zu records %s\n", pdc, (long long)latRef, got.size(), d < 0 ? "BYTE-EQUAL" : "DIFFERENT");
  }
}

// ---- 1d: a refused publish leaves the scheduler on the song the host still holds (verify-p4 finding 1) --------------------------------------------------------------------------------------------------------------------
static void refusedPublish() {
  mdrive::Case C; if (!mdrive::loadCase(THUNDER_ROOT, "mono-notes", C)) { CHECK(false, "no mono-notes"); return; } C.actions.clear();
  std::string B = C.songText; for (size_t at = 0; (at = B.find("\"mch\":0", at)) != std::string::npos; at += 7) B.replace(at, 7, "\"mch\":9");
  CHECK(B != C.songText, "song B differs from A in its MIDI channel");
  auto channels = [](const std::vector<Rec>& v, int& ch0, int& ch9) { ch0 = ch9 = 0; for (const Rec& r : v) if (!(r.flags & THUNDER_MIDI_F_CLEAR) && (r.b[0] & 0xF0) == 0x90) { if ((r.b[0] & 15) == 9) ch9++; else ch0++; } };
  { // (i) the host's command ring is full: the publish of B is E_BUSY, a play afterwards plays A with A's MIDI fields
    LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return;
    CHECK(L.H.song(C.songText) == 0, "song A"); setPorts(L.mh, C); CHECK(L.run(128 * 4, 128) == 0, "run");
    int busy = 0; for (int i = 0; i < 1000 && !busy; i++) if (L->bpm(120, -1) == THUNDER_LIVE_E_BUSY) busy++;
    CHECK(busy == 1, "the command ring was filled");
    CHECK(L.H.song(B) == THUNDER_LIVE_E_BUSY, "publish B with a full command ring: E_BUSY");
    CHECK(L.run(128 * 40, 128) == 0, "run"); CHECK(L->play(L.po(C)) == 0, "play"); CHECK(L.run(128 * 400, 128) == 0, "run");
    int ch0, ch9; channels(L.out, ch0, ch9); CHECK(ch0 > 0 && ch9 == 0, "after the refused publish song A plays with ITS channel (%d on channel 1, %d on channel 10)", ch0, ch9); }
  { // (ii) the control: the publish succeeds, B's MIDI fields are the ones heard
    LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return;
    CHECK(L.H.song(C.songText) == 0, "song A"); setPorts(L.mh, C); CHECK(L.run(128 * 4, 128) == 0, "run"); CHECK(L.H.song(B) == 0, "publish B"); CHECK(L.run(128 * 40, 128) == 0, "run");
    CHECK(L->play(L.po(C)) == 0, "play"); CHECK(L.run(128 * 400, 128) == 0, "run"); int ch0, ch9; channels(L.out, ch0, ch9); CHECK(ch9 > 0 && ch0 == 0, "a successful publish: B's channel (%d, %d)", ch0, ch9); }
  { // (iii) a publish refused by the SCHEDULER (six songs in flight, no quantum between): nothing changed on either side
    LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return;
    int rcs[10]; for (int i = 0; i < 10; i++) rcs[i] = L.H.song(i == 9 ? B : C.songText);
    int ok = 0, busy = 0; for (int i = 0; i < 10; i++) { if (rcs[i] == 0) ok++; else if (rcs[i] == THUNDER_LIVE_E_BUSY) busy++; }
    CHECK(ok == 6 && busy == 4 && rcs[6] == THUNDER_LIVE_E_BUSY, "six publishes with no quantum between are accepted, the next ones are E_BUSY (%d accepted, %d busy)", ok, busy);
    setPorts(L.mh, C); CHECK(L->play(L.po(C)) == 0, "play"); CHECK(L.run(128 * 400, 128) == 0, "run"); int ch0, ch9; channels(L.out, ch0, ch9);
    CHECK(ch0 > 0 && ch9 == 0, "the refused 10th publish (song B) changed nothing: A's channel (%d, %d)", ch0, ch9); }
}

// ---- 2: the state machine -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static int count(const std::vector<Rec>& v, uint8_t b0, int64_t from = 0, int64_t to = INT64_MAX) { int n = 0; for (const Rec& r : v) if (!(r.flags & THUNDER_MIDI_F_CLEAR) && r.b[0] == b0 && r.frame >= from && r.frame < to) n++; return n; }
static int countClear(const std::vector<Rec>& v) { int n = 0; for (const Rec& r : v) if (r.flags & THUNDER_MIDI_F_CLEAR) n++; return n; }

static void stateMachine() {
  mdrive::Case C; if (!mdrive::loadCase(THUNDER_ROOT, "loop", C)) { CHECK(false, "no loop"); return; }   // (a looping song: it has pattern edges to hold at and does not end by itself)
  C.actions.clear();
  // ---- HELD keeps MIDI running; a later stop stops it; stop from IDLE repeats the stop ----
  { LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return;
    CHECK(L.H.song(C.songText) == 0, "song"); setPorts(L.mh, C);
    thunder_live_play_opts p = L.po(C); CHECK(L->play(p) == 0, "play");
    CHECK(L.run(128 * 200, 512) == 0, "run");   // 0.53 s
    CHECK(count(L.out, 0xFA) == 1 && count(L.out, 0xF8) > 10, "playing: one Start, the clock ticks (%d, %d)", count(L.out, 0xFA), count(L.out, 0xF8));
    CHECK(L->hold(-1) == 0, "hold");
    int64_t heldAt = -1; int guard = 0;
    while (heldAt < 0 && guard++ < 400) { CHECK(L.run(128 * 40, 512) == 0, "run to the edge"); const thunder_live_stats s = stats(L.H.h); if (s.state == THUNDER_LIVE_STATE_HELD) heldAt = (int64_t)s.frames; }
    CHECK(heldAt >= 0, "EXACT STOP reached the pattern edge (HELD)");
    const size_t nAtHeld = L.out.size(); const int ticksAtHeld = count(L.out, 0xF8);
    CHECK(L.run(128 * 200, 512) == 0, "run while HELD");
    CHECK(count(L.out, 0xFC) == 0 && countClear(L.out) == 0, "HELD: no Stop, no CLEAR");
    // (the scheduler books its clock only while the sequencer is alive: it stops at the held edge; what matters is that nothing STOPPED it)
    CHECK(L.out.size() >= nAtHeld && count(L.out, 0xF8) >= ticksAtHeld, "HELD: nothing was taken back");
    CHECK(L->stop(-1) == 0, "stop"); CHECK(L.run(128 * 20, 512) == 0, "run after the stop");
    CHECK(count(L.out, 0xFC) >= 1 && countClear(L.out) >= 1, "stop from HELD: Stop + CLEAR (%d, %d)", count(L.out, 0xFC), countClear(L.out));
    const int stops1 = count(L.out, 0xFC), clears1 = countClear(L.out);
    CHECK(L->stop(-1) == 0, "stop again"); CHECK(L.run(128 * 20, 512) == 0, "run after the second stop");   // from IDLE
    CHECK(countClear(L.out) > clears1 && count(L.out, 0xFC) >= stops1, "stop from IDLE repeats the MIDI stop: the scheduler's stop ran again (CLEARs %d -> %d, Stops %d -> %d)", clears1, countClear(L.out), stops1, count(L.out, 0xFC));
    const thunder_live_stats s = stats(L.H.h); CHECK(s.state == THUNDER_LIVE_STATE_IDLE && s.midi_begin_failed == 0, "IDLE at the end"); }
  // ---- play over play, and stop then play in ONE quantum keep the order of the commands: Stop, then Start ----
  { LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return;
    CHECK(L.H.song(C.songText) == 0, "song"); setPorts(L.mh, C);
    thunder_live_play_opts p = L.po(C); CHECK(L->play(p) == 0, "play");
    CHECK(L.run(128 * 100, 128) == 0, "run");
    const size_t before = L.out.size(); (void)before;
    CHECK(L->stop(-1) == 0 && L->play(p) == 0, "stop + play in one go");
    CHECK(L.run(128 * 100, 128) == 0, "run after");
    // the records of the quantum of the stop and play: ... FC (Stop), then FA (Start) later in the stream
    int iStop = -1, iStart2 = -1, starts = 0; for (size_t i = 0; i < L.out.size(); i++) { if (L.out[i].flags & THUNDER_MIDI_F_CLEAR) continue; if (L.out[i].b[0] == 0xFC && iStop < 0) iStop = (int)i; if (L.out[i].b[0] == 0xFA) { starts++; if (starts == 2) iStart2 = (int)i; } }
    CHECK(iStop >= 0 && starts == 2 && iStart2 > iStop, "a second Start after the Stop (Stop at record %d, second Start at %d, %d Starts)", iStop, iStart2, starts);
    CHECK(L.out[(size_t)iStart2].frame >= L.out[(size_t)iStop].frame, "the second Start is not before the Stop in time");
    // a play WITHOUT a stop (play over play: the page stops first, but the core must not break): two Starts, no Stop between, no failure
    CHECK(L->play(p) == 0, "play over play"); CHECK(L.run(128 * 100, 128) == 0, "run");
    const thunder_live_stats s = stats(L.H.h); CHECK(s.midi_begin_failed == 0 && s.seq_dropped == 0, "no failure"); }
  // ---- the ENDED timer: the MIDI stop comes 0.3 s after the song's end, with the STATE records ENDED then STOPPED arg 1 ----
  { mdrive::Case E; if (!mdrive::loadCase(THUNDER_ROOT, "song-end", E)) { CHECK(false, "no song-end"); return; } E.actions.clear();
    LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return;
    CHECK(L.H.song(E.songText) == 0, "song"); setPorts(L.mh, E);
    thunder_live_play_opts p = L.po(E); CHECK(L->play(p) == 0, "play");
    CHECK(L.run((int64_t)E.endMs * 48 + 128 * 80, 128) == 0, "run");
    const std::vector<std::vector<uint8_t>> evs = L.H.poll(); int64_t endedF = -1, stoppedF = -1;
    for (const auto& r : evs) if (evType(r) == THUNDER_LIVE_EV_STATE) { const thunder_live_ev_state e = evAs<thunder_live_ev_state>(r); if (e.state == THUNDER_LIVE_ST_ENDED) endedF = evF(r); if (e.state == THUNDER_LIVE_ST_STOPPED && e.arg == 1) stoppedF = evF(r); }
    CHECK(endedF >= 0 && stoppedF >= 0, "ENDED and STOPPED(1) records came (%lld, %lld)", (long long)endedF, (long long)stoppedF);
    int64_t stopFrame = -1; for (const Rec& r : L.out) if (!(r.flags & THUNDER_MIDI_F_CLEAR) && r.b[0] == 0xFC) { stopFrame = r.frame; break; }
    CHECK(stopFrame >= 0, "the song ended and the MIDI Stop came on its own");
    if (stopFrame >= 0 && endedF >= 0) { const double dt = (double)(stopFrame - endedF) / SR; CHECK(dt > 0.25 && dt < 0.5, "the MIDI Stop is %.3f s after the song's end (0.3 s + one quantum + the Gear / FX figures)", dt); } }
}

// ---- 3: the lead ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static int64_t maxFrameAfter(double lead) {
  mdrive::Case C; if (!mdrive::loadCase(THUNDER_ROOT, "tempo-clock", C)) return -1; C.actions.clear();
  LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return -1;
  if (L.H.song(C.songText) != 0) return -1; setPorts(L.mh, C);
  if (L->midiLead(lead) != 0 || L->lead(1.2, -1) != 0) return -1;   // (the sequencer looks 1.2 s ahead in both runs: only the outbox's look-ahead differs)
  thunder_live_play_opts p = L.po(C); if (L->play(p) != 0) return -1;
  if (L.run(128 * 120, 128) != 0) return -1;   // 0.32 s of song: the Seq starts at 0.08 s
  int64_t m = -1; for (const Rec& r : L.out) m = std::max(m, r.frame); return m;
}
static void leadTest() {
  { Host H(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!H.h) return;
    CHECK(H->midiLead(0.005) == 0 && H->midiLead(0.5) == 0 && H->midiLead(1.2) == 0, "midiLead 0.005 / 0.5 / 1.2 accepted");
    CHECK(H->midiLead(1.2000001) == THUNDER_LIVE_E_ARG && H->midiLead(1.21) == THUNDER_LIVE_E_ARG && H->midiLead(0.0049) == THUNDER_LIVE_E_ARG && H->midiLead(0) == THUNDER_LIVE_E_ARG
          && H->midiLead(NAN) == THUNDER_LIVE_E_ARG && H->midiLead(INFINITY) == THUNDER_LIVE_E_ARG, "midiLead outside 0.005 .. 1.2 or not finite: E_ARG");
    // the command ring (256): full is E_BUSY, and a block empties it
    int busy = 0; for (int i = 0; i < 400; i++) if (H->midiLead(0.06) == THUNDER_LIVE_E_BUSY) busy++;
    CHECK(busy > 0, "a full midi command ring is E_BUSY (%d)", busy);
    std::vector<float> a(128), b(128); CHECK(H->process(a.data(), b.data(), 128, 0, 0) == 0 && H->midiLead(0.06) == 0, "room after a block"); }
  { Host H(opts(0, 4096)); if (!H.h) return;
    CHECK(H->midi() == nullptr && H->midiLead(0.06) == THUNDER_LIVE_E_STATE, "without F_MIDI: no scheduler, midiLead is E_STATE");
    thunder_midi_msg m[4]; CHECK(H->midiDrain(m, 4) == THUNDER_LIVE_E_STATE, "without F_MIDI: midiDrain is E_STATE");
    const std::string text = electro(); CHECK(H.song(text) == 0, "song"); thunder_live_play_opts p = playOpts(-1); CHECK(H->play(p) == 0, "play"); int64_t fr = 0; CHECK(H.run(128 * 40, 128, fr) == 0, "a host without F_MIDI plays");
  }
  const int64_t d = maxFrameAfter(0.06), w = maxFrameAfter(1.2);
  CHECK(d > 0 && w > d + (int64_t)(0.8 * SR), "a 1.2 s lead books further ahead than the default 0.06 s: the last record at frame %lld vs %lld", (long long)w, (long long)d);
}

// ---- 4: plumbing ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void plumbing() {
  // spp from `row` (midi_spp_for) is what the Start carries; an explicit spp wins
  mdrive::Case C; if (!mdrive::loadCase(THUNDER_ROOT, "start-midway", C)) { CHECK(false, "no start-midway"); return; } C.actions.clear();
  Song song; JsonError je; CHECK(song.load(C.songText.data(), C.songText.size(), &je, true), "song loads"); SeqVars vars;
  const int want = std::max(0, midi_spp_for(song, vars, C.row));
  CHECK(want > 0, "the start row has a Song Position (%d)", want);
  for (int explicitSpp : {-1, 7}) {
    LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return;
    CHECK(L.H.song(C.songText) == 0, "song"); setPorts(L.mh, C);
    thunder_live_play_opts p = L.po(C); p.spp = explicitSpp; CHECK(L->play(p) == 0, "play");
    CHECK(L.run(128 * 30, 128) == 0, "run");
    int spp = -1; for (const Rec& r : L.out) if (!(r.flags & THUNDER_MIDI_F_CLEAR) && r.b[0] == 0xF2) { spp = r.b[1] | (r.b[2] << 7); break; }
    CHECK(spp == (explicitSpp < 0 ? want : explicitSpp), "the Start's SPP is %d (spp argument %d, row %d: %d)", spp, explicitSpp, C.row, want);
    const thunder_live_stats s = stats(L.H.h); CHECK(s.midi_begin_failed == 0, "no midi_begin failure"); }
  // a port configured AFTER play(): the Start is applied after midi_begin has popped it, so the SPP is honoured (a Start issued before midi_begin would find no clock port and send none: verify-p4 probe C)
  { LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return; mdrive::Case T; if (!mdrive::loadCase(THUNDER_ROOT, "tempo-clock", T)) { CHECK(false, "no tempo-clock"); return; } T.actions.clear();
    CHECK(L.H.song(T.songText) == 0, "song"); thunder_live_play_opts p = L.po(T); p.spp = 8; CHECK(L->play(p) == 0, "play"); setPorts(L.mh, T); CHECK(L.run(128 * 40, 128) == 0, "run");
    int spp = -1; for (const Rec& r : L.out) if (!(r.flags & THUNDER_MIDI_F_CLEAR) && r.b[0] == 0xF2) { spp = r.b[1] | (r.b[2] << 7); break; }
    CHECK(spp == 8, "ports set after play(): the Start still carries its SPP (%d)", spp); }
  // a song that is no song the scheduler can read: E_SONG, nothing published (the host's own parse refuses it first)
  { Host H(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!H.h) return; const std::string bad = "not a song"; CHECK(H.song(bad) == THUNDER_LIVE_E_SONG, "a bad song is E_SONG"); }
  // the host owns its scheduler; a second host has its own
  { Host A(opts(THUNDER_LIVE_F_MIDI, 4096)), B(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!A.h || !B.h) return; CHECK(A->midi() && B->midi() && A->midi() != B->midi(), "each host owns its thunder_midi"); }
  // destroy while playing: the host owes one quantum (Stop + CLEAR); nothing leaks (ASan / the harness) and the scheduler is stopped
  { LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return; CHECK(L.H.song(C.songText) == 0, "song"); setPorts(L.mh, C); thunder_live_play_opts p = L.po(C); CHECK(L->play(p) == 0, "play"); CHECK(L.run(128 * 30, 128) == 0, "run"); }
}

int main() {
  doneTest();
  stopWhilePlaying();
  fxLatency();
  refusedPublish();
  stateMachine();
  leadTest();
  plumbing();
  std::printf("live_midi: %d checks, %d failed%s\n", g_checks, g_fail, LM_REDUCED ? " (reduced matrix: sanitizer build)" : "");
  return g_fail ? 1 : 0;
}
