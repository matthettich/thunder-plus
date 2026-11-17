// U29: extreme and NaN parameters for the 5 engines table, vector, scanned, resynth, bytebeat (core/src/engine/xeng_*.cpp), played WITH data (a valid loaded wavetable, a valid analysis, a formula), and hostile data: every parameter of every engine set, one at a time, to values from -1e308 to 1e308 (valid song JSON: normX takes any finite number),
// then every field of every engine set to NaN (through the struct: a song cannot say NaN, an API user can). The only claims: no crash, no write outside the buffer, no read outside a buffer (run under ASan + UBSan in the
// sanitizer pass), the status is kEngOk or kEngUnsupported (input the JS cannot render either: a Float32Array that would throw RangeError, e.g. a delay line of 2^40 floats), and the length is within the bound.
// No hash: for extreme values the JS itself is NaN-valued or throws, and a NaN's sign bit is the CPU's. The parameter names come from the committed golden/xeng2_ranges.json; the analysis from the fixture golden/engine_xeng2_voices.json.
//   test_engine_xeng2_fuzz <xeng2_ranges.json> <engine_xeng2_voices.json>
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
static const XRsData* gRs = nullptr; static XWtData gWt; static std::string gBb = "t*(t>>5|t>>8)";   // data every voice of the engine plays from (the fuzz sets V.rs / V.wt / V.bbExpr from them)

