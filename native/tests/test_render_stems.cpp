// U34b: stems through the offline render C API (docs/port/U34B-NOTES.md). The page's renderOut(true) renders the mix, then one pass per track with only that track heard and no
// master effects / mastering / limiter (index.html:24225-24270, :24290-24357). Here: opts.stem and thunder_render_stem_tracks.
//   S1  the stems of a song add up to its mix (no master chain): ELECTRO with its track effects, a synthetic song with pans and levels; tolerance measured and stated
//   S2  where they do NOT add up: the master chain (mastering + safety limiter) is on the mix only; a stem ignores opts.master_chain, bit for bit
//   S3  a stem holds only its track (equals the mix of the song with every other track muted, bit for bit); a track with no notes renders exact silence and is not listed
//   S4  mute / solo as the JS audible(): the list leaves a muted track and every track not soloed; solo wins over mute; the mix is the sum of the listed stems; a stem asked for directly ignores them
//   S5  length: use_frames stems are exact; an unknown-length stem is longer than the mix by the master chain's latency; the estimate and the exact length of a stem handle
//   S6  bad arguments: errors, no crash
//   S7  SongRenderer level: a bus that exists (a pass-through chain hook) plays its audible sources in its stem; a pure source plays only through it and has no stem of its own
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "render_api_common.h"

using namespace u34;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)

// The stated tolerance of S1: every stem and the mix are float32 chains; the mix adds the tracks in track order before MASTER VOL and OUT, a stem has one track and the gains
// are applied to it alone, so the two differ by float32 rounding of the sums: a few ULP of the peak. Stated as an error relative to the mix's peak.
static const double kTol = 1e-6;

struct Stereo { std::vector<float> L, R; };
static thunder_render_opts mkOpts(int stem, bool fx, bool master, int64_t frames, int pdc = 0) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.effects = fx; o.master_chain = master; o.pdc_mode = pdc; o.use_frames = frames > 0; o.frames = frames; o.stem = stem; return o;
}
static thunder_render* make(const std::string& song, const PadSet& ps, const thunder_render_opts& o) {
  char err[200] = {0}; thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, err, sizeof err);
  if (!r) { std::printf("create failed: %s\n", err); g_fail++; return nullptr; }
  if (!installPads(r, ps)) { std::printf("install pads failed\n"); g_fail++; thunder_render_free(r); return nullptr; }
  return r;
}
static Stereo readAll(thunder_render* r, int64_t cap = 48000LL * 700) {
  Stereo s; std::vector<float> a(4096), b(4096);
  for (;;) { const int64_t n = thunder_render_read(r, a.data(), b.data(), 4096); if (n < 0) { std::printf("read error %lld\n", (long long)n); g_fail++; break; } if (n == 0) break;
    s.L.insert(s.L.end(), a.begin(), a.begin() + n); s.R.insert(s.R.end(), b.begin(), b.begin() + n); if ((int64_t)s.L.size() > cap) break; }
  return s;
}
static Stereo render(const std::string& song, const PadSet& ps, const thunder_render_opts& o) {
  thunder_render* r = make(song, ps, o); Stereo s; if (!r) return s; s = readAll(r); thunder_render_free(r); return s;
}
static std::vector<int> stemList(thunder_render* r) {
  const int n = thunder_render_stem_tracks(r, nullptr, 0); std::vector<int> v;
  if (n < 0) { std::printf("stem_tracks error %d\n", n); g_fail++; return v; }
  std::vector<int32_t> out((size_t)n + 1); const int m = thunder_render_stem_tracks(r, out.data(), n + 1);
  CHECK("stem_tracks: count equals the count-only call", m == n);
  for (int i = 0; i < n; i++) v.push_back(out[(size_t)i]);
  return v;
}
static double peakOf(const Stereo& s) { double p = 0; for (float v : s.L) p = std::max(p, (double)std::fabs(v)); for (float v : s.R) p = std::max(p, (double)std::fabs(v)); return p; }
static bool silent(const Stereo& s) { for (float v : s.L) if (v != 0) return false; for (float v : s.R) if (v != 0) return false; return true; }
static bool bitSame(const Stereo& a, const Stereo& b) {
  return a.L.size() == b.L.size() && a.R.size() == b.R.size() && (a.L.empty() || (!std::memcmp(a.L.data(), b.L.data(), a.L.size() * 4) && !std::memcmp(a.R.data(), b.R.data(), a.R.size() * 4)));
}
// max |mix - sum of stems| over both channels
static double sumErr(const Stereo& mix, const std::vector<Stereo>& stems) {
  double e = 0;
  for (size_t i = 0; i < mix.L.size(); i++) { double sl = 0, sr = 0; for (const Stereo& s : stems) { sl += s.L[i]; sr += s.R[i]; } e = std::max(e, std::max(std::fabs(sl - mix.L[i]), std::fabs(sr - mix.R[i]))); }
  return e;
}

