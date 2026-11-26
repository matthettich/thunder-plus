// H6 / H7: the shell's real audio over miniaudio (CoreAudio on the Mac, WASAPI on Windows): the device list (AudioEnumerator), the stereo output (AudioOut) with a chosen device, rate and buffer, and the
// capture side (AudioIn) with EVERY channel of one device. Ids are "out:" / "in:" + the raw ma_device_id bytes in hex (the Electron helper's scheme: io/README.md), so a saved choice survives a restart.
// Ported from io/src/thunder_io.cc (the Electron helper's addon): the device records, DeviceObject / CurrentBufferFrames / InputLatencyFrames (CoreAudio), the lost-device rule.
// AUDIO THREAD: MaOut::data (hostNowNs, then the engine's callback), MaIn::data (hostNowNs, then InputCapture::onCapture's ring push). Nothing else runs there.
// UNVERIFIED without hardware: all of it (CI compiles it on the Mac runner; the null device is what the runner has).
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include "audio_devices.h"
#include "audio_in.h"
#include "audio_out.h"
#include "host_clock.h"
#include "miniaudio.h"
#if defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace tpd {
namespace {

const char* kHex = "0123456789abcdef";

std::string idHex(const ma_device_id* id) {
  const uint8_t* b = (const uint8_t*)id;
  size_t n = sizeof(ma_device_id);
  while (n > 1 && b[n - 1] == 0) n--;
  std::string s;
  for (size_t i = 0; i < n; i++) { s += kHex[b[i] >> 4]; s += kHex[b[i] & 15]; }
  return s;
}

// "out:<hex>" / "in:<hex>" back to a device id; false for anything else (the caller reports a bad id: never a guess).
bool parseId(const std::string& s, const char* prefix, ma_device_id* id) {
  std::memset(id, 0, sizeof *id);
  const size_t pl = std::strlen(prefix);
  if (s.size() < pl + 2 || s.compare(0, pl, prefix) != 0) return false;
  const size_t hn = s.size() - pl;
  if (hn % 2 || hn / 2 > sizeof(*id)) return false;
  uint8_t* b = (uint8_t*)id;
  for (size_t i = 0; i < hn / 2; i++) {
    int v = 0;
    for (int k = 0; k < 2; k++) { const char c = s[pl + i * 2 + (size_t)k]; v <<= 4; if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else return false; }
    b[i] = (uint8_t)v;
  }
  return true;
}

// ---- the shared context (enumeration and device init; miniaudio keeps it for the life of the process) ------------------------------------------------------------------------------------------------
std::mutex g_ctxM;
ma_context g_ctx;
bool g_ctxUp = false;
ma_result sharedContext() {   // g_ctxM held
  if (g_ctxUp) return MA_SUCCESS;
  const ma_result r = ma_context_init(nullptr, 0, nullptr, &g_ctx);
  if (r == MA_SUCCESS) g_ctxUp = true;
  return r;
}

#if defined(__APPLE__)
// LEAVE THE INTERFACE AS IT IS (owner, 7 Nov: "the audio interface gets reset"): miniaudio sets the device's I/O buffer size (kAudioDevicePropertyBufferFrameSize) to the period it asks for, and on CoreAudio that is
// the whole device's setting. Asking for the size it already has makes that a no-op. 0 when it cannot tell (then the system picks).
AudioObjectID deviceObject(const ma_device_id* id, bool input) {
  AudioObjectID dev = kAudioObjectUnknown;
  UInt32 sz = sizeof(dev);
  if (id) {
    CFStringRef uid = CFStringCreateWithCString(nullptr, id->coreaudio, kCFStringEncodingUTF8);
    if (!uid) return kAudioObjectUnknown;
    AudioValueTranslation t = {&uid, sizeof(uid), &dev, sizeof(dev)};
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDeviceForUID, kAudioObjectPropertyScopeGlobal, 0};
    sz = sizeof(t);
    const OSStatus st = AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, &t);
    CFRelease(uid);
    if (st != noErr) return kAudioObjectUnknown;
  } else {
    AudioObjectPropertyAddress a = {input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0};
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, &dev) != noErr) return kAudioObjectUnknown;
  }
  return dev;
}
unsigned currentBufferFrames(const ma_device_id* id, bool input) {
  const AudioObjectID dev = deviceObject(id, input);
  if (dev == kAudioObjectUnknown) return 0;
  UInt32 frames = 0, sz = sizeof(frames);
  AudioObjectPropertyAddress b = {kAudioDevicePropertyBufferFrameSize, input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput, 0};
  if (AudioObjectGetPropertyData(dev, &b, 0, nullptr, &sz, &frames) != noErr) return 0;
  return frames;
}
// The device's own delay beyond the callback's buffer, in its frames: device latency + safety offset + the first stream's latency (the scope's side). 0 when the device does not say.
unsigned hardwareLatencyFrames(const ma_device_id* id, bool input) {
  const AudioObjectID dev = deviceObject(id, input);
  if (dev == kAudioObjectUnknown) return 0;
  const AudioObjectPropertyScope scope = input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
  unsigned total = 0; UInt32 v = 0, sz = sizeof(v);
  AudioObjectPropertyAddress a = {kAudioDevicePropertyLatency, scope, 0};
  if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &sz, &v) == noErr) total += v;
  a.mSelector = kAudioDevicePropertySafetyOffset; v = 0; sz = sizeof(v);
  if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &sz, &v) == noErr) total += v;
  a.mSelector = kAudioDevicePropertyStreams; sz = 0;
  if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &sz) == noErr && sz >= sizeof(AudioStreamID)) {
    AudioStreamID streams[64]; if (sz > sizeof(streams)) sz = sizeof(streams);
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &sz, streams) == noErr) {
      AudioObjectPropertyAddress l = {kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal, 0};
      v = 0; UInt32 s2 = sizeof(v); if (AudioObjectGetPropertyData(streams[0], &l, 0, nullptr, &s2, &v) == noErr) total += v;
    }
  }
  return total < 48000 ? total : 0;   // a nonsense figure is ignored
}
#else
unsigned currentBufferFrames(const ma_device_id*, bool) { return 0; }
unsigned hardwareLatencyFrames(const ma_device_id*, bool) { return 0; }
#endif

