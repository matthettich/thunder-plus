// U28: extreme and NaN parameters for all 15 engines (core/src/engine/xeng_*.cpp): every parameter of every engine set, one at a time, to values from -1e308 to 1e308 (valid song JSON: normX takes any finite number),
// then every field of every engine set to NaN (through the struct: a song cannot say NaN, an API user can). The only claims: no crash, no write outside the buffer, no read outside a buffer (run under ASan + UBSan in the
// sanitizer pass), the status is kEngOk or kEngUnsupported (input the JS cannot render either: a Float32Array that would throw RangeError, e.g. a delay line of 2^40 floats), and the length is within the bound.
// No hash: for extreme values the JS itself is NaN-valued or throws, and a NaN's sign bit is the CPU's. The parameter names come from the committed golden/xeng_ranges.json.
//   test_engine_xeng_fuzz <xeng_ranges.json>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/xeng.h"
#include "rt/json.h"

using namespace thunder;

static int fails = 0, renders = 0, refused = 0;
static std::vector<float> buf;

static void renderIt2(XengVoice& V, double sr, double hold, const std::string& what);
static void renderIt(XengVoice& V, double sr, double hold, const std::string& what) {
  if (getenv("FUZZ_TRACE")) std::fprintf(stderr, "%s\n", what.c_str()); const auto t0 = std::chrono::steady_clock::now(); renderIt2(V, sr, hold, what);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); if (ms > 1500) std::printf("SLOW %.0f ms: %s\n", ms, what.c_str());
}
static void renderIt2(XengVoice& V, double sr, double hold, const std::string& what) {
  EngRenderOpts o; o.sr = sr; o.hold = hold;
  const size_t bound = xeng_render_bound(V, o);
  if (bound == 0) { refused++; size_t n = 0; float t[4]; if (xeng_render_voice(V, o, t, 4, &n) != kEngUnsupported) { fails++; std::printf("FAIL %s: bound 0 but not unsupported\n", what.c_str()); } return; }
  buf.assign(bound + 8, 12345.0f); size_t n = 0;
  const int st = xeng_render_voice(V, o, buf.data(), bound, &n); renders++;
  if (st == kEngUnsupported) { refused++; return; }
  if (st != kEngOk) { fails++; std::printf("FAIL %s: status %d\n", what.c_str(), st); return; }
  if (n > bound || buf[bound] != 12345.0f || buf[bound + 7] != 12345.0f) { fails++; std::printf("FAIL %s: length %zu, bound %zu or a write past it\n", what.c_str(), n, bound); }
}

