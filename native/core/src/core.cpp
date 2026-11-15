// THE CORE RUNTIME (N1 step 3, unit U05): the audio callback contract, the command and event rings, commands timed to the
// sample, and the snapshot swap. The pieces are in src/rt/ (spsc.h, snap.h, auto.h, rng.h, jsmath.h, ftz.h).
// What still makes the sound is N0's stand-in, a 16-step click pattern; the sequencer (U07) and the voices (U08) replace it,
// and until then it keeps the Android test app audible and gives the timing tests something to measure.
//
// THE CALLBACK CONTRACT (NATIVE-DESKTOP-PLAN 4.1; native/tests/rt_harness.cpp enforces the first line):
//   - thunder_process allocates nothing, frees nothing, takes no lock, does no I/O, and has no loop whose length depends on
//     anything but `frames` and fixed pool sizes.
//   - Any block length from 1 frame up: more than max_block is processed in max_block pieces.
//   - One timeline: `frame` counts every sample ever rendered. A command may carry "at" (a frame): it takes effect on exactly
//     that sample, wherever in a block it falls; without "at", at the first sample of the next block. An "at" already past
//     is applied at once.
//   - Commands and events cross threads only through fixed-size single-producer / single-consumer rings. thunder_post is
//     called from ONE control thread; thunder_poll from ONE thread (it may be another).
//   - What the callback only reads (the song, later the kit and buffers) arrives as an immutable snapshot (rt/snap.h): built
//     and freed on the control thread, swapped in at the top of a block, acknowledged with a `song` event.
//   - Denormals are flushed to zero for the length of the callback and the host's floating-point mode is restored (rt/ftz.h).
#include "thunder/core.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "rt/ftz.h"
#include "rt/snap.h"
#include "rt/spsc.h"
#include "song/song.h"

namespace {

using thunder::Spsc;

enum CmdType { C_BPM = 1, C_PLAY, C_STOP, C_ECHO };
struct Cmd { int type; double v; int64_t n; int64_t at; };   // at: the frame it takes effect on; -1: the next block's first sample

enum EvtType { E_CLOCK = 1, E_POS, E_ECHO, E_SONG };
struct Evt { int type; int64_t f; int64_t wallNs; int64_t a; int sr; int outLat; int under; uint32_t dropped = 0, late = 0; };   // dropped / late: the clock event's counters (N1 B3)

// The song as the audio thread sees it: immutable once published (song/song.h, U06: the parsed document and typed tables).
// thunder_process reads the typed tables only, never the JSON tree.
using thunder::Song;

constexpr int kPend = 64;   // timed commands waiting for their frame

constexpr int kVoices = 16, kSteps = 16, kBuffers = 8;
const float kPattern[kSteps] = {1.0f, 0, 0.4f, 0, 1.0f, 0, 0.4f, 0, 1.0f, 0, 0.4f, 0, 1.0f, 0, 0.4f, 0.3f};

struct Voice { bool on; int pos; float gain; };

struct PutBuf { std::vector<float> data; };

}  // namespace

struct thunder_core {
  int sr, max_block;
  std::vector<float> click;   // the one mono sample, generated at start-up
  Spsc<Cmd, 256> cmds;
  Spsc<Evt, 1024> evts;
  // audio-thread state
  int64_t frame = 0;
  bool playing = false;
  double bpm = 120.0, next_step = 0.0;
  int step = 0;
  Voice voices[kVoices];
  int64_t next_clock = 0;
  Cmd pend[kPend]; int npend = 0;        // timed commands, ordered by frame (equal frames: in the order posted)
  thunder::SnapSlot<Song> song;          // the song snapshot (control thread publishes, audio thread adopts)
  std::atomic<uint32_t> late_cmds{0};    // timed commands applied at once because the waiting list was full (reported in the clock event)
  uint32_t evt_dropped = 0;              // events the full event ring refused (audio thread only; reported in the clock event) (N1 B3)
  bool song_ack_pending = false; int64_t song_ack_rev = 0, song_ack_f = 0;   // a `song` event not yet in the ring: retried every piece
  // host stats (written by the host, read on the audio thread)
  std::atomic<int> out_lat{0}, under{0};
  // bulk buffers (non-audio threads only)
  PutBuf bufs[kBuffers];
};

