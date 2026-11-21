// WEB-FOLLOW item 3 (docs/port/WEB-FOLLOW-20NOV-NOTES.md): RESET TRACK's message {op:'stp', track, t} (web #251, core/seq-core.js:1229) = Players::stopTrack, for BOTH pools (the sampler's and the SoundFont's).
// The sampler pool's version is proved bit for bit against the web's own core in test_voice (case stp_msg, voice-dump.mjs). Here, through Players, the behaviour that tells it from the sequencer's STP command
// (SEV_STP: stop = false): a voice BOOKED to start just after the stop never sounds with stopTrack (stop = true), while after an STP it blips in. Also: both pools of the track are let go, another track is
// not touched, a track outside 0..NT-1 does nothing, a note after the reset plays, and the call allocates nothing (no lock, no exception).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "render/players.h"
#include "seq/seq.h"
#include "sf/sf.h"
#include "sf/sf_live.h"
#include "song/song.h"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif
using namespace thunder;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static std::vector<uint8_t> readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static const char* kSong = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[16],\"insts\":[{\"pad\":0}],\"phrases\":[[[48,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]]]}";

static SeqEvent note(double t, int track, int pad, int col) {   // poly: the booked note does not take the first one over (so only the call under test can silence it)
  SeqEvent e; e.type = SEV_NOTE; e.t = t; e.track = track; e.pad = pad; e.rate = 1; e.gain = 0.8; e.pan = 128; e.ii = 0; e.poly = true; e.col = col; return e;
}

struct Rig {
  Song song; SeqVars vars; SeqOpts so; std::unique_ptr<Seq> seq;
  std::vector<float> samp; RenderPads pads; sf::Font font; sf::PresetPcm pcm; const sf::Preset* pr = nullptr;
  std::vector<uint8_t> gm;
  bool init() {
    JsonError je; if (!song.load(kSong, std::strlen(kSong), &je)) return false;
    so.once = true; seq.reset(new Seq(song, vars, SeqMode::Song, so));
    samp.resize(48000); uint32_t s = 7; for (auto& v : samp) { s = s * 1664525u + 1013904223u; v = ((s >> 8) / 8388608.0f - 1.f) * 0.5f; }
    pads.buf[0].data = samp.data(); pads.buf[0].data2 = nullptr; pads.buf[0].len = (int)samp.size(); pads.buf[0].sr = 48000;
    gm = readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3");
    if (sf::parse(gm.data(), gm.size(), font) != sf::kParseOk) return false;
    pr = sf::sf_preset(font, 0, 19); uint8_t flags[128] = {}; flags[60] = 1; sf::DecodeOpts dop;
    if (sf::prepare_preset(font, pr, dop, pcm, flags) != sf::kPrepOk) return false;
    sf::PadParams P; P.bank = 0; P.prog = 19;
    pads.route[1] = PadRoute::SoundFont; pads.sf[1].font = &font; pads.sf[1].pr = pr; pads.sf[1].pcm = &pcm; pads.sf[1].P = P;
    return true;
  }
};

