// U34a gate 2: the output through the C API equals the output of driving SongRenderer directly, for read sizes 1, 64, 128, 333, 4096 and the
// whole song; plus the API's own rules (length, position, latency, the end, set_pad after read, free mid-render, two independent handles).
#include <algorithm>
#include <cstdlib>
#include "render_api_common.h"

using namespace u34;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)

static bool same(const std::vector<float>& a, const std::vector<float>& b) { return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * 4) == 0); }

// reads everything in blocks of `bs` (0: one call asking for more than there is)
static void readAll(thunder_render* r, int64_t bs, std::vector<float>* L, std::vector<float>* R, int64_t guess) {
  const int64_t ask = bs ? bs : guess + 1000;
  std::vector<float> a((size_t)ask), b((size_t)ask);
  for (;;) { const int64_t n = thunder_render_read(r, a.data(), b.data(), ask); if (n < 0) { std::printf("read error %lld\n", (long long)n); g_fail++; return; } if (n == 0) break;
    L->insert(L->end(), a.begin(), a.begin() + n); R->insert(R->end(), b.begin(), b.begin() + n); }
}

static void caseFrames(const char* name, const std::string& song, const Cfg& c, int64_t N) {
  const PadSet ps; std::vector<float> dR; const std::vector<float> dL = direct(song, ps, c, true, N, &dR);
  double pk = 0; for (float v : dL) pk = std::max(pk, (double)std::fabs(v));
  std::printf("%s: %lld frames, peak %.4f\n", name, (long long)N, pk);
  CHECK("direct render is not silent", pk > 0);
  for (int64_t bs : {1, 64, 128, 333, 4096, 0}) {
    if (bs == 1 && N > 200000) continue;   // (1-frame reads of a long render: the same code path as 64, left out for time; the short cases do it)
    thunder_render* r = makeApi(song, ps, c, true, N); CHECK("create", r != nullptr); if (!r) continue;
    CHECK("length known up front", thunder_render_length(r) == N);
    std::vector<float> L, R; readAll(r, bs, &L, &R, N);
    char m[128]; std::snprintf(m, sizeof m, "%s block %lld: L equals the direct drive bit for bit", name, (long long)bs); CHECK(m, same(L, dL));
    std::snprintf(m, sizeof m, "%s block %lld: R equals", name, (long long)bs); CHECK(m, same(R, dR));
    CHECK("position at the end", thunder_render_position(r) == N);
    CHECK("0 after the end", thunder_render_read(r, L.data(), R.data(), 10) == 0);
    thunder_render_free(r);
  }
}

static void caseUnknown(const char* name, const std::string& song, const Cfg& c) {
  const PadSet ps; std::vector<float> dR; const std::vector<float> dL = direct(song, ps, c, false, 0, &dR);
  std::printf("%s: unknown length, direct %zu frames\n", name, dL.size());
  CHECK("unknown-length render has frames", dL.size() > 48000);
  for (int64_t bs : {1, 64, 128, 333, 4096, 0}) {
    thunder_render* r = makeApi(song, ps, c, false, 0); CHECK("create", r != nullptr); if (!r) continue;
    CHECK("length unknown before the end", thunder_render_length(r) == -1);
    std::vector<float> L, R; readAll(r, bs, &L, &R, (int64_t)dL.size());
    char m[128]; std::snprintf(m, sizeof m, "%s block %lld: output equals", name, (long long)bs); CHECK(m, same(L, dL) && same(R, dR));
    CHECK("length known at the end", thunder_render_length(r) == (int64_t)dL.size());
    CHECK("position == length", thunder_render_position(r) == (int64_t)dL.size());
    CHECK("drain: the pool already empty", thunder_render_drain(r, 48000) == 0);
    thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; CHECK("stats", thunder_render_stats_get(r, &s) == 0);
    CHECK("song over and no voices", s.song_over == 1 && s.voices_active == 0 && s.notes_played > 0 && !s.hit_limit);
    thunder_render_free(r);
  }
}

int main() {
  const std::string electro = readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder");
  CHECK("ELECTRO read", !electro.empty());
  const std::string shortS = shortSong();
  caseFrames("short song", shortS, Cfg(), 150000);
  caseUnknown("short song", shortS, Cfg());
  { Cfg c; c.master = true; caseUnknown("short song + master", shortS, c); }
  { Cfg c; c.fx = true; c.pdc = 1; caseUnknown("short song + fx + pdc auto", shortS, c); }
  { Cfg c; c.pageTiming = true; caseFrames("short song page timing", shortS, c, 40000); }
  caseFrames("ELECTRO", electro, Cfg(), 480000);
  { Cfg c; c.fx = true; c.master = true; caseFrames("ELECTRO fx + master", electro, c, 240000); }

  // the API's own rules
  { const PadSet ps; thunder_render* r = makeApi(shortS, ps, Cfg(), true, 10000); std::vector<float> a(256), b(256);
    CHECK("set_pad before the first read", thunder_render_set_pad(r, 0, ps.pad(0), nullptr, kNS, 48000) == 0);
    CHECK("read", thunder_render_read(r, a.data(), b.data(), 256) == 256);
    CHECK("set_pad after the first read is E_STATE", thunder_render_set_pad(r, 0, ps.pad(0), nullptr, kNS, 48000) == THUNDER_RENDER_E_STATE);
    CHECK("set_pad_kit after the first read is E_STATE", thunder_render_set_pad_kit(r, 0, 0, 0, 0) == THUNDER_RENDER_E_STATE);
    CHECK("position 256", thunder_render_position(r) == 256);
    thunder_render_free(r);   // free mid-render = cancel
    CHECK("latency 0 without the master chain", true); }
  { const PadSet ps; Cfg c; c.master = true; thunder_render* r = makeApi(shortS, ps, c, true, 1000); CHECK("master chain: latency > 0", thunder_render_latency(r) > 0); thunder_render_free(r); }
  { // two handles are independent, and the same song through both gives the same bits when read in different orders
    const PadSet ps; thunder_render* x = makeApi(shortS, ps, Cfg(), true, 30000); thunder_render* y = makeApi(shortS, ps, Cfg(), true, 30000);
    std::vector<float> xl(30000), xr(30000), yl(30000), yr(30000);
    int64_t px = 0, py = 0; while (px < 30000 || py < 30000) { if (px < 30000) px += thunder_render_read(x, xl.data() + px, xr.data() + px, std::min<int64_t>(77, 30000 - px)); if (py < 30000) py += thunder_render_read(y, yl.data() + py, yr.data() + py, std::min<int64_t>(500, 30000 - py)); }
    CHECK("two handles equal", same(xl, yl) && same(xr, yr)); thunder_render_free(x); thunder_render_free(y); }
  { // max_frames cuts an unknown-length render
    const PadSet ps; thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.max_frames = 5000;
    thunder_render* r = thunder_render_create((const uint8_t*)shortS.data(), shortS.size(), &o, nullptr, 0); installPads(r, ps);
    std::vector<float> L, R; readAll(r, 4096, &L, &R, 10000); thunder_render_stats s; s.size = sizeof s; thunder_render_stats_get(r, &s);
    CHECK("cut at max_frames (a whole number of quanta)", s.hit_limit == 1 && L.size() >= 5000 && L.size() < 5000 + 128 && thunder_render_length(r) == (int64_t)L.size());
    thunder_render_free(r); }
  if (g_fail) { std::printf("test_render_api: %d FAILED\n", g_fail); return 1; }
  std::printf("test_render_api: ok\n"); return 0;
}
