// PP (ctest `live_fxvalues_alloc`): the allocation sweep behind thunder_live_fx_values (docs/port/PP-NOTES.md). For EVERY pump type and EVERY modular effect the core hosts: a running slot is planned; then, for each
// value index of its list, the list is sent with that value at its minimum, its maximum and its default (pump: the table's min / max / default; modular: the mapped value's range lo..hi for a linear / log
// control, the byte range for a choice / whole number, and the planned value), through the live path (psetPumpRt / psetModRt, applied by the block), and a few blocks run after each. malloc / calloc /
// realloc / free / posix_memalign / aligned_alloc / memalign / operator new are counted around FxHost::process only (the records are made outside it); the slot's remake counters must not move.
// Linux (glibc), not under a sanitizer: elsewhere skipped (exit 77), as live_meters2_alloc.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#define ALLOC_AUDIT 1
#include <dlfcn.h>
#endif
#ifdef ALLOC_AUDIT
static thread_local int g_on = 0; static thread_local unsigned long g_cnt = 0; static thread_local const char* g_what = nullptr;
static void note(const char* w) { g_cnt++; if (!g_what) g_what = w; }
extern "C" { void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_on) { g_on = 0; note("malloc"); g_on = 1; } return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (g_on) { g_on = 0; note("calloc"); g_on = 1; } return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (g_on) { g_on = 0; note("realloc"); g_on = 1; } return __libc_realloc(p, n); }
void free(void* p) { if (g_on && p) { g_on = 0; note("free"); g_on = 1; } __libc_free(p); }
static void* nextSym(const char* n) { return dlsym(RTLD_NEXT, n); }
int posix_memalign(void** r, size_t a, size_t n) { const int on = g_on; g_on = 0; static int (*f)(void**, size_t, size_t) = (int (*)(void**, size_t, size_t))nextSym("posix_memalign"); if (on) note("posix_memalign"); g_on = on; return f(r, a, n); }
void* aligned_alloc(size_t a, size_t n) { const int on = g_on; g_on = 0; static void* (*f)(size_t, size_t) = (void* (*)(size_t, size_t))nextSym("aligned_alloc"); if (on) note("aligned_alloc"); g_on = on; return f(a, n); }
void* memalign(size_t a, size_t n) { const int on = g_on; g_on = 0; static void* (*f)(size_t, size_t) = (void* (*)(size_t, size_t))nextSym("memalign"); if (on) note("memalign"); g_on = on; return f(a, n); } }
#endif
#include "fx/fxhost.h"
#include "rt/json.h"
#include "modfx/modfx.h"
namespace thunder {
#include "fx/pumpparams.inc"
#include "fx/modfxtable.inc"
}
using namespace thunder;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

static void feed(float* L, float* R, int64_t n0, int n) { for (int i = 0; i < n; i++) { const double t = (double)(n0 + i); L[i] = (float)(0.3 * std::sin(0.05 * t) + 0.2 * std::sin(0.31 * t)); R[i] = (float)(0.25 * std::sin(0.043 * t + 1)); } }
// `count` blocks; the audio work (process) is the only thing counted
static void blocks(FxHost& h, int64_t* n0, int count, bool counted, unsigned long* cnt, const char** what) {
  float L[128], R[128];
  for (int b = 0; b < count; b++) {
    feed(L, R, *n0, 128);
#ifdef ALLOC_AUDIT
    if (counted) { g_cnt = 0; g_what = nullptr; g_on = 1; }
#endif
    h.process(L, R, 128, (double)*n0 / h.sampleRate());
#ifdef ALLOC_AUDIT
    if (counted) { g_on = 0; if (g_cnt) { *cnt += g_cnt; if (what && !*what) *what = g_what; } }
#endif
    *n0 += 128;
    (void)cnt; (void)what; (void)counted;
  }
}
static FxPlan* planOf(FxHost& h, const std::string& js) { JsonDoc d; JsonError je; if (!d.parse(js.data(), js.size(), &je)) return nullptr; return h.planFromSong(d.root(), 120, 0, nullptr); }

