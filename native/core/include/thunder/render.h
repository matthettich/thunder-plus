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
#define THUNDER_RENDER_E_FONT (-7)       /* add_font: the bytes are not a SoundFont the core can read (not RIFF sfbk, a required chunk missing, a chunk past the end of the file) */

#define THUNDER_RENDER_MAX_SONG_BYTES ((size_t)16 << 20)
#define THUNDER_RENDER_MAX_VARS_BYTES ((size_t)4 << 20)
#ifndef THUNDER_RENDER_MAX_PAD_BYTES   /* (a test may build render_api.cpp with a smaller budget to reach the limit with little memory; no host does) */
#define THUNDER_RENDER_MAX_PAD_BYTES ((size_t)512 << 20)
#endif
#define THUNDER_RENDER_MAX_PAD_FRAMES ((int64_t)1 << 28)
#define THUNDER_RENDER_PADS 256          /* pad numbers 0 .. 255 */
#define THUNDER_RENDER_INSTS 64          /* instrument numbers 0 .. 63 (a variation is keyed by one) */
#define THUNDER_RENDER_MAX_VARIATIONS 4096   /* variations a handle holds */
#define THUNDER_RENDER_ROUTE_SAMPLER 0   /* a pad's notes play its rendered buffer (or a variation of it): the default */
#define THUNDER_RENDER_ROUTE_LIVE 1      /* a live-engine pad (the kit entry's `live`): its player is not wired in yet: its notes are SILENT and counted (stats.notes_live) */
#define THUNDER_RENDER_ROUTE_SF 2        /* a SoundFont pad (the kit entry's `sf`): its notes play through the SoundFont player when the pad has a prepared preset (set_pad_sf); without one they are SILENT; counted (stats.notes_sf, notes_sf_played) */
#define THUNDER_RENDER_MAX_FONTS 16      /* fonts a handle holds (add_font ids 0 .. 15) */
#define THUNDER_RENDER_MAX_SF_KEYS 4096  /* entries of a key list */

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
  /* appended in U34b (docs/port/U34B-NOTES.md); a host built against the older header never sees it and renders the mix */
  int32_t stem;                 /* 0: the mix. 1 .. 32: the STEM of that track (the number the page shows): what renderOut's pass(evs, {t}, .., lim = false, masterFx = false) renders: the track alone is heard
                                   (whatever mute / solo say: stem_tracks applies them), every track still plays so chokes and takeovers match the mix, through the track's effects (opts.effects), VOL, pan, MASTER VOL
                                   and OUT, WITHOUT the master effects, the master chain and the safety limiter (opts.master_chain is ignored for a stem) and with only the PDC latency trimmed
                                   (the master chain's is not). With the master chain on, an unknown-length stem is therefore longer than the mix by that latency: pass use_frames = the mix's length, as the
                                   page cuts every file to the mix's. Otherwise E_ARG from create. */
  int32_t reserved2;            /* must be 0 */
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
  /* appended with the SoundFont route */
  int32_t notes_sf_played;      /* notes of a ROUTE_SF pad that sounded through the SoundFont player (a subset of notes_sf; not part of notes_played) */
  /* appended after the SoundFont route's verification (verify-242 F6): the SoundFont player's own counters. voices_refused / voice_track_full / voice_auto_dropped and peak_voices above are the
     SAMPLER pool's and stay so. A note lost to a full SoundFont pool shows here (notes_sf > notes_sf_played also covers "no zone" and "no preset"). */
  uint32_t sf_voices_refused;   /* SoundFont notes dropped because all 32 voices were busy */
  uint32_t sf_voice_track_full; /* SoundFont notes dropped because the track's voice list was full */
  uint32_t sf_voice_auto_dropped; /* automation events the SoundFont voices dropped (a full list) */
  int32_t peak_voices_all;      /* the most voices of BOTH players (sampler + SoundFont) sounding at once */
  /* appended with the chop unit (docs/port/CHOP-NOTES.md) */
  int32_t notes_stretched;      /* notes of a sync 2 / 3 instrument: played UNSTRETCHED at their rate (the stretch is not ported yet; the page plays it stretched) */
  uint64_t seq_chopped;         /* notes the sequencer chopped (applyChop) */
  uint64_t seq_chop_skipped;    /* notes of a sliced / synced instrument whose pad had no set_pad_chop: left unchopped */
  uint64_t seq_chop_no_hits;    /* SMODE (find hits) slices without set_pad_hits: the equal grid was used */
  /* appended in U34b */
  int32_t stem;                 /* opts.stem as made (0: the mix) */
  uint32_t heard_mask;          /* bit t: track t is heard in this render (the mix: the audible tracks; a stem: its track, and for a bus that exists its audible sources) */
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
   LIVE pads are silent for now (see the defines; the notes are counted), however a buffer is set for them. SF pads play through the SoundFont player when set_pad_sf has prepared a preset for the pad (the pad's own buffer is never used); an SF pad without one is silent. Their notes take the track over (the previous
   note on the track is cut, a choke group is applied), as a live note does in the page (index.html:22015-22020, startNote's isLivePad branch) and in the core (core-processor.js:693
   liveNote, "mono: it takes over the track"); so does a MUT / GEN note whose variation is missing and a reversed + shaped note (this renderer's rule: the core's take(), core-processor.js:660, for a note it does not play). A note whose pad has NO buffer on the SAMPLER route
   (or no kit entry) does NOT take the track over: the page's startNote returns first (index.html:22012 `!kit.voices[ev.pad]`, :22022 `if (!b) return`), and the previous note
   goes on sounding. (The audio-thread core, core-processor.js:660, calls take() for such a note; the page, which an offline render follows, does not.) The SF route is approximate:
   the page's sfNote returns without taking over when the font is missing or no zone matches (index.html:22066, :22069); this renderer follows that for a prepared pad whose preset has no zone for the note's key and velocity, and still takes over for an SF pad with NO prepared preset (silent, as before the font input).
   A host that wants the page's own fallback for a
   live pad (its rendered twin) leaves the pad on SAMPLER. A pad number outside 0 .. 255 or a route outside 0 .. 2: E_ARG. */
