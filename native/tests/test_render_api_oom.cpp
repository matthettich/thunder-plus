// U34a verification fixes, the memory ones (docs/port/reviews/verify-237.md D4 to D7). This program REPLACES the global operator new / delete to count
// live bytes, to fail the k-th allocation, and (through thunder_fx_fail_at, core/dsp/fx_alloc.cpp) to make the k-th malloc of the effects return null.
//   D5  a failed create frees everything it made: an allocation failure at EVERY allocation index of create (every stride-th for the long cases),
//       live blocks and (glibc, no sanitizer) the heap back to where they started, and "out of memory" in err
//   D7  a null malloc inside an effect (the tfx Fx::operator new) is E_MEMORY too, not a crash
//   D6  an allocation failure inside read is E_MEMORY and the handle is then dead (only free; the getters still answer), nothing leaks
//   D4  the pad budget is a bound on allocation (a replacement is counted on top of the pad it replaces), and a song / vars at their limits make a
//       handle within the sizes the header states
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>
#if defined(__GLIBC__)
#include <execinfo.h>
#include <malloc.h>
#endif
#include "render_api_common.h"

#ifdef THUNDER_HAVE_TFX
extern "C" void thunder_fx_fail_at(long k);   // core/dsp/fx_alloc.cpp (the effects' malloc)
#else
static void thunder_fx_fail_at(long) {}       // (a build without the DSP libraries has no effects to fail)
#endif

#if defined(__SANITIZE_ADDRESS__)
#define TR_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer)
#define TR_SANITIZED 1
#endif
#endif

namespace {
bool g_armed = false;                 // counting / failing only while armed (the test's own allocations stay out of it)
long g_calls = 0, g_failAt = 0;       // the g_failAt-th operator new while armed throws (0: none)
bool g_failed = false;
int64_t g_live = 0, g_peak = 0;       // live bytes through operator new (all the time, so a delete of an old block balances)
constexpr size_t kHdr = 16;
// a debugging aid (TR_LEAK_TRACE=1, glibc): where each block made while armed came from, so that a leak report can name its allocator
struct Trace { void* p; void* bt[10]; int n; };
Trace g_tr[1 << 16]; bool g_trace = false;
void traceAdd(void* p) {
#if defined(__GLIBC__)
  if (!g_trace) return;
  size_t h = ((size_t)p >> 4) & 0xffff; for (int i = 0; i < 4096; i++, h = (h + 1) & 0xffff) if (!g_tr[h].p || g_tr[h].p == (void*)1) { g_tr[h].p = p; g_tr[h].n = backtrace(g_tr[h].bt, 10); return; }
#else
  (void)p;
#endif
}
void traceDel(void* p) { if (!g_trace) return; size_t h = ((size_t)p >> 4) & 0xffff; for (int i = 0; i < 4096; i++, h = (h + 1) & 0xffff) { if (g_tr[h].p == p) { g_tr[h].p = (void*)1; return; } if (!g_tr[h].p) return; } }
void traceDump() {
#if defined(__GLIBC__)
  for (Trace& t : g_tr) if (t.p && t.p != (void*)1) { std::printf("  leaked block %p, %zu bytes:\n", (char*)t.p + kHdr, *(size_t*)t.p); fflush(stdout); backtrace_symbols_fd(t.bt, t.n, 1); t.p = nullptr; }
#endif
}
}
void* operator new(size_t n) {
  if (g_armed) { ++g_calls; if (g_failAt > 0 && g_calls == g_failAt) { g_failAt = 0; g_failed = true;
#if defined(__GLIBC__)
    if (g_trace && std::getenv("TR_FAIL_SITE")) { void* bt[10]; int n = backtrace(bt, 10); std::printf("  failing allocation %ld:\n", g_calls); fflush(stdout); backtrace_symbols_fd(bt, n, 1); }
#endif
    throw std::bad_alloc(); } }
  void* p = std::malloc(n + kHdr); if (!p) throw std::bad_alloc();
  *(size_t*)p = n; g_live += (int64_t)n; if (g_live > g_peak) g_peak = g_live;
  if (g_armed) traceAdd(p);
  return (char*)p + kHdr;
}
void operator delete(void* q) noexcept { if (!q) return; char* p = (char*)q - kHdr; g_live -= (int64_t)*(size_t*)p; std::free(p); }
void operator delete(void* q, size_t) noexcept { operator delete(q); }
void* operator new[](size_t n) { return operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { try { return operator new(n); } catch (...) { return nullptr; } }   // (a sanitizer brings its own nothrow versions: they must match the delete above)
void* operator new[](size_t n, const std::nothrow_t&) noexcept { try { return operator new(n); } catch (...) { return nullptr; } }
void operator delete(void* q, const std::nothrow_t&) noexcept { operator delete(q); }
void operator delete[](void* q, const std::nothrow_t&) noexcept { operator delete(q); }
void operator delete[](void* q) noexcept { operator delete(q); }
void operator delete[](void* q, size_t) noexcept { operator delete(q); }

using namespace u34;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)

