// U25: see layer_pe.h. plaitsPre / elementsPre of index.html (line 3921 on) call for call; only built when the library is (core/CMakeLists.txt).
#include "engine/layer_pe.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef THUNDER_ENGINE_PE
#include "pe_host.h"
#endif
#include "rt/jsmath.h"

namespace thunder {

#ifdef THUNDER_ENGINE_PE
namespace {
inline double jmax(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double jmin(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }
inline bool truthy(double x) { return x != 0 && x == x; }

constexpr size_t kPlaitsMax = 48000 * 8, kElementsMax = 32000 * 8;   // the JS caps both pre-renders at 8 s
struct Workspace {
  thunder_pe::Session* sess = nullptr;
  float* pre = nullptr;                                              // kPlaitsMax floats (the larger of the two)
  ~Workspace() { thunder_pe::session_free(sess); std::free(pre); }
};
Workspace& ws() { static thread_local Workspace w; return w; }
void prepare(Workspace& w) {
  if (!w.sess) w.sess = thunder_pe::session_new();
  if (!w.pre) w.pre = (float*)std::malloc(kPlaitsMax * sizeof(float));
}
// the layer's note: base + pA * Math.exp(-t / ptau)
inline double noteAt(double base, double pA, double t, double ptau) { return base + pA * js_exp(-t / ptau); }
}  // namespace
bool pe_available() { return true; }
void pe_prepare() { prepare(ws()); }
#else
bool pe_available() { return false; }   // no Plaits / Elements library on this platform (MSVC builds no DSP libraries)
void pe_prepare() {}
#endif
bool pe_is(LType t) { return t == LType::Plaits || t == LType::Elements; }
LType pe_layer_type(const char* name, size_t n) {
  if (!pe_available()) return LType::Other;
  if (n == 6 && std::memcmp(name, "plaits", 6) == 0) return LType::Plaits;
  if (n == 8 && std::memcmp(name, "elements", 8) == 0) return LType::Elements;
  return LType::Other;
}
bool pe_layer_ok(const EngLayer& L) {
  if (L.type != LType::Elements) return true;
  const int32_t m = js_to_int32(L.pe.eModel); return m >= 0 && m <= 2;
}

void eng_layer_pe_from_json(EngLayer& L, Json j) {
  auto num = [&](const char* k, double& d) { Json v = j.get(k); if (v.isNum()) d = v.asNum(); };
  EngPE& p = L.pe;
  num("pEng", p.pEng); num("pHarm", p.pHarm); num("pTim", p.pTim); num("pMor", p.pMor); num("pDecay", p.pDecay); num("pLpg", p.pLpg); num("pFm", p.pFm); num("pTimEnv", p.pTimEnv); num("pMorEnv", p.pMorEnv);
  Json st = j.get("pStrike"); if (st.isBool()) p.pStrike = st.asBool() ? 1 : 0; else if (st.isNum()) p.pStrike = st.asNum();
  Json ax = j.get("pAux"); if (ax.isBool()) p.pAux = ax.asBool() ? 1 : 0; else if (ax.isNum()) p.pAux = ax.asNum();
  num("eModel", p.eModel); num("eStrength", p.eStrength); num("eContour", p.eContour); num("eBow", p.eBow); num("eBlow", p.eBlow); num("eStrike", p.eStrike); num("eBowT", p.eBowT); num("eFlow", p.eFlow);
  num("eBlowT", p.eBlowT); num("eMallet", p.eMallet); num("eStrikeT", p.eStrikeT); num("eGeo", p.eGeo); num("eBright", p.eBright); num("eDamp", p.eDamp); num("ePos", p.ePos); num("eSpace", p.eSpace);
}

#ifdef THUNDER_ENGINE_PE
void PeOsc::init(PeUse& use, const EngLayer& L, const EngVoice& V, double sr, double dur, double pA, double ptau) {
  using namespace thunder_pe;
  Workspace& w = ws(); prepare(w);
  if (!use.started) { session_reset(w.sess); use.started = true; }   // a fresh instance's memory at the start of the voice
  Session* S = w.sess; const EngPE& P = L.pe;
  const bool pl = L.type == LType::Plaits;
  const double rate = pl ? 48000 : 32000, CH = pl ? 48 : 32, cap = pl ? (double)kPlaitsMax : (double)kElementsMax;
  // const N = Math.min(Math.ceil(dur * rate / CH) * CH, rate * 8), out = new Float32Array(N): NaN gives an empty array, a negative length throws
  const double Nd = jmin(std::ceil(dur * rate / CH) * CH, cap);
  size_t N = 0;
  if (Nd == Nd) { if (Nd < 0) { use.bad = true; d = nullptr; n = 0; k = rate / sr; return; } N = (size_t)Nd; }
  std::memset(w.pre, 0, N * sizeof(float));
  const double base = 36 + L.tune + V.tune + L.fine / 100, gateS = jmax(0.002, V.gate / 1000);
  if (pl) {
    const int eng = js_to_int32(P.pEng); const bool strike = truthy(P.pStrike), aux = truthy(P.pAux);
    pl_init(S);
    for (size_t off = 0; off < N; off += 48) {
      const double t = (double)off / 48000, note = noteAt(base, pA, t, ptau);
      pl_set(S, eng, (float)note, (float)P.pHarm, (float)P.pTim, (float)P.pMor, (float)P.pDecay, (float)P.pLpg, (float)P.pFm, (float)P.pTimEnv, (float)P.pMorEnv, strike ? 1 : 0);
      pl_trigger(S, strike && t < gateS ? 1.0f : 0.0f);
      pl_render(S, 48);
      const float* src = aux ? pl_aux(S) : pl_out(S);
      std::memcpy(w.pre + off, src, 48 * sizeof(float));
    }
  } else {
    const int model = js_to_int32(P.eModel);
    el_init(S);
    for (size_t off = 0; off < N; off += 32) {
      const double t = (double)off / 32000, note = noteAt(base, pA, t, ptau);
      el_set(S, model, (float)note, (float)P.eStrength, (float)P.eContour, (float)P.eBow, (float)P.eBlow, (float)P.eStrike, (float)P.eBowT, (float)P.eFlow, (float)P.eBlowT,
             (float)P.eMallet, (float)P.eStrikeT, (float)P.eGeo, (float)P.eBright, (float)P.eDamp, (float)P.ePos, (float)P.eSpace);
      el_gate(S, t < gateS ? 1 : 0);
      el_process(S, 32);
      const float* a = el_main(S); const float* b = el_aux(S);
      for (int i = 0; i < 32; i++) w.pre[off + (size_t)i] = (float)(((double)a[i] + (double)b[i]) * 0.5);
    }
  }
  d = w.pre; n = N; k = rate / sr; pos = 0;
}
#else
void PeOsc::init(PeUse& use, const EngLayer&, const EngVoice&, double sr, double, double, double) { use.bad = true; d = nullptr; n = 0; k = 1 / sr; }   // unreachable: the types are refused first
#endif

double PeOsc::s() {
  const int32_t i = js_to_int32(pos); pos += k;
  if ((double)i >= (double)n - 1) return 0;   // `i >= n - 1` (n = 0: -1)
  const double f = pos - k - (double)i;
  return (double)d[i] + ((double)d[i + 1] - (double)d[i]) * f;
}

}  // namespace thunder
