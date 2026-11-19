// Follow-up after verify-238-239 (docs/port/reviews/verify-238-239.md), the test gaps and the page-parity rule (docs/port/U34A-NOTES.md "Follow-up after verify-238-239").
//   T1  a live-route, an sf-route and a missing-variation note take the track over (a long note on the same track stops); a note whose pad has no buffer does not (the page's
//       startNote returns first: index.html:22012 / :22022), and neither does a note of an unknown kit slot  (mutation M15: pool_.take dropped)
//   T2  a failed set_variation (NaN sample, bad rate, over the frame limit) leaves the table as it was                                   (mutation M8: scan after mutating)
//   T3  THUNDER_RENDER_MAX_VARIATIONS is 4096: the 4096th is held, the 4097th is E_LIMIT, a replacement at 4096 works                   (mutation M11: 4097)
//   T4  Cloud Seed is made for at most 96 kHz whatever the host's rate: FxHost at 192 / 384 kHz sounds the same, bit for bit, as at 96 kHz  (mutation M13: the cap removed)
//   P   a pad whose buffer changes loses its variations (core/core-processor.js:337), a failed set_pad drops nothing
// Constant (DC) pads: the level of the output says which buffer sounded; silence is exactly 0.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fx/fxhost.h"
#include "thunder/render.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

constexpr int kOut = 72000;        // 1.5 s at 48 kHz
constexpr int kLong = 240000;      // a 5 s pad
constexpr int FX_MUT = 41;
// 120 BPM, lpb 4: a row is 0.125 s. Track 0: a note of instrument `a` at row 0 and one of instrument `b` at row 4 (0.5 s), the second one with a command (fx, fxv) if fx >= 0.
// Instrument i plays pad i.
static std::string songJson(int a, int b, int fx = -1, int fxv = 0) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[8],\"insts\":[";
  for (int i = 0; i < 8; i++) { char t[100]; std::snprintf(t, sizeof t, "%s{\"pad\":%d,\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}", i ? "," : "", i); s += t; }
  char r[200];
  std::snprintf(r, sizeof r, "],\"phrases\":[[[60,-1,-1,-1,-1,%d,-1,-1,-1,-1,-1,-1]", a); s += r;
  for (int i = 1; i < 8; i++) {
    if (i == 4) std::snprintf(r, sizeof r, ",[60,-1,-1,-1,-1,%d,%d,%d,-1,-1,-1,-1]", b, fx, fx >= 0 ? fxv : -1);
    else std::snprintf(r, sizeof r, ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]");
    s += r;
  }
  return s + "]]}";
}
static thunder_render* mk(const std::string& song, int frames = kOut) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = frames;
  char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, err, sizeof err);
  if (!r) { std::printf("create failed: %s\n", err); g_fail++; }
  return r;
}
static std::vector<float> dc(float v, int n) { return std::vector<float>((size_t)n, v); }
struct Out { std::vector<float> L; thunder_render_stats st; bool finite = true; };
static Out drain(thunder_render* r, int frames = kOut) {
  Out o; o.L.assign((size_t)frames, 0); std::vector<float> R((size_t)frames, 0);
  int64_t got = 0; while (got < frames) { const int64_t n = thunder_render_read(r, o.L.data() + got, R.data() + got, std::min<int64_t>(4096, frames - got)); if (n <= 0) break; got += n; }
  CHECK(got == frames);
  std::memset(&o.st, 0, sizeof o.st); o.st.size = sizeof o.st; CHECK(thunder_render_stats_get(r, &o.st) == 0);
  for (float x : o.L) if (!std::isfinite(x)) o.finite = false;
  return o;
}
// mean |L| over [t0, t1) seconds
static double level(const Out& o, double t0, double t1) { double s = 0; const int a = (int)(t0 * 48000), b = (int)(t1 * 48000); for (int i = a; i < b; i++) s += std::fabs(o.L[(size_t)i]); return s / (b - a); }

