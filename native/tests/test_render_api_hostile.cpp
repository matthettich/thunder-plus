// U34a gate 3: hostile input to the render C API, meant for the ASan + UBSan build (THUNDER_SANITIZE): malformed / truncated / huge song bytes,
// pads with extreme sizes and rates, reads with bad arguments, free mid-render, several extreme fields at once, and a fixed-seed fuzz loop.
// A failure is a wrong return code; a crash or a sanitizer report ends the run. Usage: test_render_api_hostile [fuzz iterations (default 800)]
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <limits>
#include "render_api_common.h"

using namespace u34;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)
static thunder_render_opts defOpts() { thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; return o; }
static thunder_render* mk(const std::string& s, const thunder_render_opts* o, char* e = nullptr, size_t ec = 0) { return thunder_render_create((const uint8_t*)s.data(), s.size(), o, e, ec); }

static void testCreate(const std::string& good, const std::string& electro) {
  char err[160];
  const thunder_render_opts d = defOpts();
  CHECK("null song", thunder_render_create(nullptr, 10, &d, err, sizeof err) == nullptr && err[0]);
  CHECK("null song, null err", thunder_render_create(nullptr, 0, &d, nullptr, 0) == nullptr);
  CHECK("zero bytes", mk("", &d, err, sizeof err) == nullptr && err[0]);
  { const char tiny[] = "{\"v\":6}"; thunder_render* r = thunder_render_create((const uint8_t*)tiny, sizeof tiny - 1, &d, err, 1); CHECK("err cap 1 is NUL-terminated", err[0] == 0); thunder_render_free(r); }
  { char e5[5] = {1, 1, 1, 1, 1}; CHECK("junk", mk("not json at all", &d, e5, sizeof e5) == nullptr && e5[4] == 0 && std::strlen(e5) <= 4); }
  CHECK("junk with embedded NULs", mk(std::string("{\"v\":6,\0\0\"x\":[", 14), &d, err, sizeof err) == nullptr);
  CHECK("huge: over the song limit", [&] { std::string big(THUNDER_RENDER_MAX_SONG_BYTES + 1, ' '); return thunder_render_create((const uint8_t*)big.data(), big.size(), &d, err, sizeof err) == nullptr; }());
  CHECK("size_t max bytes", thunder_render_create((const uint8_t*)good.data(), (size_t)-1, &d, err, sizeof err) == nullptr);
  CHECK("deeply nested (64 MiB of '[')", [&] { std::string deep(THUNDER_RENDER_MAX_SONG_BYTES, '['); return mk(deep, &d, err, sizeof err) == nullptr; }());
  CHECK("deeply nested objects", [&] { std::string deep; for (int i = 0; i < 100000; i++) deep += "{\"a\":"; return mk(deep, &d, err, sizeof err) == nullptr; }());
  // truncated at many points (every prefix of the short song, and 60 prefixes of ELECTRO): either NULL or a usable handle, never a crash
  int made = 0;
  for (size_t n = 0; n < good.size(); n += 7) { thunder_render* r = thunder_render_create((const uint8_t*)good.data(), n, &d, err, sizeof err); if (r) { made++; std::vector<float> a(300), b(300); thunder_render_read(r, a.data(), b.data(), 300); thunder_render_free(r); } }
  for (int k = 1; k < 60; k++) { const size_t n = electro.size() * (size_t)k / 60; thunder_render* r = thunder_render_create((const uint8_t*)electro.data(), n, &d, err, sizeof err); if (r) { made++; thunder_render_free(r); } }
  std::printf("  truncations: %d prefixes made a handle\n", made);
  // options
  auto bad = [&](const char* msg, thunder_render_opts o) { CHECK(msg, thunder_render_create((const uint8_t*)good.data(), good.size(), &o, err, sizeof err) == nullptr && err[0]); };
  thunder_render_opts o;
  o = d; o.sample_rate = std::numeric_limits<double>::quiet_NaN(); bad("NaN rate", o);
  o = d; o.sample_rate = std::numeric_limits<double>::infinity(); bad("inf rate", o);
  o = d; o.sample_rate = -48000; bad("negative rate", o);
  o = d; o.sample_rate = 7999; bad("rate 7999", o);
  o = d; o.sample_rate = 384001; bad("rate 384001", o);
  o = d; o.sample_rate = 1e300; bad("rate 1e300", o);
  o = d; o.effects = 2; bad("effects 2", o);
  o = d; o.master_chain = -1; bad("master -1", o);
  o = d; o.pdc_mode = 3; bad("pdc 3", o);
  o = d; o.pdc_mode = INT_MIN; bad("pdc INT_MIN", o);
  o = d; o.use_frames = 1; o.frames = -1; bad("frames -1", o);
  o = d; o.use_frames = 1; o.frames = INT64_MAX; bad("frames INT64_MAX", o);
  o = d; o.max_frames = INT64_MIN; bad("max_frames INT64_MIN", o);
  o = d; o.reserved0 = 1; bad("reserved0", o);
  o = d; o.size = 4; bad("size 4", o);
  o = d; o.vars_json = nullptr; o.vars_bytes = 5; bad("vars null with bytes", o);
  o = d; o.vars_json = (const uint8_t*)"{"; o.vars_bytes = 1; bad("vars truncated", o);
  o = d; o.vars_json = (const uint8_t*)"[1]"; o.vars_bytes = 3; bad("vars not an object", o);
  o = d; o.vars_json = (const uint8_t*)"{}"; o.vars_bytes = THUNDER_RENDER_MAX_VARS_BYTES + 1; bad("vars over the limit", o);
  { std::vector<unsigned char> big(sizeof o + 64, 0xAB); thunder_render_opts* bo = (thunder_render_opts*)big.data(); *bo = d; bo->size = (uint32_t)big.size();   // a NEWER, larger struct: the unknown tail is ignored
    memset(big.data() + sizeof o, 0xAB, 64); thunder_render* r = thunder_render_create((const uint8_t*)good.data(), good.size(), bo, err, sizeof err); CHECK("newer larger opts struct is accepted", r != nullptr); thunder_render_free(r); }
  { thunder_render_opts s = d; s.size = (uint32_t)offsetof(thunder_render_opts, effects);   // an OLDER, shorter struct: later fields default
    thunder_render* r = thunder_render_create((const uint8_t*)good.data(), good.size(), &s, err, sizeof err); CHECK("older shorter opts struct is accepted", r != nullptr && thunder_render_length(r) == -1); thunder_render_free(r); }
  { thunder_render* r = thunder_render_create((const uint8_t*)good.data(), good.size(), nullptr, err, sizeof err); CHECK("opts NULL = defaults", r != nullptr); thunder_render_free(r); }
  { o = d; o.vars_json = (const uint8_t*)"{\"vtrk\":true,\"NSLOT\":1e30,\"TFX\":[1,2,{\"x\":null}]}"; o.vars_bytes = std::strlen((const char*)o.vars_json); thunder_render* r = thunder_render_create((const uint8_t*)good.data(), good.size(), &o, err, sizeof err); thunder_render_free(r); }
  { o = d; o.sample_rate = 384000; o.effects = 1; o.master_chain = 1; o.pdc_mode = 2; thunder_render* r = thunder_render_create((const uint8_t*)good.data(), good.size(), &o, err, sizeof err); CHECK("384 kHz all on", r != nullptr); thunder_render_free(r); }
  { o = d; o.sample_rate = 8000; thunder_render* r = thunder_render_create((const uint8_t*)good.data(), good.size(), &o, err, sizeof err); CHECK("8 kHz", r != nullptr); thunder_render_free(r); }
}

