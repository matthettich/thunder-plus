// H6: the AudioOut implementations the shells use. NullAudioOut: a real-time pacing thread with no device (CI runners have no sound card; --null-audio; headless). MiniaudioOut: the default output device through
// miniaudio (WASAPI on Windows, Core Audio on the Mac), stereo f32 at the device's own rate, low-latency profile; the buffer size, device choice and exclusive mode are H7 (Settings > Audio).
#ifndef TPD_AUDIO_OUT_H
#define TPD_AUDIO_OUT_H

#include <memory>

#include "live_host.h"

namespace tpd {
std::unique_ptr<AudioOut> makeNullAudioOut(double sampleRate = 48000, int block = 512);
std::unique_ptr<AudioOut> makeMiniaudioOut();   // only in builds that compile audio_miniaudio.cpp (the shells)
}  // namespace tpd
#endif
