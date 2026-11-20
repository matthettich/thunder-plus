**NOW: the C++ and the tests (render_stems, render_stems_bus) are in and pass in Release; next: the full Release ctest, the sanitizer run, mutation checks, then the PR.**

# U34b: stems through the offline render C API

Unit U34b of the native port. Branch `n1/u34b`, cut from `native` at 1c189d9. Builds on U34a (`thunder_render_*`, `docs/port/U34A-NOTES.md`).
Only `native/` and `docs/port/` change; `index.html` is the specification (line numbers below are of this tree's `index.html`).

## Checklist (updated in every commit)

- [x] 1. The JS stem export read, the table below written, the API designed (this file)
- [x] 2. Mixer: a "heard" override (the page's `applyMix(filter)`)
- [x] 3. Renderer: stem mode (no master effects, no master chain, only the stem's tracks heard)
- [x] 4. C API: `opts.stem`, `thunder_render_stem_tracks`, stats fields; header text
- [x] 5. Tests: sum of stems, where it does not add up, silence of a track with no notes, mute / solo, length, bad arguments
- [ ] 6. Golden / parity case (if the tree has render parity tooling that fits): see "Parity" below
- [ ] 7. Release ctest, sanitizer ctest of the render tests, PR

## 1. What the web app does when the owner exports stems

`File ▾ → Render stems (.zip)` is `renderOut(true)` (index.html:24290; menu item :14541). "Render song (WAV)" is the same function with `stems = false`.
The user's help text says it in one line: "Render stems (a zip with the mix and one WAV per track, without mastering)" (:3750 area; also :3141, :3705, :3738).

| Question | What the JS does | Where |
|---|---|---|
| **Which events** | The song is stepped once (`makeSeq(S, 'song', {row:S.renderFrom, once:true})`) into `evs`, stopping at `seq.time >= 1200` s or 100 000 steps; with RENDER FROM > 0 the effect commands before it are chased in first. `notes` = the `note` events whose `kit.voices[e.pad]` exists ("a slot the kit doesn't have is silent"). | 24299-24302 |
| **Which tracks get a stem** | `keepT` = every track `t` (0..NT-1) with `audible(t)` and at least one entry of `notes` on it. A track with notes only on MIDI-only instruments is in `keepT` (it gets a silent stem: the MIDI-only filter is later, at `pl.play`). A track with no notes gets none. | 24304; MIDI-only at 24264 |
| **Mute / solo** | `audible(t)`: solo wins (a soloed track plays even when muted), then mute, then "nobody is soloed"; with a solo somewhere, a bus's sources and a source's bus stay audible. So a muted track, or any track while another is soloed, has NO stem file (and is not in the mix either). Inside a pass the mixer's own mute / solo is replaced by the filter `keep.has(t)` (`applyMix(filter)`: gain 0 for the rest, `pre` 0 too): a stem is heard whatever the track's mute / solo flags say. | audible 20669-20673; applyMix 22154-22165; pass 24255 |
| **Buses** | `keepT` also gets a bus track that has an audible source in `keepT`, even with no notes of its own (stem named "Bus" if it has none). A *pure* source (heard only through its bus) whose bus is in `keepT` gets NO stem of its own: it is inside the bus's stem. A send (heard on its own too) keeps its own stem. A stem pass for bus `b` plays `b` plus its audible sources (`k2`). | 24306-24310, 24348-24349, 24353; pass 24228 |
| **The pass for each stem** | One whole OfflineAudioContext render per stem: `pass(evs, new Set([t]), N, sr, getB, false, false)`: every track still PLAYS (so chokes and mono takeovers match the mix: "every track plays, only `keep` is heard"), only track `t` has gain. | 24351; 24255 comment |
| **What is in a stem** | TRIM -> chains -> the track's own FX (`tfx`) -> VOL (track gain) -> TVL / TVA events -> pan (and TPN events) -> the PDC delay -> MASTER VOL (`S.master` and the MVOL events) -> OUT (`S.mout` and MOUT events). Stereo (the track's pan applies). Track FX are set only for `keep` tracks (the others are silent anyway). | out gain 24233; mo 24235; setFx 24256-24259 |
| **NOT in a stem** | master effects (`S.mfx`: `masterFx` false, no `mNode`), the master mastering chain (`useM` false), the safety limiter / mastering limiter (`lim` false), and the master chain's latency. | 24229, 24243, 24245-24250 (all behind `masterFx` / `lim`); passes 24329 vs 24351 |
| **Sends / returns** | The web app has no send levels: a send is a cable in the chains patch (see `isSendT`, :19925; `busPlan`, :19878). Native: no bus exists until a chains host is wired, so a native render has no sends or returns; see "Dormant" below. | 19878-19905 |
| **Pump** | Track effects of the pump kind (pump EQ on any track) are in `tfxChain` and are part of a stem; `pumpChainLatency` is in `lat`. A song cannot reach a pump slot in a native render (U34a notes), so nothing to do here. The master's pump effects (`pumpChainLatency(S.mfx)`) are mix-only (`masterFx`). | 24230 |
| **Levels** | No per-stem gain of its own. Every stem goes through the same MASTER VOL and OUT as the mix ("stems too, as MASTER is, so they still add up to the mix"). Then ONE export gain `g`, found on the MIX's peak, is applied to the mix and every stem alike: `g = ex.norm && !mastered ? (pk > 0 ? 0.966 / pk : 1) : (pk > 1 ? 0.999 / pk : 1)`, `mastered` = the mastering chain AND its limiter on. That is the host's, not the renderer's. | 24233-24235; 24340-24344; 24346, 24354 |
| **Pan** | The track's pan stays in its stem (stereo files). The master is not summed to mono. | 24270 returns channels 0 and 1 |
| **Latency** | Each pass renders `N + lat` and drops the first `lat`: `lat` = mastering latency + master pump latency + PDC `T` (+ the limiter look-ahead for the mix). A stem's `lat` has only the PDC part, so all files line up after the trim. | 24230-24231, 24248, 24270 |
| **Length** | One length for all files. `end` = the latest note end (pad length / rate, a cut + 0.06 s); `N0 = ceil((end + 0.05) * sr)`; with a tail effect on a kept track (or the master's, or chains) `N = N0 + 8 s`, else `N = N0`. The MIX is rendered first; with a tail its trailing silence (below `max(peak * 1e-4, 1e-6)`) is trimmed: `len = min(N, lastLoudSample + 0.05 s)`. Every stem is cut to the same `len`, with a 20 ms linear fade-out over its last samples when `len != N`. | 24322-24326, 24332-24339, 24352 |
| **Tails of a stem** | A stem keeps its own effect tails (reverb, delay) up to `len`; the length is the mix's, so a stem's tail can be cut by the mix's trim (never longer than the mix). | 24337-24339 |
| **File naming** | The host's (zip, `"<song> mix.wav"`, `"<song> NN name.wav"`: track name, else the pad with most notes, else "Bus"). Not in this unit. | 24286-24289, 24346, 24353-24354 |

## 2. What native does now, and what is missing

- The native renderer (`SongRenderer`) plays every track and mixes with the mixer's `audible_` (mute / solo from the song's `mix`). So the native MIX already equals the JS mix for mute / solo: a muted track has gain 0.
- There is no stem selection at all: the mixer's `audible_` comes only from mute / solo; `setEffects` hooks the master effects and `setMasterChain` the mastering and the limiter.
- No bus exists in the mixer until a chain hook is set on the bus track (`mix/mix.h` BUSES); the render API never sets one. So in a native render every track plays on its own.

## 3. The C API design (the smallest addition)

**Chosen: a stem selector in the options of the existing handle, plus one function that lists the stems.**

```c
/* thunder_render_opts, one field appended (zero = the default = the mix, so an older host is unchanged) */
int32_t stem;   /* 0: the mix (as before). 1 .. 32: the stem of that track, the number the page shows (track 1 = 1). Else E_ARG. */

/* The tracks that get a stem file, in ascending order, as renderOut's keepT / inBus rules decide (mute / solo, tracks with notes, buses).
   Writes up to `cap` track NUMBERS (1..32, the same numbering as opts.stem) into `out` and returns how many stems there are (may exceed cap);
   out == NULL asks only for the count. Any time before the first read; works on any handle (a mix handle or a stem handle). */
int thunder_render_stem_tracks(thunder_render* r, int32_t* out, int cap);

/* thunder_render_stats, appended */
int32_t stem;        /* opts.stem as made (0: the mix) */
uint32_t heard_mask; /* bit t: track t is heard in this render (the mix: audible tracks; a stem: the stem's track and, for a bus, its audible sources) */
```

Everything else is the existing API: create (with `opts.stem = n`) -> `set_pad` ... -> `read` until 0 -> `free`. The host makes one handle for the mix, reads it, works out `len` (tail trim, export gain: the host's, as in JS), then makes one handle per stem from `thunder_render_stem_tracks` with the same options except `stem = n` and `use_frames = 1, frames = len`.

Rules of a stem handle (each is a line of the JS above):
1. The tracks' `audible` is replaced by the heard set `{t}` (+ the audible sources of `t` when `t` is an existing bus): mute / solo flags do not matter inside the pass (24255). Every track still plays (voices, chokes, takeovers, effects run) so the heard track is what the mix has.
2. No master effects, no master chain (mastering + safety limiter), whatever `opts.master_chain` says (the host may pass the same options struct): stems skip them (24229, 24243-24250). `opts.effects` still turns on the TRACK effects.
3. MASTER VOL and OUT stay in (24233-24235).
4. The latency trimmed is the stem's own (PDC only), as `lat` is in the JS (24230-24231).
5. Length: with `use_frames` exactly `frames`. Unknown-length stems end when the song and every voice have ended, like the mix, but they trim only their own latency, so with the master chain on they are longer than the mix by the chain's latency: the host passes `use_frames` (it does in the JS: every file is cut to the mix's `len`).

### Why a selector and not a multi-output read

| | Selector (chosen) | One pass, one read with N output pairs |
|---|---|---|
| Same code path as the mix | yes: the mixer's own routing, only the heard set differs | no: per-track taps after pan, each needing its own MASTER VOL / OUT lane gains, PDC delay and the song's MVOL / MOUT event lanes: a second copy of the mix's tail |
| Matches the JS | exactly (it is the JS: N + 1 passes, same `applyMix(filter)`) | same sound for track-independent effects, but not for anything that couples tracks (buses once wired, a future sidechain) |
| Public API change | one options field, one list function, two stats fields; `read` unchanged | `read`'s signature, buffers per stem, a different length / end model |
| Memory | one handle at a time (53 MB at 48 kHz); the host frees each | one handle, but N output lanes |
| Cost | N + 1 renders (as the page does) | about one render |

The one-pass form is a pure speed-up that can be added later behind the same stem list; it is not needed for parity.

### Buses

`keepT`'s bus rules need only the song's chains patch (`busPlan`: feeds, targets, pure), not the chains engine, so they are applied from the mixer's own `BusPlan` (`Mixer::plan()`), as the page does: a bus track with an audible source in the list is listed even with no notes; a pure source whose bus is in the list has no stem of its own; the stem of a bus plays its audible sources with it (`pass`'s `k2`). ELECTRO's patch has buses: its list is 5 stems, not the 8 tracks with notes.
Until a chains host sets a chain hook on a bus (`Mixer::setChain`), no bus EXISTS in a native render: a pure source plays on its own in the mix, and in its bus's stem (it is heard there, directly), so the stems still add up to the mix. When a host wires the chains, it must set the hook only on HEARD tracks of a stem render (`SongRenderer::heardMask()`), as `pass` calls `routeTrack` for `keep` only (24258): then a pure source asked for alone plays directly, because its bus does not exist in that pass. Test `render_stems_bus` does this with a pass-through hook.

## Choices for the lead

(filled in as built; each is a ruling the lead may change)

1. **`opts.stem` is 1-based (0 = the mix)**, so the zero-filled default stays the mix and the number is the one the page shows. A bad value (negative, > 32) is E_ARG from create.
2. **A stem of a muted / non-listed track is allowed and renders it** (the JS's pass ignores mute / solo inside a pass); only `thunder_render_stem_tracks` applies mute / solo. A host that takes the list gets the JS's files.
3. **`master_chain` and master effects are ignored (not an error) for a stem**, so one options struct serves the mix and all stems.
4. **"A pad is in the kit"** (the JS's `kit.voices[e.pad]`, 24302) is, natively: the pad has a buffer, or a route other than SAMPLER (live / SoundFont), or a prepared SoundFont preset. A pad with no buffer on the sampler route counts as absent. A live or SoundFont pad's track gets a (silent today for live) stem, as in the JS.
5. **The note scan's horizon is the render's own** (`max_frames` + the latency), not the JS's 1200 s / 100 000 steps (24301), so the list matches what the native mix plays. They differ only for a song longer than 1200 s.
6. **A stem is the whole track's sound**, MIDI-only notes included in the list (a silent file), as in the JS (24304 vs 24264).
7. **Export gain, tail trim and the 20 ms fade (24332-24344) stay with the host**: they need the mix's peak and floats. The notes say how (Levels, Length rows).

## The length item (PICKUP, "To add in U34b: a length ESTIMATE ...")

Both halves were built in U34a's follow-up and are merged: `thunder_render_length_estimate` (U34A-NOTES "`thunder_render_length_estimate`") and the out-of-memory abort rule for a build without exceptions (header OUT OF MEMORY; ctest `render_api_noexc`). U34b's part is that they hold for stems: a stem handle's estimate and exact length (tests listed below).

## Not verified / limits

(filled in at the end)
