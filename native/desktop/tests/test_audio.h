// A hand-driven AudioOut for tests: nothing runs until tick().
#ifndef TPD_TEST_AUDIO_H
#define TPD_TEST_AUDIO_H
#include <chrono>
#include "live_host.h"
class ManualOut : public tpd::AudioOut {
 public:
  explicit ManualOut(double sr = 48000) : sr_(sr) {}
  bool open(Callback cb, void* user, double* sr, std::string*) override { cb_ = cb; user_ = user; *sr = sr_; open_ = true; return true; }
  bool start(std::string*) override { started_ = true; return true; }
  void close() override { started_ = false; open_ = false; }
  int state() const override { return started_ ? 3 : 0; }
  // one callback of `frames` frames; returns the interleaved block
  std::vector<float> tick(int frames) {
    std::vector<float> b((size_t)frames * 2, 0.f);
    if (cb_ && started_) { const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); cb_(user_, b.data(), frames, ns); }
    return b;
  }
  bool started_ = false, open_ = false;
 private:
  double sr_; Callback cb_ = nullptr; void* user_ = nullptr;
};

// H7: a hand-driven output that records its configuration, can refuse a device id, report latency / period and be "lost" (as the OS does when a device is unplugged).
#include <atomic>
#include <mutex>
#include <set>
struct ConfigCtl {
  std::mutex m; std::vector<tpd::AudioConfig> opened; std::set<std::string> missing;   // `missing`: out ids that will not open
  std::atomic<bool> lost{false}; std::atomic<int> reroutes{0};
  double rate = 48000;
};
class ConfigOut : public tpd::AudioOut {
 public:
  explicit ConfigOut(ConfigCtl* c) : ctl_(c) {}
  void configure(const tpd::AudioConfig& c) override { cfg_ = c; }
  bool open(Callback cb, void* user, double* sr, std::string* err) override {
    { std::lock_guard<std::mutex> g(ctl_->m); ctl_->opened.push_back(cfg_); if (ctl_->missing.count(cfg_.outId)) { if (err) *err = "no such device"; return false; } }
    cb_ = cb; user_ = user; *sr = cfg_.sampleRate ? cfg_.sampleRate : ctl_->rate; sr_ = *sr; ctl_->lost = false; return true;
  }
  bool start(std::string*) override { started_ = true; return true; }
  void close() override { started_ = false; }
  int state() const override { return started_ ? 3 : 0; }
  std::string name() const override { return cfg_.outId.empty() ? "Default Out" : "Out " + cfg_.outId; }
  int periodFrames() const override { return cfg_.bufferFrames ? cfg_.bufferFrames : 256; }
  int latencyFrames() const override { return periodFrames() + 64; }
  bool lost() const override { return ctl_->lost.load(); }
  int reroutes() const override { return ctl_->reroutes.load(); }
  std::string deviceId() const override { return cfg_.outId; }
  std::vector<float> tick(int frames) {
    std::vector<float> b((size_t)frames * 2, 0.f);
    if (cb_ && started_) { const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); cb_(user_, b.data(), frames, ns); }
    return b;
  }
  bool started_ = false;
 private:
  ConfigCtl* ctl_; tpd::AudioConfig cfg_; Callback cb_ = nullptr; void* user_ = nullptr; double sr_ = 48000;
};
#endif
