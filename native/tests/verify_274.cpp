// Verifier's probes for PR #274 (docs/port/reviews/verify-274.md). NOT registered in ctest (it prints, it is evidence for F1, F3, F4, F6, F7 of the report).
// Build (from a configured Release build dir B of native/, repo root R): c++ -O2 -std=c++17 -DTHUNDER_ENGINE_PE=1 -DTHUNDER_HAVE_PUMP=1 -DTHUNDER_HAVE_TFX=1 -DTHUNDER_MODFX_MODS=1 -DTHUNDER_ROOT=\"R\"
//   -IR/native/core/src -IR/native/tests -IR/native/core/include verify_274.cpp -o verify_274 B/core/libthunder_core.a B/core/sf/libthunder_stb_vorbis.a
//   B/core/dsp/libthunder_dsp_tfx.a B/core/dsp/libthunder_dsp_pe.a B/core/dsp/libthunder_dsp_mods.a B/core/pump/libthunder_pump.a B/core/pump/libthunder_pump_synth.a B/core/pump/libthunder_pump_pffft.a
// Main part (F1): is "the scheduler driven directly at time 0" the same as runBlocks (the Seq starts 0.08 s after Play)?
// The record stream of a SongRenderer with setMidi (Seq at 0, no FX latency) is compared with runBlocks (block 128, C.fxLat = 0): every record should be the same with
// its frame moved by the runBlocks Seq start (Play block's now + 0.08 s). It is not: see the report.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "midi/midi_host.h"
#include "midi_drive.h"
#include "render/render.h"
#include "render/players.h"
#include "seq/seq.h"
#include "thunder/midi.h"
#include "thunder/midiport.h"
#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif
using namespace mdrive;
typedef thunder_midi_msg Rec;
static int fails = 0;


// A late MONO note must end the previous voices of its CHOKE GROUP (another track) as the on-time path does (Players::note: takeAll + live_.take); a late POLY one touches nothing. Passes at 4a9d9b94
// (the sampler half of the late branch is covered by test_midi_glue test 11 as well; the SoundFont / live halves are not covered by any test: mutants M3b, M3c of the report).
static void lateChoke() {
  static const char* kSong = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"insts\":[{\"pad\":0,\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}],\"phrases\":[[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]]}";
  thunder::Song song; thunder::JsonError je; std::string base = kSong; if (!song.load(base.data(), base.size(), &je)) { std::printf("song\n"); fails++; return; }
  thunder::SeqVars vars; thunder::SeqOpts so; so.row = song.renderFrom; so.once = true; thunder::Seq seq(song, vars, thunder::SeqMode::Song, so);
  std::vector<float> dc(48000 * 4, 0.1f); thunder::RenderPads pads;
  for (int p = 0; p < 2; p++) { pads.buf[p].data = dc.data(); pads.buf[p].len = (int)dc.size(); pads.buf[p].sr = 48000; pads.kit[p].choke = 1; }
  const auto note = [&](int track, int pad, double t, bool poly) { thunder::SeqEvent e; e.type = thunder::SEV_NOTE; e.track = track; e.pad = pad; e.ii = 0; e.t = t; e.poly = poly; e.rate = 1; e.gain = 1; e.pan = 128; e.vel = 1; return e; };
  const double T0 = 10.0;
  for (int mode = 0; mode < 3; mode++) {   // 0: dropLate off, late mono note (still plays: chokes at once); 1: on, late mono; 2: on, late poly
    thunder::Players P(&pads, 48000, 16); P.setTime(T0, (int64_t)(T0 * 48000)); P.setDropLate(mode != 0);
    P.dispatch(note(1, 0, T0 - 0.001, false), seq, song);               // track 1, pad 0, choke group 1: sounding
    const int before = P.pool().liveOnTrack(1, T0 + 1.0);
    P.dispatch(note(0, 1, T0 - 0.16, mode == 2), seq, song);             // track 0, pad 1, the same choke group, 0.16 s late
    const int after = P.pool().liveOnTrack(1, T0 + 1.0);
    const bool shouldChoke = mode != 2;
    std::printf("late-choke mode %d: track 1 voices before %d after %d (late %d) -> %s\n", mode, before, after, P.stats().notesLate, (after == (shouldChoke ? 0 : 1)) ? "ok" : "WRONG"); if (after != (shouldChoke ? 0 : 1)) fails++;
  }
}

