// CHOP, the render side (docs/port/CHOP-NOTES.md, ctest `render_chop`): through the C API a pad made of four distinct DC levels (0.1 0.2 0.3 0.4, a quarter of the pad each), an instrument with slc 4 and
// notes C-4 .. D#-4: each note's output is its own slice's level for the slice's duration and exactly silent after (the note ends at its cut; the 4 ms release is allowed for), and
// the output is bit-identical at block sizes 1, 64, 128, 333 and 4096. Also: reversed notes read the slice back to front, the stretched-sync count, a pad without chop data, replacing the
// pad drops its chop data, and the hostile arguments of set_pad_chop / set_pad_hits (run under -DTHUNDER_SANITIZE=ON): a zero-length pad, NaN / inf / negative loop and points,
// slc 0 / 1 / 64 / 65 / negative, unsorted or out-of-range points, E_STATE after the first read.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "thunder/render.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

constexpr int SR = 48000, kPad = 96000 /* 2.0 s: four slices of 0.5 s */, kOut = 5 * SR;
// one track, one phrase of 32 rows (0.125 s each at 120 bpm, 4 lines per beat): a note every 8 rows = every second (C-4, C#4, D-4, D#4); `fx` adds a PLY 1 (reverse) command when asked
static std::string songJson(const std::string& inst, bool rev = false, int note0 = 48, int stepRows = 8) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[32],\"insts\":[" + inst + "],\"phrases\":[[";
  for (int y = 0; y < 32; y++) { s += y ? "," : ""; const int k = y / stepRows, hit = y % stepRows == 0 && k < 4;
    s += "[" + std::to_string(hit ? note0 + k : -1) + ",-1,-1,-1,-1,0," + (hit && rev ? "24,1" : "-1,-1") + ",-1,-1,-1,-1]"; }   // 24 = PLY in FXC order? the test checks it below
  return s + "]]}";
}
struct Out { std::vector<float> L, R; thunder_render_stats st; };
struct Cfg { double loop = 2.0; bool setChop = true; std::vector<double> hits; int hitsN = 0; std::vector<float> pad; };
static std::vector<float> levels() { std::vector<float> p((size_t)kPad); for (int i = 0; i < kPad; i++) p[(size_t)i] = 0.1f * (float)(1 + i / (kPad / 4)); return p; }
static bool render(const std::string& song, const Cfg& c, int block, Out& o, int frames = kOut) {
  thunder_render_opts op; std::memset(&op, 0, sizeof op); op.size = sizeof op; op.use_frames = 1; op.frames = frames;
  char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)song.data(), song.size(), &op, err, sizeof err);
  if (!r) { std::printf("create failed: %s\n", err); g_fail++; return false; }
  const std::vector<float>& p = c.pad.empty() ? levels() : c.pad;
  CHECK(thunder_render_set_pad(r, 0, p.data(), nullptr, (int64_t)p.size(), SR) == 0);
  if (c.setChop) CHECK(thunder_render_set_pad_chop(r, 0, c.loop) == 0);
  if (!c.hits.empty()) CHECK(thunder_render_set_pad_hits(r, 0, c.hitsN, c.hits.data(), (int)c.hits.size()) == 0);
  o.L.assign((size_t)frames, 0); o.R.assign((size_t)frames, 0);
  int64_t got = 0; while (got < frames) { const int64_t n = thunder_render_read(r, o.L.data() + got, o.R.data() + got, std::min<int64_t>(block, frames - got)); if (n <= 0) break; got += n; }
  CHECK(got == frames);
  std::memset(&o.st, 0, sizeof o.st); o.st.size = sizeof o.st; CHECK(thunder_render_stats_get(r, &o.st) == 0);
  thunder_render_free(r); return true;
}
static double mean(const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += std::fabs(v[(size_t)i]); return s / (b - a); }
static double peak(const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; i++) s = std::max(s, (double)std::fabs(v[(size_t)i])); return s; }

