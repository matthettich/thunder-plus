// A CLEAR (the Stop) meets stamped messages the pump has already handed to the sink (up to 20 ms ahead). A sink that cannot recall them (clear == NULL, or it returns INT64_MAX) leaves them
// to play, and the core must not replay them and must end every note it started: the last message of every note, by stamp, is a note-off. A recalling sink (returns 0) is the old behaviour:
// nothing is released. The song is the debug song of the Android shell (two bars of C major, 120 bpm) through the real scheduler, pump and clock map on a FAKE clock: the audio clock
// runs 1:1 with the host clock plus the output latency; the Stop is issued at every offset before / after a note-on; latency 0, 5, 19, 20, 26 ms; Gear delay 0, +30, -20 ms.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "midi/midi_host.h"
#include "seq/runner.h"
#include "seq/seq.h"
#include "song/song.h"
#include "thunder/midi.h"
#include "thunder/midiport.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace {
using namespace thunder;
struct Rec { int64_t at; int order; uint8_t b[3]; int len; bool recalled; };
struct Os {
  int64_t now = 0; int mode = 0;   // 0: clear == NULL, 1: clear returns INT64_MAX (recalls nothing), 2: clear recalls everything not yet due and returns 0
  std::vector<Rec> r; int clears = 0;
  static int64_t nowFn(void* u) { return static_cast<Os*>(u)->now; }
  static int sendFn(void* u, uint64_t dev, const uint8_t* b, int len, int64_t at) { Os* o = static_cast<Os*>(u); if (dev != 7) return -1; Rec x; std::memset(&x, 0, sizeof x); std::memcpy(x.b, b, (size_t)len); x.len = len; x.at = at; x.order = (int)o->r.size(); o->r.push_back(x); return 0; }
  static int64_t clearFn(void* u, uint64_t) { Os* o = static_cast<Os*>(u); o->clears++; if (o->mode == 1) return INT64_MAX; for (Rec& x : o->r) if (x.at > o->now) x.recalled = true; return 0; }
};

std::string songText() {   // the debug song of midi_out.cpp
  std::string s = "{\"app\":\"thunder-song\",\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"swing\":50,\"dlen\":16,\"plen\":[16],\"song\":[";
  for (int r = 0; r < 2; r++) { s += r ? ",[" : "["; s += "0"; for (int t = 1; t < 33; t++) s += ",-1"; s += "]"; }
  s += "],\"chains\":[[[0,0]"; for (int k = 1; k < 16; k++) s += ",[-1,0]";
  s += "]],\"phrases\":[[";
  const int notes[4] = {48, 52, 55, 60};
  for (int row = 0; row < 16; row++) {
    if (row) s += ",";
    char b[96];
    if (row % 4 == 0) std::snprintf(b, sizeof b, "[%d,-1,-1,-1,255,0,-1,-1,-1,-1,-1,-1]", notes[row / 4]);
    else if (row % 4 == 2) std::snprintf(b, sizeof b, "[200,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]");
    else std::snprintf(b, sizeof b, "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]");
    s += b;
  }
  s += "]],\"insts\":[{\"mo\":1,\"mch\":0,\"mpt\":0,\"pbr\":2}]}";
  return s;
}

struct Result { bool ok = true; uint64_t released = 0, cleared = 0, sent = 0; int clears = 0; int stuck = 0, dup = 0; };