int thunder_render_set_pad_route(thunder_render* r, int pad, int route);

/* Hands in a MUT / GEN variation the host rendered (the page's `vbuf`, keyed ii:vk:pad): instrument `ii` (0 .. 63), `kind` 'g' (GEN) or 'm' (MUT), `num` (0 .. 255,
   the command's value), pad (0 .. 255), `frames` MONO samples of `l` (finite) recorded at `sample_rate` Hz (finite, 1 .. 1e6). COPIED. The same key replaces; frames == 0
   (l may be NULL) removes. Variations count against THUNDER_RENDER_MAX_PAD_BYTES with the pads, one pad's frame limit applies, and at most THUNDER_RENDER_MAX_VARIATIONS
   are held (E_LIMIT); a replacement at the limit follows set_pad's rule (ERRORS (3)). Only before the first read or drain. A MUT / GEN note whose variation is missing is SILENT and counted (stats.notes_var_missing), never the base
   pad: a host whose page would play the base pad (no variation could be made) hands in the pad's own samples. A note of a sliced (slc > 1) or synced instrument plays the
   pad as it is and never looks for a variation. A pad changed with set_pad DROPS its variations (as the page does; see set_pad): set the pad first, then its variations. */
/* CHOP DATA (docs/port/CHOP-NOTES.md). A sliced (slc > 1) or synced instrument's notes are chopped by the sequencer as the page's applyChop does. It needs two things of the pad that the
   host prepares (the page's loopSecs and slicePoints): set_pad_chop: the pad's loop length in seconds (finite, > 0, <= 1e6; 0 removes the pad's chop data); set_pad_hits: with the instrument's
   SMODE on, the slice points of the pad cut into n_slices (2 .. 64) parts: count (1 .. n_slices) seconds, each finite and in 0 .. 1e6, as slicePoints returns them (count 0 removes). With SMODE off the points
   are the equal grid, which the sequencer computes. Both only before the first read or drain (E_STATE after), E_ARG for a bad argument before anything changes, E_MEMORY from set_pad_hits. A pad with no
   loop: its sliced / synced notes play unchopped (stats.seq_chop_skipped). set_pad on a pad DROPS its chop data (set the pad first). */
int thunder_render_set_pad_chop(thunder_render* r, int pad, double loop_secs);
int thunder_render_set_pad_hits(thunder_render* r, int pad, int n_slices, const double* pts, int count);

int thunder_render_set_variation(thunder_render* r, int ii, int kind, int num, int pad, const float* l, int64_t frames, double sample_rate);

