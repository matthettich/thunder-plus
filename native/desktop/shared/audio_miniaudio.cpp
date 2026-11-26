#include <chrono>
#include <cstring>

#include "audio_out.h"
#include "miniaudio.h"

namespace tpd {
namespace {
class MaOut : public AudioOut {
 public:
  ~MaOut() override { close(); }
  bool open(Callback cb, void* user, double* sampleRate, std::string* err) override {
    cb_ = cb; user_ = user;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 0;   // the device's native rate
    cfg.dataCallback = &MaOut::data;
    cfg.pUserData = this;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    const ma_result r = ma_device_init(nullptr, &cfg, &dev_);
    if (r != MA_SUCCESS) { if (err) *err = std::string("ma_device_init failed: ") + ma_result_description(r); return false; }
    init_ = true; *sampleRate = (double)dev_.sampleRate; name_ = dev_.playback.name;
    return true;
  }
  bool start(std::string* err) override {
    const ma_result r = ma_device_start(&dev_);
    if (r != MA_SUCCESS) { if (err) *err = std::string("ma_device_start failed: ") + ma_result_description(r); return false; }
    return true;
  }
  void close() override { if (init_) { ma_device_uninit(&dev_); init_ = false; } }   // stops the callback
  int state() const override { return init_ ? (int)ma_device_get_state(&dev_) : 0; }
  std::string name() const override { return name_; }
 private:
  static void data(ma_device* d, void* out, const void*, ma_uint32 frames) {
    MaOut* s = static_cast<MaOut*>(d->pUserData);
    const int64_t ns = (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    s->cb_(s->user_, static_cast<float*>(out), (int)frames, ns);
  }
  ma_device dev_{}; bool init_ = false; Callback cb_ = nullptr; void* user_ = nullptr; std::string name_;
};
}  // namespace
std::unique_ptr<AudioOut> makeMiniaudioOut() { return std::unique_ptr<AudioOut>(new MaOut()); }
}  // namespace tpd
