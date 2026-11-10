# Thunder Plus usability audit, step 4 (beta, build 544)

Walked headless Chromium at 369x800 touch, 800x900 touch, 1440x900 mouse and 1920x1080 mouse, on a clone of `beta`.
Screenshots: `/tmp/claude-0/-home-claude/d264eb71-4c3c-5584-8350-19d577ccaae5/scratchpad/step4/shots/` (prefix `fold-` 369, `unf-` 800, `mac-` 1440, `win-` 1920). Line numbers are `index.html` at build 544.

**Not covered or not trustworthy (read this first)**
- The sandbox was under heavy load from other processes (load average 35-50, one OOM kill), so some taps and clicks timed out in my harness. Every finding below was seen in a screenshot or a DOM read, not inferred from a timeout.
- Save / Export: `showSaveFilePicker` hangs in headless Chromium, so no file was written and the "Saved" toast was not seen. Save, Open and the downloads path are NOT verified.
- No real audio device: playback, meters, "Playback keeps dropping out" and the CPU toasts are environment noise and are not reported.
- Not walked: Pattern view on touch, Synth page unfolded (800), Waves on touch (it is desktop-only), pump windows, MIDI pop-outs, Record-a-sample actually recording.
- Colour contrast: DOM text only (the tracker grids are canvas). Pattern view contrast is estimated from a screenshot.
- No page error was thrown at any size (checked after every walk).

**Counts:** blocks a core job 0, slows a core job 8, cosmetic 9, unconfirmed 1. Would-change-an-owner-decision: 1.

## Findings, ranked (severity, then effort)

### Slows a core job

**U1. Help on the folded phone is about 6 steps deep.** Fold 369 (800 has the `?`). Help is only at the very bottom of the Settings sheet (the sheet is 3,550 px tall): gear, scroll past Audio, Project, Screen, MIDI, Kit, tap "Help & keys". The top bar has no `?` below about 400 px. Reading the help for a key is a core job. Shot: `/tmp/claude-0/-home-claude/d264eb71-4c3c-5584-8350-19d577ccaae5/scratchpad/step4/shots/fold-f16-settings.png`, `fold-f17-settings-scroll.png`, `fold-f50-help.png`. Fix: add a "Help & keys" row to the File menu on touch (or the top of Settings). Where: File menu item list built for `#gfile` (grep `fitem` creation near `bFile`, markup line 2773); Settings `#gsetBody` (line 2793 area). Effort S.

**U2. FX screen: EDIT does nothing the first time, with no message.** Fold. The cursor starts on the TRACK row (pink `01`), but the hint under the screen says "EDIT adds". Pressing EDIT there does nothing, silently; the user must press ▼ first. Tapping the ADD TO SLOT row did not add either. Shot: `fold-f30-fxadd.png`, `fold-f32.png`. Fix: put the cursor on SLOT 1 when the screen opens (or make EDIT on TRACK toast "Move down to a slot"). Where: `go()` line 25301 / `cs.fx` default; rows built at line 23807. Effort S.

**U3. Settings sheet has no Close and is 3,500 px long (fold).** The only way out is the gear in the top bar (it shows as pressed). Skin is at the very bottom. The Skin picker, Presets and Engine pickers all have a Close or Done; Settings does not. Shot: `fold-f16-settings.png`. Fix: a sticky "Done" in the sheet header like the Skin picker's. Where: `#gset` / `.gsetbody` (line 2793). Effort S.

**U4. Touch targets under 40 px (fold and unfolded).** Measured by DOM, css px:
- header Play `.tsong` 32x34 (line 1294), CPU `.tcpu` 37x34 (1304), File 46x34, Settings 40x34, Tracker/Synth `.seg button` 57x30 / 42x30 (442), screen tabs `.ttabs button` height 36, FX tab 35 wide (899);
- Arrange zoom `#tArr .tahd .azb` 24x24 (1562), PIN `.appin` 40x18;
- preset / engine favourite star `.pfav` 32x14 (918), sheet Close / Done 56x30, tour buttons 34 high (822);
- Settings switches 24-26 high, "Reset audio engine" and "Copy playback report" 26 high.
The File menu rows are in U5. Shots: `fold-f21-presets.png`, `fold-01-song.png`. Fix: one `@media (pointer:coarse)` block raising `min-height:40px` (heights only, so the six tabs still fit folded) for the selectors above; star and zoom get `min-width/height:40px` with the glyph kept small. Effort S for the list, M if the header height change needs a layout pass. The 19x14 px Mixer / Song canvas cells are a design question, see decisions.

