/* Thunder core: the offline song render, public C API (unit U34a).
   A song (the app's song JSON, as bytes) plus the pad buffers the host has rendered, pulled out as stereo float32 in blocks of any size.
   A C API with no callbacks: the host PULLS. No C++ types, no exceptions cross it; every argument is checked and a bad one is a return
   code (and, where there is an `err` buffer, a text), never a crash. Wraps thunder::SongRenderer (core/src/render/render.h).

   THE FLOW      create -> set_pad (and set_pad_kit) for each pad -> read, read, ... until it returns 0 -> free
                 progress = thunder_render_position() / thunder_render_length(), polled by the host; cancel = stop reading and call free.
   THREADS       Not thread-safe per handle: one thread at a time may call anything on a given handle (any thread, but never two at once;
                 never the audio thread of a live core: a render allocates and takes seconds). Two handles are independent (no shared
                 mutable state) and may run on two threads.
   ERRORS        thunder_render_create returns NULL and writes `err`. The int functions return 0 on success and a negative
                 THUNDER_RENDER_E_* code on failure; thunder_render_read returns frames written (>= 0) or a negative code. (core.h's
                 functions return 1 / 0; these return 0 / negative because read has to carry a count and an error in one value.)
                 A failed call leaves the handle as it was, except THUNDER_RENDER_E_INTERNAL (below), after which the handle only accepts free.
   OUTPUT        stereo, non-interleaved (L and R arrays), float32, at opts.sample_rate. The first latency() frames of the song's render are
                 dropped, as pass() does for a render (the master chain's look-ahead, and the PDC delay when pdc_mode is on). The samples are
                 the same whatever block sizes read() is called with (1, 64, 333, 4096, the whole song).
   LIMITS        song bytes <= THUNDER_RENDER_MAX_SONG_BYTES; vars bytes <= THUNDER_RENDER_MAX_VARS_BYTES; pad data (copied, 4 bytes a
                 sample per channel) <= THUNDER_RENDER_MAX_PAD_BYTES in all; one pad <= THUNDER_RENDER_MAX_PAD_FRAMES frames. Beyond a limit:
                 THUNDER_RENDER_E_LIMIT. A render of a song of unknown length stops at opts.max_frames (default 1260 s: renderOut's own limit
                 is 1200 s). Out of memory: THUNDER_RENDER_E_MEMORY (the allocation failure is caught inside; a build without C++ exception
                 support, such as the wasm one, aborts instead: keep to the limits there).
   MEMORY        the song is parsed once (a copy of its JSON is kept: about 3 to 10 times its size). The mixer's delay lines dominate a handle:
                 about 30 MB at 48 kHz and 120 MB at 192 kHz, growing with the sample rate (see docs/port/U34A-NOTES.md for the measured table).
                 Pad buffers are copied into the handle: the host's arrays may be freed or reused as soon as set_pad returns. */
#ifndef THUNDER_RENDER_H
#define THUNDER_RENDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THUNDER_RENDER_OK 0
#define THUNDER_RENDER_E_ARG (-1)        /* a null, negative, NaN, out-of-range or overlapping argument, or an unknown pad */
#define THUNDER_RENDER_E_STATE (-2)      /* the call is not allowed now: set_pad after the first read, drain before the end, a failed handle */
#define THUNDER_RENDER_E_SONG (-3)       /* the song bytes are not a song the core can read (create only) */
#define THUNDER_RENDER_E_LIMIT (-4)      /* a size beyond the limits above */
#define THUNDER_RENDER_E_MEMORY (-5)     /* an allocation failed */
#define THUNDER_RENDER_E_INTERNAL (-6)   /* an unexpected failure inside; the handle is dead (only free is allowed) */

#define THUNDER_RENDER_MAX_SONG_BYTES ((size_t)64 << 20)
#define THUNDER_RENDER_MAX_VARS_BYTES ((size_t)16 << 20)
#define THUNDER_RENDER_MAX_PAD_BYTES ((size_t)512 << 20)
#define THUNDER_RENDER_MAX_PAD_FRAMES ((int64_t)1 << 28)
#define THUNDER_RENDER_PADS 256          /* pad numbers 0 .. 255 */

typedef struct thunder_render thunder_render;

/* Options. `size` is FIRST and is sizeof(thunder_render_opts) as the caller compiled it: a field the caller's struct does not reach keeps
   its default, so a host built against an older header keeps working. Zero-fill the struct, set `size`, then the fields you want.
   Every field's zero value is its default. opts == NULL means all defaults. */
