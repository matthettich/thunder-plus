# M5 (second half): chains in / out level meters from the native engine (build 682, branch `m5b/chains-meters`)

Shell side: PR #334 (`n1/m3-chains-meters`): `meters.want {chains:[track]}` (array, 0..31; a boolean is ignored) and `{t:'ch.lvl', seq, f, tr, in:[l,r], out:[l,r]}` (linear peaks), under `caps.meters2`.

## What it does
- `thunder-core-client.js` (`?v=6`): `meters.want.chains` is now an ARRAY (never a boolean). It comes from `host.chWant()`, asked at every look (the 100 ms timer and the existing 500 ms tick while playing), and goes only while a native PLAY runs, `meterHold()` is false and `caps.meters2` is set; otherwise `[]`. The list is cleaned: integers 0..31, each once, ascending. The `wanted.chains` boolean is gone.
- `ch.lvl` (listener only with `meters2`, only while a native PLAY runs): `host.chLvlIn(tr, max(inL, inR), max(outL, outR))`.
- `index.html`: `ncSongHost.chWant` / `chLvlIn`; `chNativeWant` (tracks of the patch's Chain Out and Track In modules, only with NCON() and tp.nc and the chains screen visible), `chNativeIn` (stores value + time), `chNativeFeed` (the existing 60 ms chains-screen timer, native branch: `api.meters({peaks, peaksIn})`, a reading older than 400 ms is 0, the same limit as the FX bar's; the chains frame keeps its own decay and hold). The web-engine path of that timer is unchanged behind the new two lines.

## Decisions
1. The page's display is per MODULE (each Track In / Chain Out shows the track it is set to, plus the bus strip), so the client sends every track those modules name (usually 1 to 4), not one track. The shell takes a list (a 32-bit mask; no cap).
2. The chains screen has no "selected track": `chWant` is the patch's module tracks. Tracks with no Chain Out in the patch have no node, so the shell posts nothing for them.
3. Visibility is polled (500 ms tick + 100 ms debounce), not pushed: leaving the screen turns the watch off within about 0.6 s; the page ages the display to silent within 400 ms either way. No setView hook added (fewer page lines).
4. Native feed sends only `peaks` / `peaksIn` (no `mods`, `shows`, `jacks`, `byp`, `load`): the shell does not compute them (METERS-DESIGN 3), and omitted keys leave the frame's own state alone.
5. STOP: `playSent` false sends `chains:[]` and `ch.lvl` is ignored; the page's data ages out, the bars go silent.
6. Page inert test counts: NCON() 15 to 17, tp.nc 10 to 12 (lines 8 to 10). The M5 first-half tests now expect `chains: []` instead of `chains:false`.
7. Not changed: `chainsHz` (shell default 16 a second matches the page's 60 ms timer).

## Tests
`tests/native-client-m5b.test.js` (5 tests): the send rule (array, sorted, cleaned, off on hold / stop / screen left), never a boolean, no meters2, `ch.lvl` max(l, r) and guards, the page side (run the real `chNativeWant` / `chNativeFeed`: age limit, guards).