int main() {
  const std::string slc4 = songJson("{\"pad\":0,\"slc\":4}");
  Out ref; render(slc4, Cfg(), 4096, ref);
  CHECKF(ref.st.notes == 4 && ref.st.notes_played == 4 && ref.st.seq_chopped == 4 && ref.st.seq_chop_skipped == 0 && ref.st.notes_stretched == 0, "stats: notes %d played %d chopped %llu skipped %llu", ref.st.notes, ref.st.notes_played, (unsigned long long)ref.st.seq_chopped, (unsigned long long)ref.st.seq_chop_skipped);
  // what the mixer does to a constant (the pan, the gain): taken from slice 0, then the others must stand in the ratio of the DC levels
  const double c0 = mean(ref.L, 2000, 22000) / 0.1;
  CHECKF(c0 > 1e-3, "the first slice plays (%g)", c0);
  for (int k = 0; k < 4; k++) {
    const int t0 = k * SR;   // the note's start
    const double lvl = mean(ref.L, t0 + 2000, t0 + 22000), want = c0 * 0.1 * (1 + k);
    CHECKF(std::fabs(lvl - want) <= 1e-4 * c0 + 1e-7, "note %d: its own slice's level %g (want %g)", k, lvl, want);
    // the slice is 0.5 s: 24000 frames; silent from its end plus the release (4 ms = 192 frames, 0.05 s to be safe) to the next note
    CHECKF(peak(ref.L, t0 + SR / 2 + 2400, t0 + SR) == 0.0 && peak(ref.R, t0 + SR / 2 + 2400, t0 + SR) == 0.0, "note %d: exact silence after its slice", k);
    CHECKF(mean(ref.L, t0 + SR / 2 - 400, t0 + SR / 2 - 100) > 0.9 * want, "note %d: still sounding just before the slice ends", k);
  }
  // bit-identical across block sizes
  for (int b : {1, 64, 128, 333, 4096}) { Out o; render(slc4, Cfg(), b, o, kOut); CHECKF(o.L == ref.L && o.R == ref.R && std::memcmp(&o.st.notes, &ref.st.notes, 8) == 0 && o.st.seq_chopped == ref.st.seq_chopped, "block %d: output bit-identical to block 4096", b); }
  // the notes SLC-less ignore the typed pitch: transposing the notes (C-4 .. D#-4 shifted up an octave) still picks the same slices (note - ROOT mod 4), at rate 1
  { Out o; render(songJson("{\"pad\":0,\"slc\":4}", false, 60), Cfg(), 4096, o); CHECKF(o.L == ref.L, "notes an octave up: the same slices, the same output"); }
  // no chop data handed in: the sliced instrument plays the pad from its top unchopped, counted; the output differs (the first note plays 0.1 for the whole pad)
  { Cfg c; c.setChop = false; Out o; render(slc4, c, 4096, o); CHECKF(o.st.seq_chop_skipped == 4 && o.st.seq_chopped == 0 && o.L != ref.L, "no chop data: skipped (%llu) and unchopped", (unsigned long long)o.st.seq_chop_skipped); }
  // a plain instrument is untouched by chop data
  { Out a, b; Cfg c; c.setChop = false; render(songJson("{\"pad\":0}"), c, 4096, a); render(songJson("{\"pad\":0}"), Cfg(), 4096, b); CHECKF(a.L == b.L && b.st.seq_chopped == 0 && b.st.seq_chop_skipped == 0, "a plain instrument: chop data changes nothing"); }
  // SMODE hits handed in: slice 1 starts at 0.25 s of the pad (level 0.1 until 0.5 s: the slice is 0.25 .. 0.5), its duration 0.25 s
  { Cfg c; c.hitsN = 4; c.hits = {0.0, 0.25, 0.5, 1.5}; Out o; render(songJson("{\"pad\":0,\"slc\":4,\"smode\":1}"), c, 4096, o);
    CHECKF(o.st.seq_chop_no_hits == 0, "hits used"); const double l1 = mean(o.L, SR + 2000, SR + 10000); CHECKF(std::fabs(l1 - c0 * 0.1) <= 1e-4 * c0 + 1e-7, "SMODE slice 1 starts at 0.25 s of the pad (level %g, want %g)", l1, c0 * 0.1);
    CHECKF(peak(o.L, SR + 12000 + 2400, 2 * SR) == 0.0, "and it is 0.25 s long, then silent"); Cfg d = c; d.hits.clear(); Out q; render(songJson("{\"pad\":0,\"slc\":4,\"smode\":1}"), d, 4096, q); CHECKF(q.st.seq_chop_no_hits == 4, "no hits: the grid, counted"); }
  // sync 2 (stretched): counted, played unstretched at rate from ofsSec until the cut; sync 1: the rate follows the tempo
  { Cfg c; c.loop = 1.0; Out o; render(songJson("{\"pad\":0,\"sync\":2,\"beats\":4}"), c, 4096, o); CHECKF(o.st.notes_stretched == 4 && o.st.notes_played == 4, "sync 2: counted (%d) and played", o.st.notes_stretched); CHECKF(o.L.size() == (size_t)kOut && mean(o.L, 2000, 20000) > 1e-4, "sync 2 plays"); }
  // REVERSED chop (verify-chop F1): instrument rev 1, slc 4, a RAMP pad (value = frame / length). The page reads slice k back to front from its END: sample at output frame i of a note is
  // ramp((ofsSec + durSec) * SR - i) (voice.cpp: off = max(0, dur - ofsSec - durSec) is the distance from the pad's end). A voice that took off = dur - ofsSec (a mutation) starts a slice early and fails.
  { std::vector<float> ramp((size_t)kPad); for (int i = 0; i < kPad; i++) ramp[(size_t)i] = (float)i / (float)kPad;
    Cfg c; c.pad = ramp; Out o; render(songJson("{\"pad\":0,\"slc\":4,\"rev\":1}"), c, 4096, o);
    Cfg d; Out dc; render(songJson("{\"pad\":0,\"slc\":4}"), d, 4096, dc); const double g = mean(dc.L, 2000, 22000) / 0.1;   // the mixer's gain on a constant (as above)
    CHECKF(o.st.seq_chopped == 4 && g > 1e-3, "reversed chop: 4 notes chopped (%llu)", (unsigned long long)o.st.seq_chopped);
    for (int k = 0; k < 4; k++) {
      const int t0 = k * SR, a = 2000, b = 8000; double want = 0;
      for (int i = a; i < b; i++) want += (double)((k + 1) * (kPad / 4) - i) / kPad;
      want = g * want / (b - a);
      const double got = mean(o.L, t0 + a, t0 + b);
      CHECKF(std::fabs(got - want) <= 0.01 * want, "reversed note %d: reads its own slice back to front (mean %g, want %g)", k, got, want);
      // it runs down to the slice's start and stops there: the level near the end of the slice is that of the slice's first frames
      const int e0 = kPad / 4 - 2400, e1 = kPad / 4 - 400; double w2 = 0; for (int i = e0; i < e1; i++) w2 += (double)((k + 1) * (kPad / 4) - i) / kPad; w2 = g * w2 / (e1 - e0);
      const double g2 = mean(o.L, t0 + e0, t0 + e1); CHECKF(std::fabs(g2 - w2) <= 0.01 * w2 + 1e-6, "reversed note %d: near the slice's start at its end (mean %g, want %g)", k, g2, w2);
    } }
  // reversed, a slice that runs PAST the pad's end (verify-247): sync 2, beats 8 at 120 bpm: target 4 s on a 2 s loop, scale 2: ofsSec 0, durSec 4 > the pad's 2 s, so dur - ofsSec - durSec is -2 and the
  // voice's max(0, ...) puts the reader at the pad's END (ramp near 1, running down). Without the max the start is negative and the output is not that.
  { std::vector<float> ramp((size_t)kPad); for (int i = 0; i < kPad; i++) ramp[(size_t)i] = (float)i / (float)kPad;
    Cfg c; c.pad = ramp; Out o; render(songJson("{\"pad\":0,\"sync\":2,\"beats\":8,\"rev\":1}"), c, 4096, o, 2 * SR);
    Cfg d; Out dc; render(songJson("{\"pad\":0,\"slc\":4}"), d, 4096, dc); const double g = mean(dc.L, 2000, 22000) / 0.1;
    const int a0 = 2000, b0 = 8000; double want = 0; for (int i = a0; i < b0; i++) want += (double)(kPad - i) / kPad; want = g * want / (b0 - a0);
    const double got = mean(o.L, a0, b0); CHECKF(g > 1e-3 && o.st.notes_stretched >= 1 && std::fabs(got - want) <= 0.01 * want, "reversed slice past the pad's end starts at the pad's end (mean %g, want %g)", got, want); }
  // a changed pad loses its chop data
  { thunder_render_opts op; std::memset(&op, 0, sizeof op); op.size = sizeof op; op.use_frames = 1; op.frames = SR; char err[100]; const std::string sg = slc4;
    thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &op, err, sizeof err); const std::vector<float> p = levels();
    CHECK(thunder_render_set_pad(r, 0, p.data(), nullptr, kPad, SR) == 0 && thunder_render_set_pad_chop(r, 0, 2.0) == 0 && thunder_render_set_pad(r, 0, p.data(), nullptr, kPad, SR) == 0);
    std::vector<float> L(SR), R(SR); thunder_render_read(r, L.data(), R.data(), SR); thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; thunder_render_stats_get(r, &s);
    CHECKF(s.seq_chopped == 0 && s.seq_chop_skipped >= 1, "set_pad drops the pad's chop data (chopped %llu skipped %llu)", (unsigned long long)s.seq_chopped, (unsigned long long)s.seq_chop_skipped); thunder_render_free(r); }

  // ---- hostile arguments ---------------------------------------------------------------------------------------------------------------------------------
  { thunder_render_opts op; std::memset(&op, 0, sizeof op); op.size = sizeof op; op.use_frames = 1; op.frames = 2000; char err[100]; const std::string sg = slc4;
    thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &op, err, sizeof err); const double nan = std::nan(""), inf = INFINITY;
    CHECK(thunder_render_set_pad_chop(nullptr, 0, 1) == THUNDER_RENDER_E_ARG && thunder_render_set_pad_hits(nullptr, 0, 4, nullptr, 0) == THUNDER_RENDER_E_ARG);
    for (const double v : {nan, inf, -inf, -1.0, 1e-7, 1e7, 1.7e308, -1.7e308, 5e-324}) CHECK(thunder_render_set_pad_chop(r, 0, v) == THUNDER_RENDER_E_ARG);
    for (const int pad : {-1, 256, 1 << 30, -(1 << 30)}) CHECK(thunder_render_set_pad_chop(r, pad, 1.0) == THUNDER_RENDER_E_ARG);
    const double ok[] = {0, 0.5, 1, 1.5}, b1[] = {0, nan, 1, 1}, b2[] = {0, -0.1, 1, 1}, b3[] = {0, inf, 1, 1}, b4[] = {0, 1e7, 1, 1};
    CHECK(thunder_render_set_pad_hits(r, 0, 4, b1, 4) == THUNDER_RENDER_E_ARG && thunder_render_set_pad_hits(r, 0, 4, b2, 4) == THUNDER_RENDER_E_ARG && thunder_render_set_pad_hits(r, 0, 4, b3, 4) == THUNDER_RENDER_E_ARG && thunder_render_set_pad_hits(r, 0, 4, b4, 4) == THUNDER_RENDER_E_ARG);
    for (const int n : {-5, 0, 1, 65, 1 << 30}) CHECK(thunder_render_set_pad_hits(r, 0, n, ok, 4) == THUNDER_RENDER_E_ARG);
    CHECK(thunder_render_set_pad_hits(r, 0, 4, ok, 5) == THUNDER_RENDER_E_ARG && thunder_render_set_pad_hits(r, 0, 4, ok, -1) == THUNDER_RENDER_E_ARG && thunder_render_set_pad_hits(r, 0, 4, nullptr, 3) == THUNDER_RENDER_E_ARG && thunder_render_set_pad_hits(r, 256, 4, ok, 4) == THUNDER_RENDER_E_ARG);
    CHECK(thunder_render_set_pad_chop(r, 0, 2.0) == 0 && thunder_render_set_pad_hits(r, 0, 4, ok, 4) == 0 && thunder_render_set_pad_hits(r, 0, 64, ok, 4) == 0 && thunder_render_set_pad_hits(r, 0, 4, nullptr, 0) == 0 && thunder_render_set_pad_chop(r, 0, 0) == 0);
    std::vector<float> L(2000), R(2000); CHECK(thunder_render_read(r, L.data(), R.data(), 100) == 100);
    CHECK(thunder_render_set_pad_chop(r, 0, 1.0) == THUNDER_RENDER_E_STATE && thunder_render_set_pad_hits(r, 0, 4, ok, 4) == THUNDER_RENDER_E_STATE);
    thunder_render_free(r); }
  // a zero-length pad (set_pad with 0 frames removes it): chop data for it, then notes: silent, no crash; and every slc value with the real pad
  { thunder_render_opts op; std::memset(&op, 0, sizeof op); op.size = sizeof op; op.use_frames = 1; op.frames = 2 * SR; char err[100];
    for (const char* slc : {"0", "1", "2", "63", "64", "65", "-3", "-1000000", "1000000"}) for (const char* sync : {"0", "1", "2", "3"}) for (int zero = 0; zero < 2; zero++) {
      const std::string sg = songJson(std::string("{\"pad\":0,\"slc\":") + slc + ",\"sync\":" + sync + ",\"smode\":1}", zero == 1);
      thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &op, err, sizeof err); if (!r) { CHECK(false); continue; }
      const std::vector<float> p = levels(); if (!zero) CHECK(thunder_render_set_pad(r, 0, p.data(), nullptr, kPad, SR) == 0); else CHECK(thunder_render_set_pad(r, 0, nullptr, nullptr, 0, SR) == 0);
      CHECK(thunder_render_set_pad_chop(r, 0, 2.0) == 0); const double h[] = {0.4, 0.1, 5.0, 0}; thunder_render_set_pad_hits(r, 0, 4, h, 4); thunder_render_set_pad_hits(r, 0, 64, h, 4);
      std::vector<float> L(2 * SR), R(2 * SR); int64_t got = 0; while (got < 2 * SR) { const int64_t n = thunder_render_read(r, L.data() + got, R.data() + got, 4096); if (n <= 0) break; got += n; }
      bool fin = true; for (int i = 0; i < 2 * SR; i++) if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) fin = false;
      CHECKF(got == 2 * SR && fin, "slc %s sync %s zero-length %d: rendered and finite", slc, sync, zero); thunder_render_free(r); } }
  std::printf(g_fail ? "FAILED (%d)\n" : "PASS\n", g_fail);
  return g_fail ? 1 : 0;
}
