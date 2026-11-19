/* Thunder core: the offline song render, public C API (unit U34a).
   A song (the app's song JSON, as bytes) plus the pad buffers the host has rendered, pulled out as stereo float32 in blocks of any size.
   A C API with no callbacks: the host PULLS. No C++ types, no exceptions cross it; every argument is checked and a bad one is a return
   code (and, where there is an `err` buffer, a text), never a crash. Wraps thunder::SongRenderer (core/src/render/render.h).

   THE FLOW      create -> set_pad (and set_pad_kit) for each pad -> read, read, ... until it returns 0 -> free
                 progress = thunder_render_position() / thunder_render_length(), polled by the host; cancel = stop reading and call free.
   THREADS       Not thread-safe per handle: one thread at a time may call anything on a given handle (any thread, but never two at once;
                 never the audio thread of a live core: a render allocates and takes seconds). Two handles are independent and may run on
                 two threads: a handle holds all its mutable state, and what the process shares is read-only after its first use (tables made
                 once; ThreadSanitizer is clean on three threads rendering at once). One process-wide state exists in the core, the pump
                 effects' (fxpump.cpp: handle table, block buffers, "ONE AUDIO THREAD ONLY"), and a render never reaches it: the song's
                 effect slots are made only from the page's wasm effects (types 0..33 and 93..95 of fxhost.h), a pump effect (kind 200 and up)
                 is made only by a host that asks FxHost for one, which this API never does. So a host needs no rule such as "one render at a time".
   ERRORS        thunder_render_create returns NULL and writes `err`. The int functions return 0 on success and a negative
                 THUNDER_RENDER_E_* code on failure; thunder_render_read returns frames written (>= 0) or a negative code. (core.h's
                 functions return 1 / 0; these return 0 / negative because read has to carry a count and an error in one value.)
                 A failed call leaves the handle as it was, with three exceptions. (1) THUNDER_RENDER_E_INTERNAL, from any call: the handle is dead.
                 (2) THUNDER_RENDER_E_MEMORY from read or drain (an allocation failed while rendering): the handle is dead too, and the
                 frames that call had copied out are lost with it. A dead handle accepts only free: every other call that takes one returns
                 THUNDER_RENDER_E_STATE (the getters length, position, latency, length_estimate and stats_get still answer). (3) THUNDER_RENDER_E_MEMORY
                 from set_pad that REPLACES a pad, or from set_variation that REPLACES a variation, when what the handle holds now and the new copy
                 together would pass the pad limit: the old copy is released first (so the pads and variations never hold more than the limit), and
                 when the new one cannot be allocated the pad (with its variations) or the variation is removed; the handle is otherwise
                 as it was. E_MEMORY from create makes no handle and leaves nothing allocated; from any other set_pad it changes nothing.
   OUTPUT        stereo, non-interleaved (L and R arrays), float32, at opts.sample_rate. The first latency() frames of the song's render are
                 dropped, as pass() does for a render (the master chain's look-ahead, and the PDC delay when pdc_mode is on). The samples are
                 the same whatever block sizes read() is called with (1, 64, 333, 4096, the whole song).
   LIMITS        song bytes <= THUNDER_RENDER_MAX_SONG_BYTES; vars bytes <= THUNDER_RENDER_MAX_VARS_BYTES; pad data (copied, 4 bytes a
                 sample per channel) <= THUNDER_RENDER_MAX_PAD_BYTES in all, variations (4 bytes a sample) included, and the handle never holds more than that
                 for pads and variations together, even while a pad or a variation is being replaced (see ERRORS (3)); one pad <= THUNDER_RENDER_MAX_PAD_FRAMES frames. Beyond a limit: THUNDER_RENDER_E_LIMIT.
                 A render of a song of unknown length ends at opts.max_frames at the latest, EXACTLY there (the output is cut to it, not rounded to the
                 128-frame quantum), with stats.hit_limit = 1 whenever something was cut (default 1260 s: renderOut's own limit is 1200 s).
   OUT OF MEMORY an allocation that fails is caught inside and returned as THUNDER_RENDER_E_MEMORY (create: NULL and err "out of memory"; the effects'
                 own malloc failures too). That needs a build with C++ exceptions, which every host build has (the files that make a handle are
                 compiled with them). A build WITHOUT C++ exceptions (the wasm one) compiles the same source with the try / catch removed (ctest
                 render_api_noexc compiles it that way): there a failed allocation is not caught and the process ABORTS (std::terminate, or abort()
                 in the effects' allocator). A host there keeps inside the limits and the figures below.
   MEMORY        measured (Release, GCC 13, x86-64; docs/port/U34A-NOTES.md). The song is parsed once and its parse tree is kept for the life of the
                 handle: ELECTRO (0.86 MB) costs about 8.6 times its size, a hostile song made of two-byte numbers up to 15.5 times (a song at the limit,
                 16 MiB: a 249 MiB handle, 280 MiB at the peak while parsing); the vars the same way, up to 18.6 times (4 MiB: 75 MiB). The mixer's delay
                 lines dominate a normal handle: ELECTRO, no pads: about 53 MB at 48 kHz, 143 MB at 192 kHz, 263 MB at 384 kHz; with effects and the
                 master chain on about 94 / 218 / 385 MB. Add the pads (4 bytes a sample, copied) up to the pad limit. The worst handle one can make
                 with arguments inside the limits is therefore about 1.2 GB: 249 MiB (song) + 75 MiB (vars) + 512 MiB (pads) + 385 MB (384 kHz with
                 effects and master chain). Pad buffers are copied into the handle: the host's arrays may be freed or reused as soon as set_pad returns.
   SAMPLE RATE   8000 to 384000 Hz. The Cloud Seed reverb (an effect) is built for at most 96 kHz: above that it runs with its 96 kHz timing (shorter
                 delays than the rate asks for) so that it stays inside its buffers; every other effect follows the rate, and up to 96 kHz nothing changes. */
