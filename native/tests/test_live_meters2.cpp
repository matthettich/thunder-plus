// M1 (ctest `live_meters2`): the effect level and dynamics records, METERS-DESIGN 4.1 / 5a.
//  (1) layout: FXLVL 56 bytes, FXDYN 152, entries 16, field offsets, the type numbers, EV_MAX holds them, CAP_METERS2.
//  (2) thunder_live_meters_set: E_STATE without F_METERS, E_ARG cases, the replacement state, E_BUSY when the command ring is full (nothing changed).
//  (3) FXLVL / FXDYN against FxHost::popLevels / popDyn: a second FxHost (made from the same plan) is fed the same signal by a test tap in the same quantum; every record equals the reference's own post, bit for bit,
//      with a level-only comp slot (type 7: reduction 0) and a pump COMP slot (type 201: reduction below 0) in one chain; the `type` of each entry and thunder_fx_type_name of it.
//  (4) unwatched tracks post nothing (track 1 has the same chain and the same signal); the audio is bit-equal with and without watching; the master's effects (track 32) can be watched.
//  (5) switching the spec off stops the records; a replacement spec without a track stops that track; kinds can be chosen apart.
//  (6) thunder_live_meters_want alone behaves as before: METERS only, and the METERS records are bit-equal to those of a run that also watches effects (rate_hz of the spec = the same bank).
//  (7) ring order with METERS: one `seq` order, no gap, METERS before the FX records of the same quantum; a consumer that never polls: the ring drops the newest and counts it, nothing else breaks.
//  (8) thunder_fx_type_name: the reverse of the plan reader's table.
#include <array>
#include <cstddef>

#include "live_common.h"
#include "fx/fxhost.h"
#include "rt/json.h"
using namespace lt;

static_assert(sizeof(thunder_live_ev_fxlvl) == 56 && sizeof(thunder_live_ev_fxdyn_ent) == 16 && sizeof(thunder_live_ev_fxdyn) == 152, "M1 record sizes");
static_assert(offsetof(thunder_live_ev_fxlvl, track) == 16 && offsetof(thunder_live_ev_fxlvl, slots) == 20 && offsetof(thunder_live_ev_fxlvl, lvl) == 24, "FXLVL layout");
static_assert(offsetof(thunder_live_ev_fxdyn, track) == 16 && offsetof(thunder_live_ev_fxdyn, n) == 18 && offsetof(thunder_live_ev_fxdyn, reserved) == 20 && offsetof(thunder_live_ev_fxdyn, ent) == 24, "FXDYN layout");
static_assert(offsetof(thunder_live_ev_fxdyn_ent, slot) == 0 && offsetof(thunder_live_ev_fxdyn_ent, type) == 2 && offsetof(thunder_live_ev_fxdyn_ent, red_db) == 4
              && offsetof(thunder_live_ev_fxdyn_ent, in_db) == 8 && offsetof(thunder_live_ev_fxdyn_ent, out_db) == 12, "FXDYN entry layout");
static_assert(THUNDER_LIVE_EV_FXLVL == 11 && THUNDER_LIVE_EV_FXDYN == 12 && THUNDER_LIVE_EV_MAX >= sizeof(thunder_live_ev_fxdyn), "type numbers, the poll slot");
static_assert(sizeof(thunder_live_meters_spec) == 32, "the spec: size, kinds, rate_hz, fx_tracks, chains_hz");

