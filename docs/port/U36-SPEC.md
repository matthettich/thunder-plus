# U36 spec, first slice: the shells' MIDI out (port table, sending `MidiSched`'s records) and the core glue that calls `MidiSched` each block

Written 20 Nov 2026 by a spec worker for the lead. A build spec for a Sonnet worker. Nothing was compiled and no code was changed to write it.
PICKUP.md:41 words the slice: "U36 (the shells' MIDI: port table, sending `MidiSched`'s records; clock in is phase 2)"; the task adds "the core glue that calls `MidiSched` each block".
Marks: **UNVERIFIED** = not checked in a file or on a device; the builder checks it first and reports. Line numbers are `native` at 5abe174 unless a file is named
`beta` (the web app at build 660, `git show origin/beta:index.html`; its lines are NOT the lines U35-NOTES.md quotes, which are an older `index.html`).
Read first: `native/core/include/thunder/midi.h` (all 110 lines), `docs/port/U35-NOTES.md` Part 4, `native/tests/midi_drive.h`.

## 1. Scope

IN (this slice):
1. **Core glue** (section 2): the calls that feed `MidiSched` from the block render, the split of `thunder_midi_process` that makes that possible, the SPP helper, the
   mixer's audible mask and FX latency fed in every block, and `Players`' lateness test.
2. **Port table and out queue** (section 3): a platform-neutral C API, in the core library, with no device code and no thread: ports A to D mapped to devices by id, the
   device list the shell reports, hot-plug, a missing port, the audio-frame-to-host-time map, the time-ordered send queue with the CLEAR rule, a `sink` the shell fills in.
3. **Tests that need no shell** (section 3.6): a fake sink and a fake clock.
4. **The shells' sinks, as far as a spec can fix them** (section 3.5): Android (`MidiManager` + AMidi) is the first build target (N2 task H2); desktop (CoreMIDI,
   WinMM) is described so the C API does not have to change for N4 (task H7).

