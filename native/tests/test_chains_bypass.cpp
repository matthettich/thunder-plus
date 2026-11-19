// U31 verification round 2 (D5b): the Bypass crossfade across the blocks the live runner cuts at events, and a SECOND Bypass toggle arriving inside a fade that is still running: the output must stay continuous (no
// step bigger than the uncut fade's own), a harmless event next to a switch must not change one sample, and the LAST toggle must win. Adapted from the verifier's scratch program bypass5.cpp (which killed the mutation
// 'the carried fade follows the switches instead of its target', cengine.cpp, that no earlier test did). Phaser (stereo), Ensemble (stereo); host blocks of 128 and 37.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "chains/chains.h"
#include "rt/json.h"
using namespace thunder; using namespace thunder::chains;
static const int N = 12288;
static std::string patch(const std::string& type, bool stereo) {
  std::string cab = "{\"from\":[\"a\",\"out\"],\"to\":[\"m\",\"in\"]}"; if (stereo) cab += ",{\"from\":[\"a\",\"outR\"],\"to\":[\"m\",\"inR\"]}";
  cab += ",{\"from\":[\"m\",\"out\"],\"to\":[\"c\",\"in\"]}"; if (stereo) cab += ",{\"from\":[\"m\",\"outR\"],\"to\":[\"c\",\"inR\"]}";
  return "{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"m\",\"type\":\"" + type + "\",\"params\":{\"_bn\":3}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":1}}],\"cables\":[" + cab + "]}"; }
