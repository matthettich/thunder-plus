// U35: MidiSched's own rules, beyond the web's ledgers: cadence independence (the live core's blocks give the web pump's stream), B7 (a stalled host: late ticks
// dropped and counted, no burst, the phase kept), B4 (per-track ownership, the per-pitch count), B5 / B6 (STOP forgets), B8, B9, the shared lateness test, and
// hostile input to the C++ entry points.
#include <cstdio>

#include "midi_drive.h"

using namespace mdrive;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static std::string root() { return THUNDER_ROOT; }

// ---- 1. cadence independence ---------------------------------------------------------------------------------------------------------
static void cadence() {
  // the cases where the stream does not depend on when the host happens to book (no song end in the window, no live tempo change)
  const char* names[] = {"mono-notes", "note-len", "overlap-poly", "chan-share", "velocity", "cc-program", "cc-ins", "slide-arp", "bend-fine", "modes-mixed", "column-switch", "echo", "lfo-cc", "muted-solo", "tempo-clock", "start-midway", "loop", "start-stop"};
  for (const char* n : names) {
    Case C; if (!loadCase(root(), n, C)) { CHECK(false, "%s: no case", n); continue; }
    Drive A, B, B2; run(A, C); runBlocks(B, C, 96); runBlocks(B2, C, 480);   // (blocks that divide the ms grid: the song starts at the first block >= playAt)
    for (Drive* X : {&B, &B2}) {
      // same messages, same times (one frame), as a multiset per (port, bytes); the Stop's frame may differ by the 17 ms song-end poll: ignore kind STOP / RELEASE after the end
      std::vector<Out> a = A.kept, b = X->kept; int miss = 0, extra = 0;
      std::vector<char> used(b.size(), 0);
      for (const Out& o : a) {
        int best = -1; int64_t bd = 1 << 30;
        for (size_t j = 0; j < b.size(); j++) { if (used[j] || b[j].cab != o.cab || b[j].len != o.len || std::memcmp(b[j].b, o.b, (size_t)o.len)) continue; const int64_t d = std::llabs(b[j].frame - o.frame); if (d <= 1 && d < bd) { bd = d; best = (int)j; } }
        if (best < 0) { if (o.kind != MK_STOP && o.kind != MK_RELEASE) miss++; } else used[(size_t)best] = 1;
      }
      for (size_t j = 0; j < b.size(); j++) if (!used[j] && b[j].kind != MK_STOP && b[j].kind != MK_RELEASE) extra++;
      CHECK(miss == 0 && extra == 0, "cadence %s (block %d): %d messages of the 25 ms pump missing, %d extra", n, X == &B ? 96 : 480, miss, extra);
    }
  }
  std::printf("cadence: ok\n");
}

// ---- a hand rig: one Song with MIDI instruments, a scheduler, events built by hand ---------------------------------------------------------
struct Rig {
  Song song; MidiSched s{48000}; std::vector<Out> out; double sr = 48000;
  Rig() { song.insts[0].mo = 1; song.insts[1].mo = 1; song.insts[1].mch = 0; s.setSong(&song); s.setPort(0, true, false, 0, 0); s.setPort(1, true, true, 0, 1); s.setFxLatency(0); }
  void drain() { MidiMsg b[512]; for (;;) { const int64_t n = s.drain(b, 512); if (n <= 0) break; for (int64_t i = 0; i < n; i++) { if (b[i].flags & MIDI_F_CLEAR) { for (size_t k = 0; k < out.size();) if (out[k].cab == b[i].cable && out[k].frame > b[i].frame) out.erase(out.begin() + (long)k); else k++; continue; } Out o; o.frame = b[i].frame; o.port = b[i].port; o.len = b[i].len; std::memcpy(o.b, b[i].b, 3); o.kind = b[i].kind; o.cab = b[i].cable; out.push_back(o); } } }
  void book(double now) { s.book(now, now, 120, false); drain(); }
  static SeqEvent note(int track, int ii, double mn, double t, bool poly = false, int col = 0) { SeqEvent e; e.type = SEV_NOTE; e.track = track; e.ii = ii; e.mn = mn; e.vel = 1; e.t = t; e.poly = poly; e.col = col; return e; }
  static SeqEvent off(int track, double t) { SeqEvent e; e.type = SEV_OFF; e.track = track; e.t = t; return e; }
  int count(int status, int note = -1, int port = -1) const { int n = 0; for (const Out& o : out) if ((o.b[0] & 0xF0) == status && (note < 0 || o.b[1] == note) && (port < 0 || o.port == port) && (status != 0x90 || o.b[2] > 0)) n++; return n; }
  double msOf(const Out& o) const { return o.frame * 1000.0 / sr; }
};

// ---- B4: held notes are owned per TRACK; a per port / channel / pitch count -------------------------------------------------------------------
static void ownership() {
  { Rig r;   // two tracks, one port + channel, the same pitch
    r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, false, 0.95); r.s.onEvent(Rig::note(1, 0, 60, 1.1), nullptr, 120, false, 0.95);
    r.s.onEvent(Rig::off(0, 1.2), nullptr, 120, false, 0.95); r.s.onEvent(Rig::off(1, 1.3), nullptr, 120, false, 0.95);
    r.book(0.95); r.book(1.05); r.book(1.15); r.book(1.25); r.book(1.35);
    int on = 0, offs = 0; std::vector<double> offT; for (const Out& o : r.out) { if ((o.b[0] & 0xF0) == 0x90) on++; else if ((o.b[0] & 0xF0) == 0x80) { offs++; offT.push_back(r.msOf(o)); } }
    CHECK(on == 2, "B4: two note-ons, got %d", on);
    CHECK(offs == 2, "B4: a retrigger OFF and the last holder's OFF (2), got %d", offs);
    CHECK(offT.size() == 2 && std::fabs(offT[0] - 1100) < 0.05 && std::fabs(offT[1] - 1300) < 0.05, "B4: the OFFs are at 1100 (the retrigger) and 1300 (the last holder): %.3f %.3f", offT.size() > 0 ? offT[0] : -1, offT.size() > 1 ? offT[1] : -1);
    CHECK(r.s.stats().collisions == 1, "B4: one shared-channel collision counted, got %d", (int)r.s.stats().collisions);
    CHECK(r.s.stats().offsSuppressed == 1, "B4: the first holder's OFF is suppressed (counted), got %d", (int)r.s.stats().offsSuppressed);
    CHECK(r.s.pitchCount(0, 0, 60) == 0, "B4: the count is 0 at the end, got %d", r.s.pitchCount(0, 0, 60)); }
  { Rig r;   // a mono track's takeover ends only ITS notes
    r.s.onEvent(Rig::note(1, 0, 62, 1.0), nullptr, 120, false, 0.95); r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, false, 0.95);
    r.s.onEvent(Rig::note(0, 0, 64, 1.2), nullptr, 120, false, 0.95);
    r.book(0.95); r.book(1.1); r.book(1.15); r.book(1.25);
    CHECK(r.count(0x80, 60) == 1, "B4: track 0's takeover ends its own 60");
    CHECK(r.count(0x80, 62) == 0, "B4: track 0's takeover must not end track 1's 62 (got %d offs)", r.count(0x80, 62)); }
  { Rig r;   // the same track retriggering its own pitch
    r.s.onEvent(Rig::note(0, 0, 60, 1.0, true, 0), nullptr, 120, false, 0.95); r.s.onEvent(Rig::note(0, 0, 60, 1.1, true, 1), nullptr, 120, false, 0.95);
    r.book(0.95); r.book(1.05); r.book(1.15);
    CHECK(r.s.stats().collisions == 0, "B4: the same track is not a collision"); }
  std::printf("ownership: done\n");
}