static int64_t heapInUse() {
#if defined(__GLIBC__) && defined(M_MXFAST) && !defined(TR_SANITIZED) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 33)
  return (int64_t)mallinfo2().uordblks + (int64_t)mallinfo2().hblkhd;   // small blocks + mmapped ones: every malloc in use, ours and the effects'
#else
  return 0;
#endif
}
static bool haveHeap() { return heapInUse() != 0; }

static thunder_render* mk(const std::string& song, const thunder_render_opts& o, char* err = nullptr, size_t cap = 0) { return thunder_render_create((const uint8_t*)song.data(), song.size(), &o, err, cap); }
static thunder_render_opts opts(double sr, bool fx, bool master, int pdc = 0, bool useFrames = false, int64_t frames = 0) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.sample_rate = sr; o.effects = fx; o.master_chain = master; o.pdc_mode = pdc; o.use_frames = useFrames; o.frames = frames; return o;
}

// D5 / D7: a failure at each allocation index (via operator new, or via the effects' malloc) of create: NULL, "out of memory", nothing left behind.
static void createFailures(const char* name, const std::string& song, const thunder_render_opts& o, bool viaFxMalloc, long stride) {
  char err[200];
  { thunder_render* w = mk(song, o); CHECK("warm-up create", w != nullptr); thunder_render_free(w); }   // lazy statics (tables) are made once: not leaks
  int64_t live0 = g_live, heap0 = heapInUse();
  long tested = 0, leaks = 0, ok = 0, total = -1; int64_t worst = 0;
  for (long k = 1;; k += stride) {
    g_calls = 0; g_failed = false; g_failAt = viaFxMalloc ? 0 : k; g_armed = true; if (viaFxMalloc) thunder_fx_fail_at(k);
    thunder_render* r = mk(song, o, err, sizeof err);
    g_armed = false; g_failAt = 0; thunder_fx_fail_at(0);
    if (r) { thunder_render_free(r); total = k; break; }     // k is past the last allocation of a create: it succeeded
    tested++;
    if (std::strstr(err, "out of memory") == nullptr) { std::printf("  k=%ld: err = \"%s\"\n", k, err); CHECK("err says out of memory", false); }
    const int64_t dl = g_live - live0, dh = haveHeap() ? heapInUse() - heap0 : 0;
    if (dl != 0 || dh > 4096) { leaks++; worst = std::max(worst, std::max(dl, dh)); if (leaks <= 5) { std::printf("  LEAK at k=%ld: %lld live bytes, heap +%lld\n", k, (long long)dl, (long long)dh); if (g_trace) traceDump(); } }
    else ok++;
    if (g_trace) for (Trace& t : g_tr) t.p = nullptr;
    live0 = g_live; heap0 = heapInUse();   // (a leak is counted once, where it happens)
    if (k > 200000) { CHECK("create finishes", false); break; }
  }
  std::printf("%s (%s): %ld failures injected (create makes about %ld allocations), %ld leaked (worst %lld bytes)\n", name, viaFxMalloc ? "fx malloc" : "operator new", tested, total, leaks, (long long)worst);
  CHECK("no failed create leaks", leaks == 0);
  CHECK("failures were injected", tested > 0 && ok + leaks == tested);
}

