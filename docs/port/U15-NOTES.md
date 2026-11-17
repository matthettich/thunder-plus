# D4: U15 whole-song test (Flex / Step gate / Retrigger on ELECTRO): NOTES (branch `n1/u15-song`, PR #190)

## Result: PASSES (fix applied with the lead's approval, 16 Nov evening)
`native/tests/test_u15song.cpp` renders `sounds/ELECTRO.thunder` through `SongRenderer` (sequencer -> voices -> `Mixer` -> `FxBank` -> `FxHost` -> clkcores)
with Flex, Step gate and Retrigger on tracks 0, 1, 2 (all three in series), 4, 8 and, from the song itself, on the master, plus FLX / GAT / RTG / FXO tracker commands
(`native/tests/golden/u15song-patch.json`), 384000 frames (8 s). Reference: the real ThunderFx worklet and the page-strip model of test_mix run in Node
(`native/tools/u15-song-dump.mjs`). Gate: bit-exact. With the fix below all 768000 samples equal Node's and the committed golden (hash `0x75c0cc2d28be319b`); no golden changed.

The test first failed (before the fix): with FLX / RTG commands on the master column 171180 of 768000 samples differed (first at sample 5498, got -0.182175323, want -0.184038609; largest difference 0.839).
Track effects were bit-exact.

## Cause
`SongRenderer::masterHook` (`native/core/src/render/render.cpp`) calls `fx_->host(kSongNT).process(L, R, frames, t0)` with **no beat clock** (`bp` defaults to none), while
`FxBank::hook` gives every track node `Mixer::beat(track)`. In the page the master effects node hears the clock exactly like a track node: `index.html` `setMfx` calls
`clockTo(mfx)` (clkBus -> `_cd` -> the node's `bp` param). So in the renderer a **master Flex / Step gate / Retrigger never sees the song position** (it behaves as a stopped
clock) and FLX/GAT/RTG on the master column act differently from the page. (Retrigger in mode 0 and Flex with a pattern are tempo-locked: both need `bp`.)

Proof, both measured here:
1. Node's master node fed `bp` as "param at its default" (the `none` mode) gives output hash `0xac7b917eb835a2f7` = the C++ render's hash (bit-exact, 0 of 768000 differ).
2. With the one-line change `process(L, R, frames, t0, r->mix_.beat(kSongNT), frames)` in `masterHook` (tried locally, reverted, NOT in this PR) the C++ render equals Node's with
   the clock: 0 of 768000 samples differ, hash `0x75c0cc2d28be319b` = the committed golden.
Before commands were put on the master column the song's own master Flex (FLX 00 at the start) and Retrigger (no RTG) were pass-through, which is why the track-only run hid it.

## Fix (applied)
`SongRenderer::masterHook` now calls `fx_->host(kSongNT).process(L, R, frames, t0, r->mix_.beat(kSongNT), frames)`: the master node hears the clock like a track node.
**The test's tap needed the clock too:** `tapHook` in `test_u15song.cpp` copied the old call for the master (no clock), so the check "the taps do not change the render" failed once the product
changed; its master line now passes `C.mix->beat(NT), N` (the tap mirrors the product hook). No golden, no gate changed.

## Master clock delay under a PDC plan (from reading, not tested)
The page has one: `pdcSync` does `set(mfx._cd, pl.c.get(MT))`, and the FXC lane time adds `n._cd._pv` for any node. Native matches: `pdc.cpp` sets `out.c[NT]`, `pdc_apply` ->
`Mixer::setClockDelays` covers all `kMixTracks` including the master, `Mixer::clockBlock` delays the master's clock when the master hook is set so `beat(kSongNT)` is the delayed copy,
and `FxBank::event` adds `clockDelaySec(kSongNT)` to master FXC times. This test uses no PDC plan, so it is not exercised.

## What the test is, and what it relies on
- Node cannot make the voices and the sequencer: they come from the C++ (`test_u15song --dump <dir>`: what each effects node is given after TRIM, the beat clock it hears, the
  sequencer's FXC / TVL / TVA / TPN / MVOL / MOUT events by quantum, from a second `Seq` run in step with the renderer). The golden also holds an input hash of those, so a
  platform whose voices differ fails on that line first. The event times Node uses are the page's rule `max(t, quantum start)` (no latency plan: clock delay 0); FXV events are
  not used (the tool aborts on a kind it does not apply; none occur).
- Taps: a pass-through hook on every track (`setFx`) and on the master; a check proves the tapped render equals the plain `setEffects(true)` render.
- Pads are synthetic and libm-free (the build has `-ffp-contract=off`), the real ELECTRO patterns / instruments are used. Tracks 3, 5, 6, 7 of ELECTRO make no sound in the first 8 s
  (muted by `smute` or empty phrases), so the effects are on 0, 1, 2, 4, 8 and the master.
- ctest on Linux x86-64 with Node: `u15song` (golden, every platform), `u15song_dump`, `u15song_ref`, `u15song_node` (bit-exact vs the local Node), `u15song_golden_current`.
