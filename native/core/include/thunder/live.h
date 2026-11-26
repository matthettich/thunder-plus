/* Thunder core: the LIVE host, public C API (unit U37: P0 wrote this contract, P5 implemented it in core/src/live/live_api.cpp over thunder::LiveHost; what the live host does not build yet is said under THUNDER_LIVE_CAP_*).
   The real-time counterpart of render.h: a song, the pads the host has rendered, and a transport, played in real time through the sequencer, the voices,
   the mixer, the effects and the master chain, and pulled by the host's audio callback. A C API, so that wasm, Kotlin / JNI and Swift bind to it the
   same way. No callbacks, no C++ types, no exceptions cross it; every argument is checked and a bad one is a return code, never a crash.
   Wraps thunder::LiveHost (core/src/live/live_host.h). The design, the rulings and the reasons are in docs/port/U37-DESIGN-DRAFT.md (sections 3, 4, 5, 12)
   and docs/port/U37-P0-NOTES.md; where this header and the draft differ, this header (once frozen) wins.

   TWO HOST MODELS
     A threaded SHELL (Android / Mac / Windows): a CONTROL thread (the UI side: every call marked CONTROL, one at a time) and an AUDIO thread (the device callback:
       thunder_live_process only). Both run at once. Any other thread may call the ANY functions.
     The WEB: one thread. The AudioWorklet's message handlers run on the rendering thread BETWEEN process() calls; the page cannot share memory with the module.
       Handlers play the CONTROL role and process() the AUDIO role, never at the same time. Everything a CONTROL function does (a parse, a Seq, an effect object, a pad
       copy) therefore steals time from the quantum (2.67 ms at 48 kHz); the host decides when to call them (the lead's ruling D2 / D3: whole song text, debounced).
     The rules below are written for the shell. In the web they hold trivially; "between quanta" is then the whole story.

   THE QUANTUM  The core renders in fixed quanta of THUNDER_LIVE_QUANTUM = 128 frames, whatever the size of the host's call. A quantum is rendered when its first
     sample is needed and handed out from a small buffer, so the samples are the same for every call size (1, 64, 128, 480, 4096) and no latency is added. The grid
     of quanta is anchored at the first thunder_live_process call (engine frame 0; see TIME). In the web the worklet's blocks are quanta when frame0 is a multiple of 128. One quantum
     runs these NINE STEPS in this order (SongRenderer::quantum, render.cpp:93, is the template; the live host differs where marked NEW):
       1. APPLY COMMANDS (NEW). Pop the command ring into the waiting list; apply, in ring order, every command due in this quantum (see TIME), among them play
          (which first adopts the song snapshot it needs), stop, hold, loop, bpm, lead, perf, stop_track, mix_set, fx_commit, fx_param, meters.
       2. ADOPT (NEW). seq_adopt(song slot, runner) (session.h:100); THEN, if step 1 stopped the play or the Seq was dropped (stop, held-and-dropped, ended-and-dropped),
          PadsHost::seqGone() (pads_snap.h): it MUST come before this quantum's beginQuantum, so that a snapshot may be adopted with a null Seq; THEN
          PadsHost::beginQuantum(players, runner.seq()) (pads_snap.h), EVERY quantum, passing the Seq the runner uses (null when there is none): it publishes the board (the epoch of the
          buffer each voice slot is on), adopts a newly published pads snapshot (Players::setPads) and points the Seq's chop table at the current snapshot. Then the bus plan, the PDC plan and
          the master configuration. The pads adopt is ALWAYS after seq_adopt and before the runner, so that a Seq that has just been adopted gets the current chop table before it steps.
          (The host's sessions are made with opts.chop = nullptr: beginQuantum sets the table.)
       3. CLOCKS. players.setTime(t0, frame), mixer.setTime(t0); t0 = frame / sample_rate.
       4. MIDI BEGIN. midi_begin(midi, t0, 128 / sr, latency, audible mask) (midi_host.h:18). Unconditional, also when no step is due. Then, IMMEDIATELY AFTER it returns: the direct MidiSched::stop / start queued by step 1 (see MIDI).
       5. STEP. runner.pump(t0, lead, ...): the catch-up skip, EXACT STOP, at most 3 steps (runner.h:50). For each step: MidiSched::onStep, then every event of
          the step is dispatched (mixer, beat clock, effects, Players): the order of render.cpp:106-108. NEW against the renderer: the runner, hold, the HELD and ENDED
          reports, the optional step dump.
       6. MIDI END. midi_end(midi, t0, seq time, seq tempo, seq alive) (midi_host.h:22). Unconditional.
       7. SONG OVER. Once, when the sequencer has ended: Players::songOver(seq time); the host stops MIDI 0.3 s later (see MIDI).
       8. SOUND. players.render into the mixer's inputs, mixer.render (tracks, effects, master chain), the meters.
       9. AFTER THE BLOCK. Post the events (clock, position, meters, state ...), publish the statistics. The host then drains MIDI records and, in a shell that runs
          the core's own MIDI pump, calls thunder_midiout_clock_map (midiport.h:119: AFTER the block).

   TIME         THE API SPEAKS HOST FRAMES; THE ENGINE RUNS ITS OWN GAPLESS COUNT (ruling D11 holds at the boundary). A host frame is an int64 on the HOST's timeline (`currentFrame` in the worklet, the callback's running
                count in a shell): never seconds, never a wall clock. The engine counts the frames IT has rendered from 0, and every time it works with (the sequencer, the voices, the mixer, the timers) is
                engine frame / sample_rate; the host's frame of an engine frame is engine + one offset, set by the first thunder_live_process call (engine frame 0 = its frame0) and CHANGED by a re-anchor. So the
                song's audio is bit-for-bit the same whatever frame0 the host starts at (0, 37, 2^31, 10^12).
                A call's frame0 must equal the previous call's frame0 + its frames; a call that does not is a RE-ANCHOR: nothing inside the engine moves (no step is skipped or repeated, no silence, no burst, the
                output across the seam is the uninterrupted run), the offset changes so that this frame0 names the next sample, it is counted in stats.reanchors and reported as an ERROR record (where REANCHOR,
                value = frame0 - the expected frame, may be negative). A forward gap's frames never happen; a backward jump replays host frame numbers. A suspended context that does not advance currentFrame leaves
                no hole. frame0, t0 and `at` above 2^62 are E_ARG (process: E_ARG, the outputs zeroed).
                A command carries `at`: a HOST frame, or THUNDER_LIVE_NOW (-1). It is converted to engine time at the moment it is judged, with the offset then in force: so a timed command that waits across
                a re-anchor is judged against the new mapping, and a command whose `at` fell in a skipped gap is due at the next quantum and applied there. Without `at` / with an `at` in the past it is applied
                at the top of the next quantum. With a future `at` it waits in a list of 64 and is applied at the top of the quantum that CONTAINS `at` (so up to 127 frames early); the commands that carry a time
                on into the sound (stop, stop_track, mix_set, fx_param) use `at` sample-accurately from there. Commands are applied in the order sent (RING ORDER), whatever their `at`: a due command is never
                held behind a later-sent one. A 65th waiting command is applied at once instead and counted (stats.cmd_late; one ERROR record a quantum at most). A STOP and a PLAY CLEAR the waiting list
                (stats.cmds_cleared counts the commands dropped): a timed bpm / loop / perf / stop / mix_set of the old play never lands in the next. play has no `at`: its `t0` (a host frame) is its time.
                Every EVENT record's `f`, stats.frames and stats.frame_origin are HOST frames (converted with the offset in force when the record is posted). The MIDI records of thunder_live_midi_drain are the one exception: they carry the ENGINE frame (see MIDI).

   ERRORS       int functions return 0 or a negative THUNDER_LIVE_E_*; thunder_live_create returns NULL and writes its err text. A failed call changes nothing, unless
                its own comment says so. E_BUSY (a fixed ring is full) changes nothing and loses nothing: THE CALLER OWNS THE RETRY. It must keep the message and call
                again after the next thunder_live_process call has returned (in the web: after the next block; calling again before it cannot help, because only
                process() empties a ring); a caller that drops an E_BUSY message loses the edit. thunder_live_stats.cmd_busy counts them.
   NULL AND DEAD HANDLES  A NULL handle is E_ARG for every function that returns an int (thunder_live_process writes nothing then: it cannot know the buffers' size), NULL for thunder_live_midi, and a no-op for destroy,
                thunder_live_host_stats and thunder_live_free. A DEAD handle (an exception that was not an allocation failure came out of a CONTROL call, which returned E_INTERNAL) answers E_STATE to every call but the getters
                (abi, caps, stats_get, midi, host_stats, poll, alloc, free) and destroy; process zeroes its outputs (when they are valid) and returns E_STATE. A dead handle never runs process again: MIDI notes already sounding on an external port are NOT released by the core, so the shell sends its own all-notes-off (thunder_midi_panic / freeing its thunder_midiout) when it sees E_INTERNAL or E_STATE.
                There is no FATAL record: the audio side cannot post for a handle it
                does not run, and the call that killed the handle said E_INTERNAL. (THUNDER_LIVE_WHERE_FATAL is reserved.)
   OUT OF MEMORY  A CONTROL call NEVER aborts on a failed allocation: it returns E_MEMORY (or E_LIMIT when a budget says no first) and changes nothing. The AUDIO
                path allocates nothing, ever (rt_harness runs it under an allocator that aborts). What a host must still respect: this header's limits and the render.h
                MEMORY figures. Known exception, by construction: in a build WITHOUT C++ exceptions (wasm) the control paths that use std::vector / std::string inside the
                parser (Song::load, SeqVars::load, the pads snapshot's RenderPads copy, the SoundFont parse) cannot catch a failed allocation: there the call may abort. The
                host keeps inside opts.max_song_bytes / max_pad_bytes (E_LIMIT is returned BEFORE any such allocation) and treats a trap as fatal and falls back (ruling D14; docs/port/U37-P0-NOTES.md, question 4). P6 (the wasm target) MUST MEASURE the largest
                song and pad set that stays inside these limits on a phone before the limits are trusted. A native build with exceptions catches them and returns E_MEMORY.
   ABI          Structs that cross the API start with a `size` field (sizeof as the caller compiled it; THE CALLER'S STRUCT MUST BE AT LEAST AS LONG AS `size` SAYS: the core reads `size` bytes of it, a garbage size is a read out of bounds): a field the caller's struct does not reach keeps its default (zero, except thunder_live_play_opts.t0, which is THUNDER_LIVE_NOW when the struct is too short to hold it),
                a field a newer caller has and this build does not know must be zero. Zero-fill, set `size`, set what you want; every zero is the default. Event records
                have fixed sizes, the same on wasm32 and 64-bit (no pointer, no size_t, no long): see EVENTS. A function a build does not have is not exported; the
                caps word says which optional parts the build has.

   NOT IN THIS API  the chains host's sidechain taps and sends, the 20 ms CHN switch ramp, Resample takes and the pump service() driver (C3 / C4: a song that uses them is not yet played by this host; a pump module in a live chain has no spare instance
                until then); the stretch (sync 2 / 3, sx: D10); the U04 drum module (U04's
                seam is not wired; `lxp` has no function); a live swap of pump effects (D1; psetPump exists in C++, but one command would be 1.1 KB); note.on / note.off from
                the host (keys, previews and MIDI thru stay with the page: D16); MIDI in, MIDI clock in, Link, recording, input metering; pad renders, song renders and
                stems (render.h); the live engines other than bass / vox / acid / mach (D8: a pad of another kind stays on the SAMPLER route and plays its rendered twin);
                a live master-chain swap while it does not exist in C++ (THUNDER_LIVE_CAP_MASTER_SWAP, below).
                THIS HEADER IS USED BY THE WEB HOST AND BY EVERY SHELL: nothing in it may assume a page (no DOM, no JSON message names, no wall clock, no Web MIDI).
   THREAD LEGEND    CONTROL: one thread at a time (any thread, never two at once; the same thread that makes the thunder_midi_* / thunder_midiout_* control calls on this
                    host's MIDI handle). AUDIO: thunder_live_process only. ANY: any thread, any time. */