extern "C" {

thunder_core* thunder_create(int sample_rate, int max_block) {
  // N1 review A3: refuse what the core cannot run on. A rate under 50 made the 20 ms click zero samples long and the callback read
  // click[0] of an empty vector; a rate outside 8 kHz..384 kHz is not an audio device; a block under one frame is no block.
  if (sample_rate < 8000 || sample_rate > 384000 || max_block < 1) return nullptr;
  thunder_core* c = new (std::nothrow) thunder_core();
  if (!c) return nullptr;
  c->sr = sample_rate;
  c->max_block = max_block;
  int len = sample_rate / 50;   // 20 ms click: a decaying 1.8 kHz sine
  c->click.resize((size_t)len);
  for (int i = 0; i < len; i++) {
    double t = (double)i / sample_rate;
    c->click[(size_t)i] = (float)(0.8 * std::sin(2.0 * 3.14159265358979 * 1800.0 * t) * std::exp(-t * 220.0));
  }
  for (auto& v : c->voices) v.on = false;
  return c;
}

// N1 review B3: every push into the event ring is checked. A refused event is counted (evt_dropped, in the clock event) except the
// `song` acknowledgement, which is kept and retried on the next piece until it fits: a host that waits for {"t":"song","rev":N} must
// get it. One acknowledgement covers every revision up to N: several snapshots adopted in one block give one event for the last.
static inline void push_evt(thunder_core* c, const Evt& e) { if (!c->evts.push(e)) c->evt_dropped++; }

// The audio thread's side of one command, at frame `now`.
static inline void apply_cmd(thunder_core* c, const Cmd& cmd, int64_t now, int64_t wall) {
  switch (cmd.type) {
    case C_BPM: if (cmd.v >= 20.0 && cmd.v <= 400.0) c->bpm = cmd.v; break;
    case C_PLAY: c->playing = true; c->step = 0; c->next_step = (double)now; break;
    case C_STOP: c->playing = false; break;
    case C_ECHO: { Evt e{E_ECHO, now, wall, cmd.n, c->sr, 0, 0}; push_evt(c, e); } break;
    default: break;
  }
}

// One piece of at most max_block frames. Written by the lead (U05, 15 Nov). Checked: no allocation (the vectors are only
// read; click.data() is stable; the snapshot swap moves pointers through rings), no locks (SPSC atomics), no I/O, no
// unbounded loops (the command ring, the waiting list and the voice pool are fixed; the step loop runs at most once per
// frame), steady_clock::now() is a vDSO read.
static void process_piece(thunder_core* c, float* out_l, float* out_r, int frames, int64_t wall) {
  if (c->song.adopt()) { c->song_ack_pending = true; c->song_ack_rev = c->song.get()->rev; c->song_ack_f = c->frame; }   // (a newer one replaces an unsent older: its ack covers it)
  if (c->song_ack_pending) { Evt e{E_SONG, c->song_ack_f, wall, c->song_ack_rev, c->sr, 0, 0}; if (c->evts.push(e)) c->song_ack_pending = false; }
  Cmd cmd;
  while (c->cmds.pop(cmd)) {
    if (cmd.at < 0 || cmd.at <= c->frame) { apply_cmd(c, cmd, c->frame, wall); continue; }
    if (c->npend >= kPend) { c->late_cmds.fetch_add(1, std::memory_order_relaxed); apply_cmd(c, cmd, c->frame, wall); continue; }
    int i = c->npend;   // keep the list ordered by frame; a command posted later at the same frame stays later
    while (i > 0 && c->pend[i - 1].at > cmd.at) { c->pend[i] = c->pend[i - 1]; i--; }
    c->pend[i] = cmd; c->npend++;
  }
  const float* click = c->click.data();
  const int clickLen = (int)c->click.size();
  for (int i = 0; i < frames; i++) {
    const int64_t now = c->frame + i;
    if (c->npend && c->pend[0].at <= now) {
      int k = 0;
      while (k < c->npend && c->pend[k].at <= now) apply_cmd(c, c->pend[k++], now, wall);
      for (int q = k; q < c->npend; q++) c->pend[q - k] = c->pend[q];
      c->npend -= k;
    }
    if (c->playing && c->next_step <= (double)now) {
      const float g = kPattern[c->step];
      if (g > 0.0f) {
        for (int v = 0; v < kVoices; v++) if (!c->voices[v].on) { c->voices[v].on = true; c->voices[v].pos = 0; c->voices[v].gain = g; break; }
      }
      if ((c->step & 3) == 0) { Evt e{E_POS, now, wall, c->step, c->sr, 0, 0}; push_evt(c, e); }
      c->step = (c->step + 1) & (kSteps - 1);
      c->next_step += (double)c->sr * 60.0 / c->bpm / 4.0;
    }
    float s = 0.0f;
    for (int v = 0; v < kVoices; v++) {
      Voice& vo = c->voices[v];
      if (!vo.on) continue;
      s += click[vo.pos] * vo.gain;
      if (++vo.pos >= clickLen) vo.on = false;
    }
    out_l[i] = s; out_r[i] = s;
  }
  c->frame += frames;
  if (c->frame >= c->next_clock) {   // ten a second
    Evt e{E_CLOCK, c->frame, wall, 0, c->sr, c->out_lat.load(std::memory_order_relaxed), c->under.load(std::memory_order_relaxed)};
    e.dropped = c->evt_dropped; e.late = c->late_cmds.load(std::memory_order_relaxed);
    push_evt(c, e);
    c->next_clock = c->frame + c->sr / 10;
  }
}

void thunder_process(thunder_core* c, float* out_l, float* out_r, int frames) {
  if (!c || frames <= 0) return;
  thunder::ScopedFtz ftz;
  const int64_t wall = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  const int piece = c->max_block > 0 ? c->max_block : frames;
  for (int done = 0; done < frames; ) {
    const int n = frames - done < piece ? frames - done : piece;
    process_piece(c, out_l + done, out_r + done, n, wall);
    done += n;
  }
}

static const char* find_key(const char* s, int len, const char* key) {   // finds "key": and returns the text after the colon
  int kl = (int)std::strlen(key);
  for (int i = 0; i + kl + 3 <= len; i++) {
    if (s[i] == '"' && std::memcmp(s + i + 1, key, (size_t)kl) == 0 && s[i + 1 + kl] == '"') {
      int j = i + 2 + kl;
      while (j < len && (s[j] == ' ' || s[j] == ':')) j++;
      return s + j;
    }
  }
  return nullptr;
}
static bool parse_num(const char* p, const char* end, double* out) {   // locale-free decimal
  bool neg = false; double v = 0; int digits = 0;
  if (p < end && *p == '-') { neg = true; p++; }
  while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; digits++; }
  if (p < end && *p == '.') { p++; double f = 0.1; while (p < end && *p >= '0' && *p <= '9') { v += (*p - '0') * f; f *= 0.1; p++; digits++; } }
  if (!digits) return false;
  if (p < end && (*p == 'e' || *p == 'E')) return false;   // no exponent form here: refused, not read as its mantissa
  *out = neg ? -v : v; return true;
}

