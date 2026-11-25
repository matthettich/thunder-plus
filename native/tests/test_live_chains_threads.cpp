// C2b (ctest `live_chains_threads`, also run under ThreadSanitizer, R13): a real AUDIO thread runs LiveHost::process in a loop while the CONTROL thread plans in a loop: structural publishes (a whole-patch plan: preparePatch of the
// engine on the control thread, the commit on the audio thread), CHN flips (keep-patch plans), parameter publishes and chain_edit (the edit ring), play / stop / hold, collect (settle + delete of the plans that came back), stats.
// E_BUSY is the answer to a second plan in flight and is retried. At the end both threads stop and the host is quiet: every call answered 0 / E_BUSY, no block failed, no sample was non-finite, a final structural publish lands,
// and the host is destroyed with plans in every place (R14). Under TSan any unsynchronised access of a plan, a node or a patch between the two threads is a report (the engine's own files are not changed by this unit).
#include <atomic>
#include <chrono>
#include <thread>

#include "live_common.h"
using namespace lt;
constexpr int Q = THUNDER_LIVE_QUANTUM;
#ifdef THUNDER_SANITIZE_BUILD
static const int ITER = 1500;
#else
static const int ITER = 6000;
#endif
static std::string patchOf(double freq, int extra) {
  std::string m = "{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"osc1\",\"type\":\"osc\",\"params\":{\"wave\":1,\"freq\":" + std::to_string(freq) + ",\"fm\":0,\"level\":0.5}},"
    "{\"id\":\"mA\",\"type\":\"smixer\",\"params\":{\"chans\":2,\"level\":1,\"l1\":1,\"p1\":0,\"l2\":1,\"p2\":0}},{\"id\":\"o2\",\"type\":\"chainOut\",\"params\":{\"track\":2,\"level\":1}}";
  for (int k = 1; k <= extra; k++) m += ",{\"id\":\"z" + std::to_string(k) + "\",\"type\":\"vca\",\"params\":{\"gain\":1}}";
  return "{\"format\":\"chains.patch\",\"version\":1,\"modules\":[" + m + "],\"cables\":[{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in2\"]},"
    "{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in2R\"]},{\"from\":[\"mA\",\"out\"],\"to\":[\"o2\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"o2\",\"inR\"]}]}";
}
static std::string songOf(const std::string& patch, bool on0 = true) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"master\":255,\"mout\":255,\"ntr\":8,\"song\":[[-1,0";
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
struct Rng { uint64_t s; uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); } int below(int n) { return (int)(next() % (uint32_t)n); } };

int main() {
  const std::string V[6] = {songOf(patchOf(330, 0)), songOf(patchOf(330, 1)), songOf(patchOf(330, 2)), songOf(patchOf(330, 0), false), songOf(patchOf(441, 0)), songOf(patchOf(523, 1), false)};
  thunder_live_opts o = opts(THUNDER_LIVE_F_CHAINS | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER, 1024);
  Host A(o); if (!A.h) return 1;
  CHECK(A.song(V[0], 1) == 0, "setup"); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); CHECK(A->play(po) == 0, "play");
  std::atomic<bool> stop{false}; std::atomic<int> audioErr{0}; std::atomic<int64_t> frames{0}; int64_t endFrame = 0;
  std::thread audio([&] {
    std::vector<float> l(1024), r(1024); Rng g{77}; int64_t fr = 0;
    while (!stop.load(std::memory_order_acquire)) {
      const int n = g.below(3) == 0 ? 1 + g.below(1024) : 128; if (A->process(l.data(), r.data(), n, fr, 0)) audioErr++;
      for (int i = 0; i < n; i++) if (!std::isfinite(l[(size_t)i]) || !std::isfinite(r[(size_t)i])) audioErr++;
      fr += n; frames.store(fr, std::memory_order_relaxed); if ((fr & 0x3FFF) < 1024) std::this_thread::yield();
    }
    endFrame = fr;
  });
  int bad = 0, busy = 0, ok = 0; Rng g{4242}; int rev = 2;
  auto retry = [&](auto&& f) { int rc = f(); for (int k = 0; k < 80 && rc == THUNDER_LIVE_E_BUSY; k++) { busy++; std::this_thread::yield(); std::this_thread::sleep_for(std::chrono::microseconds(100)); rc = f(); } return rc; };
  static const char* edits[3] = {"[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":400}]", "[{\"op\":\"param\",\"id\":\"mA\",\"name\":\"level\",\"value\":0.6},{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"level\",\"value\":0.3}]", "[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":220}]"};
  for (int it = 0; it < ITER; it++) {
    const int k = g.below(100); int rc = 0;
    if (k < 35) rc = retry([&] { return A.song(V[g.below(6)], rev++); });
    else if (k < 55) rc = retry([&] { const char* j = edits[g.below(3)]; return A->chainEdit((const uint8_t*)j, std::strlen(j), THUNDER_LIVE_NOW); });
    else if (k < 62) rc = retry([&] { return A->play(po); });
    else if (k < 68) rc = retry([&] { return A->stop(THUNDER_LIVE_NOW); });
    else if (k < 71) rc = retry([&] { return A->hold(THUNDER_LIVE_NOW); });
    else if (k < 85) A->collect();
    else if (k < 92) { uint8_t buf[512]; while (A->poll(buf, sizeof buf) > 0) {} }
    else { thunder_live_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; A->statsGet(&s); if (s.chains_failed_blocks) bad++; }
    if (rc == 0) ok++; else if (rc != THUNDER_LIVE_E_BUSY && rc != THUNDER_LIVE_E_STATE) { bad++; std::printf("op %d (k %d): rc %d\n", it, k, rc); }
    if ((it & 31) == 0) std::this_thread::sleep_for(std::chrono::microseconds(150));
  }
  stop.store(true, std::memory_order_release); audio.join();
  CHECK(bad == 0, "every call answered 0 / E_BUSY / E_STATE (%d bad, %d busy, %d ok)", bad, busy, ok);
  CHECK(audioErr.load() == 0, "process never failed and never produced a non-finite sample (%d)", audioErr.load());
  CHECK(endFrame > 48000 * 2, "the audio thread ran (%lld frames)", (long long)endFrame);
  // quiet now: the last in-flight plan comes back through a few quanta, a final structural publish lands, a play sounds
  int64_t fr = endFrame; std::vector<float> l, r; A.run(l, r, Q * 8, Q, fr); A->collect();
  CHECK(retry([&] { return A->play(po); }) == 0, "play after the storm"); A.run(l, r, Q * 8, Q, fr);
  int rc = retry([&] { A->collect(); A.run(l, r, Q * 2, Q, fr); return A.song(V[2], rev++); }); CHECK(rc == 0, "a final structural publish (%d)", rc); A.run(l, r, Q * 40, Q, fr);
  const thunder_live_stats s = stats(A.h);
  CHECK(s.chains_failed_blocks == 0 && s.chains_nodes >= 1 && s.chains_plans > 20, "no failed block, nodes %u, plans %u, busy %u, refused %u, blocks %llu", s.chains_nodes, s.chains_plans, s.chains_busy, s.chains_refused, (unsigned long long)s.chains_blocks);
  double pk = 0; for (size_t i = l.size() - (size_t)Q * 20; i < l.size(); i++) pk = std::max(pk, (double)std::fabs(l[i])); CHECK(pk > 0.01, "the chain sounds after the storm (peak %g)", pk);
  // destroyed with a plan queued (R14)
  A.song(V[1], rev++);   // a plan left in flight (or E_BUSY): the host is destroyed with it, no quantum runs again (R14)
  std::printf("live_chains_threads: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
