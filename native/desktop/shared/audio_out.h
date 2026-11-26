// H6: the AudioOut implementations the shells use. NullAudioOut: a real-time pacing thread with no device (CI runners have no sound card; --null-audio; headless). MiniaudioOut: the default output device through
// miniaudio (WASAPI on Windows, Core Audio on the Mac), stereo f32 at the device's own rate, low-latency profile; H7 adds the device choice, rate and buffer (AudioConfig), the device list and the capture side. WASAPI exclusive mode is the Windows half of H7.
#ifndef TPD_AUDIO_OUT_H
#define TPD_AUDIO_OUT_H

#include <memory>

#include "audio_devices.h"
#include "audio_in.h"
#include "live_host.h"

namespace tpd {
std::unique_ptr<AudioOut> makeNullAudioOut(double sampleRate = 48000, int block = 512);
std::unique_ptr<AudioOut> makeMiniaudioOut();   // only in builds that compile audio_miniaudio.cpp (the shells)
std::shared_ptr<AudioEnumerator> makeMiniaudioEnumerator();   // H7: the OS device list
std::unique_ptr<AudioIn> makeMiniaudioIn();                    // H7: every channel of one capture device
}  // namespace tpd
#endif
