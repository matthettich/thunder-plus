// U25: the Plaits / Elements layer types over the WHOLE of every slider's range, with no fenced region: every Plaits model 0..23 and every Elements model, every knob at 0, 1, 0.5, near the ends
// (0.9, 0.99, 1e-6) and random, FM / envelope amounts over -1..1, notes from far below to far above the range the page can reach (layer tune / fine / pitch envelope amount / voice tune), every
// sample rate the app uses, and non-finite or huge values in the fields a song file can hold (tune, fine, pitch amount, decay, gate, length ...). Also a knob OUTSIDE its range, and non-finite,
// which must be refused (kEngUnsupported), never rendered. Claim: no crash, no write outside the buffer, no read outside a table (run under ASan + UBSan in the sanitizer pass:
// -DTHUNDER_SANITIZE=ON), kEngOk for every in-range voice, the length within the bound, and EACH RENDER INSIDE A TIME LIMIT (FUZZ_LIMIT_S, default 60 s; the sanitizer build is several times slower).
// No reference and no hash. (Elements' String model blows up at some corners in the wasm too: how many renders are not finite is printed, not judged.)
//   test_engine_u25_fuzz [count]      FUZZ_TRACE=1 prints each case before it renders
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <vector>

#include "engine/engine.h"
#include "engine/layer_pe.h"

using namespace thunder;

static uint32_t rs = 25251;   // FUZZ_SEED changes it
static double rnd() { rs = rs * 1664525u + 1013904223u; return rs / 4294967296.0; }

static double knob() {   // a slider position: the ends, the middle, close to the ends (where the tables run out), or anywhere
  const double r = rnd();
  if (r < 0.2) return 0;
  if (r < 0.4) return 1;
  if (r < 0.5) return 0.5;
  if (r < 0.58) return 0.9;
  if (r < 0.66) return 0.99;
  if (r < 0.72) return 1e-6;
  if (r < 0.78) return 0.999999;
  return rnd();
}
static double bip() { return knob() * 2 - 1; }   // -1 .. 1
static double hostile() {   // what a hand-edited song file can hold in a numeric field
  static const double h[] = {NAN, INFINITY, -INFINITY, 1e30, -1e30, 1e300, -1e300, 4e9, -4e9, 1e-300, 0, -0.0, 1e9, -1e9};
  return h[(int)(rnd() * (sizeof h / sizeof h[0]))];
}
static double badKnob(bool plaits) {   // a value no slider holds (and the refusal must catch it)
  static const double h[] = {NAN, INFINITY, -INFINITY, 1e30, -1e30, 1.0001, -1.5, -0.001, 1e300};
  return h[(int)(rnd() * (double)(sizeof h / sizeof h[0] - (plaits ? 2 : 0)))];   // -0.001 and 1e300 are the last two: -0.001 is a valid Plaits envelope amount, so Plaits stops short of it
}
static bool gHostile = false;   // a non-finite or huge value went into this voice: kEngUnsupported is then as good as kEngOk (the note is refused, never rendered)
static double wide(double lo, double hi) {
  if (rnd() >= 0.04) return lo + rnd() * (hi - lo);
  const double h = hostile(); if (!(std::fabs(h) < 1000)) gHostile = true;
  return h;
}

