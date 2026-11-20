// LIVE ROUTE B5: the track's ONE voice list across the three players (the sampler's VoicePool, the SoundFont pool, the LivePool): core/core-processor.js T(track), pinned through Players::dispatch.
//   P2  a bass / acid / mach note is the MONO branch of liveNote even on a poly event: the sampler's voices of the track give way; a polyVox note takes nothing over
//   P3  the poly cap: a live note has no cap loop, but a live voice is in T and counts: a later sampler poly note shifts the OLDEST voice of the three pools out (the order of take / cap / choke is pinned here for the sampler and live routes)
//   mono live takes live (the node's own voice excepted), a sampler mono note takes live voices, a note nobody plays takes over (`this.take`)
//   choke: a live note chokes the sampler's and the live voices of other tracks with its group, a sampler note chokes live voices, polyVox chokes nothing
//   C4: a live voice counts in voices() from its note-on, ends after its gate is off and its output is quiet (or 4 s), and nothing counts it before
//   test_live_tracklist
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/live_pool.h"
#include "render/players.h"
#include "render/render.h"
#include "song/song.h"

using namespace thunder;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)
static const double SR = 48000;

enum { P_BASS = 0, P_SMP = 1, P_ACID = 2, P_VOX = 3, P_SMP_CK = 4, P_BASS_CK = 5, P_VOX_CK = 6 };

struct Fx {
  Song song; SeqVars vars; SeqOpts so; std::unique_ptr<Seq> seq; std::unique_ptr<RenderPads> pads; std::unique_ptr<Players> pl; std::vector<float> dc;
  Fx() {
    JsonError je; const std::string sj = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"phrases\":[[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]]}";
    if (!song.load(sj.data(), sj.size(), &je)) { std::printf("song: load failed\n"); g_fail++; }
    so.row = song.renderFrom; so.once = true; seq.reset(new Seq(song, vars, SeqMode::Song, so));
    pads.reset(new RenderPads()); dc.assign((size_t)(SR * 3), 0.25f);
    for (int p : {P_SMP, P_SMP_CK}) { pads->buf[p].data = dc.data(); pads->buf[p].len = (int)dc.size(); pads->buf[p].sr = SR; }
    pads->kit[P_SMP_CK].choke = 1; pads->kit[P_BASS_CK].choke = 1; pads->kit[P_VOX_CK].choke = 1;
    for (int p : {P_BASS, P_ACID, P_VOX, P_BASS_CK, P_VOX_CK}) pads->route[p] = PadRoute::Live;
    pl.reset(new Players(pads.get(), SR, 16));
    BassP bp; AcidP ap; VoxP vp; vp.txt = "la di da ba"; vp.sylLen = 180;
    LivePadConfig c; c.kind = LiveKind::Bass; c.bass = bp; c.trackMask = 0b1111; CHECK(pl->live().addPad(P_BASS, c)); CHECK(pl->live().addPad(P_BASS_CK, c));
    c = LivePadConfig(); c.kind = LiveKind::Acid; c.acid = ap; c.trackMask = 0b1111; CHECK(pl->live().addPad(P_ACID, c));
    c = LivePadConfig(); c.kind = LiveKind::Vox; c.vox = vp; c.trackMask = 0b1111; CHECK(pl->live().addPad(P_VOX, c)); CHECK(pl->live().addPad(P_VOX_CK, c));
    pl->setTime(0, 0);
  }
  void play(double t, int track, int pad, bool poly, int col = -1) {
    SeqEvent e; e.type = SEV_NOTE; e.t = t; e.track = track; e.pad = pad; e.rate = 1; e.gain = 0.8; e.pan = 128; e.poly = poly; e.col = col;
    CHECK(pl->dispatch(e, *seq, song));
  }
  int smp(int track, double t) { return pl->pool().liveOnTrack(track, t); }   // the sampler's voices in T at t (a voice killed at or before t is gone)
  LiveDebugNode node(int track, int pad, int j = 0) { LiveDebugNode d; if (!pl->live().debugNode(track, pad, j, &d)) { std::printf("no node %d %d %d\n", track, pad, j); g_fail++; } return d; }
  bool inT(int track, int pad, int j = 0) { return node(track, pad, j).inT; }
  bool let(int track, int pad, double t, int j = 0) { const LiveDebugNode d = node(track, pad, j); return d.curStop && std::fabs(d.stopAt - t) < 1e-12; }   // its gate went low at t
};

