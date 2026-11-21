// VERIFY-266: a middle bus that also has notes. docs/port/BUS-INTO-BUS-NOTES.md named this test and did not have it. The web's makeSeq drops the notes of every track in busT (feeds.has(t),
// core/seq-core.js beta :274, :648, :689), so with the chain 1 -> 2 -> 3 a note on track 2 (the middle bus) or track 3 (the last bus) must not be heard, and the mix is the same
// as with notes on track 1 alone. Before bus into bus a track with a Track In was never a bus, so its notes played: that is what changed. The renders share test_render_bus_chain's
// chain (analytic reference there); here the check is "the extra notes change nothing", with a control that they DO sound when the chain is off.
#include "render/render_api.cpp"
#include "render_api_common.h"

using namespace u34;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)

static void standIn(void*, int, float* L, float* R, int N, double, const MixFeed* fd, int nf) {
  for (int i = 0; i < N; i++) for (int j = 0; j < nf; j++) { L[i] = L[i] + fd[j].L[i]; R[i] = R[i] + fd[j].R[i]; }
}
static const int kVolA = 144, kVolB = 176;   // the buses' faders (E0 = 224 is unity)
// tracks (0-based): 0 notes; 1 bus A (fed by 0, feeds 2); 2 bus B (fed by 1). `chain` false: no patch (the source alone, as the reference)
static std::string song(bool chain, const char* m0 = "{}", const char* m1 = nullptr, const char* m2 = nullptr, const char* row = "[0,-1,-1]") {
  std::string s = std::string("{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[") + row + "],\"chains\":[[[0,0]]],\"plen\":[8],\"insts\":[";
  for (int i = 0; i < 8; i++) s += std::string(i ? "," : "") + "{\"pad\":" + std::to_string(i) + ",\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}";
  s += "],\"phrases\":[[";
  for (int r = 0; r < 8; r++) { char b[128]; if (r % 2 == 0 && r < 6) std::snprintf(b, sizeof b, "%s[%d,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]", r ? "," : "", 60 + r); else std::snprintf(b, sizeof b, ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); s += b; }
  char mx[256]; std::snprintf(mx, sizeof mx, "]],\"mix\":[%s,%s,%s]", m0, m1 ? m1 : (chain ? ("{\"vol\":" + std::to_string(kVolA) + "}").c_str() : "{}"), m2 ? m2 : "{}");
  s += mx;
  if (chain) s += ",\"chainOn\":[true,true,true],\"chainPatch\":{\"format\":\"chains.patch\",\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"oa\",\"type\":\"chainOut\",\"params\":{\"track\":2}},{\"id\":\"b\",\"type\":\"trackIn\",\"params\":{\"track\":2}},{\"id\":\"ob\",\"type\":\"chainOut\",\"params\":{\"track\":3}}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"oa\",\"in\"]},{\"from\":[\"b\",\"out\"],\"to\":[\"ob\",\"in\"]}]}";
  return s + "}";
}
struct Stereo { std::vector<float> L, R; };
// live: a hook on every bus (a live host: a muted bus still exists, busSync); not live: only on the heard ones (the web's offline pass(): routeTrack for `keep` only)
static thunder_render* make(const std::string& js, int stem, int64_t frames, const PadSet& ps, bool live = false) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = frames; o.stem = stem;
  thunder_render* r = thunder_render_create((const uint8_t*)js.data(), js.size(), &o, nullptr, 0); if (!r) { std::printf("create failed\n"); g_fail++; return nullptr; }
  installPads(r, ps);
  const uint32_t heard = r->R->heardMask();
  for (int t = 0; t < 8; t++) if ((live || (heard >> t & 1u)) && r->R->mixer().plan().feedMask[t]) r->R->mixer().setChain(t, standIn, nullptr);
  return r;
}
static Stereo readAll(thunder_render* r, int64_t n) { Stereo s; s.L.assign((size_t)n, 0); s.R.assign((size_t)n, 0); int64_t got = 0; while (got < n) { const int64_t k = thunder_render_read(r, s.L.data() + got, s.R.data() + got, std::min<int64_t>(4096, n - got)); if (k <= 0) break; got += k; }
  if (got != n) { std::printf("FAIL readAll: the render stopped after %lld of %lld frames (line %d)\n", (long long)got, (long long)n, __LINE__); g_fail++; }
  return s; }
