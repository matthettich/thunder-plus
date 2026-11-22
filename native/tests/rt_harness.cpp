// A5: THE REAL-TIME TEST HARNESS. Runs the core's audio callback offline under an allocator that aborts when anything is
// allocated or freed while the callback is running, and checks the clock-drift logger (thunder/rtlog.h) against a made-up clock.
//   rt_harness            the test core: 60 s of blocks of mixed sizes, commands arriving meanwhile, no allocation -> exit 0
//   rt_harness --selftest proves the guard works: a child process allocates inside the guarded region and MUST die
//                         (SIGABRT); also the same with malloc and with free. Exit 0 only if every child was killed.
// The gate (WORKPLAN, N0 A5): fails on a deliberate `new` in the callback; passes on the test core.
// Every port unit from N1 on runs its process() through rt_guard the same way.
#include "thunder/core.h"
#include "thunder/rtlog.h"

#include "seq/seq.h"
#include "seq/runner.h"
#include "seq/session.h"
#include <chrono>
#include "song/song.h"
#include "mix/mix.h"
#include "mix/pdc.h"
#include "master/master.h"
#include "master/chain.h"
#include "meter/meter.h"
#include "fx/fxbank.h"
#include "modfx/mods.h"
#include "render/render.h"
#include "render/players.h"
#include "render/pads_snap.h"
#include "engine/engine.h"
#include "engine/layer_pe.h"
#include "engine/xeng.h"
#ifdef THUNDER_HAVE_LXR
#include "thunder/lxr_module.h"
#endif
#include "engine/xeng_live.h"
#include "engine/bass_core.h"
#include "engine/vox_core.h"
#include "engine/live_shell.h"
#include "engine/pumpsynth.h"
#include "voice/voice.h"
#include "sf/sf.h"
#include "sf/sf_live.h"
#include "waves/waves.h"
#include "chains/chains.h"
#include "live/live_host.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#define RT_HAVE_FORK 1
#endif

// ---- the guard: thread-local, so only the thread inside the callback is policed -------------------------------------
static thread_local int rt_depth = 0;
static void rt_violation(const char* what) {
  // no stdio buffering tricks: write and abort (fprintf may allocate, so use a fixed message via fputs on stderr, unbuffered)
  rt_depth = 0; std::fputs("rt_harness: ", stderr); std::fputs(what, stderr); std::fputs(" inside the audio callback\n", stderr); std::abort();
}
struct rt_guard { rt_guard() { rt_depth++; } ~rt_guard() { rt_depth--; } };