static void testPads(const std::string& good) {
  const PadSet ps; const thunder_render_opts d = defOpts(); char err[64];
  thunder_render* r = mk(good, &d, err, sizeof err); CHECK("create", r != nullptr); if (!r) return;
  const float* p = ps.pad(0); const double NaN = std::numeric_limits<double>::quiet_NaN(), Inf = std::numeric_limits<double>::infinity();
  CHECK("null handle", thunder_render_set_pad(nullptr, 0, p, nullptr, 10, 48000) == THUNDER_RENDER_E_ARG);
  for (int bp : {-1, 256, INT_MIN, INT_MAX}) CHECK("bad pad number", thunder_render_set_pad(r, bp, p, nullptr, 10, 48000) == THUNDER_RENDER_E_ARG && thunder_render_set_pad_kit(r, bp, 0, 0, 0) == THUNDER_RENDER_E_ARG);
  CHECK("negative frames", thunder_render_set_pad(r, 0, p, nullptr, -1, 48000) == THUNDER_RENDER_E_ARG);
  CHECK("frames INT64_MIN", thunder_render_set_pad(r, 0, p, nullptr, INT64_MIN, 48000) == THUNDER_RENDER_E_ARG);
  CHECK("frames INT64_MAX: limit, nothing read", thunder_render_set_pad(r, 0, p, nullptr, INT64_MAX, 48000) == THUNDER_RENDER_E_LIMIT);
  CHECK("frames 2^28+1: limit", thunder_render_set_pad(r, 0, p, nullptr, ((int64_t)1 << 28) + 1, 48000) == THUNDER_RENDER_E_LIMIT);
  CHECK("frames 2^28 mono = 1 GiB: limit", thunder_render_set_pad(r, 0, p, p, (int64_t)1 << 28, 48000) == THUNDER_RENDER_E_LIMIT);
  CHECK("null L", thunder_render_set_pad(r, 0, nullptr, nullptr, 10, 48000) == THUNDER_RENDER_E_ARG);
  for (double bad : {NaN, Inf, -Inf, 0.0, -1.0, 0.5, 1e6 + 1, 1e300, 1e-300}) CHECK("bad pad rate", thunder_render_set_pad(r, 0, p, nullptr, 10, bad) == THUNDER_RENDER_E_ARG);
  { std::vector<float> nn(100, 0.5f); nn[57] = std::numeric_limits<float>::quiet_NaN(); CHECK("NaN sample in L", thunder_render_set_pad(r, 0, nn.data(), nullptr, 100, 48000) == THUNDER_RENDER_E_ARG);
    nn[57] = 0; nn[99] = INFINITY; CHECK("inf sample in R", thunder_render_set_pad(r, 0, p, nn.data(), 100, 48000) == THUNDER_RENDER_E_ARG); }
  CHECK("frames 0 with NULLs removes the pad", thunder_render_set_pad(r, 5, nullptr, nullptr, 0, NaN) == 0);
  CHECK("choke 256", thunder_render_set_pad_kit(r, 0, 256, 0, 0) == THUNDER_RENDER_E_ARG);
  CHECK("choke -1", thunder_render_set_pad_kit(r, 0, -1, 0, 0) == THUNDER_RENDER_E_ARG);
  CHECK("syn 2", thunder_render_set_pad_kit(r, 0, 0, 2, 0) == THUNDER_RENDER_E_ARG);
  CHECK("rel NaN / -1 / 61", thunder_render_set_pad_kit(r, 0, 0, 0, NaN) && thunder_render_set_pad_kit(r, 0, 0, 0, -1) && thunder_render_set_pad_kit(r, 0, 0, 0, 61));
  // memory budget: 4 x 128 MiB fills it; the fifth is refused; replacing a pad returns its bytes
  { const size_t F = (size_t)1 << 25; std::vector<float> big(F, 0.1f);
    for (int k = 0; k < 4; k++) CHECK("128 MiB pad fits", thunder_render_set_pad(r, 10 + k, big.data(), nullptr, (int64_t)F, 48000) == 0);
    CHECK("the fifth is over the budget", thunder_render_set_pad(r, 20, big.data(), nullptr, (int64_t)F, 48000) == THUNDER_RENDER_E_LIMIT);
    CHECK("replacing pad 10 with a larger (stereo) one is over", thunder_render_set_pad(r, 10, big.data(), big.data(), (int64_t)F, 48000) == THUNDER_RENDER_E_LIMIT);
    CHECK("replacing pad 10 with a small one", thunder_render_set_pad(r, 10, p, nullptr, 100, 48000) == 0);
    CHECK("now a (nearly) 128 MiB one fits again", thunder_render_set_pad(r, 20, big.data(), nullptr, (int64_t)F - 1000, 48000) == 0);
    for (int k = 11; k < 14; k++) thunder_render_set_pad(r, k, nullptr, nullptr, 0, 0); thunder_render_set_pad(r, 20, nullptr, nullptr, 0, 0); }
  // extreme but valid pads, then a render under the sanitizers
  const float one[1] = {0.7f}, two[2] = {0.7f, -0.7f};
  for (int k = 0; k < 8; k++) { const double sr = k == 0 ? 1 : k == 1 ? 1e6 : k == 2 ? 8000 : k == 3 ? 384000 : k == 4 ? 3.7 : k == 5 ? 999999.5 : 48000; CHECK("pad", thunder_render_set_pad(r, k, k < 4 ? one : p, k & 1 ? two : nullptr, k < 4 ? 1 : (k & 1 ? 2 : 24000), sr) == 0); }
  { std::vector<float> a(512), b(512); for (int i = 0; i < 40; i++) { const int64_t n = thunder_render_read(r, a.data(), b.data(), 512); CHECK("render with extreme pads", n >= 0); if (n <= 0) break; } }
  thunder_render_free(r);
}

