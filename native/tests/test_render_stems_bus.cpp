// U34b S7: stems with a bus (docs/port/U34B-NOTES.md, "Dormant until a chains host exists"). No bus exists through the C API today (no chains host): this test makes one the way a host
// will, a chain hook on the bus track (Mixer::setChain; here a pass-through that adds the sources' sound to the bus), on the handle's own renderer, and checks renderOut's rules
// (index.html:24306-24310, 24228):
//   * the list: the bus is listed although it has no notes (a source plays into it); the pure source has no stem of its own; a track that is no part of the bus keeps its stem
//   * the stem of the bus holds its source (pass's k2); the stems add up to the mix
//   * a pure source asked for alone is heard directly (its bus is not heard in that pass, so it does not exist: routeTrack is for `keep` only, :24258)
//   * mute: a muted source is not in its bus's stem
// It includes render_api.cpp to reach the handle's renderer (the file's types are private to it); the linked copy of thunder_core is not used for those symbols.
#include "render/render_api.cpp"
#include "render_api_common.h"

using namespace u34;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)

static void standIn(void*, int, float* L, float* R, int N, double, const MixFeed* fd, int nf) {
  for (int i = 0; i < N; i++) for (int j = 0; j < nf; j++) { L[i] = L[i] + fd[j].L[i]; R[i] = R[i] + fd[j].R[i]; }
}
// tracks: 0 a source (notes), 1 the bus (no notes), 2 a track on its own (notes). Track 1's chain takes track 0's Track In and puts it out on track 2 (the page's numbers: 1 and 2).
static std::string song(const char* mix0 = "{}") {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0,-1,1]],\"chains\":[[[0,0]],[[1,0]]],\"plen\":[8,8],\"insts\":[";
  for (int i = 0; i < 8; i++) s += std::string(i ? "," : "") + "{\"pad\":" + std::to_string(i) + ",\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}";
  s += "],\"phrases\":[[";
  for (int p = 0; p < 2; p++) { if (p) s += "],["; for (int r = 0; r < 8; r++) { char b[128]; if (r % 2 == 0 && r < 6) std::snprintf(b, sizeof b, "%s[%d,-1,-1,-1,-1,%d,-1,-1,-1,-1,-1,-1]", r ? "," : "", 60 + p, p); else std::snprintf(b, sizeof b, ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); s += b; } }
  s += "]],\"mix\":[" + std::string(mix0) + ",{},{}]";
  s += ",\"chainOn\":[true,true,false],\"chainPatch\":{\"format\":\"chains.patch\",\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"b\",\"type\":\"chainOut\",\"params\":{\"track\":2}}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"b\",\"in\"]}]}}";
  return s;
}
struct Stereo { std::vector<float> L, R; };
// a handle of the song (stem 0: the mix, 1..: a stem) with the bus hook set the way a chains host must: on the bus only when it is heard
static thunder_render* make(const std::string& js, int stem, int64_t frames, const PadSet& ps) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = frames; o.stem = stem;
  thunder_render* r = thunder_render_create((const uint8_t*)js.data(), js.size(), &o, nullptr, 0); if (!r) { std::printf("create failed\n"); g_fail++; return nullptr; }
  installPads(r, ps);
  if (r->R->heardMask() >> 1 & 1u) r->R->mixer().setChain(1, standIn, nullptr);
  return r;
}
static Stereo readAll(thunder_render* r, int64_t n) { Stereo s; s.L.assign((size_t)n, 0); s.R.assign((size_t)n, 0); int64_t got = 0; while (got < n) { const int64_t k = thunder_render_read(r, s.L.data() + got, s.R.data() + got, std::min<int64_t>(4096, n - got)); if (k <= 0) break; got += k; } return s; }
static double peakOf(const Stereo& s) { double p = 0; for (float v : s.L) p = std::max(p, (double)std::fabs(v)); for (float v : s.R) p = std::max(p, (double)std::fabs(v)); return p; }
static std::vector<int> list(thunder_render* r) { int32_t o[40]; const int n = thunder_render_stem_tracks(r, o, 40); std::vector<int> v; for (int i = 0; i < n && i < 40; i++) v.push_back(o[i]); return v; }

