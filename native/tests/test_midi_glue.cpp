// U36 PR B: the MIDI glue inside the song renderer (SongRenderer::setMidi, render.cpp quantum(): midi_begin before the step loop, MidiSched::onStep per step before its events are dispatched,
// midi_end after the loop) and the lateness rule of Players (setDropLate). docs/port/U36-SPEC.md 2.2, 2.5, 3.6 cases 9, 10, 11.
//   9  glue_cadence    the record stream of a SongRenderer with setMidi, at host blocks 1, 96, 128, 480 and 4096, equals the stream of the scheduler driven directly on the same 128-frame cadence
//                      (the code of runBlocks: SeqRunner::pump -> onStep -> book), record for record; the hook never changes the audio; a null hook is the old renderer (the render goldens)
//   10 glue_audible_fx mute / solo / a CHAINED bus: the mixer's audible mask is the web's audible(t) (computed here from the web's formula over the edges, not from the plan's closures) and the records
//                      follow it; the FX latency (setPlan, the master chain) moves every record by exactly latencyFrames(); a stem render refuses a MIDI hook
//   11 players_late    setDropLate: a note 0.16 s late is not played (sampler, SoundFont, live engine), counted, a late MONO note still ends the previous voice, a late poly one touches nothing, an OUTPUT 2
//                      note is not sent to the gear and an OUTPUT 1 (MIDI only) note IS; off by default (bit-identical)
// The scheduler's own behaviour (ports, clock, LFO, echo...) is test_midi_ledger's and test_midi_sched's; this file is about WHERE the renderer calls it.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "midi/midi_host.h"
#include "midi/midisched.h"
#include "midi_drive.h"
#include "mix/mix.h"
#include "render/players.h"
#include "render/render.h"
#include "sf/sf.h"
#include "sf/sf_live.h"
#include "thunder/midi.h"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif
using namespace mdrive;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

typedef thunder_midi_msg Rec;
static bool sameRec(const Rec& a, const Rec& b) { return a.frame == b.frame && a.port == b.port && a.len == b.len && a.flags == b.flags && a.kind == b.kind && a.cable == b.cable && std::memcmp(a.b, b.b, 3) == 0; }
static int firstDiff(const std::vector<Rec>& a, const std::vector<Rec>& b) { const size_t n = std::min(a.size(), b.size()); for (size_t i = 0; i < n; i++) if (!sameRec(a[i], b[i])) return (int)i; return a.size() == b.size() ? -1 : (int)n; }

// ---- a renderer with the MIDI glue, and a scheduler driven directly on the same cadence ---------------------------------------------------------------
constexpr double kSr = 48000;
struct Host {
  Case C; Song song; SeqVars vars; RenderPads pads; SeqOpts so; std::unique_ptr<thunder::SongRenderer> R; thunder_midi* h = nullptr; std::string text;
  double seqStart = 0;          // SongRenderer::setSeqStart (the web's PLAY: 0.08 s)
  bool sound = false;           // every instrument OUTPUT 2 (pad + MIDI) and every pad a tone: the render is NOT silent
  std::vector<float> tone;
  Host() {}
  ~Host() { R.reset(); if (h) thunder_midi_free(h); }
  bool init(const std::string& songText, const Case& c, bool hook = true) {
    C = c; text = songText; JsonError je; if (!song.load(text.data(), text.size(), &je)) return false;
    if (sound) {
      tone.resize(48000); for (size_t i = 0; i < tone.size(); i++) tone[i] = 0.5f * (float)std::sin(0.05 * (double)i);
      for (int i = 0; i < thunder::kNI; i++) song.insts[i].mo = 2;
      for (int p = 0; p < thunder::kRenderPads; p++) { pads.buf[p].data = tone.data(); pads.buf[p].len = (int)tone.size(); pads.buf[p].sr = 48000; }
    }
    so.live = true; so.row = C.row; if (C.hasLoop) { so.hasLoop = true; so.loopY0 = C.y0; so.loopY1 = C.y1; }
    R.reset(new thunder::SongRenderer(song, vars, pads, kSr, 4096, so, 16));
    if (seqStart > 0 && !R->setSeqStart(seqStart)) return false;
    if (!hook) return true;
    h = thunder_midi_create(kSr, nullptr, 0); if (!h) return false;
    thunder_midi_set_song(h, (const uint8_t*)text.data(), text.size());
    for (int p = 0; p < 4; p++) { int cab = 100 + p; if (!C.ports[p].empty()) { cab = p; for (int q = 0; q < p; q++) if (C.ports[q] == C.ports[p]) { cab = q; break; } } thunder_midi_set_port(h, p, !C.ports[p].empty(), C.clk[p] != 0, C.gear[p], cab); }
    return R->setMidi(h);
  }
  void play() { thunder_midi_start(h, C.row > 0 ? sppFor(song, vars, C.row) : 0); }   // posted before the first block: midi_begin applies it before the step loop
  void drain(std::vector<Rec>& out) { Rec buf[512]; for (;;) { const int64_t n = thunder_midi_drain(h, buf, 512); if (n <= 0) break; out.insert(out.end(), buf, buf + n); } }
};

