// U31: the allocation audit of the live audio path over EVERY …chains module type (a verifier's driver made permanent): for each type a patch (Track In -> the module, every parameter at its maximum -> Chain Out) is
// PREPARED, then swapped in by the live runner and run for 40 blocks of 128 / 77 / 300 frames with a parameter event for every parameter, a Bypass, a _bn change and a tempo change in every block; malloc / calloc /
// realloc / free / operator new / delete are COUNTED while ChainsLive::process runs (service() runs outside it). Any call is a failure and the type is named with the first call's stack. Linux (glibc), not
// under a sanitizer (they bring their own allocator): elsewhere the test is skipped (exit 77).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
#define ALLOC_AUDIT 1
#include <execinfo.h>
#endif
#include "chains/chains.h"
#include "rt/json.h"
using namespace thunder; using namespace thunder::chains;
#ifdef ALLOC_AUDIT
static thread_local int g_on = 0; static thread_local unsigned long g_cnt = 0; static thread_local char g_first[2048]; static thread_local bool g_have = false;
static void note(const char* w) { g_cnt++; if (!g_have) { g_have = true; void* bt[14]; int n = backtrace(bt, 14); char** sy = backtrace_symbols(bt, n); std::snprintf(g_first, sizeof g_first, "%s", w); for (int i = 2; i < n && std::strlen(g_first) < 1700; i++) { std::strcat(g_first, " <- "); std::strncat(g_first, sy[i], 110); } std::free(sy); } }
extern "C" { void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_on) { g_on = 0; note("malloc"); g_on = 1; } return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (g_on) { g_on = 0; note("calloc"); g_on = 1; } return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (g_on) { g_on = 0; note("realloc"); g_on = 1; } return __libc_realloc(p, n); }
void free(void* p) { if (g_on && p) { g_on = 0; note("free"); g_on = 1; } __libc_free(p); } }
#else
static int g_on = 0; static unsigned long g_cnt = 0; static char g_first[1]; static bool g_have = false;
#endif
struct Doc { JsonDoc d; Json r; explicit Doc(const std::string& t) { if (!d.parse(t)) std::printf("bad json: %s\n", t.c_str()); r = d.root(); } };
int main(int argc, char** argv) {
#ifndef ALLOC_AUDIT
  (void)argc; (void)argv; std::printf("chains_alloc: skipped (needs glibc without a sanitizer)\n"); return 77;
#else
  const bool fast = argc > 1 && std::string(argv[1])=="fast"; const char* only = argc > 2 ? argv[2] : nullptr; const int onlyP = argc > 3 ? atoi(argv[3]) : -1;
  int bad = 0, tested = 0, expected = 0;   // expected: every type this build can run (a closed build has no library effects, Marbles, Tides or pump modules: fewer)
  for (int t = 0; t < cinfo_count(); t++) { const std::string ty = cinfo_at(t).type; if (ty != "trackIn" && ty != "chainOut" && chains_have_lib(ty)) expected++; }
  for (int t = 0; t < cinfo_count(); t++) {
    const WInfo& wi = cinfo_at(t); const std::string type = wi.type;
    if (type == "trackIn" || type == "chainOut") continue;
    if (!chains_have_lib(type)) continue; if (only && type != only) continue;
    std::string ps; for (int k = 0; k < wi.np; k++) { char b[160]; std::snprintf(b, sizeof b, "%s\"%s\":%.17g", k ? "," : "", wi.params[k].name, wi.params[k].mx); ps += b; }
    std::string cab = "{\"from\":[\"a\",\"out\"],\"to\":[\"m\",\"in\"]},{\"from\":[\"a\",\"outR\"],\"to\":[\"m\",\"inR\"]}"; std::string txt = type == "expr" ? ",\"text\":\"a*0.5+tanh(b)\"" : "";
    std::string outs; for (int k = 0; k < wi.nout && k < 2; k++) outs += std::string(",{\"from\":[\"m\",\"") + wi.out[k].name + "\"],\"to\":[\"c\",\"" + (k ? "inR" : "in") + "\"]}";
    std::string pm = std::string("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"m\",\"type\":\"") + type + "\",\"params\":{" + ps + "}" + txt + "},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1}}],\"cables\":[" + cab + outs + "]}";
    std::string pm0 = std::string("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1}}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"c\",\"in\"]}]}");
    Doc d0(pm0), d1(pm); waves::Host host; host.setFixed(.3);
    ChainsLive L(48000, 0, ChainsLiveConfig(), &host); if (!L.valid() || !L.node().engine().loadPatch(d0.r)) { std::printf("%s: cannot start\n", type.c_str()); continue; }
    if (!L.preparePatch(d1.r, 300)) { std::printf("%s: patch refused\n", type.c_str()); continue; }
    std::vector<float> x(4096), l(4096), r(4096); uint32_t s = 5; for (float& v : x) { s = s * 1664525u + 1013904223u; v = ((float)(s >> 8) / 16777216.f - .5f) * 1.6f; }
    g_cnt = 0; g_have = false; tested++;
    for (int b = 0; b < (fast ? 12 : 40); b++) {
      // events (control side, not under the flag): a parameter change of every parameter, a bypass, _bn, tempo
      for (int k = 0; k < wi.np; k++) if (onlyP < 0 || k == onlyP) L.postParam("m", wi.params[k].name, wi.params[k].mn + (wi.params[k].mx - wi.params[k].mn) * ((b * 7 + k * 3) % 11) / 10.0, L.enginePos() + 200 + 5 * k);
      if (b == 5) L.postParam("m", "_bn", 3, L.enginePos() + 160);
      { ChainsEvent te; te.kind = ChainsEvent::Tempo; te.at = L.enginePos() + 170; te.a = 90 + b; L.post(te); }
      ChainsEvent e; e.kind = ChainsEvent::Bypass; e.at = L.enginePos() + 190; e.a = 3; e.b = (b & 1) ? 1 : -1; L.post(e);
      NodeIn ni; ni.inL = x.data(); ni.inR = x.data() + 100; NodeOut no; no.L = l.data(); no.R = r.data();
      const int n = (b % 3 == 0) ? 128 : (b % 3 == 1 ? 77 : 300);
      g_on = 1; L.process(ni, no, n); g_on = 0;
      L.service();
    }
    if (g_cnt) { bad++; std::printf("ALLOC on the audio path: %-10s %lu allocator calls; first: %s\n", type.c_str(), g_cnt, g_first); }
  }
  std::printf("chains_alloc: audited %d module types through the live runner (patch swap + parameter / bypass / tempo events), %d allocated on the audio path\n", tested, bad);
  return (bad || (!only && (tested != expected || tested < 90))) ? 1 : 0;
#endif
}
