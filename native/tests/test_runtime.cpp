// U05: the core runtime's own tests (ctest `runtime`).
//   1. js_exp, the seeded random stream and Auto against hashes made by the JS (native/tools/runtime-golden.mjs): bit for bit.
//   2. Auto against the closure version it replaced (the C++ of tests/core-auto.test.js's RefAuto), every event kind, 3000
//      random timelines, at() and prune(): bit for bit.
//   3. A full timeline refuses an event and counts it; nothing is written past its storage.
//   4. The callback contract: a command with "at" lands on exactly that sample; same-frame commands keep their order; a block
//      longer than max_block gives the same samples as short blocks; song snapshots swap and are acknowledged.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

#include "rt/auto.h"
#include "rt/jsmath.h"
#include "rt/rng.h"
#include "thunder/core.h"

#include "golden/runtime_golden.inc"

using namespace thunder;

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); fails++; } } while (0)

struct Fnv { uint64_t h = 0xcbf29ce484222325ull; void add(double x) { unsigned char b[8]; std::memcpy(b, &x, 8); for (int i = 0; i < 8; i++) { h ^= b[i]; h *= 0x100000001b3ull; } } };
static bool same(double a, double b) { return std::memcmp(&a, &b, 8) == 0 || (a != a && b != b); }   // Object.is

struct Lcg { int64_t seed; double operator()() { seed = (seed * 16807) % 2147483647; return (double)seed / 2147483647.0; } };

static double f_sq(const double*, double x) { return x * x; }
static double f_exp(const double*, double x) { return 1 - js_exp(-x * 3); }
static double f_lin(const double*, double x) { return x * 0.5 + 0.25; }
static double f_sin(const double*, double x) { return std::sin(x * 7); }
static Curve mk(double (*fn)(const double*, double)) { Curve c; c.fn = fn; return c; }

