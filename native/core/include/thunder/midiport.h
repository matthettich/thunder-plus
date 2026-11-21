/* Thunder core: the MIDI out PORT TABLE and the timed SEND QUEUE (unit U36, PR A). Between thunder_midi_drain (the scheduler's records, thunder/midi.h) and the
   shell's MIDI sink. The core never touches a device and starts no thread: the shell lists its devices (thunder_midiout_set_devices), names the four port letters
   (thunder_midiout_assign), gives the audio clock's map to the host clock (thunder_midiout_clock_map), and calls thunder_midiout_pump in a loop on a thread of its own;
   the pump calls the shell's sink to send a message when it is due. Design and rulings: docs/port/U36-SPEC.md section 3 and 8, decisions in docs/port/U36-NOTES.md.

   THE DEVICE TOKEN (contract) Every sink call names a device by the TOKEN the shell put in thunder_midiout_dev, never by a position in a list, so no lock is needed between set_devices (CONTROL) and
             pump (SENDER). The pump may call send / clear with a token that set_devices has just taken out of the list (the pump read the cable an instant before): the sink must answer
             an unknown token with send < 0 and ignore it in clear, and its token-to-device table must be safe to read from the sender thread while the control thread changes it (a
             lock of its own, or a copy swapped atomically; the core takes none). Nothing else needs a lock: clock_map stays lock-free, for the audio callback.
             A send that returns < 0 marks the device's cable gone UNTIL THE NEXT set_devices (which gives it another chance): a shell that lists only on hot-plug must call set_devices again to revive it.
   BEFORE THE FIRST MAP Everything the pump drains before the first thunder_midiout_clock_map is dropped (dropped_nomap), a Start included: the shell must call clock_map once before PLAY
             (thunder_midi_start). In the audio callback call clock_map BEFORE thunder_midi_process (or midi_begin) of the block, not after it: the pair (the frame the block starts at, the host time it is heard) is
             known in advance, and a pump that runs between the block's process and its clock_map would drain the block's records (the first PLAY's Start and first notes) while no map exists and drop them
             (midiport_threads flake, 21 Nov: 4 notes in 2996, all in dropped_nomap). After the first map the order does not matter (the floor compares the callback's clock with the last block start, so the
             pair is at most one block older). From a timer it adds up to the age of the pair to the floor's shift.
   LIFETIME  thunder_midiout_free: after BOTH the sender thread (pump) and every thread that calls clock_map (the audio callback) have stopped.
   THREADS   CONTROL: create, free, set_devices, assign, set_clock, set_gear, set_playing, set_out_latency, get_port: the ONE thread that also calls the thunder_midi_set_* calls
             (single producer); set_devices, assign, set_clock, set_gear and set_playing re-sync the scheduler's ports and return THUNDER_MIDIOUT_E_BUSY if its command ring was full (the table is
             updated anyway: repeat that call, or any other of these five, and the scheduler is brought up to date; set_out_latency and get_port do not sync). SENDER: thunder_midiout_pump, the ONLY thread that calls thunder_midi_drain; every sink call comes
             from it (and so may take the shell's own mutex), except sink.now_ns, which must be callable from any thread (a clock read: clock_map calls it). ANY: clock_map
             (one writer at a time: a concurrent call is skipped, never blocks), stats_get.
   TIMES     int64 nanoseconds of the host clock the sink's now_ns reads. A record's `frame` is the core's audio frame (thunder/midi.h): host time = the clock map.
   ERRORS    int functions return 0 or a negative THUNDER_MIDIOUT_E_* code; a bad argument changes nothing and is never a crash. */
#ifndef THUNDER_MIDIPORT_H
#define THUNDER_MIDIPORT_H

#include <stdint.h>

#include "thunder/midi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THUNDER_MIDIOUT_OK 0
#define THUNDER_MIDIOUT_E_ARG THUNDER_MIDI_E_ARG        /* NULL, NaN, out of range, an id of 96 bytes or more, a list of more than 32 or with a repeated id */
#define THUNDER_MIDIOUT_E_BUSY THUNDER_MIDI_E_BUSY      /* the scheduler's command ring is full: the table changed, the scheduler is not up to date yet: repeat */