static void testRead(const std::string& good) {
  const PadSet ps; const thunder_render_opts d = defOpts(); thunder_render* r = makeApi(good, ps, Cfg(), false, 0); CHECK("create", r != nullptr); if (!r) return;
  std::vector<float> a(1000), b(1000);
  CHECK("null handle", thunder_render_read(nullptr, a.data(), b.data(), 10) == THUNDER_RENDER_E_ARG);
  CHECK("null L", thunder_render_read(r, nullptr, b.data(), 10) == THUNDER_RENDER_E_ARG);
  CHECK("null R", thunder_render_read(r, a.data(), nullptr, 10) == THUNDER_RENDER_E_ARG);
  CHECK("L == R", thunder_render_read(r, a.data(), a.data(), 10) == THUNDER_RENDER_E_ARG);
  CHECK("overlap", thunder_render_read(r, a.data(), a.data() + 5, 10) == THUNDER_RENDER_E_ARG);
  CHECK("adjacent is fine", thunder_render_read(r, a.data(), a.data() + 500, 500) == 500);
  CHECK("negative frames", thunder_render_read(r, a.data(), b.data(), -1) == THUNDER_RENDER_E_ARG);
  CHECK("INT64_MIN", thunder_render_read(r, a.data(), b.data(), INT64_MIN) == THUNDER_RENDER_E_ARG);
  CHECK("INT64_MAX", thunder_render_read(r, a.data(), b.data(), INT64_MAX) == THUNDER_RENDER_E_ARG);
  CHECK("frames 0 with NULL buffers", thunder_render_read(r, nullptr, nullptr, 0) == 0);
  CHECK("a failed read left the position alone", thunder_render_position(r) == 500);
  CHECK("null handle everywhere", thunder_render_length(nullptr) == -1 && thunder_render_position(nullptr) == -1 && thunder_render_latency(nullptr) == -1 && thunder_render_drain(nullptr, 1) < 0 && thunder_render_stats_get(nullptr, nullptr) < 0);
  CHECK("drain before the end is E_STATE", thunder_render_drain(r, 1000) == THUNDER_RENDER_E_STATE);
  CHECK("drain negative", thunder_render_drain(r, -1) == THUNDER_RENDER_E_ARG);
  thunder_render_stats s; s.size = 2; CHECK("stats with a tiny size", thunder_render_stats_get(r, &s) == THUNDER_RENDER_E_ARG);
  { thunder_render_stats t; std::memset(&t, 0xAB, sizeof t); t.size = 12; CHECK("stats with a short struct", thunder_render_stats_get(r, &t) == 0 && t.size == 12 && ((unsigned char*)&t)[20] == 0xAB); }
  thunder_render_free(r); thunder_render_free(nullptr);
  // free mid-render in every configuration (cancel), before any read, after set_pad only, after some reads, and a stale-looking re-create
  for (int k = 0; k < 8; k++) { Cfg c; c.fx = k & 1; c.master = k & 2; c.pdc = (k >> 2) & 1 ? 2 : 0; thunder_render* q = makeApi(good, ps, c, k & 1, 100000);
    if (k % 3) { std::vector<float> x(333), y(333); for (int i = 0; i < k; i++) thunder_render_read(q, x.data(), y.data(), 333); } thunder_render_free(q); }
  (void)d;
}