// D6: an allocation failure inside read is E_MEMORY (the effects make no effect inside a read: no Fx is built there, so no effect malloc happens in it) and the handle is then dead; nothing leaks
static void readFailures(const char* name, const std::string& song, const thunder_render_opts& o, int nProbe) {
  const PadSet ps; std::vector<float> a(4096), b(4096);
  long total = 0;   // how many allocations a whole read makes
  { thunder_render* r = mk(song, o); installPads(r, ps); g_calls = 0; g_failAt = 0; g_armed = true; while (thunder_render_read(r, a.data(), b.data(), 4096) > 0) {} g_armed = false; total = g_calls; thunder_render_free(r); }
  std::printf("%s: a whole read makes %ld allocations\n", name, total);
  CHECK("a read allocates (the effects make their plans)", total > 0);
  int dead = 0, survived = 0, deadFx = 0; (void)deadFx;
  const int64_t live0 = g_live, heap0 = heapInUse();
  for (int i = 0; i < nProbe && total > 0; i++) {
    const bool viaFx = false; const int j = i;
    const long k = 1 + (long)((int64_t)j * total / nProbe);
    thunder_render* r = mk(song, o); installPads(r, ps);
    g_calls = 0; g_failed = false; g_failAt = viaFx ? 0 : k; g_armed = true; if (viaFx) thunder_fx_fail_at(k);
    int64_t n = 0; while ((n = thunder_render_read(r, a.data(), b.data(), 4096)) > 0) {}
    g_armed = false; g_failAt = 0; thunder_fx_fail_at(0);
    if (n >= 0) { survived++; thunder_render_free(r); continue; }   // the failure came after the last allocation of the read (or was never reached)
    char msg[120]; std::snprintf(msg, sizeof msg, "%s k=%ld (%s): read returns E_MEMORY (got %lld)", name, k, viaFx ? "fx malloc" : "operator new", (long long)n);
    CHECK(msg, n == THUNDER_RENDER_E_MEMORY);
    CHECK("then read is E_STATE", thunder_render_read(r, a.data(), b.data(), 10) == THUNDER_RENDER_E_STATE);
    CHECK("then set_pad is E_STATE", thunder_render_set_pad(r, 0, ps.pad(0), nullptr, kNS, 48000) == THUNDER_RENDER_E_STATE);
    CHECK("then set_pad_kit is E_STATE", thunder_render_set_pad_kit(r, 0, 0, 0, 0) == THUNDER_RENDER_E_STATE);
    CHECK("then drain is E_STATE", thunder_render_drain(r, 1000) == THUNDER_RENDER_E_STATE);
    thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; CHECK("the getters still answer", thunder_render_stats_get(r, &s) == 0 && thunder_render_position(r) >= 0);
    thunder_render_free(r); (viaFx ? deadFx : dead)++;
    CHECK("free of a dead handle leaves nothing behind", g_live == live0 && (!haveHeap() || heapInUse() - heap0 <= 4096));
  }
  std::printf("%s: %d of %d probes hit the failure: a dead handle each; %d ran past it\n", name, dead, nProbe, survived);
  CHECK("failures were injected inside read (operator new)", dead > 0);
}

// a failed set_pad (allocation) leaves the handle as it was
static void padFailure() {
  const std::string song = shortSong(); const PadSet ps; std::vector<float> a, b, a2, b2;
  auto all = [&](thunder_render* r, std::vector<float>* L, std::vector<float>* R) { std::vector<float> x(4096), y(4096); int64_t n; while ((n = thunder_render_read(r, x.data(), y.data(), 4096)) > 0) { L->insert(L->end(), x.begin(), x.begin() + n); R->insert(R->end(), y.begin(), y.begin() + n); } };
  { thunder_render* r = makeApi(song, ps, Cfg(), false, 0); all(r, &a, &b); thunder_render_free(r); }
  thunder_render* r = makeApi(song, ps, Cfg(), false, 0);
  g_calls = 0; g_failAt = 1; g_failed = false; g_armed = true; const int rc = thunder_render_set_pad(r, 5, ps.pad(5), nullptr, kNS, 48000); g_armed = false; g_failAt = 0;
  CHECK("set_pad: E_MEMORY", rc == THUNDER_RENDER_E_MEMORY && g_failed);
  CHECK("set_pad works again after it", thunder_render_set_pad(r, 5, ps.pad(5), nullptr, kNS, 48000) == 0);
  all(r, &a2, &b2); thunder_render_free(r);
  CHECK("the render is the clean one", a.size() == a2.size() && std::memcmp(a.data(), a2.data(), a.size() * 4) == 0 && std::memcmp(b.data(), b2.data(), b.size() * 4) == 0);
}

