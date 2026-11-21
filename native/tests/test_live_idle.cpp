// LIVE ROUTE 1b, the idle skip (docs/port/LIVE-ROUTE-NOTES.md): a node whose voice has ended and whose output is silent is not processed, and it resumes exactly as if it had been.
// The proof: two pools get the same script, one with the skip (the default) and one processing every node every quantum (setIdleSkip(false)); every quantum's mix of every track input is compared BIT FOR BIT, and the counters.
// The scripts leave long idle stretches (the skip must be engaged: idleNodes() > 0 and the other pool's 0), then start the first note AFTER the stretch at offsets that are not quantum, control-block or sample aligned, with a rate
// that differs from the one the idle core last saw (the vox core latches its pitch from the rate lane in idle blocks), after a tempo / wobble / engine / rate event and a stopAll in the stretch, on a node that never ran, with the
// vox syllable counter and the poly nodes. All four kinds. `test_live_idle --bench` prints the cost of a silent vox pad on 33 tracks with and without the skip.
//   test_live_idle
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
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

using Act = std::function<void(LivePool&)>;
struct Script { std::vector<std::pair<double, Act>> evs; double secs = 6; double voxGlide = 80; std::vector<size_t> mustWake; };   // mustWake: events that must take a node out of the skip (checked on the skipping pool: idleNodes() drops)   // (the time an event is handed over at, what it does)

static SeqEvent mkNote(double t, int track, double rate) { SeqEvent e; e.type = SEV_NOTE; e.t = t; e.track = track; e.pad = 0; e.rate = rate; e.gain = 1; e.pan = 128; return e; }
static Act N(double t, int tr, double rate, bool poly = false, int col = 0, bool slide = false, double slideDur = 0.15, int choke = 0) {
  SeqEvent e = mkNote(t, tr, rate); e.poly = poly; e.col = col; if (slide) { e.has |= SH_SLIDE; e.slideFrom = rate * 0.5; e.slideDur = slideDur; }
  return [e, choke](LivePool& lp) { bool p; lp.note(e, 0, 120, nullptr, choke, &p); };
}
static Act O(double t, int tr) { SeqEvent e; e.type = SEV_OFF; e.t = t; e.track = tr; return [e](LivePool& lp) { lp.off(e); }; }
static Act MUL(double t, int tr, double m) { SeqEvent e; e.type = SEV_MOD; e.t = t; e.track = tr; e.has |= SH_MUL; e.mul = m; return [e](LivePool& lp) { lp.mod(e, nullptr); }; }
static Act WOB(double t, int tr, double w) { SeqEvent e; e.type = SEV_MOD; e.t = t; e.track = tr; e.has |= SH_WOB; e.wob = w; return [e](LivePool& lp) { lp.mod(e, nullptr); }; }
static Act ENG(double t, int tr, double cut, double res, double ofs) {
  SeqEvent e; e.type = SEV_ENG; e.t = t; e.track = tr; for (int k = 0; k < 5; k++) e.engC[k] = -1; e.engC[0] = cut; e.engC[1] = res; for (int k = 0; k < 7; k++) e.engO[k] = 0; e.engO[0] = ofs; e.engO[6] = ofs * 0.5;
  return [e](LivePool& lp) { lp.eng(e); };
}
static Act TEMPO(double t, double bpm) { return [t, bpm](LivePool& lp) { lp.tempo(t, bpm); }; }
static Act STOPALL(double t) { return [t](LivePool& lp) { lp.stopAll(t); }; }
static void at(Script& s, double t, Act a) { s.evs.push_back({t - 0.25, std::move(a)}); }   // handed over 0.25 s ahead, as the sequencer does

static LivePadConfig cfgOf(LiveKind kind, double voxGlide) {
  LivePadConfig c; c.kind = kind; c.trackMask = (1u << 0) | (1u << 2);
  if (kind == LiveKind::Bass) { c.bass.rel = 40; c.bass.glide = 60; }
  if (kind == LiveKind::Vox) { c.vox.txt = "la di da ne"; c.vox.glide = voxGlide; c.vox.rel = 90; }
  return c;
}