#ifndef THUNDER_RENDER_H
#define THUNDER_RENDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THUNDER_RENDER_OK 0
#define THUNDER_RENDER_E_ARG (-1)        /* a null, negative, NaN, out-of-range or overlapping argument, or an unknown pad */
#define THUNDER_RENDER_E_STATE (-2)      /* the call is not allowed now: set_pad after the first read or a drain, drain before the end, a dead handle */
#define THUNDER_RENDER_E_SONG (-3)       /* the song bytes are not a song the core can read (create only) */
#define THUNDER_RENDER_E_LIMIT (-4)      /* a size beyond the limits above */
#define THUNDER_RENDER_E_MEMORY (-5)     /* an allocation failed (from read or drain: the handle is dead; see ERRORS) */
#define THUNDER_RENDER_E_INTERNAL (-6)   /* an unexpected failure inside; the handle is dead (only free is allowed) */

#define THUNDER_RENDER_MAX_SONG_BYTES ((size_t)16 << 20)
#define THUNDER_RENDER_MAX_VARS_BYTES ((size_t)4 << 20)
#define THUNDER_RENDER_MAX_PAD_BYTES ((size_t)512 << 20)
#define THUNDER_RENDER_MAX_PAD_FRAMES ((int64_t)1 << 28)
#define THUNDER_RENDER_PADS 256          /* pad numbers 0 .. 255 */
#define THUNDER_RENDER_INSTS 64          /* instrument numbers 0 .. 63 (a variation is keyed by one) */
#define THUNDER_RENDER_MAX_VARIATIONS 4096   /* variations a handle holds */
#define THUNDER_RENDER_ROUTE_SAMPLER 0   /* a pad's notes play its rendered buffer (or a variation of it): the default */
#define THUNDER_RENDER_ROUTE_LIVE 1      /* a live-engine pad (the kit entry's `live`): its player is not wired in yet: its notes are SILENT and counted (stats.notes_live) */
#define THUNDER_RENDER_ROUTE_SF 2        /* a SoundFont pad (the kit entry's `sf`): no host input for a font yet: its notes are SILENT and counted (stats.notes_sf) */

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
  int32_t use_frames;           /* 1: the render is exactly `frames` long (after the latency trim; past the song's end it runs on: exact zeros, or with effects their tails); 0: it runs until the song is over and every voice has ended */
  int32_t reserved1;            /* must be 0 */
  int64_t frames;               /* used when use_frames = 1: 0 .. 2^40 */
  int64_t max_frames;           /* unknown-length renders end here at the latest, in frames of output, exactly (stats.hit_limit = 1 if cut). 0: 1260 s at the sample rate */
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
  /* appended in the dispatch step (D2 / D3); a host built against the older header never sees them */
  int32_t notes_live;           /* notes of a THUNDER_RENDER_ROUTE_LIVE pad (silent) */
  int32_t notes_sf;             /* notes of a THUNDER_RENDER_ROUTE_SF pad (silent) */
  int32_t notes_var;            /* notes that played a MUT / GEN variation (a subset of notes_played) */
  int32_t notes_var_missing;    /* MUT / GEN notes whose variation was not handed in with set_variation (silent: never the base pad) */
  int32_t notes_unshaped;       /* notes of a sliced (slc > 1) or synced instrument with MUT / GEN / CUT / RES / DEC: the pad as it is, as the page's varRec returns it */
  int32_t events_ignored;       /* sequencer events this renderer has no route for (eng, chc, tempo, the MIDI events) */
} thunder_render_stats;