#define THUNDER_MIDIOUT_MAX_DEVS 32          /* devices the shell can list at once */
#define THUNDER_MIDIOUT_ID_BYTES 96          /* UTF-8, NUL-terminated: at most 95 bytes of id */
#define THUNDER_MIDIOUT_MAX_QUEUE 4096       /* messages the queue holds */
#define THUNDER_MIDIOUT_SEND_AHEAD_NS 20000000LL   /* CAP_TS: a message goes to the OS this early, stamped */
#define THUNDER_MIDIOUT_SPIN_NS 500000LL           /* no CAP_TS: the pump waits until this long before the time, then spins */
#define THUNDER_MIDIOUT_LATE_NS 2000000LL          /* a message sent later than this behind its time counts in stats.late */
#define THUNDER_MIDIOUT_TICK_LATE_NS 20000000LL    /* a clock tick later than this is dropped, never sent in a burst */
#define THUNDER_MIDIOUT_STALE_NS 2000000000LL      /* a clock map not refreshed for this long is stale (counted, still used) */

#define THUNDER_MIDIOUT_DEV_TS 1u            /* thunder_midiout_dev.flags: the OS honours a send time (informational) */
#define THUNDER_MIDIOUT_CAP_TS 1u            /* thunder_midiout_sink.caps: send() may be called up to SEND_AHEAD_NS early with a time; else the pump waits and spins */

#define THUNDER_MIDIOUT_PORT_NONE 0          /* no id chosen */
#define THUNDER_MIDIOUT_PORT_OK 1            /* id chosen and the device is listed */
#define THUNDER_MIDIOUT_PORT_MISSING 2       /* id chosen, device not listed: the page shows "(not connected)" */

typedef struct thunder_midiout thunder_midiout;

typedef struct thunder_midiout_dev {         /* one physical output, as the shell found it */
  char id[THUNDER_MIDIOUT_ID_BYTES];         /* the web's id: "n:" + the port's name, " #2" for the second of two with one name (and "n:Name / PortName" for a device with several outputs) */
  char name[THUNDER_MIDIOUT_ID_BYTES];       /* what Settings shows */
  uint32_t flags;                            /* THUNDER_MIDIOUT_DEV_TS */
  uint32_t reserved;                         /* 0 */
  uint64_t token;                            /* the shell's own handle for this device: what send / clear are given. Use a COUNTER, not a pointer: a token must never be reused for another device while the
                                                handle lives (a record that read the old one may still be in flight). It stays the same for as long as the shell keeps the device open, whatever the device's place in
                                                the list. Two devices of one list must not share a token (set_devices returns E_ARG) */
} thunder_midiout_dev;

typedef struct thunder_midiout_sink {        /* implemented by the shell */
  void* user;
  int64_t (*now_ns)(void* user);             /* the host clock the times are in (Android CLOCK_MONOTONIC, Mac mach host time as ns, Windows QPC as ns); callable from any thread, and from the AUDIO thread (clock_map calls it): bounded, no allocation, no lock, no throw, no I/O */
  int (*send)(void* user, uint64_t dev, const uint8_t* b, int len, int64_t at_ns);   /* dev = a thunder_midiout_dev.token; at_ns <= now: send now; 0 on success, < 0 = the device is gone */
  void (*clear)(void* user, uint64_t dev);   /* optional (NULL): drop what the OS still holds for the device (CoreMIDI MIDIFlushOutput); called only with CAP_TS, when a CLEAR meets a stamped message not yet due */
  uint32_t caps;                             /* THUNDER_MIDIOUT_CAP_TS or 0 */
} thunder_midiout_sink;

typedef struct thunder_midiout_port {
  int state;                                 /* THUNDER_MIDIOUT_PORT_* */
  int dev;                                   /* index in the current list, -1 if not OK */
  char id[THUNDER_MIDIOUT_ID_BYTES];         /* the chosen id ("" = none) */
  char name[THUNDER_MIDIOUT_ID_BYTES];       /* the device's name when OK, else "" */
  int clock;                                 /* the clock box */
  double gear_ms;                            /* the Gear delay, -50..200, rounded */
} thunder_midiout_port;