struct Result { bool equal = true; uint32_t maxIdle = 0, idleOther = 0; int64_t firstDiff = -1; int wakeFail = 0; };
// Runs the script on a skipping and on a processing pool, quantum by quantum, and compares the two mixes bit for bit (both channels of tracks 0 and 2) and the counters.
static Result runPair(LiveKind kind, const Script& sc) {
  LivePool A(SR), B(SR); const LivePadConfig cfg = cfgOf(kind, sc.voxGlide);
  CHECK(A.addPad(0, cfg)); CHECK(B.addPad(0, cfg)); B.setIdleSkip(false);
  Result r; std::vector<char> doneA(sc.evs.size(), 0);
  std::vector<float> al(128), ar(128), a2l(128), a2r(128), bl(128), br(128), b2l(128), b2r(128);
  float* ia[kVoiceTracks] = {}; float* ra[kVoiceTracks] = {}; float* ib[kVoiceTracks] = {}; float* rb[kVoiceTracks] = {};
  ia[0] = al.data(); ra[0] = ar.data(); ia[2] = a2l.data(); ra[2] = a2r.data(); ib[0] = bl.data(); rb[0] = br.data(); ib[2] = b2l.data(); rb[2] = b2r.data();
  const int64_t total = (int64_t)(sc.secs * SR);
  for (int64_t q0 = 0; q0 < total; q0 += 128) {
    const double t0 = (double)q0 / SR; A.setTime(t0); B.setTime(t0);
    for (size_t i = 0; i < sc.evs.size(); i++) if (!doneA[i] && sc.evs[i].first <= t0) { doneA[i] = 1; const uint32_t before = A.idleNodes(); sc.evs[i].second(A); sc.evs[i].second(B);
      if (std::find(sc.mustWake.begin(), sc.mustWake.end(), i) != sc.mustWake.end() && !(A.idleNodes() < before)) r.wakeFail++; }
    for (auto* v : {&al, &ar, &a2l, &a2r, &bl, &br, &b2l, &b2r}) std::fill(v->begin(), v->end(), 0.f);
    A.render(ia, ra, 128, q0); B.render(ib, rb, 128, q0);
    r.maxIdle = std::max(r.maxIdle, A.idleNodes()); r.idleOther += B.idleNodes();
    if (r.equal && (std::memcmp(al.data(), bl.data(), 512) || std::memcmp(ar.data(), br.data(), 512) || std::memcmp(a2l.data(), b2l.data(), 512) || std::memcmp(a2r.data(), b2r.data(), 512))) {
      r.equal = false; r.firstDiff = q0;
    }
  }
  const LiveStats sa = A.stats(), sb = B.stats();
  if (sa.played != sb.played || sa.noNode != sb.noNode || sa.late != sb.late || sa.queueDropped != sb.queueDropped || sa.forcedOff != sb.forcedOff || sa.failed != sb.failed || sa.refused != sb.refused) r.equal = false;
  for (int tr : {0, 2}) { double s1, l1, k1, s2, l2, k2; if (A.debugVoxG(tr, 0, &s1, &l1, &k1) && B.debugVoxG(tr, 0, &s2, &l2, &k2) && (s1 != s2 || l1 != l2 || k1 != k2)) r.equal = false; }
  return r;
}


