// A5: THE REAL-TIME TEST HARNESS. Runs the core's audio callback offline under an allocator that aborts when anything is
// allocated or freed while the callback is running, and checks the clock-drift logger (thunder/rtlog.h) against a made-up clock.
//   rt_harness            the test core: 60 s of blocks of mixed sizes, commands arriving meanwhile, no allocation -> exit 0
//   rt_harness --selftest proves the guard works: a child process allocates inside the guarded region and MUST die
//                         (SIGABRT); also the same with malloc and with free. Exit 0 only if every child was killed.
// The gate (WORKPLAN, N0 A5): fails on a deliberate `new` in the callback; passes on the test core.
// Every port unit from N1 on runs its process() through rt_guard the same way.
#include "thunder/core.h"
#include "thunder/rtlog.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
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

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selftest();
  if (int r = run_logger()) return r;
  return run_core();
}