/* Parses the song and builds the renderer. song: the app's song JSON (a .thunder file's bytes or a song message), not NUL-terminated, copied.
   NULL and writes `err` (NUL-terminated, truncated to errCap; err may be NULL, errCap 0) on failure. */
thunder_render* thunder_render_create(const uint8_t* song, size_t song_bytes, const thunder_render_opts* opts, char* err, size_t err_cap);

/* Hands in pad `pad` (0 .. 255): `frames` samples of L, and of R for a stereo pad (R == NULL: mono), recorded at `sample_rate` Hz (finite,
   1 .. 1e6). The samples must be finite (else E_ARG). COPIED: the host may free or reuse its arrays when this returns. A pad set twice is replaced;
   frames == 0 (L and R may then be NULL) removes the pad (its notes are silent). Only before the first read or drain (E_STATE after).
   The pads and variations together may not pass THUNDER_RENDER_MAX_PAD_BYTES: a replacement is judged by what the handle would hold after it (else E_LIMIT, the pad as
   it was); see ERRORS (3) for the one case in which a replacement releases the old copy first.
   A pad whose buffer changes LOSES ITS VARIATIONS (every instrument's, any kind and number) when the call succeeds, as the page drops them: core/core-processor.js:337
   (`case 'buf'`: the pad's data is set or deleted, then varDrop(d.pad), :239; seq-core.js:1195 and :1097 are the same code). That includes frames == 0 (the pad removed), and the
   bytes the variations held count as freed by the call. A failed call (E_ARG, E_LIMIT, E_STATE) drops nothing. A host sets the pad first and its variations after. */
int thunder_render_set_pad(thunder_render* r, int pad, const float* l, const float* r_, int64_t frames, double sample_rate);

/* The kit entry of a pad (the page's coreKitV(): {choke, syn, rel}); defaults 0, 0, 0. Only before the first read or drain. choke: 0 .. 255; syn: 0 / 1; rel: finite, 0 .. 60 s. */
int thunder_render_set_pad_kit(thunder_render* r, int pad, int choke, int syn, double rel);

/* Which player a pad's notes are for (the page's coreKitV() `live` / `sf`): THUNDER_RENDER_ROUTE_*. Default SAMPLER. Only before the first read or drain.
   LIVE and SF pads are silent for now (see the defines; the notes are counted), however a buffer is set for them. Their notes take the track over (the previous
   note on the track is cut, a choke group is applied), as a live note does in the page (index.html:22015-22020, startNote's isLivePad branch) and in the core (core-processor.js:693
   liveNote, "mono: it takes over the track"); so does a MUT / GEN note whose variation is missing and a reversed + shaped note (this renderer's rule: the core's take(), core-processor.js:660, for a note it does not play). A note whose pad has NO buffer on the SAMPLER route
   (or no kit entry) does NOT take the track over: the page's startNote returns first (index.html:22012 `!kit.voices[ev.pad]`, :22022 `if (!b) return`), and the previous note
   goes on sounding. (The audio-thread core, core-processor.js:660, calls take() for such a note; the page, which an offline render follows, does not.) The SF route is approximate:
   the page's sfNote returns without taking over when the font is missing or no zone matches (index.html:22066, :22069); this renderer has neither input and always takes over.
   A host that wants the page's own fallback for a
   live pad (its rendered twin) leaves the pad on SAMPLER. A pad number outside 0 .. 255 or a route outside 0 .. 2: E_ARG. */
int thunder_render_set_pad_route(thunder_render* r, int pad, int route);

