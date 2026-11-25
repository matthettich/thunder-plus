// P3b (ctest `live_fxparams`).
// (1) The FX-param window (the one C2 L1 closed for chains): a quantum drains only the fx_param records that were in the ring BEFORE its command loop. A TEST seam between the command loop and the drain pushes
//     fx_plan + fx_commit and then fx_param: the commit is not yet seen by this quantum, so the record must wait for the next one, where it meets the NEW chain (it used to be applied to the old chain and lost
//     under the swap). The same sends made before the next quantum give the same samples.
// (2) The random state of the effects (wasi rand(), stmlib::Random, DaisySP myrand(): Beads, Clouds, Cloud Seed, the pitch shifter) belongs to a PLAN: FxHost::plan (control thread) never touches what
//     FxHost::process (audio thread) reads. Single thread: planning (and dropping) plans between blocks leaves the audio unchanged, bit for bit; plans are deterministic. Two threads (TSan reports the old race):
//     one plans in a loop while the other processes.
#include <atomic>
#include <functional>
#include <thread>

#include "live_common.h"
#include "fx/fxhost.h"
#include "rt/json.h"
using namespace lt;

namespace {
#if defined(THUNDER_SANITIZE_BUILD) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr int kThreadPlans = 8;
#else
constexpr int kThreadPlans = 40;
#endif
size_t ndiff(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return a.size() + b.size() + 1;
  size_t bad = 0; for (size_t i = 0; i < a.size(); i++) if (std::memcmp(&a[i], &b[i], 4) != 0) bad++;
  return bad;
}
double peakOf(const std::vector<float>& v) { double p = 0; for (float x : v) p = std::max(p, (double)std::fabs(x)); return p; }

// ---- (1) ----
struct Seam { Host* H; const std::string* slots; double bpm; bool withParam, armed; int rc[3]; };
std::vector<float> fxParamRun(const std::string& text, const PadSet& ps, bool viaSeam, bool withParam, int rcOut[3]) {
  Host H(opts(THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER, 4096)); std::vector<float> l, r;
  if (!H.h) return l;
  H.song(text); H.loadPads(ps);
  // track 0 first holds a delay in slot 0 (committed 64 quanta in); the edit under test replaces it by a tape in slot 0: an old chain that the param could wrongly reach, and a new chain that must get it
  const std::string slots = "[{\"type\":\"tape\",\"on\":true}]", slots0 = "[{\"type\":\"delay\",\"on\":true,\"time\":3,\"fb\":60,\"mix\":50}]"; const double bpm = 120;
  double p12[12]; for (int i = 0; i < 12; i++) p12[i] = 0.1 * i;
  int64_t fr = 0; H->play(playOpts(0));
  H->fxPlan(0, (const uint8_t*)slots0.data(), slots0.size(), bpm); H->fxCommit(0, -1); H.run(l, r, 128 * 64, 128, fr);
  Seam sm{&H, &slots, bpm, withParam, false, {0, 0, 0}};
  if (viaSeam) {
    H->setTestFxSeam([](void* u) { Seam* m = (Seam*)u; if (!m->armed) return; m->armed = false; Host& h = *m->H;
      m->rc[0] = h->fxPlan(0, (const uint8_t*)m->slots->data(), m->slots->size(), m->bpm); m->rc[1] = h->fxCommit(0, -1);
      if (m->withParam) { double q[12]; for (int i = 0; i < 12; i++) q[i] = 0.1 * i; m->rc[2] = h->fxParam(0, 0, q, -1); } }, &sm);
    sm.armed = true; H.run(l, r, 128, 128, fr);   // the seam fires in this quantum
    H->setTestFxSeam(nullptr, nullptr);
  } else {
    H.run(l, r, 128, 128, fr);                    // the same quantum without the sends: they are made before the next one
    sm.rc[0] = H->fxPlan(0, (const uint8_t*)slots.data(), slots.size(), bpm); sm.rc[1] = H->fxCommit(0, -1);
    if (withParam) sm.rc[2] = H->fxParam(0, 0, p12, -1);
  }
  if (rcOut) for (int i = 0; i < 3; i++) rcOut[i] = sm.rc[i];
  H.run(l, r, 128 * 200, 128, fr);
  return l;
}
// ---- (2) ----
FxPlan* planOf(FxHost& h, const std::string& js) { JsonDoc d; JsonError je; if (!d.parse(js.data(), js.size(), &je)) return nullptr; return h.planFromSong(d.root(), 120, 0, nullptr); }
const char* kChainX = "[{\"type\":\"beads\",\"on\":true},{\"type\":\"pshift\",\"on\":true},{\"type\":\"clouds\",\"on\":true},{\"type\":\"reverb\",\"on\":true}]";
const char* kChainY = "[{\"type\":\"clouds\",\"on\":true},{\"type\":\"beads\",\"on\":true},{\"type\":\"pshift\",\"on\":true},{\"type\":\"space\",\"on\":true}]";
void feed(float* L, float* R, int64_t n0, int n) { for (int i = 0; i < n; i++) { const double t = (double)(n0 + i); L[i] = (float)(0.3 * std::sin(0.05 * t) + 0.2 * std::sin(0.31 * t)); R[i] = (float)(0.25 * std::sin(0.043 * t + 1)); } }
// blocks blocks of 128 on h (a continuing sample clock *n0); `between(b)` runs between blocks
std::vector<float> blocks(FxHost& h, int64_t* n0, int count, const std::function<void(int)>& between = nullptr) {
  std::vector<float> out; float L[128], R[128];
  for (int b = 0; b < count; b++) { if (between) between(b); feed(L, R, *n0, 128); h.process(L, R, 128, (double)*n0 / h.sampleRate()); out.insert(out.end(), L, L + 128); out.insert(out.end(), R, R + 128); *n0 += 128; }
  return out;
}
}  // namespace

