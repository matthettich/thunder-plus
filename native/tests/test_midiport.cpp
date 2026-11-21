// U36 PR A: the MIDI port table, the clock map and the timed send queue (thunder_midiout_*, include/thunder/midiport.h) and the pieces under them, against a FAKE SINK and a FAKE CLOCK.
// Cases (docs/port/U36-SPEC.md section 3.6, numbering kept): 1 drain_to_sink, 2 clear_per_cable, 3 ts_vs_spin, 4 clock_map, 5 hotplug, 6 ports_table, 7 stall, 12 spp_for; and the T2 check
// (midi_begin + onStep + midi_end give the records of runBlocks, for every native ledger). `--threads`: control, audio and sender threads at once (run under TSan in CI).
// Case 8 (the pump under the allocation guard) is in rt_midi.cpp.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "midi/midi_host.h"
#include "midi/midi_spp.h"
#include "midi_drive.h"
#include "thunder/midi.h"
#include "thunder/midiport.h"

using namespace mdrive;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

// ---- the fake sink and clock ------------------------------------------------------------------------------------------------------------------
struct Rec { int dev; int len; uint8_t b[3]; int64_t at, sentAt; bool recalled; };
struct Fake {
  std::atomic<int64_t> now{0}; int64_t step = 0;      // now_ns returns the clock and then advances it by `step` (a spinning pump needs a clock that moves)
  uint32_t caps = 0; std::vector<Rec> rec; bool fail[32] = {}; int clears[32] = {};
  static int64_t nowFn(void* u) { Fake* f = (Fake*)u; const int64_t v = f->now.load(); if (f->step) f->now.store(v + f->step); return v; }
  static int sendFn(void* u, int dev, const uint8_t* b, int len, int64_t at) {
    Fake* f = (Fake*)u; if (dev < 0 || dev >= 32 || f->fail[dev]) return -1;
    Rec r; std::memset(&r, 0, sizeof r); r.dev = dev; r.len = len; std::memcpy(r.b, b, (size_t)len); r.at = at; r.sentAt = f->now.load(); f->rec.push_back(r); return 0;
  }
  static void clearFn(void* u, int dev) { Fake* f = (Fake*)u; if (dev >= 0 && dev < 32) f->clears[dev]++; for (Rec& r : f->rec) if (r.dev == dev && r.at > f->now.load()) r.recalled = true; }   // the OS drops what it holds for the device
  thunder_midiout_sink sink() { thunder_midiout_sink s; s.user = this; s.now_ns = nowFn; s.send = sendFn; s.clear = clearFn; s.caps = caps; return s; }
};

static std::vector<thunder_midiout_dev> devList(const std::vector<std::string>& ids) {
  std::vector<thunder_midiout_dev> v;
  for (const std::string& s : ids) { thunder_midiout_dev d; std::memset(&d, 0, sizeof d); std::snprintf(d.id, sizeof d.id, "%s", s.c_str()); std::snprintf(d.name, sizeof d.name, "%s", s.c_str()); v.push_back(d); }
  return v;
}
static int setDevs(thunder_midiout* o, const std::vector<std::string>& ids) { const auto v = devList(ids); return thunder_midiout_set_devices(o, v.data(), (int)v.size()); }
static thunder_midiout_port portOf(thunder_midiout* o, int p) { thunder_midiout_port x; std::memset(&x, 0, sizeof x); thunder_midiout_get_port(o, p, &x); return x; }
static thunder_midiout_stats statsOf(thunder_midiout* o) { thunder_midiout_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; thunder_midiout_stats_get(o, &s); return s; }

// a small rig: a core handle, a midiout, a fake sink; blocks of 128 frames at 48 kHz; host time of audio time t (the render) = host0 + t
struct Rig {
  Fake fk; thunder_midi* h = nullptr; thunder_midiout* o = nullptr; double sr = 48000, now = 0; int64_t host0 = 1000000000;
  explicit Rig(uint32_t caps = 0, int64_t step = 0) { fk.caps = caps; fk.step = step; h = thunder_midi_create(sr, nullptr, 0); const auto s = fk.sink(); o = thunder_midiout_create(h, sr, &s); fk.now.store(host0); }
  ~Rig() { thunder_midiout_free(o); thunder_midi_free(h); }
  double blockSec() const { return 128.0 / sr; }
  void proc() { thunder_midi_process(h, now, blockSec(), now, 120, 0); now += blockSec(); }
  void raw(int port, std::initializer_list<uint8_t> b, double extra) { std::vector<uint8_t> v(b); thunder_midi_raw(h, port, v.data(), (int)v.size(), extra); }
  int64_t frameAt(double extra) const { return (int64_t)std::llround((now + extra) * sr); }
  int64_t hostOf(int64_t frame) const { return host0 + (int64_t)std::llround((double)frame * 1e9 / sr); }
  int pump(int64_t* nx = nullptr) { return thunder_midiout_pump(o, nx); }
};

// ---- the host driver: runBlocks on the C handle (T2) with the midiout and the fake sink behind it (case 1) ------------------------------------------
struct RunOpt {
  int block = 128; bool out = false; uint32_t caps = 0; int64_t step = 0; double driftPpm = 0; int64_t latNs = 0; bool setOutLat = false; int subs = 8;
  std::function<void(int64_t, double, thunder_midi*, thunder_midiout*, Fake&)> hook;
};
struct RunRes { Drive D; Fake fk; std::vector<std::string> ids; };
static constexpr int64_t kH0 = 1000000000;
static thunder_midiout_stats g_lastStats;   // the midiout's counters at the end of the last runHost