// ---- the fuzz ----
static uint32_t g_rng = 20251118u;
static uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
static std::string mutate(std::string s) {
  static const char* ext[] = {"0", "-1", "2147483648", "-2147483649", "99999999999999999999", "1e308", "-1e308", "1e-308", "4294967295", "65536", "0.5", "1e9", "255", "256", "-128", "1000000", "3.4e38", "9007199254740993"};
  const int edits = 1 + (int)(rnd() % 6);
  for (int e = 0; e < edits && !s.empty(); e++) {
    switch (rnd() % 5) {
      case 0: s[rnd() % s.size()] ^= (char)(1u << (rnd() % 8)); break;                                   // a flipped bit
      case 1: case 2: case 3: {                                                                          // a number replaced by an extreme one
        size_t pos = rnd() % s.size(); for (int t = 0; t < 400 && !(s[pos] >= '0' && s[pos] <= '9'); t++) pos = rnd() % s.size();
        size_t a = pos, b = pos; while (a > 0 && (std::isdigit((unsigned char)s[a - 1]) || s[a - 1] == '-' || s[a - 1] == '.')) a--; while (b < s.size() && (std::isdigit((unsigned char)s[b]) || s[b] == '.')) b++;
        s.replace(a, b - a, ext[rnd() % (sizeof ext / sizeof *ext)]); break; }
      default: s.erase(rnd() % s.size(), 1 + rnd() % 9); break;                                          // bytes cut out
    }
  }
  if (rnd() % 10 == 0 && !s.empty()) s.resize(rnd() % s.size());
  return s;
}

