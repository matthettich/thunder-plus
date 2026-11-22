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

  // THE ENGINE HAS ITS OWN GAPLESS TIMELINE (live.h TIME): the host's frame0 is mapped by one offset, so the output is the offline render BIT FOR BIT for ANY frame0, at every call size, for the whole run.
  struct Case { int cs; int64_t f0; };
  const Case cases[] = {{1, 0}, {64, 0}, {128, 0}, {480, 0}, {4096, 0}, {127, 37}, {129, 37}, {480, 1000}, {4096, 77}, {128, 5000}, {333, (int64_t)1 << 31}, {4096, (int64_t)1 << 31}, {128, 1000000000000LL}, {1024, 1000000000000LL}};
  for (const Case& c : cases) {
    Host H(opts(0, 4096)); if (!H.h) return 1;
    CHECK(H.song(text) == 0, "song_publish");
    CHECK(H.loadPads(ps) == 0, "pads");
    thunder_live_play_opts po = playOpts(c.f0); CHECK(H->play(po) == 0, "play");
    std::vector<float> l, r; int64_t frame = c.f0;
    CHECK(H.run(l, r, N, c.cs, frame) == 0, "process");
    size_t bad = 0, first = (size_t)-1;
    for (int64_t i = 0; i < N; i++) if (std::memcmp(&l[(size_t)i], &refL[(size_t)i], 4) != 0 || std::memcmp(&r[(size_t)i], &refR[(size_t)i], 4) != 0) { if (first == (size_t)-1) first = (size_t)i; bad++; }
    CHECK(bad == 0, "call size %d, frame0 %lld: %zu samples differ over the whole run, first at %zu", c.cs, (long long)c.f0, bad, first);
    const size_t badLoose = bad; const double maxd = 0;
    const thunder_live_stats s = stats(H.h);
    CHECK(s.seq_dropped == 0 && s.notes_no_pad == 0, "no dropped events, no note without a pad (%d)", s.notes_no_pad);
    CHECK(s.reanchors == 0, "no re-anchor");
    std::printf("call size %4d, frame0 %5lld: %s (%zu samples differ in %lld, max %g; %llu quanta, %d notes played)\n", c.cs, (long long)c.f0, bad ? "DIFFERENT" : "bit-equal where required", badLoose, (long long)N, maxd, (unsigned long long)s.quanta, s.notes_played);
  }
  // RE-ANCHORS move nothing inside the engine: 10 s, then a backward jump (frame0 = 0), then a forward gap of 5 s, the call sizes not multiples of the quantum (the seam falls inside a quantum): the output is the
  // uninterrupted offline render sample for sample, CLOCK and POS records keep coming, and nothing is repeated or skipped.
  for (int kind = 0; kind < 2; kind++) {
    Host H(opts(0, 4096)); if (!H.h) return 1; H.song(text); H.loadPads(ps); thunder_live_play_opts po = playOpts(5000); H->play(po);
    std::vector<float> l, r; int64_t frame = 5000; const int64_t A = (int64_t)(SR * 10) + 37, B = (int64_t)(SR * 8) + 11;
    H.run(l, r, A, 333, frame);
    const auto before = H.poll(); frame = kind == 0 ? 0 : frame + (int64_t)(SR * 5);   // backward / forward
    H.run(l, r, B, 333, frame);
    size_t bad = 0, first = (size_t)-1; for (size_t i = 0; i < l.size(); i++) if (std::memcmp(&l[i], &refL[i], 4) != 0 || std::memcmp(&r[i], &refR[i], 4) != 0) { if (first == (size_t)-1) first = i; bad++; }
    CHECK(l.size() == (size_t)(A + B) && bad == 0, "%s re-anchor: the output across the seam equals the uninterrupted render (%zu of %zu samples differ, first at %zu)", kind ? "forward" : "backward", bad, l.size(), first);
    const auto s = stats(H.h); CHECK(s.reanchors == 1 && s.seq_dropped == 0, "one re-anchor counted (%u)", s.reanchors);
    int clock2 = 0, pos2 = 0; int64_t lastF = -1; bool ordered = true; for (auto& rec : H.poll()) { if (evType(rec) == THUNDER_LIVE_EV_CLOCK) clock2++; if (evType(rec) == THUNDER_LIVE_EV_POS) pos2++; if (evType(rec) == THUNDER_LIVE_EV_CLOCK) { if (evF(rec) <= lastF) ordered = false; lastF = evF(rec); } }
    CHECK(clock2 >= 70 && pos2 >= 50 && ordered, "after the seam CLOCK (%d) and POS (%d) records keep coming, their host frames ascending", clock2, pos2); (void)before;
  }
  // frame0 near INT64_MAX is E_ARG at the stated bound (2^62), outputs zeroed, nothing overflows
  { Host H(opts()); std::vector<float> a(128, 1.f), b(128, 1.f); CHECK(H->process(a.data(), b.data(), 128, INT64_MAX - 10, 0) == THUNDER_LIVE_E_ARG && a[0] == 0 && H->process(a.data(), b.data(), 128, ((int64_t)1 << 62) + 1, 0) == THUNDER_LIVE_E_ARG, "frame0 above 2^62 is E_ARG");
    CHECK(H->process(a.data(), b.data(), 128, (int64_t)1 << 62, 0) == 0, "2^62 itself is accepted"); CHECK(H->bpm(100, INT64_MAX) == THUNDER_LIVE_E_ARG && H->stop(((int64_t)1 << 62) + 1) == THUNDER_LIVE_E_ARG, "`at` above 2^62 is E_ARG"); }
  std::printf("live_equals_render: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