**U5. File menu rows are 30 px on touch, with keyboard shortcuts shown.** Fold, unfolded. `.gfile .fitem{min-height:0}` (line 100) beats `.setmenu button{min-height:40px}` (line 1002, inside `@media (pointer:coarse)` opened at line 995), so the touch rule never applies. The rows also read "Ctrl+Z", "Ctrl+Shift+S" on a phone. Shot: `fold-f10-filemenu.png`. Fix: add `.gfile .fitem{min-height:40px} .gfile .fitem kbd{display:none}` inside the coarse block. Effort S.

**U6. Synth page, folded: picking the Layers engine looks like nothing happened.** The picker closes, the engine label changes, and the Layers editor is 600 px further down, behind the pad dock, with no scroll cue. Shot: `fold-f25-layers.png`, `fold-f26-synth-600.png` (scrolled). Fix: after an engine pick on touch, scroll `#main` so the engine's editor is in view. Where: engine picker result handler (grep `xPreBtn` / engine pick near line 2928). Effort S.

**U7. Keyboard: Tab never moves focus (Mac and Windows sizes).** 40 Tab presses after clicking the page left `document.activeElement` on BODY every time. File, Edit, View, Audio & MIDI, Settings, Play and `?` cannot be reached from the keyboard, Alt+F / Alt+E do not open the menus, and there is no visible focus ring anywhere in the tracker screens. Ctrl+S / O / N and F1 / `?` do work. Cause: Tab is the tracker's "next track / next column" key (line 31882). Menus already have arrow-key handling once open (`menuNavKey`, line 13854), they just cannot be opened. Shot: `mac-m-tab5.png`. Fix (no change to Tab): F6 / Shift+F6 cycles focus into the top bar, and Alt+F / Alt+E / Alt+V open the menus. Effort M. See the decisions list.

**U8. Chains screen on the unfolded phone opens badly framed.** 800 wide: the patch sits bottom-right and Chain Out is cut off at the right edge, the help box covers modules, and its text tells a touch user about "Option" and "Alt on Windows". Desktop has a Fit button; the 800 top bar does not. Shot: `unf-u-chains.png`. Fix: Fit on first open when the patch is wider than the frame, and collapse the help after the first time. Where: `chains/` frame (patch view init and `Help` box), top bar buttons at 800. Effort M. (The help box covering modules is also listed in the earlier audit as V14 and is still open; see U17.)

### Cosmetic

**U9. Two buttons both called "Presets…" on the Synth page** (fold, Mac, Windows). One is the pad presets (`#vPre`, line 2928), the other the engine's (`#xPreBtn`). Different lists, same label. Shots: `fold-f20-synth.png`, `win-w2-synth.png`. Fix: "Pad presets…" and "Engine presets…". Effort S.

**U10. The unsaved dot widens File and clips "Synth" (fold).** After the first edit `#bFile.dirty::before` (line 733) adds a bullet, the button grows, "Tracker | Synth" is squeezed and "Synth" loses its last letter; LEFT shifts left. Shot: `fold-f30-fxadd.png` compared with `fold-01-song.png`. Fix: position the dot absolutely on the button corner. Effort S.

**U11. Toasts sit on the controls you just used (fold).** After File → New song the toast covers Tracker/Synth, File, Settings and Play for about 2 s (line 1322, `top:3px`). Shot: `fold-f12-newsong2.png`. Fix: top at the tab strip's bottom (about 112 px) instead. Effort S.

**U12. Settings text that only makes sense with a mouse, on a phone.** "Pattern view: right-click a row number to color the song row" (line 23912). Effort S: hide or reword under `(pointer:coarse)`.

**U13. First-run tour footnote is squeezed (fold).** "The hint line under the grid shows the keys…" wraps into a five-line column beside the two buttons. Shot: `fold-00-load.png`. Fix: `flex-wrap:wrap` on `.welcome .wfoot` (line 821). Effort S.

**U14. Picker rows with no description are 90 px tall (fold).** MOD target picker: VOL / PAN / FX rows have an empty second line, so every row is twice as tall as the 45 px list rows elsewhere. Shot: `fold-f36.png`. Fix: drop the empty `<small>` in `perfPick`'s `item()` (line 28112 area). Effort S.