// ---- B9, B5, B6, B8 ---------------------------------------------------------------------------------------------------------------------------
static void stopRules() {
  { Rig r;   // B9: a note booked but not yet on when the STOP comes: no ON, no OFF
    r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, false, 0.5); r.s.onEvent(Rig::off(0, 1.2), nullptr, 120, false, 0.5);
    r.book(0.5); r.s.stop(0.55); r.drain(); r.book(0.6); r.book(1.1); r.book(1.3);
    CHECK(r.count(0x90) == 0 && r.count(0x80) == 0, "B9: nothing for a note that never started (on %d, off %d)", r.count(0x90), r.count(0x80)); }
  { Rig r;   // B9: a note that does sound gets its OFF at the STOP, once
    r.s.onEvent(Rig::note(0, 0, 60, 0.5), nullptr, 120, false, 0.45); r.book(0.45); r.book(0.55);
    r.s.stop(0.6); r.drain();
    CHECK(r.count(0x90, 60) == 1 && r.count(0x80, 60) == 1, "B9: a sounding note is ended at the STOP (on %d, off %d)", r.count(0x90, 60), r.count(0x80, 60)); }
  { Rig r;   // B5 + B6: STOP forgets the last CC value and the used channels
    SeqEvent c; c.type = SEV_MCC; c.track = 0; c.ii = 0; c.k = 0; c.v = 100; c.t = 0.1; c.ramp = 0; r.s.onEvent(c, nullptr, 120, false, 0.05); r.book(0.05); r.book(0.12);
    r.s.onEvent(Rig::note(0, 0, 60, 0.2), nullptr, 120, false, 0.15); r.book(0.15); r.book(0.25);
    r.s.stop(0.3); r.drain();
    const int n123 = r.count(0xB0) ;
    r.out.clear(); r.s.stop(0.35); r.drain();
    CHECK(r.out.empty(), "B6: a second STOP sends nothing (the used-channel table was cleared by the first): %zu messages", r.out.size());
    r.out.clear();
    SeqEvent g = c; g.v = 20; g.ramp = 0.1; g.t = 1.0; r.s.start(0, 0.9); r.s.onEvent(g, nullptr, 120, false, 0.9); r.book(0.9); r.book(1.0); r.book(1.2);
    int cc74 = 0; for (const Out& o : r.out) if ((o.b[0] & 0xF0) == 0xB0 && o.b[1] == 74) cc74++;
    CHECK(cc74 == 1, "B5: the first CSL glide of a PLAY sends its start value alone (1 CC), got %d (a glide from the old 100 would be many)", cc74);
    (void)n123; }
  { Rig r;   // B8: Stop after the last tick actually sent, never before it
    Case C; loadCase(root(), "start-stop", C); C.gear[0] = 40; Drive D; run(D, C);   // a positive Gear delay puts the ticks later than the stop time: the Stop must follow them
    int64_t lastTick = -1, stopF = -1; for (const Out& o : D.kept) { if (o.b[0] == 0xF8) lastTick = std::max(lastTick, o.frame); if (o.b[0] == 0xFC) stopF = o.frame; }
    CHECK(stopF >= lastTick && lastTick > 0, "B8: the Stop (%lld) is not before the last tick (%lld)", (long long)stopF, (long long)lastTick);
    CHECK(stopF - lastTick <= 48000 * 0.03, "B8: no phantom gap: the Stop is %.2f ms after the last tick", (stopF - lastTick) * 1000.0 / 48000); }
  std::printf("stop rules: done\n");
}