// ---- the device list -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
class MaEnum : public AudioEnumerator {
 public:
  std::vector<AudioDevice> list() override {
    std::lock_guard<std::mutex> g(g_ctxM);
    if (sharedContext() != MA_SUCCESS) return {};
    ma_device_info *pPlay = nullptr, *pCap = nullptr; ma_uint32 nPlay = 0, nCap = 0;
    if (ma_context_get_devices(&g_ctx, &pPlay, &nPlay, &pCap, &nCap) != MA_SUCCESS) return {};
    const char* be = ma_get_backend_name(g_ctx.backend);
    std::vector<AudioEndpoint> eps;
    auto side = [&](const ma_device_info* arr, ma_uint32 n, bool input) {
      for (ma_uint32 i = 0; i < n; i++) {
        AudioEndpoint e; e.input = input; e.raw = idHex(&arr[i].id); e.name = arr[i].name; e.backend = be ? be : ""; e.isDefault = arr[i].isDefault != 0;
        const std::string key = (input ? "i" : "o") + e.raw;
        auto it = cache_.find(key);
        if (it == cache_.end()) {   // the details (channels, rates) once per device id: asking a driver every second is not free
          ma_device_info info; std::memset(&info, 0, sizeof info);
          Detail d; bool any = false;
          if (ma_context_get_device_info(&g_ctx, input ? ma_device_type_capture : ma_device_type_playback, &arr[i].id, &info) == MA_SUCCESS) {
            for (ma_uint32 k = 0; k < info.nativeDataFormatCount && k < 64; k++) {
              const ma_uint32 c = info.nativeDataFormats[k].channels;
              if (c == 0) any = true; else if ((int)c > d.channels) d.channels = (int)c;
              const ma_uint32 r = info.nativeDataFormats[k].sampleRate; if (r) d.rates.push_back((int)r);
            }
          }
          if (d.channels == 0 && (any || info.nativeDataFormatCount == 0)) d.channels = 2;   // unknown is reported as 2 (the helper's rule)
          it = cache_.emplace(key, d).first;
        }
        e.channels = it->second.channels; e.rates = it->second.rates;
        eps.push_back(std::move(e));
      }
    };
    side(pPlay, nPlay, false);
    side(pCap, nCap, true);
    return mergeAudioEndpoints(eps);
  }
 private:
  struct Detail { int channels = 0; std::vector<int> rates; };
  std::map<std::string, Detail> cache_;
};