// The stream through the renderer: `frames` frames in host blocks of `block`, drained after every block.
// `stopRule`: the host's STOP of the web's frame(): 0.3 s after the sequencer has ended, thunder_midi_stop (what the renderer's header says the host owes)
static bool runGlue(Host& H, int block, int64_t frames, std::vector<Rec>& out, std::vector<float>* audio = nullptr, bool stopRule = false) {
  std::vector<float> L((size_t)block), Rr((size_t)block); H.play(); bool stopped = false;
  for (int64_t done = 0; done < frames;) {
    if (stopRule && !stopped && H.R->songOver() && (double)done / kSr > H.R->seqTime() + 0.3) { stopped = true; thunder_midi_stop(H.h); }
    const int n = (int)std::min<int64_t>(block, frames - done); H.R->block(L.data(), Rr.data(), n); done += n; H.drain(out);
    if (audio) { audio->insert(audio->end(), L.begin(), L.begin() + n); audio->insert(audio->end(), Rr.begin(), Rr.begin() + n); }
  }
  return true;
}

// The same song on the scheduler DIRECTLY (runBlocks' loop: the runner's pump, onStep, book; 128-frame blocks, `now` = the block's start, the sequencer at time 0 as the renderer's), with the
// figures the renderer gives it: `fxLat` seconds and the audible mask.
static void runDirect(const Case& C, const Song& song, const SeqVars& vars, double fxLat, uint64_t audible, int quanta, std::vector<Rec>& out) {
  thunder::MidiSched sched(kSr); configure(sched, C, song); sched.setFxLatency(fxLat); sched.setAudible(audible); sched.setBlock(128.0 / kSr);
  SeqOpts o; o.live = true; o.row = C.row; if (C.hasLoop) { o.hasLoop = true; o.loopY0 = C.y0; o.loopY1 = C.y1; }
  Seq seq(song, vars, SeqMode::Song, o); SeqRunner runner; runner.start(&seq);
  sched.start(C.row > 0 ? sppFor(song, vars, C.row) : 0, 0);
  std::vector<SeqEvent> evs(12288); SeqStep st;
  for (int q = 0; q < quanta; q++) {
    const double now = (double)q * 128 / kSr;
    runner.pump(now, 0.25, evs.data(), (int)evs.size(), &st, [&](const SeqEvent* e, int n, const SeqStep& step) { sched.onStep(e, n, step, seq, now); });
    sched.book(now, seq.time(), seq.bpm(), seq.alive());
    MidiMsg buf[512]; for (;;) { const int64_t n = sched.drain(buf, 512); if (n <= 0) break; for (int64_t i = 0; i < n; i++) { Rec r; std::memcpy(&r, &buf[i], sizeof r); out.push_back(r); } }
  }
}