// ---- the shell's contract: fastForward(clock + N) == processing N samples of silence, for every N, bit for bit (including the output ring and the vox pitch latch) -----------------------------------------------------
template <class SH> static void setupCore(SH& sh) { (void)sh; }
template <> void setupCore<LiveBass>(LiveBass& sh) { BassP p; p.rel = 40; p.glide = 60; sh.core().set(p); }
template <> void setupCore<LiveVox>(LiveVox& sh) { VoxP p; p.txt = "la di"; p.glide = 80; sh.core().set(p); }
static LiveEvent lev(uint32_t at, LiveEvType t, double v = 0) { LiveEvent e; e.at = at; e.type = t; e.v = v; return e; }
template <class SH> static int shellCase(const char* name) {
  int cases = 0; const int Ns[] = {1, 3, 15, 16, 17, 31, 33, 100, 127, 128, 129, 200, 511, 512, 513, 700, 1000, 1500, 3000, 9000};
  for (int N : Ns) for (int rate2 = 0; rate2 < 2; rate2++) {
    SH A(SR), B(SR); setupCore(A); setupCore(B);
    std::vector<float> la(128), ra(128), lb(128), rb(128);
    auto both = [&](uint32_t n, const LiveEvent* ev, uint32_t nev) { A.process(la.data(), ra.data(), n, ev, nev); B.process(lb.data(), rb.data(), n, ev, nev); return std::memcmp(la.data(), lb.data(), n * 4) == 0 && std::memcmp(ra.data(), rb.data(), n * 4) == 0; };
    bool eq = true; int chunks = 0;
    { const LiveEvent ev[] = {lev(0, LiveEvType::RateSet, 0.7), lev(3, LiveEvType::NoteOn)}; eq &= both(128, ev, 2); }
    { const LiveEvent ev[] = {lev(10, LiveEvType::NoteOff)}; eq &= both(128, ev, 1); }
    while (!A.idle() && chunks < 2000) { eq &= both(128, nullptr, 0); chunks++; }
    CHECKF(A.idle() && B.idle(), "%s: the shell never went idle", name);
    // a second, short note at another rate: the pitch latch is still gliding (slowly: 80 ms) when the voice ends, so the number of idle blocks counts
    { const LiveEvent ev[] = {lev(0, LiveEvType::RateSet, 1.5), lev(5, LiveEvType::NoteOn)}; eq &= both(128, ev, 2); }
    { const LiveEvent ev[] = {lev(60, LiveEvType::NoteOff)}; eq &= both(128, ev, 1); }
    chunks = 0; while (!A.idle() && chunks < 2000) { eq &= both(128, nullptr, 0); chunks++; }
    CHECKF(A.idle() && B.idle(), "%s: the shell never went idle (2)", name);
    // A: N samples of silence processed; B: the same by fastForward
    for (int left = N; left > 0;) { const uint32_t m = (uint32_t)std::min(left, 128); A.process(la.data(), ra.data(), m, nullptr, 0); left -= (int)m; }
    B.fastForward(B.clock() + (uint64_t)N);
    CHECKF(A.clock() == B.clock(), "%s N=%d: the clocks differ (%llu, %llu)", name, N, (unsigned long long)A.clock(), (unsigned long long)B.clock());
    { const uint32_t o = 1 + (uint32_t)(N % 13) + (uint32_t)rate2 * 16;   // (the note starts 1 .. 28 samples after the stretch: before the next control block ends)
      const LiveEvent ev[] = {lev(o > 1 ? o - 1 : 0, LiveEvType::RateSet, rate2 ? 1.3 : 0.7), lev(o, LiveEvType::NoteOn), lev(100, LiveEvType::Bpm, 133)}; eq &= both(128, ev, 3); }
    for (int i = 0; i < 6; i++) eq &= both(128, nullptr, 0);
    { const LiveEvent ev[] = {lev(20, LiveEvType::NoteOff)}; eq &= both(128, ev, 1); }
    for (int i = 0; i < 8; i++) eq &= both(128, nullptr, 0);
    CHECKF(eq, "%s N=%d rate2=%d: the fast-forwarded shell differs from the processed one", name, N, rate2); cases++;
  }
  return cases;
}

static const char* nameOf(LiveKind k) { return k == LiveKind::Bass ? "bass" : k == LiveKind::Vox ? "vox" : k == LiveKind::Acid ? "acid" : "mach"; }
static int g_runs = 0;
static void expect(LiveKind kind, const char* what, const Script& sc) {
  const Result r = runPair(kind, sc); g_runs++;
  CHECKF(r.equal, "%s / %s: the skipping pool differs from the processing one (first differing quantum at sample %lld)", nameOf(kind), what, (long long)r.firstDiff);
  CHECKF(r.maxIdle > 0, "%s / %s: the skip was never engaged (the test is not testing)", nameOf(kind), what);
  CHECKF(r.wakeFail == 0, "%s / %s: an event that must end the skip of a node did not (%d)", nameOf(kind), what, r.wakeFail);
  CHECKF(r.idleOther == 0, "%s / %s: the processing pool skipped", nameOf(kind), what);
}

