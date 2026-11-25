#include "engine/padfx.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "fx/fxhost.h"
#include "modfx/modfx.h"
#include "rt/jsmath.h"
#include "song/song.h"
#ifdef THUNDER_HAVE_TFX
#include "tfx.h"
#endif

namespace thunder {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline double jmn(double a, double b) { return (a != a || b != b) ? kNaN : (a < b ? a : b); }   // Math.min (NaN wins)
inline double jmx(double a, double b) { return (a != a || b != b) ? kNaN : (a > b ? a : b); }   // Math.max
inline double sq01(double v) { return std::sqrt(jmx(0, jmn(1, v))); }
inline double i32(double v) { return (double)js_to_int32(v); }                                 // `v | 0`
inline double sgn(double v) { return v != v ? kNaN : v > 0 ? 1 : v < 0 ? -1 : v; }              // Math.sign
constexpr double kSpaceSizeMax = 0.99;                                                           // SPACE_SIZE_MAX
inline double truthy(double v) { return (v == v && v != 0) ? 1 : 0; }                            // `v ? 1 : 0`

enum Cls : uint8_t { kTx, kTxRaw, kCrush, kChorus, kMod };
typedef void (*MapFn)(const double* f, double sr, double bpm, double* p);
struct Def {
  const char* key; Cls cls; int id; int n; const char* keys[12]; MapFn map; int blend;   // blend: index of the REVERB MIX value, -1 none
};
#define M(...) [](const double* f, double sr, double bpm, double* p) { (void)f; (void)sr; (void)bpm; __VA_ARGS__ }
const Def kDefs[] = {
  {"eq", kTx, 6, 8, {"bass", "mid", "treble", "bfreq", "tfreq", "lowpass", "highpass", "output"},
    M(p[0] = (f[2] + 12) / 24; p[1] = (f[1] + 12) / 24; p[2] = (f[0] + 12) / 24; p[3] = sq01((f[5] - 1) / 15); p[4] = sq01((f[4] - 1) / 15); p[5] = sq01((f[3] - 30) / 1570);
      p[6] = f[6] <= 20.5 ? 0 : sq01((f[6] - 30) / 1570); p[7] = (f[7] + 18) / 36;), -1},
  {"comp", kTx, 7, 5, {"pressure", "speed", "mewiness", "output", "mix"}, M(p[0] = f[0]; p[1] = f[1]; p[2] = (f[2] + 1) / 2; p[3] = f[3]; p[4] = f[4];), -1},
  {"reverb", kTx, 8, 5, {"replace", "brightness", "detune", "bigness", "mix"}, M(p[0] = f[0]; p[1] = f[1]; p[2] = f[2]; p[3] = f[3]; p[4] = 1;), 4},
  {"delay", kTx, 9, 6, {"time", "regen", "tone", "reso", "flutter", "mix"},
    M(p[0] = js_pow((jmx(1, jmn(26, 88200 / (f[0] / 1000 * sr))) - 1) / 25, 0.25); p[1] = f[1]; p[2] = f[2]; p[3] = f[3]; p[4] = f[4]; p[5] = f[5];), -1},
  {"beads", kTx, 10, 10, {"time", "size", "pitch", "shape", "density", "feedback", "blend", "reverb", "media", "freeze"},
    M(p[0] = i32(f[8]); p[1] = 0; p[2] = truthy(f[9]); p[3] = f[0]; p[4] = jmn(f[1], .995); p[5] = f[2]; p[6] = f[3]; p[7] = f[4]; p[8] = f[5]; p[9] = f[6]; p[10] = f[7]; p[11] = 0;), -1},
  {"flanger", kTx, 11, 5, {"depth", "rate", "delay", "fb", "mix"}, M(for (int i = 0; i < 5; i++) p[i] = f[i];), -1},
  {"phaser", kTx, 12, 6, {"depth", "rate", "freq", "fb", "poles", "mix"}, M(for (int i = 0; i < 6; i++) p[i] = f[i];), -1},
  {"tremolo", kTx, 13, 3, {"rate", "depth", "shape"}, M(p[0] = f[0]; p[1] = f[1]; p[2] = i32(f[2]);), -1},
  {"odrive", kTx, 14, 4, {"drive", "tone", "out", "mix"}, M(for (int i = 0; i < 4; i++) p[i] = f[i];), -1},
  {"folder", kTx, 15, 4, {"gain", "offset", "out", "mix"}, M(for (int i = 0; i < 4; i++) p[i] = f[i];), -1},
  {"autowah", kTx, 16, 3, {"wah", "level", "mix"}, M(for (int i = 0; i < 3; i++) p[i] = f[i];), -1},
  {"pshift", kTx, 17, 4, {"semis", "win", "spray", "mix"}, M(for (int i = 0; i < 4; i++) p[i] = f[i];), -1},
  {"ladder", kTx, 18, 5, {"cut", "res", "drive", "mode", "mix"}, M(p[0] = f[0]; p[1] = f[1]; p[2] = f[2]; p[3] = i32(f[3]); p[4] = f[4];), -1},
  {"xmod", kTx, 19, 7, {"alg", "timbre", "note", "clvl", "mlvl", "mix", "carrier"}, M(p[0] = f[0]; p[1] = f[1]; p[2] = f[3]; p[3] = f[4]; p[4] = i32(f[6]); p[5] = f[2]; p[6] = f[5];), -1},
  {"space", kTx, 20, 8, {"size", "decay", "diff", "mod", "tone", "lowcut", "pre", "mix"},
    M(p[0] = jmn(f[0], kSpaceSizeMax); p[1] = f[1]; p[2] = f[2]; p[3] = f[3]; p[4] = f[4]; p[5] = f[5]; p[6] = f[6]; p[7] = 1;), 7},
  {"lofi", kTx, 21, 4, {"rate", "rez", "hard", "mix"}, M(for (int i = 0; i < 4; i++) p[i] = f[i];), -1},
  {"drive", kTx, 22, 1, {"drive"}, M(p[0] = f[0];), -1},
  {"air", kTx, 23, 2, {"air", "gnd"}, M(p[0] = f[0]; p[1] = f[1];), -1},
  {"room", kTx, 24, 4, {"size", "sustain", "mulch", "wet"}, M(p[0] = f[0]; p[1] = f[1]; p[2] = f[2]; p[3] = 1;), 3},
  {"comb", kTx, 25, 5, {"note", "fb", "damp", "spread", "mix"}, M(p[0] = 440 * js_pow(2, (f[0] - 69) / 12); p[1] = f[1]; p[2] = f[2]; p[3] = f[3]; p[4] = f[4];), -1},
  {"transient", kTx, 26, 4, {"att", "sus", "out", "mix"}, M(for (int i = 0; i < 4; i++) p[i] = f[i];), -1},
  {"tgate", kTx, 27, 5, {"len", "depth", "smooth", "div", "pat"},
    M(static const double kDiv[6] = {0.125, 0.16666667, 0.25, 0.33333333, 0.5, 1}; static const double kPat[9] = {65535, 21845, 43690, 18761, 22235, 54749, 4879, 4353, 5193};
      const double d = i32(f[3]), q = i32(f[4]);   // [..][(f.div | 0)]: out of range = undefined
      p[0] = (d >= 0 && d < 6) ? bpm / 60 / kDiv[(int)d] : kNaN; p[1] = f[0]; p[2] = f[1]; p[3] = f[2]; p[4] = (q >= 0 && q < 9) ? kPat[(int)q] : kNaN; p[5] = 16; p[6] = 0; p[7] = 0;), -1},
  {"ott", kTx, 28, 6, {"depth", "time", "lo", "mid", "hi", "out"}, M(for (int i = 0; i < 6; i++) p[i] = f[i];), -1},
  {"fshift", kTx, 29, 3, {"shift", "fb", "mix"}, M(p[0] = sgn(f[0]) * f[0] * f[0] * 2000; p[1] = f[1]; p[2] = f[2];), -1},
  {"vinyl", kTx, 30, 4, {"mlow", "slow", "hlim", "wear"}, M(for (int i = 0; i < 4; i++) p[i] = f[i];), -1},
  {"dub", kTx, 31, 2, {"amt", "mix"}, M(p[0] = f[0]; p[1] = f[1];), -1},
  {"cab", kTx, 32, 6, {"int", "room", "size", "axis", "out", "model"}, M(p[0] = (i32(f[5]) + 0.5) / 6; p[1] = f[0]; p[2] = f[1]; p[3] = f[2]; p[4] = f[3]; p[5] = f[4];), -1},
  {"tube", kTx, 33, 2, {"input", "tube"}, M(p[0] = f[0]; p[1] = f[1];), -1},
  // thunder-dsp.wasm effects with a tfx twin: the same Airwindows classes (density id 2, tape id 3), or DaisySP's Chorus / Decimator (ids 4, 5) used as the page's wrapper uses them
  {"density", kTxRaw, 2, 4, {"drive", "highpass", "output", "mix"}, M(p[0] = 0.2 + f[0] * 0.8; p[1] = f[1]; p[2] = f[2]; p[3] = f[3];), -1},
  {"tape", kTxRaw, 3, 6, {"input", "soften", "bump", "flutter", "output", "mix"}, M(for (int i = 0; i < 6; i++) p[i] = f[i];), -1},
  {"chorus", kChorus, 4, 5, {"depth", "rate", "delay", "feedback", "mix"}, M(p[0] = f[0]; p[1] = f[1]; p[2] = f[2]; p[3] = f[3]; p[4] = 1;), -1},
  {"crush", kCrush, 5, 4, {"down", "bits", "mix", "smooth"}, M(p[0] = f[0]; p[1] = f[1]; p[2] = truthy(f[3]); p[3] = 1;), -1},
};
#undef M
constexpr int kNTx = (int)(sizeof kDefs / sizeof kDefs[0]);

// the MODFX effects that are pad effects (pad: true in the page's MODFX block)
const char* const kModKeys[] = {"spring", "plate", "lverb", "ensemble", "dimension", "bbd", "harm", "flex", "sgate", "retrig", "spectral", "stretch", "convolve", "autotune", "vowel", "rotary",
  "gdelay", "reverse", "sdelay", "filter", "cut", "compressor", "buscomp", "glue", "ngate", "limiter", "ringmod", "envfilt"};
constexpr int kNMod = (int)(sizeof kModKeys / sizeof kModKeys[0]);
constexpr int kMaxVals = 24;

bool haveTfx() {
#ifdef THUNDER_HAVE_TFX
  return true;
#else
  return false;
#endif
}

// a saved value as the page's maps see it
double valueOf(Json v) {
  if (v.isNum()) return v.asNum();
  if (v.isBool()) return v.asBool() ? 1 : 0;
  if (v.isNull()) return 0;
  if (v.isStr()) return song_js_plus(v);
  return kNaN;
}
// MODFX: the key an effect object keeps a module parameter under (fxKey)
std::string modKey(const char* p) { std::string s(p); return (s == "type" || s == "on") ? s + "_" : s; }

int findIndex(std::string_view t) {
  for (int i = 0; i < kNTx; i++) if (t == kDefs[i].key) return i;
  for (int i = 0; i < kNMod; i++) if (t == kModKeys[i]) return kNTx + i;
  return -1;
}
}  // namespace