struct Ev { int64_t at; int kind; double a, b; };
static void run(const std::string& type, bool stereo, const std::vector<Ev>& evs, int hostBlock, std::vector<float>& L, std::vector<float>& R) {
  waves::Host host; host.setFixed(.3); ChainsLive live(48000, 0, ChainsLiveConfig(), &host); JsonDoc d; d.parse(patch(type, stereo));
  if (!live.loadPatch(d.root())) { std::printf("patch refused for %s\n", type.c_str()); std::exit(2); }
  for (const Ev& e : evs) { ChainsEvent ce; ce.kind = (ChainsEvent::Kind)e.kind; ce.at = e.at; ce.a = e.a; ce.b = e.b; if (!live.post(ce)) std::exit(3); }
  std::vector<float> xl(N), xr(N); for (int i = 0; i < N; i++) { xl[(size_t)i] = (float)(.5 * std::sin(i * 0.0587) + .2 * std::sin(i * 0.31)); xr[(size_t)i] = (float)(.45 * std::sin(i * 0.0413 + 1) + .15 * std::sin(i * 0.77)); }
  L.assign(N, 0); R.assign(N, 0);
  for (int p = 0; p < N; p += hostBlock) { const int n = std::min(hostBlock, N - p); NodeIn in; in.inL = &xl[(size_t)p]; in.inR = &xr[(size_t)p]; NodeOut out; out.L = &L[(size_t)p]; out.R = &R[(size_t)p]; live.process(in, out, n); }
}
static const Ev BYP(int64_t at, double v) { return Ev{at, (int)ChainsEvent::Bypass, 3, v}; }
static const Ev HARM(int64_t at) { return Ev{at, (int)ChainsEvent::Tempo, 120, 0}; }
static double maxStep(const std::vector<float>& x, int from = 300) { double m = 0; for (int i = from + 1; i < N; i++) m = std::max(m, (double)std::fabs(x[(size_t)i] - x[(size_t)i - 1])); return m; }
static double rmsDiff(const std::vector<float>& a, const std::vector<float>& b, int from, int to) { double s = 0; for (int i = from; i < to; i++) { const double d = (double)a[(size_t)i] - b[(size_t)i]; s += d * d; } return std::sqrt(s / (to - from)); }
int main() {
  struct T { const char* type; bool stereo; const char* what; } types[] = {{"phaser", true, "stereo"}, {"ensemble", true, "stereo2"}};
  int fails = 0, runs = 0;
  for (const T& t : types) for (int hb : {128, 37}) {
    // baselines
    std::vector<float> bL, bR, dL, dR;
    for (int dir = 0; dir < 2; dir++) for (int k : {1, 10, 64, 127}) {
      std::vector<Ev> A, B; const int64_t S = 1000 + 37;   // not a multiple of 128 on purpose
      if (dir == 0) { A = {BYP(S, 1)}; B = {BYP(S, 1), HARM(S + k)}; }
      else { A = {BYP(200, 1), BYP(3000 + 37, 0)}; B = {BYP(200, 1), BYP(3000 + 37, 0), HARM(3000 + 37 + k)}; }
      std::vector<float> aL, aR, cL, cR; run(t.type, t.stereo, A, hb, aL, aR); run(t.type, t.stereo, B, hb, cL, cR); runs += 2;
      const bool eq = aL == cL && aR == cR; if (!eq) { fails++; int fi = 0; while (aL[(size_t)fi] == cL[(size_t)fi] && aR[(size_t)fi] == cR[(size_t)fi]) fi++; std::printf("FAIL %s(%s) hb%d dir%d k%d: output differs with a harmless event (first diff at %d, max |dL| %.3g)\n", t.type, t.what, hb, dir, k, fi, [&] { double m = 0; for (int i = 0; i < N; i++) m = std::max(m, (double)std::fabs(aL[(size_t)i] - cL[(size_t)i])); return m; }()); }
    }
    // reference fade at an aligned (uncut) position, and continuity under second toggles
    std::vector<float> uL, uR; run(t.type, t.stereo, {BYP(1024, 1)}, 128, uL, uR); const double stepU = maxStep(uL), stepUR = maxStep(uR);
    std::vector<float> wL, wR; run(t.type, t.stereo, {}, hb, wL, wR); const double stepW = maxStep(wL);
    std::vector<float> yL, yR; run(t.type, t.stereo, {BYP(200, 1)}, hb, yL, yR); const double stepY = maxStep(yL);
    double worstRatio = 0; std::string tail;
    for (int mode = 0; mode < 2; mode++) for (int j : {1, 10, 64, 127, 200}) {
      std::vector<Ev> E = mode == 0 ? std::vector<Ev>{BYP(1037, 1), BYP(1037 + j, 0)} : std::vector<Ev>{BYP(200, 1), BYP(3037, 0), BYP(3037 + j, 1)};
      std::vector<float> eL, eR; run(t.type, t.stereo, E, hb, eL, eR); runs++;
      const double ref = std::max(std::max(stepU, stepW), stepY); const double st = maxStep(eL), stR = maxStep(eR); const double ratio = std::max(st, stR) / std::max(ref, std::max(stepUR, 1e-9)); worstRatio = std::max(worstRatio, ratio);
      // final state: mode 0 ends released (like the wet baseline), mode 1 ends bypassed (like the dry baseline)
      const double dw = rmsDiff(eL, wL, 9000, N - 200), dd = rmsDiff(eL, yL, 9000, N - 200);
      const bool endsRight = mode == 0 ? dw <= dd : dd <= dw;
      if (false) std::printf("  %s(%s) hb%d mode%d j%d: max step %.4g/%.4g (uncut ref %.4g wet %.4g dry %.4g) ratio %.2f; tail nearer to %s %s\n", t.type, t.what, hb, mode, j, st, stR, stepU, stepW, stepY, ratio, dw <= dd ? "WET" : "DRY", endsRight ? "(last toggle wins)" : "<-- WRONG final state");
      if (!endsRight) fails++; if (ratio > 1.5) { fails++; std::printf("FAIL discontinuity: ratio %.2f\n", ratio); }
    }
  }
  std::printf("bypass5: %d runs, %d failures\n", runs, fails); return fails ? 1 : 0; }