// ---- B7: a stalled host ------------------------------------------------------------------------------------------------------------------------
static void stall() {
  Case C; loadCase(root(), "loop", C); C.actions.clear(); C.endMs = 3500;   // clock on A at 120 bpm: one tick every 20.8333 ms
  Drive N; runBlocks(N, C, 96);
  Drive S; runBlocks(S, C, 96, 1000, 400);                     // no block runs for 400 ms from 1000 ms
  std::vector<int64_t> tk; for (const Out& o : S.kept) if (o.b[0] == 0xF8) tk.push_back(o.frame);
  const double tl = 48000.0 * 60 / 120 / 24;
  bool mono = true; int bursts = 0, offgrid = 0; int64_t big = 0;
  for (size_t i = 1; i < tk.size(); i++) { const int64_t d = tk[i] - tk[i - 1]; if (d <= 0) mono = false; if (d < tl * 0.5) bursts++; const double q = d / tl; if (std::fabs(q - std::round(q)) > 0.01) offgrid++; big = std::max(big, d); }
  CHECK(mono, "B7: ticks are strictly increasing");
  CHECK(bursts == 0, "B7: no burst: %d tick pairs closer than half a tick", bursts);
  CHECK(offgrid == 0, "B7: the phase is kept: %d intervals are not a whole number of ticks", offgrid);
  // the count: how many ticks were dropped: read through a direct run
  { Case C3 = C; Drive D3; D3.C = C3; JsonError je; D3.song.load(C3.songText.data(), C3.songText.size(), &je);
    MidiSched s(48000); configure(s, C3, D3.song);
    SeqOpts lo; lo.hasLoop = true; lo.loopY0 = 0; lo.loopY1 = 0; lo.live = true; Seq sq(D3.song, D3.vars, SeqMode::Song, lo); sq.setTime(0.08);
    std::vector<SeqEvent> evs(12288); SeqStep st; s.start(0, 0);
    while (sq.alive() && sq.time() < 3.0) { const int k = sq.step(evs.data(), (int)evs.size(), &st); s.onStep(evs.data(), k, st, sq, 0.0); }   // ahead of time: the anchors for 3 s
    s.book(0.0, sq.time(), sq.bpm(), true);
    const uint64_t t0 = s.stats().ticks;
    s.book(2.0, sq.time(), sq.bpm(), true);     // the host was away for 2 s: 96 ticks are late; more than the old 512-tick cap's worth would need a longer gap, the next test
    CHECK(s.stats().lateTicks >= 90, "B7: late ticks are counted (%llu)", (unsigned long long)s.stats().lateTicks);
    CHECK(s.stats().ticks - t0 <= 5, "B7: and not sent: only the ticks inside the look-ahead go out (%llu)", (unsigned long long)(s.stats().ticks - t0)); }
  { // no 512-tick cap: a 30 s gap = 1440 ticks in one call
    Case C3 = C; Drive D3; JsonError je; D3.song.load(C3.songText.data(), C3.songText.size(), &je);
    MidiSched s(48000); configure(s, C3, D3.song);
    SeqOpts lo; lo.hasLoop = true; lo.loopY0 = 0; lo.loopY1 = 0; lo.live = true; Seq sq(D3.song, D3.vars, SeqMode::Song, lo); sq.setTime(0.08); std::vector<SeqEvent> evs(12288); SeqStep st; s.start(0, 0);
    while (sq.alive() && sq.time() < 31.0) { const int k = sq.step(evs.data(), (int)evs.size(), &st); s.onStep(evs.data(), k, st, sq, 0.0); }
    CHECK(sq.time() > 25, "(the test song runs %.1f s: needs more than 25 s for the cap test)", sq.time());
    (void)0;
    s.book(0.0, sq.time(), sq.bpm(), true); const uint64_t t0 = s.stats().ticks; s.book(14.0, sq.time(), sq.bpm(), true);
    CHECK(s.stats().lateTicks > 600, "B7: no 512-tick cap: %llu late ticks counted in one call (14 s = 672)", (unsigned long long)s.stats().lateTicks);
    CHECK(s.stats().ticks - t0 >= 3, "B7: and the same call books the ticks inside the look-ahead (%llu)", (unsigned long long)(s.stats().ticks - t0)); }
  { // the tempo map holds the last 256 steps: ticks older than its oldest anchor are late too (counted), and the clock goes on (it does not stall)
    Case C3 = C; Drive D3; JsonError je; D3.song.load(C3.songText.data(), C3.songText.size(), &je);
    MidiSched s(48000); configure(s, C3, D3.song);
    SeqOpts lo; lo.hasLoop = true; lo.loopY0 = 0; lo.loopY1 = 0; lo.live = true; Seq sq(D3.song, D3.vars, SeqMode::Song, lo); sq.setTime(0.08); std::vector<SeqEvent> evs(12288); SeqStep st; s.start(0, 0);
    while (sq.alive() && sq.time() < 61.0) { const int k = sq.step(evs.data(), (int)evs.size(), &st); s.onStep(evs.data(), k, st, sq, 0.0); }
    s.book(0.0, sq.time(), sq.bpm(), true); const uint64_t t0 = s.stats().ticks; s.book(61.0, sq.time(), sq.bpm(), true);
    CHECK(s.stats().lateTicks > 2800, "B7: ticks older than the oldest anchor are counted late: %llu (about 2900)", (unsigned long long)s.stats().lateTicks);
    CHECK(s.stats().ticks - t0 >= 3, "B7: and the clock goes on: %llu ticks in the look-ahead", (unsigned long long)(s.stats().ticks - t0)); }
  // late notes: the host's lateness verdict (the shared test) drops the MIDI note too
  CHECK(seq_note_late(1.0, 1.2) && !seq_note_late(1.0, 1.1) && !seq_note_late(1.0, 0.9), "the shared lateness test: late = 0.15 s behind (and %d)", 0);
  { Rig r; r.song.insts[0].mo = 2; r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, true, 1.2); r.book(1.2); CHECK(r.count(0x90) == 0 && r.s.stats().lateNotes == 1, "B7: the MIDI companion of a pad note the players dropped is dropped and counted (%d)", 0); }
  for (double late : {0.2, 1.0, 30.0}) { Rig r;   // F8: a MIDI-only instrument has no pad to drop: its late note is sent at once (at `now`), as the page does, and is not counted late
    r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, true, 1.0 + late); r.book(1.0 + late);
    CHECK(r.count(0x90, 60) == 1 && r.s.stats().lateNotes == 0, "F8: a MIDI-only note %.1f s late is sent (%d), not dropped", late, r.count(0x90, 60));
    CHECK(r.out.size() && r.out[0].frame == (int64_t)std::llround((1.0 + late) * 48000), "F8: and at once: frame %lld", r.out.size() ? (long long)r.out[0].frame : -1LL); }
  { Rig r; r.song.insts[0].mo = 2; r.s.onEvent(Rig::note(0, 0, 60, 0.5), nullptr, 120, false, 0.45); r.book(0.45); r.book(0.6);
    r.s.onEvent(Rig::note(0, 0, 64, 1.0), nullptr, 120, true, 1.2); r.book(1.2);
    CHECK(r.count(0x80, 60) == 1 && r.count(0x90, 64) == 0, "B7: a late mono note still ends the track's previous note (no hang)"); }
  std::printf("stall: done (N %zu, S %zu messages)\n", N.kept.size(), S.kept.size());
}


