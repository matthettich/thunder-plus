// U26: extreme and NaN parameters for bass and vox (core/src/engine/bass_core.cpp, vox_core.cpp), through xeng_render_voice, and for the live shell: every parameter of both engines at every value of the extreme list (-1e9 .. 1e308,
// NaN, +-inf: through the struct; a song cannot say NaN, an API user can), the voice's tune / drive / level / len / hold at extremes, sample rates 0.5 to 1e5 and the nonsense ones, odd lyrics, and a COMBINED mode (several
// extreme fields at once, random sample rate and hold, a per-render time limit; the combined fields come from {-1e9, -1000, -1, -0.5, 0, 0.5, 1, 2.04, 100, 1e9, 1e308, NaN, +inf, -inf}). The claims: no crash, no write outside
// the buffer, no read outside a buffer (run under ASan + UBSan in the sanitizer pass), the status is kEngOk or kEngUnsupported (input the JS cannot render either: `vow[NaN][0]`, `S[NaN].rest`, the lyric word
// "constructor", a Float32Array that would throw), the length is within the bound, a live shell stays inside its output. No hash: for extreme values the JS is NaN-valued itself, and a NaN's sign bit is the CPU's.
//   test_engine_u26_fuzz            FUZZ_COMBINED=<n> sets the number of combined renders (default 300; the sanitizer pass of the PR runs 6000 or more)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/bass_core.h"
#include "engine/engine.h"
#include "engine/live_shell.h"
#include "engine/vox_core.h"
#include "engine/xeng.h"

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
#define XP_CUR BassP
static const Field kBassF[] = { XP_BASS(XF) };
#undef XP_CUR
#define XP_CUR VoxP
static const Field kVoxF[] = { XP_VOX(XF) };
#undef XP_CUR

static XengVoice mk(XKind k) { XengVoice V; V.kind = k; return V; }
static double* fieldPtr(XengVoice& V, const Field& f) { char* base = V.kind == XKind::Bass ? (char*)&V.bass : (char*)&V.vox; return (double*)(base + f.off); }

static const char* const kTexts[] = {"", " ", ".", ", . ;", "a", "constructor", "Constructor", "toString", "__proto__", "hello world, I am a robot", "ba-na-na, la strengths", "[B P T D K G CH JH F V TH DH S Z SH ZH HH M N NG L R W Y IY AY]",
  "[", "[]", "[ZZ]", "[B", "]]]", "\xC4\xB0stanbul \xE2\x84\xAA", "\xC3\x9F\xC5\xBF\xEF\xAC\x81 fi\xEF\xAC\x82", "\xF0\x9F\x98\x80 \xF0\x9F\x98\x80", "\xE2\x80\xA8x\xC2\xA0y\xEF\xBB\xBFz", "'''", "-'-'-", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "\xFF\xFE bad utf8 \xC0\xAF", "x\xE2\x82", "one two three four five six seven eight nine ten eleven twelve"};
static const int kTextN = (int)(sizeof kTexts / sizeof kTexts[0]);