int pad_fx_count() { return kNTx + kNMod; }
const char* pad_fx_key(int i) { return i < 0 ? nullptr : i < kNTx ? kDefs[i].key : i < kNTx + kNMod ? kModKeys[i - kNTx] : nullptr; }
int pad_fx_nvals(int i) { if (i < 0) return 0; if (i < kNTx) return kDefs[i].n; if (i >= kNTx + kNMod) return 0; const modfx::Def* d = modfx::find(kModKeys[i - kNTx]); return d ? d->np : 0; }
const char* pad_fx_valkey(int i, int k) { if (i >= 0 && i < kNTx && k >= 0 && k < kDefs[i].n) return kDefs[i].keys[k]; return nullptr; }
bool pad_fx_map(int i, const double* v, double sr, double bpm, double* p12) { if (i < 0 || i >= kNTx) return false; for (int k = 0; k < 12; k++) p12[k] = 0; kDefs[i].map(v, sr, bpm, p12); return true; }

void pad_fx_parse(EngFx& f, Json j) {
  const int i = findIndex(j.str("type")); if (i < 0) return;
  if (i < kNTx && !haveTfx()) return;
  f.lib = i; f.kind = EngFx::Kind::Lib;
  for (double& x : f.lv) x = kNaN;
  if (i < kNTx) { for (int k = 0; k < kDefs[i].n; k++) f.lv[k] = valueOf(j.get(kDefs[i].keys[k])); return; }
  const modfx::Def* d = modfx::find(kModKeys[i - kNTx]);
  if (!d || d->np > kMaxVals) { f.kind = EngFx::Kind::Other; f.lib = -1; return; }
  for (int k = 0; k < d->np; k++) { Json v = j.get(modKey(d->params[k].name)); f.lv[k] = v.isNum() ? v.asNum() : kNaN; }   // io.p: a finite number, else the module's default
  { Json m = j.get("mix"); f.mix = m.isNum() ? m.asNum() : kNaN; }                                                             // the latency rule reads f.mix
}

