/* Thunder core: MIDI out scheduling, public C API (unit U35).
   What the web app's "MIDI out and MIDI clock" does (notes, CCs, pitch bend, program changes, LFO and glide CCs, echoes, MIDI clock, Start / Stop /
   Continue / Song Position, the Gear delay, the FX latency, PANIC), as a SCHEDULER: it makes timed MIDI messages and a shell sends them. The core never
   touches a MIDI device. Owner's rulings of 19 Nov (docs/port/U35-NOTES.md): the suspected web bugs B1..B9 are fixed here, not copied.

   THE OUTPUT   a stream of 16-byte records {frame, port, len, flags, kind, b[3], cable}. `frame` is the core's audio frame counter at the engine rate, EXCLUDING the
                device's output latency (the shell adds it) and INCLUDING the core's own FX latency (thunder_midi_set_fx_latency, read from the mixer every
                block, no cache) less the port's Gear delay. A record with THUNDER_MIDI_F_CLEAR is a STOP marker for that cable: the shell drops every message
                it still holds for the cable whose frame is later than the marker's (the helper's clear()). thunder_midi_drain pulls records in order.
   THE FLOW     the host's audio code calls thunder_midi_process() once per block (it applies the commands the control thread posted, then books the clock, the LFOs,
                the glides, the echoes and the outbox up to the look-ahead). The core's sequencer glue feeds events through the C++ entry points of
                core/src/midi/midisched.h (onStep / onEvent with the shared lateness test): not part of this C API.
   THREADS      THREE roles, never more than one thread in each (the control calls are SINGLE-PRODUCER: one control thread, or the caller serialises them; debug
                builds assert it): CONTROL (every thunder_midi_set_* / start / stop / release / panic / key_* / raw call, create,
                free: they post commands on a wait-free ring and allocate nothing but set_song, which parses); AUDIO (thunder_midi_process and the C++ entry
                points: no allocation, no lock, no I/O, bounded work); CONSUMER (thunder_midi_drain, thunder_midi_stats_get: any one other thread). A command
                that finds the ring full returns THUNDER_MIDI_E_BUSY and changes nothing: retry after the next block.
   ERRORS       int functions return 0 or a negative THUNDER_MIDI_E_* code; a bad argument is a code (never a crash, never an abort), a message
                the scheduler refuses (a note outside 0..127, a NaN time) is counted in stats (bad_args / bad_events), never sent. */
#ifndef THUNDER_MIDI_H
#define THUNDER_MIDI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THUNDER_MIDI_OK 0
#define THUNDER_MIDI_E_ARG (-1)      /* a null, NaN, out-of-range argument */
#define THUNDER_MIDI_E_STATE (-2)    /* not allowed now (a dead handle) */
#define THUNDER_MIDI_E_BUSY (-3)     /* the command ring is full: try again after the next block */
#define THUNDER_MIDI_E_MEMORY (-4)   /* an allocation failed (create: NULL) */
#define THUNDER_MIDI_E_SONG (-5)     /* set_song: the bytes are not a song the core can read */

#define THUNDER_MIDI_PORTS 4         /* A..D */
#define THUNDER_MIDI_F_CLEAR 1       /* record flag: the cable's STOP marker (see THE OUTPUT) */
#define THUNDER_MIDI_MAX_SONG_BYTES ((size_t)16 << 20)

/* record kinds: which part of the scheduler made it (for logging; the shell sends all of them alike) */
#define THUNDER_MIDI_K_EVENT 0
#define THUNDER_MIDI_K_CLOCK 1
#define THUNDER_MIDI_K_LFO 2
#define THUNDER_MIDI_K_RAMP 3
#define THUNDER_MIDI_K_ECHO 4
#define THUNDER_MIDI_K_RELEASE 5
#define THUNDER_MIDI_K_STOP 6
#define THUNDER_MIDI_K_START 7
#define THUNDER_MIDI_K_JOIN 8
#define THUNDER_MIDI_K_PANIC 9
#define THUNDER_MIDI_K_LIVE 10

typedef struct thunder_midi thunder_midi;

typedef struct thunder_midi_msg {   /* 16 bytes */
  int64_t frame;
  uint8_t port, len, flags, kind;
  uint8_t b[3];
  uint8_t cable;                    /* the cable id the port had (set_port) when the message was made */
} thunder_midi_msg;

