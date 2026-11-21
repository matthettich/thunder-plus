// BUS INTO BUS (web build 651; docs/port/BUS-INTO-BUS-NOTES.md): a two-bus chain through the offline renderer. Track 1 plays notes; its Track In goes to Chain Out 2
// (bus A, track 2) whose own Track In goes to Chain Out 3 (bus B, track 3): 1 -> 2 -> 3, the middle bus is a source and a bus. The chain hooks are the stand-in the
// stems test uses (a pass-through that adds the sources' sound to the bus), set on every heard bus as a chains host must (pass(): routeTrack for `keep`).
//
// THE REFERENCE is analytic, from the web's own laws, not a second implementation of the mixer: with pass-through buses the web's graph is
//   source strip -> (pure: leaves `out` for its bus) -> bus A's chains node -> A's strip (VOL gA) -> (A is pure too) -> bus B's node -> B's strip (VOL gB) -> out
// so the mix is the source rendered ALONE (no patch: the same voice, strip and master) times gA * gB, with trackGain() the web's law (index.html trackGain, checked by test_mix
// against the page's strips). A browser was not available to regenerate a web render of the chain (see the notes), so no web-rendered golden is committed.
// Also: no block of delay in a chain (the same sample positions as the source alone), a muted middle bus silences the mix, solo follows the chain, and the stems.
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
static std::string song(bool chain, const char* m0 = "{}", const char* m1 = nullptr, const char* m2 = nullptr) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0,-1,-1]],\"chains\":[[[0,0]]],\"plen\":[8],\"insts\":[";
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
static Stereo readAll(thunder_render* r, int64_t n) { Stereo s; s.L.assign((size_t)n, 0); s.R.assign((size_t)n, 0); int64_t got = 0; while (got < n) { const int64_t k = thunder_render_read(r, s.L.data() + got, s.R.data() + got, std::min<int64_t>(4096, n - got)); if (k <= 0) break; got += k; } return s; }
static Stereo render(const std::string& js, int stem, int64_t N, const PadSet& ps, bool live = false) { thunder_render* r = make(js, stem, N, ps, live); Stereo s; if (r) { s = readAll(r, N); thunder_render_free(r); } return s; }
static double peakOf(const Stereo& s) { double p = 0; for (float v : s.L) p = std::max(p, (double)std::fabs(v)); for (float v : s.R) p = std::max(p, (double)std::fabs(v)); return p; }
// max |a - k b| over both channels
static double diff(const Stereo& a, const Stereo& b, double k) { double e = 0; for (size_t i = 0; i < a.L.size() && i < b.L.size(); i++) e = std::max(e, std::max(std::fabs(a.L[i] - k * b.L[i]), std::fabs(a.R[i] - k * b.R[i]))); return e; }
static std::vector<int> list(thunder_render* r) { int32_t o[40]; const int n = thunder_render_stem_tracks(r, o, 40); std::vector<int> v; for (int i = 0; i < n && i < 40; i++) v.push_back(o[i]); return v; }