namespace {
inline float fl(double v) { return (float)v; }

#ifdef THUNDER_HAVE_TFX
extern "C" void thunder_wasi_rand_use(uint64_t* state);
extern "C" void thunder_fx_state_use(uint32_t* stmlib_rng, uint32_t* daisy_myrand);
// The effect sources' statics (libc rand, stmlib's generator, DaisySP's myrand) are per wasm instance on the page and per process here: each pad effect gets its own set at the values of
// a fresh instance, as the track host gives each host (fx/fxhost.cpp ModuleState). (The page shares ONE tfx instance among all pads of a render worker, so its pad effects depend on what ran
// before; the first use of a fresh instance is the one reproducible choice.)
struct StateScope {
  uint64_t r = 0; uint32_t stm = 0x21, daisy = 1;
  StateScope() { thunder_wasi_rand_use(&r); thunder_fx_state_use(&stm, &daisy); }
  ~StateScope() { thunder_wasi_rand_use(nullptr); thunder_fx_state_use(nullptr, nullptr); }
};
// txRun (dither = true, clip = true, mono = (L + R) / 2) and the raw wrappers (aw_density, aw_totape: the left side as it is)
bool runFx(const Def& d, const double* v, const float* a, size_t n, double sr, double bpm, std::vector<float>& out, bool txMode) {
  StateScope ss; std::unique_ptr<Fx> fx(fx_make_tfx(d.id, (float)sr)); if (!fx) return false;
  double p[12]; for (int k = 0; k < 12; k++) p[k] = 0; d.map(v, sr, bpm, p);
  for (int k = 0; k < 12; k++) fx->p[k] = fl(p[k]);
  out.assign(n, 0.0f);
  float L[128], R[128];
  for (size_t off = 0; off < n; off += 128) {
    const int m = (int)(n - off < 128 ? n - off : 128);
    std::memset(L, 0, sizeof L); std::memset(R, 0, sizeof R);
    for (int i = 0; i < m; i++) {
      double x = a[off + i];
      if (txMode) { x = (x > -8 && x < 8 ? x : 0) + ((i & 1) ? 1e-18 : -1e-18); }   // no NaN / infinity into the wasm; +-1e-18 against denormals
      L[i] = R[i] = fl(x);
    }
    fx->process(L, R, m);
    if (txMode) for (int i = 0; i < m; i++) { if (!(L[i] > -8.f && L[i] < 8.f)) L[i] = 0.f; if (!(R[i] > -8.f && R[i] < 8.f)) R[i] = 0.f; }   // tfx_process's guard
    for (int i = 0; i < m; i++) out[off + i] = txMode ? fl(((double)L[i] + (double)R[i]) * 0.5) : L[i];
  }
  return true;
}
// the pad wrappers with a twin in tfx: Decimator (crush), Chorus
bool runCrushChorus(const Def& d, const double* v, const float* a, size_t n, double sr, double bpm, std::vector<float>& out) {
  const bool cr = d.cls == kCrush; const double mix = v[cr ? 2 : 4];
  out.assign(n, 0.0f);
  if (cr) {
    std::vector<float> w; if (!runFx(d, v, a, n, sr, bpm, w, false)) return false;
    for (size_t i = 0; i < n; i++) out[i] = fl((double)a[i] * (1 - mix) + (double)w[i] * mix); return true; }
  // chorus: the wasm wrapper gives the dry-free left and right (x 1.4 in the tfx twin); the pad takes their mean at the page's 1.6 gain. Run again for the right side.
  StateScope ss; std::unique_ptr<Fx> fx(fx_make_tfx(d.id, (float)sr)); if (!fx) return false;
  double p[12] = {0}; d.map(v, sr, bpm, p); for (int k = 0; k < 12; k++) fx->p[k] = fl(p[k]);
  // `chunked`: 4032 samples per call (the last one rounded up to 96 with zeros), the wrapper sets the knobs once per call, and DaisySP's Chorus is not the same when they are set every 128 samples
  std::vector<float> L(4032), R(4032);
  for (size_t off = 0; off < n; off += 4032) {
    const int k = (int)(n - off < 4032 ? n - off : 4032), m = std::min(4032, (k + 95) / 96 * 96);
    std::fill(L.begin(), L.end(), 0.0f); std::fill(R.begin(), R.end(), 0.0f);
    for (int i = 0; i < k; i++) L[i] = R[i] = a[off + i];
    fx->process(L.data(), R.data(), m);
    for (int i = 0; i < k; i++) { const double wet = ((double)L[i] + (double)R[i]) * 0.5 / 1.4f;
      out[off + i] = fl((double)a[off + i] * (1 - mix) + wet * mix * 1.6); }
  }
  return true;
}
#endif

// modFxRun
bool runMod(int mi, const EngFx& f, const float* a, size_t n, double sr, double bpm, std::vector<float>& out) {
  const modfx::Def* def = modfx::find(kModKeys[mi]); if (!def) return false;
  if (def->np > kMaxVals || def->nin > modfx::kMaxPorts || def->nout > modfx::kMaxPorts || def->nout == 0) return false;
  const int B = modfx::kBlock, lat = std::strcmp(def->type, "spectral") == 0 ? 2048 : 0;   // the one pad module with `latency` (SPX_N)
  const bool fullyWet = !(f.mix == f.mix) || f.mix >= 0.999;                                // f.mix == null || f.mix >= 0.999
  const size_t shift = (lat && fullyWet) ? (size_t)lat : 0, n0 = ((n + shift + B - 1) / B) * (size_t)B;
  std::vector<float> buf((size_t)(def->nin + def->nout + 1) * B, 0.0f);
  modfx::IO io; double p[kMaxVals];
  float* z = buf.data(); int inL = -1, inR = -1, outL = -1, outR = -1;
  for (int k = 0; k < def->nin; k++) { io.in[k] = z + (size_t)k * B; io.linked[k] = false; if (!std::strcmp(def->in[k].name, "in")) inL = k; else if (!std::strcmp(def->in[k].name, "inR")) inR = k; }
  if (inR >= 0) io.linked[inR] = true;
  for (int k = 0; k < def->nout; k++) { io.out[k] = z + (size_t)(def->nin + k) * B; if (!std::strcmp(def->out[k].name, "out")) outL = k; else if (!std::strcmp(def->out[k].name, "outR")) outR = k; }
  for (int k = 0; k < def->np; k++) p[k] = (f.lv[k] == f.lv[k] && std::isfinite(f.lv[k])) ? f.lv[k] : def->params[k].def;
  io.p = p;
  modfx::Ctx c; c.sr = sr; c.block = B; c.full = true; c.tempo = bpm; c.lpb = 4; c.ticks = 6; c.playing = true; c.startN = 1; c.key = nullptr; c.irs = nullptr;
  modfx::SeedCounter seeds(12345);
  std::unique_ptr<modfx::Module> mod = def->make(sr, seeds); if (!mod) return false;
  out.assign(n, 0.0f);
  const float* oL = io.out[outL >= 0 ? outL : 0]; const float* oR = outR >= 0 ? io.out[outR] : oL;
  for (size_t o = 0; o < n0; o += (size_t)B) {
    for (int i = 0; i < B; i++) { const float v = (o + i < n) ? a[o + i] : 0.0f; if (inL >= 0) z[(size_t)inL * B + i] = v; if (inR >= 0) z[(size_t)inR * B + i] = v; }
    c.fail = false;
    if (!mod->run(io, c) || c.fail) return false;   // the JS's catch: the sound as it was
    for (int i = 0; i < B; i++) {
      const size_t j = o + i; if (j < shift || j - shift >= n) continue;
      const double y = ((double)oL[i] + (double)oR[i]) * 0.5; out[j - shift] = (y > -8 && y < 8) ? fl(y) : 0.0f;
    }
  }
  return true;
}
}  // namespace

