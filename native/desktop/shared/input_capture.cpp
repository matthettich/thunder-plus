#include "input_capture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "host_clock.h"
#include "text_util.h"

namespace tpd {

// THE CAPTURE THREAD'S CALLBACK. A ring push and a counter: no lock, no allocation, no system call, no log (the clock is read by the OS layer before it calls).
void InputCapture::onCapture(void* user, const float* data, int frames, int channels, int64_t hostNs) {
  InputCapture* s = static_cast<InputCapture*>(user);
  const uint64_t f0 = s->frame_.load(std::memory_order_relaxed);
  if (s->startNs_.load(std::memory_order_relaxed) == 0) s->startNs_.store(hostNs, std::memory_order_relaxed);
  if (channels == (int)s->ring_.channels()) { if (!s->ring_.put(f0, data, (uint32_t)frames)) s->dropped_.fetch_add((uint64_t)frames, std::memory_order_relaxed); }
  s->frame_.store(f0 + (uint64_t)frames, std::memory_order_relaxed);
}

bool InputCapture::open(const AudioInConfig& cfg, std::string* err) {
  close();
  std::lock_guard<std::mutex> g(m_);
  auto in = factory_ ? factory_() : nullptr;
  if (!in) { if (err) *err = "no audio input available"; return false; }
  AudioInInfo inf;
  frame_.store(0); startNs_.store(0); dropped_.store(0); nextFrame_ = 0;
  // the ring is made first with a channel count the open will confirm: open() tells the channels before the stream starts, the callback is only called after start()
  std::string e;
  struct Probe { InputCapture* s; } probe{this};
  if (!in->open(cfg, &InputCapture::onCapture, this, &inf, &e)) { if (err) *err = e.empty() ? "the input did not open" : e; return false; }
  if (inf.channels <= 0 || inf.sampleRate <= 0 || !ring_.init((uint32_t)inf.channels, (size_t)inf.sampleRate)) { in->close(); if (err) *err = "could not set up the input buffer"; return false; }
  std::string se;
  if (!in->start(&se)) { in->close(); ring_.free_(); if (err) *err = se.empty() ? "the input did not start" : se; return false; }
  info_ = inf; in_ = std::move(in);
  chans_.clear(); peaks_.clear(); recording_ = false; cur_.reset();
  run_ = true;
  th_ = std::thread([this] { drain(); });
  return true;
}

void InputCapture::close() {
  std::unique_ptr<AudioIn> in;
  { std::lock_guard<std::mutex> g(m_); in = std::move(in_); }
  if (in) in->close();   // no capture callback after this
  run_ = false;
  if (th_.joinable()) th_.join();
  std::lock_guard<std::mutex> g(m_);
  ring_.free_(); recording_ = false; cur_.reset(); chans_.clear(); peaks_.clear(); info_ = AudioInInfo();
}

bool InputCapture::isOpen() const { std::lock_guard<std::mutex> g(m_); return in_ != nullptr; }
bool InputCapture::lost() const { std::lock_guard<std::mutex> g(m_); return in_ && in_->lost(); }
AudioInInfo InputCapture::info() const { std::lock_guard<std::mutex> g(m_); return info_; }
uint32_t InputCapture::availableFrames() const { std::lock_guard<std::mutex> g(m_); return in_ ? (uint32_t)ring_.framesWaiting() : 0; }
bool InputCapture::armed() const { std::lock_guard<std::mutex> g(m_); return !chans_.empty(); }
bool InputCapture::recording() const { std::lock_guard<std::mutex> g(m_); return recording_; }

bool InputCapture::arm(const std::vector<int>& chans, std::string* err) {
  std::lock_guard<std::mutex> g(m_);
  if (!in_) { if (err) *err = "no input is open (dev.open with an input first)"; return false; }
  if (chans.empty() || chans.size() > 64) { if (err) *err = "rec.arm: ins must list 1..64 channels"; return false; }
  for (int c : chans) if (c < 0 || c >= info_.channels) { if (err) *err = "rec.arm: the input has " + std::to_string(info_.channels) + " channels, not channel " + std::to_string(c); return false; }
  if (recording_) { if (err) *err = "a take is running"; return false; }
  chans_ = chans; peaks_.assign(chans.size(), 0.f);
  return true;
}

bool InputCapture::start(std::string* err) {
  std::lock_guard<std::mutex> g(m_);
  if (!in_ || chans_.empty()) { if (err) *err = "rec.start: arm first"; return false; }
  if (recording_) { if (err) *err = "a take is already running"; return false; }
  auto t = std::make_shared<Take>();
  t->id = nextId_++; t->sr = info_.sampleRate; t->channels = (int)chans_.size(); t->latencyFrames = info_.latencyFrames; t->data.assign(chans_.size(), std::vector<float>());
  cur_ = t; recording_ = true; takeFull_ = false;
  curFrame0_ = UINT64_MAX;   // the drain sets it at the first frame it sees
  return true;
}

int InputCapture::stop(std::string* err) {
  std::lock_guard<std::mutex> g(m_);
  if (!recording_ || !cur_) { if (err) *err = "rec.stop: no take is running"; return -1; }
  recording_ = false;
  const int64_t startNs = startNs_.load(std::memory_order_relaxed);
  const double lat = (double)cur_->latencyFrames;
  const uint64_t f0 = curFrame0_ == UINT64_MAX ? 0 : curFrame0_;
  cur_->hostNs0 = startNs + (int64_t)(((double)f0 - lat) * 1e9 / (info_.sampleRate > 0 ? info_.sampleRate : 48000.0));
  cur_->full = takeFull_;
  takes_[cur_->id] = cur_;
  const int id = cur_->id;
  cur_.reset();
  return id;
}

std::shared_ptr<const Take> InputCapture::take(int id) const { std::lock_guard<std::mutex> g(m_); auto it = takes_.find(id); return it == takes_.end() ? nullptr : it->second; }
void InputCapture::freeTake(int id) { std::lock_guard<std::mutex> g(m_); takes_.erase(id); }

std::vector<float> InputCapture::readPeaks() { std::lock_guard<std::mutex> g(m_); std::vector<float> p = peaks_; std::fill(peaks_.begin(), peaks_.end(), 0.f); return p; }

void InputCapture::drain() {
  std::vector<float> buf;
  while (run_.load()) {
    uint32_t got = 0; uint64_t f0 = 0; int ch = 0;
    {
      std::lock_guard<std::mutex> g(m_);
      if (!in_) break;
      ch = (int)ring_.channels();
      const size_t waiting = ring_.framesWaiting();
      if (waiting > 0) {
        const uint32_t want = (uint32_t)std::min<size_t>(waiting, 48000);
        buf.resize((size_t)want * (size_t)ch);
        got = ring_.take(buf.data(), want, &f0);
      }
      if (got > 0) {
        // a gap: the capture thread dropped callbacks between the last frame we saw and this one. A take is filled with silence for it, in place.
        if (f0 > nextFrame_ && recording_ && cur_) {
          const uint64_t gap = f0 - nextFrame_;
          if (curFrame0_ != UINT64_MAX) { for (auto& d : cur_->data) d.insert(d.end(), (size_t)gap, 0.f); cur_->frames += (int64_t)gap; cur_->gaps++; }
        }
        nextFrame_ = f0 + got;
        for (size_t k = 0; k < chans_.size(); k++) {
          const int c = chans_[k]; float pk = peaks_[k];
          for (uint32_t i = 0; i < got; i++) { const float v = std::fabs(buf[(size_t)i * (size_t)ch + (size_t)c]); if (v > pk) pk = v; }
          peaks_[k] = pk;
        }
        if (recording_ && cur_) {
          if (curFrame0_ == UINT64_MAX) curFrame0_ = f0;
          const int64_t bytes = (cur_->frames + (int64_t)got) * (int64_t)chans_.size() * 4;
          if (bytes > kMaxTakeBytes) { takeFull_ = true; }
          else {
            for (size_t k = 0; k < chans_.size(); k++) {
              auto& d = cur_->data[k]; const size_t o = d.size(); d.resize(o + got);
              const int c = chans_[k];
              for (uint32_t i = 0; i < got; i++) d[o + i] = buf[(size_t)i * (size_t)ch + (size_t)c];
            }
            cur_->frames += (int64_t)got;
          }
        }
      }
    }
    if (got == 0) std::this_thread::sleep_for(std::chrono::milliseconds(4));
    emitRec();
  }
}

// ~15 a second while armed: {t:'rec', pk:[per armed channel], frames:N (of the take so far), rec:bool, full:bool, dropped:N}
void InputCapture::emitRec() {
  if (!emit_) return;
  const int64_t now = hostNowNs();
  if (now - lastEmitNs_ < 66000000LL) return;
  std::string js;
  { std::lock_guard<std::mutex> g(m_);
    if (chans_.empty() || !in_) return;
    lastEmitNs_ = now;
    js = "{\"t\":\"rec\",\"pk\":[";
    for (size_t k = 0; k < peaks_.size(); k++) { char b[32]; std::snprintf(b, sizeof b, "%s%.4f", k ? "," : "", (double)peaks_[k]); js += b; peaks_[k] = 0.f; }
    js += std::string("],\"frames\":") + std::to_string(cur_ ? (long long)cur_->frames : 0LL) + ",\"rec\":" + (recording_ ? "true" : "false") + ",\"full\":" + (takeFull_ ? "true" : "false") + ",\"dropped\":" + std::to_string((unsigned long long)dropped_.load()) + "}";
  }
  emit_(js);
}

}  // namespace tpd
