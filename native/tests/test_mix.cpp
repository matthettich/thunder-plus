// U09: the mixer (core/src/mix) against reference A: the JS model of the page's Web Audio strip (tools/mix-dump.mjs, itself checked
// against real Web Audio in Chrome: tools/mix-webaudio-check.mjs, result in golden/mix-webaudio-check.txt).
//   test_mix <dir>     for every case of golden/mix_golden.inc: <dir>/<case>.json (the song's mixer part, the inputs' shape, the
//                      commands), <case>.in.f32 (the tracks' inputs) and <case>.f32 (the JS's master output). The C++ plays the same
//                      inputs and commands one block at a time. Gate: the difference at or below -80 dB of the reference's peak;
//                      the figure is printed, and whether the output is the JS's bit for bit (and the committed hash).
//   test_mix           no reference (no Node): lists the cases; nothing can be said of the C++.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mix/mix.h"
#include "rt/json.h"
#include "golden/mix_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }

// the stand-in chains node of the reference: the track's own input and every source, 1:1
static void standIn(void*, int, float* L, float* R, int N, double, const MixFeed* fd, int nf) {
  for (int i = 0; i < N; i++) for (int j = 0; j < nf; j++) { L[i] = L[i] + fd[j].L[i]; R[i] = R[i] + fd[j].R[i]; }
}

struct Result { bool ran = false; double db = 0; bool exact = false, hashOk = false; uint32_t dropped = 0; int peakEv = 0; double peak = 0; };

static Result runCase(const std::string& dir, const MixGolden& G) {
  Result R; std::string js, in, ref;
  if (!readFile(dir + "/" + G.name + ".json", &js) || !readFile(dir + "/" + G.name + ".in.f32", &in) || !readFile(dir + "/" + G.name + ".f32", &ref)) return R;
  JsonDoc doc; if (!doc.parse(js)) return R;
  Json C = doc.root(), S = C.get("S");
  const int N = (int)C.num("N"), blocks = (int)C.num("blocks"); const double sr = C.num("sr"); const size_t n = (size_t)N * (size_t)blocks;
  Json ins = C.arr("ins");
  if (ref.size() != n * 2 * 4 || in.size() != ins.size() * n * 2 * 4) return R;
  const float* refF = (const float*)ref.data(); const float* inF = (const float*)in.data();
  Mixer M(sr, N);
  bool chainOn[kSongNT]; Json co = S.arr("chainOn"), mix = S.arr("mix");
  for (int t = 0; t < kSongNT; t++) { chainOn[t] = co.at((size_t)t).asBool(); Json m = mix.at((size_t)t); M.setTrack(t, m.num("vol", 224), m.num("trim", 128), m.num("pan", 128), m.boolean("mute"), m.boolean("solo")); }
  Json patch = S.get("chainPatch");
  M.setPlan(bus_plan(patch, chainOn));
  if (patch.str("format") == "chains.patch") { const uint32_t cf = chains_feed(patch, chainOn); for (int t = 0; t < kSongNT; t++) if (cf >> t & 1u) M.setChain(t, standIn, nullptr); }
  M.setMaster(S.num("master", 255), S.num("mout", 255)); M.apply();
  for (size_t k = 0; k < ins.size(); k++) M.setMono((int)ins.at(k).num("t"), ins.at(k).boolean("mono"));
  Json cmds = C.arr("cmds");
  std::vector<float> out(n * 2), oL((size_t)N), oR((size_t)N);
  for (int b = 0; b < blocks; b++) {
    const double cur = (double)b * N / sr; M.setTime(cur);
    for (size_t k = 0; k < cmds.size(); k++) { Json c = cmds.at(k); if ((int)c.num("b") != b) continue;
      if (c.str("op") == "ev") { SeqEvent e; const std::string_view ty = c.str("type");
        e.type = ty == "tvl" ? SEV_TVL : ty == "tva" ? SEV_TVA : ty == "tpn" ? SEV_TPN : ty == "mvol" ? SEV_MVOL : SEV_MOUT; e.t = c.num("t"); e.v = c.num("v"); e.track = (int32_t)c.num("track", -1); M.event(e); }
      else { MixSet s; s.tr = (int)c.num("tr"); const std::string_view k2 = c.str("k");
        s.k = k2 == "vol" ? MixKey::Vol : k2 == "trim" ? MixKey::Trim : k2 == "pan" ? MixKey::Pan : k2 == "mute" ? MixKey::Mute : k2 == "solo" ? MixKey::Solo : k2 == "out" ? MixKey::Out : MixKey::Send;
        s.v = c.num("v"); s.ramp = c.num("ramp", -1); s.at = c.num("at", -1); M.set(s); } }
    for (size_t k = 0; k < ins.size(); k++) { const int t = (int)ins.at(k).num("t"); const float* src = inF + k * 2 * n + (size_t)b * (size_t)N;
      std::memcpy(M.inputsL()[t], src, sizeof(float) * (size_t)N); std::memcpy(M.inputsR()[t], src + n, sizeof(float) * (size_t)N); }
    M.render(oL.data(), oR.data(), N, cur);
    std::memcpy(out.data() + (size_t)b * (size_t)N, oL.data(), sizeof(float) * (size_t)N); std::memcpy(out.data() + n + (size_t)b * (size_t)N, oR.data(), sizeof(float) * (size_t)N);
  }
  double pk = 0, md = 0; for (size_t i = 0; i < n * 2; i++) { pk = std::max(pk, (double)std::fabs(refF[i])); md = std::max(md, (double)std::fabs(refF[i] - out[i])); }
  R.ran = true; R.peak = pk; R.exact = std::memcmp(refF, out.data(), n * 2 * 4) == 0; R.db = md == 0 ? -INFINITY : 20 * std::log10(md / (pk > 0 ? pk : 1));
  R.hashOk = fnv(out.data(), out.size()) == G.hash && (int)out.size() == G.floats; R.dropped = M.autoDropped(); R.peakEv = M.peakEvents();
  return R;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("mix: no reference directory (Node not found): %d cases not run\n", kMixGoldenN); for (int i = 0; i < kMixGoldenN; i++) std::printf("  %s (not checked)\n", kMixGolden[i].name); return 0; }
  int fails = 0, exact = 0; double worst = -INFINITY;
  std::printf("%-18s %10s  %-9s %s\n", "case", "dB", "bit-exact", "(longest lane)");
  for (int i = 0; i < kMixGoldenN; i++) {
    const Result R = runCase(argv[1], kMixGolden[i]);
    if (!R.ran) { std::printf("%-18s could not be run (reference files missing or malformed)  FAIL\n", kMixGolden[i].name); fails++; continue; }
    const bool ok = R.db <= -80 && R.dropped == 0; if (!ok) fails++; if (R.exact) exact++; if (R.db > worst) worst = R.db;
    char db[32]; if (R.exact) std::snprintf(db, sizeof db, "exact"); else std::snprintf(db, sizeof db, "%.1f", R.db);
    std::printf("%-18s %10s  %-9s (%d)%s%s%s\n", kMixGolden[i].name, db, R.exact ? "yes" : "no", R.peakEv, R.exact != R.hashOk ? "  [committed hash disagrees with the reference run]" : "", R.dropped ? "  [automation events dropped]" : "", ok ? "" : "  FAIL");
  }
  std::printf("mix: %d cases, %d bit-exact, worst %s dB, %d failed (gate -80 dB)\n", kMixGoldenN, exact, std::isinf(worst) ? "exact" : std::to_string(worst).c_str(), fails);
  return fails ? 1 : 0;
}
