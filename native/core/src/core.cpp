// N0 TEST CORE. Plays a 16-step click pattern at a set tempo from its own sample clock. Thrown away in N1.
// Rules (NATIVE-DESKTOP-PLAN 4.1): thunder_process allocates nothing, takes no lock, does no I/O; commands and events
// cross threads only through the fixed-size single-producer / single-consumer rings below.
#include "thunder/core.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace {

template <class T, uint32_t N>
struct Spsc {   // N must be a power of two
  T buf[N];
  std::atomic<uint32_t> head{0}, tail{0};   // head: next write (producer), tail: next read (consumer)
  bool push(const T& v) {
    uint32_t h = head.load(std::memory_order_relaxed);
    if (h - tail.load(std::memory_order_acquire) >= N) return false;
    buf[h & (N - 1)] = v;
    head.store(h + 1, std::memory_order_release);
    return true;
  }
  bool pop(T& v) {
    uint32_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return false;
    v = buf[t & (N - 1)];
    tail.store(t + 1, std::memory_order_release);
    return true;
  }
};

enum CmdType { C_BPM = 1, C_PLAY, C_STOP, C_ECHO };
struct Cmd { int type; double v; int64_t n; };

enum EvtType { E_CLOCK = 1, E_POS, E_ECHO };
struct Evt { int type; int64_t f; int64_t wallNs; int64_t a; int sr; int outLat; int under; };

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
  // host stats (written by the host, read on the audio thread)
  std::atomic<int> out_lat{0}, under{0};
  // bulk buffers (non-audio threads only)
  PutBuf bufs[kBuffers];
};

extern "C" {

thunder_core* thunder_create(int sample_rate, int max_block) {
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

// F-REVIEW (A3): this is the audio callback's only entry into the core. Check: no allocation (the vectors are only
// read; click.data() is stable), no locks (the rings are SPSC atomics), no I/O, no unbounded loops (the voice pool is
// fixed; the step loop runs at most once per frame), steady_clock::now() is a vDSO read.
void thunder_process(thunder_core* c, float* out_l, float* out_r, int frames) {
  if (frames <= 0) return;
  const int64_t wall = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  Cmd cmd;
  while (c->cmds.pop(cmd)) {
    switch (cmd.type) {
      case C_BPM: if (cmd.v >= 20.0 && cmd.v <= 400.0) c->bpm = cmd.v; break;
      case C_PLAY: c->playing = true; c->step = 0; c->next_step = (double)c->frame; break;
      case C_STOP: c->playing = false; break;
      case C_ECHO: { Evt e{E_ECHO, c->frame, wall, cmd.n, c->sr, 0, 0}; c->evts.push(e); } break;
      default: break;
    }
  }
  const float* click = c->click.data();
  const int clickLen = (int)c->click.size();
  for (int i = 0; i < frames; i++) {
    const int64_t now = c->frame + i;
    if (c->playing && c->next_step <= (double)now) {
      const float g = kPattern[c->step];
      if (g > 0.0f) {
        for (int v = 0; v < kVoices; v++) if (!c->voices[v].on) { c->voices[v].on = true; c->voices[v].pos = 0; c->voices[v].gain = g; break; }
      }
      if ((c->step & 3) == 0) { Evt e{E_POS, now, wall, c->step, c->sr, 0, 0}; c->evts.push(e); }
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
  if (c->frame >= c->next_clock) {   // ten a second (A5's logger replaces this)
    Evt e{E_CLOCK, c->frame, wall, 0, c->sr, c->out_lat.load(std::memory_order_relaxed), c->under.load(std::memory_order_relaxed)};
    c->evts.push(e);
    c->next_clock = c->frame + c->sr / 10;
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
  *out = neg ? -v : v; return true;
}

int thunder_post(thunder_core* c, const char* s, int len) {
  if (!c || !s || len <= 0) return 0;
  const char* t = find_key(s, len, "t");
  if (!t || t >= s + len || *t != '"') return 0;
  t++;
  const char* end = s + len;
  Cmd cmd{0, 0, 0};
  auto is = [&](const char* w) { size_t n = std::strlen(w); return (size_t)(end - t) > n && std::memcmp(t, w, n) == 0 && t[n] == '"'; };
  if (is("bpm")) { cmd.type = C_BPM; const char* v = find_key(s, len, "v"); if (!v || !parse_num(v, end, &cmd.v)) return 0; }
  else if (is("play")) cmd.type = C_PLAY;
  else if (is("stop")) cmd.type = C_STOP;
  else if (is("echo")) { cmd.type = C_ECHO; const char* n = find_key(s, len, "n"); double d = 0; if (n && parse_num(n, end, &d)) cmd.n = (int64_t)d; }
  else return 0;
  return c->cmds.push(cmd) ? 1 : 0;
}

int thunder_poll(thunder_core* c, char* buf, int cap) {
  Evt e;
  if (!c || !buf || cap < 160 || !c->evts.pop(e)) return 0;
  int n = 0;
  if (e.type == E_CLOCK) n = std::snprintf(buf, (size_t)cap, "{\"t\":\"clock\",\"f\":%lld,\"wallNs\":%lld,\"sr\":%d,\"outLat\":%d,\"under\":%d}", (long long)e.f, (long long)e.wallNs, e.sr, e.outLat, e.under);
  else if (e.type == E_POS) n = std::snprintf(buf, (size_t)cap, "{\"t\":\"pos\",\"f\":%lld,\"step\":%lld}", (long long)e.f, (long long)e.a);
  else if (e.type == E_ECHO) n = std::snprintf(buf, (size_t)cap, "{\"t\":\"echo\",\"n\":%lld,\"f\":%lld}", (long long)e.a, (long long)e.f);
  return n > 0 && n < cap ? n : 0;
}

int thunder_put(thunder_core* c, int id, const char* meta, const float* data, int count) {
  if (!c || id < 0 || id >= kBuffers || !data || count < 0) return 0;
  double o = 0, n = 0;
  if (meta) { int ml = (int)std::strlen(meta); const char* po = find_key(meta, ml, "o"); const char* pn = find_key(meta, ml, "n"); if (po) parse_num(po, meta + ml, &o); if (pn) parse_num(pn, meta + ml, &n); }
  PutBuf& b = c->bufs[id];
  if (o == 0) b.data.assign(n > 0 ? (size_t)n : (size_t)count, 0.0f);
  if ((size_t)o + (size_t)count > b.data.size()) return 0;
  std::memcpy(b.data.data() + (size_t)o, data, sizeof(float) * (size_t)count);
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
