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
#include "chains/chost.h"

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
static thunder_render* make(const std::string& js, int stem, int64_t frames, const PadSet& ps, bool live = false, bool real = false) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = frames; o.stem = stem; o.chains = real ? 1 : 0;   // real: the C1 ChainsHost (opts.chains) instead of the stand-in
  thunder_render* r = thunder_render_create((const uint8_t*)js.data(), js.size(), &o, nullptr, 0); if (!r) { std::printf("create failed\n"); g_fail++; return nullptr; }
  installPads(r, ps);
  if (real) return r;
  const uint32_t heard = r->R->heardMask();
  for (int t = 0; t < 8; t++) if ((live || (heard >> t & 1u)) && r->R->mixer().plan().feedMask[t]) r->R->mixer().setChain(t, standIn, nullptr);
  return r;
}
// a render that stops before the requested frame count is a counted failure (never a zero-padded buffer that passes a silence check)
static Stereo readAll(thunder_render* r, int64_t n) { Stereo s; s.L.assign((size_t)n, 0); s.R.assign((size_t)n, 0); int64_t got = 0; while (got < n) { const int64_t k = thunder_render_read(r, s.L.data() + got, s.R.data() + got, std::min<int64_t>(4096, n - got)); if (k <= 0) break; got += k; }
  if (got != n) { std::printf("FAIL readAll: the render stopped after %lld of %lld frames (line %d)\n", (long long)got, (long long)n, __LINE__); g_fail++; }
  return s; }
static Stereo render(const std::string& js, int stem, int64_t N, const PadSet& ps, bool live = false, bool real = false) { thunder_render* r = make(js, stem, N, ps, live, real); Stereo s; if (r) { s = readAll(r, N); thunder_render_free(r); } return s; }
static double peakOf(const Stereo& s) { double p = 0; for (float v : s.L) p = std::max(p, (double)std::fabs(v)); for (float v : s.R) p = std::max(p, (double)std::fabs(v)); return p; }
// max |a - k b| over both channels
static double diff(const Stereo& a, const Stereo& b, double k) { double e = 0; for (size_t i = 0; i < a.L.size() && i < b.L.size(); i++) e = std::max(e, std::max(std::fabs(a.L[i] - k * b.L[i]), std::fabs(a.R[i] - k * b.R[i]))); return e; }
static std::vector<int> list(thunder_render* r) { int32_t o[40]; const int n = thunder_render_stem_tracks(r, o, 40); std::vector<int> v; for (int i = 0; i < n && i < 40; i++) v.push_back(o[i]); return v; }

