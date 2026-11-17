// U27: extreme and NaN parameters for acid, mach and blip (core/src/engine/xeng_live.cpp, xeng_blip.cpp), through xeng_render_voice: every parameter of every engine at every value of the extreme list (-1e308 .. 1e308, NaN, +-inf: through the
// struct; a song cannot say NaN, an API user can), the voice's tune / drive / level / len / hold at extremes, sample rates 1 to 96000 and the nonsense ones, and a COMBINED mode (several extreme fields at once, random sample rate and
// hold, a per-render time limit). The claims: no crash, no write outside the buffer, no read outside a buffer (run under ASan + UBSan in the sanitizer pass), the status is kEngOk or kEngUnsupported (input the JS cannot render either:
// a Float32Array that would throw, `CHORDS[NaN].length`, a Swarm of 1e9 voices), the length is within the bound. No hash: for extreme values the JS is NaN-valued itself, and a NaN's sign bit is the CPU's.
//   test_engine_xeng3_fuzz            FUZZ_COMBINED=<n> sets the number of combined renders (default 300; the sanitizer pass of the PR runs 5000 or more)
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/xeng.h"
#include "engine/xeng_live.h"

using namespace thunder;

static int fails = 0, renders = 0, refused = 0;
static std::vector<float> buf;
static const double kExt[] = {-1e9, -1000, -1, -0.5, 0, 0.5, 1, 2.04, 100, 1e9, 1e308, NAN, INFINITY, -INFINITY, 1e-300, -1e308};
static const int kExtN = (int)(sizeof kExt / sizeof kExt[0]);

static void renderIt(const XengVoice& V, double sr, double hold, const std::string& what) {
  if (getenv("FUZZ_TRACE")) std::fprintf(stderr, "%s\n", what.c_str());
  const auto t0 = std::chrono::steady_clock::now();
  EngRenderOpts o; o.sr = sr; o.hold = hold;
  const size_t bound = xeng_render_bound(V, o);
  if (bound == 0) { refused++; size_t n = 0; float t[4]; if (xeng_render_voice(V, o, t, 4, &n) != kEngUnsupported) { fails++; std::printf("FAIL %s: bound 0 but not unsupported\n", what.c_str()); } }
  else {
    buf.assign(bound + 8, 12345.0f); size_t n = 0;
    const int st = xeng_render_voice(V, o, buf.data(), bound, &n); renders++;
    if (st == kEngUnsupported) refused++;
    else if (st != kEngOk) { fails++; std::printf("FAIL %s: status %d\n", what.c_str(), st); }
    else if (n > bound || buf[bound] != 12345.0f || buf[bound + 7] != 12345.0f) { fails++; std::printf("FAIL %s: length %zu, bound %zu or a write past it\n", what.c_str(), n, bound); }
  }
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (s > 0.25 && getenv("FUZZ_SLOW")) std::printf("slow %.2f s: %s\n", s, what.c_str());
  if (s > 20) { fails++; std::printf("FAIL: over the time limit (%.1f s): %s\n", s, what.c_str()); }
}

struct Field { const char* name; size_t off; };
#define XF(n, d) {#n, offsetof(XP_CUR, n)},
#define XP_CUR AcidP
static const Field kAcidF[] = { XP_ACID(XF) };
#undef XP_CUR
#define XP_CUR MachP
static const Field kMachF[] = { XP_MACH(XF) };
#undef XP_CUR
#define XP_CUR XBlipP
static const Field kBlipF[] = { XP_BLIP(XF) };
#undef XP_CUR

static XengVoice mk(XKind k) { XengVoice V; V.kind = k; return V; }
static double* fieldPtr(XengVoice& V, const Field& f) { char* base = V.kind == XKind::Acid ? (char*)&V.acid : V.kind == XKind::Mach ? (char*)&V.mach : (char*)&V.blip; return (double*)(base + f.off); }

