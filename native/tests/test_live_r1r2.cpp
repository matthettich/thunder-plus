// U37 P3 (ctest `live_r1r2`): the two follow-ups of the P2 verifier.
// R1: a song_publish made after the audio side gave a started play back (collect() lowers playOut_) but before the quantum stored the state PLAYING took the IDLE path: the running song missed its edit.
//     The test seam runs collect() + song_publish in exactly that window; the edit must be adopted in the SAME quantum (SONG record at frame 0), not acknowledged a quantum later (frame 128).
// R2: test_once / test_sim of thunder_live_play_opts are E_ARG unless the host was created with F_DUMP_STEPS.
#include "live_common.h"
using namespace lt;

struct Ctx { Host* H; const std::string* text; int rc = -99; uint32_t pending = 99; };
static void seam(void* u) { Ctx* c = (Ctx*)u; c->H->h->collect(); c->rc = c->H->song(*c->text, 2); c->pending = stats(c->H->h).song_pending; }

int main() {
  const std::string text = electro(); PadSet ps;
  {
    Host H(opts(0, 4096)); H.song(text, 1); H.loadPads(ps);
    Ctx c{&H, &text}; H->setTestAudioSeam(seam, &c);
    thunder_live_play_opts po = playOpts(0); CHECK(H->play(po) == 0, "play");
    int64_t frame = 0; CHECK(H.run(512, 128, frame) == 0, "process");
    CHECK(c.rc == 0, "the publish inside the window returned %d", c.rc);
    CHECK(c.pending == 1, "R1: the publish inside the window built a successor snapshot for the running play (song_pending %u; 0 = it took the IDLE path)", c.pending);
    int64_t f2 = -1; for (auto& r : H.poll()) if (evType(r) == THUNDER_LIVE_EV_SONG && evAs<thunder_live_ev_song>(r).rev == 2) f2 = evF(r);
    CHECK(f2 == 0, "R1: the edit (rev 2) was adopted by the running play in the same quantum (SONG record at frame %lld, expected 0)", (long long)f2);
  }
  {
    Host H(opts(0, 4096)); H.song(text); H.loadPads(ps);
    thunder_live_play_opts a = playOpts(0); a.test_once = 1; thunder_live_play_opts b = playOpts(0); b.test_sim = 1;
    CHECK(H->play(a) == THUNDER_LIVE_E_ARG && H->play(b) == THUNDER_LIVE_E_ARG, "R2: test_once / test_sim without F_DUMP_STEPS are E_ARG");
    CHECK(H->play(playOpts(0)) == 0, "a normal play is fine");
    Host D(opts(THUNDER_LIVE_F_DUMP_STEPS, 4096)); D.song(text); D.loadPads(ps);
    CHECK(D->play(a) == 0 && D->play(b) == 0, "R2: with F_DUMP_STEPS they are accepted");
  }
  std::printf("live_r1r2: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
