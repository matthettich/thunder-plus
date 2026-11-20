# Press and drag a bar (build 649): what was found, what was built

Owner, 19 Nov: "Can we also make sliders like the swing slider controllable by clicking on the slider and dragging?"

## What was found (index.html line numbers are for build 649)

- **The bar is the browser's own `<input type=range>`, not a drawn one, and it was locked on purpose.** SWING is a row of `projRows()`
  (line ~27256). It is drawn by `tsetRow(r)` (~36953) as `[−] [range] [+] [value]`, in the Settings menu (`buildProj` / `buildScreen`)
  and in the settings sheets (`setSheet`, ~36872; SWING is in the Song sheet, Groove group). Since 27 Oct the CSS had
  `#gset / .rlock input[type=range]:not(.unl){pointer-events:none; opacity:.5}` and a script ("tap a slider to unlock it, then drag; it
  locks again after 4 s", `rlWire` / `rlLockAll`) because a finger scrolling the menu passed over the bars. That lock is what the owner
  met. It is replaced (not layered on).
- **The text screens have no bar.** The tracker's Project / Inst / FX screens (`rowsOf`, text drawn into `#grid`, ~30440) show the
  same rows as plain text (`SWING   50%`) and move them with the cursor and the keys (`nudge`, ~27597: `r.set(clamp(r.get() + d * step)); r.after(); saveS(); draw()`).
  Nothing to press there; not touched.
- **The Synth / Instrument / Mixer pages' sliders (`makeRow`, the Mixer faders, the FX bar) are another system** ("ONE SLIDER", 27 Oct, and
  "ONE RULE FOR VALUE CONTROLS", 3 Nov, ~17250-17400): a mouse drag moves the value *relative* to where it is (no jump to the click, Shift
  a tenth, Ctrl/Cmd four times), on a phone a sideways move of 6 px starts it, a tap does nothing; right-click Reset / Type a value,
  double-click types a value. That was an owner decision, it already scrolls and already drags, and it is left as it is.
- **Commit path of a tsetRow slider:** `input` event -> `put(v)` -> `tsetSet(r, v)`: `r.set(clamp(v))`, `r.after()` if any, `saveS()`, `draw()`,
  then `rg.value = r.get()`, the `<output>`, the +/- greying and the info text. `late:true` rows (MONO / POLY TRACKS): `input` only shows
  the number; `change` (release) does `put` and `buildProjKeep()` (the sheet rebuilds, the limits of the other row follow). PATTERN LEN
  rebuilds on `change`. Toggle / `segs` / `opts` / `act` / `show` rows are drawn as checkboxes, segmented buttons, selects, buttons, text:
  no range, not touched.
- **Undo:** `snap()` holds a snapshot while a pointer is down (`undoHeld`, set by a window pointerdown, released by pointerup /
  pointercancel; 27 Oct "ONE UNDO STEP PER GESTURE"), so everything a drag does is one step with no new code. The …chains editor
  of build 640 has its own mechanism; this is the app's.
- **Rows with an own curve:** none among the tsetRow rows. Every bar is linear in the input's own `min / max / step`
  (`rg.min = r.min` …, index rows such as ARRANGE LOOK-AHEAD are 0..n). The pointer maps through the input's range, so whatever the bar
  draws is what the pointer gets. (The curved sliders are `makeRow`'s, not touched.)
- `r.min / r.max` of a row are numbers read when the sheet is built (MONO TRACKS reads `tracksLimits()` then); `late` rows rebuild on release.

## What was built (`/* ===== BAR DRAG */` block at ~15337, tests in `tests/bar-drag.test.js`)

- One capturing `pointerdown` on the document; it acts only inside `#gset` / `.rlock` (menu and sheets) and only on a press that lands
  within 4 px sideways / 8 px up or down of a visible, enabled range (the range itself is `pointer-events:none`; buttons, selects and
  text inputs keep their own taps). That is **every range in the menu and the sheets**: the tsetRow rows and the Audio rows.
- Mapping: the thumb centre runs `left + 4.5 .. right - 4.5` (the 9 px thumb), snapped to the grid `min + k*step`, last step inside `max`
  (as the browser does), clamped. `min / max / step` are read at the press.
- The value goes in as a keyboard / native drag does: `input.value = v` + an `input` event while moving, one `change` on release. So the
  row's own `set`, `after`, `saveS`, `draw`, the `late` rule and PATTERN LEN's rebuild all run exactly as for a key press.
- The browser never sees the press (pointer-events:none), so it cannot jump the thumb on a scroll; `touch-action:pan-y` is on the
  cell that holds the range, so a vertical swipe scrolls natively and the script gets the sideways ones.

## Choices for the lead

1. **The lock is gone** (the owner's own 27 Oct decision, built to stop scroll accidents). The scroll safety is now the gesture rule (below),
   not the lock. If a mouse user reports stray changes from a click on a bar, the mouse press could ask for a drag of 3 px first.
2. **Text screens and the Synth / Mixer / FX-bar sliders were not changed** (no bar there / an owner-decided relative drag that already works,
   see above). A request to make *those* jump to the click is a separate decision (it would undo "no jump to the click").
3. **"Press selects that row":** the Settings menu and the sheets have no selected row (no cursor); the press focuses the slider so the
   arrow keys and `-- - + ++` carry on from there. Nothing else to select.
4. **Fine control on touch** needs the browser to keep the gesture after it started sideways; it should (Chrome and Samsung Internet
   filter by the first move's direction) but only the owner's phone can say: punch list.
5. **Throttle:** one `commit` costs 15-40 ms on this machine (saveS + draw; TEMPO also `rtAllFx` + `chainsTempo`), so the thumb, the
   number and the tip follow every frame and the song follows every 50 ms or 2x the last commit, and once more on release.
   Nothing expensive is deferred to the release only (re-timing while dragging TEMPO is wanted), except `late` rows.