// ---- 1. against the JS ----------------------------------------------------------------------------------------------
static void golden() {
  { Fnv H; Lcg r{777}; for (int i = 0; i < 20000; i++) { const double x = (r() * 2 - 1) * (i % 4 == 0 ? 700 : i % 4 == 1 ? 30 : i % 4 == 2 ? 1.2 : 1e-3); H.add(js_exp(x)); }
    CHECK(H.h == kExpGolden, "js_exp gives V8's Math.exp, bit for bit, over 20000 values"); }
  // U07: js_pow, js_round, the 32-bit conversions, js_imul and js_number_to_string (the same programs as runtime-golden.mjs)
  { Fnv H; Lcg r{4242}; const double INF = HUGE_VAL, NaN = std::numeric_limits<double>::quiet_NaN();
    for (int i = 0; i < 20000; i++) { const double a = r(), b = r(); const int m = i % 5;
      H.add(m == 0 ? js_pow(2, (std::floor(a * 241) - 120 + (b < 0.3 ? std::floor(b * 100) / 100 : 0)) / 12) : m == 1 ? js_pow(a * 1.2, 2) : m == 2 ? js_pow(a * 100, b * 8 - 4)
        : m == 3 ? js_pow(2, (b * 2400 - 1200) / 1200) : js_pow(10, (a * 2 - 1) * 300 * b)); }
    const double xy[][2] = {{0, 0}, {-0.0, 3}, {-0.0, -3}, {-8, 1.0 / 3}, {-8, 3}, {-8, 2}, {1, INF}, {-1, -INF}, {2, INF}, {0.5, INF}, {2, -INF}, {NaN, 0}, {1, NaN}, {INF, -1}, {-INF, 3}, {4, 0.5}, {-4, 0.5},
      {5e-324, 0.5}, {2, -1074}, {2, -1080}, {2, 1023.5}, {2, 1024}, {10, 308}, {10, -320}, {1.0000001, 1e10}, {0.9999999, 1e20}, {3, -1}, {7, 1}};
    for (const auto& q : xy) { const double v = js_pow(q[0], q[1]); H.add(v != v ? std::numeric_limits<double>::quiet_NaN() : v); }
    CHECK(H.h == kPowGolden, "js_pow gives V8's Math.pow, bit for bit, over 20000 values and the special cases"); }
  { Fnv H; Lcg r{99}; const double INF = HUGE_VAL, NaN = std::numeric_limits<double>::quiet_NaN();
    const double xs[] = {0, -0.0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999999999999994, -0.49999999999999994, 4503599627370497.0, -4503599627370497.0, 9007199254740991.0, NaN, INF, -INF, 1e300, -1e300, 2147483647.5, 4294967295.7, 4294967296.0, -4294967297.2, 6e18, -6e18};
    for (double x : xs) { H.add(js_round(x)); H.add((double)js_to_int32(x)); H.add((double)js_to_uint32(x)); }
    for (int i = 0; i < 20000; i++) { const double a = r(), b = r(), x = (a * 2 - 1) * (i % 3 == 0 ? 1000 : i % 3 == 1 ? 5e9 : 3e19), h = std::floor(b * 2000) / 2 - 500;
      H.add(js_round(x)); H.add(js_round(h)); H.add((double)js_to_int32(x)); H.add((double)js_to_uint32(x)); H.add((double)(js_to_int32(x) >> 3)); H.add((double)(int32_t)js_imul(js_to_uint32(x), js_to_uint32(b * 4294967296.0))); }
    CHECK(H.h == kRoundGolden, "js_round, js_to_int32 / js_to_uint32 and js_imul equal the JS's, over 20000 values and the edge cases"); }
  { uint64_t h = 0xcbf29ce484222325ull; Lcg r{31337}; const double INF = HUGE_VAL, NaN = std::numeric_limits<double>::quiet_NaN();
    const auto addS = [&h](double x) { char b[32]; const int n = js_number_to_string(x, b); for (int i = 0; i < n; i++) { h ^= (unsigned char)b[i]; h *= 0x100000001b3ull; } h ^= 10; h *= 0x100000001b3ull; };
    const double xs[] = {0, -0.0, 1, -1, 0.1, 1e21, 1e-7, 0.000001, 123456789012345680000.0, 1.7976931348623157e308, 5e-324, 0.5, 1.0 / 3, 2.0 / 3, 100, 1e5, 123.456, NaN, INF, -INF, 0.009542, 1.145038};
    for (double x : xs) addS(x);
    for (int i = 0; i < 6000; i++) { const double a = r(), b = r(); const int m = i % 6;
      addS(m == 0 ? js_round(a * 4e8) / 1e6 : m == 1 ? js_round((a - 0.5) * 2e3) / 1e6 : m == 2 ? std::floor(a * 1e6) : m == 3 ? a * js_pow(10, std::floor(b * 60) - 30) : m == 4 ? -a * b : js_round(a * 1e9) * js_pow(10, std::floor(b * 30))); }
    CHECK(h == kNumStrGolden, "js_number_to_string equals String(x) in V8 over 6000 values and the edge cases"); }
  { const uint32_t nn[4] = {1, 2, 0x7f, 0xff};
    for (int k = 0; k < 4; k++) { Mulberry32 g(sed_seed(nn[k])); Fnv H; for (int i = 0; i < 1000; i++) H.add(g.next()); CHECK(H.h == kRngGolden[k], "the seeded random stream equals the JS's"); } }
  const Curve fns[3] = {mk(f_sq), mk(f_exp), mk(f_lin)};
  const double NONE = 1e300; Lcg rnd{12345}; int bad = 0, first = -1; uint32_t dropped = 0;
  for (int r = 0; r < kAutoGoldenN; r++) {
    Fnv H;
    const double v0 = rnd() * 2 - 1; AutoN<64> A(v0); const int n = (int)std::floor(rnd() * 10); double tt = 0;
    for (int k = 0; k < n; k++) {
      const double z = rnd(); if (!(z < 0.2)) tt += rnd() * 0.2;
      const int kind = (int)std::floor(rnd() * 4); const double v = rnd() * 2 - 1, tc = rnd() * 0.05, d = rnd() * 0.1;
      if (kind == 0) A.set(v, tt); else if (kind == 1) A.lin(v, tt); else if (kind == 2) A.target(v, tt, tc); else A.curve(fns[k % 3], tt, d);
    }
    for (int q = 0; q < 40; q++) { const double t = rnd() * (tt + 0.3); H.add(A.at(t)); }
    { const double c = rnd() * (tt + 0.3); double a = 0, b = 0; const bool ha = A.constBlk(c, c + 0.01, &a), hb = A.constFrom(c, &b); H.add(ha ? a : NONE); H.add(hb ? b : NONE); }
    { const double t0 = rnd() * (tt + 0.3); double C[8]; A.fill(C, t0, 1.0 / 48000, 8); for (int i = 0; i < 8; i++) H.add(C[i]); }
    for (int q = 0; q < 3; q++) {
      const double t = rnd() * (tt + 0.2); A.prune(t); H.add(A.v0); H.add((double)A.size());
      for (int s = 0; s < 10; s++) { const double u = t + rnd() * 0.3; H.add(A.at(u)); }
    }
    { const double t = rnd() * (tt + 0.3); A.hold(t); H.add((double)A.size()); for (int s = 0; s < 5; s++) { const double u = rnd() * (tt + 0.4); H.add(A.at(u)); } }
    { const double t = rnd() * (tt + 0.3); A.cancel(t); H.add((double)A.size()); H.add(A.at(t + 0.05)); }
    dropped += A.dropped;
    if (H.h != kAutoGolden[r]) { bad++; if (first < 0) first = r; }
  }
  if (bad) std::printf("  %d of %d timelines differ from the JS (first: %d)\n", bad, kAutoGoldenN, first);
  CHECK(bad == 0, "Auto (set, lin, target, curve, at, fill, constBlk, constFrom, prune, hold, cancel) equals the JS on 600 timelines, bit for bit");
  CHECK(dropped == 0, "no event was dropped");
}

