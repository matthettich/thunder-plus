// VERIFIER'S adversarial tests (not part of the PR). argv[1]: det | thr [seconds]
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <thread>
#include <algorithm>

static std::atomic<long> g_live{0}, g_audioAllocs{0};
static thread_local bool t_audio = false;
static void* cnt(size_t n) { void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); g_live.fetch_add(1, std::memory_order_relaxed); if (t_audio) g_audioAllocs.fetch_add(1, std::memory_order_relaxed); return p; }
static void unc(void* p) noexcept { if (p) { g_live.fetch_sub(1, std::memory_order_relaxed); std::free(p); } }
void* operator new(size_t n) { return cnt(n); }
void* operator new[](size_t n) { return cnt(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { try { return cnt(n); } catch (...) { return nullptr; } }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { try { return cnt(n); } catch (...) { return nullptr; } }
void operator delete(void* p) noexcept { unc(p); }
void operator delete[](void* p) noexcept { unc(p); }
void operator delete(void* p, size_t) noexcept { unc(p); }
void operator delete[](void* p, size_t) noexcept { unc(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { unc(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { unc(p); }

#include "chains/chost.h"
#include "live_common.h"
using namespace lt;
constexpr int Q = THUNDER_LIVE_QUANTUM;
static const uint32_t FC = THUNDER_LIVE_F_CHAINS;

static std::string patchOf(double freq, int extra, double level = 0.5) {
  std::string m = "{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"osc1\",\"type\":\"osc\",\"params\":{\"wave\":1,\"freq\":" + std::to_string(freq) + ",\"fm\":0,\"level\":" + std::to_string(level) + "}},"
    "{\"id\":\"mA\",\"type\":\"smixer\",\"params\":{\"chans\":2,\"level\":1,\"l1\":1,\"p1\":0,\"l2\":1,\"p2\":0}},{\"id\":\"o2\",\"type\":\"chainOut\",\"params\":{\"track\":2,\"level\":1}}";
  for (int k = 1; k <= extra; k++) m += ",{\"id\":\"z" + std::to_string(k) + "\",\"type\":\"vca\",\"params\":{\"gain\":1}}";
  return "{\"format\":\"chains.patch\",\"version\":1,\"modules\":[" + m + "],\"cables\":[{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in2\"]},"
    "{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in2R\"]},{\"from\":[\"mA\",\"out\"],\"to\":[\"o2\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"o2\",\"inR\"]}]}";
}
static std::string songOf(const std::string& patch, bool on0 = true, int ticks = 6, int lpb = 4) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":" + std::to_string(lpb) + ",\"ticks\":" + std::to_string(ticks) + ",\"master\":255,\"mout\":255,\"ntr\":8,\"song\":[[-1,0";
  for (int t = 2; t < kSongNT; t++) s += ",-1";
  s += "]],\"chains\":[[[0,0]]],\"plen\":[8],\"phrases\":[[[60,-1,-1,-1,-1,0,38,17,19,90,-1,-1]";
  for (int i = 1; i < 8; i++) s += ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]";
  s += "]],\"mix\":[";
  for (int t = 0; t < kSongNT; t++) s += std::string(t ? "," : "") + "{\"vol\":224,\"trim\":128,\"pan\":128,\"mute\":false,\"solo\":false}";
  s += "],\"chainOn\":[" + std::string(on0 ? "true" : "false") + ",true";
  for (int t = 2; t < kSongNT; t++) s += ",false";
  s += "],\"chainPatch\":" + patch + "}";
  return s;
}
static int edit(LiveHost* h, const std::string& j) { return h->chainEdit((const uint8_t*)j.data(), j.size(), THUNDER_LIVE_NOW); }
struct Rig { Host H; int64_t fr = 0; std::vector<float> l, r; explicit Rig(const thunder_live_opts& o) : H(o) {} int run(int64_t n) { return H.run(l, r, n, Q, fr); } thunder_live_stats st() { return stats(H.h); } };
static size_t ndiff(const std::vector<float>& a, const std::vector<float>& b, size_t from) { size_t bad = 0; if (a.size() != b.size()) return 1u << 30; for (size_t i = from; i < a.size(); i++) if (std::memcmp(&a[i], &b[i], 4)) bad++; return bad; }
static double pk(const std::vector<float>& v, size_t from) { double p = 0; for (size_t i = from; i < v.size(); i++) p = std::max(p, (double)std::fabs(v[i])); return p; }

struct Rng { uint64_t s; uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); } int below(int n) { return (int)(next() % (uint32_t)n); } };

static void det() {
  thunder_live_opts o = opts(FC, 4096);
  // D1: a param record waiting in the ring when a structural publish lands is applied AFTER the swap (stale value over the new patch)
  {
    Rig A(o), B(o); CHECK(A.H.h && B.H.h, "create");
    const std::string S0 = songOf(patchOf(330, 0)), S1 = songOf(patchOf(523, 1));
    for (Rig* R : {&A, &B}) { CHECK(R->H.song(S0, 1) == 0, "s0"); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); CHECK(R->H->play(po) == 0, "play"); R->run(Q * 60); R->H->collect(); }
    // A: chain_edit(osc1.freq = 200) then (no quantum between) structural publish S1 (freq 523).  B: only the structural publish.
    CHECK(edit(A.H.h, "[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":200}]") == 0, "A edit");
    CHECK(A.H.song(S1, 2) == 0 && B.H.song(S1, 2) == 0, "structural publish");
    A.l.clear(); A.r.clear(); B.l.clear(); B.r.clear(); A.run(Q * 80); B.run(Q * 80);
    const size_t nd = ndiff(A.l, B.l, 0);
    std::printf("D1 stale param over a newer structural patch: %zu of %zu samples differ (0 = fine)\n", nd, A.l.size());
    CHECK(nd == 0, "D1: a param record older than a structural publish must not override the new patch");
  }
  // D2: chain_edit moves the audio side but not chainsSong_: a later publish of the ORIGINAL values finds Same and sends nothing
  {
    Rig A(o), B(o);
    const std::string S0 = songOf(patchOf(330, 0));
    for (Rig* R : {&A, &B}) { CHECK(R->H.song(S0, 1) == 0, "s0"); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); CHECK(R->H->play(po) == 0, "play"); R->run(Q * 60); R->H->collect(); }
    CHECK(edit(A.H.h, "[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":900}]") == 0, "edit");
    A.run(Q * 10); B.run(Q * 10);
    CHECK(A.H.song(S0, 2) == 0 && B.H.song(S0, 2) == 0, "republish original (cancel the gesture)");
    A.l.clear(); A.r.clear(); B.l.clear(); B.r.clear(); A.run(Q * 80); B.run(Q * 80);
    const size_t nd = ndiff(A.l, B.l, 0);
    std::printf("D2 chain_edit then republish of the ORIGINAL song: %zu of %zu samples differ (0 = audio follows the song)\n", nd, A.l.size());
    CHECK(nd == 0, "D2: audio keeps the edited value although the shell's song says otherwise");
  }
  // D3: ticks / lpb changed with the same chain patch: nodes keep the old transport
  {
    Rig A(o); CHECK(A.H.song(songOf(patchOf(330, 0), true, 6, 4), 1) == 0, "s"); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); CHECK(A.H->play(po) == 0, "play"); A.run(Q * 8);
    CHECK(A.H->stop(THUNDER_LIVE_NOW) == 0, "stop"); A.run(Q * 4); A.H->collect();
    CHECK(A.H.song(songOf(patchOf(330, 0), true, 8, 3), 2) == 0, "idle publish ticks 8 lpb 3");
    CHECK(A.H->play(po) == 0, "play2"); A.run(Q * 8);
    chains::ChainsNode* n = A.H->chainsHostForTest()->node(1); CHECK(n != nullptr, "node");
    if (n) { const waves::Transport t = n->engine().transport(); std::printf("D3 transport after play of a song with ticks 8 lpb 3: ticks %g lpb %g tempo %g (the page sends ticks 8, lpb 3)\n", (double)t.ticks, (double)t.lpb, (double)t.tempo); CHECK(t.ticks == 8 && t.lpb == 3, "D3 transport lpb/ticks stale"); }
  }
  // D4: inFlight never sticks: many structural publishes with and without collect, play/stop with plans, drop paths
  {
    Rig A(o); Rng g{5}; int busy = 0, ok = 0; int rev = 1; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW);
    CHECK(A.H.song(songOf(patchOf(330, 0)), rev++) == 0, "s"); CHECK(A.H->play(po) == 0, "play");
    for (int it = 0; it < 400; it++) {
      const int k = g.below(10); int rc = 0;
      if (k < 4) rc = A.H.song(songOf(patchOf(300 + g.below(5) * 20, g.below(3)), g.below(4) != 0), rev++);
      else if (k == 4) { rc = A.H->play(po); }
      else if (k == 5) rc = A.H->stop(THUNDER_LIVE_NOW);
      else if (k == 6) A.H->collect();
      else A.run(Q * (1 + g.below(3)));
      if (rc == THUNDER_LIVE_E_BUSY) busy++; else if (rc == 0) ok++; else if (rc != THUNDER_LIVE_E_STATE) CHECK(false, "rc %d at %d", rc, it);
    }
    for (int k = 0; k < 10; k++) { A.H->collect(); A.run(Q * 4); }
    int rc = A.H.song(songOf(patchOf(777, 2), true), rev++); if (rc == THUNDER_LIVE_E_BUSY) { A.H->collect(); A.run(Q * 4); A.H->collect(); rc = A.H.song(songOf(patchOf(778, 2), true), rev++); }
    CHECK(rc == 0, "D4 after the storm a structural publish is accepted again (rc %d; busy %d ok %d)", rc, busy, ok);
    A.run(Q * 20); A.H->collect(); const auto s = A.st(); CHECK(s.chains_nodes >= 1, "nodes %u", s.chains_nodes);
  }

  // D6: play() answering E_BUSY because a chains plan is in flight: were the song's 33 effects plans committed anyway (unwindFx)?
  {
    thunder_live_opts oe = opts(FC | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER, 4096);
    Rig A(oe); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW);
    CHECK(A.H.song(songOf(patchOf(330, 0)), 1) == 0, "s"); CHECK(A.H->play(po) == 0, "play1 (its plan is in flight: no quantum ran)");
    CHECK(A.H->stop(THUNDER_LIVE_NOW) == 0, "stop");
    CHECK(A.H.song(songOf(patchOf(330, 2)), 2) == 0, "idle publish with a different patch");
    const thunder_live_stats s0 = A.st();
    const int rc = A.H->play(po);
    A.run(Q * 4);
    const thunder_live_stats s1 = A.st();
    std::printf("D6 play2 while a chains plan is in flight: rc %d (E_BUSY=%d); commands applied by the audio side afterwards: %u (play1 + stop = 2; more means the failed play still committed effects plans); busy %u\n", rc, THUNDER_LIVE_E_BUSY, s1.cmds_applied - s0.cmds_applied, s1.cmd_busy);
    CHECK(rc == THUNDER_LIVE_E_BUSY, "D6 play is E_BUSY");
    // the Play and the Stop of play1 are applied in that run: 2 commands; anything above is the leaked effects commits
    CHECK(s1.cmds_applied - s0.cmds_applied <= 2, "D6: a play answering E_BUSY must leave NOTHING changed, yet %u commands reached the audio side", s1.cmds_applied - s0.cmds_applied);
  }
  // D5: chain_edit validation
  {
    Rig A(o); CHECK(edit(A.H.h, "[]") == THUNDER_LIVE_E_STATE, "E_STATE before any plan");
    CHECK(A.H.song(songOf(patchOf(330, 1)), 1) == 0, "s"); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); CHECK(A.H->play(po) == 0, "play"); A.run(Q * 8);
    struct C { const char* j; int rc; } cs[] = {
      {"[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":1e999}]", THUNDER_LIVE_E_ARG},
      {"[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":\"1\"}]", THUNDER_LIVE_E_ARG},
      {"[{\"op\":\"param\",\"id\":\"osc1\\u0000zz\",\"name\":\"freq\",\"value\":1}]", THUNDER_LIVE_E_ARG},
      {"[{\"op\":\"param\",\"id\":\"i1\",\"name\":\"track\",\"value\":2}]", THUNDER_LIVE_E_ARG},
      {"[{\"op\":\"param\",\"id\":\"o2\",\"name\":\"track\",\"value\":3}]", THUNDER_LIVE_E_ARG},
      {"[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":1},{\"op\":\"param\",\"id\":\"nope\",\"name\":\"freq\",\"value\":1}]", THUNDER_LIVE_E_ARG},
      {"[{\"op\":\"text\",\"id\":\"osc1\"}]", THUNDER_LIVE_E_ARG},
      {"{}", THUNDER_LIVE_E_ARG}, {"", THUNDER_LIVE_E_ARG},
      {"[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"abcdefghijklmnopqrstuvwxyzabcdefg\",\"value\":1}]", THUNDER_LIVE_E_LIMIT},
      {"[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":5}]", 0},
    };
    for (const C& c : cs) { const int rc = A.H->chainEdit((const uint8_t*)c.j, std::strlen(c.j), THUNDER_LIVE_NOW); CHECK(rc == c.rc, "D5 [%s] -> %d (want %d)", c.j, rc, c.rc); }
    CHECK(A.H->chainEdit((const uint8_t*)"[]", 2, 5) == THUNDER_LIVE_E_ARG, "at != NOW");
    CHECK(A.H->chainEdit(nullptr, 3, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG, "null");
    CHECK(A.H->chainEdit((const uint8_t*)"[]", (size_t)1 << 21, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_LIMIT, "limit");
    // ring: 256 ops fits exactly, 257 -> LIMIT; BUSY when full
    std::string big = "["; for (int i = 0; i < 256; i++) big += std::string(i ? "," : "") + "{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":" + std::to_string(100 + i) + "}"; big += "]";
    // (the earlier accepted edit is still in the ring: no quantum ran)
    CHECK(edit(A.H.h, big) == THUNDER_LIVE_E_BUSY, "256 ops with 1 waiting: BUSY");
    A.run(Q * 2); CHECK(edit(A.H.h, big) == 0, "256 ops after a quantum drained the ring");
    big.insert(1, "{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":1},"); A.run(Q * 2); CHECK(edit(A.H.h, big) == THUNDER_LIVE_E_LIMIT, "257 ops");
    // a param for a module the audio side no longer has (swap removes z1): harmless
    CHECK(edit(A.H.h, "[{\"op\":\"param\",\"id\":\"z1\",\"name\":\"gain\",\"value\":0.5}]") == 0, "z1 exists");
    CHECK(A.H.song(songOf(patchOf(330, 0)), 2) == 0, "structural publish removing z1"); A.run(Q * 40);
    CHECK(edit(A.H.h, "[{\"op\":\"param\",\"id\":\"z1\",\"name\":\"gain\",\"value\":0.5}]") == THUNDER_LIVE_E_ARG, "z1 gone");
    const auto s = A.st(); CHECK(s.chains_failed_blocks == 0 && s.chains_feed_mismatch == 0, "no failed block / mismatch");
  }
}

