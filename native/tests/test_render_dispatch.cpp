// WIRING STEP 3 (D2 / D3): which source a note plays. Small synthetic songs, one note each, pads that are constants (DC), so the level of the output says
// exactly which buffer sounded: the base pad 0.2, a MUT variation 0.4, a GEN variation 0.1, and silence is exactly 0. Through the C API
// (thunder_render_set_variation / set_pad_route), and the C API against a direct SongRenderer drive bit for bit for the new inputs.
// Also the hostile arguments of the two new functions (run under -DTHUNDER_SANITIZE=ON).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "render/render.h"
#include "song/song.h"
#include "thunder/render.h"

using namespace thunder;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

constexpr int FX_MUT = 41, FX_GEN = 42, FX_DEC = 50;
constexpr int kPadFrames = 48000, kOut = 12000;
struct Inst { int pad = 0, slc = 0, sync = 0, mo = 0; };

// one phrase, four rows; row 0 has the note on instrument `inst` with one command; the rest are empty
static std::string songJson(const std::vector<Inst>& insts, int inst, int fx = -1, int fxv = 0) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"insts\":[";
  for (size_t i = 0; i < insts.size(); i++) {
    char b[200]; std::snprintf(b, sizeof b, "%s{\"pad\":%d,\"tsp\":0,\"slc\":%d,\"sync\":%d,\"mo\":%d}", i ? "," : "", insts[i].pad, insts[i].slc, insts[i].sync, insts[i].mo); s += b;
  }
  char r[160]; std::snprintf(r, sizeof r, "],\"phrases\":[[[60,-1,-1,-1,-1,%d,%d,%d,-1,-1,-1,-1]", inst, fx, fx >= 0 ? fxv : -1); s += r;
  for (int i = 1; i < 4; i++) s += ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]";
  return s + "]]}";
}

struct Setup {
  std::vector<float> pad[256];                 // DC pads (empty: no pad)
  struct Var { int ii; char kind; int num, pad; float v; };
  std::vector<Var> vars;
  int route[256] = {};
};
static std::vector<float> dc(float v, int n = kPadFrames) { return std::vector<float>((size_t)n, v); }

struct Result { std::vector<float> L, R; thunder_render_stats st; double lvl = 0, peak = 0; };
// level: mean |L| over output frames 2000..7000 (inside the note, past its start and before its end)
static Result render(const std::string& song, const Setup& su, int block = 4096) {
  Result res; thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = kOut;
  char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &o, err, sizeof err);
  if (!r) { std::printf("create failed: %s\n", err); g_fail++; return res; }
  for (int p = 0; p < 256; p++) {
    if (!su.pad[p].empty()) CHECK(thunder_render_set_pad(r, p, su.pad[p].data(), nullptr, (int64_t)su.pad[p].size(), 48000) == 0);
    if (su.route[p]) CHECK(thunder_render_set_pad_route(r, p, su.route[p]) == 0);
  }
  for (const auto& v : su.vars) { std::vector<float> d = dc(v.v); CHECK(thunder_render_set_variation(r, v.ii, v.kind, v.num, v.pad, d.data(), (int64_t)d.size(), 48000) == 0); }
  res.L.assign(kOut, 0); res.R.assign(kOut, 0);
  int64_t got = 0; while (got < kOut) { const int64_t n = thunder_render_read(r, res.L.data() + got, res.R.data() + got, std::min<int64_t>(block, kOut - got)); if (n <= 0) break; got += n; }
  CHECK(got == kOut);
  std::memset(&res.st, 0, sizeof res.st); res.st.size = sizeof res.st; CHECK(thunder_render_stats_get(r, &res.st) == 0);
  double sum = 0; for (int i = 2000; i < 7000; i++) sum += std::fabs(res.L[i]); res.lvl = sum / 5000;
  for (int i = 0; i < kOut; i++) res.peak = std::max(res.peak, (double)std::max(std::fabs(res.L[i]), std::fabs(res.R[i])));
  thunder_render_free(r);
  return res;
}

