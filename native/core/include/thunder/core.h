/* Thunder core: public C API (WORKPLAN section 10). A C API, not C++ classes, so Kotlin / Swift / wasm bind to it
   the same way. thunder_process is the ONLY function called on the audio thread. */
#ifndef THUNDER_CORE_H
#define THUNDER_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct thunder_core thunder_core;

/* Any thread, not the audio thread. Allocates everything the core will ever need. */
thunder_core* thunder_create(int sample_rate, int max_block);

/* AUDIO THREAD. Writes `frames` samples (frames <= max_block) to out_l and out_r. No allocation, no locks, no I/O. */
void thunder_process(thunder_core* core, float* out_l, float* out_r, int frames);

/* One non-audio thread at a time (single producer). A command as UTF-8 JSON (N0): {"t":"bpm","v":120}, {"t":"play"},
   {"t":"stop"}, {"t":"echo","n":<int>}. Returns 1 if queued, 0 if the queue is full or the command is unknown. */
int thunder_post(thunder_core* core, const char* bytes, int len);

/* One non-audio thread (single consumer). Copies the next event (UTF-8 JSON, NUL-terminated) into buf; returns its
   length, or 0 if none. Events: clock {f, wallNs, sr, outLat, under} (10 a second), pos {f, step}, echo {n, f}. */
int thunder_poll(thunder_core* core, char* buf, int cap);

/* Any non-audio thread. Hands the core a bulk buffer. N0: stored only. meta: {"o":<float offset>,"n":<total floats>}
   (offset 0 allocates the buffer). `count` floats are copied in at `o`. Returns 1 on success. */
int thunder_put(thunder_core* core, int id, const char* meta, const float* data, int count);

/* Any thread. Reads back what thunder_put stored: number of floats and a sum, for the A4 proof. Returns 1 if the id exists. */
int thunder_put_info(thunder_core* core, int id, int64_t* count, double* sum);

/* Any thread. The host reports its output latency (frames) and underrun count; the next clock events carry them.
   (N0 stand-in for the A5 clock logger's host inputs.) */
void thunder_host_stats(thunder_core* core, int out_latency_frames, int underruns);

/* Any non-audio thread, after the audio thread has stopped calling thunder_process. */
void thunder_destroy(thunder_core* core);

#ifdef __cplusplus
}
#endif
#endif