int main(int argc, char** argv) {
  if (argc > 1 && !std::strcmp(argv[1], "--bench")) {
    for (int skip = 0; skip < 2; skip++) for (int played = 0; played < 2; played++) {
      LivePool lp(SR); LivePadConfig c; c.kind = LiveKind::Vox; c.trackMask = 0; c.vox.txt = "la di da ne"; lp.addPad(0, c); lp.setIdleSkip(skip != 0);
      std::vector<float> L(128 * 33), R(128 * 33); float* in[kVoiceTracks]; float* inr[kVoiceTracks]; for (int t = 0; t < kVoiceTracks; t++) { in[t] = L.data() + 128 * t; inr[t] = R.data() + 128 * t; }
      if (played) for (int t = 0; t < 33; t++) { lp.setTime(0); lp.note(mkNote(0.01, t, 1), 0, 120, nullptr); SeqEvent o; o.type = SEV_OFF; o.t = 0.2; o.track = t; lp.off(o); }
      const int64_t total = (int64_t)(20 * SR); const auto t0 = std::chrono::steady_clock::now();
      for (int64_t q = 0; q < total; q += 128) { lp.setTime((double)q / SR); lp.render(in, inr, 128, q); }
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      std::printf("bench: silent vox pad, 33 tracks, 20 s, %s, %s: %.0f ms (%.2fx real time), %u nodes skipped\n", played ? "every track played one note first" : "never played", skip ? "idle skip ON" : "idle skip OFF", ms, ms / 20000.0, lp.idleNodes());
    }
    return 0;
  }
  { const int c = shellCase<LiveBass>("bass") + shellCase<LiveVox>("vox") + shellCase<LiveAcid>("acid") + shellCase<LiveMach>("mach"); std::printf("idle skip: %d shell fast-forward cases (every N from 1 to 9000 samples) equal to processing, bit for bit\n", c); }
  const LiveKind kinds[] = {LiveKind::Bass, LiveKind::Vox, LiveKind::Acid, LiveKind::Mach};
  for (LiveKind kind : kinds) {
    // 1. the first note after an idle stretch, at offsets that are not aligned to a quantum, a control block or a sample step, with a rate that differs from the last one (0.7 -> 1.3)
    for (int k = 0; k < 6; k++) {
      Script s; s.secs = 7;
      const double T2 = 2.0 + 0.000371 * k * 17 + 0.00001 * k;
      at(s, 0.10, N(0.10, 0, 0.7)); at(s, 0.35, O(0.35, 0)); at(s, T2, N(T2, 0, 1.3)); at(s, T2 + 0.2, O(T2 + 0.2, 0));
      at(s, T2 + 3.1, N(T2 + 3.1, 0, 0.5)); at(s, T2 + 3.3, O(T2 + 3.3, 0)); at(s, 1.0, N(1.0, 2, 1.0)); at(s, 1.1, O(1.1, 2)); at(s, 4.77, N(4.77, 2, 2.0));
      expect(kind, "first note after an idle stretch", s);
    }
    // 2. a short stretch (just past the end of the voice), so that the skip starts late in the stretch, and a note at 3 sample offsets inside one control block
    for (int k = 0; k < 4; k++) {
      Script s; s.secs = 3;
      const double T2 = 0.62 + 0.000021 * k * 3; at(s, 0.05, N(0.05, 0, 0.9)); at(s, 0.25, O(0.25, 0)); at(s, T2, N(T2, 0, 1.6)); at(s, T2 + 0.15, O(T2 + 0.15, 0));
      at(s, 1.9 + 0.0003 * k, N(1.9 + 0.0003 * k, 0, 0.4));
      expect(kind, "a short stretch", s);
    }
    // 3. a slide before the stretch, tempo / wobble / engine events and a rate change inside it, then the note
    { Script s; s.secs = 7;
      at(s, 0.10, N(0.10, 0, 1.0, false, 0, true)); at(s, 0.40, O(0.40, 0));
      at(s, 1.0, TEMPO(1.0, 90)); at(s, 1.2, ENG(1.2, 0, 40, 0.3, 12)); at(s, 1.4, WOB(1.4, 0, 0.5)); at(s, 1.6, ENG(1.6, 0, -1, -1, 0)); at(s, 1.8, N(1.8, 0, 1.1)); at(s, 2.0, O(2.0, 0));
      at(s, 3.0, STOPALL(3.0)); at(s, 3.5, N(3.5, 0, 0.9)); at(s, 3.7, O(3.7, 0));
      at(s, 4.2, TEMPO(4.2, 140)); at(s, 4.6, N(4.6, 0, 0.6, false, 0, true)); at(s, 4.8, O(4.8, 0));
      expect(kind, "events inside the stretch", s); }
    // 4. a node that never ran (skipped from its first quantum) gets an engine event, a tempo and then its first note; the engine state of the track was kept for it
    { Script s; s.secs = 5;
      at(s, 0.10, N(0.10, 0, 1.0)); at(s, 0.3, O(0.3, 0)); at(s, 1.0, ENG(1.0, 2, 55, 0.6, 7)); at(s, 1.5, TEMPO(1.5, 100)); at(s, 2.7, N(2.7, 2, 0.8)); at(s, 2.9, O(2.9, 2)); at(s, 4.0, N(4.0, 2, 1.2)); at(s, 4.1, O(4.1, 2));
      expect(kind, "a node that never ran", s); }
    // 4b. a slide that outlasts the voice: the rate lane is still moving while the node is idle (the vox core latches the pitch from it every idle block), so the skip must wait until the lane is constant
    { Script s; s.secs = 8;
      at(s, 0.10, N(0.10, 0, 1.2, false, 0, true, 3.0)); at(s, 0.30, O(0.30, 0)); at(s, 2.0, N(2.0, 0, 0.8)); at(s, 2.2, O(2.2, 0)); at(s, 6.0, N(6.0, 0, 1.5)); at(s, 6.2, O(6.2, 0));
      expect(kind, "a slide that outlasts the voice", s); }
    // 4c. a voice another track's note choked is over, but it is still in its track's list (a mod reaches it): a `mul` writes its rate lane while the node idles, with nothing booked (the lane is not constant any more)
    { Script s; s.secs = 7; s.voxGlide = 900;
      at(s, 0.10, N(0.10, 0, 1.0, false, 0, false, 0.15, 1)); at(s, 0.50, N(0.50, 2, 1.0, false, 0, false, 0.15, 1)); at(s, 0.70, O(0.70, 2));
      s.mustWake.push_back(s.evs.size()); s.evs.push_back({3.70, MUL(0.45, 0, 2.0)});   /* (a mod timed before the stop, delivered late, a little before the next note (handed over at 3.75): it still reaches the voice, and the idle blocks after it, a slow glide, must see its rate) */ at(s, 4.0, N(4.0, 0, 1.3));  at(s, 4.2, O(4.2, 0));
      expect(kind, "a mod on a choked, idle node", s); }
    // 4d. the first note of a node handed over right after the pool's first quantum (the node ran no quantum yet: its first idle blocks are the wake's), and a very slow glide (the pitch latch has not converged: the number of idle blocks counts)
    { Script s; s.secs = 3; s.voxGlide = 900;
      s.evs.push_back({128.0 / SR, N(0.26, 2, 1.3)}); at(s, 0.5, O(0.5, 2));   /* (handed over at the pool's second quantum) */
      at(s, 0.10, N(0.10, 0, 0.5)); at(s, 0.30, O(0.30, 0)); at(s, 0.60, N(0.60, 0, 1.7)); at(s, 0.75, O(0.75, 0)); at(s, 1.9, N(1.9, 0, 1.2)); at(s, 2.1, O(2.1, 0));
      expect(kind, "the first note of a node right after the first quantum, a slow glide", s); }
    // 4e. the first note after the stretch at every alignment of the ring of the shell (512 samples) and of the control block: the shell's output ring must hold silence where the idle blocks would have written it
    for (int k = 0; k < 60; k++) {
      Script s; s.secs = 1.6; const double T2 = 0.55 + k * 0.0063;
      at(s, 0.05, N(0.05, 0, 1.0)); at(s, 0.15, O(0.15, 0)); at(s, T2, N(T2, 0, 1.4)); at(s, T2 + 0.1, O(T2 + 0.1, 0));
      expect(kind, "a short stretch at one of 60 alignments", s);
    }
    // 5. a note that is never released before the stretch cannot be skipped (the voice has not ended), and a long hold ends only with its off
    { Script s; s.secs = 6;
      at(s, 0.10, N(0.10, 0, 1.0)); at(s, 2.0, O(2.0, 0)); at(s, 4.0, N(4.0, 0, 0.7)); at(s, 4.2, O(4.2, 0));
      expect(kind, "a hold", s); }
  }
  // 6. vox: the poly nodes and the syllable counter, mono and poly notes after stretches
  { Script s; s.secs = 9;
    at(s, 0.10, N(0.10, 0, 1.0, true, 0)); at(s, 0.10, N(0.10, 0, 1.5, true, 1)); at(s, 0.40, O(0.40, 0));
    at(s, 2.5, N(2.5, 0, 0.8, true, 0)); at(s, 2.7, O(2.7, 0)); at(s, 4.0, N(4.0, 0, 1.2, true, 2)); at(s, 4.0, N(4.0, 0, 0.9, true, 0)); at(s, 4.3, O(4.3, 0));
    at(s, 5.5, N(5.5, 0, 1.1)); at(s, 5.7, O(5.7, 0)); at(s, 7.0, N(7.0, 0, 1.3, true, 3)); at(s, 7.2, O(7.2, 0)); at(s, 8.0, N(8.0, 0, 0.6));
    at(s, 3.2, ENG(3.2, 0, 60, 0.2, 9));
    expect(LiveKind::Vox, "poly nodes and the syllable counter", s); }
  std::printf("idle skip: %d skipping / processing pairs compared bit for bit\n", g_runs);
  if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
  std::printf("PASS\n");
  return 0;
}
