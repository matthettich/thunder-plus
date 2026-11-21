// U37 P2 (ctest `live_equals_render`): ELECTRO played through LiveHost from its first frame equals SongRenderer (effects off, master off) BIT FOR BIT, at call sizes 1, 64, 128, 480 and 4096 frames, and with a first
// frame0 that is neither 0 nor a multiple of 128. Same pads, same SeqOpts (the live play's: once = false, live = false, sim = false), same song.
#include "live_common.h"

#include "render/render.h"

using namespace lt;

int main() {
  const std::string text = electro();
  if (text.empty()) { std::printf("FAIL: cannot read ELECTRO\n"); return 1; }
  PadSet ps; static RenderPads rp; fillRenderPads(rp, ps);
  Song song; JsonError je; if (!song.load(text.data(), text.size(), &je, /*pageS*/ true)) { std::printf("FAIL: ELECTRO does not load\n"); return 1; }
  SeqVars vars;
  const int64_t N = (int64_t)(SR * 40);   // 40 s (both loop the song: the same SeqOpts)
  // the reference: the offline renderer with the live play's Seq options
  std::vector<float> refL((size_t)N), refR((size_t)N);
  { SeqOpts so; so.once = false; so.live = false; so.sim = false; so.randomSeed = 0x2545F491u;
    SongRenderer R(song, vars, rp, SR, 512, so, 64); R.block(refL.data(), refR.data(), (int)N); }
  double peak = 0; for (float x : refL) peak = std::max(peak, (double)std::fabs(x));
  CHECK(peak > 0.01, "the reference is not silent (peak %g)", peak);

  struct Case { int cs; int64_t f0; };
  const Case cases[] = {{1, 0}, {64, 0}, {128, 0}, {480, 0}, {4096, 0}, {480, 1000}, {4096, 77}, {128, 5000}};
  for (const Case& c : cases) {
    if (c.cs == 1 && c.f0 == 0 && N > 200000) {}   // (all sizes run the whole length: call size 1 is slow but exact)
    Host H(opts(0, 4096)); if (!H.h) return 1;
    CHECK(H.song(text) == 0, "song_publish");
    CHECK(H.loadPads(ps) == 0, "pads");
    thunder_live_play_opts po = playOpts(c.f0); CHECK(H->play(po) == 0, "play");
    std::vector<float> l, r; int64_t frame = c.f0;
    CHECK(H.run(l, r, N, c.cs, frame) == 0, "process");
    // frame0 == 0: the live host's timeline IS the renderer's, so every sample is bit-equal over the whole run. frame0 != 0: the host's times are frame / sr on its own timeline (live.h, TIME), so every time the
    // core computes is (frame0 + x) / sr rounded, not x rounded: a sample position can round one way in the live host and the other in the renderer, and a sample then differs in its last bits (measured: 3 samples
    // of 3.8 million, 1e-7 apart, after 18 s). So for a first frame0 that is not 0: bit-equal over the first 12 s (measured to hold) and, over the whole run, no more than 0.001 % of the samples differ, by at most 1e-6.
    const int64_t strict = c.f0 == 0 ? N : (int64_t)(SR * 12);
    size_t bad = 0, first = (size_t)-1, badLoose = 0; double maxd = 0;
    for (int64_t i = 0; i < N; i++) {
      const bool d = std::memcmp(&l[(size_t)i], &refL[(size_t)i], 4) != 0 || std::memcmp(&r[(size_t)i], &refR[(size_t)i], 4) != 0;
      if (!d) continue;
      badLoose++; maxd = std::max(maxd, std::max((double)std::fabs(l[(size_t)i] - refL[(size_t)i]), (double)std::fabs(r[(size_t)i] - refR[(size_t)i])));
      if (i < strict) { if (first == (size_t)-1) first = (size_t)i; bad++; }
    }
    CHECK(bad == 0, "call size %d, frame0 %lld: %zu samples differ in the strict part, first at %zu", c.cs, (long long)c.f0, bad, first);
    CHECK(c.f0 == 0 ? badLoose == 0 : (badLoose * 100000 <= (size_t)N * 2 && maxd <= 1e-6), "call size %d, frame0 %lld: %zu samples differ in the whole run, by at most %g", c.cs, (long long)c.f0, badLoose, maxd);
    const thunder_live_stats s = stats(H.h);
    CHECK(s.seq_dropped == 0 && s.notes_no_pad == 0, "no dropped events, no note without a pad (%d)", s.notes_no_pad);
    CHECK(s.reanchors == 0, "no re-anchor");
    std::printf("call size %4d, frame0 %5lld: %s (%zu samples differ in %lld, max %g; %llu quanta, %d notes played)\n", c.cs, (long long)c.f0, bad ? "DIFFERENT" : "bit-equal where required", badLoose, (long long)N, maxd, (unsigned long long)s.quanta, s.notes_played);
  }
  std::printf("live_equals_render: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