**U15. Hints say "Ctrl/⌘+C" on Windows** (the Song screen "SELECTING" hint). Effort S, show the platform's own key.

**U16. Contrast (DOM text, IDM, IDM pink & green, IDM pink paper, 909).** Under 4.5:1: BPM and LPB values in the transport (`#tClkB`, `#tClkT`, `.tclk .tce` line 1216) pink on pale, 12 px: 2.7 (IDM), 2.9 (IDM p&g), 2.9 (pink paper), 1.9 (909); row numbers `.rn` 3.3-3.5:1 at 9-10.5 px in IDM and IDM p&g; group label "POLY 09-16" `.gp` 3.3-3.5:1 at 9 px; "Stop" `.tl` 3.5:1 on pink paper; the stage label `#tStagePhT` 1.5:1 in all four. 909 Pattern view, dimmed other-track notes: about 1.6:1 (estimated from `mac-m-909-phrase.png`). Fix: use `--ink` for the clock values and row numbers, a darker accent for 9 px labels. Effort S to M (touches skin variables, not audio).

**U17. …waves help box covers the ADSR rows (Mac and Windows, `mac-m-alt8.png`).** Still open from the earlier audit (V14). It has an ×. Effort S: start collapsed after the first visit.

### Unconfirmed

**U18. MOD target picker opened with TRACK 09 expanded while the MOD screen said TRACK 01 (fold, once).** `tgtPicker` (line 28121) passes `Math.min(t0, G.length-1)`, an index into the groups list, not a track number; that matches the track only while every track has targets. Not reproduced on a second run. Shot: `fold-f35x.png`. Effort S once confirmed.

## Quick wins (S effort, no owner decision): work list for another engineer

1. Coarse-pointer block (`@media (pointer:coarse)`, opens line 995): `.gfile .fitem{min-height:40px}`; `.gfile .fitem kbd{display:none}` (U5).
2. Same block: `min-height:40px` for `.tsong`, `.tcpu`, `.ttabs button`, `.seg button`, `.welcome .wfoot button`, sheet Close / Done buttons; `.pfav`, `#tArr .tahd .azb`, `.appin` to 40x40 hit area (U4). Check the folded header still fits at 369.
3. FX screen: cursor starts on SLOT 1, or EDIT on TRACK toasts a hint (U2).
4. Settings sheet: sticky "Done" in the header (U3).
5. Add a "Help & keys" item to the File menu on touch (U1).
6. After an engine pick on touch, scroll `#main` to the engine editor (U6).
7. Rename the two "Presets…" buttons (U9).
8. `#bFile.dirty::before` absolute-positioned (U10).
9. Toast `top` below the tab strip on `body.mob` (U11, line 1322).
10. Hide the right-click wording on touch (U12); `flex-wrap:wrap` on `.wfoot` (U13); drop the empty sub-line in `perfPick` (U14); platform-correct "Ctrl" vs "⌘" in hints (U15).
11. Clock values and row numbers use `--ink` in IDM, IDM pink & green, IDM pink paper, 909 (U16).

Nothing above touches audio, timing or sequencer code. Nothing needs **audio review**.

## Needs the owner's decision (plain questions)

1. Keyboard focus: may F6 (and Alt+F / Alt+E / Alt+V) take you into the top bar and open the menus on desktop? It leaves Tab alone (U7).
2. Do you want a "Help & keys" row in the File menu on the phone, or in a different place (U1)?
3. Mixer, Song and Pattern cells are about 19x14 px on the folded phone. Tapping selects, and the pad does the rest. Leave as is, or make the Mixer rows taller on touch only?
4. Should the Settings sheet get a Done button, or is the gear-as-toggle how you want it to work (U3)?
5. Chains on the unfolded phone: Fit the patch on open (U8), and hide the Option / Alt help text on touch?
6. Toasts at the top of the phone screen cover the buttons you just used. Move them below the tabs, or leave (U11)?

## Would change an owner decision (not in the fix list)

1. Tab key (owner, 31 Oct: "Tab moves by track, as Renoise"): making Tab move focus through the top bar would change that. U7's F6 route avoids it; if you want Tab itself to leave the tracker, that is this one.

Checked and left alone: Save beside MUTE unfolded only, the COMMANDS row, the dual play buttons, the 4 s FOLLOW hold, hold-to-mute on touch, EDIT pick up / drop. U4 raises heights only and keeps all of them.
