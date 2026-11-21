// Players (render/players.h) driven DIRECTLY, without SongRenderer (docs/port/FOLLOWUPS-20NOV-NOTES.md item 6; verify-260 section 7).
// Before this the code that decides what a note does to the OTHER pool's voices (the poly cap shared by the sampler's pool and the SoundFont pool, the takeover of both pools,
// the choke across tracks and pools, serials that give the age across pools) was guarded only by render_sf, through whole songs. Here every case is one dispatch() call
// and the observable is the two pools' voice lists on the track (liveOnTrack) right after it, plus the routing counters.
//   P1  the cap of four is shared: a poly SF note over four sampler voices releases the OLDEST (a sampler voice); a poly sampler note over SF-first voices releases the SF voice (age by serial, not
//       by pool); the same with the other mix of routes
//   P2  a mono note takes the track over in BOTH pools, whichever route it is (sampler, SoundFont), and a Live-routed note and a missing variation take it over without sounding
//   P3  a poly note refused by the sampler (a buffer the pool cannot play) touches nothing (no cap, no takeover)
//   P4  the choke: a note of a choke group silences the group's voices on OTHER tracks in both pools, and none on its own track
//   P5  off and stp end both pools; mod is accepted (its effect is in the output, not in the voice lists)
//   P6  setPads re-points the pads between calls
// WHAT THIS CANNOT PIN (found by mutation, see the notes): the order of `take` and `capPoly` inside note() is not observable. For a poly note take() only drops dead voices (capPoly's liveOnTrack
// does the same) and chokes OTHER tracks (the cap counts this track); for a mono note capPoly returns before it counts. So swapping those two calls does not change any voice list, any output
// sample or any counter, and no test can tell. What the test does pin is everything the order exists for: the cap counts across the pools, the oldest goes (by serial), and the takeover and
// the choke reach both pools.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "render/players.h"
#include "sf/sf.h"
#include "sf/sf_live.h"
#include "song/song.h"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif
using namespace thunder;
static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf("" __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

static std::vector<uint8_t> readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }

// A song only for the Seq that Players reads the arp table and the tempo from (no event of it is used).
static const char* kSong = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"insts\":[{\"pad\":0,\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}],\"phrases\":[[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]]}";

constexpr int kPadSr = 48000, kPadLen = 48000 * 4;   // pads of 4 s: no voice ends by itself in these tests
constexpr int PAD_SMP_A = 0, PAD_SMP_B = 1, PAD_SF = 2, PAD_SF_CHOKE = 3, PAD_SMP_CHOKE = 4, PAD_TINY = 5, PAD_LIVE = 6, PAD_NONE = 7;

struct Rig {
  Song song; SeqVars vars; std::vector<float> dc; sf::Font font; const sf::Preset* pr = nullptr; sf::PresetPcm pcm; RenderPads pads;
  std::unique_ptr<Seq> seq; std::unique_ptr<Players> P; std::vector<uint8_t> gm; float tiny[1] = {0.1f};
  bool ok = false;
  Rig() : dc((size_t)kPadLen, 0.1f) {
    JsonError je; if (!song.load(kSong, std::strlen(kSong), &je)) { std::printf("song does not load\n"); return; }
    gm = readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3");
    if (gm.empty() || sf::parse(gm.data(), gm.size(), font) != sf::kParseOk) { std::printf("font does not load\n"); return; }
    pr = sf::sf_preset(font, 0, 19); uint8_t flags[128] = {}; flags[60] = 1; sf::DecodeOpts dop;
    if (!pr || sf::prepare_preset(font, pr, dop, pcm, flags) != sf::kPrepOk) { std::printf("preset does not prepare\n"); return; }
    for (int p : {PAD_SMP_A, PAD_SMP_B, PAD_SMP_CHOKE}) { pads.buf[p].data = dc.data(); pads.buf[p].len = kPadLen; pads.buf[p].sr = kPadSr; }
    pads.kit[PAD_SMP_CHOKE].choke = 3;
    for (int p : {PAD_SF, PAD_SF_CHOKE}) { pads.route[p] = PadRoute::SoundFont; pads.sf[p].font = &font; pads.sf[p].pr = pr; pads.sf[p].pcm = &pcm; pads.sf[p].P.bank = 0; pads.sf[p].P.prog = 19; }
    pads.kit[PAD_SF_CHOKE].choke = 3;
    pads.buf[PAD_TINY].data = tiny; pads.buf[PAD_TINY].len = 1; pads.buf[PAD_TINY].sr = kPadSr;   // one sample: the pool refuses it (VoicePool::playable)
    pads.route[PAD_LIVE] = PadRoute::Live;                                                         // a live-engine pad: silent here, takes the track over
    SeqOpts so; so.row = song.renderFrom; so.once = true;
    seq.reset(new Seq(song, vars, SeqMode::Song, so));
    P.reset(new Players(&pads, kPadSr, 64)); P->setTime(0, 0);
    ok = true;
  }
  // a note on `track`, `pad`, at t; rate 1 (the SoundFont pad plays key 60, the one prepared)
  static SeqEvent note(int track, int pad, double t, bool poly) { SeqEvent e; e.type = SEV_NOTE; e.track = track; e.pad = pad; e.ii = 0; e.t = t; e.poly = poly; e.rate = 1; e.gain = 1; e.pan = 128; e.vel = 1; return e; }
  void go(int track, int pad, double t, bool poly) { P->dispatch(note(track, pad, t, poly), *seq, song); }
  int smp(int track, double t) { return P->pool().liveOnTrack(track, t); }
  int sfv(int track, double t) { return P->sfPool().liveOnTrack(track, t); }
};

