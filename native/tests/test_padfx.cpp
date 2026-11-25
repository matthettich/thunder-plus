// PFX: the library pad effects (engine/padfx.cpp) against the page's own applyFx / padFx (tools/padfx-dump.mjs runs them in Node with tfx.wasm, thunder-dsp.wasm and the shared
// MODFX modules). Per case: the RMS of the difference against the RMS of the page's result (gate -100 dB; "exact" where every float32 is equal), the committed FNV hash of the page's result.
//   test_padfx <dir>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/engine_json.h"
#include "engine/padfx.h"
#include "engine/xeng.h"
#include "rt/json.h"
#include "golden/padfx_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static int fails = 0;

static std::vector<float> floats(const std::string& s) { std::vector<float> v(s.size() / 4); if (!v.empty()) std::memcpy(v.data(), s.data(), v.size() * 4); return v; }

// padFx: the sound in a buffer N + tail long, each effect in order, then the trim and the 2 ms fade (index.html padFx)
static std::vector<float> padFx(const std::vector<EngFx>& fx, const std::vector<float>& a, double sr, double tail, double bpm, bool* ok) {
  const size_t N = a.size(), ext = (size_t)std::min((double)N + std::round(tail * sr), sr * 312);
  std::vector<float> buf(ext, 0.0f); std::memcpy(buf.data(), a.data(), std::min(N, ext) * 4);
  for (const EngFx& f : fx) { std::vector<float> r; if (!pad_fx_lib(f, buf.data(), buf.size(), sr, bpm, r)) { *ok = false; return buf; } buf = r; }
  size_t end = buf.size(); while (end > 1 && std::fabs(buf[end - 1]) < 1e-4) end--;
  buf.resize(std::max<size_t>(end, 1)); const size_t fl = std::min(buf.size(), (size_t)std::llround(sr * 0.002));
  for (size_t i = 0; i < fl; i++) buf[buf.size() - 1 - i] = (float)((double)buf[buf.size() - 1 - i] * ((double)i / (double)fl));
  return buf;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("FAIL: no reference directory (ctest runs tools/padfx-dump.mjs first)\n"); return 1; }
  const std::string dir = argv[1];
  int exact = 0, ran = 0; double worst = -1e9; std::string worstName;
  for (int g = 0; g < kPadFxGoldenN; g++) {
    const PadFxGolden& G = kPadFxGolden[g]; std::string js, in, ref;
    if (!readFile(dir + "/" + G.name + ".json", &js) || !readFile(dir + "/" + G.name + ".in.f32", &in) || !readFile(dir + "/" + G.name + ".f32", &ref)) { std::printf("%-26s MISSING reference\n", G.name); fails++; continue; }
    JsonDoc doc; if (!doc.parse(js)) { std::printf("%-26s bad json\n", G.name); fails++; continue; }
    Json c = doc.root(); const double sr = c.num("sr", 48000), bpm = c.num("bpm", 120);
    const std::vector<float> a = floats(in), want = floats(ref);
    std::vector<float> got; bool ok = true;
    if (std::string(c.str("kind")) == "voice" || std::string(c.str("kind")) == "electro") {   // renderVoice: layers, pad effects, trim, fade
      Json vj = c.get("voice"); EngVoice* V = new EngVoice; EngRenderOpts o; o.sr = sr; o.hold = 0; o.bpm = bpm;
      const std::string_view en = vj.str("engine");
      if (!en.empty() && en != "layers") {   // an X engine pad (ELECTRO's Machine voice): padFx runs only for the engines that are not live
        XengVoice* X = new XengVoice; ok = xeng_voice_from_json(*X, vj, vj.arr("fx"));
        if (ok) { const size_t bound = xeng_render_bound(*X, o); got.assign(bound, 0.0f); size_t n = 0; ok = bound && xeng_render_voice(*X, o, got.data(), bound, &n) == kEngOk; got.resize(n); }
        delete X; delete V;
      } else {
      ok = eng_voice_from_json(*V, vj, vj.arr("layers"), vj.arr("fx"));
      if (ok) { const size_t bound = eng_render_bound(*V, o); got.assign(bound, 0.0f); size_t n = 0; ok = bound && eng_render_voice(*V, o, got.data(), bound, &n) == kEngOk; got.resize(n); }
      delete V; }
    } else if (std::string(c.str("kind")) == "fx") {
      EngFx f; eng_fx_from_json(f, c.get("fx"));
      if (f.kind != EngFx::Kind::Lib) { std::printf("%-26s not hosted (kind %d)\n", G.name, (int)f.kind); fails++; continue; }
      ok = pad_fx_lib(f, a.data(), a.size(), sr, bpm, got);
    } else {
      Json l = c.arr("fxs"); std::vector<EngFx> fx(l.size());
      for (size_t i = 0; i < l.size(); i++) { eng_fx_from_json(fx[i], l.at(i)); if (fx[i].kind != EngFx::Kind::Lib) ok = false; }
      if (ok) got = padFx(fx, a, sr, c.num("tail", 0), bpm, &ok);
    }
    if (!ok) { std::printf("%-26s native effect could not run\n", G.name); fails++; continue; }
    ran++;
    if (got.size() != want.size()) { std::printf("%-26s FAIL length %zu vs %zu\n", G.name, got.size(), want.size()); fails++; continue; }
    double e2 = 0, r2 = 0; bool same = true;
    for (size_t i = 0; i < got.size(); i++) { const double d = (double)got[i] - (double)want[i]; e2 += d * d; r2 += (double)want[i] * (double)want[i]; if (!(got[i] == want[i] || (std::isnan(got[i]) && std::isnan(want[i])))) same = false; }
    if (!same && std::getenv("PADFX_DBG") && std::strstr(G.name, std::getenv("PADFX_DBG"))) { int k = 0; for (size_t i = 0; i < got.size() && k < 8; i++) if (got[i] != want[i]) { std::printf("   [%zu] got %.9g want %.9g in %.9g\n", i, got[i], want[i], i < a.size() ? a[i] : 0.f); k++; } }
    if (std::getenv("PADFX_OUT")) { std::ofstream o(std::string(std::getenv("PADFX_OUT")) + "/" + G.name + ".got.f32", std::ios::binary); o.write((const char*)got.data(), got.size() * 4); }
    const double db = same ? -999 : 10 * std::log10(e2 / std::max(r2, 1e-30));
    const bool hashOk = fnv(want.data(), want.size()) == G.hash && (int)want.size() == G.floats;
    const double gate = std::strstr(G.name, "pshift") ? -80.0 : -100.0;   // the pitch shifter's two crossfaded taps: tfx.wasm is built with -ffast-math, the host without (test_fx.cpp: e_pshift_def -68.5 dB there); -85.2 / -96.7 measured here
    const bool pass = db <= gate && hashOk;
    if (same) exact++; else if (db > worst) { worst = db; worstName = G.name; }
    char b[24]; if (same) std::snprintf(b, sizeof b, "exact"); else std::snprintf(b, sizeof b, "%.1f dB", db);
    std::printf("%-26s %-10s%s%s\n", G.name, b, hashOk ? "" : "  [committed hash disagrees with the reference run]", pass ? "" : "  FAIL");
    if (!pass) fails++;
  }
  std::printf("padfx: %d cases run, %d bit-exact, worst %s (%s), %d failed (gate -100 dB RMS, pshift -80)\n", ran, exact, worst < -900 ? "exact" : (std::to_string(worst) + " dB").c_str(), worstName.c_str(), fails);
  return fails ? 1 : 0;
}
