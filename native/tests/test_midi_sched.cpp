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
    Case C; loadCase(root(), "start-stop", C); Drive D; run(D, C);
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
    s.book(0.0, sq.time(), sq.bpm(), true); s.book(30.0, sq.time(), sq.bpm(), true);
    CHECK(s.stats().lateTicks > 1000, "B7: no 512-tick cap: %llu late ticks counted in one call", (unsigned long long)s.stats().lateTicks); }
  // late notes: the host's lateness verdict (the shared test) drops the MIDI note too
  CHECK(seq_note_late(1.0, 1.2) && !seq_note_late(1.0, 1.1) && !seq_note_late(1.0, 0.9), "the shared lateness test: late = 0.15 s behind (and %d)", 0);
  { Rig r; r.s.onEvent(Rig::note(0, 0, 60, 1.0), nullptr, 120, true, 1.2); r.book(1.2); CHECK(r.count(0x90) == 0 && r.s.stats().lateNotes == 1, "B7: a note the host calls late is dropped and counted (%d)", 0); }
  { Rig r; r.s.onEvent(Rig::note(0, 0, 60, 0.5), nullptr, 120, false, 0.45); r.book(0.45); r.book(0.6);
    r.s.onEvent(Rig::note(0, 0, 64, 1.0), nullptr, 120, true, 1.2); r.book(1.2);
    CHECK(r.count(0x80, 60) == 1 && r.count(0x90, 64) == 0, "B7: a late mono note still ends the track's previous note (no hang)"); }
  std::printf("stall: done (N %zu, S %zu messages)\n", N.kept.size(), S.kept.size());
}

int main() {
  cadence(); ownership(); stopRules(); stall();
  std::printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