// F-hold: after the 21 Nov ruling the pump HOLDS records until the first clock_map. What does the first map do to a backlog? Notes 0.0 .. 0.9 s apart are made (and drained into the queue), the
// first map arrives 1 s of audio later (a shell that calls clock_map late): every note whose time is past is sent AT ONCE (late, stamped in the past), none is dropped by the 0.15 s rule.
struct HSink { int64_t now = 0; std::vector<std::pair<int64_t, int>> sent; };
static int64_t hNow(void* u) { return ((HSink*)u)->now; }
static int hSend(void* u, uint64_t, const uint8_t* b, int, int64_t at) { HSink* s = (HSink*)u; s->sent.push_back({at, b[0]}); return 0; }
static int64_t hClear(void*, uint64_t) { return 0; }
static void holdBurst() {
  thunder_midi* h = thunder_midi_create(48000, nullptr, 0); HSink hs; hs.now = 1000000000;
  thunder_midiout_sink sk; std::memset(&sk, 0, sizeof sk); sk.user = &hs; sk.now_ns = hNow; sk.send = hSend; sk.clear = hClear; sk.caps = 0;
  thunder_midiout* o = thunder_midiout_create(h, 48000, &sk);
  thunder_midiout_dev d; std::memset(&d, 0, sizeof d); std::strcpy(d.id, "X"); std::strcpy(d.name, "X"); d.token = 1; thunder_midiout_set_devices(o, &d, 1); thunder_midiout_assign(o, 0, "X");
  double now = 0; const double blk = 128.0 / 48000;
  const auto proc = [&]() { thunder_midi_process(h, now, blk, now, 120, 0); now += blk; };
  proc();
  for (int i = 0; i < 6; i++) { const uint8_t on[3] = {0x90, (uint8_t)(60 + i), 100}; thunder_midi_raw(h, 0, on, 3, 0.2 * i); const uint8_t off[3] = {0x80, (uint8_t)(60 + i), 0}; thunder_midi_raw(h, 0, off, 3, 0.2 * i + 0.1); }
  for (int i = 0; i < 400; i++) { proc(); hs.now += 2666667; thunder_midiout_pump(o, nullptr); }   // ~1.07 s of audio, no map: nothing is sent
  const size_t before = hs.sent.size();
  const int64_t host0 = hs.now - (int64_t)(now * 1e9);                   // the map: the audio clock is the host's clock now
  thunder_midiout_clock_map(o, (int64_t)std::llround(now * 48000), hs.now); (void)host0;
  thunder_midiout_pump(o, nullptr);
  thunder_midiout_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; thunder_midiout_stats_get(o, &st);
  int past = 0; for (auto& r : hs.sent) if (r.first < hs.now - 150000000) past++;
  std::printf("hold-burst: before the map %zu sent; right after it %zu sent in one pump call, %d stamped more than 150 ms in the past, late %llu, dropped_late_ticks %llu\n", before, hs.sent.size(), past, (unsigned long long)st.late, (unsigned long long)st.dropped_late_ticks);
  thunder_midiout_free(o); thunder_midi_free(h);
}

// F-silent: test_midi_glue's "the hook never changes the audio" compares two renders of two-ports with an EMPTY RenderPads: is there any audio to compare? (CodeRabbit asked; the builder did not answer that comment)
static void audioSilent() {
  Case C; if (!loadCase(THUNDER_ROOT, "two-ports", C)) return;
  Song song; JsonError je; song.load(C.songText.data(), C.songText.size(), &je); SeqVars vars; RenderPads pads; SeqOpts so; so.live = true;
  thunder::SongRenderer R(song, vars, pads, 48000, 480, so, 16); std::vector<float> L(480), Rr(480); float mx = 0;
  for (int i = 0; i < 640; i++) { R.block(L.data(), Rr.data(), 480); for (float v : L) mx = std::max(mx, std::fabs(v)); for (float v : Rr) mx = std::max(mx, std::fabs(v)); }
  std::printf("audio-silent: two-ports through the renderer with the test's empty RenderPads: peak |sample| = %g over %d frames (notes seen %d, played %d)\n", mx, 640 * 480, R.stats().notes, R.stats().notesPlayed);
}