// D4: the pad budget bounds the allocation (a big block may carry a few bytes of allocator overhead, MSVC's aligned vector blocks: compare with a tolerance)
static bool nearBytes(int64_t a, int64_t b) { return a - b < 4096 && b - a < 4096; }
static void padBudget() {
  const size_t n = (size_t)1 << 26;   // 256 MiB of mono pad
  float* host = (float*)std::calloc(n, 4); CHECK("host buffer", host != nullptr); if (!host) return;
  const std::string song = shortSong(); thunder_render* r = mk(song, opts(8000, false, false));
  const int64_t base = g_live; g_peak = g_live;
  CHECK("pad 0, 256 MiB", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n, 48000) == 0);
  CHECK("pad 1, 256 MiB (512 MiB in all: the limit)", thunder_render_set_pad(r, 1, host, nullptr, (int64_t)n, 48000) == 0);
  CHECK("one more frame is over the limit", thunder_render_set_pad(r, 2, host, nullptr, 1, 48000) == THUNDER_RENDER_E_LIMIT);
  const int64_t atLimit = g_live - base;
  CHECK("a replacement whose final state is over the limit is refused (the pad stays as it was)", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n + 1, 48000) == THUNDER_RENDER_E_LIMIT && g_live - base == atLimit);
  CHECK("replacing a pad at the limit with one of the same size works (the old copy is released first)", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n, 48000) == 0 && g_live - base == atLimit);
  host[5] = NAN; CHECK("a non-finite sample in a replacement is E_ARG and the pad stays", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n, 48000) == THUNDER_RENDER_E_ARG && g_live - base == atLimit); host[5] = 0;
  CHECK("replacing with a smaller one returns the difference", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n / 2, 48000) == 0 && g_live - base == atLimit - (int64_t)(n * 2));
  CHECK("then the room is there again", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n, 48000) == 0 && g_live - base == atLimit);
  { g_calls = 0; g_failAt = 1; g_failed = false; g_armed = true; const int rc = thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n, 48000); g_armed = false; g_failAt = 0;
    CHECK("an allocation failure in a limit-size replacement: E_MEMORY, and the pad (released first) is gone, as render.h says", rc == THUNDER_RENDER_E_MEMORY && g_failed && nearBytes(g_live - base, atLimit - (int64_t)(n * 4)));
    CHECK("the handle works on: the pad can be set again", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n, 48000) == 0 && g_live - base == atLimit); }
  CHECK("removing a pad frees it", thunder_render_set_pad(r, 0, nullptr, nullptr, 0, 0) == 0 && nearBytes(g_live - base, atLimit - (int64_t)(n * 4)));
  CHECK("then it can be set again", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n, 48000) == 0);
  const double peakMiB = (double)(g_peak - base) / 1048576.0;
  std::printf("pad budget: peak %.1f MiB of handle memory above the empty handle for pads (limit 512 MiB)\n", peakMiB);
  CHECK("the pads never held more than the pad limit", g_peak - base <= (int64_t)THUNDER_RENDER_MAX_PAD_BYTES + (1 << 20));
  thunder_render_free(r); std::free(host);
  CHECK("free gives it all back", g_live <= base);
}


