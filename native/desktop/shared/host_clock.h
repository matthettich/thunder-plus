// H7: the ONE clock of the desktop shell. Every time the shell hands the core or the page (the audio callback's wall time, CoreMIDI packet times, MIDI-out send times, the clock map) is on this clock, in ns.
//   macOS    CLOCK_UPTIME_RAW = mach_absolute_time() in ns: the clock CoreMIDI's MIDITimeStamp and CoreAudio's host times use (so a CoreMIDI time converts with the mach timebase and nothing else)
//   others   std::steady_clock (QueryPerformanceCounter on Windows, CLOCK_MONOTONIC on Linux)
// Safe on the audio thread: one clock read, no lock, no allocation.
#ifndef TPD_HOST_CLOCK_H
#define TPD_HOST_CLOCK_H

#include <cstdint>
#if defined(__APPLE__)
#include <time.h>
#else
#include <chrono>
#endif

namespace tpd {
inline int64_t hostNowNs() {
#if defined(__APPLE__)
  return (int64_t)clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
  return (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
}  // namespace tpd
#endif