// ---- 2. against the closure version (tests/core-auto.test.js RefAuto) -------------------------------------------------
struct Ref {
  double v0; std::vector<AutoEv> ev;
  void ins(const AutoEv& e) { size_t i = ev.size(); while (i > 0 && ev[i - 1].t > e.t) i--; ev.insert(ev.begin() + (long)i, e); }
  struct St { bool on = false; double from = 0, t = 0, to = 0, tc = 1; };
  double at(double t) const {
    double v = v0, pt = 0; St st;
    auto val = [&](double x) { return st.on ? st.to + (st.from - st.to) * js_exp(-(x - st.t) / st.tc) : v; };
    for (const AutoEv& e : ev) {
      if (e.k == 1) { if (t < e.t) { const double a = val(pt); return e.t > pt ? a + (e.v - a) * (t - pt) / (e.t - pt) : e.v; } v = e.v; st.on = false; pt = e.t; continue; }
      if (t < e.t) return val(t);
      if (e.k == 0) { v = e.v; st.on = false; pt = e.t; }
      else if (e.k == 2) { const double from = val(e.t); st.on = true; st.from = from; st.t = e.t; st.to = e.v; st.tc = e.tc; pt = e.t; }
      else { if (t < e.t + e.d) return e.c(t); v = e.c(e.t + e.d); st.on = false; pt = e.t + e.d; }
    }
    return val(t);
  }
  void prune(double t) {
    if (ev.empty() || ev[0].t > t) return;
    double v = v0, pt = 0; St st; int cur = -1; size_t i = 0;
    auto val = [&](double x) { return st.on ? st.to + (st.from - st.to) * js_exp(-(x - st.t) / st.tc) : v; };
    for (; i < ev.size(); i++) { const AutoEv& e = ev[i];
      if (e.k == 1) { if (e.t > t) break; v = e.v; st.on = false; pt = e.t; continue; }
      if (e.t > t) break;
      if (e.k == 0) { v = e.v; st.on = false; cur = -1; pt = e.t; }
      else if (e.k == 2) { const double from = val(e.t); st.on = true; st.from = from; st.t = e.t; st.to = e.v; st.tc = e.tc; cur = -1; }
      else { if (e.t + e.d > t) cur = (int)i; else { v = e.c(e.t + e.d); st.on = false; cur = -1; pt = e.t + e.d; } } }
    if (i == 0) return;
    std::vector<AutoEv> head;
    if (!st.on && cur < 0 && i < ev.size() && ev[i].k == 1) { AutoEv h; h.k = 0; h.t = pt; h.v = v; head.push_back(h); head.insert(head.end(), ev.begin() + (long)i, ev.end()); v0 = v; ev = head; return; }
    const double now = cur >= 0 ? ev[(size_t)cur].c(t) : val(t);
    { AutoEv h; h.k = 0; h.t = t; h.v = now; head.push_back(h); }
    if (st.on) { AutoEv h; h.k = 2; h.t = t; h.v = st.to; h.tc = st.tc; head.push_back(h); }
    if (cur >= 0) head.push_back(ev[(size_t)cur]);
    head.insert(head.end(), ev.begin() + (long)i, ev.end());
    v0 = now; ev = head;
  }
};