int main(int argc, char** argv) {
  if (!pe_available()) { std::printf("fuzz U25: no Plaits / Elements library on this platform: skipped\nPASS test_engine_u25_fuzz\n"); return 0; }
  const int count = argc > 1 ? std::atoi(argv[1]) : 2400;
  const double limitS = std::getenv("FUZZ_LIMIT_S") ? std::atof(std::getenv("FUZZ_LIMIT_S")) : 60.0;
  if (std::getenv("FUZZ_SEED")) rs = (uint32_t)std::strtoul(std::getenv("FUZZ_SEED"), nullptr, 10);
  eng_init();
  static const double srs[] = {22050, 32000, 44100, 48000, 96000};
  int fails = 0, renders = 0, refusedOk = 0, refusedHostile = 0, nonFinite = 0, byModel[24] = {}, byElModel[3] = {}; double slowest = 0; std::vector<float> buf;
  for (int i = 0; i < count; i++) {
    gHostile = false; EngVoice V; V.len = rnd() < 0.05 ? wide(0, 3) : 0.2 + rnd() * 0.5; V.gate = rnd() < 0.05 ? wide(0, 400) : 50 + rnd() * 300; V.tune = rnd() < 0.1 ? wide(-48, 48) : (double)(int)(rnd() * 24 - 12); V.drive = rnd() < 0.2 ? rnd() : 0;
    const int nl = 1 + (int)(rnd() * 3); bool outOfRange = false;
    for (int k = 0; k < nl; k++) {
      EngLayer& L = V.layers[k]; L.on = true; L.type = (i + k) % 3 == 2 ? LType::Elements : LType::Plaits; L.level = 0.3 + rnd() * 0.7; L.seed = 1 + (double)(int)(rnd() * 4e9);
      L.tune = rnd() < 0.08 ? wide(-300, 300) : (double)(int)(rnd() * 192 - 96); L.fine = rnd() < 0.08 ? wide(-300, 300) : (double)(int)(rnd() * 200 - 100);
      L.pAmt = rnd() < 0.5 ? (rnd() < 0.06 ? wide(-300, 300) : (double)(int)(rnd() * 192 - 96)) : 0; L.pDec = rnd() < 0.05 ? wide(0, 500) : 5 + rnd() * 300; L.atk = 0.1 + rnd() * 20; L.dec = 50 + rnd() * 1500;
      L.adsr = rnd() < 0.4; L.sus = rnd(); L.rel = 5 + rnd() * 300; if (rnd() < 0.2) { L.lpCut = 200 + rnd() * 8000; L.lpRes = rnd(); }
      EngPE& P = L.pe;
      if (L.type == LType::Plaits) { P.pEng = (double)((i / 3 + k * 7) % 24); P.pHarm = knob(); P.pTim = knob(); P.pMor = knob(); P.pDecay = knob(); P.pLpg = knob(); P.pFm = bip(); P.pTimEnv = bip(); P.pMorEnv = bip(); P.pStrike = rnd() < 0.6; P.pAux = rnd() < 0.3;
        byModel[(int)P.pEng]++;
        if (i % 40 == 39 && k == 0) { double* e[] = {&P.pHarm, &P.pTim, &P.pMor, &P.pDecay, &P.pLpg, &P.pFm, &P.pTimEnv, &P.pMorEnv}; *e[(int)(rnd() * 8)] = badKnob(true); outOfRange = true; } }
      else { P.eModel = (double)(int)(rnd() * 3); byElModel[(int)P.eModel]++; double* e[] = {&P.eStrength, &P.eContour, &P.eBow, &P.eBlow, &P.eStrike, &P.eBowT, &P.eFlow, &P.eBlowT, &P.eMallet, &P.eStrikeT, &P.eGeo, &P.eBright, &P.eDamp, &P.ePos, &P.eSpace};
        for (double* x : e) *x = knob();
        if (i % 40 == 39 && k == 0) { *e[(int)(rnd() * 15)] = badKnob(false); outOfRange = true; } }
    }
    EngRenderOpts o; o.sr = srs[i % 5]; o.hold = rnd() < 0.4 ? 0.1 + rnd() * 0.4 : 0;
    if (std::getenv("FUZZ_TRACE")) { std::fprintf(stderr, "case %d sr %g hold %g len %g gate %g vtune %g:", i, o.sr, o.hold, V.len, V.gate, V.tune); for (int k = 0; k < nl; k++) { const EngLayer& L = V.layers[k]; const EngPE& P = L.pe; if (L.type == LType::Plaits) std::fprintf(stderr, " [plaits eng %g h %g t %g m %g tune %g fine %g pAmt %g pDec %g strike %g fm %g te %g me %g]", P.pEng, P.pHarm, P.pTim, P.pMor, L.tune, L.fine, L.pAmt, L.pDec, P.pStrike, P.pFm, P.pTimEnv, P.pMorEnv); else std::fprintf(stderr, " [elements %g tune %g pAmt %g]", P.eModel, L.tune, L.pAmt); } std::fprintf(stderr, "\n"); }
    const size_t bound = eng_render_bound(V, o);
    if (bound == 0) { if (outOfRange) { refusedOk++; continue; } if (gHostile) { refusedHostile++; continue; } std::printf("FAIL %d: bound 0\n", i); fails++; continue; }
    if (bound > ((size_t)1 << 27)) continue;   // a length of minutes: not a memory question (the allocation is the caller's)
    buf.assign(bound + 8, 12345.0f); size_t n = 0; const auto t0 = std::chrono::steady_clock::now();
    const int st = eng_render_voice(V, o, buf.data(), bound, &n); renders++;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); if (secs > slowest) slowest = secs;
    if (secs > limitS) { std::printf("FAIL %d: the render took %.1f s, over the limit of %.0f s\n", i, secs, limitS); fails++; }
    if (outOfRange) { if (st != kEngUnsupported) { std::printf("FAIL %d: a knob outside its range must be refused, status %d\n", i, st); fails++; } else refusedOk++; continue; }
    if (st == kEngUnsupported && gHostile) { refusedHostile++; continue; }
    if (st != kEngOk) { std::printf("FAIL %d: status %d\n", i, st); fails++; continue; }
    if (n > bound || buf[bound] != 12345.0f || buf[bound + 7] != 12345.0f) { std::printf("FAIL %d: length %zu, bound %zu or a write past it\n", i, n, bound); fails++; }
    for (size_t j = 0; j < n; j++) if (!std::isfinite(buf[j])) { nonFinite++; break; }
  }
  int minM = 1 << 30; for (int m = 0; m < 24; m++) if (byModel[m] < minM) minM = byModel[m];
  std::printf("fuzz U25: %d renders over the whole of the sliders' ranges (every Plaits model at least %d times, Elements models %d / %d / %d), %d out-of-range voices refused as required, %d voices with a non-finite or huge note refused, %d failed; %d renders not finite (printed, not judged); slowest render %.1f s\n",
    renders, minM, byElModel[0], byElModel[1], byElModel[2], refusedOk, refusedHostile, fails, nonFinite, slowest);
  if (!fails) std::printf("PASS test_engine_u25_fuzz\n");
  return fails ? 1 : 0;
}