int main() {
#ifndef ALLOC_AUDIT
  std::printf("live_fxvalues_alloc: skipped (needs glibc without a sanitizer)\n"); return 77;
#else
#if !defined(THUNDER_HAVE_PUMP) || !defined(THUNDER_HAVE_TFX)
  std::printf("live_fxvalues_alloc: skipped (no effects / pump library)\n"); return 77;
#endif
  int swept = 0; long sends = 0;
  // ---- pump: every type, every value index, min / max / default ----
  for (int pt = 0; pt < kPumpTypes; pt++) for (int var = 0; var < (pt == 0 ? 5 : 1); var++) {   // EQ: also planned in linear phase (phase 1) at each lin_quality, where its other values are live too
    const char* key = fx_pump_key(pt); std::string js = std::string("[{\"type\":\"p") + key + "\",\"on\":true";
    if (var > 0) js += ",\"pump\":{\"phase\":1,\"lin_quality\":" + std::to_string(var - 1) + "}";
    js += "}]";
    char tag[40]; std::snprintf(tag, sizeof tag, var ? "%s/lin%d" : "%s", key, var - 1);
    FxHost h(48000.0); FxPlan* p = planOf(h, js);
    if (!p) { std::printf("  pump %-8s: not hosted (refused at planning)\n", tag); continue; }
    h.commit(p); delete p;
    if (h.chainSize() != 1 || !fx_is_pump(h.slotType(0))) { std::printf("  pump %-8s: not a hosted slot here\n", tag); continue; }
    const FxEntry base = *h.chainEntry(0); const int np = base.npv; int64_t n0 = 0;
    if (fx_pump_live_locked(pt, base.pv, np)) { std::printf("  pump %-10s: planned in linear phase: the whole slot rides in the song (fx_values refuses it); measured to allocate on every change when not refused\n", tag); CHECK(pt == 0 && var > 0, "only a linear-phase EQ is locked (%s)", tag); continue; }
    CHECK(var == 0, "a linear-phase EQ (%s) must be locked", tag);
    blocks(h, &n0, 200, false, nullptr, nullptr);   // running, warmed
    const uint32_t r0 = h.audioRemakes(); unsigned long cnt = 0; const char* what = nullptr; int bad = 0; double badV = 0; int badI = -1, fixedN = 0;
    for (int i = 0; i < np; i++) {
      if (fx_pump_live_fixed(pt, i)) { fixedN++; continue; }   // rides in the song: fxValues() refuses a change here
      const PumpParam& q = kPumpParams[pt][i];
      const double vals[3] = {q.mn, q.mx, q.def};
      for (int k = 0; k < 3; k++) {
        float pv[kPumpMaxParams]; for (int j = 0; j < np; j++) pv[j] = base.pv[j]; pv[i] = (float)vals[k];
        h.psetPumpRt((double)n0 / 48000.0, 0, pv, np); sends++;
        const unsigned long before = cnt; blocks(h, &n0, 3, true, &cnt, &what);
        if (cnt != before && !bad) { bad = 1; badI = i; badV = vals[k]; } else if (cnt != before) bad++;
        if (cnt != before && std::getenv("FXV_LIST")) std::printf("    alloc: pump %s index %d (%s) = %g: %lu calls\n", tag, i, fx_pump_param_name(pt, i), vals[k], cnt - before);
      }
      { float pv[kPumpMaxParams]; for (int j = 0; j < np; j++) pv[j] = base.pv[j]; h.psetPumpRt((double)n0 / 48000.0, 0, pv, np); blocks(h, &n0, 2, true, &cnt, &what); }   // back to the planned list
    }
    const uint32_t remakes = h.audioRemakes() - r0;
    std::printf("  pump %-10s: %3d values x 3 (%d ride in the song): %lu allocator calls, %u remakes%s\n", tag, np, fixedN, cnt, remakes, bad ? " <-- ALLOCATES" : "");
    if (bad) std::printf("    first: value index %d (%s) = %g, via %s (%d sends)\n", badI, fx_pump_param_name(pt, badI), badV, what ? what : "?", bad);
    CHECK(cnt == 0 && remakes == 0, "pump %s: %lu allocator calls, %u remakes while values landed", tag, cnt, remakes);
    swept += var == 0;
  }
  // ---- modular: every effect, every control, lo / hi / planned ----
  for (int m = 0; m < kModDefN; m++) {
    const ModDef& d = kModDefs[m]; const std::string js = std::string("[{\"type\":\"") + d.key + "\",\"on\":true}]";
    FxHost h(48000.0); FxPlan* p = planOf(h, js);
    if (!p) { std::printf("  mod  %-10s: not hosted (refused at planning)\n", d.key); continue; }
    h.commit(p); delete p;
    if (h.chainSize() != 1 || !fx_is_mod(h.slotType(0))) { std::printf("  mod  %-10s: not a hosted slot here\n", d.key); continue; }
    const FxEntry base = *h.chainEntry(0); const int n = base.nmp; int64_t n0 = 0;
    blocks(h, &n0, 200, false, nullptr, nullptr);
    unsigned long cnt = 0; const char* what = nullptr; int bad = 0, badI = -1; double badV = 0;
    for (int i = 0; i < n; i++) {
      const ModParam& q = d.ps[i];
      const double lo = q.kind == 'l' || q.kind == 'g' ? q.lo : (double)q.bmin, hi = q.kind == 'l' || q.kind == 'g' ? q.hi : (double)q.bmax;
      const double vals[3] = {lo, hi, base.mp[i]};
      for (int k = 0; k < 3; k++) {
        double mp[kModMaxParams]; for (int j = 0; j < n; j++) mp[j] = base.mp[j]; mp[i] = vals[k];
        h.psetModRt((double)n0 / 48000.0, 0, mp, n); sends++;
        const unsigned long before = cnt; blocks(h, &n0, 3, true, &cnt, &what);
        if (cnt != before && !bad) { bad = 1; badI = i; badV = vals[k]; } else if (cnt != before) bad++;
      }
      { double mp[kModMaxParams]; for (int j = 0; j < n; j++) mp[j] = base.mp[j]; h.psetModRt((double)n0 / 48000.0, 0, mp, n); blocks(h, &n0, 2, true, &cnt, &what); }
    }
    std::printf("  mod  %-10s: %2d controls x 3: %lu allocator calls, %u remakes, %u dead%s\n", d.key, n, cnt, h.modRemakes(), h.modDead(), bad ? " <-- ALLOCATES" : "");
    if (bad) std::printf("    first: control %d (%s) = %g, via %s (%d sends)\n", badI, d.ps[badI].p, badV, what ? what : "?", bad);
    CHECK(cnt == 0 && h.modRemakes() == 0 && h.modDead() == 0, "mod %s: %lu allocator calls, %u remakes, %u dead", d.key, cnt, h.modRemakes(), h.modDead());
    swept++;
  }
  CHECK(swept >= 40, "the sweep covered %d effect types", swept);
  std::printf("live_fxvalues_alloc: %d types, %ld sends, %d checks, %d failed\n", swept, sends, g_checks, g_fail);
  return g_fail ? 1 : 0;
#endif
}
