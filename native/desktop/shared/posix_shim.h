// MSVC only (force-included into the Android shell's plain-C++ files, which call clock_gettime(CLOCK_MONOTONIC)). Not on any other platform.
#pragma once
#ifdef _MSC_VER
#include <time.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
static inline int clock_gettime(int, struct timespec* ts) {
  static LARGE_INTEGER freq = {{0, 0}};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER c; QueryPerformanceCounter(&c);
  const long long ns = (long long)((double)c.QuadPart * 1e9 / (double)freq.QuadPart);
  ts->tv_sec = (time_t)(ns / 1000000000LL); ts->tv_nsec = (long)(ns % 1000000000LL);
  return 0;
}
#endif
#endif
