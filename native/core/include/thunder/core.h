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

/* AUDIO THREAD. Writes `frames` samples to out_l and out_r (any count; more than max_block is done in pieces).
   No allocation, no locks, no I/O. */
void thunder_process(thunder_core* core, float* out_l, float* out_r, int frames);

/* One control thread (single producer). A command as UTF-8 JSON, "t" first: {"t":"bpm","v":120}, {"t":"play"},
   {"t":"stop"}, {"t":"echo","n":<int>}, {"t":"song.load","rev":<int>,...}. bpm / play / stop / echo may carry "at":<frame>
   (the core's sample timeline, as clock events report it): the command takes effect on exactly that sample; without it,
   at the first sample of the next block. Returns 1 if queued, 0 if the queue is full or the command is unknown. */
int thunder_post(thunder_core* core, const char* bytes, int len);

/* One non-audio thread (single consumer). Copies the next event (UTF-8 JSON, NUL-terminated) into buf (cap >= 256); returns its
   length, or 0 if none. Events: clock {f, wallNs, sr, outLat, under, evtDropped, lateCmds} (10 a second; evtDropped: events the
   full event ring refused since the start, lateCmds: timed commands applied at once because the waiting list was full), pos {f, step},
   echo {n, f}, song {rev, f} (the audio thread now reads that song snapshot, from frame f; it is kept and retried until the ring has
   room, and one acknowledgement for rev N covers every revision below N that was adopted in the same block). */
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
