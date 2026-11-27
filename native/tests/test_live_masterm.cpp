// M2 (ctest `live_masterm`): the master meters record (MASTERM, type 14), METERS-DESIGN 4.1 / 5a.
//  (1) layout (248 bytes), the spec did NOT grow (the kind bit is the whole subscription), E_STATE without F_METERS.
//  (2) the values: bit-equal to a lock-step reference MasterProcessor(meters on) that is fed what the master chain is fed (the same host with mastering off, the OUT fader at unity), field by field except `load` (wall clock).
//  (3) with the suppressor on: flags bit 0 and the 48 bands bit-equal; the loudness of a silent stretch is -180 and nothing is infinite.
//  (4) unwatched: nothing posted, and the audio is bit-equal with watching, with watching switched on and off in the middle, and with MASTERM in a spec of other kinds only.
//  (5) the rate (about 20 a second), a replacement spec without MASTERM stops the records, an identical re-send changes nothing, the first records after an `on` are fresh (the peaks of the quanta after it only).
//  (6) no F_MASTER host: MASTERM accepted, nothing posted. Mastering off in the song: nothing posted (the worklet is not in the path).
//  (7) ring order: METERS, FXLVL, CHLVL, MASTERM, one gapless seq order.
#include <cstddef>

#include "masterm_common.h"
#include "master/chain.h"
#include "master/master.h"
#include "rt/json.h"
#include "chlvl_common.h"
using namespace lt;

static_assert(sizeof(thunder_live_ev_masterm) == 248 && offsetof(thunder_live_ev_masterm, in_pk) == 16 && offsetof(thunder_live_ev_masterm, load) == 48 && offsetof(thunder_live_ev_masterm, flags) == 52 && offsetof(thunder_live_ev_masterm, sup) == 56, "MASTERM layout");
static_assert(THUNDER_LIVE_EV_MASTERM == 14 && THUNDER_LIVE_EV_MAX >= sizeof(thunder_live_ev_masterm), "type number");
static_assert(sizeof(thunder_live_meters_spec) == 40, "the spec did not grow for MASTERM");

namespace {
constexpr uint32_t MF = THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_MASTER;
constexpr uint32_t MK = THUNDER_LIVE_MK_MASTERM;
thunder_live_meters_spec spec(uint32_t kinds, double rate = 0) { thunder_live_meters_spec s; std::memset(&s, 0, sizeof s); s.size = sizeof s; s.kinds = kinds; s.rate_hz = rate; return s; }
struct Run { std::vector<float> l, r; std::vector<std::vector<uint8_t>> recs; MTap tap; };
struct Cfg {
  uint32_t flags = MF; bool mstOn = true, sup = false; bool apply = true; thunder_live_meters_spec sp = spec(MK); int quanta = 640;
  std::vector<std::pair<int, thunder_live_meters_spec>> later;   // (quantum, spec) applied once that many quanta are done
  std::vector<std::pair<int, int>> modes;                        // (quantum, tap mode)
};
bool go(const Cfg& c, Run& o) {
  Host H(opts(c.flags, 4096)); if (!H.h) { CHECK(false, "create"); return false; }
  CHECK(H.song(mastSong(c.mstOn, c.sup)) == 0, "song");
  H->setTestTap(mTapFn, &o.tap);
  CHECK(H->play(playOpts(THUNDER_LIVE_NOW)) == 0, "play");
  if (c.apply) CHECK(H->metersSet(c.sp) == 0, "spec");
  int64_t fr = 0; int done = 0;
  while (done < c.quanta) {
    int step = std::min(8, c.quanta - done);
    for (auto& x : c.later) if (done < x.first) step = std::min(step, x.first - done);
    for (auto& x : c.modes) if (done < x.first) step = std::min(step, x.first - done);
    for (auto& x : c.modes) if (done == x.first) o.tap.mode = x.second;
    H.run(o.l, o.r, 128 * step, 128, fr); done += step;
    for (auto& r : H.poll()) o.recs.push_back(std::move(r));
    for (auto& x : c.later) if (done == x.first) CHECK(H->metersSet(x.second) == 0, "later spec");
  }
  H->setTestTap(nullptr, nullptr);
  return true;
}
std::vector<thunder_live_ev_masterm> mm(const Run& r) { std::vector<thunder_live_ev_masterm> v; for (auto& x : r.recs) if (evType(x) == THUNDER_LIVE_EV_MASTERM) v.push_back(evAs<thunder_live_ev_masterm>(x)); return v; }
size_t ndiff(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return a.size() + b.size() + 1;
  size_t bad = 0; for (size_t i = 0; i < a.size(); i++) if (std::memcmp(&a[i], &b[i], 4) != 0) bad++;
  return bad;
}
bool same(float a, double b) { return a == (float)b; }
}  // namespace

