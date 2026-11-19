// U34a verification fixes (docs/port/reviews/verify-237.md): each check here failed on the code the verifier tested (7741ba6) and passes now.
//   D1 max_frames exact + hit_limit, D2 drain starts the handle, the two surviving mutations (the pad copy; drain before the end), D8 the reverb at
//   192 / 384 kHz (the check that sees it is the sanitizer build: out of bounds in CloudSeed; here also that the output is finite and sane),
//   and the length estimate. The out-of-memory fixes (D4 to D7) are in test_render_api_oom.cpp.
#include <algorithm>
#include <cstdlib>
#include "render_api_common.h"

using namespace u34;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)

static thunder_render_stats stats(thunder_render* r) { thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; thunder_render_stats_get(r, &s); return s; }

static int64_t readAll(thunder_render* r, std::vector<float>* L, std::vector<float>* R, int64_t bs = 4096) {
  std::vector<float> a((size_t)bs), b((size_t)bs); int64_t tot = 0;
  for (;;) { const int64_t n = thunder_render_read(r, a.data(), b.data(), bs); if (n <= 0) break; if (L) L->insert(L->end(), a.begin(), a.begin() + n); if (R) R->insert(R->end(), b.begin(), b.begin() + n); tot += n; }
  return tot;
}

static thunder_render* mkMax(const std::string& song, const Cfg& c, int64_t maxFrames) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.effects = c.fx; o.master_chain = c.master; o.max_frames = maxFrames;
  return thunder_render_create((const uint8_t*)song.data(), song.size(), &o, nullptr, 0);
}

// D1: the output ends exactly at max_frames, hit_limit = 1 whenever the cut happened (also inside the last quantum of the natural render)
static void caseMaxFrames(const char* name, const std::string& song, const Cfg& c) {
  const PadSet ps;
  thunder_render* z = mkMax(song, c, 0); CHECK("create", z != nullptr); if (!z) return; installPads(z, ps);
  const int64_t nat = readAll(z, nullptr, nullptr); CHECK("natural render has frames", nat > 1000); CHECK("natural: hit_limit 0", stats(z).hit_limit == 0); thunder_render_free(z);
  std::vector<int64_t> cases = {1, 2, 100, 127, 128, 129, 255, 1000, 5000, nat / 2, nat - 129, nat - 128, nat - 127, nat - 100, nat - 2, nat - 1};
  for (int64_t m : cases) {
    thunder_render* r = mkMax(song, c, m); CHECK("create", r != nullptr); if (!r) continue; installPads(r, ps);
    const int64_t n = readAll(r, nullptr, nullptr, 333);
    char msg[160]; std::snprintf(msg, sizeof msg, "%s max_frames=%lld (natural %lld): %lld frames", name, (long long)m, (long long)nat, (long long)n);
    CHECK(msg, n == m);
    CHECK("hit_limit = 1 (the cut happened)", stats(r).hit_limit == 1);
    CHECK("length == max_frames", thunder_render_length(r) == m);
    CHECK("position == max_frames", thunder_render_position(r) == m);
    thunder_render_free(r);
  }
  for (int64_t m : {nat, nat + 1, nat + 5000}) {   // no cut: the natural length, hit_limit 0
    thunder_render* r = mkMax(song, c, m); CHECK("create", r != nullptr); if (!r) continue; installPads(r, ps);
    const int64_t n = readAll(r, nullptr, nullptr); char msg[160]; std::snprintf(msg, sizeof msg, "%s max_frames=%lld (>= natural %lld): %lld frames", name, (long long)m, (long long)nat, (long long)n);
    CHECK(msg, n == nat); CHECK("hit_limit 0 when nothing was cut", stats(r).hit_limit == 0); thunder_render_free(r);
  }
  // the first frames of a cut render are the natural render's first frames
  { std::vector<float> a, b, c2, d; thunder_render* r = mkMax(song, c, 0); installPads(r, ps); readAll(r, &a, &b); thunder_render_free(r);
    r = mkMax(song, c, 1000); installPads(r, ps); readAll(r, &c2, &d); thunder_render_free(r);
    CHECK("cut render is the prefix", c2.size() == 1000 && std::memcmp(a.data(), c2.data(), 4000) == 0 && std::memcmp(b.data(), d.data(), 4000) == 0); }
}

