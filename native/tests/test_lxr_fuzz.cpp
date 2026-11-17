// U04: combined-fields fuzz of the LXR module (run under ASan + UBSan with -DTHUNDER_SANITIZE=ON, which also turns on float-cast-overflow for the firmware's C).
//   test_lxr_fuzz [renders = 150] [seed = 1]
// Every render draws SEVERAL fields at once from {extreme, NaN, +-inf, out-of-range, ordinary}: the kind, the knobs p<n>, vdst, ldst, vel, open, tune, drive, level, vlen, hold, bpm and the
// sample rate (8000..96000 mostly; also 0, negative, NaN, 1e30: those must be REFUSED, not crash). A render has a time limit; a returned render must be finite and no longer than the bound.
// A third of the iterations also run the live path: a kit of random bytes (any length), random trigger lists (offsets out of order and far past the block, voices -3..9, velocities and notes at
// the extremes, up to 300 events), random block sizes (1..5000 frames, not a multiple of the firmware's 64).
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "thunder/lxr_module.h"
using namespace thunder;
static uint64_t s = 1;
static uint32_t rnd() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 11); }
static double uni() { return rnd() / 4294967296.0; }
static double pick(const double* v, int n) { return v[rnd() % (unsigned)n]; }
static double field(double lo, double hi) {   // a mix of extremes and ordinary values
  static const double ext[] = {0, 1, -1, 0.5, 127, 128, 254, 255, 256, 1e9, -1e9, 1e30, -1e30, 4294967295.0, -4294967296.0, 2147483648.0, 1e-30, NAN, INFINITY, -INFINITY};
  const unsigned r = rnd() % 10;
  if (r < 3) return pick(ext, (int)(sizeof ext / sizeof ext[0]));
  if (r < 5) return lo;
  if (r < 6) return hi;
  return lo + (hi - lo) * uni();
}
int main(int argc, char** argv) {
  const int N = argc > 1 ? std::atoi(argv[1]) : 150; s = argc > 2 ? (uint64_t)std::atoll(argv[2]) * 0x9E3779B97F4A7C15ull | 1 : 0x9E3779B97F4A7C15ull;
  static const double rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 44140, 48000, 88200, 96000};
  static const double badRates[] = {0, -1, NAN, INFINITY, 1e30, 1e7, 5, 1e-9};
  int ok = 0, refused = 0, tooSmall = 0, bad = 0, slow = 0, live = 0; double worstMs = 0; std::vector<float> out, lbuf;
  for (int it = 0; it < N; it++) {
    ThunderLxrRequest R; std::memset(&R, 0, sizeof R);
    R.kind = (int)(rnd() % 6) - 1;   // -1 and 4 are out of range
    const int nk = (R.kind >= 0 && R.kind < 4) ? lxr::KnobCount(R.kind) : 0; (void)nk;
    ThunderLxrDescribe d; std::memset(&d, 0, sizeof d); const bool have = lxr::Describe(R.kind, &d);
    for (int i = 0; i < THUNDER_LXR_MAX_PARAMS; i++) if (have && d.present[i]) R.p[i] = d.def[i];
    const int nf = 1 + (int)(rnd() % 12);   // several fields at once
    for (int f = 0; f < nf; f++) {
      switch (rnd() % 12) {
        case 0: case 1: case 2: { int i = (int)(rnd() % THUNDER_LXR_MAX_PARAMS); R.p[i] = field(0, 255); break; }
        case 3: R.vdst = field(0, 12); break; case 4: R.ldst = field(0, 40); break; case 5: R.vel = field(1, 127); break; case 6: R.open = field(0, 1); break;
        case 7: R.tune = field(-60, 60); break; case 8: R.drive = field(0, 1); R.level = field(-24, 12); break; case 9: R.vlen = field(0, 12); break;
        case 10: R.hold = field(0, 12); break; default: R.bpm = field(20, 300); break;
      }
    }
    R.sr = (rnd() % 12 == 0) ? pick(badRates, (int)(sizeof badRates / sizeof badRates[0])) : (rnd() % 3 ? pick(rates, (int)(sizeof rates / sizeof rates[0])) : 8000 + std::floor(uni() * 88000));
    if (R.kind < 0 || R.kind > 3) R.kind = (int)(rnd() % 4) + ((rnd() % 5 == 0) ? 7 : 0);
    const auto t0 = std::chrono::steady_clock::now();
    const size_t bound = lxr::RenderBound(R);
    int st = THUNDER_LXR_REFUSED; size_t n = 0;
    if (bound) { out.assign(bound + 1, -777.f); st = lxr::RenderPad(R, out.data(), bound + 1, &n); }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); if (ms > worstMs) worstMs = ms;
    if (ms > 30000) { slow++; std::printf("it %d: %.0f ms (limit 30000) kind %d sr %g\n", it, ms, R.kind, R.sr); }
    if (st == THUNDER_LXR_OK) {
      ok++; bool fin = n <= bound; for (size_t i = 0; i < n && fin; i++) if (!std::isfinite(out[i])) fin = false;
      if (!fin) { bad++; if (std::getenv("LXR_FUZZ_V")) { for (int i = 0; i < THUNDER_LXR_MAX_PARAMS; i++) if (R.p[i] != 0) std::printf(" p%d=%g", i, R.p[i]); std::printf("\n vdst %g ldst %g vel %g open %g tune %g drive %g level %g vlen %g hold %g bpm %g\n", R.vdst, R.ldst, R.vel, R.open, R.tune, R.drive, R.level, R.vlen, R.hold, R.bpm); for (size_t i = 0; i < n; i++) if (!std::isfinite(out[i])) { std::printf(" first non-finite at %zu\n", i); break; } } std::printf("it %d: a returned render is not finite or longer than its bound (n %zu bound %zu) kind %d sr %g\n", it, n, bound, R.kind, R.sr); }
    } else if (st == THUNDER_LXR_TOO_SMALL) tooSmall++; else { refused++; if (bound && !(R.sr > 0 && R.sr <= 1e7)) bad++; }
    if (!bound && R.sr >= 8000 && R.sr <= 96000 && R.kind >= 0 && R.kind < 4) { bad++; std::printf("it %d: a render with a legal kind and rate was refused (sr %g kind %d)\n", it, R.sr, R.kind); }
    if (it % 3 == 0) {   // live path
      uint8_t kit[600]; const size_t nkit = (rnd() % 4 == 0) ? (rnd() % 600) : (size_t)lxr::NumParams(); for (size_t i = 0; i < sizeof kit; i++) kit[i] = (uint8_t)(rnd() % 5 == 0 ? 255 : rnd());
      lxr::LiveOpen(kit, nkit, field(1, 400));
      const int blocks = 1 + (int)(rnd() % 3);
      for (int b = 0; b < blocks; b++) {
        const size_t frames = 1 + rnd() % 5000; const size_t nev = rnd() % 300; std::vector<ThunderLxrEvent> ev(nev);
        for (auto& e : ev) { e.offset = (rnd() % 5 == 0) ? (uint32_t)rnd() : (uint32_t)(rnd() % (frames + 100)); e.voice = (int)(rnd() % 13) - 3; e.vel = (rnd() % 4 == 0) ? (int32_t)rnd() : (int)(rnd() % 160) - 10; e.note = (rnd() % 4 == 0) ? (int32_t)rnd() : (int)(rnd() % 160) - 10; }
        const int voice = (int)(rnd() % 9) - 2; lbuf.assign(frames + 1, -777.f);
        const int r = lxr::LiveProcess(voice, ev.data(), nev, lbuf.data(), frames); live++;
        if (r == THUNDER_LXR_OK) { for (size_t i = 0; i < frames; i++) if (!std::isfinite(lbuf[i])) { bad++; std::printf("it %d: live output not finite\n", it); break; } if (lbuf[frames] != -777.f) { bad++; std::printf("it %d: live wrote past frames\n", it); } }
      }
    }
  }
  std::printf("lxr_fuzz: %d renders: %d ok, %d refused, %d too small, %d live blocks; worst %.0f ms; %d slow, %d bad  %s\n", N, ok, refused, tooSmall, live, worstMs, slow, bad, (slow || bad) ? "FAIL" : "ok");
  return (slow || bad) ? 1 : 0;
}
