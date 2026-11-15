/* A5: the clock-drift logger. A host wraps its audio callback with rtlog_begin / rtlog_end; any other thread reads a
   snapshot. It answers the one question stage N0 exists for: does the audio run at the sound card's speed (1.00 s of sound
   per 1.00 s of wall time), with the screen on, off, and the app in the background? All state is lock-free atomics written
   by the audio thread only; nothing here allocates, locks or does I/O. C, header-only, so every host (JNI, Swift, wasm) uses
   the same numbers.
     ratio      seconds of audio rendered per second of wall time since rtlog_reset, in parts per million of 1.0
                (the gate: within +-1000 ppm = 0.1 %, measured over the whole run, the first second left out)
     max_gap    the longest wall time between two callbacks (a stall shows here before it shows as an underrun)
     max_work   the longest a callback took
     late       callbacks that took longer than the audio they produced (the engine could not keep up)
     underruns  the host's own count (AAudio xruns ...), passed in by the host */
#ifndef THUNDER_RTLOG_H
#define THUNDER_RTLOG_H
#include <stdint.h>
#ifdef __cplusplus
#include <atomic>
#define RT_ATOMIC(T) std::atomic<T>
#else
#include <stdatomic.h>
#define RT_ATOMIC(T) _Atomic(T)
#endif

typedef struct rtlog {
  RT_ATOMIC(int64_t) frames;      /* frames rendered since reset */
  RT_ATOMIC(int64_t) first_ns;    /* wall time of the first callback counted (after the settle time) */
  RT_ATOMIC(int64_t) first_frames;/* frames rendered before that callback */
  RT_ATOMIC(int64_t) last_ns;     /* wall time of the last callback's start */
  RT_ATOMIC(int64_t) max_gap_ns, max_work_ns, late, calls, underruns;
  int64_t begin_ns;               /* audio thread only */
  int sample_rate;
  int64_t settle_frames;          /* callbacks before this many frames are not counted in ratio / max_gap (start-up) */
} rtlog;

typedef struct rtlog_snapshot {
  int64_t frames, calls, late, underruns;
  double audio_s, wall_s;         /* over the counted span */
  double ratio_ppm;               /* (audio_s / wall_s - 1) * 1e6; 0 until a second has been counted */
  double max_gap_ms, max_work_ms;
  int pass;                       /* 1 when |ratio_ppm| <= 1000 and underruns == 0 and late == 0, over at least 1 s */
} rtlog_snapshot;

static inline void rtlog_reset(rtlog* g, int sample_rate) {
  g->frames = 0; g->first_ns = 0; g->first_frames = 0; g->last_ns = 0; g->max_gap_ns = 0; g->max_work_ns = 0; g->late = 0; g->calls = 0; g->underruns = 0;
  g->begin_ns = 0; g->sample_rate = sample_rate; g->settle_frames = sample_rate;   /* the first second is start-up */
}
/* AUDIO THREAD, at the top of the callback. now_ns: a monotonic clock. */
static inline void rtlog_begin(rtlog* g, int64_t now_ns) {
  const int64_t f = g->frames;
  if (f >= g->settle_frames) {
    if (g->first_ns == 0) { g->first_ns = now_ns; g->first_frames = f; }
    else { const int64_t gap = now_ns - g->last_ns; if (gap > g->max_gap_ns) g->max_gap_ns = gap; }
  }
  g->last_ns = now_ns; g->begin_ns = now_ns;
}
/* AUDIO THREAD, at the bottom of the callback, with the frames it rendered. */
static inline void rtlog_end(rtlog* g, int64_t now_ns, int frames) {
  const int64_t work = now_ns - g->begin_ns;
  if (g->frames >= g->settle_frames) {
    if (work > g->max_work_ns) g->max_work_ns = work;
    if (work * (int64_t)g->sample_rate > (int64_t)frames * 1000000000LL) g->late = g->late + 1;
  }
  g->frames = g->frames + frames; g->calls = g->calls + 1;
}
/* Any thread: the host's own underrun count. */
static inline void rtlog_host_underruns(rtlog* g, int64_t n) { g->underruns = n; }
/* Any thread. The span runs from the first counted callback's start to the last callback's start, so both ends are
   callback starts and the audio between them is exactly the frames rendered between them. */
static inline rtlog_snapshot rtlog_read(rtlog* g) {
  rtlog_snapshot s; const int64_t first = g->first_ns, last = g->last_ns, f = g->frames, f0 = g->first_frames;
  s.frames = f; s.calls = g->calls; s.late = g->late; s.underruns = g->underruns;
  s.max_gap_ms = (double)g->max_gap_ns / 1e6; s.max_work_ms = (double)g->max_work_ns / 1e6;
  s.audio_s = 0; s.wall_s = 0; s.ratio_ppm = 0; s.pass = 0;
  if (first && last > first && g->sample_rate > 0) {
    /* frames rendered before the LAST callback began = f minus that callback's own frames; the caller cannot know those,
       so the last callback is counted on both sides instead: wall to "now" is not used, only callback starts. */
    s.wall_s = (double)(last - first) / 1e9;
    s.audio_s = (double)(f - f0) / (double)g->sample_rate;
    if (s.wall_s >= 1.0) {
      /* the last callback's frames are in f but its duration is not in wall_s: take one average callback off */
      const int64_t calls = g->calls; const double per = calls > 0 ? (double)f / (double)calls / (double)g->sample_rate : 0;
      s.audio_s -= per;
      s.ratio_ppm = (s.audio_s / s.wall_s - 1.0) * 1e6;
      s.pass = (s.ratio_ppm <= 1000.0 && s.ratio_ppm >= -1000.0 && s.underruns == 0 && s.late == 0) ? 1 : 0;
    }
  }
  return s;
}
#endif
