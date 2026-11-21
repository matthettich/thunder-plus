// VERIFY-271 F1 (docs/port/reviews/verify-271.md): RESET TRACK must let a LIVE engine's voice go too. The web's core message {op:'stp', track} (core/seq-core.js:1229, core/core-processor.js:354) kills
// every voice in T(track) with kill(v, at, 0.004, true); a live engine's voice is in that list and kill() has a live branch (core-processor.js:534-538: gate low at `at`, a later booked note taken back).
// Players::stopTrack (render/players.h) let go the sampler's and the SoundFont's voices only; LivePool (added by PR #265) has no stopTrack. A held live bass note on the track sounds on after the reset.
// Control: no reset, the note holds (so the case proves something). Reference: the sequencer's STP command (Players::dispatch SEV_STP) does silence the live note, so the rig can hear a stop.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/live_pool.h"
#include "render/players.h"
#include "seq/seq.h"
#include "song/song.h"

using namespace thunder;
static int g_fail = 0;
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)
static const double SR = 48000;
enum Mode { kControl, kStp, kStopTrack, kStopOtherTrack };

static double run(Mode m) {
  Song song; JsonError je;
  const char* sj = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"insts\":[{\"pad\":0}],\"phrases\":[[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]]}";
  if (!song.load(sj, std::strlen(sj), &je)) { std::printf("song did not load\n"); return -1; }
  SeqVars vars; SeqOpts so; so.once = true; Seq seq(song, vars, SeqMode::Song, so);
  std::unique_ptr<RenderPads> pads(new RenderPads()); pads->route[0] = PadRoute::Live;
  Players pl(pads.get(), SR, 8);
  LivePadConfig c; c.kind = LiveKind::Bass; c.trackMask = 0b1; if (!pl.live().addPad(0, c)) { std::printf("addPad failed\n"); return -1; }
  const int N = 128, total = (int)(1.4 * SR) / N * N; const double stopAt = 0.6;   // a held note from 0.02 s (no release): it sounds until something lets it go
  std::vector<float> bl(N), br(N); float* inL[kVoiceTracks] = {}; float* inR[kVoiceTracks] = {}; inL[0] = bl.data(); inR[0] = br.data();
  SeqEvent n; n.type = SEV_NOTE; n.t = 0.02; n.track = 0; n.pad = 0; n.rate = 1; n.gain = 0.8; n.pan = 128;
  bool done = false, noted = false; double e = 0;
  for (int q0 = 0; q0 < total; q0 += N) {
    const double t0 = (double)q0 / SR; pl.setTime(t0, q0);
    if (!noted) { noted = true; pl.dispatch(n, seq, song); }
    if (!done && t0 >= stopAt - 0.1) { done = true;   // issued 0.1 s ahead of the stop time, inside the live pool's look-ahead rule
      if (m == kStp) { SeqEvent s; s.type = SEV_STP; s.t = stopAt; s.track = 0; s.flag = false; pl.dispatch(s, seq, song); }
      else if (m == kStopTrack) pl.stopTrack(0, stopAt);
      else if (m == kStopOtherTrack) pl.stopTrack(3, stopAt); }
    std::fill(bl.begin(), bl.end(), 0.f); std::fill(br.begin(), br.end(), 0.f);
    pl.render(inL, inR, N, t0, q0);
    if (t0 >= stopAt + 0.3) for (int i = 0; i < N; i++) e += (double)bl[i] * bl[i] + (double)br[i] * br[i];
  }
  return e;
}

int main() {
  const double ctl = run(kControl), stp = run(kStp), rst = run(kStopTrack), oth = run(kStopOtherTrack);
  std::printf("energy 0.3 s after the stop: control %.4g, STP %.4g, stopTrack(0) %.4g, stopTrack(3) %.4g\n", ctl, stp, rst, oth);
  CHECKF(ctl > 1e-3, "the control (a held live bass note, no reset) is silent: the case proves nothing (%g)", ctl);
  CHECKF(stp < ctl * 1e-3, "the sequencer's STP does not silence the held live note: the rig cannot hear a stop (%g vs %g)", stp, ctl);
  CHECKF(rst < ctl * 1e-3, "F1: Players::stopTrack(0) leaves the live engine's voice sounding (%g vs control %g)", rst, ctl);
  CHECKF(oth == ctl, "stopTrack of another track changed this track (%g vs %g)", oth, ctl);
  std::printf("stptrack_live: %d failed\n", g_fail);
  return g_fail ? 1 : 0;
}
