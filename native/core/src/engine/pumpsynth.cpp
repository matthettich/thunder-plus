// U33: the …pump synths hosted natively (pumpsynth.h). The render is PsynCore.render / finish / latency of pump/synth/pump-synth-engine.js, line for line.
#include "engine/pumpsynth.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "engine/xeng_util.h"
#include "rt/jsmath.h"

// pump's C interface (core/pump: wasm/pump_synth.cpp, built strict as thunder_pump_synth; the same entry points pump-synth.wasm exports). Where pump is not built (MSVC, THUNDER_WITH_PUMP=OFF)
// every instance is refused (make() is false), as a pump effect slot is left out there.
#if defined(THUNDER_HAVE_PUMP)
extern "C" {
int psyn_types();
int psyn_nparams(int type);
float* psyn_stage();
int psyn_new(int type, double sr);
void psyn_free(int h);
float* psyn_values(int h);
void psyn_commit(int h);
void psyn_midi(int h, int frame, int status, int d1, int d2);
float* psyn_out(int ch);
void psyn_process(int h, int n);
int psyn_latency(int h);
void psyn_tempo(int h, double bpm);
float* psyn_buf(int n);
void psyn_file(int h, int n, double sr);
}
#else
static int psyn_nparams(int) { return 0; }
static float* psyn_stage() { static float st[512]; return st; }
static int psyn_new(int, double) { return -1; }
static void psyn_free(int) {}
static float* psyn_values(int) { return nullptr; }
static void psyn_commit(int) {}
static void psyn_midi(int, int, int, int, int) {}
static float* psyn_out(int) { static float o[4096]; return o; }
static void psyn_process(int, int) {}
static int psyn_latency(int) { return 0; }
static void psyn_tempo(int, double) {}
static float* psyn_buf(int) { return nullptr; }
static void psyn_file(int, int, double) {}
#endif