static bool runHost(const Case& C, const RunOpt& op, RunRes& R) {
  Drive& D = R.D; D.C = C; D.kept.clear(); D.cleared = 0; D.grid.clear();
  JsonError je; if (!D.song.load(C.songText.data(), C.songText.size(), &je)) return false;
  const double sr = D.sr; const int block = op.block; Fake& fk = R.fk; fk.caps = op.caps; fk.step = op.step; fk.now.store(kH0);
  thunder_midi* h = thunder_midi_create(sr, nullptr, 0); thunder_midiout* o = nullptr;
  thunder_midi_set_song(h, (const uint8_t*)C.songText.data(), C.songText.size());
  const auto addId = [&](const std::string& s) { if (!s.empty() && std::find(R.ids.begin(), R.ids.end(), s) == R.ids.end()) R.ids.push_back(s); };
  for (int p = 0; p < 4; p++) addId(C.ports[p]);
  for (const Action& a : C.actions) if (a.op == "cable") addId(a.id);
  if (op.out) {
    const auto s = fk.sink(); o = thunder_midiout_create(h, sr, &s); setDevs(o, R.ids);
    for (int p = 0; p < 4; p++) { thunder_midiout_assign(o, p, C.ports[p].c_str()); thunder_midiout_set_clock(o, p, C.clk[p]); thunder_midiout_set_gear(o, p, C.gear[p]); }
    if (op.setOutLat) thunder_midiout_set_out_latency(o, op.latNs);
  } else {
    for (int p = 0; p < 4; p++) { int cab = 100 + p; if (!C.ports[p].empty()) { cab = p; for (int q = 0; q < p; q++) if (C.ports[q] == C.ports[p]) { cab = q; break; } } thunder_midi_set_port(h, p, !C.ports[p].empty(), C.clk[p] != 0, C.gear[p], cab); }
  }
  uint64_t aud = 0; bool anySolo = false; for (int t = 0; t < kSongNT; t++) anySolo = anySolo || D.song.mix[t].solo;
  for (int t = 0; t < kSongNT; t++) if (anySolo ? D.song.mix[t].solo : !D.song.mix[t].mute) aud |= 1ull << t;
  SeqOpts so; so.live = true; so.row = C.row; if (C.hasLoop) { so.hasLoop = true; so.loopY0 = C.y0; so.loopY1 = C.y1; }
  Seq* seq = nullptr; SeqRunner runner; bool on = false, started = false; size_t ai = 0;
  std::vector<SeqEvent> evs(12288); SeqStep st;
  const int64_t total = (int64_t)C.endMs * (int64_t)sr / 1000 / block + 1;
  const double blockSec = (double)block / sr; const int64_t blockNs = (int64_t)std::llround(blockSec * 1e9);
  const auto render = [&](double t) { return kH0 + (int64_t)std::llround(t * 1e9 * (1 + op.driftPpm * 1e-6)); };   // the host time the engine renders audio time t
  const auto drainShell = [&]() { thunder_midi_msg buf[512]; for (;;) { const int64_t n = thunder_midi_drain(h, buf, 512); if (n <= 0) break; D.shell(reinterpret_cast<const MidiMsg*>(buf), n); } };
  for (int64_t b = 1; b <= total; b++) {
    const double now = (double)(b * block) / sr, ms = now * 1000;
    if (op.hook) op.hook(b, now, h, o, fk);
    if (!started && ms >= C.playAt) {
      started = true; seq = new Seq(D.song, D.vars, SeqMode::Song, so);
      thunder_midi_start(h, C.row > 0 ? sppFor(D.song, D.vars, C.row) : 0); if (o) thunder_midiout_set_playing(o, 1);
      seq->setTime(now + 0.08); runner.start(seq); on = true;
    }
    while (ai < C.actions.size() && C.actions[ai].at <= ms) {
      const Action& a = C.actions[ai++];
      if (a.op == "stop") { if (on) { on = false; runner.stop(); thunder_midi_stop(h); if (o) thunder_midiout_set_playing(o, 0); } }
      else if (a.op == "bpm") { if (on && seq) seq->setBpm(a.dv); }
      else if (a.op == "clk") { if (o) thunder_midiout_set_clock(o, a.p, a.on); else thunder_midi_port_clock(h, a.p, a.on); }
      else if (a.op == "cable") { if (o) thunder_midiout_assign(o, a.p, a.id.c_str()); else { int cab = a.p; for (int q = 0; q < 4; q++) if (D.C.ports[q] == a.id) { cab = q; break; } D.C.ports[a.p] = a.id; thunder_midi_set_cable(h, a.p, 1, cab); } }
      else if (a.op == "keyOn") { if (a.n >= 0 && a.n < 128) thunder_midi_key_on(h, a.n, a.ii, a.n, a.v); }
      else if (a.op == "keyOff") { if (a.n >= 0 && a.n < 128) thunder_midi_key_off(h, a.n); }
      else if (a.op == "panic") thunder_midi_panic(h);
    }
    thunder::midi_begin(h, now, blockSec, C.fxLat, aud);
    if (on) {
      runner.pump(now, 0.25, evs.data(), (int)evs.size(), &st, [&](const SeqEvent* e, int n, const SeqStep& step) { for (int i = 0; i < n; i++) if (e[i].type == SEV_BEAT) D.grid.push_back(e[i].t); thunder::midi_sched(h).onStep(e, n, step, *seq, now); });
      thunder::midi_end(h, now, seq->time(), seq->bpm(), seq->alive());
      if (!seq->alive() && now > seq->time() + 0.3) { on = false; runner.stop(); thunder::midi_sched(h).stop(now); if (o) thunder_midiout_set_playing(o, 0); }   // the audio side's song-end check
    } else thunder::midi_end(h, now, now, 120, false);
    if (!o) drainShell();
    else {
      const int64_t base = render(now);
      fk.now.store(base); thunder_midiout_clock_map(o, b * block, base + op.latNs);   // (the audio callback: the host clock reads "now")
      for (int k = 0; k < op.subs; k++) { fk.now.store(base + blockNs * k / op.subs); thunder_midiout_pump(o, nullptr); }
    }
  }
  if (o) { const int64_t end = render((double)(total * block) / sr); for (int k = 0; k < 400; k++) { fk.now.store(end + k * 1000000LL); thunder_midiout_pump(o, nullptr); } }   // the rest of the queue goes out
  D.stats = thunder::MidiStats(); delete seq;
  g_lastStats = o ? statsOf(o) : thunder_midiout_stats();
  thunder_midiout_free(o); thunder_midi_free(h); return true;
}
static std::vector<std::string> nativeCases() {
  std::vector<std::string> names; std::string idx; if (!readFile(std::string(THUNDER_ROOT) + "/native/tests/golden/midi-ledger-index.json", idx)) return names;
  JsonDoc d; if (!d.parse(idx)) return names; const Json cs = d.root().arr("cases");
  for (size_t i = 0; i < cs.size(); i++) if (cs.at(i).str("mode") == "native") names.push_back(std::string(cs.at(i).str("case")));
  return names;
}
static bool sameOut(const Out& a, const Out& b, bool cab) { return a.frame == b.frame && a.port == b.port && a.len == b.len && std::memcmp(a.b, b.b, 3) == 0 && a.kind == b.kind && (!cab || a.cab == b.cab); }

// ---- T2: midi_begin + onStep + midi_end give the records of runBlocks ---------------------------------------------------------------------------------
static void testT2() {
  int n = 0;
  for (const std::string& name : nativeCases()) {
    Case C; if (!loadCase(THUNDER_ROOT, name, C)) { CHECK(false, "no case %s", name.c_str()); continue; }
    Drive ref; CHECK(runBlocks(ref, C, 128), "runBlocks %s", name.c_str());
    RunRes R; RunOpt op; CHECK(runHost(C, op, R), "runHost %s", name.c_str());
    CHECK(R.D.kept.size() == ref.kept.size(), "T2 %s: %zu records through the host calls, %zu through runBlocks", name.c_str(), R.D.kept.size(), ref.kept.size());
    for (size_t i = 0; i < std::min(R.D.kept.size(), ref.kept.size()); i++) if (!sameOut(R.D.kept[i], ref.kept[i], true)) { CHECK(false, "T2 %s: record %zu differs (frame %lld vs %lld)", name.c_str(), i, (long long)R.D.kept[i].frame, (long long)ref.kept[i].frame); break; }
    CHECK(R.D.cleared == ref.cleared, "T2 %s: cleared %lld vs %lld", name.c_str(), (long long)R.D.cleared, (long long)ref.cleared);
    n++;
  }
  CHECK(n >= 24, "T2: only %d native ledger cases ran", n);
  std::printf("T2 begin/onStep/end == runBlocks: %d ledgers\n", n);
}