OUT, listed and not designed:
- **MIDI clock IN** (the follower: Clock from, SPP / Start / Stop in, the tempo follower, the `clkLat()` trim at `beta` index.html:17883-17890). Phase 2 (U35-NOTES A10; PICKUP.md:41).
- **MIDI IN to the core**: `midi.in` events (CORE-BRIDGE.md:76), notes and knobs to pads, CC learn, the thru path (`thunder_midi_raw` exists and is not wired). Phase 2 with clock in.
- **Gear delay "Measure..."** (`midiLatRun`; it needs an input loopback): phase 2. The per-port Gear delay NUMBER is in scope (it is a `set_port` argument).
- **Link, recording, audio inputs** (the rest of U36 in ENGINE-INVENTORY.md:249): not this slice.
- **`thunder_process` and the live host** (core.cpp:123 is still N0's click pattern): U37, the lead's own. This slice does not edit `core.cpp`. See 2.5.
- B10 (RPN bend range, bank select): a later opt-in (U35-NOTES Rulings).
- SysEx and anything longer than 3 bytes: `MidiMsg` carries 1 to 3 bytes (midi.h:56-61) and the scheduler refuses the rest.
- iOS shell, Linux shell (WORKPLAN.md section 12: "low priority").

## 2. The core glue

### 2.1 The state of the code (what exists)
- `MidiSched` (`native/core/src/midi/midisched.h:73`) is complete and is not wired into any block render. Its own header says the host calls, on the AUDIO THREAD only,
  `onStep` (`:103`) once per played step and `book` (`:105`) once per block, `start / stop / release / panic / cableChanged / portToggled / live*` and the setters between blocks (`:5-9`).
- The C API (`include/thunder/midi.h`, `src/midi/midi_api.cpp`) wraps one `MidiSched` with a 256-slot control to audio command ring (`midi_api.cpp:20`, `kMidiCmdCap` midisched.h:44).
  `thunder_midi_process` (`midi_api.cpp:146-154`) does THREE things in one call: pops and applies every posted command (`apply`, `:56-79`), `setBlock`, then `book`. It has **no way to
  call `onStep`**: `onStep` needs `SeqEvent`s and a `Seq`, which are not in a C header (midi.h:11-12 says so: "not part of this C API").
- The only block render that steps a sequencer is the offline `SongRenderer::quantum()` (`native/core/src/render/render.cpp:89-107`). `thunder_process` / `process_piece`
  (`core.cpp:123-169`) is N0's click pattern: no `Seq`, no `Players`, no `Mixer`. The live host that replaces it is U37 (PICKUP.md:20-21).
- The test double of the live cadence already exists: `native/tests/midi_drive.h:164-199` `runBlocks` (`SeqRunner::pump` then `onStep` then `book`, every `block` frames).
- `Players` does NOT yet call `seq_note_late` (grep: only `midisched.cpp:536` uses it), although U35-NOTES.md:305-307 says it must (B7).

### 2.2 The order inside one block (the one the tests already pin; do not change it)
`midi_drive.h:192-194` and `:133-136`: commands applied, then the step loop (each step: `onStep`), then `book`, then the song-end check. In the render that is, in `quantum()`:

```
t0 = qFrame_ / sr_                                   render.cpp:91
 A. midi_begin(h, t0, kRenderQuantum / sr_, fxLatSec, mix_.audibleMask())      NEW, before the step loop (after render.cpp:92)
 B. while (seq_->alive() && seq_->time() < t0 + lead && n++ < 3) {            render.cpp:95
        k = seq_->step(...)                          :96
        sched.onStep(evs_.get(), k, *step_, *seq_, t0)    NEW, between :97 and :98 (whole step first, as midi_drive.h:192)
        for each event: dispatch(evs_[i])            :98 (unchanged)
    }
 C. midi_end(h, t0, seq_->time(), seq_->bpm(), seq_->alive())                  NEW, after the loop, before "2. the voices" (render.cpp:100)
```
Arguments, exactly:
- `now` = `t0`, the block's start in the core's audio time, seconds = frames / sample rate (midi.h:101-103). Never `now + block`.
- `block_sec` = `kRenderQuantum / sr_` (render.h:31): the core steps in 128-frame quanta whatever the host's block (render.h:108-111), so MidiSched sees 128-frame blocks. F6 (U35-NOTES.md:316) makes the stream block-size independent.
- `fxLatSec` = `latencyFrames() / sr_` (`render.h:118`: `extra_ + mchain_->latency() + pdc_trim_frames(plan_, sr_)`) = the web's `moFxLat()` (`beta` index.html:17942). Read every block, no cache (A6). UNVERIFIED: that `pdc_trim_frames` gives 0 while the web would give 0 (the web's `pdcModeOf() === 'auto' && !tp.on ? 0 : T` term); it only matters while playing, where the two agree.
- `audible` = `mix_.audibleMask()` (`mix/mix.h:153`; set at `mix.cpp:196` and `:244` from `mix_audible(plan_, mute_, solo_)`, which is the web's `audible(t)` incl. buses, or the stem's forced mask). `MidiSched::setAudible` takes a `uint64_t` bit per track; tracks 32 and up are always heard (`midisched.h:85`, `audibleT`). Widen the `uint32_t`; do not shift.
- `seqTime` = `seq_->time()` (the next step's time), `bpm` = `seq_->bpm()`, `alive` = `seq_->alive()`.
- When a quantum has no steps, A and C still run: `book` releases the outbox, the clock, LFOs and glides (midi_drive.h:195 `else sched.book(now, now, 120, false)` is the stopped case).

### 2.3 The split of `thunder_midi_process` (CHANGE, small, `midi_api.cpp`)
Add `native/core/src/midi/midi_host.h` (C++ only, internal, not installed) and implement it in `midi_api.cpp` where `struct thunder_midi` is visible:
```cpp
namespace thunder {
// AUDIO THREAD only. begin: pops and applies the posted commands (apply(), midi_api.cpp:56), setBlock, setFxLatency, setAudible.
int  midi_begin(thunder_midi* h, double now, double blockSec, double fxLatSec, uint64_t audible);
MidiSched& midi_sched(thunder_midi* h);            // for onStep / onEvent (the caller owns the call order)
void midi_end(thunder_midi* h, double now, double seqTime, double bpm, bool alive);   // book + publish(h) (midi_api.cpp:62)
}
```
`thunder_midi_process` stays and becomes `midi_begin` + `midi_end` with the same argument checks, so `test_midi_api`, `test_midi_sched`, `rt_midi` do not change. `set_fx_latency` and
`set_audible` (midi.h:89-90) remain for hosts that have no mixer; with the glue they are called by `midi_begin` and their posted commands are simply overwritten each block.
Why not make the C API call `onStep`: it would put `SeqEvent` in a public C header, which `midi.h` and WORKPLAN section 3 rule 2 keep out.

### 2.4 How records leave the audio thread
Through the ring that exists: `MidiSched::ring_` (`Spsc<MidiMsg, 8192>`, `midisched.h:35`, `:165`), pushed by `push()` (`midisched.cpp:62`), popped by `drain` (`midisched.cpp:143`, C: `thunder_midi_drain` `midi_api.cpp:156`). No new ring. One consumer thread: the shell's MIDI thread (3.4). Real-time rules that already hold and must keep holding (midisched.h:6-9, midi.h:14-17):
no allocation, no lock, no I/O after the constructor; fixed arrays, a full one is counted in `thunder_midi_stats` and never a crash; the ring's drop order when the consumer stalls is clock, CC, bend, then note-on and Start, and a NOTE-OFF only at zero free (F4: `midisched.h:37-38`; `stats.may_hang`). The shell must drain at least every 5 ms (the ring holds 8192 records).
`rt_midi.cpp` already proves the entry points under the allocation and lock guard; extend it (task T7).

### 2.5 What the glue touches, and what it does not
- NEW optional hook on `SongRenderer`: `void setMidi(thunder_midi* h)` (control thread, before the first block, like `setEffects` render.h:136). Null (the default) = no change at all: every existing render golden stays bit-identical (WORKPLAN section 3 rule 3). The offline render API (`render_api.cpp`) does not set it.
- The live host (U37) calls the same three (`midi_begin`, `onStep` per step, `midi_end`) from its own block, in the same order as 2.2. The lead writes that; the call sequence and the tests of 2.2 are the contract. UNVERIFIED: the live host's structure (LIVE-ROUTE-DESIGN.md is on `n1/live-route`, which this task must not touch).
- PLAY / STOP / hold: the host posts `thunder_midi_start(h, spp)` on the control thread when it creates the `Seq`, and `thunder_midi_stop(h)` when STOP is requested or `seq_->alive()` has been false for 0.3 s after `seq_->time()` (the web's `frame()` rule, `midi_drive.h:194,152`; `beta` index.html `frame()` calls `tStop` after `tp.seq.time + 0.3`). The page's `play` sets `seq->setTime(now + 0.08)` (`midi_drive.h:135`); `midi_begin` runs `start` before the step loop so the first beat event sends Start.
- **SPP helper (CHANGE):** move `sppFor` (`native/tests/midi_drive.h:95-104`: a `Seq` with `sim = true`, stepped to the first step whose playhead is on `row`, the beat event's `v * 4` rounded and clamped 0..16383) into the core as `int thunder::midi_spp_for(const Song&, const SeqVars&, int row)` in `src/midi/midi_spp.{h,cpp}`. CONTROL THREAD ONLY (it allocates a `Seq`; U35-NOTES A9). The tests include it from there. A PLAY from the top or a sect / phrase sends Start (spp 0).
- `Players` lateness (B7, one test for pad and MIDI): add `void setDropLate(bool)` to `render/players.h` (default false, so renders stay bit-identical) and, when on, `Players::dispatch` (`players.h:229`) returns without playing a NOTE for which `seq_note_late(e.t, t0_)` is true (`t0_` from `setTime`, `players.h:231`), counting it in `PlayerStats`. A late mono note must still end the previous voice, as `MidiSched` does (`midisched.cpp:522`, "a late mono note still ends the track's previous note"). The live host turns it on; the web's rule is `beta` index.html coreMsg ("a note more than 0.15 s late is dropped", U35-NOTES.md:178).
- The mo == 1 instrument already makes no pad sound (`Players` counts `notesMidiOnly`, `players.h:217`). Do not change it.

## 3. The port table

### 3.1 Shape: one new C header, implemented in the core, no devices, no threads
`native/core/include/thunder/midiport.h` + `native/core/src/midi/midiport.cpp`, added to `native/core/CMakeLists.txt:12,25` next to `midi_api.cpp`. The core never touches a device (midi.h:2); the shell supplies a **sink**. The core object `thunder_midiout` sits between `thunder_midi_drain` and the sink: it owns the table, the clock map and the time-ordered queue; the shell's MIDI thread calls `thunder_midiout_pump` in a loop. Same style as `thunder/render.h` and `midi.h` (C API, pull, int error codes, `E_ARG` never a crash).

```c
typedef struct thunder_midiout thunder_midiout;
#define THUNDER_MIDIOUT_MAX_DEVS 32      /* devices the shell can list at once */
#define THUNDER_MIDIOUT_ID_BYTES 96      /* UTF-8, NUL-terminated */

typedef struct thunder_midiout_dev {     /* one physical output, as the shell found it */
  char id[THUNDER_MIDIOUT_ID_BYTES];     /* the web's id: "n:" + the port's name, " #2" for the second of two with the same name (beta index.html:18661-18672 midiNames / midiRemap, :18622 nmScan1) */
  char name[THUNDER_MIDIOUT_ID_BYTES];   /* what Settings shows */
  uint32_t flags;                        /* THUNDER_MIDIOUT_DEV_TS: the OS honours a send time (3.5) */
} thunder_midiout_dev;

typedef struct thunder_midiout_sink {    /* implemented by the shell; every call comes from the shell's MIDI thread (the one that calls _pump) */
  void* user;
  int64_t (*now_ns)(void* user);                                    /* the host clock the times below are in: Android CLOCK_MONOTONIC, Mac mach host time as ns, Windows QPC as ns */
  int  (*send)(void* user, int dev, const uint8_t* b, int len, int64_t at_ns);   /* dev = index in the list last given to _set_devices; at_ns <= now: send now; 0 on success, < 0 = device gone */
  void (*clear)(void* user, int dev);                               /* optional (NULL): drop what the OS still holds for the device (CoreMIDI MIDIFlushOutput; Windows midiOutReset); the queue's own CLEAR does not need it */
  uint32_t caps;                                                    /* THUNDER_MIDIOUT_CAP_TS: send() may be called up to send_ahead_ns early with a time; else the pump waits and sends at the time */
} thunder_midiout_sink;
```
Functions (the thread of each in brackets; `CONTROL` = the one thread that also calls the `thunder_midi_set_*` calls of midi.h, single producer):
```c
thunder_midiout* thunder_midiout_create(thunder_midi* core_midi, double sample_rate, const thunder_midiout_sink* sink);   /* CONTROL; NULL on a bad argument */
void thunder_midiout_free(thunder_midiout* o);                       /* CONTROL, after the pump thread has stopped */
/* The device list. The shell calls it after every scan and every hot-plug (3.3). Replaces the list; ports are re-resolved by id (3.2). Returns 0 or E_ARG. */
int thunder_midiout_set_devices(thunder_midiout* o, const thunder_midiout_dev* devs, int n);                              /* CONTROL */
/* A port letter (0..3 = A..D) names a device by id ("" = none), as the page's saved `midi.outIds` (beta index.html:17609-17632). Applies now. */
int thunder_midiout_assign(thunder_midiout* o, int port, const char* dev_id);                                             /* CONTROL */
int thunder_midiout_set_clock(thunder_midiout* o, int port, int on);                                                      /* CONTROL: the clock box of a port */
int thunder_midiout_set_gear(thunder_midiout* o, int port, double ms);                                                    /* CONTROL: -50..200, rounded (midisched.cpp:41) */
typedef struct thunder_midiout_port { int state; int dev; char id[THUNDER_MIDIOUT_ID_BYTES]; char name[THUNDER_MIDIOUT_ID_BYTES]; int clock; double gear_ms; } thunder_midiout_port;
#define THUNDER_MIDIOUT_PORT_NONE 0      /* no id chosen */
#define THUNDER_MIDIOUT_PORT_OK 1        /* id chosen and the device is listed */
#define THUNDER_MIDIOUT_PORT_MISSING 2   /* id chosen, device not listed: the page shows "(not connected)" (beta index.html:18801) */
int thunder_midiout_get_port(const thunder_midiout* o, int port, thunder_midiout_port* out);                              /* CONTROL */
/* The audio clock: core frame `frame0` is AUDIBLE at host time `host_ns0` (the output latency is already in it: 3.4). Called ~10 to 50 times a second. */
void thunder_midiout_clock_map(thunder_midiout* o, int64_t frame0, int64_t host_ns0);                                     /* any thread; wait-free (seqlock) */
/* One turn of the sender. Drains thunder_midi_drain into the queue, applies CLEAR markers, sends what is due, returns how many messages it sent. *next_ns = host time of the next
   due message (INT64_MAX if none): the shell sleeps until then or until it is woken by new records (call it every <= 2 ms: the core's ring holds 8192 records, midisched.h:35). */
int thunder_midiout_pump(thunder_midiout* o, int64_t* next_ns);                                                           /* the SENDER thread, one only */
typedef struct thunder_midiout_stats { uint32_t size; uint32_t reserved; uint64_t sent, dropped_gone, dropped_cleared, dropped_nomap, late, queue_full, max_queue, send_errors; } thunder_midiout_stats;
int thunder_midiout_stats_get(const thunder_midiout* o, thunder_midiout_stats* out);                                      /* any thread */
```
`THUNDER_MIDIOUT_SEND_AHEAD_NS` = 20 ms: with `CAP_TS` a message is handed to the OS once `at_ns - now_ns <= 20 ms`, stamped; without it the pump sleeps until 0.5 ms before and spins (the model of `beta` `io/src/midi.cc`: "sleeps until ~0.5 ms before, then spins the rest", `midiClear` drops what is queued). Both are in the pump; the sink only differs in `caps`.

### 3.2 The table's rules (what `set_devices` / `assign` do to the core)
- Ownership: the page owns the saved choices (`localStorage` key `thunderplus-midi-v1`, beta index.html:17606) and sends them in `midi.cfg` (5.5). The core holds the table only for the session (U35-NOTES A4: "port table is the shell's / U36's"; `MidiSched` holds `connected / clock_on / gear_delay` per letter).
- A port is **connected** (to `MidiSched`) iff its id names a listed device. `assign` / `set_devices` call `thunder_midi_set_port(h, port, connected, clock_on, gear_ms, cable)` (midi.h:87) while stopped, and `thunder_midi_set_cable(h, port, connected, cable)` (midi.h:96, `MidiSched::cableChanged` `midisched.cpp:648`) while playing, so notes are released, the old cable gets Stop and the new one joins the clock (web's `change` handler, beta index.html:18806-18810). "Playing" = the host told the table (`thunder_midiout_set_playing`, add it: CONTROL, 0 / 1).
- **cable id** (midi.h:85-86: 0..250, equal = one physical cable, one clock stream, one CLEAR): the device's index in the current list, so two letters on the same id share a stream (the web's `clkPorts` sends each cable once, midisched Part 1.3). A port with no device gets `200 + port` (distinct, never in the list's range). The id is in every record (midi.h:60), so a re-plug that renumbers the list cannot re-route queued messages. UNVERIFIED: that 32 devices x 4 stays under 250 (it does: 32 < 200).
- Ports never need to be re-created: `set_devices` re-resolves all four every time. A saved id whose device is plugged in later finds its letter then (web: "a saved port plugged in later finds its letter", beta index.html:18617).

### 3.3 Hot-plug and a missing port
- The shell rescans (the web rescans every 3 s: `NM.scanT`, beta index.html:18583; Android: `MidiManager.registerDeviceCallback`, desktop: libremidi's observer or a 3 s poll) and calls `set_devices` with the whole list. The core diffs it by id.
- **A listed device goes away** while its letter is chosen: the port becomes `MISSING`, the core calls `set_cable(port, 0, 200 + port)`: notes released, Stop sent to the old cable (the sink returns `< 0`: counted in `dropped_gone`, never retried), the port sends nothing from then on, exactly the web's `midiOut(p)` returning null (beta index.html:17895, "a send to a port with no cable returns silently", R-M5). The choice is NOT moved to another device (web rule: "a choice whose device is gone stays ... names can't tell which of two same-name devices left", beta index.html:18612-18615). Queued records for that cable are dropped at the next pump (`dropped_gone`).
- **It comes back:** next `set_devices` lists the id, the port is `OK`, `set_cable(port, 1, cable)`; if the song is playing and the port's clock box is ticked it joins with SPP + Continue (`joinPort`, `midisched.cpp:625-635`).
- **Two devices with one name:** ids are `n:Name` and `n:Name #2` (web). When one of a group goes the others' ids shift; the web closes and reopens the whole group. The shell does the same (re-lists and the table re-resolves). Known limit, carried over: after the first of two identical devices is unplugged a letter set to `Name` plays the one still there (HANDOVER "Still to do", beta HANDOVER.md:306-308). Android multi-port devices: UNVERIFIED how to name them (see 5.3).
- A `send` that fails (`< 0`) for a device marks it gone for the rest of that scan (no retry storm) and is reported through `stats.send_errors`.
- The page is told: the shell posts a bridge event (5.5) after every `set_devices` and every status change.

### 3.4 Timestamps: from a `MidiMsg.frame` to host time
`MidiMsg.frame` = the core's audio frame counter at the engine rate, EXCLUDING the device's output latency, INCLUDING the core's FX latency, minus the port's Gear delay (midi.h:6-9; U35-NOTES A3). So host time at which to send =
`at_ns = host_ns0 + (frame - frame0) * 1e9 / sample_rate`, where `(frame0, host_ns0)` is what `thunder_midiout_clock_map` last received: **the host time at which core frame `frame0` is heard**, i.e. output latency included. That is the only place output latency enters; the core does not add it (A3).
- **The map's smoothing is the core's** (one copy, testable): the web's `mapUpd` (beta index.html:17909-17923): the offset `at - frame/sr` smoothed 2 % per call, a jump over 50 ms taken at once, and never put the heard moment sooner than the reported base + output latency (`imp < fl` rule). `thunder_midiout_clock_map` gets the raw pair, plus `thunder_midiout_set_out_latency(o, ns)` (the floor; CONTROL) for the `fl` rule.
- **Do not use the `clock` event's `(f, wallNs)` for this**: `wallNs` is read at the start of the callback (`core.cpp:174`) and `f` is the frame AFTER the block (`core.cpp:162-164`), one block apart and not a presentation time. The shell makes its own pair at each callback it already runs.
- Frames before the first map are not sent (`dropped_nomap`). A map that has not been refreshed for 2 s is stale: the pump keeps the last one and counts it (`late`).
- **Android** (task H2): at each AAudio callback the shell knows `F_cb`, the frames it has passed to `thunder_process` so far (it adds them itself; it must equal the core's frame counter, which starts at 0 at `thunder_create`, `core.cpp:66`, and counts every piece, `:162`). `AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &pos, &tns)` (already dlsym'd in `android/app/src/main/cpp/jni.cpp:87,100-109`) gives a presented stream position and its time. Core frame `f` plays at stream position `f + (framesWritten0 - F_cb)` where `framesWritten0` is `AAudioStream_getFramesWritten` read in the same callback; so `frame0 = pos - (framesWritten0 - F_cb)`, `host_ns0 = tns`. UNVERIFIED on a device (N0 measured output latency 26 ms screen on, RESULTS-N0.md; `getTimestamp` can fail for a while after start: the shell then falls back to `host_ns0 = monoNs() at callback start`, `frame0 = F_cb`, `set_out_latency(latFrames)` from `jni.cpp:110` and accepts the smoothing).
- **Mac**: `host_ns0` = the output callback's `inTimeStamp->mHostTime` converted to ns, plus the device latency and safety offset (`kAudioDevicePropertyLatency`, `...SafetyOffset`, buffer frames). UNVERIFIED whether miniaudio exposes that stamp; if it does not, use the callback-start fallback of the Android paragraph with the device's reported latency.
- **Windows**: WASAPI: QPC at callback start + the stream's latency (`IAudioClient::GetStreamLatency`) as the fallback; `IAudioClock::GetPosition` gives a position and QPC pair for the exact map. UNVERIFIED (not read in the miniaudio vendored copy).
- Gear delay and the Start / clock rule are already in the frame (B3).

### 3.5 What each sink does with `at_ns` (send-time APIs)
| Shell | Send | Honours a time? | Clear | `caps` | Notes |
|---|---|---|---|---|---|
| Android (API 29+, `minSdk = 29`, `build.gradle.kts`) | `MidiManager.openDevice` (Kotlin) to `AMidiDevice_fromJava`, `AMidiInputPort_open`, then `AMidiInputPort_sendWithTimestamp(port, data, n, timestampNs)` (the "input port" is the one the app WRITES to; timestamp is `CLOCK_MONOTONIC` ns, 0 = now) | USB: yes per U35-NOTES.md:155 ("honoured on USB"); Bluetooth LE MIDI: UNVERIFIED | none: nothing recalls a message already handed over, hence the core's 60 ms outbox and the pump's 20 ms send-ahead | `CAP_TS` for USB, none for BT / virtual until measured | libremidi's Android backend needs a Java class not in the tree and reads with a 1 ms sleep (RESULTS-N0.md A8): do not use it; about 150 lines of JNI (A8's H2 plan) |
| macOS | CoreMIDI `MIDISend` / `MIDISendEventList` with a `MIDIPacket.timeStamp` in mach host-time units (`AudioConvertNanosToHostTime`) | yes (the driver schedules it) | `MIDIFlushOutput(endpoint)` | `CAP_TS` | libremidi's CoreMIDI backend is present (A8) and the Electron helper `io/src/midi.cc` already runs it |
| Windows | WinMM `midiOutShortMsg` (what libremidi uses in `io/` today; the owner tested WinMM, beta HANDOVER.md:130). WinRT `MidiOutPort.SendBuffer` also has no timestamp | no | `midiOutReset` (also kills sounding notes: use only at PANIC) | none: the pump waits and spins (0.5 ms) | the shell's pump thread needs `timeBeginPeriod(1)` and a TIME_CRITICAL priority (the helper does both, `io/src/midi.cc`) |
Shared by all: the pump thread (one per core) sets its own priority (Android `Process.setThreadPriority(URGENT_AUDIO)`; macOS QoS user-interactive; Windows TIME_CRITICAL). It is not the audio thread and may take a mutex around the sink.
A message sent more than 2 ms late is counted in `stats.late` (the helper's rule).

### 3.6 What is testable in the core, with no shell
A fake sink (records `(dev, bytes, at_ns)`, can fail a device, has a settable `now_ns`) and a fake clock map. New file `native/tests/test_midiport.cpp` (ctest `midiport`), plus `test_midi_glue.cpp` (ctest `midi_glue`). Each case below is a test of its own name:
1. `drain_to_sink`: run the existing ledger cases through `Drive` (`midi_drive.h`) with `thunder_midi_*` + `thunder_midiout` and the fake sink; the (time, bytes) the sink got equals what `Drive::shell` (`midi_drive.h:67-76`) kept, converted with the map. All 24 native-mode ledgers (`test_midi_ledger.cpp`), expected-difference table unchanged (`midi-ledger-expect.json`).
2. `clear_per_cable`: a CLEAR marker (`THUNDER_MIDI_F_CLEAR`) drops queued messages of ITS cable with a later frame and none of another cable (the helper's `clear()`, midi.h:7-9); messages already sent are not recalled; two letters on one device share the cable.
3. `ts_vs_spin`: a `CAP_TS` sink gets a message once it is within 20 ms with its exact `at_ns`; a non-TS sink gets it at `now >= at_ns - 0.5 ms` (fake clock stepped in 0.1 ms).
4. `clock_map`: smoothing 2 %, a 60 ms jump taken at once, the latency floor, a stale map, frames before the first map not sent. Pure functions of the map: exact.
5. `hotplug`: a listed device goes away mid-song: its letter is `MISSING`, `set_cable(port, 0, ...)` happened (Stop to the old cable reached the sink once, then `< 0`), the other port is unaffected, no record for the gone cable reaches the sink afterwards; it returns: `OK`, joins with SPP + Continue; two same-name devices (`#2` shift); the choice is never moved.
6. `ports_table`: `assign` with an unknown id gives `MISSING` and a later `set_devices` fixes it; `set_gear` clamps -50..200; `set_clock` forwards; bad arguments (NULL, port 4, a 200-byte id, `n` 33) return `E_ARG` and change nothing; `get_port` after every step.
7. `stall`: the pump not called for 400 ms: no burst, nothing sent later than `at_ns + 20 ms` is delayed beyond what the fake clock says (late ones are counted in `late` and dropped for clock ticks; notes follow `seq_note_late`'s verdict already made in the core).
8. `rt_midiport` (extend `rt_midi.cpp`, `tests/CMakeLists.txt:102`): the pump allocates nothing (fixed queue, 4096 entries, `queue_full` counted). The pump is not the audio thread; the rule is kept for a simpler shell, not demanded by WORKPLAN.
9. `glue_cadence` (`test_midi_glue`): `SongRenderer` with `setMidi` over `midi-ledger-*.song.json` songs at host blocks 1, 96, 128, 480, 4096: the record stream (frames, bytes, order) is identical to `runBlocks` (`midi_drive.h:164`); no-hook render is bit-identical to today's (`render` goldens).
10. `glue_audible_fx`: solo / mute / a bus (the case `muted-solo`) and `setMasterChain` (FX latency from `latencyFrames()`): the records move by exactly `latencyFrames()`.
11. `players_late`: with `setDropLate(true)` a note 0.16 s late is not played by the pad and not sent for an OUTPUT 2 instrument; for an OUTPUT 1 (MIDI only) it IS sent (F8); a late mono note still ends the previous (U35-NOTES 4.7 F8).
12. `spp_for`: `midi_spp_for` equals `sppFor`'s results on `start-midway`'s row.
Under sanitizers the same tests run in CI (`sanitizers` job, 120 minutes, PICKUP.md:36); `midi_api --threads` pattern (midi_api_threads) is the template for a three-thread test: control, audio (fake), sender.
Not testable without a device: that an OS honours a timestamp, hot-plug events, latency figures, the AAudio and CoreAudio maps. Those are H2 / H7 owner tests (WORKPLAN.md:171,196).

## 4. Decisions already made that bind this work
(Quote, then source.)
1. "late notes for gear-only instruments are sent, not dropped" (PICKUP.md:49). F8: a note for a MIDI-only instrument (OUTPUT 1) is never dropped for lateness: sent at once, late (clamped to `now`), not counted; only OUTPUT 2 drops its MIDI note when the pad voice was dropped (U35-NOTES.md 4.7 F8; `midisched.cpp:522`). Late ticks are still dropped.
2. "a note-off always wins on a full queue" (PICKUP.md:49). F4: ring priorities, `may_hang` after a dropped OFF (midisched.h:37-38; midi.h:69-75). The sender's queue repeats the rule: when it is full, drop clock ticks, then CCs, then note-ons, never a note-off, CLEAR or Stop (add to `queue_full` handling).
3. "the MIDI clock is realigned after a TPO command" (PICKUP.md:49); B12: each interval is integrated with the tempo in force when its step started (U35-NOTES.md:226). Nothing in the glue may read `beat` events' own beat for the clock; `onStep` gets the `Seq` for that.
4. The owner, 19 Nov 20:45: "the suspected web bugs are FIXED IN THE NATIVE PORT, not copied" (U35-NOTES.md:189): B1 to B9; B10 / A5 kept (no RPN, no bank select); B11 struck.
5. A3: the core's frames exclude the device's output latency (the shell adds it) and include the core's FX latency; A6: FX latency read every block, no cache (U35-NOTES.md Rulings, last paragraph: "A3 accepted ... A6: no cache"). A4: the port table is the shell's / U36's. A7: the hidden-page rules (`MOLED`, the 1.2 s look-ahead, `seqHandover`) are not ported. A9: SPP on the control thread with a `Seq`. A10: clock-in is phase 2.
6. "The core never touches a MIDI device" (midi.h:2). `MidiSched` makes records; "a shell sends them". No device code, no thread, no `std::function` in `native/core` headers (WORKPLAN.md:98-99 rule 2: "no framework types in `native/core/` headers, no exceptions or RTTI on the audio path").
7. Threads (midi.h:13-17): three roles, one thread each: CONTROL (single producer: "one control thread, or the caller serialises them; debug builds assert it", F10), AUDIO, CONSUMER (`thunder_midi_drain`, `stats_get`). `thunder_midiout` adds a fourth name for the same CONSUMER: the sender thread; it must be the only thread that calls `thunder_midi_drain`.
8. Time is frames on the core's sample timeline, never seconds or `performance.now()` (CORE-BRIDGE.md:46-47); the core's callback allocates nothing, locks nothing, does no I/O (core.cpp:6-8; WORKPLAN.md:98).
9. The outbox lead is 60 ms (`kDefaultLeadSec`, midisched.h:42; the web's `MOB_AHEAD = 60`, beta index.html:17951). Raising it is allowed (`thunder_midi_set_lead`, 5 ms to 500 ms); the glue keeps the default until measured.
10. "Stop is stamped at the stop time" (B8) and Gear delay applies to Start / Continue / SPP / ticks / Stop (B3). Do not subtract Gear delay again in the shell.
11. Every message a shell sees is valid: status >= 0x80, no SysEx, lengths 1 to 3, data < 0x80 (`nmValid`, the Cirklon "Sysex interrupt", beta HANDOVER.md:151-153; the core refuses the rest). The sink asserts it in debug builds.
12. Per cable CLEAR: every record carries the cable id the port had when the message was MADE (midi.h:60; U35-NOTES.md 4.5 item 4); a cable change keeps queued messages on the old cable.
13. Process rules for the worker (PICKUP.md:7-14): own `git worktree` under `/home/claude/wt/`, push work in progress every 30 minutes with `docs/port/U36-NOTES.md` current, at most two compiling workers (2 cores), never `pkill`, one git network operation at a time, no CI runs on a push to `native` (only on PRs), `gh` through REST only. Outside `native/` only `docs/port/**` may change (WORKPLAN.md section 0, "Where things live").
14. The web app on `beta` is never edited from `native` (WORKPLAN.md section 1). Bugs found: written under "Bugs still open" in HANDOVER.md by the lead.

## 5. OPEN QUESTIONS for the lead
Each: options, recommendation, what ports most easily across Android / Mac / Windows.
1. **Who sends on time.** (a) The core's `thunder_midiout_pump` does the waiting and calls `sink.send` at the time (spin / sleep), on every platform. (b) Each shell hands everything to the OS with a timestamp (AMidi, CoreMIDI) and Windows alone gets (a). (c) as proposed in 3.1: one pump, `CAP_TS` lets a shell hand messages to the OS up to 20 ms early. **Recommend (c).** Easiest: (a) is identical on all three, one code path, fully testable in the core; (c) is (a) plus one `if`, so Mac and USB-Android get OS-side timing without a second design. Android's pump thread is not a real-time thread: a 20 ms send-ahead with `CAP_TS` removes its jitter on USB; on Bluetooth LE MIDI the OS timing is UNVERIFIED, so keep a 60 ms outbox and measure (the owner's USB interface test, WORKPLAN.md:384).
2. **Where this code lives.** (a) `native/core/src/midi/` as proposed (pure C++, no devices, tested by ctest). (b) a new `native/shell-common/`. **Recommend (a).** WORKPLAN.md:287 lists only `native/core, android, desktop, ios, tests, tools`; a new folder is a change to the plan; (a) is built once by every shell's CMake, so Android, Mac and Windows share the same object code.
3. **Device ids and multi-port devices.** (a) The web's id (`n:` + name, ` #2`) so the saved `outIds` / `names` map across engines and across the web copy (beta index.html:18661-18672). (b) A stable id per OS (Android `MidiDeviceInfo` id + port number; CoreMIDI unique id; WinMM name + index). **Recommend (a) now, plus `n:Name / PortName` for a device with several outputs**, and (b) as a later field `stable_id` when the owner has two identical devices (HANDOVER's open item). Easiest: names exist on all three; Android's `MidiDeviceInfo.getId()` is not stable across reboots (UNVERIFIED), CoreMIDI's unique ids are, WinMM has none.
4. **A device goes away while playing.** (a) The web rule, kept (the letter keeps its id, shows "(not connected)", sends nothing, rejoins when back). (b) Fall back to another device with the same name. (c) Stop the song and show a toast. **Recommend (a)** plus the bridge event and the counter; (c) is wrong for live use. Same on all three; only the OS callback differs.
5. **`midi.cfg` shape on the frozen bridge** (CORE-BRIDGE.md:62: `{outs:[...], clockOut, clockIn, gearDelay}`, BRIDGE_V = 1 frozen in section 6). The page has four letters and a Gear delay PER port (beta index.html:17621, `lats`). (a) Read it as `{outs:[id,id,id,id], clockOut:[bool x4], gearDelay:[ms x4], clockIn}` (arrays of four; the row's words allow it). (b) Add a second message `midi.port`. **Recommend (a)** and say so in CORE-BRIDGE.md (a one-line note, lead's decision to edit). New upward events are additive (CORE-BRIDGE.md:44-45 "unknown types are ignored"): `midi.ports {devs:[{id,name}], ports:[{state,id,gear,clock}], late, sent}` after every scan (reuse of the `dev` event, `{kind:'changed'}`, CORE-BRIDGE.md:78, is the alternative; recommend a new type, it carries the list the Settings table needs). Same on all shells (the bridge is the shells' shared layer).
6. **Where the glue's first user is.** (a) Only `SongRenderer::setMidi` now, the live host (U37) copies the three calls later. (b) Wait for U37. **Recommend (a):** it makes the glue testable now (3.6 cases 9 to 11) and costs one null-checked pointer; U37's author gets a tested contract. Platform-independent.
7. **Sender queue full or pump stalled.** (a) Drop by the core's F4 order (clock, CC, bend, note-on; never a note-off or CLEAR) and set `may_hang`; the shell sends PANIC. (b) Block the audio thread. **Recommend (a)** (b is forbidden by midi.h:14-17). Same everywhere.
8. **Clock IN, MIDI IN, thru and Measure.** One phase 2 (U35 A10) or split? **Recommend one phase 2, in this order: MIDI in to bridge events, then clock in, then Measure** (it needs both). Android: AMidi output port + a `poll` reader (RESULTS-N0.md A8); Mac / Windows: libremidi inputs as the helper's; the timestamp of an input message is the shell's.
9. **Windows API.** (a) WinMM through libremidi (what the helper uses and the owner has tested, beta HANDOVER.md:130). (b) WinRT `MidiOutPort`. (c) the newer Windows MIDI Services. **Recommend (a)**; (c) UNVERIFIED (not read). WinMM's `midiOutShortMsg` is synchronous but short messages return at once; no timestamps: the core pump covers it.
10. **Send-ahead and lead numbers.** 20 ms send-ahead and 60 ms lead are copied from the web's outbox and the helper (U35-NOTES.md:36); the real figures wait for a measurement on a USB interface (H2) and the Windows PC (H7). **Recommend** shipping them as constants in `midiport.h` and a debug `stats`, and tuning from `late` / `max_queue` after the owner's test.

## 6. Task list, in build order (each: the done-test; one PR into `native`, `@coderabbitai review`, Fable F-review of T2, T3, T5, T6 because they sit on the audio thread or the time map)
- **T1. SPP helper into the core.** Move `sppFor` to `src/midi/midi_spp.{h,cpp}` (`thunder::midi_spp_for`); `native/tests/midi_drive.h:95` includes it. Done when: `midi_ledger`, `midi_sched` pass unchanged and test `spp_for` (3.6 case 12) passes.
- **T2. Split `thunder_midi_process`.** `midi_host.h` (`midi_begin`, `midi_sched`, `midi_end`), `thunder_midi_process` rebuilt on them. Done when: `midi_api`, `midi_api_threads`, `midi_sched`, `rt_midi`, `rt_midi_selftest` pass with no edit; a new check shows `midi_begin` + `onStep` + `midi_end` gives the same records as `runBlocks` for the 24 ledgers.
- **T3. `SongRenderer::setMidi` (2.2).** Edit `render.h` / `render.cpp:89-107` only; `Mixer::audibleMask` widened to the `uint64_t` argument. Done when: `midi_glue` cases `glue_cadence` and `glue_audible_fx` pass and every existing render, stem and golden test (`render_*`, `stem*`, `rt_core`) is bit-identical (the hook is null).
- **T4. `Players::setDropLate`.** Done when: `players_late` passes and the existing `players` tests are unchanged (default off).
- **T5. `midiport.h` / `midiport.cpp`:** table, queue, CLEAR, pump with TS / spin, bad arguments. Done when: `midiport` cases 1, 2, 3, 5 (without the clock map), 6, 7 pass under ASan / UBSan and `midi_api --threads`-style three-thread test (control, audio, sender) is clean under TSan.
- **T6. The clock map and `set_out_latency`.** Done when: case 4 passes and case 1 passes with a map that has output latency and a 1000 ppm drift.
- **T7. Real-time guard.** Extend `rt_midi.cpp` to run `midi_begin / onStep / midi_end` inside `SongRenderer::quantum()` under the allocation and lock guard, and the pump under the allocation guard. Done when: `rt_midi` and `rt_midi_selftest` pass; the selftest still fails on a deliberate `new` in the glue.
- **T8. Notes.** `docs/port/U36-NOTES.md` (state, checklist, the decisions the worker had to take, which open questions the lead answered) and CORE-BRIDGE.md note only if the lead says so (5.5). Done when: the notes list every UNVERIFIED of this spec as checked or still open.
- **T9. Android sink (task H2's first half).** `native/android/app/src/main/java/app/thunderplus/`: `MidiPorts.kt` (`MidiManager`, `registerDeviceCallback`, `openDevice`, the device list, hot-plug), `jni.cpp` additions (AMidi: `AMidiDevice_fromJava`, `AMidiInputPort_open / sendWithTimestamp / close`; the sink; the pump thread with URGENT_AUDIO priority; the clock map from `jni.cpp:100-109`); CMake links `amidi` (`CMakeLists.txt` in `android/app/src/main/cpp`, minSdk 29 already). Compile only in CI; no MIDI permission is needed for USB (UNVERIFIED; Bluetooth needs `BLUETOOTH_CONNECT` on API 31+ and `android.software.midi` as `uses-feature required=false`, `AndroidManifest.xml` has none of these today). Done when: the debug APK builds in CI and the shell's unit test of the Kotlin diffing of device lists passes; the device test is the owner's (loopback with a USB MIDI interface, WORKPLAN.md:171,384).
- **T10. Desktop sink (N4 task H7, not this slice's build, listed so the API is not changed later):** CoreMIDI and WinMM sinks over `io/src/midi.cc`'s libremidi logic; the same pump. Done when: the C API of T5 needed no change.

## 7. UNVERIFIED, collected (the builder checks each and reports in U36-NOTES.md)
1. The live host's structure and whether it keeps `SongRenderer`'s 128-frame quantum (2.2, 2.5).
2. `pdc_trim_frames` versus the web's "auto and stopped is 0" term (2.2).
3. That `Mixer::audibleMask()` equals the web's `audible(t)` for every bus / solo case (it is `mix_audible`, `mix.cpp:196`).
4. AAudio's `getTimestamp` map recipe on the Fold (3.4); CoreAudio's `mHostTime` through miniaudio; WASAPI's clock pair.
5. Whether Android honours a timestamp over Bluetooth LE MIDI; whether USB honours it on the owner's phone and interface (3.5).
6. Android manifest and permission needs; the stability of `MidiDeviceInfo.getId()`; naming of multi-port devices (3.3, 5.3).
7. Windows MIDI Services (5.9).
8. That the pump can meet "drain every 2 ms" on Android without a real-time priority (the helper's 2 ms poll is a Node thread, not Android).

## 8. The lead's rulings (Fable, 20 Nov 21:40 Mountain; the builder follows these)
- **5.1 to 5.10: every recommendation is accepted as written** (5.1 c: one core pump, `CAP_TS` hands over 20 ms early; 5.2 a: `native/core/src/midi/`; 5.3 a with `n:Name / PortName`, `stable_id` later; 5.4 a; 5.5 a: `midi.cfg` read as arrays of four plus the new upward event `midi.ports`, and the builder adds the one-line note to CORE-BRIDGE.md, BRIDGE_V stays 1; 5.6 a; 5.7 a; 5.8 one phase 2 in the order given; 5.9 a: WinMM through libremidi; 5.10 constants + stats, tuned after the owner's device tests).
- **Build order changed to keep clear of open work in the song renderer:** PR A = T1, T2, T5, T6 and the pump half of T7 (new files plus `midi_api.cpp`; nothing in `render/` or `mix/`). PR B = T3, T4, the rest of T7: it edits `render.cpp` / `players.h`, so it starts only after #265 (`n1/live-route`) and `n1/bus-into-bus` are merged, rebased on them. T9 (the Android sink) after PR A. T10 stays with N4.
- 2.2's `audible`: PR B checks UNVERIFIED 3 against the bus-into-bus plan (transitive solo) before it widens the mask; if `audibleMask()` and the web differ for a chained bus, stop and write it up.
- F-review by the lead: PR A's pump and clock map, all of PR B.