static void reference() {
  const Curve fns[3] = {mk(f_sin), mk(f_sq), mk(f_exp)};
  Lcg rnd{12345}; long N = 0, bad = 0, pruneBad = 0; uint32_t dropped = 0;
  for (int r = 0; r < 3000; r++) {
    const double v0 = rnd() * 2 - 1; AutoN<64> A(v0); Ref B; B.v0 = v0; const int n = (int)std::floor(rnd() * 10); double tt = 0;
    for (int k = 0; k < n; k++) {
      const double z = rnd(); if (!(z < 0.2)) tt += rnd() * 0.2;
      const int kind = (int)std::floor(rnd() * 4); const double v = rnd() * 2 - 1, tc = rnd() * 0.05, d = rnd() * 0.1;
      AutoEv e; e.k = kind; e.t = tt; e.v = v; e.tc = tc > 1e-5 ? tc : 1e-5; e.d = d; e.c = fns[k % 3];
      if (kind == 0) { A.set(v, tt); e.tc = e.d = 0; } else if (kind == 1) { A.lin(v, tt); e.tc = e.d = 0; } else if (kind == 2) { A.target(v, tt, tc); e.d = 0; } else { A.curve(fns[k % 3], tt, d); e.v = 0; e.tc = 0; }
      B.ins(e);
    }
    for (int q = 0; q < 100; q++) { const double t = rnd() * (tt + 0.3); N++; if (!same(A.at(t), B.at(t))) bad++; }
    for (int q = 0; q < 4; q++) {
      const double t = rnd() * (tt + 0.2); A.prune(t); B.prune(t);
      if (!same(A.v0, B.v0) || A.size() != (int)B.ev.size()) pruneBad++;
      for (int s = 0; s < 25; s++) { const double u = t + rnd() * 0.3; N++; if (!same(A.at(u), B.at(u))) bad++; }
    }
    dropped += A.dropped;
  }
  CHECK(N >= 600000, "compared 600000 values");
  if (bad || pruneBad) std::printf("  at() differs %ld times, prune() %ld times\n", bad, pruneBad);
  CHECK(bad == 0, "Auto.at() equals the closure version on random timelines (every event kind)");
  CHECK(pruneBad == 0, "Auto.prune() equals the closure version");
  CHECK(dropped == 0, "no event was dropped (reference run)");
}

// ---- 3. a full list ---------------------------------------------------------------------------------------------------
static void capacity() {
  struct Box { double before[4]; AutoN<8> a{0.5}; double after[4]; } b;
  for (double& x : b.before) x = 1234.5; for (double& x : b.after) x = 1234.5;
  int ok = 0; for (int i = 0; i < 20; i++) ok += b.a.target(1.0, 1.0 + i, 0.01) ? 1 : 0;
  CHECK(ok == 8 && b.a.size() == 8 && b.a.dropped == 12, "a full timeline refuses events and counts them");
  b.a.prune(100.0);   // folds eight targets into a head of two
  CHECK(b.a.size() == 2 && std::fabs(b.a.at(200.0) - 1.0) < 1e-9, "prune folds a full list");
  { AutoN<1> c(0); c.curve(mk(f_sq), 0.0, 10.0); c.prune(1.0); CHECK(c.size() == 2, "prune may grow a one-event list to a head of two (the slack)"); }
  bool intact = true; for (double x : b.before) if (x != 1234.5) intact = false; for (double x : b.after) if (x != 1234.5) intact = false;
  CHECK(intact, "nothing was written outside the timeline's storage");
}

// ---- 4. the callback contract ---------------------------------------------------------------------------------------
static void post(thunder_core* c, const char* s) { if (!thunder_post(c, s, (int)std::strlen(s))) { std::printf("FAIL: post %s\n", s); fails++; } }
static long long num(const char* buf, const char* key) { const char* p = std::strstr(buf, key); long long v = -1; if (p) std::sscanf(p + std::strlen(key), "%lld", &v); return v; }

