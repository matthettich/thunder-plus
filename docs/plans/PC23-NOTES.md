# PC23: PC2 (#323) + PC3 (#324) combined

One branch (`pc23/pads-song`, from `pc3/song-transport` + a merge of `pc2/pads`) so the lead reviews the final code once. #323 and #324 are to be closed in favour of this.

## How each conflict was resolved
1. `index.html` `loadClient` / `attach(...)`: PC3's `Object.assign({...PC1 keys}, ncSongHost())` kept; `?v=2` once. (PC2 had not touched this line.)
2. `index.html` CHANGES: one entry for 678 combining both lines (pads, kit, variations, chop data, song, PLAY / STOP, fallback; "nothing changes while the native engine is off"). BUILD 678 and `sw.js` v678 were identical on both sides.
3. `thunder-core-client.js` header comment: merged into one (PC1 + PC2 + PC3). Both sets of functions kept. `hello` has `live: true` (PC2). PC3's optional `NC.resend` hook was removed (PC2 re-sends kit and pads itself on `engine up`, `NC.kit(P.api)`). `start()` already used `NC.kit` / `NC.chopAll` / `NC.whenPads(1500)` through `host.padApi()`, and `can()` reads `NC.padProblem`; `host.padApi()` now returns `ncPadApi()`.
4. `tests/native-client-inert.test.js` guard (b): counts are from the merged `index.html`: `NCON()` 10 (PC1 1 + PC3 3 + PC2 6), `NC()` 13, `tp.nc` 9 occurrences on 7 lines, `ncPadApi` 8 occurrences. Every assertion of both sides kept (PC2's per-function `NCON()` check included).
5. Test fixes needed by the merge (no assertion removed): `tests/native-client.test.js` sandbox gets `setInterval` / `clearInterval` and its host gets the song-side stubs the page always supplies (PC3's client reads `host.songVer` on `engine up`); `tests/native-client-song.test.js` no longer sets or asserts `NC.resend`.

## Lines the lead must read (merged files)
`thunder-core-client.js`
- PC2 pads: constants and state 367-430; `enqueue` / `pump` 433-460; `putCopy` and the `put` send 462-480; budget code (`limitBytes` 427, over-budget checks 482-503, `problem()` ~385, `padProblem` 379); `NC.variation` 547; `NC.pad` 534; `NC.chopAll` 564; `NC.kit` 566; `NC.kitCheck` 575; `NC.newKit` 581; `NC.whenPads` 595; engine-up re-send 406.
- PC3: `can` 74-83; `start` 85-108 (kit / chop / whenPads / verdict / play, lines 91-97); `stop` 109; `fallbackNote` 174; `handBack` 186; `publish` 189; `verdictNow` 211; `watch` (watchdog) 264; events `clock` 285, `engine` 298, `device` 311.
- `hello` with `live: true`: line 53.

`index.html` (every branch)
- PC1: `loadClient` 8766; `padSR` 8909.
- PC2: `setKit` 15258 (`NC().newKit`); `coreKit` 25425; `coreKitCheck` 25432; `coreVar` 25458; `coreBuf` 25462; `ncPadApi` 25465; `setSong` 27220 (`NC()` use).
- PC3: `saveS` 22298; `perfPush` 22635; `resetAudio` 24273; `ncSongHost` block 24955-24990 (`padApi` at 24958); `tStart` 24991; `corePost` 25423; `tStop` 25792; `msMeta` 26048; `bgPlay` 26053; `perfFill` 33004.
- CHANGES entry: search `[678,`; BUILD 27278.