static void testP2() {   // a poly track: a sampler chord, then a bass note (poly event): the chord is released
  Fx f; for (int k = 0; k < 3; k++) f.play(0.10 + 0.01 * k, 0, P_SMP, true);
  CHECK(f.smp(0, 0.2) == 3);
  f.play(0.30, 0, P_BASS, true);
  CHECKF(f.smp(0, 0.30) == 0, "the sampler's chord is still in T after a bass note on a poly event (%d)", f.smp(0, 0.30));
  CHECK(f.inT(0, P_BASS));
  // acid and mach are the same branch; the chord comes back, then an acid note
  for (int k = 0; k < 3; k++) f.play(0.40 + 0.01 * k, 1, P_SMP, true); CHECK(f.smp(1, 0.5) == 3);
  f.play(0.60, 1, P_ACID, true); CHECKF(f.smp(1, 0.60) == 0, "acid: the chord survives (%d)", f.smp(1, 0.60));
  // polyVox takes nothing over: the chord stays, the live bass stays
  for (int k = 0; k < 2; k++) f.play(0.40 + 0.01 * k, 2, P_SMP, true);
  f.play(0.41, 2, P_BASS, false); CHECK(f.smp(2, 0.42) == 0);   // (a mono note on a poly-less track: control)
  for (int k = 0; k < 2; k++) f.play(0.50 + 0.01 * k, 2, P_SMP, true); CHECK(f.smp(2, 0.6) == 2);
  f.play(0.70, 2, P_VOX, true, 0); CHECKF(f.smp(2, 0.70) == 2, "a polyVox note took the sampler's voices over (%d)", f.smp(2, 0.70));
  CHECKF(f.inT(2, P_VOX, 1) || f.inT(2, P_VOX, 2) || f.inT(2, P_VOX, 3) || f.inT(2, P_VOX, 4), "the vox voice is not in T");
  CHECKF(f.inT(2, P_BASS) && !f.let(2, P_BASS, 0.70), "a polyVox note let the track's bass go");
}

static void testMonoTakesLive() {
  Fx f; f.play(0.10, 0, P_BASS, false); CHECK(f.inT(0, P_BASS));
  f.play(0.20, 0, P_ACID, false);   // mono live takes the live voice of another pad
  CHECK(f.let(0, P_BASS, 0.20)); CHECK(!f.inT(0, P_BASS)); CHECK(f.inT(0, P_ACID)); CHECK(!f.let(0, P_ACID, 0.20));
  // the node's own voice is not killed by the takeover: it is restarted (the gate is cancelled and set again), not let go
  f.play(0.30, 0, P_ACID, false); CHECK(f.inT(0, P_ACID)); CHECKF(!f.node(0, P_ACID).curStop, "the node's own voice was let go by its own note");
  // a sampler MONO note takes the live voices of the track (kill at its time); a sampler POLY note does not
  f.play(0.40, 0, P_SMP, true); CHECK(f.inT(0, P_ACID) && !f.let(0, P_ACID, 0.40));
  f.play(0.50, 0, P_SMP, false); CHECK(f.let(0, P_ACID, 0.50)); CHECK(!f.inT(0, P_ACID)); CHECK(f.smp(0, 0.50) == 1);
  // another track is not touched
  f.play(0.55, 1, P_BASS, false); f.play(0.60, 0, P_SMP, false); CHECK(f.inT(1, P_BASS) && !f.node(1, P_BASS).curStop);
  // a note nobody plays (a live pad with no live params; a track outside the mask) takes over like any (`this.take`)
  Fx g; g.pads->route[7] = PadRoute::Live; g.play(0.10, 0, P_BASS, false); g.play(0.20, 0, 7, false);
  CHECKF(g.let(0, P_BASS, 0.20), "a live note with no node did not take the live voice over"); g.play(0.30, 0, P_ACID, true); g.play(0.40, 0, 7, true); CHECKF(!g.let(0, P_ACID, 0.40), "a POLY note with no node killed a voice");
}