namespace {
#if defined(THUNDER_SANITIZE_BUILD) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr int kQ = 240;
#else
constexpr int kQ = 600;
#endif
constexpr uint32_t FXALL = THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_MASTER;
// track 0 and 1: a level-only comp (slot 1, type 7), a pump COMP that works hard on this signal (slot 2, type 201), a tape (slot 0)
const char* kChain = "[{\"type\":\"tape\",\"on\":true},{\"type\":\"comp\",\"on\":true},{\"type\":\"pcomp\",\"on\":true,\"pump\":{\"threshold\":-40,\"ratio\":8}}]";
const char* kMasterChain = "[{\"type\":\"tape\",\"on\":true},{\"type\":\"comp\",\"on\":true}]";

void sig(float* L, float* R, int64_t n0, float g) {
  for (int i = 0; i < 128; i++) { const double t = (double)(n0 + i); L[i] = g * (float)(0.5 * std::sin(0.0575 * t)); R[i] = g * (float)(0.4 * std::sin(0.0431 * t + 0.3) + 0.05 * std::sin(0.31 * t)); }
}
struct Tap {
  FxHost* ref = nullptr;                          // a second host with track 0's chain, fed what the tap writes into track 0, in the same quantum
  std::vector<std::array<float, 8>> lv;           // its own posts, in order
  std::vector<std::vector<FxDyn>> dyn;
  bool feed1 = true;                              // track 1 hears the signal too
};
void tapFn(void* u, Mixer& m, int64_t qStart) {
  Tap& T = *static_cast<Tap*>(u);
  float L[128], R[128]; sig(L, R, qStart, 1.f);
  std::memcpy(m.inputsL()[0], L, sizeof L); std::memcpy(m.inputsR()[0], R, sizeof R);
  if (T.feed1) { float a[128], b[128]; sig(a, b, qStart, 0.8f); std::memcpy(m.inputsL()[1], a, sizeof a); std::memcpy(m.inputsR()[1], b, sizeof b); }
  if (T.ref) {
    T.ref->process(L, R, 128, (double)qStart / SR);
    std::array<float, 8> v; if (T.ref->popLevels(v.data())) T.lv.push_back(v);
    FxDyn d[kFxSlots]; const int n = T.ref->popDyn(d, kFxSlots); if (n > 0) T.dyn.emplace_back(d, d + n);
  }
}
FxPlan* planOf(FxHost& h, const char* js) { static JsonDoc d; JsonError je; if (!d.parse(js, std::strlen(js), &je)) return nullptr; return h.planFromSong(d.root(), 120, 0, nullptr); }

struct Run {
  std::vector<float> l, r; std::vector<std::vector<uint8_t>> recs; thunder_live_stats st{}; Tap tap; int rc[4] = {0, 0, 0, 0};
};
struct Cfg { bool spec = false; thunder_live_meters_spec sp{}; bool want = false; bool ref = false; bool plan1 = true, masterPlan = false; int quanta = kQ; bool poll = true; int offAt = -1; thunder_live_meters_spec sp2{}; };
void go(const Cfg& c, Run& o) {
  Host H(opts(FXALL, 4096)); if (!H.h) { CHECK(false, "create"); return; }
  const std::string a = kChain, b = kMasterChain;
  o.rc[0] = H->fxPlan(0, (const uint8_t*)a.data(), a.size(), 120); o.rc[0] |= H->fxCommit(0, -1);
  if (c.plan1) { o.rc[1] = H->fxPlan(1, (const uint8_t*)a.data(), a.size(), 120); o.rc[1] |= H->fxCommit(1, -1); }
  if (c.masterPlan) { o.rc[2] = H->fxPlan(32, (const uint8_t*)b.data(), b.size(), 120); o.rc[2] |= H->fxCommit(32, -1); }
  FxHost ref(SR); FxPlan* rp = nullptr;
  if (c.ref) { rp = planOf(ref, kChain); if (rp) { ref.commit(rp); ref.setLevels(true); ref.setTelemetry(true); o.tap.ref = &ref; } }
  H->setTestTap(tapFn, &o.tap);
  if (c.spec) o.rc[3] = H->metersSet(c.sp);
  if (c.want) o.rc[3] |= H->meters(30);   // (after the spec: the later call wins for the METERS rate)
  int64_t fr = 0; int done = 0;
  auto drain = [&] { for (auto& r : H.poll()) o.recs.push_back(std::move(r)); };
  while (done < c.quanta) {
    const int step = c.offAt >= 0 && done < c.offAt ? std::min(32, c.offAt - done) : std::min(32, c.quanta - done);
    H.run(o.l, o.r, 128 * step, 128, fr); done += step; if (c.poll) drain();
    if (c.offAt >= 0 && done == c.offAt) { H->metersSet(c.sp2); }
  }
  if (c.poll) drain();
  o.st = stats(H.h);
  H->setTestTap(nullptr, nullptr);
  delete rp;   // the plan stays its owner's after commit (FxHost REAL-TIME RULES): the reference host keeps the objects
}
std::vector<std::vector<uint8_t>> ofType(const Run& r, int type) { std::vector<std::vector<uint8_t>> v; for (auto& x : r.recs) if (evType(x) == type) v.push_back(x); return v; }
thunder_live_meters_spec spec(uint32_t kinds, uint64_t tracks, double rate = 0) { thunder_live_meters_spec s; std::memset(&s, 0, sizeof s); s.size = sizeof s; s.kinds = kinds; s.fx_tracks = tracks; s.rate_hz = rate; return s; }
size_t ndiff(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return a.size() + b.size() + 1;
  size_t bad = 0; for (size_t i = 0; i < a.size(); i++) if (std::memcmp(&a[i], &b[i], 4) != 0) bad++;
  return bad;
}
}  // namespace