// ---- the output ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
class MaOut : public AudioOut {
 public:
  ~MaOut() override { close(); }
  void configure(const AudioConfig& c) override { want_ = c; }
  bool open(Callback cb, void* user, double* sampleRate, std::string* err) override {
    cb_ = cb; user_ = user;
    ma_device_id id; const bool haveId = !want_.outId.empty();
    if (haveId && !parseId(want_.outId, "out:", &id)) { if (err) *err = "not a device id: " + want_.outId; return false; }
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID = haveId ? &id : nullptr;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = (ma_uint32)want_.sampleRate;   // 0: the device's native rate
    cfg.periodSizeInFrames = (ma_uint32)want_.bufferFrames;
    if (want_.bufferFrames == 0) { const unsigned cur = currentBufferFrames(haveId ? &id : nullptr, false); if (cur) cfg.periodSizeInFrames = cur; }   // never resize the interface
    cfg.dataCallback = &MaOut::data;
    cfg.notificationCallback = &MaOut::notify;
    cfg.pUserData = this;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    ma_result r;
    { std::lock_guard<std::mutex> g(g_ctxM); r = sharedContext(); if (r == MA_SUCCESS) r = ma_device_init(&g_ctx, &cfg, &dev_); }
    if (r != MA_SUCCESS) { if (err) *err = std::string("ma_device_init failed: ") + ma_result_description(r); return false; }
    init_ = true; id_ = want_.outId; *sampleRate = (double)dev_.sampleRate; name_ = dev_.playback.name;
    period_ = (int)dev_.playback.internalPeriodSizeInFrames;
    const double devRate = dev_.playback.internalSampleRate > 0 ? (double)dev_.playback.internalSampleRate : (double)dev_.sampleRate;
    const unsigned hw = hardwareLatencyFrames(haveId ? &id : nullptr, false);
    // what sits between the callback and the speaker: the callback's buffer (as many periods as miniaudio keeps queued: UNVERIFIED on CoreAudio, where it is one) plus the device's own delay, in the engine's frames
    latency_ = (int)(((double)period_ * (double)(dev_.playback.internalPeriods ? dev_.playback.internalPeriods : 1) + (double)hw) * (double)dev_.sampleRate / devRate);
    return true;
  }
  bool start(std::string* err) override {
    const ma_result r = ma_device_start(&dev_);
    if (r != MA_SUCCESS) { if (err) *err = std::string("ma_device_start failed: ") + ma_result_description(r); return false; }
    return true;
  }
  void close() override { if (init_) { closing_ = true; ma_device_uninit(&dev_); init_ = false; } }   // stops the callback
  int state() const override { return init_ ? (int)ma_device_get_state(&dev_) : 0; }
  std::string name() const override { return name_; }
  int periodFrames() const override { return period_; }
  int latencyFrames() const override { return latency_; }
  bool lost() const override { return lost_.load(std::memory_order_relaxed); }
  int reroutes() const override { return reroutes_.load(std::memory_order_relaxed); }
  std::string deviceId() const override { return id_; }
 private:
  // THE AUDIO THREAD: the clock read, then the engine's callback.
  static void data(ma_device* d, void* out, const void*, ma_uint32 frames) {
    MaOut* s = static_cast<MaOut*>(d->pUserData);
    s->cb_(s->user_, static_cast<float*>(out), (int)frames, hostNowNs());
  }
  // miniaudio's own thread: atomics only. `stopped` while we did not ask for it = the device went away; `rerouted` = the OS moved the stream (the default output changed): miniaudio keeps playing.
  static void notify(const ma_device_notification* n) {
    MaOut* s = static_cast<MaOut*>(n->pDevice->pUserData);
    if (n->type == ma_device_notification_type_stopped && !s->closing_.load()) s->lost_.store(true);
    else if (n->type == ma_device_notification_type_rerouted) s->reroutes_.fetch_add(1);
  }
  ma_device dev_{}; bool init_ = false; Callback cb_ = nullptr; void* user_ = nullptr; std::string name_, id_; AudioConfig want_;
  int period_ = 0, latency_ = 0;
  std::atomic<bool> lost_{false}, closing_{false};
  std::atomic<int> reroutes_{0};
};

