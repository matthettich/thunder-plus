/* The LXR module (U04): the firmware's voices hosted natively, behind the seam (include/thunder/lxr_seam.h).
 * Copyright (C) 2026 the Thunder Plus authors. SPDX-License-Identifier: GPL-3.0-only, with the additional permission and terms in lxr/NOTICE.md.
 * Built on the LXR firmware, Copyright (C) 2012-2026 Erica Synths (lxr/src/firmware). Not official Erica Synths firmware.
 *
 * THE PAGE'S CALL SEQUENCE is the specification (lxr/lxr-engine.js, LxrCore.render; the firmware glue is lxr/src/lxr_wasm.c, compiled unchanged into this library):
 *   power-on (the wasm memory restored; lxr_init(0x2545F491); lxr_init_kit()), the bytes of the pad written over the init kit (the velocity / LFO targets and the voice LFOs reset,
 *   then every knob the pad holds, Math.max(0, Math.min(255, Math.round(v) || 0))), lxr_set_bpm(Math.round(bpm || 120)), lxr_load(), 20 blocks of 16 firmware blocks to settle the
 *   smoothing, lxr_trigger, then blocks of 16 until the voice has been quiet for 0.3 s or the hold is over, a 32-tap windowed-sinc resample to the app's rate, Level and Drive, the trim
 *   and a 3 ms fade. Every Math.* is the core's js_* (fdlibm, as V8 has them); every float32 the JS stores is a float here. The JS is ported with its quirks.
 * No allocation after construction: the buffers are static; nothing here takes a lock (a second concurrent caller is refused). */
#include "thunder/lxr_module.h"

#include <atomic>
#include <cmath>
#include <cstring>

#include "rt/jsmath.h"

extern "C" {
void lxr_init(uint32_t seed);
void lxr_init_kit(void);
uint8_t* lxr_params(void);
int lxr_num_params(void);
void lxr_load(void);
void lxr_trigger(int voice, int vel, int note);
int lxr_render(int voice, int n);
float* lxr_out(void);
void lxr_set_bpm(int bpm);
int lxr_block(void);
float lxr_rate(void);
void lxr_host_snapshot(void);
void lxr_host_restore(void);
int lxr_host_par(int which);
}