// One run. leadNs: the Stop is issued this long BEFORE the second note-on's time (negative: after it).
Result run(int mode, int64_t latNs, double gearMs, int64_t leadNs, int64_t pumpDelayNs = 0) {   // pumpDelayNs: the pump does not run for this long after the Stop is posted (the CLEAR is drained late)
  const double sr = 48000; const int blk = 128; const int64_t H0 = 1000000000LL;
  Os os; os.mode = mode; os.now = H0;
  thunder_midiout_sink sk; std::memset(&sk, 0, sizeof sk); sk.user = &os; sk.now_ns = Os::nowFn; sk.send = Os::sendFn; sk.clear = mode == 0 ? nullptr : Os::clearFn; sk.caps = THUNDER_MIDIOUT_CAP_TS;
  thunder_midi* h = thunder_midi_create(sr, nullptr, 0); thunder_midiout* o = thunder_midiout_create(h, sr, &sk);
  thunder_midiout_dev d; std::memset(&d, 0, sizeof d); std::strcpy(d.id, "n:Fake"); std::strcpy(d.name, "Fake"); d.token = 7; d.flags = THUNDER_MIDIOUT_DEV_TS;
  thunder_midiout_set_devices(o, &d, 1); thunder_midiout_assign(o, 0, "n:Fake"); thunder_midiout_set_out_latency(o, latNs);
  const std::string text = songText();
  Song song; SeqVars vars; JsonError je; song.load(text.data(), text.size(), &je);
  SeqOpts so; so.live = true; so.once = true; so.row = 0;
  std::unique_ptr<Seq> seq(new Seq(song, vars, SeqMode::Song, so)); SeqRunner runner; std::vector<SeqEvent> evs(256); SeqStep st;
  thunder_midiout_set_clock(o, 0, 1); thunder_midiout_set_gear(o, 0, gearMs); thunder_midiout_set_playing(o, 1);
  thunder_midi_set_song(h, reinterpret_cast<const uint8_t*>(text.data()), text.size()); thunder_midi_start(h, 0);
  const int64_t stepNs = 250000, blkNs = (int64_t)(blk / sr * 1e9);
  const int64_t T1 = 500000000LL + 500000000LL;   // the second note-on (song time 0.5 s after the start + 0.5 s): audio time
  const int64_t stopAt = T1 - leadNs;
  int64_t nextBlk = 0, frames = 0; bool stopped = false; int64_t tStop = 0;
  const int64_t tEnd = stopAt + 600000000LL;
  for (int64_t t = 0; t <= tEnd; t += stepNs) {
    os.now = H0 + t;
    if (!stopped && t >= stopAt) {   // the Stop: what engineStop does for the last block, at this audio time
      const double now = (double)t / 1e9;
      midi_begin(h, now, 0.01, 0.0, ~(uint64_t)0); midi_sched(h).stop(now); midi_end(h, now, now, 120, false); thunder_midiout_set_playing(o, 0);
      stopped = true; tStop = t;
    }
    while (!stopped && t >= nextBlk) {   // an audio block (the callback runs at the audio time: the block is heard one output latency later)
      const double now = (double)frames / sr, bs = (double)blk / sr;
      midi_begin(h, now, bs, 0.0, ~(uint64_t)0);
      if (frames == 0) { seq->setTime(now + 0.5); runner.start(seq.get()); }
      runner.pump(now, 0.25, evs.data(), (int)evs.size(), &st, [&](const SeqEvent* e, int n, const SeqStep& step) { midi_sched(h).onStep(e, n, step, *seq, now); });
      midi_end(h, now, seq->time(), seq->bpm(), seq->alive());
      thunder_midiout_clock_map(o, frames, H0 + (int64_t)(now * 1e9) + latNs);
      frames += blk; nextBlk += blkNs;
    }
    if (!stopped || t >= tStop + pumpDelayNs) thunder_midiout_pump(o, nullptr);
  }
  Result R; thunder_midiout_stats ss; std::memset(&ss, 0, sizeof ss); ss.size = sizeof ss; thunder_midiout_stats_get(o, &ss);
  R.released = ss.released_after_stop; R.cleared = ss.dropped_cleared; R.sent = ss.sent; R.clears = os.clears;
  // the device's view: what was not recalled, by stamp (then by arrival)
  std::map<int, std::vector<const Rec*>> byNote;
  for (const Rec& x : os.r) { if (x.recalled) continue; const int ty = x.b[0] & 0xF0; if (ty == 0x80 || ty == 0x90) byNote[(x.b[0] & 15) * 128 + x.b[1]].push_back(&x); }
  for (auto& kv : byNote) {
    auto& v = kv.second;
    const Rec* last = v[0];
    for (const Rec* x : v) if (x->at > last->at || (x->at == last->at && x->order > last->order)) last = x;
    const int ty = last->b[0] & 0xF0;
    if (!(ty == 0x80 || last->b[2] == 0)) { R.stuck++; R.ok = false; }
  }
  for (size_t i = 0; i < os.r.size(); i++) for (size_t j = i + 1; j < os.r.size(); j++) {   // a note-on sent twice with the same stamp: a replay
    const Rec &a = os.r[i], &b = os.r[j];
    if (!a.recalled && !b.recalled && (a.b[0] & 0xF0) == 0x90 && a.b[2] > 0 && a.len == b.len && a.at == b.at && std::memcmp(a.b, b.b, (size_t)a.len) == 0) { R.dup++; R.ok = false; }
  }
  thunder_midiout_free(o); thunder_midi_free(h);
  return R;
}
}  // namespace

