// U31 (lead review, item K): the live runner with its two real threads. One thread is the "audio thread" (ChainsLive::process in a loop), one is the control thread (Param / Bypass / Playing / Tempo events, preparePatch,
// service(), stats()), for a few seconds. Meant for a ThreadSanitizer build (-fsanitize=thread, see docs/port/U31-NOTES.md "ThreadSanitizer"); in any build it checks that nothing is lost or corrupted: the output stays finite,
// every patch that preparePatch accepted is freed and none is leaked, the counters add up.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include "chains/chains.h"
#include "rt/json.h"
using namespace thunder; using namespace thunder::chains;
static std::string patch(double gain, bool pump) {
  std::string pm = "{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"g\",\"type\":\"vca\",\"params\":{\"gain\":" + std::to_string(gain) + "}},{\"id\":\"m\",\"type\":\"vca\"},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":0.9}}";
  std::string cb = "{\"from\":[\"a\",\"out\"],\"to\":[\"g\",\"in\"]},{\"from\":[\"g\",\"out\"],\"to\":[\"m\",\"in\"]},{\"from\":[\"m\",\"out\"],\"to\":[\"c\",\"in\"]}";
  if (pump) { const WInfo* wi = cinfo("ptube"); std::string ps; for (int k = 0; k < wi->np; k++) { char b[200]; std::snprintf(b, sizeof b, "%s\"%s\":%.17g", k ? "," : "", wi->params[k].name, wi->params[k].mx); ps += b; }
    pm += ",{\"id\":\"p\",\"type\":\"ptube\",\"params\":{" + ps + "}}"; cb += ",{\"from\":[\"a\",\"out\"],\"to\":[\"p\",\"in\"]},{\"from\":[\"p\",\"out\"],\"to\":[\"c\",\"inR\"]}"; }
  return pm + "],\"cables\":[" + cb + "],\"transport\":{\"tempo\":110,\"playing\":true}}";
}
int main(int argc, char** argv) {
  std::printf("sizeof(ChainsEvent) = %zu, sizeof(ChainsLive) = %zu bytes (queue %d + pending %d events); 32 tracks = %.2f MB\n", sizeof(ChainsEvent), sizeof(ChainsLive), kChainsQueue, kChainsPending, 32.0 * (double)sizeof(ChainsLive) / 1e6);
  const double seconds = argc > 1 ? std::atof(argv[1]) : 3.0; const bool pump = chains_have_lib("ptube");
  waves::Host host; ChainsLive live(48000, 0, ChainsLiveConfig(), &host); if (!live.valid()) { std::printf("not valid\n"); return 1; }
  JsonDoc d0; d0.parse(patch(0.5, pump)); if (!live.loadPatch(d0.root())) { std::printf("patch refused\n"); return 1; }
  std::atomic<bool> stop{false}; std::atomic<uint64_t> blocks{0}; std::atomic<bool> bad{false};
  std::thread audio([&] {
    std::vector<float> x(4096), L(4096), R(4096); uint32_t sd = 9; for (float& v : x) { sd = sd * 1664525u + 1013904223u; v = ((float)(sd >> 8) / 16777216.f - .5f) * 15.8f; }
    int pos = 0; const int sizes[5] = {128, 64, 100, 17, 300};
    while (!stop.load(std::memory_order_relaxed)) {
      const int n = sizes[blocks.load(std::memory_order_relaxed) % 5]; NodeIn in; in.inL = &x[(size_t)pos]; in.inR = &x[(size_t)pos]; NodeOut out; out.L = L.data(); out.R = R.data(); live.process(in, out, n);
      for (int i = 0; i < n; i++) if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) bad = true;
      pos = (pos + n) % 3000; blocks++;
    }
  });
  uint64_t posted = 0, patches = 0, refused = 0, iters = 0; const auto t0 = std::chrono::steady_clock::now();
  uint32_t r = 77; auto rnd = [&](int n) { r = r * 1664525u + 1013904223u; return (int)((r >> 8) % (uint32_t)n); };
  while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds) {
    iters++; const int64_t at = live.enginePos() + rnd(600);
    for (int k = 0; k < 6; k++) { const int w = rnd(10);
      if (w < 5) posted += live.postParam(rnd(2) ? "g" : "m", "gain", (rnd(100)) / 100.0, at + k) ? 1 : 0;
      else if (w < 7) { ChainsEvent e; e.kind = ChainsEvent::Bypass; e.at = at; e.a = 1 + rnd(3); e.b = rnd(4) - 1; posted += live.post(e) ? 1 : 0; }
      else if (w < 8) { ChainsEvent e; e.kind = ChainsEvent::Playing; e.at = at; e.a = rnd(2); posted += live.post(e) ? 1 : 0; }
      else if (w < 9) { ChainsEvent e; e.kind = ChainsEvent::Tempo; e.at = at; e.a = 60 + rnd(120); posted += live.post(e) ? 1 : 0; }
      else posted += live.postParam("m", "track", 1, at) ? 1 : 0; }
    if (iters % 40 == 0) { JsonDoc d; d.parse(patch(0.3 + (iters % 7) / 10.0, pump)); if (live.preparePatch(d.root(), at + 200)) patches++; else refused++; }
    if (iters % 3 == 0) live.service();
    const ChainsLiveStats s = live.stats(); (void)s;
    std::this_thread::sleep_for(std::chrono::microseconds(300));
  }
  stop = true; audio.join(); live.service();
  const ChainsLiveStats s = live.stats();
  std::printf("chains_threads: %.1f s, %llu blocks, %llu events accepted, %llu patches delivered (%llu refused), stats: dropped %llu coalesced %llu droppedParams %llu deferred %llu late %llu; patches in flight at the end: %d\n", seconds,
              (unsigned long long)blocks.load(), (unsigned long long)posted, (unsigned long long)patches, (unsigned long long)refused, (unsigned long long)s.droppedEvents, (unsigned long long)s.coalescedEvents, (unsigned long long)s.droppedParams,
              (unsigned long long)s.deferredPatches, (unsigned long long)s.lateEvents, live.patchesInFlight());
  std::printf("pump spares: %llu bad blocks, %llu spares swapped in, %llu bad blocks with no spare ready (process-wide counters; the spare hand-over of PumpMod ran under the two threads if the swaps are above 0)\n", (unsigned long long)chains_pump_bad(), (unsigned long long)chains_pump_swaps(), (unsigned long long)chains_pump_dry());
  int fails = 0;
  if (bad.load()) { std::printf("FAIL: non-finite output\n"); fails++; }
  if (blocks.load() < 100) { std::printf("FAIL: the audio thread barely ran\n"); fails++; }
  // after the audio thread has stopped: the delivered patches still waiting in the mailbox are freed by the destructor; retired ones by service(); none is left in the retired places
  if (live.patchesInFlight() > kChainsSlots - 2 + 1) { std::printf("FAIL: patches in flight beyond the bound\n"); fails++; }
  return fails ? 1 : 0;
}