static void thr(int seconds) {
  // warm-up: static tables (chains registry, etc.)
  { thunder_live_opts o = opts(FC | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER, 1024); Rig w(o); w.H.song(songOf(patchOf(330, 1)), 1); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q * 20); w.H.song(songOf(patchOf(331, 2)), 2); w.run(Q * 20); w.H->collect(); w.run(Q * 4); }
  const long base = g_live.load();
  {
    thunder_live_opts o = opts(FC | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER, 1024);
    Host A(o); if (!A.h) { CHECK(false, "create"); return; }
    thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW);
    CHECK(A.song(songOf(patchOf(330, 0)), 1) == 0, "setup"); CHECK(A->play(po) == 0, "play");
    // one priming process call on the audio thread so pe_prepare is done; then count allocations on it
    std::atomic<bool> stop{false}; std::atomic<int> audioErr{0}; std::atomic<int64_t> frames{0}; std::atomic<bool> primed{false}; int64_t endFrame = 0;
    std::thread audio([&] {
      std::vector<float> l(1024), r(1024); Rng g{91}; int64_t fr = 0;
      A->process(l.data(), r.data(), 128, fr, 0); fr += 128; primed = true; t_audio = true;
      while (!stop.load(std::memory_order_acquire)) {
        const int n = g.below(4) == 0 ? 1 + g.below(1024) : 128; if (A->process(l.data(), r.data(), n, fr, 0)) audioErr++;
        for (int i = 0; i < n; i++) if (!std::isfinite(l[(size_t)i]) || !std::isfinite(r[(size_t)i])) { audioErr++; break; }
        fr += n; frames.store(fr, std::memory_order_relaxed); if ((fr & 0x3FFF) < 512) std::this_thread::yield();
      }
      t_audio = false; endFrame = fr;
    });
    while (!primed) std::this_thread::yield();
    const std::string V[8] = {songOf(patchOf(330, 0)), songOf(patchOf(330, 1)), songOf(patchOf(330, 3)), songOf(patchOf(330, 0), false), songOf(patchOf(441, 0)), songOf(patchOf(523, 1), false), songOf(patchOf(330, 0, 0.25)), songOf(patchOf(330, 1, 0.7), false, 8, 3)};
    static const char* ed[5] = {"[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":400}]", "[{\"op\":\"param\",\"id\":\"mA\",\"name\":\"level\",\"value\":0.6},{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"level\",\"value\":0.3}]",
      "[{\"op\":\"param\",\"id\":\"z1\",\"name\":\"gain\",\"value\":0.7}]", "[{\"op\":\"param\",\"id\":\"z3\",\"name\":\"gain\",\"value\":0.7}]", "[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"_bn\",\"value\":3},{\"op\":\"param\",\"id\":\"mA\",\"name\":\"_byp\",\"value\":1}]"};
    Rng g{1234}; int rev = 2; long calls = 0, busy = 0, bad = 0; const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds) {
      const int k = g.below(100); int rc = 0; calls++;
      if (k < 30) rc = A.song(V[g.below(8)], rev++);
      else if (k < 55) { const char* j = ed[g.below(5)]; rc = A->chainEdit((const uint8_t*)j, std::strlen(j), THUNDER_LIVE_NOW); }
      else if (k < 62) rc = A->play(po);
      else if (k < 68) rc = A->stop(THUNDER_LIVE_NOW);
      else if (k < 71) rc = A->hold(THUNDER_LIVE_NOW);
      else if (k < 85) A->collect();
      else if (k < 92) { uint8_t buf[512]; while (A->poll(buf, sizeof buf) > 0) {} }
      else { thunder_live_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; A->statsGet(&s); if (s.chains_failed_blocks || s.chains_feed_mismatch) bad++; }
      if (rc == THUNDER_LIVE_E_BUSY) busy++; else if (rc != 0 && rc != THUNDER_LIVE_E_STATE && rc != THUNDER_LIVE_E_ARG) { bad++; std::printf("rc %d k %d\n", rc, k); }
      if ((calls & 15) == 0) std::this_thread::sleep_for(std::chrono::microseconds(80));
    }
    stop = true; audio.join();
    std::printf("thr: %ld calls, %ld busy, %ld bad, audio %lld frames, audio-thread allocations %ld\n", calls, busy, bad, (long long)endFrame, g_audioAllocs.load());
    CHECK(bad == 0, "bad %ld", bad); CHECK(audioErr == 0, "audio errors %d", audioErr.load()); CHECK(g_audioAllocs.load() == 0, "allocations on the audio thread: %ld", g_audioAllocs.load());
    // quiesce; the control side must be able to plan again (inFlight not stuck)
    std::vector<float> l, r; int64_t fr = endFrame; int rc = -1;
    for (int k = 0; k < 20 && rc != 0; k++) { A->collect(); A.run(l, r, Q * 4, Q, fr); rc = A.song(V[2], rev++); }
    CHECK(rc == 0, "a structural publish is accepted after the storm (rc %d)", rc);
    A.run(l, r, Q * 40, Q, fr); A->collect();
    const thunder_live_stats s = stats(A.h); CHECK(s.chains_failed_blocks == 0 && s.chains_feed_mismatch == 0 && s.chains_nodes >= 1, "nodes %u failed %llu mismatch %llu plans %u busy %u refused %u", s.chains_nodes, (unsigned long long)s.chains_failed_blocks, (unsigned long long)s.chains_feed_mismatch, s.chains_plans, s.chains_busy, s.chains_refused);
    A.song(V[1], rev++);   // leave a plan in flight
  }
  const long after = g_live.load();
  std::printf("live allocations: baseline %ld after destroy %ld\n", base, after);
  CHECK(after == base, "live allocation count back to baseline (%ld vs %ld)", after, base);
}
int main(int argc, char** argv) {
  const std::string m = argc > 1 ? argv[1] : "det";
  if (m == "det") det(); else thr(argc > 2 ? std::atoi(argv[2]) : 30);
  std::printf("vadv %s: %d checks, %d failed\n", m.c_str(), g_checks, g_fail); return g_fail ? 1 : 0;
}
