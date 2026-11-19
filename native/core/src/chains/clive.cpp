// U31: the live runner of one …chains track node (see the comment above ChainsEvent in chains.h for what the worklet does and what this does instead; docs/port/U31-NOTES.md for the protocol).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "chains/chains.h"
#include "waves/graph.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

namespace thunder {
namespace chains {

static inline void bump(std::atomic<uint64_t>& c) { c.fetch_add(1, std::memory_order_relaxed); }   // (every counter of ChainsLive: relaxed atomics, written by whichever thread counts, readable from any)
ChainsLiveStats ChainsLive::stats() const {
  ChainsLiveStats s; auto g = [](const std::atomic<uint64_t>& c) { return c.load(std::memory_order_relaxed); };
  s.blocks = g(ctr_.blocks); s.cutBlocks = g(ctr_.cutBlocks); s.lateEvents = g(ctr_.lateEvents); s.droppedEvents = g(ctr_.droppedEvents); s.refusedPatches = g(ctr_.refusedPatches); s.failedBlocks = g(ctr_.failedBlocks);
  s.coalescedEvents = g(ctr_.coalescedEvents); s.deferredPatches = g(ctr_.deferredPatches); s.droppedParams = g(ctr_.droppedParams); return s;
}
// debug builds: the control calls come from one thread (the first that calls); a second one is a host bug and stops here with a message
void ChainsLive::ctlCheck() {
#ifndef NDEBUG
  const size_t me = std::hash<std::thread::id>()(std::this_thread::get_id()) | 1; size_t none = 0;
  if (!ctlThread_.compare_exchange_strong(none, me) && none != me) { std::fprintf(stderr, "ChainsLive: post / postParam / preparePatch / service / loadPatch called from two threads (single control thread required)\n"); std::abort(); }
#endif
}

static EngineOptions liveOpts() { EngineOptions o; o.transport.tempo = 120; o.transport.lpb = 4; o.transport.ticks = 6; o.transport.playing = false; return o; }

ChainsLive::ChainsLive(double sr, int track, const ChainsLiveConfig& cfg, Host* host) : node_(sr, track, liveOpts(), host), sr_(sr), cfg_(cfg) {
  cfg_.taps = std::max(0, std::min(cfg_.taps, kMaxTaps)); cfg_.feeds = std::max(0, std::min(cfg_.feeds, kMaxFeeds / 2));
  for (auto& p : slot_) p.store(nullptr); for (auto& p : retired_) p.store(nullptr);
  for (double& b : byp_) b = -1;
  node_.engine().setCarryFade(true);   // a Bypass crossfade runs its 128 samples across the cuts the events make
  node_.engine().setInlineService(false);   // the control thread services the spares (service()); the audio thread never makes a pump instance
  const size_t ni = (size_t)nIn();
  inRing_.reset(new float[ni * kChainsRing]()); outRing_.reset(new float[10 * kChainsRing]()); seg_.reset(new float[ni * kBlock]()); oseg_.reset(new float[10 * kBlock]());
  ptrIn_.assign(ni, nullptr); ptrOut_.assign(10, nullptr);
}
ChainsLive::~ChainsLive() { for (auto& p : slot_) chains_free_patch(p.exchange(nullptr)); for (auto& p : retired_) chains_free_patch(p.exchange(nullptr)); }

bool ChainsLive::post(const ChainsEvent& e) {
  ctlCheck();
  if (e.kind == ChainsEvent::Param && (std::memchr(e.id, 0, sizeof e.id) == nullptr || std::memchr(e.name, 0, sizeof e.name) == nullptr)) { bump(ctr_.droppedEvents); return false; }
  const uint32_t t = qt_.load(std::memory_order_relaxed), h = qh_.load(std::memory_order_acquire);
  // the last kChainsReserve places are for events that must never be lost (Playing, Bypass, Tempo, Patch): a Param is refused first, so the others still find room
  if (t - h >= (uint32_t)(kChainsQueue - (e.kind == ChainsEvent::Param ? kChainsReserve : 0))) { bump(ctr_.droppedEvents); return false; }
  q_[t % (uint32_t)kChainsQueue] = e; qt_.store(t + 1, std::memory_order_release); return true;
}
bool ChainsLive::postParam(const char* id, const char* name, double v, int64_t at) {
  ChainsEvent e; e.kind = ChainsEvent::Param; e.at = at; e.a = v;
  if (std::strlen(id) >= sizeof e.id || std::strlen(name) >= sizeof e.name) { bump(ctr_.droppedEvents); return false; }
  std::strcpy(e.id, id); std::strcpy(e.name, name); return post(e);
}
bool ChainsLive::preparePatch(Json doc, int64_t at) {
  ctlCheck();
  // a patch in flight is one prepared and not yet swapped in (slot_) or swapped out and not yet collected (retired_). Each swap (or dropped Patch event) puts one patch into a retired_ place, so the places must cover
  // R (retired now) + S (waiting in slot_) + 1 (the one the audio thread holds between `slot_[k].exchange(nullptr)` and `retired_[r].store(...)`, which neither count sees) + 1 (the patch being prepared) <= kChainsSlots.
  // The count below is R + S, so a patch is refused from kChainsSlots - 2 on (service() collects the retired ones). With that bound the audio thread always finds a retired place and a dropped Patch always has one.
  int k = -1, inflight = 0; for (int i = 0; i < kChainsSlots; i++) { if (!slot_[i].load()) { if (k < 0) k = i; } else inflight++; if (retired_[i].load()) inflight++; }
  if (k < 0 || inflight >= kChainsSlots - 2) { bump(ctr_.refusedPatches); return false; }
  waves::PatchImpl* p = node_.engine().preparePatch(doc);
  if (!p) { bump(ctr_.refusedPatches); return false; }
  slot_[k].store(p);
  ChainsEvent e; e.kind = ChainsEvent::Patch; e.at = at; e.a = k;
  if (!post(e)) { chains_free_patch(slot_[k].exchange(nullptr)); return false; }
  return true;
}
void ChainsLive::service() {
  ctlCheck();
  for (auto& p : retired_) chains_free_patch(p.exchange(nullptr));
  node_.engine().serviceSparesOf(node_.engine().currentPatch());   // (the engine sets it whenever the patch changes: no fallback to its unique_ptr, which the audio thread may be replacing)
}

void ChainsLive::drain() {
  for (;;) {
    const uint32_t h = qh_.load(std::memory_order_relaxed), t = qt_.load(std::memory_order_acquire);
    if (h == t) break;
    ChainsEvent e = q_[h % (uint32_t)kChainsQueue]; qh_.store(h + 1, std::memory_order_release);
    if (e.at < pos_) { e.at = pos_; bump(ctr_.lateEvents); }
    // the pending array is full or nearly so: parameter events coalesce (a later value of the same module and parameter replaces the earlier one); an event that must not be lost (Playing, Bypass,
    // Tempo, Patch) makes room by removing the oldest parameter event; a parameter event is refused first (the last kChainsReserve places are for the others); only when nothing can give way is an event dropped,
    // and a dropped Patch hands its patch to the control thread (retired_) so its slot is free again
    int ins = npend_; while (ins > 0 && pend_[ins - 1].at > e.at) ins--;   // where it sorts in: after every event at or before its time
    if (e.kind == ChainsEvent::Param) {
      if (npend_ >= kChainsPending - kChainsReserve) {
        int same = -1; for (int j = 0; j < npend_; j++) if (pend_[j].kind == ChainsEvent::Param && std::strcmp(pend_[j].id, e.id) == 0 && std::strcmp(pend_[j].name, e.name) == 0) same = j;
        if (same < 0) { bump(ctr_.droppedEvents); continue; }
        eraseAt(same); bump(ctr_.coalescedEvents); if (same < ins) ins--;
      }
    } else if (npend_ >= kChainsPending) {
      const int old = victim(e);
      if (old < 0) { bump(ctr_.droppedEvents); if (e.kind == ChainsEvent::Patch) dropPatch(e); continue; }
      if (pend_[old].kind == ChainsEvent::Param) bump(ctr_.droppedParams); else bump(ctr_.coalescedEvents);
      eraseAt(old); if (old < ins) ins--;
    }
    std::memmove(&pend_[ins + 1], &pend_[ins], sizeof(ChainsEvent) * (size_t)(npend_ - ins)); pend_[ins] = e; npend_++;   // stable
  }
}
// What may give way when the array is full, for the incoming event e. The incoming event is the LATER one (posted after everything pending), so an earlier pending event of its own kind (a Tempo; a Bypass of the same
// switch; a Playing) gives way to it: the last state posted wins, a stop always lands. The cost is the intermediate state of the removed one (it was pending for a moment later than now). Order: the oldest Param (a loss, counted
// as droppedParams), else the earliest pending event of e's own kind, else a pending Tempo / Bypass / Playing a later pending one of its kind supersedes; -1: nothing can give way.
int ChainsLive::victim(const ChainsEvent& e) const {
  auto same = [](const ChainsEvent& x, const ChainsEvent& y) { return x.kind == y.kind && (x.kind == ChainsEvent::Tempo || x.kind == ChainsEvent::Playing || (x.kind == ChainsEvent::Bypass && x.a == y.a)); };
  for (int j = 0; j < npend_; j++) if (pend_[j].kind == ChainsEvent::Param) return j;
  for (int j = 0; j < npend_; j++) if (same(pend_[j], e)) return j;
  for (int j = 0; j < npend_; j++) {
    if (pend_[j].kind != ChainsEvent::Tempo && pend_[j].kind != ChainsEvent::Bypass && pend_[j].kind != ChainsEvent::Playing) continue;
    for (int k = j + 1; k < npend_; k++) if (same(pend_[j], pend_[k])) return j;
  }
  return -1;
}
void ChainsLive::eraseAt(int j) { std::memmove(&pend_[j], &pend_[j + 1], sizeof(ChainsEvent) * (size_t)(npend_ - j - 1)); npend_--; }
// a Patch event that cannot be delivered: its patch goes to the control thread (retired_; preparePatch kept a place for it) and its slot is free again
void ChainsLive::dropPatch(const ChainsEvent& e) {
  const int k = (int)e.a; if (k < 0 || k >= kChainsSlots) return;
  waves::PatchImpl* p = slot_[k].exchange(nullptr); if (!p) return;
  for (int i = 0; i < kChainsSlots; i++) if (!retired_[i].load()) { retired_[i].store(p); return; }
  slot_[k].store(p);   // (cannot happen: preparePatch keeps R + S <= kChainsSlots - 2, see there) put back, never freed here
}
void ChainsLive::apply(const ChainsEvent& e) {
  ChainsEngine& E = node_.engine();
  switch (e.kind) {
    case ChainsEvent::Param: E.setParam(std::string_view(e.id), std::string_view(e.name), e.a); break;
    case ChainsEvent::Tempo: E.setTransportTempo(e.a); break;
    case ChainsEvent::Playing: { Transport t = E.transport(); t.playing = e.a != 0; E.setTransport(t); break; }
    case ChainsEvent::Bypass: { const int n = (int)e.a; if (n >= 1 && n <= kBypSwitches) byp_[n - 1] = e.b; break; }
    case ChainsEvent::Patch: {
      const int k = (int)e.a; if (k < 0 || k >= kChainsSlots) break;
      int r = -1; for (int i = 0; i < kChainsSlots; i++) if (!retired_[i].load()) { r = i; break; }
      if (r < 0) { bump(ctr_.deferredPatches); ChainsEvent again = e; again.at = pos_ + kBlock; int i = npend_; while (i > 0 && pend_[i - 1].at > again.at) { pend_[i] = pend_[i - 1]; i--; } pend_[i] = again; npend_++; break; }   // (cannot happen with preparePatch's bound; kept as a safe fallback) tried again a block later
      waves::PatchImpl* p = slot_[k].exchange(nullptr); if (!p) break;
      retired_[r].store(E.swapPatch(p));
      break;
    }
  }
}
// renders the engine samples [pos_, end) (end - pos_ in 1..128): the inputs from the ring, the outputs into the ring
void ChainsLive::segment(int64_t end) {
  const int n = (int)(end - pos_); const int ni = nIn();
  for (int c = 0; c < ni; c++) { float* d = seg_.get() + (size_t)c * kBlock; const float* r = inRing_.get() + (size_t)c * kChainsRing; for (int i = 0; i < n; i++) d[i] = r[(size_t)((pos_ + i) & (kChainsRing - 1))]; ptrIn_[(size_t)c] = d; }
  NodeIn in; int c = 0; in.inL = ptrIn_[(size_t)c++]; in.inR = ptrIn_[(size_t)c++];
  if (cfg_.sc) { in.scL = ptrIn_[(size_t)c++]; in.scR = ptrIn_[(size_t)c++]; }
  in.taps = cfg_.taps ? &ptrIn_[(size_t)c] : nullptr; in.nTaps = cfg_.taps; c += cfg_.taps;
  in.sends = cfg_.sends ? &ptrIn_[(size_t)c] : nullptr; in.nSends = cfg_.sends ? 8 : 0; if (cfg_.sends) c += 8;
  in.feeds = cfg_.feeds ? &ptrIn_[(size_t)c] : nullptr; in.nFeeds = cfg_.feeds; in.byp = byp_;
  for (int k = 0; k < 10; k++) ptrOut_[(size_t)k] = oseg_.get() + (size_t)k * kBlock;
  NodeOut out; out.L = ptrOut_[0]; out.R = ptrOut_[1]; out.sends = &ptrOut_[2]; out.nSends = 8;
  ChainsEngine& E = node_.engine(); const uint64_t f0 = E.stats().failedBlocks;
  node_.process(in, out, n);
  if (E.stats().failedBlocks != f0) bump(ctr_.failedBlocks);
  bump(ctr_.blocks); if (n < kBlock) bump(ctr_.cutBlocks);
  for (int k = 0; k < 10; k++) { const float* s = oseg_.get() + (size_t)k * kBlock; float* r = outRing_.get() + (size_t)k * kChainsRing; for (int i = 0; i < n; i++) r[(size_t)((pos_ + i) & (kChainsRing - 1))] = s[i]; }
  pos_ = end; posPub_.store(end, std::memory_order_relaxed);
}
void ChainsLive::process(const NodeIn& in, NodeOut& out, int n) {
  int done = 0; const int ni = nIn();
  while (done < n) {
    const int c = std::min(kBlock, n - done); const int64_t hostEnd = hostPos_ + c;
    // the host's chunk into the input ring (an absent input: silence)
    { int k = 0;
      auto put = [&](int ch, const float* p) { float* r = inRing_.get() + (size_t)ch * kChainsRing; for (int i = 0; i < c; i++) r[(size_t)((hostPos_ + i) & (kChainsRing - 1))] = p ? p[done + i] : 0.f; };
      put(k++, in.inL); put(k++, in.inR ? in.inR : in.inL);
      if (cfg_.sc) { put(k++, in.scL); put(k++, in.scR ? in.scR : in.scL); }
      for (int j = 0; j < cfg_.taps; j++) put(k++, (in.taps && j < in.nTaps) ? in.taps[j] : nullptr);
      if (cfg_.sends) for (int j = 0; j < 8; j++) put(k++, (in.sends && j < in.nSends) ? in.sends[j] : nullptr);
      for (int j = 0; j < cfg_.feeds; j++) { const float* a = (in.feeds && j < in.nFeeds) ? in.feeds[2 * j] : nullptr; const float* b = (in.feeds && j < in.nFeeds) ? in.feeds[2 * j + 1] : nullptr; put(k++, a); put(k++, b ? b : a); }
      (void)ni; }
    drain();
    for (;;) {
      while (npend_ > 0 && pend_[0].at <= pos_) { const ChainsEvent e = pend_[0]; std::memmove(&pend_[0], &pend_[1], sizeof(ChainsEvent) * (size_t)(npend_ - 1)); npend_--; apply(e); }
      int64_t end = pos_ + kBlock; if (npend_ > 0 && pend_[0].at < end) end = pend_[0].at;
      if (end > hostEnd) break;
      segment(end);
    }
    for (int j = 0; j < c; j++) {
      const int64_t e = hostPos_ + j - kChainsLatency;
      auto get = [&](int ch) { return e < 0 ? 0.f : outRing_[(size_t)ch * kChainsRing + (size_t)(e & (kChainsRing - 1))]; };
      if (out.L) out.L[done + j] = get(0);
      if (out.R && out.R != out.L) out.R[done + j] = get(1);
      if (out.sends) for (int k = 0; k < out.nSends && k < 8; k++) if (out.sends[k]) out.sends[k][done + j] = get(2 + k);
    }
    hostPos_ = hostEnd; done += c;
  }
}

// ---- offline: the same loop, no queue, no latency ----
struct ChainsLiveOffline {
  static bool run(double sr, int track, const ChainsLiveConfig& cfg, Host* host, const std::vector<ChainsEvent>& events, const std::vector<Json>& patches, const std::vector<std::vector<float>>& inputs,
                  int64_t frames, std::vector<std::vector<float>>& outputs, Json initialPatch, const std::vector<int>* busMap, const std::vector<int>* tapMap) {
    ChainsLive w(sr, track, cfg, host); if (!w.valid()) return false;
    if (initialPatch.isObj() && !w.node_.engine().loadPatch(initialPatch)) return false;
    if (busMap) w.node_.engine().setBusMap(busMap->data(), (int)busMap->size());
    if (tapMap) w.node_.engine().setTapMap(tapMap->data(), (int)tapMap->size());
    for (size_t i = 0; i < patches.size() && i < (size_t)kChainsSlots; i++) { waves::PatchImpl* p = w.node_.engine().preparePatch(patches[i]); if (!p) return false; w.slot_[i].store(p); }
    for (const ChainsEvent& ev : events) {
      ChainsEvent e = ev; if (e.at < 0) e.at = 0; if (w.npend_ >= kChainsPending) return false;
      int i = w.npend_; while (i > 0 && w.pend_[i - 1].at > e.at) { w.pend_[i] = w.pend_[i - 1]; i--; } w.pend_[i] = e; w.npend_++;
    }
    outputs.assign(10, std::vector<float>((size_t)frames, 0.f));
    const int ni = w.nIn(); if ((int)inputs.size() < ni) return false;
    while (w.pos_ < frames) {
      while (w.npend_ > 0 && w.pend_[0].at <= w.pos_) { const ChainsEvent e = w.pend_[0]; std::memmove(&w.pend_[0], &w.pend_[1], sizeof(ChainsEvent) * (size_t)(w.npend_ - 1)); w.npend_--; w.apply(e); }
      int64_t end = w.pos_ + kBlock; if (w.npend_ > 0 && w.pend_[0].at < end) end = w.pend_[0].at;
      if (end > frames) end = frames;   // (the last segment may be short: the live runner never renders past what the host has given)
      for (int c = 0; c < ni; c++) for (int64_t k = w.pos_; k < end; k++) w.inRing_[(size_t)c * kChainsRing + (size_t)(k & (kChainsRing - 1))] = k < (int64_t)inputs[(size_t)c].size() ? inputs[(size_t)c][(size_t)k] : 0.f;
      const int64_t from = w.pos_; w.segment(end);
      for (int c = 0; c < 10; c++) for (int64_t k = from; k < end; k++) outputs[(size_t)c][(size_t)k] = w.outRing_[(size_t)c * kChainsRing + (size_t)(k & (kChainsRing - 1))];
    }
    return true;
  }
};
bool ChainsLive::renderOffline(double sr, int track, const ChainsLiveConfig& cfg, Host* host, const std::vector<ChainsEvent>& events, const std::vector<Json>& patches, const std::vector<std::vector<float>>& inputs,
                               int64_t frames, std::vector<std::vector<float>>& outputs, Json initialPatch, const std::vector<int>* busMap, const std::vector<int>* tapMap) {
  return ChainsLiveOffline::run(sr, track, cfg, host, events, patches, inputs, frames, outputs, initialPatch, busMap, tapMap);
}

}  // namespace chains
}  // namespace thunder
