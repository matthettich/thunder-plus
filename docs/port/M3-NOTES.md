# M3 notes: the chains in / out level record (CHLVL) (METERS-DESIGN 4.1, 5a, owner's choice (a))

Branch `n1/m3-chains-meters`, PR into `native`. The lead reads the audio-side diff on the top model. The shell part is based on M4 (#332, not merged when this started): `origin/n1/m4-shell-meters` (52a3c80) is MERGED INTO this branch; when #332 merges, the merge commit here drops out of the diff.

## Built
- `chost.h/.cpp` (`ChainsHost`): the level tap. `setPeakWatch(mask)`, `popPeaks(t, pk[4])`, `hasNode(t)`; members `pkWatch_`, `pk_[kSongNT][4]` (running peaks {inL, inR, outL, outR}). In `ChainsHost::hook` (the mixer's per-track hook, audio thread): when the track's bit is set, `peakMax()` over the pair BEFORE `nd->process` (in) and AFTER it (out; the hook works in place). Not watched (mask 0, the default and every offline host): one bit test, no sample touched.
- `live.h`: `THUNDER_LIVE_EV_CHLVL` 13 (40 bytes: head, `int32 track`, `uint32 reserved`, `float in[2]`, `float out[2]`); `thunder_live_meters_spec` gains `uint64 chains_tracks` LAST (size 32 -> 40; the ABI size rule: an older caller passes size 32 and watches no chains track); `THUNDER_LIVE_MK_CHLVL` (4) now works; `chains_hz` works (0 = 16 a second, at most 30).
- `live_host.h/.cpp`: `LiveCmd::MetersSet` carries `chains_tracks` in `i1` (unused before; i0 kinds, i64a fx_tracks, x rate, y chains_hz); `metersSet` (E_ARG for chains_tracks above bit 31); `applyMetersSet` flips `ChainsHost::setPeakWatch` on an EDGE of the watched set only; `postChainsMeters` (step 9, after `postFxMeters`).
- Shell: `live_events.cpp` CHLVL -> `{t:"ch.lvl", seq, f, tr, in:[l,r], out:[l,r]}`; `BridgeTranslator` `meters.want` `chains:[tracks]` -> kind CHLVL (4) + its own mask; `LiveNative.nativeMetersSet(hz, kinds, fxTracks, chainsTracks)`, JNI sets `chains_tracks`.
- Tests: `live_chlvl` (layout, spec rules, known signal, unwatched / no node, silence, bit-equal audio, rates, replacement, identical re-send, ring order with METERS + FXLVL), `live_chlvl_alloc` (0 allocator calls over 400 quanta and over the watch edges), `android_live_events` (ch.lvl JSON), Kotlin `BridgeTranslatorTest.metersWantEffectLists` (chains lines). `live_meters2` size assert 32 -> 40.

## Lines the lead must read (audio side)
- `chost.cpp`: `peakMax` (new static), `ChainsHost::hook` (two added lines: `const bool tap = ...; if (tap) peakMax(in...)` before `chains::NodeIn in`, and `if (tap) peakMax(out...)` after the failedBlocks line), `setPeakWatch`, `popPeaks`.
- `live_host.cpp`: `applyMetersSet` (the new `if (chains_) {...}` block and `chHz_ = ...` at its end), `postChainsMeters` (new), `afterBlock` (one added call after `postFxMeters`). `metersSet` (control) gained two lines.
- Nothing existing was moved; no existing record or ABI field changed (the spec struct grew at its end).

## Behaviour
- Watched = kinds has CHLVL AND the track's bit in chains_tracks AND the host has F_CHAINS. Posts only for a watched track that HAS a node now (`popPeaks` false otherwise); a watched track with no node, an unwatched one, or nothing watched: nothing posted, nothing computed (the hook's scan is gated per track).
- Pacing: `nextChAt_` (engine frame), whole quanta, `round(sr / chains_hz)`; the running peak is the max since the previous record of that track (reset by `popPeaks`), so no sample's peak is lost between posts. A changed watched set restarts the pacing (first record the next quantum, covering one quantum) and zeroes the peaks of the tracks whose bit changed; an identical re-send does neither (test: same records as an unbroken run).
- A track whose hook the mixer skips (not called) keeps its peaks at 0: records still post zeros while the node exists.
- Order inside a quantum: METERS, FXLVL / FXDYN, CHLVL. `f` = frame at the end of the quantum.

## Decisions for the lead
1. **Per-track record, not the design's 280-byte all-tracks record.** The task's JSON (`tr`, `in:[l,r]`, `out:[l,r]`) is per track with L and R, and the page watches ONE track; a 280-byte record of 31 zero entries at 16 a second would be waste. The design's `valid` mask / mono `in[32]` / `out[32]` are NOT built. The page's chains screen draws one mono peak per track: it takes `max(l, r)` (page side, M5).
2. **Its own track mask (`chains_tracks`) instead of reusing `fx_tracks`.** M4's translator already folds `fx` and `dyn` into ONE mask (so `fx:[3], dyn:[5]` watches both on both; harmless for the page's one-track rule, but it would couple the chains screen to the FX bar's track). The spec struct grew at its end (ABI size rule), the command uses the free `i1`.
3. **No new cap bit.** CHLVL is under M1's `CAP_METERS2` (0x400): the page already gates on `hello.caps.meters2`; M6 documents CHLVL under 7.2. (0x800 is PP's.)
4. **Event name `ch.lvl`** (the task's): the page's `thunder-core-client.js` on beta has no chains handler yet (only `fx.lvl` / `fx.dyn` under `meters2`), and its `meters.want` still sends `chains` as a BOOLEAN. The translator accepts an array of tracks (CHLVL) and ignores a boolean (no track named); M5's second half must send `chains:[track]` and handle `ch.lvl`. The design's sketch named it `chains.lvl` with `i` / `o` arrays; M5 must match the shell, not the sketch.
5. `chains_hz` is in the core API but the shell does not pass it (0 = 16 a second, the design's default). Add a `chainsHz` field to `meters.want` if the page wants another rate.
6. Chains tracks are 0..31 (no master); `chains:[32]` or `["m"]` refuses the message in the translator, `chains_tracks` above bit 31 is E_ARG in the core.
7. Peaks are taken at the chains hook: in = after the mixer's TRIM, before the node; out = after the node, before the track's effects / fader / pan (the page's analysers sit on the chains node's own in / out the same way).
8. Not run: the Kotlin unit tests (no Android SDK on this machine; CI runs them) and a TSan build (single-threaded tests).

## Tests run
See the PR description / final report.