static void testPolyCap() {   // P3
  { Fx f;   // the live voice is the oldest: the fourth sampler poly note after it shifts it out
    f.play(0.10, 0, P_VOX, true, 0);
    for (int k = 0; k < 3; k++) f.play(0.20 + 0.01 * k, 0, P_SMP, true);
    CHECK(f.smp(0, 0.3) == 3);
    int vj = 0; for (int j = 1; j <= 4; j++) if (f.inT(0, P_VOX, j)) vj = j; CHECK(vj > 0);
    f.play(0.40, 0, P_SMP, true);   // T holds 4 (1 live + 3 sampler): the oldest is the live voice
    CHECKF(f.let(0, P_VOX, 0.40, vj) && !f.inT(0, P_VOX, vj), "the cap did not shift the oldest voice (a live one) out");
    CHECKF(f.smp(0, 0.40) == 4, "the sampler lost a voice instead (%d)", f.smp(0, 0.40));
    f.play(0.50, 0, P_SMP, true);   // now four sampler voices: the oldest sampler voice goes
    CHECKF(f.smp(0, 0.50) == 4, "cap: %d sampler voices", f.smp(0, 0.50)); }
  { Fx f;   // a live note has no cap loop of its own: four sampler voices, then a polyVox note: five in T; the next sampler note takes two out
    for (int k = 0; k < 4; k++) f.play(0.10 + 0.01 * k, 0, P_SMP, true);
    f.play(0.30, 0, P_VOX, true, 0);
    CHECKF(f.smp(0, 0.30) == 4, "a live note shifted a sampler voice out (%d)", f.smp(0, 0.30));
    int vj = 0; for (int j = 1; j <= 4; j++) if (f.inT(0, P_VOX, j)) vj = j; CHECK(vj > 0);
    f.play(0.40, 0, P_SMP, true);   // 5 in T: the two oldest (sampler voices; the live one is the newest) go, then this one joins: 3 + 1
    CHECKF(f.smp(0, 0.40) == 3 && f.inT(0, P_VOX, vj) && !f.let(0, P_VOX, 0.40, vj), "cap with 5 in T: sampler %d, live in T %d", f.smp(0, 0.40), (int)f.inT(0, P_VOX, vj)); }
  { Fx f;   // the order in the middle: sampler, live, sampler, sampler: the fourth cap releases the first sampler voice, the next the live one
    f.play(0.10, 0, P_SMP, true); f.play(0.20, 0, P_VOX, true, 0); f.play(0.30, 0, P_SMP, true); f.play(0.40, 0, P_SMP, true);
    int vj = 0; for (int j = 1; j <= 4; j++) if (f.inT(0, P_VOX, j)) vj = j;
    f.play(0.50, 0, P_SMP, true); CHECKF(f.inT(0, P_VOX, vj) && f.smp(0, 0.50) == 3, "the first sampler voice should go first (sampler %d)", f.smp(0, 0.50));
    f.play(0.60, 0, P_SMP, true); CHECKF(f.let(0, P_VOX, 0.60, vj), "then the live voice"); }
}

