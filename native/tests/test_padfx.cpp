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
#include "fx/fxhost.h"
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

// Hostile saved values (a file can hold anything the page's UI cannot make): every hosted type with huge, negative, zero, non-finite-looking and wrong-typed numbers must come back (run or
// refuse), finite where the page's own rule says so, and never crash or hang.
static int fuzz() {
  int bad = 0, runs = 0; std::vector<float> a(6000); for (size_t i = 0; i < a.size(); i++) a[i] = (float)(0.4 * std::sin(0.05 * (double)i) * std::exp(-(double)i / 2500));
  const char* vals[] = {"1e30", "-1e30", "0", "-1", "1e-30", "\"x\"", "null", "true", "[1]", "7", "0.5"};
  for (int t = 0; t < pad_fx_count(); t++) for (const char* v : vals) {
    std::string j = std::string("{\"type\":\"") + pad_fx_key(t) + "\",\"on\":true";
    for (int k = 0; k < 24; k++) { const char* key = pad_fx_valkey(t, k); if (key) j += std::string(",\"") + key + "\":" + v; }
    // a module's own parameter names: a few common ones
    for (const char* key : {"mix", "time", "size", "decay", "rate", "depth", "tone", "pre", "feedback", "fb", "threshold", "ratio", "freq", "q", "cutoff", "amount", "semis", "len", "div", "mode", "wet", "gain"}) j += std::string(",\"") + key + "\":" + v;
    j += "}";
    JsonDoc d; if (!d.parse(j)) { std::printf("fuzz: bad json for %s\n", pad_fx_key(t)); bad++; continue; }
    EngFx f; eng_fx_from_json(f, d.root()); if (f.kind != EngFx::Kind::Lib) continue;
    std::vector<float> out; pad_fx_lib(f, a.data(), a.size(), 48000, 120, out); runs++;
    if (!out.empty() && out.size() != a.size()) { std::printf("fuzz: %s with %s changed the length\n", pad_fx_key(t), v); bad++; }
    for (float x : out) if (!std::isfinite(x)) { std::printf("fuzz: %s with %s: non-finite output\n", pad_fx_key(t), v); bad++; break; }
  }
  // hand-built EngFx (no parser, no clamp before pad_fx_lib): every control hostile, then one at a time against its default, beads with Tail = Hold
  {
    const double hv[] = {1e300, -1e300, 0.0, -1.0, 1e-300, 1e9, 7.0, 0.99, 1.0};
    for (int t = 0; t < pad_fx_count(); t++) {
      EngFx base; { std::string j = std::string("{\"type\":\"") + pad_fx_key(t) + "\"}"; JsonDoc d; d.parse(j); eng_fx_from_json(base, d.root()); }
      if (base.kind != EngFx::Kind::Lib) continue;
      const int nv = pad_fx_nvals(t);
      for (int k = 0; k < nv; k++) for (double x : hv) for (int fr = 0; fr < 2; fr++) {
        EngFx f = base; for (int q = 0; q < nv; q++) f.lv[q] = 0.5;   // a value inside every range
        f.lv[k] = x; if (!std::strcmp(pad_fx_key(t), "beads") && fr) { f.lv[9] = 1; f.lv[1] = x; }
        std::vector<float> out; pad_fx_lib(f, a.data(), a.size(), 48000, 120, out); runs++;
        for (float y : out) if (!std::isfinite(y)) { std::printf("fuzz: %s control %d = %g: non-finite output\n", pad_fx_key(t), k, x); bad++; break; }
      }
    }
  }
  std::printf("padfx fuzz: %d runs on %d types, %d bad\n", runs, pad_fx_count(), bad);
  return bad;
}