int main() {
  const int64_t lats[] = {0, 5000000, 19000000, 20000000, 26000000};
  const double gears[] = {0, 30, -20};
  uint64_t releasedTotal[3] = {0, 0, 0}; int runs = 0, bad = 0;
  for (int mode = 0; mode <= 2; mode++) for (int64_t lat : lats) for (double gear : gears) for (int lead = -5; lead <= 25; lead++) {
    const Result R = run(mode, lat, gear, (int64_t)lead * 1000000LL);
    runs++; releasedTotal[mode] += R.released;
    if (!R.ok) { bad++; if (bad <= 10) std::printf("FAIL mode %d lat %lld ms gear %.0f lead %d ms: %d notes left on, %d replayed note-ons\n", mode, (long long)(lat / 1000000), gear, lead, R.stuck, R.dup); g_fail++; }
    if (mode == 2) CHECK(R.released == 0, "a recalling sink never gets a release (lat %lld gear %.0f lead %d: %llu)", (long long)(lat / 1000000), gear, lead, (unsigned long long)R.released);
  }
  // the CLEAR is drained late (the pump stalls after the Stop): a note-on that was handed over, and is already due when the CLEAR is drained, must be released too, with every kind of sink
  uint64_t lateReleased[3] = {0, 0, 0};
  for (int mode = 0; mode <= 2; mode++) for (int64_t lat : {(int64_t)0, (int64_t)5000000}) for (double gear : gears) for (int64_t dly : {500000LL, 2000000LL, 10000000LL}) for (int lead = -5; lead <= 25; lead++) {
    const Result R = run(mode, lat, gear, (int64_t)lead * 1000000LL, dly);
    runs++; lateReleased[mode] += R.released;
    if (!R.ok) { bad++; if (bad <= 10) std::printf("FAIL late drain %lld us mode %d lat %lld ms gear %.0f lead %d ms: %d notes left on, %d replayed note-ons\n", (long long)(dly / 1000), mode, (long long)(lat / 1000000), gear, lead, R.stuck, R.dup); g_fail++; }
  }
  std::printf("late drain: releases sent: no clear %llu, INT64_MAX %llu, recalling %llu\n", (unsigned long long)lateReleased[0], (unsigned long long)lateReleased[1], (unsigned long long)lateReleased[2]);
  CHECK(lateReleased[2] > 0, "a recalling sink releases a note-on that was already due when the CLEAR was drained (%llu)", (unsigned long long)lateReleased[2]);
  std::printf("%d runs, %d with a stuck or replayed note; releases sent: no clear %llu, clear returns INT64_MAX %llu, recalling %llu\n", runs, bad, (unsigned long long)releasedTotal[0], (unsigned long long)releasedTotal[1], (unsigned long long)releasedTotal[2]);
  CHECK(releasedTotal[0] > 20 && releasedTotal[0] == releasedTotal[1], "the release path was exercised and is the same for a NULL clear and INT64_MAX (%llu, %llu)", (unsigned long long)releasedTotal[0], (unsigned long long)releasedTotal[1]);
  if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
  std::printf("midiout_norecall: ok\n");
  return 0;
}