typedef struct thunder_midi_stats { /* all counted since create */
  uint32_t size;                    /* in: sizeof as the caller compiled it; a field past it is not written */
  uint32_t reserved0;
  uint64_t sent, ring_full, pend_full, held_full, echo_full, glide_full, ramp_key_full;
  uint64_t late_ticks, late_notes;  /* B7: dropped, never sent in a burst */
  uint64_t collisions;              /* B4: a note-on for a pitch another TRACK holds on that port + channel (a shell can warn) */
  uint64_t same_track_retrig, offs_suppressed, silent_notes;
  uint64_t ticks, starts, stops, bad_events, bad_args, cmd_full;
  uint64_t max_pend;
  /* F4: the ring is full because the consumer stalled: what was dropped, by kind (each also counts in ring_full). Order of dropping: clock ticks, CCs, bends,
     pressure, program (below 512 free slots); then note-ons, Start / SPP (below 256); NOTE-OFF, CLEAR, All Notes Off and Stop only when NOTHING is free. */
  uint64_t drop_ticks, drop_ctl, drop_note_on, drop_start, drop_off, drop_clear, fut_full;
  uint64_t may_hang;                /* 1 = a NOTE-OFF / CLEAR / All Notes Off / Stop was dropped: the gear may be left with hung notes: the shell should send PANIC (thunder_midi_panic clears it) */
} thunder_midi_stats;

/* CONTROL thread. NULL and `err` (when err != NULL and errcap > 0) on a bad rate (8000 .. 384000) or no memory. */
thunder_midi* thunder_midi_create(double sample_rate, char* err, size_t errcap);
void thunder_midi_free(thunder_midi* h);   /* NULL is fine; no other thread may be inside a call on h */

/* CONTROL. The song's instruments (the MIDI port, channel, CC numbers, ECHO, LFO of each). Parsed here; handed over to the audio thread
   on the next block, the old one freed by a later control call. */
int thunder_midi_set_song(thunder_midi* h, const uint8_t* json, size_t bytes);
/* CONTROL. port 0..3; connected: a cable is set; clock_on: this port gets the MIDI clock; gear_ms the port's Gear delay (-50..200, rounded);
   cable: an id: ports with the same id are one physical cable (one clock stream, one CLEAR); 0..250. */
int thunder_midi_set_port(thunder_midi* h, int port, int connected, int clock_on, double gear_ms, int cable);
int thunder_midi_set_lead(thunder_midi* h, double seconds);          /* outbox look-ahead 0.005 .. 1.2 (default 0.060; the cap was 0.5 before U37 P4) */
int thunder_midi_set_fx_latency(thunder_midi* h, double seconds);    /* 0 .. 2; the mixer's figure, every block */
int thunder_midi_set_audible(thunder_midi* h, uint64_t mask);        /* bit t: track t is heard (mix_audible); a muted track's gear goes quiet */
int thunder_midi_start(thunder_midi* h, int spp);                    /* PLAY: spp = Song Position Pointer in 16ths (0: from the top) */
int thunder_midi_stop(thunder_midi* h);                              /* STOP / song end: all notes off, CLEAR, clock Stop; forgets the last CC values and used channels */
int thunder_midi_release(thunder_midi* h, int playing);              /* a cable / OUTPUT / CH edit: notes off; playing != 0 keeps what is queued */
int thunder_midi_panic(thunder_midi* h);                             /* sustain off, All Sound Off, All Notes Off on every channel of every connected port */
int thunder_midi_port_clock(thunder_midi* h, int port, int on);      /* a port's clock box (while playing: Stop / join) */
int thunder_midi_set_cable(thunder_midi* h, int port, int connected, int cable);   /* a cable change while playing */
int thunder_midi_key_on(thunder_midi* h, int key, int inst, int note, int vol);    /* a held key (key 0..127 is the caller's id), inst 0..63, tracker note, vol 0..255 or -1 */
int thunder_midi_key_off(thunder_midi* h, int key);
int thunder_midi_raw(thunder_midi* h, int port, const uint8_t* bytes, int len, double extra_seconds);   /* thru: one complete channel / system message, 1..3 bytes (no SysEx) */

/* AUDIO thread: apply the posted commands, then book up to the look-ahead. now_sec = this block's start in the core's audio time (frames / rate),
   block_sec = this block's length (frames / rate; 0 = none known): the look-aheads grow by it, so the stream is the same for every block size (F6),
   seq_time = the sequencer's next step time, bpm its tempo, seq_alive 0 / 1. Returns the number of commands applied (>= 0) or a negative code. */
int thunder_midi_process(thunder_midi* h, double now_sec, double block_sec, double seq_time, double bpm, int seq_alive);

/* CONSUMER: up to cap records in order; returns how many (>= 0) or a negative code. */
int64_t thunder_midi_drain(thunder_midi* h, thunder_midi_msg* out, int64_t cap);
int thunder_midi_stats_get(const thunder_midi* h, thunder_midi_stats* out);   /* a snapshot; the counters are the audio thread's, read racily (monotonic, fine for logs) */

#ifdef __cplusplus
}
#endif
#endif