// V1 (verify-238-239): the budget bounds a variation replacement too (the old copy is released first when old + new do not fit together), a pad change drops the pad's variations
// and their bytes count as freed
static void varBudget() {
  const size_t n = (size_t)1 << 26;   // 256 MiB of mono samples
  float* host = (float*)std::calloc(n, 4); CHECK("host buffer", host != nullptr); if (!host) return;
  const std::string song = shortSong(); thunder_render* r = mk(song, opts(8000, false, false));
  const int64_t base = g_live; g_peak = g_live;
  CHECK("variation A (pad 0), 256 MiB", thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n, 48000) == 0);
  CHECK("variation B (pad 1), 256 MiB (the limit)", thunder_render_set_variation(r, 0, 'm', 2, 1, host, (int64_t)n, 48000) == 0);
  CHECK("one more frame is over the limit", thunder_render_set_variation(r, 0, 'm', 3, 2, host, 1, 48000) == THUNDER_RENDER_E_LIMIT);
  const int64_t atLimit = g_live - base;
  CHECK("a replacement whose final state is over the limit is refused, the variation stays", thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n + 1, 48000) == THUNDER_RENDER_E_LIMIT && g_live - base == atLimit);
  g_peak = g_live;
  CHECK("replacing a variation at the limit with one of the same size works", thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n, 48000) == 0 && g_live - base == atLimit);
  CHECK("... and the peak never passed the limit (the old copy was released first)", g_peak - base <= (int64_t)THUNDER_RENDER_MAX_PAD_BYTES + (1 << 20));
  host[5] = NAN; CHECK("a non-finite sample in a replacement is E_ARG and the variation stays", thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n, 48000) == THUNDER_RENDER_E_ARG && g_live - base == atLimit); host[5] = 0;
  CHECK("replacing with a smaller one returns the difference", thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n / 2, 48000) == 0 && g_live - base == atLimit - (int64_t)(n * 2));
  g_peak = g_live;
  CHECK("then the room is there again (old + new do not fit together: released first)", thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n, 48000) == 0 && g_live - base == atLimit);
  CHECK("... peak within the limit", g_peak - base <= (int64_t)THUNDER_RENDER_MAX_PAD_BYTES + (1 << 20));
  { g_calls = 0; g_failAt = 1; g_failed = false; g_armed = true; const int rc = thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n, 48000); g_armed = false; g_failAt = 0;
    CHECK("an allocation failure in a limit-size replacement: E_MEMORY, the variation (released first) is gone, as render.h says", rc == THUNDER_RENDER_E_MEMORY && g_failed && nearBytes(g_live - base, atLimit - (int64_t)(n * 4)));
    CHECK("the handle works on: it can be set again", thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n, 48000) == 0 && g_live - base == atLimit); }
  // old + new fit together: built first, a failure leaves the old one
  CHECK("remove B", thunder_render_set_variation(r, 0, 'm', 2, 1, nullptr, 0, 0) == 0 && nearBytes(g_live - base, atLimit - (int64_t)(n * 4)));
  { g_calls = 0; g_failAt = 1; g_failed = false; g_armed = true; const int rc = thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n / 2, 48000); g_armed = false; g_failAt = 0;
    CHECK("a failed replacement that fits alongside the old copy leaves the old copy", rc == THUNDER_RENDER_E_MEMORY && g_failed && nearBytes(g_live - base, atLimit - (int64_t)(n * 4))); }
  CHECK("B again", thunder_render_set_variation(r, 0, 'm', 2, 1, host, (int64_t)n, 48000) == 0 && g_live - base == atLimit);
  // set_pad drops the pad's variations: pad 0 (256 MiB) replaces nothing, A (pad 0) is dropped, B (pad 1) stays: 256 + 256 = the limit
  g_peak = g_live;
  CHECK("set_pad 0 at 256 MiB: A is dropped, the final state fits", thunder_render_set_pad(r, 0, host, nullptr, (int64_t)n, 48000) == 0 && g_live - base == atLimit);
  CHECK("... without the handle ever holding more than the limit", g_peak - base <= (int64_t)THUNDER_RENDER_MAX_PAD_BYTES + (1 << 20));
  CHECK("A is not there: no room for another variation", thunder_render_set_variation(r, 0, 'm', 1, 2, host, 1, 48000) == THUNDER_RENDER_E_LIMIT);
  CHECK("removing pad 0 frees it", thunder_render_set_pad(r, 0, nullptr, nullptr, 0, 0) == 0 && nearBytes(g_live - base, atLimit - (int64_t)(n * 4)));
  CHECK("removing pad 1 (it has B) frees B", thunder_render_set_pad(r, 1, nullptr, nullptr, 0, 0) == 0 && nearBytes(g_live - base, 0));
  CHECK("everything fits again", thunder_render_set_variation(r, 0, 'm', 1, 0, host, (int64_t)n, 48000) == 0 && thunder_render_set_variation(r, 0, 'm', 2, 1, host, (int64_t)n, 48000) == 0 && nearBytes(g_live - base, atLimit));
  thunder_render_free(r); std::free(host);
  CHECK("free gives it all back", g_live <= base);
}