// ---- the input ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
class MaIn : public AudioIn {
 public:
  ~MaIn() override { close(); }
  bool open(const AudioInConfig& c, Callback cb, void* user, AudioInInfo* info, std::string* err) override {
    cb_ = cb; user_ = user;
    ma_device_id id; const bool haveId = !c.inId.empty();
    if (haveId && !parseId(c.inId, "in:", &id)) { if (err) *err = "not a device id: " + c.inId; return false; }
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.pDeviceID = haveId ? &id : nullptr;
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 0;                      // every channel the device has
    cfg.capture.shareMode = ma_share_mode_shared;
    cfg.sampleRate = (ma_uint32)c.sampleRate;      // 0: the device's own; another rate is converted by miniaudio
    cfg.periodSizeInFrames = (ma_uint32)c.bufferFrames;
    if (c.bufferFrames == 0) { const unsigned cur = currentBufferFrames(haveId ? &id : nullptr, true); if (cur) cfg.periodSizeInFrames = cur; }   // never resize the interface
    cfg.performanceProfile = ma_performance_profile_low_latency;
    cfg.dataCallback = &MaIn::data;
    cfg.notificationCallback = &MaIn::notify;
    cfg.pUserData = this;
    ma_result r;
    { std::lock_guard<std::mutex> g(g_ctxM); r = sharedContext(); if (r == MA_SUCCESS) r = ma_device_init(&g_ctx, &cfg, &dev_); }
    if (r != MA_SUCCESS) { if (err) *err = std::string("ma_device_init failed: ") + ma_result_description(r); return false; }
    init_ = true;
    channels_ = (int)dev_.capture.channels;
    const double devRate = dev_.capture.internalSampleRate > 0 ? (double)dev_.capture.internalSampleRate : (double)dev_.sampleRate;
    const unsigned hw = hardwareLatencyFrames(haveId ? &id : nullptr, true);
    info->channels = channels_; info->sampleRate = (double)dev_.sampleRate; info->deviceRate = devRate; info->periodFrames = (int)dev_.capture.internalPeriodSizeInFrames;
    info->latencyFrames = (int)((double)hw * (double)dev_.sampleRate / devRate);   // the interface's own delay (the callback's buffer is not in it: the frames' times are taken at the callback)
    info->name = dev_.capture.name; info->id = c.inId;
    return true;
  }
  bool start(std::string* err) override {
    const ma_result r = ma_device_start(&dev_);
    if (r != MA_SUCCESS) { if (err) *err = std::string("ma_device_start failed: ") + ma_result_description(r); return false; }
    return true;
  }
  void close() override { if (init_) { closing_ = true; ma_device_uninit(&dev_); init_ = false; } }
  bool lost() const override { return lost_.load(std::memory_order_relaxed); }
 private:
  // THE CAPTURE THREAD: the clock read, then InputCapture::onCapture (one ring push).
  static void data(ma_device* d, void*, const void* in, ma_uint32 frames) {
    MaIn* s = static_cast<MaIn*>(d->pUserData);
    if (in) s->cb_(s->user_, static_cast<const float*>(in), (int)frames, s->channels_, hostNowNs());
  }
  static void notify(const ma_device_notification* n) {
    MaIn* s = static_cast<MaIn*>(n->pDevice->pUserData);
    if (n->type == ma_device_notification_type_stopped && !s->closing_.load()) s->lost_.store(true);
  }
  ma_device dev_{}; bool init_ = false; Callback cb_ = nullptr; void* user_ = nullptr; int channels_ = 0;
  std::atomic<bool> lost_{false}, closing_{false};
};

}  // namespace

std::unique_ptr<AudioOut> makeMiniaudioOut() { return std::unique_ptr<AudioOut>(new MaOut()); }
std::shared_ptr<AudioEnumerator> makeMiniaudioEnumerator() { return std::make_shared<MaEnum>(); }
std::unique_ptr<AudioIn> makeMiniaudioIn() { return std::unique_ptr<AudioIn>(new MaIn()); }

}  // namespace tpd
