// H7: the audio INPUT of the desktop shell: every channel of one capture device (the owner's Cranborne 500R8 presents 28), as float32, delivered by the OS's capture callback to a lock-free ring
// (input_capture.h). The OS part is audio_miniaudio.cpp (a separate ma_device of type capture: its own clock, its own thread, like the Electron helper); a fake in the tests.
#ifndef TPD_AUDIO_IN_H
#define TPD_AUDIO_IN_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace tpd {

struct AudioInConfig { std::string inId; int sampleRate = 0; int bufferFrames = 0; };   // "" = the system default input; 0 = the device's own
struct AudioInInfo { int channels = 0; double sampleRate = 0; int periodFrames = 0; int latencyFrames = 0; double deviceRate = 0; std::string name, id; };

class AudioIn {
 public:
  // The capture thread's callback: `frames` interleaved frames of `channels` samples; hostNs = host_clock.h time of the callback's start. RT rules: the callee does a ring push and nothing else.
  using Callback = void (*)(void* user, const float* interleaved, int frames, int channels, int64_t hostNs);
  virtual ~AudioIn() = default;
  virtual bool open(const AudioInConfig& cfg, Callback cb, void* user, AudioInInfo* info, std::string* err) = 0;   // does not start
  virtual bool start(std::string* err) = 0;
  virtual void close() = 0;                                  // stops; no callback runs after it returns
  virtual bool lost() const { return false; }                // the device stopped by itself (unplugged)
};
using AudioInFactory = std::function<std::unique_ptr<AudioIn>()>;

}  // namespace tpd
#endif