// several extreme fields at once: EVERY number of the song (or, for ELECTRO, a third of them) replaced by one extreme value, for each extreme
static std::string allNumbers(const std::string& s, const char* ext, uint32_t pctOf3) {
  std::string o; size_t i = 0;
  while (i < s.size()) {
    if ((s[i] >= '0' && s[i] <= '9') || (s[i] == '-' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9')) {
      size_t j = i + 1; while (j < s.size() && (std::isdigit((unsigned char)s[j]) || s[j] == '.' || s[j] == 'e' || s[j] == 'E' || s[j] == '+' || s[j] == '-')) j++;
      if (rnd() % 3 < pctOf3) o += ext; else o.append(s, i, j - i);
      i = j;
    } else o += s[i++];
  }
  return o;
}
static void testExtremeAll(const std::string& good, const std::string& electro) {
  static const char* ext[] = {"0", "-1", "2147483647", "2147483648", "-2147483648", "99999999999999999999", "1e308", "-1e308", "1e-308", "4294967296", "65535", "0.5", "1e9", "255", "-128", "3.4e38"};
  int made = 0, runs = 0; const PadSet ps; std::vector<float> a(777), b(777);
  for (const char* e : ext) for (int pass = 0; pass < 2; pass++) {
    const std::string s = pass == 0 ? allNumbers(good, e, 3) : allNumbers(electro, e, 1);
    for (int fx = 0; fx < 2; fx++) {
      thunder_render_opts o = defOpts(); o.effects = fx; o.master_chain = fx; o.pdc_mode = fx ? 1 : 0; char err[64];
      thunder_render* r = mk(s, &o, err, sizeof err); runs++; if (!r) continue; made++;
      for (int p = 0; p < 64; p += 3) thunder_render_set_pad(r, p, ps.pad(p), p & 1 ? ps.pad(1) : nullptr, kNS, 48000);
      for (int k = 0; k < 5; k++) if (thunder_render_read(r, a.data(), b.data(), 777) < 0) { std::printf("FAIL extreme: read < 0\n"); g_fail++; break; }
      thunder_render_free(r);
    }
  }
  std::printf("  extreme fields at once: %d of %d songs made a handle and rendered\n", made, runs);
}
static void fuzz(int iters, const std::string& good, const std::string& electro) {
  const PadSet ps; int made = 0, refused = 0; long long frames = 0; int64_t nan = 0;
  std::vector<float> a(5000), b(5000);
  for (int it = 0; it < iters; it++) {
    const bool big = it % 12 == 11;
    const std::string s = mutate(big ? electro : good);
    thunder_render_opts o = defOpts(); static const double rates[] = {8000, 44100, 48000, 48000, 96000, 22050, 384000}; o.sample_rate = rates[rnd() % 7];
    if (o.sample_rate == 384000 && rnd() % 3) o.sample_rate = 48000;
    o.effects = rnd() % 3 == 0; o.master_chain = rnd() % 3 == 0; o.pdc_mode = (int)(rnd() % 3); o.page_timing = rnd() % 4 == 0; o.use_frames = rnd() % 2; o.frames = 1 + rnd() % 30000; o.max_frames = rnd() % 2 ? 0 : 1 + rnd() % 40000;
    char err[96]; thunder_render* r = mk(s, &o, err, sizeof err);
    if (!r) { refused++; continue; }
    made++;
    const int np = 1 + (int)(rnd() % 6);
    for (int k = 0; k < np; k++) { const int pad = (int)(rnd() % 70); const int64_t fr = rnd() % 5 == 0 ? (int64_t)(rnd() % 4) : (int64_t)(1 + rnd() % kNS); const double sr = rnd() % 4 == 0 ? 1 + (double)(rnd() % 999999) : 48000;
      thunder_render_set_pad(r, pad, ps.pad(pad % 64), rnd() % 2 ? ps.pad((pad + 1) % 64) : nullptr, std::min<int64_t>(fr, kNS), sr); thunder_render_set_pad_kit(r, pad, (int)(rnd() % 4), (int)(rnd() % 2), (rnd() % 100) / 100.0); }
    if (rnd() % 2) for (int p = 0; p < 64; p += 1 + (int)(rnd() % 3)) thunder_render_set_pad(r, p, ps.pad(p), nullptr, kNS, 48000);
    int64_t got = 0; const int reads = 1 + (int)(rnd() % 8);
    for (int k = 0; k < reads; k++) {
      static const int64_t sizes[] = {1, 7, 63, 128, 129, 1000, 4096, 5000}; const int64_t n = sizes[rnd() % 8];
      const int64_t w = thunder_render_read(r, a.data(), b.data(), n); if (w < 0) { std::printf("fuzz %d: read returned %lld\n", it, (long long)w); g_fail++; break; }
      for (int64_t i = 0; i < w; i++) if (!std::isfinite(a[(size_t)i]) || !std::isfinite(b[(size_t)i])) nan++;
      got += w; if (w == 0) break;
      if (rnd() % 9 == 0) thunder_render_set_pad(r, 0, ps.pad(0), nullptr, 100, 48000);   // after the first read: E_STATE, never a crash
    }
    frames += got; thunder_render_position(r); thunder_render_length(r); thunder_render_latency(r);
    thunder_render_free(r);   // mostly mid-render
  }
  std::printf("  fuzz: %d iterations (seed 20251118): %d handles made, %d refused, %lld frames read, %lld non-finite output samples (not a failure: extreme songs can overflow)\n", iters, made, refused, frames, (long long)nan);
}

int main(int argc, char** argv) {
  const std::string good = shortSong(), electro = readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder");
  if (electro.empty()) { std::printf("cannot read ELECTRO\n"); return 2; }
  testCreate(good, electro); std::printf("  create: done\n");
  testPads(good); std::printf("  pads: done\n");
  testRead(good); std::printf("  read: done\n");
  testExtremeAll(good, electro); std::printf("  extreme: done\n");
  fuzz(argc > 1 ? std::atoi(argv[1]) : 800, good, electro);
  if (g_fail) { std::printf("test_render_api_hostile: %d FAILED\n", g_fail); return 1; }
  std::printf("test_render_api_hostile: ok\n"); return 0;
}