// ---- case 9 -------------------------------------------------------------------------------------------------------------------------------------------
static void test9() {
  // every native ledger case that needs no control actions (the glue has no control side: PLAY is posted before the first block); the first five are also run at every host block size
  const char* names[] = {"two-ports", "mono-notes", "tempo-clock", "echo", "lfo-cc", "bend-fine", "cc-ins", "cc-program", "chan-share", "column-switch", "modes-mixed", "muted-solo", "note-len", "overlap-poly", "slide-arp", "song-end", "velocity", "fx-latency"};
  const int blocks[] = {1, 96, 128, 480, 4096};
  int cases = 0; long records = 0;
  for (size_t ci = 0; ci < sizeof names / sizeof *names; ci++) {
    Case C; if (!loadCase(THUNDER_ROOT, names[ci], C)) { CHECK(false, "9: no case %s", names[ci]); continue; }
    if (!C.actions.empty()) { CHECK(false, "9: case %s has actions", names[ci]); continue; }
    const int64_t quanta = (int64_t)C.endMs * (int64_t)kSr / 1000 / 128, frames = quanta * 128;
    std::vector<Rec> ref;
    { Host H; if (!H.init(C.songText, C)) { CHECK(false, "9: %s does not build", names[ci]); continue; }
      runDirect(C, H.song, H.vars, (double)H.R->latencyFrames() / kSr, H.R->mixer().audibleMask(), (int)quanta, ref); }
    CHECK(!ref.empty(), "9 %s: the reference made no records", names[ci]);
    for (int bi = 0; bi < 5; bi++) {
      if (ci >= 5 && blocks[bi] != 128) continue;
      Host H; if (!H.init(C.songText, C)) { CHECK(false, "9: %s does not build", names[ci]); break; }
      std::vector<Rec> got; runGlue(H, blocks[bi], frames, got);
      const int d = firstDiff(ref, got);
      CHECK(d < 0, "9 glue_cadence %s at host block %d: the stream differs at record %d (%zu vs %zu records)", names[ci], blocks[bi], d, got.size(), ref.size());
      thunder_midi_stats ts; std::memset(&ts, 0, sizeof ts); ts.size = sizeof ts; thunder_midi_stats_get(H.h, &ts);
      CHECK(ts.ring_full == 0, "9 %s: the ring did not overflow (%llu)", names[ci], (unsigned long long)ts.ring_full);
      records += (long)got.size();
    }
    cases++;
  }
  std::printf("9 glue_cadence: %d songs, %ld records compared with the scheduler driven directly\n", cases, records);
  // THE REAL REFERENCE (21 Nov verifier F1): the renderer WITH the web's 0.08 s Seq start (setSeqStart) against runBlocks itself (block 128; Play at the first block at or after playAt; the Seq starts
  // 0.08 s after it; no actions, no FX latency: the renderer has none here). Every record, every field, CLEAR applied by the same shell: the Play block's frame moves the renderer's. All 18 songs, start-midway and loop.
  { const char* all[] = {"two-ports", "mono-notes", "tempo-clock", "echo", "lfo-cc", "bend-fine", "cc-ins", "cc-program", "chan-share", "column-switch", "modes-mixed", "muted-solo", "note-len", "overlap-poly", "slide-arp", "song-end", "velocity", "fx-latency", "start-midway", "loop"};
    int n = 0; long recs = 0;
    for (const char* nm : all) {
      Case C; if (!loadCase(THUNDER_ROOT, nm, C)) { CHECK(false, "9: no case %s", nm); continue; }
      C.actions.clear(); C.fxLat = 0;
      Drive D; D.sr = kSr; if (!runBlocks(D, C, 128)) { CHECK(false, "9: runBlocks failed on %s", nm); continue; }
      const int64_t total = (int64_t)C.endMs * (int64_t)kSr / 1000 / 128 + 1; int64_t b = 1; while ((double)(b * 128) / kSr * 1000 < C.playAt) b++;
      Host H; H.seqStart = 0.08; if (!H.init(C.songText, C)) { CHECK(false, "9: %s does not build", nm); continue; }
      std::vector<Rec> got; runGlue(H, 128, (total - b + 1) * 128, got, nullptr, true);
      Drive G; G.C = C; std::vector<MidiMsg> raw(got.size()); for (size_t i = 0; i < got.size(); i++) std::memcpy(&raw[i], &got[i], sizeof(MidiMsg)); G.shell(raw.data(), (int64_t)raw.size());
      const int64_t shift = b * 128; size_t bad = G.kept.size() == D.kept.size() ? G.kept.size() : 0; bool same = G.kept.size() == D.kept.size();
      for (size_t i = 0; same && i < G.kept.size(); i++) { const Out& x = D.kept[i]; const Out& y = G.kept[i]; if (x.frame != y.frame + shift || x.port != y.port || x.len != y.len || x.kind != y.kind || x.cab != y.cab || std::memcmp(x.b, y.b, 3)) { same = false; bad = i; } }
      CHECK(same && !D.kept.empty() && G.cleared == D.cleared, "9 glue_vs_runBlocks %s: the renderer with the 0.08 s start equals runBlocks record for record (%zu vs %zu records, first difference at %zu; cleared %lld vs %lld)", nm, G.kept.size(), D.kept.size(), bad, (long long)G.cleared, (long long)D.cleared);
      if (!same && std::getenv("GLUE_DEBUG")) for (size_t i = 0; i < 6 && i < G.kept.size() && i < D.kept.size(); i++) std::printf("   rb %02x %02x %02x @%lld k%d p%d | gl %02x %02x %02x @%lld+%lld k%d p%d\n", D.kept[i].b[0], D.kept[i].b[1], D.kept[i].b[2], (long long)D.kept[i].frame, D.kept[i].kind, D.kept[i].port, G.kept[i].b[0], G.kept[i].b[1], G.kept[i].b[2], (long long)G.kept[i].frame, (long long)shift, G.kept[i].kind, G.kept[i].port);
      n++; recs += (long)D.kept.size();
    }
    std::printf("9 glue_vs_runBlocks: %d songs, %ld records equal to runBlocks (setSeqStart 0.08)\n", n, recs); }
  // the hook never changes the audio, and no hook is the renderer as it was (the existing render, stem and golden tests are the other half of that)
  { Case C; if (!loadCase(THUNDER_ROOT, "two-ports", C)) { CHECK(false, "9: no two-ports case"); return; }
    Host A, B; A.sound = B.sound = true; const bool okA = A.init(C.songText, C, false), okB = B.init(C.songText, C, true);
    if (!okA || !okB) { CHECK(false, "9: the audio comparison does not build"); return; }
    std::vector<float> a, b; std::vector<Rec> sink; const int64_t frames = 128 * 400;
    { std::vector<float> L(480), R(480); for (int64_t d = 0; d < frames;) { const int n = (int)std::min<int64_t>(480, frames - d); A.R->block(L.data(), R.data(), n); a.insert(a.end(), L.begin(), L.begin() + n); a.insert(a.end(), R.begin(), R.begin() + n); d += n; } }
    runGlue(B, 480, frames, sink, &b);
    float pk = 0; for (float v : a) pk = std::max(pk, std::fabs(v));
    CHECK(pk > 0.01f && A.R->stats().notesPlayed > 0, "9: the audio compared is NOT silent (peak %g, %d notes played)", (double)pk, A.R->stats().notesPlayed);
    CHECK(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0, "9: the audio with the MIDI hook is bit-identical to the audio without it");
    CHECK(A.R->stats().steps == B.R->stats().steps && A.R->stats().events == B.R->stats().events, "9: the same steps and events with and without the hook"); }
}