static thread_local unsigned long rt_mallocs = 0;   // malloc / calloc calls outside the guard (glibc, not under ASan: U25's per-thread workspace)
static thread_local unsigned long rt_allocs = 0;   // operator new calls outside the guard (U33: what an event-bearing block of pump's synths allocates is counted, not forbidden)
void* operator new(std::size_t n) { if (rt_depth) rt_violation("operator new"); rt_allocs++; void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new[](std::size_t n) { if (rt_depth) rt_violation("operator new[]"); void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (rt_depth) rt_violation("operator new (nothrow)"); rt_allocs++; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { if (rt_depth) rt_violation("operator new[] (nothrow)"); return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { if (rt_depth && p) rt_violation("operator delete"); std::free(p); }
void operator delete[](void* p) noexcept { if (rt_depth && p) rt_violation("operator delete[]"); std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (rt_depth && p) rt_violation("operator delete"); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (rt_depth && p) rt_violation("operator delete[]"); std::free(p); }

// malloc / free themselves (C code, std::vector's allocator on some platforms): glibc lets us wrap them.
// (Not under AddressSanitizer: it brings its own malloc / free / posix_memalign, and wrapping a part of them hands its memory to glibc: "free(): invalid size". The operator new / delete guards above stay.)
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
extern "C" {
void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (rt_depth) rt_violation("malloc"); rt_mallocs++; return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (rt_depth) rt_violation("calloc"); rt_mallocs++; return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (rt_depth) rt_violation("realloc"); return __libc_realloc(p, n); }
void free(void* p) { if (rt_depth && p) rt_violation("free"); __libc_free(p); }
}
#define RT_MALLOC_GUARDED 1
#endif

static int fail(const char* msg) { std::fprintf(stderr, "rt_harness FAIL: %s\n", msg); return 1; }

// ---- the test core under the guard ---------------------------------------------------------------------------------
static int run_core() {
  const int SR = 48000, MAXB = 1024;
  thunder_core* c = thunder_create(SR, MAXB);
  if (!c) return fail("thunder_create");
  std::vector<float> l(MAXB), r(MAXB);
  const char* play = "{\"t\":\"play\"}"; thunder_post(c, play, (int)std::strlen(play));
  // block sizes a phone really hands out: bursts of 96 / 192 / 240 / 480, and odd ones
  const int sizes[] = {96, 192, 240, 480, 128, 1, 1024, 441, 7, 512};
  int64_t frames = 0; double peak = 0, energy = 0; unsigned seed = 1; int bi = 0; char ev[256];
  rtlog log; rtlog_reset(&log, SR);
  int64_t fake_ns = 1000000000LL;   // a made-up clock that runs exactly at the sample rate
  while (frames < (int64_t)SR * 60) {
    const int n = sizes[bi++ % 10];
    if ((bi % 97) == 0) { char cmd[64]; seed = seed * 1664525u + 1013904223u; int len = std::snprintf(cmd, sizeof cmd, "{\"t\":\"bpm\",\"v\":%d}", 60 + (int)(seed >> 24)); thunder_post(c, cmd, len); }
    if ((bi % 31) == 0) { const char* e = "{\"t\":\"echo\",\"n\":7}"; thunder_post(c, e, (int)std::strlen(e)); }
    // U05: timed commands (they wait in the callback's own list) and song snapshots (swapped inside the callback, freed outside it)
    if ((bi % 41) == 0) { char cmd[96]; int len = std::snprintf(cmd, sizeof cmd, "{\"t\":\"echo\",\"n\":9,\"at\":%lld}", (long long)(frames + 1500)); thunder_post(c, cmd, len); }
    if ((bi % 53) == 0) { char cmd[96]; int len = std::snprintf(cmd, sizeof cmd, "{\"t\":\"song.load\",\"rev\":%d,\"S\":{}}", bi); thunder_post(c, cmd, len); }
    rtlog_begin(&log, fake_ns);
    { rt_guard g; thunder_process(c, l.data(), r.data(), n); }   // <- THE GUARDED CALL
    rtlog_end(&log, fake_ns + 1000, n);
    fake_ns += (int64_t)n * 1000000000LL / SR;
    for (int i = 0; i < n; i++) { const double v = std::fabs(l[(size_t)i]); if (v > peak) peak = v; energy += v * v; if (!(v < 4.0)) return fail("a sample is not finite or far too loud"); }
    frames += n;
    while (thunder_poll(c, ev, (int)sizeof ev) > 0) {}   // the consumer keeps the event ring from filling
  }
  thunder_destroy(c);
  if (!(peak > 0.05)) return fail("the test core made no sound");
  const rtlog_snapshot s = rtlog_read(&log);
  if (!(std::fabs(s.ratio_ppm) < 50.0)) { std::fprintf(stderr, "ratio %.1f ppm\n", s.ratio_ppm); return fail("the logger does not read 1.00 s per 1.00 s on an exact clock"); }
  if (!s.pass) return fail("the logger's pass flag is off on a clean run");
  std::printf("rt_harness: test core OK: %lld frames, no allocation in the callback, peak %.3f, logger %.1f ppm over %.1f s, max gap %.2f ms\n", (long long)frames, peak, s.ratio_ppm, s.wall_s, s.max_gap_ms);
  return 0;
}

// ---- the logger against clocks that are wrong on purpose -----------------------------------------------------------
static int run_logger() {
  const int SR = 48000, N = 192;
  auto run = [&](double speed, int stall_at, int slow_at) {   // speed: wall seconds per second of audio
    rtlog g; rtlog_reset(&g, SR); int64_t ns = 5000000000LL;
    for (int b = 0; b < SR * 30 / N; b++) {
      rtlog_begin(&g, ns); rtlog_end(&g, ns + (b == slow_at ? 9000000 : 200000), N);
      ns += (int64_t)((double)N * 1e9 / SR * speed) + (b == stall_at ? 250000000LL : 0);
    }
    return rtlog_read(&g);
  };
  rtlog_snapshot a = run(1.0, -1, -1); if (!a.pass || std::fabs(a.ratio_ppm) > 50) return fail("logger: exact clock");
  rtlog_snapshot b = run(1.0 / 0.85, -1, -1);   // the owner's Windows report: the clock at 0.85x
  if (b.pass || !(b.ratio_ppm < -140000 && b.ratio_ppm > -160000)) { std::fprintf(stderr, "ratio %.0f\n", b.ratio_ppm); return fail("logger: a clock at 0.85x must read about -150000 ppm and not pass"); }
  rtlog_snapshot c = run(1.002, -1, -1); if (c.pass) return fail("logger: 0.2 % slow must not pass (the gate is 0.1 %)");
  rtlog_snapshot d = run(1.0005, -1, -1); if (!d.pass) return fail("logger: 0.05 % off is inside the gate");
  rtlog_snapshot e = run(1.0, 4000, -1); if (!(e.max_gap_ms > 250 && e.max_gap_ms < 260)) return fail("logger: a 250 ms stall must show as the longest gap");
  rtlog_snapshot f = run(1.0, -1, 5000); if (f.late != 1 || f.pass || !(f.max_work_ms > 8.9)) return fail("logger: a 9 ms callback for 4 ms of audio is late");
  rtlog g; rtlog_reset(&g, SR); int64_t ns = 1; for (int k = 0; k < SR * 3 / N; k++) { rtlog_begin(&g, ns); rtlog_end(&g, ns + 1000, N); ns += (int64_t)N * 1000000000LL / SR; }
  rtlog_host_underruns(&g, 2); if (rtlog_read(&g).pass) return fail("logger: a host underrun must fail the run");
  std::printf("rt_harness: logger OK (exact, 0.85x = %.0f ppm, gate at 0.1 %%, stall %.0f ms, late callbacks, underruns)\n", b.ratio_ppm, e.max_gap_ms);
  return 0;
}

// ---- the guard must really kill -------------------------------------------------------------------------------------
static volatile void* sink;
static int selftest() {
#ifdef RT_HAVE_FORK
  struct Case { const char* name; int kind; };
  const Case cases[] = {{"new int inside the callback", 0}, {"std::vector growing inside the callback", 1}, {"delete inside the callback", 2},
#ifdef RT_MALLOC_GUARDED
    {"malloc inside the callback", 3}, {"free inside the callback", 4},
#endif
  };
  for (const Case& k : cases) {
    std::fflush(stdout); std::fflush(stderr);
    const pid_t pid = fork();
    if (pid < 0) return fail("fork");
    if (pid == 0) {
      if (!std::freopen("/dev/null", "w", stderr)) {}
      void* pre = ::operator new(4); void* m = std::malloc(8); std::vector<int> v;
      { rt_guard g;   // what a callback that breaks the rule looks like
        if (k.kind == 0) sink = ::operator new(42);   /* the function, not a new-expression: clang may elide a new-expression whose result goes nowhere (CI, 15 Nov) */
        else if (k.kind == 1) { for (int i = 0; i < 100; i++) v.push_back(i); sink = v.data(); }
        else if (k.kind == 2) ::operator delete(pre);
        else if (k.kind == 3) sink = std::malloc(16);
        else std::free(m);
      }
      _exit(0);   // reached only if the guard let it through
    }
    int st = 0; if (waitpid(pid, &st, 0) < 0) return fail("waitpid");
    if (!(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT)) { std::fprintf(stderr, "case: %s\n", k.name); return fail("the guard let an allocation through (the harness would prove nothing)"); }
    std::printf("rt_harness: guard kills: %s\n", k.name);
  }
  return 0;
#else
  std::printf("rt_harness: selftest skipped on this platform (no fork); the guard itself is the same code\n");
  return 0;
#endif
}

// ---- U07: the sequencer's step() under the guard ---------------------------------------------------------------------
// The song is read and the sequencer built outside the guard (the control thread's work); every step() runs inside it.
// ELECTRO three ways: the whole song played live (it wraps: songWrap, wake), a render from row 9 (CHASE), and one phrase looped.
static int run_seq() {
  std::string text; { std::ifstream f(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder", std::ios::binary); if (!f) return fail("cannot read sounds/ELECTRO.thunder"); std::ostringstream o; o << f.rdbuf(); text = o.str(); }
  thunder::Song song; thunder::JsonError je; if (!song.load(text.data(), text.size(), &je)) return fail("ELECTRO does not load");
  thunder::SeqVars vars; const int CAP = 1024; std::vector<thunder::SeqEvent> evs((size_t)CAP); thunder::SeqStep st;
  for (int k = 0; k < 3; k++) {
    thunder::SeqOpts o; o.sim = k != 0; o.live = k == 0; o.once = k == 1; if (k == 1) o.row = 9; if (k == 2) { o.track = 0; o.phrase = 0; }
    thunder::Seq sq(song, vars, k == 2 ? thunder::SeqMode::Phrase : thunder::SeqMode::Song, o);
    long total = 0; int steps = 0; double last = -1; bool order = true;
    { rt_guard g; for (; steps < 6000 && sq.alive(); steps++) { total += sq.step(evs.data(), CAP, &st); if (st.t < last) order = false; last = st.t; } }
    if (!order) return fail("the sequencer's steps went back in time");
    if (sq.dropped()) return fail("the sequencer dropped events");
    if (total < steps) return fail("the sequencer made fewer events than steps (every step has its beat)");
    std::printf("rt_harness: sequencer run %d: %d steps, %ld events, no allocation\n", k, steps, total);
  }
  return 0;
}

// ---- CHOP: step() with chop on under the guard ---------------------------------------------------------------------------------------------------
// Seven instruments (slc 4 grid, slc 16 with SLC, slc 4 with SMODE hits, sync 1, 2, 3, slc 8 with RET retriggers) on a looping phrase, a SeqChop handed in; every step() is inside the guard. The song and the table are the control thread's work.
static int run_seq_chop() {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0,1,2,3,4,5,6]],\"chains\":[[[0,0]],[[1,0]],[[2,0]],[[3,0]],[[4,0]],[[5,0]],[[6,0]]],\"plen\":[16,16,16,16,16,16,16],\"insts\":["
    "{\"pad\":0,\"slc\":4},{\"pad\":1,\"slc\":16},{\"pad\":0,\"slc\":4,\"smode\":1},{\"pad\":1,\"sync\":1},{\"pad\":0,\"sync\":2,\"beats\":4,\"grain\":2},{\"pad\":1,\"sync\":3,\"beats\":2},{\"pad\":0,\"slc\":8}],\"phrases\":[";
  for (int t = 0; t < 7; t++) { s += t ? ",[" : "[";
    for (int y = 0; y < 32; y++) { const int hit = y < 16 && y % 2 == 0; s += std::string(y ? "," : "") + "[" + std::to_string(hit ? 44 + (y * 3 + t) % 12 : -1) + ",-1,-1,-1,-1," + std::to_string(t) + ","
        + (t == 1 && hit ? "65,5" : "-1,-1") + "," + (t == 6 && hit ? "11,36" : "-1,-1") + (t == 4 && hit ? ",25,128" : ",-1,-1") + "]"; }
    s += "]"; }
  s += "]}";
  thunder::Song song; thunder::JsonError je; if (!song.load(s.data(), s.size(), &je)) return fail("the chop song does not load");
  thunder::SeqChop chop; const double hits[] = {0, 0.31, 0.77, 1.4}; chop.setLoop(0, 2.0); chop.setLoop(1, 1.6); chop.setHits(0, 4, hits, 4);
  thunder::SeqVars vars; thunder::SeqOpts o; o.sim = false; o.chop = &chop; o.once = false;
  thunder::Seq sq(song, vars, thunder::SeqMode::Song, o);
  const int CAP = 4096; std::vector<thunder::SeqEvent> evs((size_t)CAP); thunder::SeqStep st; int steps = 0; long chopped = 0;
  { rt_guard g; for (; steps < 3000; steps++) { const int k = sq.step(evs.data(), CAP, &st); for (int i = 0; i < k; i++) { if (evs[i].has & thunder::SH_CHOP) chopped++; } } }
  if (sq.dropped()) return fail("the chop run dropped events");
  if (chopped == 0 || sq.chopped() == 0 || sq.chopStretched() == 0) return fail("the chop run chopped nothing (the guard would prove nothing)");
  std::printf("rt_harness: sequencer with chop: %d steps, %ld chopped notes (%llu with st), no allocation\n", steps, chopped, (unsigned long long)sq.chopStretched());
  return 0;
}

// ---- SEQ-LIVE A3: the runner and the clock setters under the guard ---------------------------------------------------------------
// ELECTRO played live from a SeqRunner, a block of 128 frames at 48 kHz at a time. Inside the guard, every block: pump(); and at fixed blocks
// setTime (before the first step, by the control path), setBpm, shift, hold(), a stalled `now` (the catch-up skip). Nothing may allocate.
static int run_seq_runner() {
  std::string text; { std::ifstream f(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder", std::ios::binary); if (!f) return fail("cannot read sounds/ELECTRO.thunder"); std::ostringstream o; o << f.rdbuf(); text = o.str(); }
  thunder::Song song; thunder::JsonError je; if (!song.load(text.data(), text.size(), &je)) return fail("ELECTRO does not load");
  thunder::SeqVars vars; const int CAP = 1024; std::vector<thunder::SeqEvent> evs((size_t)CAP); thunder::SeqStep st;
  thunder::SeqOpts o; o.live = true; thunder::Seq sq(song, vars, thunder::SeqMode::Song, o);
  thunder::SeqRunner run; run.start(&sq);
  long steps = 0, held = 0, ended = 0; double now = 100;
  { rt_guard g;
    sq.setTime(now + 0.1); sq.setTime(std::nan("")); sq.setBpm(std::nan("")); sq.setBpm(0); sq.setBpm(-5); sq.shift(std::nan(""));   // the refusals under the guard too
    for (int b = 0; b < 20000; b++) {
      if (b == 300) sq.setBpm(97.3);
      if (b == 900) sq.setBpm(131);
      if (b == 500) sq.shift(0.37);
      if (b == 700) sq.shift(-0.011);
      if (b == 1200) now += 0.5;                 // a stalled audio thread: the skip
      if (b == 18000) run.hold();               // EXACT STOP: the song ends at its next pattern edge
      const thunder::SeqPump r = run.pump(now, b < 100 ? 0.6 : 0.25, evs.data(), CAP, &st, [&](const thunder::SeqEvent*, int, const thunder::SeqStep&) { steps++; });
      if (r.held) held++; if (r.ended) ended++;
      now += 128.0 / 48000;
    } }
  if (steps < 200) return fail("the runner played too few steps");
  if (held != 1) return fail("the held stop did not report exactly once");
  if (ended != 0) return fail("the runner reported an end with the song still running");
  if (run.skips() < 1) return fail("the stalled block did not skip");
  if (sq.dropped()) return fail("the sequencer dropped events");
  std::printf("rt_harness: sequencer runner: %ld steps, %llu skips, %llu refused sets, no allocation\n", steps, (unsigned long long)run.skips(), (unsigned long long)sq.badSets());
  return 0;
}

// ---- SEQ-LIVE Part B: seq_adopt (takeOver + rebind) and the pump under the guard ---------------------------------------------------------------
// ELECTRO live through a SeqSession. The control side (outside the guard) publishes a new snapshot every 40 blocks: the same Song re-pointed (a vars-only
// message) and a Song read again, three at once now and then (the skipped snapshots). Inside the guard, every block: seq_adopt() (takeOver, SeqRunner::rebind,
// the slot's pop / push) and pump(). Nothing may allocate. takeOver's own wall time is measured on the audio side and printed (the worst case).
static int run_seq_takeover() {
  using namespace thunder;
  std::string text; { std::ifstream f(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder", std::ios::binary); if (!f) return fail("cannot read sounds/ELECTRO.thunder"); std::ostringstream o; o << f.rdbuf(); text = o.str(); }
  std::shared_ptr<Song> s1 = std::make_shared<Song>(), s2 = std::make_shared<Song>(); JsonError je;
  if (!s1->load(text.data(), text.size(), &je) || !s2->load(text.data(), text.size(), &je)) return fail("ELECTRO does not load");
  SeqVars vars; SeqOpts o; o.live = true; std::shared_ptr<SeqSession> sess = SeqSession::create(SeqMode::Song, o); SnapSlot<SongSnap> slot; SeqRunner run;
  if (!sess->publish(slot, s1, vars)) return fail("first publish"); seq_adopt(slot, run); run.start(slot.get()->seq.get()); run.seq()->setTime(100);
  const int CAP = 12288; std::vector<SeqEvent> evs((size_t)CAP); SeqStep st; long steps = 0, adopts = 0; double now = 100, worst = 0;
  for (int b = 0; b < 12000; b++) {
    if (b % 40 == 20) { if (b % 200 == 20) { sess->publish(slot, s2, vars); sess->publish(slot, s1, vars); } if (!sess->publish(slot, (b / 40) % 2 ? s1 : s2, vars)) return fail("publish"); }
    bool changed;
    { rt_guard g;
      const auto t0 = std::chrono::steady_clock::now();
      changed = seq_adopt(slot, run);
      const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); if (changed && dt > worst) worst = dt;
      run.pump(now, 0.25, evs.data(), CAP, &st, [&](const SeqEvent*, int, const SeqStep&) { steps++; }); }
    if (changed) adopts++;
    slot.collect(); now += 128.0 / 48000;
  }
  if (steps < 400) return fail("the live sequencer played too few steps");
  if (adopts < 200) return fail("too few adoptions");
  if (run.seq()->dropped()) return fail("the sequencer dropped events");
  std::printf("rt_harness: seq takeOver: %ld steps, %ld adoptions (seq_adopt incl. the slot, worst %.1f us), no allocation\n", steps, adopts, worst * 1e6);
  return 0;
}

// ---- U08: the sampler voice pool under the guard ----------------------------------------------------------------------------
// The pool and the pads are made outside the guard (the control thread); every note / off / mod / stp / stopAll and every render() runs
// inside it: notes of every kind (poly, reverse, CUT / RES / DEC shaped, slide, arpeggio, vibrato, tremolo, fade-in) arriving between blocks
// of mixed sizes, as the sequencer's rows arrive while the callback runs.
static int run_voice() {
  using namespace thunder;
  const int PN = 3, NS = 30000; std::vector<float> pd[PN], pd2((size_t)NS); VoiceBuf bufs[PN]; VoiceKit kit[PN];
  for (int p = 0; p < PN; p++) { pd[p].resize((size_t)NS); for (int i = 0; i < NS; i++) pd[p][(size_t)i] = (float)std::sin(0.02 * (p + 1) * i) * (1.0f - (float)i / NS); }
  for (int i = 0; i < NS; i++) pd2[(size_t)i] = (float)std::sin(0.013 * i) * 0.5f;
  for (int p = 0; p < PN; p++) { bufs[p].data = pd[p].data(); bufs[p].data2 = p == 2 ? pd2.data() : nullptr; bufs[p].len = NS; bufs[p].sr = 44100; kit[p].choke = p == 1 ? 1 : 0; kit[p].syn = p != 1; kit[p].rel = 0.05; }
  VoicePool pool(48000, 64);
  const int MAXB = 512; std::vector<float> L((size_t)8 * MAXB), R((size_t)8 * MAXB); float* ls[kVoiceTracks] = {}; float* rs[kVoiceTracks] = {};
  for (int t = 0; t < 8; t++) { ls[t] = &L[(size_t)t * MAXB]; rs[t] = &R[(size_t)t * MAXB]; }
  static const double arp[8] = {0.001, 1, 0.01, 1.5, 0.02, 2, 0.03, 1.25};
  uint32_t rng = 20251115; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  long frame = 0; double peak = 0; long notes = 0; bool finite = true;
  { rt_guard g;
    for (int blk = 0; blk < 3000; blk++) {
      const int N = 1 + (int)(rnd() * MAXB); const double t0 = (double)frame / 48000;
      pool.setTime(t0);
      const int ne = (int)(rnd() * 3.5);
      for (int q = 0; q < ne; q++) {
        SeqEvent e; const double z = rnd(); e.track = (int)(rnd() * 8); e.t = t0 + rnd() * 0.01; e.pad = (double)(int)(rnd() * PN); e.rate = 0.5 + rnd() * 1.5; e.gain = 0.2 + rnd() * 0.8; e.pan = rnd() * 255; e.col = (int)(rnd() * 4);
        if (z < 0.55) { e.type = SEV_NOTE; e.poly = rnd() < 0.6; e.rev = rnd() < 0.15; e.ofs = rnd() < 0.2 ? rnd() * 255 : 0; e.fdi = rnd() < 0.15 ? 0.02 : 0; e.trm = rnd() < 0.1 ? 0x4f : 0; e.apn = rnd() < 0.1 ? 0x38 : 0;
          const double y = rnd(); const double* ap = nullptr;
          if (y < 0.15) { e.has |= SH_SLIDE; e.slideFrom = 0.5; e.slideDur = 0.05; } else if (y < 0.3) { e.has |= SH_ARP; e.arpN = 4; ap = arp; } else if (y < 0.4) { e.has |= SH_VIB; e.vibSpd = 4; e.vibDep = 9; e.vibT1 = e.t + 0.2; } else if (y < 0.5) { e.has |= SH_BEND; e.bendMul = 2; e.bendT1 = e.t + 0.1; }
          if (rnd() < 0.2 && !e.rev) { e.has |= SH_TW | SH_TWCUT | SH_TWDEC; e.twCut = 100; e.twRes = 100; e.twDec = 120; }
          if (rnd() < 0.1) { e.has |= SH_CUT; e.cut = e.t + 0.05; }
          const int pi = (int)e.pad; if (VoicePool::shapedReverse(e, bufs[pi])) pool.take(e, &kit[pi]); else if (pool.note(e, ap, bufs[pi], kit[pi], 120)) notes++;
        } else if (z < 0.7) { e.type = SEV_OFF; if (rnd() < 0.5) { e.has |= SH_COLS; e.cols = (uint8_t)(1u << e.col); } pool.off(e); }
        else if (z < 0.9) { e.type = SEV_MOD; e.has |= SH_GAIN | SH_PAN; if (rnd() < 0.3) { e.has |= SH_MUL; e.mul = 1.2; } pool.mod(e, nullptr); }
        else if (z < 0.97) { e.type = SEV_STP; e.flag = rnd() < 0.3; pool.stp(e); }
        else pool.stopAll(t0 + 0.001);
      }
      std::memset(L.data(), 0, L.size() * 4); std::memset(R.data(), 0, R.size() * 4);
      pool.render(ls, rs, N, t0);
      for (int t = 0; t < 8; t++) for (int i = 0; i < N; i++) { const double a = ls[t][i], b = rs[t][i]; if (!(a == a) || !(b == b)) finite = false; peak = std::max(peak, std::max(std::fabs(a), std::fabs(b))); }
      frame += N;
    }
  }
  if (!finite) return fail("the voice pool produced NaN");
  if (pool.dropped() || pool.tOverflow() || pool.autoDropped()) return fail("the voice pool dropped notes or automation events");
  if (notes < 100 || peak <= 0) return fail("the voice run played (almost) nothing");
  std::printf("rt_harness: voice pool: %ld notes over %ld frames, peak %d voices, no allocation\n", notes, frame, pool.peakVoices());
  return 0;
}

// ---- U32: the SoundFont live pool under the guard ---------------------------------------------------------------------------------
// The font is parsed and the preset's samples decoded OUTSIDE the guard (the control thread, "prepared before play"); every note / off / mod / stp / stopAll and every render() runs inside it, at mixed host block sizes.
static int run_sf() {
  using namespace thunder; using namespace thunder::sf;
  std::ifstream fin(std::string(THUNDER_ROOT) + "/native/tests/golden/sf_test.sf2", std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
  Font font; if (bytes.empty() || parse(bytes.data(), bytes.size(), font) != kParseOk || font.presets.empty()) return fail("sf: the test font did not load");
  std::vector<PresetPcm> pcms(font.presets.size()); for (size_t i = 0; i < font.presets.size(); i++) prepare_preset(font, &font.presets[i], DecodeOpts(), pcms[i]);
  const int PN = (int)std::min<size_t>(3, font.presets.size()); SfPad pads[3]; VoiceKit kit[3];
  for (int p = 0; p < PN; p++) { pads[p].font = &font; pads[p].pr = &font.presets[(size_t)p]; pads[p].pcm = &pcms[(size_t)p]; pads[p].P.atk = 1; pads[p].P.rel = 1; pads[p].P.velo = 0.7; kit[p].syn = true; kit[p].choke = p == 1 ? 1 : 0; }
  SfPool pool(48000, 32);
  const int MAXB = 512; std::vector<float> L((size_t)8 * MAXB), R((size_t)8 * MAXB); float* ls[kVoiceTracks] = {}; float* rs[kVoiceTracks] = {};
  for (int t = 0; t < 8; t++) { ls[t] = &L[(size_t)t * MAXB]; rs[t] = &R[(size_t)t * MAXB]; }
  static const double arp[8] = {0.001, 1, 0.01, 1.5, 0.02, 2, 0.03, 1.25};
  uint32_t rng = 20261117; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  long frame = 0; double peak = 0; long notes = 0; bool finite = true;
  { rt_guard g;
    for (int blk = 0; blk < 3000; blk++) {
      const int N = 1 + (int)(rnd() * MAXB); const double t0 = (double)frame / 48000;
      pool.setFrame((uint64_t)frame);
      const int ne = (int)(rnd() * 3.5);
      for (int q = 0; q < ne; q++) {
        SeqEvent e; const double z = rnd(); e.track = (int)(rnd() * 8); e.t = t0 + rnd() * 0.01 + 0.003; e.rate = 0.5 + rnd() * 1.5; e.gain = 0.2 + rnd() * 0.8; e.pan = rnd() * 255; e.col = (int)(rnd() * 4);
        if (z < 0.55) { e.type = SEV_NOTE; e.poly = rnd() < 0.6; const double y = rnd(); const double* ap = nullptr;
          if (y < 0.15) { e.has |= SH_SLIDE; e.slideFrom = 0.5; e.slideDur = 0.05; } else if (y < 0.3) { e.has |= SH_ARP; e.arpN = 4; ap = arp; } else if (y < 0.4) { e.has |= SH_VIB; e.vibSpd = 4; e.vibDep = 9; e.vibT1 = e.t + 0.2; } else if (y < 0.5) { e.has |= SH_BEND; e.bendMul = 2; e.bendT1 = e.t + 0.1; }
          if (rnd() < 0.1) { e.has |= SH_CUT; e.cut = e.t + 0.05; }
          const int pi = (int)(rnd() * PN); if (rnd() < 0.1) pool.take(e, &kit[pi]); else if (pool.note(e, ap, pads[pi], kit[pi])) notes++;
        } else if (z < 0.7) { e.type = SEV_OFF; if (rnd() < 0.5) { e.has |= SH_COLS; e.cols = (uint8_t)(1u << e.col); } pool.off(e); }
        else if (z < 0.9) { e.type = SEV_MOD; e.has |= SH_GAIN | SH_PAN; if (rnd() < 0.3) { e.has |= SH_MUL; e.mul = 1.2; } pool.mod(e, nullptr); }
        else if (z < 0.97) { e.type = SEV_STP; e.flag = rnd() < 0.3; pool.stp(e); }
        else pool.stopAll(t0 + 0.001);
      }
      std::memset(L.data(), 0, L.size() * 4); std::memset(R.data(), 0, R.size() * 4);
      pool.render(ls, rs, N, (uint64_t)frame);
      for (int t = 0; t < 8; t++) for (int i = 0; i < N; i++) { const double a = ls[t][i], b = rs[t][i]; if (!(a == a) || !(b == b)) finite = false; peak = std::max(peak, std::max(std::fabs(a), std::fabs(b))); }
      frame += N;
    }
  }
  if (!finite) return fail("the SoundFont pool produced NaN");
  if (pool.tOverflow() || pool.autoDropped()) return fail("the SoundFont pool dropped automation events");
  if (notes < 100 || peak <= 0) return fail("the SoundFont run played (almost) nothing");
  std::printf("rt_harness: SoundFont pool: %ld notes over %ld frames (%u refused for lack of a voice), no allocation\n", notes, frame, pool.dropped());
  return 0;
}

// ---- U09: the mixer under the guard ------------------------------------------------------------------------------------------------
// The mixer, the bus plan and a stand-in chain hook are set up outside the guard; inside it: blocks of mixed sizes with noise on eight
// tracks, the sequencer's mixer events (TVL / TVA / TPN / MVOL / MOUT) and mix.set of every kind (the page's law, at once, a ramp)
// arriving between blocks, the song's lanes reset now and then (STOP), the plan and the hook swapped in and out.
static void mix_sum_hook(void*, int, float* L, float* R, int N, double, const thunder::MixFeed* fd, int nf) {
  for (int i = 0; i < N; i++) for (int j = 0; j < nf; j++) { L[i] += fd[j].L[i]; R[i] += fd[j].R[i]; }
}
static float g_clkSink = 0;   // (keeps the clock reads from being optimised away)
static void mix_clock_hook(void* u, int track, float* L, float*, int N, double, const thunder::MixFeed*, int) { const float* b = ((thunder::Mixer*)u)->beat(track); for (int i = 0; i < N; i += 16) g_clkSink += b[i]; (void)L; }
static int run_mix() {
  using namespace thunder;
  const int MAXB = 512; Mixer M(48000, MAXB);
  BusPlan P; P.feeds[3][0] = 1; P.feeds[3][1] = 2; P.nFeeds[3] = P.feedLen[3] = 2; P.feedMask[3] = 6; P.targets[1] = P.targets[2] = 8; P.pure = 6; P.pre = 2;
  std::vector<float> oL((size_t)MAXB), oR((size_t)MAXB);
  uint32_t rng = 20251116; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  long frame = 0; double peak = 0; bool finite = true; long cmds = 0;
  { rt_guard g;
    M.setPlan(P); M.setChain(3, mix_sum_hook, nullptr);
    M.setFx(2, mix_clock_hook, &M); M.setFx(5, mix_clock_hook, &M); M.setMasterFx(mix_clock_hook, &M);   // effects nodes: each reads its beat clock (through its delay)
    double beat = 0;
    for (int blk = 0; blk < 4000; blk++) {
      const int N = 1 + (int)(rnd() * MAXB); const double t0 = (double)frame / 48000; M.setTime(t0);
      const int ne = (int)(rnd() * 3.2);
      for (int q = 0; q < ne; q++) { const double z = rnd(); cmds++;
        if (z < 0.5) { SeqEvent e; const int k = (int)(rnd() * 5); e.type = k == 0 ? SEV_TVL : k == 1 ? SEV_TVA : k == 2 ? SEV_TPN : k == 3 ? SEV_MVOL : SEV_MOUT; e.track = (int)(rnd() * 8); e.v = (double)(int)(rnd() * 256); e.t = t0 + rnd() * 0.25; M.event(e); }
        else if (z < 0.97) { MixSet c; const int k = (int)(rnd() * 7); c.k = (MixKey)k; c.tr = c.k == MixKey::Out || rnd() < 0.1 ? kSongNT : (int)(rnd() * 8); c.v = c.k == MixKey::Mute || c.k == MixKey::Solo ? (rnd() < 0.3 ? 1 : 0) : (double)(int)(rnd() * 256);
          const double y = rnd(); c.ramp = y < 0.6 ? -1 : y < 0.8 ? 0 : 1 + rnd() * 2000; if (rnd() < 0.3) c.at = t0 + rnd() * 0.01; M.set(c); }
        else if (z < 0.985) M.songLanesReset();
        else { const bool on = rnd() < 0.5; M.setChain(3, on ? mix_sum_hook : nullptr, nullptr); M.setPlan(P); } }
      // U10: a new latency plan now and then (every delay point, glided or at once, idle or in force), the beat clock's events and stops
      if (rnd() < 0.04) { PdcPlan pl; pl.on = rnd() < 0.9; for (int t = 0; t < kMixTracks; t++) { pl.d[t] = rnd() < 0.4 ? 0 : rnd() * 0.12; pl.f[t] = rnd() < 0.5 ? 0 : rnd() * 0.12; pl.c[t] = rnd() < 0.5 ? 0 : rnd() * 0.12; }
        PdcApply a; a.live = rnd() < 0.5; a.playing = rnd() < 0.7; a.now = rnd() < 0.2; a.mode = rnd() < 0.15 ? PdcMode::Off : rnd() < 0.5 ? PdcMode::On : PdcMode::Auto; pdc_apply(M, pl, a); cmds++; }
      if (rnd() < 0.25) { SeqEvent e; e.type = SEV_BEAT; e.t = t0 + rnd() * 0.02; beat += 0.0625 + rnd() * 0.2; if (rnd() < 0.03) beat = rnd() * 40; e.v = beat; M.event(e); cmds++; }
      if (rnd() < 0.01) { M.clockStop(t0 + rnd() * 0.01); cmds++; }
      for (int t = 0; t < 8; t++) { float *l = M.inputsL()[t], *r = M.inputsR()[t]; for (int i = 0; i < N; i++) { l[i] = (float)(rnd() - 0.5); r[i] = (float)(rnd() - 0.5); } }
      M.render(oL.data(), oR.data(), N, t0);
      for (int i = 0; i < N; i++) { const double a = oL[(size_t)i], b = oR[(size_t)i]; if (!(a == a) || !(b == b)) finite = false; peak = std::max(peak, std::max(std::fabs(a), std::fabs(b))); }
      frame += N;
    }
  }
  if (!finite) return fail("the mixer produced NaN");
  if (M.autoDropped()) return fail("the mixer dropped automation events (a lane or the clock's)");
  if (peak <= 0) return fail("the mixer run was silent");
  std::printf("rt_harness: mixer: %ld commands over %ld frames, longest lane %d of %d / %d, no allocation\n", cmds, frame, M.peakEvents(), Mixer::kAutoUi, Mixer::kAutoSong);
  return 0;
}

// ---- U09: the whole path, sequencer -> voices -> mixer, under the guard ---------------------------------------------------------------
// ELECTRO as a render (once), with a made-up pad for every slot, through SongRenderer::block in blocks of mixed sizes: every block
// inside the guard. Then on, until the voice pool is empty: with the mixer every track has an output, so every voice ends (U08's trap).
static int run_song() {
  using namespace thunder;
  std::string text; { std::ifstream f(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder", std::ios::binary); if (!f) return fail("cannot read sounds/ELECTRO.thunder"); std::ostringstream o; o << f.rdbuf(); text = o.str(); }
  Song song; JsonError je; if (!song.load(text.data(), text.size(), &je)) return fail("ELECTRO does not load");
  SeqVars vars; static RenderPads pads; const int PN = 64, NS = 24000; std::vector<float> pd((size_t)PN * NS);
  for (int p = 0; p < PN; p++) { float* d = &pd[(size_t)p * NS]; for (int i = 0; i < NS; i++) d[i] = (float)std::sin(0.01 * (p % 7 + 1) * i) * (1.0f - (float)i / NS) * 0.3f;
    pads.buf[p].data = d; pads.buf[p].len = NS; pads.buf[p].sr = 48000; pads.kit[p].syn = p >= 8; pads.kit[p].rel = p >= 8 ? 0.1 : 0; pads.kit[p].choke = p == 3 ? 1 : 0; }
  SeqOpts o; o.once = true; o.row = song.renderFrom;
  const int MAXB = 512; SongRenderer R(song, vars, pads, 48000, MAXB, o);
  std::vector<float> L((size_t)MAXB), Rr((size_t)MAXB);
  uint32_t rng = 7; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  double peak = 0; bool finite = true; long tail = 0;
  PdcSlot lat{31, 0, true, true, 1000}; const PdcPlan plan = pdc_plan(BusPlan(), &lat, 1, 48000, PdcMode::Auto);   // a made-up latent slot: every other track waits 1000 samples
  { rt_guard g;
    R.setPlan(plan, PdcMode::Auto, 0);
    while (!R.songOver() && R.frame() < 48000L * 600) { const int N = 1 + (int)(rnd() * MAXB); R.block(L.data(), Rr.data(), N);
      for (int i = 0; i < N; i++) { const double a = L[(size_t)i], b = Rr[(size_t)i]; if (!(a == a) || !(b == b)) finite = false; peak = std::max(peak, std::max(std::fabs(a), std::fabs(b))); } }
    while (R.voices() > 0 && tail < 48000L * 30) { R.block(L.data(), Rr.data(), MAXB); tail += MAXB; }
  }
  const RenderStats s = R.stats();
  if (!R.songOver()) return fail("the song did not end in ten minutes");
  if (R.voices() != 0) return fail("the voice pool did not empty after the song ended");
  if (!finite) return fail("the song render produced NaN");
  if (s.voiceDropped || s.voiceOverflow || s.voiceAutoDropped || s.mixAutoDropped || s.seqDropped) return fail("the song render lost notes or events");
  if (s.notesPlayed < 1000 || peak <= 0) return fail("the song render played (almost) nothing");
  if (s.beats < 100 || !R.mixer().clock().on() || R.mixer().clock().dropped()) return fail("the beat clock did not follow the song");
  std::printf("rt_harness: whole song: %d steps, %d notes played, %d mixer events, %lld frames + %.2f s until the pool emptied, peak %d voices, no allocation\n", s.steps, s.notesPlayed, s.mixEvents, (long long)(s.frames - tail), tail / 48000.0, s.peakVoices);
  return 0;
}

// ---- LIVE ROUTE 1b: a pool of the four live pads driven directly under the guard: notes, ENGINE TARGETS events (up to 4 points a lane, more than a lane holds: pruning and overflow), stopAll, hostile eng values, idle
// stretches (the idle skip: a node leaves the pass and wakes at its next event) and the notes after them. The pads are declared outside the guard. Nothing may allocate, nothing may be non-finite.
static int run_live_eng() {
  using namespace thunder;
  LivePool lp(48000); const LiveKind kinds[4] = {LiveKind::Bass, LiveKind::Vox, LiveKind::Acid, LiveKind::Mach};
  for (int k = 0; k < 4; k++) { LivePadConfig c; c.kind = kinds[k]; c.trackMask = 0xFFu; c.vox.txt = "la di da ba"; if (!lp.addPad(k, c)) return fail("addPad failed (live eng)"); }
  const int NT = 8; static std::vector<float> bufL((size_t)kVoiceTracks * 128), bufR((size_t)kVoiceTracks * 128); float* inL[kVoiceTracks]; float* inR[kVoiceTracks];
  for (int t = 0; t < kVoiceTracks; t++) { inL[t] = bufL.data() + (size_t)t * 128; inR[t] = bufR.data() + (size_t)t * 128; }
  uint32_t rng = 5; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
  bool finite = true; uint32_t maxIdle = 0, notes = 0; const int64_t total = 48000LL * 14;
  { rt_guard g;
    for (int64_t q0 = 0; q0 < total; q0 += 128) {
      const double now = (double)q0 / 48000; lp.setTime(now);
      if (q0 % (128 * 20) == 0) {   // every ~53 ms: a round of events 0.1 s ahead, with long quiet stretches (seconds 4..8 and 10..12 play nothing)
        const double sec = now; const bool quiet = (sec >= 4 && sec < 8) || (sec >= 10 && sec < 12);
        for (int t = 0; t < NT; t++) {
          if (!quiet && rnd() < 0.5) { SeqEvent e; e.type = SEV_NOTE; e.t = now + 0.1 + rnd() * 0.01; e.track = t; e.pad = (double)(int)(rnd() * 4); e.rate = 0.5 + rnd(); e.gain = 1; e.pan = 128; e.poly = (int)e.pad == 1 && rnd() < 0.5; e.col = (int)(rnd() * 3);
            if (lp.note(e, (int)e.pad, 120, nullptr)) notes++;
            SeqEvent o; o.type = SEV_OFF; o.t = e.t + 0.02 + rnd() * 0.1; o.track = t; lp.off(o); }
          if ((!quiet && rnd() < 0.3) || (quiet && rnd() < 0.02)) { SeqEvent e; e.type = SEV_ENG; e.t = now + 0.05 + rnd() * 0.2; e.track = t;
            for (int k = 0; k < 5; k++) e.engC[k] = rnd() < 0.5 ? -1 : rnd() * 100; for (int k = 0; k < 7; k++) e.engO[k] = rnd() < 0.5 ? 0 : (rnd() - 0.5) * 40;
            if (rnd() < 0.05) e.engC[(int)(rnd() * 5)] = rnd() < 0.5 ? nan : inf; if (rnd() < 0.03) e.t = nan; lp.eng(e); }
        }
        if (rnd() < 0.05) lp.tempo(now + 0.1, 60 + rnd() * 120);
        if (rnd() < 0.02 && !quiet) lp.stopAll(now + 0.1);
        if (quiet && rnd() < 0.01) lp.stopAll(now + 0.1);
      }
      for (int t = 0; t < NT; t++) { std::fill(inL[t], inL[t] + 128, 0.f); std::fill(inR[t], inR[t] + 128, 0.f); }
      lp.render(inL, inR, 128, q0); maxIdle = std::max(maxIdle, lp.idleNodes());
      for (int t = 0; t < NT; t++) for (int i = 0; i < 128; i++) if (!std::isfinite(inL[t][i]) || !std::isfinite(inR[t][i])) finite = false;
    }
  }
  const LiveStats s = lp.stats();
  if (!finite) return fail("the live eng pool produced a non-finite sample");
  if (notes < 50) return fail("the live eng pool played (almost) nothing");
  if (maxIdle == 0) return fail("the idle skip never engaged in the live eng pool");
  if (s.failed) return fail("the live eng pool lost a node");
  std::printf("rt_harness: live eng pool: %u notes, ENGINE TARGETS events, stopAll, %u nodes idle at most, %u refused values, no allocation\n", notes, maxIdle, s.refused);
  return 0;
}

// ---- LIVE ROUTE: the same song with four of its pads played by the live engines (bass, vox, acid, mach on every track), through SongRenderer::block, every block inside the guard ------------------------------------------------
// The pads are declared outside the guard (the nodes allocate). The song's end releases the held live voices (C4), so the pool empties.
static int run_live() {
  using namespace thunder;
  std::string text; { std::ifstream f(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder", std::ios::binary); if (!f) return fail("cannot read sounds/ELECTRO.thunder"); std::ostringstream o; o << f.rdbuf(); text = o.str(); }
  Song song; JsonError je; if (!song.load(text.data(), text.size(), &je)) return fail("ELECTRO does not load");
  SeqVars vars; static RenderPads pads; const int PN = 64, NS = 24000; static std::vector<float> pd((size_t)PN * NS);
  for (int p = 0; p < PN; p++) { float* d = &pd[(size_t)p * NS]; for (int i = 0; i < NS; i++) d[i] = (float)std::sin(0.01 * (p % 7 + 1) * i) * (1.0f - (float)i / NS) * 0.3f;
    pads.buf[p].data = d; pads.buf[p].len = NS; pads.buf[p].sr = 48000; pads.kit[p].syn = p >= 8; pads.kit[p].rel = p >= 8 ? 0.1 : 0; pads.kit[p].choke = p == 3 ? 1 : 0; }
  std::vector<int> used; for (int i = 0; i < kNI; i++) { const int pd2 = (int)song.insts[i].pad; if (pd2 >= 0 && pd2 < PN && std::find(used.begin(), used.end(), pd2) == used.end()) used.push_back(pd2); }
  if (used.size() < 4) return fail("ELECTRO uses fewer than four pads");
  SeqOpts o; o.once = true; o.row = song.renderFrom;
  const int MAXB = 512; SongRenderer R(song, vars, pads, 48000, MAXB, o);
  const LiveKind kinds[4] = {LiveKind::Bass, LiveKind::Vox, LiveKind::Acid, LiveKind::Mach};
  for (int k = 0; k < 4; k++) { LivePadConfig c; c.kind = kinds[k]; c.trackMask = 0; BassP bp; VoxP vp; vp.txt = "la di da ba"; AcidP ap; MachP mp; c.bass = bp; c.vox = vp; c.acid = ap; c.mach = mp;
    pads.route[used[(size_t)k]] = PadRoute::Live; if (!R.players().live().addPad(used[(size_t)k], c)) return fail("addPad failed"); }
  std::vector<float> L((size_t)MAXB), Rr((size_t)MAXB);
  uint32_t rng = 11; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  double peak = 0; bool finite = true; long tail = 0;
  { rt_guard g;
    while (!R.songOver() && R.frame() < 48000L * 600) { const int N = 1 + (int)(rnd() * MAXB); R.block(L.data(), Rr.data(), N);
      for (int i = 0; i < N; i++) { const double a = L[(size_t)i], b = Rr[(size_t)i]; if (!(a == a) || !(b == b)) finite = false; peak = std::max(peak, std::max(std::fabs(a), std::fabs(b))); } }
    while (R.voices() > 0 && tail < 48000L * 30) { R.block(L.data(), Rr.data(), MAXB); tail += MAXB; }
  }
  const RenderStats s = R.stats();
  if (!R.songOver()) return fail("the live song did not end");
  if (R.voices() != 0) return fail("the voice pool did not empty after the live song ended (held live voices)");
  if (!finite) return fail("the live song produced NaN");
  if (s.notesLivePlayed < 20 || peak <= 0) return fail("the live song played (almost) nothing");
  if (s.liveFailed || s.liveQueueDropped) return fail("the live pool lost events");
  std::printf("rt_harness: live song: %d live notes played (%u late, %u no node), %.2f s until the pool emptied, peak %d voices, no allocation\n", s.notesLivePlayed, s.liveLate, s.liveNoNode, tail / 48000.0, s.peakVoicesAll);
  return 0;
}

// ---- U37 P1: the pads snapshot under the guard ---------------------------------------------------------------------------------------------------------
// The control side (outside the guard) replaces pads, publishes and collects between quanta. Inside the guard, every quantum: PadsHost::beginQuantum (the scan of the voices' epochs, the adopt of
// a published snapshot when there is one, Players::setPads, Seq::setChop), then notes on the adopted pads and the voices' render. Nothing may allocate or free; every free is the control side's.
static int run_pads_snap() {
  using namespace thunder;
  std::string text; { std::ifstream f(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder", std::ios::binary); if (!f) return fail("cannot read sounds/ELECTRO.thunder"); std::ostringstream o; o << f.rdbuf(); text = o.str(); }
  Song song; JsonError je; if (!song.load(text.data(), text.size(), &je)) return fail("ELECTRO does not load");
  SeqVars vars; SeqOpts so; so.once = true; Seq sq(song, vars, SeqMode::Song, so);
  Players pl(nullptr, 48000, 64);
  PadsHost* host = PadsHost::create(pl); if (!host) return fail("PadsHost::create");
  const int PN = 8, NS = 12000; std::vector<float> d((size_t)NS);
  auto setp = [&](int p, int v) { for (int i = 0; i < NS; i++) d[(size_t)i] = (float)std::sin(0.02 * (p + 1) * i) * 0.3f * (1.0f - (float)i / NS) + 0.001f * (float)(v % 7); return host->setPad(p, d.data(), nullptr, NS, 48000); };
  for (int p = 0; p < PN; p++) if (setp(p, 0) != 0) return fail("setPad");
  host->chop().setLoop(0, 1.0); if (host->publish() != PadsHost::kOk) return fail("publish");
  static float store[kVoiceTracks][128]; float* ptr[kVoiceTracks]; for (int i = 0; i < kVoiceTracks; i++) ptr[i] = store[i];
  uint32_t rng = 99; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  int adopted = 0, notes = 0, maxVoices = 0; bool finite = true; int64_t frame = 0;
  for (int q = 0; q < 1500; q++) {
    if (q % 9 == 0) { if (setp((int)(rnd() * PN), q) != 0) return fail("setPad (loop)"); if (host->publish() == PadsHost::kFull) { host->collect(); } }
    host->collect();   // control thread: outside the guard
    const double t0 = (double)frame / 48000;
    { rt_guard g;
      if (host->beginQuantum(pl, &sq)) adopted++;
      pl.setTime(t0, frame);
      if (q % 2 == 0) { SeqEvent e; e.type = SEV_NOTE; e.track = (int)(rnd() * 8); e.pad = (int)(rnd() * PN); e.ii = 0; e.t = t0 + rnd() * 0.01; e.poly = rnd() < 0.5; e.rate = 1; e.gain = 1; e.pan = 128; e.vel = 1; pl.dispatch(e, sq, song); notes++; }
      for (int i = 0; i < kVoiceTracks; i++) std::memset(store[i], 0, sizeof store[i]);
      pl.render(ptr, ptr, 128, t0, frame);
      for (int i = 0; i < 128; i++) if (!(store[0][i] == store[0][i])) finite = false;
      if (pl.voices() > maxVoices) maxVoices = pl.voices(); }
    frame += 128;
  }
  { rt_guard g; pl.stopAll((double)frame / 48000); }
  for (int q = 0; q < 400 && pl.voices() > 0; q++) { const double t0 = (double)frame / 48000; { rt_guard g; host->beginQuantum(pl, &sq); pl.setTime(t0, frame); pl.render(ptr, ptr, 128, t0, frame); } frame += 128; }
  { rt_guard g; host->beginQuantum(pl, &sq); }
  host->collect();
  const bool leak = host->retired() != 0 || host->buffers() != (size_t)PN;
  const size_t made = host->made(); pl.setPads(nullptr); delete host;
  if (adopted < 100 || notes < 500 || maxVoices < 2 || !finite) return fail("the pads snapshot run played too little or produced NaN");
  if (leak) return fail("pads snapshot: a retired buffer was not freed after its voices ended");
  std::printf("rt_harness: pads snapshot: %d adopts, %d notes, %d voices at most, %zu buffers made, every one freed by the control side, no allocation on the audio side\n", adopted, notes, maxVoices, made);
  return 0;
}

// ---- U37 P2: LiveHost::process under the guard --------------------------------------------------------------------------------------------
// ELECTRO through the whole host (commands ring, waiting list, adoption of song and pads snapshots, the nine-step quantum, the event rings, the step dump): blocks of mixed sizes (1 .. 1024: the quantum split and
// a re-anchor), commands arriving between blocks (bpm, loop, perf, mix_set, hold, play, stop, timed ones), a song and a pads publish every so often (control side, outside the guard), the consumer polling.
// Only `process` runs inside the guard: it must not allocate (the control side's calls, which do, run outside).
static int run_live_host() {
  using namespace thunder;
  std::string text; { std::ifstream f(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder", std::ios::binary); if (!f) return fail("cannot read sounds/ELECTRO.thunder"); std::ostringstream o; o << f.rdbuf(); text = o.str(); }
  for (int pass = 0; pass < 2; pass++) {
    thunder_live_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.sample_rate = 48000; o.max_block = 1024; o.flags = pass ? THUNDER_LIVE_F_DUMP_STEPS : 0;
    char err[256] = {0}; LiveHost* H = LiveHost::create(o, err, sizeof err); if (!H) return fail("LiveHost::create");
    char e2[128]; if (H->songPublish((const uint8_t*)text.data(), text.size(), nullptr, 0, 1, e2, sizeof e2)) return fail("LiveHost song publish");
    const int PN = 64, NS = 24000; std::vector<float> d((size_t)NS);
    auto setp = [&](int p, int v) { for (int i = 0; i < NS; i++) d[(size_t)i] = (float)std::sin(0.01 * (p % 7 + 1) * i + 0.1 * (v % 5)) * (1.0f - (float)i / NS) * 0.3f; return H->padSet(p, d.data(), nullptr, NS, 48000); };
    for (int p = 0; p < PN; p++) if (setp(p, 0) || H->padKit(p, p == 3 ? 1 : 0, p >= 8 ? 1 : 0, p >= 8 ? 0.1 : 0)) return fail("LiveHost pad");
    if (H->padsPublish()) return fail("LiveHost pads publish");
    thunder_live_play_opts po; std::memset(&po, 0, sizeof po); po.size = sizeof po; po.t0 = THUNDER_LIVE_NOW; po.spp = -1; if (H->play(po)) return fail("LiveHost play");
    static float L[1024], R[1024]; uint32_t rng = 7; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
    int64_t frame = 0; int calls = 0; bool finite = true; double peak = 0; uint8_t buf[512]; char js[16384]; long dumped = 0; uint64_t recs = 0; int endedSeen = 0, stoppedSeen = 0, heldSeen = 0;
    while (frame < 48000 * 25) {
      static const int sizes[] = {128, 128, 480, 1, 64, 1024, 333, 128, 512};
      const int n = sizes[(int)(rnd() * 9)];
      // control side, between blocks
      const bool quiet = calls >= 2600 && calls < 3700;   // no stop / play meanwhile: the armed hold reaches a pattern edge (HELD)
      if (calls == 2600) H->hold(THUNDER_LIVE_NOW);
      const int k = (quiet && (calls % 97 == 61 || calls % 97 == 67 || calls % 97 == 79 || calls % 97 == 83 || calls % 97 == 89)) ? -1 : calls % 97;
      if (k == 5) H->bpm(80 + (int)(rnd() * 80), THUNDER_LIVE_NOW);
      else if (k == 11) H->bpm(100, frame + 20000);
      else if (k == 17) H->loop(rnd() < 0.5, 0, 4, THUNDER_LIVE_NOW);
      else if (k == 23) H->perf((int)(rnd() * 3), 0, (double)(int)(rnd() * 2), THUNDER_LIVE_NOW);
      else if (k == 29) H->mixSet((int)(rnd() * 8), THUNDER_LIVE_MIX_MUTE, (double)(int)(rnd() * 2), -1, THUNDER_LIVE_NOW);
      else if (k == 31) H->mixSet(32, THUNDER_LIVE_MIX_VOL, 200, 480, THUNDER_LIVE_NOW);
      else if (k == 41) { H->songPublish((const uint8_t*)text.data(), text.size(), nullptr, 0, 10 + calls, e2, sizeof e2); }
      else if (k == 47) { setp((int)(rnd() * PN), calls); H->padsPublish(); }
      else if (k == 53) H->collect();
      else if (k == 59) H->lead(0.1 + 0.5 * rnd(), THUNDER_LIVE_NOW);
      else if (k == 61 && calls > 1000) { H->hold(THUNDER_LIVE_NOW); }
      else if (k == 67 && calls > 1000) { H->play(po); }
      else if (k == 71 && calls % 3 == 0) { H->stopTrack((int)(rnd() * 8), THUNDER_LIVE_NOW); }
      else if (k == 79 && calls > 600 && calls % 2 == 0) { H->stop(THUNDER_LIVE_NOW); }   // stop (voices let go, waiting list cleared), the play above restarts it
      else if (k == 83 && calls > 600) { thunder_live_play_opts pe = po; pe.has_only = 1; pe.only = 0; H->play(pe); }   // a song that ends at once: Players::songOver, ENDED, the 0.3 s timer, STOPPED
      else if (k == 89 && calls > 600 && calls % 4 == 1) { H->bpm(90, frame + 5000); H->stop(frame + 9000); }   // a timed command and a timed stop
      else if (k == 73) { for (int i = 0; i < 20; i++) H->mixSet(i, THUNDER_LIVE_MIX_VOL, 100.0 + i, 0, frame + i * 300); }
      int64_t f0 = frame; if (calls == 700) f0 += 777; if (calls == 1500) f0 = frame / 2; if (calls == 2200) f0 = frame + 123456;   // three re-anchors: forward, backward, forward
      { rt_guard g; if (H->process(L, R, n, f0, 0)) return fail("LiveHost::process"); }
      frame = f0 + n; calls++;
      for (int i = 0; i < n; i++) { if (!(L[i] == L[i]) || !(R[i] == R[i])) finite = false; peak = std::max(peak, (double)std::fabs(L[i])); }
      for (int r; (r = H->poll(buf, sizeof buf)) > 0;) { int o2 = 0; while (o2 < r) { thunder_live_ev_head hd; std::memcpy(&hd, buf + o2, sizeof hd); recs++; if (hd.type == THUNDER_LIVE_EV_STATE) { thunder_live_ev_state es; std::memcpy(&es, buf + o2, sizeof es); if (es.state == THUNDER_LIVE_ST_ENDED) endedSeen++; if (es.state == THUNDER_LIVE_ST_STOPPED) stoppedSeen++; } if (hd.type == THUNDER_LIVE_EV_HELD) heldSeen++; o2 += hd.bytes; } }
      if (pass) for (int r; (r = H->dumpStep(js, sizeof js)) > 0;) dumped++;
    }
    thunder_live_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; H->statsGet(&st);
    if (!finite || peak < 0.01) return fail("LiveHost run: silent or non-finite");
    if (st.reanchors != 3 || st.quanta < 5000 || st.steps < 100 || st.cmds_applied < 50) return fail("LiveHost run did too little (3 re-anchors, quanta, steps or commands)");
    if (endedSeen < 1 || stoppedSeen < 2 || heldSeen < 1) return fail("LiveHost run never ended a song or never stopped (ENDED / STOPPED / HELD paths not exercised)");
    if (pass && dumped < 50) return fail("LiveHost step dump delivered too little");
    std::printf("rt_harness: LiveHost%s: %d process calls, %llu quanta, %llu steps, %llu commands, %u late commands, %llu records (%d ENDED, %d STOPPED, %d HELD), %ld dumped steps, no allocation in process\n", pass ? " (with the step dump)" : "", calls, (unsigned long long)st.quanta, (unsigned long long)st.steps, (unsigned long long)st.cmds_applied, st.cmd_late, (unsigned long long)recs, endedSeen, stoppedSeen, heldSeen, dumped);
    delete H;
  }
  return 0;
}

// ---- U11: the master processor under the guard -----------------------------------------------------------------------------------------
// All three stages (and the loudness meters) on a loud noisy signal, blocks of mixed sizes (not multiples of 128: the quantum split), settings
// changed between blocks (stages on / off, every parameter, the limiter's look-ahead, junk values), samples outside +-64 and NaN now and then,
// silence long enough for the shortcut; the meter queue popped as a host would. Nothing may allocate.
static int run_master() {
  using namespace thunder;
  MasterProcessor P(48000, true);   // (the constructor allocates: outside the guard)
  const int MAXB = 700; std::vector<float> L((size_t)MAXB), R((size_t)MAXB);
  uint32_t rng = 4242; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  long frame = 0, msgs = 0; bool finite = true; double peak = 0; MasterMeter m;
  { rt_guard g;
    MasterSet s0; s0.hasSup = s0.hasComp = s0.hasLim = true; s0.sup.on = s0.comp.on = s0.lim.on = true; P.set(s0);
    for (int blk = 0; blk < 1500; blk++) {
      const int N = 1 + (int)(rnd() * MAXB);
      if (rnd() < 0.15) { MasterSet s; const double z = rnd();
        if (z < 0.34) { s.hasSup = true; s.sup.on = rnd() < 0.7; s.sup.depth = rnd(); s.sup.sharp = rnd(); s.sup.sel = rnd() * 6; s.sup.lo = 20 + rnd() * 400; s.sup.hi = 4000 + rnd() * 16000; s.sup.delta = rnd() < 0.2; s.sup.mix = rnd(); }
        else if (z < 0.67) { s.hasComp = true; s.comp.on = rnd() < 0.7; s.comp.thr = -40 + rnd() * 30; s.comp.ratio = 1 + rnd() * 10; s.comp.atk = rnd() * 30; s.comp.rel = 20 + rnd() * 300; s.comp.hpf = rnd() < 0.3 ? 0 : rnd() * 300; s.comp.knee = rnd() * 12; s.comp.mix = rnd(); s.comp.autoRel = rnd() < 0.5; s.comp.autoMakeup = rnd() < 0.5; if (rnd() < 0.1) s.comp.ratio = std::nan(""); }
        else { s.hasLim = true; s.lim.on = rnd() < 0.8; s.lim.drive = rnd() * 18; s.lim.ceil = -6 * rnd(); s.lim.rel = 10 + rnd() * 500; s.lim.look = 1 + rnd() * 9; s.lim.tp = rnd() < 0.7; if (rnd() < 0.1) s.lim.look = 1e9; }
        P.set(s); }
      const bool quiet = (blk / 150) % 3 == 2;
      for (int i = 0; i < N; i++) { L[(size_t)i] = quiet ? 0.0f : (float)((rnd() - 0.5) * 1.6); R[(size_t)i] = quiet ? 0.0f : (float)((rnd() - 0.5) * 1.6); }
      if (!quiet && rnd() < 0.05) { L[0] = std::nanf(""); R[(size_t)(N - 1)] = 1000.0f; }
      P.process(L.data(), R.data(), N);
      for (int i = 0; i < N; i++) { if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false; peak = std::max(peak, (double)std::fabs(L[(size_t)i])); }
      while (P.popMeter(&m)) msgs++;
      frame += N;
    }
  }
  if (!finite) return fail("the master processor produced NaN");
  if (peak <= 0) return fail("the master processor run was silent");
  if (msgs < 100) return fail("the master processor posted (almost) no meter messages");
  std::printf("rt_harness: master processor: %ld frames, %ld meter messages, %u silent quanta skipped, no allocation\n", frame, msgs, P.silentBlocks());
  return 0;
}

// ---- U12: the master chain (EQ biquads -> mastering worklet -> safety limiter) under the guard, reconfigured between blocks, driven as the mixer's master hook ----
static int run_chain() {
  using namespace thunder;
  const int MAXB = 600; Mixer M(48000, MAXB); MasterChain C(48000, MAXB); M.setMasterFx(&MasterChain::hook, &C);
  MeterBank bank(48000, 30); MeterEvent ev; long events = 0;   // U13: the meters fed from the mixer's pairs and the master output, in the same guarded loop
  std::vector<float> oL((size_t)MAXB), oR((size_t)MAXB);
  uint32_t rng = 99; auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; };
  long frame = 0; bool finite = true; double peak = 0; int cfgs = 0;
  { rt_guard g;
    for (int blk = 0; blk < 1200; blk++) {
      const int N = 1 + (int)(rnd() * MAXB); const double t0 = (double)frame / 48000; M.setTime(t0);
      if (blk % 40 == 0) { MasterChainConfig c = master_chain_fresh(rnd() < 0.7); c.mstOn = rnd() < 0.7; c.eqOn = rnd() < 0.8; c.nBands = 1 + (int)(rnd() * 7);
        for (int i = 0; i < c.nBands; i++) { EqBand& b = c.bands[i]; b.type = (EqType)(int)(rnd() * 5); b.f = 20 + rnd() * 19980; b.g = -18 + rnd() * 36; b.q = 0.1 + rnd() * 17.9; b.slope = rnd() < 0.3 ? 12 : rnd() < 0.5 ? 24 : 48; b.on = rnd() < 0.85; }
        c.set.sup.on = rnd() < 0.5; c.set.comp.on = rnd() < 0.6; c.set.lim.on = rnd() < 0.5; C.configure(c); cfgs++; }
      for (int t = 0; t < 4; t++) { float *l = M.inputsL()[t], *r = M.inputsR()[t]; for (int i = 0; i < N; i++) { l[i] = (float)((rnd() - 0.5) * 1.5); r[i] = (float)((rnd() - 0.5) * 1.5); } }
      M.render(oL.data(), oR.data(), N, t0);
      meters_feed(bank, M, oL.data(), oR.data(), N); bank.advance(N, t0 + (double)N / 48000); while (bank.pop(&ev)) events++;
      for (int i = 0; i < N; i++) { const double a = oL[(size_t)i], b = oR[(size_t)i]; if (!std::isfinite(a) || !std::isfinite(b)) finite = false; peak = std::max(peak, std::max(std::fabs(a), std::fabs(b))); }
      frame += N;
    }
  }
  if (events < 100) return fail("the meters published (almost) nothing");
  if (!finite) return fail("the master chain produced NaN");
  if (peak <= 0) return fail("the master chain run was silent");
  std::printf("rt_harness: master chain + meters: %ld frames, %d configurations, %ld meter events, no allocation\n", frame, cfgs, events);
  return 0;
}


// U14: the track-effect host under the guard: process (lanes, FXV, rest, sleep, telemetry, odd block sizes) and commit() of a plan made ahead.
static int run_fx() {
  using namespace thunder;
#ifndef THUNDER_HAVE_TFX
  std::printf("rt_fx: not run on this platform: no DSP library (the effects are not built here)\n");
  return 0;   // one part of the harness: the other parts' results stand; the message above says what was left out
#else
  FxHost H(48000.0); FxEntry e[8]; const int types[8] = {6, 7, 8, 9, 4, 3, 28, 26};
  for (int i = 0; i < 8; i++) { const int id = types[i]; e[i].t = id; e[i].slot = i; e[i].on = i != 3; std::snprintf(e[i].key, kFxKey, "%d:%s", i, fx_key_of(id)); e[i].nvals = fx_param_count(id);
    for (int k = 0; k < e[i].nvals; k++) e[i].vals[k] = fx_param_default(id, k); fx_map(id, e[i].vals, 48000.0, 120.0, 0.0, e[i].p); }
  FxPlan* p1 = H.plan(e, 8); H.commit(p1); delete p1;
  e[3].on = true; e[0].p[0] = 0.9; FxPlan* p2 = H.plan(e, 7); FxPlan* p3 = H.plan(e, 8);   // (made ahead on the control thread; committed in order below)
  H.setLevels(true); H.setTelemetry(true);
  std::vector<float> L(8192), R(8192); uint32_t sd = 7; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (float)(sd >> 8) / 16777216.f - 0.5f; };
  double t = 0; int nb = 0; float lv[8]; FxDyn dy[8]; double p12[12] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0, 0, 0, 0, 0, 0};
  { rt_guard g;
    for (int b = 0; b < 3000; b++) {
      const int n = (b % 7 == 0) ? 1 + (b * 37) % 200 : 128; for (int i = 0; i < n; i++) { L[(size_t)i] = b < 2000 ? rnd() * 0.5f : 0.f; R[(size_t)i] = b < 2000 ? rnd() * 0.5f : 0.f; }
      if (b == 100) H.lane(0, 1, t + 0.001); if (b == 200) H.lane(1, 0, t + 0.0001); if (b == 300) H.reset(t); if (b == 400) H.psetAt(t + 0.001, 1, p12);
      if (b == 500) H.setRest(true, t); if (b == 700) H.setRest(false, t); if (b == 900) H.commit(p2); if (b == 1000) H.commit(p3);
      if (b == 1500) H.lane(2, 1, t); H.process(L.data(), R.data(), n, t); t += n / 48000.0; nb++;
      H.popLevels(lv); H.popDyn(dy, 8); }
    H.process(L.data(), R.data(), 5000, t);   // too long: silence
  }
  delete p2; delete p3;
  if (H.laneDropped() || H.queueDropped()) { std::printf("rt_fx FAIL: dropped lanes %u queue %u\n", H.laneDropped(), H.queueDropped()); return 1; }
  std::printf("rt_fx ok: %d blocks, the track-effect host under the allocation guard\n", nb); return 0;
#endif
}

// U15: the tempo-locked kinds (Flex, Step gate, Retrigger) under the guard: the cores' rings are made in plan(), process() takes the beat clock and the FLX / GAT / RTG lanes,
// odd block sizes (chunked), a clock that stops and jumps, a chain swap that retires the Flex (deleted outside the guard with its plan).
static int run_fxclk() {
  using namespace thunder;
  FxHost H(48000.0); FxEntry e[3]; const int types[3] = {95, 94, 93};
  for (int i = 0; i < 3; i++) { const int id = types[i]; e[i].t = id; e[i].slot = i; e[i].on = true; std::snprintf(e[i].key, kFxKey, "%d:%s", i, fx_key_of(id)); e[i].nvals = fx_param_count(id);
    for (int k = 0; k < e[i].nvals; k++) e[i].vals[k] = fx_param_default(id, k);
    if (id != 93) e[i].vals[id == 95 ? 3 : 7] = 0;   // STARTS AT PLAY: the slot's own PATTERN from the start
    e[i].vals[0] = id == 95 ? 0x2a : id == 94 ? 4 : 1;   // Flex pattern 2A, Step gate pattern 4, Retrigger mode ALWAYS
    fx_map(id, e[i].vals, 48000.0, 120.0, 0.0, e[i].p); }
  FxPlan* p1 = H.plan(e, 3); H.commit(p1); delete p1;
  FxPlan* p2 = H.plan(e + 2, 1);   // Retrigger only: the Flex and the Step gate are retired
  std::vector<float> L(4096), R(4096), bp(4096); uint32_t sd = 7; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (float)(sd >> 8) / 16777216.f - 0.5f; };
  double t = 0, beat = 0; int nb = 0;
  { rt_guard g;
    for (int b = 0; b < 3000; b++) {
      const int n = (b % 5 == 0) ? 1 + (b * 37) % 1000 : 128; bool run = !(b >= 1000 && b < 1100);
      for (int i = 0; i < n; i++) { L[(size_t)i] = b < 2500 ? rnd() * 0.5f : 0.f; R[(size_t)i] = b < 2500 ? rnd() * 0.5f : 0.f; bp[(size_t)i] = run ? (float)std::fmod(beat, 16.0) : -1.f; if (run) beat += 2.0 / 48000 * 4; }
      if (b == 1500) beat = 3.2;   // a jump (a song loop)
      if (b == 100) H.lane(8, 0x12, t); if (b == 200) H.lane(9, 7, t); if (b == 300) H.lane(10, 3, t); if (b == 400) H.lane(10, -1, t); if (b == 600) H.lane(0, 0, t); if (b == 700) H.lane(0, -1, t);
      if (b == 2000) H.commit(p2);
      H.process(L.data(), R.data(), n, t, bp.data(), b % 11 == 0 ? 1 : n); t += n / 48000.0; nb++; }
    H.process(L.data(), R.data(), 100, t, nullptr, 0);   // the clock param absent
  }
  delete p2;
  if (H.laneDropped() || H.queueDropped()) { std::printf("rt_fxclk FAIL: dropped lanes %u queue %u\n", H.laneDropped(), H.queueDropped()); return 1; }
  if (H.slotCount() != 1 || H.slotType(0) != 93) { std::printf("rt_fxclk FAIL: the chain swap left %d slots\n", H.slotCount()); return 1; }
  std::printf("rt_fxclk ok: %d blocks, Flex / Step gate / Retrigger under the allocation guard\n", nb); return 0;
}


// U18: the pump effects under the guard. Four hosts carry all 25 pump types (up to 8 slots each) and run: ON / OFF by chain and by FXO lanes, parameter changes of every kind
// (a chain with other values: pump_commit; FXV: psetPump), the transport and notes of the creative effects, COMP's sidechain sources, telemetry, odd block sizes, the node's
// sleep and rest, and a chain swap that retires slots. Everything that makes a pump instance (plan, delete) is outside the guard (the control thread). NOT here, because the JS
// itself does it and it allocates (pump_free + pump_new in the audio thread): the wake of a slot that rested, and a plugin that put out garbage; audioRemakes() must be 0 for
// this run, and rt_pump_remake below shows that those two DO allocate (accepted for now, spare instances are deferred to U31: the lead, 16 Nov; fxpump.cpp REAL-TIME RULES).
static void pump_entry(thunder::FxEntry* e, int pt, int slot, bool on) {
  using namespace thunder;
  e->t = kPumpT0 + pt; e->slot = slot; e->on = on; e->bi = fx_pump_bypass(pt); e->cr = fx_pump_creative(pt); e->clk = fx_pump_clocked(pt); e->npv = 0;   // npv 0: pump's own defaults
  std::snprintf(e->key, kFxKey, "%d:p%s", slot, fx_pump_key(pt));
}
static int run_pump() {
  using namespace thunder;
#ifndef THUNDER_HAVE_PUMP
  std::printf("rt_pump: not run on this platform: no pump library\n");
  return 0;
#else
  const int kH = 4; FxHost* H[kH]; FxPlan* swap[kH]; FxEntry e[kH][8]; int n[kH] = {0, 0, 0, 0}; int pts[kH][8];
  for (int h = 0; h < kH; h++) H[h] = new FxHost(48000.0);
  for (int pt = 0; pt < kPumpTypes; pt++) { const int h = pt % kH; pump_entry(&e[h][n[h]], pt, n[h], true); pts[h][n[h]] = pt; n[h]++; }   // 7, 6, 6, 6 slots
  for (int h = 0; h < kH; h++) { FxPlan* p = H[h]->plan(e[h], n[h]); H[h]->commit(p); delete p; }
  // the same chains with other values (every parameter at a wild value: pump clamps each to its own range) and the first slot left out, made ahead of time
  static FxEntry w[kH][8]; uint32_t sd = 11; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (float)(sd >> 8) / 16777216.f - 0.5f; };
  for (int h = 0; h < kH; h++) { for (int i = 0; i < n[h]; i++) { w[h][i] = e[h][i]; w[h][i].on = true; w[h][i].npv = 24; for (int k = 0; k < w[h][i].npv; k++) w[h][i].pv[k] = rnd() * 200.f; if (fx_pump_bypass(pts[h][i]) >= 0 && fx_pump_bypass(pts[h][i]) < w[h][i].npv) w[h][i].pv[fx_pump_bypass(pts[h][i])] = 0; }
    swap[h] = H[h]->plan(w[h], n[h]); }
  FxPlan* shrink[kH]; for (int h = 0; h < kH; h++) shrink[h] = H[h]->plan(e[h] + 1, n[h] - 1);
  std::vector<float> L(4096), R(4096), bp(4096), tap(4096), snd(4096); uint32_t sd2 = 7; auto rn = [&]() { sd2 = sd2 * 1664525u + 1013904223u; return (float)(sd2 >> 8) / 16777216.f - 0.5f; };
  const float* taps[1] = {tap.data()}; const float* sends[1] = {snd.data()}; const int tm[1] = {3};
  double t = 0, beat = 0; int nb = 0; float lv[8]; FxDyn dy[8]; PumpTelOut pt8[8]; PumpLat pl[8];
  for (int h = 0; h < kH; h++) { H[h]->setLevels(true); H[h]->setTelemetry(true); H[h]->setTapMap(tm, 1); }
  { rt_guard g;
    for (int b = 0; b < 2400; b++) {
      const int nn = (b % 7 == 0) ? 1 + (b * 37) % 700 : 128;
      for (int h = 0; h < kH; h++) {
        for (int i = 0; i < nn; i++) { L[(size_t)i] = b < 2000 ? rn() * 0.5f : 0.f; R[(size_t)i] = b < 2000 ? rn() * 0.5f : 0.f; tap[(size_t)i] = rn(); snd[(size_t)i] = rn(); bp[(size_t)i] = b % 400 < 300 ? (float)std::fmod(beat + i * 2.0 / 48000, 16.0) : -1.f; }
        if (h == 0 && b == 100) { for (int s = 0; s < 8; s++) H[h]->lane(s, s % 2, t); }                      // FXO: half the slots forced off (an FXO slot never rests, so no wake) and half forced on
        if (h == 0 && b == 300) H[h]->lane(0, -1, t);
        if (h == 1 && b == 500) { float pv[24]; for (int k = 0; k < 24; k++) pv[k] = rn() * 100; H[h]->psetPump(t + 0.001, 0, pv, 24); }   // FXV
        if (h == 2 && b == 200) H[h]->pmidi(1, t + 0.01, 0x90, 60, 100);
        if (h == 2 && b == 210) H[h]->pmidi(1, t + 0.05, 0x80, 60, 0);
        if (h == 3 && b == 600) H[h]->setRest(true, t); if (h == 3 && b == 800) H[h]->setRest(false, t);
        if (b == 1000) H[h]->commit(swap[h]); if (b == 1400) H[h]->commit(shrink[h]);
        H[h]->setSources(taps, 1, sends, 1);
        H[h]->process(L.data(), R.data(), nn, t, bp.data(), b % 11 == 0 ? 1 : nn);
        H[h]->popLevels(lv); H[h]->popDyn(dy, 8); H[h]->popPumpTel(pt8, 8); H[h]->popLatencies(pl, 8);
      }
      t += nn / 48000.0; beat += nn * 2.0 / 48000; if (beat >= 16) beat -= 16; nb++; }
  }
  // the wake of a rested slot DOES allocate in the audio thread (pump_free + pump_new, as in the JS): shown here outside the guard so that the claim in fxpump.cpp is a measured one
  { FxHost* W = new FxHost(48000.0); FxEntry we[1]; pump_entry(&we[0], 3, 0, false); FxPlan* p = W->plan(we, 1); W->commit(p); delete p;
    std::vector<float> a(128), c(128); for (int b = 0; b < 400; b++) { for (int i = 0; i < 128; i++) a[(size_t)i] = c[(size_t)i] = rn() * 0.3f; W->process(a.data(), c.data(), 128, b * 128 / 48000.0, bp.data(), 128); }
    we[0].on = true; p = W->plan(we, 1); W->commit(p); delete p;
    for (int b = 400; b < 410; b++) { for (int i = 0; i < 128; i++) a[(size_t)i] = c[(size_t)i] = rn() * 0.3f; W->process(a.data(), c.data(), 128, b * 128 / 48000.0, bp.data(), 128); }
    const unsigned wk = W->audioRemakes(); delete W;
    if (!wk) { std::printf("rt_pump note: no wake remake seen for an OFF slot switched on (the rest did not start in this run)\n"); } else std::printf("rt_pump: a rested slot switched on made %u instance(s) in the audio thread (as the JS does; not under the guard)\n", wk); }
  uint32_t remakes = 0, gated = 0; for (int h = 0; h < kH; h++) { remakes += H[h]->audioRemakes(); gated += H[h]->pumpGated(); }
  for (int h = 0; h < kH; h++) { delete swap[h]; delete shrink[h]; delete H[h]; }
  if (remakes || gated) { std::printf("rt_pump FAIL: %u audio-thread instance remakes, %u gated blocks in a run that should have none\n", remakes, gated); return 1; }
  std::printf("rt_pump ok: %d blocks x %d hosts, all %d pump types under the allocation guard (values moved, FXO, FXV, notes, sidechain, telemetry, rest, chain swaps)\n", nb, kH, kPumpTypes); return 0;
#endif
}

// U16: Beat grains, Beat scramble, Glitch sequencer and the Vintage sampler under the guard: the rings are made in plan(), the grain list is a fixed array, process() takes the beat
// clock and the lanes, odd block sizes, a clock that stops and jumps (the grains free-run), a chain swap that retires three of them (deleted outside the guard with the plan).
static int run_fxidm() {
  using namespace thunder;
  FxHost H(48000.0); FxEntry e[4]; const int types[4] = {100, 101, 102, 103};
  for (int i = 0; i < 4; i++) { const int id = types[i]; e[i].t = id; e[i].slot = i; e[i].on = true; std::snprintf(e[i].key, kFxKey, "%d:%s", i, fx_key_of(id)); e[i].nvals = fx_param_count(id);
    for (int k = 0; k < e[i].nvals; k++) e[i].vals[k] = fx_param_default(id, k);
    if (id == 100) { e[i].vals[1] = 255; e[i].vals[6] = 3; e[i].vals[0] = 3; }   // density 255, RANDOM PITCH any, 1/32: the grain list fills up
    if (id == 101) { e[i].vals[1] = 255; e[i].vals[3] = 0; }
    if (id == 102) { e[i].vals[1] = 255; }
    fx_map(id, e[i].vals, 48000.0, 120.0, 0.0, e[i].p); }
  FxPlan* p1 = H.plan(e, 4); H.commit(p1); delete p1;
  FxPlan* p2 = H.plan(e + 3, 1);   // the Vintage sampler only: the other three are retired
  std::vector<float> L(4096), R(4096), bp(4096); uint32_t sd = 7; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (float)(sd >> 8) / 16777216.f - 0.5f; };
  double t = 0, beat = 0; int nb = 0;
  { rt_guard g;
    for (int b = 0; b < 3000; b++) {
      const int n = (b % 5 == 0) ? 1 + (b * 37) % 1000 : 128; bool run = !(b >= 1000 && b < 1100);
      for (int i = 0; i < n; i++) { L[(size_t)i] = b < 2500 ? rnd() * 0.5f : 0.f; R[(size_t)i] = b < 2500 ? rnd() * 0.5f : 0.f; bp[(size_t)i] = run ? (float)std::fmod(beat, 16.0) : -1.f; if (run) beat += 2.0 / 48000 * 4; }
      if (b == 1500) beat = 3.2;   // a jump (a song loop)
      if (b == 600) H.lane(0, 0, t); if (b == 700) H.lane(0, -1, t);
      if (b == 2000) H.commit(p2);
      H.process(L.data(), R.data(), n, t, bp.data(), b % 11 == 0 ? 1 : n); t += n / 48000.0; nb++; }
    H.process(L.data(), R.data(), 100, t, nullptr, 0);   // the clock param absent
  }
  delete p2;
  if (H.laneDropped() || H.queueDropped()) { std::printf("rt_fxidm FAIL: dropped lanes %u queue %u\n", H.laneDropped(), H.queueDropped()); return 1; }
  if (H.slotCount() != 1 || H.slotType(0) != 103) { std::printf("rt_fxidm FAIL: the chain swap left %d slots\n", H.slotCount()); return 1; }
  std::printf("rt_fxidm ok: %d blocks, Beat grains / scramble / Glitch / Vintage sampler under the allocation guard\n", nb); return 0;
}

// U19: every MODFX module (core/src/modfx) under the guard: made (rings and all) outside it, then 1500 blocks each with noise on every input, parameters moving over their ranges, ports
// linked and unlinked, partial blocks (the output is always the whole buffer), tempo / start changes, at 44.1 / 48 / 96 kHz. Nothing may allocate.
static int run_modfx() {
  using namespace thunder::modfx;
  long blocks = 0; const double srs[3] = {44100, 48000, 96000};
  for (int si = 0; si < 3; si++) for (int m = 0; m < count(); m++) {
    const Def& d = at(m); const double sr = srs[si]; SeedCounter seeds(12345 + (uint32_t)m); std::unique_ptr<Module> mod = d.make(sr, seeds);
    static float in[kMaxPorts][kBlock], out[kMaxPorts][kBlock]; static double p[128]; IO io; Ctx c; c.sr = sr;   // U31: kMaxPorts grew to 32; the random draws stay those of the 20 ports this loop was written with
    for (int k = 0; k < kMaxPorts; k++) { io.in[k] = in[k]; io.out[k] = out[k]; io.linked[k] = false; } io.p = p;
    uint32_t sd = 99 + (uint32_t)m; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (double)(sd >> 8) / 16777216.0; };
    { rt_guard g;
      for (int b = 0; b < 1500; b++) {
        for (int k = 0; k < 20; k++) { io.linked[k] = ((b / 50 + k) % 3) != 0; for (int i = 0; i < kBlock; i++) in[k][i] = b < 1100 ? (float)(rnd() * 1.6 - .8) : 0.f; }
        for (int k = 0; k < d.np; k++) { const ParamDef& q = d.params[k]; p[k] = b < 200 ? q.def : q.mn + (q.mx - q.mn) * rnd(); }
        c.block = (b % 7 == 0) ? 1 + (b * 13) % 127 : kBlock; c.full = (b % 5) != 0; c.tempo = 60 + (b / 100) * 20; c.startN = (double)(b / 400); c.playing = (b % 9) != 0;
        mod->run(io, c); blocks++; }
    }
  }
  std::printf("rt_modfx ok: %d modules x 3 sample rates, %ld blocks under the allocation guard\n", count(), blocks); return 0;
}

// U17: Pitch reverb, Pitch delay, Resonator and Degrade under the guard: every buffer is made in plan(), process() takes the block and the lanes, odd block sizes, a switch off and on
// (the 5 ms fade), a settings change by FXV, a chain swap that retires three of them (deleted outside the guard with the plan), silence for long enough to nap the three that nap.
static int run_fxu17() {
  using namespace thunder;
  FxHost H(48000.0); FxEntry e[4]; const int types[4] = {96, 97, 98, 99};
  for (int i = 0; i < 4; i++) { const int id = types[i]; e[i].t = id; e[i].slot = i; e[i].on = true; std::snprintf(e[i].key, kFxKey, "%d:%s", i, fx_key_of(id)); e[i].nvals = fx_param_count(id);
    for (int k = 0; k < e[i].nvals; k++) e[i].vals[k] = fx_param_default(id, k);
    if (id == 96) { e[i].vals[0] = 2; e[i].vals[2] = 255; }   // Dual, SHIMMER full
    if (id == 97) { e[i].vals[0] = 3; e[i].vals[2] = 200; e[i].vals[5] = 255; }   // Tape, FEEDBACK, WOBBLE
    fx_map(id, e[i].vals, 48000.0, 120.0, 0.0, e[i].p); }
  FxPlan* p1 = H.plan(e, 4); H.commit(p1); delete p1;
  FxPlan* p2 = H.plan(e + 3, 1);   // Degrade only: the other three are retired
  std::vector<float> L(4096), R(4096), bp(4096, 0.f); uint32_t sd = 7; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (float)(sd >> 8) / 16777216.f - 0.5f; };
  double t = 0; int nb = 0;
  { rt_guard g;
    for (int b = 0; b < 3000; b++) {
      const int n = (b % 5 == 0) ? 1 + (b * 37) % 1000 : 128;
      for (int i = 0; i < n; i++) { L[(size_t)i] = b < 2000 ? rnd() * 0.5f : 0.f; R[(size_t)i] = b < 2000 ? rnd() * 0.5f : 0.f; }
      if (b == 600) H.lane(1, 0, t); if (b == 700) H.lane(1, -1, t);
      if (b == 900) { double pv[12]; for (int k = 0; k < 12; k++) pv[k] = e[2].p[k]; pv[1] = 10; H.psetAt(t, 2, pv); }
      if (b == 2200) H.commit(p2);
      H.process(L.data(), R.data(), n, t, bp.data(), 1); t += n / 48000.0; nb++; }
    H.process(L.data(), R.data(), 100, t, nullptr, 0);
  }
  delete p2;
  if (H.laneDropped() || H.queueDropped()) { std::printf("rt_fxu17 FAIL: dropped lanes %u queue %u\n", H.laneDropped(), H.queueDropped()); return 1; }
  if (H.slotCount() != 1 || H.slotType(0) != 99) { std::printf("rt_fxu17 FAIL: the chain swap left %d slots\n", H.slotCount()); return 1; }
  std::printf("rt_fxu17 ok: %d blocks, Pitch reverb / Pitch delay / Resonator / Degrade under the allocation guard\n", nb); return 0;
}

// U22: the engine foundation under the guard: the render of a voice WITHOUT pad effects (layers of every type of this unit, the burst, the layer and voice filters, the voice amp, the LFO / envelope /
// sequence matrix, the drive) into a buffer the caller made, over and over. (A voice WITH the four JS pad effects allocates: U17's pad_* return std::vector. That is an offline render, not an audio path.)
static int run_engine() {
  using namespace thunder;
  eng_sin_table();   // (made on first use: outside the guard)
  std::vector<EngVoice> vs(3); std::vector<std::vector<float>> bufs; std::vector<EngRenderOpts> os(3);
  { EngVoice& V = vs[0]; V.drive = 0.3; V.level = -3; V.layers[0].on = true; V.layers[0].type = LType::Sine; V.layers[0].pAmt = 30; V.layers[0].pDec = 55; V.layers[0].dec = 300; V.layers[0].seed = 11;
    V.layers[1].on = true; V.layers[1].type = LType::White; V.layers[1].burst = 3; V.layers[1].spacing = 11; V.layers[1].hpCut = 850; V.layers[1].lpCut = 2600; V.layers[1].lpRes = 0.35; V.layers[1].seed = 12;
    V.layers[2].on = true; V.layers[2].type = LType::Metal; V.layers[2].a = 0.5; V.layers[2].hpCut = 7200; V.layers[2].delay = 20; V.layers[2].seed = 13; }
  { EngVoice& V = vs[1]; V.tune = 24; V.gate = 400; V.len = 6; os[1].hold = 1;
    for (int i = 0; i < 3; i++) { EngLayer& L = V.layers[i]; L.on = true; L.adsr = true; L.type = i == 0 ? LType::Saw : i == 1 ? LType::Square : LType::Tri; L.fine = i * 9 - 9; L.atk = 20; L.dec = 600; L.sus = 0.7; L.rel = 300; L.lpCut = 1800; L.lpEnv = 0.3; L.seed = 20 + (double)i; }
    V.gLpCut = 3000; V.gLpEnv = 0.4; V.gAmpOn = true; V.gAdsr = true; V.gAtk = 5; V.gDec = 400; V.gSus = 0.8; V.gRel = 200;
    V.lfoRate[0] = 5; V.lfoDepth[0] = 1; V.lfoShape[0] = 5; V.rdst[0] = 1; V.ramt[0] = 0.4; V.lfoRate[1] = 3; V.lfoDepth[1] = 1; V.lfoShape[1] = 4; V.rdst[1] = 3; V.ramt[1] = 3; V.lfoSync[2] = 3; V.lfoDepth[2] = 1; V.rdst[2] = 2; V.ramt[2] = 0.2;
    V.envS[0] = 0.5; V.rdst[3] = 4; V.ramt[3] = 1; V.seqDepth = 1; V.seqSync = 2; V.rdst[6] = 1; V.ramt[6] = 0.5; for (int i = 0; i < 32; i++) V.seq[i] = (i % 5) / 4.0 - 0.3; }
  { EngVoice& V = vs[2]; V.len = 1.5; static const LType t[8] = {LType::Pink, LType::Brown, LType::Blue, LType::Lfsr, LType::Dust, LType::Grit, LType::Pulse, LType::Sine};
    for (int i = 0; i < 8; i++) { EngLayer& L = V.layers[i]; L.on = true; L.type = t[i]; L.a = 0.3; L.b = 0.6; L.level = 0.4; L.seed = 100 + (double)i; L.atk = 1; L.dec = 800; } os[2].sr = 44100; }
  for (size_t i = 0; i < vs.size(); i++) { bufs.emplace_back(eng_render_bound(vs[i], os[i]) + 1); if (bufs[i].size() < 2) { std::printf("rt_engine FAIL: bound 0\n"); return 1; } }
  size_t total = 0; int renders = 0;
  { rt_guard g;
    for (int rep = 0; rep < 12; rep++) for (size_t i = 0; i < vs.size(); i++) { size_t n = 0; const int st = eng_render_voice(vs[i], os[i], bufs[i].data(), bufs[i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
  }
done:
  if (!total) { std::printf("rt_engine FAIL: a render failed\n"); return 1; }
  std::printf("rt_engine ok: %d voice renders (%zu samples) under the allocation guard\n", renders, total); return 0;
}

// U23: the layer types click, wavetable, pd, chip, supersaw, pwm, sample (plain and looped), grains and the wavetable's table rebuilds under the guard: renders into a caller's buffer, over and over, no allocation.
// (A sample layer with a vintage mode makes the machine-rate copy of its sample: that allocates, an offline render; it is not in this loop.)
static int run_engine_u23() {
  using namespace thunder;
  eng_init();   // (the sine table and the PWM sampler's built-in cymbal: made outside the guard)
  static float smpData[3000]; for (int i = 0; i < 3000; i++) smpData[i] = (float)(((i * 7919) % 2003) / 1001.5 - 1) * 0.8f;
  std::vector<EngVoice> vs(3); std::vector<std::vector<float>> bufs; std::vector<EngRenderOpts> os(3);
  { EngVoice& V = vs[0]; V.len = 0.6; V.level = -2; static const LType t[4] = {LType::Click, LType::Chip, LType::Pd, LType::Supersaw};
    for (int i = 0; i < 4; i++) { EngLayer& L = V.layers[i]; L.on = true; L.type = t[i]; L.a = 0.2 + 0.2 * i; L.b = 0.3 + 0.15 * i; L.level = 0.5; L.seed = 30 + (double)i; L.atk = 1; L.dec = 300; L.tune = i * 5; } }
  { EngVoice& V = vs[1]; V.len = 0.5; EngLayer& L = V.layers[0]; L.on = true; L.type = LType::Wavetable; L.a = 0.6; L.b = 0.2; L.pAmt = -30; L.pDec = 200; L.dec = 400; L.seed = 41; L.u23.wtUni = 5; L.u23.wtSub = 0.3; L.u23.wtNoise = 0.1; L.u23.wtBend = -0.4;
    V.lfoRate[0] = 6; V.lfoDepth[0] = 1; V.lfoShape[0] = 0; V.rdst[0] = 1; V.ramt[0] = 3; }
  { EngVoice& V = vs[2]; V.len = 0.8; static const LType t[4] = {LType::Pwm, LType::Sample, LType::Sample, LType::Grains};
    for (int i = 0; i < 4; i++) { EngLayer& L = V.layers[i]; L.on = true; L.type = t[i]; L.a = 0.1 + 0.1 * i; L.b = i == 1 ? 0.8 : 0.3; L.level = 0.5; L.seed = 50 + (double)i; L.atk = 1; L.dec = 600; L.u23.smp = i == 0 ? nullptr : smpData; L.u23.smpLen = i == 0 ? 0 : 3000; L.u23.smpSr = 22050;
      if (i == 2) { L.u23.lpMode = 1; L.u23.lpS = 0.1; L.u23.lpE = 0.8; L.u23.lpX = 0.2; } } }
  for (size_t i = 0; i < vs.size(); i++) { bufs.emplace_back(eng_render_bound(vs[i], os[i]) + 1); if (bufs[i].size() < 2) { std::printf("rt_engine_u23 FAIL: bound 0\n"); return 1; } }
  size_t total = 0; int renders = 0;
  { rt_guard g;
    for (int rep = 0; rep < 8; rep++) for (size_t i = 0; i < vs.size(); i++) { size_t n = 0; const int st = eng_render_voice(vs[i], os[i], bufs[i].data(), bufs[i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
  }
done:
  if (!total) { std::printf("rt_engine_u23 FAIL: a render failed\n"); return 1; }
  std::printf("rt_engine_u23 ok: %d voice renders (%zu samples) under the allocation guard\n", renders, total); return 0;
}

// U28: the rendered-only engines under the guard: every engine of the unit that is meant not to allocate (all but strings and wind, whose delay lines are Float32Arrays sized from the pitch: std::vector here, an offline render)
// renders into a caller's buffer, over and over. (A voice with the four JS pad effects allocates: U17's pad_* return std::vector.) Drift's chip-sized delay line lives on the stack.
static int run_engine_xeng() {
  using namespace thunder;
  static const char* const kinds[13] = {"tone", "modal", "swarm", "drift", "piano", "ep", "organ", "breath", "fmdrum", "phase", "partials", "skin", "chip"};
  std::vector<XengVoice> vs(13); std::vector<std::vector<float>> bufs; std::vector<EngRenderOpts> os(13);
  for (int i = 0; i < 13; i++) { XengVoice& V = vs[i]; V.kind = xeng_kind(kinds[i], std::strlen(kinds[i])); V.level = -3; V.drive = i % 3 == 0 ? 0.3 : 0; V.len = 0.6; os[i].hold = i % 2 ? 0.3 : 0; }
  vs[2].swarm.voices = 24; vs[2].swarm.scatter = 0.5; vs[2].swarm.shimmer = 0.4; vs[2].swarm.bloom = 200; vs[3].drift.voices = 4; vs[3].drift.age = 0.8; vs[4].piano.strings = 3; vs[6].organ.rotary = 2; vs[6].organ.perc = 1; vs[6].organ.d4 = 5;
  vs[10].partials.count = 128; vs[10].partials.famt = 0.5; vs[1].modal.modes = 24; vs[1].modal.exc = 2; vs[7].breath.grain = 0.5; vs[7].breath.bands = 8;
  for (size_t i = 0; i < vs.size(); i++) { bufs.emplace_back(xeng_render_bound(vs[i], os[i]) + 1); if (bufs[i].size() < 2) { std::printf("rt_engine_xeng FAIL: bound 0 (%s)\n", kinds[i]); return 1; } }
  size_t total = 0; int renders = 0;
  { rt_guard g;
    for (int rep = 0; rep < 3; rep++) for (size_t i = 0; i < vs.size(); i++) { size_t n = 0; const int st = xeng_render_voice(vs[i], os[i], bufs[i].data(), bufs[i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
  }
done:
  if (!total) { std::printf("rt_engine_xeng FAIL: a render failed\n"); return 1; }
  std::printf("rt_engine_xeng ok: %d voice renders (%zu samples) of 13 engines under the allocation guard\n", renders, total); return 0;
}

// U29: the second set of rendered-only engines under the guard: table (with a loaded wavetable), vector (both modes), scanned and resynth (with an analysis) take every array from the per-thread scratch arena
// (xeng_util.h): one warm-up render of each outside the guard sizes it, then the same renders run over and over without a single allocation. Bytebeat is not here: its compiled formula is std::vector (an offline render).
static std::string xb64(const std::vector<uint8_t>& u) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string s;
  for (size_t i = 0; i < u.size(); i += 3) { const unsigned a = u[i], b = i + 1 < u.size() ? u[i + 1] : 0, c = i + 2 < u.size() ? u[i + 2] : 0, v = (a << 16) | (b << 8) | c;
    s += T[(v >> 18) & 63]; s += T[(v >> 12) & 63]; s += i + 1 < u.size() ? T[(v >> 6) & 63] : '='; s += i + 2 < u.size() ? T[v & 63] : '='; }
  return s;
}
static int run_engine_xeng2() {
  using namespace thunder;
  XengVoice V[5]; EngRenderOpts o[5]; std::vector<std::vector<float>> bufs;
  uint32_t seed = 99; auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (int)(seed >> 16); };
  V[0].kind = XKind::Table; V[0].table.table = 4; V[0].table.unison = 3; V[0].table.pos = 0.4; V[0].table.pmod = 0.3;
  { std::vector<uint8_t> u; for (int i = 0; i < 6 * 12 * 4; i++) u.push_back((uint8_t)rnd()); V[0].wt.present = V[0].wt.valid = true; V[0].wt.nf = 6; V[0].wt.h = 12; V[0].wt.pk = 0.5; V[0].wt.a = xb64(u); }
  V[1].kind = XKind::Vector; V[1].vector.unison = 3; V[1].vector.venv = 1; V[1].vector.lx = 0.4; V[1].vector.wa = 12; V[1].vector.wb = 13; V[1].vector.wc = 16; V[1].vector.wd = 17;
  V[2].kind = XKind::Vector; V[2].vector.mode = 1; V[2].vector.steps = 6; V[2].vector.sgate = 0.5; V[2].vector.sfenv = 0.5; V[2].vector.unison = 2; V[2].vector.s1 = 17; V[2].vector.spread = 0.5; o[2].bpm = 140;
  V[3].kind = XKind::Scanned; V[3].scanned.exc = 2; V[3].scanned.path = 2; V[3].scanned.rexc = 4; V[3].scanned.hard = 0.5; V[3].scanned.masses = 80;
  V[4].kind = XKind::Resynth; V[4].resynth.fkeep = 1; V[4].resynth.noise = 1; V[4].resynth.pmot = 1; V[4].resynth.harm = 0.4;
  { XRsData& r = V[4].rs; r.present = true; r.pitched = true; r.np = 5; r.nf = 20; r.hop = 0.02; r.f0 = 220; r.dur = 0.4; r.pk = 1; r.npk = 1; r.rat = {1, 2, 3, 4.02, 5.1}; r.hn = {1, 2, 3, 4, 5};
    std::vector<uint8_t> a, n, pc; for (int i = 0; i < 100; i++) a.push_back((uint8_t)(120 + rnd() % 130)); for (int i = 0; i < 160; i++) n.push_back((uint8_t)(100 + rnd() % 100)); for (int i = 0; i < 40; i++) pc.push_back((uint8_t)(rnd() & 255));
    r.a = xb64(a); r.n = xb64(n); r.pc = xb64(pc); }
  for (int i = 0; i < 5; i++) { V[i].level = -3; V[i].len = 0.5; o[i].hold = 0.3; bufs.emplace_back(xeng_render_bound(V[i], o[i]) + 1); if (bufs[(size_t)i].size() < 2) { std::printf("rt_engine_xeng2 FAIL: bound 0 (%d)\n", i); return 1; } }
  size_t total = 0; int renders = 0;
  for (int i = 0; i < 5; i++) { size_t n = 0; if (xeng_render_voice(V[i], o[i], bufs[(size_t)i].data(), bufs[(size_t)i].size() - 1, &n) != kEngOk) { std::printf("rt_engine_xeng2 FAIL: warm-up render %d\n", i); return 1; } }   // sizes the arena (and the FFT twiddles)
  { rt_guard g;
    for (int rep = 0; rep < 3; rep++) for (int i = 0; i < 5; i++) { size_t n = 0; const int st = xeng_render_voice(V[i], o[i], bufs[(size_t)i].data(), bufs[(size_t)i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
  }
done:
  if (!total) { std::printf("rt_engine_xeng2 FAIL: a render failed\n"); return 1; }
  std::printf("rt_engine_xeng2 ok: %d voice renders (%zu samples) of table / vector / scanned / resynth under the allocation guard\n", renders, total); return 0;
}

// U27: acid, mach (all seven machines) and blip under the guard: nothing at all allocates (the cores are plain objects on the stack, the render goes into the caller's buffer), so no warm-up render. Also the cores driven directly
// the way a live path would (note on with legato / accent, blocks of 64, a new rate each block).
static int run_engine_xeng3() {
  using namespace thunder;
  std::vector<XengVoice> V; std::vector<EngRenderOpts> o; std::vector<std::vector<float>> bufs;
  { XengVoice a; a.kind = XKind::Acid; a.acid.res = 0.9; a.acid.wave = 0.4; a.acid.sub = 0.3; a.acid.noise = 0.1; a.acid.drive = 0.3; V.push_back(a); }
  for (int m = 0; m < 7; m++) { XengVoice a; a.kind = XKind::Mach; a.mach.machine = m; a.mach.lfoDepth = 0.5; a.mach.lfoDest = m % 4; a.mach.glide = 80; V.push_back(a); }
  { XengVoice a; a.kind = XKind::Blip; a.blip.filt = -0.4; a.blip.grain = 3; a.blip.fm = 0.3; a.blip.ring = 0.4; a.blip.noise = 0.3; a.blip.drive = 0.4; V.push_back(a); }
  { XengVoice a; a.kind = XKind::Blip; a.blip.filt = 0.6; a.blip.res = 0.9; V.push_back(a); }
  for (XengVoice& v : V) { v.level = -3; v.len = 0.5; EngRenderOpts e; e.hold = 0.3; o.push_back(e); bufs.emplace_back(xeng_render_bound(v, e) + 1); if (bufs.back().size() < 2) { std::printf("rt_engine_xeng3 FAIL: bound 0\n"); return 1; } }
  size_t total = 0; int renders = 0; std::vector<float> L(48000), R(48000);
  { rt_guard g;
    for (int rep = 0; rep < 3; rep++) for (size_t i = 0; i < V.size(); i++) { size_t n = 0; const int st = xeng_render_voice(V[i], o[i], bufs[i].data(), bufs[i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
    { AcidCore a(48000); MachCore m(48000); AcidP ap; MachP mp; mp.machine = 3; a.set(ap); m.set(mp);
      for (int rep = 0; rep < 2; rep++) { a.on(rep > 0, rep == 0); m.on(rep > 0);
        for (int b = 0; b < 40; b++) { a.render(L.data(), R.data(), L.size(), b * 64, b * 64 + 64, 1 + 0.01 * b); m.render(L.data(), R.data(), L.size(), b * 64, b * 64 + 64, 1 + 0.01 * b); if (m.bad()) { total = 0; goto done; } total += 128; }
        a.off(); m.off(); a.set(ap); m.set(mp); renders += 2; } }
  }
done:
  if (!total) { std::printf("rt_engine_xeng3 FAIL: a render failed\n"); return 1; }
  std::printf("rt_engine_xeng3 ok: %d renders (%zu samples) of acid / mach (7 machines) / blip and the live cores under the allocation guard\n", renders, total); return 0;
}

// U26: bass (renderBass through xeng_render_voice: nothing allocates, the core is a plain object on the stack), the bass and voice cores driven the way a live path would, and the LIVE SHELL (live_shell.h): construction allocates
// (a voice's lyrics are parsed and its segment list sized: control thread), process() with note / rate / bpm / wob events never does. The vox renders themselves (xeng_vox, xeng_render_voice for a vox voice) DO allocate (the lyrics,
// offline only) and are not in the guard: documented in xeng.h.
static int run_engine_u26() {
  using namespace thunder;
  std::vector<XengVoice> V; std::vector<EngRenderOpts> o; std::vector<std::vector<float>> bufs;
  { XengVoice a; a.kind = XKind::Bass; a.bass.det = 20; a.bass.parts = 64; a.bass.wCut = 3; a.bass.wShape = 2; a.bass.comb = 0.4; a.bass.vowel = 1.5; V.push_back(a); }
  { XengVoice a; a.kind = XKind::Bass; a.bass.shape = 0.6; a.bass.sub = 0.7; a.bass.glide = 80; a.bass.wRate = 9; a.bass.wCut = 2; V.push_back(a); }
  for (XengVoice& v : V) { v.level = -3; v.len = 0.5; EngRenderOpts e; e.hold = 0.3; o.push_back(e); bufs.emplace_back(xeng_render_bound(v, e) + 1); if (bufs.back().size() < 2) { std::printf("rt_engine_u26 FAIL: bound 0\n"); return 1; } }
  std::vector<float> L(4096), R(4096); size_t total = 0; int renders = 0;
  LiveBass* lb = new LiveBass(48000); LiveVox* lv = new LiveVox(48000); { VoxP p; p.txt = "ba-na-na, la"; lv->core().set(p); } { BassP p; p.wCut = 3; p.det = 20; lb->core().set(p); }
  BassCore bc(48000); { BassP p; p.parts = 64; bc.set(p); } VoxCore vc(48000); { VoxP p; vc.set(p); }
  {
    rt_guard g;
    for (int rep = 0; rep < 3; rep++) for (size_t i = 0; i < V.size(); i++) { size_t n = 0; const int st = xeng_render_voice(V[i], o[i], bufs[i].data(), bufs[i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
    for (int rep = 0; rep < 2; rep++) { bc.on(rep > 0); vc.on(rep > 0, rep, false);
      for (int b = 0; b < 30; b++) { bc.render(L.data(), R.data(), L.size(), b * 64, b * 64 + 64, 1 + 0.01 * b); vc.render(L.data(), R.data(), L.size(), b * 64, b * 64 + 64, 1 + 0.01 * b); if (bc.bad()) { total = 0; goto done; } total += 128; }
      bc.off(); vc.off(); renders += 2; }
    // the live shell: blocks of 128 and of 17, events at every kind
    for (int blk : {128, 17, 1}) { LiveEvent ev[6]; for (int k = 0; k < 6; k++) { ev[k].at = (uint32_t)(k * 3 % blk); ev[k].type = (LiveEvType)k; ev[k].v = k == 2 || k == 3 ? 1.5 : k == 4 ? 140 : 0.5; ev[k].dur = 500; ev[k].leg = k & 1; }
      ev[0].type = LiveEvType::NoteOn; ev[1].type = LiveEvType::NoteOff;
      for (int rep = 0; rep < 24; rep++) { const uint32_t n = (uint32_t)blk; LiveEvent mine[6]; uint32_t m = 0; for (int k = 0; k < 6; k++) if (rep % 4 == 0 || k >= 2) mine[m++] = ev[k]; mine[0].type = (rep % 2) ? LiveEvType::NoteOn : LiveEvType::NoteOff;
        lb->process(L.data(), R.data(), n, mine, m); lv->process(L.data(), R.data(), n, mine, m); total += 2 * n; } renders += 2; }
  }
done:
  delete lb; delete lv;
  if (!total) { std::printf("rt_engine_u26 FAIL: a render failed\n"); return 1; }
  std::printf("rt_engine_u26 ok: %d renders (%zu samples) of bass, the bass / voice cores and the live shell (bass, vox) under the allocation guard\n", renders, total); return 0;
}

// U33: the pump synths' per-block interface (PumpSynth: the live lanes' reusable path) under the guard, WITH events. Control thread, outside the guard: every instrument is made (psyn_new allocates) and FREEZE's built-in
// spectra are built (PumpSynth::builtins). Under the guard, for all 13 instruments: 640 blocks of 32 to 512 frames in which every kind of block occurs: no event; a note-on; a note-off; several events at once (note-ons and
// -offs, a chord with equal offsets, offsets NOT in queue order so the event sort has work to do, a pitch bend and a controller); and a block with 40 events queued in reverse order. Nothing may allocate in any of them.
// (Before the fourth patch of third_party/prepare-pump.sh every block with an event made one operator new in all 13 synths: the temporary buffer of std::stable_sort in pump's SynthBase::run; this case then aborts
// with "operator new" inside the guard. The first note-on of each instrument is under the guard too.) The rendered path (xeng_psyn) is offline and allocates by design (an instance per render).
static int run_pumpsynth() {
#ifndef THUNDER_HAVE_PUMP
  std::printf("rt_pumpsynth: not run (no pump library on this platform)\n"); return 0;
#else
  using namespace thunder;
  PumpSynth::builtins();
  const int ns = 13; PumpSynth S[13]; size_t total = 0; unsigned long events = 0, blocksWithEvents = 0;
  for (int t = 0; t < ns; t++) {
    if (!S[t].make(t, 48000.0, nullptr, 0)) { std::printf("rt_pumpsynth FAIL: type %d not made\n", t); return 1; }
    if (t == 12) S[t].tempo(120.0);
  }
  { rt_guard g;
    for (int b = 0; b < 640; b++) for (int t = 0; t < ns; t++) {
      const int n = 32 + (b * 37 + t * 11) % 481; const int kind = (b + t) % 8; bool any = true;
      switch (kind) {
        case 0: S[t].midi(0, 0x90, 60, 100); events++; break;                                                   // a note-on
        case 1: S[t].midi(n / 2, 0x80, 60, 0); events++; break;                                                 // a note-off
        case 2: S[t].midi(n - 1, 0x90, 64, 90); S[t].midi(n - 1, 0x90, 67, 90); S[t].midi(n - 1, 0x90, 71, 90); events += 3; break;   // a chord, equal offsets: the sort must keep their order
        case 3: S[t].midi(n - 1, 0x80, 64, 0); S[t].midi(3, 0x90, 72, 110); S[t].midi(n / 3, 0x80, 67, 0); S[t].midi(0, 0xE0, 0, 80); S[t].midi(1, 0xB0, 1, 64); events += 5; break;   // out of order, bend, CC
        case 4: for (int e = 0; e < 40; e++) S[t].midi(n > e ? n - 1 - e : 0, e % 2 ? 0x80 : 0x90, 40 + e, 80); events += 40; break;   // 40 events, reverse offsets
        default: any = false; break;                                                                            // no event
      }
      if (any) blocksWithEvents++;
      S[t].process(n); const float* L = PumpSynth::out(0); for (int i = 0; i < n; i++) total += std::isfinite(L[i]) ? 1u : 0u;
    }
  }
  std::printf("rt_pumpsynth ok: 13 instruments x 640 blocks (%zu finite samples of the left channel; %lu blocks with events, %lu events: note-on, note-off, several at once, reverse order) under the allocation guard\n", total, blocksWithEvents, events);
  return 0;
#endif
}

// U24: the FM / additive / karplus / vosim / formant layers under the guard (their oscillator objects live on the stack: Karplus-Strong's delay line is a 16 KB member, nothing allocates).
static int run_engine_u24() {
  using namespace thunder;
  eng_sin_table();
  static const LType t[3][6] = {{LType::Fm2, LType::Fmfb, LType::Fmmetal, LType::Fmcross, LType::Fm4, LType::Fm6}, {LType::Genesis, LType::Sb, LType::Arcade, LType::Addsaw, LType::Addodd, LType::Membrane},
    {LType::Bell, LType::Bar, LType::Chord, LType::Karplus, LType::Vosim, LType::Formant}};
  std::vector<EngVoice> vs(3); std::vector<std::vector<float>> bufs; std::vector<EngRenderOpts> os(3);
  for (int v = 0; v < 3; v++) { EngVoice& V = vs[(size_t)v]; V.len = 1.0; V.drive = v == 1 ? 0.3 : 0; V.gate = 300;
    for (int i = 0; i < 6; i++) { EngLayer& L = V.layers[i]; L.on = true; L.type = t[v][i]; L.a = 0.35 + 0.1 * i; L.b = 0.45; L.level = 0.4; L.tune = (double)(i * 3 - 6); L.atk = 1; L.dec = 600; L.seed = 300 + (double)(v * 10 + i); L.adsr = v == 2 && i % 2; if (L.adsr) { L.sus = 0.6; L.rel = 200; } }
    V.layers[0].lpCut = 2500; V.layers[0].lpEnv = 0.4; V.layers[1].burst = 2; V.layers[1].spacing = 10; }
  os[1].sr = 44100; os[2].hold = 0.4;
  for (size_t i = 0; i < vs.size(); i++) { bufs.emplace_back(eng_render_bound(vs[i], os[i]) + 1); if (bufs[i].size() < 2) { std::printf("rt_engine_u24 FAIL: bound 0\n"); return 1; } }
  size_t total = 0; int renders = 0;
  { rt_guard g;
    for (int rep = 0; rep < 6; rep++) for (size_t i = 0; i < vs.size(); i++) { size_t n = 0; const int st = eng_render_voice(vs[i], os[i], bufs[i].data(), bufs[i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
  }
done:
  if (!total) { std::printf("rt_engine_u24 FAIL: a render failed\n"); return 1; }
  std::printf("rt_engine_u24 ok: %d voice renders (%zu samples) of FM / additive / karplus / vosim / formant layers under the allocation guard\n", renders, total); return 0;
}

// U25: the Plaits / Elements layers under the guard. The workspace (the library's Session and the pre-render buffer, about 1.8 MB per thread) is made once off the audio thread (eng_init -> pe_prepare);
// after that a render of these layers does not allocate: the Session is zeroed in place, the pre-render goes into the thread's buffer. (Without the library the types are not supported: the case is skipped.)
static int run_engine_u25() {
  using namespace thunder;
  if (!pe_available()) { std::printf("rt_engine_u25 skipped: no Plaits / Elements library on this platform\n"); return 0; }
  eng_init();
  std::vector<EngVoice> vs(4); std::vector<std::vector<float>> bufs; std::vector<EngRenderOpts> os(4);
  vs[0].layers[0].on = true; vs[0].layers[0].type = LType::Plaits; vs[0].layers[0].pe.pEng = 8; vs[0].layers[0].dec = 800; vs[0].len = 0.8;
  vs[1].layers[0].on = true; vs[1].layers[0].type = LType::Plaits; vs[1].layers[0].pe.pEng = 18; vs[1].layers[0].pe.pStrike = 0; vs[1].layers[0].adsr = true; vs[1].layers[0].lpCut = 3000; vs[1].layers[1].on = true; vs[1].layers[1].type = LType::Plaits;
  vs[1].layers[1].pe.pEng = 16; vs[1].len = 0.6; os[1].hold = 0.3;
  vs[2].layers[0].on = true; vs[2].layers[0].type = LType::Elements; vs[2].layers[0].pe.eModel = 1; vs[2].layers[0].pe.eBow = 0.5; vs[2].layers[0].dec = 1000; vs[2].layers[0].tune = 12; vs[2].len = 0.8; os[2].sr = 44100;
  vs[3].layers[0].on = true; vs[3].layers[0].type = LType::Elements; vs[3].layers[0].pe.eModel = 0; vs[3].layers[1].on = true; vs[3].layers[1].type = LType::Plaits; vs[3].layers[1].pe.pEng = 20; vs[3].layers[1].burst = 2; vs[3].len = 0.6; vs[3].drive = 0.3;
  for (size_t i = 0; i < vs.size(); i++) { vs[i].layers[0].level = 0.7; bufs.emplace_back(eng_render_bound(vs[i], os[i]) + 1); if (bufs[i].size() < 2) { std::printf("rt_engine_u25 FAIL: bound 0\n"); return 1; } }
  size_t total = 0; int renders = 0;
  { rt_guard g;
    for (int rep = 0; rep < 3; rep++) for (size_t i = 0; i < vs.size(); i++) { size_t n = 0; const int st = eng_render_voice(vs[i], os[i], bufs[i].data(), bufs[i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
  }
done:
  if (!total) { std::printf("rt_engine_u25 FAIL: a render failed\n"); return 1; }
  // THE THREAD'S WORKSPACE. The guard above covers a thread that called eng_init() (so pe_prepare()). The workspace is per thread (thread_local): a thread that renders a Plaits / Elements layer without it
  // makes it on its first render (the Session, about 1.8 MB: a calloc and a malloc), which an audio callback must never do. Two fresh threads show both: A renders without preparing (pe_thread_ready() is false
  // before, true after; the first render allocates: counted outside the guard, glibc without ASan only), B calls pe_prepare() first and then renders under the guard, as the wiring of a live lane must.
  bool aBefore = true, aAfter = false, bReady = false, bOk = false; unsigned long aFirst = 0, aSecond = 0;
  { size_t nn = 0; std::vector<float> ba(bufs[0].size()), bb(bufs[0].size());
    std::thread A([&]() { aBefore = pe_thread_ready(); const unsigned long m0 = rt_mallocs; const int st = eng_render_voice(vs[0], os[0], ba.data(), ba.size() - 1, &nn); aFirst = rt_mallocs - m0; aAfter = pe_thread_ready() && st == kEngOk;
      const unsigned long m1 = rt_mallocs; eng_render_voice(vs[0], os[0], ba.data(), ba.size() - 1, &nn); aSecond = rt_mallocs - m1; });
    A.join();
    std::thread B([&]() { pe_prepare(); bReady = pe_thread_ready(); { rt_guard g; size_t n2 = 0; bOk = eng_render_voice(vs[0], os[0], bb.data(), bb.size() - 1, &n2) == kEngOk && eng_render_voice(vs[1], os[1], bufs[1].data(), bufs[1].size() - 1, &n2) == kEngOk; } });
    B.join(); }
#ifdef RT_MALLOC_GUARDED
  const bool allocSeen = aFirst > 0 && aSecond == 0;
#else
  const bool allocSeen = true;   // malloc is not counted here (ASan or a non-glibc libc): only the readiness flags are checked
#endif
  if (aBefore || !aAfter || !bReady || !bOk || !allocSeen) { std::printf("rt_engine_u25 FAIL: thread workspace (unprepared thread ready before %d, after %d; first render made %lu allocations, the second %lu; prepared thread ready %d, guarded renders ok %d)\n", aBefore, aAfter, aFirst, aSecond, bReady, bOk); return 1; }
  std::printf("rt_engine_u25 ok: %d voice renders (%zu samples) of Plaits / Elements layers under the allocation guard (the thread's workspace made beforehand); a second thread: unprepared -> its first render made %lu allocations (pe_thread_ready false then true), prepared with pe_prepare() -> guarded renders ok\n", renders, total, aFirst); return 0;
}


// U30: the …waves engine and its live runner under the guard. The patch (every kind of module a voice chain uses: oscillators, FM, additive, noise, Plaits, Elements, a string, filter, envelope, LFO, random, mixer, a
// sequencer, Expression, tfx effects, the modfx delay line and reverbs) is PREPARED outside it (every module object of every voice is made there); inside: notes on and off, parameter / text / transport
// changes, odd block sizes, patch swaps in the live runner, events posted. Nothing may allocate. (NOT here: Marbles, whose object is made again at its first block when its seed is drawn: U30-NOTES.md.)
static int run_waves() {
  using namespace thunder; using namespace thunder::waves;
  const char* pj = "{\"modules\":[{\"id\":\"h\",\"type\":\"hostIn\"},{\"id\":\"v\",\"type\":\"vco\",\"params\":{\"wave\":0}},{\"id\":\"f\",\"type\":\"fm\"},{\"id\":\"a\",\"type\":\"additive\",\"params\":{\"model\":5}},{\"id\":\"n\",\"type\":\"noise\",\"params\":{\"model\":6}},"
    "{\"id\":\"p\",\"type\":\"plaits\"},{\"id\":\"el\",\"type\":\"elements\"},{\"id\":\"k\",\"type\":\"string\"},{\"id\":\"x\",\"type\":\"mixer\"},{\"id\":\"vf\",\"type\":\"vcf\"},{\"id\":\"e\",\"type\":\"adsr\"},{\"id\":\"g\",\"type\":\"vca\"},{\"id\":\"l\",\"type\":\"lfo\"},"
    "{\"id\":\"r\",\"type\":\"random\"},{\"id\":\"q\",\"type\":\"seq\"},{\"id\":\"ex\",\"type\":\"expr\",\"text\":\"tanh(a*k1)+rnd*0.01\"},{\"id\":\"ch\",\"type\":\"chorus\"},{\"id\":\"rv\",\"type\":\"reverb\"},{\"id\":\"dl\",\"type\":\"dline\",\"params\":{\"time\":120}},"
    "{\"id\":\"sp\",\"type\":\"spring\"},{\"id\":\"cl\",\"type\":\"clouds\"},{\"id\":\"o\",\"type\":\"out\"}],\"cables\":["
    "{\"from\":[\"h\",\"pitch\"],\"to\":[\"v\",\"pitch\"]},{\"from\":[\"h\",\"pitch\"],\"to\":[\"f\",\"pitch\"]},{\"from\":[\"h\",\"pitch\"],\"to\":[\"a\",\"pitch\"]},{\"from\":[\"h\",\"gate\"],\"to\":[\"a\",\"gate\"]},{\"from\":[\"h\",\"pitch\"],\"to\":[\"n\",\"pitch\"]},"
    "{\"from\":[\"h\",\"pitch\"],\"to\":[\"p\",\"pitch\"]},{\"from\":[\"h\",\"gate\"],\"to\":[\"p\",\"gate\"]},{\"from\":[\"h\",\"pitch\"],\"to\":[\"el\",\"pitch\"]},{\"from\":[\"h\",\"gate\"],\"to\":[\"el\",\"gate\"]},{\"from\":[\"h\",\"pitch\"],\"to\":[\"k\",\"pitch\"]},{\"from\":[\"h\",\"gate\"],\"to\":[\"k\",\"gate\"]},"
    "{\"from\":[\"v\",\"out\"],\"to\":[\"x\",\"in1\"]},{\"from\":[\"f\",\"out\"],\"to\":[\"x\",\"in2\"]},{\"from\":[\"a\",\"out\"],\"to\":[\"x\",\"in3\"]},{\"from\":[\"n\",\"out\"],\"to\":[\"x\",\"in4\"]},{\"from\":[\"p\",\"out\"],\"to\":[\"x\",\"in5\"]},{\"from\":[\"el\",\"out\"],\"to\":[\"x\",\"in6\"]},{\"from\":[\"k\",\"out\"],\"to\":[\"x\",\"in7\"]},"
    "{\"from\":[\"x\",\"out\"],\"to\":[\"vf\",\"in\"]},{\"from\":[\"l\",\"out\"],\"to\":[\"vf\",\"cutoff\"]},{\"from\":[\"r\",\"out\"],\"to\":[\"vf\",\"p:res\"]},{\"from\":[\"h\",\"gate\"],\"to\":[\"e\",\"gate\"]},{\"from\":[\"vf\",\"out\"],\"to\":[\"g\",\"in\"]},{\"from\":[\"e\",\"out\"],\"to\":[\"g\",\"cv\"]},"
    "{\"from\":[\"g\",\"out\"],\"to\":[\"ex\",\"a\"]},{\"from\":[\"ex\",\"out\"],\"to\":[\"ch\",\"in\"]},{\"from\":[\"g\",\"out\"],\"to\":[\"dl\",\"in\"],\"fb\":true},{\"from\":[\"ch\",\"out\"],\"to\":[\"rv\",\"in\"]},{\"from\":[\"ch\",\"outR\"],\"to\":[\"rv\",\"inR\"]},{\"from\":[\"rv\",\"out\"],\"to\":[\"sp\",\"in\"]},"
    "{\"from\":[\"sp\",\"out\"],\"to\":[\"cl\",\"in\"]},{\"from\":[\"cl\",\"out\"],\"to\":[\"o\",\"in\"]},{\"from\":[\"cl\",\"outR\"],\"to\":[\"o\",\"inR\"]}]}";
  JsonDoc d; if (!d.parse(pj)) { std::printf("rt_waves FAIL: patch json\n"); return 1; }
  Host host; host.setFixed(.37); EngineOptions o; o.maxVoices = 4; o.transport.tempo = 120; o.transport.playing = true;
  WavesEngine e(48000, o, &host); if (!e.loadPatch(d.root())) { std::printf("rt_waves FAIL: the patch was refused\n"); return 1; }
  JsonDoc d2; d2.parse("{\"modules\":[{\"id\":\"h\",\"type\":\"hostIn\"},{\"id\":\"v\",\"type\":\"vco\"},{\"id\":\"o\",\"type\":\"out\"}],\"cables\":[{\"from\":[\"h\",\"pitch\"],\"to\":[\"v\",\"pitch\"]},{\"from\":[\"v\",\"out\"],\"to\":[\"o\",\"in\"]}]}");
  Host host2; host2.setFixed(.2); WavesLive live(48000, 4, &host2); live.preparePatch(d.root(), 0); live.preparePatch(d2.root(), 3000);
  Json dj = d.root(); (void)dj;
  const int kN = 4;   // the first live patch must also be applied: events are posted below
  long blocks = 0; float L[512], R[512]; const int sizes[8] = {128, 64, 17, 1, 100, 128, 3, 127};
  uint32_t sd = 5; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (double)(sd >> 8) / 16777216.0; };
  { rt_guard g;
    for (int b = 0; b < 600; b++) {
      if (b % 40 == 0) e.noteOn(-12 + (int)(rnd() * 24), .5 + rnd() * .5); if (b % 40 == 25) e.noteOff(-12); if (b == 300) e.allNotesOff();
      if (b % 17 == 0) { e.setParam("vf", "cutoff", rnd()); e.setParam("v", "wave", (double)(int)(rnd() * 4)); e.setParam("rv", "mix", rnd()); e.setParam("x", "l1", rnd() * 1.5); e.setParam("nope", "x", 1); }
      if (b % 90 == 0) e.setText("ex", b % 180 ? "sin(t*220*2*pi)*a" : "a * k1 + rnd * 0.1");
      if (b % 50 == 0) { Transport t; t.tempo = 90 + (b % 100); t.startN = b / 50; e.setTransport(t); }
      if (b % 111 == 0) e.setOnly(b % 222 ? 1 : 0);
      e.process(b % 9 == 0 ? 1 + b % 127 : 128); float l[128], r[128]; waves_mixdown(e, l, r, 128); blocks++;
    }
    // the live runner: events posted as the audio runs, host blocks of odd sizes, a patch swap
    for (int b = 0; b < 800; b++) {
      const int n = sizes[b % 8]; const int64_t now = live.enginePos();
      if (b % 31 == 0) { waves::LiveEvent ev; ev.kind = waves::LiveEvent::NoteOn; ev.lane = (uint8_t)(b % kN); ev.at = now + 200; ev.a = -10 + (int)(rnd() * 20); ev.b = .8; ev.legato = b % 62 == 0; live.post(ev); }
      if (b % 31 == 15) { waves::LiveEvent ev; ev.kind = waves::LiveEvent::NoteOff; ev.lane = (uint8_t)((b - 15) % kN); ev.at = now + 150; live.post(ev); }
      if (b % 19 == 0) { waves::LiveEvent ev; ev.kind = waves::LiveEvent::Pitch; ev.lane = 0; ev.at = now + 130; ev.a = rnd() * 4; live.post(ev); }
      if (b % 53 == 0) { waves::LiveEvent ev; ev.kind = b % 106 ? waves::LiveEvent::Level : waves::LiveEvent::Drive; ev.at = now + 140; ev.a = rnd(); live.post(ev); }
      live.process(L, R, n); blocks++;
    }
  }
  live.collectRetired();
  for (int i = 0; i < 128; i++) if (!std::isfinite(L[i])) { std::printf("rt_waves FAIL: non-finite output\n"); return 1; }
  std::printf("rt_waves ok: %ld blocks of the …waves engine and its live runner under the allocation guard (the patch prepared beforehand)\n", blocks); return 0;
}

// U31: the …chains engine, a track node and its live runner under the guard. The patch (Track In 1 and 2, VCF, Vocoder, Repeat, Resample, Sidechain Comp / In / Out, Morph, Osc, Noise, LFO, Expression, the stereo
// mixer, BYP on a module, and, where the libraries are in the build, tfx effects) is PREPARED outside the guard; inside: blocks of odd sizes, parameter / BYP / transport changes, inputs, Resample takes, patch swaps and
// events in the live runner. A second engine runs pump modules that GO BAD (hot input, every parameter at its maximum): the spare instance takes over inside the guard, the control thread (outside it) frees the dead
// one and makes the next spare (decision (e) of U18). Nothing inside may allocate.
static int run_chains() {
  using namespace thunder; using namespace thunder::chains;
  const bool fx = waves::Host::haveEffects();
  std::string pj = "{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"b\",\"type\":\"trackIn\",\"params\":{\"track\":2}},{\"id\":\"f\",\"type\":\"vcf\",\"params\":{\"cutoff\":0.5,\"_bn\":3}},{\"id\":\"v\",\"type\":\"vocoder\"},"
    "{\"id\":\"r\",\"type\":\"repeat\"},{\"id\":\"s\",\"type\":\"resample\",\"params\":{\"rec\":0,\"bars\":0}},{\"id\":\"q\",\"type\":\"sccomp\",\"params\":{\"sck\":9}},{\"id\":\"i\",\"type\":\"scin\",\"params\":{\"sck\":1}},{\"id\":\"o\",\"type\":\"scout\",\"params\":{\"snd\":1}},"
    "{\"id\":\"mo\",\"type\":\"morph\"},{\"id\":\"os\",\"type\":\"osc\"},{\"id\":\"wn\",\"type\":\"wnoise\"},{\"id\":\"l\",\"type\":\"lfo\"},{\"id\":\"ex\",\"type\":\"expr\",\"text\":\"tanh(a*k1)*0.5\"},{\"id\":\"mx\",\"type\":\"smixer\"},{\"id\":\"g\",\"type\":\"vca\"},"
    + std::string(fx ? "{\"id\":\"rv\",\"type\":\"reverb\"},{\"id\":\"ch\",\"type\":\"chorus\"}," : "") + "{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1}},{\"id\":\"d\",\"type\":\"chainOut\",\"params\":{\"track\":2}}],\"cables\":["
    "{\"from\":[\"a\",\"out\"],\"to\":[\"f\",\"in\"]},{\"from\":[\"i\",\"out\"],\"to\":[\"f\",\"p:cutoff\"]},{\"from\":[\"f\",\"out\"],\"to\":[\"v\",\"in\"]},{\"from\":[\"v\",\"out\"],\"to\":[\"r\",\"in\"]},{\"from\":[\"r\",\"out\"],\"to\":[\"s\",\"in\"]},"
    "{\"from\":[\"s\",\"out\"],\"to\":[\"q\",\"in\"]},{\"from\":[\"q\",\"out\"],\"to\":[\"mx\",\"in1\"]},{\"from\":[\"f\",\"out\"],\"to\":[\"mo\",\"in\"]},{\"from\":[\"os\",\"out\"],\"to\":[\"mo\",\"inB\"]},{\"from\":[\"mo\",\"out\"],\"to\":[\"mx\",\"in2\"]},"
    "{\"from\":[\"wn\",\"out\"],\"to\":[\"ex\",\"a\"]},{\"from\":[\"l\",\"out\"],\"to\":[\"ex\",\"c\"]},{\"from\":[\"ex\",\"out\"],\"to\":[\"mx\",\"in3\"]},{\"from\":[\"a\",\"out\"],\"to\":[\"o\",\"in\"]},{\"from\":[\"mx\",\"out\"],\"to\":[\"g\",\"in\"]},{\"from\":[\"l\",\"out\"],\"to\":[\"g\",\"cv\"]},{\"from\":[\"b\",\"out\"],\"to\":[\"d\",\"in\"]},"
    + std::string(fx ? "{\"from\":[\"g\",\"out\"],\"to\":[\"ch\",\"in\"]},{\"from\":[\"ch\",\"out\"],\"to\":[\"rv\",\"in\"]},{\"from\":[\"rv\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"rv\",\"outR\"],\"to\":[\"c\",\"inR\"]}" : "{\"from\":[\"g\",\"out\"],\"to\":[\"c\",\"in\"]}") + "]}";
  // Marbles, Tides and Cloud Seed (space), the modules that were made on the audio thread before U31's in-place storage: wired into the mixer so that they run (where their libraries are in the build)
  { std::string ex, ec; const bool mb = chains_have_lib("marbles"), td = chains_have_lib("tides"), sp = chains_have_lib("space");
    if (mb) { ex += ",{\"id\":\"mr\",\"type\":\"marbles\"}"; ec += ",{\"from\":[\"mr\",\"x1\"],\"to\":[\"mx\",\"in4\"]}"; }
    if (td) { ex += ",{\"id\":\"td\",\"type\":\"tides\"}"; ec += ",{\"from\":[\"td\",\"out1\"],\"to\":[\"mx\",\"in5\"]}"; }
    if (sp) { ex += ",{\"id\":\"sp\",\"type\":\"space\"}"; ec += ",{\"from\":[\"a\",\"out\"],\"to\":[\"sp\",\"in\"]},{\"from\":[\"sp\",\"out\"],\"to\":[\"mx\",\"in6\"]}"; }
    const size_t m0 = pj.find("],\"cables\":["); pj.insert(m0, ex); const size_t c0 = pj.rfind("]}"); pj.insert(c0, ec);
    std::printf("rt_chains: Marbles %d, Tides %d, space %d in the patch\n", (int)mb, (int)td, (int)sp); }
  JsonDoc d; if (!d.parse(pj)) { std::printf("rt_chains FAIL: patch json\n"); return 1; }
  const char* p2 = "{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\"},{\"id\":\"g\",\"type\":\"vca\"},{\"id\":\"c\",\"type\":\"chainOut\"}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"g\",\"in\"]},{\"from\":[\"g\",\"out\"],\"to\":[\"c\",\"in\"]}],\"transport\":{\"playing\":true}}";
  JsonDoc d2; d2.parse(p2);
  waves::Host host; host.setFixed(.37); EngineOptions o; o.focus = 0; o.transport.playing = true;
  ChainsEngine e(48000, o, &host); if (!e.loadPatch(d.root())) { std::printf("rt_chains FAIL: the patch was refused\n"); return 1; }
  e.setInlineService(false);
  std::vector<float> in(128 * 4), inR(128 * 4), tap(128), snd(8 * 128), mixL(128), mixR(128); uint32_t sd = 7; auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (double)(sd >> 8) / 16777216.0; };
  for (size_t i = 0; i < in.size(); i++) { in[i] = (float)(rnd() - .5) * .6f; inR[i] = (float)(rnd() - .5) * .6f; } for (float& x : tap) x = (float)(rnd() - .5) * .5f; for (float& x : snd) x = (float)(rnd() - .5) * .2f;
  const float* taps[2] = {tap.data(), tap.data()}; const float* sends[8]; for (int k = 0; k < 8; k++) sends[k] = &snd[(size_t)k * 128];
  long blocks = 0, takes = 0; const int sizes[8] = {128, 64, 17, 1, 100, 128, 3, 127};
  for (int b = 0; b < 700; b++) {
    { rt_guard g;
      e.setTrackInputs(in.data(), inR.data(), 4); e.setTaps(taps, 2); e.setSendIn(sends, 8); e.setSidechain(tap.data(), nullptr, 128);
      if (b % 13 == 0) { e.setParam("f", "cutoff", rnd()); e.setParam("l", "rate", rnd()); e.setParam("g", "gain", rnd()); e.setParam("nope", "x", 1); e.setParam("f", "nope", 1); }
      if (b % 50 == 3) e.setParam("f", "_bn", (double)(1 + b % 15)); if (b % 70 == 5) e.setParam("c", "track", 1 + b % 2); if (b % 31 == 0) e.setBypassSwitch(1 + b % 15, (double)(int)(rnd() * 4) - 1);
      if (b % 40 == 7) e.setParam("s", "rec", (double)((b / 40) & 1)); if (b % 90 == 0) e.setParam("s", "bars", (double)(b / 90 % 4));
      if (b % 60 == 0) { Transport t; t.tempo = 90 + (b % 100); t.playing = (b / 60) & 1 ? false : true; e.setTransport(t); }
      e.process(b % 9 == 0 ? 1 + b % 127 : 128); chains_mixdown(e, mixL.data(), mixR.data(), 128); blocks++;
      ResampleTake tk; while (e.popTake(&tk)) { takes++; e.releaseTake(tk); }
    }
    e.serviceSpares();
  }
  // a track node with every input, and the live runner with events and a patch swap
  { waves::Host h2; h2.setFixed(.2); ChainsLiveConfig cfg; cfg.taps = 2; cfg.sc = true; cfg.sends = true; cfg.feeds = 1; ChainsLive live(48000, 0, cfg, &h2);
    const std::vector<int> bm = {1}; live.node().engine().setBusMap(bm.data(), 1);
    if (!live.node().engine().loadPatch(d.root())) { std::printf("rt_chains FAIL: live patch\n"); return 1; }
    live.preparePatch(d2.root(), 4000); live.preparePatch(d.root(), 9000);
    std::vector<float> L(512), R(512), so(8 * 512), x(512), x2(512), x3(512); for (size_t i = 0; i < 512; i++) { x[i] = (float)(rnd() - .5) * .5f; x2[i] = x[i] * .5f; x3[i] = x[i] * .3f; }
    const float* tps[2] = {x2.data(), x3.data()}; const float* snds[8]; for (int k = 0; k < 8; k++) snds[k] = x3.data(); const float* fd[2] = {x2.data(), x3.data()}; float* sop[8]; for (int k = 0; k < 8; k++) sop[k] = &so[(size_t)k * 512];
    for (int b = 0; b < 800; b++) {
      const int n = sizes[b % 8]; const int64_t now = live.enginePos();
      { rt_guard g;
        if (b % 11 == 0) { live.postParam("f", "cutoff", rnd(), now + 150); live.postParam("g", "gain", rnd(), now + 90); live.postParam("zz", "x", 1, now + 10); }
        if (b % 29 == 0) { ChainsEvent ev; ev.kind = ChainsEvent::Bypass; ev.at = now + 130; ev.a = 1 + b % 15; ev.b = (double)(int)(rnd() * 3) - 1; live.post(ev); }
        if (b % 37 == 0) { ChainsEvent ev; ev.kind = ChainsEvent::Tempo; ev.at = now + 140; ev.a = 80 + b % 60; live.post(ev); }
        NodeIn ni; ni.inL = x.data(); ni.inR = x2.data(); ni.scL = x.data(); ni.taps = tps; ni.nTaps = 2; ni.sends = snds; ni.nSends = 8; ni.feeds = fd; ni.nFeeds = 1; NodeOut no; no.L = L.data(); no.R = R.data(); no.sends = sop; no.nSends = 8;
        live.process(ni, no, n); blocks++;
      }
      live.service();
    }
  }
  // pump modules that go bad: the spare takes over inside the guard; the control thread (outside) frees the dead instance and makes the next spare
  uint64_t swaps = 0, bad = 0;
  if (chains_have_lib("ptube") && chains_have_lib("pcrush") && chains_have_lib("pstutter")) {
    std::string mods, cabs; const char* T[3] = {"ptube", "pcrush", "pstutter"}; std::string ids = "";
    for (int i = 0; i < 3; i++) { const WInfo* wi = cinfo(T[i]); std::string ps; for (int k = 0; k < wi->np; k++) { char bb[200]; std::snprintf(bb, sizeof bb, "%s\"%s\":%.17g", k ? "," : "", wi->params[k].name, wi->params[k].mx); ps += bb; }
      mods += std::string(",{\"id\":\"p") + std::to_string(i) + "\",\"type\":\"" + T[i] + "\",\"params\":{" + ps + "}}";
      cabs += std::string(",{\"from\":[\"") + (i ? "p" + std::to_string(i - 1) : std::string("a")) + "\",\"out\"],\"to\":[\"p" + std::to_string(i) + "\",\"in\"]}"; }
    JsonDoc dp; dp.parse("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\"}" + mods + ",{\"id\":\"c\",\"type\":\"chainOut\"}],\"cables\":[" + cabs.substr(1) + ",{\"from\":[\"p2\",\"out\"],\"to\":[\"c\",\"in\"]}]}");
    waves::Host h3; ChainsEngine ep(48000, o, &h3); if (!ep.loadPatch(dp.root())) { std::printf("rt_chains FAIL: pump patch\n"); return 1; } ep.setInlineService(false);
    std::vector<float> hot(128 * 300); for (float& x : hot) x = (float)(rnd() - .5) * 15.8f; const uint64_t b0 = chains_pump_bad(), s0 = chains_pump_swaps();
    for (int b = 0; b < 300; b++) { { rt_guard g; ep.setTrackInputs(&hot[(size_t)b * 128], &hot[(size_t)b * 128], 1); ep.process(128); blocks++; } ep.serviceSpares(); }
    bad = chains_pump_bad() - b0; swaps = chains_pump_swaps() - s0;
    if (bad == 0) { std::printf("rt_chains FAIL: no pump module went bad: the spare path was not exercised\n"); return 1; }
  }
  for (int i = 0; i < 128; i++) if (!std::isfinite(mixL[(size_t)i])) { std::printf("rt_chains FAIL: non-finite output\n"); return 1; }
  std::printf("rt_chains ok: %ld blocks of the …chains engine, a track node and its live runner under the allocation guard (%ld Resample takes; %llu pump blocks went bad, %llu spares swapped in)\n", blocks, takes, (unsigned long long)bad, (unsigned long long)swaps); return 0;
}
#ifdef THUNDER_HAVE_LXR
// U04: the LXR module under the guard: the four kinds rendered as pad voices through the core's seam (xeng_render_voice) and the live path (live_open + live_process blocks with triggers),
// repeatedly. The module owns its buffers (static: the firmware is global state anyway); a render into the caller's buffer and a live block allocate nothing.
static int run_lxr() {
  using namespace thunder;
  if (thunder_lxr_module_register() != 0) { std::printf("rt_lxr FAIL: the module did not register\n"); return 1; }
  static const char* const kinds[4] = {"lxdrum", "lxsnare", "lxcym", "lxhat"};
  XengVoice vs[4]; EngRenderOpts o; std::vector<std::vector<float>> bufs;
  const ThunderLxrApi* a = thunder_lxr_api();
  for (int i = 0; i < 4; i++) {
    XengVoice& V = vs[i]; V.kind = xeng_kind(kinds[i], std::strlen(kinds[i])); V.level = 0; V.len = 0.4;
    std::memset(&V.lxr, 0, sizeof V.lxr); V.lxr.kind = i; V.lxr.vel = 110; V.lxr.sr = 48000; V.lxr.bpm = 120; V.lxr.vlen = 0.4; V.lxr.drive = 0.3;
    ThunderLxrDescribe d; std::memset(&d, 0, sizeof d); a->describe(i, &d); for (int k = 0; k < THUNDER_LXR_MAX_PARAMS; k++) if (d.present[k]) V.lxr.p[k] = d.def[k];
    bufs.emplace_back(xeng_render_bound(V, o) + 1); if (bufs[(size_t)i].size() < 2) { std::printf("rt_lxr FAIL: bound 0 (%s)\n", kinds[i]); return 1; }
  }
  std::vector<uint8_t> kit(247, 64);   /* NumParams */ std::vector<float> blk(777 + 1); ThunderLxrEvent ev[3] = {{0, 0, 100, 60}, {130, 3, 90, 62}, {400, 5, 127, 70}};
  for (int i = 0; i < 4; i++) { size_t n = 0; if (xeng_render_voice(vs[i], o, bufs[(size_t)i].data(), bufs[(size_t)i].size() - 1, &n) != kEngOk) { std::printf("rt_lxr FAIL: warm-up %d\n", i); return 1; } }   // takes the power-on snapshot
  if (a->live_open(kit.data(), kit.size(), 120) != THUNDER_LXR_OK) { std::printf("rt_lxr FAIL: live_open\n"); return 1; }
  size_t total = 0; int renders = 0;
  { rt_guard g;
    for (int rep = 0; rep < 3; rep++) {
      for (int i = 0; i < 4; i++) { size_t n = 0; const int st = xeng_render_voice(vs[i], o, bufs[(size_t)i].data(), bufs[(size_t)i].size() - 1, &n); if (st != kEngOk) { total = 0; goto done; } total += n; renders++; }
      if (a->live_open(kit.data(), kit.size(), 120) != THUNDER_LXR_OK) { total = 0; goto done; }
      for (int v = 0; v < 6; v++) { if (a->live_process(v, ev, 3, blk.data(), 777) != THUNDER_LXR_OK) { total = 0; goto done; } total += 777; renders++; }
    }
  }
done:
  if (!total) { std::printf("rt_lxr FAIL: a render failed\n"); return 1; }
  thunder_lxr_unregister();
  std::printf("rt_lxr ok: %d renders (%zu samples) of the four LXR kinds, pad and live, under the allocation guard\n", renders, total); return 0;
}
#endif

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selftest();
  if (int r = run_logger()) return r;
  if (int r = run_core()) return r;
  if (int r = run_seq()) return r;
  if (int r = run_seq_chop()) return r;
  if (int r = run_seq_runner()) return r;
  if (int r = run_seq_takeover()) return r;
  if (int r = run_voice()) return r;
  if (int r = run_sf()) return r;
  if (int r = run_mix()) return r;
  if (int r = run_master()) return r;
  if (int r = run_chain()) return r;
  if (int r = run_fx()) return r;
  if (int r = run_fxclk()) return r;
  if (int r = run_pump()) return r;
  if (int r = run_fxidm()) return r;
  if (int r = run_modfx()) return r;
  if (int r = run_fxu17()) return r;
  if (int r = run_engine_u23()) return r;
  if (int r = run_engine()) return r;
  if (int r = run_engine_xeng()) return r;
#ifdef THUNDER_HAVE_LXR
  if (int r = run_lxr()) return r;
#endif
  if (int r = run_engine_xeng2()) return r;
  if (int r = run_engine_xeng3()) return r;
  if (int r = run_pumpsynth()) return r;
  if (int r = run_engine_u24()) return r;
  if (int r = run_engine_u25()) return r;
  if (int r = run_engine_u26()) return r;
  if (int r = run_waves()) return r;
  if (int r = run_chains()) return r;
  if (int r = run_live()) return r;
  if (int r = run_live_eng()) return r;
  if (int r = run_pads_snap()) return r;
  if (int r = run_live_host()) return r;
  return run_song();
}