/* SOUNDFONT PADS (docs/port/RENDER-SF-NOTES.md). A font is handed in ONCE (add_font) and shared by any number of pads (set_pad_sf names it by id), so a font used by 8 pads is
   parsed once. Everything is done at set time, off the render path: the font bytes are COPIED and parsed, the preset's samples are decoded (SF3: Ogg Vorbis) into float32; reads allocate
   nothing. All three functions: only before the first read or drain (E_STATE after, and on a dead handle); E_ARG for a null handle or any bad argument, before anything changes.
   BUDGET: the font copy and the tables parse() builds, and every pad's decoded PCM (4 bytes a frame), count against THUNDER_RENDER_MAX_PAD_BYTES with the pads and variations (E_LIMIT).
   The decoder's own budgets apply too (sf.h DecodeOpts: 256 MiB of PCM for one preset, 512 MiB of PCM and 2^29 work units for all the presets of one font, 2^28 work units for one call):
   a preset beyond them is refused with E_LIMIT. A failed call changes nothing.

   thunder_render_add_font: `bytes` (n > 0; the file as the page's parseSF takes it, SF2 or SF3) are copied. Returns the font id (0 .. THUNDER_RENDER_MAX_FONTS - 1) or a negative code:
   E_ARG (null, n == 0), E_LIMIT (n or the font's parse tables over the budget, a parse beyond the parser's limits, all ids taken), E_FONT (not a SoundFont), E_MEMORY.
   thunder_render_remove_font: frees a font and its memory. E_ARG for an unknown id or a font that a pad still uses (clear that pad's SoundFont first).

   thunder_render_set_pad_sf: pad `pad` (0 .. 255) plays font `font_id` preset (bank, prog): bank 0 .. 16383, prog 0 .. 127; a preset the font does not have falls back as the page's sfPreset does
   (bank 0 or 128 with the same program, else the first preset of that bank, else the font's first). `params`: the pad's sf values (NULL: all defaults); `keys`/`nkeys`: the MIDI keys the song and
   the pad can play (0 .. 127, up to THUNDER_RENDER_MAX_SF_KEYS entries, duplicates fine), the key AFTER the pad's `trans` (key = clamp(round(60 + 12 log2(rate) + trans), 0, 127), what the player computes).
   Only the zones that cover a listed key are decoded (the full-keyboard Grand Piano is 191 MB, one octave 28 MB): a note on another key is SILENT. keys == NULL (nkeys 0): every zone of the preset.
   keys != NULL with nkeys == 0 is E_ARG. A pad set twice is replaced: the new preset is built first, so the old and the new copy must fit the budget together (else E_LIMIT, the pad as it was;
   call clear_pad_sf first to make room). Setting a pad's SoundFont does not change its route: set THUNDER_RENDER_ROUTE_SF too.
   thunder_render_clear_pad_sf: removes the pad's prepared preset (its notes are silent again) and frees its PCM; a pad with none: 0, nothing done. */
typedef struct thunder_render_sf_params {
  uint32_t size;                /* sizeof(thunder_render_sf_params) as the caller compiled it; fields beyond it keep their defaults */
  uint32_t reserved0;           /* must be 0 */
  double trans;                 /* semitones, finite -128 .. 128 (default 0) */
  double velo;                  /* velocity sensitivity 0 .. 1 (default 0.7) */
  double atk;                   /* attack multiplier 0 .. 100 (default 1) */
  double rel;                   /* release multiplier 0 .. 100 (default 1) */
  double level;                 /* dB, finite -200 .. 200 (default 0) */
  double len;                   /* a drum pad's maximum length in seconds, 0 .. 3600 (default 0: 2 s) */
} thunder_render_sf_params;
int thunder_render_add_font(thunder_render* r, const uint8_t* bytes, size_t n);
int thunder_render_remove_font(thunder_render* r, int font_id);
int thunder_render_set_pad_sf(thunder_render* r, int pad, int font_id, int bank, int prog, const thunder_render_sf_params* params, const uint8_t* keys, size_t nkeys);
int thunder_render_clear_pad_sf(thunder_render* r, int pad);

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
   duration (about 6000 frames at 120 BPM, 48 kHz, lpb 4; measured 2.3 % of ELECTRO): the real length is never SHORTER than the estimate unless the render is cut (an unknown-length render does not end before its audio clock has reached the sequencer's end), and
   usually longer. It is not a promise either way: clamp a progress fraction to 1. (Before FU1 this comment said "always longer" and was false: the render could end up to 0.3 s before the sequencer's end.) Capped at the render's max_frames. With use_frames = 1, or once the render has ended, it is the exact length
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

/* The tracks that get a stem file, as renderOut decides (index.html:24304-24310): the audible tracks (mute / solo as the song has them) that have at least one note whose pad is in the
   kit (a pad set with a buffer, a route other than SAMPLER, or a prepared SoundFont; the page's `kit.voices[e.pad]`), plus a bus that exists and has an audible source in the list, minus a
   pure source that plays only through a bus in the list. (No bus exists in a native render until a chains host is wired, so today it is the first rule alone.) The notes are those the
   render itself plays (the sequencer is stepped, without audio, as for length_estimate, to the end or to max_frames). Writes up to `cap` track NUMBERS 1 .. 32, ascending, into `out` (the
   numbers opts.stem takes) and returns how many stems there are (more than cap is fine: the rest are not written); out == NULL (cap 0) asks only for the count. Call it after the pads
   are set; any handle (mix or stem), before or after the first read. E_ARG: NULL handle, cap < 0, out != NULL with cap == 0; E_STATE: a dead handle; E_LIMIT: the song's end is out of reach
   (20 million steps); E_MEMORY. */
int thunder_render_stem_tracks(thunder_render* r, int32_t* out, int cap);

/* Frees the handle at any time (cancel). NULL is allowed. Not while another thread is inside a call on the same handle. */
void thunder_render_free(thunder_render* r);

#ifdef __cplusplus
}
#endif
#endif
