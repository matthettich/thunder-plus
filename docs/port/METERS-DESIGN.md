# METERS-DESIGN: effect and chains level meters on the new engine (DRAFT)

**Status: DRAFT, 25 Nov 2026. Written by a builder on a cheaper model; the lead reads it on the top model before anything is built.** Nothing here is built, and no code changed. Owner, 25 Nov: "effect and chains level meters" come FIRST on the new engine, before pad flashes and the click.

Sources read: `native/core/include/thunder/live.h` section 8 and `thunder_live_meters_want`; `native/core/src/meter/meter.h`; `native/core/src/fx/fxhost.h` / `fxhost.cpp` / `fxpump.cpp`; `native/core/src/master/master.h`, `chain.h`; `native/core/src/chains/chains.h`, `cengine.cpp`; `native/core/src/live/live_host.{h,cpp}`; the shell's `live_events.cpp`, `live_jni.cpp` (`origin/n1/n2-anr`); `index.html` on `origin/beta` (line numbers below are that file as fetched on 25 Nov); `docs/port/CORE-BRIDGE.md` sections 3, 4, 6.8, 7 (G6) and `docs/port/N2-PAGE-CLIENT.md` (G6 rows).

## 1. What the core posts today

One record, `THUNDER_LIVE_EV_METERS` (type 4), 304 bytes: the 16-byte head, `float a[70]`, `float reserved[2]` (`reserved[0]` is the MeterBank's own count). Only with `THUNDER_LIVE_F_METERS`, and only after `thunder_live_meters_want(L, hz)` with `0 < hz <= 30` (the shell asks for 10 Hz at `hello`, `live_jni.cpp:339`; the bridge's `meters.want {rate}` is the page's switch). Posted from `LiveHost::postMeters()` in step 9, on the big (non-critical) ring.

What the 70 floats are (`meter.h`, CORE-BRIDGE section 6 item 8): 35 slots of `[2s]` peak, `[2s+1]` RMS, linear:
- slots 0..31: the tracks. Window: the last 512 samples of the MONO mix (L+R)/2 after VOL / TRIM / pan (as the page's per-strip analyser, fftSize 512);
- slot 32: the preview bus;
- slots 33 and 34: master left and right, 1024-sample windows (the page's two master analysers).

Decay, holds and floors stay with the page. That is all. There is no per-slot effect level, no dynamics reduction, no chains reading, no master reduction, and no input (TRIM) peak on the bridge. CORE-BRIDGE section 7 deferred them as **G6** ("FX / chains / master / TRIM meters"); section 6 item 8 says "the FX bar's per-slot peaks come with U14; the master processor's own reduction meters are a separate small message".

## 2. What the web app draws, and from where

| UI | Reads | Where (`index.html`) | In the 70 floats? |
|---|---|---|---|
| Strip / Song / Mixer track meters (`b.lv`), master pair (`rt.mlv`, `mpk`) | `AnalyserNode` peak (mono, 512 / 1024), decay 0.86 per 8.3 ms, hold 1.5 s | 36159-36185 | **Yes** |
| Mixer TRIM light (`b.ipk`) | analyser `b.ai` after TRIM, peak held 0.6 s | 36166-36170 | **No** (input side) |
| Deck strip meter (focus track or master out, L and R separately) | two analysers, fftSize 512 | 33201-33215 | Mono only: **no L / R** for a track; master L / R yes |
| **FX bar** slot meters (`fxSlotLevel`) | the effects worklet's `{lvl:[8]}`: each slot's peak out, max(abs L, abs R), posted every 12th block (about 31 a second); on only for ONE node at a time (`fxLvlSet`), age limit 400 ms, drawn on -54..0 dB | 32461-32468, 33607-33625, worklet 21212, 21662 | **No** |
| **FX dynamics** (FX screen, FX bar's `dkDynMeters`, stage bars) | the worklet's `{dyn:[[slot, key, [reduction dB, level in dB, peak out dB]]...]}`, every 12th block, while `{tel:true}` for one node (`fxTelSet`); 80-point history on the page (`fxDynFeed`) | 32421-32455, 32572-32603, 33630-33660, worklet 21201-21227, 21660 | **No** |
| …PUMP METER / LIMIT loudness readouts | the worklet's `{pump:...}` / `ptel` snapshot of up to 32 floats per slot, only for slots whose pop-up wants it | `PumpFX._feed`, 21664, 32605 | **No** |
| **…chains screen** track meters | page analysers on each track's chains node: `peaksIn[t]` and `peaks[t]` (what goes in, what comes out), every 60 ms while the chains screen is open, passed to the iframe as `api.meters({peaks, peaksIn, load, mods, shows, jacks, byp})` | 35995-36010 (`chMeterWanted`, `chainsTelemetry`, 19059-19069, 21470) | **No** |
| chains `mods`, `shows` (Meter / Limiter module displays), `jacks` (per-jack meters), `byp` | the chains worklet's own telemetry, only while `telemetry: true` | 36000-36008 | **No**, and **not computed by the core either** (see 3) |
| Master dynamics (comp / limiter reduction, suppressor bands, in / out peak, true peak, LUFS, load) | the master worklet's `{meter:{inPk, outPk, tp, inLu, outLu, load, comp, compIn, lim, sup}}` into `mstMeter` | 24797-24807, 32827, 32846, 32908, 35907, 36060 | **No** |

The page already decides per screen what it needs (one FX node at a time, the chains screen only while shown, master when its panel is open). The native design should keep that: ask for exactly what the showing screen draws (CORE-BRIDGE section 3 already lists `meters.want {rate, tracks, fx:[...], spec}` for this).

## 3. What the core already computes and does not post

- **`FxHost` level out (`lvl_`, `lvlP_`, `lvlOut_[8]`).** Exactly the page's `{lvl}`: for each of the 8 slots, the peak of max(abs L, abs R) of the slot's output, reset every 12th block (`fxhost.cpp:394-396`). `setLevels(bool)` switches it; `popLevels(float[8])` hands it over once per post. (`fxhost.h:173, 189`.) It is unused by the live host: nothing calls `setLevels` there (only tests and `rt_harness.cpp`). Pump slots count only when on (`fxhost.cpp:378`).
- **`FxHost` dynamics (`tel_`, `dynOut_[8]`, `FxDyn {slot, key[24], v[3]}`).** Exactly the page's `{dyn}`: `v = [reduction dB, level in dB, peak out dB]`, for the dynamics slots (types 7, 26, 28: the level-only ones), and for pump COMP / LIMIT / BAND through `pumpTelSlot` (`fxpump.cpp:315-327`). `setTelemetry(bool)`, `popDyn(FxDyn*, max)`. Also `popPumpTel` (`PumpTelOut`: the METER / LIMIT loudness snapshot, 32 floats). Unused by the live host as well.
- **Both run on the audio thread already** (between blocks; the 12-block counter and the copy into `lvlOut_` / `dynOut_` are in `FxHost::process`), allocation-free, so posting them is a copy.
- **Chains.** `ChainsEngine` has only a `telemetryOn_` flag (`cengine.cpp:147, 180`) that chooses which modules stay active; it computes NO `mods`, `shows`, `jacks`, `byp`, per-track in / out peaks or `load` for the page. So the chains numbers are new core work, not a post of something that exists.
- **Master.** `MasterProcessor` computes `MasterMeter {inPk, outPk, tp, inLu, outLu, load, comp, compIn, lim, hasSup, sup[48]}` and `popMeter` (queue 8, about 20 a second), but `MasterChain` builds both its processors with `meters = false` (`chain.cpp:76`), so nothing is computed live. Turning it on costs the loudness (BS.1770) filters and the per-band suppressor read-out; not measured.
- **TRIM and per-channel strip.** The mixer holds `trim_[]`; the input-side peak is not tapped. The 70-float record is mono post-pan for tracks.

## 4. Proposal

**Principle:** keep `METERS` (type 4) untouched (BRIDGE_V stays 1, additive). Add small, separate records, each on the big ring, each only while subscribed. New numbers 11 and up (types 1..10 are taken; `THUNDER_LIVE_EV_MAX` 320 is the largest record, and all below fit).

### 4.1 Records (section 8 rules: packed, little-endian, 8-byte aligned, `bytes` a multiple of 8, head first)

All floats are linear unless marked dB; a head's `f` is the frame at the end of the window.

| Type | Name | Size | Body after the 16-byte head | Posts |
|---|---|---|---|---|
| 11 | `FXLVL` | 56 | `int32 track` (0..31 tracks, 32 the master's effects), `uint32 slots` (bit k: slot k reported), `float lvl[8]` (peak out per slot, `FxHost::popLevels`) | one per watched track per `FxHost` post (every 12th block, about 31 / s at 48 kHz) |
| 12 | `FXDYN` | 152 | `int16 track`, `int16 n` (0..8), `int32 reserved`, then `n` x 16-byte entries `{int16 slot, int16 type, float red_dB, float in_dB, float out_dB}`; the unused tail is zero | one per watched track per `FxHost` dynamics post (every 12th block, only when the track has a dynamics slot) |
| 13 | `CHLVL` | 280 | `uint32 valid` (bit t: track t has a chains node), `uint32 reserved`, `float in[32]`, `float out[32]` (peak in, peak out of each track's chains node) | at the wanted rate, default 16 / s (the page's 60 ms timer) |
| 14 | `MASTERM` | 248 | `float inPk, outPk, tp, inLu, outLu, comp, compIn, lim, load`, `uint32 flags` (bit 0: `sup` present), `float sup[48]` (dB) | the processor's own cadence, about 20 / s |
| 15 | `TRIMLVL` (optional, see Q7) | 152 | `uint32 valid`, `uint32 reserved`, `float pk[32]` (peak after TRIM, before the strip) | at the wanted rate |
| 16 | `FXPTEL` (phase 2) | 152 | `int16 track, int16 slot, int16 type, int16 n`, `float t[32]` (pump METER / LIMIT snapshot) | per watched slot, 5 / s |

A type the shell does not know is skipped by `bytes` (existing rule), so an older shell is safe.

dB values for `FXDYN` and `MASTERM` follow the worklets' own numbers (`-180` for silence), so the page's `fxDynFeed` and master code run unchanged. Peaks in `FXLVL` are linear, as the page's `n.lvl` is (the page maps -54..0 dB itself, `fxSlotLevel`).

### 4.2 Subscription (control side)

The existing `thunder_live_meters_want(L, hz)` stays. Add one call, `thunder_live_meters_set(L, const thunder_live_meters_spec*)`, a small struct: `double rate_hz` (METERS, as today), `uint32 kinds` (bits: FXLVL, FXDYN, CHLVL, MASTERM, TRIMLVL, FXPTEL), `uint64 fx_tracks` (bit t, t 0..31, bit 32 master: which `FxHost`s report), `double chains_hz`. Needs the matching flag (`F_METERS`; `E_STATE` otherwise). It travels as one `LiveCmd::Meters`-style command (`LiveCmd` has `x`, `y`, `i0`, `i64a`: enough, no new ring); applied at the top of the next quantum, which calls `setLevels` / `setTelemetry` on the watched hosts and `false` on the rest, and on the master processors' `meters` switch. The spec is a REPLACEMENT state, so the page can re-send it any time (after `engine up`, after `hello`). A cap on watched `FxHost`s (suggest 4) keeps the cost bounded; more is `E_ARG`.

### 4.3 Which thread fills them (audio thread: no allocation, no lock)

- `FXLVL`, `FXDYN`: step 9 of the quantum, after the block: for each watched track, `host(t).popLevels(...)` / `popDyn(...)` (both are plain copies of arrays that `FxHost::process` filled in the same thread); if they returned data, build the record on the stack and `post()`. No new maths. `FxDyn.key` is not carried (24 bytes of text; see Q4).
- `CHLVL`: new. A running peak per track (in, out) kept in fixed arrays by the chains pass (`commitChains` / the chains host's per-block mix), reset at each post, so it is `max(abs)` over the block for 64 numbers. The input side is the chains node's trackIn, the output side the node's bus sum into the mixer. Cost: one pass over 2 x block for each active track.
- `MASTERM`: `MasterProcessor::popMeter` (its own queue, 8 deep) drained in step 9; needs `meters = true` on the mastering processor (Q5).
- No record ever blocks; a full ring refuses the newest and counts it (`evt_dropped`), as for `METERS`.
- Ring budget: all metered streams on at once is about 31 x 4 (watched FX tracks, lvl) + 31 x 4 (dyn) + 16 (chains) + 20 (master) + 30 (METERS) of at most 150 bytes, about 400 records / s; the 1024-record ring holds 2.5 s at a 100 ms poll. Fine.

### 4.4 The shell's JSON form

`live_events.cpp` decodes by type and emits one JSON line per record, like `meters` (`{t:'meters', seq, at, a:[...]}`). Proposed (each also carries `f` and `seq`; floats printed to 4 decimals, and 3 for dB, to keep lines short):

```
{t:'fx.lvl',  seq, f, tr, a:[8]}                         // a[k]: peak of slot k, 0 when the slot did not report
{t:'fx.dyn',  seq, f, tr, d:[[slot, type, red, in, out], ...]}
{t:'chains.lvl', seq, f, i:[32], o:[32]}                 // null entries for tracks without a node
{t:'master.meters', seq, f, inPk, outPk, tp, inLu, outLu, comp, compIn, lim, load, sup:[48] | null}
{t:'trim.lvl', seq, f, a:[32]}                           // only if Q7 says yes
{t:'fx.ptel', seq, f, tr, slot, type, a:[n]}              // phase 2
```

`tr` is the track, 32 the master's effects (the page's `-1` is mapped by the client). `master.meters` has the same field names as the page's `mstMeter` object, so the page code that reads `mstMeter.comp`, `.lim`, `.sup`, `.load` needs no change.

The page's request, `meters.want`, becomes (replacing the loose sketch in CORE-BRIDGE section 3): `{t:'meters.want', rate, fx:[tracks with the FX bar or FX screen shown (0..31, 32 = master)], dyn:[tracks whose dynamics are shown], chains:bool, master:bool, trim:bool}`. The shell maps it to `thunder_live_meters_set`; it is a replacement state.

### 4.5 The page side (PC5's meter view)

PC5 already plans "a small view object that replaces the Web Audio analysers" (N2-PAGE-CLIENT section 4.7; today the missing numbers show empty, G6). With these records:

- **Subscription:** `fxLvlSet(node)` / `fxTelSet(node)` (index.html 32463, 32469) and `chainsTelemetry` / `showChains` / `leaveChains` (19060-19069) each become a call that updates a single "wanted" object in the client and sends `meters.want` 100 ms after the last change (debounced, whole state, re-sent on `engine up`). `node` becomes a track number (master = 32); nothing else about "one node at a time" changes.
- **FX bar:** `fxSlotLevel(t, k)` reads `view.fxLvl[t][k]` and its timestamp (the 400 ms age rule stays), same -54..0 dB map. The web path keeps reading `n.lvl`.
- **Dynamics:** the `fx.dyn` handler calls the existing `fxDynIn(view, list)` with `[slot, key, [red, in, out]]`, the key built from the page's own `S.tfx[tr][slot].type` (`slot + ':' + type`); no history work is new (`fxDynFeed` keeps its 80-point buffer).
- **Chains screen:** the 60 ms timer builds `api.meters({peaks:out, peaksIn:inp, load, mods:[], shows:[], jacks:[], byp:[]})` from `chains.lvl` instead of the analysers; `load` from the `clock` event's `load` (G9). The empty `mods`, `shows`, `jacks`, `byp` mean the editor's live slider modulation, Meter / Limiter displays, jack meters and bypass lights stay dark in native mode in phase 1 (Q2).
- **Master:** `mstMeter = event` (sup null becomes undefined) and the CPU panel's "Master dynamics" line.
- **Fallback:** any panel's data older than its age limit shows as silent, never as an old value (the page already does this for `lvl` and `dyn`). If the shell does not know a type the page asked for (`hello.caps.meters2` absent), the panel stays empty as today; no fallback to the old engine is needed.
- The page keeps owning decay, holds, floors, paint cadence and the `meterHold()` rule (METERS: OFF while playing): while `meterHold()` is true the page sends a `meters.want` with the subscriptions off, so the core does no work either.

### 4.6 Rates

| Stream | Rate | Why |
|---|---|---|
| `METERS` | as today, wanted <= 30 | unchanged |
| `FXLVL`, `FXDYN` | every 12th block (the FxHost's own counter): 31.25 / s at 48 kHz, 28.7 / s at 44.1 kHz | the page's own numbers, so the existing smoothing and 400 / 600 ms age limits apply as they are |
| `CHLVL` | 16 / s, adjustable up to 30 | the page's chains screen timer is 60 ms |
| `MASTERM` | the processor's cadence (about 20 / s) | as the page's master worklet |
| `FXPTEL` | 5 / s | pump pop-ups' own cadence |

## 5. Open questions for the lead

1. **Scope of "chains level meters"**: is the owner's meaning only the per-track in / out peaks on the chains screen (`CHLVL`: cheap, phase 1), or also the editor's live-modulation sliders, Meter / Limiter displays and jack meters (`mods`, `shows`, `jacks`, `byp`)? The latter is new ChainsHost work (the core computes none of it), roughly 12 to 20 builder-hours and a separate design; this draft only reserves a place for it. Which does the owner expect "first"?
2. **Phase 1 with dark chains extras**: acceptable that `mods`, `shows`, `jacks`, `byp` stay empty in native mode until phase 2? (Today's G6 behaviour for them.)
3. **How many tracks watched at once.** The web shows one FX node at a time. Is that a rule for native too (cap 1, cheapest) or may the Mixer show several (cap 4 proposed)? The Mixer's own FX bar in the owner's use: one?
4. **Key in `FXDYN`**: the page's `fxDynIn` rejects a report whose key no longer matches the slot's type (a plan was just committed). I propose carrying a 16-bit `type` id in the entry and letting the page compare it to its own table (needs the id mapping in `fxtable`), or, simpler, carrying nothing and accepting up to one stale post of 32 ms after a swap. Which?
5. **Master meter cost and which processor.** `MasterChain` has two processors (mastering and the safety limiter), both built with `meters = false`. Turning the mastering one on costs the BS.1770 loudness filters and the 48-band suppressor read-out (not measured on the Fold). Wanted only while the master panel shows (my proposal). Should the safety limiter's reduction also be posted (a separate `lim2` field), or is it never shown?
6. **Meter of the pad and send paths**: the page's FX bar is also used for the master's effects (`rt.mfx`) and bus tracks above 32; this draft covers tracks 0..31 and the master (32). Preview bus effects: none today.
7. **TRIM light and per-channel strip meter**: not part of "effect and chains meters". Include `TRIMLVL` (about 2 to 3 builder-hours) and a stereo focus-track pair (the Deck strip reads L and R separately, the 70 floats are mono)? Without them native mode shows no TRIM light and the Deck strip uses the mono value for both bars. (CORE-BRIDGE G6 already lists the TRIM light as a gap.)
8. **Contract wording**: this needs a new sub-section in CORE-BRIDGE section 7.x (events 11..16, `meters.want` fields, `hello` caps `meters2`) and the section 4 rate table; the lead's call whether it is frozen with BRIDGE_V 1 (additive) or waits for N3.
9. **Time base**: the page stamps ages with its own clock when an event arrives (as `n.lvlT`). Any need for the core's `f` on these records on the page (to align to the song)? I assumed no.
10. **Command ring**: the spec fits `LiveCmd` (`x`, `y`, `i0`, `i64a`); OK to extend the existing `Meters` command rather than add a type? It changes what `thunder_live_meters_want` means internally only.

## 6. Work split

Builder-hours are a guess for a mid-size model working from this draft, including tests, not including the owner's device test.

| Unit | What | Files | Hours |
|---|---|---|---|
| **M1 core: FX records** | `meters_set` struct and C call, command, `setLevels` / `setTelemetry` driven by the spec, `FXLVL` and `FXDYN` posts in step 9, ring accounting, tests (record layout, compared with `FxHost::popLevels` / `popDyn` on a fixed signal; no allocation; ring order with `METERS`) | `live.h`, `live_host.{h,cpp}`, a new test | 6 to 8 |
| **M2 core: master record** | `meters` flag on the mastering `MasterProcessor`, runtime switch, `MASTERM` post, measure the cost | `chain.{h,cpp}`, `master.cpp`, `live_host.cpp` | 4 to 6 |
| **M3 core: chains in / out peaks** | per-track peak in and out in the chains pass, `CHLVL` post, tests (silence, a known tone, a track without a node) | `chost.cpp`, `live_host.cpp` | 5 to 7 |
| **M4 shell** | `live_events.cpp` decode and JSON for the new types, `BridgeTranslator` `meters.want` fields, JNI `meters_set`, `hello` caps, Kotlin unit tests of the translator | `native/android/...` | 4 to 5 |
| **M5 page client** | the "wanted" state and debounced `meters.want`, the five event handlers, `view` accessors for `fxSlotLevel`, `fxDynIn`, chains `api.meters`, `mstMeter`; tests with a fake core | `thunder-core-client.js`, small `index.html` guards (PC5) | 6 to 8 |
| **M6 contract and docs** | CORE-BRIDGE 7.x, N2-PAGE-CLIENT G6 rows, HANDOVER punch-list items | `docs/port/` | 1 to 2 |
| **M7 (optional)** | `TRIMLVL` and the stereo focus-track pair | M1 + M4 + M5 pieces | 3 to 4 |
| **Phase 2 (not first)** | `FXPTEL`; chains `mods`, `shows`, `jacks`, `byp` telemetry in ChainsHost | core and page | 12 to 20 (own design first) |

Order: M1 then M4 then M5 gives the FX bar and dynamics meters end to end (about 16 to 21 h) before M2 and M3 are done; M3 and M2 are independent of each other and can run in parallel. Phase 1 total (M1 to M6): about 26 to 36 builder-hours.

Owner's test list (to add to the punch list when built): on the Fold, with the FX bar open on a track with a compressor, the slot meters move with the sound and the reduction bar hangs while the compressor works; the chains screen shows in / out peaks per track; the master panel's reduction meters move; with METERS OFF while playing nothing is read.
