// LIVE ROUTE, the verifier's round on PR #265 (docs/port/reviews/verify-265.md): the tests that close its findings and kill its surviving mutants.
//   F2  a note-off is never dropped when a queue is full: LiveShell::enqueue (direct), and the whole pool with 255 wobble events + the off in one chunk plus one event pending (62 alignments: never a stuck note); counted;
//   F4  NaN / Inf / absurd values in any numeric note or mod field never reach a core: the output is finite for all four kinds, every refusal is counted (LiveStats::refused);
//   L5  the pool timeline's forced off: the events at or after an off that finds the timeline full are discarded and counted (mutant X08).
//   test_live_review
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <memory>
#include <vector>

#include "engine/live_pool.h"
#include "engine/live_shell.h"
#include "rt/sampletime.h"
#include "seq/seq.h"

using namespace thunder;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)
static const double SR = 48000;

static float peakOf(const std::vector<float>& v, size_t a, size_t b) { float p = 0; for (size_t i = a; i < b && i < v.size(); i++) p = std::max(p, std::fabs(v[i])); return p; }

// ---- F2 (a): LiveShell::enqueue, driven directly -------------------------------------------------------------------------------------------------------------------------------------------------
static void shellRun(const std::vector<LiveEvent>& ev, uint32_t* dropped, uint32_t* forced, float* peakHeld, float* peakEnd) {
  LiveBass sh(SR); BassP p; p.rel = 20; sh.core().set(p);
  std::vector<float> L(128), R(128); std::vector<float> all;
  sh.process(L.data(), R.data(), 128, ev.data(), (uint32_t)ev.size()); all.insert(all.end(), L.begin(), L.end());
  for (int i = 0; i < 80; i++) { sh.process(L.data(), R.data(), 128, nullptr, 0); all.insert(all.end(), L.begin(), L.end()); }
  *dropped = sh.dropped(); *forced = sh.forced(); *peakHeld = peakOf(all, 128, 1024); *peakEnd = peakOf(all, all.size() - 1024, all.size());
}
static void testShell() {
  auto mk = [](uint32_t at, LiveEvType t, double v = 0) { LiveEvent e; e.at = at; e.type = t; e.v = v; return e; };
  // 257 events in one call: a NoteOn, 255 wobbles, the NoteOff (all of it earlier than the off: the latest wobble gives way)
  { std::vector<LiveEvent> ev; ev.push_back(mk(0, LiveEvType::NoteOn)); for (int i = 0; i < 255; i++) ev.push_back(mk(10, LiveEvType::Wob, 0.5)); ev.push_back(mk(120, LiveEvType::NoteOff));
    uint32_t d, f; float ph, pe; shellRun(ev, &d, &f, &ph, &pe);
    CHECKF(ph > 0.01f, "the note never sounded (%g)", ph); CHECKF(pe < 1e-3f, "stuck note: the off was dropped (%g at the end)", pe); CHECKF(f == 1 && d >= 1, "forced %u dropped %u", f, d); }
  // the off before queued later events: they are discarded (counted), the off kept
  { std::vector<LiveEvent> ev; ev.push_back(mk(0, LiveEvType::NoteOn)); for (int i = 0; i < 255; i++) ev.push_back(mk(100, LiveEvType::Wob, 0.5)); ev.push_back(mk(50, LiveEvType::NoteOff));
    uint32_t d, f; float ph, pe; shellRun(ev, &d, &f, &ph, &pe);
    CHECKF(pe < 1e-3f, "stuck note (%g)", pe); CHECKF(f == 1 && d == 255, "forced %u dropped %u (want 1 and the 255 later wobbles)", f, d); }
  // a NoteOn and a wobble event on a full queue are dropped and counted, as before
  { std::vector<LiveEvent> ev; for (int i = 0; i < 256; i++) ev.push_back(mk(5, LiveEvType::Wob, 0.5)); ev.push_back(mk(6, LiveEvType::NoteOn)); ev.push_back(mk(7, LiveEvType::Wob, 0.25));
    uint32_t d, f; float ph, pe; shellRun(ev, &d, &f, &ph, &pe); CHECKF(d == 2 && f == 0, "dropped %u forced %u", d, f); }
}