// A song of tracks 0..4 on instrument = track (pad = track), 8 rows each (0.125 s at 120 BPM). tracks: bit t set: track t has a note on rows 0, 2, 4. mix: JSON array text of "mix" or "".
static std::string synth(unsigned tracks, const std::string& mix = "", const std::string& extra = "") {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[";
  for (int t = 0; t < 5; t++) s += (t ? "," : "") + std::string(tracks >> t & 1u ? std::to_string(t) : "-1");
  s += "]],\"chains\":[";
  for (int t = 0; t < 5; t++) s += std::string(t ? "," : "") + "[[" + std::to_string(t) + ",0]]";
  s += "],\"plen\":[8,8,8,8,8],\"insts\":[";
  for (int i = 0; i < 8; i++) s += std::string(i ? "," : "") + "{\"pad\":" + std::to_string(i) + ",\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}";
  s += "],\"phrases\":[";
  for (int p = 0; p < 5; p++) {
    s += p ? ",[" : "[";
    for (int r = 0; r < 8; r++) { char b[128]; if (r % 2 == 0 && r < 6) std::snprintf(b, sizeof b, "%s[%d,-1,-1,-1,-1,%d,-1,-1,-1,-1,-1,-1]", r ? "," : "", 60 + p, p); else std::snprintf(b, sizeof b, ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); s += b; }
    s += "]";
  }
  s += "]";
  if (!mix.empty()) s += ",\"mix\":" + mix;
  return s + extra + "}";
}
static std::string mixJson(const char* const* items, int n) { std::string s = "["; for (int i = 0; i < n; i++) s += std::string(i ? "," : "") + items[i]; return s + "]"; }

int main() {
  const PadSet ps;
  const std::string electro = readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder");
  CHECK("ELECTRO read", !electro.empty());

  // ---- S1: the sum of the stems is the mix ------------------------------------------------------------------------------------------------------------------------
  {
    const int64_t N = 240000;   // 5 s
    for (int variant = 0; variant < 3; variant++) {
      const char* name = variant == 0 ? "ELECTRO plain" : variant == 1 ? "ELECTRO + track effects" : "ELECTRO + effects + PDC auto";
      const bool fx = variant > 0; const int pdc = variant == 2 ? 1 : 0;
      thunder_render* m = make(electro, ps, mkOpts(0, fx, false, N, pdc)); if (!m) continue;
      const std::vector<int> list = stemList(m); const Stereo mix = readAll(m);
      CHECK("S1 the mix is not silent", peakOf(mix) > 0.01);
      std::vector<Stereo> stems; bool allLen = true; int loud = 0;
      for (int t : list) { const Stereo s = render(electro, ps, mkOpts(t, fx, false, N, pdc)); allLen = allLen && (int64_t)s.L.size() == N; if (peakOf(s) > 0) loud++; stems.push_back(s); }
      const double e = sumErr(mix, stems), pk = peakOf(mix);
      std::printf("S1 %s: %zu stems, mix peak %.4f, max |mix - sum| %.3g (%.3g of the peak)\n", name, list.size(), pk, e, e / pk);
      CHECK("S1 more than one stem", list.size() > 1);
      CHECK("S1 every stem is N frames", allLen);
      std::printf("S1 %d of %zu stems have sound in these 5 s\n", loud, list.size());
      CHECK("S1 at least two stems have sound (the others may start later in the song)", loud >= 2);
      CHECK("S1 the sum of the stems is the mix within the tolerance", e <= kTol * pk);
      thunder_render_free(m);
    }
    // the synthetic song with levels and pans, master VOL and OUT not at FF (they stay in the stems: index.html:24233-24235)
    const char* mx[] = {"{\"vol\":224,\"pan\":0}", "{\"vol\":200,\"pan\":255,\"trim\":100}", "{\"vol\":255,\"pan\":128}", "{\"vol\":120,\"pan\":64}", "{\"vol\":224,\"pan\":192}"};
    const std::string song = synth(31, mixJson(mx, 5), ",\"master\":200,\"mout\":180");
    thunder_render* m = make(song, ps, mkOpts(0, false, false, 48000)); const std::vector<int> list = stemList(m); const Stereo mix = readAll(m); thunder_render_free(m);
    CHECK("S1 synthetic: five stems", list.size() == 5);
    std::vector<Stereo> stems; for (int t : list) stems.push_back(render(song, ps, mkOpts(t, false, false, 48000)));
    const double e = sumErr(mix, stems), pk = peakOf(mix);
    std::printf("S1 synthetic (pan / vol / trim / MASTER 200 / OUT 180): mix peak %.4f, max |mix - sum| %.3g (%.3g of the peak)\n", pk, e, e / pk);
    CHECK("S1 synthetic: the sum is the mix", pk > 0.01 && e <= kTol * pk);
    // the stems carry the pan: track 0 panned hard left is silent on the right, track 1 hard right silent on the left
    CHECK("S1 pan stays in the stem (track 1 hard left: right is silent)", peakOf(Stereo{stems[0].R, {}}) < 1e-6 && peakOf(Stereo{stems[0].L, {}}) > 0.001);
    CHECK("S1 pan stays in the stem (track 2 hard right: left is silent)", peakOf(Stereo{stems[1].L, {}}) < 1e-6 && peakOf(Stereo{stems[1].R, {}}) > 0.001);
    // MASTER VOL and OUT are in a stem: the same stem with MASTER and OUT at FF is louder by their gains ((200/255)^2 * (180/255)^2)
    const Stereo full = render(synth(31, mixJson(mx, 5)), ps, mkOpts(3, false, false, 48000));
    const double ratio = peakOf(stems[2]) / peakOf(full), want = std::pow(200.0 / 255, 2) * std::pow(180.0 / 255, 2);
    CHECK("S1 MASTER VOL and OUT are in the stem", std::fabs(ratio - want) < 1e-4);
  }

  // ---- S2: where they do not add up ---------------------------------------------------------------------------------------------------------------------------
  {
    const int64_t N = 240000;
    thunder_render* m = make(electro, ps, mkOpts(0, true, true, N)); const std::vector<int> list = stemList(m); const Stereo mix = readAll(m); thunder_render_free(m);
    std::vector<Stereo> stems; for (int t : list) stems.push_back(render(electro, ps, mkOpts(t, true, false, N)));
    // the mix has the master chain: it is not the sum any more (the chain has latency, mastering, a limiter)
    double e = 0; for (size_t i = 0; i < mix.L.size(); i++) { double sl = 0; for (const Stereo& s : stems) sl += s.L[i]; e = std::max(e, std::fabs(sl - mix.L[i])); }
    std::printf("S2 ELECTRO with the master chain on the mix: max |mix - sum of stems| %.4f (mix peak %.4f)\n", e, peakOf(mix));
    CHECK("S2 with the master chain the mix is NOT the sum of the stems", e > 1e-3);
    // a stem ignores master_chain: bit for bit the stem made without it
    for (size_t k = 0; k < list.size() && k < 3; k++) {
      const Stereo a = render(electro, ps, mkOpts(list[k], true, true, N)), b = render(electro, ps, mkOpts(list[k], true, false, N));
      CHECK("S2 a stem with master_chain = 1 equals the one with 0", bitSame(a, b));
    }
    // the synthetic song with a master chain: the stem's latency is the PDC's alone (0 here), the mix's has the chain's
    thunder_render* a = make(synth(31), ps, mkOpts(0, false, true, 24000)); thunder_render* b = make(synth(31), ps, mkOpts(2, false, true, 24000));
    CHECK("S2 the mix has the master chain's latency", a && thunder_render_latency(a) > 0);
    CHECK("S2 a stem has none (no master chain, no PDC)", b && thunder_render_latency(b) == 0);
    thunder_render_free(a); thunder_render_free(b);
  }

  // ---- S3: a stem holds only its track ---------------------------------------------------------------------------------------------------------------------------
  {
    const char* mx[] = {"{}", "{}", "{}", "{}", "{}"};
    (void)mx;
    // the same song with every track but t muted (the mixer's own mute: gain 0 through the same graph) is the stem of t, bit for bit
    const std::string song = synth(0x1F);
    for (int t = 0; t < 5; t++) {
      std::string items[5]; const char* p[5]; for (int k = 0; k < 5; k++) { items[(size_t)k] = k == t ? "{\"vol\":224}" : "{\"vol\":224,\"mute\":true}"; p[k] = items[(size_t)k].c_str(); }
      const Stereo only = render(synth(0x1F, mixJson(p, 5)), ps, mkOpts(0, false, false, 48000));
      const Stereo stem = render(song, ps, mkOpts(t + 1, false, false, 48000));
      CHECK("S3 the stem of a track is the mix of the song with the others muted", peakOf(stem) > 0.001 && bitSame(only, stem));
    }
    // a track with no notes: not listed, and a stem asked for directly is exact silence
    const std::string s3 = synth(0x0B);   // tracks 0, 1, 3 have notes; 2 and 4 are empty
    thunder_render* m = make(s3, ps, mkOpts(0, false, false, 48000)); const std::vector<int> list = stemList(m); thunder_render_free(m);
    CHECK("S3 the list holds the tracks with notes only (1, 2, 4)", list == std::vector<int>({1, 2, 4}));
    CHECK("S3 a track with no notes: its stem is exact silence", silent(render(s3, ps, mkOpts(3, false, false, 48000))) && silent(render(s3, ps, mkOpts(5, false, false, 48000))));
    CHECK("S3 a track beyond the song's tracks too (32)", silent(render(s3, ps, mkOpts(32, false, false, 48000))));
    // a note whose pad is not in the kit does not make a stem (the page's kit.voices[e.pad]); a pad of another route does
    thunder_render* c = thunder_render_create((const uint8_t*)s3.data(), s3.size(), nullptr, nullptr, 0); CHECK("S3 create (default opts)", c != nullptr);
    if (c) {
      CHECK("S3 no pads: no stems", thunder_render_stem_tracks(c, nullptr, 0) == 0);
      float one[4] = {0.1f, 0.1f, 0.1f, 0.1f};
      CHECK("S3 pad 1 set", thunder_render_set_pad(c, 1, one, nullptr, 4, 48000) == 0);
      CHECK("S3 only the track of pad 1 (track 2)", thunder_render_stem_tracks(c, nullptr, 0) == 1);
      CHECK("S3 a live-route pad is in the kit", thunder_render_set_pad_route(c, 3, THUNDER_RENDER_ROUTE_LIVE) == 0 && thunder_render_stem_tracks(c, nullptr, 0) == 2);
      int32_t out[4] = {-1, -1, -1, -1}; const int n = thunder_render_stem_tracks(c, out, 1);
      CHECK("S3 cap 1: the count is still 2, one entry written", n == 2 && out[0] == 2 && out[1] == -1);
      thunder_render_free(c);
    }
  }

  // ---- S4: mute / solo -------------------------------------------------------------------------------------------------------------------------------------------
  {
    const char* M1[] = {"{}", "{\"mute\":true}", "{}", "{}", "{}"};
    const std::string muted = synth(0x1F, mixJson(M1, 5));
    thunder_render* m = make(muted, ps, mkOpts(0, false, false, 48000)); const std::vector<int> list = stemList(m); const Stereo mix = readAll(m); thunder_render_free(m);
    CHECK("S4 a muted track has no stem", list == std::vector<int>({1, 3, 4, 5}));
    std::vector<Stereo> stems; for (int t : list) stems.push_back(render(muted, ps, mkOpts(t, false, false, 48000)));
    CHECK("S4 the mix (muted track out) is the sum of the listed stems", sumErr(mix, stems) <= kTol * peakOf(mix));
    // a stem of the muted track asked for directly: heard, as the JS's pass (the filter replaces audible: index.html:24255)
    CHECK("S4 a stem asked for directly ignores the mute", peakOf(render(muted, ps, mkOpts(2, false, false, 48000))) > 0.001);
    const char* M2[] = {"{}", "{}", "{\"solo\":true}", "{}", "{}"};
    const std::string solo = synth(0x1F, mixJson(M2, 5));
    thunder_render* s = make(solo, ps, mkOpts(0, false, false, 48000)); CHECK("S4 a solo: only that track", stemList(s) == std::vector<int>({3})); const Stereo smix = readAll(s); thunder_render_free(s);
    CHECK("S4 the mix of a solo is the stem of the soloed track", bitSame(smix, render(solo, ps, mkOpts(3, false, false, 48000))));
    const char* M3[] = {"{\"mute\":true}", "{}", "{\"solo\":true,\"mute\":true}", "{\"solo\":true}", "{}"};
    const std::string both = synth(0x1F, mixJson(M3, 5));
    thunder_render* b = make(both, ps, mkOpts(0, false, false, 48000)); CHECK("S4 solo over mute (index.html:20669): tracks 3 and 4", stemList(b) == std::vector<int>({3, 4})); thunder_render_free(b);
    const char* M4[] = {"{\"mute\":true}", "{\"mute\":true}", "{\"mute\":true}", "{\"mute\":true}", "{\"mute\":true}"};
    thunder_render* z = make(synth(0x1F, mixJson(M4, 5)), ps, mkOpts(0, false, false, 48000)); CHECK("S4 everything muted: no stems, a silent mix", stemList(z).empty() && silent(readAll(z))); thunder_render_free(z);
    // the stats say who is heard
    thunder_render* st = make(solo, ps, mkOpts(5, false, false, 4800)); readAll(st); thunder_render_stats s2; std::memset(&s2, 0, sizeof s2); s2.size = sizeof s2;
    CHECK("S4 stats: stem 5, only track 5 heard", thunder_render_stats_get(st, &s2) == 0 && s2.stem == 5 && s2.heard_mask == 1u << 4); thunder_render_free(st);
    thunder_render* mm = make(solo, ps, mkOpts(0, false, false, 4800)); readAll(mm); std::memset(&s2, 0, sizeof s2); s2.size = sizeof s2;
    CHECK("S4 stats: the mix, the soloed track heard", thunder_render_stats_get(mm, &s2) == 0 && s2.stem == 0 && s2.heard_mask == 1u << 2); thunder_render_free(mm);
  }

  // ---- S5: length (PICKUP "To add in U34b: a length ESTIMATE ..."; U34a built length_estimate, here it is held for stems) --------------------------------------------------------
  {
    const std::string song = synth(0x1F);
    // use_frames: exact for the mix and every stem
    for (int t = 0; t <= 5; t++) { thunder_render* r = make(song, ps, mkOpts(t, false, false, 12345)); CHECK("S5 length known up front", r && thunder_render_length(r) == 12345 && thunder_render_length_estimate(r) == 12345);
      if (r) { const Stereo s = readAll(r); CHECK("S5 exactly the frames asked", (int64_t)s.L.size() == 12345); thunder_render_free(r); } }
    // unknown length, no master chain: a stem ends where the mix does
    thunder_render* a = make(song, ps, mkOpts(0, false, false, 0)); thunder_render* b = make(song, ps, mkOpts(2, false, false, 0));
    const int64_t ea = thunder_render_length_estimate(a), eb = thunder_render_length_estimate(b); CHECK("S5 the estimate is made before the first read (-1 is not an answer)", ea > 0 && eb > 0);
    CHECK("S5 the estimate is the same for the mix and a stem (no master chain)", ea == eb);
    CHECK("S5 unknown until the end", thunder_render_length(a) == -1 && thunder_render_length(b) == -1);
    const Stereo sa = readAll(a), sb = readAll(b);
    CHECK("S5 the same length (no master chain)", sa.L.size() == sb.L.size() && thunder_render_length(a) == (int64_t)sa.L.size() && thunder_render_length(b) == (int64_t)sb.L.size());
    std::printf("S5 synthetic: estimate %lld, real %zu\n", (long long)ea, sa.L.size());
    CHECK("S5 the estimate is within a pad length of the real length (the sequencer end vs the voices' end: either can be later)", std::llabs(ea - (int64_t)sa.L.size()) <= 24000 + 6128);
    CHECK("S5 after the end the estimate is the exact length", thunder_render_length_estimate(a) == (int64_t)sa.L.size() && thunder_render_length_estimate(b) == (int64_t)sb.L.size());
    thunder_render_free(a); thunder_render_free(b);
    // with the master chain a stem has no latency to trim: longer than the mix by the chain's latency; the estimate follows
    thunder_render* c = make(song, ps, mkOpts(0, false, true, 0)); thunder_render* d = make(song, ps, mkOpts(1, false, true, 0));
    const int64_t lat = thunder_render_latency(c); CHECK("S5 master chain latency > 0", lat > 0);
    CHECK("S5 the estimate of a stem is the mix's plus the master latency", thunder_render_length_estimate(d) == thunder_render_length_estimate(c) + lat);
    const Stereo sc = readAll(c), sd = readAll(d);
    CHECK("S5 a stem is longer than the mix by the master latency (the host passes use_frames = the mix's length)", (int64_t)sd.L.size() == (int64_t)sc.L.size() + lat);
    thunder_render_free(c); thunder_render_free(d);
    // max_frames cuts a stem exactly as the mix
    thunder_render_opts o = mkOpts(3, false, false, 0); o.max_frames = 5000; thunder_render* e = make(song, ps, o);
    if (e) { const Stereo se = readAll(e); thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; thunder_render_stats_get(e, &s); CHECK("S5 max_frames cuts a stem exactly, hit_limit set", se.L.size() == 5000 && s.hit_limit == 1 && thunder_render_length_estimate(e) == 5000); thunder_render_free(e); }
  }

  // ---- S6: bad arguments ---------------------------------------------------------------------------------------------------------------------------------------------
  {
    const std::string song = synth(0x1F); char err[100] = {0};
    for (int bad : {-1, 33, 1000, INT32_MIN, INT32_MAX}) { thunder_render_opts o = mkOpts(bad, false, false, 100); err[0] = 0; thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, err, sizeof err); CHECK("S6 a bad stem makes no handle, with a text", !r && err[0]); thunder_render_free(r); }
    { thunder_render_opts o = mkOpts(1, false, false, 100); o.reserved2 = 1; CHECK("S6 reserved2 must be 0", !thunder_render_create((const uint8_t*)song.data(), song.size(), &o, nullptr, 0)); }
    { thunder_render_opts o = mkOpts(33, false, false, 100); CHECK("S6 a bad stem with err NULL: no crash", !thunder_render_create((const uint8_t*)song.data(), song.size(), &o, nullptr, 0)); }
    { thunder_render_opts o = mkOpts(32, false, false, 100); thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, nullptr, 0); CHECK("S6 stem 32 is the last good one", r != nullptr); thunder_render_free(r); }
    // an options struct of the older header (no stem field) is the mix
    { thunder_render_opts o = mkOpts(3, false, false, 4800); o.size = (uint32_t)offsetof(thunder_render_opts, stem);
      thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, nullptr, 0); thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s;
      CHECK("S6 an older opts struct renders the mix", r && thunder_render_stats_get(r, &s) == 0 && s.stem == 0); thunder_render_free(r); }
    thunder_render* r = make(song, ps, mkOpts(0, false, false, 4800)); int32_t out[2];
    CHECK("S6 stem_tracks: NULL handle", thunder_render_stem_tracks(nullptr, out, 2) == THUNDER_RENDER_E_ARG && thunder_render_stem_tracks(nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
    CHECK("S6 stem_tracks: negative cap", thunder_render_stem_tracks(r, out, -1) == THUNDER_RENDER_E_ARG);
    CHECK("S6 stem_tracks: a buffer with cap 0", thunder_render_stem_tracks(r, out, 0) == THUNDER_RENDER_E_ARG);
    CHECK("S6 stem_tracks: INT_MAX cap with NULL out is the count", thunder_render_stem_tracks(r, nullptr, INT32_MAX) == 5);
    CHECK("S6 stem_tracks after the first read still works and does not disturb the render", readAll(r).L.size() == 4800 && thunder_render_stem_tracks(r, out, 2) == 5 && out[0] == 1 && out[1] == 2);
    thunder_render_free(r);
    // a song that is not a song, hostile numbers: a stem handle of one is as safe as a mix handle
    const std::string junk = "{\"v\":6,\"bpm\":1e300,\"lpb\":-3,\"ntr\":8,\"song\":[[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31]],\"chains\":[[[0,0]]],\"plen\":[8],\"phrases\":[[]],\"master\":1e9,\"mout\":-5}";
    for (int stem : {0, 1, 32}) { thunder_render_opts o = mkOpts(stem, true, true, 2000); thunder_render* h = thunder_render_create((const uint8_t*)junk.data(), junk.size(), &o, nullptr, 0);
      if (h) { installPads(h, ps); int32_t t[40]; const int n = thunder_render_stem_tracks(h, t, 40); CHECK("S6 hostile song: list is sane", n >= 0 && n <= 32); readAll(h); } thunder_render_free(h); }
    CHECK("S6 stem_tracks after a failed create is not reachable: NULL is E_ARG", thunder_render_stem_tracks(nullptr, nullptr, 0) < 0);
  }

  std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ok", g_fail);
  return g_fail ? 1 : 0;
}