static void testChoke() {
  { Fx f; f.play(0.10, 1, P_SMP_CK, true); CHECK(f.smp(1, 0.2) == 1); f.play(0.30, 2, P_BASS_CK, false); CHECKF(f.smp(1, 0.30) == 0, "a live note did not choke the sampler's voice of its group"); }
  { Fx f; f.play(0.10, 2, P_BASS_CK, false); f.play(0.30, 1, P_SMP_CK, true); CHECKF(f.let(2, P_BASS_CK, 0.30), "a sampler note did not choke the live voice of its group"); }
  { Fx f; f.play(0.10, 1, P_SMP_CK, true); f.play(0.30, 2, P_VOX_CK, true, 0); CHECKF(f.smp(1, 0.30) == 1, "a polyVox note chokes nothing"); }   // (liveNote's poly branches have no choke loop)
  { Fx f; f.play(0.10, 2, P_VOX_CK, true, 0); int vj = 0; for (int j = 1; j <= 4; j++) if (f.inT(2, P_VOX_CK, j)) vj = j; CHECK(vj > 0);
    f.play(0.30, 1, P_SMP_CK, true); CHECKF(f.let(2, P_VOX_CK, 0.30, vj), "a sampler note did not choke a polyVox voice of its group"); }
  { Fx f; f.play(0.10, 2, P_BASS_CK, false); f.play(0.30, 3, P_BASS_CK, false); CHECKF(f.let(2, P_BASS_CK, 0.30), "live chokes live across tracks"); CHECK(!f.let(3, P_BASS_CK, 0.30)); }
}

static void testEnding() {   // C4
  Fx f; Players& pl = *f.pl; float bl[128], br[128]; float* inL[kVoiceTracks] = {}; float* inR[kVoiceTracks] = {}; inL[0] = bl; inR[0] = br;
  SeqEvent e; e.type = SEV_NOTE; e.t = 0.02; e.track = 0; e.pad = P_BASS; e.rate = 1; e.gain = 0.8; e.pan = 128; e.has = SH_REL; e.rel = 0.06;
  CHECK(pl.voices() == 0); bool seen = false; double endedAt = -1;
  for (int q = 0; q < 48000 / 128 * 6; q++) {
    const int64_t q0 = (int64_t)q * 128; const double t0 = (double)q0 / SR; pl.setTime(t0, q0);
    if (q == 0) pl.dispatch(e, *f.seq, f.song);
    std::memset(bl, 0, sizeof bl); std::memset(br, 0, sizeof br); pl.render(inL, inR, 128, t0, q0);
    if (t0 < 0.02 + 0.015 && t0 > 0.005) CHECK(pl.voices() == 1);   // counts from the note-on (the shell is lat samples ahead; the count is the pool's)
    if (t0 > 0.03 && t0 < 0.059) CHECKF(pl.voices() == 1, "voices %d while the note is held (t %g)", pl.voices(), t0);
    if (t0 > 0.5 && pl.voices() == 1 && !seen) seen = true;
    if (pl.voices() == 0 && endedAt < 0 && t0 > 0.03) endedAt = t0;
  }
  CHECKF(endedAt > 0.06 && endedAt <= 0.06 + 4.0 + 0.01, "the voice ended at %g (the off is at 0.06; 4 s at the latest)", endedAt);
  std::printf("a bass note off at 0.06 s ended at %.3f s (quiet for 4096 samples, or 4 s)\n", endedAt);
  // held: no off, no end
  Fx g; SeqEvent h = e; h.has = 0; h.t = 0.02; g.pl->dispatch(h, *g.seq, g.song); bool still = true;
  for (int q = 0; q < 48000 / 128 * 6; q++) { const int64_t q0 = (int64_t)q * 128; g.pl->setTime((double)q0 / SR, q0); std::memset(bl, 0, sizeof bl); std::memset(br, 0, sizeof br); g.pl->render(inL, inR, 128, (double)q0 / SR, q0); if (g.pl->voices() != 1) still = false; }
  CHECKF(still, "a held live voice ended by itself");
  g.pl->songOver(6.0); for (int q = 0; q < 48000 / 128 * 6; q++) { const int64_t q0 = (int64_t)(48000 * 6 + q * 128); g.pl->setTime((double)q0 / SR, q0); std::memset(bl, 0, sizeof bl); std::memset(br, 0, sizeof br); g.pl->render(inL, inR, 128, (double)q0 / SR, q0); }
  CHECKF(g.pl->voices() == 0, "songOver did not release the held voice (%d)", g.pl->voices());
}

int main() {
  testP2(); testMonoTakesLive(); testPolyCap(); testChoke(); testEnding();
  if (g_fail) { std::printf("live_tracklist: %d FAILED\n", g_fail); return 1; }
  std::printf("live_tracklist: all checks passed\n"); return 0;
}