int thunder_post(thunder_core* c, const char* s, int len) {
  if (!c || !s || len <= 0) return 0;
  const char* t = find_key(s, len, "t");
  if (!t || t >= s + len || *t != '"') return 0;
  t++;
  const char* end = s + len;
  Cmd cmd{0, 0, 0, -1};
  c->song.collect();   // snapshots the audio thread has let go of are freed here, on the control thread
  auto is = [&](const char* w) { size_t n = std::strlen(w); return (size_t)(end - t) > n && std::memcmp(t, w, n) == 0 && t[n] == '"'; };
  if (is("bpm")) { cmd.type = C_BPM; const char* v = find_key(s, len, "v"); if (!v || !parse_num(v, end, &cmd.v)) return 0; }
  else if (is("play")) cmd.type = C_PLAY;
  else if (is("stop")) cmd.type = C_STOP;
  else if (is("echo")) { cmd.type = C_ECHO; const char* n = find_key(s, len, "n"); double d = 0; if (n && parse_num(n, end, &d)) cmd.n = (int64_t)d; }
  else if (is("song.load")) {   // {"t":"song.load","rev":N,"S":{...}}: parsed here, on the control thread, into an immutable Song
    Song* sn = new (std::nothrow) Song(); if (!sn) return 0;
    thunder::JsonError je;
    if (!sn->load(s, (size_t)len, &je)) { delete sn; return 0; }   // malformed: nothing is queued
    if (!c->song.publish(sn)) { delete sn; return 0; }
    return 1;
  }
  else return 0;
  { const char* a = find_key(s, len, "at"); double d = 0; if (a && parse_num(a, end, &d) && d >= 0) cmd.at = (int64_t)d; }
  return c->cmds.push(cmd) ? 1 : 0;
}