#ifndef THUNDER_LIVE_H
#define THUNDER_LIVE_H

#include <stddef.h>
#include <stdint.h>

#include "thunder/midi.h"      /* thunder_midi, thunder_midi_msg */
#include "thunder/render.h"    /* THUNDER_RENDER_* limits and ROUTE / LIVE kinds, thunder_render_sf_params, thunder_render_live_params (reused, not repeated) */

#ifdef __cplusplus
extern "C" {
#endif

#define THUNDER_LIVE_ABI 1u

#define THUNDER_LIVE_OK 0
#define THUNDER_LIVE_E_ARG THUNDER_RENDER_E_ARG              /* -1  a null, negative, NaN, out-of-range or overlapping argument, or an unknown pad / track / font */
#define THUNDER_LIVE_E_STATE THUNDER_RENDER_E_STATE          /* -2  not allowed now (see each call: no song published, an effects plan still staged, a live-pad change while playing in a build without THUNDER_LIVE_CAP_LIVE_SWAP, a dead handle) */
#define THUNDER_LIVE_E_SONG THUNDER_RENDER_E_SONG            /* -3  the bytes are not a song (or not a vars object) the core can read; the err text says where */
#define THUNDER_LIVE_E_LIMIT THUNDER_RENDER_E_LIMIT          /* -4  a size beyond a limit or the budget (opts.max_pad_bytes, max_song_bytes, the fixed counts below) */
#define THUNDER_LIVE_E_MEMORY THUNDER_RENDER_E_MEMORY        /* -5  an allocation failed (never an abort from a control call; see OUT OF MEMORY) */
#define THUNDER_LIVE_E_INTERNAL THUNDER_RENDER_E_INTERNAL    /* -6  an unexpected failure inside: the handle is DEAD (only destroy, the getters and thunder_live_alloc / free still work) */
#define THUNDER_LIVE_E_FONT THUNDER_RENDER_E_FONT            /* -7  font_add: not a SoundFont the core can read */
#define THUNDER_LIVE_E_BUSY (-8)                             /* a fixed ring is full: nothing changed; retry after the next process call (see ERRORS) */
#define THUNDER_LIVE_E_UNSUPPORTED (-9)                      /* this build lacks the part (no effects library, a master swap not built): see thunder_live_caps */

#define THUNDER_LIVE_QUANTUM 128          /* frames: the quantum (kRenderQuantum) */
#define THUNDER_LIVE_NOW ((int64_t)-1)    /* `at` / t0: as soon as possible */
#define THUNDER_LIVE_PADS THUNDER_RENDER_PADS            /* 256 */
#define THUNDER_LIVE_TRACKS 32            /* tracks 0 .. 31; the master column / master effects / master strip is index 32 (kSongNT) in the calls that say so */
#define THUNDER_LIVE_MASTER 32
#define THUNDER_LIVE_MAX_FONTS THUNDER_RENDER_MAX_FONTS
#define THUNDER_LIVE_MAX_SONG_BYTES THUNDER_RENDER_MAX_SONG_BYTES   /* the ceiling of opts.max_song_bytes (16 MiB) */
#define THUNDER_LIVE_MAX_VARS_BYTES THUNDER_RENDER_MAX_VARS_BYTES   /* 4 MiB */
#define THUNDER_LIVE_MAX_PAD_FRAMES THUNDER_RENDER_MAX_PAD_FRAMES   /* one pad (or variation) at most 2^28 frames */
#define THUNDER_LIVE_DEFAULT_PAD_BYTES ((uint64_t)256 << 20)        /* opts.max_pad_bytes == 0: the computer's budget of ruling D7 (a phone host passes 128 MiB) */
#define THUNDER_LIVE_CMD_RING 256         /* commands the control side can have queued (a fixed ring; the 257th is E_BUSY) */
#define THUNDER_LIVE_CMD_WAIT 64          /* timed commands waiting for their frame */
#define THUNDER_LIVE_FXPARAM_RING 64      /* fx_param messages queued */
#define THUNDER_LIVE_SONG_RING 16         /* song snapshots published and not yet adopted (SnapSlot<SongSnap>::N); the 17th publish is E_BUSY */
#define THUNDER_LIVE_PADS_RING 16         /* pads snapshots published and not yet adopted */

/* create flags */
#define THUNDER_LIVE_F_MIDI 1u            /* the host makes and OWNS a thunder_midi (see MIDI); without it every MIDI function below is E_STATE / NULL and the song's MIDI instruments are not parsed */
#define THUNDER_LIVE_F_EFFECTS 2u         /* the host makes the track and master effects hosts (FxBank). Without it the song's effects are not played and fx_plan / fx_commit / fx_param are E_STATE */
#define THUNDER_LIVE_F_MASTER 4u          /* the host makes the master chain (mastering + safety limiter) and the master effects hook */
#define THUNDER_LIVE_F_METERS 8u          /* the host makes a MeterBank (meters events only while thunder_live_meters_want has switched them on) */
#define THUNDER_LIVE_F_DUMP_STEPS 16u     /* TEST ONLY: thunder_live_dump_step is available (see there); costs a ring of opts.dump_steps steps and a copy of every step's events on the audio thread */
#define THUNDER_LIVE_F_PAGE_TIMING 32u    /* TEST ONLY: voices timed as the web page's player (VoicePool::setPageTiming, "never on in an app"), for the A/B against the JS engine */
#define THUNDER_LIVE_F_CHAINS 64u         /* the host makes a chains host (ChainsHost, live; opts.chains_bytes is its memory budget): the song's chains play (see CHAINS below). create fails ("out of memory") when it cannot be made. Without it the host is as it was: no chain, no bus, thunder_live_chain_edit is E_STATE */

/* caps (thunder_live_caps) */
#define THUNDER_LIVE_CAP_EFFECTS 1u       /* tfx effects can be made (the effects library is linked); fx_plan does not answer E_UNSUPPORTED. Set by P5 when the core is built with THUNDER_HAVE_TFX */
#define THUNDER_LIVE_CAP_MASTER_SWAP 2u   /* thunder_live_master_config works while a song plays (built in C++ by U37 P3, set by P5 in every build) */
#define THUNDER_LIVE_CAP_LIVE_SWAP 4u     /* thunder_live_pad_live may be called while a song plays (the live pool is swapped with the pads snapshot). NOT BUILT, and not set (P5): the live host has no live-engine pads at all yet, see CAP_ENGINES */
#define THUNDER_LIVE_CAP_SF 8u            /* font_add / pad_sf work. NOT SET by P5: LiveHost does not wire SoundFonts yet, font_add / font_remove / pad_sf answer E_UNSUPPORTED whatever their arguments (pad_sf_clear: 0 for a pad in range) */
#define THUNDER_LIVE_CAP_TAKE 16u         /* thunder_live_pad_take is available (PadsHost::setPadTake, built in P2; set by P5 in every build) */
#define THUNDER_LIVE_CAP_CHAINS 0x200u    /* F_CHAINS exists and thunder_live_chain_edit works (bit 9: set by P5 / C2 in every build; bits 5..8 are CAP_ENGINES) */
#define THUNDER_LIVE_CAP_ENGINES 0x1E0u   /* bits 5..8: live engines built: bass 1<<5, vox 1<<6, acid 1<<7, mach 1<<8 (THUNDER_RENDER_LIVE_x kind k: bit 4 + k). NONE is set by P5: pad_live answers E_UNSUPPORTED (pad_live_clear: 0 for a pad in range); a live pad stays on the SAMPLER route (ruling D8) */
/* M1 */ #define THUNDER_LIVE_CAP_METERS2 0x400u   /* bit 10: thunder_live_meters_set, thunder_fx_type_name and the FXLVL / FXDYN records exist (METERS-DESIGN 5a; set in every build; needs F_METERS to be used) */

typedef struct thunder_live thunder_live;

/* ---- 1. LIFE ---------------------------------------------------------------------------------------------------------------------------------------------- */

/* Create options. `size` first. Every zero is its default. opts == NULL: all defaults. */
typedef struct thunder_live_opts {
  uint32_t size;
  uint32_t reserved0;           /* must be 0 */
  double sample_rate;           /* Hz, 8000 .. 384000; 0: 48000. The host's rate: ctx.sampleRate / the device's. NaN or outside: E_ARG */
  uint64_t max_pad_bytes;       /* THE MEMORY BUDGET (ruling D7): pads + variations + fonts (copy and tables) + decoded SoundFont PCM + live-engine nodes, in bytes, INCLUDING buffers retired
                                   but not yet freed (voices still read them). 0: THUNDER_LIVE_DEFAULT_PAD_BYTES. Above 2^31: E_ARG. A call that would pass it returns E_LIMIT and changes nothing */
  uint64_t max_song_bytes;      /* the longest song text song_publish takes; 0: 16 MiB; above 16 MiB: E_ARG. A parse tree is 8.6 to 15.5 times the text (render.h MEMORY): a web host passes less */
  int32_t max_block;            /* the most frames one process call will pass, 1 .. 1048576; 0: 128. A process call above it is E_ARG. The quantum is 128 whatever this says */
  uint32_t flags;               /* THUNDER_LIVE_F_* */
  int32_t max_voices;           /* sampler voices (the SoundFont pool has its own 32); 0: 64; 1 .. 256 */
  int32_t dump_steps;           /* F_DUMP_STEPS: steps the dump ring holds; 0: 256 */
  uint32_t reserved1, reserved2;/* must be 0 */
  uint64_t chains_bytes;        /* APPENDED by C2 (the `size` rule: an older caller's struct ends before it and gets 0): F_CHAINS: the chains host's TOTAL memory budget in bytes (the patches of every node, the swap's second set included: chost.h MEMORY).
                                   0: 1.5 GiB (kChainsHostDefaultBudget); a phone host passes less. A plan that would pass it is refused whole (the song plays with the chains it had, counted: stats.chains_over_budget). Ignored without F_CHAINS */
} thunder_live_opts;

/* ANY. The ABI number this header is (THUNDER_LIVE_ABI) and the optional parts this BUILD has (THUNDER_LIVE_CAP_*): the page's hello / caps. They do not depend on a handle. */
uint32_t thunder_live_abi(void);
uint32_t thunder_live_caps(void);

/* CONTROL (any thread, once, before anything else). Allocates everything the host will ever need EXCEPT what the calls below add (a song, pads, effect objects): the
   voices, the mixer's delay rings (ELECTRO: about 53 MB at 48 kHz without effects, 94 MB with; 143 / 218 MB at 192 kHz: render.h MEMORY), the event buffers (5.6 MB), the rings,
   the MIDI scheduler when F_MIDI. Returns NULL and writes `err` (NUL-terminated, truncated to err_cap; err may be NULL with err_cap 0) on a bad option or no memory ("out of memory"),
   leaving nothing allocated. The first thunder_live_process call on a thread prepares that thread's Plaits / Elements workspace (pe_prepare(): per thread,
   it allocates) and is the one call that may allocate; stats.thread_prepares counts how many times it happened (more than 1: the audio thread changed). The web does it from its first message handler with frames = 0
   (that handler runs on the rendering thread); a shell cannot run anything on the audio thread before the stream starts (the system makes that thread), so it accepts the allocation in its first callback, before any song plays. The state is
   IDLE: no song, no pads, nothing playing, silent. The handle lives until thunder_live_destroy. */
thunder_live* thunder_live_create(const thunder_live_opts* opts, char* err, size_t err_cap);

/* CONTROL, after the audio thread has stopped calling thunder_live_process and no other call is in flight, and (when F_MIDI) after thunder_midiout_free on the handle's MIDI handle.
   Frees everything, whatever the voices say. NULL is fine. Destroy runs NO quantum and books NO MIDI record (nothing could deliver it: the handle is freed with the host), so a shell that must not leave a hung
   note calls thunder_live_stop, calls process once more, drains / pumps the MIDI records and only then destroys (the duty of docs/port/PICKUP.md: "one more quantum before the host is destroyed"). */
void thunder_live_destroy(thunder_live* L);

/* AUDIO. Writes `frames` samples to out_l and out_r (frames 0 .. max_block; each array needs room for `frames` floats; they must not overlap). The frame of the first sample on the
   host's timeline is `frame0` (>= 0; see TIME). `wall_ns`: the host's monotonic clock at the start of this call in nanoseconds (0: not known: the web has no clock in the worklet, and
   a wasm build may import none); it is only copied into the clock event. No allocation, no lock, no I/O, no exception, bounded work: at most 3 steps per quantum, a fixed voice and
   event count. Takes effect: everything queued by the CONTROL calls above is applied in the quanta this call renders (see THE QUANTUM). The outputs are ALWAYS written (zeros
   on an error). Returns 0, E_ARG (a null handle or output, frames outside 0 .. max_block, frame0 < 0, overlapping buffers; with a NULL handle nothing is written) or E_STATE (a dead handle: silence).
   A re-anchor is not an error (see TIME). Stack use: the deepest path puts about 12 KB (an effects commit) + 7 KB (an LFO step) on the stack. */
int thunder_live_process(thunder_live* L, float* out_l, float* out_r, int frames, int64_t frame0, int64_t wall_ns);

/* ANY. A snapshot of the counters (all since create unless it says otherwise). The audio side's numbers are published once a quantum with relaxed stores: each is a
   valid value, the set is not one instant. `size` first (in: sizeof as the caller compiled it; a field past it is not written). */
#define THUNDER_LIVE_STATE_IDLE 0        /* no sequencer exists and the MIDI scheduler is stopped; voices already sounding may still ring out. Also the state before the first play */
#define THUNDER_LIVE_STATE_PLAYING 1     /* a sequencer is stepping */
#define THUNDER_LIVE_STATE_ENDED 2       /* the sequencer ran out (ENDED record); the MIDI stop follows 0.3 s later and then the state is IDLE */
#define THUNDER_LIVE_STATE_HELD 3        /* EXACT STOP: the sequencer stopped at a pattern edge (HELD record); voices ring out and MIDI keeps running until thunder_live_stop */
/* THE STATE MACHINE. Each cell: state after the event is APPLIED (at a quantum top; the CONTROL call only queues it), then the record posted. STOPPED is a record, not a state: it announces the move to IDLE.
   Hold only arms a flag of the runner (SeqRunner::hold); a held edge, the song's end and the 0.3 s timer are events of the audio side.
     event \ state                 IDLE                   PLAYING                  HELD                     ENDED
     play                          PLAYING, PLAYING       PLAYING, PLAYING         PLAYING, PLAYING         PLAYING, PLAYING   (a new session, Seq and MIDI Start every time; voices are NOT cut; the ENDED timer is cancelled)
     stop                          IDLE, none             IDLE, STOPPED arg 0      IDLE, STOPPED arg 0      IDLE, STOPPED arg 0   (stopAll + MIDI stop; from IDLE the MIDI stop is repeated, harmless)
     hold                          IDLE, none (armed)     PLAYING, none (armed)    HELD, none (no-op)       ENDED, none (armed, no effect)
     a step at an edge, hold armed -                      HELD, HELD (positions)   -                        -
     the sequencer runs out        -                      ENDED, ENDED             -                        -                     (Players::songOver; arms the 0.3 s timer)
     0.3 s after ENDED             -                      -                        -                        IDLE, STOPPED arg 1   (MidiSched::stop; the 0.3 s runs from the SONG's end, max(quantum start, end time), not from the quantum that found the end)
   A play on a host that is PLAYING or HELD does not stop the old song's voices or its MIDI first (the page stops first, as tStop does). */
typedef struct thunder_live_stats {
  uint32_t size;
  int32_t state;                /* THUNDER_LIVE_STATE_*, as of the last quantum */
  int64_t frames;               /* the frame the next quantum starts at (host timeline); 0 before the first call */
  int64_t frame_origin;         /* the frame0 the quantum grid is anchored at (the first call's, or the last re-anchor's) */
  uint64_t quanta, calls;       /* quanta rendered; process calls */
  uint64_t steps, events;       /* sequencer steps played; events dispatched */
  uint64_t seq_dropped;         /* events a step could not hold (Seq::dropped); the tests assert 0 */
  uint64_t runner_skips;        /* catch-up skips of the runner (a held-up audio thread) */
  uint64_t cmds_applied;        /* commands applied (the ring's and the waiting list's) */
  uint64_t pad_bytes_held;      /* what the budget counts: current + in snapshots + retired buffers still waiting for their voices */
  uint64_t pad_bytes_retired;   /* of those, retired: freed by thunder_live_collect once no voice of their pad reads them */
  uint64_t pad_budget;          /* opts.max_pad_bytes as made */
  uint64_t session_bytes;       /* the PLAY's SeqSession registries (roll pages, counters, ids): bytes */
  uint64_t dump_dropped;        /* F_DUMP_STEPS: steps the dump ring refused (a test that sees > 0 is invalid) */
  int32_t latency_frames;       /* the master chain's look-ahead + the PDC delay: what the output is later than the song's time (a render trims it) */
  int32_t voices;               /* voices sounding: sampler + SoundFont + live engines */
  int32_t peak_voices;
  int32_t notes, notes_played, notes_late, notes_no_pad, notes_live, notes_sf, notes_var_missing, notes_stretched, events_ignored;   /* PlayerStats: as RenderStats */
  int32_t pad_buffers;          /* PadBufs alive (current, in snapshots, retired) */
  int32_t pad_retired;          /* of those, retired */
  uint32_t cmd_busy;            /* E_BUSY answers given (control side) */
  uint32_t cmd_late;            /* timed commands applied at once because the waiting list was full */
  uint32_t evt_dropped;         /* records any ring refused (the oldest is kept; CLOCK records carry this counter) */
  uint32_t reanchors;           /* process calls whose frame0 was not the expected one */
  uint32_t song_pending;        /* snapshots published and not yet adopted (0 .. THUNDER_LIVE_SONG_RING) */
  uint32_t session_full;        /* 1: SeqSession::sessionFull(): the PLAY's registries passed 8 MB; a new play starts a fresh session (see song_publish) */
  uint32_t fx_remakes;          /* pump instances made in the audio thread (FxHost::audioRemakes): the one allocation of pump slots */
  uint32_t fx_failed_blocks;    /* effects blocks that failed soft (silence) */
  uint32_t midi_begin_failed;   /* midi_begin returned < 0 (cannot happen with finite arguments): counted; the first one also posts thunder_midi_panic */
  uint32_t midi_may_hang;       /* the MIDI scheduler's may_hang (thunder_midi_stats): the shell should panic */
  uint32_t live_failed;         /* live nodes whose core refused: silent from then on */
  uint32_t live_late;           /* live events that arrived inside the look-ahead */
  uint32_t thread_prepares;     /* times a process call prepared its thread's workspace (1 normally; more: the audio thread changed; 0 in a build that has no per-thread Plaits / Elements workspace to prepare) */
  uint32_t evt_crit_dropped;    /* critical records (STATE, HELD, ERROR, SONG, PADS) the critical ring refused: see EVENTS; also counted in evt_dropped */
  uint32_t cmds_cleared;        /* waiting timed commands dropped by a stop or a play (see TIME) */
  uint32_t cmds_no_seq;         /* bpm / loop / perf commands applied with no sequencer running: dropped and counted here (they are also in cmds_applied) */
  /* APPENDED by C2 (the `size` rule; all zero without F_CHAINS). The audio side's, published once a quantum like the rest: */
  uint64_t chains_blocks;       /* node blocks run (one per node per quantum) */
  uint64_t chains_failed_blocks;/* blocks in which the engine failed: that track's output is silence for the block (the buses are cleared, as the worklet's catch does) */
  uint64_t chains_events;       /* chc / tempo events applied to the nodes */
  uint64_t chains_events_dropped;/* events that found the host's 4096-event queue full (the newest is dropped) */
  uint64_t chains_events_bad;   /* events with a track / switch / time / value that cannot be applied */
  uint64_t chains_chc_no_node;  /* chc events for a track that has no node (the page: nothing) */
  uint64_t chains_feed_mismatch;/* a feed the mixer handed a node whose track is not the node's bus map's j-th: cannot happen with a plan from the song; the tests assert 0 */
  /* The control side's (relaxed atomics of the host: any thread reads them): */
  uint32_t chains_nodes;        /* nodes the audio side has now (as of the last quantum) */
  uint32_t chains_plans;        /* plans handed to the audio side (a play's or a publish's) */
  uint32_t chains_refused;      /* plans the host refused whole (the engine refused the patch, or it was over budget): the call SUCCEEDED, the song plays with the chains it had (none: dry) */
  uint32_t chains_over_budget;  /* of those, refused for the memory budget */
  uint32_t chains_busy;         /* plan calls answered E_BUSY because a plan was still in flight */
  uint32_t chains_params_stale; /* parameter records the audio side DROPPED because a whole-patch plan (a swap) was committed after they were pushed: the swap carries every value of the song (C2 V3a) */
} thunder_live_stats;
int thunder_live_stats_get(const thunder_live* L, thunder_live_stats* out);

/* ANY thread, wait-free. THE ENGINE OFFSET: *out = (host frame) - (engine frame), the one number that maps the engine's own timeline to the host's (see TIME and MIDI). It is ONE 64-bit atomic that the audio side stores in
   thunder_live_process when it changes (the first call: that call's frame0; every re-anchor, in the call that makes it, even one that completes no quantum), so a read is never torn and never stale for a call that has returned.
   0 before the first process call with frames > 0. May be negative (a backward re-anchor). Returns 0, or E_ARG (NULL handle or out). Works on a dead handle. (it is its own call: two relaxed counters in stats_get could tear it by one quantum.) */
int thunder_live_engine_offset(const thunder_live* L, int64_t* out);

/* ANY. A block of `nfloats` floats (> 0, <= 2^30) from the host's own allocator (`new float[]`: aligned as operator new[] is for float, which is at least alignof(float) and 16 bytes on the 64-bit targets; nothing in the core needs more than alignof(float)), uninitialised, for thunder_live_pad_take (the worklet writes a pad into it and hands it over,
   saving a copy). NULL when it cannot be had (never an abort). Valid until it is given to thunder_live_pad_take (which takes it) or thunder_live_free. It does NOT count against the budget until taken. */
float* thunder_live_alloc(int64_t nfloats);
void thunder_live_free(float* block);   /* ANY. NULL is fine. Only for a block from thunder_live_alloc that was not (successfully) taken */

/* ---- 2. SONG ---------------------------------------------------------------------------------------------------------------------------------------------- */

/* CONTROL. Allocates (parses: a tree of 8.6 to 15.5 times `song_bytes`; builds the Song, the vars, the bus plan; when F_MIDI hands the song's MIDI instruments to the scheduler, which
   parses it again; that scheduler holds at most SIX songs in flight, the audio side frees them as it adopts: SIX publishes with no quantum between (a suspended device) are accepted and the SEVENTH returns E_BUSY with nothing changed, so a shell that lets
   the user edit while the device is suspended retries after audio has run. The scheduler's post is the LAST thing a refused publish could fail on: after ANY returned error the host and the scheduler hold the same song). Publishes the WHOLE song text (ruling D3; there is no patch: Song::load is the only entry, with pageS = true: the text is the page's own S) and its vars
   ({"vtrk":..,"NSLOT":..,"TFX":{..}}; vars == NULL, vars_bytes 0: the defaults). Both are copied; the caller's bytes are free on return. Neither needs a NUL.
   `rev`: the caller's revision number (any int64), echoed in the SONG record when the audio side adopts it.
   WHAT HAPPENS, by state.
     IDLE (no play issued, or stop issued): the host keeps the new Song as the one the next play will use. Nothing is built for the audio side, no session is touched, the call is cheap.
       The SONG record is still posted, at the next quantum.
     PLAYING (a play was issued and no stop): the new Song goes to the PLAY's SeqSession as a SUCCESSOR snapshot (SeqSession::publish, session.h:51) and the audio side adopts it at
       the top of the next quantum (step 2; the Seq that was running hands its state to it: Seq::takeOver). A note already booked is not re-timed (the web has the same limit).
       The bus plan built from the song's chains patch is published beside it and adopted at the same quantum boundary or the next (never earlier than the song); WITH F_CHAINS the mixer's bus plan is set by the chains host's commit alone (see CHAINS) and this one is only freed.
     A PUBLISH WHILE THE PREVIOUS ONE IS NOT ADOPTED YET is queued: up to THUNDER_LIVE_SONG_RING (16) snapshots may wait; at the next quantum top the audio side adopts only the NEWEST;
       the older ones are retired unrun and freed by thunder_live_collect; ONE SONG record, for the newest rev, covers every lower rev adopted in that quantum (core.h's rule). The 17th
       waiting publish is E_BUSY and nothing changed (the registry growth it staged is rolled back).
     SESSION FULL: the PLAY's SeqSession only grows (slots are never reused). When session.bytes() passes kSeqSessionMaxBytes (8 MB) the publish STILL SUCCEEDS; stats.session_full becomes 1
       and an ERROR record (where SESSION_FULL, non-fatal) is posted once. Nothing ends the session by itself; the NEXT PLAY starts a fresh one (its counters restart there). The host
       cannot restart it mid-play without a seam (docs/port/SEQ-TAKEOVER-BUILD-NOTES.md "Choices for the lead": options a / b / c; this contract implements (b)).
   Returns 0, E_ARG (null / empty song, vars_bytes without vars), E_LIMIT (song_bytes > opts.max_song_bytes, vars_bytes > 4 MiB), E_SONG (not a song / vars object; `err` says where, as
   thunder_render_create's; err may be NULL), E_MEMORY, E_BUSY (above). The old Song stays in force on every error. The Song and its snapshots live until adopted, replaced and collected. */
int thunder_live_song_publish(thunder_live* L, const uint8_t* song, size_t song_bytes, const uint8_t* vars, size_t vars_bytes, int64_t rev, char* err, size_t err_cap);

/* ---- 3. PADS ---------------------------------------------------------------------------------------------------------------------------------------------- */
/* THE FLOW. The pad calls STAGE changes on the control side; nothing is audible until thunder_live_pads_publish builds ONE immutable snapshot of everything staged
   (PadsHost::publish, pads_snap.h) and hands it over; the audio side adopts it at the top of a quantum (step 2). A pad that did not change is shared between snapshots, not copied.
   Voices already sounding keep the buffer they started on; a buffer nobody refers to is retired and freed by thunder_live_collect only when no voice slot's board entry names ITS epoch
   (pads_snap.h, ruling D5 refined: exact, per buffer and per voice slot; a long sample on one pad holds only its own buffer). thunder_live_create publishes an EMPTY pads snapshot
   (PadsHost::create(players)), so the pads exist, empty, from the first quantum. A pad whose audio changes LOSES its variations and its chop data (render.h's rule): stage the pad first, then its variations, the chop and the hits.
   The pad calls may be made while a song plays. THE BUDGET: each call that adds bytes is judged BEFORE it allocates against opts.max_pad_bytes with stats.pad_bytes_held (which counts retired
   buffers still waiting for their voices): E_LIMIT, nothing changed, the old pad still current. A replaced pad's old bytes still count until collect frees them, so E_LIMIT can be transient: collect, wait
   for the voices, retry (stats.pad_bytes_retired says how much is waiting). */

/* CONTROL. Allocates. Pad `pad` (0 .. 255) = `frames` samples of l, and of r for a stereo pad (r == NULL: mono), recorded at `rate` Hz (finite, 1 .. 1e6). The samples must be finite (else E_ARG).
   COPIED: the caller's arrays are free on return. frames == 0 removes the pad (l and r may be NULL; its notes are silent). 1 .. THUNDER_LIVE_MAX_PAD_FRAMES frames.
   The same samples, rate and (no) variations as the pad already has: nothing changes. Takes effect at the next thunder_live_pads_publish + the quantum that adopts it.
   Errors: E_ARG, E_LIMIT (budget, frames), E_MEMORY. */
int thunder_live_pad_set(thunder_live* L, int pad, const float* l, const float* r, int64_t frames, double rate);

/* CONTROL. As pad_set, but TAKES OWNERSHIP of `block`, which must come from thunder_live_alloc and hold `frames` floats (mono) or `2 * frames` (stereo: all of L, then all of R), saving the copy
   (section 3.3 of the draft: three copies of a pad per load, one fewer). On 0 the block belongs to the host and must not be touched or freed by the caller. On ANY error the block is STILL the
   caller's (free it or retry). The samples are checked in place (finite). Needs THUNDER_LIVE_CAP_TAKE (PadsHost::setPadTake, new); without it E_UNSUPPORTED and the block is the caller's.
   The identical-data shortcut of pad_set does not apply (the new block is kept, the old pad retired). */
int thunder_live_pad_take(thunder_live* L, int pad, float* block, int stereo, int64_t frames, double rate);

/* CONTROL. The kit entry of a pad (the page's coreKitV(): {choke, syn, rel}); defaults 0, 0, 0. choke 0 .. 255; syn 0 / 1; rel finite 0 .. 60 s. No allocation beyond the next publish. */
int thunder_live_pad_kit(thunder_live* L, int pad, int choke, int syn, double rel);

/* CONTROL. Which player a pad's notes are for: THUNDER_RENDER_ROUTE_SAMPLER 0 / _LIVE 1 / _SF 2 (render.h: all its rules about takeovers and silent pads hold). A live pad of a kind
   that is not built plays its rendered twin only if the host leaves the pad on SAMPLER (ruling D8): the host decides, the core does not fall back by itself. E_ARG for a bad pad / route. */
int thunder_live_pad_route(thunder_live* L, int pad, int route);

/* CONTROL. Allocates. A MUT / GEN variation the host rendered (render.h set_variation: key ii 0 .. 63, kind 'g' / 'm', num 0 .. 255, pad 0 .. 255, `frames` MONO samples of l at `rate`).
   COPIED. The same key replaces; frames == 0 removes. At most THUNDER_RENDER_MAX_VARIATIONS (4096); the budget counts it. Stage the pad first (its audio change drops its variations).
WHAT THE CALLER MAY RELY ON: the host STAGES the variation (a copy, held until the next publish) and builds the pad's snapshot ONCE at thunder_live_pads_publish, so a pad with any number of variations costs ONE copy
   of the pad at publish (plus the variations' own samples, which exist twice between this call and the publish: set all of a pad's variations, then publish once). The budget counts the staged copy at once. Errors: E_ARG (also: the pad has no audio staged), E_LIMIT, E_MEMORY. */
int thunder_live_var_set(thunder_live* L, int ii, int kind, int num, int pad, const float* l, int64_t frames, double rate);

/* CONTROL. The chop data of a pad (render.h CHOP DATA): pad_chop: the pad's loop length in seconds (finite, > 0, <= 1e6; 0 removes); pad_hits: with the instrument's SMODE on, the slice points
   of the pad cut into n_slices (2 .. 64) parts: `count` (1 .. n_slices) seconds each finite in 0 .. 1e6 (count 0 removes). Staged in the snapshot's SeqChop; the Seq reads the new table from the
   next step after adoption (Seq::setChop). Events already stepped (up to `lead` ahead) were cut with the old table. Allocates (hits). Errors: E_ARG, E_MEMORY. */
int thunder_live_pad_chop(thunder_live* L, int pad, double loop_secs);
int thunder_live_pad_hits(thunder_live* L, int pad, int n_slices, const double* pts, int count);

/* CONTROL. SOUNDFONTS (render.h SOUNDFONT PADS: all its rules, limits and the preset fall-back hold; it needs THUNDER_LIVE_CAP_SF, else E_UNSUPPORTED). font_add: the file's bytes (SF2 / SF3) are COPIED and
   parsed: returns the font id 0 .. 15 or a negative code (E_ARG, E_LIMIT, E_FONT, E_MEMORY). font_remove: E_ARG for an unknown id or a font a pad still uses. pad_sf: pad plays font_id preset
   (bank, prog) with `params` (NULL: defaults) and the `keys` list (NULL: every zone); the preset's samples are decoded here, off the audio path, into float32: ON THE WEB THIS IS A LONG STALL
   OF THE AUDIO THREAD (ruling D9: the first release falls back to the JS core for SoundFont songs; the API exists so that the shells and the later Worker route have it). pad_sf does not
   change the route: set THUNDER_RENDER_ROUTE_SF too. pad_sf_clear frees the pad's preset (0 if none). Staged; audible after pads_publish. The decoded PCM and the font count against the budget. */
int thunder_live_font_add(thunder_live* L, const uint8_t* bytes, size_t n);
int thunder_live_font_remove(thunder_live* L, int font_id);
int thunder_live_pad_sf(thunder_live* L, int pad, int font_id, int bank, int prog, const thunder_render_sf_params* params, const uint8_t* keys, size_t nkeys);
int thunder_live_pad_sf_clear(thunder_live* L, int pad);

/* CONTROL. LIVE-ENGINE PADS (render.h LIVE-ENGINE PADS: kinds BASS 1, VOX 2, ACID 3, MACH 4; the voice JSON is copied and parsed; params->track_mask). The nodes are made HERE (a vox pad: 235 KB a masked
   track; they count against the budget); nothing is allocated at a note. They do not change the route: set THUNDER_RENDER_ROUTE_LIVE too. THE CATCH: LivePool::addPad is a control call that today
   must not overlap the audio thread (live_pool.h:71). With THUNDER_LIVE_CAP_LIVE_SWAP the new pool travels with the next pads snapshot and the audio side swaps to it at the quantum top: live voices
   sounding at that moment are cut (released), the sampler's voices are not touched. WITHOUT the cap these calls are E_STATE while the state is not IDLE (and in IDLE they act at once; a threaded shell must also have no process call in flight: Players renders its live pool every quantum, so the cap is what makes the call safe against a running audio thread): the page then
   changes live pads only between plays. Errors: E_ARG, E_LIMIT, E_STATE, E_MEMORY, E_UNSUPPORTED (a kind not built: see THUNDER_LIVE_CAP_ENGINES). pad_live_clear removes the pad's nodes (0 if none). */
int thunder_live_pad_live(thunder_live* L, int pad, int kind, const uint8_t* voice_json, size_t n, const thunder_render_live_params* params);
int thunder_live_pad_live_clear(thunder_live* L, int pad);

/* CONTROL. Allocates (builds the snapshot: PadsHost::publish). Makes everything staged since the last publish audible: the audio side adopts it at the top of the next quantum and posts a PADS
   record (the snapshot's counter). Nothing staged: 0, nothing published. Errors: E_BUSY (THUNDER_LIVE_PADS_RING snapshots wait: collect and retry), E_MEMORY (nothing published, the staging is kept: retry),
   E_STATE (a dead handle). The snapshot lives until a newer one is adopted and collected. */
int thunder_live_pads_publish(thunder_live* L);

/* CONTROL. Frees what the audio side has given back: retired song snapshots (and, with them, sessions nobody refers to any more), retired pads snapshots, pad buffers whose voices have ended (the epoch
   rule), retired effects plans, spent play plans. Returns the number of objects freed (>= 0), or a negative code (E_STATE: dead). EVERY allocating CONTROL call above calls it first, but a host that makes none
   for a while must call it itself every few milliseconds (a worklet: once per message handler or per 128 blocks): the audio side adopts a new snapshot only while the return ring has room, so a control
   thread that never collects stalls adoption (nothing is lost; the old snapshot keeps playing). No audio work; bounded by what is waiting. */
int thunder_live_collect(thunder_live* L);

/* ---- 4. TRANSPORT ----------------------------------------------------------------------------------------------------------------------------------------- */
/* Each is a command: validated and queued by the CONTROL call, applied by the audio side at the top of a quantum (see TIME). E_BUSY: the ring (THUNDER_LIVE_CMD_RING) is full; see ERRORS. */

#define THUNDER_LIVE_MODE_SONG 0        /* SeqMode::Song */
#define THUNDER_LIVE_MODE_SECT 1
#define THUNDER_LIVE_MODE_PHRASE 2

typedef struct thunder_live_play_opts {   /* the Seq's options (SeqOpts, seq.h) as a PLAY gives them, and what the host needs to start */
  uint32_t size;
  uint32_t reserved0;           /* must be 0 */
  int64_t t0;                   /* the frame the sequencer's first step is due at, on the host's timeline (the page: ctx.currentTime + 0.08 + count-in, as frames; ruling D-1). < 0: the next quantum's start + 0.08 s */
  uint64_t only;                /* has_only: bit t = track t plays (BUS PLAY) */
  int32_t mode;                 /* THUNDER_LIVE_MODE_* */
  int32_t row;                  /* the row PLAY starts on (below the top: CHASE) */
  int32_t track, sect, phrase;  /* sect / phrase mode (sect, phrase -1: none) */
  int32_t has_loop, loop_y0, loop_y1;   /* a loop of song rows / chain rows / steps by mode */
  int32_t has_seed;             /* 1: `seed` is this PLAY's chance seed (o.seed) */
  uint32_t seed;
  uint32_t random_seed;         /* the stream that stands for Math.random (ruling D17): the page sends a Date.now()-derived value; tests pass 0x2545F491 (the golden seed). 0 is taken as the golden seed */
  int32_t has_only;
  int32_t spp;                  /* Song Position Pointer in 16ths for the MIDI Start: 0 .. 16383; -1: the host computes it from `row` (midi_spp_for: it STEPS A SEQ, allocates, control-side) */
  int32_t pdc_mode;             /* latency compensation: 0 off, 1 auto, 2 on (pdc_mode_of); the plan is built here from the song's effects */
  int32_t test_once, test_sim;  /* TEST ONLY (the goldens of tools/seq-golden.mjs are made with once / sim on): SeqOpts::once / sim. 0, 0: a live play's (loops; no sim). A product leaves them 0: nonzero is E_ARG unless the host was created with F_DUMP_STEPS */
} thunder_live_play_opts;

/* CONTROL. Allocates (a fresh SeqSession for this PLAY and its starting Seq, the Players-side plans, the effects plans of the song, the master chain's configuration, the PDC plan) and uses the LAST
   PUBLISHED song and vars. Then queues the play command (BEFORE it publishes the snapshot, and the audio side holds every adoption back until the play that owns the new session has started: an older play's runner never meets the new session's unstarted Seq); the play CLEARS the waiting list of timed commands when it starts (stats.cmds_cleared); at the top of the next quantum the audio side (1) adopts the new snapshot (the runner starts on its Seq: SeqRunner::start; a Seq that was running is
   dropped, its voices are NOT cut: the page stops first, or calls stop_track / stop), (2) Seq::setTime(t0), (3) mixer.load(song) at once (every level as the song has it, no ramp) + songLanesReset + clockStop,
   (4) commits the song's track and master effects, the master chain configuration and the PDC plan, (5) starts the MIDI scheduler (MidiSched::start(spp, t0), D-1: called by the audio side right after midi_begin of the same quantum (see MIDI), so the Start never lands after the first step), (6) seeds the tempo consumers from Seq::bpm() (SEV_TEMPO is not emitted at the start: seq.cpp:995 emits it only on a change: the mixer's beat clock, FxBank::setBpm, LivePool::tempo), (7) posts a STATE record PLAYING. The seed is applied: Seq options as given, `chop` = the current pads snapshot's table (set again every
   quantum by PadsHost::beginQuantum). The host's own rules (ruling D-4): Players::setDropLate(true) is set once at create: a note more than 0.15 s late is not played (counted in stats.notes_late).
   NO `at`: t0 is the time. A t0 already past is caught up by the runner (skip). A t0 more than `lead` ahead waits (the runner steps when the next step is within lead).
   Errors: E_STATE (no song published; an effects plan staged by fx_plan and not committed; a dead handle), E_ARG (bad field), E_LIMIT (a song that needs more nodes / voices than the budget), E_MEMORY,
   E_BUSY (ring full: the SeqSession built for it is discarded; retry rebuilds it). The caller's `o` is copied. */
int thunder_live_play(thunder_live* L, const thunder_live_play_opts* o);

/* CONTROL. STOP. Queues the stop. Applied at the quantum containing `at` (THUNDER_LIVE_NOW: the next), and it CLEARS the waiting list of timed commands (stats.cmds_cleared): SeqRunner::stop; Players::stopAll(at) (every voice of every pool lets go at `at`, booked notes are taken back);
   mixer.songLanesReset() + clockStop(at); the Seq is dropped and PadsHost::seqGone() is called at the top of the next quantum, before beginQuantum (step 2); the MIDI scheduler is stopped (MidiSched::stop(t0): all notes off, CLEAR, clock Stop: D-2; applied right after midi_begin of the quantum that holds `at`, with that QUANTUM'S START as its time, so up to 127 frames (2.6 ms at 48 kHz) before `at`, while the voices stop at `at`; see MIDI); a STATE record STOPPED. The host goes IDLE (THE STATE MACHINE). Effects tails are NOT cut.
   A song_publish issued after this call returns no longer reaches the sequencer. Stop on an IDLE host: 0, harmless (it still stops MIDI). The silence never waits for the core: a host also mutes its own output. */
int thunder_live_stop(thunder_live* L, int64_t at);

/* CONTROL. EXACT STOP: arms SeqRunner::hold() (runner.h:37) at `at`. The first step at a pattern edge (a new song row, or the same row again) is not played; the runner drops the sequencer and the audio side posts a
   HELD record with that step's time and the positions of the tracks, and the state becomes HELD. Voices ring out. The page then decides (usually: thunder_live_stop). Armed with no sequencer: the flag is set anyway. */
int thunder_live_hold(thunder_live* L, int64_t at);

/* CONTROL. The loop (with no sequencer running the command is dropped and counted in stats.cmds_no_seq) (Seq::setLoop, seq.h:146: rows clamped to the mode's range): has 0 / 1, y0 .. y1. Applied at `at`; the sequencer takes it at its next step. E_ARG: y0 > y1 or negative. */
int thunder_live_loop(thunder_live* L, int has, int y0, int y1, int64_t at);

/* CONTROL. The tempo (with no sequencer running the command is dropped and counted in stats.cmds_no_seq) (Seq::setBpm: a value that is not finite or not > 0 is E_ARG here; one finite and positive is clamped to [1e-3, 1e6] by the Seq and counted in Seq::badSets). Heard from the Seq's next step after `at`. */
int thunder_live_bpm(thunder_live* L, double bpm, int64_t at);

/* CONTROL. The sequencer's look-ahead in seconds, 0.05 .. 2 (SongRenderer::lead): the page's 0.25, 0.6 for 1.5 s after PLAY, up to 1.2 hidden. NOT the MIDI outbox's (thunder_live_midi_lead). */
int thunder_live_lead(thunder_live* L, double seconds, int64_t at);

/* CONTROL. PERFORM's live state (with no sequencer running the command is dropped and counted in stats.cmds_no_seq): what = THUNDER_LIVE_PERF_XF (v: the crossfader, finite), _MACRO (index 0 .. 7, v finite), _FILL (v 0 / 1). Heard from the Seq's next step after `at`. */
#define THUNDER_LIVE_PERF_XF 0
#define THUNDER_LIVE_PERF_MACRO 1
#define THUNDER_LIVE_PERF_FILL 2
int thunder_live_perf(thunder_live* L, int what, int index, double v, int64_t at);

/* CONTROL. RESET TRACK (Players::stopTrack, players.h:47: the track's voices in the sampler, SoundFont and live pools let go over 4 ms at max(now, at); a voice booked later never sounds). The page
   republishes the song FIRST (a song_publish), then calls this. track 0 .. 31. */
int thunder_live_stop_track(thunder_live* L, int track, int64_t at);

/* ---- 5. MIXER --------------------------------------------------------------------------------------------------------------------------------------------- */

#define THUNDER_LIVE_MIX_VOL 0     /* MixKey::Vol (track 0 .. 31; 32 = MASTER VOL) */
#define THUNDER_LIVE_MIX_TRIM 1
#define THUNDER_LIVE_MIX_PAN 2
#define THUNDER_LIVE_MIX_MUTE 3    /* value 0 / 1 */
#define THUNDER_LIVE_MIX_SOLO 4    /* value 0 / 1 */
#define THUNDER_LIVE_MIX_OUT 6     /* MixKey::Out (track 32 only: OUT). 5 (Send) is not a thing this mixer has: E_ARG */

/* CONTROL. mix.set (CORE-BRIDGE section 3): Mixer::set(MixSet) (mix.h:80-90). track 0 .. 32. THE VALUE RANGE IS PER KEY, as the mixer takes it (mix.h:84 "00..FF"; mix.cpp mix_curve, mix_track_gain,
   mix_trim_gain, mix_pan): VOL, TRIM, OUT 0 .. 255 (finite; the mixer clamps); PAN 0 .. 255 with 128 the centre (mix_pan = clamp((v - 128) / 127, -1, +1): a left pan is BELOW 128, never negative); MUTE, SOLO 0 or 1.
   Anything else is E_ARG. MUTE and SOLO are tracks 0 .. 31 only; TRIM and PAN too; the master has VOL and OUT. `ramp` in FRAMES: < 0: the page's law (a 12 ms approach; MASTER VOL jumps), 0: at once,
   > 0: linear over that many frames (the MUTE crossfade). Applied at the quantum containing `at` (a host frame), sample-accurately from there (MixSet::at = engine time of `at`). Mute / solo re-aim the audible
   mask the MIDI scheduler reads each block (gear goes quiet). The same change must also be in the next song text (the Seq reads S.mix for TVA / TPN): the host does not copy it there. Errors: E_ARG, E_BUSY. */
int thunder_live_mix_set(thunder_live* L, int track, int key, double value, double ramp, int64_t at);

/* CONTROL. Allocates. THE PDC PLAN (latency compensation; pdc_plan + Mixer::setDelays, mix.h:117): builds the plan from the song's bus plan and the latency of the effects plans as last COMMITTED (or, for a plan
   staged and not yet committed, as planned), with `mode` (0 off, 1 auto, 2 on), and publishes it; the audio side adopts it at the top of the next quantum and applies it with a 50 ms glide (10 ms when a play starts
   the same quantum); Auto is in force only while PLAYING. It is NOT recomputed by any other call: the page calls it after an fx_commit that changes a pump slot, after a song_publish that changes the chains patch,
   and when the mode changes (play builds it itself). The BUS PLAN has no call of its own: song_publish builds it (bus_plan(song.chainPatch, song.chainOn)) and publishes it beside the song snapshot (a bus EXISTS only
   with a chains host: ruling D1, F_CHAINS; without one the plan only gives solo its chains-aware audible mask). Errors: E_ARG, E_MEMORY, E_BUSY (a plan is waiting; retry), E_STATE (no song published). */
int thunder_live_pdc_set(thunder_live* L, int mode);

/* ---- 6. EFFECTS AND MASTER -------------------------------------------------------------------------------------------------------------------------------- */
/* Need F_EFFECTS (else E_STATE). tfx effects only (ids 0 .. 33 and the JS cores 93 .. 103: fxhost.h); a pump effect (kind 200 and up) is hosted too (FX1: PumpSlot), but a pump slot has no FXV / fx_param yet (a slot whose
   engine is missing is left out, as the JS leaves it out: the sound passes). The track is 0 .. 31 or THUNDER_LIVE_MASTER (the master effects host). */

/* CONTROL. Allocates (makes the effect objects: Fx::operator new is malloc). `slots_json`: the song's slot list for the track (S.tfx[t] / S.mfx: up to 8 objects or null), exactly as FxHost::planFromSong
   reads it (fxhost.h:130), `bpm` the tempo the tempo-locked effects are mapped for. Stages a plan for the track; it is NOT audible until thunder_live_fx_commit. At most ONE staged plan per track: a second fx_plan for a
   track whose plan was not committed is E_STATE (FxHost reuses objects of the previous plan: every plan must be committed, in order). E_UNSUPPORTED: no effects library in this build. Errors: E_ARG, E_STATE, E_MEMORY, E_LIMIT (n above 4 MiB: judged before the parse). */
int thunder_live_fx_plan(thunder_live* L, int track, const uint8_t* slots_json, size_t n, double bpm);

/* CONTROL. Commits the staged plan of the track: queued, and at the top of the quantum containing `at` the audio side calls FxHost::commit (fxhost.h:134): slots in the plan's order, objects of equal key kept (no
   state lost), the others retired. THE OLD PLAN COMES BACK: the plan object is returned through a ring and freed by thunder_live_collect, never on the audio thread. A pump slot may pass the sound for one block.
   E_STATE: nothing staged for the track. E_BUSY: ring full (the plan stays staged; retry). */
int thunder_live_fx_commit(thunder_live* L, int track, int64_t at);

/* CONTROL. One parameter set of a tfx slot: the 12 mapped parameters of the slot (FxHost::psetAt, fxhost.h:135: applied by the first block that starts within 64 samples of `at`; `p12` is what fx_map gives for the
   controls; the page's {pset}). slot 0 .. 7. `p12` is copied. Errors: E_ARG, E_STATE, E_BUSY (THUNDER_LIVE_FXPARAM_RING). A slot's ON / OFF is a plan change (a FxEntry with `on`): fx_plan + fx_commit. */
int thunder_live_fx_param(thunder_live* L, int track, int slot, const double* p12, int64_t at);

/* PP (docs/port/PP-NOTES.md). CONTROL. THE KNOBS OF A PUMP OR A MODULAR EFFECT SLOT, at once (THUNDER_LIVE_CAP_FXVALUES; the page's {pset:{slot, pv}} for a pump slot, {pset:{slot, mp}} for a MODFX slot).
   `kind`: THUNDER_LIVE_FXV_PUMP (1) or THUNDER_LIVE_FXV_MOD (2). `v`, `n` doubles, copied: for a pump slot the slot's WHOLE value list in the plugin's own parameter order (pumpPv: the page's `pv`; n = the plugin's
   parameter count, the same n the slot was planned with; each value is stored as a float32, as the page's Float32Array does; the slot's own bypass entry is overwritten by the slot's ON / OFF); for a modular slot
   the module's values in the table's order (the page's `mp`: TFX[k].map(...); n = the effect's control count). The caller maps its controls to these values; the core does not. `slot` 0 .. 7 as in the song's slot list.
   `track` 0 .. 31 or THUNDER_LIVE_MASTER. `at`: as fx_param (THUNDER_LIVE_NOW, or a host frame: applied by the first block that starts within 64 samples of it). Queued on a ring of THUNDER_LIVE_FXVALUES_RING
   records; drained at step 1 after the commands, with the same snapshot rule as fx_param (a record pushed after an fx_commit that the quantum's command loop did not see waits for the next quantum and meets the new chain).
   JUDGED ON THE CONTROL SIDE against the track's LAST PLANNED chain (the last fx_plan, or the plan play() made; staged or committed): E_ARG when the slot is not a hosted slot of that kind there (a tfx slot, a slot
   the plan left out, an empty one), when n is not the planned count, when a value is NaN / infinite (or does not fit a float32 for a pump slot), when `kind` is not 1 or 2, `at` above 2^62, `v` NULL. E_STATE: no
   F_EFFECTS (or a dead handle). E_BUSY: the ring is full, nothing changed, the caller retries after the next process() (live.h ERRORS). Nothing is posted and nothing changes for a tfx slot (use fx_param).
   The audio side applies a record to the chain IN FORCE at that time: a slot that is no longer of that kind then (a later-timed commit changed it) is left as it is. A pump slot that is RESTING (OFF by its own setting)
   stores the values and does not wake: nothing is audible from it, and no instance is made on the audio thread; it is made with them when it is switched on. Values do not change what a plan commits later:
   the page sends the plan's values again with the next fx_plan; a song's own FXV for the same slot still computes from its planned values. */
#define THUNDER_LIVE_FXVALUES_RING 16
#define THUNDER_LIVE_FXV_PUMP 1
#define THUNDER_LIVE_FXV_MOD 2
#define THUNDER_LIVE_CAP_FXVALUES 0x800u   /* bit 11 (bit 10 is M1's THUNDER_LIVE_CAP_METERS2): thunder_live_fx_values exists. Set in every build; without the pump library / with no effects it answers E_STATE / E_ARG as above */
int thunder_live_fx_values(thunder_live* L, int track, int slot, int kind, const double* v, int n, int64_t at);

/* CONTROL. THE MASTER CHAIN (mastering + safety limiter: master_chain_from_song(mst, limiter), chain.h:36). `mst_json`: the song's `mst` object (NULL / n 0: freshMst); `safety`: S.limiter 0 / 1. play already sets the song's own
   configuration at its start; this call is the EDIT while playing (rtRoute): the configuration is parsed here and the audio side applies it at the top of the next quantum (MasterChain::configure keeps the processors'
   states). BUILT IN C++ by U37 P3 (LiveHost::masterConfig: MasterChain::configure at the top of a quantum; a changed latency shows in stats.latency_frames; no PDC path is needed, the PDC plan never reads the master chain): P5 sets THUNDER_LIVE_CAP_MASTER_SWAP. (The sentence below about E_UNSUPPORTED is for a build without it.) Without the cap: E_UNSUPPORTED,
   nothing changes (the page then republishes the song and the chain follows at the next play). Needs F_MASTER (else E_STATE). n above 4 MiB is E_LIMIT; mst_json NULL with n > 0, or `safety` not 0 / 1, is E_ARG. Errors: E_ARG, E_LIMIT, E_MEMORY, E_STATE, E_BUSY (E_UNSUPPORTED never in a P5 build). */
int thunder_live_master_config(thunder_live* L, const uint8_t* mst_json, size_t n, int safety);

/* ---- 6b. CHAINS (F_CHAINS; THUNDER_LIVE_CAP_CHAINS) ---------------------------------------------------------------------------------------------------------- */
/* CHAINS. The song's chains (index.html chainsNode / routeTrack / busSync: one ChainsNode per track that has CHN on and a Chain Out in the patch, every such track heard; mute and solo are the mixer's) play through the
   host exactly as through SongRenderer::setChains (bit for bit for a song played from its start). WHAT THE CONTROL SIDE DOES (in `play` and in a PLAYING `song_publish`; an IDLE publish builds nothing: the next play does): it
   compares the new song's chain patch and CHN flags with those the audio side has (or will have once what is in flight lands) and acts by what differs:
     nothing differs ............................ nothing is made or sent;
     only parameter values (same modules, cables, text) ... the new values go to every node through a fixed ring (no reset, no click: the page's `param` op); a value that does not fit the record is a structural change;
     only the bus plan / the CHN flags ........... a plan that leaves every existing node's patch alone (module state survives) and gives the nodes their new bus maps; plus the parameter values;
     the patch differs otherwise ................. a WHOLE-PATCH SWAP (ruling D4 (a)): every node gets the new patch, its module state is RESET, a click is accepted.
   The plan is committed at the top of the quantum that applies it, BEFORE the song's adoption (step 1 against step 2; or a few quanta before it when the adoption waits for a full return ring: accepted), and the mixer's bus plan
   is set by that commit ONLY. ONE PLAN IS IN FLIGHT at a time: a play or a publish that needs a plan while the previous one has not come back (thunder_live_collect settles it) is E_BUSY with NOTHING changed (retry after the
   next callback, as every edit). A plan the engine refuses (the patch) or that would pass opts.chains_bytes is NOT an error: the call succeeds, the song plays with the chains it had (none: dry), stats.chains_refused counts it
   and the next publish or play tries again. TRANSPORT (the page's chainsTempo): a play sends {tempo = the Seq's, playing true} to every node (startN + 1; a host that was already playing goes through not playing first,
   as the page's tStart does), a stop (and the 0.3 s timer after ENDED, as the page's tStop) {playing false}; HELD keeps playing until the stop; a tempo change reaches the nodes as the sequencer's tempo event. A play and a stop
   clear the events waiting for their quantum. The host starts not playing. EVENTS: the tracker's BYP (chc) and tempo events reach the nodes at the quantum the page's rules give (chost.h EVENTS). NOT IN THIS VERSION: sidechain taps
   and sends (a patch's Sidechain In / Out are silent), the 20 ms CHN switch ramp, Resample takes, and the pump spare service (nobody makes a pump module's spare: a bad pump block is silent for good). */

/* CONTROL. Moves parameters of the playing chains NOW, before the shell's next song_publish (the page's `chnDiff` ops `param`: {"op":"param","id":"vcf1","name":"cutoff","value":0.4}): `ops_json` is a JSON ARRAY of
   such ops (the page's own shape: id, name, value; other fields are ignored), ALL OR NOTHING. Every node gets every op (each holds the whole patch). The shell still publishes the song afterwards: that publish then finds
   the same values (a parameter change; the same values go to the nodes again, harmless). THE HOST REMEMBERS WHAT chain_edit SENT (since its record of the playing song last moved: up to 256 distinct (module id, param name) pairs) and puts each back to the value of the NEXT published song unless that publish's own diff already carries it: a shell that cancels a gesture and publishes the original values gets the original values on the nodes; a publish that carries the edited value changes nothing more; more than 256 distinct pairs make the next publish a whole-patch plan. A record that was pushed before a whole-patch plan (a swap) was committed is DROPPED by the audio side (stats.chains_params_stale): the swap carries every value of its song. A `param` whose name is `_bn` or `track` is allowed for the module types not refused below, and runs on the audio thread through the engine's allocation-free prepareDerived. `at` must be THUNDER_LIVE_NOW in this version (the parameter is kept for the ABI). Errors: E_STATE (no F_CHAINS, or no chains
   were ever planned for a song: nothing to edit), E_ARG (NULL with n > 0, `at` not NOW, malformed JSON or not an array, an op that is not `param`, an id that is not a module of the playing patch,
   a value that is not a finite number, a module of type trackIn, chainOut, scin or scout: their parameters move the bus plan or the sources and go through a publish), E_LIMIT (n above 1 MiB; an id or a name that does not fit a record, 47 / 31 bytes;
   more ops than the ring can ever hold, THUNDER_LIVE_CHAIN_RING), E_MEMORY, E_BUSY (more ops than the ring has room for right now: retry after the next callback). */
#define THUNDER_LIVE_CHAIN_RING 256
int thunder_live_chain_edit(thunder_live* L, const uint8_t* ops_json, size_t n, int64_t at);

/* ---- 7. MIDI ---------------------------------------------------------------------------------------------------------------------------------------------- */
/* THE HOST OWNS ITS thunder_midi (F_MIDI): made at create, freed at destroy, NEVER given to the page (a raw pointer the audio side reads is not safe to swap: PICKUP duty D-8 (e)). What the host does itself, in this order:
     song_publish   thunder_midi_set_song(song text) (parsed there; the old one retired by a later control call).
     play           (audio side, step 4, IMMEDIATELY AFTER midi_begin of the quantum that applies the play, before the step loop) MidiSched::start(spp, t0). D-1: the Start never comes after the first step of the Seq.
     every quantum  midi_begin / onStep / midi_end (steps 4 to 6), also quanta without a step; the audible mask and the FX latency (the master chain's + the PDC delay) are taken from the mixer every block.
     stop           (audio side, step 4, immediately after midi_begin, before the step loop) MidiSched::stop(t0): all notes off, CLEAR, clock Stop; forgets last CC values.
     song end       Players::songOver at the end; the MIDI stop follows 0.3 s later (the web's frame() rule) on its own; STATE ENDED is posted at the end, STOPPED at the MIDI stop.
     midi_begin < 0 counted in stats.midi_begin_failed (a debug build asserts); the first one makes the scheduler's OWN panic (MidiSched::panic, on the audio thread: thunder_midi_panic is a control call) and posts one non-fatal ERROR record (WHERE_MIDI).
   WHERE THE DIRECT CALLS SIT, AND P4's DONE-TEST. A posted thunder_midi_start / _stop is applied inside midi_begin (blockBegin, midi_api.cpp:83-90): first setFxLatency / setAudible with this block's figures, then every posted
   command in ring order (apply(): OP_START -> MidiSched::start(spp, now), OP_STOP -> MidiSched::stop(now), midi_api.cpp:70-71), then setBlock; `now` is the quantum's start. The host's direct call is made right after midi_begin
   returns, with the same `now` and the same figures in force, before onStep. It is the posted command's point EXCEPT (1) its place among OTHER posted commands (one the shell posted before the play runs before the Start in both
   paths; one posted after the play is popped by midi_begin before the direct Start here, but after it in the posted path) and (2) stop's `at` (the posted path has none: it stops at the block top; the host stops at the quantum
   containing `at`). It is NOT provable from the code that the two are byte-identical in every case, so P4's DONE-TEST decides it: the record stream (Start, SPP, the first Gear-delayed notes; Stop, CLEAR, note-offs) of the
   direct path must be BYTE-EQUAL to the posted path (thunder_midi_start / _stop posted before the block) for the 18 ledger songs (native/tests/golden/midi-ledger-expect.json, test_midi_ledger.cpp). If it is not,
   THE POSTED PATH IS THE FALLBACK P4 MUST USE: thunder_live_play / thunder_live_stop post thunder_midi_start / _stop themselves, before they queue their own command, and the 0.3 s song-end stop is posted by the CONTROL side
   when thunder_live_collect sees the ENDED record. The contract of this header is the same either way.
   SHIPPED IN P4: THE DIRECT PATH. test_live_midi (ctest live_midi) found the stream byte-equal to the posted path for the 18 ledger songs (and start-midway and loop); the posted fallback was not needed (docs/port/U37-P4-NOTES.md). The ENDED timer's Stop is issued at step 4 of the quantum in which
   t0 >= endStopAt (the same test as the STOPPED record of step 7); a queued Start / Stop pair is applied in the order the commands were applied.
   THE RECORDS' `frame` IS THE ENGINE FRAME (lead's ruling of 24 Nov, verify-p4 finding 2): the engine's own gapless count at the engine rate, EXCLUDING the device's output latency and INCLUDING the core's FX latency (midi.h THE OUTPUT). It is NOT
   a host frame: it equals one only while the offset of TIME is 0 (the first process call had frame0 = 0 and nothing re-anchored). THE WAY TO MAP IT (and the engine frame of any record or time the engine keeps): host frame = engine frame + thunder_live_engine_offset() (one atomic, see there; a gapless shell, whose frame0 always continues the last call, has the offset of its first frame0, 0 when it counts from 0).
   A host frame becomes the host's clock with the CLOCK records (f and wall_ns). Do NOT derive the offset from stats.frames and stats.quanta: those are two separate loads and tear by 128 frames. thunder_live_midi_drain and thunder_midiout_clock_map add no offset (a record booked before a re-anchor would be mapped with the wrong one).
   ORDER AGAINST A SHELL'S OWN MIDI COMMAND (lead's ruling of 24 Nov, verify-p4 finding 5, ACCEPTED): a command the shell posts on the host's thunder_midi (set_port, set_cable, ...) AFTER thunder_live_play and before the quantum that applies the play is popped by midi_begin BEFORE
   the host's direct Start of that quantum; in the posted path (thunder_midi_start posted by the shell) it would run after it. So a port configured after play() is in force for that Start (its SPP is sent). A shell that wants the other order configures before play(). */

/* ANY thread for the call; the handle is used under midi.h's own THREAD rules. Returns the host's thunder_midi, or NULL without F_MIDI. Valid until thunder_live_destroy; the shell passes it to
   thunder_midiout_create (and must free the thunder_midiout BEFORE destroy). The shell's CONTROL thread is the ONE control thread of the handle: it may make the thunder_midi_set_port / set_cable / port_clock /
   release / panic / key_* / raw calls itself (midi.h), as it makes this host's CONTROL calls, never two at once. It must NOT call thunder_midi_set_song / start / stop / set_audible / set_fx_latency
   itself (the host does: song_publish; play; stop; the mixer). */
thunder_midi* thunder_live_midi(thunder_live* L);

/* CONSUMER, one thread: up to cap records in order (thunder_midi_drain): returns how many or a negative code. THE WEB'S page-side sink (ruling D6 A): the worklet drains after each block (step 9) and posts the
   records, once per block and only when non-empty. EXCLUSIVE with thunder_midiout_pump: a shell that runs the core's own MIDI pump (ruling D6 B) never calls this. E_STATE without F_MIDI. */
int64_t thunder_live_midi_drain(thunder_live* L, thunder_midi_msg* out, int64_t cap);

/* CONTROL. The MIDI outbox's look-ahead in seconds, 0.005 .. 1.2 (default 0.060): thunder_midi_set_lead, whose cap goes from 0.5 s to 1.2 s in P4 (ruling D6: a hidden page's lead is 1.2 s). Immediate on the control ring;
   E_BUSY if that ring is full (retry after the next block). Separate from thunder_live_lead on purpose (see the notes). E_STATE without F_MIDI. */
int thunder_live_midi_lead(thunder_live* L, double seconds);

/* ---- 8. EVENTS TO THE HOST -------------------------------------------------------------------------------------------------------------------------------- */
/* ONE consumer thread (any non-audio thread; the worklet's process() after the block in the web). Records are PACKED, little-endian, 8-byte aligned, of the fixed sizes below, the same on wasm32 and 64-bit.
   Every record starts with thunder_live_ev_head; `bytes` is the record's whole size (a reader skips a type it does not know by it); `f` is the frame the event refers to on the host's timeline. Rates (CORE-BRIDGE section 4):
   CLOCK 10 a second, POS 30 a second while playing, METERS at the wanted rate (<= 30), ROWS per tracker row, the others when they happen. TWO RINGS, ONE ORDER. The CRITICAL records (STATE, HELD, SONG, PADS, and an ERROR with fatal = 1: each changes the state machine the page follows, or is the only report of something it must not lose, and at most a few come per quantum) have their own ring of 64 records, so that
   a flood of POS / ROWS / METERS / CLOCK can never push one out; the rest (CLOCK, POS, ROWS, METERS, LATE and every NON-FATAL ERROR: REANCHOR, CMD, SESSION, VOICES; REANCHOR and CMD at most one a quantum) share the big ring (1024 records). thunder_live_poll merges the two by `seq`, so the reader sees them STRICTLY in the order they were posted. A full ring refuses
   the NEWEST record of its kind and counts it (stats.evt_dropped; the next CLOCK carries the total; a critical one is also in stats.evt_crit_dropped): a consumer that stops polling for 64 critical records loses the 65th (a jittery host that re-anchors on every call can no longer cause it), and
   knows it by the gap in `seq` and by the counter. Nothing is dropped silently. A consumer polls at least once every 100 ms (a worklet: once per block or per message). */
#define THUNDER_LIVE_EV_CLOCK 1
#define THUNDER_LIVE_EV_POS 2
#define THUNDER_LIVE_EV_ROWS 3
#define THUNDER_LIVE_EV_METERS 4
#define THUNDER_LIVE_EV_STATE 5
#define THUNDER_LIVE_EV_HELD 6
#define THUNDER_LIVE_EV_SONG 7
#define THUNDER_LIVE_EV_PADS 8
#define THUNDER_LIVE_EV_ERROR 9
#define THUNDER_LIVE_EV_LATE 10
#define THUNDER_LIVE_EV_MAX 320           /* the largest record: poll with cap >= 512 */

typedef struct thunder_live_ev_head {     /* 16 bytes */
  uint16_t type;                          /* THUNDER_LIVE_EV_* */
  uint16_t bytes;                         /* this record's size, header included, a multiple of 8 */
  uint32_t seq;                           /* counts from 1 over all records (a gap = dropped records); poll merges the two rings by it. METERS: the MeterBank's own count is in reserved[0] (as uint32 bits) */
  int64_t f;
} thunder_live_ev_head;

typedef struct thunder_live_ev_clock {    /* 48 bytes. f = the first frame of the quantum that posted it: the page's (frame, time) pair; with wall_ns, the shell's (frame, host clock) pair */
  thunder_live_ev_head h;
  int64_t wall_ns;                        /* the wall_ns of the process call that rendered this quantum (0: unknown) */
  int32_t sr;
  int32_t out_lat;                        /* frames: thunder_live_host_stats */
  uint32_t under;                         /* the host's underrun count (thunder_live_host_stats) */
  uint32_t evt_dropped;                   /* stats.evt_dropped */
  uint32_t late_cmds;                     /* stats.cmd_late */
  int32_t voices;
} thunder_live_ev_clock;

typedef struct thunder_live_ev_pos {      /* 32 bytes. f = the frame the step sounds at. row, step: the highest of the valid tracks (what SeqRunner::pump computes: song row and step), cr: that track's chain row */
  thunder_live_ev_head h;
  int32_t row, step, cr, bpm10;           /* bpm10: the tempo x 10, rounded (Seq::bpm) */
} thunder_live_ev_pos;

typedef struct thunder_live_ev_rowent {   /* 8 bytes: one track's place at a step (SeqPos) */
  int16_t row, cr, step, phrase;          /* phrase -1: none */
} thunder_live_ev_rowent;
typedef struct thunder_live_ev_rows {     /* ROWS (type 3) and HELD (type 6): 24 + 8 * n bytes, n = the number of set bits of `valid` (<= 33). f = the step's time (HELD: the held step's time). Entries follow, ascending by track */
  thunder_live_ev_head h;
  uint64_t valid;                         /* bit t: track t (0 .. 31) has a position; bit 32: the master column */
  /* thunder_live_ev_rowent ent[n] follows */
} thunder_live_ev_rows;

#define THUNDER_LIVE_METER_FLOATS 70      /* kMeterFloats: slots 0 .. 31 tracks, 32 the preview bus, 33 master L, 34 master R: [2s] peak, [2s + 1] rms (meter.h) */
typedef struct thunder_live_ev_meters {   /* 304 bytes. f = the frame at the end of the window. Only with F_METERS and thunder_live_meters_want on */
  thunder_live_ev_head h;
  float a[THUNDER_LIVE_METER_FLOATS];
  float reserved[2];
} thunder_live_ev_meters;

#define THUNDER_LIVE_ST_PLAYING 1         /* play was applied: f = the frame; arg = 0 */
#define THUNDER_LIVE_ST_STOPPED 2         /* the move to IDLE (THE STATE MACHINE): arg 0 by thunder_live_stop, 1 by the 0.3 s MIDI stop after the song's end */
#define THUNDER_LIVE_ST_ENDED 3           /* the sequencer ran out (once per play); f = the sequencer's end time; voices may still sound */
typedef struct thunder_live_ev_state {    /* 24 bytes */
  thunder_live_ev_head h;
  int32_t state, arg;
} thunder_live_ev_state;

typedef struct thunder_live_ev_song {     /* 24 bytes: the audio side now reads song snapshot `rev` from frame f (one for the newest rev adopted in a quantum) */
  thunder_live_ev_head h;
  int64_t rev;
} thunder_live_ev_song;
typedef struct thunder_live_ev_pads {     /* 24 bytes: the audio side now reads pads snapshot number `id` (PadsSnap::id: the publish counter) from frame f */
  thunder_live_ev_head h;
  int64_t id;
} thunder_live_ev_pads;

#define THUNDER_LIVE_WHERE_CMD 1          /* a command was applied at once (waiting list full) or refused when applied */
#define THUNDER_LIVE_WHERE_SESSION 2      /* the PLAY's SeqSession is full (code 0): informational */
#define THUNDER_LIVE_WHERE_REANCHOR 3     /* a process call with an unexpected frame0 (code 0; value = frames skipped, may be negative) */
#define THUNDER_LIVE_WHERE_MIDI 4         /* midi_begin failed (code = its return) */
#define THUNDER_LIVE_WHERE_VOICES 5       /* a voice or event pool overflowed (value = the cumulative count) */
#define THUNDER_LIVE_WHERE_FATAL 6        /* the handle is dead (E_INTERNAL inside): fatal = 1; the page stops, drops the node and uses the JS core (ruling D14) */
typedef struct thunder_live_ev_error {    /* 32 bytes */
  thunder_live_ev_head h;
  int32_t where, code, fatal, value;      /* value: REANCHOR: the frames skipped (negative: backwards); VOICES: the cumulative count; else 0 */
} thunder_live_ev_error;

typedef struct thunder_live_ev_late {     /* 40 bytes: the late / lost counters, cumulative, posted when one changed and at most 4 a second (the playback report's lines) */
  thunder_live_ev_head h;
  uint32_t notes_late;                    /* Players: notes dropped for lateness (> 0.15 s behind: ruling D-4) */
  uint32_t midi_late_notes, midi_late_ticks;   /* the scheduler's late_notes / late_ticks (dropped, never sent in a burst) */
  uint32_t runner_skips;
  uint32_t fx_remakes;
  uint32_t midi_may_hang;
} thunder_live_ev_late;

/* M1 (METERS-DESIGN 4.1 / 5a): the effect level and dynamics records. Types 11 and 12; EV_MAX is unchanged (the largest is 152). Only with F_METERS | F_EFFECTS and thunder_live_meters_set (below) watching the track:
   nothing is posted, and nothing is computed, for a track that is not watched. Both go on the big ring, in step 9 after the METERS record of the same quantum, as FxHost's own every-12th-block post (about 31 a second at 48 kHz).
   f = the frame at the end of the quantum that posted them. A full ring refuses the newest and counts it (stats.evt_dropped), as for METERS. */
#define THUNDER_LIVE_EV_FXLVL 11
#define THUNDER_LIVE_EV_FXDYN 12
typedef struct thunder_live_ev_fxlvl {    /* 56 bytes. The track's effects: the peak out of each slot since the last post, linear, max(abs L, abs R) (FxHost::popLevels: the page's {lvl}) */
  thunder_live_ev_head h;
  int32_t track;                          /* 0 .. 31 tracks, 32 (THUNDER_LIVE_MASTER) the master's effects */
  uint32_t slots;                         /* bit k: the chain has a slot k (lvl[k] is its peak; a slot that was off all the time reads 0). The other lvl[] entries are 0 */
  float lvl[8];
} thunder_live_ev_fxlvl;
typedef struct thunder_live_ev_fxdyn_ent {   /* 16 bytes */
  int16_t slot;                           /* the slot number 0 .. 7 */
  int16_t type;                           /* the core's effect type id of that slot now (FxEntry::t; thunder_fx_type_name turns it into the page's type string): the page drops a report whose type is not its slot's */
  float red_db, in_db, out_db;            /* the worklet's own numbers: [reduction dB, level in dB, peak out dB]; -180 for silence (FxDyn::v) */
} thunder_live_ev_fxdyn_ent;
typedef struct thunder_live_ev_fxdyn {    /* 152 bytes. One post of a track's dynamics slots (comp / limiter-like slots and pump COMP, LIMIT, BAND); only when the track has one */
  thunder_live_ev_head h;
  int16_t track;                          /* as FXLVL */
  int16_t n;                              /* 1 .. 8 entries follow in ent[]; the unused tail is zero */
  int32_t reserved;                       /* 0 */
  thunder_live_ev_fxdyn_ent ent[8];
} thunder_live_ev_fxdyn;

/* ANY thread, once before audio: the host's output latency in frames and its underrun count; the next CLOCK records carry them (core.h thunder_host_stats). */
void thunder_live_host_stats(thunder_live* L, int out_latency_frames, int underruns);

/* CONSUMER. Copies the next whole records, in order, into buf (8-byte aligned; cap >= THUNDER_LIVE_EV_MAX, 512 advised: as many records as fit), and returns the bytes written (0: none). Nothing is allocated. Negative:
   E_ARG (null, cap too small for the next record: it stays queued). */
int thunder_live_poll(thunder_live* L, void* buf, int cap);

/* CONTROL. Switches METERS records on (rate_hz > 0, <= 30: MeterBank::setRate) or off (0). Needs F_METERS (else E_STATE). Queued: applied at the top of a quantum. Meters are the page's graph replacement
   (ruling D15): peak and rms per track and the master, windows of 512 / 1024 samples (meter.h). E_ARG: rate NaN, negative or above 30. E_BUSY. */
int thunder_live_meters_want(thunder_live* L, double rate_hz);

/* M1: CONTROL. The whole metering subscription as one REPLACEMENT state (METERS-DESIGN 4.2 / 5a): the page may re-send it at any time (after hello, after an engine restart); what it leaves out is switched off.
   `size` first (the ABI rule of the other structs). rate_hz: METERS exactly as thunder_live_meters_want (0 off, > 0 and <= 30 on). kinds: THUNDER_LIVE_MK_* bits; CHLVL and MASTERM are reserved now (accepted, they post
   nothing yet). fx_tracks: bit t (0 .. 31) watches track t's effects, bit 32 the master's; with THUNDER_LIVE_MK_FXLVL the watched hosts post FXLVL, with THUNDER_LIVE_MK_FXDYN FXDYN (no cap on the number of
   tracks: the page asks for one). chains_hz: reserved for CHLVL (0 .. 30). Applied at the top of the next quantum (a NEW command: thunder_live_meters_want alone is unchanged and may still be used alone;
   the later of the two calls wins for the METERS rate). Needs F_METERS (E_STATE otherwise); without F_EFFECTS the FX kinds simply post nothing. E_ARG: NULL, size < 4, a field past our struct that is not zero,
   unknown kinds bits, fx_tracks above bit 32, a rate or chains_hz that is NaN, negative or above 30. E_BUSY: the command ring is full, nothing changed. */
#define THUNDER_LIVE_MK_FXLVL 1u
#define THUNDER_LIVE_MK_FXDYN 2u
#define THUNDER_LIVE_MK_CHLVL 4u      /* reserved (M3) */
#define THUNDER_LIVE_MK_MASTERM 8u    /* reserved (M2) */
typedef struct thunder_live_meters_spec {
  uint32_t size;
  uint32_t kinds;               /* THUNDER_LIVE_MK_* */
  double rate_hz;               /* METERS: 0 off, <= 30 */
  uint64_t fx_tracks;           /* bit t: track t's effects (0 .. 31), bit 32: the master's */
  double chains_hz;             /* reserved for CHLVL */
} thunder_live_meters_spec;
int thunder_live_meters_set(thunder_live* L, const thunder_live_meters_spec* spec);

/* M1: ANY (no handle). The page's type string of a core effect type id (FxEntry::t, thunder_live_ev_fxdyn_ent::type): "comp", "reverb", "flex", a pump effect as the page writes it ("p" + its name: "pcomp"), a MODFX
   effect by its key. NULL for an id the core has no name for. The pointer is to a static string (valid for the life of the process; the first call may build the pump names: do not call it on the audio thread). */
const char* thunder_fx_type_name(int type_id);

/* ---- 9. TEST ONLY ------------------------------------------------------------------------------------------------------------------------------------------ */

/* CONTROL (the consumer of the dump ring). Needs F_DUMP_STEPS (else E_STATE). TEST ONLY, for the browser A/B of docs/port/U37-DESIGN-DRAFT.md section 8: the audio side copies the events of EVERY step it plays
   into a fixed ring (no allocation); this call takes the oldest step and writes it as ONE line of UTF-8 JSON, NUL-terminated, as {"t":<step time>,"bpm0":..,"tks":..,"evs":[{...}, ...]} with EACH EVENT written as tools/seq-golden.mjs compares it (corePlain: keys sorted, numbers rounded to 1e-6;
   native/tests/seq_plain.h is the C++ writer), converted HERE on the control side (it allocates; it is not on the audio path and never runs in a product build).
   Returns the length (> 0), 0 when no step is waiting, or -(needed length) when cap is too small (the step stays). A step the ring could not hold is counted in stats.dump_dropped. */
int thunder_live_dump_step(thunder_live* L, char* json, int cap);

#ifdef __cplusplus
}
#endif
#endif