static Stereo render(const std::string& js, int stem, int64_t N, const PadSet& ps, bool live = false) { thunder_render* r = make(js, stem, N, ps, live); Stereo s; if (r) { s = readAll(r, N); thunder_render_free(r); } return s; }
static double peakOf(const Stereo& s) { double p = 0; for (float v : s.L) p = std::max(p, (double)std::fabs(v)); for (float v : s.R) p = std::max(p, (double)std::fabs(v)); return p; }
// max |a - k b| over both channels
static double diff(const Stereo& a, const Stereo& b, double k) { double e = 0; for (size_t i = 0; i < a.L.size() && i < b.L.size(); i++) e = std::max(e, std::max(std::fabs(a.L[i] - k * b.L[i]), std::fabs(a.R[i] - k * b.R[i]))); return e; }
static std::vector<int> list(thunder_render* r) { int32_t o[40]; const int n = thunder_render_stem_tracks(r, o, 40); std::vector<int> v; for (int i = 0; i < n && i < 40; i++) v.push_back(o[i]); return v; }

int main() {
  const PadSet ps; const int64_t N = 48000;
  const std::string vB = "{\"vol\":" + std::to_string(kVolB) + "}";
  const std::string base = song(true, "{}", nullptr, vB.c_str());                       // notes on track 1 only
  const std::string n2 = song(true, "{}", nullptr, vB.c_str(), "[0,0,-1]");             // + notes on track 2, the MIDDLE bus
  const std::string n3 = song(true, "{}", nullptr, vB.c_str(), "[0,-1,0]");             // + notes on track 3, the LAST bus
  const std::string n23 = song(true, "{}", nullptr, vB.c_str(), "[0,0,0]");             // notes on all three
  const Stereo mix = render(base, 0, N, ps);
  CHECK("the chain's mix has sound", peakOf(mix) > 0.01);
  CHECK("notes on the middle bus are dropped: the mix is the same sample for sample", diff(render(n2, 0, N, ps), mix, 1) <= 1e-9);
  CHECK("notes on the last bus are dropped too", diff(render(n3, 0, N, ps), mix, 1) <= 1e-9);
  CHECK("notes on all three: still the same mix", diff(render(n23, 0, N, ps), mix, 1) <= 1e-9);
  // control: with NO chain, tracks 2 and 3 are plain tracks and their notes sound (so the equalities above are not vacuous)
  { const Stereo a = render(song(false, "{}", nullptr, nullptr, "[0,-1,-1]"), 0, N, ps), b = render(song(false, "{}", nullptr, nullptr, "[0,0,-1]"), 0, N, ps);
    CHECK("control, chain off: notes on track 2 DO sound (the mix differs from track 1 alone)", diff(b, a, 1) > 0.01 * peakOf(a)); }
  // the stems: only the last bus, whatever tracks have notes (a bus has no stem of its own for notes it does not play)
  { thunder_render* r = make(n23, 0, N, ps); CHECK("stem list with notes on all three: the last bus only", r && list(r) == std::vector<int>({3})); if (r) thunder_render_free(r); }
  { thunder_render* r = make(n2, 0, N, ps); CHECK("stem list with notes on 1 and 2: the last bus only", r && list(r) == std::vector<int>({3})); if (r) thunder_render_free(r); }
  // the last bus's stem is the mix; the middle bus's stem is still the source through bus A (its own notes do not add)
  { const double gA = mix_track_gain(kVolA);
    CHECK("stem of the last bus (notes on all three) = the mix", diff(render(n23, 3, N, ps), mix, 1) <= 1e-9);
    const Stereo ref = render(song(false), 0, N, ps);
    CHECK("stem of the middle bus (notes on 1 and 2) = the source times its fader", diff(render(n2, 2, N, ps), ref, gA) <= 1e-6 * peakOf(ref)); }
  std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ok", g_fail);
  return g_fail ? 1 : 0;
}