// ---- single rules, by hand --------------------------------------------------------------------------------------------------------------------------
static void rules() {
  { Rig r;   // R-M4: a message already late goes out at once (never in the past)
    r.s.onEvent(Rig::note(0, 0, 60, 0.5), nullptr, 120, false, 1.0); r.book(1.0);
    CHECK(r.out.size() == 1 && r.out[0].frame >= 48000, "R-M4: a note booked for 0.5 s at now = 1.0 goes out at 1.0 (frame %lld)", r.out.empty() ? -1LL : (long long)r.out[0].frame); }
  { Rig r;   // R-M13: poly, at most 4 voices on a track: the 5th ends the oldest
    for (int c = 0; c < 5; c++) r.s.onEvent(Rig::note(0, 0, 60 + c, 1.0 + c * 0.001, true, c), nullptr, 120, false, 0.95);
    r.book(0.95); r.book(1.05);
    CHECK(r.count(0x90) == 5 && r.count(0x80, 60) == 1 && r.count(0x80) == 1, "R-M13: the 5th poly note ends the oldest (ons %d, offs %d)", r.count(0x90), r.count(0x80)); }
  { Rig r;   // R-M18: CC numbers default to 74, 71, 1, 7 when the instrument has none
    for (int k = 0; k < 4; k++) r.song.insts[0].cc[k] = -1;
    for (int k = 0; k < 4; k++) { SeqEvent c; c.type = SEV_MCC; c.track = 0; c.ii = 0; c.k = k; c.v = 50 + k; c.t = 0.1 + k * 0.01; r.s.onEvent(c, nullptr, 120, false, 0.05); }
    r.book(0.05); r.book(0.2);
    int cc[4] = {74, 71, 1, 7}; bool ok = r.out.size() == 4; for (int k = 0; ok && k < 4; k++) ok = r.out[(size_t)k].b[1] == cc[k] && r.out[(size_t)k].b[2] == 50 + k;
    CHECK(ok, "R-M18: the default CC numbers"); }
  { Rig r;   // R-M19: a new CC value cuts a running glide
    SeqEvent a; a.type = SEV_MCC; a.track = 0; a.ii = 0; a.k = 0; a.v = 0; a.t = 0.1; r.s.onEvent(a, nullptr, 120, false, 0.05); r.book(0.05); r.book(0.12);
    SeqEvent g = a; g.v = 100; g.ramp = 1.0; g.t = 1.0; r.s.onEvent(g, nullptr, 120, false, 0.95);
    SeqEvent c = a; c.v = 20; c.ramp = 0; c.t = 1.3; r.s.onEvent(c, nullptr, 120, false, 0.95);
    for (double now = 0.95; now < 2.5; now += 0.03) r.book(now);
    int late = 0; for (const Out& o : r.out) if ((o.b[0] & 0xF0) == 0xB0 && o.b[1] == 74 && r.msOf(o) > 1301 && o.b[2] != 20) late++;
    CHECK(late == 0, "R-M19: after the new value at 1.3 s the glide sends nothing more (%d messages)", late); }
  { Rig r;   // R-M21: a long bend ramp is 48 steps at most (10 ms each, capped)
    r.song.insts[0].pbr = 48;
    SeqEvent e = Rig::note(0, 0, 60, 1.0); e.has |= SH_BEND; e.bendMul = 2; e.bendT1 = 3.0; r.s.onEvent(e, nullptr, 120, false, 0.95);
    for (double now = 0.95; now < 3.2; now += 0.03) r.book(now);
    CHECK(r.count(0xE0) == 48, "R-M21: the ramp is 48 bend messages (got %d)", r.count(0xE0)); }
  { Rig r;   // R-M35: PLAY while a song's tempo map is still held starts a new one
    Case C; loadCase(root(), "loop", C); Drive D; JsonError je; D.song.load(C.songText.data(), C.songText.size(), &je);
    MidiSched s(48000); configure(s, C, D.song); SeqOpts lo; lo.hasLoop = true; lo.loopY0 = 0; lo.loopY1 = 0; lo.live = true; std::vector<SeqEvent> evs(12288); SeqStep st; std::vector<MidiMsg> out(4096);
    { Seq a(D.song, D.vars, SeqMode::Song, lo); a.setTime(0.08); s.start(0, 0); for (int i = 0; i < 20; i++) { const int k = a.step(evs.data(), (int)evs.size(), &st); s.onStep(evs.data(), k, st, a, 0.0); } s.book(0.0, a.time(), a.bpm(), true); }
    s.start(0, 5.0);   // (no stop in between)
    Seq b(D.song, D.vars, SeqMode::Song, lo); b.setTime(5.08);
    int ticks = 0; double now = 5.0;
    for (; now < 5.8; now += 0.0267) { while (b.time() < now + 0.25) { const int k = b.step(evs.data(), (int)evs.size(), &st); s.onStep(evs.data(), k, st, b, now); } s.book(now, b.time(), b.bpm(), true); }
    int64_t n; while ((n = s.drain(out.data(), 4096)) > 0) for (int64_t i = 0; i < n; i++) if (out[(size_t)i].b[0] == 0xF8 && out[(size_t)i].frame >= 5 * 48000) ticks++;
    CHECK(ticks >= 20 && s.stats().lateTicks == 0, "R-M35: a new PLAY starts a new tempo map: %d ticks in the first 0.7 s (about 33 expected), %llu counted late (0 expected)", ticks, (unsigned long long)s.stats().lateTicks); }
  { Rig r;   // R-M38: the FX latency is read as it is NOW, every block
    r.s.setFxLatency(0.010); r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, false, 0.95); r.book(0.95);
    r.s.setFxLatency(0.020); r.s.onEvent(Rig::note(0, 0, 62, 1.0), nullptr, 120, false, 0.95); r.book(0.96);
    double m60 = -1, m62 = -1; for (const Out& o : r.out) if ((o.b[0] & 0xF0) == 0x90) (o.b[1] == 60 ? m60 : m62) = r.msOf(o);
    CHECK(std::fabs(m60 - 1010) < 0.05 && std::fabs(m62 - 1020) < 0.05, "R-M38: no cache: %.3f %.3f (1010, 1020)", m60, m62); }
  { Rig r;   // R-M40: equal timestamps keep their order: the mono track's OFF before the next ON
    r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, false, 0.5); r.s.onEvent(Rig::note(0, 0, 62, 1.2), nullptr, 120, false, 0.5);
    for (double now = 0.5; now < 1.4; now += 0.03) r.book(now);
    int on62 = -1, off60 = -1; for (size_t i = 0; i < r.out.size(); i++) { const Out& o = r.out[i]; if ((o.b[0] & 0xF0) == 0x90 && o.b[1] == 62) on62 = (int)i; if ((o.b[0] & 0xF0) == 0x80 && o.b[1] == 60) off60 = (int)i; }
    CHECK(off60 >= 0 && on62 > off60 && r.out[(size_t)on62].frame == r.out[(size_t)off60].frame, "R-M40: the OFF comes before the ON at the same time (off at %d, on at %d)", off60, on62); }
  { Rig r;   // B9a: a cable change (release while playing) ends the sounding note; its own OFF, still waiting in the outbox, must not follow as a second OFF
    SeqEvent e = Rig::note(0, 0, 60, 1.0); e.has |= SH_CUT; e.cut = 1.6; r.s.onEvent(e, nullptr, 120, false, 0.95); r.book(0.95); r.book(1.05);
    r.s.release(1.1, true); r.drain();
    for (double now = 1.1; now < 1.8; now += 0.03) r.book(now);
    CHECK(r.count(0x80, 60) == 1, "B9: one OFF for the note, not a second one from the outbox (%d)", r.count(0x80, 60)); }
  { Rig r;   // B9: a note whose ON is already in the shell's queue (a few ms ahead) when the STOP comes: the shell drops it, so no OFF for it
    r.s.onEvent(Rig::note(0, 0, 60, 1.03), nullptr, 120, false, 0.99); r.book(0.99);
    r.s.stop(0.995); r.drain();
    CHECK(r.count(0x90, 60) == 0 && r.count(0x80, 60) == 0, "B9: no ON (cleared) and no OFF: ons %d, offs %d", r.count(0x90, 60), r.count(0x80, 60)); }
  std::printf("single rules: done\n");
}