static void takeover() {
  // the first note (instrument 0, pad 0: a 5 s DC of 0.2) sounds from 0 s; the second note is at 0.5 s. Level in 0.1 .. 0.4 s is the first note alone; in 0.7 .. 1.2 s whatever is left.
  auto run = [&](int b, int fx, int fxv, int route, bool padB, bool varB, float padLevel = 0.3f) {
    thunder_render* r = mk(songJson(0, b, fx, fxv)); if (!r) return Out();
    std::vector<float> p0 = dc(0.2f, kLong), pb = dc(padLevel, kLong);
    CHECK(thunder_render_set_pad(r, 0, p0.data(), nullptr, kLong, 48000) == 0);
    if (padB) CHECK(thunder_render_set_pad(r, b, pb.data(), nullptr, kLong, 48000) == 0);
    if (route) CHECK(thunder_render_set_pad_route(r, b, route) == 0);
    if (varB) CHECK(thunder_render_set_variation(r, b, 'm', 5, b, pb.data(), kLong, 48000) == 0);
    Out o = drain(r); thunder_render_free(r); return o;
  };
  const Out ctl = run(1, -1, 0, 0, true, false);   // control: a sampler note takes the track over and plays its own pad (0.3)
  const double c = level(ctl, 0.1, 0.4) / 0.2;
  CHECKF(c > 1e-3, "the long note plays: %g", c);
  CHECKF(std::fabs(level(ctl, 0.7, 1.2) - c * 0.3) < 1e-4 * c + 1e-7, "control: the second sampler note plays its pad (%g want %g)", level(ctl, 0.7, 1.2), c * 0.3);
  CHECK(ctl.finite);

  const Out live = run(1, -1, 0, THUNDER_RENDER_ROUTE_LIVE, true, false);
  CHECKF(level(live, 0.1, 0.4) > 0.5 * c * 0.2, "live route: the long note sounds before");
  CHECKF(level(live, 0.7, 1.2) == 0.0, "live route: the long note was taken over (level %g)", level(live, 0.7, 1.2));
  CHECK(live.st.notes_live == 1 && live.st.notes_played == 1);
  const Out live2 = run(1, -1, 0, THUNDER_RENDER_ROUTE_LIVE, false, false);   // no buffer at all on the live pad: still takes over
  CHECKF(level(live2, 0.7, 1.2) == 0.0, "live route without a buffer: taken over (%g)", level(live2, 0.7, 1.2));

  const Out sf = run(2, -1, 0, THUNDER_RENDER_ROUTE_SF, true, false);
  CHECKF(level(sf, 0.1, 0.4) > 0.5 * c * 0.2 && level(sf, 0.7, 1.2) == 0.0, "sf route: taken over (%g)", level(sf, 0.7, 1.2));
  CHECK(sf.st.notes_sf == 1 && sf.st.notes_played == 1);

  const Out miss = run(3, FX_MUT, 5, 0, true, false);   // a MUT note whose variation was not handed in: silent, counted, and it takes the track over
  CHECKF(level(miss, 0.1, 0.4) > 0.5 * c * 0.2 && level(miss, 0.7, 1.2) == 0.0, "missing variation: taken over (%g)", level(miss, 0.7, 1.2));
  CHECK(miss.st.notes_var_missing == 1 && miss.st.notes_played == 1);

  const Out nopad = run(4, -1, 0, 0, false, false);     // a note whose pad has no buffer: silent, counted, and the long note goes on (the page returns before it takes over)
  CHECKF(std::fabs(level(nopad, 0.7, 1.2) - level(nopad, 0.1, 0.4)) < 1e-4 * c + 1e-7 && level(nopad, 0.7, 1.2) > 0.5 * c * 0.2, "no buffer: the long note goes on (%g, before %g)", level(nopad, 0.7, 1.2), level(nopad, 0.1, 0.4));
  CHECK(nopad.st.notes_no_pad == 1 && nopad.st.notes_played == 1);
  const Out nokit = run(7, -1, 0, 0, false, false);     // the same for an instrument whose pad was never set (pad 7)
  CHECK(level(nokit, 0.7, 1.2) > 0.5 * c * 0.2 && nokit.st.notes_no_pad == 1);
}

