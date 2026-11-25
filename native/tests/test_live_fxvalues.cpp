// PP (ctest `live_fxvalues`): thunder_live_fx_values, the knobs of a pump or a modular (MODFX) effect slot at once (docs/port/PP-NOTES.md).
// (1) The call's judgement: a slot that is not of that kind in the track's last planned chain is E_ARG (a tfx slot, an empty slot, the other kind, a count that is not the planned one, NaN, bad track / slot /
//     kind / at), E_STATE without F_EFFECTS, E_BUSY when the 16-record ring is full (nothing lost, the retry lands), and a refused call changes nothing (the run is bit-equal to one that never made it).
// (2) A pump slot's values: the change is audible in the quantum the call is made for (the run equals the run without the call up to that quantum, and differs from it inside it) and, once settled, equals the run
//     whose plan held the new values from the start. The same for a modular slot (memoryless gain, so the equality is exact).
// (3) The snapshot rule (P3b's seam): a record pushed after an fx_commit that the quantum's command loop did not see waits for the next quantum and meets the NEW chain: bit-equal to the same sends made before the quantum.
// (4) The audio side, in FxHost: nothing allocates while the values land (a counting operator new), a RESTING pump slot takes them without waking (no fresh instance: audioRemakes() stays; the old psetPump wakes it),
//     a record for a slot that is no longer of that kind is left alone.
#include <atomic>
#include <cstdlib>
#include <functional>
#include <new>

#include "live_common.h"
#include "fx/fxhost.h"
#include "rt/json.h"
using namespace lt;

