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
#include "song/song.h"
#include "mix/mix.h"
#include "mix/pdc.h"
#include "master/master.h"
#include "render/render.h"
#include "voice/voice.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
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

void* operator new(std::size_t n) { if (rt_depth) rt_violation("operator new"); void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new[](std::size_t n) { if (rt_depth) rt_violation("operator new[]"); void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (rt_depth) rt_violation("operator new (nothrow)"); return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { if (rt_depth) rt_violation("operator new[] (nothrow)"); return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { if (rt_depth && p) rt_violation("operator delete"); std::free(p); }
void operator delete[](void* p) noexcept { if (rt_depth && p) rt_violation("operator delete[]"); std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (rt_depth && p) rt_violation("operator delete"); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (rt_depth && p) rt_violation("operator delete[]"); std::free(p); }

// malloc / free themselves (C code, std::vector's allocator on some platforms): glibc lets us wrap them.
#if defined(__GLIBC__)
extern "C" {
void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (rt_depth) rt_violation("malloc"); return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (rt_depth) rt_violation("calloc"); return __libc_calloc(a, b); }
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

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selftest();
  if (int r = run_logger()) return r;
  if (int r = run_core()) return r;
  if (int r = run_seq()) return r;
  if (int r = run_voice()) return r;
  if (int r = run_mix()) return r;
  if (int r = run_master()) return r;
  return run_song();
}