bool pad_fx_lib(const EngFx& f, const float* a, size_t n, double sr, double bpm, std::vector<float>& out) {
  if (f.kind != EngFx::Kind::Lib || f.lib < 0 || f.lib >= kNTx + kNMod) return false;
  if (n == 0) { out.clear(); return true; }
  if (f.lib >= kNTx) return runMod(f.lib - kNTx, f, a, n, sr, bpm, out);
#ifdef THUNDER_HAVE_TFX
  const Def& d = kDefs[f.lib];
  if (d.cls == kCrush || d.cls == kChorus) return runCrushChorus(d, f.lv, a, n, sr, bpm, out);
  if (!runFx(d, f.lv, a, n, sr, bpm, out, d.cls == kTx)) return false;
  if (d.blend >= 0) {   // REVERB MIX: the wasm runs fully wet, the pad blends it with rvMix
    const double am = (f.lv[d.blend] < 0 ? 0 : f.lv[d.blend] > 1 ? 1 : f.lv[d.blend]) * 3.14159265358979323846 / 2, s = js_sin(am), dr = js_cos(am), w = s * s;
    for (size_t i = 0; i < n; i++) out[i] = fl((double)a[i] * dr + (double)out[i] * w);
  }
  return true;
#else
  (void)bpm; return false;
#endif
}

}  // namespace thunder