int main() {
  CHECK((thunder_live_caps() & THUNDER_LIVE_CAP_METERS2) != 0, "CAP_METERS2 is set");
  const uint64_t T0 = 1ull << 0, T1 = 1ull << 1, TM = 1ull << 32;

  {   // ---- (2) the call ----
    Host N(opts(THUNDER_LIVE_F_EFFECTS, 4096)); thunder_live_meters_spec s = spec(THUNDER_LIVE_MK_FXLVL, T0);
    CHECK(N->metersSet(s) == THUNDER_LIVE_E_STATE, "without F_METERS: E_STATE");
    Host H(opts(FXALL, 4096));
    CHECK(H->metersSet(s) == 0, "a good spec");
    s = spec(0x10u, T0); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "an unknown kinds bit");
    s = spec(THUNDER_LIVE_MK_FXLVL, 1ull << 33); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "a track bit above the master's");
    s = spec(THUNDER_LIVE_MK_FXLVL, ~0ull); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "all 64 track bits");
    s = spec(THUNDER_LIVE_MK_FXLVL, T0 | TM); CHECK(H->metersSet(s) == 0, "tracks and the master");
    s = spec(THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_CHLVL | THUNDER_LIVE_MK_MASTERM, 0); CHECK(H->metersSet(s) == 0, "the reserved kinds are accepted");
    s = spec(THUNDER_LIVE_MK_FXLVL, T0, 31); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "rate above 30");
    s = spec(THUNDER_LIVE_MK_FXLVL, T0, -1); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "negative rate");
    s = spec(THUNDER_LIVE_MK_FXLVL, T0, NAN); CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "NaN rate");
    s = spec(THUNDER_LIVE_MK_FXLVL, T0); s.chains_hz = 31; CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "chains_hz above 30");
    s = spec(THUNDER_LIVE_MK_FXLVL, T0); s.chains_hz = NAN; CHECK(H->metersSet(s) == THUNDER_LIVE_E_ARG, "chains_hz NaN");
    // the C entry: NULL, a handle-less call, the size rule
    CHECK(thunder_live_meters_set(nullptr, &s) == THUNDER_LIVE_E_ARG, "NULL handle");
    thunder_live_opts o = opts(FXALL, 4096); char err[256]; thunder_live* L = thunder_live_create(&o, err, sizeof err);
    CHECK(L != nullptr, "C create (%s)", err);
    if (L) {
      thunder_live_meters_spec g = spec(THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_FXDYN, T0 | TM);
      CHECK(thunder_live_meters_set(L, nullptr) == THUNDER_LIVE_E_ARG, "NULL spec");
      CHECK(thunder_live_meters_set(L, &g) == 0, "C: a good spec");
      thunder_live_meters_spec sh = g; sh.size = 3; CHECK(thunder_live_meters_set(L, &sh) == THUNDER_LIVE_E_ARG, "size below 4");
      sh = g; sh.size = 8; CHECK(thunder_live_meters_set(L, &sh) == 0, "a short struct: the rest is zero (a replacement)");
      struct Big { thunder_live_meters_spec s; uint64_t extra; } big; std::memset(&big, 0, sizeof big); big.s = g; big.s.size = sizeof big;
      CHECK(thunder_live_meters_set(L, &big.s) == 0, "a longer struct with a zero tail");
      big.extra = 1; CHECK(thunder_live_meters_set(L, &big.s) == THUNDER_LIVE_E_ARG, "a longer struct with a field we do not know set");
      thunder_live_destroy(L);
    }
    // E_BUSY: nothing runs, the ring holds 256
    Host B(opts(FXALL, 4096)); int ok = 0, busy = 0; s = spec(THUNDER_LIVE_MK_FXLVL, T0);
    for (int i = 0; i < 300; i++) { const int rc = B->metersSet(s); if (rc == 0) ok++; else if (rc == THUNDER_LIVE_E_BUSY) busy++; }
    CHECK(ok == THUNDER_LIVE_CMD_RING && busy == 300 - THUNDER_LIVE_CMD_RING, "command ring full: %d accepted, %d E_BUSY", ok, busy);
    int64_t fr = 0; std::vector<float> l, r; B.run(l, r, 128 * 4, 128, fr); CHECK(B->metersSet(s) == 0, "room again after a quantum");
  }