// ---- a counting allocator (armed around the audio-side work only) ----
static std::atomic<long> g_alloc{0}; static std::atomic<bool> g_arm{false}, g_count{false};   // g_count: tests that use blocks() below ask for the audio work to be counted (blocks() arms around process only)
void* operator new(std::size_t n) { if (g_arm.load(std::memory_order_relaxed)) g_alloc.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (g_arm.load(std::memory_order_relaxed)) g_alloc.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
constexpr int kSeg = 64;   // quanta before the change
// the song's slot text for a pump (pwide out_gain g) / a modular (utility gain byte b) slot, and the values the page would send for it (what the reader makes: the FxEntry's pv / mp)
std::string pumpSlot(double g, bool on = true) { char b[160]; std::snprintf(b, sizeof b, "{\"type\":\"pwide\",\"on\":%s,\"pump\":{\"out_gain\":%g}}", on ? "true" : "false", g); return b; }
std::string modSlot(int gain) { char b[120]; std::snprintf(b, sizeof b, "{\"type\":\"utility\",\"on\":true,\"gain\":%d}", gain); return b; }
bool entryOf(const std::string& slot, FxEntry* e) { JsonDoc d; JsonError je; if (!d.parse(slot.data(), slot.size(), &je)) return false; return FxHost::entryFromSlot(d.root(), 0, 48000.0, 120, 0, e, nullptr, nullptr); }
std::vector<double> pumpValues(double g) { FxEntry e; std::vector<double> v; if (!entryOf(pumpSlot(g), &e)) return v; for (int i = 0; i < e.npv; i++) v.push_back((double)e.pv[i]); return v; }
std::vector<double> modValues(int gain) { FxEntry e; std::vector<double> v; if (!entryOf(modSlot(gain), &e)) return v; for (int i = 0; i < e.nmp; i++) v.push_back(e.mp[i]); return v; }
std::string chain(const std::string& s0) { return "[" + s0 + "]"; }

size_t ndiff(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0) {
  if (a.size() != b.size()) return a.size() + b.size() + 1;
  size_t bad = 0; for (size_t i = from; i < a.size(); i++) if (std::memcmp(&a[i], &b[i], 4) != 0) bad++;
  return bad;
}
long firstDiff(const std::vector<float>& a, const std::vector<float>& b) { for (size_t i = 0; i < a.size() && i < b.size(); i++) if (std::memcmp(&a[i], &b[i], 4) != 0) return (long)i; return -1; }
double maxDiff(const std::vector<float>& a, const std::vector<float>& b, size_t from) { double m = 0; for (size_t i = from; i < a.size() && i < b.size(); i++) m = std::max(m, (double)std::fabs(a[i] - b[i])); return m; }
double peakOf(const std::vector<float>& v, size_t from) { double p = 0; for (size_t i = from; i < v.size(); i++) p = std::max(p, (double)std::fabs(v[i])); return p; }

// One run: track 0 holds `first` from the start (fx_plan + fx_commit, then kSeg quanta); then `mid` is called (the change under test) and `total` quanta more are run. Returns the left channel.
struct Run { std::vector<float> l; int rc = 0; long allocs = 0; };
Run run(const std::string& text, const PadSet& ps, const std::string& first, const std::function<int(Host&)>& mid, int total, bool armAlloc = false) {
  Run o; Host H(opts(THUNDER_LIVE_F_EFFECTS, 4096)); std::vector<float> r;
  if (!H.h) { o.rc = -99; return o; }
  H.song(text); H.loadPads(ps);
  int64_t fr = 0; H->play(playOpts(0));
  const std::string c = chain(first);
  H->fxPlan(0, (const uint8_t*)c.data(), c.size(), 120); H->fxCommit(0, -1);
  H.run(o.l, r, 128 * kSeg, 128, fr);
  if (mid) o.rc = mid(H);
  if (armAlloc) {   // the quanta are pulled here with preallocated buffers: only the core's own work is counted
    std::vector<float> a(128), b(128); o.l.reserve(o.l.size() + (size_t)128 * total); long n = 0;
    for (int q = 0; q < total; q++) { g_alloc = 0; g_arm = true; H->process(a.data(), b.data(), 128, fr, 0); g_arm = false; n += g_alloc.load(); fr += 128; o.l.insert(o.l.end(), a.begin(), a.end()); }
    o.allocs = n;
  } else H.run(o.l, r, 128 * total, 128, fr);
  return o;
}

// ---- (3) the seam ----
struct Seam { Host* H; std::string chainNew; std::vector<double> vals; int kind; bool armed; int rc[3]; };
Run seamRun(const std::string& text, const PadSet& ps, const std::string& oldFirst, const std::string& newFirst, const std::vector<double>& vals, int kind, bool viaSeam) {
  Run o; Host H(opts(THUNDER_LIVE_F_EFFECTS, 4096)); std::vector<float> r;
  H.song(text); H.loadPads(ps);
  int64_t fr = 0; H->play(playOpts(0));
  const std::string c0 = chain(oldFirst), c1 = chain(newFirst);
  H->fxPlan(0, (const uint8_t*)c0.data(), c0.size(), 120); H->fxCommit(0, -1); H.run(o.l, r, 128 * kSeg, 128, fr);
  Seam sm{&H, c1, vals, kind, false, {0, 0, 0}};
  auto sends = [](Seam* m) { Host& h = *m->H; m->rc[0] = h->fxPlan(0, (const uint8_t*)m->chainNew.data(), m->chainNew.size(), 120); m->rc[1] = h->fxCommit(0, -1); m->rc[2] = h->fxValues(0, 0, m->kind, m->vals.data(), (int)m->vals.size(), -1); };
  if (viaSeam) {
    H->setTestFxSeam([](void* u) { Seam* m = (Seam*)u; if (!m->armed) return; m->armed = false; Seam* mm = m; Host& h = *mm->H;
      mm->rc[0] = h->fxPlan(0, (const uint8_t*)mm->chainNew.data(), mm->chainNew.size(), 120); mm->rc[1] = h->fxCommit(0, -1); mm->rc[2] = h->fxValues(0, 0, mm->kind, mm->vals.data(), (int)mm->vals.size(), -1); }, &sm);
    sm.armed = true; H.run(o.l, r, 128, 128, fr);
    H->setTestFxSeam(nullptr, nullptr);
  } else { H.run(o.l, r, 128, 128, fr); sends(&sm); }
  H.run(o.l, r, 128 * 150, 128, fr);
  o.rc = (sm.rc[0] || sm.rc[1] || sm.rc[2]) ? 1 : 0;
  return o;
}

// ---- (4) FxHost level ----
FxPlan* planOf(FxHost& h, const std::string& js) { JsonDoc d; JsonError je; if (!d.parse(js.data(), js.size(), &je)) return nullptr; return h.planFromSong(d.root(), 120, 0, nullptr); }
void feed(float* L, float* R, int64_t n0, int n) { for (int i = 0; i < n; i++) { const double t = (double)(n0 + i); L[i] = (float)(0.3 * std::sin(0.05 * t) + 0.2 * std::sin(0.31 * t)); R[i] = (float)(0.25 * std::sin(0.043 * t + 1)); } }
std::vector<float> blocks(FxHost& h, int64_t* n0, int count) {
  std::vector<float> out; float L[128], R[128];
  for (int b = 0; b < count; b++) { feed(L, R, *n0, 128); if (g_count) g_arm = true; h.process(L, R, 128, (double)*n0 / h.sampleRate()); g_arm = false; out.insert(out.end(), L, L + 128); out.insert(out.end(), R, R + 128); *n0 += 128; }
  return out;
}
}  // namespace

