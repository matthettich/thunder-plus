// M3 (ctest `live_chlvl`): the chains in / out level record (CHLVL, type 13), METERS-DESIGN 4.1 / 5a, owner's choice (a).
//  (1) layout: 40 bytes, offsets, type number, the spec grew to 40 bytes (chains_tracks last) and a caller with the old 32-byte struct is still accepted (it watches no chains track); E_ARG for chains_tracks above bit 31.
//  (2) peaks against a known signal through a chains patch (Track In -> Chain Out at 0.5): in = the pulse (0.8 L, 0.3 R), out = half of it.
//  (3) silence: the records are zeros. A track that is not watched posts nothing (track 1 has the same node and signal); a watched track with no node (5) posts nothing; nothing watched: no CHLVL, and the audio is bit-equal.
//  (4) the rate: chains_hz 10 / 0 (16) / 30 / above 30 (E_ARG); a replacement spec without CHLVL stops the records; an identical re-send does not disturb the pacing.
//  (5) ring order with METERS and FXLVL: one gapless `seq` order, METERS, then FXLVL, then CHLVL of one quantum.
#include <cstddef>

#include "chlvl_common.h"
#include "chains/chost.h"
using namespace lt;

static_assert(sizeof(thunder_live_ev_chlvl) == 40 && offsetof(thunder_live_ev_chlvl, track) == 16 && offsetof(thunder_live_ev_chlvl, reserved) == 20 && offsetof(thunder_live_ev_chlvl, in) == 24 && offsetof(thunder_live_ev_chlvl, out) == 32, "CHLVL layout");
static_assert(THUNDER_LIVE_EV_CHLVL == 13 && THUNDER_LIVE_EV_MAX >= sizeof(thunder_live_ev_chlvl), "type number");
static_assert(sizeof(thunder_live_meters_spec) == 40 && offsetof(thunder_live_meters_spec, chains_tracks) == 32, "the spec: size, kinds, rate_hz, fx_tracks, chains_hz, chains_tracks");

namespace {
constexpr int kQ = 600;
constexpr uint32_t CF = THUNDER_LIVE_F_CHAINS | THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER;
thunder_live_meters_spec spec(uint32_t kinds, uint32_t chTracks, double chHz = 0, double rate = 0, uint64_t fx = 0) {
  thunder_live_meters_spec s; std::memset(&s, 0, sizeof s); s.size = sizeof s; s.kinds = kinds; s.chains_tracks = chTracks; s.chains_hz = chHz; s.rate_hz = rate; s.fx_tracks = fx; return s;
}
struct Run { std::vector<float> l, r; std::vector<std::vector<uint8_t>> recs; ChTap tap; };
// quanta: total; sp (applied at start when kinds != 0 || force); sp2 applied at `at` (-1: never); silenceAt: from that quantum the input is silent
struct Cfg { bool apply = true; thunder_live_meters_spec sp{}; int quanta = kQ; int at = -1; thunder_live_meters_spec sp2{}; int silenceAt = -1; bool fx = false; };
bool go(const Cfg& c, Run& o) {
  Host H(opts(CF, 4096)); if (!H.h) { CHECK(false, "create"); return false; }
  CHECK(H.song(chlvlSong()) == 0, "song");
  H->setTestTap(chTapFn, &o.tap);
  CHECK(H->play(playOpts(THUNDER_LIVE_NOW)) == 0, "play");
  if (c.fx) { const std::string a = "[{\"type\":\"tape\",\"on\":true},{\"type\":\"comp\",\"on\":true}]"; CHECK(H->fxPlan(0, (const uint8_t*)a.data(), a.size(), 120) == 0 && H->fxCommit(0, -1) == 0, "fx plan"); }
  if (c.apply) CHECK(H->metersSet(c.sp) == 0, "spec");
  int64_t fr = 0; int done = 0;
  auto drain = [&] { for (auto& r : H.poll()) o.recs.push_back(std::move(r)); };
  while (done < c.quanta) {
    int step = std::min(16, c.quanta - done);
    if (c.at >= 0 && done < c.at) step = std::min(step, c.at - done);
    if (c.silenceAt >= 0 && done < c.silenceAt) step = std::min(step, c.silenceAt - done);
    if (c.silenceAt >= 0 && done >= c.silenceAt) o.tap.on = false;
    H.run(o.l, o.r, 128 * step, 128, fr); done += step; drain();
    if (c.at >= 0 && done == c.at) CHECK(H->metersSet(c.sp2) == 0, "spec2");
  }
  CHECK(stats(H.h).chains_failed_blocks == 0 && stats(H.h).chains_nodes == 2, "two nodes, no failed block");
  H->setTestTap(nullptr, nullptr);
  return true;
}
std::vector<thunder_live_ev_chlvl> ch(const Run& r, int track = -1) {
  std::vector<thunder_live_ev_chlvl> v; for (auto& x : r.recs) if (evType(x) == THUNDER_LIVE_EV_CHLVL) { auto e = evAs<thunder_live_ev_chlvl>(x); if (track < 0 || e.track == track) v.push_back(e); } return v;
}
size_t ndiff(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return a.size() + b.size() + 1;
  size_t bad = 0; for (size_t i = 0; i < a.size(); i++) if (std::memcmp(&a[i], &b[i], 4) != 0) bad++;
  return bad;
}
}  // namespace