template <class P> static void nanEach(XengVoice& V, P& p, const char* kind, double sr) {
  const size_t K = sizeof(P) / sizeof(double); double* d = reinterpret_cast<double*>(&p);
  std::vector<double> save(d, d + K);
  for (size_t k = 0; k <= K; k++) {   // k == K: all of them
    std::memcpy(d, save.data(), K * sizeof(double));
    if (k < K) d[k] = NAN; else for (size_t j = 0; j < K; j++) d[j] = NAN;
    renderIt(V, sr, k % 2 ? 0.05 : 0, std::string(kind) + " NaN field " + std::to_string(k));
  }
  std::memcpy(d, save.data(), K * sizeof(double));
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("FAIL: usage: test_engine_xeng_fuzz <xeng_ranges.json>\n"); return 1; }
  std::ifstream f(argv[1], std::ios::binary); std::stringstream ss; ss << f.rdbuf(); const std::string txt = ss.str();
  JsonDoc doc; if (txt.empty() || !doc.parse(txt)) { std::printf("FAIL: %s missing or not JSON\n", argv[1]); return 1; }
  eng_init();
  static const double vals[] = {-1e308, -1000, -1, 0, 1e-300, 0.5, 100, 1e30, 1e308};
  static const char* const kinds[15] = {"tone", "modal", "swarm", "drift", "piano", "ep", "organ", "breath", "strings", "fmdrum", "wind", "phase", "partials", "skin", "chip"};
  Json R = doc.root().obj("ranges");
  for (const char* kind : kinds) {
    Json P = R.obj(kind); if (!P.isObj()) { std::printf("FAIL: no parameters for %s\n", kind); return 1; }
    for (size_t pi = 0; pi < 64; pi++) {
      if (P.valAt(pi).missing()) break;
      const std::string name(P.keyAt(pi));
      for (double v : vals) for (int variant = 0; variant < (v == 0.5 || v == 100 ? 2 : 1); variant++) {
        char tmp[96]; std::snprintf(tmp, sizeof tmp, "%.17g", v);
        std::string js = std::string("{\"engine\":\"") + kind + "\",\"" + kind + "\":{\"" + name + "\":" + tmp + "}" + (variant ? ",\"tune\":-1000,\"len\":1e308" : ",\"level\":1e30,\"drive\":1") + "}";
        JsonDoc d; if (!d.parse(js)) { std::printf("FAIL: bad json %s\n", js.c_str()); return 1; }
        XengVoice V; if (!xeng_voice_from_json(V, d.root(), Json())) { std::printf("FAIL: voice %s\n", js.c_str()); return 1; }
        renderIt(V, 2000, variant ? 0 : 0.02, js);
      }
    }
    // the voice-level extremes alone, and NaN in every field
    { XengVoice V; V.kind = xeng_kind(kind, std::strlen(kind));
      for (double t : {-1e5, -1000.0, 1000.0, 1e5}) { V.tune = t; renderIt(V, 2000, 0.02, std::string(kind) + " tune"); }
      V.tune = 0; V.len = NAN; V.gate = NAN; V.level = NAN; V.drive = NAN; renderIt(V, 2000, 0, std::string(kind) + " NaN voice fields"); V = XengVoice(); V.kind = xeng_kind(kind, std::strlen(kind));
      for (double sr : {-1.0, 0.0, 1.0, 100.0, 1e9, (double)NAN}) renderIt(V, sr, 0.02, std::string(kind) + " sample rate " + std::to_string(sr));
      switch (V.kind) {
        case XKind::Tone: nanEach(V, V.tone, kind, 2000); break;         case XKind::Modal: nanEach(V, V.modal, kind, 2000); break;       case XKind::Swarm: nanEach(V, V.swarm, kind, 2000); break;
        case XKind::Drift: nanEach(V, V.drift, kind, 2000); break;       case XKind::Piano: nanEach(V, V.piano, kind, 2000); break;       case XKind::Ep: nanEach(V, V.ep, kind, 2000); break;
        case XKind::Organ: nanEach(V, V.organ, kind, 2000); break;       case XKind::Breath: nanEach(V, V.breath, kind, 2000); break;     case XKind::Strings: nanEach(V, V.strings, kind, 2000); break;
        case XKind::FmDrum: nanEach(V, V.fmdrum, kind, 2000); break;     case XKind::Wind: nanEach(V, V.wind, kind, 2000); break;         case XKind::Phase: nanEach(V, V.phase, kind, 2000); break;
        case XKind::Partials: nanEach(V, V.partials, kind, 2000); break; case XKind::Skin: nanEach(V, V.skin, kind, 2000); break;       case XKind::Chip: nanEach(V, V.chip, kind, 2000); break;
        default: break;
      } }
  }
  // the case the verifier found: wind at oct -100 (a delay line of 2^40 floats) is refused, and so is strings, never an empty buffer
  { XengVoice V; V.kind = XKind::Wind; V.wind.oct = -100; EngRenderOpts o; size_t n = 0; float t[8];
    if (xeng_render_bound(V, o) != 0 || xeng_render_voice(V, o, t, 8, &n) != kEngUnsupported) { fails++; std::printf("FAIL: wind oct -100 must be refused\n"); }
    XengVoice S; S.kind = XKind::Strings; S.strings.oct = -100; if (xeng_render_voice(S, o, t, 8, &n) == kEngOk) { fails++; std::printf("FAIL: strings oct -100 must not render\n"); } }
  std::printf("engine xeng fuzz: %d renders, %d refused, %d failed\n", renders, refused, fails);
  if (!fails) std::printf("PASS test_engine_xeng_fuzz\n");
  return fails ? 1 : 0;
}
