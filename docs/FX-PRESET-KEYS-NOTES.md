# FX preset keys: notes (19 Nov, build 653)

Owner, 19 Nov: "Can we make it so pressing Cmd plus left and right on FX presets scrolls through them?" Line numbers are
`index.html` at build 647 (before this change), unless a file is named.

## 1. Every "preset" picker the app has, and how each is changed today

| Where | Picker | Today |
|---|---|---|
| FX screen, a track's or the master's slot, built-in effect | the `› PRESET  NAME` action row, `fxRows` 27029 | EDIT / Enter / click runs `fxPresetOpen` (26954): the list overlay `#fxPick` (`fxp`, `fxpFill` 27593) with INIT, FACTORY, MY PRESETS, THIS SONG; ▲▼ Enter, Esc, tap twice |
| FX screen, a pump effect | the same row, `fxRows` 27033 | `pumpPresetOpen` (27625): the same overlay, sections FACTORY / SOURCES / CHAINS |
| Deck "FX" non-bar view, pump slot only | `PRESET ▾` button, 32032 | `pumpPresetOpen`; built-in effects have no preset control there |
| Desktop FX bar (`dkFxBar`, `deckFxBar` 32070) | none | the cards show parameter rows only; no preset row, no button |
| Pump pop-up windows (`PumpFX.open`) | their own PRESET menu (separate window code) | not this picker |
| Synth page: pad presets `#vPre` 3323, layer presets `#lPre` 3417, engine presets (`ENGINE_PRESET_SHEET` 12338), the older `<select>`s `bassPre`, `blipPre`, `xPre`, `voxPre` 3339-3385, tuning preset 14237 | a sheet (or a `<select>`) | tap a row; apply is `applyVoicePreset` 12249 (a whole voice), a different code path |
| `chains/index.html`, `waves/index.html` modules | a preset button per module, `PRESETS` 3321, `applyPreset(n, name, vals)` 3426 | click, then a small menu; lives in a frame with its own key handler |

None of them has `-- - + ++`: that row set (the `b:-16 / -1 / 1 / 16` keypad buttons, `kpButtons` 35590ff) acts on `r.get` / `r.set` rows,
and the PRESET row is an action row with neither.

## 2. How picking applies a preset

- Built-in: `fxpChoose` (27638) takes `fxp.tpre` and calls `fxPreApply(t, slot, p.vals, p.name)` (26949): every parameter of the
  effect goes to the preset's value, else to its default (INIT is `{}`: all defaults); the sidechain source `sck` stays unless named;
  then `f.pname = name; fxChanged(t); saveS(); draw(); toast('Slot N: NAME')`.
- Pump: `PumpFX.applyPreset(t, k, p.index)` (20076; sets the values, `f.pname`, `apply()` which does `saveS()` + `rtFx(t)`), then
  the toast `Preset: NAME`, `draw()`.
- Live audio: `fxChanged` (23890) calls `rtFx(t)`; the audio graph is rebuilt for that track's effect in the running transport.
- Undo: `saveS()` (21533) calls `snap()` (21400): each changed state is one step ("settle at the end of the event"). Nothing groups
  steps over time except a held pointer (`undoHeld`).
- Edited parameters: a parameter row's `set` deletes `f.pname` (27051), so a changed slot has no preset name: "custom". The row then
  reads `› PRESET` with no name. There is no explicit "custom" list entry; the list opens on INIT (`Math.max(0, cur)`).
- User vs factory: list order is INIT, factory (`FX_FACTORY`), MY PRESETS (`fxUserList`, localStorage), THIS SONG (`fxPreSongList`,
  those not already in MY PRESETS). A pump effect has only its own list. Names can repeat across sections: `pname` match takes the first.
- The overlay's own list clamps at both ends (`fxpMove` 27637: `clamp(fxp.i + d, 0, fxpN() - 1)`); it does not wrap.

## 3. What Cmd / Ctrl + Left / Right do today

Document keydown (35412): `typing(e)` returns first (inputs, textareas, selects, contenteditable, a pump window's own keys), so in a
text field the browser's line start / end (and word moves with Alt) are untouched. Then `fileKeys`, tracker only, then
`fxp.open && fxpKeys` (overlay open: `fxpKeys` 27664 has no case for modifier + arrows, falls through), `globalKeys`, then
`keysDown` (35634). `keysDown` with `mod` (Ctrl or Cmd, `e.ctrlKey || e.metaKey`, both on every OS) ends in 35665-35667:
`bump(a === 'right' || a === 'up' ? 1 : -1, a === 'up' || a === 'down')`: **Cmd/Ctrl + arrows change the value under the cursor**
(◀▶ by 1, ▲▼ by more), in KEYS and PAD mode alike (in PAD mode the pad's EDIT + arrows reach the same `bump` through `dirPress`).
The caller calls `e.preventDefault()` whenever `keysDown` returns true, so the browser's history back / forward (Cmd+Left/Right in a
Mac browser tab; Electron has no such default unless a menu adds it) never fires on these keys inside the tracker.
- Tracker grid, KEYS and PAD: a value changes (note, volume, instrument, FX value, Song block number...); on a cell with nothing
  to change (action rows, e.g. `› PRESET`) `bump` returns (27542: `if (!r || !r.get) return`).
- Mixer: the value (volume, pan; on the CHN / FX rows on / off); `tv === 'mod'`: `modBump`.
- Synth page (`view === 'synth'`): the tracker handler does not run (`view !== 'tracker'` 35418). Only `soloMuteKey` and undo keys
  are taken there; Cmd+Left/Right are not handled (a browser tab would go back; Electron: nothing).
- FX bar with the keys (`dkFbFoc`, `dkFbKey` 32053, a capture listener that stops the event): Cmd/Ctrl + arrows change the selected
  value (◀▶ ±1, ▲▼ more). The bar has no preset row, so this is a value key only.
- Text fields: untouched (see above). `typing()` also covers the `.pickbtn`.
- Alt + arrows change screen; Shift + arrows select. Neither is touched.

## 4. Decisions (see the PR for the rest)

- The key goes where the other value keys go, `bump`: on an action row that has a `pstep` (the `› PRESET` row) it steps the preset;
  every other row, and every other cursor position, keeps its behaviour. Because `bump` is also the pad's EDIT + ◀▶, the phone's EDIT +
  ◀▶ on that row steps presets too (nothing was bound there).
- Owner's decision (19 Nov): from a parameter row of the slot Cmd/Ctrl + ◀▶ steps the preset too (cursor stays). That row's value nudge moves to Cmd/Ctrl+Shift+◀▶ (Shift+arrows with a modifier had no other use on ◀▶). The slot list (SLOT n rows: effect swap), TRACK and ACTIVE keep their own Cmd+◀▶. The pad's EDIT+◀▶ on a parameter row is unchanged.
- The FX bar has no preset row: the key there stays a value key.
- Ends: stop at the first and last preset, as the preset list does (no wrap); a toast says "first / last preset".