typedef struct thunder_render_opts {
  uint32_t size;
  uint32_t reserved0;           /* must be 0 */
  double sample_rate;           /* Hz, 8000 .. 384000; 0: 48000. NaN, below 8000 or above 384000: E_ARG */
  int32_t effects;              /* 0 / 1: the song's track and master effects (tfx), as thunder-render --fx */
  int32_t master_chain;         /* 0 / 1: the song's master chain (mastering + safety limiter), as --master */
  int32_t pdc_mode;             /* latency compensation: 0 off, 1 auto, 2 on (the plan is built from the effects' latency: it matters only with effects = 1) */
  int32_t page_timing;          /* 0 / 1: voices timed as the web page's player (--page-timing) */
  int32_t use_frames;           /* 1: the render is exactly `frames` long (after the latency trim); 0: it runs until the song is over and every voice has ended */
  int32_t reserved1;            /* must be 0 */
  int64_t frames;               /* used when use_frames = 1: 0 .. 2^40 */
  int64_t max_frames;           /* unknown-length renders stop here, in frames of output (stats.hit_limit = 1). 0: 1260 s at the sample rate */
  const uint8_t* vars_json;     /* the page's coreVars() JSON {"vtrk":..,"NSLOT":..,"TFX":{..}} (NULL: the defaults) */
  size_t vars_bytes;
} thunder_render_opts;

/* What a render counted (RenderStats of render.h). `size` first, as in the opts. Any time; the numbers are as of now. */
typedef struct thunder_render_stats {
  uint32_t size;
  int32_t song_over;            /* the sequencer has reached the end */
  int32_t voices_active;        /* voices still sounding */
  int32_t hit_limit;            /* an unknown-length render was cut at max_frames */
  int64_t frames_rendered;      /* frames the song renderer has produced, the latency included */
  int32_t steps, events, notes, notes_played, notes_no_pad, notes_midi_only, notes_shaped_rev, mix_events;
  int32_t peak_voices;
  uint32_t voices_refused, voice_track_full, voice_auto_dropped, mix_auto_dropped;
  uint64_t seq_dropped;
} thunder_render_stats;

/* Parses the song and builds the renderer. song: the app's song JSON (a .thunder file's bytes or a song message), not NUL-terminated, copied.
   NULL and writes `err` (NUL-terminated, truncated to errCap; err may be NULL, errCap 0) on failure. */
thunder_render* thunder_render_create(const uint8_t* song, size_t song_bytes, const thunder_render_opts* opts, char* err, size_t err_cap);

/* Hands in pad `pad` (0 .. 255): `frames` samples of L, and of R for a stereo pad (R == NULL: mono), recorded at `sample_rate` Hz (finite,
   1 .. 1e6). The samples must be finite (else E_ARG). COPIED: the host may free or reuse its arrays when this returns. A pad set twice is replaced;
   frames == 0 (L and R may then be NULL) removes the pad (its notes are silent). Only before the first read (E_STATE after). */
int thunder_render_set_pad(thunder_render* r, int pad, const float* l, const float* r_, int64_t frames, double sample_rate);

/* The kit entry of a pad (the page's coreKitV(): {choke, syn, rel}); defaults 0, 0, 0. Only before the first read. choke: 0 .. 255; syn: 0 / 1; rel: finite, 0 .. 60 s. */
int thunder_render_set_pad_kit(thunder_render* r, int pad, int choke, int syn, double rel);

/* The length of the output in frames after the trim. With opts.use_frames = 1 it is `frames`, known from the start. Otherwise it is -1
   until the render has reached its end (the song's length depends on its voices' tails, which only running the render can tell), and the
   total from then on. -1 also for a bad handle (check the handle yourself). */
int64_t thunder_render_length(thunder_render* r);

/* Writes up to `frames` samples into l and r (each needs room for `frames` floats) and returns how many were written: fewer than asked only at
   the end; 0 at (and after) the end; negative on error (E_ARG: null buffer, frames < 0, overlapping buffers). frames == 0 returns 0 and
   touches nothing. Any block size: the output does not depend on it. The first call fixes the pads and the options. */
int64_t thunder_render_read(thunder_render* r, float* l, float* r_, int64_t frames);

/* Frames returned by read so far (the progress numerator). -1 for a null handle. */
int64_t thunder_render_position(thunder_render* r);

/* The latency in frames the render drops from its start (as the CLI's --master trim). -1 for a null handle. */
int thunder_render_latency(thunder_render* r);

/* Fills *out (out->size = sizeof(thunder_render_stats) before the call). */
int thunder_render_stats_get(thunder_render* r, thunder_render_stats* out);

/* After the output has ended (read returned 0, or position == length): keeps rendering, discarding the samples, until the song is over and
   every voice has ended, at most max_frames frames. Returns the frames run (>= 0) or E_LIMIT if the voices never ended (a track with no
   output; thunder-render's exit 3). E_STATE before the end. */
int64_t thunder_render_drain(thunder_render* r, int64_t max_frames);

/* Frees the handle at any time (cancel). NULL is allowed. Not while another thread is inside a call on the same handle. */
void thunder_render_free(thunder_render* r);

#ifdef __cplusplus
}
#endif
#endif