typedef struct thunder_midiout_stats {       /* all counted since create */
  uint32_t size;                             /* in: sizeof as the caller compiled it; a field past it is not written */
  uint32_t reserved;
  uint64_t sent;                             /* messages the sink accepted */
  uint64_t dropped_gone;                     /* the message's device is not listed, or the sink said it is gone */
  uint64_t dropped_cleared;                  /* removed by a CLEAR marker */
  uint64_t dropped_nomap;                    /* drained before the first clock map */
  uint64_t late;                             /* sent (or a tick dropped) more than 2 ms behind its time, or sent while the map was stale */
  uint64_t queue_full;                       /* a message did not fit: it, or a lower-priority one, was dropped (clock ticks, then CCs, then note-ons, never a note-off or Stop) */
  uint64_t max_queue;                        /* the most messages the queue held */
  uint64_t send_errors;                      /* the sink returned < 0 */
  uint64_t dropped_late_ticks;               /* clock ticks more than 20 ms behind: dropped (also in late) */
  uint64_t may_hang;                         /* 1 = a note-off, Stop or All Notes Off did not fit: the shell should send PANIC (thunder_midi_panic) */
} thunder_midiout_stats;

/* CONTROL. NULL on a bad argument (null core_midi / sink / now_ns / send, a rate outside 8000..384000) or no memory. The core_midi and the sink must outlive it. */
thunder_midiout* thunder_midiout_create(thunder_midi* core_midi, double sample_rate, const thunder_midiout_sink* sink);
void thunder_midiout_free(thunder_midiout* o);   /* NULL is fine. Only after the pump thread has stopped AND no new call to clock_map / stats_get / any other function can start, and every call in flight has returned (the audio callback included) */

/* CONTROL. The whole device list (after every scan and every hot-plug): replaces the list; the four ports are re-resolved by id. n 0..32. */
int thunder_midiout_set_devices(thunder_midiout* o, const thunder_midiout_dev* devs, int n);
/* CONTROL. Port letter 0..3 (A..D) names a device by id ("" = none). Applies now. An id that is not listed gives MISSING (and finds its device when it is listed). */
int thunder_midiout_assign(thunder_midiout* o, int port, const char* dev_id);
int thunder_midiout_set_clock(thunder_midiout* o, int port, int on);        /* the clock box of a port */
int thunder_midiout_set_gear(thunder_midiout* o, int port, double ms);      /* the Gear delay: -50..200, rounded (what the scheduler does) */
int thunder_midiout_set_playing(thunder_midiout* o, int playing);           /* the host's transport: while playing a cable change joins / stops the clock (thunder_midi_set_cable); while stopped it is set_port */
int thunder_midiout_set_out_latency(thunder_midiout* o, int64_t ns);        /* the floor of the clock map: the reported base + output latency, 0 .. 2 s */
int thunder_midiout_get_port(const thunder_midiout* o, int port, thunder_midiout_port* out);   /* CONTROL */

/* ANY thread, wait-free. Core frame `frame0` is AUDIBLE at host time `host_ns0` (the device's output latency is already in it: the core adds none). Called ~10 to 50 times
   a second, from the audio callback or the shell's own timer. Smoothed as the web's mapUpd: 2 % per 25 ms of host time between calls (the web's call rate while playing, so the time constant is the same at any callback size), a jump over 50 ms taken at once, never earlier than set_out_latency says. */
void thunder_midiout_clock_map(thunder_midiout* o, int64_t frame0, int64_t host_ns0);

/* SENDER, one thread only. Drains the scheduler's records into the queue, applies CLEAR markers, sends what is due; returns how many it sent (>= 0) or a negative code.
   *next_ns (may be NULL) = the host time at which the next message wants attention (INT64_MAX if none): sleep until then, but call it at least every 2 ms (the scheduler's ring holds
   8192 records). Allocates nothing, takes no lock. */
int thunder_midiout_pump(thunder_midiout* o, int64_t* next_ns);

int thunder_midiout_stats_get(const thunder_midiout* o, thunder_midiout_stats* out);   /* ANY thread: a snapshot */

#ifdef __cplusplus
}
#endif
#endif