int main() {
  const PadSet ps; const int64_t N = 48000;
  const double gA = mix_track_gain(kVolA), gB = mix_track_gain(kVolB);
  const std::string alone = song(false), js = song(true, "{}", nullptr, ("{\"vol\":" + std::to_string(kVolB) + "}").c_str());
  const Stereo ref = render(alone, 0, N, ps), mix = render(js, 0, N, ps);
  const double pk = peakOf(ref);
  CHECK("the reference (the source alone) has sound", pk > 0.01);
  { const double e = diff(mix, ref, gA * gB);
    std::printf("two-bus chain: source alone peak %.4f, chain peak %.4f, max |mix - alone * gA * gB| %.3g (gA %.4f gB %.4f)\n", pk, peakOf(mix), e, gA, gB);
    CHECK("the chain's mix is the source alone times both buses' faders, sample for sample (no block of delay between the buses)", e <= 1e-6 * pk);
    CHECK("the middle bus does not also reach the master (its sound is heard once)", peakOf(mix) < pk * 1.0001 * gA * gB + 1e-6); }
  { thunder_render* m = make(js, 0, N, ps);
    const BusPlan& P = m->R->mixer().plan();
    CHECK("plan: 1 feeds 2, 2 feeds 3; the middle bus is pure", m && P.feedMask[1] == 1u && P.feedMask[2] == 2u && P.pure == 3u);
    CHECK("the stem list: the last bus only", m && list(m) == std::vector<int>({3})); thunder_render_free(m); }
  // transitive solo: soloing the source keeps both buses; a mute still wins; solo on the last bus keeps everything upstream
  { Stereo s0 = render(song(true, "{\"solo\":true}", nullptr, ("{\"vol\":" + std::to_string(kVolB) + "}").c_str()), 0, N, ps);
    CHECK("solo on the source: both buses stay (the chain is heard)", diff(s0, mix, 1) <= 1e-9);
    Stereo s2 = render(song(true, "{}", nullptr, ("{\"solo\":true,\"vol\":" + std::to_string(kVolB) + "}").c_str()), 0, N, ps);
    CHECK("solo on the last bus: the whole chain upstream stays", diff(s2, mix, 1) <= 1e-9);
    Stereo s1 = render(song(true, "{}", ("{\"solo\":true,\"vol\":" + std::to_string(kVolA) + "}").c_str(), ("{\"vol\":" + std::to_string(kVolB) + "}").c_str()), 0, N, ps);
    CHECK("solo on the middle bus: its source and the bus after it stay", diff(s1, mix, 1) <= 1e-9);
    Stereo sm = render(song(true, "{\"solo\":true}", nullptr, ("{\"mute\":true,\"vol\":" + std::to_string(kVolB) + "}").c_str()), 0, N, ps, true);
    CHECK("solo on the source with the last bus muted: silent (mute wins; the buses exist)", peakOf(sm) == 0); }
  // a muted middle bus is heard only through the next bus, so a host that keeps the buses (live) is silent ...
  { const std::string mj = song(true, "{}", ("{\"mute\":true,\"vol\":" + std::to_string(kVolA) + "}").c_str(), ("{\"vol\":" + std::to_string(kVolB) + "}").c_str());
    CHECK("a muted middle bus (buses kept): nothing reaches the master", peakOf(render(mj, 0, N, ps, true)) == 0);
    // ... and the web's offline pass routes only the heard tracks, so there the source of a muted bus plays directly (the quirk noted in the spec; native does the same, not fixed)
    CHECK("offline, as the web: the source of a muted middle bus is heard directly", diff(render(mj, 0, N, ps), ref, 1) <= 1e-6 * pk); }
  // stems: the bus B stem is the mix; the middle bus's stem is the source through bus A alone (bus B is not heard); the source's own stem is the source alone
  { const Stereo sb = render(js, 3, N, ps);
    CHECK("stem of the last bus = the mix", diff(sb, mix, 1) <= 1e-9);
    const Stereo sa = render(js, 2, N, ps);
    thunder_render* a = make(js, 2, N, ps); const uint32_t hm = a ? a->R->heardMask() : 0; if (a) thunder_render_free(a);
    CHECK("setStem(middle bus): heard = it and its source, not the bus after it", hm == 3u);
    CHECK("stem of the middle bus = the source times its fader", diff(sa, ref, gA) <= 1e-6 * pk);
    thunder_render* s = make(js, 1, N, ps); const uint32_t h1 = s ? s->R->heardMask() : 0; if (s) thunder_render_free(s);
    CHECK("setStem(source): heard = the source alone", h1 == 1u);
    thunder_render* b = make(js, 3, N, ps); const uint32_t hb = b ? b->R->heardMask() : 0; if (b) thunder_render_free(b);
    CHECK("setStem(last bus): heard = the whole chain (any depth)", hb == 7u); }
  { // a muted middle bus does not pull in what only feeds it: the last bus's stem holds the last bus and nothing else
    const std::string mj = song(true, "{}", ("{\"mute\":true,\"vol\":" + std::to_string(kVolA) + "}").c_str());
    thunder_render* b = make(mj, 3, N, ps); const uint32_t hb = b ? b->R->heardMask() : 0; if (b) thunder_render_free(b);
    CHECK("busClosure: a muted middle bus is not added, so its source is not either", hb == 4u);
    // (NOT fixed, as the web: "a pure source whose only bus is muted still gets a stem": pinned here so a change is noticed)
    thunder_render* m = make(mj, 0, N, ps); CHECK("current behaviour: the source of a muted bus still gets a stem of its own (the web's known quirk, left as is)", m && list(m) == std::vector<int>({1})); if (m) thunder_render_free(m); }
  std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ok", g_fail);
  return g_fail ? 1 : 0;
}
