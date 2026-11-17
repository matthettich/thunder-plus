// Hardening (17 Nov 2026): the small items of docs/port/REVIEW-16NOV.md (B1-2, B1-4, B1-5, B1-7, B3-3) and the runaway-flag item of docs/port/TODAY.md ("Evening, 16 Nov", 5): each is C++ undefined
// behaviour or an unchecked assumption where the JS gives a defined result. One test per item, named for it in every failure line; docs/port/HARDENING-NOTES.md says for each what the JS does, what the C++ did and what it does now.
// Built with the sanitizers in CI (THUNDER_SANITIZE), where a read out of range, a cast of NaN or a signed overflow is itself a failure. No golden file is read or written here.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "engine/engine.h"
#include "engine/engine_json.h"
#include "engine/xeng.h"
#include "engine/xeng_util.h"
#include "fx/clkcores.h"
#include "fx/u17cores.h"
#include "modfx/modfx.h"
#include "modfx/util.h"
#include "rt/json.h"

static int g_fail = 0;
#define CHECK(id, cond) do { if (!(cond)) { std::printf("FAIL %s: %s (line %d)\n", id, #cond, __LINE__); g_fail++; } } while (0)

namespace {
using thunder::Json;
using thunder::JsonDoc;
const double kNaN = std::numeric_limits<double>::quiet_NaN();
bool bitsEqual(float a, float b) { return std::memcmp(&a, &b, 4) == 0 || (std::isnan(a) && std::isnan(b)); }

// ---- B1-2: FlexCore 5-tuplet: K.tup[j] with j = -1 -------------------------------------------------------------------------------------
// JS: `sj = o.q === 5 ? FlexCore.K.tup[j] : j` with K.tup = [0, 0, 1, 2, 3]; j = Math.min(q - 1, Math.floor(u * q + 1e-9)) is -1 when u < -2e-10 (the position a hair below a beat boundary): tup[-1] is
// undefined, sj / 4 is NaN and so is the read position for that one sample.
void testTup() {
  static const double want[5] = {0, 0, 1, 2, 3};
  for (int j = 0; j < 5; j++) CHECK("B1-2 table", thunder::flex_tup_slot((double)j) == want[j]);
  CHECK("B1-2 table -0", thunder::flex_tup_slot(-0.0) == 0);   // tup["-0"] is tup[0] in the JS: the property key of -0 is "0"
  const double bad[] = {-1, -2, 5, 6, 1e9, -1e9, 0.5, kNaN, INFINITY, -INFINITY};
  for (double j : bad) CHECK("B1-2 outside the table is NaN (the JS's undefined)", std::isnan(thunder::flex_tup_slot(j)));
  // the arithmetic the core does with it, as the JS does it: C + C * (NaN / 4 + ...) is NaN
  const double C = 1, u = -3e-10, q = 5, j = std::fmin(q - 1, std::floor(u * q + 1e-9));
  CHECK("B1-2 the j the position a hair below the boundary makes is -1", j == -1);
  CHECK("B1-2 and the read position of that sample is NaN", std::isnan(C * (thunder::flex_tup_slot(j) / 4 + (u - j / q))));
}

// ---- B1-5: sinT(p) with p - floor(p) == 1 --------------------------------------------------------------------------------------------
// JS: const sinT = p => { p -= Math.floor(p); const x = p * TS, i = x | 0; return SIN[i] + (SIN[i + 1] - SIN[i]) * (x - i); }: a tiny negative p gives p - floor(p) == 1 exactly (-1e-20 - -1), i = 4096,
// SIN[4097] is undefined (the table has TS + 1 = 4097 entries, 0..4096) and the result is NaN.
void testSinT() {
  CHECK("B1-5 tiny negative phase is NaN", std::isnan(thunder::modfx::sinT(-1e-20)));
  CHECK("B1-5 -1e-17", std::isnan(thunder::modfx::sinT(-1e-17)));
  CHECK("B1-5 -2^-60 less one", std::isnan(thunder::modfx::sinT(-1.0 - 1e-30)) == false);   // -1 - 1e-30 is -1 in a double: floor is -1, p - floor = 0: not the case, a normal 0
  CHECK("B1-5 0 is sin(0)", thunder::modfx::sinT(0.0) == (double)thunder::modfx::kSin[0]);
  CHECK("B1-5 1 wraps to 0", thunder::modfx::sinT(1.0) == (double)thunder::modfx::kSin[0]);
  CHECK("B1-5 1/4 is 1", std::fabs(thunder::modfx::sinT(0.25) - 1.0) < 1e-6);
  CHECK("B1-5 the largest phase below 1 is finite", std::isfinite(thunder::modfx::sinT(std::nextafter(1.0, 0.0))));
  CHECK("B1-5 NaN is NaN", std::isnan(thunder::modfx::sinT(kNaN)));
  CHECK("B1-5 Infinity is NaN (Infinity - Infinity)", std::isnan(thunder::modfx::sinT(INFINITY)));
  CHECK("B1-5 -0.25 is -1", std::fabs(thunder::modfx::sinT(-0.25) + 1.0) < 1e-6);
}

// ---- B1-7: NaN to int casts, int overflow -----------------------------------------------------------------------------------------------
// Bytebeat: the JS passes clamp(Math.round(p.a), 0, 255) (NaN stays NaN) to P.s8(t, A, B, C, mode), which stores it into an Int32Array (St[++s] = a): NaN is stored as 0.
// The C++ had (int32_t)NaN, undefined behaviour. A NaN knob must render exactly as the knob at 0.
std::vector<float> renderBytebeat(int formula, double a, double b, double c, int mode) {
  using namespace thunder::modfx;
  const Def* d = find("bytebeat"); SeedCounter sc; std::unique_ptr<Module> m = d->make(48000.0, sc);
  static float in[kMaxPorts][kBlock], out[kMaxPorts][kBlock]; IO io; Ctx ctx;
  for (int k = 0; k < kMaxPorts; k++) { std::memset(in[k], 0, sizeof in[k]); io.in[k] = in[k]; io.linked[k] = false; }
  io.out[0] = out[0]; double p[8]; for (int k = 0; k < d->np; k++) p[k] = d->params[k].def;
  p[0] = formula; p[2] = a; p[3] = b; p[4] = c; p[5] = mode; p[1] = 32000; io.p = p;
  std::vector<float> r;
  for (int blk = 0; blk < 12; blk++) { m->run(io, ctx); r.insert(r.end(), out[0], out[0] + kBlock); }
  return r;
}
void testBytebeat() {
  int diffs = 0, nonzero = 0;
  for (int f = 0; f < 16; f++) for (int mode = 0; mode < 3; mode++) {
    const std::vector<float> n1 = renderBytebeat(f, kNaN, 128, 128, mode), z1 = renderBytebeat(f, 0, 128, 128, mode);
    const std::vector<float> n2 = renderBytebeat(f, 128, kNaN, kNaN, mode), z2 = renderBytebeat(f, 128, 0, 0, mode);
    const std::vector<float> n3 = renderBytebeat(f, kNaN, kNaN, kNaN, mode), z3 = renderBytebeat(f, 0, 0, 0, mode);
    for (size_t i = 0; i < n1.size(); i++) { if (!bitsEqual(n1[i], z1[i]) || !bitsEqual(n2[i], z2[i]) || !bitsEqual(n3[i], z3[i])) diffs++; if (n1[i] != 0) nonzero++; }
  }
  CHECK("B1-7 Bytebeat: a NaN a / b / c renders as 0 (all 16 formulas, 3 modes)", diffs == 0);
  CHECK("B1-7 Bytebeat: the renders are not all silent (the comparison means something)", nonzero > 1000);
}
// PitchTrk: tMax = Math.min(d.length - 2, Math.ceil(rs / Math.max(20, P.min))) is NaN when `min` is NaN; `for (t = 1; t <= tMax; ...)` then never runs, `o` (an index made from tMax) is never read:
// the C++ cast it with (int), undefined for NaN. Under the sanitizers this run is the check; the output must be finite and the module must not hang or write outside.
void testPitchTrk() {
  using namespace thunder::modfx;
  const Def* d = find("pitchtrk"); SeedCounter sc; std::unique_ptr<Module> m = d->make(48000.0, sc);
  static float in[kMaxPorts][kBlock], out[kMaxPorts][kBlock]; IO io; Ctx ctx;
  for (int k = 0; k < kMaxPorts; k++) { std::memset(in[k], 0, sizeof in[k]); io.in[k] = in[k]; io.linked[k] = true; }
  for (int k = 0; k < d->nout; k++) io.out[k] = out[k];
  double p[8]; for (int k = 0; k < d->np; k++) p[k] = d->params[k].def;
  CHECK("B1-7 PitchTrk: parameter 4 is min", std::strcmp(d->params[4].name, "min") == 0);
  p[4] = kNaN; io.p = p; bool finite = true;
  for (int blk = 0; blk < 120; blk++) {   // 120 blocks = 15360 samples: the 1024-entry ring (4 samples a step) is full after 32 blocks, so the detector's branch runs many times
    for (int i = 0; i < kBlock; i++) in[0][i] = (float)(0.5 * std::sin(2 * 3.141592653589793 * 220.0 * (blk * kBlock + i) / 48000.0));
    m->run(io, ctx);
    for (int k = 0; k < d->nout; k++) for (int i = 0; i < kBlock; i++) if (!std::isfinite(out[k][i])) finite = false;
  }
  CHECK("B1-7 PitchTrk with a NaN min: finite outputs, no cast of NaN", finite);
}
// PitchCorrect: `++st.miss > 2` where miss is a Number that just keeps counting (it only ever asks "> 2"); the C++ int overflowed after 2^31 control blocks (about 66 days). missInc saturates.
void testMiss() {
  using namespace thunder::modfx;
  CHECK("B1-7 missInc counts", missInc(0) == 1 && missInc(9) == 10 && missInc(2) == 3);
  CHECK("B1-7 missInc saturates (no signed overflow)", missInc(kMissCap) == kMissCap && missInc(2147483647) == 2147483647 && missInc(kMissCap - 1) == kMissCap);
  int m = 9; for (int i = 0; i < 3 * kMissCap / 2; i++) m = missInc(m);
  CHECK("B1-7 a run past the cap stays at it and above 2", m == kMissCap && m > 2);
}

// ---- B1-4: pad_pitchfx with a saved null ------------------------------------------------------------------------------------------------
// JS applyFx: tail = Math.round(sr * (rv ? 3 + f.size * 4 : Math.min(8, f.time / 1000 * (1 + 6 * f.fb)))): with size null that is 3 + 0 = 3 (a valid tail); with size undefined NaN (new Float32Array(NaN) is
// empty and L.set(a) throws a RangeError). JSON.stringify writes null for a NaN, so a null reaches the code from a saved voice. The core's own `f.size != null ? f.size : 0.6` treats both alike (0.6).
std::vector<float> pitchFx(const char* json, double sr, size_t n) {
  JsonDoc doc; if (!doc.parse(std::string(json))) { std::printf("FAIL B1-4 test json: %s\n", json); g_fail++; return {}; }
  thunder::EngFx f; thunder::eng_fx_from_json(f, doc.root());
  std::vector<float> in(n); for (size_t i = 0; i < n; i++) in[i] = (float)(0.4 * std::sin(0.07 * (double)i) * std::exp(-(double)i / 2000.0));
  return thunder::pad_pitchfx(f.kind == thunder::EngFx::Kind::PitchReverb, thunder::PadPitchFx{f.mode, f.time, f.fb, f.pitch, f.pitch2, f.tone, f.size, f.wow, f.mix, f.sizeNull}, in.data(), n, sr);
}
size_t engineBound(const char* fxJson, double sr) {
  JsonDoc dv, dl, df; dv.parse(std::string("{}")); dl.parse(std::string("[{\"type\":\"sine\"}]")); df.parse(std::string(fxJson));
  thunder::EngVoice V; if (!thunder::eng_voice_from_json(V, dv.root(), dl.root(), df.root())) return 12345678;
  thunder::EngRenderOpts o; o.sr = sr; return thunder::eng_render_bound(V, o);
}
void testNullPitchFx() {
  const double sr = 8000; const size_t n = 300;
  const char* common = "\"mode\":0,\"fb\":0.3,\"pitch\":12,\"pitch2\":-12,\"tone\":0.6,\"wow\":0.2,\"mix\":0.4";
  auto J = [&](const char* type, const char* extra) { return std::string("{\"type\":\"") + type + "\"," + common + "," + extra + "}"; };
  // size: null -> tail round(sr * 3) = 24000; the sound is the one with size 0.6 (the core's default) cut to that length
  const std::vector<float> nul = pitchFx(J("preverb", "\"size\":null,\"time\":380").c_str(), sr, n);
  const std::vector<float> dfl = pitchFx(J("preverb", "\"size\":0.6,\"time\":380").c_str(), sr, n);   // tail round(sr * (3 + 0.6 * 4)) = 43200
  CHECK("B1-4 preverb, size null: the tail is the JS's 3 s", nul.size() == n + 24000);
  CHECK("B1-4 the size 0.6 reference is longer (3 + 2.4 s)", dfl.size() == n + 43200);
  bool same = nul.size() <= dfl.size(); for (size_t i = 0; same && i < nul.size(); i++) same = bitsEqual(nul[i], dfl[i]);
  CHECK("B1-4 size null sounds as size 0.6 (the core's default), as the JS has it", same);
  { bool fin = true; for (float x : nul) if (!std::isfinite(x)) fin = false; CHECK("B1-4 finite", fin); }
  // size absent (undefined): a RangeError in the JS: refused (empty), as before
  CHECK("B1-4 preverb, size absent: refused as before (the JS throws)", pitchFx(J("preverb", "\"time\":380").c_str(), sr, n).empty());
  // a real NaN reaches pad_pitchfx without the flag: refused as before
  { thunder::PadPitchFx f{0, 380, 0.3, 12, -12, 0.6, kNaN, 0.2, 0.4}; std::vector<float> in(n, 0.1f); CHECK("B1-4 a NaN size (no null flag) is refused as before", thunder::pad_pitchfx(true, f, in.data(), n, sr).empty()); }
  // pdelay: time null -> tail Math.round(sr * Math.min(8, null / 1000 * (1 + 6 * fb))) = 0 and the core's time is 0 (Math.max(10, Math.min(2000, null)) is 10): the same as "time":0
  const std::vector<float> tn = pitchFx(J("pdelay", "\"size\":0.6,\"time\":null").c_str(), sr, n), t0 = pitchFx(J("pdelay", "\"size\":0.6,\"time\":0").c_str(), sr, n);
  CHECK("B1-4 pdelay, time null: tail 0, length n", tn.size() == n);
  bool same2 = tn.size() == t0.size(); for (size_t i = 0; same2 && i < tn.size(); i++) same2 = bitsEqual(tn[i], t0[i]);
  CHECK("B1-4 pdelay, time null sounds as time 0", same2);
  CHECK("B1-4 pdelay, time absent: refused as before (tail NaN)", pitchFx(J("pdelay", "\"size\":0.6").c_str(), sr, n).empty());
  const std::vector<float> fn = pitchFx("{\"type\":\"pdelay\",\"mode\":0,\"time\":380,\"fb\":null,\"pitch\":12,\"tone\":0.6,\"size\":0.6,\"wow\":0.2,\"mix\":0.4}", sr, n);
  const std::vector<float> f0 = pitchFx("{\"type\":\"pdelay\",\"mode\":0,\"time\":380,\"fb\":0,\"pitch\":12,\"tone\":0.6,\"size\":0.6,\"wow\":0.2,\"mix\":0.4}", sr, n);
  bool same3 = fn.size() == f0.size() && !fn.empty(); for (size_t i = 0; same3 && i < fn.size(); i++) same3 = bitsEqual(fn[i], f0[i]);
  CHECK("B1-4 pdelay, fb null sounds as fb 0 (and the tail is the JS's)", same3);
  // the voice length bound (engine.cpp and xeng_voice.cpp have their own copy of the tail formula): a preverb with size null makes the 3 s tail, one with size absent none (the JS throws there)
  const size_t bNull = engineBound("[{\"type\":\"preverb\",\"size\":null,\"mode\":0,\"time\":380,\"fb\":0.3,\"pitch\":12,\"tone\":0.6,\"mix\":0.4}]", 48000);
  const size_t bZero = engineBound("[{\"type\":\"preverb\",\"size\":0,\"mode\":0,\"time\":380,\"fb\":0.3,\"pitch\":12,\"tone\":0.6,\"mix\":0.4}]", 48000);
  const size_t bAbs = engineBound("[{\"type\":\"preverb\",\"mode\":0,\"time\":380,\"fb\":0.3,\"pitch\":12,\"tone\":0.6,\"mix\":0.4}]", 48000);
  CHECK("B1-4 voice bound: size null is the same as size 0 (a 3 s tail)", bNull == bZero && bNull > 0);
  CHECK("B1-4 voice bound: size absent has no tail (shorter)", bAbs < bNull);
}

// ---- B3-3: ctx.block <= kBlock ---------------------------------------------------------------------------------------------------------
// run() is the host's one way into a module: a block of more than kBlock frames (or a negative one) is refused: nothing is called, nothing is written. process() is protected.
template <class T, class = void> struct CanProcess : std::false_type {};
template <class T> struct CanProcess<T, std::void_t<decltype(std::declval<T&>().process(std::declval<const thunder::modfx::IO&>(), std::declval<const thunder::modfx::Ctx&>()))>> : std::true_type {};
static_assert(!CanProcess<thunder::modfx::Module>::value, "B3-3: a caller holding a Module must not reach process() (only run())");

void testBlock() {
  using namespace thunder::modfx;
  // (a probe Module made here would carry this TU's RTTI against the library's -fno-rtti: the vptr check of the sanitizer build rejects it; the registry's own modules are used instead)
  SeedCounter sc0; std::unique_ptr<Module> pr = find("bytebeat")->make(48000.0, sc0); static float in0[kMaxPorts][kBlock], out0[kMaxPorts][kBlock]; IO io; Ctx c; double p0[8];
  for (int k = 0; k < kMaxPorts; k++) { io.in[k] = in0[k]; io.out[k] = out0[k]; } for (int k = 0; k < 8; k++) p0[k] = find("bytebeat")->params[k].def; io.p = p0;
  const int ok[] = {0, 1, 37, 127, 128};
  for (int b : ok) { c.block = b; CHECK("B3-3 a block up to kBlock runs", pr->run(io, c)); }
  CHECK("B3-3 five calls made, none refused", pr->refused() == 0);
  const int bad[] = {129, 130, 256, 4096, 1 << 20, 2147483647, -1, -128, (-2147483647 - 1)};
  for (int b : bad) { c.block = b; CHECK("B3-3 a block above kBlock (or below 0) is refused", !pr->run(io, c)); }
  CHECK("B3-3 nine refusals counted", pr->refused() == 9);
  // every real module, canary buffers: a refused block changes nothing; a block of 128 still renders
  static float in[kMaxPorts][kBlock], out[kMaxPorts][kBlock]; int refusedAll = 0, touched = 0, ranOk = 0;
  for (int i = 0; i < count(); i++) {
    const Def& d = at(i); SeedCounter sc; std::unique_ptr<Module> m = d.make(48000.0, sc);
    IO x; for (int k = 0; k < kMaxPorts; k++) { for (int j = 0; j < kBlock; j++) { in[k][j] = 0.1f; out[k][j] = 12345.f; } x.in[k] = in[k]; x.linked[k] = true; x.out[k] = out[k]; }
    std::vector<double> p((size_t)(d.np > 0 ? d.np : 1)); for (int k = 0; k < d.np; k++) p[(size_t)k] = d.params[k].def; x.p = p.data();
    Ctx cx; for (int b : {129, 256, 4096, -1}) { cx.block = b; if (!m->run(x, cx)) refusedAll++; }
    for (int k = 0; k < kMaxPorts; k++) for (int j = 0; j < kBlock; j++) if (out[k][j] != 12345.f) touched++;
    cx.block = kBlock; if (m->run(x, cx)) ranOk++;
  }
  CHECK("B3-3 every module refuses all four oversized or negative blocks", refusedAll == 4 * count());
  CHECK("B3-3 a refused block writes nothing", touched == 0);
  CHECK("B3-3 and the same modules run a 128 block", ranOk == count());
}

// ---- xeng_swarm / xeng_drift: the runaway flag at the end of the control block ------------------------------------------------------------
// A path faster than 1e4 wraps per control step is refused (the JS's `while (q.ph >= 1)` would not end). The flag was tested at the START of the next control block, so a voice of one control block
// (32 samples or fewer) was rendered instead. Such a voice needs a low sample rate here (N = ceil(len * sr), len >= 0.07 s): sr = 200 gives N = 15 (swarm) and 20 (drift).
void testRunaway() {
  using namespace thunder; using thunder::xe::kRefuse;
  std::vector<float> out(1 << 16);
  for (double sr : {200.0, 48000.0}) {
    SwarmP s; s.rate = 1e9; s.atk = 0.5; s.rel = 1; DriftP d; d.wowRate = 1e9; d.atk = 0.5; d.rel = 1;
    const size_t Ns = xeng_swarm(s, sr, 0.01, nullptr, 0), Nd = xeng_drift(d, sr, 0.01, nullptr, 0);
    if (sr == 200.0) CHECK("runaway: the test voices are one control block (N <= 32)", Ns >= 1 && Ns <= 32 && Nd >= 1 && Nd <= 32);
    else CHECK("runaway: the 48 kHz voices are many blocks", Ns > 32 && Nd > 32);
    CHECK("runaway swarm: a runaway path is refused", xeng_swarm(s, sr, 0.01, out.data(), sr == 200.0 ? out.size() : Ns) == kRefuse);
    CHECK("runaway drift: a runaway path is refused", xeng_drift(d, sr, 0.01, out.data(), sr == 200.0 ? out.size() : Nd) == kRefuse);
    SwarmP s2; s2.atk = 0.5; s2.rel = 1; DriftP d2; d2.atk = 0.5; d2.rel = 1;   // ordinary rates: rendered at the same length
    const size_t Ns2 = xeng_swarm(s2, sr, 0.01, nullptr, 0), Nd2 = xeng_drift(d2, sr, 0.01, nullptr, 0);
    const size_t rs = xeng_swarm(s2, sr, 0.01, out.data(), Ns2), rd = xeng_drift(d2, sr, 0.01, out.data(), Nd2);
    CHECK("runaway swarm: an ordinary voice still renders", rs != kRefuse && rs > 0 && rs <= Ns2);
    CHECK("runaway drift: an ordinary voice still renders", rd != kRefuse && rd > 0 && rd <= Nd2);
  }
  // just below the limit (the voices' own paths reach 1.4 * rate * dt = 8960 per control step < 1e4, dt = 0.16 at sr 200 -> rate 4e4 gives 8960): rendered, not refused
  { thunder::SwarmP s; s.atk = 0.5; s.rel = 1; s.rate = 4e4;
    const size_t N = xeng_swarm(s, 200.0, 0.01, nullptr, 0); const size_t r = xeng_swarm(s, 200.0, 0.01, out.data(), N);
    CHECK("runaway swarm: just below the limit renders", r != kRefuse && r > 0); }
}
}  // namespace

int main() {
  testTup(); testSinT(); testBytebeat(); testPitchTrk(); testMiss(); testNullPitchFx(); testBlock(); testRunaway();
  if (g_fail) { std::printf("hardening: %d FAILED\n", g_fail); return 1; }
  std::printf("hardening: ok (B1-2 tup, B1-5 sinT, B1-7 bytebeat / pitchtrk / miss, B1-4 null pitchfx, B3-3 block guard, swarm / drift runaway)\n");
  return 0;
}
