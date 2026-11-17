// U33: a combined-fields fuzz of the …pump synths' rendered path and of the per-block interface (core/src/engine/pumpsynth.{h,cpp}). Every one of the 13 synths; several parameters at once at extreme / NaN /
// infinite values (or every parameter at once); notes (Tune) from -200 to +300 and the non-finite ones; Hit (vel) and a kit's Drum out of range; Level and Drive extreme; sample rates 8000 to 96000 and the
// ones pump refuses; lengths (hold) from one sample to 10 s and out of range; FREEZE's File of any length and rate; and PumpSynth driven directly: events at wild frames, blocks of 1 to 4096 frames, set() while it
// runs. A per-render time limit (SIGALRM where there is one). Under ASan + UBSan (-DTHUNDER_SANITIZE=ON) a memory error, a signed overflow, a bad shift, a read past a table etc. aborts the run.
//   FUZZ_COMBINED=<n>   xeng_psyn renders (default 150 in ctest; 10000+ for the local sanitizer runs)    FUZZ_DIRECT=<n>   PumpSynth scenarios (default 60)    FUZZ_SEED=<n>    FUZZ_LIMIT=<s> per render (default 30)
// A render whose output is not finite (with a Level within 300 dB and a Drive within 1e100: past those the JS overflows too) is counted and listed (the pump synths end in a safety limiter: it should not happen);
// it is not an error of this test (see U33-NOTES.md), a crash or a timeout is.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <csignal>
#define FUZZ_ALARM 1
#endif

#include "engine/pumpsynth.h"

using namespace thunder;

struct Rng { uint64_t s; explicit Rng(uint64_t x) : s(x * 0x9E3779B97F4A7C15ull + 1) {} uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } double u() { return (double)(next() >> 11) / 9007199254740992.0; } int pick(int n) { return (int)(next() % (uint64_t)n); } };
static std::string gWhat;
#ifdef FUZZ_ALARM
static void onAlarm(int) { const char m[] = "pumpsynth_fuzz: A RENDER EXCEEDED THE TIME LIMIT: "; (void)!write(2, m, sizeof m - 1); (void)!write(2, gWhat.c_str(), gWhat.size()); (void)!write(2, "\n", 1); _exit(3); }
#endif
static const double kExt[] = {-1e308, -1e9, -1000, -1, -0.5, 0, 1e-300, 0.5, 1, 2, 100, 1e9, 1e308, NAN, INFINITY, -INFINITY, 1e-7, -1e-7, 4294967296.0, 2147483648.0};
static double ext(Rng& r) { return kExt[r.pick((int)(sizeof kExt / sizeof kExt[0]))]; }
static double valueFor(Rng& r, const PsynParamInfo* p) {
  switch (r.pick(5)) {
    case 0: return ext(r);
    case 1: return p->mn; case 2: return p->mx;
    case 3: return p->mn + r.u() * (p->mx - p->mn);
    default: return p->mx + (p->mx - p->mn) * (r.u() * 2 - 0.5);   // around and past the ends
  }
}
static double rateOf(Rng& r) {
  static const double fixed[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 1000, 1000.5, 768000, 768001, 0, -1, NAN, INFINITY, 1e9, 999.9, 7999};
  switch (r.pick(6)) { case 0: return fixed[r.pick((int)(sizeof fixed / sizeof fixed[0]))]; case 1: return 8000 + r.u() * 88000; default: { static const double c[] = {8000, 11025, 16000, 22050, 22050, 44100, 48000, 96000}; return c[r.pick(8)]; } }
}
static double holdOf(Rng& r, double sr) {
  switch (r.pick(10)) {
    case 0: return ext(r);
    case 1: return 1.0 / (sr == sr && sr > 0 ? sr : 48000);   // one sample
    case 2: return 10.0;
    case 3: return 12.5;
    case 4: return 0.001; case 5: return 0.02;
    default: return 0.02 + r.u() * 0.4;
  }
}
static double tuneOf(Rng& r) {
  switch (r.pick(8)) { case 0: return ext(r); case 1: return -200 + r.u() * 500; case 2: return std::floor(-200 + r.u() * 500); default: return -30 + r.u() * 60; }
}