// ---- F3: a STOP anywhere in a note's last 100 ms never leaves it hanging, and never ends it twice ----------------------------------------------------------
static void stopSweep() {
  int bad = 0, runs = 0, onRuns = 0;
  for (int ms = 940; ms <= 1700; ms++) {   // the note is on from 1.000 s to 1.600 s (cut); the sweep covers before the ON, the ON frame itself, the whole note and after the OFF
    for (int mode = 0; mode < 2; mode++) {   // 0: books every 10 ms; 1: books every 37 ms (a bigger block: the OFF is out of the outbox earlier or later)
      Rig r; r.s.setBlock(mode ? 0.037 : 0.010);
      SeqEvent e = Rig::note(0, 0, 60, 1.0); e.has |= SH_CUT; e.cut = 1.6; r.s.onEvent(e, nullptr, 120, false, 0.9);
      const double step = mode ? 0.037 : 0.010; double now = 0.9; const double stopT = ms / 1000.0;
      while (now + step <= stopT + 1e-9) { r.book(now); now += step; }
      r.book(now); if (now < stopT - 1e-9) now = now;   // (the last block before the stop)
      r.s.stop(stopT); r.drain();
      for (double t = stopT + 0.01; t < 1.9; t += 0.03) r.book(t);   // the host goes on (the stopped scheduler must send nothing more for the note)
      const int64_t sf = (int64_t)std::llround(stopT * 48000);
      int ons = 0, offs = 0, sounding = 0; int64_t offF = -1; std::vector<const Out*> seq60;
      for (const Out& o : r.out) if (o.b[1] == 60 && ((o.b[0] & 0xF0) == 0x90 || (o.b[0] & 0xF0) == 0x80) && o.port == 0) seq60.push_back(&o);
      std::stable_sort(seq60.begin(), seq60.end(), [](const Out* a, const Out* b) { return a->frame < b->frame; });
      for (const Out* o : seq60) { const bool on = (o->b[0] & 0xF0) == 0x90 && o->b[2] > 0; if (on) { ons++; sounding++; } else { offs++; sounding--; offF = o->frame; } }
      runs++; if (ons) onRuns++;
      const bool ok = sounding == 0 && ons == offs && ons <= 1 && (ons == 0 || offF <= std::max<int64_t>(sf, 48000 * 16 / 10));
      if (!ok) { if (bad++ < 5) CHECK(false, "F3: STOP at %d ms (mode %d): %d on, %d off, %d left sounding, the off at frame %lld (stop %lld)", ms, mode, ons, offs, sounding, (long long)offF, (long long)sf); }
    }
  }
  CHECK(bad == 0 && onRuns > 600, "F3: %d of %d stop times left a hung or doubled note (%d runs had a note)", bad, runs, onRuns);
  std::printf("F3 sweep: %d runs, %d with a note, %d bad\n", runs, onRuns, bad);
}

// ---- F4: a stalled consumer: what the ring drops, in what order ---------------------------------------------------------------------------------------
static void stalledConsumer() {
  const int cap = kMidiRingCap;
  { Rig r;
    const uint8_t tick[1] = {0xF8}, cc[3] = {0xB0, 7, 100}, on[3] = {0x90, 30, 100}, offb[3] = {0x80, 30, 0};
    const int nT = cap + 800; for (int i = 0; i < nT; i++) r.s.liveRaw(0, tick, 1, 0, 0.0);
    const int keptT = cap - kRingReserveLow + 1;                     // accepted while at least `reserve` slots are free
    CHECK((int)r.s.stats().dropTicks == nT - keptT && r.s.stats().dropCtl == 0 && r.s.stats().mayHang == 0, "F4: ticks go first: %d dropped, expected %d", (int)r.s.stats().dropTicks, nT - keptT);
    for (int i = 0; i < 100; i++) r.s.liveRaw(0, cc, 3, 0, 0.0);
    CHECK(r.s.stats().dropCtl == 100 && r.s.stats().dropNoteOn == 0, "F4: CCs are dropped below the reserve (%d)", (int)r.s.stats().dropCtl);
    for (int i = 0; i < 400; i++) r.s.liveRaw(0, on, 3, 0, 0.0);
    CHECK((int)r.s.stats().dropNoteOn == 400 - (kRingReserveLow - kRingReserveNote) && r.s.stats().dropOff == 0, "F4: note-ons are dropped next, below the smaller reserve (%d dropped)", (int)r.s.stats().dropNoteOn);
    for (int i = 0; i < 100; i++) r.s.liveRaw(0, offb, 3, 0, 0.0);
    CHECK(r.s.stats().dropOff == 0 && r.s.stats().mayHang == 0, "F4: note-offs are still accepted while ANY slot is free (%d dropped)", (int)r.s.stats().dropOff);
    r.s.stop(0.5);   // CLEAR markers (one per connected port) and the rest of a stop
    r.drain(); int clears = 0; (void)clears;
    CHECK(r.s.stats().dropClear == 0 && r.s.stats().mayHang == 0, "F4: the STOP's CLEAR markers made it into the full ring (%d dropped)", (int)r.s.stats().dropClear);
    // and the consumer did get them: drain through the raw ring
    Rig q; for (int i = 0; i < nT; i++) q.s.liveRaw(0, tick, 1, 0, 0.0); q.s.stop(0.5);
    MidiMsg b[512]; int nclr = 0; for (;;) { const int64_t n = q.s.drain(b, 512); if (n <= 0) break; for (int64_t i = 0; i < n; i++) if (b[i].flags & MIDI_F_CLEAR) nclr++; }
    CHECK(nclr == 2, "F4: both CLEAR markers are in the stream (%d)", nclr);
  }
  { Rig r; const uint8_t offb[3] = {0x80, 30, 0};   // only when the ring is completely full of important records is one of them dropped, counted, and flagged
    for (int i = 0; i < cap + 10; i++) r.s.liveRaw(0, offb, 3, 0, 0.0);
    CHECK(r.s.stats().dropOff == 10 && r.s.stats().ringFull == 10 && r.s.stats().mayHang == 1, "F4: 10 offs dropped only at a completely full ring (%d), the hung-note flag is set (%d)", (int)r.s.stats().dropOff, (int)r.s.stats().mayHang);
    r.drain(); r.s.panic(1.0); r.drain();
    CHECK(r.s.stats().mayHang == 0, "F4: a panic that could be queued clears the flag"); }
  { Rig r; const uint8_t tick[1] = {0xF8}, on[3] = {0x90, 30, 100};   // a dropped note-on cancels its own later off
    for (int i = 0; i < cap; i++) r.s.liveRaw(0, tick, 1, 0, 0.0);
    for (int i = 0; i < 300; i++) r.s.liveRaw(0, on, 3, 0, 0.0);
    const uint64_t d0 = r.s.stats().dropNoteOn;
    SeqEvent e = Rig::note(0, 0, 60, 1.0); e.has |= SH_CUT; e.cut = 1.2; r.s.onEvent(e, nullptr, 120, false, 0.9);
    r.s.book(0.9, 0.9, 120, false); r.s.book(1.05, 1.05, 120, false); r.s.book(1.25, 1.25, 120, false);
    CHECK(r.s.stats().dropNoteOn == d0 + 1, "F4: the ON of the note was dropped (%d)", (int)(r.s.stats().dropNoteOn - d0));
    r.drain(); CHECK(r.count(0x90, 60) == 0 && r.count(0x80, 60) == 0, "F4: and its OFF never follows (on %d, off %d)", r.count(0x90, 60), r.count(0x80, 60));
    CHECK(r.s.stats().offsSuppressed >= 1, "F4: the OFF is counted as suppressed"); }
  std::printf("stalled consumer: done\n");
}