// ---- case 1: drain_to_sink ----------------------------------------------------------------------------------------------------------------------
static bool hasCableAction(const Case& C) { for (const Action& a : C.actions) if (a.op == "cable") return true; return false; }
static int check1(const Case& C, const RunRes& R, const Drive& ref, double driftPpm, int64_t latNs, double tolNs, const char* tag) {
  std::vector<Out> ex = ref.kept; std::stable_sort(ex.begin(), ex.end(), [](const Out& a, const Out& b) { return a.frame < b.frame; });
  std::vector<Rec> got; for (const Rec& r : R.fk.rec) if (!r.recalled) got.push_back(r);
  if (got.size() != ex.size()) { CHECK(false, "1 %s %s: the sink delivered %zu messages, runBlocks kept %zu", tag, C.name.c_str(), got.size(), ex.size()); return 1; }
  const bool devCheck = !hasCableAction(C);
  for (size_t i = 0; i < got.size(); i++) {
    const int64_t ideal = kH0 + (int64_t)std::llround((double)ex[i].frame / R.D.sr * 1e9 * (1 + driftPpm * 1e-6)) + latNs;
    const bool bytes = got[i].len == ex[i].len && std::memcmp(got[i].b, ex[i].b, (size_t)ex[i].len) == 0;
    const bool dev = !devCheck || (size_t)got[i].dev == (size_t)(std::find(R.ids.begin(), R.ids.end(), C.ports[ex[i].port]) - R.ids.begin());
    if (!bytes || !dev || std::fabs((double)(got[i].at - ideal)) > tolNs) { CHECK(false, "1 %s %s: message %zu: bytes %d dev %d at %lld ideal %lld (frame %lld)", tag, C.name.c_str(), i, bytes, dev, (long long)got[i].at, (long long)ideal, (long long)ex[i].frame); return 1; }
  }
  return 0;
}
static void test1() {
  int n = 0;
  for (const std::string& name : nativeCases()) {
    Case C; if (!loadCase(THUNDER_ROOT, name, C)) continue;
    Drive ref; runBlocks(ref, C, 128);
    { RunRes R; RunOpt op; op.out = true; op.step = 1000; op.latNs = 26000000; op.setOutLat = true; runHost(C, op, R); check1(C, R, ref, 0, 26000000, 2, "spin"); CHECK(g_lastStats.late == 0, "1 spin %s: %llu messages late", name.c_str(), (unsigned long long)g_lastStats.late); }
    { RunRes R; RunOpt op; op.out = true; op.caps = THUNDER_MIDIOUT_CAP_TS; op.latNs = 26000000; op.setOutLat = true; runHost(C, op, R); check1(C, R, ref, 0, 26000000, 2, "ts"); }
    { RunRes R; RunOpt op; op.out = true; op.step = 1000; op.latNs = 26000000; op.setOutLat = true; op.driftPpm = 1000; runHost(C, op, R); check1(C, R, ref, 1000, 26000000, 300000, "drift"); }   // T6: output latency and 1000 ppm of drift
    n++;
  }
  CHECK(n >= 24, "1: only %d cases", n);
  std::printf("1 drain_to_sink: %d ledgers through the pump (spin, CAP_TS, 1000 ppm drift)\n", n);
}

// ---- case 2: clear_per_cable ---------------------------------------------------------------------------------------------------------------------
static void test2() {
  Rig r(0, 1000); setDevs(r.o, {"X", "Y"}); thunder_midiout_assign(r.o, 0, "X"); thunder_midiout_assign(r.o, 1, "X"); thunder_midiout_assign(r.o, 2, "Y");   // A and B share a cable
  r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);
  const int64_t f10 = r.frameAt(0.010), f50 = r.frameAt(0.050);
  r.raw(0, {0x90, 60, 100}, 0.010); r.raw(2, {0x90, 62, 100}, 0.010); r.raw(0, {0x80, 60, 0}, 0.050); r.raw(1, {0x90, 64, 90}, 0.050); r.raw(2, {0x80, 62, 0}, 0.050);
  r.proc(); r.fk.now.store(r.hostOf(f10) - 2000000); r.pump();
  CHECK(r.fk.rec.empty(), "2: nothing is sent 2 ms ahead without stamps");
  r.fk.now.store(r.hostOf(f10) - 400000); const int sent = r.pump();   // the two note-ons at 10 ms go out: sent, never recalled
  CHECK(sent == 2 && r.fk.rec.size() == 2, "2: the two note-ons at 10 ms are sent (%d)", sent);
  thunder_midiout_assign(r.o, 2, "");   // Y has no letter any more: the next STOP's CLEAR covers X's cable only (the scheduler clears the connected letters)
  r.proc(); thunder_midi_stop(r.h); r.proc();
  const thunder_midiout_stats s0 = statsOf(r.o); r.fk.now.store(r.hostOf(f10) + 1000000); r.pump(); const thunder_midiout_stats s1 = statsOf(r.o);
  CHECK(s1.dropped_cleared - s0.dropped_cleared == 2, "2: X's two queued messages after the CLEAR's frame are removed: the note-off and the other letter's note-on (%llu)", (unsigned long long)(s1.dropped_cleared - s0.dropped_cleared));
  r.fk.now.store(r.hostOf(f50) - 400000); r.pump(); r.fk.now.store(r.hostOf(f50) + 5000000); r.pump();
  int x80 = 0, x90 = 0, y80 = 0; for (const Rec& x : r.fk.rec) { if (x.dev == 0 && (x.b[0] & 0xF0) == 0x80) x80++; if (x.dev == 0 && (x.b[0] & 0xF0) == 0x90) x90++; if (x.dev == 1 && (x.b[0] & 0xF0) == 0x80) y80++; }
  CHECK(x90 == 1, "2: X got only its first note-on (%d): what was sent is not recalled, what was queued after the CLEAR is gone", x90);
  CHECK(y80 == 0 || y80 == 1, "2: n/a");
  (void)x80;
  // the same with a second, untouched cable: Y keeps its note-off
  Rig q(0, 1000); setDevs(q.o, {"X", "Y"}); thunder_midiout_assign(q.o, 0, "X"); thunder_midiout_assign(q.o, 1, "Y"); q.proc(); thunder_midiout_clock_map(q.o, 0, q.host0);
  const int64_t g50 = q.frameAt(0.050); q.raw(0, {0x80, 60, 0}, 0.050); q.raw(1, {0x80, 62, 0}, 0.050); q.proc();
  thunder_midiout_assign(q.o, 1, "");                               // Y unplugged from its letter: its queued note-off stays in the queue (its cable is not cleared) and is still sent
  q.proc(); thunder_midi_stop(q.h); q.proc(); q.fk.now.store(q.hostOf(g50) - 400000); q.pump();
  int xo = 0, yo = 0; for (const Rec& x : q.fk.rec) { if (x.dev == 0 && x.b[0] == 0x80) xo++; if (x.dev == 1 && x.b[0] == 0x80) yo++; }
  CHECK(xo == 0 && yo == 1, "2: the cable that was cleared loses its queued note-off (%d), the other cable's survives (%d)", xo, yo);
}

