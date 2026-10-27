# Usability audit (26–27 Oct 2026)

Four audits of Thunder Plus at build 254: keys, mouse/trackpad/touch, behaviours, and a screen-by-screen visual
walkthrough (desktop 1400 and 1920, Fold unfolded 882x1104, Fold cover 412x915). The owner-facing write-up with the
plan is the doc "Thunder Plus: usability audit and plan" (https://claude.ai/artifact/8yDBMKuKciLpnVqYRHVrTH). This
file is the agent-facing version: where each finding is in the code.

Line numbers are `index.html` at build 254 unless a file is named; they drift as the file changes, so grep the named
function. "Owner decision, revisit?" = a choice the owner made on purpose (see HANDOVER.md); don't change it without
asking. Severity: P1 breaks expectations or costs work, P2 friction, P3 polish. Effort: S < 1 h, M hours, L a day+.

Phase 1 (bugs, no design changes) is tracked in HANDOVER.md, "27 Oct: usability audit".
Phase 1 is done (build 255, HANDOVER "27 Oct (2)"). The owner's calls on every "Owner decision, revisit?" row are in
HANDOVER "27 Oct (3)": build Phase 2 to those.

## Keys

| # | Finding | Where | Sev | Fix |
|---|---|---|---|---|
| K1 | KEYS mode: Shift, Z, X stay held. Keys `keysDown` doesn't use fall through to the PAD table; keyup returns early in KEYS mode, so the PAD modifier is never released. Shift then X on an empty Song cell pastes; Z then X on a volume cell clears it | keydown/keyup ~21714–21720 | P1 | In KEYS mode return before the PAD fall-through, or release on keyup in both modes |
| K2 | Insert deletes rows 16–31 of a 32-row phrase and throws (`A.length = 16`; phrases store 32 rows) | row insert ~17748; PROWS 13999 | P1 | Use the phrase's own length / PROWS |
| K3 | Help (2372–2373), key legend (2240–2258) and README:213–215 describe the pre-25-Oct Space order, Ctrl+Shift+arrows (now Alt+arrows), CLN on Ctrl+D / Ctrl+Shift+D (dead; Chrome bookmarks all tabs), swapped Delete/Shift+Delete, `/` as a note; Help omits `/` `\` mute, Ctrl+/ solo, Ctrl+Space, Ctrl+T, Ctrl+Alt+C; "keys that work everywhere" lists tracker-only keys. The first-run tour says "Space plays this screen, Shift+Space the song", wrong on desktop KEYS mode | Help, legend, tour, README | P1 | Text pass; later one key table generates all of them |
| K4 | Delete on a selection = Cut (replaces the clipboard) | `del()` ~17707 → `copySel(true)` ~17729 | P2 | Clear without touching `clip` |
| K5 | Note keys use `e.key` (layout-dependent): QWERTZ plays A for C, AZERTY loses sharps and the `1` note off | ~21906, Synth page ~10722, waves ~3140 | P2 | Note keys from `e.code`; hex/FX letters keep `e.key` |
| K6 | AltGr (Ctrl+Alt on Windows) counts as Ctrl: `{ } [ ] \` on DE/FR/Nordic layouts never reach their commands | ~21830, ~21842 | P2 | `e.getModifierState('AltGraph')` |
| K7 | A `<select>` keeps focus after a pick (the button blur at ~7404 doesn't cover selects): piano keys and arrows go to the select | `lType` ~7651 and other selects | P2 | Blur on `change` (as `fxAdd` ~10586) |
| K8 | Synth page key handler checks only `view !== 'tracker'`: on the …waves screen the arrows change the hidden pad | ~10708 | P2 | Require `view === 'synth'` |
| K9 | …waves/…chains frames keep F1, F5–F8, Space (waves), song undo; Ctrl+S/N/O act on the patch (downloads `.waves.json`) | waves 2913/1719, chains 3348/1777 | P1/P2 | Owner decision, revisit? Pass app keys up like the Alt+digit view keys |
| K10 | No Insert key on Macs/laptops; no Insert/Remove row in Edit ▾ | — | P2 | Menu items + a free key (Ctrl+Enter) |
| K11 | Browser: F5 reloads with no prompt (no `beforeunload`), F6 address bar, Ctrl+D bookmarks, on the Synth page | — | P2 | Transport keys on the Synth page; `beforeunload` when dirty |
| K12 | Space differs per screen/mode | `playKeySwap` ~18290, Synth ~10709 | P2 | Owner decision, revisit? |
| K13 | F1 opens Help with Alt/Shift held (blocks Alt+F1/F2 transpose) | ~22366 | P3 | Bare F1 only |
| K14 | Mac: ⌃Space input switch, ⌘⇧/ Help search, ⌘M minimise (MIDI learn in editors, waves 2916), ⌘R reload | — | P3 | Document; move MIDI learn off ⌘M |
| K15 | Ctrl+B ignores key repeat | ~21843 | P3 | Ignore `e.repeat` |
| K16 | Ctrl+▲▼ values / Alt+▲▼ screens vs Renoise pattern/instrument; Backspace doesn't pull rows up | — | P3 | Owner decision, revisit? |

Missing commands (Renoise key): octave transpose and track/pattern scope (F11/F12, Alt/Shift/Ctrl scope), interpolate
(Ctrl+I), end block (Ctrl+E), octave on a laptop (Ctrl+[ ]), instrument prev/next (numpad − +), note off on A, song
row insert/delete and first/last (Ctrl+Insert/Delete, Ctrl+Home/End), follow (Scroll Lock), play current row,
preferences (Ctrl+,), optional edit lock (Renoise Esc). Sources: https://tutorials.renoise.com/wiki/Keyboard_Shortcuts,
https://files.renoise.com/manual/Renoise%20User%20Manual_28.pdf

## Mouse, trackpad, touch

| Surface | Drag | Fine | Reset | Wheel | Type | Right-click |
|---|---|---|---|---|---|---|
| `makeRow` sliders (~7474) | native, jumps | Shift at press only (~11727) | dbl-click slider/label | focused slider only, 2 %/event (~11721) | dbl-click value | Reset/Type/Learn |
| Deck …chains rows (~20139) | same | same | same | same | same | two menus (bug) |
| Deck volume fader (~19839) | relative on cap | Shift ×0.25 | dbl-click 0 dB | none | none | none |
| Mixer fader bars (~19008) | absolute | Shift 1 step/4 px | dbl-click E0 | none | none | Edit menu + starts a drag |
| Tracker values (~21499) | vertical 16 px/step | Shift = BIG steps | none | hover, 40 px/step | keys | Edit menu |
| pump windows (pump/ui/kit.js) | relative | Shift, re-anchored (238) | Ctrl/Cmd/Alt-click (701) | hover, sign only 2 % (743) | dbl/right-click | menus |
| …chains/…waves sliders (chains 2113) | native, jumps | none | dbl-click | zooms canvas (2756) | dbl-click | module menu |
| Song/Arrange (`sxDown` ~20689) | move; Alt/Ctrl copy; Shift select; Ctrl/Cmd picks | – | – | native; Ctrl+wheel/pinch zoom | – | full menu `sxCtx` |
| Tracker grid (`msel` ~21344) | select; drag selection moves; Alt/Ctrl copies | – | – | cursor row / 40 px | – | Edit menu; track number solos |

| # | Finding | Where | Sev | Fix |
|---|---|---|---|---|
| M1 | Native sliders jump to the click; dbl-click reset jumps first; Fold tap while scrolling changes value (`touch-action:pan-y` 212) | `makeRow` 7477, chains 2116 | P1 | Own pointer handling: relative, no-move click = nothing, ~8 px threshold on touch |
| M2 | Wheel = one step per event: a trackpad flick (30–100 events) moves 60–200 % | SLIDER MOUSE ~11725, pump `pWheel` kit.js 743 | P1 | Accumulate deltaY (lines→px, ~100 px a notch), as the tracker does (~21623) |
| M3 | Deck …chains row: `makeRow`'s menu (7500) + `dkcMenu` (20142, `askPick` modal) both open; slider menu over the modal | ~20142 | P1 bug | `makeRow` `extraItems`; drop the second listener |
| M4 | Mouse hold on an Arrange block mutes it | ~20635–20639 | P1 | Owner decision, revisit? Touch/pen only |
| M5 | Drag from the cursor's filled cell edits the value | ~21380, ~21511 | P1 | Owner decision, revisit? Drag always selects |
| M6 | Shift fine vs big steps by surface (Mixer VOL cell under a fader) | ~17285, ~21594, ~21625 | P1 | Owner decision, revisit? |
| M7 | Shift mid-drag jumps (deck fader 19844 rescales; Mixer bar 19020 switches mode; `makeRow` ignores) | — | P2 | Re-anchor on modifier change |
| M8 | Wheel only on the last-clicked slider, no visible focus, grabs page scroll | ~11722 | P2 | Owner decision, revisit? Hover-to-wheel after a pause |
| M9 | …chains/…waves sliders lack wheel/Shift/menu; wheel zooms (2756); trackpad guess (2754) misreads Windows touchpads | chains/waves | P2 | Share the slider; owner decision on zoom |
| M10 | No context menu on pads (right-click plays, ~10658), track numbers (solo only), Mixer strips (generic), FX slots, layers; elsewhere suppressed (1761) | — | P2 | Menus per item |
| M11 | No long-press menu on tracker cells / Arrange blocks (pump ~709 and section lane ~21013 have one) | ~21551 | P2 | Owner decision, revisit? |
| M12 | Touch targets 13–30 px (deck rows 1326/1367, Mixer grab 731–732, Arrange 16 px); no `(pointer:coarse)` rule (chains has 276–281) | — | P2 | ≥ 40 px on coarse pointers |
| M13 | Mac pinch (Ctrl+wheel) scrolls the tracker / changes a VOL cell | ~21618–21626 | P2 | Ignore Ctrl+wheel there |
| M14 | Four menu systems; Copy/Cut/Paste order differs (20911 vs 18494); "Ctrl+C" hard-coded on Mac | — | P2 | One component with `keyHint` |
| M15–21 | Cursors (687, kit.js 805); fixed-speed vertical-only edge scroll (21414, 20737); Mixer bar drags on any button (19009); page-wide non-passive wheel (11721); stage ✕ hover-only (1418); slider undo grouping 350 ms (14271); hex drag 16 px/step | — | P3 | — |

## Behaviours

| # | Finding | Where | Sev | Fix |
|---|---|---|---|---|
| B1 | "Save changes?" misses `setKit` (11129–11136: Load kit, Factory drums, Clear slots), `wavesChanged` (12559), pad/kit names (7571–7572), choke (7570), SLOTS (7591–7602); only `saveS`/`changed` mark dirty (16301) | — | P1 | Mark dirty from all of them |
| B2 | Open project keeps the undo history; undo brings back the old song while `projFile` points at the new file; Ctrl+S then writes it over the new file; song and kit are two steps | `setSong` ~16724, history ~14239–14303, `undoStep` ~14294 | P1 | Fresh history on Open (Funk breaks keeping undo is the owner's decision, HANDOVER ~2039; Open is not that) |
| B3 | `toast(msg)` ignores a second argument (36 callers pass ms) and always clears in 1.3–2 s; one element, so the next toast wipes the last (newer-build warning 14351 wiped by "Loaded…") | ~7516–7521, `#toast` 2396 | P1 | Honour ms, queue, errors longer, `aria-live` |
| B4 | One undo step per mouse move: `stSet` 19837–19848, master EQ 19658–19661, `gridDragMove` 21591 → `bump` → `setCell` → `saveS`, deck FX 20032 → `fxChanged` 15590, tuning 10448, Settings sliders 22548 → `tsetSet` 22508 | — | P2 | `undoBegin`/`undoEnd` per gesture |
| B5 | Editors' own undo; Thunder Ctrl+Z reverts `S.chainPatch` and drops the frame (14299); parent ignores Ctrl+Z there (21708) | — | P2 | Owner decision, revisit? (HANDOVER ~1732) |
| B6 | Undo toasts only "Undo"; Synth-page Ctrl+Z (14306) undoes invisible phrase edits | — | P2 | Named steps |
| B7 | Not undoable: pad/kit name, choke, …waves edits | — | P2 | — |
| B8 | No `beforeunload` in the browser (desktop asks, 16415) | — | P2 | Ask when dirty |
| B9 | Two tabs: the warning shows once for 2 s (14329); a `BroadcastChannel` exists (11083) | — | P2 | Lock + lasting banner |
| B10 | Backups skipped while playing (16402), full base64 projects (16396), failures console-only (16282) | — | P2 | Back up on stop; report failures |
| B11 | Files dropped on the …chains/…waves screens may navigate the frame (no drop handlers; Electron `will-navigate` guards the main frame only) | — | P2 | Cancel and forward drops |
| B12 | A dialog from Settings sits under `.setmenu` (z 120 vs `.ask` 70, lines 63/553); the Settings key handler (22641–22647, window capture, `stopImmediatePropagation`) eats Enter/Escape | `askSheet` | P2 bug | Close menus in `askSheet` (as `sheetShell` 22412) or raise it and let it take keys |
| B13 | "Save changes?" (list mode): no OK button (7544), `ok.focus()` no-op (7559), Enter = NaN = Cancel (7551, 7564); used by `okToLeave` (16311) | — | P2 bug | Focus the first choice; Enter picks it |
| B14 | Four confirm styles: `askSheet`, inline Yes/Cancel (11670–11678), "Tap again" (11448), "Tap EDIT twice"; Settings uses `askYes` (22516) | — | P2 | One |
| B15 | Render progress = 2 s toasts (16084, 16099, 16121) + "RENDERING" (18966); raw errors (16131); songs over 20 min cut silently (16073) | — | P2 | Progress + Cancel |
| B16 | Clipboard in memory only, per screen (16907, 17911, 17943) | — | P2 | System clipboard text format |
| B17 | Synth page: no Ctrl+C/V (10708); FX chains/instruments uncopyable; Mixer/FX no selection (17904) | — | P2 | — |
| B18 | Second Stop does nothing more; All Notes Off only on used channels (12412); full reset in Settings → Audio (15342) | `tStop` ~15766 | P2 | Second Stop = panic |
| B19 | Naming (block/pattern/phrase; pad/slot/voice; "put a sect on it" 15672 with Sect hidden 16733); Help "⚙ Settings → MIDI" (2380) vs Audio & MIDI ▾ on desktop (11258); Factory drums vs Factory kit (11402 vs 1847); three update commands | — | P2 | Glossary + sweep |
| B20 | `snap()` = `JSON.stringify(S)` per save (14246), per mouse move with B4 | — | P2 | Grouping + size cap |
| B21 | Sample trim only when stopped (11028); 2 MB page parse; SW deletes old caches at once (sw.js 14–15) | — | P3 | — |
| B22 | `user-scalable=no` (5); muted `#8a8a8a` on white 3.45:1 (1543, 1577); toast no live region | — | P3 | — |

## Look (walkthrough; screenshots in the session scratchpad, not kept)

| # | Finding | Sev | Fix |
|---|---|---|---|
| V1 | Fold unfolded (882) and 1024 px: the transport strip covers the screen tabs (FX shows "F", PROJECT hidden, a tap on FX hits BPM); the tab strip scrolls with no sign | P1 | Transport on its own row when narrow, or a popover |
| V2 | 1024–1180 px: the top bar wraps into a ragged second row (CPU, ?, Transpose, Song); Synth page on the Fold too | P1 | Overflow menu |
| V3 | Dark skins (Minimal black, Neon, Pastel on dark OS) keep a light pastel tracker grid; 30 skins of mixed quality | P1 | Grid from skin variables; ~4 skins |
| V4 | 36 font/size/weight/spacing mixes on the Song screen (8–22 px, 8.5, 10.02, 12.576 px); case mixed ("Tracker · Synth" vs "SONG · PHRASE"; "44.1k" vs "44.1K"; "...Thunder" vs "…chains"); Helvetica Neue isn't bundled (Arial on Windows, Roboto on Android) | P2 | Bundled UI font, 5 sizes, ≥ 10 px |
| V5 | "LEFT 0:02" → "—" shrinks 11 px and shifts Rows/−/+; italic CPU box changes width | P2 | Fixed widths, tabular-nums |
| V6 | Contrast: "C4 · MONO" 1.4:1, "PH 00 I00" 3.0:1, deck values 3.0:1 (lighter than labels), FX "TRACK 01 · SIGNAL" nearly invisible | P2 | Two muted tiers ≥ 4.5:1 |
| V7 | Two mono hint lines everywhere; 20 px on the Fold, ~8 px on desktop Phrase; Arrange hint cut off | P2 | One line, one size, a switch |
| V8 | Menus need the keyboard: Enter/Space/↓/Alt+F don't open or move through File ▾ | P2 | Menu keyboard support |
| V9 | Song: "−", "○", "+", "SECT +" unlabelled at 24 px; dogs take half the screen; sidebar repeats the tab row and squeezes a cheat sheet into 210 px | P2/P3 | Labels; smaller dogs; drop duplicates |
| V10 | Phrase at 1400: ~10 px grid in the top 40 %, empty below (TEXT SIZE 80 % default); header controls ~16 px tall beside 36 px transport; "04 / -16" header reads as negative | P2 | 100 % default; one control height |
| V11 | Mixer text 2× Phrase's; empty dB box when stopped; meters a flat block. FX shows 4 of 8 slots over ~350 px empty; deck values cut mid-word | P2 | Segmented meters; show all slots |
| V12 | pump windows: 6–8 px letter-spaced microtext, float over the transport with no backdrop, a different visual language | P2 | ≥ 10 px; dock/dim |
| V13 | Synth page: 2 px nested frames, dashed empty layers, native selects beside custom pickers, waveform cut at the left, "0 ct" vs "+7 cents", "C4 at C-4" | P2 | 1 px dividers, styled selects, one unit style |
| V14 | …waves help box covers ADSR rows; "Tape del…" cut; three project names ("Starter beat", "Thunder 1", "Untitled") | P2 | — |
| V15 | Menu items with wrapping explanations; 27-item right-click menu; View/Audio read like docs (9-line Mixer size note); native selects cut ("its MIDI gea"); rename-dialog swatches all empty in grey skin; no primary button styling; toasts over deck sliders; "Tap again: New song" with no warning colour | P2/P3 | Short labels, ⓘ, primary buttons |
| V16 | Fold PAD keys: offset "shadow" looks misprinted; ▶ alone on a row. Cover screen: ? hidden (no Help), CPU "- -", CLICK/OCT/LOOP/TRANSP gone, last Project row under the hint line, pad names "Sna…", layers 5–8 hidden. Arrange column numbers run together ("10111213141516") | P2 | Cover-screen pass |

Good already: the tour (short, adapts to PAD/KEYS), the engine picker and preset browser, pump windows' panels and
knobs, focus rings, reduced-motion support.