// ---- case 10 ------------------------------------------------------------------------------------------------------------------------------------------
// The web's audible(t) (index.html, beta build 660) over an explicit edge list: solo over mute, then "nobody soloed", then a soloed bus keeps everything upstream of it (srcAll) and a soloed source every bus
// downstream (busAll), through any depth. The closures are walked here from the edges, not read from BusPlan.
struct Edge { int from, to; };
static uint32_t webAudible(const std::vector<Edge>& E, uint32_t mute, uint32_t solo) {
  uint32_t a = 0;
  for (int t = 0; t < thunder::kSongNT; t++) {
    bool on;
    if (solo >> t & 1u) on = true; else if (mute >> t & 1u) on = false; else if (!solo) on = true;
    else {
      on = false;
      for (int dir = 0; dir < 2 && !on; dir++) {   // dir 0: the buses downstream of t, dir 1: the tracks upstream of t
        std::vector<int> w{t}; uint32_t seen = 1u << t;
        while (!w.empty()) { const int x = w.back(); w.pop_back(); for (const Edge& e : E) { const int a1 = dir ? e.to : e.from, b1 = dir ? e.from : e.to; if (a1 == x && !(seen >> b1 & 1u)) { seen |= 1u << b1; w.push_back(b1); } } }
        if ((seen & ~(1u << t)) & solo) on = true;
      }
    }
    if (on) a |= 1u << t;
  }
  return a;
}
// the two-ports song (tracks 0..3 each with a MIDI instrument on channels 1 6 10 2) with a chains patch: 1 -> 3 and 3 -> 4 (track numbers as the patch has them, 1-based): track 0 feeds bus 2, bus 2 feeds bus 3
static std::string chainedSong(const std::string& base, uint32_t mute, uint32_t solo) {
  std::string s = base;
  const size_t cl = s.rfind('}'); if (cl == std::string::npos) return s;
  const size_t cl2 = s.rfind('}', cl - 1); (void)cl2;
  std::string patch = "\"chainOn\":[true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true],"
    "\"chainPatch\":{\"format\":\"chains.patch\",\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":3}},"
    "{\"id\":\"d\",\"type\":\"trackIn\",\"params\":{\"track\":3}},{\"id\":\"e\",\"type\":\"chainOut\",\"params\":{\"track\":4}}],\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"d\",\"out\"],\"to\":[\"e\",\"in\"]}]},";
  // the song object is the value of "song": the new keys go in right after its opening brace
  const size_t o = s.find("\"song\":{"); if (o == std::string::npos) return s;
  size_t brace = s.find('{', o + 7);
  // drop the song's own chainOn (the later key wins in most parsers, but do not depend on it): replaced by ours
  s.insert(brace + 1, patch);
  const size_t co = s.find("\"chainOn\":[", brace + 1 + patch.size());
  if (co != std::string::npos) { const size_t ce = s.find(']', co); s.erase(co, ce - co + 2); }
  // mute / solo of tracks 0..4 in mix[]
  const size_t mx = s.find("\"mix\":[", brace); size_t p = mx;
  for (int t = 0; t < 5 && p != std::string::npos; t++) {
    p = s.find("\"mute\":", p); if (p == std::string::npos) break;
    const size_t mEnd = s.find(',', p); s.replace(p, mEnd - p, std::string("\"mute\":") + ((mute >> t & 1u) ? "true" : "false"));
    p = s.find("\"solo\":", p); const size_t sEnd = s.find_first_of(",}", p); s.replace(p, sEnd - p, std::string("\"solo\":") + ((solo >> t & 1u) ? "true" : "false"));
  }
  return s;
}
static uint32_t chanMask(const std::vector<Rec>& v) { uint32_t m = 0; for (const Rec& r : v) if ((r.b[0] & 0xF0) == 0x90 && r.b[2] > 0) m |= 1u << (r.b[0] & 15); return m; }