namespace thunder {
namespace lxr {
namespace {

struct KindTab {
  int voice; double len; int nKnobs; const uint16_t* knobN; const int16_t* knobD; int nTgt; const uint8_t* tgt; int vdst, ldst, vel, open;
};
#include "lxr_tables.inc"

constexpr int kMaxHoldBlocks = 16;                                  // the page renders 16 firmware blocks a call
constexpr size_t kRawMax = size_t(12.0 * 44140.0) + 64 * 16 + 64;   // the longest raw voice: 12 s plus one call
constexpr int kPH = 1024, kA = 16;                                  // resample: fractional positions, half the taps
float g_raw[kRawMax];
float g_kt[(kPH + 1) * 2 * kA];
double g_ktFrom = -1, g_ktTo = -1;
std::atomic_flag g_busy = ATOMIC_FLAG_INIT;
bool g_snapped = false;

struct Lock {
  bool ok;
  Lock() : ok(!g_busy.test_and_set(std::memory_order_acquire)) { if (ok && !g_snapped) { lxr_host_snapshot(); g_snapped = true; } }
  ~Lock() { if (ok) g_busy.clear(std::memory_order_release); }
};

inline double orv(double a, double b) { return (a != 0 && a == a) ? a : b; }   // a || b for numbers
inline double mn(double a, double b) { return (a != a || b != b) ? NAN : (a < b ? a : b); }
inline double mx(double a, double b) { return (a != a || b != b) ? NAN : (a > b ? a : b); }
inline double round0(double x) { const double r = js_round(x); return (r != r) ? 0.0 : r; }   // Math.round(x) || 0 (a -0 is 0 too)
inline uint8_t byteOf(double x) { double r = round0(x); r = r < 0 ? 0 : r > 255 ? 255 : r; return (uint8_t)r; }

void powerOn() { lxr_host_restore(); lxr_init(0x2545F491u); lxr_init_kit(); }

// The pad's bytes over the init kit that powerOn() left in the firmware's edit buffer (lxr-engine.js render, lines "const b = ..." to "X.lxr_set_bpm").
void writeKit(const ThunderLxrRequest& r, uint8_t* b) {
  const KindTab& K = kKinds[r.kind];
  const int np = kTabNumParams, vd = lxr_host_par(0), tl = lxr_host_par(1), vl = lxr_host_par(2);
  for (int i = 0; i < 6; i++) { b[vd + i] = 0; b[tl + i] = 0; b[vl + i] = (uint8_t)(i + 1); }
  for (int i = 0; i < K.nKnobs; i++) { const int n = K.knobN[i]; if (n < np) b[n] = byteOf(r.p[n]); }
  auto tg = [&](double v) -> uint8_t { const double q = round0(v); return (q > 0 && q <= K.nTgt) ? K.tgt[(int)q - 1] : (uint8_t)0; };
  b[vd + K.voice] = tg(r.vdst); b[tl + K.voice] = tg(r.ldst);
}

int hatTrigger(const ThunderLxrRequest& r) { return r.kind == 3 ? (round0(r.open) != 0 ? 6 : 5) : kKinds[r.kind].voice; }

double holdSeconds(const ThunderLxrRequest& r) { return mn(12, mx(0.05, orv(r.hold, orv(r.vlen, kKinds[r.kind].len)))); }

void buildKernel(double from, double to) {
  if (g_ktFrom == from && g_ktTo == to) return;
  g_ktFrom = from; g_ktTo = to;
  const double fc = mn(1, to / from) * 0.97, pi = 3.141592653589793;
  for (int p = 0; p <= kPH; p++) {
    const double f = (double)p / kPH; double w = 0;
    for (int m = 0; m < 2 * kA; m++) {
      const double d = f + m - (kA - 1), u = d * fc, sc = u == 0 ? 1 : js_sin(pi * u) / (pi * u), k = sc * (0.5 + 0.5 * js_cos(pi * d / kA));
      g_kt[p * 2 * kA + m] = (float)k; w += k;
    }
    for (int m = 0; m < 2 * kA; m++) g_kt[p * 2 * kA + m] = (float)((double)g_kt[p * 2 * kA + m] / w);
  }
}

// resample(x, from, to) into y; returns the length (y holds at least bound floats: the caller has checked).
size_t resample(const float* x, size_t L, double from, double to, float* y) {
  if (std::fabs(from - to) < 0.5) { std::memcpy(y, x, L * sizeof(float)); return L; }
  buildKernel(from, to);
  const double ratio = from / to;
  const size_t N = (size_t)std::floor((double)L / ratio);
  for (size_t j = 0; j < N; j++) {
    const double t = (double)j * ratio, i0 = std::floor(t);
    const size_t row = (size_t)js_round((t - i0) * kPH) * 2 * kA; double s = 0;
    for (int m = 0; m < 2 * kA; m++) { const double i = i0 - m + kA - 1; if (i >= 0 && i < (double)L) s += (double)x[(size_t)i] * (double)g_kt[row + m]; }
    y[j] = (float)s;
  }
  return N;
}

size_t finish(float* out, size_t len, double level, double drive, double sr) {
  const double lv = js_pow(10, orv(level, 0) / 20), d0 = orv(drive, 0), dr = d0 > 0.001 ? 1 + d0 * d0 * 10 : 0, dn = dr != 0 ? 1 / js_tanh(dr) : 1;
  for (size_t i = 0; i < len; i++) { double y = (double)out[i] * lv; if (dr != 0) y = js_tanh(y * dr) * dn; out[i] = (float)y; }
  size_t end = len; while (end > 1 && std::fabs((double)out[end - 1]) < 1e-4) end--;
  const size_t n = len == 0 ? 0 : (end > 1 ? end : 1);
  const double fl = mn((double)n, js_round(sr * 0.003));
  for (size_t i = 0; (double)i < fl; i++) { float& v = out[n - 1 - i]; v = (float)((double)v * ((double)i / fl)); }
  return n;
}

size_t boundOf(const ThunderLxrRequest& r) {
  if (r.kind < 0 || r.kind >= THUNDER_LXR_KINDS) return 0;
  if (!(r.sr > 0 && r.sr <= 1e7)) return 0;
  const double rate = (double)lxr_rate(), maxS = js_round(holdSeconds(r) * rate), rawN = maxS + 64.0 * kMaxHoldBlocks;
  if (!(rawN <= (double)kRawMax)) return 0;
  const double N = std::fabs(rate - r.sr) < 0.5 ? rawN : std::floor(rawN / (rate / r.sr)) + 1;
  return (N >= 1 && N <= (double)kMaxFloats) ? (size_t)N : 0;
}

}  // namespace

int NumParams() { return lxr_num_params(); }
double NativeRate() { return (double)lxr_rate(); }
int BlockFrames() { return lxr_block(); }
int KnobCount(int kind) { return (kind >= 0 && kind < 4) ? kKinds[kind].nKnobs : 0; }

bool Describe(int kind, ThunderLxrDescribe* d) {
  if (kind < 0 || kind >= THUNDER_LXR_KINDS || !d) return false;
  const KindTab& K = kKinds[kind];
  std::memset(d, 0, sizeof *d);
  d->nparams = kTabNumParams; d->rate = kTabRate; d->len = K.len;
  for (int i = 0; i < K.nKnobs; i++) { d->present[K.knobN[i]] = 1; d->def[K.knobN[i]] = K.knobD[i]; }
  d->vdst = K.vdst; d->ldst = K.ldst; d->vel = K.vel; d->open = K.open < 0 ? 0 : K.open; d->hasOpen = K.open >= 0;
  return true;
}

size_t RenderBound(const ThunderLxrRequest& r) { return boundOf(r); }

size_t PadKitBytes(const ThunderLxrRequest& r, uint8_t* out, size_t cap) {
  if (r.kind < 0 || r.kind >= THUNDER_LXR_KINDS || cap < (size_t)kTabNumParams) return 0;
  Lock L; if (!L.ok) return 0;
  powerOn(); uint8_t* b = lxr_params(); writeKit(r, b); std::memcpy(out, b, (size_t)kTabNumParams);
  return (size_t)kTabNumParams;
}

int RenderPad(const ThunderLxrRequest& r, float* out, size_t cap, size_t* n, float* raw, size_t rawCap, size_t* rawN) {
  *n = 0; if (rawN) *rawN = 0;
  const size_t bound = boundOf(r);
  if (bound == 0) return THUNDER_LXR_REFUSED;
  if (cap < bound) return THUNDER_LXR_TOO_SMALL;
  Lock L; if (!L.ok) return THUNDER_LXR_REFUSED;
  const int vi = kKinds[r.kind].voice, blk = lxr_block();
  powerOn();
  writeKit(r, lxr_params());
  lxr_set_bpm(js_to_int32(js_round(orv(r.bpm, 120))));   // wasm's i32 parameter: ToInt32 of Math.round(bpm || 120)
  lxr_load();
  for (int i = 0; i < 20; i++) lxr_render(vi, kMaxHoldBlocks);   // 1/3 s for the knob and fader smoothing to settle
  const int trig = hatTrigger(r);
  const double nt = mx(0, mn(127, 60 + round0(orv(r.tune, 0)))), vl = mx(1, mn(127, round0(orv(r.vel, 127))));
  lxr_trigger(trig, (int)vl, (int)nt);
  const double rate = (double)lxr_rate(), maxS = js_round(holdSeconds(r) * rate);
  size_t nn = 0, quiet = 0;
  while ((double)nn < maxS) {
    const int c = lxr_render(vi, kMaxHoldBlocks); const float* o = lxr_out();
    std::memcpy(g_raw + nn, o, (size_t)c * sizeof(float)); nn += (size_t)c;
    double pk = 0; for (int i = 0; i < c; i++) { const double a = std::fabs((double)o[i]); if (a > pk) pk = a; }
    quiet = pk < 3e-5 ? quiet + (size_t)c : 0;
    if ((double)nn > rate * 0.05 && (double)quiet > rate * 0.3) break;   // the voice has died away
  }
  (void)blk;
  if (raw && rawCap) { const size_t m = nn < rawCap ? nn : rawCap; std::memcpy(raw, g_raw, m * sizeof(float)); if (rawN) *rawN = m; }
  const size_t len = resample(g_raw, nn, rate, r.sr, out);
  *n = finish(out, len, r.level, r.drive, r.sr);
  return THUNDER_LXR_OK;
}

// ---------------------------------------------------------------- the live path (provisional)
namespace {
constexpr int kBlk = 64, kPend = 64;
struct LiveVoice { float blk[kBlk]; int pos = kBlk; uint64_t abs = 0; int nPend = 0; struct Ev { uint64_t at; int fw, vel, note; } pend[kPend]; };
LiveVoice g_live[6];
bool g_liveOpen = false;
inline int renderVoiceOf(int v) { return v < 5 ? v : 5; }   // firmware trigger numbers 5 and 6 (closed / open hat) both play on the hat voice
}  // namespace

int LiveOpen(const uint8_t* kit, size_t n, double bpm) {
  Lock L; if (!L.ok) return THUNDER_LXR_REFUSED;
  if (!kit || n == 0 || n > (size_t)kTabNumParams) return THUNDER_LXR_REFUSED;
  powerOn(); uint8_t* b = lxr_params(); std::memcpy(b, kit, n);
  lxr_set_bpm(js_to_int32(js_round(orv(bpm, 120)))); lxr_load();
  for (int v = 0; v < 6; v++) for (int i = 0; i < 20; i++) lxr_render(v, kMaxHoldBlocks);
  for (int v = 0; v < 6; v++) { g_live[v].pos = kBlk; g_live[v].abs = 0; g_live[v].nPend = 0; }
  g_liveOpen = true;
  return THUNDER_LXR_OK;
}

int LiveProcess(int voice, const ThunderLxrEvent* ev, size_t nev, float* out, size_t frames) {
  if (voice < 0 || voice > 5 || (!out && frames) || (nev && !ev)) return THUNDER_LXR_REFUSED;
  Lock L; if (!L.ok) return THUNDER_LXR_REFUSED;
  if (!g_liveOpen) return THUNDER_LXR_REFUSED;
  LiveVoice& V = g_live[voice];
  size_t mine = 0; for (size_t i = 0; i < nev; i++) if (ev[i].voice >= 0 && ev[i].voice <= 6 && renderVoiceOf(ev[i].voice) == voice && (size_t)ev[i].offset < (frames ? frames : 1)) mine++;
  if (V.nPend + (int)mine > kPend) return THUNDER_LXR_REFUSED;   // nothing is taken: the caller drains first
  const uint64_t base = V.abs;   // an event's time: the frames already produced for this voice plus its offset into this call
  for (size_t i = 0; i < nev; i++) {
    const ThunderLxrEvent& e = ev[i];
    if (e.voice < 0 || e.voice > 6 || renderVoiceOf(e.voice) != voice || (size_t)e.offset >= (frames ? frames : 1)) continue;
    // keep the pending list sorted by time (insertion; stable for equal times)
    int k = V.nPend++; const uint64_t at = base + e.offset;
    while (k > 0 && V.pend[k - 1].at > at) { V.pend[k] = V.pend[k - 1]; k--; }
    V.pend[k].at = at; V.pend[k].fw = e.voice; V.pend[k].vel = e.vel; V.pend[k].note = e.note;
  }
  for (size_t i = 0; i < frames; i++) {
    if (V.pos >= kBlk) {   // a new block: the triggers whose time has come act now (the first block boundary at or after their offset)
      int used = 0;
      while (used < V.nPend && V.pend[used].at <= V.abs) { const auto& p = V.pend[used++]; lxr_trigger(p.fw, (int)mx(1, mn(127, (double)p.vel)), (int)mx(0, mn(127, (double)p.note))); }
      if (used) { for (int k = used; k < V.nPend; k++) V.pend[k - used] = V.pend[k]; V.nPend -= used; }
      lxr_render(voice, 1); std::memcpy(V.blk, lxr_out(), sizeof V.blk); V.pos = 0;
    }
    out[i] = V.blk[V.pos++]; V.abs++;
  }
  return THUNDER_LXR_OK;
}

}  // namespace lxr
}  // namespace thunder

