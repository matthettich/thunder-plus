// U35: drives Seq + MidiSched the way the web app's pump does (tools/midi-ledger.mjs's HARNESS: 1 ms steps of the audio clock, the pump
// every 25 ms with a 0.25 s look-ahead, the 17 ms song-end check) and plays the output through a fake SHELL that applies the CLEAR markers
// the way the helper's clear() does. Used by test_midi_ledger (the goldens) and the block-cadence tests.
#ifndef THUNDER_TESTS_MIDI_DRIVE_H
#define THUNDER_TESTS_MIDI_DRIVE_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "midi/midisched.h"
#include "rt/json.h"
#include "seq/seq.h"
#include "song/song.h"

namespace mdrive {
using namespace thunder;

struct Out { int64_t frame; int port; int len; uint8_t b[3]; uint8_t kind; int cab; };

struct Action { int at; std::string op; int p = 0, ii = 0, n = 0, v = 0; double dv = 0; std::string id; bool on = false; };

struct Case {
  std::string name, songText;
  std::vector<std::string> ports;       // "" = no cable
  std::vector<int> clk; std::vector<double> gear;
  double outLat = 0, fxLat = 0;
  int playAt = 500, endMs = 4000, row = 0; bool hasLoop = false; int y0 = 0, y1 = 0;
  std::vector<Action> actions;
};

inline bool readFile(const std::string& path, std::string& out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::ostringstream s; s << f.rdbuf(); out = s.str(); return true; }

// the web's moFxLat for the one case that has it (tools/midi-ledger.mjs 'fx-latency': mastering on, PDC T 12.3 ms): read off the ledger's first Start
constexpr double kFxLatCase = 0.071633333333;

inline bool loadCase(const std::string& root, const std::string& name, Case& C, std::vector<std::string>* expectNotes = nullptr) {
  std::string g, s;
  if (!readFile(root + "/native/tests/golden/midi-ledger-" + name + ".json", g) || !readFile(root + "/native/tests/golden/midi-ledger-" + name + ".song.json", s)) return false;
  JsonDoc d; if (!d.parse(g)) return false;
  const Json su = d.root().obj("setup"); C.name = name; C.songText = s;
  C.ports.clear(); C.clk.clear(); C.gear.clear(); C.actions.clear();
  const Json P = su.arr("ports"), K = su.arr("clock"), L = su.arr("gearDelayMs");
  for (size_t i = 0; i < 4; i++) { C.ports.push_back(i < P.size() && P.at(i).isStr() ? std::string(P.at(i).asStr()) : ""); C.clk.push_back(i < K.size() && K.at(i).asBool() ? 1 : 0); C.gear.push_back(i < L.size() ? L.at(i).asNum() : 0); }
  C.outLat = su.num("outputLatencyS"); C.playAt = su.get("playAtMs").isNum() ? (int)su.num("playAtMs") : (1 << 30); C.endMs = (int)su.num("endMs");
  C.fxLat = su.boolean("masteringOn") ? kFxLatCase : 0.003;   // moFxLat: the safety limiter's 3 ms is always in
  const Json o = su.obj("playOpts"); C.row = (int)o.num("row"); const Json lp = o.obj("loop"); C.hasLoop = lp.isObj(); if (C.hasLoop) { C.y0 = (int)lp.num("y0"); C.y1 = (int)lp.num("y1"); }
  const Json A = su.arr("actions");
  for (size_t i = 0; i < A.size(); i++) { const Json a = A.at(i); Action x; x.at = (int)a.num("at"); x.op = std::string(a.str("op")); x.p = (int)a.num("p"); x.ii = (int)a.num("ii"); x.n = (int)a.num("n"); x.v = (int)a.num("v"); x.dv = a.num("v"); x.id = std::string(a.str("id")); x.on = a.boolean("on"); C.actions.push_back(x); }
  std::stable_sort(C.actions.begin(), C.actions.end(), [](const Action& a, const Action& b) { return a.at < b.at; });
  (void)expectNotes; return true;
}

struct Drive {
  Case C; Song song; SeqVars vars; MidiSched* sched = nullptr;
  std::vector<Out> kept;                 // what the shell delivered (CLEAR applied), in stream order
  int64_t cleared = 0;
  double sr = 48000;
  int cableOf(int p) const { if (C.ports[p].empty()) return -1 - p; for (int q = 0; q <= p; q++) if (C.ports[q] == C.ports[p]) return q; return p; }
  void shell(const MidiMsg* m, int64_t n) {
    for (int64_t i = 0; i < n; i++) {
      if (m[i].flags & MIDI_F_CLEAR) {
        const int cab = m[i].cable;
        for (size_t k = 0; k < kept.size();) if (kept[k].cab == cab && kept[k].frame > m[i].frame) { kept.erase(kept.begin() + (long)k); cleared++; } else k++;
        continue;
      }
      Out o; o.frame = m[i].frame; o.port = m[i].port; o.len = m[i].len; std::memcpy(o.b, m[i].b, 3); o.kind = m[i].kind; o.cab = m[i].cable; kept.push_back(o);
    }
  }
  void drainAll() { MidiMsg buf[512]; for (;;) { const int64_t n = sched->drain(buf, 512); if (n <= 0) break; shell(buf, n); } }
};

inline void configure(MidiSched& s, const Case& C, const Song& song) {
  s.setSong(&song);
  for (int p = 0; p < 4; p++) {
    int cab = 100 + p; if (!C.ports[p].empty()) { cab = p; for (int q = 0; q < p; q++) if (C.ports[q] == C.ports[p]) { cab = q; break; } }
    s.setPort(p, !C.ports[p].empty(), C.clk[p] != 0, C.gear[p], cab);
  }
  s.setFxLatency(C.fxLat);
  // the mixer's audible(t) without buses (the ledger cases have none): a solo wins, then mute
  uint64_t aud = 0; bool anySolo = false; for (int t = 0; t < kSongNT; t++) anySolo = anySolo || song.mix[t].solo;
  for (int t = 0; t < kSongNT; t++) if (anySolo ? song.mix[t].solo : !song.mix[t].mute) aud |= 1ull << t;
  s.setAudible(aud);
}

// The Song Position Pointer for a PLAY from `row` (index.html sppFor: the beat of the row inside its block, in 16ths). The control thread / shell computes it
// (A9); here from a Seq run from the top: the first step whose playhead is on `row` (or beyond) gives the beat.
inline int sppFor(const Song& song, const SeqVars& vars, int row) {
  SeqOpts o; o.once = true; o.sim = true; Seq sq(song, vars, SeqMode::Song, o);
  std::vector<SeqEvent> evs(4096); SeqStep st;
  for (int n = 0; n < 100000 && sq.alive(); n++) {
    const int k = sq.step(evs.data(), (int)evs.size(), &st);
    int r = -1; for (int t = 0; t < kSeqTracks; t++) if (st.pos[t].valid && st.pos[t].row > r) r = st.pos[t].row;
    if (r >= row) { for (int i = 0; i < k; i++) if (evs[i].type == SEV_BEAT) return std::max(0, std::min(16383, (int)std::llround(evs[i].v * 4))); return 0; }
  }
  return 0;
}

// Runs the case to its end (the web harness's stepMs loop). `pre` is called once with the scheduler before the run (hostile tests).
template <class F>
inline bool run(Drive& D, const Case& C, F&& pre) {
  D.C = C; D.kept.clear(); D.cleared = 0;
  JsonError je; if (!D.song.load(C.songText.data(), C.songText.size(), &je)) return false;
  MidiSched sched(D.sr); D.sched = &sched; configure(sched, C, D.song); pre(sched);
  SeqOpts o; o.live = true; o.row = C.row; if (C.hasLoop) { o.hasLoop = true; o.loopY0 = C.y0; o.loopY1 = C.y1; }
  Seq* seq = nullptr; bool on = false, started = false; size_t ai = 0; double ahead = 0;
  std::vector<SeqEvent> evs(4096); SeqStep st;
  int keyH[128]; for (int& h : keyH) h = -1;
  int pumpDue = 0, frameDue = 17;      // ms (the page's timers: the frame timer was made first)
  const auto pump = [&](double now) {
    if (!on) return;
    const double outLat = std::min(0.6, C.outLat), want = std::max(0.25, outLat + 0.1);
    ahead = want <= (ahead ? ahead : 0.25) ? want : std::min(want, (ahead ? ahead : 0.25) + 0.75 * 0.025);
    int n = 0;
    while (seq->alive() && seq->time() < now + ahead && n++ < 64) {
      const int k = seq->step(evs.data(), (int)evs.size(), &st);
      sched.onStep(evs.data(), k, st, *seq, now);
    }
    sched.book(now, seq->time(), seq->bpm(), seq->alive());
  };
  for (int ms = 1; ms <= C.endMs; ms++) {
    const double now = ms / 1000.0;
    if (!started && ms >= C.playAt) {
      started = true;
      seq = new Seq(D.song, D.vars, SeqMode::Song, o);
      sched.start(C.row > 0 ? sppFor(D.song, D.vars, C.row) : 0, now);
      seq->setTime(now + 0.08); on = true; ahead = 0; pumpDue = ms + 25;
      pump(now);
    }
    while (ai < C.actions.size() && C.actions[ai].at <= ms) {
      const Action& a = C.actions[ai++];
      if (a.op == "stop") { if (on) { on = false; sched.stop(now); } }
      else if (a.op == "bpm") { if (on && seq) seq->setBpm(a.dv); }
      else if (a.op == "clk") sched.portToggled(a.p, a.on, now);
      else if (a.op == "cable") { int cab = a.p; for (int q = 0; q < 4; q++) if (D.C.ports[q] == a.id) { cab = q; break; } D.C.ports[a.p] = a.id; sched.cableChanged(a.p, true, cab, now); }
      else if (a.op == "keyOn") { if (a.n >= 0 && a.n < 128) keyH[a.n] = sched.liveNoteOn(a.ii, a.n, a.v, 16, now); }
      else if (a.op == "keyOff") { if (a.n >= 0 && a.n < 128 && keyH[a.n] >= 0) { sched.liveNoteOff(keyH[a.n], now); keyH[a.n] = -1; } }
      else if (a.op == "panic") sched.panic(now);
    }
    // the timers due this ms, by due time then creation order: the frame timer (17 ms) first when both are due
    for (;;) {
      const bool f = ms >= frameDue, p = on && ms >= pumpDue;
      if (!f && !p) break;
      if (f && (!p || frameDue <= pumpDue)) { frameDue += 17; if (on && !seq->alive() && now > seq->time() + 0.3) { on = false; sched.stop(now); } }
      else { pumpDue += 25; pump(now); }
    }
    D.drainAll();
  }
  D.sched = nullptr; delete seq; return true;
}
inline bool run(Drive& D, const Case& C) { return run(D, C, [](MidiSched&) {}); }
}  // namespace mdrive
#endif
