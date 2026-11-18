// U33 follow-up: the NOT-BUILT configuration of the pump synths (THUNDER_WITH_PUMP=OFF, and MSVC, where pump is not built). core/src/engine/pumpsynth.cpp then has no pump library behind it and every instance must be
// REFUSED cleanly: make() is false for all 13 instruments, an instrument reports latency 0, the instance's calls are harmless, and the rendered path gives what the page's render gives for a refused instance:
// Float32Array(1), one zero sample, never a crash and never the 0 that means "buffer too small". (test_pumpsynth and test_pumpsynth_fuzz print "not run" and return 77 there: they need the library.)
// Built in every configuration; where pump IS built it prints "not run" and returns 77 (ctest SKIP); the CI step "Closed build" (THUNDER_WITH_PUMP=OFF) runs it for real.
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/pumpsynth.h"

using namespace thunder;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); fails++; } } while (0)

int main() {
#ifdef THUNDER_HAVE_PUMP
  std::printf("pumpsynth_refused: not run: the pump library is built in this configuration (the closed build THUNDER_WITH_PUMP=OFF runs it)\n");
  return 77;
#else
  PumpSynth::builtins();   // harmless without the library
  const double rates[3] = {48000.0, 8000.0, 96000.0};
  int made = 0, refused = 0, renders = 0;
  for (int t = 0; t < kPsynTypes; t++) {
    CHECK(psyn_kind(t) && psyn_param_count(t) > 0, "type %d: the table is still there (the page's names and ranges do not need the library)", t);
    for (double sr : rates) {
      PumpSynth s;
      if (s.make(t, sr, nullptr, 0)) { made++; CHECK(false, "type %d at %g Hz: make() is true without the library", t, sr); }
      else refused++;
      CHECK(!s.ok(), "type %d: ok() after a refused make()", t);
      CHECK(s.latency() == 0, "type %d: a refused instance reports latency %d", t, s.latency());
      s.tempo(120.0); s.set(0, 1.0); s.midi(0, 0x90, 60, 100); s.midi(5, 0x80, 60, 0); s.process(256); s.process(4096); s.file(nullptr, 0, 48000.0);
      const float* L = PumpSynth::out(0); const float* R = PumpSynth::out(1);
      CHECK(L && R, "type %d: out() is null", t);
      s.destroy();
      CHECK(xeng_psyn_latency(t, sr) == 0, "type %d at %g Hz: xeng_psyn_latency is not 0", t, sr);
      PsynPad P; P.type = t; P.setDefaults();
      const size_t need = xeng_psyn(P, sr, 0.5, 0, 0, nullptr, nullptr, 0);   // the size query
      CHECK(need >= 1, "type %d: the size query gave 0", t);
      std::vector<float> out(need ? need : 1, -777.f);
      const size_t len = xeng_psyn(P, sr, 0.5, 0, 0, nullptr, out.data(), out.size());   // the render
      CHECK(len == 1 && out[0] == 0.f, "type %d at %g Hz: the render of a refused instance is %zu samples, first %g (want one zero sample)", t, sr, len, (double)out[0]);
      float one[1] = {-777.f};   // a caller's buffer of one float is enough for the refused sound only if the work area fits: a too small buffer is 0, never a write past it
      const size_t tiny = xeng_psyn(P, sr, 0.5, 0, 0, nullptr, one, 1);
      CHECK(tiny == 0 || (tiny == 1 && one[0] == 0.f), "type %d: a one-float buffer gave %zu", t, tiny);
      renders++;
    }
  }
  { PsynPad P; P.type = 99; const size_t len = xeng_psyn(P, 48000.0, 0.5, 0, 0, nullptr, nullptr, 0); CHECK(len == 1, "an unknown type gave a size of %zu", len); }
  std::printf("pumpsynth_refused: %d instruments x %d rates refused (%d made, %d refused), %d renders each one zero sample, latency 0, every call harmless  %s\n", kPsynTypes, 3, made, refused, renders, fails ? "FAIL" : "ok");
  return fails ? 1 : 0;
#endif
}