namespace thunder {
using namespace xe;

#include "engine/pumpsynthtable.inc"
static_assert(kPsynDefN == kPsynTypes, "the table has the 13 instruments");

namespace {
int maxParams() { int m = 0; for (int i = 0; i < kPsynDefN; i++) m = kPsynDefs[i].n > m ? kPsynDefs[i].n : m; return m; }
const bool kTableOk = maxParams() <= kPsynMaxParams;

// KINDS of pump-synth-engine.js: key, plugin key, type id, len, trim, kit, drums, bpm. (The page's order is by type id: pmono 0 ... pwob 12 after plife 10 / pfreeze 11.)
const PsynKind kKinds[kPsynTypes] = {
  {"pmono", "mono", 0, 2, 1, false, 0, false},   {"ppoly", "poly", 1, 4, 1.5, false, 0, false}, {"pcoil", "coil", 2, 4, 1, false, 0, false},   {"porbit", "orbit", 3, 3, 1, false, 0, false},
  {"pfold", "fold", 4, 2, 1, false, 0, false},   {"pgloss", "gloss", 5, 2, 1, false, 0, false}, {"ptract", "tract", 6, 4, 1, false, 0, false},
  {"pstrike", "strike", 7, 1.5, 1, true, 8, false}, {"pboing", "boing", 8, 1.5, 1, true, 8, false}, {"px0x", "x0x", 9, 1.5, 1, true, 11, false},
  {"plife", "life", 10, 1.5, 1, true, 8, false}, {"pfreeze", "freeze", 11, 4, 1, false, 0, false}, {"pwob", "wob", 12, 2, 1, false, 0, true},
};

// Math.fround of a double without the C++ conversion's undefined behaviour above the float range (a value at or above FLT_MAX + half an ulp is Infinity)
inline float fround(double d) {
  if (d != d) return std::nanf("");
  if (d >= 3.4028235677973366e38) return INFINITY;
  if (d <= -3.4028235677973366e38) return -INFINITY;
  return (float)d;
}
inline bool refused(int type, double rate) { return type < 0 || type >= kPsynTypes || !(rate > 1000.0 && rate <= 768000.0); }   // psyn_new's own refusal
}  // namespace

const PsynKind* psyn_kind(int type) { return type >= 0 && type < kPsynTypes ? &kKinds[type] : nullptr; }
const PsynKind* psyn_kind_by_key(const char* key) { for (const PsynKind& k : kKinds) if (std::strcmp(k.key, key) == 0) return &k; return nullptr; }
int psyn_param_count(int type) { return type >= 0 && type < kPsynDefN && kTableOk ? kPsynDefs[type].n : 0; }
const PsynParamInfo* psyn_param(int type, int i) {
  if (type < 0 || type >= kPsynDefN || i < 0 || i >= kPsynDefs[type].n) return nullptr;
  return &kPsynParams[kPsynDefs[type].first + i];
}
int psyn_param_index(int type, const char* sym) {
  for (int i = 0, n = psyn_param_count(type); i < n; i++) if (std::strcmp(kPsynParams[kPsynDefs[type].first + i].sym, sym) == 0) return i;
  return -1;
}
const char* psyn_pad_key(const char* sym) {
  if (!std::strcmp(sym, "drive")) return "o_drive";
  if (!std::strcmp(sym, "tune")) return "o_tune";
  if (!std::strcmp(sym, "level")) return "o_level";
  return sym;
}

PsynPad::PsynPad() { for (double& x : v) x = std::nan(""); }
void PsynPad::setDefaults() { for (int i = 0, n = psyn_param_count(type); i < n; i++) v[i] = kPsynParams[kPsynDefs[type].first + i].def; }

// ---- PumpSynth -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
bool PumpSynth::make(int type, double sr, const double* values, int n) {
  destroy();
  if (type < 0 || type >= kPsynTypes || !kTableOk || psyn_nparams(type) != kPsynDefs[type].n) return false;
  float* st = psyn_stage();
  for (int i = 0; i < kPsynDefs[type].n && i < 512; i++) {
    const double v = values && i < n ? values[i] : std::nan("");
    st[i] = std::isfinite(v) ? fround(v) : std::nanf("");   // unwritten (NaN) = the default, as pump's stage; a non-finite value is the default, as the page's render
  }
  h_ = psyn_new(type, sr); type_ = h_ >= 0 ? type : -1;
  return h_ >= 0;
}
void PumpSynth::destroy() { if (h_ >= 0) psyn_free(h_); h_ = -1; }
int PumpSynth::latency() const { return h_ >= 0 ? psyn_latency(h_) : 0; }
void PumpSynth::tempo(double bpm) { if (h_ >= 0) psyn_tempo(h_, bpm); }
void PumpSynth::file(const float* mono, int n, double sr) {
  if (h_ < 0 || !mono || n < 1) return;
  float* p = psyn_buf(n); if (!p) return;
  std::memcpy(p, mono, (size_t)n * sizeof(float));
  psyn_file(h_, n, sr);
}
void PumpSynth::set(int i, double v) {
  if (h_ < 0) return;
  float* p = psyn_values(h_); if (!p) return;
  if (i < 0 || i >= psyn_param_count(type_)) return;
  p[i] = fround(v);
  psyn_commit(h_);
}
void PumpSynth::midi(int frame, int status, int d1, int d2) { if (h_ >= 0) psyn_midi(h_, frame, status, d1, d2); }
void PumpSynth::process(int n) { if (h_ >= 0) psyn_process(h_, n); }
const float* PumpSynth::out(int ch) { return psyn_out(ch); }
void PumpSynth::builtins() {
  const int src = psyn_param_index(11, "source");
  for (int a = 1; a <= 7; a++) {
    double v[kPsynMaxParams]; for (double& x : v) x = std::nan("");
    if (src >= 0) v[src] = a;
    PumpSynth s; if (!s.make(11, 48000.0, v, kPsynDefs[11].n)) continue;
    s.process(256);
  }
}

// ---- the rendered path -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
namespace {
struct Plan {
  const PsynKind* K = nullptr; bool ok = false;
  double rate = 0, holdS = 0, offAt = 0, maxS = 0; int note = 60, vel = 100;
};
Plan planOf(const PsynPad& P, double sr, double hold, double vlen) {
  Plan q; q.K = psyn_kind(P.type); if (!q.K) return q;
  const double tune = orz(P.tune), semis = q.K->kit ? 0 : js_round(tune), frac = tune - semis;
  q.rate = sr * js_pow(2, -frac / 12);
  if (refused(P.type, q.rate)) return q;
  q.ok = true;
  const double kd = mx(0, mn((q.K->drums ? q.K->drums : 8) - 1, js_round(orz(P.kd))));
  const int wk = (int)kd;   // an integer 0..drums-1
  static const int kWhite[7] = {0, 2, 4, 5, 7, 9, 11};
  q.note = q.K->kit ? 48 + kWhite[wk % 7] + 12 * (wk / 7) : (int)mx(0, mn(127, 60 + semis));
  q.vel = (int)mx(1, mn(127, js_round(orv(P.vel, 100))));
  q.holdS = mn(12, mx(0.02, orv(hold, orv(vlen, q.K->len))));
  q.offAt = js_round(q.holdS * q.rate);
  q.maxS = js_round(mn(12, q.holdS + 8) * q.rate);
  return q;
}
}  // namespace

int xeng_psyn_latency(int type, double sr) {
  const double r = orv(sr, 48000);
  if (refused(type, r)) return 0;
  PumpSynth s; if (!s.make(type, r, nullptr, 0)) return 0;
  return s.latency();
}

size_t xeng_psyn(const PsynPad& P, double sr, double hold, double vlen, double bpm, const PsynFile* file, float* out, size_t cap) {
  const Plan q = planOf(P, sr, hold, vlen);
  if (!q.ok) { if (out && cap >= 1) out[0] = 0; return out ? (cap >= 1 ? 1 : 0) : 1; }   // Float32Array(1)
  const size_t work = (size_t)q.maxS + (size_t)kPsynBlock;   // raw = new Float32Array(maxS + BLK)
  if (!out) return work;
  if (cap < work) return 0;

  // values staged: the page's st[i] = fx[sym] != null ? fx[sym] : P[al(sym)], a number that is finite or the default
  double v[kPsynMaxParams];
  const int np = kPsynDefs[q.K->type].n;
  for (int i = 0; i < np; i++) v[i] = P.v[i];
  if (q.K->type == 9) { const int i = psyn_param_index(9, "seq_mode"); if (i >= 0) v[i] = 0; }   // FIXED.px0x: its own sequencer off
  if (q.K->type == 10) { const int i = psyn_param_index(10, "mode"); if (i >= 0) v[i] = 0; }      // FIXED.plife
  PumpSynth s;
  if (!s.make(q.K->type, q.rate, v, np)) { out[0] = 0; return 1; }   // h < 0: Float32Array(1)
  if (bpm > 0) s.tempo(bpm);                                          // WOB's wobble: the song's tempo
  if (q.K->type == 11 && file && file->data && file->len > 0) {      // FREEZE's File source: the pad's own sound
    const double nn = mn(file->len, js_round(file->sr * 20));
    if (nn == nn && nn >= 1 && nn <= 192000.0 * 40) s.file(file->data, (int)nn, file->sr);
  }
  const int lat = s.latency();
  s.midi(0, 0x90, q.note, q.vel);
  const double g = q.K->trim;
  size_t n = 0; int quiet = 0; bool off = false;   // n: samples rendered so far (a multiple of BLK)
  while ((double)n < q.maxS + lat) {
    if (!off && q.offAt < (double)n + kPsynBlock) { s.midi((int)mx(0, q.offAt - (double)n), 0x80, q.note, 0); off = true; }
    s.process(kPsynBlock);
    const float* L = PumpSynth::out(0); const float* R = PumpSynth::out(1);
    double pk = 0;
    for (int i = 0; i < kPsynBlock; i++) {
      const double y = 0.5 * g * ((double)L[i] + (double)R[i]);
      const long long j = (long long)n + i - lat;
      if (j >= 0 && (double)j < (double)work) out[j] = (float)y;
      const double a = y < 0 ? -y : y; if (a > pk) pk = a;
    }
    n += kPsynBlock;
    quiet = pk < 3e-5 ? quiet + kPsynBlock : 0;
    if (off && (double)n > q.offAt + q.rate * 0.02 && quiet > q.rate * 0.3) break;   // released and died away
  }
  long long len = (long long)n - lat; if (len < 1) len = 1; if ((double)len > (double)work) len = (long long)work;
  // finish(out, P, sr): Level and Drive as the other engines apply them (no normalising), the silent end trimmed, a 3 ms fade
  level_drive(out, (size_t)len, P.level, P.drive, 1.0);
  return trim_fade(out, (size_t)len, sr, 0.003);
}

}  // namespace thunder