static void caseDrainStarts() {
  const std::string song = shortSong(); const PadSet ps;
  float x[256] = {0}, y[256] = {0};
  // D2: create(use_frames = 1, frames = 0): drain is allowed (the output is empty) and it runs the song: set_pad / set_pad_kit are then E_STATE
  { thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = 0;
    thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, nullptr, 0); CHECK("create", r != nullptr);
    CHECK("set_pad before the drain", thunder_render_set_pad(r, 0, ps.pad(0), nullptr, kNS, 48000) == 0);
    const int64_t d = thunder_render_drain(r, 48000 * 30); CHECK("drain runs the song", d > 0);
    CHECK("set_pad after drain is E_STATE", thunder_render_set_pad(r, 1, ps.pad(1), nullptr, kNS, 48000) == THUNDER_RENDER_E_STATE);
    CHECK("set_pad_kit after drain is E_STATE", thunder_render_set_pad_kit(r, 1, 0, 0, 0) == THUNDER_RENDER_E_STATE);
    CHECK("the handle is not dead: read is 0", thunder_render_read(r, x, y, 256) == 0);
    thunder_render_free(r); }
  // M8: drain before the end is E_STATE (never read; part way; and it changes nothing: the render goes on as a handle that never saw the drain)
  { std::vector<float> a, b, a2, b2;
    thunder_render* r = makeApi(song, ps, Cfg(), false, 0); readAll(r, &a, &b); thunder_render_free(r);
    r = makeApi(song, ps, Cfg(), false, 0); CHECK("create", r != nullptr);
    CHECK("drain before the first read is E_STATE", thunder_render_drain(r, 48000) == THUNDER_RENDER_E_STATE);
    CHECK("pads still settable after a refused drain", thunder_render_set_pad(r, 2, ps.pad(2), nullptr, kNS, 48000) == 0);
    CHECK("read 1000", thunder_render_read(r, x, y, 256) == 256);
    a2.insert(a2.end(), x, x + 256); b2.insert(b2.end(), y, y + 256);
    CHECK("drain part way is E_STATE", thunder_render_drain(r, 48000 * 30) == THUNDER_RENDER_E_STATE);
    readAll(r, &a2, &b2);
    CHECK("the render is what it was without the refused drains", a2.size() == a.size() && std::memcmp(a.data(), a2.data(), a.size() * 4) == 0 && std::memcmp(b.data(), b2.data(), b.size() * 4) == 0);
    CHECK("drain at the end is allowed", thunder_render_drain(r, 48000) == 0);
    thunder_render_free(r); }
  // a use_frames handle: drain before its frames are read is E_STATE too
  { thunder_render* r = makeApi(song, ps, Cfg(), true, 48000); CHECK("drain (use_frames, nothing read) is E_STATE", thunder_render_drain(r, 48000 * 30) == THUNDER_RENDER_E_STATE);
    CHECK("read 100", thunder_render_read(r, x, y, 100) == 100); CHECK("drain (use_frames, part way) is E_STATE", thunder_render_drain(r, 48000 * 30) == THUNDER_RENDER_E_STATE); thunder_render_free(r); }
}

// M3: the pad is COPIED: the host's arrays may be overwritten and freed as soon as set_pad returns
static void casePadCopy() {
  const std::string song = shortSong(); const PadSet ps; const Cfg c;
  std::vector<float> a, b, a2, b2;
  thunder_render* r0 = makeApi(song, ps, c, false, 0); readAll(r0, &a, &b); thunder_render_free(r0);
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o;
  thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, nullptr, 0); CHECK("create", r != nullptr);
  for (int p = 0; p < ps.n; p++) {
    float* l = new float[kNS]; float* rr = (p & 1) ? new float[kNS] : nullptr;
    std::memcpy(l, ps.pad(p), kNS * 4); if (rr) std::memcpy(rr, ps.pad((p + 1) % ps.n), kNS * 4);
    CHECK("set_pad", thunder_render_set_pad(r, p, l, rr, kNS, 48000) == 0);
    for (int i = 0; i < kNS; i++) { l[i] = 123.f; if (rr) rr[i] = -456.f; }   // overwritten, then freed
    delete[] l; delete[] rr;
    CHECK("set_pad_kit", thunder_render_set_pad_kit(r, p, PadSet::choke(p), PadSet::syn(p), PadSet::rel(p)) == 0);
  }
  readAll(r, &a2, &b2); thunder_render_free(r);
  CHECK("the render equals the one made from arrays that stayed", a.size() == a2.size() && std::memcmp(a.data(), a2.data(), a.size() * 4) == 0 && std::memcmp(b.data(), b2.data(), b.size() * 4) == 0);
}