// A general song builder for the cases below: `row` is the song row (which tracks play chain 0), `chainOn` the CHN flags (8 tracks), `patch` the chains patch ("" = none), `mix` the mix array's entries.
// Tracks 0-based; the patch below uses the page's 1-based numbers. Notes are the same eight-row phrase as above.
struct TI { int t; std::vector<int> outs; };
static std::string patchOf(const std::vector<TI>& v) {
  std::string m, c; std::vector<int> cos;
  for (size_t i = 0; i < v.size(); i++) {
    m += std::string(m.empty() ? "" : ",") + "{\"id\":\"ti" + std::to_string(i) + "\",\"type\":\"trackIn\",\"params\":{\"track\":" + std::to_string(v[i].t) + "}}";
    for (int o : v[i].outs) { bool have = false; for (int x : cos) have |= x == o; if (!have) cos.push_back(o);
      c += std::string(c.empty() ? "" : ",") + "{\"from\":[\"ti" + std::to_string(i) + "\",\"out\"],\"to\":[\"co" + std::to_string(o) + "\",\"in\"]}"; }
  }
  for (int o : cos) m += ",{\"id\":\"co" + std::to_string(o) + "\",\"type\":\"chainOut\",\"params\":{\"track\":" + std::to_string(o) + "}}";
  return "{\"format\":\"chains.patch\",\"modules\":[" + m + "],\"cables\":[" + c + "]}";
}
static std::string gsong(const char* row, const std::vector<int>& chainOn, const std::string& patch, const std::string& mix) {
  std::string s = std::string("{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[") + row + "],\"chains\":[[[0,0]]],\"plen\":[8],\"insts\":[";
  for (int i = 0; i < 8; i++) s += std::string(i ? "," : "") + "{\"pad\":" + std::to_string(i) + ",\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}";
  s += "],\"phrases\":[[";
  for (int r = 0; r < 8; r++) { char b[128]; if (r % 2 == 0 && r < 6) std::snprintf(b, sizeof b, "%s[%d,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]", r ? "," : "", 60 + r); else std::snprintf(b, sizeof b, ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); s += b; }
  s += "]],\"mix\":[" + mix + "]";
  if (!patch.empty()) { s += ",\"chainOn\":["; for (size_t i = 0; i < chainOn.size(); i++) s += std::string(i ? "," : "") + (chainOn[i] ? "true" : "false"); s += "],\"chainPatch\":" + patch; }
  return s + "}";
}

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
  // C1: the REAL chains host (opts.chains, a Track In -> Chain Out patch run by the engine) gives the stand-in's mix, the stems and the heard masks: the stand-in can be replaced by it
  { const Stereo real = render(js, 0, N, ps, false, true);
    std::printf("real chains host: max |real - stand-in| %.3g (stand-in peak %.4f)\n", diff(real, mix, 1), peakOf(mix));
    CHECK("the real host plays the two-bus chain as the stand-in does", diff(real, mix, 1) <= 1e-6 * pk);
    CHECK("real host, stem of the last bus = the mix", diff(render(js, 3, N, ps, false, true), mix, 1) <= 1e-6 * pk);
    CHECK("real host, stem of the middle bus = the source times its fader", diff(render(js, 2, N, ps, false, true), ref, gA) <= 1e-6 * pk);
    thunder_render* a = make(js, 2, N, ps, false, true); CHECK("real host: stem(middle bus) hooks only heard tracks", a && a->R->chainsHost() && a->R->chainsHost()->node(1) != nullptr && a->R->chainsHost()->node(2) == nullptr && a->R->chainsHost()->node(0) == nullptr); if (a) thunder_render_free(a); }
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
  // CodeRabbit on #266: THREE buses, 4 -> 3 -> 2 -> 1 (the page's numbers; tracks 3, 2, 1, 0 here). The source has the HIGHEST number and the last bus the lowest, so the stem list needs more than one
  // expansion pass and the audio crosses three buses: still no block of delay (the same sample positions as the source alone) and the mix is the source times the three faders.
  { const int vC = 112, vB = 144, vA = 176;   // faders of the buses 3, 2, 1 (tracks 2, 1, 0)
    const double gC = mix_track_gain(vC), gB3 = mix_track_gain(vB), gA3 = mix_track_gain(vA);
    const std::string patch = patchOf({{4, {3}}, {3, {2}}, {2, {1}}});
    const std::vector<int> on = {1, 1, 1, 1, 0, 0, 0, 0};
    const std::string mixJ = "{\"vol\":" + std::to_string(vA) + "},{\"vol\":" + std::to_string(vB) + "},{\"vol\":" + std::to_string(vC) + "},{}";
    const std::string alone3 = gsong("[-1,-1,-1,0]", on, "", "{},{},{},{}"), chain3 = gsong("[-1,-1,-1,0]", on, patch, mixJ);
    const Stereo r0 = render(alone3, 0, N, ps), m3 = render(chain3, 0, N, ps);
    const double e = diff(m3, r0, gA3 * gB3 * gC), p0 = peakOf(r0);
    std::printf("three-bus chain 4 -> 3 -> 2 -> 1: source alone peak %.4f, chain peak %.4f, max |mix - alone * gA * gB * gC| %.3g\n", p0, peakOf(m3), e);
    CHECK("three buses: the source alone has sound", p0 > 0.01);
    CHECK("three buses: the mix is the source times the three faders, sample for sample (the impulse reaches the last bus in the SAME block: no block of delay per bus)", e <= 1e-6 * p0);
    // the first sound sits at the same frame as the source alone's (a block of delay would move it by at least one block of the mixer)
    auto firstNonZero = [](const Stereo& s) { for (size_t i = 0; i < s.L.size(); i++) if (std::fabs(s.L[i]) > 1e-9f || std::fabs(s.R[i]) > 1e-9f) return (long long)i; return -1LL; };
    CHECK("three buses: the first sample of sound is at the same frame as the source alone", firstNonZero(r0) >= 0 && firstNonZero(m3) == firstNonZero(r0));
    thunder_render* m = make(chain3, 0, N, ps);
    CHECK("three buses: the plan (4 feeds 3, 3 feeds 2, 2 feeds 1; the three upper ones are pure)", m && m->R->mixer().plan().feedMask[2] == 8u && m->R->mixer().plan().feedMask[1] == 4u && m->R->mixer().plan().feedMask[0] == 2u && m->R->mixer().plan().pure == 14u);
    CHECK("three buses: the stem list reaches the FINAL bus (number 1) and nothing else", m && list(m) == std::vector<int>({1})); if (m) thunder_render_free(m);
    const Stereo st = render(chain3, 1, N, ps);
    CHECK("three buses: the stem of the final bus = the mix", diff(st, m3, 1) <= 1e-9);
    thunder_render* h = make(chain3, 1, N, ps); CHECK("three buses: setStem(final bus): heard = the source and all three buses", h && h->R->heardMask() == 15u); if (h) thunder_render_free(h); }
  // CodeRabbit "major" on #266, PINNED: a NESTED SEND's direct signal is counted in two stems. This is the WEB'S OWN known stems double-count (beta build 660), and native matches the web on purpose.
  // It must change TOGETHER with the web's fix, not before: when the web's stems change, this test is the one to update. Patch (page numbers): track 1 -> its own Chain Out (1) and -> bus 2;
  // bus 2 -> bus 3; notes on track 1 only. Track 1 is a send (it keeps its own out), bus 2 is pure. Stems listed: track 1 (the send's own stem) and bus 3 (its stem takes the send with it,
  // direct path on). So stem 1 + stem 3 holds the send's direct signal twice: stem 1 + stem 3 = mix + the source's direct signal.
  { const std::string patch = patchOf({{1, {1, 2}}, {2, {3}}});
    const std::string js = gsong("[0]", {1, 1, 1, 0, 0, 0, 0, 0}, patch, "{},{},{}");
    const std::string alone = gsong("[0]", {1, 1, 1, 0, 0, 0, 0, 0}, "", "{},{},{}");
    const Stereo ref = render(alone, 0, N, ps), mixS = render(js, 0, N, ps), s1 = render(js, 1, N, ps), s3 = render(js, 3, N, ps);
    thunder_render* m = make(js, 0, N, ps);
    CHECK("nested send: the plan (1 -> 2 -> 3; the send 1 is not pure, bus 2 is)", m && m->R->mixer().plan().feedMask[1] == 1u && m->R->mixer().plan().feedMask[2] == 2u && m->R->mixer().plan().pure == 2u);
    CHECK("nested send (the web's behaviour at beta build 660): the stems listed are 1 (the send) and 3 (the last bus); bus 2 is inside bus 3's stem", m && list(m) == std::vector<int>({1, 3})); if (m) thunder_render_free(m);
    double e = 0; for (size_t i = 0; i < mixS.L.size(); i++) e = std::max(e, std::max(std::fabs((double)s1.L[i] + s3.L[i] - mixS.L[i] - ref.L[i]), std::fabs((double)s1.R[i] + s3.R[i] - mixS.R[i] - ref.R[i])));
    std::printf("nested send: source alone peak %.4f, mix peak %.4f, stem 1 peak %.4f, stem 3 peak %.4f, max |stem 1 + stem 3 - mix - direct| %.3g\n", peakOf(ref), peakOf(mixS), peakOf(s1), peakOf(s3), e);
    CHECK("nested send: the stem of track 1 is its direct signal", diff(s1, ref, 1) <= 1e-6 * peakOf(ref));
    CHECK("nested send (the web's double-count): stem 1 + stem 3 = the mix + the send's direct signal, so the direct signal is in the stems twice", peakOf(ref) > 0.01 && e <= 1e-6 * peakOf(ref)); }
  std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ok", g_fail);
  return g_fail ? 1 : 0;
}