int main() {
  const std::string text = electro();
  if (text.empty()) { std::printf("FAIL: cannot read ELECTRO\n"); return 1; }
  CHECK((thunder_live_caps() & THUNDER_LIVE_CAP_FXVALUES) != 0, "the capability bit is set (caps 0x%x)", thunder_live_caps());
  { const double v[2] = {0, 0}; CHECK(thunder_live_fx_values(nullptr, 0, 0, THUNDER_LIVE_FXV_PUMP, v, 2, -1) == THUNDER_LIVE_E_ARG, "a NULL handle is E_ARG"); }
  PadSet ps;
#if defined(THUNDER_HAVE_TFX) && defined(THUNDER_HAVE_PUMP)
  const std::vector<double> pv0 = pumpValues(0), pv9 = pumpValues(9), mp0 = modValues(182), mp1 = modValues(230);
  CHECK(pv0.size() == 32 && pv9.size() == 32 && pv0[0] == 0 && pv9[0] == 9, "the page's pump list for pwide (%zu values, out_gain %g / %g)", pv0.size(), pv0.empty() ? 0 : pv0[0], pv9.empty() ? 0 : pv9[0]);
  CHECK(mp0.size() == 5 && mp1.size() == 5 && mp0[0] != mp1[0], "the module list for utility (%zu values, gain %g / %g)", mp0.size(), mp0.empty() ? 0 : mp0[0], mp1.empty() ? 0 : mp1[0]);
  const std::string P0 = pumpSlot(0), P9 = pumpSlot(9), M0 = modSlot(182), M1 = modSlot(230);

  {   // ---- (1) the judgement ----
    Host H(opts(THUNDER_LIVE_F_EFFECTS, 4096)); std::vector<float> l, r; int64_t fr = 0;
    H.song(text); H.loadPads(ps); H->play(playOpts(0));
    CHECK(H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG, "no plan yet: the slot is nothing (E_ARG)");
    // slot 0 pump, slot 1 modular, slot 2 tfx (tape), slot 3 empty (null)
    const std::string c = "[" + P0 + "," + M0 + ",{\"type\":\"tape\",\"on\":true},null]";
    CHECK(H->fxPlan(0, (const uint8_t*)c.data(), c.size(), 120) == 0 && H->fxCommit(0, -1) == 0, "plan and commit the mixed chain");
    // the slots of the song reader are numbered by their place: 0 pump, 1 modular, 2 tfx
    double nan = std::nan(""), inf = INFINITY, huge[32]; for (int i = 0; i < 32; i++) huge[i] = 1e300;
    std::vector<double> bad = pv0; bad[5] = nan; std::vector<double> bad2 = pv0; bad2[5] = inf; std::vector<double> mbad = mp0; mbad[1] = nan;
    CHECK(H->fxValues(0, 0, THUNDER_LIVE_FXV_MOD, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG, "a pump slot with kind MOD is E_ARG");
    CHECK(H->fxValues(0, 1, THUNDER_LIVE_FXV_PUMP, mp0.data(), (int)mp0.size(), -1) == THUNDER_LIVE_E_ARG, "a modular slot with kind PUMP is E_ARG");
    CHECK(H->fxValues(0, 2, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG && H->fxValues(0, 2, THUNDER_LIVE_FXV_MOD, mp0.data(), (int)mp0.size(), -1) == THUNDER_LIVE_E_ARG, "a tfx slot is E_ARG, whatever the kind");
    CHECK(H->fxValues(0, 3, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG, "an empty slot is E_ARG");
    CHECK(H->fxValues(0, 5, THUNDER_LIVE_FXV_MOD, mp0.data(), (int)mp0.size(), -1) == THUNDER_LIVE_E_ARG, "a slot past the chain is E_ARG");
    CHECK(H->fxValues(1, 0, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG, "another track (no plan) is E_ARG");
    CHECK(H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size() - 1, -1) == THUNDER_LIVE_E_ARG && H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, pv0.data(), 0, -1) == THUNDER_LIVE_E_ARG, "a count that is not the planned one is E_ARG");
    CHECK(H->fxValues(0, 1, THUNDER_LIVE_FXV_MOD, mp0.data(), 4, -1) == THUNDER_LIVE_E_ARG, "a modular count that is not the planned one is E_ARG");
    CHECK(H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, bad.data(), (int)bad.size(), -1) == THUNDER_LIVE_E_ARG && H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, bad2.data(), (int)bad2.size(), -1) == THUNDER_LIVE_E_ARG && H->fxValues(0, 1, THUNDER_LIVE_FXV_MOD, mbad.data(), (int)mbad.size(), -1) == THUNDER_LIVE_E_ARG, "NaN / infinity is E_ARG");
    { std::vector<double> big = pv0; big[3] = 1e300; CHECK(H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, big.data(), (int)big.size(), -1) == THUNDER_LIVE_E_ARG, "a pump value that does not fit a float32 is E_ARG"); (void)huge; }
    CHECK(H->fxValues(0, 0, 0, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG && H->fxValues(0, 0, 3, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG, "kind 0 / 3 is E_ARG");
    CHECK(H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, nullptr, (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG, "NULL values is E_ARG");
    CHECK(H->fxValues(-1, 0, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG && H->fxValues(33, 0, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG && H->fxValues(0, -1, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG && H->fxValues(0, 8, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_ARG, "track / slot out of range is E_ARG");
    CHECK(H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), ((int64_t)1 << 62) + 1) == THUNDER_LIVE_E_ARG, "at above 2^62 is E_ARG");
    // nothing above reached the ring: the ring is empty, nothing was counted as busy, and the run is the one that never made the calls
    CHECK(stats(H.h).cmd_busy == 0, "no refusal counted as busy");
    H.run(l, r, 128 * 40, 128, fr);
    const std::vector<float> a = l;
    Host H2(opts(THUNDER_LIVE_F_EFFECTS, 4096)); std::vector<float> l2, r2; int64_t fr2 = 0; H2.song(text); H2.loadPads(ps); H2->play(playOpts(0));
    H2->fxPlan(0, (const uint8_t*)c.data(), c.size(), 120); H2->fxCommit(0, -1); H2.run(l2, r2, 128 * 40, 128, fr2);
    CHECK(ndiff(a, l2) == 0 && peakOf(a, 0) > 0.001, "(tfx slot, wrong kinds) the refused calls changed nothing: %zu samples differ from the run that never made them", ndiff(a, l2));
    // the ring: 16 records fit, the 17th is E_BUSY (counted), and after the next process the retry lands
    int ok = 0, rc = 0; for (int i = 0; i < 20; i++) { rc = H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, pv9.data(), (int)pv9.size(), -1); if (rc == 0) ok++; else break; }
    CHECK(ok == THUNDER_LIVE_FXVALUES_RING && rc == THUNDER_LIVE_E_BUSY && stats(H.h).cmd_busy == 1, "the ring holds %d records, then E_BUSY (ok %d, rc %d, busy %u)", THUNDER_LIVE_FXVALUES_RING, ok, rc, stats(H.h).cmd_busy);
    H.run(l, r, 128, 128, fr);
    CHECK(H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, pv9.data(), (int)pv9.size(), -1) == 0, "after a block the retry lands");
    // a host without F_EFFECTS
    Host N(opts(0, 4096)); CHECK(N->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, pv0.data(), (int)pv0.size(), -1) == THUNDER_LIVE_E_STATE, "without F_EFFECTS: E_STATE");
  }

  {   // ---- (2) a pump slot: audible in the quantum, equal to the plan with the values from the start ----
    const int T = 200;
    const Run C = run(text, ps, P0, nullptr, T);
    const Run B = run(text, ps, P9, nullptr, T);
    const Run A = run(text, ps, P0, [&](Host& H) { return H->fxValues(0, 0, THUNDER_LIVE_FXV_PUMP, pv9.data(), (int)pv9.size(), -1); }, T, true);
    CHECK(A.rc == 0, "pump values accepted (%d)", A.rc);
    const size_t q0 = (size_t)kSeg * 128, n = A.l.size() + q0;   // (the runs return the left channel of every call: kSeg + T quanta)
    (void)n;
    CHECK(A.l.size() == (size_t)(kSeg + T) * 128 && B.l.size() == A.l.size() && C.l.size() == A.l.size(), "the runs have their samples");
    CHECK(peakOf(C.l, 0) > 0.001 && ndiff(C.l, B.l) > 1000, "the two plans differ audibly (%zu samples): the test can see the change", ndiff(C.l, B.l));
    const long fd = firstDiff(A.l, C.l);
    CHECK(fd >= (long)q0 && fd < (long)q0 + 128, "the change is audible in the quantum it was made for: first differing sample %ld, quantum is [%zu, %zu)", fd, q0, q0 + 128);
    const size_t settled = q0 + 128 * 40;
    const double md = maxDiff(A.l, B.l, settled), pk = peakOf(B.l, settled);
    CHECK(pk > 0.001 && md <= 1e-4 * pk + 1e-7, "a pump slot's value change equals the plan with the value from the start once settled: max difference %g of peak %g", md, pk);
    CHECK(A.allocs == 0, "no allocation in the audio path while the pump values land (%ld)", A.allocs);
    std::printf("  pump: first diff %ld, settled max diff %g (peak %g), %zu samples bit-different after settling\n", fd, md, pk, ndiff(A.l, B.l, settled));
  }
  {   // ---- (2b) a modular slot ----
    const int T = 200;
    const Run C = run(text, ps, M0, nullptr, T);
    const Run B = run(text, ps, M1, nullptr, T);
    const Run A = run(text, ps, M0, [&](Host& H) { return H->fxValues(0, 0, THUNDER_LIVE_FXV_MOD, mp1.data(), (int)mp1.size(), -1); }, T, true);
    CHECK(A.rc == 0, "modular values accepted (%d)", A.rc);
    const size_t q0 = (size_t)kSeg * 128;
    CHECK(peakOf(C.l, 0) > 0.001 && ndiff(C.l, B.l) > 1000, "the two plans differ audibly (%zu samples)", ndiff(C.l, B.l));
    const long fd = firstDiff(A.l, C.l);
    CHECK(fd >= (long)q0 && fd < (long)q0 + 128, "the change is audible in the quantum it was made for: first differing sample %ld, quantum is [%zu, %zu)", fd, q0, q0 + 128);
    const size_t after = q0 + 128;
    const size_t settled = q0 + 128 * 40; const double md = maxDiff(A.l, B.l, settled), pk = peakOf(B.l, settled);
    CHECK(pk > 0.001 && md <= 1e-4 * pk + 1e-7, "a modular slot's value change equals the plan with the value from the start once settled: max difference %g of peak %g (%zu bit-different)", md, pk, ndiff(A.l, B.l, settled));
    std::printf("  modular: first diff %ld, differing samples right after the quantum %zu, settled max diff %g (peak %g)\n", fd, ndiff(A.l, B.l, after), md, pk);
    CHECK(A.allocs == 0, "no allocation in the audio path while the modular values land (%ld)", A.allocs);
  }
  {   // ---- (2c) a timed change: `at` ----
    const int T = 100; const int64_t at = (int64_t)(kSeg + 10) * 128 + 50;   // inside quantum kSeg + 10 (first block starting within 64 samples of `at`)
    const Run C = run(text, ps, M0, nullptr, T);
    const Run A = run(text, ps, M0, [&](Host& H) { return H->fxValues(0, 0, THUNDER_LIVE_FXV_MOD, mp1.data(), (int)mp1.size(), at); }, T);
    const long fd = firstDiff(A.l, C.l); const long q = (long)(kSeg + 10) * 128;
    CHECK(fd >= q && fd < q + 128, "a timed record lands in the quantum that holds `at`: first differing sample %ld, quantum [%ld, %ld)", fd, q, q + 128);
  }

  {   // ---- (3) the snapshot rule ----
    // old chain: pheat in slot 0; new chain: pwide in slot 0 (another key: a new object); the values are pwide's. A record that meets the OLD chain would be lost under the swap.
    const std::string H0 = "{\"type\":\"pheat\",\"on\":true}";
    const Run S = seamRun(text, ps, H0, P0, pv9, THUNDER_LIVE_FXV_PUMP, true), E = seamRun(text, ps, H0, P0, pv9, THUNDER_LIVE_FXV_PUMP, false);
    CHECK(S.rc == 0 && E.rc == 0, "plan, commit and values accepted at the seam (%d %d)", S.rc, E.rc);
    CHECK(ndiff(S.l, E.l) == 0, "(pump) a record pushed after a commit the loop did not see meets the NEW chain: %zu samples differ from the run with all three sent before the quantum", ndiff(S.l, E.l));
    const Run N = run(text, ps, H0, [&](Host& H) { const std::string c = chain(P0); int a = H->fxPlan(0, (const uint8_t*)c.data(), c.size(), 120), b = H->fxCommit(0, -1); return a | b; }, 150 + 1 - 1);
    CHECK(ndiff(E.l, N.l, (size_t)(kSeg + 1) * 128) > 1000, "the values are audible on the new chain (%zu samples differ from the run without them)", ndiff(E.l, N.l, (size_t)(kSeg + 1) * 128));
    const std::string W0 = "{\"type\":\"width\",\"on\":true}";
    const Run S2 = seamRun(text, ps, W0, M0, mp1, THUNDER_LIVE_FXV_MOD, true), E2 = seamRun(text, ps, W0, M0, mp1, THUNDER_LIVE_FXV_MOD, false);
    CHECK(S2.rc == 0 && E2.rc == 0 && ndiff(S2.l, E2.l) == 0, "(modular) the same: %zu samples differ", ndiff(S2.l, E2.l));
  }
#else
  std::printf("live_fxvalues: no effects / pump library in this build: only the capability and the NULL handle were checked\n");
#endif

#if defined(THUNDER_HAVE_TFX) && defined(THUNDER_HAVE_PUMP)
  {   // ---- (4) FxHost: a resting slot is not woken; nothing allocates; a slot of another kind is left alone ----
    // (a) RESTING: a pump slot that is OFF by its own setting rests after a quarter second of the same output. The old queue (psetPump) wakes it with a fresh instance (audioRemakes); psetPumpRt does not.
    const std::string off = chain(pumpSlot(0, false));
    uint32_t wakeOld = 0, wakeRt = 0; std::vector<float> oRt, oOld; long allocRt = 0;
    for (int mode = 0; mode < 2; mode++) {
      FxHost h(48000.0); int64_t n0 = 0; FxPlan* p = planOf(h, off); CHECK(p != nullptr, "plan the OFF pump slot"); if (!p) continue; h.commit(p); delete p;
      blocks(h, &n0, 600);   // 1.6 s: it is resting by now
      const uint32_t r0 = h.audioRemakes();
      const std::vector<double> v = pv9; std::vector<float> f(v.begin(), v.end());
      if (mode == 0) h.psetPump((double)n0 / 48000.0, 0, f.data(), (int)f.size()); else h.psetPumpRt((double)n0 / 48000.0, 0, f.data(), (int)f.size());
      if (mode == 1) { g_alloc = 0; g_count = true; }
      const std::vector<float> o = blocks(h, &n0, 40);
      if (mode == 1) { g_count = false; allocRt = g_alloc.load(); }
      (mode == 0 ? wakeOld : wakeRt) = h.audioRemakes() - r0; (mode == 0 ? oOld : oRt) = o;
    }
    CHECK(wakeOld >= 1, "the control: the old queue wakes a resting pump slot with a fresh instance (%u remakes): the slot was resting", wakeOld);
    CHECK(wakeRt == 0 && allocRt == 0, "psetPumpRt does not wake a resting slot and allocates nothing (remakes %u, allocations %ld)", wakeRt, allocRt);
    // (b) a RUNNING pump slot and a modular slot: no allocation while the values land and the next blocks run
    {
      FxHost h(48000.0); int64_t n0 = 0; FxPlan* p = planOf(h, "[" + P0 + "," + M0 + "]"); h.commit(p); delete p;   // (slot 0 pump, slot 1 modular)
      blocks(h, &n0, 100);
      std::vector<float> f(pv9.begin(), pv9.end());
      g_alloc = 0; g_count = true;
      h.psetPumpRt((double)n0 / 48000.0, 0, f.data(), (int)f.size()); h.psetModRt((double)n0 / 48000.0, 1, mp1.data(), (int)mp1.size());
      blocks(h, &n0, 50);
      g_count = false;
      CHECK(g_alloc.load() == 0 && h.audioRemakes() == 0, "no allocation, no fresh instance when values land on a running pump slot and a modular slot (%ld, %u)", g_alloc.load(), h.audioRemakes());
      CHECK(h.queueDropped() == 0, "no record dropped");
    }
    // (c) a record for a slot that is no longer of that kind is left alone: the same chain with and without a modular record aimed at a tfx slot / a pump record aimed at a modular slot
    {
      const std::string cc = "[" + M0 + ",{\"type\":\"tape\",\"on\":true}," + P0 + "]";
      std::vector<float> o[2]; for (int mode = 0; mode < 2; mode++) {
        FxHost h(48000.0); int64_t n0 = 0; FxPlan* p = planOf(h, cc); h.commit(p); delete p; blocks(h, &n0, 50);
        if (mode == 1) { std::vector<float> f(pv9.begin(), pv9.end()); h.psetModRt((double)n0 / 48000.0, 1, mp1.data(), (int)mp1.size()); h.psetPumpRt((double)n0 / 48000.0, 1, f.data(), (int)f.size()); h.psetPumpRt((double)n0 / 48000.0, 0, f.data(), (int)f.size()); h.psetModRt((double)n0 / 48000.0, 2, mp1.data(), (int)mp1.size()); h.psetModRt((double)n0 / 48000.0, 5, mp1.data(), (int)mp1.size()); }
        o[mode] = blocks(h, &n0, 50);
      }
      CHECK(peakOf(o[0], 0) > 0.001 && ndiff(o[0], o[1]) == 0, "records aimed at a slot of another kind (a tfx slot, a modular slot with pump values, a pump slot with modular values, a slot past the chain) change nothing: %zu samples differ", ndiff(o[0], o[1]));
    }
  }
#endif
  std::printf("live_fxvalues: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