// D4: a hostile song and vars at their limits: the handle's memory, measured, is what the header says
static std::string bigSong(size_t bytes, const char* key) {   // a valid song plus an ignored key holding numbers, `bytes` long
  std::string s = shortSong(); s.pop_back(); s += std::string(",\"") + key + "\":[0"; while (s.size() + 3 < bytes) s += ",0"; s += "]}"; while (s.size() < bytes) s += ' '; return s;
}
static void hostileSizes() {
  { const std::string electro = readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder"); const std::string tiny = shortSong();
    for (double sr : {8000.0, 48000.0}) { const int64_t b0 = g_live; thunder_render* r = mk(electro, opts(sr, false, false)); const int64_t a = g_live - b0; thunder_render_free(r);
      const int64_t b1 = g_live; thunder_render* t = mk(tiny, opts(sr, false, false)); const int64_t c = g_live - b1; thunder_render_free(t);
      std::printf("ELECTRO (%zu bytes) at %.0f Hz: handle %.1f MiB; a 1 KiB song: %.1f MiB; the song's share about %.1f times its size\n", electro.size(), sr, (double)a / 1048576, (double)c / 1048576, (double)(a - c) / (double)electro.size()); } }
  { const std::string song = bigSong(THUNDER_RENDER_MAX_SONG_BYTES, "zz"); CHECK("song at the limit is exactly the limit", song.size() == THUNDER_RENDER_MAX_SONG_BYTES);
    const int64_t b0 = g_live; thunder_render_opts o = opts(8000, false, false); g_peak = g_live; thunder_render* r = mk(song, o); CHECK("a song at the limit is accepted", r != nullptr);
    const double mib = (double)(g_live - b0) / 1048576.0, peak = (double)(g_peak - b0) / 1048576.0, per = (double)(g_live - b0) / (double)song.size();
    std::printf("song of %zu bytes (two-byte numbers): handle %.1f MiB (%.1f times the song), peak while parsing %.1f MiB\n", song.size(), mib, per, peak);
    CHECK("handle for a limit-sized song is within the header's figure (<= 20 times, <= 400 MiB)", per <= 20.0 && (g_live - b0) <= ((int64_t)400 << 20));
    CHECK("peak while parsing is within 25 times", peak * 1048576.0 <= 25.0 * (double)song.size());
    thunder_render_free(r); CHECK("freed", g_live <= b0); }
  { std::string v3 = std::string("{\"zz\":[0"); while (v3.size() + 3 < THUNDER_RENDER_MAX_VARS_BYTES) v3 += ",0"; v3 += "]}"; while (v3.size() < THUNDER_RENDER_MAX_VARS_BYTES) v3 += ' ';
    thunder_render_opts o = opts(8000, false, false); o.vars_json = (const uint8_t*)v3.data(); o.vars_bytes = v3.size();
    const std::string song = shortSong();
    const int64_t b0 = g_live; thunder_render* r = mk(song, o); CHECK("vars at the limit are accepted", r != nullptr);
    const double mib = (double)(g_live - b0) / 1048576.0;
    std::printf("vars of %zu bytes: handle %.1f MiB (%.1f times the vars)\n", v3.size(), mib, (double)(g_live - b0) / (double)v3.size());
    CHECK("handle with limit-sized vars is within the header's figure (<= 20 times the vars)", (double)(g_live - b0) <= 20.0 * (double)v3.size() + 80.0 * 1048576.0);
    thunder_render_free(r); CHECK("freed", g_live <= b0); }
}

static bool want(int argc, char** argv, const char* name) { if (argc < 2) return true; for (int i = 1; i < argc; i++) if (!std::strcmp(argv[i], name)) return true; return false; }
int main(int argc, char** argv) {
  const std::string electro = readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder"); CHECK("ELECTRO read", !electro.empty());
  const std::string shortS = shortSong();
  g_trace = std::getenv("TR_LEAK_TRACE") != nullptr;
  std::printf("heap accounting: %s\n", haveHeap() ? "operator new + mallinfo2" : "operator new only (LSan / the sanitizer sees the rest)");
  if (want(argc, argv, "pad")) padFailure();
  if (want(argc, argv, "budget")) padBudget();
  if (want(argc, argv, "varbudget")) varBudget();
  if (want(argc, argv, "sizes")) hostileSizes();
  // create: every allocation index on the short songs, a stride on ELECTRO (8000 Hz: the same allocations, smaller buffers)
  if (want(argc, argv, "create")) {
    createFailures("short plain", shortS, opts(8000, false, false), false, 1);
    createFailures("short fx+master+pdc", shortS, opts(8000, true, true, 2), false, 1);
    createFailures("ELECTRO fx+master+pdc", electro, opts(8000, true, true, 1), false, 11);
  }
#ifdef THUNDER_HAVE_TFX
  if (want(argc, argv, "fx")) {
    createFailures("ELECTRO fx+master+pdc", electro, opts(8000, true, true, 1), true, 1);
  }
#endif
#ifdef THUNDER_HAVE_TFX   // (a read allocates only through the effects' plans: without the DSP libraries there is nothing to fail)
  if (want(argc, argv, "read")) readFailures("ELECTRO fx+master (8 s)", electro, opts(8000, true, true, 0, true, 8000 * 8), 24);
#endif
  std::printf(g_fail ? "FAILED: %d\n" : "OK\n", g_fail);
  return g_fail ? 1 : 0;
}