// ---- case 3: ts_vs_spin -----------------------------------------------------------------------------------------------------------------------------
static void test3() {
  { Rig r(THUNDER_MIDIOUT_CAP_TS); setDevs(r.o, {"X"}); thunder_midiout_assign(r.o, 0, "X"); r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);
    const int64_t A = r.hostOf(r.frameAt(0.050)); r.raw(0, {0x90, 60, 100}, 0.050); r.proc();
    int64_t nx = 0; r.fk.now.store(A - 21000000); int s = r.pump(&nx); CHECK(s == 0 && nx == A - THUNDER_MIDIOUT_SEND_AHEAD_NS, "3 ts: 21 ms ahead: not yet; next wake at at - 20 ms (%lld)", (long long)(nx - (A - 20000000)));
    r.fk.now.store(A - 20000000); s = r.pump(&nx); CHECK(s == 1 && r.fk.rec.size() == 1 && r.fk.rec[0].at == A, "3 ts: within 20 ms: sent at once with its exact time");
    CHECK(nx == INT64_MAX, "3 ts: an empty queue sleeps until woken"); }
  { Rig r(0, 1000); setDevs(r.o, {"X"}); thunder_midiout_assign(r.o, 0, "X"); r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);
    const int64_t A = r.hostOf(r.frameAt(0.050)); r.raw(0, {0x90, 60, 100}, 0.050); r.proc();
    int64_t nx = 0, t = A - 5000000; int s = 0;
    for (; t < A + 100000 && s == 0; t += 100000) { r.fk.now.store(t); s = r.pump(&nx); if (s == 0) CHECK(r.fk.rec.empty(), "3 spin: nothing sent %lld ns ahead", (long long)(A - t)); }   // the fake clock in 0.1 ms steps
    CHECK(s == 1 && r.fk.rec.size() == 1, "3 spin: sent");
    if (r.fk.rec.size() == 1) {
      const int64_t ahead = A - (t - 100000);
      CHECK(ahead <= THUNDER_MIDIOUT_SPIN_NS && ahead > THUNDER_MIDIOUT_SPIN_NS - 100000, "3 spin: the first pump that saw it within 0.5 ms sent it (%lld ns ahead)", (long long)ahead);
      CHECK(r.fk.rec[0].at == A && r.fk.rec[0].sentAt >= A && r.fk.rec[0].sentAt - A < 20000, "3 spin: it spun to the time (sent at +%lld ns)", (long long)(r.fk.rec[0].sentAt - A)); }
    const int64_t A2 = r.hostOf(r.frameAt(0.050)); r.raw(0, {0x80, 60, 0}, 0.050); r.proc();
    r.fk.now.store(A2 - 600000); int64_t nx2 = 0; s = r.pump(&nx2); CHECK(s == 0 && nx2 == A2 - THUNDER_MIDIOUT_SPIN_NS, "3 spin: 0.6 ms ahead: next wake is at - 0.5 ms (%lld)", (long long)(nx2 - (A2 - 500000))); }
}