// ---- F2 (b): the whole pool ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
static SeqEvent mkNote(double t, int track, int pad, double rate) { SeqEvent e; e.type = SEV_NOTE; e.t = t; e.track = track; e.pad = pad; e.rate = rate; e.gain = 1; e.pan = 128; return e; }
static SeqEvent mkMod(double t, int track) { SeqEvent e; e.type = SEV_MOD; e.t = t; e.track = track; return e; }
struct PoolRun {
  std::unique_ptr<LivePool> lp; std::vector<float> L; int64_t total = 0;
  explicit PoolRun(LiveKind kind, int64_t tot) : lp(new LivePool(SR)), total(tot) {
    LivePadConfig cfg; cfg.kind = kind; cfg.trackMask = 1; if (kind == LiveKind::Bass) cfg.bass.rel = 20; if (kind == LiveKind::Vox) cfg.vox.txt = "la";
    CHECK(lp->addPad(0, cfg)); L.assign((size_t)tot, 0.f);
  }
  // events (time, action) are handed over 0.25 s ahead, as the sequencer does
  template <class F> void run(const std::vector<std::pair<double, F>>& evs) {
    std::vector<char> done(evs.size(), 0); std::vector<float> bl(128), br(128); float* inL[kVoiceTracks] = {}; float* inR[kVoiceTracks] = {}; inL[0] = bl.data(); inR[0] = br.data();
    for (int64_t q0 = 0; q0 < total; q0 += 128) {
      const double t0 = (double)q0 / SR; lp->setTime(t0);
      for (size_t i = 0; i < evs.size(); i++) if (!done[i] && evs[i].first - 0.25 <= t0) { done[i] = 1; evs[i].second(*lp); }
      std::fill(bl.begin(), bl.end(), 0.f); std::fill(br.begin(), br.end(), 0.f); lp->render(inL, inR, 128, q0);
      for (int i = 0; i < 128 && q0 + i < total; i++) L[(size_t)(q0 + i)] = bl[(size_t)i];
    }
  }
};
using Act = std::function<void(LivePool&)>;
static void testPoolOverflow() {
  int sensitive = 0, counted = 0;
  for (int a = 0; a < 62; a++) {
    const int lat = (int)LiveBass::latency(); const int64_t Wc = 128 * 40 + lat + a;   // the first sample of the chunk that gets the flood
    std::vector<std::pair<double, Act>> evs;
    evs.push_back({1000.0 / SR, [](LivePool& lp) { lp.note(mkNote(1000.0 / SR, 0, 0, 1), 0, 120, nullptr); }});
    auto wob = [&](int64_t T) { return [T](LivePool& lp) { SeqEvent e = mkMod((double)T / SR, 0); e.has |= SH_WOB; e.wob = 0.5; lp.mod(e, nullptr); }; };
    evs.push_back({(double)(Wc - 1) / SR, wob(Wc - 1)});                                  // one event the previous chunk leaves pending
    for (int i = 0; i < 255; i++) { const int64_t T = Wc + (i % 100); evs.push_back({(double)T / SR, wob(T)}); }
    const int64_t Toff = Wc + 120; evs.push_back({(double)Toff / SR, [Toff](LivePool& lp) { SeqEvent e = mkMod((double)Toff / SR, 0); lp.off(e); }});
    PoolRun r(LiveKind::Bass, Wc + 20000); r.run(evs);
    const float held = peakOf(r.L, 3000, 4000), tail = peakOf(r.L, r.L.size() - 1024, r.L.size());
    CHECKF(held > 0.01f, "alignment %d: the note did not sound (%g)", a, held);
    CHECKF(tail < 1e-3f, "alignment %d: a stuck note: the off was lost (tail %g)", a, tail);
    const LiveStats s = r.lp->stats(); if (s.forcedOff > 0 && s.queueDropped > 0) counted++; sensitive++;
  }
  CHECKF(counted > 0, "no alignment overflowed a queue (%d of %d): the test is not testing", counted, sensitive);
  std::printf("pool overflow: %d of %d alignments overflowed a queue and counted it; none stuck\n", counted, sensitive);
}
// L5, the pool timeline: a full timeline and an off before the events booked after it: they go (counted), the off stays (mutant X08)
static void testTimelineForcedOff() {
  std::vector<std::pair<double, Act>> evs;
  evs.push_back({1.0, [](LivePool& lp) { lp.note(mkNote(1.0, 0, 0, 1), 0, 120, nullptr); }});
  for (int i = 0; i < 255; i++) { const double t = 1.1 + i * 1e-4; evs.push_back({1.0, [t](LivePool& lp) { SeqEvent e = mkMod(t, 0); e.has |= SH_WOB; e.wob = 0.5; lp.mod(e, nullptr); }}); }
  evs.push_back({1.0, [](LivePool& lp) { SeqEvent e = mkMod(1.05, 0); lp.off(e); }});
  PoolRun r(LiveKind::Bass, 48000 * 2); r.run(evs);
  const LiveStats s = r.lp->stats();
  CHECKF(s.forcedOff == 1 && s.queueDropped == 255, "forced %u dropped %u (want 1 and the 255 wobbles booked after the off)", s.forcedOff, s.queueDropped);
  CHECKF(peakOf(r.L, 48000 + 3000, 48000 + 4000) > 0.01f, "the note did not sound");   // (sounding at 1.06 s?) off at 1.05: released by then, see the tail
  CHECKF(peakOf(r.L, r.L.size() - 1024, r.L.size()) < 1e-3f, "stuck note");
}

