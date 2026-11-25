# M5 (first half): the page client's effect meters (build 680, branch `m5/fx-meters`, draft PR into `beta`)

Design: `docs/port/METERS-DESIGN.md` 2, 4.4, 4.5 and the lead's rulings in 5a (ruling 3: one track at a time; ruling 4: `fx.dyn` carries the type, the page drops a mismatch); contract `docs/port/CORE-BRIDGE.md` 7 / 7.1.
Only the EFFECT meters (`fx.lvl`, `fx.dyn`). Chains and master meters wait for their core units. Test counts: before 1972 tests / 1915 pass / 0 fail / 57 skipped (`beta` bed4f97); after 1982 / 1925 / 0 / 57 (+10 in `tests/native-client-meters.test.js`; the inert test's guard counts and `?v=4` updated).

## What it does
- Client (`thunder-core-client.js`, the "M5" block at the end; `?v=4`): a `wanted` object `{rate, fx:[tr], dyn:[tr], chains:false, master:false}` and one debounced `meters.want`.
- Only when `hello`'s reply has `caps.meters2`. Without it: no view is made (`NC.fxNode` returns null), no listener is added, nothing is sent.
- What goes out is the wish only while a native PLAY runs (`playSent`) and the page's `meterHold()` is false; otherwise everything off. The timer is 100 ms after the last change, whole state, and a state equal to the last one sent sends nothing.
- It is looked at again at: a wish change, every `watch()` call (PLAY start / STOP / fall-back / engine change / device stop all pass there), a 500 ms tick while playing (the owner can switch METERS under a running PLAY), and `engine {up:true}` (forced; `sent` is forgotten, so it goes even if it equals the last one). While the engine is down nothing is sent.
- One track at a time: the page's `{lvl:true}` / `{tel:true}` on a track REPLACES the list (`[tr]`); `{lvl:false}` removes only that track.

## The exact message (as sent)
`{t:'meters.want', rate:10, fx:[3], dyn:[3], chains:false, master:false}`; all off: `{t:'meters.want', rate:10, fx:[], dyn:[], chains:false, master:false}`. Track 32 = the master's effects.
Nothing is sent when nothing was ever asked and nothing is wanted (the shell's own 10 Hz METERS from `hello` stands).

## The page side (`index.html`): ONE guarded line plus a host hook line
1. `fxDynNodeOf = t => NCON() && tp.nc ? NC().fxNode(t < 0 ? 32 : t) : !rt ? null : ...` (the old expression unchanged behind it).
2. `ncSongHost`: `meterHold:() => meterHold(), fxDynIn:(n, list) => fxDynIn(n, list)`.
`NC.fxNode(tr)` returns a node-like view `{tr, lvl, lvlT, dyn, port:{postMessage}}` (cached per track). Its port takes the page's OWN messages `{lvl:bool}` / `{tel:bool}`, so `fxLvlSet` / `fxTelSet` / `fxSlotLevel` / `fxDynIn` / `dkDynMeters` / the stage bars run unchanged on it.

## Deviations from the brief (all choose the smaller change on the page)
- **`fxLvlSet` / `fxTelSet` / `fxSlotLevel` have no branch of their own.** In a native PLAY `rt` is null, so `fxDynNodeOf` gave null and the three functions never had a node. Giving them the view through `fxDynNodeOf` means the page's existing code tells the client (via the port) and reads the view (`fxLvlNode.lvl`, age rule, -54..0 dB map and the readers' `node.dyn.get(k)` all untouched). Net: 1 `NCON()`, 1 `NC()`, 1 `tp.nc` added (inert test counts 14 / 17 / 9 -> 15 / 18 / 10, lines with `tp.nc` 7 -> 8).
- `rate` is always 10 (the rate the shell asks at hello). Switching the 70-float METERS rate belongs to the first half of PC5, which is not built; this unit does not touch it, also when "everything off".
- `fx.lvl` is kept only for a track that has a view (the page asked for it at some time); an event for a track never asked is ignored.
- `fx.dyn`'s `type` string is compared with `S.tfx[tr][slot].type` / `S.mfx[slot].type` at arrival; a reported slot whose type differs, or that is empty, is dropped.

## Open questions for the lead
1. `rate` as above: ok to leave at 10 always, or should "everything off" send `rate:0`? (Then PC5's first half must send it back on.)
2. In the shell, does `meters.want` with `fx` and `dyn` as two lists map to the one `fx_tracks` bitmask + `kinds` of `meters_set` (METERS-DESIGN 4.2)? The page may want FX-bar track and stage track to be different tracks; the client sends them as two lists of at most one each.

## Lines to review (`thunder-core-client.js`, the M5 block, search "M5")
`MTR`, `mtrState`, `mtrNote`, `mtrFlush` (the whole send rule); `NC.fxNode` (the view and its port); `onFxLvl`, `onFxDyn`; hooks: `watch()` (first lines and the tick), `sendPlay`, the `engine` `up:true` handler, `subscribe()` (the two `on(...)` only with `meters2`), `detach`.
`index.html`: `fxDynNodeOf`; `ncSongHost` (`meterHold`, `fxDynIn`).