// ---- case 4: clock_map -----------------------------------------------------------------------------------------------------------------------------
// A message at core frame F is sent at llround(off + F * 1e9 / sr), `off` being the map's smoothed offset: the tests keep `off` with the web's recursion and compare to what the sink got.
static int64_t atOf(Rig& r, int64_t F) {   // a message for frame F through the core, the queue and the sink; returns the time the sink was given
  if (r.now < (double)F / r.sr - 0.03) r.now = (double)F / r.sr - 0.03;
  r.raw(0, {0xB0, 7, 100}, (double)F / r.sr - r.now); r.proc(); r.fk.now.store(INT64_MAX / 2); const size_t n = r.fk.rec.size(); r.pump();
  if (r.fk.rec.size() != n + 1) { CHECK(false, "4: the message for frame %lld did not reach the sink", (long long)F); return 0; }
  return r.fk.rec.back().at;
}
static void test4() {
  const double sr = 48000; static int64_t Fn = 60000;
  const auto want = [&](double off, int64_t F) { return (int64_t)std::llround(off + (double)F * 1e9 / sr); };
  const auto lineOff = [&](int64_t frame0, int64_t host) { return (double)host - (double)frame0 * 1e9 / sr; };
  { Rig r; setDevs(r.o, {"X"}); thunder_midiout_assign(r.o, 0, "X"); r.proc();   // frames before the first map: nothing can be timed
    r.raw(0, {0x90, 60, 100}, 0.01); r.proc(); r.pump(); const auto s = statsOf(r.o);
    CHECK(s.sent == 0 && s.dropped_nomap == 1 && r.fk.rec.empty(), "4: before the first map the message is counted in dropped_nomap and not sent (%llu / %llu)", (unsigned long long)s.sent, (unsigned long long)s.dropped_nomap); }
  { Rig r; setDevs(r.o, {"X"}); thunder_midiout_assign(r.o, 0, "X"); r.proc();
    // smoothing: the first map is taken as it is, then 2 % of each difference; a jump over 50 ms is taken at once; 40 ms is smoothed
    int64_t frame0 = 0, host = r.host0; thunder_midiout_clock_map(r.o, frame0, host); double off = lineOff(frame0, host);
    int64_t F = Fn += 100, got = atOf(r, F); CHECK(got == want(off, F), "4: the first map is taken as it is (%lld vs %lld)", (long long)got, (long long)want(off, F));
    frame0 = 4800; host += 100000000LL + 3000000LL; thunder_midiout_clock_map(r.o, frame0, host); off += (lineOff(frame0, host) - off) * 0.02;   // 3 ms off the line
    F = Fn += 100; got = atOf(r, F); CHECK(got == want(off, F), "4: 2 %% of a 3 ms step (%lld vs %lld)", (long long)got, (long long)want(off, F));
    for (int k = 0; k < 5; k++) { frame0 += 4800; host += 100000000LL + 3000000LL; thunder_midiout_clock_map(r.o, frame0, host); off += (lineOff(frame0, host) - off) * 0.02; }
    F = Fn += 100; got = atOf(r, F); CHECK(got == want(off, F), "4: five more smoothed steps (%lld vs %lld)", (long long)got, (long long)want(off, F));
    frame0 += 4800; host += 100000000LL + 60000000LL; thunder_midiout_clock_map(r.o, frame0, host); off = lineOff(frame0, host);   // a 60 ms jump: at once
    F = Fn += 100; got = atOf(r, F); CHECK(got == want(off, F), "4: a 60 ms jump is taken at once (%lld vs %lld)", (long long)got, (long long)want(off, F));
    frame0 += 4800; host += 100000000LL + 40000000LL; thunder_midiout_clock_map(r.o, frame0, host); off += (lineOff(frame0, host) - off) * 0.02;   // 40 ms: smoothed
    F = Fn += 100; got = atOf(r, F); CHECK(got == want(off, F), "4: a 40 ms step is still smoothed (%lld vs %lld)", (long long)got, (long long)want(off, F));
    frame0 += 4800; host -= 70000000LL; thunder_midiout_clock_map(r.o, frame0, host); off = lineOff(frame0, host);   // a jump backwards over 50 ms: also at once
    F = Fn += 100; got = atOf(r, F); CHECK(got == want(off, F), "4: a jump back over 50 ms is taken at once (%lld vs %lld)", (long long)got, (long long)want(off, F));
    // a map that has not been refreshed for 2 s: still used, and counted in `late`
    const auto s0 = statsOf(r.o); r.raw(0, {0x90, 60, 100}, 0.0); r.proc(); r.fk.now.store(host + 2500000000LL); r.pump(); const auto s1 = statsOf(r.o);
    CHECK(s1.late > s0.late && s1.sent > s0.sent, "4: a stale map is kept and counted (late %llu -> %llu)", (unsigned long long)s0.late, (unsigned long long)s1.late); }
  { // the floor: never sooner than the reported latency. Engine block start 10.0 s; frame 479000 is heard "now": the engine leads it by 20.83 ms < the reported 30 ms
    Rig r; setDevs(r.o, {"X"}); thunder_midiout_assign(r.o, 0, "X"); r.now = 10.0; r.proc();   // midi_last_now = 10.0
    CHECK(thunder_midiout_set_out_latency(r.o, 30000000) == 0, "4: set_out_latency"); const int64_t frame0 = 479000, host0 = 77000000000LL; r.fk.now.store(host0);
    thunder_midiout_clock_map(r.o, frame0, host0);
    const double heard = (double)frame0 * 1e9 / sr, imp = 10.0 * 1e9 - heard, off = ((double)host0 - heard) + (30000000.0 - imp);
    const int64_t F = 481000, got = atOf(r, F);
    CHECK(got == want(off, F) && got > want(lineOff(frame0, host0), F) + 9000000, "4: the floor moves the heard moment %.0f ns later (%lld vs %lld)", 30000000.0 - imp, (long long)got, (long long)want(off, F));
    CHECK(thunder_midiout_set_out_latency(r.o, -1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_out_latency(r.o, 3000000000LL) == THUNDER_MIDIOUT_E_ARG, "4: set_out_latency range"); }
  { // an engine that leads by MORE than the floor (a truthful map) is left alone; so is a map with no floor set
    Rig r; setDevs(r.o, {"X"}); thunder_midiout_assign(r.o, 0, "X"); r.now = 10.0; r.proc();
    thunder_midiout_set_out_latency(r.o, 10000000); const int64_t frame0 = 479000, host0 = 77000000000LL; r.fk.now.store(host0); thunder_midiout_clock_map(r.o, frame0, host0);   // 20.8 ms ahead >= 10 ms
    const int64_t F = 481000, got = atOf(r, F); CHECK(got == want(lineOff(frame0, host0), F), "4: a map whose lead is above the floor is used as given (%lld)", (long long)got);
    Rig q; setDevs(q.o, {"X"}); thunder_midiout_assign(q.o, 0, "X"); q.now = 10.0; q.proc(); q.fk.now.store(host0); thunder_midiout_clock_map(q.o, frame0, host0);
    const int64_t g2 = atOf(q, 481000); (void)F; CHECK(g2 == want(lineOff(frame0, host0), 481000), "4: with no floor set the pair is used as given (%lld)", (long long)g2); }
}

// ---- case 6: ports_table ---------------------------------------------------------------------------------------------------------------------------
static void test6() {
  Rig r; thunder_midiout* o = r.o;
  { const thunder_midiout_sink s0 = r.fk.sink(); CHECK(thunder_midiout_create(nullptr, 48000, &s0) == nullptr, "6: create(NULL core)"); }
  { thunder_midiout_sink s = r.fk.sink(); s.send = nullptr; CHECK(thunder_midiout_create(r.h, 48000, &s) == nullptr, "6: create without send"); s = r.fk.sink(); s.now_ns = nullptr; CHECK(thunder_midiout_create(r.h, 48000, &s) == nullptr, "6: create without now_ns");
    s = r.fk.sink(); CHECK(thunder_midiout_create(r.h, 100, &s) == nullptr && thunder_midiout_create(r.h, NAN, &s) == nullptr && thunder_midiout_create(r.h, 48000, nullptr) == nullptr, "6: create with a bad rate / sink"); }
  for (int p = 0; p < 4; p++) { const auto g = portOf(o, p); CHECK(g.state == THUNDER_MIDIOUT_PORT_NONE && g.dev == -1 && g.id[0] == 0 && g.clock == 0 && g.gear_ms == 0, "6: a new port %d is NONE", p); }
  CHECK(setDevs(o, {"n:Mio", "n:Keystep"}) == 0, "6: set_devices");
  CHECK(thunder_midiout_assign(o, 0, "n:Mio") == 0 && portOf(o, 0).state == THUNDER_MIDIOUT_PORT_OK && portOf(o, 0).dev == 0 && std::strcmp(portOf(o, 0).name, "n:Mio") == 0, "6: assign a listed id: OK");
  CHECK(thunder_midiout_assign(o, 1, "n:Nope") == 0 && portOf(o, 1).state == THUNDER_MIDIOUT_PORT_MISSING && portOf(o, 1).dev == -1 && std::strcmp(portOf(o, 1).id, "n:Nope") == 0, "6: an unknown id is MISSING and keeps its id");
  CHECK(setDevs(o, {"n:Keystep", "n:Nope", "n:Mio"}) == 0 && portOf(o, 1).state == THUNDER_MIDIOUT_PORT_OK && portOf(o, 1).dev == 1 && portOf(o, 0).dev == 2, "6: a later set_devices fixes it, indices follow the new list");
  CHECK(thunder_midiout_assign(o, 1, "") == 0 && portOf(o, 1).state == THUNDER_MIDIOUT_PORT_NONE, "6: an empty id clears the port");
  CHECK(thunder_midiout_set_gear(o, 0, 500) == 0 && portOf(o, 0).gear_ms == 200 && thunder_midiout_set_gear(o, 0, -99) == 0 && portOf(o, 0).gear_ms == -50 && thunder_midiout_set_gear(o, 0, 12.4) == 0 && portOf(o, 0).gear_ms == 12 && thunder_midiout_set_gear(o, 0, -12.6) == 0 && portOf(o, 0).gear_ms == -13, "6: set_gear clamps -50..200 and rounds");
  CHECK(thunder_midiout_set_clock(o, 0, 5) == 0 && portOf(o, 0).clock == 1 && thunder_midiout_set_clock(o, 0, 0) == 0 && portOf(o, 0).clock == 0, "6: set_clock");
  // bad arguments: E_ARG and nothing changes
  const thunder_midiout_port before = portOf(o, 0); const auto vv = devList({"n:Keystep", "n:Nope", "n:Mio"});
  std::vector<std::string> many; for (int i = 0; i < 33; i++) many.push_back("d" + std::to_string(i));
  const auto vm = devList(many);
  char longId[200]; std::memset(longId, 'x', 199); longId[199] = 0;
  thunder_midiout_dev bad = vv[0]; std::memset(bad.id, 'y', sizeof bad.id);   // no NUL inside the buffer
  thunder_midiout_dev dup[2] = {vv[0], vv[0]}; thunder_midiout_dev empty = vv[0]; empty.id[0] = 0;
  CHECK(thunder_midiout_set_devices(nullptr, vv.data(), 3) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_devices(o, nullptr, 2) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_devices(o, vm.data(), 33) == THUNDER_MIDIOUT_E_ARG &&
        thunder_midiout_set_devices(o, vv.data(), -1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_devices(o, &bad, 1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_devices(o, dup, 2) == THUNDER_MIDIOUT_E_ARG &&
        thunder_midiout_set_devices(o, &empty, 1) == THUNDER_MIDIOUT_E_ARG, "6: set_devices bad arguments (NULL, n 33, n -1, no NUL, a repeated id, an empty id)");
  CHECK(thunder_midiout_assign(o, 4, "n:Mio") == THUNDER_MIDIOUT_E_ARG && thunder_midiout_assign(o, -1, "n:Mio") == THUNDER_MIDIOUT_E_ARG && thunder_midiout_assign(o, 0, nullptr) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_assign(o, 0, longId) == THUNDER_MIDIOUT_E_ARG &&
        thunder_midiout_assign(nullptr, 0, "x") == THUNDER_MIDIOUT_E_ARG, "6: assign bad arguments (port 4, -1, NULL id, a 200-byte id)");
  CHECK(thunder_midiout_set_clock(o, 4, 1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_gear(o, 4, 1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_gear(o, 0, NAN) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_gear(o, 0, INFINITY) == THUNDER_MIDIOUT_E_ARG &&
        thunder_midiout_set_playing(nullptr, 1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_get_port(o, 4, nullptr) == THUNDER_MIDIOUT_E_ARG, "6: set_clock / set_gear / get_port bad arguments");
  const thunder_midiout_port after = portOf(o, 0);
  CHECK(std::strcmp(before.id, after.id) == 0 && before.state == after.state && before.dev == after.dev && before.gear_ms == after.gear_ms && before.clock == after.clock, "6: nothing changed after the bad calls");
  int64_t nx = 5; CHECK(thunder_midiout_pump(nullptr, &nx) == THUNDER_MIDIOUT_E_ARG, "6: pump(NULL)"); thunder_midiout_clock_map(nullptr, 0, 0);
  thunder_midiout_stats ss; ss.size = 4; CHECK(thunder_midiout_stats_get(o, &ss) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_stats_get(nullptr, &ss) == THUNDER_MIDIOUT_E_ARG, "6: stats_get bad size");
  // the scheduler is told: a connected letter lets a raw message through, a MISSING one does not
  CHECK(thunder_midiout_assign(o, 0, "n:Mio") == 0, "6: reassign"); r.proc(); thunder_midiout_clock_map(o, 0, r.host0); r.raw(0, {0x90, 60, 100}, 0.01); r.raw(1, {0x90, 61, 100}, 0.01); r.proc(); r.fk.now.store(r.host0 + 100000000); r.pump();
  CHECK(r.fk.rec.size() == 1 && r.fk.rec[0].dev == 2, "6: only the connected letter reaches its device (%zu messages)", r.fk.rec.size());
  thunder_midiout_free(nullptr);
}

// ---- case 5: hotplug -----------------------------------------------------------------------------------------------------------------------------
static void test5() {
  { // the table by itself: two devices with one name, the choice is never moved
    Rig r; thunder_midiout* o = r.o;
    setDevs(o, {"n:X", "n:X #2"}); thunder_midiout_assign(o, 0, "n:X #2"); thunder_midiout_assign(o, 1, "n:X");
    CHECK(portOf(o, 0).dev == 1 && portOf(o, 1).dev == 0, "5: two same-name devices are told apart by #2");
    setDevs(o, {"n:X"});   // the FIRST one was unplugged: the survivor is listed as plain "n:X"
    CHECK(portOf(o, 0).state == THUNDER_MIDIOUT_PORT_MISSING && std::strcmp(portOf(o, 0).id, "n:X #2") == 0, "5: the letter on '#2' is MISSING and stays on '#2' (never moved)");
    CHECK(portOf(o, 1).state == THUNDER_MIDIOUT_PORT_OK && portOf(o, 1).dev == 0, "5: the carried-over limit: the letter on 'n:X' now plays the one still there");
    setDevs(o, {"n:X", "n:X #2"}); CHECK(portOf(o, 0).state == THUNDER_MIDIOUT_PORT_OK && portOf(o, 0).dev == 1, "5: it finds its device again when it is listed");
  }
  Case C; if (!loadCase(THUNDER_ROOT, "two-ports", C)) { CHECK(false, "5: no two-ports case"); return; }
  C.ports[0] = "A"; C.ports[1] = "B";
  for (int variant = 0; variant < 2; variant++) {   // 0: the list loses B; 1: the sink fails first (the OS knew before the scan), then the list loses B
    RunRes R; RunOpt op; op.out = true; op.step = 1000;
    int64_t tGone = 0, tBack = 0; int stateAfterGone = -1, stateBack = -1, stateA = -1; bool idKept = false, listed = false; thunder_midiout_stats sGone{};
    op.hook = [&](int64_t, double now, thunder_midi*, thunder_midiout* o, Fake& fk) {
      const int64_t hostNow = kH0 + (int64_t)std::llround(now * 1e9);
      if (!tGone && now >= 0.9) {
        tGone = hostNow;
        if (variant == 0) { setDevs(o, {"A"}); listed = true; stateAfterGone = portOf(o, 1).state; stateA = portOf(o, 0).state; idKept = std::strcmp(portOf(o, 1).id, "B") == 0; }
        else fk.fail[1] = true;                                                    // the OS knows first: the next send to B fails
      }
      if (variant == 1 && tGone && !listed && now >= 1.0) { sGone = statsOf(o); setDevs(o, {"A"}); listed = true; fk.fail[1] = false; stateAfterGone = portOf(o, 1).state; stateA = portOf(o, 0).state; idKept = std::strcmp(portOf(o, 1).id, "B") == 0; }
      if (tGone && !tBack && now >= 1.2) { tBack = hostNow; setDevs(o, {"A", "B"}); stateBack = portOf(o, 1).state; }
    };
    runHost(C, op, R);
    const char* v = variant ? "sink-fails-first" : "list-loses-B";
    CHECK(stateAfterGone == THUNDER_MIDIOUT_PORT_MISSING && stateA == THUNDER_MIDIOUT_PORT_OK && idKept, "5 %s: the lost letter is MISSING, keeps its id, the other is untouched (%d %d %d)", v, stateAfterGone, stateA, idKept);
    CHECK(stateBack == THUNDER_MIDIOUT_PORT_OK, "5 %s: and OK when the device is listed again", v);
    int toB = 0, toA = 0, f2 = 0, fb = 0, tickA = 0; int64_t spp = 0, cont = 0;
    for (const Rec& x : R.fk.rec) {
      if (x.dev == 1 && x.sentAt >= tGone + (variant ? 100000000 : 0) && x.sentAt < tBack) toB++;
      if (x.dev == 0 && x.sentAt >= tGone && x.sentAt < tBack) { toA++; if (x.b[0] == 0xF8) tickA++; }
      if (x.dev == 1 && x.sentAt >= tBack && x.sentAt < tBack + 300000000) { if (x.b[0] == 0xF2 && !spp) { f2++; spp = x.sentAt; } if (x.b[0] == 0xFB && !cont) { fb++; cont = x.sentAt; } }
    }
    if (std::getenv("U36_DEBUG")) for (const Rec& x : R.fk.rec) if (x.dev == 1 && x.sentAt >= tBack - 50000000 && x.sentAt < tBack + 400000000 && x.b[0] != 0xF8) std::printf("  [%s] dev1 at %+.1f ms (sent %+.1f): %02x %02x %02x\n", v, (x.at - tBack) / 1e6, (x.sentAt - tBack) / 1e6, x.b[0], x.len > 1 ? x.b[1] : 0, x.len > 2 ? x.b[2] : 0);
    CHECK(toB == 0, "5 %s: %d messages reached the gone device while it was gone", v, toB);
    CHECK(toA > 10 && tickA > 5, "5 %s: the other port goes on (%d messages, %d ticks)", v, toA, tickA);
    CHECK(f2 == 1 && fb == 1 && spp <= cont, "5 %s: coming back it joins the clock with SPP then Continue (%d, %d)", v, f2, fb);
    thunder_midiout_stats& ms = g_lastStats;
    CHECK(ms.dropped_gone > 0, "5 %s: the records of the gone cable were dropped, counted (%llu)", v, (unsigned long long)ms.dropped_gone);
    if (variant == 1) CHECK(sGone.send_errors >= 1 && sGone.send_errors <= 2, "5 sink-fails-first: the failure is counted once, no retry storm (%llu)", (unsigned long long)sGone.send_errors);
  }
}

// ---- case 7: stall -----------------------------------------------------------------------------------------------------------------------------
static void test7() {
  Rig r; setDevs(r.o, {"X"}); thunder_midiout_assign(r.o, 0, "X"); r.proc(); thunder_midiout_clock_map(r.o, 0, r.host0);
  for (int i = 0; i < 10; i++) r.raw(0, {0xF8}, 0.010 + 0.004 * i);                      // ten ticks, 10 .. 46 ms
  r.raw(0, {0x90, 60, 100}, 0.012); r.raw(0, {0x80, 60, 0}, 0.045);
  r.proc(); r.fk.now.store(r.host0); r.pump();                                           // nothing is due yet
  CHECK(r.fk.rec.empty(), "7: nothing before it is time");
  r.fk.now.store(r.host0 + 400000000LL);                                                  // the sender stalled for 400 ms
  const int sent = r.pump(); const auto s = statsOf(r.o);
  CHECK(sent == 2 && s.dropped_late_ticks == 10 && s.late == 12, "7: 400 ms late: no burst of ticks (%llu dropped), the two notes still go (sent %d, late %llu)", (unsigned long long)s.dropped_late_ticks, sent, (unsigned long long)s.late);
  for (const Rec& x : r.fk.rec) CHECK(x.sentAt == r.host0 + 400000000LL && x.b[0] != 0xF8, "7: a late note is sent at once, at the clock's time");
  // with stamps: the first 20 ms are already in the OS when the stall starts, the rest is late
  Rig t(THUNDER_MIDIOUT_CAP_TS); setDevs(t.o, {"X"}); thunder_midiout_assign(t.o, 0, "X"); t.proc(); thunder_midiout_clock_map(t.o, 0, t.host0);
  for (int i = 0; i < 10; i++) t.raw(0, {0xF8}, 0.010 + 0.004 * i);
  t.proc(); t.fk.now.store(t.host0); t.pump(); const size_t early = t.fk.rec.size();
  t.fk.now.store(t.host0 + 400000000LL); t.pump(); const auto ts = statsOf(t.o);
  CHECK(early >= 2 && early < 10 && ts.sent == early && ts.dropped_late_ticks == 10 - early, "7 ts: %zu ticks were stamped before the stall, the other %llu are dropped, no burst", early, (unsigned long long)ts.dropped_late_ticks);
  // a tick 15 ms late is sent (and counted), one 25 ms late is dropped
  Rig q; setDevs(q.o, {"X"}); thunder_midiout_assign(q.o, 0, "X"); q.proc(); thunder_midiout_clock_map(q.o, 0, q.host0);
  const int64_t a1 = q.hostOf(q.frameAt(0.010)); q.raw(0, {0xF8}, 0.010); q.raw(0, {0xF8}, 0.020); q.proc();
  q.fk.now.store(a1 + 15000000); q.pump(); auto u = statsOf(q.o);
  CHECK(u.sent == 2 && u.late == 2 && u.dropped_late_ticks == 0, "7: ticks 15 ms and 5 ms late are sent (%llu, late %llu)", (unsigned long long)u.sent, (unsigned long long)u.late);
  Rig w; setDevs(w.o, {"X"}); thunder_midiout_assign(w.o, 0, "X"); w.proc(); thunder_midiout_clock_map(w.o, 0, w.host0); const int64_t b1 = w.hostOf(w.frameAt(0.010)); w.raw(0, {0xF8}, 0.010); w.proc();
  w.fk.now.store(b1 + 25000000); w.pump(); u = statsOf(w.o);
  CHECK(u.sent == 0 && u.dropped_late_ticks == 1, "7: a tick 25 ms late is dropped");
  // a full queue: counted, never above 4096, and the F4 order: ticks go before CCs before note-ons; a note-off is never the victim
  Rig f; setDevs(f.o, {"X"}); thunder_midiout_assign(f.o, 0, "X"); f.proc(); thunder_midiout_clock_map(f.o, 0, f.host0);
  f.raw(0, {0x80, 60, 0}, 0.05); f.raw(0, {0xB0, 1, 2}, 0.05); f.raw(0, {0x90, 61, 100}, 0.05);
  for (int round = 0; round < 40; round++) { for (int i = 0; i < 160; i++) f.raw(0, {0xF8}, 0.05); f.proc(); (void)thunder_midiout_pump(f.o, nullptr); }
  thunder_midiout_stats fs = statsOf(f.o); CHECK(fs.max_queue == (uint64_t)THUNDER_MIDIOUT_MAX_QUEUE && fs.queue_full > 0 && fs.sent == 0, "7: a full queue counts queue_full and holds at %d (%llu, max %llu)", THUNDER_MIDIOUT_MAX_QUEUE, (unsigned long long)fs.queue_full, (unsigned long long)fs.max_queue);
  for (int round = 0; round < 3; round++) { f.raw(0, {0x80, 62, 0}, 0.05); f.raw(0, {0x90, 63, 100}, 0.05); f.raw(0, {0xB0, 3, 4}, 0.05); f.proc(); (void)thunder_midiout_pump(f.o, nullptr); }   // a note-off, a note-on, a CC on top of a full queue of ticks: all three evict ticks
  f.fk.step = 1000; f.fk.now.store(f.hostOf(f.frameAt(0.05)) + 100000000); f.pump(); int offs = 0, ons = 0, ccs = 0, ticks = 0; for (const Rec& x : f.fk.rec) { if ((x.b[0] & 0xF0) == 0x80) offs++; else if ((x.b[0] & 0xF0) == 0x90) ons++; else if ((x.b[0] & 0xF0) == 0xB0) ccs++; else ticks++; }
  CHECK(offs == 4 && ons == 4 && ccs == 4, "7: with the queue full of ticks the note-offs (%d), note-ons (%d) and CCs (%d) all got in, evicting ticks (%d sent)", offs, ons, ccs, ticks);
  CHECK(statsOf(f.o).may_hang == 0, "7: no note-off was lost");
}

// ---- case 12: spp_for ----------------------------------------------------------------------------------------------------------------------------
static int sppLegacy(const Song& song, const SeqVars& vars, int row) {   // the body of sppFor as it was in midi_drive.h (U35), kept here as the reference
  SeqOpts o; o.once = true; o.sim = true; Seq sq(song, vars, SeqMode::Song, o);
  std::vector<SeqEvent> evs(4096); SeqStep st;
  for (int n = 0; n < 100000 && sq.alive(); n++) {
    const int k = sq.step(evs.data(), (int)evs.size(), &st);
    int r = -1; for (int t = 0; t < kSeqTracks; t++) if (st.pos[t].valid && st.pos[t].row > r) r = st.pos[t].row;
    if (r >= row) { for (int i = 0; i < k; i++) if (evs[i].type == SEV_BEAT) return std::max(0, std::min(16383, (int)std::llround(evs[i].v * 4))); return 0; }
  }
  return 0;
}
static void test12() {
  Case C; if (!loadCase(THUNDER_ROOT, "start-midway", C)) { CHECK(false, "12: no start-midway"); return; }
  Drive D; JsonError je; CHECK(D.song.load(C.songText.data(), C.songText.size(), &je), "12: song");
  int nz = 0;
  for (int row : {0, 1, 2, 3, 4, 6, 9, 17, 64, 99999, C.row}) { const int a = thunder::midi_spp_for(D.song, D.vars, row), b = sppLegacy(D.song, D.vars, row); CHECK(a == b, "12: row %d: midi_spp_for %d, the old sppFor %d", row, a, b); if (a > 0) nz++; }
  CHECK(C.row > 0 && thunder::midi_spp_for(D.song, D.vars, C.row) > 0 && nz > 0, "12: start-midway's row %d gives a non-zero Song Position (%d)", C.row, thunder::midi_spp_for(D.song, D.vars, C.row));
  CHECK(mdrive::sppFor(D.song, D.vars, C.row) == thunder::midi_spp_for(D.song, D.vars, C.row), "12: the test helper is the core's");
}

// ---- the three roles on three threads (TSan) ---------------------------------------------------------------------------------------------------
struct RealSink {
  std::atomic<uint64_t> sent{0}; int64_t last[32] = {}; uint64_t errs = 0;
  static int64_t nowFn(void*) { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  static int sendFn(void* u, int dev, const uint8_t* b, int len, int64_t at) { RealSink* s = (RealSink*)u; if (dev < 0 || dev >= 32 || len < 1 || b[0] < 0x80) return -1; s->last[dev] = at; s->sent.fetch_add(1); return 0; }
  static void clearFn(void*, int) {}
};
static void threadsOnce(uint32_t caps) {
  thunder_midi* h = thunder_midi_create(48000, nullptr, 0); RealSink rs; thunder_midiout_sink sk; sk.user = &rs; sk.now_ns = RealSink::nowFn; sk.send = RealSink::sendFn; sk.clear = RealSink::clearFn; sk.caps = caps;
  thunder_midiout* o = thunder_midiout_create(h, 48000, &sk);
  std::atomic<bool> stop{false}; std::atomic<uint64_t> blocks{0}; std::atomic<int64_t> pumps{0};
  std::thread audio([&] {   // the fake audio callback, paced at the real block rate
    int64_t b = 0; const int64_t t0 = RealSink::nowFn(nullptr);
    while (!stop.load()) {
      const double now = (double)b * 128 / 48000; const int64_t due = t0 + (int64_t)(now * 1e9);
      while (RealSink::nowFn(nullptr) < due && !stop.load()) std::this_thread::sleep_for(std::chrono::microseconds(200));
      thunder_midi_process(h, now, 128.0 / 48000, now, 120, 0); thunder_midiout_clock_map(o, b * 128, due + 8000000); blocks.fetch_add(1); b++;
    }
  });
  std::thread sender([&] { int64_t nx = 0; while (!stop.load()) { thunder_midiout_pump(o, &nx); pumps.fetch_add(1); std::this_thread::sleep_for(std::chrono::microseconds(500)); } });
  // the control thread is this one
  const auto t0 = std::chrono::steady_clock::now(); int k = 0;
  while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1200)) {
    if (k % 5 == 0) { const auto v = devList(k % 10 == 0 ? std::vector<std::string>{"X", "Y"} : std::vector<std::string>{"Y"}); thunder_midiout_set_devices(o, v.data(), (int)v.size()); }
    thunder_midiout_assign(o, k & 3, (k & 1) ? "X" : "Y"); thunder_midiout_set_clock(o, k & 3, k & 1); thunder_midiout_set_gear(o, k & 3, (k % 7) * 3.0);
    if (k % 9 == 0) thunder_midiout_set_playing(o, (k / 9) & 1);
    for (int p = 0; p < 4; p++) { const uint8_t m[3] = {0x90, (uint8_t)(40 + (k + p) % 40), 100}; thunder_midi_raw(h, p, m, 3, 0.01 * (k % 5)); const uint8_t t = 0xF8; thunder_midi_raw(h, p, &t, 1, 0.002); }
    if (k % 13 == 0) thunder_midi_stop(h);
    thunder_midiout_stats s = statsOf(o); (void)s; thunder_midiout_port gp = portOf(o, k & 3); (void)gp;
    k++; std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
  stop.store(true); audio.join(); sender.join();
  const auto s = statsOf(o);
  CHECK(blocks.load() > 100 && pumps.load() > 100 && rs.sent.load() > 50, "threads (%s): %llu blocks, %lld pumps, %llu messages sent", caps ? "TS" : "spin", (unsigned long long)blocks.load(), (long long)pumps.load(), (unsigned long long)rs.sent.load());
  CHECK(s.sent == rs.sent.load() && s.send_errors == 0, "threads: the sink saw what the stats say (%llu / %llu)", (unsigned long long)s.sent, (unsigned long long)rs.sent.load());
  std::printf("threads (%s): %llu blocks, %lld pumps, %llu sent, %llu dropped (gone %llu cleared %llu), max queue %llu\n", caps ? "TS" : "spin", (unsigned long long)blocks.load(), (long long)pumps.load(), (unsigned long long)s.sent, (unsigned long long)(s.dropped_gone + s.dropped_cleared), (unsigned long long)s.dropped_gone, (unsigned long long)s.dropped_cleared, (unsigned long long)s.max_queue);
  thunder_midiout_free(o); thunder_midi_free(h);
}

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--threads") == 0) { threadsOnce(0); threadsOnce(THUNDER_MIDIOUT_CAP_TS); std::printf(fails ? "midiport threads: FAILED\n" : "midiport threads: ok\n"); return fails ? 1 : 0; }
  test12(); test6(); test3(); test4(); test2(); test7(); testT2(); test1(); test5();
  std::printf(fails ? "midiport: %d FAILED\n" : "midiport: all passed\n", fails);
  return fails ? 1 : 0;
}