static void test10() {
  Case C; if (!loadCase(THUNDER_ROOT, "two-ports", C)) { CHECK(false, "10: no case"); return; }
  // two-ports: 4 MIDI instruments, tracks 0..3, MIDI channels (mch) 0, 5, 9, 1 -> channel bits
  if (std::getenv("GLUE_DEBUG")) { Host H; H.init(C.songText, C); std::vector<Rec> g; runGlue(H, 128, 128 * 1000, g); std::printf("plain:"); for (const Rec& r : g) if (r.b[0] >= 0x80 && r.b[0] < 0xF0) std::printf(" %02x%02x p%d@%lld", r.b[0], r.b[1], r.port, (long long)r.frame); std::printf("\n"); }
  const std::vector<Edge> E = {{0, 2}, {2, 3}};
  const uint32_t combos[][2] = {{0, 0}, {0, 1}, {0, 8}, {0, 4}, {4, 1}, {1, 1}, {8, 2}, {0, 2}, {4, 2}, {2, 4}, {8, 8}, {1, 8}};   // {mute, solo}
  for (const auto& mc : combos) {
    const uint32_t mute = mc[0], solo = mc[1]; const std::string txt = chainedSong(C.songText, mute, solo);
    Host H; if (!H.init(txt, C)) { CHECK(false, "10: chained song m%x s%x does not build", mute, solo); continue; }
    if (std::getenv("GLUE_DEBUG")) { std::printf("chain %d; mix:", (int)(txt.find("chainPatch") != std::string::npos)); for (int t = 0; t < 5; t++) std::printf(" %d%d", (int)H.song.mix[t].mute, (int)H.song.mix[t].solo); std::printf(" insts mo/mch:"); for (int i = 0; i < 4; i++) std::printf(" %d/%d/%d", H.song.insts[i].mo, H.song.insts[i].mch, H.song.insts[i].mpt); std::printf("\n"); }
    const uint32_t want = webAudible(E, mute, solo), have = (uint32_t)H.R->mixer().audibleMask();
    CHECK((have & 0xFu) == (want & 0xFu) && H.R->mixer().plan().feedMask[3] == 4u && H.R->mixer().plan().feedMask[2] == 1u, "10 glue_audible_fx: mute %x solo %x: the mixer's mask %x is the web's %x (feeds %x %x)", mute, solo, have, want, H.R->mixer().plan().feedMask[2], H.R->mixer().plan().feedMask[3]);
    const int64_t quanta = 400, frames = quanta * 128; std::vector<Rec> got, ref;
    runGlue(H, 480, frames, got);
    runDirect(C, H.song, H.vars, 0, (uint64_t)want, (int)quanta, ref);   // the scheduler fed the web's mask directly
    CHECK(firstDiff(ref, got) < 0, "10: mute %x solo %x: the records equal the scheduler's with the web's audible mask (%zu vs %zu)", mute, solo, got.size(), ref.size());
    // what the gear hears: the channels of the audible NOTE tracks only (track 0 is channel 0, track 1 channel 5); soloing the end bus keeps its sources through the middle one
    const int ch[2] = {0, 5}; uint32_t wantCh = 0; for (int t = 0; t < 2; t++) if (want >> t & 1u) wantCh |= 1u << ch[t];   // (tracks 2 and 3 are buses here: the sequencer plays no notes on a bus; track 3 of the song has no port)
    const uint32_t gotCh = chanMask(got); if (std::getenv("GLUE_DEBUG")) { std::printf("m%x s%x:", mute, solo); for (const Rec& r : got) if (r.b[0] >= 0x80 && r.b[0] < 0xF0) std::printf(" %02x%02x@%lld", r.b[0], r.b[1], (long long)r.frame); std::printf("\n"); }
    CHECK(gotCh == wantCh, "10: mute %x solo %x: note-on channels %x, the audible tracks' channels %x", mute, solo, gotCh, wantCh);
  }
  // a live solo: the mixer moves the mask, the next block's figures follow. Track 0's note at frame 23520 (ch 0) is stepped about block 90: heard without a solo, gone after a solo of track 1 at block 40
  for (int solo = 0; solo < 2; solo++) {
    Host H; H.init(chainedSong(C.songText, 0, 0), C); std::vector<Rec> a; std::vector<float> L(128), R(128); H.play();
    for (int q = 0; q < 40; q++) { H.R->block(L.data(), R.data(), 128); H.drain(a); }
    if (solo) { thunder::MixSet ms; ms.tr = 1; ms.k = thunder::MixKey::Solo; ms.v = 1; ms.ramp = 0; H.R->mixer().set(ms);
      CHECK(((uint32_t)H.R->mixer().audibleMask() & 0xFu) == 2u, "10: a live solo of track 1 leaves only track 1 audible (%llx)", (unsigned long long)H.R->mixer().audibleMask()); }
    std::vector<Rec> b; for (int q = 0; q < 300; q++) { H.R->block(L.data(), R.data(), 128); H.drain(b); }
    int late0 = 0; for (const Rec& r : b) if (r.b[0] == 0x90 && r.b[2] > 0 && r.frame > 20000) late0++;
    CHECK(late0 == (solo ? 0 : 1), "10: ch 0 note-ons after block 40 (solo %d): %d", solo, late0);
  }
  // the FX latency: every record moves by exactly latencyFrames() (the glue reads it every block): the PDC plan's T, what pass() adds, and the master chain's look-ahead
  // (a case without a Gear delay: a message made earlier than the block's start is clamped to it, which would hide the shift on the first records)
  Case CF; if (!loadCase(THUNDER_ROOT, "mono-notes", CF)) { CHECK(false, "10: no mono-notes case"); return; }
  struct Fx { const char* name; int kind; };
  const Fx fxs[] = {{"plan T 12.3 ms + extra 777", 0}, {"master chain", 1}, {"plan T + extra + master chain", 2}};
  for (const Fx& fx : fxs) {
    Host H0, H1; H0.init(CF.songText, CF); H1.init(CF.songText, CF);
    thunder::PdcPlan plan; plan.on = true; plan.T = 0.0123;
    if (fx.kind == 0 || fx.kind == 2) H1.R->setPlan(plan, thunder::PdcMode::On, 777);
    if (fx.kind == 1 || fx.kind == 2) { thunder::MasterChainConfig cfg = thunder::master_chain_from_song(H1.song.mst, H1.song.limiter); H1.R->setMasterChain(cfg); }
    const int64_t lat = H1.R->latencyFrames();
    const int64_t frames = 128 * 800; std::vector<Rec> r0, r1; runGlue(H0, 480, frames, r0); runGlue(H1, 480, frames, r1);
    CHECK(lat > 0 && H0.R->latencyFrames() == 0, "10 %s: the renderer reports a latency (%lld)", fx.name, (long long)lat);
    bool ok = r0.size() == r1.size() && !r0.empty(); int64_t worst = 0;
    for (size_t i = 0; ok && i < r0.size(); i++) { if (r1[i].frame - r0[i].frame != lat || std::memcmp(r0[i].b, r1[i].b, 3) != 0) { ok = false; worst = r1[i].frame - r0[i].frame; } }
    if (std::getenv("GLUE_DEBUG")) { std::printf("%s lat %lld\n", fx.name, (long long)lat); for (size_t i = 0; i < 6 && i < r0.size() && i < r1.size(); i++) std::printf("   r0 %02x@%lld  r1 %02x@%lld\n", r0[i].b[0], (long long)r0[i].frame, r1[i].b[0], (long long)r1[i].frame); }
    if (!ok && std::getenv("GLUE_DEBUG")) for (size_t i = 0; i < r0.size() && i < r1.size(); i++) if (r1[i].frame - r0[i].frame != lat || std::memcmp(r0[i].b, r1[i].b, 3)) std::printf("  %zu: %02x %02x %02x kind %d flags %d f0 %lld f1 %lld | %02x %02x %02x kind %d\n", i, r0[i].b[0], r0[i].b[1], r0[i].b[2], r0[i].kind, r0[i].flags, (long long)r0[i].frame, (long long)r1[i].frame, r1[i].b[0], r1[i].b[1], r1[i].b[2], r1[i].kind);
    CHECK(ok, "10 glue_audible_fx %s: every record moves by exactly latencyFrames() = %lld frames (%zu vs %zu records; off by %lld at the first miss)", fx.name, (long long)lat, r1.size(), r0.size(), (long long)(worst - lat));
  }
  // a stem has no MIDI: its forced mask is not the audible mask, so the two are refused together
  { Host H; H.init(C.songText, C, false); CHECK(H.R->setStem(0) && !H.R->setMidi((thunder_midi*)&H), "10: a stem render refuses a MIDI hook");
    Host G; G.init(C.songText, C, true); CHECK(!G.R->setStem(0), "10: a renderer with a MIDI hook refuses to become a stem"); }
}