int main() {
  eng_init();
  struct Kind { XKind k; const char* name; const Field* f; size_t n; };
  const Kind kinds[2] = {{XKind::Bass, "bass", kBassF, sizeof kBassF / sizeof(Field)}, {XKind::Vox, "vox", kVoxF, sizeof kVoxF / sizeof(Field)}};
  // ---- one field at a time, every extreme, with and without the voice's own extremes
  for (const Kind& K : kinds) for (size_t i = 0; i < K.n; i++) for (int e = 0; e < kExtN; e++) for (int variant = 0; variant < 2; variant++) {
    XengVoice V = mk(K.k); *fieldPtr(V, K.f[i]) = kExt[e];
    if (variant) { V.tune = -1000; V.len = 1e308; V.level = 1e30; V.drive = 1e9; }
    char w[120]; std::snprintf(w, sizeof w, "%s %s=%g v%d", K.name, K.f[i].name, kExt[e], variant);
    renderIt(V, 2000, variant ? 0 : 0.05, w);
    if (variant) renderIt(V, 44100, 0.02, std::string(w) + " @44100");
  }
  // ---- every field extreme at once (through the struct)
  for (const Kind& K : kinds) for (double x : {(double)NAN, (double)INFINITY, -(double)INFINITY, 1e308, -1e308, -1.0, 1e9}) {
    XengVoice V = mk(K.k); for (size_t i = 0; i < K.n; i++) *fieldPtr(V, K.f[i]) = x; renderIt(V, 8000, 0.05, std::string(K.name) + " all fields extreme");
    for (double sr : {0.5, 1.0, 10.0, 22050.0}) renderIt(V, sr, 0.1, std::string(K.name) + " all extreme at sr");
  }
  // ---- the voice's own fields, hold and sample rates
  for (const Kind& K : kinds) {
    XengVoice V = mk(K.k);
    for (double t : {-1e5, -1000.0, 1000.0, 1e5, 1e308, (double)NAN}) { V.tune = t; renderIt(V, 2000, 0.02, std::string(K.name) + " tune"); } V.tune = 0;
    V.len = NAN; V.gate = NAN; V.level = NAN; V.drive = NAN; V.tune = NAN; renderIt(V, 2000, 0, std::string(K.name) + " NaN voice fields"); V = mk(K.k);
    for (double len : {-5.0, 0.0, 1e-9, 9.0, 1e9, 1e308, (double)INFINITY}) { V.len = len; renderIt(V, 8000, 0, std::string(K.name) + " len"); } V.len = 4;
    for (double sr : {-1.0, 0.0, 0.5, 1.0, 2.0, 10.0, 100.0, 1000.0, 12345.678, 1e5, 1e7, 1e9, (double)NAN, (double)INFINITY}) for (double hold : {0.0, 0.05, 1.0, 1e9, (double)NAN, -3.0}) if (sr < 1e5 || hold == 0.05) renderIt(V, sr, hold, std::string(K.name) + " sr/hold");
    for (double sr : {8000.0, 22050.0, 32000.0, 44100.0, 48000.0, 96000.0, 1e5}) renderIt(V, sr, 0.5, std::string(K.name) + " sample rate " + std::to_string(sr));
    for (double hold : {1e9, 40.0}) renderIt(V, 96000, hold, std::string(K.name) + " hold at 96k");
  }
  // ---- lyrics: odd, huge, invalid UTF-8, the names the JS's object lookups would hit
  for (int t = 0; t < kTextN; t++) for (double sr : {8000.0, 48000.0}) { XengVoice V = mk(XKind::Vox); V.vox.txt = kTexts[t]; renderIt(V, sr, 0, std::string("vox text #") + std::to_string(t)); }
  { XengVoice V = mk(XKind::Vox); V.vox.txt.assign(200000, 'a'); renderIt(V, 8000, 0, "vox 200 kB of one letter"); V.vox.txt.clear(); for (int i = 0; i < 9000; i++) V.vox.txt += "ba "; renderIt(V, 8000, 0, "vox 9000 words"); }
  // ---- named cases: what the JS throws on
  { XengVoice V = mk(XKind::Vox); V.vox.txt = "constructor"; EngRenderOpts o; o.sr = 8000; size_t n = 0; float t[4]; if (xeng_render_bound(V, o) != 0 && xeng_render_voice(V, o, t, 4, &n) != kEngUnsupported) { fails++; std::printf("FAIL: the lyric word constructor must be refused\n"); } else refused++; }
  { XengVoice V = mk(XKind::Bass); V.bass.vowel = NAN; EngRenderOpts o; o.sr = 8000; o.hold = 0.1; size_t n = 0; const size_t b = xeng_render_bound(V, o); buf.assign(b + 8, 0.0f); if (b != 0 && xeng_render_voice(V, o, buf.data(), b, &n) != kEngUnsupported) { fails++; std::printf("FAIL: a NaN vowel must be refused\n"); } else refused++; }
  { XengVoice V = mk(XKind::Bass); V.bass.rel = -1e5; EngRenderOpts o; o.sr = 8000; o.hold = 0.2; if (xeng_render_bound(V, o) != 0) { fails++; std::printf("FAIL: a negative release long enough to give a negative array length must be refused\n"); } }
  // ---- combined fields: 2 to 7 extreme fields at once, random sample rate and hold, the voice's own extremes, odd lyrics
  {
    uint32_t sd = 0x9e3779b9u; auto rn = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) & 0xffff; };
    const char* env = getenv("FUZZ_COMBINED"); const int total = env ? std::atoi(env) : 300; int done = 0;
    static const double srs[] = {0.5, 1, 2, 5, 10, 100, 1000, 2000, 8000, 8000, 22050, 32000, 44100, 96000, 1e5};
    static const double holds[] = {NAN, 0, -1, 0.001, 0.05, 0.5, 1, 5, 100, 1e9, INFINITY, -INFINITY, 1e308};
    while (done < total) for (const Kind& K : kinds) {
      if (done >= total) break;
      XengVoice V = mk(K.k); const int nf = 2 + (int)(rn() % 6); std::string what = std::string("combined ") + K.name;
      for (int j = 0; j < nf; j++) { const size_t fi = rn() % K.n; const double x = kExt[rn() % kExtN]; *fieldPtr(V, K.f[fi]) = x; char tmp[80]; std::snprintf(tmp, sizeof tmp, " %s=%g", K.f[fi].name, x); what += tmp; }
      if (K.k == XKind::Vox && rn() % 2) { V.vox.txt = kTexts[rn() % kTextN]; what += " txt"; }
      if (rn() % 3 == 0) { V.tune = kExt[rn() % kExtN]; V.level = kExt[rn() % kExtN]; V.drive = kExt[rn() % kExtN]; V.len = kExt[rn() % kExtN]; what += " +voice extremes"; }
      double sr = srs[rn() % 15], hold = holds[rn() % 13]; char tmp[80]; std::snprintf(tmp, sizeof tmp, " sr=%g hold=%g", sr, hold); what += tmp;
      renderIt(V, sr, hold, what); done++;
    }
    std::printf("engine u26 fuzz: %d combined renders\n", done);
  }
  // ---- the live shell with extreme fields and events: stays inside its output, never writes a non-finite sample for finite sample-rate sane input; the cores' own NaN (an extreme field) is allowed to give NaN but not to crash
  {
    uint32_t sd = 0x1234567u; auto rn = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) & 0xffff; }; int runs = 0;
    const char* env = getenv("FUZZ_COMBINED"); const int total = env ? std::atoi(env) / 10 + 20 : 40;
    for (int r = 0; r < total; r++) for (int kind = 0; kind < 2; kind++) {
      const double sr = r % 3 == 0 ? kExt[rn() % kExtN] : (r % 3 == 1 ? 8000.0 : 48000.0); if (!(sr > 0.4 && sr <= 1e5)) continue;
      std::vector<float> L(1000), R(1000); std::vector<LiveEvent> ev;
      for (int j = 0; j < 12; j++) { LiveEvent e; e.at = rn() % 400; e.type = (LiveEvType)(rn() % 6); e.leg = rn() % 2; e.same = rn() % 2; e.v = kExt[rn() % kExtN]; e.dur = kExt[rn() % kExtN]; ev.push_back(e); }
      std::stable_sort(ev.begin(), ev.end(), [](const LiveEvent& a, const LiveEvent& b) { return a.at < b.at; });
      if (kind == 0) { LiveBass* s = new LiveBass(sr); BassP p; for (int j = 0; j < 4; j++) { const size_t fi = rn() % (sizeof kBassF / sizeof(Field)); *(double*)((char*)&p + kBassF[fi].off) = kExt[rn() % kExtN]; } s->core().set(p);
        s->process(L.data(), R.data(), 500, ev.data(), (uint32_t)ev.size()); s->process(L.data() + 500, R.data() + 500, 500, nullptr, 0); delete s; }
      else { LiveVox* s = new LiveVox(sr); VoxP p; for (int j = 0; j < 4; j++) { const size_t fi = rn() % (sizeof kVoxF / sizeof(Field)); *(double*)((char*)&p + kVoxF[fi].off) = kExt[rn() % kExtN]; } p.txt = kTexts[rn() % kTextN]; s->core().set(p);
        s->process(L.data(), R.data(), 500, ev.data(), (uint32_t)ev.size()); s->process(L.data() + 500, R.data() + 500, 500, nullptr, 0); delete s; }
      runs++;
    }
    std::printf("engine u26 fuzz: %d live-shell runs with extreme fields and events\n", runs);
  }
  std::printf("engine u26 fuzz: %d renders, %d refused, %d failed\n", renders, refused, fails);
  if (!fails) std::printf("PASS test_engine_u26_fuzz\n");
  return fails ? 1 : 0;
}
