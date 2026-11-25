# PC2 notes: native page client, pads

Branch `pc2/pads` (from `beta` ac4dc49, build 677). Design: `docs/port/N2-PAGE-CLIENT.md` on `native` (sections 0, 2, 4.3 rows 7-10, 4.5, 5, 6, 9), contract `docs/port/CORE-BRIDGE.md` section 7 (G2, G3, G4), shell reference `BridgeTranslator.kt` / `LiveBridge.kt` / `thunder_bridge.js` on `origin/n1/n2-h1c`.

## Done
- `index.html` (branches 7-10): `coreBuf` -> `NC().pad(i, api)`; `coreKit` -> `NC().kit(api)`; `coreKitCheck` -> `NC().kitCheck(api)`; `coreVar` -> `NC().variation(ev, rec)`; `setSong` -> `NC().newSong(api)` (after `S = s`); `setKit` -> `NC().newKit(api)` (after the cache is cleared). Each is `if (NCON()) ...` at the top of the function (coreKit: top of its second line, see 3). New `const ncPadApi` after `coreBuf`. `BUILD` 678, one `CHANGES` line, client `?v=2` (index.html and `sw.js` OPTIONAL), cache `thunderplus-v678`.
- `thunder-core-client.js`: pad queue (one put in flight, FIFO), `put` (copies) -> `buf.use` -> `buf.free`, one `pads.commit` 40 ms after the last, `pad.chop`, variations (`buf.use kind:'var'` with `ii`, `vk`), `kit.load`, budget, removal, `whenPads(ms)`, `padStats()`, `padProblem`, handling of the `pads`, `kit`, `err`, `engine` events.
- `tests/native-client.test.js` (new, 15 tests, fake core) and `tests/native-client-inert.test.js` (guard counts: `NCON()` 1 -> 7, `NC()` 1 -> 7, `ncPadApi` 6, `?v=2`).

## For PC3 (what it must call)
- `NC.kit(api)` then `NC.padsAll(api)` at PLAY (kit.load first, then every pad the core does not hold; `kit()` already calls `padsAll`), `NC.chopAll(api)` after an instrument edit, `await NC.whenPads(1500)` before `play` on the first PLAY, and read `NC.padProblem` in `can()` (a non-empty string is the fallback reason; it is cleared by a new kit, a new song, or the core rebuilt).
- `api` is `ncPadApi()` (the page object); the client keeps the last one, so later calls may pass `undefined`.

## Deviations from the design (each leaves the switch-off path more untouched, or follows the shell as built)
1. **`put` ids are small integers** (a counter), not `'pad:'+i+':'+rev`: the shell reads `id` with `optInt` and `PutStore` keys by Int.
2. **`buf.free {id}` is sent after every `buf.use`**: `PutStore.take` does not remove the buffer, so without it a 256 MB store fills up. Not in the design.
3. **`coreKit` guard is on its second line** (`if (NCON()) return NC().kit(...)` before `corePost({op:'kit'...})`), not the first: PC3's branch 16 edits the `corePost` line directly above, and two changes on adjacent lines conflict in git. Cost: the first line (`coreKitV()` + `JSON.stringify`) still runs once per call while native is on.
4. **The page's `HOST_API` literal in `loadClient` is untouched.** The pad code gets what it needs from `ncPadApi()` passed per call (cache, nslots, kitV, insts, loopSecs, slicePoints, toast), so PC3 can grow the `attach` list without a merge conflict. `ncPadApi` is a `const` arrow (no new window property).
5. **`pad.chop.slices` are seconds into the loop, not frames** (the contract text says frames): `live.h` `pad_hits` takes seconds and `LiveBridge` passes the array straight on. `n` (slice count, 2..64) is added (the shell reads it). Open question for the lead: fix the contract wording or the shell.
6. **Removal is `buf.use {id:-1, pad, kind:'pad', frames:0}`** (design 4.5, `live.h` "frames == 0 removes"). The shell as built (`LiveBridge` BufUse) looks up a put by `id` first and answers an `err` event "no put with id -1": **H1b must handle `frames:0` before the lookup**. Until then a removed pad stays audible in the core (the err is logged, nothing else happens).
7. **`kit.load` is `{kv, rev}`** as in section 2.2 of the design; the contract table says `{kit, rev}`. The shell passes the raw message to the core, whose parser I could not see. It is only sent when the voice list changed (signature), not on every PLAY.
8. **After `kit.load` the shell's `kit` event lists `empty` slots** (not in the design): a pad this client sent before that `kit.load` and that is listed is sent again (entries sent after it are not).
9. **`pads.commit` waits for an idle queue** (never while a put is in flight): the shell's `pads.commit` force-sweeps unfinished puts (`PutStore.sweep(force)`), so committing mid-put would kill it.
10. **Budget:** once over 90 % of `maxPadBytes` (`hello`), or the shell says limit / memory, `padProblem` is set, one toast, and NO further pad or variation is sent until a new kit / song / rebuilt core. `maxPadBytes` 0 (shell gives none) = no client limit. The shell's counter also holds retired buffers; the client counts only what it sent (+ variations), as the design says.
11. **Variations the client keeps a reference to** (to send them again after their pad is replaced, which drops them in the core: `live.h`) are capped at 48 MB (the page's own VAR cap); the page has no per-note events in native mode (the JS core re-asks through `rows`), so this is how they come back.
12. **Chop data** is derived in the client from `S.insts` (`pad`, `slc`, `smode`, `sync`) with the page's `loopSecs` / `slicePoints`: sent after a pad, again after the pad is replaced, on `chopAll`, and `loopSecs: 0` when the instrument stopped using it. If several instruments share a pad, the first with SMODE + slices wins, else the first that slices or syncs.
13. **`newSong` re-sends every pad** (design row 10), which also drops the old song's variations in the core; costs one pass over the pads on each song open. `newKit` removes every pad the core held first.
14. Hello is unchanged (PC1): it does not send `live:true`, and `LiveBridge.hello` only starts the live engine for `live:true`. **PC1/PC3 must add `live:true` to the hello** or a live shell never starts. Not changed here.

## Cited lines re-located (design W: -> beta ac4dc49)
`install` 8922 -> 8930; `coreBuf` 25423 -> 25432; `coreKit` 25386 -> 25392; `coreKitCheck` 25393 -> 25402; `coreVar` 25419 -> 25428; `setSong` 27177 -> 27188; `setKit` 15242 -> 15250; `varWant` 11949-11975 -> about 11968; `loopSecs` / `slicePoints` 24071-24100 -> 24079-24099; `BUILD` 27243.
