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

/* Any non-audio thread. A command as UTF-8 JSON (N0). Returns 1 if queued, 0 if the queue is full or the command is
   malformed. */
int thunder_post(thunder_core* core, const char* bytes, int len);

/* Any non-audio thread. Copies the next event (UTF-8 JSON) into buf; returns its length, or 0 if none. */
int thunder_poll(thunder_core* core, char* buf, int cap);

/* Any non-audio thread. Hands the core a bulk buffer (sample data). N0: stored only. Returns 1 on success. */
int thunder_put(thunder_core* core, int id, const char* meta, const float* data, int frames);

/* Any non-audio thread, after the audio thread has stopped calling thunder_process. */
void thunder_destroy(thunder_core* core);

#ifdef __cplusplus
}
#endif
#endif
