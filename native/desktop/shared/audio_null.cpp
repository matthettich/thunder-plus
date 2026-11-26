#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "audio_out.h"

namespace tpd {
namespace {
class NullOut : public AudioOut {
 public:
  NullOut(double sr, int block) : sr_(sr), block_(block) {}
  ~NullOut() override { close(); }
  bool open(Callback cb, void* user, double* sampleRate, std::string*) override { cb_ = cb; user_ = user; *sampleRate = sr_; return true; }
  bool start(std::string*) override {
    if (run_.exchange(true)) return true;
    th_ = std::thread([this] {
      std::vector<float> buf((size_t)block_ * 2);
      auto next = std::chrono::steady_clock::now();
      while (run_.load()) {
        const int64_t ns = (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        cb_(user_, buf.data(), block_, ns);
        next += std::chrono::nanoseconds((int64_t)(1e9 * block_ / sr_));
        std::this_thread::sleep_until(next);
      }
    });
    return true;
  }
  void close() override { run_ = false; if (th_.joinable()) th_.join(); }
  int state() const override { return run_.load() ? 3 : 0; }
  std::string name() const override { return "null (no device)"; }
 private:
  double sr_; int block_; Callback cb_ = nullptr; void* user_ = nullptr; std::atomic<bool> run_{false}; std::thread th_;
};
}  // namespace
std::unique_ptr<AudioOut> makeNullAudioOut(double sr, int block) { return std::unique_ptr<AudioOut>(new NullOut(sr, block)); }
}  // namespace tpd
