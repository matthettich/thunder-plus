// U10: the delays in the mixer (mix/delay.h, Mixer::setDelays) and the offline renderer's trim.
//   1. an impulse / noise through a strip with a 2048-sample plan: every output sample is the no-plan output 2048 samples later, bit for bit
//   2. a latent track effect (a 2048-sample delay in the track's effect hook) and a plan: the latent track and a plain one land together
//   3. the same through a bus: two sources into a bus, one latent: the plan's f delays the other one's feed, the bus hears both together
//   4. a plan changed while playing: live (a 50 ms glide: nothing before it, the whole delay after it), AUTO idle (0 until a song plays), off
//   5. a delay over the cap is cut to it and counted; a zero plan is the mixer with no plan, bit for bit
//   6. the renderer: ELECTRO with a made-up latent slot on an empty track, rendered trimmed, equals the plain render bit for bit
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mix/mix.h"
#include "mix/pdc.h"
#include "render/render.h"

using namespace thunder;

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%-84s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }

struct Rng { uint32_t s = 12345; double next() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0 * 2 - 1; } };

// a track effect with a latency: a plain delay of `n` samples (what a look-ahead effect is, to the plan)
struct LatFx { DelayRing r; int n; LatFx(int n_, int maxB) : n(n_) { r.init(n_, maxB); } };
static void latHook(void* u, int, float* L, float* R, int N, double, const MixFeed*, int) { LatFx* f = (LatFx*)u; f->r.push(L, R, N); f->r.read(f->n, L, R, N); }
static void sumHook(void*, int, float* L, float* R, int N, double, const MixFeed* fd, int nf) { for (int i = 0; i < N; i++) for (int j = 0; j < nf; j++) { L[i] += fd[j].L[i]; R[i] += fd[j].R[i]; } }

constexpr int MB = 128;
// feeds `sig` (per track: a function of the frame) through the mixer in blocks of the given sizes; returns the L and R outputs
struct Run { std::vector<float> L, R; };
template <class Sig, class Pre>
static Run play(Mixer& M, int frames, const Sig& sig, const int* sizes, int nsizes, const Pre& pre) {
  Run o; o.L.resize((size_t)frames); o.R.resize((size_t)frames); int f = 0, k = 0;
  while (f < frames) { const int N = std::min(sizes[k++ % nsizes], frames - f); M.setTime((double)f / 48000); pre(f);
    for (int t = 0; t < kMixTracks; t++) for (int i = 0; i < N; i++) { float l = 0, r = 0; sig(t, f + i, &l, &r); M.inputsL()[t][i] = l; M.inputsR()[t][i] = r; }
    M.render(&o.L[(size_t)f], &o.R[(size_t)f], N, (double)f / 48000); f += N; }
  return o;
}
static const int SZ[] = {128, 100, 37, 64, 1, 128, 90};
static const auto noPre = [](int) {};
static std::vector<float> noise(int n, uint32_t seed) { Rng g; g.s = seed; std::vector<float> v((size_t)n); for (auto& x : v) x = (float)g.next(); return v; }

static void apply(Mixer& M, const PdcPlan& p, bool live, bool playing, PdcMode mode = PdcMode::Auto, bool now = false) { PdcApply a; a.mode = mode; a.live = live; a.playing = playing; a.now = now; pdc_apply(M, p, a); }

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }

int main() {
  const int NF = 9000;
  // ---- 1. a delay point: x[n - 2048] exactly -----------------------------------------------------------------------------------------
  {
    const std::vector<float> a = noise(NF, 1), b = noise(NF, 2);
    auto sig = [&](int t, int n, float* l, float* r) { if (t == 3) { *l = a[(size_t)n]; *r = b[(size_t)n]; } };
    Mixer ref(48000, MB); Run A = play(ref, NF, sig, SZ, 7, noPre);
    Mixer M(48000, MB); BusPlan P; M.setPlan(P);
    PdcPlan plan; plan.on = true; plan.T = 2048.0 / 48000; for (int t = 0; t < kMixTracks; t++) plan.d[t] = 2048.0 / 48000;   // (2048 / 48000 * 48000 is not exactly 2048)
    M.setTime(0); apply(M, plan, false, false);
    Run B = play(M, NF, sig, SZ, 7, noPre);
    bool exact = true, lead0 = true; for (int i = 0; i < NF; i++) { if (i < 2048) { if (B.L[(size_t)i] != 0 || B.R[(size_t)i] != 0) lead0 = false; } else if (B.L[(size_t)i] != A.L[(size_t)i - 2048] || B.R[(size_t)i] != A.R[(size_t)i - 2048]) exact = false; }
    check(lead0, "plan 2048: silence for the first 2048 samples");
    check(exact, "plan 2048: output[n] == no-plan output[n - 2048], bit for bit (mixed block sizes 1..128)");
    // one impulse: lands exactly 2048 late
    auto imp = [&](int t, int n, float* l, float*) { if (t == 3 && n == 100) *l = 1; };
    Mixer M2(48000, MB); M2.setPlan(P); M2.setTime(0); apply(M2, plan, false, false); Run C = play(M2, NF, imp, SZ, 7, noPre);
    int first = -1, count = 0; for (int i = 0; i < NF; i++) if (C.L[(size_t)i] != 0) { if (first < 0) first = i; count++; }
    check(first == 100 + 2048 && count == 1 && std::fabs(C.L[(size_t)first] - 1.0f) < 1e-6f, "an impulse at 100 lands at 2148, once, at full level");
  }
  // ---- 2. a latent effect on a track + the plan: the two tracks land together ------------------------------------------------------
  {
    auto imp = [&](int t, int n, float* l, float*) { if ((t == 4 || t == 7) && n == 100) *l = 1; };
    auto run = [&](bool t4, bool t7, bool withPlan) { Mixer M(48000, MB); BusPlan P; M.setPlan(P); LatFx fx(2048, MB); M.setFx(4, latHook, &fx);
      if (withPlan) { PdcSlot s{4, 0, true, true, 2048}; PdcPlan plan = pdc_plan(P, &s, 1, 48000, PdcMode::Auto); M.setTime(0); apply(M, plan, false, false); }
      auto sg = [&](int t, int n, float* l, float* r) { if ((t == 4 && t4) || (t == 7 && t7)) imp(t, n, l, r); }; return play(M, NF, sg, SZ, 7, noPre); };
    Run a = run(true, false, true), b = run(false, true, true), ab = run(true, true, true), nocomp = run(false, true, false);
    int ia = -1, ib = -1, in = -1; for (int i = 0; i < NF; i++) { if (a.L[(size_t)i] != 0 && ia < 0) ia = i; if (b.L[(size_t)i] != 0 && ib < 0) ib = i; if (nocomp.L[(size_t)i] != 0 && in < 0) in = i; }
    check(ia == 2148 && ib == 2148, "latent effect on track 4 (2048) and a plain track 7 with the plan: both land at 2148");
    check(in == 100, "(without the plan the plain track lands at 100: 2048 before the latent one)");
    bool sum = true; for (int i = 0; i < NF; i++) if (ab.L[(size_t)i] != a.L[(size_t)i] + b.L[(size_t)i] || ab.R[(size_t)i] != a.R[(size_t)i] + b.R[(size_t)i]) sum = false;
    check(sum, "the two together are the sum of the two alone, bit for bit (in phase)");
  }
  // ---- 3. through a bus: the feed delay (fdl) -------------------------------------------------------------------------------------------
  {
    BusPlan P; P.feeds[3][0] = 1; P.feeds[3][1] = 2; P.nFeeds[3] = P.feedLen[3] = 2; P.feedMask[3] = 6; P.targets[1] = P.targets[2] = 8; P.pure = 6; P.pre = 0;
    auto run = [&](bool s1, bool s2, uint32_t pre) { P.pre = pre; Mixer M(48000, MB); M.setPlan(P); M.setChain(3, sumHook, nullptr); LatFx fx(2048, MB); M.setFx(1, latHook, &fx);
      PdcSlot s{1, 0, true, true, 2048}; PdcPlan plan = pdc_plan(P, &s, 1, 48000, PdcMode::Auto); M.setTime(0); apply(M, plan, false, false);
      auto sg = [&](int t, int n, float* l, float*) { if (n == 100 && ((t == 1 && s1) || (t == 2 && s2))) *l = 1; }; return play(M, NF, sg, SZ, 7, noPre); };
    for (uint32_t pre : {0u, 4u}) {   // source 2 heard POST (fdl) / PRE (fdlP)
      Run a = run(true, false, pre), b = run(false, true, pre), ab = run(true, true, pre);
      int ia = -1, ib = -1, na = 0, nb = 0; for (int i = 0; i < NF; i++) { if (a.L[(size_t)i] != 0) { if (ia < 0) ia = i; na++; } if (b.L[(size_t)i] != 0) { if (ib < 0) ib = i; nb++; } }
      bool sum = true; for (int i = 0; i < NF; i++) if (ab.L[(size_t)i] != a.L[(size_t)i] + b.L[(size_t)i]) sum = false;
      char w[160]; std::snprintf(w, sizeof w, "bus, source 1 latent, source 2 %s: both land at 2148 (%d, %d), once each, and sum exactly", pre ? "PRE (fdlP)" : "POST (fdl)", ia, ib);
      check(ia == 2148 && ib == 2148 && na == 1 && nb == 1 && sum, w);
    }
  }
  // ---- 4. a plan changed while playing ------------------------------------------------------------------------------------------------
  {
    const std::vector<float> a = noise(NF * 4, 5);
    auto sig = [&](int t, int n, float* l, float* r) { if (t == 3) { *l = a[(size_t)n]; *r = a[(size_t)n] * 0.5f; } };
    BusPlan P; PdcPlan plan; plan.on = true; plan.T = 2048.0 / 48000; for (int t = 0; t < kMixTracks; t++) plan.d[t] = 2048.0 / 48000;
    const int F = NF * 4; Mixer ref(48000, MB); Run A = play(ref, F, sig, SZ, 7, noPre);
    // live, playing: the plan arrives at frame 9000 and glides over 50 ms = 2400 frames
    Mixer M(48000, MB); M.setPlan(P); bool done = false;
    Run B = play(M, F, sig, SZ, 7, [&](int f) { if (!done && f >= 9000) { done = true; apply(M, plan, true, true); } });
    // the first block at or after 9000 starts at fs; the glide runs fs .. fs + 2400; after that the delay is exactly 2048
    int fs = -1; { int f = 0, k = 0; while (f < F) { const int N = std::min(SZ[k++ % 7], F - f); if (f >= 9000 && fs < 0) fs = f; f += N; } }
    bool before = true, after = true; for (int i = 0; i < fs; i++) if (B.L[(size_t)i] != A.L[(size_t)i]) before = false;
    for (int i = fs + 2400 + 1; i < F; i++) if (B.L[(size_t)i] != A.L[(size_t)i - 2048]) after = false;
    check(before && after, "live glide: untouched before the change, x[n - 2048] exactly from 50 ms after it");
    double worst = 0; for (int i = fs; i < fs + 2400; i++) { const double lo = std::min(A.L[(size_t)i], A.L[(size_t)i - 2048]); (void)lo; worst = std::max(worst, (double)std::fabs(B.L[(size_t)i])); }
    check(worst > 0 && worst < 4, "the glide itself plays (a moving delay: not silent, not wild)");
    // AUTO live while stopped: idle, every delay 0; then PLAY (now: 10 ms) sets it
    Mixer M3(48000, MB); M3.setPlan(P); M3.setTime(0); apply(M3, plan, true, false, PdcMode::Auto);
    Run C = play(M3, 4000, sig, SZ, 7, noPre); bool same = true; for (int i = 0; i < 4000; i++) if (C.L[(size_t)i] != A.L[(size_t)i]) same = false;
    check(same && M3.delayNow(3, 0) == 0, "AUTO live and stopped: idle, all delays 0, output equals the no-plan mixer");
    Mixer M4(48000, MB); M4.setPlan(P); M4.setTime(0); apply(M4, plan, true, false, PdcMode::On);
    check(M4.delayNow(3, 0) == plan.d[3], "mode ON live and stopped: the plan is in force");
    Mixer M5(48000, MB); M5.setPlan(P); M5.setTime(0); apply(M5, plan, false, false, PdcMode::Auto); M5.setTime(1.0); PdcPlan none; apply(M5, none, false, false, PdcMode::Off);
    check(M5.delayNow(3, 0) == 0, "mode OFF: the plan is all zeros, the delays go");
  }
  // ---- 5. the cap, and a zero plan --------------------------------------------------------------------------------------------------
  {
    BusPlan P; PdcPlan big; big.on = true; big.T = 1.25; for (int t = 0; t < kMixTracks; t++) big.d[t] = t == 3 ? 1.25 : 0;
    Mixer M(48000, MB); M.setPlan(P); M.setTime(0); apply(M, big, false, false);
    check(M.delayNow(3, 0) == kPdcMaxSec && M.pdcClamped() == 1, "a 1.25 s delay is cut to the JS's 0.99 s and counted once");
    Mixer M2(48000, MB, 0.25); M2.setPlan(P); M2.setTime(0); apply(M2, big, false, false); PdcPlan mid = big; mid.d[3] = 0.5; apply(M2, mid, false, false);
    check(M2.delayNow(3, 0) == 0.25 && M2.pdcClamped() == 2, "a mixer built with a 0.25 s cap cuts to that (two requests over it)");
    BusPlan BP; BP.feeds[3][0] = 1; BP.feeds[3][1] = 2; BP.nFeeds[3] = BP.feedLen[3] = 2; BP.feedMask[3] = 6; BP.targets[1] = BP.targets[2] = 8; BP.pure = 6; BP.pre = 2;
    const std::vector<float> a = noise(NF, 9); auto sig = [&](int t, int n, float* l, float* r) { if (t < 4) { *l = a[(size_t)((n + t * 977) % NF)]; *r = -*l * 0.7f; } };
    auto run = [&](bool zero) { Mixer m(48000, MB); m.setPlan(BP); m.setChain(3, sumHook, nullptr); if (zero) { PdcPlan z = pdc_plan(BP, nullptr, 0, 48000, PdcMode::Auto); m.setTime(0); apply(m, z, false, false); } return play(m, NF, sig, SZ, 7, noPre); };
    Run x = run(false), y = run(true); bool eq = true; for (int i = 0; i < NF; i++) if (x.L[(size_t)i] != y.L[(size_t)i] || x.R[(size_t)i] != y.R[(size_t)i]) eq = false;
    check(eq, "a plan with no latency anywhere gives the mixer's output bit for bit (buses, PRE and POST feeds)");
  }
  // ---- 6. the renderer trims as pass() does ----------------------------------------------------------------------------------------------
  {
    std::string text; if (!readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder", &text)) { check(false, "cannot read sounds/ELECTRO.thunder"); return 1; }
    Song song; JsonError je; if (!song.load(text.data(), text.size(), &je)) { check(false, "ELECTRO does not load"); return 1; }
    SeqVars vars; static RenderPads pads; const int PN = 64, NS = 24000; std::vector<float> pd((size_t)PN * NS);
    for (int p = 0; p < PN; p++) { float* d = &pd[(size_t)p * NS]; for (int i = 0; i < NS; i++) d[i] = (float)std::sin(0.01 * (p % 7 + 1) * i) * (1.0f - (float)i / NS) * 0.3f; pads.buf[p].data = d; pads.buf[p].len = NS; pads.buf[p].sr = 48000; pads.kit[p].syn = p >= 8; pads.kit[p].rel = p >= 8 ? 0.1 : 0; pads.kit[p].choke = p == 3 ? 1 : 0; }
    SeqOpts o; o.once = true; o.row = song.renderFrom; const int N = 48000 * 4, B = 512;
    std::vector<float> aL((size_t)N), aR((size_t)N), bL((size_t)N), bR((size_t)N);
    { SongRenderer R(song, vars, pads, 48000, B, o); for (int f = 0; f < N; f += B) R.block(&aL[(size_t)f], &aR[(size_t)f], std::min(B, N - f)); }
    SongRenderer R(song, vars, pads, 48000, B, o); const BusPlan BP;   // (ELECTRO's own plan has pure sources: pdcPlan counts them routed, but no bus exists here until a chains host is plugged in (U14), so they would play early, as in the page when the chains node is missing)
    PdcSlot s{31, 0, true, true, 2048};   // a made-up latent effect on track 32 (empty in ELECTRO): everything else waits for it
    const PdcPlan plan = pdc_plan(BP, &s, 1, 48000, PdcMode::Auto); R.setPlan(plan, PdcMode::Auto, 0);
    check(R.latencyFrames() == 2048 && pdc_trim_frames(plan, 48000) == 2048, "ELECTRO with a latent slot: the render is 2048 frames longer, trimmed");
    R.renderTrimmed(bL.data(), bR.data(), N);
    // The master's own lanes (MASTER VOL, the song's MVOL / MOUT) sit AFTER the delays, so a 4 ms approach the song starts with at t = 0 acts on the
    // delayed sound 2048 samples later: the first 4 time constants (191 frames) differ, exactly as in the page. Past them the render is the plain one.
    const int SKIP = 960; bool eq = true; int firstBad = -1, nd = 0, ndEarly = 0;
    for (int i = 0; i < N; i++) if (aL[(size_t)i] != bL[(size_t)i] || aR[(size_t)i] != bR[(size_t)i]) { if (i < SKIP) ndEarly++; else { eq = false; nd++; if (firstBad < 0) firstBad = i; } }
    check(eq, "ELECTRO, 4 s: trimmed render == plain render, bit for bit from frame 960 on");
    if (!eq) std::printf("  %d frames differ, first at %d\n", nd, firstBad);
    check(ndEarly > 0 && ndEarly < 200, "  (and only the master lanes' opening 4 ms approach differs before it: 191 frames)");
    check(R.mixer().pdcClamped() == 0, "no delay was cut to the cap");
  }
  std::printf("pdcmix: %d failed\n", fails);
  return fails ? 1 : 0;
}
