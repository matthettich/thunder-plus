# PC4: live edits (build 679, branch `pc4/live-edits`, draft PR into `beta`)

Design: `docs/port/N2-PAGE-CLIENT.md` 4.4 step 1 and 4.3 branch 13; contract `docs/port/CORE-BRIDGE.md` section 7; the shell's parsing is `BridgeTranslator.kt` (`origin/n1/n2-anr`).
Builds on PC1..PC3 (merged, build 678). Test counts: before 1944 tests / 1887 pass / 0 fail / 57 skipped (`npm test` on `beta` 088a4f4); after: see the PR.

## What it does
While a native PLAY runs, `saveS()` -> `NC.edit()` starts ONE diff task per 16 ms. It compares the live state with `L.sent` (the signature of what the core holds) and sends one small message per changed thing:

| Page state | Message | Notes |
|---|---|---|
| `S.mix[t].vol / trim / pan` | `mix.set {tr, k, v, ramp:-1}` | values as stored (0..255) |
| `S.mix[t].mute / solo` | `mix.set {tr, k, v:0/1, ramp: 12 ms in frames}` | |
| `S.master`, `S.mout` | `mix.set {tr:32, k:'vol' / 'out'}` | `send` is never sent (folded into the song) |
| `S.tfx[t]` / `S.mfx`, same slots, an ordinary effect's mapped values moved | `fx.set {tr, slot, p:[12]}` (tr 32 = master) | `p` from the page's own `TFX[type].map(f, sr, bpm)`; a non-finite value is left to the song |
| a slot added / removed / retyped / on-off | `fx.plan {tr, slots: S.tfx[t], bpm}` | the shell plans and commits |
| a pump or modular slot's parameters | nothing small (no `fx.set` exists for them: live.h); the song carries them | its on / off / type is an `fx.plan` |
| `S.mst`, `S.limiter` | `master.config {mst, safety}` | |
| PDC mode (`aset`, not in the song) | `pdc.set {mode}`; also in `play {pdc}` | also after an `fx.plan` that touches a pump slot and after an accepted song with chains (live.h `pdc_set`: "the page calls it after ...") |
| a …chains slider | `chain.edit {ops:[{op:'param',id,name,value}]}` | one message per 16 ms with the last value per module + name |

The whole song still goes 250 ms after the last edit (the song text must carry every change). The small messages go first (same port, ordered).

## The page side (`index.html`, 4 one-line branches and 3 host functions)
- `mfSet` (the track fader drag) and the master OUT bar's pointer-move handler (32022, next to its `moApply()` call; `moApply` itself is untouched, `tests/gain-staging.test.js` pins its text) call `NC().edit()`: **both change `S` without `saveS()` until the pointer is let go**, so without these two lines a drag is silent in native mode until release. (This is the answer to "list the applyMix sites": see below.)
- `pdcSet` calls `NC().edit()` (an `aset` change: no `saveS`).
- `thunderChains.param` (branch 13): `if (NCON()) return NC().chainParam(id, name, value);` after the patch update and before the `rt` guard. `param()` does **not** `saveS` (the editor's autosave follows about a second later), so the client makes the song stale itself.
- `ncSongHost`: `fxSlot(f, sr, bpm)` (tfxChain's mapping for one slot without `pumpSync`'s side effect), `chainMods()` (module id -> type), `pdc()` (0 off, 1 auto, 2 on).
- `?v=2` -> `3` (index.html, sw.js, the inert test); BUILD 679; `sw.js` cache v679; one CHANGES line.

### The `applyMix` call sites (N2-PAGE-CLIENT 9: the lead asked for the list)
`index.html` at this build (`rt.pl.applyMix` / `applyMix`), each with what follows:
1. 22243 `undoStep`: `saveS()` runs just before it. OK.
2. 24413 `rtInit` (first audio start): not an edit. n/a.
3. 24943 `mixChanged`: `saveS()` in the same function. OK (28 callers: keys, wheel, pointer-up).
4. 27685 `setTracks`: `saveS()` at the end of the same function (line 27702). OK.
5. 31945 `mfSet` (the track fader's pointer-move and wheel): **no `saveS`**. The wheel calls `mixChanged()` right after; the drag calls it on pointer-up (`mfEnd`). During the drag nothing told the client: fixed here with the `NC().edit()` in `mfSet`.
6. 33015 `perfReload`-style song reload (`Object.assign(S, n)`): `saveS()` just before. OK.
7. 33031 part load (PRT): `saveS()` just before. OK.
8. 26272 offline render's own `pl.applyMix(keep)`: not the live mixer. n/a.
(Inside the player, 23298 `planChanged` and 23850 TVL / TPN reset call it on the page's own graph; no `rt` in native mode.)
Also without `saveS` during the gesture: the master OUT bar (pointer-move; `saveS` on pointer-up, the pointer-up handler `moDragEnd`) — fixed the same way — and `thunderChains.param` (above). The other `rtRoute()` / `moApply()` callers (tracker keys, MASTER rows) end in `saveS()`.

## The signature (`L.sent`): why it is never stale
`L.sent` is a snapshot object `{ref: S, mix[32][5], master, mout, fx[33], mst, safety, mods}`.
- **Taken** in `publish()` at the moment the song text is made (`mine.snap`), so it is exactly the text. It becomes `L.sent` only when the shell **accepts** that song (`settleV` ok -> `liveAccepted`; once per song).
- **While that song is out and unanswered**, every small message the flush sends is also written into the pending snapshot (`n` in `liveFlush`), so a value changed back to the old one in that gap is still sent (tested).
- **Dropped** (null = no small message until the next accepted song): `engine {up:false}`, `engine {up:true}`, `NC.newSong` (setSong), `detach()`, and a different `S` object at diff time. Nothing else writes it.
- A song refused while playing keeps the old baseline (the core still holds the old song plus our small messages).
- Small messages are only sent while `playSent`; edits while stopped go with the whole song, and the accepted song is the baseline at PLAY (no `mix.set` for them; tested).

## Open questions for the lead
1. Does the core re-read `S.mix` into its Mixer when a song is adopted / PLAY starts? The design assumes yes (the baseline after an accepted song = the song text). If the Mixer keeps its own values from earlier `mix.set` calls, a PLAY after a stop would start with stale fader values. Not checked in C++ here.
2. `pdc.set` after an accepted song with chains (live.h says the page calls it); implemented as "every accepted publish mid-play when the song has chain modules". Cut it if the shell recomputes by itself.
3. Pump / modular slot knobs only reach the sound with the 250 ms whole-song publish (no small message exists). Fine for now?
4. A new `fx.plan` does not carry the effect's old state across a type change (the shell's rule); nothing to do on the page.

## Lines to review (the diff-signature code), `thunder-core-client.js` at this commit
- PC4 block header + state: 613-626 (`L`, `liveReset`, `liveSoon`).
- `fxEntry` (one effect list's raw / structure / mapped values): 629-646; `liveSnap`: 647-657.
- `liveAccepted` (baseline replaced; once per song) 658-664; `hasMods` 665.
- `liveFlush` (the diff and every message): 667-708.
- `NC.chainParam`: 710-720.
- Hooks outside the block: `edit` 70-75; `publish` snapshot 203; `settleV` 244; `sendPlay` pdc 258-259; `engine` handlers `liveReset` 316 and 320; `NC.newSong` 601; `detach` 66; `schedPublish` 153.
- `index.html`: `fxSlot` / `chainMods` / `pdc` 24979-24982; `mfSet` 31950; the OUT bar drag (`S.mout = v; moApply(); ...`) 32022; `pdcSet` 15940; `thunderChains.param` 19019.