enum Mode { kControl, kStopTrack, kStp, kStopOther, kStopBad };
// two tracks: 0 plays the sampler pad (0), 1 the SoundFont pad (1); each has a note at t = 0 and one booked at `booked`. At `stopAt` the mode's call is made. Returns the energy of each track after `from`.
static void run(Rig& R, Mode m, double stopAt, double booked, double from, double outE[2], bool first = true) {
  Players pl(&R.pads, 48000, 64);
  const int N = 128, blocks = 330; std::vector<float> L((size_t)N * kVoiceTracks), Rr((size_t)N * kVoiceTracks);
  float* ls[kVoiceTracks]; float* rs[kVoiceTracks]; for (int k = 0; k < kVoiceTracks; k++) { ls[k] = &L[(size_t)k * N]; rs[k] = &Rr[(size_t)k * N]; }
  outE[0] = outE[1] = 0; bool done = false;
  const SeqEvent e0[2] = {note(0.0, 0, 0, 0), note(0.0, 1, 1, 0)}, e1[2] = {note(booked, 0, 0, 1), note(booked, 1, 1, 1)};
  for (int b = 0; b < blocks; b++) {
    const double t0 = (double)((int64_t)b * N) / 48000.0; pl.setTime(t0, (int64_t)b * N);
    if (b == 0) { for (int i = 0; i < 2; i++) { if (first) pl.dispatch(e0[i], *R.seq, R.song); pl.dispatch(e1[i], *R.seq, R.song); } }
    if (!done && t0 >= stopAt - 1e-9) { done = true;
      if (m == kStopTrack) { pl.stopTrack(0, stopAt); pl.stopTrack(1, stopAt); }
      else if (m == kStopOther) pl.stopTrack(5, stopAt);
      else if (m == kStopBad) { pl.stopTrack(-1, stopAt); pl.stopTrack(kSongNT, stopAt); pl.stopTrack(1000, stopAt); }
      else if (m == kStp) { SeqEvent s; s.type = SEV_STP; s.t = stopAt; s.track = 0; s.flag = false; pl.dispatch(s, *R.seq, R.song); s.track = 1; pl.dispatch(s, *R.seq, R.song); } }
    std::memset(L.data(), 0, L.size() * 4); std::memset(Rr.data(), 0, Rr.size() * 4);
    pl.render(ls, rs, N, t0, (int64_t)b * N);
    if (t0 >= from) for (int k = 0; k < 2; k++) for (int i = 0; i < N; i++) outE[k] += (double)ls[k][i] * ls[k][i] + (double)rs[k][i] * rs[k][i];
  }
}

int main() {
  Rig R; if (!R.init()) { std::printf("cannot set up the rig (the GM font?)\n"); return 1; }
  const double stopAt = 112 * 128 / 48000.0, booked = stopAt + 0.001, from = stopAt + 0.2;   // (stopAt = a block start: the call is made at the top of that block, before the booked note starts)   // the booked note is 1 ms after the stop: inside the 4 ms fade a plain kill gives it
  double c[2], st[2], sp[2], ot[2], bad[2];
  run(R, kControl, stopAt, booked, from, c); run(R, kStopTrack, stopAt, booked, from, st); run(R, kStp, stopAt, booked, from, sp); run(R, kStopOther, stopAt, booked, from, ot); run(R, kStopBad, stopAt, booked, from, bad);
  std::printf("energy after the stop, track 0 (sampler) / track 1 (SoundFont): control %.4g / %.4g, stopTrack %.4g / %.4g, STP %.4g / %.4g, stopTrack(5) %.4g / %.4g\n", c[0], c[1], st[0], st[1], sp[0], sp[1], ot[0], ot[1]);
  CHECK(c[0] > 1 && c[1] > 1e-3, "the control plays on (%g, %g): the case proves nothing", c[0], c[1]);
  CHECK(st[0] < c[0] * 1e-3 && st[1] < c[1] * 1e-3, "stopTrack leaves sound after the reset (%g, %g)", st[0], st[1]);   // everything after `from` is gone, the booked note included
  CHECK(ot[0] == c[0] && ot[1] == c[1], "stopTrack of another track changed these tracks");
  CHECK(bad[0] == c[0] && bad[1] == c[1], "stopTrack of a track outside 0..NT-1 changed something");
  // what tells the message from the sequencer's STP: the note booked 1 ms after the stop. STP lets it blip in (stop = false); stopTrack does not (stop = true). Measured from just after the stop.
  double st2[2], sp2[2]; run(R, kStopTrack, stopAt, booked, stopAt + 0.0005, st2, false); run(R, kStp, stopAt, booked, stopAt + 0.0005, sp2, false);   // only the booked note
  std::printf("energy of the booked note alone: stopTrack %.4g / %.4g, STP %.4g / %.4g\n", st2[0], st2[1], sp2[0], sp2[1]);
  CHECK(st2[0] < sp2[0] && st2[1] < sp2[1], "the booked note 1 ms after the stop does not blip in after stopTrack, and does after an STP: %g vs %g, %g vs %g", st2[0], sp2[0], st2[1], sp2[1]);
  std::printf("stptrack: %d failed\n", fails);
  return fails ? 1 : 0;
}