int thunder_poll(thunder_core* c, char* buf, int cap) {
  Evt e;
  if (!c || !buf || cap < 256 || !c->evts.pop(e)) return 0;
  int n = 0;
  if (e.type == E_CLOCK) n = std::snprintf(buf, (size_t)cap, "{\"t\":\"clock\",\"f\":%lld,\"wallNs\":%lld,\"sr\":%d,\"outLat\":%d,\"under\":%d,\"evtDropped\":%u,\"lateCmds\":%u}", (long long)e.f, (long long)e.wallNs, e.sr, e.outLat, e.under, (unsigned)e.dropped, (unsigned)e.late);
  else if (e.type == E_POS) n = std::snprintf(buf, (size_t)cap, "{\"t\":\"pos\",\"f\":%lld,\"step\":%lld}", (long long)e.f, (long long)e.a);
  else if (e.type == E_SONG) n = std::snprintf(buf, (size_t)cap, "{\"t\":\"song\",\"rev\":%lld,\"f\":%lld}", (long long)e.a, (long long)e.f);
  else if (e.type == E_ECHO) n = std::snprintf(buf, (size_t)cap, "{\"t\":\"echo\",\"n\":%lld,\"f\":%lld}", (long long)e.a, (long long)e.f);
  return n > 0 && n < cap ? n : 0;
}

// N1 review A2: the offset and the total come from text, so they are checked as numbers before any is turned into a size: a negative
// offset wrapped (size_t)o + count back into range and wrote before the buffer; a huge total was a std::bad_alloc (an abort here).
// Accepted: whole numbers, o >= 0, 0 <= n <= kMaxPutFloats, and o + count inside the buffer.
int thunder_put(thunder_core* c, int id, const char* meta, const float* data, int count) {
  constexpr double kMaxPutFloats = 134217728.0;   // 2^27 floats (512 MB) a buffer
  if (!c || id < 0 || id >= kBuffers || !data || count < 0) return 0;
  double o = 0, n = 0;
  if (meta) { int ml = (int)std::strlen(meta); const char* po = find_key(meta, ml, "o"); const char* pn = find_key(meta, ml, "n"); if (po && !parse_num(po, meta + ml, &o)) return 0; if (pn && !parse_num(pn, meta + ml, &n)) return 0; }   // a key that is there but not a number: refused, not 0
  if (!(o >= 0 && o <= kMaxPutFloats && o == std::floor(o)) || !(n >= 0 && n <= kMaxPutFloats && n == std::floor(n))) return 0;
  PutBuf& b = c->bufs[id];
  if (o == 0) b.data.assign(n > 0 ? (size_t)n : (size_t)count, 0.0f);
  const size_t size = b.data.size(), off = (size_t)o;
  if (off > size || (size_t)count > size - off) return 0;
  if (count > 0) std::memcpy(b.data.data() + off, data, sizeof(float) * (size_t)count);
  return 1;
}

int thunder_put_info(thunder_core* c, int id, int64_t* count, double* sum) {
  if (!c || id < 0 || id >= kBuffers || c->bufs[id].data.empty()) return 0;
  double s = 0; for (float v : c->bufs[id].data) s += v;
  if (count) *count = (int64_t)c->bufs[id].data.size();
  if (sum) *sum = s;
  return 1;
}

void thunder_host_stats(thunder_core* c, int out_latency_frames, int underruns) {
  if (!c) return;
  c->out_lat.store(out_latency_frames, std::memory_order_relaxed);
  c->under.store(underruns, std::memory_order_relaxed);
}

void thunder_destroy(thunder_core* c) { delete c; }

}  // extern "C"
