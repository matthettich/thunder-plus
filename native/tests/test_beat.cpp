// U10: the beat clock (core/src/mix/beat.h, fed through Mixer::event) against the core processor's own clock output (tools/beat-dump.mjs runs
// core/seq-core.js in a Node vm: the sequencer's 'beat' events booked LEAD ahead, tempo changes, stops, a restart, a wrap past 16, a loop wrap,
// a start in mid-block, late events, tiny blocks). The float32 lane (beat + 1 playing, 0 stopped) must be the JS's bit for bit. Then the
// readers: an effects node's clock through its own delay (pdcPlan's c).
//   test_beat <dir>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mix/mix.h"
#include "mix/pdc.h"
#include "rt/json.h"
#include "golden/beat_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%-86s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }

static bool runCase(const std::string& dir, const BeatGolden& G) {
  std::string js, ref; if (!readFile(dir + "/" + G.name + ".json", &js) || !readFile(dir + "/" + G.name + ".f32", &ref)) { std::printf("%-14s no reference\n", G.name); return false; }
  JsonDoc doc; if (!doc.parse(js)) return false; Json M = doc.root();
  const int N = (int)M.num("N"), blocks = (int)M.num("blocks"); Json ev = M.arr("events");
  const float* R = (const float*)ref.data(); const bool refHash = fnv(R, ref.size() / 4) == G.hash;
  Mixer mix(48000, N, 0.01); std::vector<float> oL((size_t)N), oR((size_t)N);
  size_t k = 0; int64_t bad = 0, firstBad = -1; int peakOn = 0; bool bus = true;
  for (int b = 0; b < blocks; b++) {
    const double t0 = (double)((int64_t)b * N) / 48000; mix.setTime(t0);
    while (k < ev.size() && (int)ev.at(k).num("b") == b) { Json a = ev.at(k++).get("a");
      if (a.str("op") == "stop") mix.clockStop(a.get("at").isNum() ? a.num("at") : t0);
      else { SeqEvent e; e.type = SEV_BEAT; e.t = a.num("t"); e.v = a.num("b"); mix.event(e); } }
    mix.render(oL.data(), oR.data(), N, t0);
    const float* c = mix.beatLane(); const float* bs = mix.beatBus();
    for (int i = 0; i < N; i++) { if (std::memcmp(&c[i], &R[(size_t)b * N + i], 4) != 0) { bad++; if (firstBad < 0) firstBad = (int64_t)b * N + i; } if (bs[i] != c[i] + -1.0f) bus = false; }
    if (mix.clock().peak() > peakOn) peakOn = mix.clock().peak();
  }
  const bool ok = bad == 0 && bus && mix.clock().dropped() == 0;
  std::printf("%-14s %7d blocks of %3d: %s  (reference hash %s, longest clock lane %d of %d, dropped %u)%s\n", G.name, blocks, N, bad == 0 ? "bit-exact" : "DIFFERS", refHash ? "as committed" : "NOT the committed one", peakOn, BeatClock::kAuto, mix.clock().dropped(), ok ? "" : "  FAIL");
  if (bad) std::printf("   %lld samples differ, the first at %lld\n", (long long)bad, (long long)firstBad);
  return ok && refHash;
}

// the readers
static std::vector<float> seen[2];
struct Probe { Mixer* m; int track; int slot; };
static void probeHook(void* u, int, float* L, float*, int N, double, const MixFeed*, int) { Probe* p = (Probe*)u; (void)L; const float* b = p->m->beat(p->track); seen[p->slot].insert(seen[p->slot].end(), b, b + N); }

int main(int argc, char** argv) {
  int bad = 0;
  if (argc >= 2) { for (int i = 0; i < kBeatGoldenN; i++) if (!runCase(argv[1], kBeatGolden[i])) bad++; }
  else std::printf("beat: no reference directory (Node not found): the cases were not run\n");
  if (bad) { std::printf("beat: %d case(s) failed\n", bad); fails += bad; }
  // ---- the readers: clkBus through each effects node's delay ----
  {
    const int N = 128, F = 128 * 60; Mixer M(48000, N);
    Probe p0{&M, 3, 0}, p1{&M, kSongNT, 1}; M.setFx(3, probeHook, &p0); M.setMasterFx(probeHook, &p1);
    PdcPlan plan; plan.on = true; plan.c[3] = 2048.0 / 48000; plan.c[kSongNT] = 0;
    std::vector<float> oL((size_t)N), oR((size_t)N), lane;
    M.setTime(0); PdcApply a; pdc_apply(M, plan, a);
    for (int b = 0; b * N < F; b++) { const double t0 = (double)b * N / 48000; M.setTime(t0);
      if (b == 0) { SeqEvent e; e.type = SEV_BEAT; e.t = 0; e.v = 0.5; M.event(e); }
      if (b % 2 == 0) { SeqEvent e; e.type = SEV_BEAT; e.t = t0 + 0.001 * b; e.v = 0.5 + b * 0.01; M.event(e); }
      M.render(oL.data(), oR.data(), N, t0); lane.insert(lane.end(), M.beatBus(), M.beatBus() + N); }
    bool d = seen[0].size() == lane.size(), z = true, m = seen[1].size() == lane.size();
    for (int i = 0; d && i < (int)lane.size(); i++) { const float want = i < 2048 ? 0.0f : lane[(size_t)i - 2048]; if (seen[0][(size_t)i] != want) d = false; if (i < 2048 && seen[0][(size_t)i] != 0) z = false; if (seen[1][(size_t)i] != lane[(size_t)i]) m = false; }
    check(d, "track 3's effects node hears the clock 2048 samples late (c = 2048 / sr), bit for bit");
    check(z, "  and a new delay starts from 0 (silence), not -1");
    check(m && M.clockDelaySec(kSongNT) == 0 && std::fabs(M.clockDelaySec(3) - 2048.0 / 48000) < 1e-15, "the master's node (c = 0) hears the clock as it is; clockDelaySec reports n._cd._pv");
  }
  std::printf("beat: %d failed\n", fails);
  return fails ? 1 : 0;
}