int main() {
  const uint32_t C = THUNDER_LIVE_MK_CHLVL;
  {   // ---- (1) the call ----
    Host N(opts(THUNDER_LIVE_F_CHAINS, 4096)); thunder_live_meters_spec s = spec(C, 1);
    CHECK(N->metersSet(s) == THUNDER_LIVE_E_STATE, "no F_METERS: E_STATE");
    Host H(opts(CF, 4096)); if (!H.h) return 1;
    s = spec(C, 1); CHECK(H->metersSet(s) == 0, "a CHLVL spec is accepted");
    s.chains_tracks = 1ull << 32; CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "chains_tracks above bit 31: E_ARG");
    s = spec(C, 1, 31); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "chains_hz above 30: E_ARG");
    s = spec(C, 1, -1); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "negative chains_hz: E_ARG");
    s = spec(C, 1, 30); CHECK(H->metersSet(s) == 0, "chains_hz 30 is the top");
  }
  {   // ---- (2) peaks against a known signal; (3) track 1 / track 5 post nothing ----
    Run R; Cfg c; c.sp = spec(C, 1u << 0); go(c, R);
    const auto v = ch(R); CHECK(v.size() > 8, "records came (%zu)", v.size());
    size_t bad = 0, other = 0, badH = 0; float inL = 0, inR = 0, outL = 0, outR = 0;
    for (size_t i = 1; i < v.size(); i++) {   // (the first one covers one quantum: it may hold the pulse or not)
      const auto& e = v[i]; if (e.track != 0) other++;
      if (e.reserved != 0) bad++;
      inL = e.in[0]; inR = e.in[1]; outL = e.out[0]; outR = e.out[1];
      if (!(std::fabs(inL - 0.8f) < 1e-5f && std::fabs(inR - 0.3f) < 1e-5f)) bad++;
      if (!(std::fabs(outL - 0.4f) < 2e-3f && std::fabs(outR - 0.15f) < 2e-3f)) badH++;
    }
    CHECK(other == 0, "only the watched track 0 posts (track 1 has the same node and the same signal, track 5 has no node)");
    CHECK(bad == 0, "in = the pulse (0.8, 0.3): last %g %g", inL, inR);
    CHECK(badH == 0, "out = half the pulse (0.4, 0.15): last %g %g", outL, outR);
    bool ord = true; int64_t last = -1; for (auto& x : R.recs) if (evType(x) == THUNDER_LIVE_EV_CHLVL) { const int64_t f = evF(x); if (f <= last || f % 128 != 0) ord = false; last = f; }
    CHECK(ord, "f rises and falls on quantum ends");
  }
  {   // the same for track 1 alone (half the signal), and for a track with no node
    Run R; Cfg c; c.sp = spec(C, (1u << 1) | (1u << 5)); go(c, R);
    const auto v1 = ch(R, 1), v5 = ch(R, 5), v0 = ch(R, 0);
    CHECK(v5.empty() && v0.empty() && v1.size() > 8, "track 5 (no node) and track 0 (unwatched) post nothing; track 1 posts (%zu %zu %zu)", v5.size(), v0.size(), v1.size());
    bool ok = true; for (size_t i = 1; i < v1.size(); i++) ok = ok && std::fabs(v1[i].in[0] - 0.4f) < 1e-5f && std::fabs(v1[i].in[1] - 0.15f) < 1e-5f; CHECK(ok, "track 1's in is half of track 0's");
  }
  {   // ---- (3) silence, nothing watched, bit-equal audio ----
    Run R; Cfg c; c.sp = spec(C, 1u << 0); c.silenceAt = 300; go(c, R);
    const auto v = ch(R); size_t nz = 0;
    for (size_t i = 0; i < v.size(); i++) if (v[i].h.f > (int64_t)(300 + 40) * 128) nz++;
    CHECK(nz > 3, "records go on in silence (%zu)", nz);
    bool allZero = true; for (size_t i = 0; i < v.size(); i++) if (v[i].h.f > (int64_t)(300 + 40) * 128 && (v[i].in[0] != 0 || v[i].in[1] != 0 || v[i].out[0] != 0 || v[i].out[1] != 0)) allZero = false;
    CHECK(allZero, "after the input stops, the peaks are exactly 0");
    Run A, B, D; Cfg off; off.apply = false; go(off, A);
    Cfg w; w.sp = spec(C, (1u << 0) | (1u << 1) | (1u << 5)); go(w, B);
    Cfg k; k.sp = spec(THUNDER_LIVE_MK_FXLVL, 1u << 0, 0, 0, 1); go(k, D);   // a spec without CHLVL (chains_tracks set but the kind is not)
    CHECK(ch(A).empty() && ch(D).empty(), "nothing watched / kind not asked: no CHLVL record");
    CHECK(!ch(B).empty() && ndiff(A.l, B.l) == 0 && ndiff(A.r, B.r) == 0, "the audio is bit-equal with and without watching");
  }
  {   // ---- (4) the rate ----
    auto count = [&](double hz) { Run R; Cfg c; c.sp = spec(C, 1, hz); go(c, R); return ch(R).size(); };
    const double secs = kQ * 128 / SR;   // 1.6 s
    const size_t n16 = count(0), n10 = count(10), n30 = count(30), n5 = count(5);
    CHECK(n16 >= (size_t)(16 * secs) - 2 && n16 <= (size_t)(16 * secs) + 2, "default 16 a second: %zu", n16);
    CHECK(n10 >= (size_t)(10 * secs) - 2 && n10 <= (size_t)(10 * secs) + 2, "10 a second: %zu", n10);
    CHECK(n30 >= (size_t)(30 * secs) - 3 && n30 <= (size_t)(30 * secs) + 3, "30 a second: %zu", n30);
    CHECK(n5 >= (size_t)(5 * secs) - 2 && n5 <= (size_t)(5 * secs) + 2, "5 a second: %zu", n5);
    // a replacement without CHLVL stops the records; the identical re-send leaves the pacing alone
    Run R; Cfg c; c.sp = spec(C, 1); c.at = 300; c.sp2 = spec(0, 0); go(c, R);
    size_t after = 0; for (auto& e : ch(R)) if (e.h.f > (int64_t)(300 + 17) * 128) after++;
    CHECK(!ch(R).empty() && after == 0, "a replacement spec without CHLVL stops the records (%zu after)", after);
    Run U, V; Cfg u; u.sp = spec(C, 1); go(u, U); Cfg v; v.sp = spec(C, 1); v.at = 200; v.sp2 = spec(C, 1); go(v, V);
    const auto a = ch(U), b = ch(V); bool same = a.size() == b.size(); for (size_t i = 0; same && i < a.size(); i++) same = a[i].h.f == b[i].h.f && std::memcmp(a[i].in, b[i].in, 16) == 0;
    CHECK(same, "an identical re-send changes nothing (%zu vs %zu records)", a.size(), b.size());
  }
  {   // ---- (5) ring order with METERS and FXLVL ----
    Run R; Cfg c; c.fx = true; c.sp = spec(C | THUNDER_LIVE_MK_FXLVL, 1u << 0, 30, 30, 1u << 0); go(c, R);
    uint32_t last = 0; bool gapless = true, order = true; int nM = 0, nF = 0, nC = 0; int64_t qf = -1; int stage = 0;
    for (auto& x : R.recs) {
      const uint32_t s = evSeq(x); if (last && s != last + 1) gapless = false; last = s;
      const int t = evType(x); const int64_t f = evF(x);
      if (t == THUNDER_LIVE_EV_METERS || t == THUNDER_LIVE_EV_FXLVL || t == THUNDER_LIVE_EV_CHLVL) {
        const int rank = t == THUNDER_LIVE_EV_METERS ? 1 : t == THUNDER_LIVE_EV_FXLVL ? 2 : 3;
        t == THUNDER_LIVE_EV_METERS ? nM++ : t == THUNDER_LIVE_EV_FXLVL ? nF++ : nC++;
        if (t != THUNDER_LIVE_EV_METERS && f == qf && rank < stage) order = false;   // within one quantum (same f): METERS, FXLVL, CHLVL
        if (t == THUNDER_LIVE_EV_METERS) { qf = -1; stage = 0; } else { if (f != qf) { qf = f; stage = 0; } stage = rank; }
      }
    }
    CHECK(gapless, "one gapless seq order"); CHECK(order, "FXLVL before CHLVL in a quantum"); CHECK(nM > 5 && nF > 5 && nC > 5, "all three kinds came (%d %d %d)", nM, nF, nC);
  }
  std::printf("live_chlvl: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
