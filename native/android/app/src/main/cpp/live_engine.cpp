// N2 H1a: LiveEngine (live_engine.h). Read the header first.
#include "live_engine.h"

#include <time.h>

#include <chrono>
#include <cstdio>
#include <cstring>

#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace tpa {

void setFlushToZero() {
#if defined(__aarch64__)
  uint64_t fpcr; asm volatile("mrs %0, fpcr" : "=r"(fpcr)); fpcr |= (1ull << 24); asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__x86_64__)
  _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
}

int64_t LiveEngine::monoNs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }

bool LiveEngine::create(const Config& c, std::string* err) {
  if (L_) { if (err) *err = "already created"; return false; }
  cfg_ = c; chainsOn_ = false;
  thunder_live_opts o; std::memset(&o, 0, sizeof o);
  o.size = (uint32_t)sizeof o;
  o.sample_rate = c.sampleRate;
  o.max_pad_bytes = c.padBytes;
  o.max_block = c.maxBlock;
  o.flags = THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER | THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_MIDI;
#ifdef THUNDER_LIVE_F_CHAINS
  if (c.chains) { o.flags |= THUNDER_LIVE_F_CHAINS; o.chains_bytes = c.chainsBytes; chainsOn_ = true; }
#endif
  char e[256] = {0};
  L_ = thunder_live_create(&o, e, sizeof e);
  if (!L_) { if (err) *err = e[0] ? e : "thunder_live_create failed"; return false; }
  l_.assign((size_t)c.maxBlock, 0.f); r_.assign((size_t)c.maxBlock, 0.f);
  frame0_ = 0; calls_.store(0);
  { std::lock_guard<std::mutex> g(sm_); st_ = Status(); }
  return true;
}

void LiveEngine::startThreads() {
  if (!L_ || run_.exchange(true)) return;
  control_ = std::thread([this] { controlLoop(); });
  poller_ = std::thread([this] { pollLoop(); });
}

void LiveEngine::stopThreads() {
  if (!run_.exchange(false)) return;
  { std::lock_guard<std::mutex> g(qm_); }
  qcv_.notify_all();
  if (control_.joinable()) control_.join();
  if (poller_.joinable()) poller_.join();
  std::lock_guard<std::mutex> g(qm_);
  for (auto& t : q_) { std::lock_guard<std::mutex> g2(t->m); t->done = true; t->rc = -1000; t->cv.notify_all(); }
  q_.clear();
}

void LiveEngine::destroy() {
  stopThreads();
  std::unique_lock<std::shared_mutex> w(lifeM_);   // waits for every any-thread reader (statsJson, engineOffset) that is inside the host; later ones see L_ == nullptr
  if (L_) { thunder_live_destroy(L_); L_ = nullptr; }
  l_.clear(); l_.shrink_to_fit(); r_.clear(); r_.shrink_to_fit();
}

// THE AUDIO CALLBACK BODY. Only: flush-to-zero, thunder_live_process in pieces, interleave. The scratch is the engine's, made at create.
void LiveEngine::render(float* out, int frames, int64_t wallNs) {
  setFlushToZero();
  if (!L_ || l_.empty()) { std::memset(out, 0, sizeof(float) * 2 * (size_t)frames); return; }
  float* const l = l_.data(); float* const r = r_.data();
  const int mb = cfg_.maxBlock;
  int done = 0;
  while (done < frames) {
    int n = frames - done; if (n > mb) n = mb;
    const int rc = thunder_live_process(L_, l, r, n, frame0_, done == 0 ? wallNs : 0);   // the outputs are written even on an error (zeros)
    (void)rc;
    float* o = out + 2 * (size_t)done;
    for (int i = 0; i < n; i++) { o[2 * i] = l[i]; o[2 * i + 1] = r[i]; }
    frame0_ += n; done += n;
  }
  // the shell's gain: a linear ramp to the target over 150 ms, per call (no allocation, no lock)
  const float tgt = duckTarget_.load(std::memory_order_relaxed);
  if (duckCur_ != tgt || duckCur_ != 1.f) {
    const float step = (float)(1.0 / (0.15 * cfg_.sampleRate));
    float g = duckCur_;
    for (int i = 0; i < frames; i++) {
      if (g < tgt) { g += step; if (g > tgt) g = tgt; } else if (g > tgt) { g -= step; if (g < tgt) g = tgt; }
      out[2 * (size_t)i] *= g; out[2 * (size_t)i + 1] *= g;
    }
    duckCur_ = g;
  }
  calls_.fetch_add(1, std::memory_order_release);
}