// ---- case 11 ------------------------------------------------------------------------------------------------------------------------------------------
constexpr int kPadSr = 48000, kPadLen = 48000 * 4;
constexpr int PAD_SMP = 0, PAD_SF = 1, PAD_LIVE = 2, PAD_NONE = 3;
static const char* kSong = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"insts\":[{\"pad\":0,\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}],\"phrases\":[[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]]}";

static void test11() {
  Song song; JsonError je; std::string base = kSong;
  // an instrument table with an OUTPUT 2 (mo 2: MIDI + pad), an OUTPUT 1 (mo 1) and a pad-only one
  if (!song.load(base.data(), base.size(), &je)) { CHECK(false, "11: the song does not load"); return; }
  song.insts[1].mo = 2; song.insts[1].mch = 3; song.insts[2].mo = 1; song.insts[2].mch = 4;
  SeqVars vars; SeqOpts so; so.row = song.renderFrom; so.once = true; Seq seq(song, vars, SeqMode::Song, so);
  std::vector<float> dc((size_t)kPadLen, 0.1f); RenderPads pads;
  pads.buf[PAD_SMP].data = dc.data(); pads.buf[PAD_SMP].len = kPadLen; pads.buf[PAD_SMP].sr = kPadSr;
  pads.route[PAD_LIVE] = PadRoute::Live;
  // a SoundFont pad
  std::vector<uint8_t> gm; { FILE* f = std::fopen((std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3").c_str(), "rb"); if (f) { std::fseek(f, 0, SEEK_END); gm.resize((size_t)std::ftell(f)); std::fseek(f, 0, SEEK_SET); if (std::fread(gm.data(), 1, gm.size(), f) != gm.size()) gm.clear(); std::fclose(f); } }
  sf::Font font; sf::PresetPcm pcm; const sf::Preset* pr = nullptr; bool haveSf = false;
  if (!gm.empty() && sf::parse(gm.data(), gm.size(), font) == sf::kParseOk) { pr = sf::sf_preset(font, 0, 19); uint8_t flags[128] = {}; flags[60] = 1; sf::DecodeOpts dop; haveSf = pr && sf::prepare_preset(font, pr, dop, pcm, flags) == sf::kPrepOk; }
  if (haveSf) { pads.route[PAD_SF] = PadRoute::SoundFont; pads.sf[PAD_SF].font = &font; pads.sf[PAD_SF].pr = pr; pads.sf[PAD_SF].pcm = &pcm; pads.sf[PAD_SF].P.bank = 0; pads.sf[PAD_SF].P.prog = 19; }
  CHECK(haveSf, "11: the SoundFont pad is prepared");
  const auto note = [&](int track, int pad, double t, bool poly, int ii) { SeqEvent e; e.type = SEV_NOTE; e.track = track; e.pad = pad; e.ii = ii; e.t = t; e.poly = poly; e.rate = 1; e.gain = 1; e.pan = 128; e.vel = 1; return e; };
  const double T0 = 10.0;   // the quantum's start
  const double Q = T0 + 1.0;
  struct Route { const char* name; int pad; };
  const Route routes[] = {{"sampler", PAD_SMP}, {"SoundFont", PAD_SF}, {"live engine", PAD_LIVE}};
  for (const Route& rt : routes) {
    if (rt.pad == PAD_SF && !haveSf) continue;
    for (int mode = 0; mode < 3; mode++) {   // 0: dropLate off (default), 1: on, a note 0.16 s late; 2: on, a note 0.14 s late (still played)
      Players P(&pads, kPadSr, 16); P.setTime(T0, (int64_t)(T0 * kPadSr)); if (mode) P.setDropLate(true);
      const double late = mode == 2 ? 0.14 : 0.16;
      // a first (on time) note on track 0, then the late one: mono and poly cases
      for (int poly = 0; poly < 2; poly++) {
        Players Q2(&pads, kPadSr, 16); Q2.setTime(T0, (int64_t)(T0 * kPadSr)); if (mode) Q2.setDropLate(true);
        Q2.dispatch(note(0, PAD_SMP, T0 - 0.001, true, 0), seq, song);   // a sampler voice already sounding on the track (the "previous voice"), on time
        const int before = Q2.pool().liveOnTrack(0, Q);
        const PlayerStats s0 = Q2.stats();
        Q2.dispatch(note(0, rt.pad, T0 - late, poly != 0, 0), seq, song);
        const PlayerStats s1 = Q2.stats();
        const int after = Q2.pool().liveOnTrack(0, Q) + Q2.sfPool().liveOnTrack(0, Q) + Q2.live().liveOnTrack(0, Q);
        const bool dropped = mode == 1;
        CHECK(before == 1, "11 %s: the setup voice sounds (%d)", rt.name, before);
        CHECK((s1.notesLate - s0.notesLate) == (dropped ? 1 : 0), "11 players_late %s mode %d poly %d: notesLate counts the dropped note (%d)", rt.name, mode, poly, s1.notesLate - s0.notesLate);
        CHECK(s1.notes - s0.notes == 1, "11 %s: the note is counted in notes", rt.name);
        if (dropped) {
          CHECK(s1.notesPlayed == s0.notesPlayed && s1.notesSfPlayed == s0.notesSfPlayed && s1.notesLivePlayed == s0.notesLivePlayed && s1.notesLive == s0.notesLive && s1.notesSf == s0.notesSf, "11 %s: a dropped note counts in no route", rt.name);
          // a late MONO note still ends the previous voice (the sampler voice of the track is released at the note's time: not sounding at Q); a late POLY note touches nothing
          CHECK(after == (poly ? 1 : 0), "11 players_late %s poly %d: the track's voices after a late note: %d (mono ends the previous one, poly leaves it)", rt.name, poly, after);
        } else {
          CHECK(s1.notesLate == 0, "11 %s: nothing dropped (mode %d)", rt.name, mode);
          const int routed = (s1.notesPlayed - s0.notesPlayed) + (s1.notesSfPlayed - s0.notesSfPlayed) + (s1.notesLive - s0.notesLive);
          CHECK(routed >= 1 || rt.pad == PAD_LIVE, "11 %s mode %d: the note went down its route (%d)", rt.name, mode, routed);
        }
      }
      (void)P;
    }
  }
  // the previous voice on the SoundFont and the live route (mutants M3b / M3c of the verifier: a late MONO note must end THEIR voices too; a late poly one touches nothing)
  for (int poly = 0; poly < 2; poly++) {
    if (haveSf) {
      Players P(&pads, kPadSr, 16); P.setTime(T0, (int64_t)(T0 * kPadSr)); P.setDropLate(true);
      P.dispatch(note(0, PAD_SF, T0 - 0.001, false, 0), seq, song);
      const int before = P.sfPool().liveOnTrack(0, Q); P.dispatch(note(0, PAD_SMP, T0 - 0.16, poly != 0, 0), seq, song);
      CHECK(before == 1 && P.stats().notesLate == 1 && P.sfPool().liveOnTrack(0, Q) == (poly ? 1 : 0), "11 players_late SoundFont previous voice, late %s note: before %d, after %d", poly ? "poly" : "mono", before, P.sfPool().liveOnTrack(0, Q));
    }
    { Players P(&pads, kPadSr, 16); P.setTime(T0, (int64_t)(T0 * kPadSr)); P.setDropLate(true);
      LivePadConfig cfg; cfg.kind = LiveKind::Bass; cfg.trackMask = 1; const bool added = P.live().addPad(PAD_LIVE, cfg); CHECK(added, "11: a live bass pad");
      P.dispatch(note(0, PAD_LIVE, T0 - 0.001, false, 0), seq, song);
      const int before = P.live().liveOnTrack(0, Q); P.dispatch(note(0, PAD_SMP, T0 - 0.16, poly != 0, 0), seq, song);
      CHECK(before == 1 && P.stats().notesLate == 1 && P.live().liveOnTrack(0, Q) == (poly ? 1 : 0), "11 players_late live previous voice, late %s note: before %d, after %d", poly ? "poly" : "mono", before, P.live().liveOnTrack(0, Q)); }
  }
  // off / mod / stp are never dropped for lateness
  { Players P(&pads, kPadSr, 16); P.setTime(T0, (int64_t)(T0 * kPadSr)); P.setDropLate(true);
    P.dispatch(note(0, PAD_SMP, T0 - 0.001, true, 0), seq, song);
    SeqEvent off = note(0, PAD_SMP, T0 - 0.5, true, 0); off.type = SEV_OFF; P.dispatch(off, seq, song);
    CHECK(P.stats().notesLate == 0 && P.pool().liveOnTrack(0, Q) == 0, "11: a late OFF is applied, not dropped (%d)", P.pool().liveOnTrack(0, Q)); }
  // the gear: the same note to MidiSched with the same verdict. OUTPUT 2 (pad + MIDI): not sent late; OUTPUT 1 (MIDI only): sent, late; the pad side: only OUTPUT 2 reaches the pad route at all
  for (int mo = 1; mo <= 2; mo++) {
    thunder::MidiSched sched(kSr); sched.setSong(&song); sched.setPort(0, true, false, 0, 0); sched.setAudible(~0ull); sched.start(0, T0);
    SeqEvent e = note(0, PAD_SMP, T0 - 0.16, false, mo == 2 ? 1 : 2); SeqStep st; std::vector<SeqEvent> one(1, e);
    Players P(&pads, kPadSr, 16); P.setTime(T0, (int64_t)(T0 * kPadSr)); P.setDropLate(true); P.dispatch(e, seq, song);
    sched.onStep(one.data(), 1, st, seq, T0); sched.book(T0, T0 + 0.1, 120, true);
    MidiMsg buf[64]; const int64_t n = sched.drain(buf, 64); int ons = 0; for (int64_t i = 0; i < n; i++) if ((buf[i].b[0] & 0xF0) == 0x90) ons++;
    if (mo == 2) CHECK(ons == 0 && P.stats().notesLate == 1, "11: OUTPUT 2, 0.16 s late: not sent to the gear (%d note-ons) and not played by the pad (late %d)", ons, P.stats().notesLate);
    else CHECK(ons == 1 && P.stats().notesLate == 0 && P.stats().notesMidiOnly == 1, "11: OUTPUT 1 (MIDI only), 0.16 s late: IS sent (%d note-ons); the pad side never sees it (late %d, midi-only %d)", ons, P.stats().notesLate, P.stats().notesMidiOnly);
  }
}

int main() {
  test9(); test10(); test11();
  std::printf(fails ? "midi_glue: %d of %d checks FAILED\n" : "midi_glue: all %d checks passed\n", fails ? fails : checks, checks);
  return fails ? 1 : 0;
}