int main() {
  std::vector<Inst> I(4); I[0].pad = 0; I[1].pad = 0; I[2].pad = 0; I[3].pad = 0;
  Setup base; base.pad[0] = dc(0.2f);
  const Result b0 = render(songJson(I, 0), base);
  const double c = b0.lvl / 0.2;   // what the mixer and the pan do to a constant
  CHECKF(b0.lvl > 1e-3, "the base pad plays (level %g)", b0.lvl);
  CHECK(b0.st.notes == 1 && b0.st.notes_played == 1 && b0.st.notes_var == 0 && b0.st.notes_live == 0 && b0.st.notes_sf == 0 && b0.st.notes_var_missing == 0);
  auto near = [&](const Result& r, double v) { return std::fabs(r.lvl - c * v) <= 1e-4 * c + 1e-7; };
  auto silent = [&](const Result& r) { return r.peak == 0.0; };

  // D2: MUT -> the variation (0.4), not the base pad (0.2)
  { Setup s = base; s.vars.push_back({0, 'm', 5, 0, 0.4f}); const Result r = render(songJson(I, 0, FX_MUT, 5), s);
    CHECKF(near(r, 0.4), "MUT plays the variation: level %g, want %g", r.lvl, c * 0.4); CHECK(r.st.notes_var == 1 && r.st.notes_played == 1 && r.st.notes_var_missing == 0); }
  // GEN -> its own variation (0.1); a MUT with the same number does not stand in for it
  { Setup s = base; s.vars.push_back({0, 'g', 7, 0, 0.1f}); s.vars.push_back({0, 'm', 7, 0, 0.4f}); const Result r = render(songJson(I, 0, FX_GEN, 7), s);
    CHECKF(near(r, 0.1), "GEN plays the GEN variation: level %g, want %g", r.lvl, c * 0.1); CHECK(r.st.notes_var == 1); }
  // a missing variation is silence with a count, never the base pad: no variation at all, wrong kind, wrong number, wrong instrument, wrong pad
  { const Setup none = base; const Result r = render(songJson(I, 0, FX_MUT, 5), none); CHECK(silent(r)); CHECK(r.st.notes_var_missing == 1 && r.st.notes_played == 0 && r.st.notes == 1); }
  { Setup s = base; s.vars.push_back({0, 'g', 5, 0, 0.4f}); const Result r = render(songJson(I, 0, FX_MUT, 5), s); CHECK(silent(r) && r.st.notes_var_missing == 1); }
  { Setup s = base; s.vars.push_back({0, 'm', 6, 0, 0.4f}); const Result r = render(songJson(I, 0, FX_MUT, 5), s); CHECK(silent(r) && r.st.notes_var_missing == 1); }
  { Setup s = base; s.vars.push_back({1, 'm', 5, 0, 0.4f}); const Result r = render(songJson(I, 0, FX_MUT, 5), s); CHECK(silent(r) && r.st.notes_var_missing == 1); }
  { Setup s = base; s.vars.push_back({0, 'm', 5, 3, 0.4f}); const Result r = render(songJson(I, 0, FX_MUT, 5), s); CHECK(silent(r) && r.st.notes_var_missing == 1); }
  // a variation replaced and removed through the API
  { thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = kOut; const std::string sg = songJson(I, 0, FX_MUT, 5);
    thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &o, nullptr, 0); CHECK(r);
    std::vector<float> a = dc(0.4f), bb = dc(0.8f), p = dc(0.2f);
    CHECK(thunder_render_set_pad(r, 0, p.data(), nullptr, kPadFrames, 48000) == 0);
    CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, a.data(), kPadFrames, 48000) == 0);
    CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, bb.data(), kPadFrames, 48000) == 0);   // replaced: 0.8
    std::vector<float> L(kOut), R(kOut); CHECK(thunder_render_read(r, L.data(), R.data(), kOut) == kOut);
    double sum = 0; for (int i = 2000; i < 7000; i++) sum += std::fabs(L[i]); CHECKF(std::fabs(sum / 5000 - c * 0.8) <= 1e-4 * c + 1e-7, "replaced variation: %g want %g", sum / 5000, c * 0.8);
    thunder_render_free(r);
    r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &o, nullptr, 0);
    CHECK(thunder_render_set_pad(r, 0, p.data(), nullptr, kPadFrames, 48000) == 0);
    CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, a.data(), kPadFrames, 48000) == 0);
    CHECK(thunder_render_set_variation(r, 0, 'm', 5, 0, nullptr, 0, 0) == 0);   // removed
    CHECK(thunder_render_read(r, L.data(), R.data(), kOut) == kOut);
    double pk = 0; for (float x : L) pk = std::max(pk, (double)std::fabs(x)); CHECK(pk == 0.0);
    thunder_render_free(r); }

  // D2: live and sf pads: silent with a count, whatever buffer the host set for them
  for (int route = 1; route <= 2; route++) {
    Setup s = base; s.route[0] = route; const Result r = render(songJson(I, 0), s);
    CHECK(silent(r)); CHECK(r.st.notes == 1 && r.st.notes_played == 0 && (route == 1 ? r.st.notes_live == 1 && r.st.notes_sf == 0 : r.st.notes_sf == 1 && r.st.notes_live == 0));
    Setup s2 = base; s2.pad[0].clear(); s2.route[0] = route; const Result r2 = render(songJson(I, 0), s2);   // no buffer at all
    CHECK(silent(r2) && (route == 1 ? r2.st.notes_live == 1 : r2.st.notes_sf == 1)); }
  { Setup s = base; s.route[0] = 1; s.pad[1] = dc(0.2f); I[1].pad = 1; const Result r = render(songJson(I, 1), s); CHECKF(near(r, 0.2), "another pad stays a sampler: %g", r.lvl); I[1].pad = 0; }
  // a null kit entry: no pad, silent, counted
  { Setup s; const Result r = render(songJson(I, 0), s); CHECK(silent(r) && r.st.notes_no_pad == 1 && r.st.notes_played == 0); }
  // MIDI-only instrument: silent (unchanged)
  { Setup s = base; I[2].mo = 1; const Result r = render(songJson(I, 2), s); CHECK(silent(r) && r.st.notes_midi_only == 1); I[2].mo = 0; }

  // D3: slc > 1 or sync: the pad as it is, no variation and no shaping
  { Setup s = base; s.vars.push_back({0, 'm', 5, 0, 0.4f}); I[1].slc = 4; I[2].sync = 1; I[3].slc = 1;
    const Result a = render(songJson(I, 1, FX_MUT, 5), s); CHECKF(near(a, 0.2), "sliced MUT plays the pad: %g want %g", a.lvl, c * 0.2); CHECK(a.st.notes_unshaped == 1 && a.st.notes_var == 0 && a.st.notes_var_missing == 0);
    s.vars.clear(); s.vars.push_back({2, 'm', 5, 0, 0.4f}); s.vars.push_back({3, 'm', 5, 0, 0.4f});
    const Result y = render(songJson(I, 2, FX_MUT, 5), s); CHECKF(near(y, 0.2), "synced MUT plays the pad: %g", y.lvl); CHECK(y.st.notes_unshaped == 1 && y.st.notes_var == 0);
    const Result o = render(songJson(I, 3, FX_MUT, 5), s); CHECKF(near(o, 0.4), "slc 1 is not sliced: the variation plays: %g", o.lvl); CHECK(o.st.notes_unshaped == 0 && o.st.notes_var == 1);
    // no variation handed in, but sliced: still the pad (never looks for one)
    Setup s0 = base; const Result a0 = render(songJson(I, 1, FX_MUT, 5), s0); CHECKF(near(a0, 0.2), "sliced MUT, no variation: %g", a0.lvl); CHECK(a0.st.notes_var_missing == 0);
    // DEC shapes a plain instrument (the level has decayed away at 40 ms) and not a sliced or synced one
    const Result d0 = render(songJson(I, 0, FX_DEC, 0), base); CHECKF(d0.lvl < 0.05 * c * 0.2, "DEC shapes the pad: level %g", d0.lvl); CHECK(d0.st.notes_unshaped == 0);
    const Result d1 = render(songJson(I, 1, FX_DEC, 0), base); CHECKF(near(d1, 0.2), "DEC on a sliced instrument: the pad as it is: %g", d1.lvl); CHECK(d1.st.notes_unshaped == 1);
    const Result d2 = render(songJson(I, 2, FX_DEC, 0), base); CHECKF(near(d2, 0.2), "DEC on a synced instrument: the pad as it is: %g", d2.lvl); CHECK(d2.st.notes_unshaped == 1);
    const Result d3 = render(songJson(I, 3, FX_DEC, 0), base); CHECKF(d3.lvl < 0.05 * c * 0.2, "DEC on slc 1 shapes: %g", d3.lvl);
    I[1].slc = 0; I[2].sync = 0; I[3].slc = 0; }

  // the C API equals a direct SongRenderer drive bit for bit with the new inputs (variation, route), at several block sizes
  { const std::string sg = songJson(I, 0, FX_MUT, 5); Setup s = base; s.vars.push_back({0, 'm', 5, 0, 0.4f}); s.pad[1] = dc(0.3f); s.route[1] = 1;
    Song song; JsonError je; CHECK(song.load(sg.data(), sg.size(), &je)); SeqVars vars; RenderPads pads;
    pads.buf[0].data = s.pad[0].data(); pads.buf[0].len = kPadFrames; pads.buf[0].sr = 48000;
    pads.buf[1].data = s.pad[1].data(); pads.buf[1].len = kPadFrames; pads.buf[1].sr = 48000; pads.route[1] = PadRoute::Live;
    { std::vector<float> d = dc(0.4f); pads.setVar(0, 'm', 5, 0, d.data(), kPadFrames, 48000); }
    SeqOpts so; so.row = song.renderFrom; so.once = true; SongRenderer R(song, vars, pads, 48000, 128, so);
    std::vector<float> dl(kOut), dr(kOut); R.block(dl.data(), dr.data(), kOut);
    for (int bs : {1, 64, 128, 333, 4096, kOut}) { const Result r = render(sg, s, bs); CHECKF(!std::memcmp(r.L.data(), dl.data(), kOut * 4) && !std::memcmp(r.R.data(), dr.data(), kOut * 4), "C API != direct at block %d", bs); }
    const RenderStats ds = R.stats(); const Result r = render(sg, s);
    CHECK(r.st.notes_var == ds.notesVar && r.st.notes_live == ds.notesLive && r.st.notes_var_missing == ds.notesVarMissing && r.st.notes_played == ds.notesPlayed); }

  // hostile arguments: set_variation / set_pad_route
  { const std::string sg = songJson(I, 0, FX_MUT, 5); thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = 1000;
    thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &o, nullptr, 0); CHECK(r);
    std::vector<float> d = dc(0.1f, 100); const int A = THUNDER_RENDER_E_ARG;
    CHECK(thunder_render_set_variation(nullptr, 0, 'm', 0, 0, d.data(), 100, 48000) == A);
    CHECK(thunder_render_set_pad_route(nullptr, 0, 1) == A);
    for (int ii : {-1, 64, 1 << 30, INT32_MIN}) CHECK(thunder_render_set_variation(r, ii, 'm', 0, 0, d.data(), 100, 48000) == A);
    for (int k : {0, (int)'G', (int)'M', -1, (int)'x', 256 + (int)'g'}) CHECK(thunder_render_set_variation(r, 0, k, 0, 0, d.data(), 100, 48000) == A);
    for (int n : {-1, 256, INT32_MAX, INT32_MIN}) CHECK(thunder_render_set_variation(r, 0, 'g', n, 0, d.data(), 100, 48000) == A);
    for (int p : {-1, 256, INT32_MAX}) CHECK(thunder_render_set_variation(r, 0, 'g', 0, p, d.data(), 100, 48000) == A);
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, d.data(), -1, 48000) == A);
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, d.data(), INT64_MIN, 48000) == A);
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, nullptr, 100, 48000) == A);
    for (double sr : {0.0, 0.5, -1.0, 1e6 + 1, (double)INFINITY, (double)NAN}) CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, d.data(), 100, sr) == A);
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, d.data(), (int64_t)1 << 40, 48000) == THUNDER_RENDER_E_LIMIT);
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, d.data(), THUNDER_RENDER_MAX_PAD_FRAMES + 1, 48000) == THUNDER_RENDER_E_LIMIT);
    { std::vector<float> bad = d; bad[50] = NAN; CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, bad.data(), 100, 48000) == A); bad[50] = INFINITY; CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, bad.data(), 100, 48000) == A); }
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, nullptr, 0, 0) == 0);          // removing what is not there
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, d.data(), 100, 1) == 0);       // rates 1 and 1e6 are fine
    CHECK(thunder_render_set_variation(r, 63, 'm', 255, 255, d.data(), 100, 1e6) == 0);
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, d.data(), 1, 48000) == 0);     // a 1-frame variation: renders (the voice refuses it, counted)
    for (int p : {-1, 256, INT32_MAX, INT32_MIN}) CHECK(thunder_render_set_pad_route(r, p, 1) == A);
    for (int rt : {-1, 3, INT32_MAX, INT32_MIN}) CHECK(thunder_render_set_pad_route(r, 0, rt) == A);
    CHECK(thunder_render_set_pad_route(r, 255, 2) == 0 && thunder_render_set_pad_route(r, 255, 0) == 0);
    // the table limit: 4096 variations fit (63 * 2 * 256 ... keys vary), one more is refused, a replacement of a held key is not
    int made = 2; for (int ii = 0; ii < 64 && made < THUNDER_RENDER_MAX_VARIATIONS; ii++) for (int pad = 0; pad < 256 && made < THUNDER_RENDER_MAX_VARIATIONS; pad++) for (int num = 1; num < 256 && made < THUNDER_RENDER_MAX_VARIATIONS; num++) {
      if (thunder_render_set_variation(r, ii, 'g', num, pad, d.data(), 2, 48000) == 0) made++; else { CHECK(false); made = THUNDER_RENDER_MAX_VARIATIONS; } }
    CHECK(made == THUNDER_RENDER_MAX_VARIATIONS);
    CHECK(thunder_render_set_variation(r, 62, 'm', 3, 3, d.data(), 2, 48000) == THUNDER_RENDER_E_LIMIT);
    CHECK(thunder_render_set_variation(r, 63, 'm', 255, 255, d.data(), 50, 48000) == 0);   // replacing a held key at the limit is fine
    CHECK(thunder_render_set_variation(r, 63, 'm', 255, 255, nullptr, 0, 0) == 0);          // removing one makes room
    CHECK(thunder_render_set_variation(r, 62, 'm', 3, 3, d.data(), 2, 48000) == 0);
    // after the first read: E_STATE, and the render still works (variation 1 frame, 2 frames: refused or played, no crash)
    std::vector<float> L(1000), Rr(1000); CHECK(thunder_render_read(r, L.data(), Rr.data(), 1000) == 1000);
    CHECK(thunder_render_set_variation(r, 0, 'm', 0, 0, d.data(), 100, 48000) == THUNDER_RENDER_E_STATE);
    CHECK(thunder_render_set_pad_route(r, 0, 1) == THUNDER_RENDER_E_STATE);
    thunder_render_free(r); }
  // the 512 MiB budget is shared with the pads: 4 x 128 MiB variations fit, the fifth does not, removing one gives room, a pad counts too
  { const std::string sg = songJson(I, 0); thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), nullptr, nullptr, 0); CHECK(r);
    const int64_t big = (int64_t)32 << 20;   // 2^25 frames = 128 MiB
    std::vector<float> z((size_t)big, 0.0f);
    for (int k = 0; k < 4; k++) CHECK(thunder_render_set_variation(r, 0, 'g', k, 0, z.data(), big, 48000) == 0);
    CHECK(thunder_render_set_variation(r, 0, 'g', 4, 0, z.data(), 1, 48000) == THUNDER_RENDER_E_LIMIT);
    CHECK(thunder_render_set_pad(r, 5, z.data(), nullptr, 1, 48000) == THUNDER_RENDER_E_LIMIT);
    CHECK(thunder_render_set_variation(r, 0, 'g', 3, 0, z.data(), 1, 48000) == 0);        // a smaller replacement
    CHECK(thunder_render_set_variation(r, 0, 'g', 0, 0, nullptr, 0, 0) == 0);              // removed: room for a pad
    CHECK(thunder_render_set_pad(r, 5, z.data(), nullptr, big, 48000) == 0);
    thunder_render_free(r); }
  // a handle with no variation or route set renders exactly what it did (the unrouted default): the base level
  { const Result r = render(songJson(I, 0), base); CHECK(near(r, 0.2)); }

  std::printf(g_fail ? "render_dispatch: %d FAILED\n" : "render_dispatch: all passed\n", g_fail);
  return g_fail ? 1 : 0;
}