int main() {
#ifndef THUNDER_HAVE_PUMP
  std::printf("pumpsynth_fuzz: not run on this platform: no pump library (it is not built here)\n");
  return 77;
#endif
  const char* e;
  const long nComb = (e = std::getenv("FUZZ_COMBINED")) ? std::atol(e) : 150, nDirect = (e = std::getenv("FUZZ_DIRECT")) ? std::atol(e) : 60;
  const uint64_t seed = (e = std::getenv("FUZZ_SEED")) ? (uint64_t)std::atoll(e) : 1;
  const int limit = (e = std::getenv("FUZZ_LIMIT")) ? std::atoi(e) : 30;
#ifdef FUZZ_ALARM
  std::signal(SIGALRM, onAlarm);
#endif
  Rng r(seed); long refused = 0, one = 0, nonFinite = 0, samples = 0; double slowest = 0; std::string slowWhat; std::vector<float> buf, file;
  std::string nf;
  PumpSynth::builtins();   // FREEZE's built-in spectra (about a second): outside the time limit
  for (long it = 0; it < nComb; it++) {
    PsynPad P; P.type = r.pick(kPsynTypes); if (r.pick(40) == 0) P.type = r.pick(2) ? -1 : kPsynTypes + r.pick(3);
    const int np = psyn_param_count(P.type);
    if (np) {   // several parameters at once (2 to 8), or all of them
      const bool allAtOnce = r.pick(12) == 0; const int k = allAtOnce ? np : 2 + r.pick(7);
      if (r.pick(2)) P.setDefaults();
      for (int j = 0; j < k; j++) { const int i = allAtOnce ? j : r.pick(np); P.v[i] = valueFor(r, psyn_param(P.type, i)); }
    }
    P.tune = tuneOf(r); P.vel = r.pick(4) == 0 ? ext(r) : (double)(r.pick(160) - 10); P.kd = r.pick(3) == 0 ? ext(r) : (double)(r.pick(14) - 2);
    P.level = r.pick(3) == 0 ? ext(r) : -80 + r.u() * 100; P.drive = r.pick(3) == 0 ? ext(r) : r.u() * 2;
    const double sr = rateOf(r), hold = holdOf(r, sr), vlen = r.pick(3) == 0 ? ext(r) : 0, bpm = r.pick(3) == 0 ? ext(r) : 20 + r.u() * 280;
    PsynFile F; const PsynFile* pf = nullptr;
    if (P.type == 11 && r.pick(2)) {   // FREEZE's File: any length, any rate, any content
      const size_t n = r.pick(4) == 0 ? (size_t)r.pick(3) : (size_t)(1 + r.pick(60000)); file.assign(n + 1, 0.f);
      for (size_t i = 0; i < n; i++) file[i] = r.pick(50) == 0 ? (float)ext(r) : (float)(std::sin(0.05 * (double)i) * 0.5 + (r.u() - 0.5) * 0.2);
      F.data = r.pick(8) == 0 ? nullptr : file.data(); F.len = (double)n; F.sr = r.pick(4) == 0 ? ext(r) : rateOf(r); pf = &F;
      if (P.v[psyn_param_index(11, "source")] == P.v[psyn_param_index(11, "source")]) {} P.v[psyn_param_index(11, "source")] = 0;
    }
    char w[400]; std::snprintf(w, sizeof w, "case %ld seed %llu: type %d sr %g hold %g vlen %g bpm %g tune %g vel %g kd %g level %g drive %g", it, (unsigned long long)seed, P.type, sr, hold, vlen, bpm, P.tune, P.vel, P.kd, P.level, P.drive);
    gWhat = w; if (std::getenv("FUZZ_VERBOSE")) { std::fprintf(stderr, "%s\n", w); if (std::getenv("FUZZ_VERBOSE")[0] == '2') for (int i = 0; i < np; i++) if (P.v[i] == P.v[i]) std::fprintf(stderr, "    %s = %.17g\n", psyn_param(P.type, i)->sym, P.v[i]); }
    const auto t0 = std::chrono::steady_clock::now();
#ifdef FUZZ_ALARM
    alarm((unsigned)limit);
#endif
    const size_t bound = xeng_psyn(P, sr, hold, vlen, bpm, pf, nullptr, 0);
    if (bound < 1 || bound > ((size_t)1 << 26)) { std::printf("pumpsynth_fuzz FAIL: bound %zu: %s\n", bound, w); return 1; }
    buf.assign(bound + 2, -999.f);
    const size_t len = xeng_psyn(P, sr, hold, vlen, bpm, pf, buf.data(), bound);
#ifdef FUZZ_ALARM
    alarm(0);
#endif
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); if (dt > slowest) { slowest = dt; slowWhat = w; }
    if (len < 1 || len > bound) { std::printf("pumpsynth_fuzz FAIL: length %zu (bound %zu): %s\n", len, bound, w); return 1; }
    if (buf[bound] != -999.f || buf[bound + 1] != -999.f) { std::printf("pumpsynth_fuzz FAIL: wrote past the bound: %s\n", w); return 1; }
    if (len == 1 && bound == 1) refused++; else if (len == 1) one++;
    bool fin = true; for (size_t i = 0; i < len; i++) if (!std::isfinite(buf[i])) { fin = false; break; }
    const bool expectNF = !(std::fabs(P.level) <= 300 && std::fabs(P.drive) <= 1e100);   // Level 1e9 dB or Drive 1e308 overflow in the JS too (Math.pow(10, 5e7) is Infinity)
    if (!fin && !expectNF) { nonFinite++; if (nf.size() < 1500) nf += std::string("  ") + w + "\n"; }
    samples += (long)len;
    // too small a buffer is refused, never written
    if (bound > 8 && it % 17 == 0) { std::vector<float> small(bound - 1, -5.f); if (xeng_psyn(P, sr, hold, vlen, bpm, pf, small.data(), small.size()) != 0) { std::printf("pumpsynth_fuzz FAIL: a too small buffer was accepted: %s\n", w); return 1; } }
  }
  // PumpSynth driven directly: blocks of 1..4096, events at any frame, values moved while it runs
  long events = 0, dnf = 0;
  for (long it = 0; it < nDirect; it++) {
    const int type = r.pick(kPsynTypes), np = psyn_param_count(type); const double sr = r.pick(3) ? 48000 : rateOf(r);
    std::vector<double> v((size_t)np, NAN); for (int j = 0, k = r.pick(10); j < k; j++) { const int i = r.pick(np); v[(size_t)i] = valueFor(r, psyn_param(type, i)); }
    char w[200]; std::snprintf(w, sizeof w, "direct %ld seed %llu: type %d sr %g", it, (unsigned long long)seed, type, sr); gWhat = w;
    PumpSynth s; if (!s.make(type, sr, v.data(), np)) { continue; }
#ifdef FUZZ_ALARM
    alarm((unsigned)limit);
#endif
    if (r.pick(3) == 0) s.tempo(ext(r));
    for (int b = 0, nb = 3 + r.pick(20); b < nb; b++) {
      const int ne = r.pick(6); for (int j = 0; j < ne; j++) { s.midi(r.pick(4) == 0 ? (int)ext(r) % 100000 : r.pick(5000) - 100, r.pick(3) == 0 ? 0x80 : r.pick(4) == 0 ? 0xE0 : r.pick(8) == 0 ? 0xB0 : 0x90, r.pick(140) - 5, r.pick(140) - 5); events++; }
      if (r.pick(4) == 0) s.set(r.pick(np + 4) - 2, valueFor(r, psyn_param(type, r.pick(np))));
      if (r.pick(60) == 0) for (int j = 0; j < 600; j++) s.midi(r.pick(4096), 0x90, r.pick(128), 100);   // more than the 512 a call keeps
      const int n = r.pick(5) == 0 ? 4096 : r.pick(5) == 0 ? 1 + r.pick(3) : 1 + r.pick(4096);
      s.process(n);
      for (int ch = 0; ch < 2; ch++) { const float* o = PumpSynth::out(ch); for (int i = 0; i < n; i++) if (!std::isfinite(o[i])) { dnf++; i = n; } }
    }
    (void)s.latency();
#ifdef FUZZ_ALARM
    alarm(0);
#endif
  }
  std::printf("pumpsynth_fuzz: %ld renders of xeng_psyn (%ld refused, %ld one-sample, %ld non-finite; %ld samples, slowest %.2f s: %s) and %ld PumpSynth scenarios (%ld events, %ld non-finite blocks): no crash, no timeout, nothing written past a bound  ok\n",
    nComb, refused, one, nonFinite, samples, slowest, slowWhat.c_str(), nDirect, events, dnf);
  if (nonFinite) std::printf("  renders with a non-finite output (first ones):\n%s", nf.c_str());
  return 0;
}