// The TRACK effect host with a Beads slot held (freeze) at its largest Size: the same upstream divide by zero (review F1) would be on the audio thread. Process 2 s; must finish, finite.
static int trackBeads() {
  int bad = 0;
  for (double size : {0.99, 0.995, 1.0}) for (int media = 0; media < 4; media++) {
    FxHost H(48000.0); FxEntry e; std::snprintf(e.key, kFxKey, "0:beads"); e.t = 10; e.slot = 0; e.on = true;
    const double p[12] = {(double)media, 0, 1, 0.2, size, 0.5, 0.5, 0.85, 0.2, 1, 0.25, 0};   // the page's TFX map order (media, 0, freeze, time, size, pitch, shape, density, feedback, blend, reverb)
    for (int k = 0; k < 12; k++) e.p[k] = p[k];
    FxPlan* pl = H.plan(&e, 1); if (!pl) { std::printf("trackBeads: no plan (no effects library)\n"); return 0; }
    H.commit(pl); delete pl;
    std::vector<float> L(128), R(128); unsigned s = 1; bool fin = true;
    for (int b = 0; b < 750; b++) { for (int i = 0; i < 128; i++) { s = s * 1664525u + 1013904223u; L[i] = R[i] = ((float)(s >> 8) / 8388608.0f - 1.0f) * 0.3f; }
      H.process(L.data(), R.data(), 128, b * 128 / 48000.0); for (int i = 0; i < 128; i++) if (!std::isfinite(L[i]) || !std::isfinite(R[i])) fin = false; }
    if (!fin) { std::printf("trackBeads: size %g media %d: non-finite output\n", size, media); bad++; }
  }
  std::printf("padfx track Beads held at size .99 / .995 / 1: %d bad\n", bad);
  return bad;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("SKIP: no reference directory (no node: tools/padfx-dump.mjs makes it)\n"); return 77; }
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
    const double gate = std::strstr(G.name, "pshift") ? -70.0 : !std::strcmp(G.name, "pad_three") ? -80.0 : -100.0;   // the pitch shifter's two crossfaded taps: tfx.wasm is built with -ffast-math, the host without (test_fx.cpp: e_pshift_def -68.5 dB there); -85.7 / -75.0 measured here. pad_three: odrive -> plate -> tape, the tape stage amplifies the odrive's last-bit difference (each alone: <= -138 dB): -84.8 measured
    double maxe = 0; for (size_t i = 0; i < got.size(); i++) maxe = std::max(maxe, std::fabs((double)got[i] - (double)want[i]));
    // Clouds hands its output over as int16 (the page's own wasm is built with -ffast-math, this host strict): a sample may land on the neighbouring step, on either channel, and the page's
    // mean of the two and its resample from 32 kHz leave at most 1.5 steps (test_fx.cpp's e_clouds_alt rule, one step per channel). Rings: fast-math rounding in a resonator bank, -88 dB measured.
    const bool clouds = std::strstr(G.name, "clouds") != nullptr;
    const bool pass = (clouds ? maxe <= 1.6 / 32768.0 : db <= ((std::strstr(G.name, "rings") ? -80.0 : std::strstr(G.name, "beads_media") ? -50.0 : gate))) && hashOk;
    if (same) exact++; else if (db > worst) { worst = db; worstName = G.name; }
    char b[24]; if (same) std::snprintf(b, sizeof b, "exact"); else std::snprintf(b, sizeof b, "%.1f dB", db);
    std::printf("%-26s %-10s%s%s\n", G.name, b, hashOk ? "" : "  [committed hash disagrees with the reference run]", pass ? "" : "  FAIL");
    if (!pass) fails++;
  }
  std::printf("padfx: %d cases run, %d bit-exact, worst %s (%s), %d failed (gate -100 dB RMS; pshift -70, rings and pad_three -80, Beads media 0 (Dirty tape) -50 (measured -58); clouds 1.6 int16 steps)\n", ran, exact, worst < -900 ? "exact" : (std::to_string(worst) + " dB").c_str(), worstName.c_str(), fails);
  fails += fuzz();
  fails += trackBeads();
  return fails ? 1 : 0;
}