// ---- F7: the boundaries and limits the verifier's extra mutants found untested ----------------------------------------------------------------------------
struct Harness {   // a Song with the clock on, a Seq on a loop of the 'loop' case at one row per beat, the scheduler
  Case C; Drive D; MidiSched* s = nullptr; Seq* q = nullptr; std::vector<SeqEvent> evs; SeqStep st; double t0 = 0;
  explicit Harness(const char* name = "loop", const char* row8 = nullptr) : evs(12288) { loadCase(root(), name, C);
    if (row8) { const std::string from = "[55,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"; const size_t at = C.songText.find(from); if (at != std::string::npos) C.songText.replace(at, from.size(), row8); else CHECK(false, "the tempo-clock song has no row 8 to replace (the two-TPO-row case would be vacuous)"); }
    JsonError je; D.song.load(C.songText.data(), C.songText.size(), &je); D.song.lpb = 1; D.song.swing = 50; s = new MidiSched(48000); configure(*s, C, D.song); s->setFxLatency(0); for (int p = 0; p < 4; p++) s->setPort(p, !C.ports[(size_t)p].empty(), C.clk[(size_t)p] != 0, 0, p); }
  ~Harness() { delete q; delete s; }
  void begin(double startAt, double firstStep, bool loop = true) { SeqOpts lo; if (loop) { lo.hasLoop = true; lo.loopY0 = C.y0; lo.loopY1 = C.y1; } lo.live = true; q = new Seq(D.song, D.vars, SeqMode::Song, lo); s->start(0, startAt); q->setTime(firstStep); t0 = firstStep; }
  std::vector<double> grid; std::vector<int64_t> tf;
  void steps(int n, double now) { for (int i = 0; i < n; i++) { const int k = q->step(evs.data(), (int)evs.size(), &st); for (int j = 0; j < k; j++) if (evs[j].type == SEV_BEAT) grid.push_back(evs[j].t); s->onStep(evs.data(), k, st, *q, now); } }
  int ticks() { MidiMsg b[512]; int n = 0; for (;;) { const int64_t k = s->drain(b, 512); if (k <= 0) break; for (int64_t i = 0; i < k; i++) if (b[i].b[0] == 0xF8 && b[i].kind == MK_CLOCK) { n++; last = b[i].frame; tf.push_back(b[i].frame); } } return n; }
  int64_t last = -1;
};
// ---- FU4 / FU5: the tempo map across TPO rows (N1, T1) ------------------------------------------------------------------------------------------------------------------------------
// tempo-clock's song at one row per beat, no swing: 32 rows, a TPO 90 command on row 16 (and, with `row8`, another on row 8). The rule: tick 24 j is at row j's time, to the frame, whatever the
// tempo did before or after (the clock integrates each interval with the tempo in force when the step STARTED). liveAt / liveBpm: the host's setBpm lands just before step `liveAt`.
static int tempoOracle(const char* row8, int liveAt, double liveBpm, int jFrom, int jTo, int* checked, int shiftAt = -1) {
  Harness h("tempo-clock", row8); h.begin(0.0, 0.5, false);
  double now = 0.4;
  for (int i = 0; i < 30; i++) { if (i == liveAt) h.q->setBpm(liveBpm); if (i == shiftAt) h.q->shift(0.2); h.steps(1, now); const double endT = h.q->time(); for (; now < endT - 0.3; now += 0.05) { h.s->book(now, h.q->time(), h.q->bpm(), true); h.ticks(); } }
  for (int k = 0; k < 40; k++, now += 0.05) { h.s->book(now, h.q->time(), h.q->bpm(), true); h.ticks(); }
  int bad = 0; *checked = 0;
  if (shiftAt >= 0) {   // a stall skip keeps the clock's PHASE (B7: ticks do not follow the shifted rows), so what is checked is the TEMPO of the ticks: after the skip and before the TPO row's interval ends
    // (beats 16.5 to 17) they are 60 / 120 / 24 s apart (the interval after the TPO step is still timed at 120), from beat 18 on 60 / 90 / 24 s (the TPO's 90)
    for (size_t k = 24 * 16 + 10; k + 1 < h.tf.size() && k < 24 * 24; k++) {
      const double want = k < 24 * 17 - 1 ? 48000.0 * 60 / (liveAt >= 0 ? liveBpm : 120.0) / 24 : k >= 24 * 18 ? 48000.0 * 60 / 90 / 24 : -1; if (want < 0) continue; (*checked)++;
      if (std::fabs((double)(h.tf[k + 1] - h.tf[k]) - want) > 2) { if (bad++ < 3) std::printf("   tick %zu to %zu: %lld frames apart, %.1f expected\n", k, k + 1, (long long)(h.tf[k + 1] - h.tf[k]), want); } }
    return bad;
  }
  for (int j = jFrom; j < jTo && (size_t)j < h.grid.size(); j++) { const size_t k = 24 * (size_t)j; if (k >= h.tf.size()) break; (*checked)++;
    if (std::llabs(h.tf[k] - (int64_t)std::llround(h.grid[(size_t)j] * 48000)) > 1) { if (bad++ < 3) std::printf("   tick of row %d at frame %lld, row at %lld\n", j, (long long)h.tf[k], (long long)std::llround(h.grid[(size_t)j] * 48000)); } }
  return bad;
}
static void tempoRows() {
  int chk = 0, bad;
  bad = tempoOracle(nullptr, -1, 0, 1, 26, &chk);
  CHECK(chk >= 20 && bad == 0, "FU4 base: one TPO row (16): ticks on the rows (%d checked, %d off)", chk, bad);
  // T1: two TPO rows (8 -> 75, 16 -> 90): the second one's previous tempo (75) is neither the song's (120) nor the command's own
  bad = tempoOracle("[55,-1,-1,-1,-1,0,19,75,-1,-1,-1,-1]", -1, 0, 1, 26, &chk);
  CHECK(chk >= 20 && bad == 0, "T1: two TPO rows (8 and 16): ticks on the rows (%d checked, %d off)", chk, bad);
  // N1: a live tempo change just before the TPO step (row 16): the step is timed at the new tempo, its ticks must be too
  bad = tempoOracle(nullptr, 16, 163.3, 10, 26, &chk);
  CHECK(chk >= 12 && bad == 0, "N1: a live tempo change just before a TPO row: ticks on the rows after it (%d checked, %d off)", chk, bad);
  bad = tempoOracle("[55,-1,-1,-1,-1,0,19,75,-1,-1,-1,-1]", 16, 148.9, 10, 26, &chk);
  CHECK(chk >= 12 && bad == 0, "N1: the same after two TPO rows (%d checked, %d off)", chk, bad);
  // F1 of verify-267: the runner's stall skip (Seq::shift) just before the TPO step moves every later row by 0.2 s; the clock keeps its phase (B7) and must keep the right TEMPO
  bad = tempoOracle(nullptr, -1, 0, 17, 26, &chk, 16);
  CHECK(chk >= 100 && bad == 0, "N1: a Seq::shift (stall skip) just before a TPO row: the ticks keep the tempo (%d checked, %d off)", chk, bad);
  bad = tempoOracle(nullptr, 16, 163.3, 17, 26, &chk, 16);
  CHECK(chk >= 100 && bad == 0, "N1: a live tempo change AND a shift before the TPO row (%d checked, %d off)", chk, bad);
  bad = tempoOracle(nullptr, 8, 140.0, 4, 26, &chk);   // a live change NOT next to a TPO row: always right (the control)
  CHECK(chk >= 18 && bad == 0, "N1 control: a live change in the middle (%d checked, %d off)", chk, bad);
}
// T4: a Start (and Continue, SPP) is note-on priority in the full ring: kept while a tick is already dropped, dropped (counted as a Start) only below the smaller reserve; a Stop is kept to the last slot
static void startPriority() {
  const int cap = kMidiRingCap;
  Rig r; const uint8_t tick[1] = {0xF8}, start[1] = {0xFA}, cont[1] = {0xFB}, spp[3] = {0xF2, 0, 0}, stopb[1] = {0xFC}, on[3] = {0x90, 30, 100};
  for (int i = 0; i < cap + 100; i++) r.s.liveRaw(0, tick, 1, 0, 0.0);
  CHECK(r.s.stats().dropTicks > 0 && r.s.stats().dropStart == 0, "T4: the ring is at the tick reserve (%llu ticks dropped)", (unsigned long long)r.s.stats().dropTicks);
  r.s.liveRaw(0, start, 1, 0, 0.0); r.s.liveRaw(0, cont, 1, 0, 0.0); r.s.liveRaw(0, spp, 3, 0, 0.0);
  CHECK(r.s.stats().dropStart == 0, "T4: Start / Continue / SPP are kept where a tick is dropped (%llu dropped)", (unsigned long long)r.s.stats().dropStart);
  for (int i = 0; i < 400; i++) r.s.liveRaw(0, on, 3, 0, 0.0);   // down to the note-on reserve
  const uint64_t d0 = r.s.stats().dropStart;
  r.s.liveRaw(0, start, 1, 0, 0.0); r.s.liveRaw(0, cont, 1, 0, 0.0); r.s.liveRaw(0, spp, 3, 0, 0.0);
  CHECK(r.s.stats().dropStart == d0 + 3, "T4: below the note-on reserve a Start / Continue / SPP is dropped and counted as such (%llu)", (unsigned long long)(r.s.stats().dropStart - d0));
  r.s.liveRaw(0, stopb, 1, 0, 0.0);
  CHECK(r.s.stats().dropClear == 0 && r.s.stats().mayHang == 0, "T4: a Stop is kept (%llu dropped)", (unsigned long long)r.s.stats().dropClear);
}

