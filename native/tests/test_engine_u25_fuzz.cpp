// U25: the Plaits / Elements layer types over the corners of the sliders' ranges (knobs at 0, 1, 0.5 and random in range, every Plaits model, every Elements model, extreme notes and pitch envelopes, every
// sample rate the app uses): no crash, no write outside the buffer, no read outside a table (run under ASan + UBSan in the sanitizer pass: the upstream Mutable code reads one past its tables in places, see
// core/third_party/prepare.sh), the status is kEngOk, the length within the bound. No reference and no hash: the claim is memory safety on every input a song can hold. (Elements' String model blows up
// at some corners in the wasm too: how many renders are not finite is printed, not judged.)
//   test_engine_u25_fuzz [count]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/engine.h"
#include "engine/layer_pe.h"

using namespace thunder;

static uint32_t rs = 25251;
static double rnd() { rs = rs * 1664525u + 1013904223u; return rs / 4294967296.0; }
static double pick3() { const double r = rnd(); return r < 0.3 ? 0 : r < 0.6 ? 1 : r < 0.75 ? 0.5 : rnd(); }

int main(int argc, char** argv) {
  if (!pe_available()) { std::printf("fuzz U25: no Plaits / Elements library on this platform: skipped\nPASS test_engine_u25_fuzz\n"); return 0; }
  const int count = argc > 1 ? std::atoi(argv[1]) : 360;
  eng_init();
  static const double srs[] = {22050, 32000, 44100, 48000, 96000};
  int fails = 0, renders = 0, nonFinite = 0; std::vector<float> buf;
  for (int i = 0; i < count; i++) {
    EngVoice V; V.len = 0.2 + rnd() * 0.5; V.gate = 50 + rnd() * 300; V.tune = (double)(int)(rnd() * 24 - 12); V.drive = rnd() < 0.2 ? rnd() : 0;
    const int nl = 1 + (int)(rnd() * 3);
    for (int k = 0; k < nl; k++) {
      EngLayer& L = V.layers[k]; L.on = true; L.type = (i + k) % 3 == 2 ? LType::Elements : LType::Plaits; L.level = 0.3 + rnd() * 0.7; L.seed = 1 + (double)(int)(rnd() * 4e9);
      L.tune = (double)(int)(rnd() * 144 - 60); L.fine = (double)(int)(rnd() * 100 - 50); L.pAmt = rnd() < 0.4 ? (double)(int)(rnd() * 96 - 48) : 0; L.pDec = 5 + rnd() * 300; L.atk = 0.1 + rnd() * 20; L.dec = 50 + rnd() * 1500;
      L.adsr = rnd() < 0.4; L.sus = rnd(); L.rel = 5 + rnd() * 300; if (rnd() < 0.2) { L.lpCut = 200 + rnd() * 8000; L.lpRes = rnd(); }
      EngPE& P = L.pe;
      if (L.type == LType::Plaits) { P.pEng = (double)((i * 5 + k * 7) % 24); P.pHarm = pick3(); P.pTim = pick3(); P.pMor = pick3(); P.pDecay = pick3(); P.pLpg = pick3(); P.pFm = pick3() * 2 - 1; P.pTimEnv = pick3() * 2 - 1; P.pMorEnv = pick3() * 2 - 1; P.pStrike = rnd() < 0.6; P.pAux = rnd() < 0.3; }
      else { P.eModel = (double)(int)(rnd() * 3); double* e[] = {&P.eStrength, &P.eContour, &P.eBow, &P.eBlow, &P.eStrike, &P.eBowT, &P.eFlow, &P.eBlowT, &P.eMallet, &P.eStrikeT, &P.eGeo, &P.eBright, &P.eDamp, &P.ePos, &P.eSpace};
        for (double* x : e) *x = pick3(); }
    }
    EngRenderOpts o; o.sr = srs[i % 5]; o.hold = rnd() < 0.4 ? 0.1 + rnd() * 0.4 : 0;
    if (std::getenv("FUZZ_TRACE")) { std::fprintf(stderr, "case %d sr %g hold %g:", i, o.sr, o.hold); for (int k = 0; k < nl; k++) { const EngLayer& L = V.layers[k]; const EngPE& P = L.pe; if (L.type == LType::Plaits) std::fprintf(stderr, " [plaits eng %g h %g t %g m %g tune %g fine %g pAmt %g strike %g fm %g te %g me %g]", P.pEng, P.pHarm, P.pTim, P.pMor, L.tune, L.fine, L.pAmt, P.pStrike, P.pFm, P.pTimEnv, P.pMorEnv); else std::fprintf(stderr, " [elements %g tune %g pAmt %g]", P.eModel, L.tune, L.pAmt); } std::fprintf(stderr, "\n"); }
    const size_t bound = eng_render_bound(V, o); if (bound == 0) { std::printf("FAIL %d: bound 0\n", i); fails++; continue; }
    buf.assign(bound + 8, 12345.0f); size_t n = 0; const int st = eng_render_voice(V, o, buf.data(), bound, &n); renders++;
    if (st != kEngOk) { std::printf("FAIL %d: status %d\n", i, st); fails++; continue; }
    if (n > bound || buf[bound] != 12345.0f || buf[bound + 7] != 12345.0f) { std::printf("FAIL %d: length %zu, bound %zu or a write past it\n", i, n, bound); fails++; }
    for (size_t j = 0; j < n; j++) if (!std::isfinite(buf[j])) { nonFinite++; break; }
  }
  std::printf("fuzz U25: %d renders over the sliders' corners, %d failed; %d renders not finite (printed, not judged)\n", renders, fails, nonFinite);
  if (!fails) std::printf("PASS test_engine_u25_fuzz\n");
  return fails ? 1 : 0;
}