#ifdef THUNDER_HAVE_TFX   // (3) to (7) need the effects library (the closed build and MSVC have none: the plans cannot be made there; (1), (2) and (8) still run)
  Run base;   // no watching, no METERS: the audio of reference
  { Cfg c; go(c, base); }
  CHECK(base.rc[0] == 0 && base.rc[1] == 0, "plans accepted (%d %d)", base.rc[0], base.rc[1]);
  CHECK(ofType(base, THUNDER_LIVE_EV_FXLVL).empty() && ofType(base, THUNDER_LIVE_EV_FXDYN).empty(), "nothing watched: no FXLVL, no FXDYN");
  CHECK(ofType(base, THUNDER_LIVE_EV_METERS).empty(), "nothing asked: no METERS either");

  Run w;      // (3) (4) watching track 0, both kinds, with the lock-step reference
  { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_FXDYN, T0, 30); c.ref = true; go(c, w); }
  CHECK(w.rc[3] == 0 && w.tap.ref != nullptr, "the spec went in, the reference host was made");
  {
    const auto lv = ofType(w, THUNDER_LIVE_EV_FXLVL), dy = ofType(w, THUNDER_LIVE_EV_FXDYN);
    CHECK(!lv.empty() && lv.size() == w.tap.lv.size(), "FXLVL records %zu, the reference host's posts %zu", lv.size(), w.tap.lv.size());
    CHECK(dy.size() == w.tap.dyn.size() && !dy.empty(), "FXDYN records %zu, the reference host's posts %zu", dy.size(), w.tap.dyn.size());
    // about 31 a second: kQ quanta of 128 at 48 kHz, one post per 12
    CHECK(lv.size() == (size_t)(kQ / 12), "one FXLVL every 12th block (%zu of %d quanta)", lv.size(), kQ);
    size_t bad = 0, badSlots = 0, anyLvl = 0; int64_t lastF = 0;
    for (size_t i = 0; i < std::min(lv.size(), w.tap.lv.size()); i++) {
      const thunder_live_ev_fxlvl e = evAs<thunder_live_ev_fxlvl>(lv[i]);
      CHECK(e.h.type == THUNDER_LIVE_EV_FXLVL && e.h.bytes == 56 && e.track == 0, "FXLVL head: type %d bytes %d track %d", e.h.type, e.h.bytes, e.track);
      CHECK(e.h.f > lastF && (e.h.f % 128) == 0, "f is the end of a quantum, rising (%lld)", (long long)e.h.f); lastF = e.h.f;
      if (e.slots != 7u) badSlots++;
      for (int k = 0; k < 8; k++) { if (std::memcmp(&e.lvl[k], &w.tap.lv[i][k], 4) != 0) bad++; if (k < 3 && e.lvl[k] > 0) anyLvl++; }
    }
    CHECK(bad == 0, "FXLVL equals FxHost::popLevels bit for bit (%zu floats differ)", bad);
    CHECK(badSlots == 0, "slots mask is 0b111 for a chain of 3 (%zu records differ)", badSlots);
    CHECK(anyLvl > 0, "the levels are not all zero");
    size_t dbad = 0, ntype = 0, nred = 0; float minRed = 0;
    for (size_t i = 0; i < std::min(dy.size(), w.tap.dyn.size()); i++) {
      const thunder_live_ev_fxdyn e = evAs<thunder_live_ev_fxdyn>(dy[i]); const auto& R = w.tap.dyn[i];
      CHECK(e.h.type == THUNDER_LIVE_EV_FXDYN && e.h.bytes == 152 && e.track == 0 && e.reserved == 0, "FXDYN head");
      if ((size_t)e.n != R.size()) { dbad++; continue; }
      for (int k = 0; k < e.n; k++) {
        const thunder_live_ev_fxdyn_ent& x = e.ent[k];
        if (x.slot != R[k].slot || std::memcmp(&x.red_db, &R[k].v[0], 4) || std::memcmp(&x.in_db, &R[k].v[1], 4) || std::memcmp(&x.out_db, &R[k].v[2], 4)) dbad++;
        if (x.slot == 1 && x.type == 7) ntype++;
        if (x.slot == 2 && x.type == kPumpT0 + 1) { ntype++; if (x.red_db < minRed) minRed = x.red_db; if (x.red_db < -3) nred++; }
        const char* nm = thunder_fx_type_name(x.type);
        CHECK(nm != nullptr && ((x.slot == 1 && !std::strcmp(nm, "comp")) || (x.slot == 2 && !std::strcmp(nm, "pcomp"))), "type %d of slot %d names '%s'", x.type, x.slot, nm ? nm : "(null)");
      }
      for (int k = e.n; k < 8; k++) { thunder_live_ev_fxdyn_ent z; std::memset(&z, 0, sizeof z); CHECK(std::memcmp(&e.ent[k], &z, sizeof z) == 0, "the unused tail of FXDYN is zero"); }
    }
    CHECK(dbad == 0, "FXDYN equals FxHost::popDyn (%zu differ)", dbad);
    CHECK(ntype == 2 * dy.size(), "both dynamics slots carry their core type id (7 and %d): %zu of %zu", kPumpT0 + 1, ntype, 2 * dy.size());
    CHECK(nred > 0 && minRed < -3, "the pump COMP slot shows gain reduction (deepest %.2f dB, %zu posts below -3)", minRed, nred);
    std::printf("FXLVL %zu, FXDYN %zu, deepest reduction %.2f dB\n", lv.size(), dy.size(), minRed);
  }
  {   // (4) unwatched tracks post nothing; the audio is the same
    bool other = false; for (auto& x : w.recs) { const int t = evType(x); if (t == THUNDER_LIVE_EV_FXLVL && evAs<thunder_live_ev_fxlvl>(x).track != 0) other = true; if (t == THUNDER_LIVE_EV_FXDYN && evAs<thunder_live_ev_fxdyn>(x).track != 0) other = true; }
    CHECK(!other, "track 1 has the same chain and signal, and is not watched: no record for it");
    CHECK(ndiff(w.l, base.l) == 0 && ndiff(w.r, base.r) == 0, "watching changes no sample (%zu / %zu differ)", ndiff(w.l, base.l), ndiff(w.r, base.r));
  }
  {   // other tracks, the master, kinds apart
    Run r1; { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXLVL, T1 | TM, 0); c.masterPlan = true; go(c, r1); }
    CHECK(r1.rc[2] == 0, "master plan accepted (%d)", r1.rc[2]);
    bool t1 = false, tm = false, t0 = false; uint32_t mslots = 0;
    for (auto& x : ofType(r1, THUNDER_LIVE_EV_FXLVL)) { const auto e = evAs<thunder_live_ev_fxlvl>(x); if (e.track == 1) t1 = true; else if (e.track == 32) { tm = true; mslots |= e.slots; } else t0 = true; }
    CHECK(t1 && tm && !t0, "FXLVL for track 1 and the master (bit 32), none for track 0 (%d %d %d)", t1, tm, t0);
    CHECK(mslots == 3u, "the master chain has slots 0 and 1 (mask %u)", mslots);
    CHECK(ofType(r1, THUNDER_LIVE_EV_FXDYN).empty(), "kinds FXLVL alone: no FXDYN");
    CHECK(ofType(r1, THUNDER_LIVE_EV_METERS).empty(), "rate_hz 0: no METERS");
    Run r2; { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXDYN, T0 | T1 | TM); c.masterPlan = true; go(c, r2); }
    bool d0 = false, d1 = false, dm = false; for (auto& x : ofType(r2, THUNDER_LIVE_EV_FXDYN)) { const auto e = evAs<thunder_live_ev_fxdyn>(x); if (e.track == 0) d0 = true; if (e.track == 1) d1 = true; if (e.track == 32) { dm = true; if (e.n != 1 || e.ent[0].slot != 1 || e.ent[0].type != 7) CHECK(false, "master dyn entry: n %d slot %d type %d", e.n, e.ent[0].slot, e.ent[0].type); } }
    CHECK(d0 && d1 && dm, "FXDYN for tracks 0, 1 and the master (comp slot 1, type 7)");
    CHECK(ofType(r2, THUNDER_LIVE_EV_FXLVL).empty(), "kinds FXDYN alone: no FXLVL");
  }

  {   // (5) off stops it; a replacement spec drops a track; a re-send is a no-op on a host that stays watched
    Run r; { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_FXDYN, T0 | T1); c.sp2 = spec(0, 0); c.offAt = 160; c.quanta = 480; go(c, r); }
    int64_t lastF = 0; size_t before = 0; for (auto& x : r.recs) { const int t = evType(x); if (t == THUNDER_LIVE_EV_FXLVL || t == THUNDER_LIVE_EV_FXDYN) { before++; lastF = std::max(lastF, evF(x)); } }
    CHECK(before > 0, "records came while the spec was on");
    CHECK(lastF <= (int64_t)(160 + 1) * 128, "no FX record after the spec went off (last f %lld, off at %d)", (long long)lastF, 160 * 128);
    Run q; { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXLVL, T0 | T1); c.sp2 = spec(THUNDER_LIVE_MK_FXLVL, T0); c.offAt = 160; c.quanta = 480; go(c, q); }
    size_t a1 = 0, a1late = 0, a0late = 0; for (auto& x : ofType(q, THUNDER_LIVE_EV_FXLVL)) { const auto e = evAs<thunder_live_ev_fxlvl>(x); const bool late = e.h.f > (int64_t)(160 + 12) * 128; if (e.track == 1) { a1++; if (late) a1late++; } else if (late) a0late++; }
    CHECK(a1 > 0 && a1late == 0 && a0late > 0, "a replacement spec: track 1 stops (%zu before, %zu after), track 0 goes on (%zu after)", a1, a1late, a0late);
    Run z; { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXLVL, T0); c.sp2 = spec(THUNDER_LIVE_MK_FXLVL, T0); c.offAt = 160; c.quanta = 480; go(c, z); }
    const auto zl = ofType(z, THUNDER_LIVE_EV_FXLVL);
    CHECK(zl.size() >= 480 / 12 - 1 && zl.size() <= 480 / 12, "re-sending the same spec leaves the 12-block window alone (%zu records in 480 quanta)", zl.size());
  }

  {   // (6) meters_want alone, as before; and the METERS records do not depend on watching effects
    Run a, b;
    { Cfg c; c.want = true; go(c, a); }
    { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_FXDYN, T0, 30); go(c, b); }
    const auto ma = ofType(a, THUNDER_LIVE_EV_METERS), mb = ofType(b, THUNDER_LIVE_EV_METERS);
    CHECK(!ma.empty() && ma.size() == mb.size(), "METERS %zu (want alone) and %zu (spec with rate_hz 30)", ma.size(), mb.size());
    CHECK(ofType(a, THUNDER_LIVE_EV_FXLVL).empty() && ofType(a, THUNDER_LIVE_EV_FXDYN).empty(), "meters_want alone posts no FXLVL / FXDYN");
    size_t bad = 0; for (size_t i = 0; i < std::min(ma.size(), mb.size()); i++) { const auto x = evAs<thunder_live_ev_meters>(ma[i]), y = evAs<thunder_live_ev_meters>(mb[i]); if (std::memcmp(x.a, y.a, sizeof x.a) || x.h.f != y.h.f || std::memcmp(&x.reserved[0], &y.reserved[0], 4)) bad++; }
    CHECK(bad == 0, "the METERS records are the same with and without effects watched (%zu differ)", bad);
    CHECK(ndiff(a.l, base.l) == 0 && ndiff(b.r, base.r) == 0, "meters change no sample");
    float pk = 0; for (auto& x : ma) { const auto e = evAs<thunder_live_ev_meters>(x); pk = std::max(pk, e.a[0]); } CHECK(pk > 0.005f, "METERS slot 0 sees the signal (peak %.3f)", pk);
    // the later call wins for the rate; want after a spec with rate 0 switches METERS on again
    Run c2; { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXLVL, T0, 0); c.want = true; c.quanta = 300; go(c, c2); }
    CHECK(!ofType(c2, THUNDER_LIVE_EV_METERS).empty() && !ofType(c2, THUNDER_LIVE_EV_FXLVL).empty(), "spec, then meters_want: both streams");

    // (7) one order
    uint32_t last = 0; bool gap = false, order = true; size_t nm = 0, nf = 0; int prev = 0;
    for (auto& x : b.recs) { const uint32_t s = evSeq(x); if (last && s != last + 1) gap = true; last = s; const int t = evType(x);
      if (t == THUNDER_LIVE_EV_METERS) nm++; if (t == THUNDER_LIVE_EV_FXLVL || t == THUNDER_LIVE_EV_FXDYN) { nf++; if (prev == THUNDER_LIVE_EV_FXDYN && t == THUNDER_LIVE_EV_METERS) order = false; }
      prev = t; }
    CHECK(!gap, "seq has no gap over METERS, FXLVL, FXDYN, CLOCK, POS: one order");
    CHECK(nm > 0 && nf > 0, "both streams are in it (%zu METERS, %zu FX)", nm, nf);
    // within a quantum METERS is posted first: an FX record is never followed, in the same quantum, by a METERS record. METERS has its own `f` (its window's end), so check by position: after the first FX
    // record of a quantum comes no METERS before the next CLOCK / POS record of the NEXT quantum or an FX record of a later f.
    bool meterAfterFx = false; int64_t fxF = -1;
    for (auto& x : b.recs) { const int t = evType(x); if (t == THUNDER_LIVE_EV_FXLVL || t == THUNDER_LIVE_EV_FXDYN) fxF = evF(x); else if (t == THUNDER_LIVE_EV_METERS && fxF >= 0 && evF(x) <= fxF) meterAfterFx = true; }
    CHECK(!meterAfterFx, "a METERS record posted in a quantum comes before that quantum's FX records (no METERS record with an f at or below an earlier FX record's)");
    (void)order;
  }

  {   // (7) a consumer that never polls: the ring refuses the newest and counts; the audio is untouched
    Run f; { Cfg c; c.spec = true; c.sp = spec(THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_FXDYN, T0 | T1 | TM, 30); c.masterPlan = true; c.poll = false; c.quanta = 3000; go(c, f); }
    CHECK(f.st.evt_dropped > 0, "a full ring drops and counts (%u dropped)", f.st.evt_dropped);
    Run g; { Cfg c; c.masterPlan = true; c.poll = false; c.quanta = 3000; go(c, g); }
    CHECK(ndiff(f.l, g.l) == 0 && ndiff(f.r, g.r) == 0, "a full ring changes no sample");
  }