int LiveEngine::call(std::function<int(thunder_live*)> fn, int timeoutMs) {
  if (!run_.load()) return -1000;
  auto t = std::make_shared<Task>();
  t->fn = std::move(fn);
  t->deadlineNs = monoNs() + (int64_t)timeoutMs * 1000000LL;
  { std::lock_guard<std::mutex> g(qm_); q_.push_back(t); }
  qcv_.notify_all();
  std::unique_lock<std::mutex> lk(t->m);
  if (!t->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs + 500), [&] { return t->done; })) { t->abandoned = true; return -1000; }
  return t->rc;
}

void LiveEngine::controlLoop() {
  while (run_.load()) {
    std::shared_ptr<Task> t;
    {
      std::unique_lock<std::mutex> lk(qm_);
      if (q_.empty()) qcv_.wait_for(lk, std::chrono::milliseconds(5));
      if (!q_.empty()) { t = q_.front(); q_.pop_front(); }
    }
    if (!t) { thunder_live_collect(L_); continue; }
    int rc = -1000;
    bool skip; { std::lock_guard<std::mutex> g(t->m); skip = t->abandoned; }
    if (!skip) {
      for (;;) {
        rc = t->fn(L_);
        if (rc != THUNDER_LIVE_E_BUSY || !run_.load() || monoNs() > t->deadlineNs) break;
        // E_BUSY: nothing changed, the caller owns the retry, and only a process call empties a ring: wait for the next callback (at most 20 ms), collect, retry.
        const int64_t c0 = calls_.load(std::memory_order_acquire);
        for (int i = 0; i < 20 && calls_.load(std::memory_order_acquire) == c0 && run_.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        thunder_live_collect(L_);
      }
    }
    { std::lock_guard<std::mutex> g(t->m); t->rc = rc; t->done = true; }
    t->cv.notify_all();
  }
}

void LiveEngine::pollLoop() {
  alignas(8) unsigned char buf[512];
  int64_t lastHs = 0;
  while (run_.load()) {
    bool any = false;
    for (;;) {
      const int n = thunder_live_poll(L_, buf, (int)sizeof buf);
      if (n <= 0) break;
      any = true;
      std::lock_guard<std::mutex> g(sm_);
      int off = 0;
      while (off + (int)sizeof(thunder_live_ev_head) <= n) {
        thunder_live_ev_head h; std::memcpy(&h, buf + off, sizeof h);
        if (h.bytes < sizeof h || off + h.bytes > n) break;
        st_.records++;
        if (st_.lastSeq && h.seq != st_.lastSeq + 1) st_.gaps++;
        st_.lastSeq = h.seq;
        const unsigned char* p = buf + off;
        { std::string js; if (eventToJson(p, h.bytes, &js)) events_.push(std::move(js), eventIsCritical(h.type)); }
        switch (h.type) {
          case THUNDER_LIVE_EV_CLOCK: { thunder_live_ev_clock c; std::memcpy(&c, p, sizeof c); st_.clockF = c.h.f; st_.voices = c.voices; st_.under = c.under; st_.outLat = c.out_lat; break; }
          case THUNDER_LIVE_EV_POS: { thunder_live_ev_pos q; std::memcpy(&q, p, sizeof q); st_.row = q.row; st_.step = q.step; st_.bpm = q.bpm10 / 10.0; break; }
          case THUNDER_LIVE_EV_STATE: { thunder_live_ev_state s; std::memcpy(&s, p, sizeof s); st_.state = stateOfRecord(s.state); break; }
          case THUNDER_LIVE_EV_HELD: st_.state = THUNDER_LIVE_STATE_HELD; break;
          case THUNDER_LIVE_EV_SONG: { thunder_live_ev_song s; std::memcpy(&s, p, sizeof s); st_.songRev = s.rev; break; }
          case THUNDER_LIVE_EV_PADS: { thunder_live_ev_pads s; std::memcpy(&s, p, sizeof s); st_.padsId = s.id; break; }
          case THUNDER_LIVE_EV_METERS: { thunder_live_ev_meters m; std::memcpy(&m, p, sizeof m); st_.masterPk[0] = m.a[66]; st_.masterPk[1] = m.a[68]; break; }
          case THUNDER_LIVE_EV_ERROR: {
            thunder_live_ev_error e; std::memcpy(&e, p, sizeof e);
            st_.errors++; if (e.fatal) st_.fatal++;
            char b[96]; std::snprintf(b, sizeof b, "where %d code %d fatal %d value %d", e.where, e.code, e.fatal, e.value); st_.lastError = b; break; }
          case THUNDER_LIVE_EV_LATE: { thunder_live_ev_late l; std::memcpy(&l, p, sizeof l); st_.notesLate = l.notes_late; st_.runnerSkips = l.runner_skips; break; }
          default: break;   // ROWS, HELD: a reader skips what it does not use, by `bytes`
        }
        off += h.bytes;
      }
    }
    const int64_t now = monoNs();
    if (now - lastHs > 100000000LL) { lastHs = now; thunder_live_host_stats(L_, outLat_.load(std::memory_order_relaxed), under_.load(std::memory_order_relaxed)); }
    if (!any) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

std::string LiveEngine::statusJson() {
  std::lock_guard<std::mutex> g(sm_);
  char b[512];
  std::snprintf(b, sizeof b,
    "{\"state\":%d,\"row\":%d,\"step\":%d,\"bpm\":%.1f,\"clockF\":%lld,\"voices\":%d,\"under\":%u,\"outLat\":%d,\"songRev\":%lld,\"padsId\":%lld,\"records\":%llu,\"gaps\":%llu,"
    "\"pkL\":%.4f,\"pkR\":%.4f,\"errors\":%d,\"fatal\":%d,\"lastError\":\"%s\",\"notesLate\":%u,\"runnerSkips\":%u}",
    st_.state, st_.row, st_.step, st_.bpm, (long long)st_.clockF, st_.voices, st_.under, st_.outLat, (long long)st_.songRev, (long long)st_.padsId,
    (unsigned long long)st_.records, (unsigned long long)st_.gaps, (double)st_.masterPk[0], (double)st_.masterPk[1], st_.errors, st_.fatal, st_.lastError.c_str(), st_.notesLate, st_.runnerSkips);
  return b;
}

int64_t LiveEngine::engineOffset() {
  std::shared_lock<std::shared_mutex> rd(lifeM_);
  int64_t o = 0;
  if (L_) thunder_live_engine_offset(L_, &o);
  return o;
}

std::string LiveEngine::statsJson() {
  std::shared_lock<std::shared_mutex> rd(lifeM_);   // destroy() cannot free the host while this runs
  if (!L_) return "{}";
  thunder_live_stats s; std::memset(&s, 0, sizeof s); s.size = (uint32_t)sizeof s;
  if (thunder_live_stats_get(L_, &s) != 0) return "{}";
  char b[640];
  std::snprintf(b, sizeof b,
    "{\"state\":%d,\"frames\":%lld,\"quanta\":%llu,\"calls\":%llu,\"steps\":%llu,\"seqDropped\":%llu,\"runnerSkips\":%llu,\"padBytes\":%llu,\"padBudget\":%llu,\"latencyFrames\":%d,\"voices\":%d,\"peakVoices\":%d,"
    "\"notesPlayed\":%d,\"notesLate\":%d,\"notesNoPad\":%d,\"notesVarMissing\":%d,\"cmdBusy\":%u,\"cmdLate\":%u,\"evtDropped\":%u}",
    s.state, (long long)s.frames, (unsigned long long)s.quanta, (unsigned long long)s.calls, (unsigned long long)s.steps, (unsigned long long)s.seq_dropped, (unsigned long long)s.runner_skips,
    (unsigned long long)s.pad_bytes_held, (unsigned long long)s.pad_budget, s.latency_frames, s.voices, s.peak_voices, s.notes_played, s.notes_late, s.notes_no_pad, s.notes_var_missing, s.cmd_busy, s.cmd_late, s.evt_dropped);
  return b;
}

}  // namespace tpa
