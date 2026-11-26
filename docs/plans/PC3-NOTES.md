# PC3 notes: native page client, song, transport, fallback

Branch `pc3/song-transport` (from `beta` ac4dc49, build 677 -> 678). Design: `docs/port/N2-PAGE-CLIENT.md` (lead's checkout) 0, 2, 4, 5, 6 (PC3), 8, 9; contract `docs/port/CORE-BRIDGE.md` section 7 (frozen; G14 = no verdict in 5 s is NOT ok, `no-verdict`).

## Done
- `thunder-core-client.js` (`?v=2`): `edit()` (whole song, 250 ms, never `song.patch`), `song.load {t, title, S, vars, rev}` as an `ask` (key order matters: the shell reads `t` / `title` from the head and `rev` / `id` from the tail), verdict per rev (`reply`, `pending` + the `fit` event, 5 s `no-verdict`), `can()`, `start()` (kit hook, pads hook, verdict, then `play`), `stop`, `hold`, `bpm`, `loop`, `perf`, `lead`, `lxp`, `legacy()` (the JS core's ops), `reset()`, events `clock`, `fit`, `song`, `err`, `engine`, `device`, `state`.
- `index.html`: branches 4 (tStart), 5 (tStop), 6 (saveS), 15 (perfPush, perfFill), 16 (corePost), 17 (resetAudio); `ncSongHost()` (the page side handed to `attach`); media-session guards in `msMeta` and `bgPlay`; `tp.nc`.
- Tests: `tests/native-client-song.test.js` (fake core, fake clock, 29 tests), inert guard counts updated.

## Cited lines re-located (design W: -> beta ac4dc49)
tStart 24946 -> 24982; tStop 25725 -> ~25780; saveS 22290 -> 22298; resetAudio 24263 -> 24271; corePost 25382 -> 25422; perfPush 22627 -> 22635; perfFill 32961 -> 33000; msMeta / bgPlay 26013 / 26018.

## Deviations (each leaves the switch-off path more untouched, or fixes a design gap)
1. **corePost branch tests `tp.nc`, not `NCON()`.** With `NCON()` a song that falls back to the old engine would have its JS-core ops (`play`, `load`, ...) swallowed by `legacy()`, so the fallback would be silent. `tp.nc` is true only between a native PLAY and its STOP. Same for perfPush / perfFill (`tp.core || tp.nc`).
2. **tStart's branch is `if (NCON() && NC().can(mode, o)){ NC().start(mode, o); return; }`** (design: `return NC().start`; start is asynchronous, it cannot return "fall through"). It sits before the first-PLAY warm-up, so `rtInit()` / `audio()` never run for a native PLAY. A not-ok verdict arriving later calls `host.fallback`, which stops the stand-in and calls `tStart(mode, {...o, _nf:true})`.
3. `tp.on` is true at once (before the verdict) so STOP works while the verdict is out; STOP cancels the pending PLAY.
4. The client does not check `maxSongBytes` before sending (that needs a second stringify); the shell's refusal reply is a not-ok verdict. If the hand-over of a song takes 4 ms or more the next publish waits 1 s (design 4.4's fallback).
5. Heartbeat while playing: 2.5 s, not 1 s, and not counted while the page is hidden (+3 s after it is shown): a screen-off page's timers run late; a false "engine stopped" would stop the owner's music.
6. Fatal err toast: "The native engine stopped: <where>. Press PLAY: the old engine plays this song." (the design's text said "to try again" but also that the next PLAY uses the old engine). The old engine is used for that song (the `S` object) until the engine is rebuilt (`engine up`).
7. `native` is wanted when `aset.core === 'native'`, or on host `android` unless `aset.core === 'page'`. Other hosts only on the owner's pick.
8. `song` identity for "first PLAY of each song" = the `S` object (`setSong` replaces it). Undo may replace it too: then the message can show again.
9. RESUME restarts the row (owner answer 3); the old engine's RESUME point from a fallen-back PLAY is consumed by `begin` and not restored (rare).
10. `hold` / `stop at t` sites at index.html ~16813 / 16832 (`if (tp.core)`) are not branched: a pattern-sync recording take is a fallback condition, so they cannot run in a native PLAY. `NC.hold()` exists for `legacy({op:'hold'})`.

## For the lead (merge with PC2)
- `loadClient`'s `attach(...)` line is the one line both PRs touch: PC3 wraps it as `attach(Object.assign({...PC1 keys}, ncSongHost()))`. The inert test's counts (NCON() 4, NC() 7, tp.nc 9) will need PC2's numbers added.
- PC2's hooks (from the lead's note), called at PLAY if present: `NC.kit(api)`, `NC.chopAll(api)`, then `await NC.whenPads(1500)`; `NC.padProblem` is read in `can()`. `api` comes from `host.padApi()` (-> `ncPadApi()` once PC2 is merged). PC2 re-sends kit and pads itself on `engine up`; PC3's own `NC.resend` hook is optional and can be dropped. PLAY order in the test: kit, chop, pads, song.load verdict, play. `hello {live:true}` is PC2's.
- `draw()`'s playhead block is guarded by `ctx`, so with `ctx === null` a native PLAY shows no playhead until PC5; PC5's `ctx === null` audit still applies (`tp.on` is true now).
- Metronome, count-in (fallback), `moStart` (the shell's MIDI scheduler plays MIDI), pad flashes: not in PC3.

## Stringify budget (design 4.4 gate, node on the server, not the Fold)
`JSON.stringify` of the parsed song: ELECTRO.thunder (837 KB) median 3.1 ms, p95 3.9, max 8.4; BETA-TESTING-ELECTRO (1.5 MB) median 9.5, p95 13.5. The design budget (2 ms median / 8 ms worst on the Fold) is NOT met on ELECTRO, and the bridge stringifies again inside `ask`. Mitigation built in: debounce 1 s after a slow hand-over. Not measured on the Fold or in headless Chrome.