int main() {
  audioSilent();
  holdBurst();
  lateChoke();
  const char* names[] = {"two-ports", "mono-notes", "tempo-clock", "echo", "lfo-cc", "bend-fine", "cc-ins", "cc-program", "chan-share", "column-switch", "modes-mixed", "muted-solo", "note-len", "overlap-poly", "slide-arp", "song-end", "velocity", "fx-latency", "start-midway", "loop"};
  const double sr = 48000;
  for (const char* nm : names) {
    Case C; if (!loadCase(THUNDER_ROOT, nm, C)) { std::printf("no case %s\n", nm); fails++; continue; }
    C.actions.clear();
    // runBlocks at 128 with no FX latency (the renderer has none here)
    Case C0 = C; C0.fxLat = 0;
    Drive D; D.sr = sr; if (!runBlocks(D, C0, 128)) { fails++; continue; }
    // the block at which runBlocks pressed Play
    int64_t b = 1; while ((double)(b * 128) / sr * 1000 < C0.playAt) b++;
    const double seqStart = (double)(b * 128) / sr + 0.08; const int64_t shift = (int64_t)std::llround(seqStart * sr);
    // the renderer
    Song song; JsonError je; if (!song.load(C.songText.data(), C.songText.size(), &je)) { fails++; continue; }
    SeqVars vars; RenderPads pads; SeqOpts so; so.live = true; so.row = C.row; if (C.hasLoop) { so.hasLoop = true; so.loopY0 = C.y0; so.loopY1 = C.y1; }
    std::unique_ptr<thunder::SongRenderer> R(new thunder::SongRenderer(song, vars, pads, sr, 128, so, 16));
    thunder_midi* h = thunder_midi_create(sr, nullptr, 0);
    thunder_midi_set_song(h, (const uint8_t*)C.songText.data(), C.songText.size());
    for (int p = 0; p < 4; p++) { int cab = 100 + p; if (!C.ports[p].empty()) { cab = p; for (int q = 0; q < p; q++) if (C.ports[q] == C.ports[p]) { cab = q; break; } } thunder_midi_set_port(h, p, !C.ports[p].empty(), C.clk[p] != 0, C.gear[p], cab); }
    R->setMidi(h); thunder_midi_start(h, C.row > 0 ? sppFor(song, vars, C.row) : 0);
    std::vector<Rec> g; float L[128], Rr[128];
    const int64_t q = (int64_t)C.endMs * (int64_t)sr / 1000 / 128 + 400;
    for (int64_t i = 0; i < q; i++) { R->block(L, Rr, 128); Rec buf[512]; for (;;) { const int64_t n = thunder_midi_drain(h, buf, 512); if (n <= 0) break; for (int64_t k = 0; k < n; k++) if (!(buf[k].flags & THUNDER_MIDI_F_CLEAR)) g.push_back(buf[k]); } }
    size_t n = std::min(g.size(), D.kept.size()), diff = n; int64_t d0 = 0;
    for (size_t i = 0; i < n; i++) { const Out& o = D.kept[i]; const Rec& r = g[i]; if (o.frame != r.frame + shift || o.port != r.port || o.len != r.len || std::memcmp(o.b, r.b, 3) != 0) { diff = i; d0 = o.frame - (r.frame + shift); break; } }
    std::printf("%-14s playAt %4d  seq lead shift %lld frames  runBlocks %zu records, renderer %zu; first difference at %s%zu (frame delta %lld)\n", nm, C.playAt, (long long)shift, D.kept.size(), g.size(), diff == n ? "none in the first " : "record ", diff == n ? n : diff, (long long)d0);
    if (diff != n) { const Out& o = D.kept[diff]; const Rec& r = g[diff]; std::printf("     runBlocks %02x %02x %02x @%lld kind %d p%d | renderer %02x %02x %02x @%lld+shift kind %d p%d\n", o.b[0], o.b[1], o.b[2], (long long)o.frame, o.kind, o.port, r.b[0], r.b[1], r.b[2], (long long)r.frame, r.kind, r.port); }
    R.reset(); thunder_midi_free(h);
  }
  return fails ? 1 : 0;
}