static void contract() {
  const int sr = 48000;
  { // a command with "at" lands on exactly that sample, mid-block; same-frame commands keep their order
    thunder_core* c = thunder_create(sr, 512); std::vector<float> l(512), r(512);
    post(c, "{\"t\":\"bpm\",\"v\":60,\"at\":100003}");     // posted first, same frame as play: the step grid must use 60 bpm
    post(c, "{\"t\":\"play\",\"at\":100003}");
    post(c, "{\"t\":\"echo\",\"n\":5,\"at\":70001}");      // posted later, due earlier
    post(c, "{\"t\":\"stop\",\"at\":196003}");            // exactly on the second quarter note (48000 frames a quarter at 60): it must not sound
    std::vector<long long> pos; long long echoF = -1; char buf[256]; long long firstSound = -1;
    for (long long f = 0; f < 300000; f += 512) {
      thunder_process(c, l.data(), r.data(), 512);
      if (firstSound < 0) for (int i = 0; i < 512; i++) if (l[(size_t)i] != 0.0f) { firstSound = f + i; break; }
      while (thunder_poll(c, buf, (int)sizeof buf) > 0) { if (std::strstr(buf, "\"t\":\"pos\"")) pos.push_back(num(buf, "\"f\":")); else if (std::strstr(buf, "\"t\":\"echo\"")) echoF = num(buf, "\"f\":"); }
    }
    CHECK(echoF == 70001, "a timed echo answers from its own frame, not the block's start");
    CHECK(pos.size() == 2 && pos[0] == 100003 && pos[1] == 148003, "play at a frame starts the pattern on that sample (and the tempo set at the same frame applies)");
    CHECK(firstSound == 100004, "the first click's first non-zero sample is one after the start (its first sample is sin 0)");
    thunder_destroy(c);
  }
  { // a block longer than max_block gives the same samples as short blocks
    thunder_core* a = thunder_create(sr, 256); thunder_core* b = thunder_create(sr, 256);
    post(a, "{\"t\":\"play\",\"at\":1000}"); post(b, "{\"t\":\"play\",\"at\":1000}");
    const int N = 50000; std::vector<float> la((size_t)N), ra((size_t)N), lb((size_t)N), rb((size_t)N);
    thunder_process(a, la.data(), ra.data(), N);
    for (int o = 0; o < N; ) { const int n = N - o < 200 ? N - o : 200; thunder_process(b, lb.data() + o, rb.data() + o, n); o += n; }
    CHECK(std::memcmp(la.data(), lb.data(), sizeof(float) * (size_t)N) == 0, "one long block equals many short ones, sample for sample");
    double e = 0; for (float x : la) e += (double)x * x; CHECK(e > 1.0, "and it is not silence");
    thunder_destroy(a); thunder_destroy(b);
  }
  { // song snapshots: swapped at a block's start, acknowledged, old ones freed on the control thread
    thunder_core* c = thunder_create(sr, 512); std::vector<float> l(512), r(512); char buf[256]; std::vector<long long> revs;
    auto run = [&](int blocks) { for (int i = 0; i < blocks; i++) { thunder_process(c, l.data(), r.data(), 512); while (thunder_poll(c, buf, (int)sizeof buf) > 0) if (std::strstr(buf, "\"t\":\"song\"")) revs.push_back(num(buf, "\"rev\":")); } };
    post(c, "{\"t\":\"song.load\",\"rev\":1,\"S\":{}}"); run(1);
    post(c, "{\"t\":\"song.load\",\"rev\":2,\"S\":{}}"); post(c, "{\"t\":\"song.load\",\"rev\":3,\"S\":{}}"); run(1);   // two between blocks: the newest wins
    for (int k = 4; k < 200; k++) { char m[64]; std::snprintf(m, sizeof m, "{\"t\":\"song.load\",\"rev\":%d}", k); post(c, m); run(1); }   // many swaps: the return ring never fills
    CHECK(revs.size() == 198 && revs[0] == 1 && revs[1] == 3 && revs.back() == 199, "song snapshots swap in order and are acknowledged");
    thunder_destroy(c);
  }
}

int main() {
  golden(); reference(); capacity(); contract();
  if (fails) return 1;
  std::puts("PASS");
  return 0;
}
