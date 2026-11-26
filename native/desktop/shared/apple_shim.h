// H7: force-included (-include) into the Android shell's midi_out.cpp when it is compiled for macOS. That file sleeps to an absolute time with clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME), which macOS lacks.
// The times it passes are the MidiHost clock, which MidiManager sets to hostNowNs() (CLOCK_UPTIME_RAW), so the shim sleeps on THAT clock whatever clock id is named: a wake-up is never early or late by the
// time the Mac spent asleep.
#ifndef TPD_APPLE_SHIM_H
#define TPD_APPLE_SHIM_H
#ifdef __APPLE__
#include <time.h>
#include "host_clock.h"
#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif
static inline int clock_nanosleep(clockid_t, int flags, const struct timespec* req, struct timespec*) {
  int64_t target = (int64_t)req->tv_sec * 1000000000LL + (int64_t)req->tv_nsec;
  if (!(flags & TIMER_ABSTIME)) target += tpd::hostNowNs();
  const int64_t d = target - tpd::hostNowNs();
  if (d > 0) { struct timespec ts; ts.tv_sec = (time_t)(d / 1000000000LL); ts.tv_nsec = (long)(d % 1000000000LL); nanosleep(&ts, nullptr); }
  return 0;
}
#endif
#endif