static void failedVariation() {
  const std::string song = songJson(0, 0, FX_MUT, 5);   // the second note plays MUT 5 of pad 0
  thunder_render* r = mk(song); if (!r) return;
  std::vector<float> p0 = dc(0.2f, 48000), v = dc(0.4f, 48000), bad = v; bad[100] = NAN;
  CHECK(thunder_render_set_pad(r, 0, p0.data(), nullptr, 48000, 48000) == 0);
  CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, v.data(), 48000, 48000) == 0);
  CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, bad.data(), 48000, 48000) == THUNDER_RENDER_E_ARG);                                  // NaN sample over an existing key
  CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, v.data(), 48000, NAN) == THUNDER_RENDER_E_ARG);                                      // bad rate
  CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, v.data(), 48000, 0.5) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, nullptr, 48000, 48000) == THUNDER_RENDER_E_ARG);                                     // null samples
  CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, v.data(), (int64_t)THUNDER_RENDER_MAX_PAD_FRAMES + 1, 48000) == THUNDER_RENDER_E_LIMIT);
  CHECK(thunder_render_set_variation(r, 64, 'm', 5, 0, v.data(), 48000, 48000) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_variation(r, 0, 'm', 6, 0, bad.data(), 48000, 48000) == THUNDER_RENDER_E_ARG);                                  // NaN sample, a new key: nothing is added
  bad[100] = INFINITY; CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, bad.data(), 48000, 48000) == THUNDER_RENDER_E_ARG);
  Out o = drain(r); thunder_render_free(r);
  const double c = level(o, 0.1, 0.4) / 0.2;
  CHECKF(o.finite, "the output is finite");
  CHECKF(std::fabs(level(o, 0.7, 0.9) - c * 0.4) < 1e-4 * c + 1e-7, "the variation is as it was: %g want %g", level(o, 0.7, 0.9), c * 0.4);
  CHECK(o.st.notes_var == 1 && o.st.notes_var_missing == 0);
  // the new key that failed is not there: MUT 6 is a missing variation
  r = mk(songJson(0, 0, FX_MUT, 6)); if (!r) return;
  CHECK(thunder_render_set_pad(r, 0, p0.data(), nullptr, 48000, 48000) == 0);
  CHECK(thunder_render_set_variation(r, 0, 'm', 6, 0, bad.data(), 48000, 48000) == THUNDER_RENDER_E_ARG);
  o = drain(r); thunder_render_free(r);
  CHECK(o.st.notes_var_missing == 1 && o.st.notes_var == 0 && level(o, 0.7, 0.9) == 0.0);
}

static void variationLimit() {
  CHECK(THUNDER_RENDER_MAX_VARIATIONS == 4096);
  thunder_render* r = mk(songJson(0, 0)); if (!r) return;
  const float x = 0.1f; int ok = 0;
  for (int ii = 0; ii < 16; ii++) for (int k = 0; k < 2; k++) for (int num = 0; num < 128; num++) if (thunder_render_set_variation(r, ii, k ? 'g' : 'm', num, 0, &x, 1, 48000) == 0) ok++;
  CHECKF(ok == 4096, "4096 variations are held (%d)", ok);
  CHECK(thunder_render_set_variation(r, 16, 'g', 0, 0, &x, 1, 48000) == THUNDER_RENDER_E_LIMIT);        // the 4097th
  CHECK(thunder_render_set_variation(r, 15, 'g', 127, 0, &x, 1, 48000) == 0);                           // a replacement at 4096
  CHECK(thunder_render_set_variation(r, 0, 'm', 0, 0, nullptr, 0, 0) == 0);                              // remove one ...
  CHECK(thunder_render_set_variation(r, 16, 'g', 0, 0, &x, 1, 48000) == 0);                             // ... and a new one fits
  CHECK(thunder_render_set_variation(r, 16, 'g', 1, 0, &x, 1, 48000) == THUNDER_RENDER_E_LIMIT);
  thunder_render_free(r);
}

// P: set_pad drops the pad's variations (every instrument's) when it succeeds, not when it fails; frames == 0 drops them too; another pad's variations stay
static void padDropsVariations() {
  const std::string song = songJson(0, 0, FX_MUT, 5);
  std::vector<float> p0 = dc(0.2f, 48000), v = dc(0.4f, 48000), bad = p0; bad[7] = NAN;
  auto run = [&](int mode) {
    thunder_render* r = mk(song); if (!r) return Out();
    CHECK(thunder_render_set_pad(r, 0, p0.data(), nullptr, 48000, 48000) == 0);
    CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, v.data(), 48000, 48000) == 0);
    CHECK(thunder_render_set_variation(r, 3, 'g', 9, 0, v.data(), 48000, 48000) == 0);   // another instrument, same pad: dropped too
    CHECK(thunder_render_set_variation(r, 0, 'm', 5, 1, v.data(), 48000, 48000) == 0);   // same key, another pad: stays
    if (mode == 1) CHECK(thunder_render_set_pad(r, 0, p0.data(), nullptr, 48000, 48000) == 0);                  // replaced by the same samples
    if (mode == 2) CHECK(thunder_render_set_pad(r, 0, bad.data(), nullptr, 48000, 48000) == THUNDER_RENDER_E_ARG);   // failed: nothing dropped
    if (mode == 3) { CHECK(thunder_render_set_pad(r, 0, nullptr, nullptr, 0, 0) == 0); CHECK(thunder_render_set_pad(r, 0, p0.data(), nullptr, 48000, 48000) == 0); }   // removed, set again
    if (mode == 4) CHECK(thunder_render_set_pad(r, 1, p0.data(), nullptr, 48000, 48000) == 0);                  // another pad changed: pad 0's variations stay
    if (mode == 5) CHECK(thunder_render_set_pad(r, 0, p0.data(), nullptr, (int64_t)THUNDER_RENDER_MAX_PAD_FRAMES + 1, 48000) == THUNDER_RENDER_E_LIMIT);
    Out o = drain(r); thunder_render_free(r); return o;
  };
  const Out keep = run(0); const double c = level(keep, 0.1, 0.4) / 0.2;
  CHECK(keep.st.notes_var == 1 && std::fabs(level(keep, 0.7, 0.9) - c * 0.4) < 1e-4 * c + 1e-7);
  for (int mode : {1, 3}) { const Out o = run(mode); CHECKF(o.st.notes_var == 0 && o.st.notes_var_missing == 1 && level(o, 0.7, 0.9) == 0.0, "mode %d: the variation is dropped", mode); }
  for (int mode : {2, 4, 5}) { const Out o = run(mode); CHECKF(o.st.notes_var == 1 && std::fabs(level(o, 0.7, 0.9) - c * 0.4) < 1e-4 * c + 1e-7, "mode %d: the variation stays", mode); }
  // the other pad's variation (pad 1) is still there after pad 0 changed: a song whose second note plays pad 1's MUT
  { const std::string s1 = songJson(0, 1, FX_MUT, 5); thunder_render* r = mk(s1); if (!r) return;
    std::vector<float> p1 = dc(0.3f, 48000);
    CHECK(thunder_render_set_pad(r, 1, p1.data(), nullptr, 48000, 48000) == 0);
    CHECK(thunder_render_set_variation(r, 1, 'm', 5, 1, v.data(), 48000, 48000) == 0);
    CHECK(thunder_render_set_pad(r, 0, p0.data(), nullptr, 48000, 48000) == 0);   // pad 0 changes
    Out o = drain(r); thunder_render_free(r); CHECK(o.st.notes_var == 1); }
}