#else
  std::printf("live_meters2: parts (3) to (7) SKIPPED: this build has no effects library\n");
#endif

  {   // (8) the names
    CHECK(thunder_fx_type_name(7) && !std::strcmp(thunder_fx_type_name(7), "comp"), "7 is comp");
    CHECK(thunder_fx_type_name(kPumpT0 + 1) && !std::strcmp(thunder_fx_type_name(kPumpT0 + 1), "pcomp"), "pump COMP is pcomp");
    CHECK(thunder_fx_type_name(-1) == nullptr && thunder_fx_type_name(34) == nullptr && thunder_fx_type_name(5000) == nullptr && thunder_fx_type_name(kPumpT0 + kPumpTypes) == nullptr, "unknown ids are NULL");
    int named = 0, bad = 0;
    for (int id = 0; id < 400; id++) { const char* n = thunder_fx_type_name(id); if (!n) continue; named++;
      if (fx_is_pump(id)) { if (n[0] != 'p' || std::strcmp(n + 1, fx_pump_key(id - kPumpT0))) bad++; }
      else if (fx_is_mod(id)) { if (fx_mod_index(n) != id) bad++; }
      else if (fx_type_of(n) != id) bad++; }
    CHECK(named > 60 && bad == 0, "the names go back to the ids through the plan reader's own lookups (%d named, %d wrong)", named, bad);
    CHECK(thunder_fx_type_name(95) && !std::strcmp(thunder_fx_type_name(95), "flex"), "95 is flex");
  }

  std::printf("live_meters2: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