// ---------------------------------------------------------------- the seam's table
namespace {
using namespace thunder::lxr;
int32_t sDescribe(int32_t kind, ThunderLxrDescribe* d) { return Describe(kind, d) ? THUNDER_LXR_OK : THUNDER_LXR_REFUSED; }
size_t sBound(const ThunderLxrRequest* r) { return r ? RenderBound(*r) : 0; }
int32_t sRender(const ThunderLxrRequest* r, float* out, size_t cap, size_t* n) { if (!r || !out || !n) return THUNDER_LXR_REFUSED; return RenderPad(*r, out, cap, n); }
int32_t sOpen(const uint8_t* kit, size_t n, double bpm) { return LiveOpen(kit, n, bpm); }
int32_t sProc(int32_t v, const ThunderLxrEvent* ev, size_t nev, float* out, size_t frames) { return LiveProcess(v, ev, nev, out, frames); }
const ThunderLxrApi kApi = {THUNDER_LXR_ABI, (uint32_t)sizeof(ThunderLxrApi), "LXR-02 firmware a43cd4fc (23 Oct 2026), Thunder Plus host module 1", sDescribe, sBound, sRender, sOpen, sProc};
}  // namespace

extern "C" {
const ThunderLxrApi* thunder_lxr_module_api(void) { return &kApi; }
int thunder_lxr_module_register(void) { return thunder_lxr_register(&kApi); }
}