static void boundaries() {
  { Harness h; h.begin(0.0, 0.5); h.steps(256, 0.0);   // V2: one call books at most 4096 ticks (here 6000 are late): the loop bound is exactly 4096
    h.s->book(0.0, h.q->time(), h.q->bpm(), true); h.ticks();
    const uint64_t l0 = h.s->stats().lateTicks; h.s->book(400.0, h.q->time(), h.q->bpm(), true);
    CHECK(h.s->stats().lateTicks - l0 == 4096, "V2: one book call counts 4096 late ticks and no more (%llu)", (unsigned long long)(h.s->stats().lateTicks - l0)); }
  int nExact = -1, nPast = -1; uint64_t lExact = 9, lPast = 9;
  for (int past = 0; past < 2; past++) {   // V3 / V4: a tick exactly 20 ms late is sent, one ulp later it is dropped (tick 0 is at 0.5 s exactly, no FX latency, no Gear delay)
    Harness h; h.begin(0.0, 0.5); h.steps(1, 0.0);
    double now = 0.52; while (now - kClockLateSec > 0.5) now = std::nextafter(now, 0.0); while (now - kClockLateSec < 0.5) now = std::nextafter(now, 1.0);
    if (past) while (now - kClockLateSec <= 0.5) now = std::nextafter(now, 1.0); else CHECK(now - kClockLateSec == 0.5, "V3: found the boundary instant (%.17g)", now);
    h.s->book(now, h.q->time(), h.q->bpm(), true); const int n = h.ticks(); (past ? nPast : nExact) = n; (past ? lPast : lExact) = h.s->stats().lateTicks;
  }
  CHECK(lExact == 0 && lPast == 1 && nExact == nPast + 1, "V3/V4: tick 0 exactly on the 20 ms limit is sent (%d ticks, %llu late), one ulp past it dropped (%d ticks, %llu late)", nExact, (unsigned long long)lExact, nPast, (unsigned long long)lPast);
  { Harness h; h.begin(0.0, 0.5); h.steps(1, 0.0);   // V4: the same boundary at the OUTBOX release: tick 0 (0.5 s) is booked 70 ms ahead (more than the 60 ms lead: it waits there) and released exactly 20 ms late
    double now = 0.52; while (now - kClockLateSec > 0.5) now = std::nextafter(now, 0.0); while (now - kClockLateSec < 0.5) now = std::nextafter(now, 1.0);
    h.s->book(0.43, h.q->time(), h.q->bpm(), true); const int nWait = h.ticks();
    CHECK(nWait == 0 && h.s->pendCount() >= 1, "V4: the tick waits in the outbox (%d sent, %d waiting)", nWait, h.s->pendCount());
    h.s->book(now, h.q->time(), h.q->bpm(), true); const int n0 = h.ticks();
    CHECK(n0 >= 1 && h.s->stats().lateTicks == 0, "V4: a tick released from the outbox exactly 20 ms late is sent (%d, %llu late)", n0, (unsigned long long)h.s->stats().lateTicks); }
  { Harness h; h.begin(0.0, 0.5);   // V17: the tempo map keeps the NEWEST 256 steps: a tempo change at step 260 still bends the clock right (tick 24 j is on step j's time)
    double now = 0.4; for (int i = 0; i < 300; i++) { if (i == 260) h.q->setBpm(240); h.steps(1, now); const double endT = h.q->time(); for (; now < endT - 0.3; now += 0.05) { h.s->book(now, h.q->time(), h.q->bpm(), true); h.ticks(); } }
    int bad = 0, chk = 0; for (size_t j = 270; j < 296 && j < h.grid.size(); j++) { const size_t k = 24 * j; if (k >= h.tf.size()) break; chk++; if (std::llabs(h.tf[k] - (int64_t)std::llround(h.grid[j] * 48000)) > 1) bad++; }
    CHECK(chk >= 20 && bad == 0, "V17: after step 256 and a tempo change the ticks stay on the rows (%d checked, %d off)", chk, bad); }
  { Harness h; h.begin(0.0, 0.5); h.steps(1, 0.0);   // B7b: a tick that waited in the outbox while the host stalled is dropped when it is released, counted, not sent
    h.s->book(0.43, h.q->time(), h.q->bpm(), true); h.ticks();
    h.s->book(5.0, h.q->time(), h.q->bpm(), true); const int n = h.ticks();
    CHECK(n == 0 && h.s->stats().lateTicks >= 1 && h.s->pendCount() == 0, "B7b: the stalled tick is dropped (%d sent, %llu late, %d waiting)", n, (unsigned long long)h.s->stats().lateTicks, h.s->pendCount()); }
  { Harness h; h.C.gear[0] = 40; h.s->setPort(0, true, true, 40, 0); h.begin(0.0, 0.5); h.steps(8, 0.0);   // V18: with a 40 ms Gear delay several ticks are already queued AFTER the stop time; the Stop still follows the last tick at or before it
    for (double now = 0.5; now < 1.0 - 1e-9; now += 0.02) h.s->book(now, h.q->time(), h.q->bpm(), true);
    h.s->book(1.0, h.q->time(), h.q->bpm(), true); h.s->stop(1.0);
    MidiMsg b[512]; int64_t lastLe = -1, stopF = -1, afterNf = 0; const int64_t nf = 48000;
    for (;;) { const int64_t n = h.s->drain(b, 512); if (n <= 0) break; for (int64_t i = 0; i < n; i++) { if (b[i].b[0] == 0xF8) { if (b[i].frame <= nf) lastLe = std::max(lastLe, b[i].frame); else afterNf++; } if (b[i].b[0] == 0xFC) stopF = b[i].frame; } }
    CHECK(afterNf >= 2 && lastLe > 0 && stopF == lastLe, "V18: Stop at frame %lld = the last tick at or before the stop (%lld), with %lld ticks queued after it", (long long)stopF, (long long)lastLe, (long long)afterNf); }
  { Rig r; r.s.setPort(1, true, true, 0, 5);   // V8: the CLEAR marker carries the port's cable
    r.s.stop(0.5); MidiMsg b[64]; bool found = false, ok = true;
    for (;;) { const int64_t n = r.s.drain(b, 64); if (n <= 0) break; for (int64_t i = 0; i < n; i++) if (b[i].flags & MIDI_F_CLEAR) { if (b[i].port == 1) { found = true; ok = ok && b[i].cable == 5; } } }
    CHECK(found && ok, "V8: port 1's CLEAR marker names its cable (5)"); }
  { Rig r;   // V15: a note whose ON is exactly at the stop frame is delivered (the shell keeps frame == marker): it needs its OFF
    r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, false, 0.95); r.book(0.95);
    r.s.stop(1.0); r.drain();
    CHECK(r.count(0x90, 60) == 1 && r.count(0x80, 60) == 1, "V15: ON at the stop frame (kept) and its OFF at the stop (on %d, off %d)", r.count(0x90, 60), r.count(0x80, 60)); }
  { Rig r; const uint8_t tick[1] = {0xF8};   // V21: the ring-full counter counts
    for (int i = 0; i < kMidiRingCap + 5; i++) r.s.liveRaw(0, tick, 1, 0, 0.0);
    CHECK(r.s.stats().ringFull == (uint64_t)(5 + kRingReserveLow - 1), "V21: ringFull counts every dropped record (%llu)", (unsigned long long)r.s.stats().ringFull); }
  std::printf("boundaries: done\n");
}

int main() {
  cadence(); ownership(); stopRules(); stall(); rules(); stopSweep(); stalledConsumer(); startPriority(); boundaries(); tempoRows();
  std::printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