int main() {
  const std::string text = electro();
  if (text.empty()) { std::printf("FAIL: cannot read ELECTRO\n"); return 1; }
  PadSet ps;
  {   // ---- (1) the fx_param window ----
    int rcL[3] = {-9, -9, -9}, rcE[3] = {-9, -9, -9};
    const std::vector<float> L = fxParamRun(text, ps, true, true, rcL), E = fxParamRun(text, ps, false, true, rcE), N = fxParamRun(text, ps, false, false, nullptr);
    CHECK(L.size() == 128 * 265 && E.size() == L.size() && N.size() == L.size(), "the runs made their samples (%zu %zu %zu)", L.size(), E.size(), N.size());
    CHECK(rcL[0] == 0 && rcL[1] == 0 && rcL[2] == 0 && rcE[0] == 0 && rcE[1] == 0 && rcE[2] == 0, "fx_plan, fx_commit, fx_param accepted (%d %d %d / %d %d %d)", rcL[0], rcL[1], rcL[2], rcE[0], rcE[1], rcE[2]);
    CHECK(ndiff(L, E) == 0, "(P3b) a param pushed after a commit the loop did not see meets the NEW chain (next quantum): %zu samples differ from the run with both sent before the quantum", ndiff(L, E));
#ifdef THUNDER_HAVE_TFX
    CHECK(ndiff(E, N) > 1000, "the param is audible on the new chain (%zu samples differ from the run without it)", ndiff(E, N));
#endif
  }
#ifdef THUNDER_HAVE_TFX
  {   // ---- (2a) plans leave the audio side alone ----
    FxHost A(48000.0), B(48000.0); int64_t nA = 0, nB = 0;
    { FxPlan* p = planOf(A, kChainX); CHECK(p != nullptr, "plan X"); A.commit(p); delete p; } { FxPlan* p = planOf(B, kChainX); B.commit(p); delete p; }
    std::vector<FxPlan*> dropped; uint64_t r0 = 0, r1 = 0; uint32_t s0 = 0, s1 = 0, d0 = 0, d1 = 0;
    A.audioRandState(&r0, &s0, &d0);
    { FxPlan* p = planOf(A, kChainY); CHECK(p != nullptr, "plan Y"); dropped.push_back(p); }   // constructors that draw (Cloud Seed / Airwindows rand(), Beads, Clouds, the pitch shifter): on the PLAN's state
    A.audioRandState(&r1, &s1, &d1);
    CHECK(r0 == r1 && s0 == s1 && d0 == d1, "(P3b) plan() leaves the audio side's random state alone (rand %llu -> %llu, stmlib %u -> %u, daisy %u -> %u)", (unsigned long long)r0, (unsigned long long)r1, s0, s1, d0, d1);
    const std::vector<float> a = blocks(A, &nA, 60, [&](int b) { if (b == 10 || b == 25 || b == 40) { FxPlan* p = planOf(A, b == 25 ? kChainX : kChainY); if (p) dropped.push_back(p); } });
    const std::vector<float> bb = blocks(B, &nB, 60);
    CHECK(peakOf(a) > 0.001, "the chain sounds (peak %g)", peakOf(a));
    CHECK(ndiff(a, bb) == 0, "(P3b) plans made between blocks (never committed) leave the audio unchanged: %zu samples differ", ndiff(a, bb));
    for (FxPlan* p : dropped) delete p;
    // (2b) plans are deterministic: the same sequence of plans and blocks on two hosts, with a new plan committed in the middle (new Beads / Clouds / pitch shifter objects)
    FxHost C(48000.0), D(48000.0); int64_t nC = 0, nD = 0; std::vector<float> c, d;
    for (FxHost* h : {&C, &D}) {
      int64_t* n = h == &C ? &nC : &nD; std::vector<float>& o = h == &C ? c : d;
      { FxPlan* p = planOf(*h, kChainX); h->commit(p); delete p; }
      std::vector<float> s1 = blocks(*h, n, 30); o.insert(o.end(), s1.begin(), s1.end());
      { FxPlan* p = planOf(*h, kChainY); h->commit(p); delete p; }
      std::vector<float> s2 = blocks(*h, n, 30); o.insert(o.end(), s2.begin(), s2.end());
      { FxPlan* p = planOf(*h, kChainX); h->commit(p); delete p; }
      std::vector<float> s3 = blocks(*h, n, 30); o.insert(o.end(), s3.begin(), s3.end());
    }
    CHECK(peakOf(c) > 1e-4 && ndiff(c, d) == 0, "(P3b) the same plans and blocks on two hosts give the same samples: %zu differ", ndiff(c, d));
  }
  {   // ---- (2c) two threads: the audio thread processes, the control thread plans (TSan reports the old shared rand_ / stmRng_ / daisySeed_) ----
    FxHost H(48000.0); std::atomic<FxPlan*> in{nullptr}, back{nullptr}; std::atomic<bool> stop{false}; std::atomic<uint64_t> nonFinite{0}, blocksRun{0};
    { FxPlan* p = planOf(H, kChainX); H.commit(p); delete p; }
    std::thread audio([&] {
      int64_t n0 = 0; float L[128], R[128];
      while (!stop.load(std::memory_order_acquire)) {
        if (FxPlan* p = in.exchange(nullptr, std::memory_order_acquire)) { H.commit(p); back.store(p, std::memory_order_release); }
        { double q[12]; for (int i = 0; i < 12; i++) q[i] = 0.5 + 0.45 * std::sin(0.37 * (double)n0 / 128.0 * (i + 1)); H.psetAt((double)n0 / 48000.0, 3, q); }   // new values for slot 3 every block: Cloud Seed (Space) re-draws its random taps in process()
        feed(L, R, n0, 128); H.process(L, R, 128, (double)n0 / 48000.0); n0 += 128; blocksRun.fetch_add(1, std::memory_order_relaxed);
        for (int i = 0; i < 128; i++) if (!std::isfinite(L[i]) || !std::isfinite(R[i])) nonFinite.fetch_add(1, std::memory_order_relaxed);
      }
    });
    int made = 0;
    for (int i = 0; i < kThreadPlans; i++) {
      FxPlan* p = planOf(H, i & 1 ? kChainY : kChainX); if (!p) break; made++;   // (on the control thread, while the audio thread is inside process())
      in.store(p, std::memory_order_release);
      FxPlan* r; while (!(r = back.exchange(nullptr, std::memory_order_acquire))) std::this_thread::yield();
      delete r;
    }
    stop.store(true, std::memory_order_release); audio.join();
    CHECK(made == kThreadPlans, "all plans made (%d)", made);
    CHECK(blocksRun.load() > 0 && nonFinite.load() == 0, "the audio thread ran %llu blocks, %llu non-finite samples", (unsigned long long)blocksRun.load(), (unsigned long long)nonFinite.load());
  }
#else
  std::printf("live_fxparams: no effects library in this build: only the param window was checked\n");
#endif
  std::printf("live_fxparams: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