#ifdef THUNDER_HAVE_TFX
// T4: Cloud Seed (type 20) is built for at most 96 kHz (core/src/fx/fxhost.cpp make_fx, kCloudSeedMaxSr). With the cap, the effect made by a host at 192 or 384 kHz is the one a host
// at 96 kHz makes, so the same entry and the same input give the same bits. Without it the delays are twice / four times longer and the output differs (and, under a sanitizer, reads out of bounds).
static std::vector<float> cloudSeed(double sr) {
  using namespace thunder;
  FxHost H(sr); FxEntry e; e.t = 20; e.slot = 0; e.on = true; std::snprintf(e.key, kFxKey, "0:%s", fx_key_of(20)); e.nvals = fx_param_count(20);
  for (int k = 0; k < e.nvals; k++) e.vals[k] = fx_param_default(20, k);
  fx_map(20, e.vals, 96000.0, 120.0, 0.0, e.p);   // the same mapped parameters at every rate: only the rate the effect is MADE with may differ
  FxPlan* p = H.plan(&e, 1); CHECK(p != nullptr); if (!p) return {};
  H.commit(p); delete p;
  std::vector<float> out; uint32_t sd = 7;
  for (int b = 0; b < 300; b++) {
    float L[128], R[128];
    for (int i = 0; i < 128; i++) { sd = sd * 1664525u + 1013904223u; L[i] = b < 4 ? (float)(sd >> 8) / 16777216.f - 0.5f : 0.f; R[i] = L[i]; }
    H.process(L, R, 128, 0.0 + b * 128 / 96000.0);
    out.insert(out.end(), L, L + 128); out.insert(out.end(), R, R + 128);
  }
  return out;
}
static void cloudSeedCap() {
  const std::vector<float> a = cloudSeed(96000.0);
  double e = 0; for (float x : a) e += (double)x * x;
  CHECKF(a.size() > 0 && e > 1e-3, "Cloud Seed at 96 kHz makes sound (energy %g)", e);
  for (double sr : {192000.0, 384000.0}) {
    const std::vector<float> b = cloudSeed(sr);
    CHECKF(b.size() == a.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0, "Cloud Seed made at %.0f Hz is the 96 kHz effect, bit for bit", sr);
  }
  const std::vector<float> c = cloudSeed(48000.0);   // (and the rate does matter below the cap: the check above can fail)
  CHECK(c.size() == a.size() && std::memcmp(a.data(), c.data(), a.size() * sizeof(float)) != 0);
}
#endif

int main() {
  takeover();
  failedVariation();
  variationLimit();
  padDropsVariations();
#ifdef THUNDER_HAVE_TFX
  cloudSeedCap();
#else
  std::printf("render_var: no DSP library: the Cloud Seed check is not run\n");
#endif
  std::printf(g_fail ? "FAILED: %d\n" : "OK\n", g_fail);
  return g_fail ? 1 : 0;
}