int main() {
  const PadSet ps; const int64_t N = 48000;
  const std::string js = song();
  { thunder_render* m = make(js, 0, N, ps);
    CHECK("the bus exists (track 2's chain hook is set and the plan has its source)", m && (m->R->mixer().busMask() >> 1 & 1u) && (m->R->mixer().routedMask() & 1u));
    CHECK("the list: the bus (2) and the track on its own (3); the pure source (1) is in its bus's stem", m && list(m) == std::vector<int>({2, 3}));
    const Stereo mix = readAll(m, N); thunder_render_free(m);
    thunder_render* b = make(js, 2, N, ps); const Stereo bus = readAll(b, N);
    CHECK("the bus stem is heard: heard mask is the bus and its source", b && b->R->heardMask() == 3u); thunder_render_free(b);
    thunder_render* o = make(js, 3, N, ps); const Stereo own = readAll(o, N); thunder_render_free(o);
    double e = 0; for (size_t i = 0; i < mix.L.size(); i++) e = std::max(e, std::max(std::fabs((double)bus.L[i] + own.L[i] - mix.L[i]), std::fabs((double)bus.R[i] + own.R[i] - mix.R[i])));
    std::printf("S7 bus: mix peak %.4f, bus stem peak %.4f, max |mix - (bus + own)| %.3g\n", peakOf(mix), peakOf(bus), e);
    CHECK("the bus stem holds its source (it has sound, though the bus track has no notes)", peakOf(bus) > 0.001);
    CHECK("the bus stem + the other stem is the mix", e <= 1e-6 * peakOf(mix));
    // the pure source alone: its bus is not heard, so it does not exist in that pass and the source plays on its own
    thunder_render* s = make(js, 1, N, ps); const Stereo src = readAll(s, N);
    CHECK("a pure source asked for alone is heard directly", s && !(s->R->mixer().busMask() & 2u) && peakOf(src) > 0.001); thunder_render_free(s);
    // without the bus the source plays on its own too (no hook): the same sound the bus stem has, as the bus is a pass-through
    double d = 0; for (size_t i = 0; i < src.L.size(); i++) d = std::max(d, (double)std::fabs(src.L[i] - bus.L[i]));
    CHECK("pass-through bus: the source heard directly equals the bus stem", d <= 1e-6);
  }
  { // a muted source is not in its bus's stem, and the bus is not listed (nothing plays into it)
    const std::string mj = song("{\"mute\":true}");
    thunder_render* m = make(mj, 0, N, ps); CHECK("muted source: only the track on its own", m && list(m) == std::vector<int>({3})); thunder_render_free(m);
    thunder_render* b = make(mj, 2, N, ps); CHECK("the bus stem of a muted source: heard mask is the bus alone, silent", b && b->R->heardMask() == 2u && peakOf(readAll(b, N)) == 0); thunder_render_free(b);
  }
  { // FU3: bus INTO bus. The page's busStemPlan repeats until nothing is added, so the stem list does not depend on the order buses are visited in (a JS Map walk there, ascending track numbers here).
    // Today's bus_plan cannot make a bus that is also a source (ownIn), so the plan is written by hand: source 0 -> bus 5 -> bus 2 (the downstream bus has the LOWER number: a single ascending pass misses it).
    // Only track 0 has notes. Expected (web): keepT {0} gains 5 (a kept source feeds it), then 2 (5 is kept now); the pure tracks 0 and 5 are inside their bus's stem: the only stem is bus 2 (number 3).
    std::string js = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[8],\"insts\":[";
    for (int i = 0; i < 8; i++) js += std::string(i ? "," : "") + "{\"pad\":" + std::to_string(i) + ",\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}";
    js += "],\"phrases\":[[";
    for (int r = 0; r < 8; r++) { char b[128]; if (r % 2 == 0 && r < 6) std::snprintf(b, sizeof b, "%s[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]", r ? "," : ""); else std::snprintf(b, sizeof b, ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); js += b; }
    js += "]]";
    const std::string plain = js + "}", mutedMid = js + ",\"mix\":[{},{},{},{},{},{\"mute\":true}]}";
    auto plan = [](int src, int mid, int down) { BusPlan P; P.feedMask[mid] = 1u << src; P.feedMask[down] = 1u << mid; P.targets[src] = 1u << mid; P.targets[mid] = 1u << down; P.pure = (1u << src) | (1u << mid);
      P.feeds[mid][0] = (uint8_t)src; P.nFeeds[mid] = P.feedLen[mid] = 1; P.feeds[down][0] = (uint8_t)mid; P.nFeeds[down] = P.feedLen[down] = 1; return P; };
    auto listWith = [&](const std::string& song, const BusPlan& P) { thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = 4800;
      thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, nullptr, 0); std::vector<int> v; if (!r) { g_fail++; return v; }
      installPads(r, ps); r->R->mixer().setPlan(P); v = list(r); thunder_render_free(r); return v; };
    CHECK("bus into bus, the downstream bus has the LOWER number (0 -> 5 -> 2): the only stem is bus 2 (number 3)", listWith(plain, plan(0, 5, 2)) == std::vector<int>({3}));
    CHECK("bus into bus in ascending order (0 -> 2 -> 5): the only stem is bus 5 (number 6)", listWith(plain, plan(0, 2, 5)) == std::vector<int>({6}));
    CHECK("a muted middle bus is not added, so what only it feeds is not either: the source keeps its own stem (0 -> 5 -> 2, 5 muted)", listWith(mutedMid, plan(0, 5, 2)) == std::vector<int>({1}));
  }
  std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ok", g_fail);
  return g_fail ? 1 : 0;
}