static void renderIt2(XengVoice& V, double sr, double hold, const std::string& what);
static void renderIt(XengVoice& V, double sr, double hold, const std::string& what) {
  if (getenv("FUZZ_TRACE")) { std::fprintf(stderr, "%s\n", what.c_str()); }
  const auto t0 = std::chrono::steady_clock::now(); renderIt2(V, sr, hold, what);
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


static std::string b64(const std::vector<uint8_t>& u) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string s;
  for (size_t i = 0; i < u.size(); i += 3) { const unsigned a = u[i], b = i + 1 < u.size() ? u[i + 1] : 0, c = i + 2 < u.size() ? u[i + 2] : 0, v = (a << 16) | (b << 8) | c;
    s += T[(v >> 18) & 63]; s += T[(v >> 12) & 63]; s += i + 1 < u.size() ? T[(v >> 6) & 63] : '='; s += i + 2 < u.size() ? T[v & 63] : '='; }
  return s;
}
static void prime(XengVoice& V) {   // the voice plays from valid data
  if (V.kind == XKind::Table) V.wt = gWt;
  if (V.kind == XKind::Resynth && gRs) V.rs = *gRs;
  if (V.kind == XKind::Bytebeat) { V.hasBb = true; V.bbExpr = gBb; }
}
static bool fromJson(XengVoice& V, const std::string& js) {
  JsonDoc d; if (!d.parse(js)) { std::printf("FAIL: bad json %s\n", js.c_str()); return false; }
  if (!xeng_voice_from_json(V, d.root(), Json())) { std::printf("FAIL: voice %s\n", js.c_str()); return false; }
  prime(V); return true;
}
// a voice whose data is given as JSON text (hostile data): parsed by the engine's own reader
static void hostile(const std::string& kind, const std::string& extra, const std::string& what, double sr = 2000, double hold = 0.05) {
  const std::string js = "{\"engine\":\"" + kind + "\"," + extra + "}";
  JsonDoc d; if (!d.parse(js)) { std::printf("FAIL: bad hostile json %s\n", what.c_str()); fails++; return; }
  XengVoice V; if (!xeng_voice_from_json(V, d.root(), Json())) { std::printf("FAIL: hostile voice %s\n", what.c_str()); fails++; return; }
  renderIt(V, sr, hold, "hostile " + what);
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("FAIL: usage: test_engine_xeng2_fuzz <xeng2_ranges.json> <engine_xeng2_voices.json>\n"); return 1; }
  std::ifstream f(argv[1], std::ios::binary); std::stringstream ss; ss << f.rdbuf(); const std::string txt = ss.str();
  std::ifstream f2(argv[2], std::ios::binary); std::stringstream s2; s2 << f2.rdbuf(); const std::string txt2 = s2.str();
  JsonDoc doc, fix; if (txt.empty() || !doc.parse(txt)) { std::printf("FAIL: %s missing or not JSON\n", argv[1]); return 1; }
  if (txt2.empty() || !fix.parse(txt2)) { std::printf("FAIL: %s missing or not JSON\n", argv[2]); return 1; }
  eng_init();
  static XRsData rs0; xeng_rs_from_json(rs0, fix.root().arr("blobs").at(0)); gRs = &rs0;
  { std::vector<uint8_t> u; uint32_t s = 12345; for (int i = 0; i < 8 * 16 * 2 * 2; i++) { s = s * 1664525u + 1013904223u; const int16_t q = (int16_t)((int)(s >> 20) % 4000 - 2000); u.push_back((uint8_t)(q & 255)); u.push_back((uint8_t)((q >> 8) & 255)); }
    gWt.present = gWt.valid = true; gWt.nf = 8; gWt.h = 16; gWt.pk = 0.4; gWt.a = b64(u); }
  static const double vals[] = {-1e308, -1000, -1, 0, 1e-300, 0.5, 100, 1e30, 1e308};
  static const char* const kinds[5] = {"table", "vector", "scanned", "resynth", "bytebeat"};
  Json R = doc.root().obj("ranges");
  for (const char* kind : kinds) {
    Json P = R.obj(kind); if (!P.isObj()) { std::printf("FAIL: no parameters for %s\n", kind); return 1; }
    for (size_t pi = 0; pi < 80; pi++) {
      if (P.valAt(pi).missing()) break;
      const std::string name(P.keyAt(pi));
      for (double v : vals) for (int variant = 0; variant < (v == 0.5 || v == 100 ? 2 : 1); variant++) {
        char tmp[96]; std::snprintf(tmp, sizeof tmp, "%.17g", v);
        std::string js = std::string("{\"engine\":\"") + kind + "\",\"" + kind + "\":{\"" + name + "\":" + tmp + (strcmp(kind, "table") == 0 && name != "table" ? ",\"table\":4" : "") + "}" + (variant ? ",\"tune\":-1000,\"len\":1e308" : ",\"level\":1e30,\"drive\":1") + "}";
        XengVoice V; if (!fromJson(V, js)) return 1;
        renderIt(V, 2000, variant ? 0 : 0.02, js);
      }
    }
    // the voice-level extremes alone, and NaN in every field
    { XengVoice V; V.kind = xeng_kind(kind, std::strlen(kind)); prime(V);
      for (double t : {-1e5, -1000.0, 1000.0, 1e5}) { V.tune = t; renderIt(V, 2000, 0.02, std::string(kind) + " tune"); }
      V.tune = 0; V.len = NAN; V.gate = NAN; V.level = NAN; V.drive = NAN; renderIt(V, 2000, 0, std::string(kind) + " NaN voice fields"); V = XengVoice(); V.kind = xeng_kind(kind, std::strlen(kind)); prime(V);
      for (double sr : {-1.0, 0.0, 1.0, 100.0, 1e9, (double)NAN}) renderIt(V, sr, 0.02, std::string(kind) + " sample rate " + std::to_string(sr));
      for (double sr : {8000.0, 22050.0, 96000.0}) renderIt(V, sr, 0.05, std::string(kind) + " sample rate " + std::to_string(sr));
      switch (V.kind) {
        case XKind::Table: V.table.table = 4; nanEach(V, V.table, kind, 2000); V.table.table = 0; nanEach(V, V.table, kind, 2000); break;
        case XKind::Vector: nanEach(V, V.vector, kind, 2000); V.vector.mode = 1; nanEach(V, V.vector, kind, 2000); break;
        case XKind::Scanned: nanEach(V, V.scanned, kind, 2000); break;
        case XKind::Resynth: V.resynth.fkeep = 0.5; V.resynth.pmot = 1; nanEach(V, V.resynth, kind, 2000); break;
        case XKind::Bytebeat: nanEach(V, V.bytebeat, kind, 2000); break;
        default: break;
      } }
  }
  // ---- hostile data: a wavetable, an analysis and a formula as a song file could carry them
  const std::string tp = "\"table\":{\"table\":4,\"pos\":0.5}";
  hostile("table", tp + ",\"wtData\":{\"v\":1,\"nf\":1e9,\"h\":1e9,\"a\":\"AAAA\"}", "wt nf h 1e9");
  hostile("table", tp + ",\"wtData\":{\"v\":1,\"nf\":2.5,\"h\":8,\"a\":\"AAAA\"}", "wt nf 2.5");
  hostile("table", tp + ",\"wtData\":{\"v\":1,\"nf\":3,\"h\":1e308,\"a\":\"AAAA\"}", "wt h 1e308");
  hostile("table", tp + ",\"wtData\":{\"v\":1,\"nf\":4096,\"h\":512,\"a\":\"AAAA\"}", "wt 4096 x 512");
  hostile("table", tp + ",\"wtData\":{\"v\":1,\"nf\":4,\"h\":4096,\"a\":\"AAAA\"}", "wt h 4096 (past the 2048 FFT)");
  hostile("table", tp + ",\"wtData\":{\"v\":1,\"nf\":1,\"h\":1,\"a\":\"!!!!\\u00e9\\ud800\"}", "wt garbage a");
  hostile("table", tp + ",\"wtData\":{\"v\":1,\"nf\":2,\"h\":300,\"pk\":1e308,\"a\":\"/////w==\"}", "wt pk 1e308");
  hostile("table", tp + ",\"wtData\":{\"v\":1,\"nf\":-1,\"h\":8,\"a\":\"AAAA\"}", "wt nf -1");
  hostile("table", tp + ",\"wtData\":5", "wt a number"); hostile("table", tp + ",\"wtData\":null", "wt null"); hostile("table", tp + ",\"wtData\":[1,2]", "wt array");
  const std::string rp = "\"resynth\":{\"fkeep\":1,\"noise\":1}";
  const char* rss[] = {"{\"np\":1e10,\"nf\":4,\"a\":\"AAAA\"}", "{\"np\":3e9,\"nf\":3e9,\"a\":\"AAAA\"}", "{\"np\":4,\"nf\":1e9,\"a\":\"AAAA\"}", "{\"np\":4097,\"nf\":2,\"a\":\"AAAA\"}", "{\"np\":2,\"nf\":0.5,\"a\":\"AAAA\"}",
    "{\"np\":2,\"nf\":2,\"a\":\"AAAA\"}", "{\"np\":2,\"nf\":4,\"a\":\"AAAAAAAAAAAA\",\"rat\":[1,\"x\"],\"hn\":[1,2]}", "{\"np\":2,\"nf\":4,\"a\":\"AQIDBAUGBwg=\",\"rat\":[1,2],\"hn\":[\"a\",null,1e30]}",
    "{\"np\":2,\"nf\":4,\"a\":\"AQIDBAUGBwg=\",\"n\":\"AQ\",\"pc\":\"AQIDBAUGBwg=\",\"rat\":[1,2e308],\"hn\":[1,2],\"hop\":-5,\"dur\":1e308,\"f0\":-3,\"pk\":0,\"npk\":-1}",
    "{\"np\":2,\"nf\":4,\"a\":\"/////////w==\",\"n\":\"/////////////////////////////////////w==\",\"pc\":\"/////////////////////////w==\",\"rat\":[1.5,1e-300],\"hn\":[0,3],\"hop\":1e-9,\"dur\":1e-9,\"f0\":1e-300,\"pitched\":1}",
    "{\"np\":2,\"nf\":4,\"a\":\"/////////w==\",\"rat\":[1.5,2],\"hn\":[1,2],\"hop\":1e308,\"dur\":1e308,\"f0\":1e308,\"pitched\":true}", "5", "[]", "null", "\"AAAA\""};
  int ri = 0; for (const char* r : rss) { hostile("resynth", rp + ",\"rsData\":" + r, "rs " + std::to_string(ri++)); hostile("resynth", "\"resynth\":{\"orig\":1,\"freeze\":1,\"speed\":8,\"pos\":0.999,\"fkeep\":1},\"rsData\":" + std::string(r), "rs freeze " + std::to_string(ri), 44100, 0.1); }
  const std::string bp = "\"bytebeat\":{\"mode\":1}";
  std::vector<std::string> bbs = {"", "   ", "t", "t*", "t/0", "t%0", "1/0%0", "(" + std::string(41, '(') + "t" + std::string(41, ')') + ")", std::string(520, 't'), "t+" + std::string(300, '1'), "t\\u00e9", "\\ud800", "\\u00a0", "\\u0000", "t>>31<<31", "t<<99>>99", "0x7fffffff*t", "-2147483648/-1", "-2147483648%-1", "sqrt(-1)",
    "rnd()", "min(t)", "max(t,t,t)", "sin(t,t)", "t?t:", "t?t:t?t:t?t:t", "tri(0x80000000)", "abs(-2147483648)"};
  for (size_t i = 0; i < bbs.size(); i++) hostile("bytebeat", bp + ",\"bbExpr\":\"" + bbs[i] + "\"", "bb " + std::to_string(i));
  hostile("bytebeat", bp + ",\"bbExpr\":5", "bb number"); hostile("bytebeat", bp, "bb missing");
  hostile("bytebeat", "\"bytebeat\":{\"rate\":1e308,\"loop\":1e308,\"start\":1e308,\"a\":1e308,\"bits\":-1e308,\"smooth\":1e308}", "bb all extreme");
  hostile("bytebeat", "\"bytebeat\":{\"rate\":48000}", "bb tiny sample rate", 1.0, 5); hostile("bytebeat", "\"bytebeat\":{\"rate\":48000}", "bb sr 0.001", 0.001, 5);
  hostile("vector", "\"vector\":{\"mode\":1,\"sdiv\":0,\"stime\":1e-300,\"steps\":16}", "vector stime tiny"); hostile("vector", "\"vector\":{\"mode\":1,\"sdiv\":0,\"stime\":1e308}", "vector stime huge");
  hostile("vector", "\"vector\":{\"mode\":1,\"sdiv\":0,\"stime\":10,\"lstart\":16,\"steps\":2,\"sloop\":1}", "vector loop point past the steps");
  hostile("scanned", "\"scanned\":{\"urate\":4000,\"rexc\":1e-300,\"masses\":1e308}", "scanned rexc tiny"); hostile("scanned", "\"scanned\":{\"urate\":4000,\"rexc\":1e308}", "scanned rexc huge");
  // ---- combined fields: several parameters at once from the extreme list (the single-field loop above cannot reach a failure that needs two of them, e.g. sequence mode + unison + spread + loop), a per-render time limit.
  // The verifier's case is a named regression: spread 1e308 makes the unison lag inf * 0 = NaN for voice 0, a NaN step (the JS reads undefined and skips it).
  hostile("vector", "\"vector\":{\"mode\":1,\"unison\":2,\"spread\":1e308,\"sloop\":1,\"sdiv\":1}", "REGRESSION vector seq + unison 2 + spread 1e308 + loop (NaN step)", 48000, 1);
  hostile("vector", "\"vector\":{\"mode\":1,\"unison\":7,\"spread\":1e308,\"sloop\":0,\"sdiv\":4,\"sgate\":1,\"sfenv\":1}", "REGRESSION vector seq no loop NaN step", 48000, 1);
  { uint32_t sd = 0x9e3779b9u; auto rn = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) & 0xffff; };
    const char* env = getenv("FUZZ_COMBINED"); const int total = env ? std::atoi(env) : 600; int done = 0;
    static const double sr[] = {2000, 8000, 44100};
    while (done < total) for (const char* kind : kinds) {
      if (done >= total) break;
      Json P = R.obj(kind); std::vector<std::string> names; for (size_t pi = 0; pi < 80 && !P.valAt(pi).missing(); pi++) names.push_back(std::string(P.keyAt(pi)));
      std::string body; const int nf = 2 + (int)(rn() % 6);
      for (int k = 0; k < nf; k++) { char tmp[96]; std::snprintf(tmp, sizeof tmp, "%.17g", vals[rn() % 9]); body += (body.empty() ? "" : ",") + std::string("\"") + names[rn() % names.size()] + "\":" + tmp; }
      if (!strcmp(kind, "vector") && rn() % 2) body += ",\"mode\":1,\"unison\":" + std::to_string(1 + rn() % 7) + ",\"sloop\":" + std::to_string(rn() % 2) + ",\"sdiv\":" + std::to_string(rn() % 12) + (rn() % 2 ? ",\"spread\":1e308" : "");
      if (!strcmp(kind, "table")) body += ",\"table\":4";
      if (!strcmp(kind, "scanned") && rn() % 2) body += ",\"path\":" + std::to_string(rn() % 3) + ",\"ends\":" + std::to_string(rn() % 2) + ",\"rexc\":" + std::to_string(rn() % 3);
      const std::string js = std::string("{\"engine\":\"") + kind + "\",\"" + kind + "\":{" + body + "}" + (rn() % 3 == 0 ? ",\"tune\":-1000" : "") + "}";
      XengVoice V; if (!fromJson(V, js)) return 1;
      const int before = fails; const auto t0 = std::chrono::steady_clock::now(); renderIt(V, sr[rn() % 3], (rn() % 3) * 0.05, "combined " + js);
      if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > 20) { fails++; std::printf("FAIL: over the time limit: %s\n", js.c_str()); }
      (void)before; done++;
    }
    std::printf("engine xeng2 fuzz: %d combined renders\n", done); }
  // the over-long and hostile directly: an engine whose voice was never filled
  { XengVoice V; V.kind = XKind::Resynth; EngRenderOpts o; size_t n = 0; float t[8]; const int st = xeng_render_voice(V, o, t, 8, &n); if (st != kEngTooSmall && st != kEngOk) { fails++; std::printf("FAIL: an empty resynth voice: status %d\n", st); } }
  std::printf("engine xeng2 fuzz: %d renders, %d refused, %d failed\n", renders, refused, fails);
  if (!fails) std::printf("PASS test_engine_xeng2_fuzz\n");
  return fails ? 1 : 0;
}
