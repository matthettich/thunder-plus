// U30: the live runner of the …waves engine (see the comment above LiveEvent in waves.h for what the page does and what this does instead; docs/port/U30-NOTES.md for the protocol).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "modfx/bfutil.h"
#include "waves/waves.h"

namespace thunder {
namespace waves {

double waves_live_pitch(double tune, double rate) { return 36 + (tune == tune ? tune : 0) + 12 * js_log2(rate > 1e-6 ? rate : 1e-6) - 60; }

static EngineOptions liveOpts(int maxVoices) { EngineOptions o; o.maxVoices = maxVoices; o.hosted = false; o.transport.tempo = 120; o.transport.lpb = 4; o.transport.ticks = 6; o.transport.playing = false; return o; }

WavesLive::WavesLive(double sr, int maxVoices, Host* host) : eng_(sr, liveOpts(maxVoices), host), sr_(sr) {
  for (int& l : lane_) l = -1;
  for (auto& p : slot_) p.store(nullptr); for (auto& p : retired_) p.store(nullptr);
  std::memset(ringL_, 0, sizeof ringL_); std::memset(ringR_, 0, sizeof ringR_);
  eng_.setOnly(1);
}
WavesLive::~WavesLive() { for (auto& p : slot_) waves_free_patch(p.exchange(nullptr)); for (auto& p : retired_) waves_free_patch(p.exchange(nullptr)); }

bool WavesLive::post(const LiveEvent& e) {
  const uint32_t t = qt_.load(std::memory_order_relaxed), h = qh_.load(std::memory_order_acquire);
  if (t - h >= (uint32_t)kLiveQueue) { st_.droppedEvents++; return false; }
  q_[t % (uint32_t)kLiveQueue] = e; qt_.store(t + 1, std::memory_order_release); return true;
}
bool WavesLive::preparePatch(Json doc, int64_t at) {
  int k = -1; for (int i = 0; i < kLiveSlots; i++) if (!slot_[i].load()) { k = i; break; }
  if (k < 0) { st_.refusedPatches++; return false; }
  PatchImpl* p = eng_.preparePatch(doc);
  if (!p) { st_.refusedPatches++; return false; }
  slot_[k].store(p);
  LiveEvent e; e.kind = LiveEvent::Patch; e.at = at; e.a = k;
  if (!post(e)) { waves_free_patch(slot_[k].exchange(nullptr)); return false; }
  return true;
}
void WavesLive::collectRetired() { for (auto& p : retired_) waves_free_patch(p.exchange(nullptr)); }

void WavesLive::drain() {
  for (;;) {
    const uint32_t h = qh_.load(std::memory_order_relaxed), t = qt_.load(std::memory_order_acquire);
    if (h == t) break;
    LiveEvent e = q_[h % (uint32_t)kLiveQueue]; qh_.store(h + 1, std::memory_order_release);
    if (e.at < pos_) { e.at = pos_; st_.lateEvents++; }
    if (npend_ >= kLivePending) { st_.droppedEvents++; continue; }
    int i = npend_; while (i > 0 && pend_[i - 1].at > e.at) { pend_[i] = pend_[i - 1]; i--; }   // stable: after every event at or before its time
    pend_[i] = e; npend_++;
  }
}
void WavesLive::apply(const LiveEvent& e) {
  VoicePool& V = eng_.voices(); const int j = e.lane < kLiveLanes ? e.lane : 0;
  switch (e.kind) {
    case LiveEvent::NoteOn: {
      const int row = lane_[j];
      const bool leg = e.legato && row >= 0 && V.rows[(size_t)row].gate != 0;
      if (leg) { V.rows[(size_t)row].pitch = e.a; break; }
      if (row >= 0) { VoiceRow& rw = V.rows[(size_t)row]; if (rw.gate != 0 && rw.lane == j) { rw.gate = 0; rw.offAt = V.now; } }
      const int k = V.on(e.a, e.b);
      if (k < 0) { lane_[j] = -1; break; }
      V.rows[(size_t)k].lane = j; lane_[j] = k;
      for (int l = 0; l < kLiveLanes; l++) if (l != j && lane_[l] == k) lane_[l] = -1;   // a stolen voice
      break;
    }
    case LiveEvent::NoteOff: {
      const int row = lane_[j];
      if (row >= 0) { VoiceRow& rw = V.rows[(size_t)row]; if (rw.lane == j && rw.gate != 0) { rw.gate = 0; rw.offAt = V.now; } }
      lane_[j] = -1; break;
    }
    case LiveEvent::Pitch: { const int row = lane_[j]; if (row >= 0) { VoiceRow& rw = V.rows[(size_t)row]; if (rw.gate != 0 && rw.lane == j) rw.pitch = e.a; } break; }
    case LiveEvent::Tempo: eng_.setTransportTempo(e.a); break;
    case LiveEvent::Level: level_ = e.a; break;
    case LiveEvent::Drive: drive_ = e.a; break;
    case LiveEvent::Knob: eng_.setKnob((int)e.a, e.b); break;   // PATCH KNOBS (S2): k1..k8
    case LiveEvent::Bus: { const int b = (int)std::min(8.0, std::max(1.0, std::floor(e.a))); bus_ = b; eng_.setOnly(b); break; }
    case LiveEvent::Patch: {
      const int k = (int)e.a; if (k < 0 || k >= kLiveSlots) break;
      int r = -1; for (int i = 0; i < kLiveSlots; i++) if (!retired_[i].load()) { r = i; break; }
      if (r < 0) { st_.droppedEvents++; break; }   // nowhere to hand the old patch to (the control thread has not collected): not swapped, never freed here
      PatchImpl* p = slot_[k].exchange(nullptr); if (!p) break;
      retired_[r].store(eng_.swapPatch(p));
      for (int l = 0; l < kLiveLanes; l++) lane_[l] = -1;
      break;
    }
  }
}
// renders the engine samples [pos_, end) (end - pos_ in 1..128) into the ring
void WavesLive::segment(int64_t end) {
  const int n = (int)(end - pos_);
  if (!eng_.process(n)) st_.failedBlocks++;
  st_.blocks++; if (n < 128) st_.cutBlocks++;
  const float* bl = eng_.bus(bus_); const float* br = eng_.busR(bus_);
  const double g = js_pow(10, (level_ == level_ ? level_ : 0) / 20), dv = drive_ == drive_ ? drive_ : 0, dr = dv > 0.001 ? 1 + dv * dv * 10 : 0, dn = dr ? 1 / js_tanh(dr) : 1;
  for (int i = 0; i < n; i++) {
    double a = (double)bl[i] * g, b = (double)br[i] * g;
    if (dr != 0) { a = js_tanh(a * dr) * dn; b = js_tanh(b * dr) * dn; }
    a = modfx::soft(a); b = modfx::soft(b);
    const size_t at = (size_t)((pos_ + i) & 511); ringL_[at] = (float)a; ringR_[at] = (float)b;
  }
  pos_ = end;
}
void WavesLive::process(float* L, float* R, int n) {
  int done = 0;
  while (done < n) {
    const int c = std::min(128, n - done); const int64_t hostEnd = hostPos_ + c;
    drain();
    for (;;) {
      while (npend_ > 0 && pend_[0].at <= pos_) { const LiveEvent e = pend_[0]; std::memmove(&pend_[0], &pend_[1], sizeof(LiveEvent) * (size_t)(npend_ - 1)); npend_--; apply(e); }
      int64_t end = pos_ + 128; if (npend_ > 0 && pend_[0].at < end) end = pend_[0].at;
      if (end > hostEnd) break;
      segment(end);
    }
    for (int j = 0; j < c; j++) {
      const int64_t e = hostPos_ + j - kLiveLatency;
      L[done + j] = e < 0 ? 0.f : ringL_[(size_t)(e & 511)]; R[done + j] = e < 0 ? 0.f : ringR_[(size_t)(e & 511)];
    }
    hostPos_ = hostEnd; done += c;
  }
}

bool WavesLive::renderOffline(double sr, int maxVoices, Host* host, const std::vector<LiveEvent>& events, const std::vector<Json>& patches, int64_t frames, std::vector<float>& L, std::vector<float>& R) {
  WavesLive w(sr, maxVoices, host); if (!w.valid()) return false;
  for (size_t i = 0; i < patches.size() && i < (size_t)kLiveSlots; i++) { PatchImpl* p = w.eng_.preparePatch(patches[i]); if (!p) return false; w.slot_[i].store(p); }
  for (const LiveEvent& ev : events) {
    LiveEvent e = ev; if (e.at < 0) e.at = 0; if (w.npend_ >= kLivePending) return false;
    int i = w.npend_; while (i > 0 && w.pend_[i - 1].at > e.at) { w.pend_[i] = w.pend_[i - 1]; i--; } w.pend_[i] = e; w.npend_++;
  }
  L.assign((size_t)frames, 0.f); R.assign((size_t)frames, 0.f);
  while (w.pos_ < frames) {
    while (w.npend_ > 0 && w.pend_[0].at <= w.pos_) { const LiveEvent e = w.pend_[0]; std::memmove(&w.pend_[0], &w.pend_[1], sizeof(LiveEvent) * (size_t)(w.npend_ - 1)); w.npend_--; w.apply(e); }
    int64_t end = w.pos_ + 128; if (w.npend_ > 0 && w.pend_[0].at < end) end = w.pend_[0].at;
    const int64_t from = w.pos_; w.segment(end);
    for (int64_t k = from; k < end && k < frames; k++) { L[(size_t)k] = w.ringL_[(size_t)(k & 511)]; R[(size_t)k] = w.ringR_[(size_t)(k & 511)]; }
  }
  return true;
}


// ---- the page's offline renders of a waves patch (index.html wavesPre / wavesLiveRender): the same engine, a fixed Math.random sequence (mulberry32 seeded from the layer), one note ----
static uint32_t renderSeed(double bus, double tuneSum) {
  const double a = (double)js_to_int32(bus) * 7919;
  return js_to_uint32((double)(js_to_int32(0x9e3779b9u) ^ js_to_int32(a) ^ js_to_int32(js_round(tuneSum * 131))));
}
std::vector<float> waves_pre_render(Json doc, const WavesPreArgs& a, double* rate) {
  const double sr = 48000; if (rate) *rate = sr;
  const double N = std::fmin(std::ceil(a.dur * sr / 128) * 128, sr * 12);
  if (!(N >= 0) || N > sr * 12 + 1) return {};
  std::vector<float> out((size_t)N, 0.f);
  const uint32_t seed = renderSeed((double)js_to_int32(a.bus), a.tune + a.vtune);
  Host host; host.setRandom(Host::Rnd::Mulberry, seed);
  EngineOptions o; o.maxVoices = 4; o.hasSeed = true; o.seed = seed; o.transport.tempo = (a.tempo == a.tempo && a.tempo != 0) ? a.tempo : 120;
  WavesEngine e(sr, o, &host); if (!e.valid() || !e.loadPatch(doc)) return {};
  const double base = 36 + a.tune + a.vtune + a.fine / 100 - 60, gateS = std::fmax(0.002, a.gate / 1000);
  const int bus = (int)std::fmin(8, std::fmax(1, (double)js_to_int32(a.bus)));
  const int k = e.voiceOn(base, 1); if (k < 0) return out;
  VoiceRow& row = e.voices().rows[(size_t)k];
  for (int off = 0; off < (int)N; off += 128) {
    const double t = off / sr;
    if (row.gate != 0) { row.pitch = base + a.pA * js_exp(-t / a.ptau); if (t >= gateS) { row.gate = 0; row.offAt = e.voices().now; } }
    e.process(128);
    const float* x = e.bus(bus); const float* y = e.busR(bus); const int n = std::min(128, (int)N - off);
    for (int i = 0; i < n; i++) out[(size_t)(off + i)] = (float)(((double)x[i] + (double)y[i]) * 0.5);
  }
  return out;
}
std::vector<float> waves_live_render(Json doc, const WavesLiveRenderArgs& a) {
  const double sr = a.sr; const bool hs = a.hold == a.hold && a.hold != 0;
  const double gateS = std::fmax(0.01, hs ? a.hold : 0.5);
  if (!(sr >= 8000 && sr <= 384000)) return std::vector<float>(1, 0.f);
  const double N = std::fmin(std::ceil((gateS + 2.5) * sr / 128) * 128, sr * 10);
  std::vector<float> out((size_t)N, 0.f); const double tune = a.tune == a.tune ? a.tune : 0;
  const uint32_t seed = renderSeed((double)js_to_int32(a.bus), tune);
  Host host; host.setRandom(Host::Rnd::Mulberry, seed);
  EngineOptions o; o.maxVoices = 4; o.hasSeed = true; o.seed = seed; o.transport.tempo = (a.bpm == a.bpm && a.bpm != 0) ? a.bpm : 120;
  WavesEngine e(sr, o, &host); if (!e.valid() || !e.loadPatch(doc)) return std::vector<float>(1, 0.f);
  int64_t last = 0;
  const int bus = (int)std::fmin(8, std::fmax(1, (double)js_to_int32(a.bus)));
  const double lv = a.level == a.level ? a.level : 0, g = js_pow(10, lv / 20), dv = a.drive == a.drive ? a.drive : 0, dr = dv > 0.001 ? 1 + dv * dv * 10 : 0, dn = dr != 0 ? 1 / js_tanh(dr) : 1;
  const double vel = (a.vel == a.vel && a.vel != 0) ? a.vel : 100;
  const int k = e.voiceOn(36 + tune - 60, std::fmax(1, std::fmin(127, vel)) / 127); if (k < 0) return std::vector<float>(1, 0.f);
  VoiceRow& row = e.voices().rows[(size_t)k];
  for (int off = 0; off < (int)N; off += 128) {
    if (row.gate != 0 && off / sr >= gateS) { row.gate = 0; row.offAt = e.voices().now; }
    e.process(128);
    const float* x = e.bus(bus); const float* y = e.busR(bus); const int n = std::min(128, (int)N - off);
    for (int i = 0; i < n; i++) {
      double v = ((double)x[i] + (double)y[i]) * 0.5 * g; if (dr != 0) v = js_tanh(v * dr) * dn;
      out[(size_t)(off + i)] = (float)v; if (v > 1e-4 || v < -1e-4) last = off + i;
    }
    if (row.gate == 0 && off - last > sr * 0.3) break;
  }
  out.resize((size_t)std::max<int64_t>(1, last + 1)); return out;
}

}  // namespace waves
}  // namespace thunder