int main() {
#ifdef THUNDER_HAVE_TFX
  std::printf("TFX: yes\n");   // W-TFX: CI greps this line for the Windows clang-cl job (no effects library = the plans below are not made)
#else
  std::printf("TFX: no\n");
#endif
  {   // ---- (1) the call ----
    Host N(opts(THUNDER_LIVE_F_MASTER, 4096)); thunder_live_meters_spec s = spec(MK);
    CHECK(N->metersSet(s) == THUNDER_LIVE_E_STATE, "no F_METERS: E_STATE");
    Host H(opts(MF, 4096)); if (!H.h) return 1;
    CHECK(H->metersSet(s) == 0, "a MASTERM spec is accepted");
    s = spec(MK | 0x80); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "an unknown kinds bit is still E_ARG");
    thunder_live_meters_spec old; std::memset(&old, 0, sizeof old); old.size = 32; old.kinds = MK; CHECK(H->metersSet(old) == 0, "a caller with the older 32-byte struct can still ask for MASTERM (the kind bit is the whole subscription)");
  }
  // (2) and (3): the values against a lock-step reference
  for (int withSup = 0; withSup < 2; withSup++) {
    Run R; Cfg c; c.sup = withSup; c.modes = {{300, 1}, {450, 2}}; go(c, R);    // loud, quiet from 300, silent from 450
    Run F; Cfg f; f.mstOn = false; f.apply = false; f.modes = c.modes; go(f, F);   // the same host with mastering off: its output is the chain's input (OUT at unity, no safety limiter)
    const auto v = mm(R); CHECK(v.size() > 25, "records came (%zu)", v.size());
    JsonDoc doc; CHECK(doc.parse(mastSong(true, withSup != 0)), "parse");
    MasterChainConfig cfg = master_chain_from_song(doc.root().get("mst"), false);
    MasterProcessor ref(SR, true); MasterSet ms = cfg.set; ms.hasSup = ms.hasComp = ms.hasLim = true; ref.set(ms);
    std::vector<float> rl = F.l, rr = F.r; std::vector<MasterMeter> want;
    for (size_t o = 0; o + 128 <= rl.size(); o += 128) { ref.process(&rl[o], &rr[o], 128); MasterMeter m; while (ref.popMeter(&m)) want.push_back(m); }
    CHECK(ndiff(rl, R.l) == 0 && ndiff(rr, R.r) == 0, "(sup %d) the reference processor reproduces the host's output exactly: the input is what the chain was fed", withSup);
    CHECK(want.size() == v.size(), "(sup %d) as many records as the reference's messages (%zu %zu)", withSup, v.size(), want.size());
    size_t bad = 0, nonfin = 0, withS = 0; const size_t n = std::min(v.size(), want.size());
    for (size_t i = 0; i < n; i++) {
      const auto& e = v[i]; const MasterMeter& m = want[i];
      auto lu = [](double x) { return std::isfinite(x) && x > -180 ? (float)x : -180.f; };
      bool ok = same(e.in_pk, m.inPk) && same(e.out_pk, m.outPk) && same(e.tp, m.tp) && e.in_lu == lu(m.inLu) && e.out_lu == lu(m.outLu) && same(e.comp, m.comp) && same(e.comp_in, m.compIn) && same(e.lim, m.lim);
      ok = ok && e.flags == (m.hasSup ? 1u : 0u);
      for (int b = 0; b < 48; b++) ok = ok && same(e.sup[b], m.hasSup ? m.sup[b] : 0.f);
      if (!ok) bad++;
      const float* p = &e.in_pk; for (int k = 0; k < 9; k++) if (!std::isfinite(p[k])) nonfin++;
      for (int b = 0; b < 48; b++) if (!std::isfinite(e.sup[b])) nonfin++;
      if (e.flags & 1u) withS++;
      CHECK(e.h.f % 128 == 0 && e.load >= 0, "f on a quantum end, load >= 0");
    }
    CHECK(bad == 0, "(sup %d) every field equals the reference's message (%zu of %zu differ)", withSup, bad, n);
    CHECK(nonfin == 0, "(sup %d) nothing infinite", withSup);
    CHECK(v[0].in_lu == -180.f && v[0].out_lu == -180.f, "(sup %d) no loudness before the first 100 ms block: -180 (%g)", withSup, v[0].in_lu);
    CHECK((withSup != 0) == (withS == n), "(sup %d) the suppressor flag follows the stage (%zu)", withSup, withS);
    float minLim = 0, minComp = 0, maxIn = 0, maxOut = 0, maxLu = -180, maxSup = 0; for (auto& e : v) { minLim = std::min(minLim, e.lim); minComp = std::min(minComp, e.comp); maxIn = std::max(maxIn, e.in_pk); maxOut = std::max(maxOut, e.out_pk); maxLu = std::max(maxLu, e.in_lu); for (int b = 0; b < 48; b++) maxSup = std::max(maxSup, e.sup[b]); }
    CHECK(minLim < -0.5f && minComp < -0.5f && maxIn > 1.0f && maxOut < 1.0f && maxLu > -30.f, "(sup %d) the signal exercises the stages: lim %g comp %g in %g out %g lu %g", withSup, minLim, minComp, maxIn, maxOut, maxLu);
    if (withSup) CHECK(maxSup > 0.f, "the suppressor read-out moves (%g)", maxSup);
    const auto& last = v.back(); CHECK(last.in_pk == 0.f && last.comp_in <= -199.f || last.comp_in < 0.f, "silence at the end reads quiet (in_pk %g)", last.in_pk);
  }
  {   // ---- (4) unwatched: nothing posted; the audio is bit-equal in every case, with silence in the middle (the silence shortcut) ----
    Cfg base; base.modes = {{200, 2}}; base.quanta = 640;
    Run A, B, C2, D, E; Cfg a = base; a.apply = false; go(a, A);
    Cfg b = base; go(b, B);
    Cfg c = base; c.apply = false; c.later = {{100, spec(MK)}, {250, spec(0)}, {400, spec(MK)}, {500, spec(0)}}; go(c, C2);
    Cfg d = base; d.sp = spec(THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_FXDYN, 10); go(d, D);
    Cfg e = base; e.sp = spec(MK, 30); e.modes = {{0, 1}, {200, 2}, {350, 0}}; Cfg a2 = e; a2.apply = false; Run E2; go(e, E); go(a2, E2);
    CHECK(mm(A).empty() && mm(D).empty(), "nothing watched / another kind: no MASTERM");
    CHECK(!mm(B).empty() && !mm(C2).empty() && !mm(E).empty(), "watched: records");
    CHECK(ndiff(A.l, B.l) == 0 && ndiff(A.r, B.r) == 0, "the audio is bit-equal with and without watching");
    CHECK(ndiff(A.l, C2.l) == 0 && ndiff(A.r, C2.r) == 0, "... and with watching switched on and off four times in the middle (%zu)", ndiff(A.l, C2.l));
    CHECK(ndiff(A.l, D.l) == 0, "... and with other kinds");
    CHECK(ndiff(E.l, E2.l) == 0 && ndiff(E.r, E2.r) == 0, "... and through quiet / silent / loud stretches at 30 Hz");
    // toggled run: records only in the watched windows
    size_t out = 0; for (auto& r : mm(C2)) { const int64_t q = r.h.f / 128; if (!((q > 100 && q <= 250 + 1) || (q > 400 && q <= 500 + 1))) out++; }
    CHECK(out == 0, "records only while watched (%zu outside)", out);
  }
  {   // ---- (5) rate, replacement, re-send, a fresh start ----
    Run R; Cfg c; go(c, R); const double secs = 640 * 128 / SR; const size_t n = mm(R).size();
    CHECK(n + 2 >= (size_t)(20 * secs) && n <= (size_t)(20 * secs) + 2, "about 20 a second: %zu in %.2f s", n, secs);
    Run S; Cfg s; s.later = {{300, spec(0)}}; go(s, S); size_t after = 0; for (auto& r : mm(S)) if (r.h.f > (int64_t)(300 + 2) * 128) after++;
    CHECK(!mm(S).empty() && after == 0, "a replacement spec without MASTERM stops the records (%zu after)", after);
    Run U, V; Cfg u; u.modes = {{100, 1}, {350, 0}}; go(u, U); Cfg v = u; v.later = {{200, spec(MK)}, {400, spec(MK, 10)}}; go(v, V);
    const auto a = mm(U), b = mm(V); bool eq = a.size() == b.size(); size_t d = 0;
    for (size_t i = 0; eq && i < a.size(); i++) { thunder_live_ev_masterm x = a[i], y = b[i]; x.load = y.load = 0; x.h.seq = y.h.seq = 0; if (std::memcmp(&x, &y, sizeof x) != 0) d++; }
    CHECK(eq && d == 0, "identical re-sends (and a METERS rate change) leave the meters running: same records (%zu vs %zu, %zu differ)", a.size(), b.size(), d);
    Run W; Cfg w; w.apply = false; w.modes = {{300, 1}}; w.later = {{301, spec(MK)}}; go(w, W);
    const auto vw = mm(W); CHECK(!vw.empty(), "records after the late start");
    CHECK(!vw.empty() && vw[0].in_pk < 0.2f && vw[0].h.f > 301 * 128, "the first record after an `on` holds the quanta after it only: in_pk %g (the loud signal before it peaks above 1)", vw.empty() ? -1.f : vw[0].in_pk);
  }
  {   // ---- (6) no master chain / mastering off ----
    Run R; Cfg c; c.flags = THUNDER_LIVE_F_METERS; go(c, R); CHECK(mm(R).empty(), "no F_MASTER: MASTERM accepted, nothing posted");
    Run Q; Cfg q; q.mstOn = false; go(q, Q); CHECK(mm(Q).empty(), "mastering off in the song: the worklet is not in the path, nothing posted (%zu)", mm(Q).size());
  }
  {   // ---- (7) ring order ----
    constexpr uint32_t CF = THUNDER_LIVE_F_CHAINS | THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER;
    Host H(opts(CF, 4096)); if (!H.h) return 1;
    std::string sg = chlvlSong(); sg.pop_back(); sg += "," + mstJson(true, false) + "}";
    CHECK(H.song(sg) == 0, "song"); ChTap tap; H->setTestTap(chTapFn, &tap); CHECK(H->play(playOpts(THUNDER_LIVE_NOW)) == 0, "play");
    int64_t fr = 0; std::vector<std::vector<uint8_t>> recs; std::vector<float> l, r;
    // the play's own (empty) effects plans are committed when the play STARTS, possibly a quantum after play(): plan the effects only once it has (else the song's plan can replace this one on some platforms)
    for (int i = 0; i < 64 && stats(H.h).state == THUNDER_LIVE_STATE_IDLE; i++) { H.run(l, r, 128, 128, fr); for (auto& x : H.poll()) recs.push_back(std::move(x)); }
    CHECK(stats(H.h).state != THUNDER_LIVE_STATE_IDLE, "the play has started before the effects are planned");
    const std::string fx = "[{\"type\":\"tape\",\"on\":true},{\"type\":\"comp\",\"on\":true}]"; CHECK(H->fxPlan(0, (const uint8_t*)fx.data(), fx.size(), 120) == 0 && H->fxCommit(0, -1) == 0, "fx plan");
    thunder_live_meters_spec s = spec(MK | THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_CHLVL, 30); s.fx_tracks = 1; s.chains_tracks = 1; s.chains_hz = 30; CHECK(H->metersSet(s) == 0, "spec");
    for (int i = 0; i < 40; i++) { H.run(l, r, 128 * 16, 128, fr); for (auto& x : H.poll()) recs.push_back(std::move(x)); }
    uint32_t last = 0; bool gapless = true, order = true; int cnt[16] = {0}; int64_t qf = -1; int stage = 0;
    for (auto& x : recs) {
      const uint32_t sq = evSeq(x); if (last && sq != last + 1) gapless = false; last = sq;
      const int t = evType(x); const int64_t f = evF(x);
      const int rank = t == THUNDER_LIVE_EV_METERS ? 1 : t == THUNDER_LIVE_EV_FXLVL ? 2 : t == THUNDER_LIVE_EV_CHLVL ? 3 : t == THUNDER_LIVE_EV_MASTERM ? 4 : 0;
      if (!rank) continue; cnt[rank]++;
      if (f != qf) { qf = f; stage = 0; }
      if (rank < stage) order = false; stage = rank;   // within one quantum: METERS, FXLVL, CHLVL, MASTERM
    }
    CHECK(gapless, "one gapless seq order"); CHECK(order, "METERS, FXLVL, CHLVL, MASTERM in a quantum"); 
#ifdef THUNDER_HAVE_TFX
    CHECK(cnt[1] > 20 && cnt[2] > 20 && cnt[3] > 20 && cnt[4] > 20, "all four kinds came (%d %d %d %d)", cnt[1], cnt[2], cnt[3], cnt[4]);
#else   // no effects library (MSVC, the closed build): no FXLVL, as live_meters2
    CHECK(cnt[1] > 20 && cnt[2] == 0 && cnt[3] > 20 && cnt[4] > 20, "METERS, CHLVL, MASTERM came, no FXLVL without the effects library (%d %d %d %d)", cnt[1], cnt[2], cnt[3], cnt[4]);
#endif
    H->setTestTap(nullptr, nullptr);
  }
  std::printf("live_masterm: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