int main() {
  Rig r; if (!r.ok) { std::printf("FAIL: the rig does not build\n"); return 1; }
  const double Q = 2.0;   // the time every list is asked at: after every note below and before any 4 s pad ends

  // ---- P1: the cap of four counts both pools; the oldest (smallest serial) goes ------------------------------------------------------------------------------------------------------
  { Rig a; double t = 0.01;
    for (int i = 0; i < 4; i++) a.go(0, PAD_SMP_A, t += 0.01, true);
    CHECK(a.smp(0, Q) == 4 && a.sfv(0, Q) == 0, "four sampler voices: %d / %d", a.smp(0, Q), a.sfv(0, Q));
    a.go(0, PAD_SF, t += 0.01, true);
    CHECK(a.smp(0, Q) == 3 && a.sfv(0, Q) == 1, "a poly SF note over four sampler voices: the oldest SAMPLER voice goes (%d sampler, %d sf)", a.smp(0, Q), a.sfv(0, Q));
    a.go(0, PAD_SMP_A, t += 0.01, true);
    CHECK(a.smp(0, Q) == 3 && a.sfv(0, Q) == 1, "a poly sampler note over 3 + 1: the next oldest sampler voice goes (%d sampler, %d sf)", a.smp(0, Q), a.sfv(0, Q));
    a.go(0, PAD_SF, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true);
    CHECK(a.smp(0, Q) + a.sfv(0, Q) == 4, "the track never holds more than four voices in all (%d + %d)", a.smp(0, Q), a.sfv(0, Q)); }
  { Rig a; double t = 0.01;   // the SF voice is the OLDEST: it is the one released, though the sampler pool is the fuller
    a.go(0, PAD_SF, t += 0.01, true); for (int i = 0; i < 3; i++) a.go(0, PAD_SMP_A, t += 0.01, true);
    CHECK(a.smp(0, Q) == 3 && a.sfv(0, Q) == 1, "SF first, then three sampler voices: %d / %d", a.smp(0, Q), a.sfv(0, Q));
    a.go(0, PAD_SMP_B, t += 0.01, true);
    CHECK(a.smp(0, Q) == 4 && a.sfv(0, Q) == 0, "a fourth sampler note releases the older SF voice, not a sampler voice (%d sampler, %d sf)", a.smp(0, Q), a.sfv(0, Q)); }
  { Rig a; double t = 0.01;   // alternating routes: the age is one count across both pools
    a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true); a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true);
    CHECK(a.smp(0, Q) == 2 && a.sfv(0, Q) == 2, "alternating: 2 + 2 (%d / %d)", a.smp(0, Q), a.sfv(0, Q));
    a.go(0, PAD_SMP_B, t += 0.01, true);   // the oldest of all is the first sampler voice
    CHECK(a.smp(0, Q) == 2 && a.sfv(0, Q) == 2, "the fifth note (sampler) releases the oldest, a sampler voice: 2 + 2 (%d / %d)", a.smp(0, Q), a.sfv(0, Q));
    a.go(0, PAD_SF, t += 0.01, true);      // now the oldest is the first SF voice
    CHECK(a.smp(0, Q) == 2 && a.sfv(0, Q) == 2, "the sixth (SF) releases the oldest, an SF voice: 2 + 2 (%d / %d)", a.smp(0, Q), a.sfv(0, Q)); }
  { Rig a; double t = 0.01;   // another track's voices do not count
    for (int i = 0; i < 4; i++) a.go(1, PAD_SMP_A, t += 0.01, true);
    a.go(0, PAD_SF, t += 0.01, true); a.go(0, PAD_SMP_A, t += 0.01, true);
    CHECK(a.smp(1, Q) == 4 && a.smp(0, Q) == 1 && a.sfv(0, Q) == 1, "the cap is per track (%d / %d / %d)", a.smp(1, Q), a.smp(0, Q), a.sfv(0, Q)); }

  // ---- P2: the takeover reaches both pools --------------------------------------------------------------------------------------------------------------------------------------------
  { Rig a; double t = 0.01;
    a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true);
    CHECK(a.smp(0, Q) == 2 && a.sfv(0, Q) == 2, "set up: 2 + 2 (%d / %d)", a.smp(0, Q), a.sfv(0, Q));
    a.go(0, PAD_SMP_B, t += 0.01, false);
    CHECK(a.smp(0, Q) == 1 && a.sfv(0, Q) == 0, "a MONO sampler note takes over the SoundFont voices as well (%d sampler, %d sf)", a.smp(0, Q), a.sfv(0, Q)); }
  { Rig a; double t = 0.01;
    a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true);
    a.go(0, PAD_SF, t += 0.01, false);
    CHECK(a.smp(0, Q) == 0 && a.sfv(0, Q) == 1, "a MONO SF note takes over the sampler voices as well (%d sampler, %d sf)", a.smp(0, Q), a.sfv(0, Q)); }
  { Rig a; double t = 0.01;
    a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true);
    a.go(0, PAD_LIVE, t += 0.01, false);
    CHECK(a.smp(0, Q) == 0 && a.sfv(0, Q) == 0 && a.P->stats().notesLive == 1, "a mono Live-routed note (silent here) takes the track over in both pools (%d / %d, notesLive %d)", a.smp(0, Q), a.sfv(0, Q), a.P->stats().notesLive);
    a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true);
    a.go(0, PAD_LIVE, t += 0.01, true);
    CHECK(a.smp(0, Q) == 1 && a.sfv(0, Q) == 1, "a POLY Live-routed note takes nothing over (%d / %d)", a.smp(0, Q), a.sfv(0, Q)); }
  { Rig a; double t = 0.01;   // a pad the kit has no buffer for: silent, touches nothing (the page returns before the takeover)
    a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true);
    a.go(0, PAD_NONE, t += 0.01, false);
    CHECK(a.smp(0, Q) == 1 && a.sfv(0, Q) == 1 && a.P->stats().notesNoPad == 1, "a note whose pad has no buffer leaves the track as it was (%d / %d, notesNoPad %d)", a.smp(0, Q), a.sfv(0, Q), a.P->stats().notesNoPad); }

  // ---- P3: a poly sampler note the pool refuses touches nothing -------------------------------------------------------------------------------------------------------------------------
  { Rig a; double t = 0.01;
    for (int i = 0; i < 4; i++) a.go(0, PAD_SMP_A, t += 0.01, true);
    a.go(0, PAD_TINY, t += 0.01, true);
    CHECK(a.smp(0, Q) == 4 && a.P->stats().notesPlayed == 4, "a refused poly note releases nobody (%d sampler, %d played)", a.smp(0, Q), a.P->stats().notesPlayed);
    a.go(0, PAD_SMP_B, t += 0.01, true);
    CHECK(a.smp(0, Q) == 4 && a.P->stats().notesPlayed == 5, "and the cap works as before afterwards (%d, %d)", a.smp(0, Q), a.P->stats().notesPlayed); }

  // ---- P4: the choke ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
  { Rig a; double t = 0.01;   // group 3 on tracks 1 (sampler pad) and 2 (SF pad); an unrelated pad on track 3; a choke-group note on track 0 silences 1 and 2, not 3, not itself
    a.go(1, PAD_SMP_CHOKE, t += 0.01, true); a.go(2, PAD_SF_CHOKE, t += 0.01, true); a.go(3, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SMP_CHOKE, t += 0.01, true);
    CHECK(a.smp(1, Q) == 0, "the choked sampler voice on another track is gone (%d)", a.smp(1, Q));
    CHECK(a.sfv(2, Q) == 0, "the choked SoundFont voice on another track is gone, a sampler note chokes the other pool (%d)", a.sfv(2, Q));
    CHECK(a.smp(3, Q) == 1 && a.smp(0, Q) == 1, "an unrelated track and the choking note's own track stay (%d, %d)", a.smp(3, Q), a.smp(0, Q)); }
  { Rig a; double t = 0.01;   // and from the SF side
    a.go(1, PAD_SMP_CHOKE, t += 0.01, true); a.go(2, PAD_SF_CHOKE, t += 0.01, true); a.go(0, PAD_SF_CHOKE, t += 0.01, true);
    CHECK(a.smp(1, Q) == 0 && a.sfv(2, Q) == 0 && a.sfv(0, Q) == 1, "an SF note of the group chokes both pools on the other tracks (%d, %d), keeps its own (%d)", a.smp(1, Q), a.sfv(2, Q), a.sfv(0, Q)); }
  { Rig a; double t = 0.01;   // same track, same group: nothing is choked (the group acts across tracks)
    a.go(1, PAD_SMP_CHOKE, t += 0.01, true); a.go(1, PAD_SF_CHOKE, t += 0.01, true);
    CHECK(a.smp(1, Q) == 1 && a.sfv(1, Q) == 1, "a choke group does not cut its own track (%d / %d)", a.smp(1, Q), a.sfv(1, Q)); }

  // ---- P5: off and stp reach both pools -----------------------------------------------------------------------------------------------------------------------------------------------
  { Rig a; double t = 0.01;
    a.go(0, PAD_SMP_A, t += 0.01, true); a.go(0, PAD_SF, t += 0.01, true); a.go(5, PAD_SMP_A, t += 0.01, true);
    SeqEvent off; off.type = SEV_OFF; off.track = 0; off.t = t += 0.01; a.P->dispatch(off, *a.seq, a.song);
    CHECK(a.smp(0, Q) == 0 && a.sfv(0, Q) == 0 && a.smp(5, Q) == 1, "off ends the track in both pools, not another track (%d / %d / %d)", a.smp(0, Q), a.sfv(0, Q), a.smp(5, Q));
    // stp ends the track in both pools (the web's stop-note); mod reaches both pools (a pitch change is audible only in the output, so this checks that it is accepted and kills nothing)
    a.go(7, PAD_SMP_A, t += 0.01, true); a.go(7, PAD_SF, t += 0.01, true);
    SeqEvent mod; mod.type = SEV_MOD; mod.track = 7; mod.t = t += 0.01; mod.rate = 1; CHECK(a.P->dispatch(mod, *a.seq, a.song) && a.smp(7, Q) == 1 && a.sfv(7, Q) == 1, "mod is the players' event and takes nothing over (%d / %d)", a.smp(7, Q), a.sfv(7, Q));
    SeqEvent stp; stp.type = SEV_STP; stp.track = 7; stp.t = t += 0.01; const bool stpOk = a.P->dispatch(stp, *a.seq, a.song);
    CHECK(stpOk && a.smp(7, Q) == 0 && a.sfv(7, Q) == 0, "stp ends the track's voices in both pools (%d / %d)", a.smp(7, Q), a.sfv(7, Q));
    CHECK(!a.P->dispatch(SeqEvent(), *a.seq, a.song), "an event of another type is not the players' (false, nothing done)"); }

  // ---- P6: setPads re-points the pads between calls ------------------------------------------------------------------------------------------------------------------------------------
  { Rig a; double t = 0.01; RenderPads other; other.buf[PAD_SMP_A] = a.pads.buf[PAD_SMP_A]; other.route[PAD_SMP_A] = PadRoute::Live;
    a.go(0, PAD_SMP_A, t += 0.01, true); CHECK(a.smp(0, Q) == 1, "set up");
    a.P->setPads(&other); a.go(0, PAD_SMP_A, t += 0.01, false);
    CHECK(a.smp(0, Q) == 0 && a.P->stats().notesLive == 1, "after setPads the pad is read from the new table (%d, notesLive %d)", a.smp(0, Q), a.P->stats().notesLive);
    a.P->setPads(&a.pads); a.go(0, PAD_SMP_A, t += 0.01, false);
    CHECK(a.smp(0, Q) == 1 && a.P->stats().notesPlayed == 2, "and back (%d, played %d)", a.smp(0, Q), a.P->stats().notesPlayed); }

  std::printf("test_players: %d checks, %d failures\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
