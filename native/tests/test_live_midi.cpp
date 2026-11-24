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
#include "midi/midi_spp.h"
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
static bool postedPath(const mdrive::Case& C, int quanta, int stopAtQ, std::vector<Rec>& out, int* songEndStopQ = nullptr) {
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
  std::vector<float> L(128), Rr(128); bool stopped = false;
  for (int q = 0; q < quanta; q++) {
    if (q == stopAtQ) thunder_midi_stop(h);
    if (!stopped && R.songOver() && (double)q * 128 / SR > R.seqTime() + 0.3) { stopped = true; thunder_midi_stop(h); if (songEndStopQ) *songEndStopQ = q; }
    R.block(L.data(), Rr.data(), 128);
    drainInto(h, out);
  }
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
static bool livePath(const mdrive::Case& C, int quanta, int stopAtQ, int cs, std::vector<Rec>& out, thunder_live_stats* st = nullptr, std::vector<std::vector<uint8_t>>* evs = nullptr) {
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
  int songs = 0; long records = 0; int timerStops = 0;
  for (int i = 0; i < n; i++) {
    mdrive::Case C; if (!mdrive::loadCase(THUNDER_ROOT, list[i], C)) { CHECK(false, "no ledger case %s", list[i]); continue; }
    C.actions.clear();
    // run the song for its ledger length, stop it explicitly 100 ms before the end of the run (a song that ends before has stopped by its own timer: the explicit stop then runs from IDLE), then 30 quanta more
    const int endQ = (int)((int64_t)C.endMs * 48 / 128) + 30, stopQ = std::max(8, (int)((int64_t)C.endMs * 48 / 128) - 40);
    std::vector<Rec> ref, got; int songEndStopQ = -1;
    if (!postedPath(C, endQ, stopQ, ref, &songEndStopQ)) { CHECK(false, "%s: the posted path does not run", list[i]); continue; }
    thunder_live_stats st; std::memset(&st, 0, sizeof st);
    if (!livePath(C, endQ, stopQ, 128, got, &st)) { CHECK(false, "%s: the live path does not run", list[i]); continue; }
    const int d = firstDiff(ref, got);
    CHECK(d < 0 && !ref.empty(), "%s: the live host's MIDI stream is NOT byte-equal to the posted path (%zu vs %zu records; first difference at %d: posted %s | live %s)", list[i], ref.size(), got.size(), d,
          d >= 0 && (size_t)d < ref.size() ? hexRec(ref[(size_t)d]).c_str() : "(none)", d >= 0 && (size_t)d < got.size() ? hexRec(got[(size_t)d]).c_str() : "(none)");
    CHECK(st.midi_begin_failed == 0 && st.midi_may_hang == 0 && st.seq_dropped == 0, "%s: no midi_begin failure (%u), no hung note (%u), no dropped event", list[i], st.midi_begin_failed, st.midi_may_hang);
    thunder_midi_stats ms; (void)ms;
    if (songEndStopQ >= 0) timerStops++;
    records += (long)got.size(); songs++;
    std::printf("  %-14s %5zu records %s%s\n", list[i], got.size(), d < 0 ? "BYTE-EQUAL" : "DIFFERENT", songEndStopQ >= 0 ? " (the song ended: the 0.3 s stop ran)" : "");
  }
  std::printf("1 done-test: %d songs, %ld records byte-equal to the posted path; %d of them ended by themselves and stopped by the 0.3 s timer\n", songs, records, timerStops);
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
  // a song that is no song the scheduler can read: E_SONG, nothing published (the host's own parse refuses it first)
  { Host H(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!H.h) return; const std::string bad = "not a song"; CHECK(H.song(bad) == THUNDER_LIVE_E_SONG, "a bad song is E_SONG"); }
  // the host owns its scheduler; a second host has its own
  { Host A(opts(THUNDER_LIVE_F_MIDI, 4096)), B(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!A.h || !B.h) return; CHECK(A->midi() && B->midi() && A->midi() != B->midi(), "each host owns its thunder_midi"); }
  // destroy while playing: the host owes one quantum (Stop + CLEAR); nothing leaks (ASan / the harness) and the scheduler is stopped
  { LiveRun L(opts(THUNDER_LIVE_F_MIDI, 4096)); if (!L.H.h) return; CHECK(L.H.song(C.songText) == 0, "song"); setPorts(L.mh, C); thunder_live_play_opts p = L.po(C); CHECK(L->play(p) == 0, "play"); CHECK(L.run(128 * 30, 128) == 0, "run"); }
}

int main() {
  doneTest();
  stateMachine();
  leadTest();
  plumbing();
  std::printf("live_midi: %d checks, %d failed%s\n", g_checks, g_fail, LM_REDUCED ? " (reduced matrix: sanitizer build)" : "");
  return g_fail ? 1 : 0;
}
