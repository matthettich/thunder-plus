IN THE MIDDLE OF: Part 1 (find it). Not reproduced yet on either sequencer; next is the stopped / bus edits and a Flex-after-rest fuzz (/tmp/flex-probe.js), then diagnostics lines.

# Solo + effect change, then the mix stays quiet (owner, 19 Nov, build 647, seq=core)

## What the owner's log says
- Context ran the whole time (clock steady, underruns constant at 3, core skips 0). Plays to t=851: average -15.5 dB. Then average -34.9 / -32.1 / -30.9 dB (peak -21.2 / -10.6 / -17.0), back to -14.1 dB from 929.4.
- "Restart audio" (`resetAudio`, index.html:23321) keeps the same AudioContext (ctx time is continuous across 927-930), so the log cannot name it. It rebuilds `rt` (makePlayer, master FX, core via `coreDrop`).
- His song is `sounds/BETA-TESTING-ELECTRO.thunder`. Tracks 0-5 and 8 are sources (Track In) of bus 7 (Chain Out 8, own FX glue / pverb / pspring / flex); track 0, the loudest, has no FX and reaches the master only through the bus. Per-track levels measured here: 0 about -18, bus 7 about -22, others -33 to -40 dB. A mix of about -32 dB with peak -10.6 is what the quiet tracks (9, 10, 11 and the sources' own taps) give without the bus path: so "the bus path (or most FX nodes) is gone" fits better than a uniform gain loss (peak only 9 dB down, average 17 dB).

## Code traced (file:line, build 647)
- `applyMix` 23066: audible() 21573 (solo over mute; bus sources/targets follow a soloed bus/source), gains `setTargetAtTime(gv, now, 0.012)` on g, pan, inp, pre; `fxRest(t, on)` 23065 tells the track's FX node `{rest}` once per change (`b.fx._rest` flag).
- `routeTrack` 23117: new node gets `fxRest(t, audible(t))` 23142 (current state, not pre-solo). Old node gets `null` (dead) when the chain empties. `dry/wet/_fade` re-pointed at the new `mid`.
- ThunderFx `{rest}` 20309: `restAt = now+0.1` or -1; process 20596-20618: while resting it hears zeros, sleeps after 10 s. `set(chain)` never touches `restAt`.
- Core: `core/core-processor.js` mixEv 365-383 holds only TVL/TVA/TPN/mvol/mout (page plugs `cg`, 24425); it does not know mute / solo / audible. The song has no TVL (FLX 123x, PSL 90x only). Nothing re-sends a mix on an FX change.
- PDC `pdcSync` 22537: delays only while playing (AUTO); `n._pv` caches; no way found to leave a track with a huge delay.
- Master FX: `rtFx(NT)` 23880 posts the chain to `rt.mfx`; no rest on the master.
- Survives STOP/PLAY but not restart: effect-node worklet state (restAt, slot.dead, Flex rings, pump wasm state), AudioParam timelines, JS flags (`_rest`, `_pv`, `busKey`, `_tapTo`), `core.wired` / `core.mix`, `fxcPend`.

## Reproduction attempts (headless Chrome, real worklets, the owner's song; `tests/sweep/solo-fx.mjs`)
Solo a track, edit an effect, un-solo; levels and per-track state (fx rest flag, fader gain, delay) printed before / during / after / after STOP+PLAY. All on seq=core unless noted; every one came back to full level, no stuck rest flag, no stuck gain:
- add / remove / retype / bypass / parameter on a SILENCED track (9), solo held 1.5 s and 12 s (rest sleep engaged); on the SOLOED track (0); master bypass / add; bus 7 bypass, add, remove all; solo the bus then edit the bus or a source; same edits with the song STOPPED, PLAY after; seq=page for add-on-silenced.
- Node level (real ThunderFx + pump.wasm, `tests/fx-rest.test.js` helpers): rest 0.3 / 3 / 12 s, then a chain edit (retune, add slot, retype, OFF->ON for pump calm / comp / limit / master / tone / eq linear phase, empty chain), then `{rest:false}`: output identical to a node that was never edited-while-resting; nothing stuck.
Side finding (not the owner's): an effect added with empty params (`{type:'delay', on:true, p:{}}`) stalled the whole AudioContext clock in headless Chrome (NaN into the wasm loop). The UI always goes through `fxSwap` / `freshTfx`, so not reachable that way.

## Verdict
NOT REPRODUCED. Ranked suspects:
1. A bus / source path node (bus 7's chains node `wet` fade, its FX node, or the sources' splitter feed into it) left silent after a rebuild: fits "most of the mix gone, track 0 and bus 7 are most of the level, STOP/PLAY no cure, restart cure". Against: bus edits above all recovered.
2. A Flex / pump slot latched bad (NaN or `slot.dead`: FlexCore.rd / ScrambleCore.rd index pattern, HANDOVER 18 Nov to-do) on the bus or main tracks: the node zeroes non-finite samples, so that track is silent until the node is rebuilt. Fits the cure; no trigger tied to solo found (fuzz of all FLX values after a 12 s rest: see below if finished).
3. A solo still set in S.mix while the graph is rebuilt from it (quiet mix = one quiet track soloed). Against: restart would keep it.

## Diagnostics added so the next log settles it
- Per-track state line on PLAY and 1 s after any solo / mute change: audible, fader gain, fx rest flag, fx node present, chains node present, delay.
- "1 s / 3 s after STOP" lines are cancelled when a new PLAY starts.
