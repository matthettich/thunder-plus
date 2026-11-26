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
#endif