// D8 (third-party CloudSeed, read out of bounds at 192 / 384 kHz): the sanitizer build is what sees the bad index; here the render must finish and be finite.
static void caseHighRates() {
  const std::string electro = readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder"); CHECK("ELECTRO read", !electro.empty()); if (electro.empty()) return;
  const PadSet ps;
  for (double sr : {96000.0, 192000.0, 384000.0}) {
    thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.sample_rate = sr; o.effects = 1; o.master_chain = 1; o.use_frames = 1; o.frames = (int64_t)(sr * 8);
    thunder_render* r = thunder_render_create((const uint8_t*)electro.data(), electro.size(), &o, nullptr, 0); CHECK("create", r != nullptr); if (!r) continue;
    installPads(r, ps);
    std::vector<float> L, R; const int64_t n = readAll(r, &L, &R, 65536); double pk = 0; bool fin = true;
    for (float v : L) { if (!std::isfinite(v)) fin = false; pk = std::max(pk, (double)std::fabs(v)); }
    for (float v : R) if (!std::isfinite(v)) fin = false;
    char msg[96]; std::snprintf(msg, sizeof msg, "ELECTRO fx+master at %.0f Hz: %lld frames, finite, peak %.3f", sr, (long long)n, pk);
    std::printf("%s\n", msg);
    CHECK(msg, n == (int64_t)(sr * 8) && fin && pk < 50.0);
    thunder_render_free(r);
  }
}

// length_estimate: the sequencer's song end; exact once the length is known; never a promise
static void caseEstimate() {
  CHECK("null handle", thunder_render_length_estimate(nullptr) == -1);
  const std::string electro = readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder"); const PadSet ps;
  struct S { const char* name; std::string song; Cfg c; } songs[] = {{"short 120", shortSong(120), Cfg()}, {"short 300", shortSong(300), Cfg()}, {"ELECTRO", electro, Cfg()}, {"short fx+master", shortSong(97), [] { Cfg c; c.fx = c.master = true; return c; }()}};
  for (S& s : songs) {
    thunder_render* r = makeApi(s.song, ps, s.c, false, 0); CHECK("create", r != nullptr); if (!r) continue;
    const int64_t e0 = thunder_render_length_estimate(r); CHECK("estimate available", e0 > 0);
    CHECK("estimate before the end is not the length (unknown)", thunder_render_length(r) == -1);
    std::vector<float> x(1000), y(1000); thunder_render_read(r, x.data(), y.data(), 1000);
    CHECK("the estimate does not move as the render goes", thunder_render_length_estimate(r) == e0);
    CHECK("the estimate did not start or disturb the render: position", thunder_render_position(r) == 1000);
    const int64_t n = 1000 + readAll(r, nullptr, nullptr);
    std::printf("%s: estimate %lld, length %lld (the tails: %lld frames)\n", s.name, (long long)e0, (long long)n, (long long)(n - e0));
    CHECK("estimate is not above the length (the tails only add)", e0 <= n);
    CHECK("estimate is within the tails of the length", n - e0 < 20 * 48000);
    CHECK("exact once the render has ended", thunder_render_length_estimate(r) == n);
    thunder_render_free(r);
  }
  { thunder_render* r = makeApi(shortSong(), ps, Cfg(), true, 5000); CHECK("use_frames: the exact length", thunder_render_length_estimate(r) == 5000); thunder_render_free(r); }
  { thunder_render* r = mkMax(shortSong(), Cfg(), 3000); CHECK("capped at max_frames", thunder_render_length_estimate(r) == 3000); thunder_render_free(r); }
  { Cfg c; c.fx = c.master = true; thunder_render* r = makeApi(shortSong(), ps, c, false, 0); const int64_t lat = thunder_render_latency(r), e = thunder_render_length_estimate(r);
    thunder_render* r2 = makeApi(shortSong(), ps, Cfg(), false, 0); const int64_t e2 = thunder_render_length_estimate(r2);
    CHECK("the master chain's latency is trimmed from the estimate", lat > 0 && e == e2 - lat); thunder_render_free(r); thunder_render_free(r2); }
}

int main() {
  Cfg fm; fm.fx = true; fm.master = true;
  caseMaxFrames("short plain", shortSong(), Cfg());
  caseMaxFrames("short fx+master (latency)", shortSong(), fm);
  caseDrainStarts();
  casePadCopy();
  caseEstimate();
  caseHighRates();
  std::printf(g_fail ? "FAILED: %d\n" : "OK\n", g_fail);
  return g_fail ? 1 : 0;
}