int main() {
  eng_init();
  struct Kind { XKind k; const char* name; const Field* f; size_t n; };
  const Kind kinds[3] = {{XKind::Acid, "acid", kAcidF, sizeof kAcidF / sizeof(Field)}, {XKind::Mach, "mach", kMachF, sizeof kMachF / sizeof(Field)}, {XKind::Blip, "blip", kBlipF, sizeof kBlipF / sizeof(Field)}};
  // ---- one field at a time, every extreme, with and without the voice's own extremes
  for (const Kind& K : kinds) for (size_t i = 0; i < K.n; i++) for (int e = 0; e < kExtN; e++) for (int variant = 0; variant < 2; variant++) {
    XengVoice V = mk(K.k); *fieldPtr(V, K.f[i]) = kExt[e];
    if (variant) { V.tune = -1000; V.len = 1e308; V.level = 1e30; V.drive = 1e9; }
    char w[120]; std::snprintf(w, sizeof w, "%s %s=%g v%d", K.name, K.f[i].name, kExt[e], variant);
    renderIt(V, 2000, variant ? 0 : 0.05, w);
    if (variant) renderIt(V, 44100, 0.02, std::string(w) + " @44100");
  }
  // ---- every field NaN / inf at once (through the struct)
  for (const Kind& K : kinds) for (double x : {(double)NAN, (double)INFINITY, -(double)INFINITY, 1e308, -1e308}) {
    XengVoice V = mk(K.k); for (size_t i = 0; i < K.n; i++) *fieldPtr(V, K.f[i]) = x; renderIt(V, 8000, 0.05, std::string(K.name) + " all fields extreme");
    for (double sr : {1.0, 10.0, 22050.0}) renderIt(V, sr, 0.1, std::string(K.name) + " all extreme at sr");
  }
  // ---- the voice's own fields, hold and sample rates
  for (const Kind& K : kinds) {
    XengVoice V = mk(K.k);
    for (double t : {-1e5, -1000.0, 1000.0, 1e5, 1e308, (double)NAN}) { V.tune = t; renderIt(V, 2000, 0.02, std::string(K.name) + " tune"); } V.tune = 0;
    V.len = NAN; V.gate = NAN; V.level = NAN; V.drive = NAN; V.tune = NAN; renderIt(V, 2000, 0, std::string(K.name) + " NaN voice fields"); V = mk(K.k);
    for (double len : {-5.0, 0.0, 1e-9, 9.0, 1e9, 1e308, (double)INFINITY}) { V.len = len; renderIt(V, 8000, 0, std::string(K.name) + " len"); } V.len = 4;
    for (double sr : {-1.0, 0.0, 0.5, 1.0, 2.0, 10.0, 100.0, 1000.0, 12345.678, 1e7, 1e9, (double)NAN, (double)INFINITY}) for (double hold : {0.0, 0.05, 1.0, 1e9, (double)NAN, -3.0}) if (sr < 1e6 || hold == 0.05) renderIt(V, sr, hold, std::string(K.name) + " sample rate " + std::to_string(sr) + " hold " + std::to_string(hold));
    for (double sr : {8000.0, 22050.0, 32000.0, 44100.0, 48000.0, 96000.0}) renderIt(V, sr, 0.5, std::string(K.name) + " sample rate " + std::to_string(sr));
    for (double hold : {1e9, 40.0}) renderIt(V, 96000, hold, std::string(K.name) + " hold at 96k");
  }
  // ---- named cases: what the JS throws on or never finishes
  { XengVoice V = mk(XKind::Mach); V.mach.machine = 4; V.mach.lfoDest = 2; V.mach.lfoDepth = 1; V.mach.lfoRate = 1e308;   // at 1 Hz the LFO phase (dt = 16 s per block) runs to infinity: lfo - floor(lfo) = NaN, A is NaN, CHORDS[NaN].length throws
    EngRenderOpts o; o.sr = 1; o.hold = 5; size_t n = 0; const size_t bd = xeng_render_bound(V, o); buf.assign(bd + 8, 12345.0f);
    if (bd == 0 || xeng_render_voice(V, o, buf.data(), bd, &n) != kEngUnsupported) { fails++; std::printf("FAIL: Ensemble with a NaN A must be refused\n"); } else refused++; }
  { XengVoice V = mk(XKind::Mach); V.mach.machine = 3; V.mach.b = 1e9; EngRenderOpts o; o.sr = 8000; o.hold = 0.2; size_t n = 0; buf.assign(xeng_render_bound(V, o) + 8, 12345.0f);
    if (xeng_render_voice(V, o, buf.data(), buf.size() - 8, &n) != kEngUnsupported) { fails++; std::printf("FAIL: a Swarm of 5e9 voices must be refused\n"); } }
  { XengVoice V = mk(XKind::Mach); V.mach.machine = 3; V.mach.b = 2.04; renderIt(V, 8000, 0.2, "mach swarm 12 voices (NaN output as the JS)"); V.mach.b = -0.2; renderIt(V, 8000, 0.2, "mach swarm 1 voice"); V.mach.b = -9; renderIt(V, 8000, 0.2, "mach swarm negative"); }
  { XengVoice V = mk(XKind::Acid); V.acid.rel = -1e5; EngRenderOpts o; o.sr = 8000; o.hold = 0.2; if (xeng_render_bound(V, o) != 0) { fails++; std::printf("FAIL: a negative release long enough to give a negative array length must be refused\n"); } }
  { XengVoice V = mk(XKind::Blip); V.blip.atk = -1e9; EngRenderOpts o; o.sr = 8000; if (xeng_render_bound(V, o) != 0) { fails++; std::printf("FAIL: a Blip of negative length must be refused\n"); } }
  // ---- combined fields: 2 to 7 extreme fields at once, random sample rate and hold, the voice's own extremes
  {
    uint32_t sd = 0x9e3779b9u; auto rn = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) & 0xffff; };
    const char* env = getenv("FUZZ_COMBINED"); const int total = env ? std::atoi(env) : 300; int done = 0;
    static const double srs[] = {1, 2, 5, 10, 100, 1000, 2000, 8000, 8000, 22050, 32000, 44100, 96000};
    static const double holds[] = {NAN, 0, -1, 0.001, 0.05, 0.5, 1, 5, 100, 1e9, INFINITY, -INFINITY, 1e308};
    while (done < total) for (const Kind& K : kinds) {
      if (done >= total) break;
      XengVoice V = mk(K.k); const int nf = 2 + (int)(rn() % 6); std::string what = std::string("combined ") + K.name;
      for (int j = 0; j < nf; j++) { const size_t fi = rn() % K.n; const double x = kExt[rn() % kExtN]; *fieldPtr(V, K.f[fi]) = x; char tmp[80]; std::snprintf(tmp, sizeof tmp, " %s=%g", K.f[fi].name, x); what += tmp; }
      if (K.k == XKind::Mach && rn() % 2) { V.mach.machine = (double)(rn() % 8); if (rn() % 2) { V.mach.lfoDest = (double)(rn() % 4); V.mach.lfoRate = kExt[rn() % kExtN]; V.mach.lfoDepth = 1; } }
      if (rn() % 3 == 0) { V.tune = kExt[rn() % kExtN]; V.level = kExt[rn() % kExtN]; V.drive = kExt[rn() % kExtN]; V.len = kExt[rn() % kExtN]; what += " +voice extremes"; }
      double sr = srs[rn() % 13], hold = holds[rn() % 13]; char tmp[80]; std::snprintf(tmp, sizeof tmp, " sr=%g hold=%g", sr, hold); what += tmp;
      renderIt(V, sr, hold, what); done++;
    }
    std::printf("engine xeng3 fuzz: %d combined renders\n", done);
  }
  std::printf("engine xeng3 fuzz: %d renders, %d refused, %d failed\n", renders, refused, fails);
  if (!fails) std::printf("PASS test_engine_xeng3_fuzz\n");
  return fails ? 1 : 0;
}
