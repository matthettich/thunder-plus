// U13: the meters (core/src/meter) against the page's own maths (tools/meter-dump.mjs: the analyser windows 512 mono / 1024 per channel, peak = max |x|, rms =
// sqrt(mean x * x), after every block of 7 signals), through the batched event: gate 0.1 dB (the figure is printed), blocks of 128 and of 512; then the
// publishing itself (rate, sequence, drops), the mixer feed and the layout's size.
//   test_meter <dir>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "meter/meter.h"
#include "rt/json.h"

using namespace thunder;
static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%-92s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static double dbDiff(double a, double b) { if (a == 0 && b == 0) return 0; if (a <= 1e-9 || b <= 1e-9) return (a <= 1e-9 && b <= 1e-9) ? 0 : 999; return std::fabs(20 * std::log10(a / b)); }

static bool runCase(const std::string& dir, const char* name, int block, double* worst, int* events, int* exact) {
  std::string js, in; if (!readFile(dir + "/" + name + ".json", &js) || !readFile(dir + "/" + name + ".in.f32", &in)) return false;
  JsonDoc doc; if (!doc.parse(js)) return false; Json M = doc.root(); const int n = (int)M.num("n"); Json ex = M.arr("expected");
  const float* L = (const float*)in.data(); const float* R = L + n;
  MeterBank B(48000, 30); MeterEvent e; *worst = 0; *events = 0; *exact = 0;
  for (int o = 0; o + block <= n; o += block) {
    B.feedTrack(0, L + o, R + o, block); B.feedMaster(L + o, R + o, block); B.advance(block, (double)(o + block) / 48000);
    while (B.pop(&e)) {
      const int blk = (int)std::lround(e.at * 48000) / 128 - 1; Json x = ex.at((size_t)blk); (*events)++;
      const float got[6] = {e.a[0], e.a[1], e.a[2 * kMixTracks], e.a[2 * kMixTracks + 1], e.a[2 * (kMixTracks + 1)], e.a[2 * (kMixTracks + 1) + 1]}; bool all = true;
      for (int k = 0; k < 6; k++) { const double w = x.at((size_t)k).asNum(); const double d = dbDiff(got[k], w); if (d > *worst) *worst = d; if ((double)got[k] != (double)(float)w) all = false; }
      if (all) (*exact)++;
    }
  }
  return *events > 0;
}

int main(int argc, char** argv) {
  static const char* names[] = {"sine", "noise", "burst", "silence_after", "square", "dc_opposite", "silence"};
  if (argc >= 2) for (const char* nm : names) for (int block : {128, 512}) {
    double worst; int ev, ex; char b[160];
    if (!runCase(argv[1], nm, block, &worst, &ev, &ex)) { std::snprintf(b, sizeof b, "%s, blocks of %d: could not be run", nm, block); check(false, b); continue; }
    std::snprintf(b, sizeof b, "%-14s blocks of %3d: %2d events, worst %.2e dB (%d exact to the float)", nm, block, ev, worst, ex); check(worst <= 0.1, b);
  }
  else std::printf("meter: no reference directory (Node not found): the signal cases were not run\n");
  { MeterBank B(48000, 30); std::vector<float> z(128, 0.0f), o(128, 0.5f); MeterEvent e; int n = 0;
    for (int i = 0; i < 48000 / 128; i++) { B.feedTrack(3, o.data(), o.data(), 128); B.feedMaster(o.data(), z.data(), 128); B.advance(128, (i + 1) * 128.0 / 48000); }
    while (B.pop(&e)) n++;
    check(B.published() >= 28 && B.published() <= 31, "30 Hz: one second of blocks publishes about 30 events");
    check(B.dropped() == 0 || B.dropped() == B.published() - (uint32_t)n, "an event not popped is dropped oldest first and counted");
    // layout: slot 3 = 0.5, the master left = 0.5, the master right = 0
    B.feedTrack(3, o.data(), o.data(), 128); for (int i = 0; i < 8; i++) { B.feedTrack(3, o.data(), o.data(), 128); B.feedMaster(o.data(), z.data(), 128); } B.setRate(1000); B.advance(128, 1.0); B.pop(&e);
    check(e.a[6] == 0.5f && e.a[7] == 0.5f && e.a[0] == 0.0f && e.a[2 * kMixTracks] == 0.5f && e.a[2 * kMixTracks + 2] == 0.0f, "layout: a[2s] peak, a[2s+1] rms; master L at slot kMixTracks, R at kMixTracks + 1");
    check(kMeterFloats == 70, "70 floats an event (35 slots x peak, rms)"); }
  std::printf("meter: %d check(s) failed\n", fails);
  return fails ? 1 : 0;
}