// ---- F4: hostile numbers -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void testHostile() {
  const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
  const double vals[] = {nan, inf, -inf, 1e300, -1e300};
  const LiveKind kinds[] = {LiveKind::Bass, LiveKind::Vox, LiveKind::Acid, LiveKind::Mach};
  struct Field { const char* name; bool inNote; };
  // the numeric fields a note or a mod carries; `inNote` fields ride on the note, the others on a mod
  const char* names[] = {"t", "rate", "gain", "pan", "slideFrom", "slideDur", "bendMul", "bendT1", "vibSpd", "vibDep", "vibT1", "wob", "cut", "rel", "mul", "modGain", "modPan", "modBendMul", "modBendT1", "modT", "bpm", "modWob"};
  int runs = 0, refusedRuns = 0, nonFinite = 0;
  for (LiveKind kind : kinds) for (size_t fi = 0; fi < sizeof names / sizeof names[0]; fi++) for (double v : vals) {
    const std::string f = names[fi];
    SeqEvent n = mkNote(0.01, 0, 0, 1); SeqEvent m = mkMod(0.05, 0);
    if (f == "t") n.t = v; else if (f == "rate") n.rate = v; else if (f == "gain") n.gain = v; else if (f == "pan") n.pan = v;
    else if (f == "slideFrom") { n.has |= SH_SLIDE; n.slideFrom = v; n.slideDur = 0.05; } else if (f == "slideDur") { n.has |= SH_SLIDE; n.slideFrom = 1; n.slideDur = v; }
    else if (f == "bendMul") { n.has |= SH_BEND; n.bendMul = v; n.bendT1 = 0.1; } else if (f == "bendT1") { n.has |= SH_BEND; n.bendMul = 2; n.bendT1 = v; }
    else if (f == "vibSpd") { n.has |= SH_VIB; n.vibSpd = v; n.vibDep = 3; n.vibT1 = 0.1; } else if (f == "vibDep") { n.has |= SH_VIB; n.vibSpd = 3; n.vibDep = v; n.vibT1 = 0.1; } else if (f == "vibT1") { n.has |= SH_VIB; n.vibSpd = 3; n.vibDep = 3; n.vibT1 = v; }
    else if (f == "wob") { n.has |= SH_WOB; n.wob = v; } else if (f == "cut") { n.has |= SH_CUT; n.cut = v; } else if (f == "rel") { n.has |= SH_REL; n.rel = v; }
    else if (f == "mul") { m.has |= SH_MUL; m.mul = v; } else if (f == "modGain") { m.has |= SH_GAIN; m.gain = v; } else if (f == "modPan") { m.has |= SH_PAN; m.pan = v; }
    else if (f == "modBendMul") { m.has |= SH_BEND; m.bendMul = v; m.bendT1 = 0.1; } else if (f == "modBendT1") { m.has |= SH_BEND; m.bendMul = 2; m.bendT1 = v; }
    else if (f == "modT") m.t = v; else if (f == "modWob") { m.has |= SH_WOB; m.wob = v; }
    PoolRun r(kind, 128 * 24);
    const double bpm = f == "bpm" ? v : 120;
    std::vector<std::pair<double, Act>> evs;
    evs.push_back({0.0, [&](LivePool& lp) { lp.note(n, 0, bpm, nullptr); }});
    evs.push_back({0.0, [&](LivePool& lp) { lp.mod(m, nullptr); }});
    if (f == "bpm") evs.push_back({0.0, [&](LivePool& lp) { lp.tempo(0.02, v); }});
    r.run(evs); runs++;
    bool fin = true; for (float x : r.L) if (!std::isfinite(x)) fin = false;
    if (!fin) { nonFinite++; CHECKF(false, "kind %d field %s value %g: the output is not finite", (int)kind, names[fi], v); }
    if (!std::isfinite(v) && r.lp->stats().refused > 0) refusedRuns++;
    else if (!std::isfinite(v) && f != "slideDur") CHECKF(false, "kind %d field %s value %g: not counted as refused", (int)kind, names[fi], v);
  }
  std::printf("hostile numbers: %d runs, %d with a non-finite output, %d refusals counted\n", runs, nonFinite, refusedRuns);
}

// P4: a bass node's bpm lane follows the sequencer's tempo at each note (a song's first tempo has no event), only when it changed (mutant X11)
static void testBpmAtNote() {
  LivePool lp(SR); LivePadConfig cfg; cfg.kind = LiveKind::Bass; cfg.trackMask = 1; CHECK(lp.addPad(0, cfg));
  lp.setTime(0);
  lp.note(mkNote(0.1, 0, 0, 1), 0, 140, nullptr); lp.note(mkNote(0.2, 0, 0, 1), 0, 140, nullptr); lp.note(mkNote(0.3, 0, 0, 1), 0, 90, nullptr);
  LiveDebugNode d; CHECK(lp.debugNode(0, 0, 0, &d));
  int nb = 0; double v0 = 0, v1 = 0; for (int i = 0; i < d.ntl; i++) if (d.tl[i].type == LiveEvType::Bpm) { if (nb == 0) v0 = d.tl[i].v; else v1 = d.tl[i].v; nb++; }
  CHECKF(nb == 2 && v0 == 140 && v1 == 90, "bpm events %d (%g, %g): want 140 once, then 90", nb, v0, v1);
}

int main() {
  testBpmAtNote();
  testShell();
  testPoolOverflow();
  testTimelineForcedOff();
  testHostile();
  if (g_fail) { std::printf("live_review: %d FAILED\n", g_fail); return 1; }
  std::printf("live_review: all checks passed\n"); return 0;
}
