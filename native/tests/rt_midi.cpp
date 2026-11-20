// U35: the rt_harness proof for MIDI scheduling: the audio-thread entry points (SeqRunner::pump -> MidiSched::onStep / onEvent, book, the C API's
// thunder_midi_process with its command ring and the live keys) allocate nothing, free nothing and take no lock, over 60 s of a loud song with tempo changes,
// a stall, STOP / PLAY, cable and clock-port edits, panic, live keys and raw messages posted from the control side between blocks.
// The guard is rt_harness.cpp's (operator new / delete and glibc malloc / free abort inside the guarded region), plus the lock calls (pthread_mutex_lock,
// pthread_cond_wait, pthread_rwlock_*), and a source scan for the tokens that would take a lock or do I/O. `rt_midi --selftest`: a child that locks inside the guard must die.
#include <dlfcn.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "midi_drive.h"
#include "thunder/midi.h"

static thread_local int rt_depth = 0;
static void rt_violation(const char* what) { rt_depth = 0; std::fputs("rt_midi: ", stderr); std::fputs(what, stderr); std::fputs(" inside the audio callback\n", stderr); std::abort(); }
struct rt_guard { rt_guard() { rt_depth++; } ~rt_guard() { rt_depth--; } };
void* operator new(std::size_t n) { if (rt_depth) rt_violation("operator new"); void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new[](std::size_t n) { if (rt_depth) rt_violation("operator new[]"); void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (rt_depth) rt_violation("operator new (nothrow)"); return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { if (rt_depth && p) rt_violation("operator delete"); std::free(p); }
void operator delete[](void* p) noexcept { if (rt_depth && p) rt_violation("operator delete[]"); std::free(p); }
void operator delete(void* p, std::size_t) noexcept { if (rt_depth && p) rt_violation("operator delete"); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { if (rt_depth && p) rt_violation("operator delete[]"); std::free(p); }
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
extern "C" {
void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (rt_depth) rt_violation("malloc"); return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (rt_depth) rt_violation("calloc"); return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (rt_depth) rt_violation("realloc"); return __libc_realloc(p, n); }
void free(void* p) { if (rt_depth && p) rt_violation("free"); __libc_free(p); }
}
#define RT_MALLOC_GUARDED 1
#endif
// locks: interposed (the real one is found once, outside the guard, by dlsym)
using LockFn = int (*)(pthread_mutex_t*);
static LockFn real_lock = nullptr;
extern "C" int pthread_mutex_lock(pthread_mutex_t* m) {
  if (rt_depth) rt_violation("pthread_mutex_lock");
  if (!real_lock) real_lock = (LockFn)dlsym(RTLD_NEXT, "pthread_mutex_lock");
  return real_lock ? real_lock(m) : 0;
}
using CondFn = int (*)(pthread_cond_t*, pthread_mutex_t*);
static CondFn real_cond = nullptr;
extern "C" int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
  if (rt_depth) rt_violation("pthread_cond_wait");
  if (!real_cond) real_cond = (CondFn)dlsym(RTLD_NEXT, "pthread_cond_wait");
  return real_cond(c, m);
}

using namespace mdrive;
static int fail(const char* m) { std::fprintf(stderr, "rt_midi FAIL: %s\n", m); return 1; }

static int selftest() {
  const char* what[] = {"new", "lock"};
  for (int k = 0; k < 2; k++) {
    const pid_t pid = fork();
    if (pid == 0) { if (!std::freopen("/dev/null", "w", stderr)) _exit(2); pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER; rt_guard g; if (k == 0) { void* p = ::operator new(16); (void)p; } else pthread_mutex_lock(&m); _exit(0); }
    int st = 0; waitpid(pid, &st, 0);
    if (!(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT)) return fail("the guard did not stop a child that allocates / locks");
    std::printf("selftest: a %s inside the guard is caught\n", what[k]);
  }
  return 0;
}

static int scanSource() {
  const char* files[] = {"/native/core/src/midi/midisched.cpp", "/native/core/src/midi/midisched.h", "/native/core/src/midi/midi_api.cpp"};
  const char* bad[] = {"mutex", "lock_guard", "unique_lock", "condition_variable", "pthread_", "sem_wait", "std::thread", "std::cout", "std::cerr", "printf(", "fopen", "std::vector", "std::string", "std::map", "std::function", "push_back", "malloc(", "std::make_"};
  int found = 0;
  for (const char* f : files) {
    std::string text; if (!readFile(std::string(THUNDER_ROOT) + f, text)) return fail("cannot read a MIDI source");
    std::istringstream in(text); std::string line; int ln = 0; const bool api = std::strstr(f, "midi_api") != nullptr;
    while (std::getline(in, line)) {
      ln++; const size_t c = line.find("//"); const std::string code = c == std::string::npos ? line : line.substr(0, c);
      for (const char* b : bad) {
        if (code.find(b) == std::string::npos) continue;
        if (api && (std::strcmp(b, "printf(") == 0)) continue;                 // setErr's snprintf: the control thread's create()
        std::fprintf(stderr, "rt_midi: %s:%d uses %s: %s\n", f, ln, b, line.c_str()); found++;
      }
    }
  }
  return found ? 1 : 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--selftest") == 0) return selftest();
  if (scanSource()) return fail("a MIDI source mentions a lock, a container or I/O");
  Case C; if (!loadCase(THUNDER_ROOT, "loop", C)) return fail("no case");
  Drive D; JsonError je; if (!D.song.load(C.songText.data(), C.songText.size(), &je)) return fail("song");
  for (int i = 0; i < 16; i++) { D.song.insts[i].mo = 1; D.song.insts[i].mch = i % 4; D.song.insts[i].eco = i % 3; D.song.insts[i].lcc = i % 2 ? 74 : -1; D.song.insts[i].lrg = i % 2; }
  thunder_midi* h = thunder_midi_create(48000, nullptr, 0); if (!h) return fail("create");
  const std::string songText = C.songText;
  thunder_midi_set_song(h, (const uint8_t*)songText.data(), songText.size());
  for (int p = 0; p < 4; p++) thunder_midi_set_port(h, p, 1, p < 2, p * 3.0, p);
  thunder_midi_set_fx_latency(h, 0.0716);
  // the direct (C++) path: a scheduler of its own, fed by the runner, under the guard; and the C handle's process() with posted commands
  thunder::MidiSched s(48000); configure(s, C, D.song);
  thunder::SeqOpts o; o.live = true; o.hasLoop = true; o.loopY0 = 0; o.loopY1 = 0; thunder::Seq sq(D.song, D.vars, thunder::SeqMode::Song, o);
  thunder::SeqRunner run; run.start(&sq); sq.setTime(0.1); s.start(0, 0);
  std::vector<thunder::SeqEvent> evs(12288); thunder::SeqStep st; std::vector<thunder::MidiMsg> out(4096); std::vector<thunder_midi_msg> cout(4096);
  long msgs = 0, steps = 0; double now = 0; int keys = 0;
  for (int b = 0; b < 60 * 48000 / 128; b++) {
    // the control side (outside the guard): the posts
    if (b % 200 == 50) { thunder_midi_key_on(h, b % 100, 0, 48 + b % 12, 100); thunder_midi_key_off(h, (b + 50) % 100); }
    if (b == 1000) thunder_midi_start(h, 0);
    if (b == 3000) thunder_midi_set_cable(h, 1, 1, 0);
    if (b == 3500) thunder_midi_port_clock(h, 0, 0);
    if (b == 4000) thunder_midi_port_clock(h, 0, 1);
    if (b == 5000) thunder_midi_panic(h);
    if (b == 6000) thunder_midi_stop(h);
    if (b == 6100) thunder_midi_start(h, 0);
    if (b == 7000) { const uint8_t r[3] = {0xB0, 7, 99}; thunder_midi_raw(h, 0, r, 3, 0.01); }
    if (b % 1500 == 700) thunder_midi_set_song(h, (const uint8_t*)songText.data(), songText.size());
    if (b == 2000) sq.setBpm(133);
    if (b == 4500) now += 0.4;                                         // a stalled host
    if (b == 8000) { thunder_midi_release(h, 1); }
    {
      rt_guard g;
      run.pump(now, 0.25, evs.data(), (int)evs.size(), &st, [&](const thunder::SeqEvent* e, int n, const thunder::SeqStep& step) { s.onStep(e, n, step, sq, now); steps++; });
      s.book(now, sq.time(), sq.bpm(), sq.alive());
      if (b == 1500) s.panic(now);
      if (b == 2500) s.stop(now);
      if (b == 2600) s.start(0, now);
      if (b == 2700) { s.setPort(0, true, true, 5, 0); s.portToggled(1, true, now); s.cableChanged(2, true, 0, now); }
      if (b % 97 == 0) { const int k = s.liveNoteOn(0, 40 + b % 20, 100, 16, now); if (k >= 0) { keys++; s.liveNoteOff(k, now + 0.05); } }
      if (thunder_midi_process(h, now, now + 0.1, 120, 1) < 0) return fail("process");
    }
    msgs += s.drain(out.data(), (int64_t)out.size()); const int64_t n = thunder_midi_drain(h, cout.data(), (int64_t)cout.size()); if (n < 0) return fail("drain"); msgs += n;
    now += 128.0 / 48000;
  }
  if (steps < 200 || msgs < 1000 || keys < 10) return fail("the run did too little");
  thunder_midi_stats ts; ts.size = sizeof ts; thunder_midi_stats_get(h, &ts);
  std::printf("rt_midi: %ld steps, %ld messages (%llu late ticks, %llu pending max), no allocation, no lock; %s\n", steps, msgs, (unsigned long long)s.stats().lateTicks, (unsigned long long)s.stats().maxPend,
#ifdef RT_MALLOC_GUARDED
              "malloc guarded"
#else
              "operator new guarded only (sanitizer build)"
#endif
  );
  thunder_midi_free(h);
  return 0;
}