/* Hands in a MUT / GEN variation the host rendered (the page's `vbuf`, keyed ii:vk:pad): instrument `ii` (0 .. 63), `kind` 'g' (GEN) or 'm' (MUT), `num` (0 .. 255,
   the command's value), pad (0 .. 255), `frames` MONO samples of `l` (finite) recorded at `sample_rate` Hz (finite, 1 .. 1e6). COPIED. The same key replaces; frames == 0
   (l may be NULL) removes. Variations count against THUNDER_RENDER_MAX_PAD_BYTES with the pads, one pad's frame limit applies, and at most THUNDER_RENDER_MAX_VARIATIONS
   are held (E_LIMIT); a replacement at the limit follows set_pad's rule (ERRORS (3)). Only before the first read or drain. A MUT / GEN note whose variation is missing is SILENT and counted (stats.notes_var_missing), never the base
   pad: a host whose page would play the base pad (no variation could be made) hands in the pad's own samples. A note of a sliced (slc > 1) or synced instrument plays the
   pad as it is and never looks for a variation. A pad changed with set_pad DROPS its variations (as the page does; see set_pad): set the pad first, then its variations. */
int thunder_render_set_variation(thunder_render* r, int ii, int kind, int num, int pad, const float* l, int64_t frames, double sample_rate);

/* The length of the output in frames after the trim (an unknown-length render cut at max_frames: max_frames). With opts.use_frames = 1 it is `frames`, known from the start. Otherwise it is -1
   until the render has reached its end (the song's length depends on its voices' tails, which only running the render can tell), and the
   total from then on. -1 also for a bad handle (check the handle yourself). */
int64_t thunder_render_length(thunder_render* r);

/* Writes up to `frames` samples into l and r (each needs room for `frames` floats) and returns how many were written: fewer than asked only at
   the end; 0 at (and after) the end; negative on error (E_ARG: null buffer, frames < 0, overlapping buffers). frames == 0 returns 0 and
   touches nothing. Any block size: the output does not depend on it. The first call fixes the pads and the options. */
int64_t thunder_render_read(thunder_render* r, float* l, float* r_, int64_t frames);

/* An ESTIMATE of the length, for a progress bar before the render has reached its end: the sequencer's song end in output frames (after the
   latency trim), found by stepping a second sequencer, without any audio, to the end of the song (the song's rows, tempo changes and chance rolls
   included: it ends where the render's own sequencer does). It does not include the tails of the voices that still sound at the song's end
   (reverb, long samples), which only the render can tell, and it is the START of the song's last row (the sequencer's time when it stops), so it also misses that row's own
   duration (about 6000 frames at 120 BPM, 48 kHz, lpb 4; measured 2.3 % of ELECTRO): the real length is always longer, never shorter, than the estimate unless the render is cut, so never treat this as a promise, and
   clamp a progress fraction to 1. Capped at the render's max_frames. With use_frames = 1, or once the render has ended, it is the exact length
   (as length()). Costs a fraction of a second for a long song, once: the answer is kept. -1: not available (a null handle, a song whose end the
   stepping cannot reach in 20 million steps, or out of memory: the handle is not affected and a later call may succeed). */
int64_t thunder_render_length_estimate(thunder_render* r);

/* Frames returned by read so far (the progress numerator). -1 for a null handle. */
int64_t thunder_render_position(thunder_render* r);

/* The latency in frames the render drops from its start (as the CLI's --master trim). -1 for a null handle. */
int thunder_render_latency(thunder_render* r);

/* Fills *out (out->size = sizeof(thunder_render_stats) before the call). */
int thunder_render_stats_get(thunder_render* r, thunder_render_stats* out);

/* After the output has ended (read returned 0, or position == length): keeps rendering, discarding the samples, until the song is over and
   every voice has ended, at most max_frames frames. Returns the frames run (>= 0) or E_LIMIT if the voices never ended (a track with no
   output; thunder-render's exit 3). E_STATE before the end. Like the first read, it fixes the pads and the kit (set_pad is E_STATE after it). */
int64_t thunder_render_drain(thunder_render* r, int64_t max_frames);

/* Frees the handle at any time (cancel). NULL is allowed. Not while another thread is inside a call on the same handle. */
void thunder_render_free(thunder_render* r);

#ifdef __cplusplus
}
#endif
#endif
