# Thunder Plus — handover notes

Pick-up notes for continuing this work in a later session. Read this together with
[`README.md`](README.md) (the app's own docs) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
Keep this file up to date at the end of a work session.

## Where things stand (13 Oct 2026, morning; newest work is in the dated entries below)

- **One repo now:** `matthettich/thunder-plus` (this one), branch `main`, pushed, nothing uncommitted.
  `matthettich/Thunder-` is **retired** (13 Oct): its page forwards to this app and its service worker removes
  itself. Don't build or edit it; its old app code is in its history (last build 58a0a43).
- **Commits this session** (Plus, oldest first):
  - `2dcda77`: 14 new effects, 6 oscillators, tempo-synced modulation, …chains Steps/Clock/Repeat, and the
    song phrase-number fix.
  - `eebb19e`: 9 more effects (comb, transient, trance gate, multiband, frequency shifter, vinyl, dub, cab, tube).
  - The **break-chopping** commit on top (slices, sync, stretch), plus this file.
  - Thunder- has a matching rebuild after each of them.
  - 13 Oct: the **Beads crash fix** and effect crash guards (see below), with a Thunder- rebuild.
  - 13 Oct: **one app, two setups** (Settings → App → Mobile) and the first-PLAY render fix (see below);
    then the owner chose **one app, one repo**: Thunder- retired (forwarding page), `build-thunder.py` removed.
  - 13 Oct: **pad rendering moved to a Worker** (see below), and the offer to open ...Thunder's saved work.
- **App:** no build step. `index.html` is the whole tracker/synth app; `chains/index.html` and
  `waves/index.html` are the two modular patch editors loaded in frames.
- **Cache-busters now:** `chains-worklet.js?v=15`, `waves-worklet.js?v=11`, `master-worklet.js?v=2` (index.html, the editor
  pages, sw.js). Cache is `thunderplus-v79`.

## 19 Oct (4): drumming dogs, Arrange on every screen, pattern play, poly OFF per column, clone-then-paste

Cache `thunderplus-v172` (not yet in a desktop build: v1.2.3 is the one before this entry).
- **DRUM dogs** (Phrase stage switch DANCE · DRUM · KEYS, `#tStagePs` styled `.sxsw`; `phStage`,
  `thunderplus-phstage-v1`, default KEYS; stage kind 'drum', too narrow for the dogs → keys): SVG `.kit` groups in
  each dog's `.bob` (pittie: hi-hat, crash, kick with pink head, snare, rack tom, sticks in the arms; husky: bongos on
  a stand), shown by `.dogs.drum`. The sequencer step adds `pos[t].pd` (the pad); `sndKeep` → `dogsHit`: pad 00 kick,
  01–02 snare, 03–04 hat (04 rings), 05 tom, 06–07 crash, any other pad the bongos (a chord both hands). `drumsBeat`
  nods heads and ticks the hat / taps a bongo on a beat with nothing to hit. Strikes are WAAPI on the arm group
  (`rotate translateY`), arms rest over their drums via `--aL/--aR`.
- **FX sideways shift**: draw()'s horizontal follow now sets the least scroll from 0 that shows the cursor, counting
  only the first half-screen of a long value (an FX slot's full effect name), so the labels stay on screen.
- **PHRASE | PATTERN persists**: `pvNav` is no longer called; Shift+▶ from the pattern goes on to Inst.
- **Pattern view PLAY** (`playCtx`): plays the song row on screen, looping (`tStart('song', {loop:{v:'pattern'}})`);
  `syncLoop` keeps that loop on the pattern's row (follows it to another row) and LOOP doesn't touch it.
- **Arrange beside every screen** (`apArrOk`: any screen but Song; `apHost` remembers it; `apBack` returns to it):
  a click gives Arrange the keys (Shift+◀ and the SONG tab only on Phrase, so screen navigation is unchanged), the
  screen follows the block (`apFollow`: FX → cFxT, Mixer → its track, Inst → the block's instrument), a double click
  opens the phrase, Esc / a click on the screen gives the keys back. Right-click in the side panel: Open the phrase,
  Mute, then `cmdItems()` run through `apRun`. Right-clicks no longer count as taps or hold-to-mute there. A click on
  an empty cell (with ARRANGE FOCUS off) just moves the cursor. Checked beside FX, Mixer and Inst.
- **POLY cells per skin**: light term skins mix `--tk-poly` 35% with white (plum went grey mixing green into pink);
  the dark ones keep mixing with their background.
- **Caps Lock**: a keyup without a keydown (a Mac turning Caps Lock off) types an OFF too (`capsDn`).
- **Poly OFF per column**: a poly row's OFF becomes `{type:'off', cols}`; voices carry `col` (all three voice makers);
  the player and MIDI out (`moOffCols`, held notes tagged with `col`) stop only those columns; the piano drops only
  those columns (`pos[t].offc`). Mono tracks unchanged.
- **CLONE = copy a unique clone** (Ctrl/⌘+D, CLN, Edit ▾, right-click): `cloneHere` puts the block (or selection) on
  the clipboard with `uniq`; `paste` → `pasteClone` makes a fresh unique copy (newBlock) at the cursor each time; the
  original stays. EDIT×2 / Enter twice still makes a unique copy in place.
- **⌥/Alt (or EDIT) + arrows on an EMPTY song block** start a new pattern on the next free number (`bump` → `newBlock(c,
  -1)`, sects hidden), not the last number used (00 on a fresh song). With Arrange holding the keys it opens in the editor.
- **KEYS mode: Ctrl/⌘+arrows change screen, Shift+arrows select** (owner): keysDown's mod branch calls `nav(dir)` in
  KEYS mode (PAD mode with a keyboard keeps Ctrl/⌘+arrows = OPTION+arrows), so ⌘◀▶ walk the tabs, ⌘▲ Project, ⌘▼
  Mixer, ⌘▶ on Song opens the phrase, ⌘◀ on Phrase gives Arrange the keys (the nav wrapper). Shift+arrows go through
  `shiftSel`: ▲▼ whole lines as before, ◀▶ a block selection from the cursor (a line selection becomes a block), then ▲▼
  grow the block; screens with nothing to select still change screen. Phrase navigation moved to Shift+Alt+arrows (in
  a phrase `optDir(dir, true)`, elsewhere `shiftAltArrow` as before). Ctrl/⌘+Shift+arrows still change screen too.
  Hints (HINTK, Arrange's lines, Pattern view's) and help (quick list, KEYS table, From PAD mode) updated.
- Checked headless: all of the above, no console errors (FX scroll 0 on a long name, pattern persists Inst↔Phrase,
  pattern play rows 0/0 0/1 0/2, plum poly cell #bbccc0-ish, poly OFF `note@0 note@1 off:0`, clone ids 3 and 4 with
  the original 0 untouched).

## 19 Oct (3): keys = pad controls, COMMAND BAR, playing piano, Arrange playhead, MST in Arrange, update notice

Same cache (`thunderplus-v172`). Desktop release v1.2.3.
- **Key audit (KEYS vs PAD):** every PAD control already had a key except two: **Ctrl/⌘+arrows now = OPTION+arrows**
  (all four, `optDir(dir, true)`: in a phrase the phrase above/below/left/right on the Song grid, on the Song the nearest
  block; it used to be ⌘▲▼ = previous/next by number, which `[` `]` still do) and **Shift+Enter = EDIT** (stamps over a
  filled note; Enter picks up). The KEYS help starts with a "From PAD mode" table; the quick help and comments updated.
- **COMMAND BAR** (View ▾ → "Show the command bar (KEYS mode)", also Screen sheet → Layout & panels; `cmdBar`,
  `thunderplus-cmdbar-v1`, off by default): in KEYS mode `#tCmd` gets `.cmini`, one 29px line above the bottom panel,
  ordered like Edit ▾ (`CMD_MINI_ORDER`, words in `CMD_MINI_LAB`), tooltips from `cmdItems()` with their keys
  (`cmdTips`). PAD mode keeps COMMANDS as before; in KEYS mode COMMANDS still shows the big row when the bar is off.
- **Phrase piano plays what you hear:** the sequencer step puts `pos[t].sc` (pitches a row starts, by note column, with
  sect/inst/TSP/PIT/random transposes) and `pos[t].off` (OFF or a muted block); `frame()` keeps them per track
  (`sndKeep` → `tp.snd`, dropped on start/stop). `stagePiano` lights `tp.snd[cTrack]` while playing, whatever phrase
  or row is on screen, and widens its key range to fit; stopped, the cursor's row as before.
- **Arrange playhead:** playing blocks are inverted (`.tagrid b.f.p`): before, an instrument-coloured block
  (`b.f[style]`) didn't change at all while playing (a stronger rule). Term and rainbow skins have their own rules.
- **MST in the side Arrange** (`arrSideMst()`, return false to undo): the master column shows beside Phrase / Inst /
  FX… too, so a master phrase can be tapped open and reached with Arrange's keys; `phraseGrid` (OPTION/⌘+◀▶ in a
  phrase) and `songGridDir` step onto it; apFocus/apBack no longer push the cursor off it. drawArr/arrSize: `mx`.
- **Desktop updates:** `electron/preload.js` (new, in build.files) exposes `window.thunderDesktop.updates` (state,
  check, act, on); main.js answers on `thunder:upd-*`. updater.js tracks `{version, avail, ready, notifyOnly}`, sends it
  to the page, and if electron-updater errors it reads `api.github.com/repos/<repo>/releases/latest` (net.fetch) so an
  unsigned Mac build still hears about new versions. The page: `#bUpd` "Update" in the top bar (stays until used;
  Mac/portable open the download page, Windows/Linux restart into it) + a toast; File ▾ → "Check for updates… (you
  have x)". Only builds from v1.2.3 on have the button; older ones still get the box ~8 s after opening.
- **Top bar** under 1440px: gap 6px and 8px button padding so Edit ▾ / Update don't wrap it (checked 1100–1440, KEYS
  and PAD: never taller than before).
- **Phrase playing line inverted** (owner: match Arrange): draw() marks the playing row's `.ln` with `plr` next to the
  row number's `pl`; CSS inverts the line (`--ts-ink` background, text `--ts-bg`), the cursor on it flips back, a
  selection keeps dark text. Phrase, Sect and Pattern view.
- **Mixer size jump** (owner: Shift into the Mixer, then moving the cursor resized it): draw()'s relayout check
  (`groupBoxes` + `fitCheck`) ran before the title / info / hint lines got the new screen's text, so on entering a
  screen it measured with the old lines; the Mixer's longer hint then overflowed ~6px and the next draw (the first
  cursor move) shrank it. The check now runs after those lines. Checked: every tracker screen keeps one text size
  while the cursor moves (1177×610, 1400×860).
- Checked headless (Playwright, debug-hook copy, a mocked `thunderDesktop`): all of the above, no console errors.

## 19 Oct (2): Edit ▾ menu, right-click commands, phrase mouse clicks, Enter picks up

Same cache (`thunderplus-v172`), same session as the entry below.
- **Edit ▾** (Desktop, `deskmenu`, next to File ▾; `#bEdit` / `#gedit`, styled as `.gfile`): `geditToggle` /
  `editItems`, after the File menu's JS. In the tracker it lists `cmdItems()`; Waves / Chains: that patch's Undo / Redo;
  Synth: Undo / Redo. Opening it closes File / Settings and the reverse; Esc closes it.
- **`cmdItems()`** (after the COMMANDS row's handlers): the `CMDS` row's commands as `{a, l, k, f, off}` menu items in
  words, with `keyHint()` keys (⌘/⇧/⌥ on a Mac, Ctrl+/Shift+/Alt+ elsewhere): Ctrl/⌘ ones and Del work in both input
  modes; the value is Alt+arrows in KEYS mode, X (EDIT) + arrows in PAD mode; Shift+Space plays from the cursor row.
  Song screen: clones instead of ±16, as on the row. Also Select all. `menuTidy()` drops stray rules.
- **Right-click** (mouse; Ctrl+click on a Mac): `gridDown` moves the cursor to the clicked cell (a selection stays when
  the click is inside it), then `cmdCtx` (scr `contextmenu`) opens `cmdItems()` with `sxMenu`. Arrange as the Song
  screen keeps its own SONG EXTRAS menu, now with Undo / Redo on top and the rest of the row's commands at the bottom.
  `.sxmenu` scrolls if taller than the window.
- **Phrase + mouse** (owner): a click on the cursor's cell no longer adds or stamps a note (it only moves the cursor);
  a drag on an empty cell adds nothing (`drag.hold`); a drag on a filled cell still changes it; a click on an effect
  cell still opens the picker. Touch unchanged (a tap on the cursor's cell is EDIT).
- **Enter picks up** (owner: Enter on a hihat, inst 03, then Enter elsewhere dropped a snare, inst 01): `editTap(enter)`;
  the Enter key and the KEYS keypad's ENT on a filled phrase note now make its note and instrument current
  (`pickUp(x, y, true)`, auditions the row, a quick second Enter doesn't make a note off). The EDIT button still stamps.
  `pickUp` now always sets `instPicked` when the row has an instrument, even if it was already `last.inst`. Help texts
  updated.
- **Synth waveform one size** (Desktop synth layout, `body.sdesk`): the rule that grew `.wave` to
  `clamp(150px, 100vh − 345px, 560px)` for any engine but Layers is gone; it is 120px for all 27 engines (checked at
  1150/1400/1920 px wide), so the Engine ◀ ▶ buttons stay put while stepping through engines (they used to drop ~400px).
- **Bass wobble delay** (owner: an LFO delay): `BassCore` params `wDelay` / `wFade` (ms, 0 = off, default 0 so old
  sounds are unchanged; Wobble section: Delay, Fade in). `this.wt` counts ms from each new (non-legato) note; the
  wobble's depth `wd` is 0 until wDelay, then rises to 1 over wFade. Cutoff / vowel / comb take `lsm * wd`; the level
  takes `1 − wAmp · wd · (1 − lsm)` (depth 0 = full level). The live worklet is built from BassCore, so the tracker gets
  it too; `bassViz` sets `wt` huge so the picture shows full depth. Checked by rendering in Node (level wobble swing
  per 250 ms: flat until 500 ms, then rising over the fade) and in the app (rows, saved values, no errors).
- Checked headless (Playwright, debug-hook copy): the owner's case (Enter on 03 then on an empty row → 03), clicks and a
  drag on empty / filled phrase cells (nothing added), both right-click menus, Edit ▾ in Phrase / Song / Synth, Undo
  from the menu, Esc; no console errors.

## 19 Oct: Arrange as the Song screen no longer jumps 12 ↔ 13 rows during playback

Cache `thunderplus-v172`. Owner's screen recording (desktop app): while playing, the Song screen (Arrange in the text
grid's place) flickered between 13 rows and 12 slightly taller ones, several times a second. A feedback loop in
`drawArr` (runs on every `draw()`, so every playhead step): `arrSize()` took the row height from
`arrGrid.clientHeight` (`floor(h / 12) - 3`), then the end of `drawArr` snapped the grid's height to whole rows of
that row height; the next draw read the snapped height back and got a row height 1 px different, so it flipped
(e.g. 21 px × 13 rows = 302 px → 22 px × 12 = 291 px → 21 px × 13 …). Now both use `arrAvail()` (the panel's height
below the headers, which doesn't depend on the grid), so the row height and row count are fixed for a window size.
Checked headless at window heights 560–895 px: before, 31 of 68 heights flipped every draw; now none.
Note: desktop v1.2.2 was built from `bc8d6ed`, before "Arrange at the left" (`5c68098`): it needs a new release
to get both.

## 18 Oct (late night, 4): the CPU meter measures the whole audio thread

Cache `thunderplus-v171`. Owner: no load on the CPU meter. Without `renderCapacity` (not in Chrome, desktop or Android)
the meter only summed the app's own worklet timings (track FX, …chains, live voices, mastering), so a song of pads read
0% (the safety limiter's worklet doesn't even report: meters off). Now `audio()` starts `cpuProbe(ctx)` when there is
no renderCapacity: `thunder-cpu`, a silent AudioWorklet node on the destination that logs `Date.now()` per quantum and
every 0.5 s posts the busy share (mean in-burst gap ÷ quantum length; mean gap ÷ quantum when it can't keep up; nothing
when callbacks are one quantum long, so there's no burst to see). `cpuMeter` / `globalCpuValue` use renderCapacity,
else the probe (fresh within 1.5 s), else the old worklet sum; the tooltip says which. Checked headless with a
standalone harness (`scratchpad/probe/`): 0/17/30/53/85% as oscillator+convolver chains were added, >100% when
overloaded; in the app a plain song reads 12–14%. **Not seen on the Fold yet**: if it shows 0% or `--` there, the
device is probably asking for one quantum per callback.

## 18 Oct (late night, 3): fast envelopes on every engine

Cache `thunderplus-v170`. The owner wanted every engine able to move very fast.

- **Note-off release (the main one):** `relOf(V)` (ENGINE block, used by the tracker player's `v.rel` and the synth
  page's `releaseNote`) was the longest ADSR *layer* release with a 30 ms floor, for every voice: an engine voice
  faded over its hidden layers' release (often 150 ms) whatever its own Release said. Now: Bass, Voice and every
  engine with a `rel` param use their own (Piano/EP Dampers; Chip frames × 1000/60), floor 1 ms; Layers use the
  Voice amp's release when it is on as ADSR, else the longest ADSR layer release, floor 1 ms (30 ms with no ADSR
  layer). Engines without a release (Tone, Modal, FM Drum, Skin, Blip, SoundFont) keep the old rule exactly.
- **Floors lowered** (slider `min` and the matching `Math.max` in each core): amp Decay and Release 1 ms on Bass,
  Voice (release), Acid (Amp decay 20 → 1, release 2 → 1), Machine, Swarm, Drift, Organ (release), Table, Breath,
  Phase, Vector, Scanned, Resynth, Bytebeat, Strings/Wind/Partials (release); Wind attack 2 → 0.5 ms; Piano/EP
  Dampers 20 → 2 ms; Tone decays and FM Drum decays 1 ms; Blip decay 1 ms; Modal decay 20 → 5 ms; Skin 20 → 3 ms;
  Strings/Partials decay 0.05 → 0.01 s; Chip decay and release 0 frames (at once); SoundFont attack/release × 0.01.
  Attacks were already 0.1–0.5 ms (Acid's is a fixed 2 ms). Saved values are absolute, so songs sound the same;
  only slider positions shift a little.
- Checked headless: every engine renders at its minimums with no NaN and the expected short tail; `relOf` returns
  each engine's own release (`scratchpad/t_env.py`).

## 18 Oct (late night, 2): voice clarity, lyrics box focus

Cache `thunderplus-v169`.

- **Lyrics box kept focus** (owner: changing a voice setting kept jumping back up to the text box): a slider touched on
  a phone doesn't take focus (`touch-action: pan-y`), so the textarea kept it, the keyboard stayed up and the page
  scrolled back to it on every change. A capture `pointerdown` next to `typing()` blurs a focused text field when a
  range input is pressed. Global, but only for sliders (taps on anything else already moved focus).
- **VoxCore clarity** (owner: breathy, bursts of noise). Measured each consonant before AA against the vowel
  (`scratchpad/vox/phon.js`): S was −1 dB, Z −5, T burst −6, aspiration −12, all 6–10 dB louder than speech. Now:
  per-phoneme `fa` lowered, new `ba` (burst level) per stop, aspiration `ah` 0.42 → 0.26, V/DH voicing 0.55 → 0.3 and
  Z/ZH 0.45 → 0.35 (results: S −10, SH −8, Z −11, F −21, TH −23, V −14, DH −15, bursts −13…−18, asp −15…−17,
  HH −14). Formant smoothing by segment: 10 ms consonants, 16 ms sonorants, 24 ms into vowels (was 12 ms for all),
  so CV transitions carry place. Nasals (M N NG) get a murmur: the output crossfades to a two-pole 650 Hz low-pass
  (`K.nz`, `this.nl/nl2`). The vowel itself is bit-identical to before (held [AA] measures the same). New preset
  **Clear singer** (index 1; presets are only referenced by index at click time). Consonant noise (`hiss`) still
  gives up to +6 dB for anyone who wants the old hiss.

## 18 Oct (late night): Arrange at the left, OPTION+arrows on Song, SONG tab, background play

Cache `thunderplus-v168`. The owner, playing on the Fold, found SHIFT+◀ into Arrange confusing (Arrange sat at the right,
so ◀ moved the keys rightwards), OPTION+◀▶ on Song moving the cursor up/down, and songs cutting off when switching apps.
Each part has a setting that gives the old behaviour.

- **ARRANGE SIDE** (Settings → Screen → Layout & panels; `thunderplus-arrleft-v1`, LEFT by default): `body.arrleft`
  puts `.tbody > .tarr` first with CSS `order:-1` (DOM untouched, so `arrPlace` and the Song screen are unaffected) and
  moves the grip to its right edge; the grip's drag sign flips (`arrLeft` in the `tArrH` pointermove). `setArrLeft()`
  beside `arrW`. Untick for the old right-hand layout.
- **OPTION+arrows on Song (PAD mode)**: `optDir` calls `songGridDir(dir)` when `grid && patNavOn`: ◀▶ to the nearest
  filled block on the row (master column included where shown; not in Arrange's side panel, which has none), ▲▼ to
  the nearest filled block in the column; toast at an edge; selection untouched (as `songJump` did). Arrange holding
  the keys on Phrase runs the same code (tv = 'song'). PATTERN NAV off = the old keys (◀▶ `songJump`, ▲▼ 16 rows);
  the hint follows (`songHintOld`). Lost with it on: the 16-row page jump (owner OK'd; ‹ › and held arrows remain).
  Plain arrows, EDIT/SHIFT combos and KEYS mode unchanged.
- **Arrange focus look**: the phrase grid fades to .45 (`body.apf .tscr .tgrid`), Arrange's outline and the strip's
  border are lavender (#9a63ff) instead of ink, and the **SONG tab** (and SONG in the side panel's map) lights instead
  of PHRASE (`apAfterDraw`). **SONG tab tap** on Phrase with Arrange beside it (`apTabSong`, capture listener on
  `#tTabs`/`#tMap`): first tap gives Arrange the keys, a second opens the Song screen; elsewhere it opens Song as before.
- **BACKGROUND PLAY** (Settings → App; `thunderplus-bgplay-v1`, on): block `BACKGROUND PLAY` after `tStop()`, hooks
  `bgPlay(true)` at the end of `tStart()` and `bgPlay(false)` in `tStop()`. Media Session metadata + play/pause/stop
  handlers everywhere; on Android a hidden looping `<audio>` of an 8 s, ±1-bit, 8 kHz clip (made in `bgClip()`), because
  Chrome only gives the media notification (and its foreground service) to media elements, not Web Audio. **Not tried
  on the Fold yet**: check the notification appears, the song survives switching apps, and whether another app taking
  audio focus calls our pause handler (it would stop the song; if that's unwanted, drop the 'pause' handler).
- Tests: `scratchpad/t_song.py` (headless, 1440×900, 707×823, 373×830, all pass). The app is one strict IIFE, so tests
  serve a copy with `window.__T = c => eval(c);` added before its closing `})();` (`scratchpad/mksrv.sh`) and drive it
  with `page.evaluate('c => __T(c)', code)`. Remove `.welcome` first on phone sizes.

## 18 Oct (night): whole-app review fixes (v1.2.2)

Cache `thunderplus-v167`. A fresh-eyes review of the whole app (MIDI excluded, done before) found these; each was
triggered headless before the fix and checked after (session scratchpad scripts).

- **Kit replace is undoable, and warns:** `setKit(k, noUndo)` pushes a whole-kit undo step (`{k:'K', j}`, samples by id,
  audio kept in `smpHeld`); `undoStep` handles `'K'`. Earlier sound edits ('V') stay in the history (`kitUndoBase()`
  re-baselines the pads instead of `kitUndoReset()` dropping them). `kitReplaceOk(next, title)` asks before Load kit /
  Open project when the current kit has samples the new one lacks. `smpTidy` keeps every id in `smpHeld` and the live
  song IRs, so nothing replaced this session is deleted from IndexedDB; a minute timer lets go of `smpHeld` audio that
  no pad, undo step, preset or IR names (memory on long recording sessions).
- **Factory kit** is now **Factory drums (pads 1–8)** (`factoryPads()`): the synth slots stay, like the classic kits.
  "Export all 8 pads" labels say "all pads".
- **Short kits:** `setKit` grows the kit to `nslots()` (voice per track at 24 tracks with an older 16-voice kit threw
  in `ensure` and killed PLAY). `ensure()` returns null for a missing voice; `buffer()` too. `pump()` is now a
  try/catch around `pumpRun()`: an error stops the transport with a toast instead of repeating every 25 ms.
- **TRACKS 25–32** survive a reload: `normSong` clamps to `vtrk ? 24 : NT`; turning voice-per-track on clamps to 24.
- **Master column mute bits:** `1 << 32 === 1` hit track 1. `blockMuted` is false for `t >= MT`; `setCell` and `rowOp`
  skip the bit for the MST column.
- **Dialogs:** `askSheet`'s key handler stops every key (Space started playback, letters typed notes behind it).
  `typing()` matches every text-like input (number, search, untyped), not only `type=text`.
- **Saving on hide/close:** `flushKit()` (in `pagehide`, `visibilitychange` and `applyUpdate`) saves a pending kit
  edit at once and first asks the …waves frame to save its own (`window.__wavesFlush` in `waves/index.html`, outside
  the synced SHARED EDITOR block; the frame also saves on its own `visibilitychange`).
- **Focus:** a pointer click on a button lets go of focus (not in dialogs, menus or sheets), so Enter/Space don't
  press it again on top of the tracker action.
- **Cover screen (373 px):** tighter top bar at ≤ 400 px (gap 5, edges 8, smaller CPU meter) so it stays one row; the
  meter hides below 340 px. **PROJECT/INST/FX** labels pad to the longest label on the screen.
- **Phrase loops** past ROWS are clamped to the phrase's last row (`loopFor`).
- **README:** Thunder's data is removed after import (not "never changed"); 16 slots not 8 pads; TIC is in; GRV is
  per track; up to 32 tracks (24 with a voice per track); the Chain screen is called Sect.
- **Lockfile (done the same evening):** `.github/workflows/lockfile.yml` (Actions → Desktop lockfile → Run workflow) makes
  `electron/package-lock.json` on GitHub and commits it; `desktop.yml` installs with `npm ci` when it exists. Test build
  on Windows, Mac and Linux passed with it. After changing a version in `electron/package.json`, run that workflow.

## 18 Oct (night): MIDI review fixes (v1.2.1)

Cache `thunderplus-v166`. A fresh-eyes code review of the two MIDI merges found these; all fixed and covered by the session's
headless checks (poly chord + ARP, ARP then VOL/KIL/PIT, PBN + RET, chord + PBN, microtuning, mute mid-play, clock toggles
mid-play, clock-in Continue twice, HOP loop in a block).

- **Poly + ARP:** the 4-note limit counted ARP segments, so a chord with ARP chopped notes to 1 ms. Held entries carry a
  note id (`nid`, from `moPlay`); the limit and the same-key release work per note (`offNote`).
- **ARP return vs. commands:** `moMod` dropped the waiting return on *any* command row (VOL left the note on the arp
  pitch). Now only a retune (PIT/FIN, ARP, PBN, PVB) takes it over; VOL/PAN/KIL/OFF play it first (`pendFlush`).
  Returns are flushed from `pump()` at each row's own time (not the next beat event), so they're never booked late.
  `moMod` + ARP keeps the held note as the first step instead of retriggering it.
- **Bend dedupe by time:** `mx.bend[k] = {v, t}`; a value only skips when it repeats the latest *in time* (PBN + RET
  used to leave the channel bent for every later note). Chord notes share one PBN/PVB ramp per channel and row
  (`mx.ramp`, `rampOnce`). Microtuning (`S.tune`) now reaches gear as bend (`mn` carries the cents).
- **Muted track:** its held note ends at the next row, no bends/returns go out (`moEv` note/mod branches).
- **Clock toggles while playing** (`clkPortToggled`, `clkJoin`): a port switched off gets Stop (after its booked
  ticks); one switched on gets SPP + Continue and joins the ticks; the first port lit mid-song starts the clock from
  the current position. Changing a port's cable mid-play: Stop to the old cable, SPP + Continue to the new. STOP's Stop
  reaches ports that were running even if none is lit any more. A lit letter with no cable shows "X: no cable".
- **Clock in:** the Song Position Pointer counts once (a Continue without a new one starts from the top); Start and
  Continue both play the *song* (not the open phrase).
- **Song position lookup:** bounded (stops once every row of the block is reached, or 1024 beats / 20000 steps), in
  try/catch, so a HOP loop can't stall PLAY.
- Instrument list reads "MIDI A ch 01".

## 18 Oct (evening): MIDI phase 2 — LFO → CC, Song Position Pointer

Cache `thunderplus-v165`. Same MIDI block, after `moStart`.

- **LFO → CC:** instrument fields `lcc` (CC number, −1 off), `lsh` (`LFO_SHAPES` index), `lrb` (rate **in beats**, one of
  `LFO_RATES`' values; `lfoRateI` finds its row, so the list can change without breaking songs), `ldp`, `lct`, `lrg`
  (0 FREE, 1 NOTE). `moLfo(s)` runs from `pump()` after `moClock`: books ~80 ms ahead like the clock, a value every
  `LFO_DT` (10 ms) only when it changes (`mx.lfo[ii].v`), amplitude `dep * 64 / 127` around the centre. FREE phase =
  the song's beats from PLAY (`mx.beat`); NOTE = beats since the instrument's last note (`lfoTrig` from `moNote`, a
  short list so notes booked ahead don't restart it early). RANDOM is a hash of the cycle (and the note's time for
  NOTE). INST shows LFO CC for MIDI instruments and the other five rows once it has a CC.
- **SPP out:** `moStart(row)` (called in `tStart` before the start time is set) → `mx.spp = sppFor(row)` when a clock
  port is lit and the song starts below the top of its block (`songBlockTop`: rows with anything in them, upward).
  The beat 0 event sends `F2` (50 ms early) + `FB` instead of `FA`. Positions come from `songRowBeats(r0)`: `makeSeq`
  run with `{once:true, sim:true}` (`sim` skips chop renders) noting the beat each row is first reached, so phrase
  lengths, LPB, HOP and TIC all count. Cached per block (`blockBeats`, `rbCache`), dropped when `songVer` changes
  (`saveS()` bumps it) or `S` is a new object; with clock out or in on, `songBeatsSoon()` works the cursor's block
  out 1.5 s after an edit (and 4 s after start-up), so PLAY doesn't wait (128 rows × 16 tracks: ~100 ms headless).
- **SPP in:** `onMidi` keeps `F2` in `mx.inSpp` (16ths); `clkIn` on Continue with a position plays `tStart('song',
  {row})` from `songRowAt(P)` (the row at or before P in the cursor's block) and puts the remainder in `mx.inOff`, which
  the phase lock adds to the other device's beat. Start clears both.
- **Checked headless** (session scratchpad, fake Web MIDI): SPP 0 / 16 / 24 for a 16-, 8-, 16-row song; position →
  row; SPP 24 + Continue from row 2, Start from row 0 and from a phrase; clock in SPP 20 + Continue plays row 1 with
  1 beat to catch up, Stop stops, Start clears it; LFO sine 0–127 a cycle every 500 ms at 1/4, ~10 ms apart, changes
  only; square two values; depth 0 one message; random new values per cycle; NOTE restart drops to the bottom at each
  note; off / pad instruments send nothing; old songs load with the LFO off; phase 1 tests still pass. **Not tried with
  real gear yet.**

## 18 Oct: MIDI control — ports A–D, CC slots, PGM, pitch bend, ARP as notes

Cache `thunderplus-v164`. All in the `/* ---------- MIDI out and MIDI clock ---------- */` block unless named. Phase 1 of
the owner's MIDI plan (phase 2, the LFO → CC and Song Position Pointer, is the entry above).

- **Ports:** `NPORT = 4`, `PORTL = 'ABCD'`. Device settings (`thunderplus-midi-v1`): `midi.outIds[4]` (Web MIDI port
  ids) and `midi.clk[4]` (clock per port); the old `outId`/`clkOut` load as port A and are still written (= port A) for
  an older copy of the app. Songs: `I.mpt` (0–3), so a song names a letter and each device maps it to its own cable.
  `midiOut(p)`, `msend(bytes, t, early, p)`, `msendClk` (each lit port's cable once: two letters can name the same
  cable). Settings → MIDI: `#midiOutSel0–3` + `#segClkOut` (four toggles).
- **CC slots:** `CC_CMDS = ['CCA'..'CCD']`, `CC_DEF = [74, 71, 1, 7]`, `NCC`, `ccNum(I, k)`, `CC_NAMES` (labels on
  INST). Songs: `I.cc` (one CC number per slot; fewer saved slots fill from `CC_DEF`). **To add slots:** append to
  `CC_CMDS` and `CC_DEF`, and append the same names at the **end** of `FXC` (never in the middle: saved songs store
  command numbers). INST rows, help strings and `makeSeq` all loop over `CC_CMDS`.
- **Commands** (end of `FXC`): `CCA`–`CCD` (value > 7F sends 7F; RND step 8), `PGM` (00–7F). `makeSeq` pushes
  `{type:'mcc', ii, k, v}` / `{type:'mpg', ii, v}` ahead of the row's notes (only when the row plays: CHA/NTH, muted
  blocks drop them), instrument = INS / the I column / the track's last. `MIDI_ONLY_EV`: `moEv` sends them (`moCmd`,
  PGM 1 ms before the note) and returns true; the player's `play()` ignores them, so renders never sound them.
- **Pitch bend:** `I.pbr` (BEND RANGE, ±1–48, default 2). `bendTo` (dedupes against `mx.bend`, the last 14-bit value per
  port×channel, so gear never sees bends unless a song uses them), `bendRamp` (~10 ms steps, ≤ 48), `bendVib` (PVB at the
  pad's speed/depth). `moNote`: starts at the note's fraction of a semitone (FIN, microtuned PIT) or, with PSL, at the
  track's last pitch (clamped to the range), then glides; PBN/PVB after. `moMod` (rows with no note): PIT/FIN retune the
  held note, PBN bends on from where it is, PVB, ARP. STOP sends bend centre on bent channels.
- **ARP as notes:** `arpSegs` (a note a step, each to the next). The return to the note's own pitch after the steps sits in
  `mx.pend[track]` until the sequencer is past it (`pendAt` from `moOff`/`moNote`/`moMod`, flushed on the next step's
  `beat` event): a note, OFF or command on that row cancels it, so no 1 ms blip at the next note.
- **Held notes** carry `p`, `base`, `vel`, `rng`, `fine`; `moPlay(t, p, ch, n, vel, rng, fine, segs, end)` sounds and books.
- **Checked headless** (session scratchpad scripts, not in the repo: a fake Web MIDI with two cables, and screenshots at 707, 373 and
  1440 wide): old settings → port A with clock; old songs load with port A / ±2 / default CCs; new fields round-trip;
  PGM then CC then note on A ch 1; CC on a note-less row; CCC FF → 7F; instrument 01 on port B ch 3; PBN ramps to the
  right value and ends at the step's end; PIT on a held note; PSL starts bent and returns to centre; PVB both ways; FIN
  50 → C#4 bent down a half; ARP steps a tick apart, no return before a next note, return and hold before empty rows;
  no note under 2 ms (exact audio-time run feeding `makeSeq` straight into `moEv`); clock to both cables, one Start and
  one Stop each; All Notes Off and bend centre at STOP; pad instruments send nothing. **Not tried with real gear yet.**

## 17 Oct (late night): settings sheets — Screen…, Skin…, Song…, Input…

Cache `thunderplus-v163`. Block `SETTINGS SHEETS` (JS, just before `tsetLabel`; CSS after `.pbs .pempty`), hooks marked
`SETTINGS SHEETS` in `buildProj` and `buildScreen`.

- **What:** the long Settings lists open in pop-out sheets built on the preset sheet's shell (`.pbs/.pbox/.phd/.pcat`):
  `setSheet('screen' | 'input' | 'song')` and `skinSheet()`. `SHEETS` lists each sheet's groups by row label; the rows
  are the same `projRows()` entries drawn by `tsetRow()` (the sheet's list carries class `gset` for the row styles,
  with `.ssh .plist.gset` undoing the dropdown's 400 px width). Search filters by label, checkbox text and info.
  `sheetGrp` remembers the open group per sheet. `sheetShell()` is the frame: Done / Escape / tap outside close it,
  taps in the first 350 ms are swallowed (the opening tap), keys typed in it never reach the tracker.
- **Menus now:** Screen part (gear on Mobile, View on Desktop) = `SHEET_QUICK` rows (TEXT SIZE, SIMPLE PHRASE, FOLLOW),
  any Screen row no sheet lists, then **Screen settings…** and **Skin…** (`sheetBtn`). Project = **Song & input**
  (Song settings…, Input settings…), Render & export, Mastering. Panels (keypad, bottom panel, stage) are in the
  Screen sheet's Layout & panels group on every device now. Phone menu 7,792 → 3,048 px tall; desktop View
  3,535 → 570 px.
- **Skin sheet:** groups (`SKIN_GROUPS` + the other Simple skins split light/dark by background luminance), each skin a
  tiny tracker in its `dots` colours (`skinMock`: text = the most contrasting dot). A tap applies it live; Done keeps it,
  **Back to …** restores the skin the sheet opened with. The old `#skins` chips stay built (hidden) for `applySkin`.
- Sliders in sheets are locked like the menu's: the lock's listeners are `rlWire(node)` now (menu + each sheet,
  class `rlock`); `rlRanges()` covers both. `buildProj()` calls `sheetRefresh` so the Song sheet's IR list updates.
- **Back to the old menus:** `SET_SHEETS = false` (one line). Every row is back where it was.
- Checked headless at 373, 707 and 1440 wide: each sheet opens, groups, search, Simple phrase toggled from the sheet,
  tempo +, Groove and IR groups, Input rows, skin tap / Back / Done, Escape clears search then closes, Simple night skin.

## 17 Oct (night): Flex fixed (clicks and buzz), simple phrase screen

Cache `thunderplus-v162`, `chains-worklet.js?v=27`, `waves-worklet.js?v=23`.

- **Flex clicked and sounded distorted.** Cause: the beat clock is a float32 AudioParam, and `clkSpb` re-measured the
  tempo from 16 samples of it every block, so samples-per-beat wobbled by up to ~0.3% block to block. A replayed beat
  sits up to 4 beats back, so its read head was shoved 10–200 samples every 128 (hard jumps under 8 samples clicked,
  bigger ones crossfaded every block and buzzed). Measured on a steady sine: REPEAT 1/4 jumped ~99 times a bar
  instead of 4 (287 at 90 BPM / 44.1k), glitches around −35 dB.
- **Fix** (in `index.html`, copied identically into `chains/chains-worklet.js` and synced to …waves):
  - `clkSpb` measures the slope over a run of blocks (used from 4096 samples, restarted every ~32768, unwrapping the
    16-beat wrap); a tempo change > 2% snaps, but only when two blocks running agree (a half-stalled block can't fake
    one); stopped/stalled/jumping clocks keep the last good value. RetrigCore uses it too (only at engage).
  - `FlexCore` keeps its own float64 song position `ph` (advanced a sample at a time, locked to the clock each block:
    gently within 0.004 beat, at once beyond) and a glided samples-per-beat `sp` (100 ms), so the read head is smooth.
  - `FlexCore.rd` is 4-point Hermite (linear within 2 samples of now, where the far point isn't written yet): half
    speed, tape stop, backspin and scratch were gritty on cymbals with linear reads.
  - Result, real Chromium OfflineAudioContext with the player's ramped ConstantSource clock (dry baseline −124 dB):
    REPEAT 1/4 −52 → −115 dB, HALF SPEED −47 → −81 dB, REVERSE BEATS −43 → −87 dB, REPEAT 1/4 @ 93 BPM −45 → −108 dB;
    exactly the intended jumps per bar. Also checked: tempo snaps and glides, stop/restart, a stalled clock.
- **Simple phrase screen** (Settings → Screen → SIMPLE PHRASE, "Show simple phrase screen", key
  `thunderplus-simplephrase-v1`, **off** by default). On Phrase only: no Arrange (`body.simpleph .tarr` hidden), no
  song strip (`apStripOk`), no stage piano (`stageWant()`), no Pattern view (`pvRoom`), and `fit()` sizes the text to
  the phrase's own width (`spCols()`) with only 12 lines (LEN, header, 10 rows) required, so it grows (cover screen
  9.3 → 16.3 px, desktop 20.6 → 27.5 px) and the rest scroll with the cursor. `spSync()` in `draw()` toggles the body
  class and re-fits on entering or leaving Phrase. All hooks are marked `SIMPLE PHRASE`. To take it out: delete the
  block after `arrBody` and the marked lines.

## 17 Oct: acoustic kits and Funk breaks

- **What:** three clean acoustic kits (38 more pad voices) and twelve original funk-style breaks you can load as
  patterns. The kits picker is now **File ▾ → Drum kits…** (and Settings → Kit → **Drum kits…**). It lists the
  classic machines first, then **Funk kit**, **Studio kit** and **Jazz kit**. **File ▾ → Funk breaks…** loads a break
  as a new song (its tempo, swing and acoustic kit on pads 1–8; Undo brings the old song back), or adds its four bars
  to the current song two rows after the last content row.
- **Kits:** plain Layers voices like the classic ones. The kick, snare and tom bodies are membrane and sine layers,
  with strong filtered-noise beater and stick layers on top. Cymbals are `metal` plus noise. Every kit has a quieter
  **Snare (ghost)** voice, softer and shorter, for ghost notes. Funk: 12 voices (dry, tight, deadened). Studio: 13
  (bigger and brighter, mid tom, ride bell). Jazz: 13 (tuned-up small kit, ride, brush sweep and brush tap). They were
  tuned against measurements of acoustic hits in drum machine ROMs (R-8, SR-16, LinnDrum, from the same
  `tidal-drum-machines` set, measured only). Peak targets match the classic kits (kicks −1 dB, toms −2, hits −3,
  hats and cymbals −7), with ghosts and brushes at −6. Each peak is the loudest of six renders, because the noise
  seed moves a peak by up to 3 dB.
- **Code renames:** `x0xMachines()` → `drumKitGroups()` (now classic and acoustic groups), `x0xKits()` →
  `drumKits()`, `x0xTag` → `kitTag`. `kitTag` strips "-style…" or " kit (…)" for the short names. In the pad Presets
  list the classic groups come first, then Drift (tape pads), then the three acoustic groups, so existing Recent
  picks keep their keys. Kit pad order for the acoustic kits: 1 kick, 2 snare, 3 ghost snare, 4 closed hat, 5 open
  hat, 6 floor tom, 7 high tom, 8 crash (the Jazz kit has a ride on 8).
- **Breaks:** `FUNK_BREAKS` (right before the `engGroup` comment in `index.html`) holds 12 breaks:
  `[name, BPM, swing, kit group, feel, bars]`. Each bar is a set of 16-step strings: K kick, S snare (f = flam),
  G ghost, H hats (o = open), T toms, C crash. They are written for this app in the classic vocabulary
  (syncopated kicks, ghosts around the backbeat, open-hat lifts, a fill or variation in bar 4) and not transcribed
  from records. `feel` is how late each part sits, in ms: snare, ghosts, hats 0…n, kick 0…n. A fixed hash nudges
  each hit, so a break plays the same every time.
  - `breakParts(i, bpm, ticks, lpb)` turns a break into hits per track: `[step, pad, volume, DEL ticks]`. Feel is
    written as DEL, so break songs run at 24 ticks a step. Swing is the song's own (`swing`, 16ths).
  - `placeBreak(s, i, y)` writes one sect per track at song row y and a phrase per bar. Identical bars share a
    phrase. It uses only free sects and phrases (not referenced by the song, not used by a used sect, not empty),
    picks the instruments that play pads 1–8, and returns false if there isn't room.
  - Tracks: 1 kick, 2 snare, 3 ghosts and flam graces, 4 hats (closed chokes open through choke group 1), 5 toms,
    6 crash.
  - `breakSong(i)` builds the new song. It puts a light record chain on the master (comp press 80, small room, tape
    with less head bump), and mixes the kick track 2 dB down with the snare and hats a little up.
  - `breakRow()` and `pickBreak()` are the menu flow.
- **WAV loops:** the same twelve breaks were rendered through the app's own renderer (a headless page calling
  `makeSeq` + `pass`, with the master FX) as 4-bar 44.1 kHz 16-bit loops normalised to −1 dBFS and sent to the owner.
  They are not in the repo. Each loop is the middle pass of a three-pass render, so it starts with the previous
  pass's tails, and its first 256 samples crossfade in from the third pass, so the loop point is seamless.
- **Not heard by ear yet** (headless only). The brush sweep and the jazz ride are the loosest sounds.
- **Cache:** `thunderplus-v161`.

## 17 Oct: Drift engine (tape pads, for Boards of Canada-style pads)

- **What:** a rendered synth-slot engine, `drift` (`class DriftCore` in the ENGINE block, right after `SwarmCore`;
  `XENG.drift`, `renderX`, `ENGINE_GROUPS` → Synths, the engine segment list, `XUI.drift` with 13 presets). Its
  presets are also a pad Presets group, "Drift (tape pads)", added last through the new `engGroup()` helper (the
  engine groups' mapping, which was inline in `factoryVoicePresets`, now uses it too; same output).
- **Design:** laid out like the big 1970s Yamaha CS-series polysynths (the owner's reference for those pads): two
  oscillator lines per voice (I: saw, pulse; II: saw, pulse, an interval and a detune; shared PWM; sub, noise, ring mod
  of I's saw by II's pulse), then a 12 dB high-pass and a 12 dB low-pass (TPT state-variable) with a filter ADSR, the
  amp ADSR, delayed vibrato, 1–4 unison voices each with its own slow pitch and cutoff drift. Then the tape stage, which
  is where most of the sound is: wow (a 0.05–3 Hz sine plus a slower random sway, up to ±35 cents) and flutter
  (7.3 + 11.9 Hz plus jitter, up to ±9 cents) as pitch modulation shared by every voice, tanh tape saturation, a
  BBD-style chorus (8 ms ± 2.5 ms, two LFOs), Age (two one-pole low-passes from 18 kHz down to ~2.6 kHz, a low cut up
  to 135 Hz, and occasional 40–120 ms dropouts) and Hiss (−62 to −28 dB, following the note). Seeded, normalised to
  0.8 then Level/Drive as Swarm. Original code, generic name; the CS series is credited as the layout's inspiration.
- **Checked** headless: every preset renders (no NaN), the Worker render is identical to the page's, the card builds
  and presets apply; wow/flutter rates and depths measured from the output's pitch. Not heard by ear yet. A 6 s pad
  takes ~0.6 s to render in Node, so a few seconds on the phone (in the Worker).
- **Cache:** `thunderplus-v160`.

## 17 Oct: classic drum machine kits (CR-78, 808, 606, 909, 707, 727, 505)

- **What:** 97 new pad voices, one Presets group per machine ("808-style drums" and so on, appended after the other
  groups so saved Recent picks keep their keys), and **File ▾ → Drum kits…** / **Settings → Kit → Drum kits…** (named
  Classic drum kits… until the acoustic kits joined), which puts eight of one machine's sounds on pads 1–8 (synth slots and any extra drum pads untouched; each
  pad is its own undo step). Kit pads follow the factory kit's order (kick, snare, clap, closed hat, open hat, tom,
  percussion, extra) so a pattern plays sensibly on any kit; hats keep choke group 1 (the CR-78 has none).
- **Code:** one block before `factoryVoicePresets()` in `index.html`: `drumKitGroups()` (the voices; was
  `x0xMachines`), `drumKits()` (the eight per kit; was `x0xKits`), `kitTag`, `loadClassicKit(i)`, `pickClassicKit()`. All plain Layers voices — no new DSP, no samples.
  Helpers inside: `hz(f, vt)` turns Hz into tune/fine (voices above C6 sit on a voice tuned +12/+24 so layers stay in
  ±48), `cut(hold, dec)` is the voice amp used as a gate (808/606 open hats, sampled tails), `clap(...)` builds a clap,
  `grain()` is 8-bit Degrade plus a voice low-pass on the 707/505 (sample-ROM) voices. `D(name, layers, opts, level,
  drive)`: level puts each pad's loudest peak at the factory kit's (kicks −1 dB, toms −2, hits −3, hats/cymbals −7);
  drive is only used as extra gain on the quiet filtered-noise pads (claps, hats, cymbals).
- **How they were tuned:** rendered offline in Node with the app's own ENGINE block and measured against recordings
  of the real machines (pitch and pitch sweep from zero crossings, decay in dB at set times, spectrum bands and peaks).
  Tools in `tools/drums/` (`render.mjs` renders and range-checks, `compare.py` prints reference vs ours); the reference
  recordings come from the `ritchse/tidal-drum-machines` collection and are only ever measured — not in the repo, not
  in the app. Headline numbers: 808 kick 50 Hz from a ~85 Hz start, snare 172 + 338 Hz, toms 89/133/181, congas
  201/281/412, clave 2.42 kHz, cowbell 557 + 824 Hz; 909 kick sweeps ~250 → 56 Hz (layer env 40 st / 38 ms with the
  voice env −14 st / 15 ms, which gives the 909's slow-then-fast curve), snare tone 345 → 212 Hz; 606 kick 55 Hz.
- **Names:** model numbers only, as style descriptors ("808-style", "909 Kick"), like the existing "(808-ish)"
  presets; no "Roland", "TR-" or "CompuRhythm". Before a commercial release, decide whether to keep the numbers.
- **Not heard by ear yet** (headless only): worth a listen on the Fold, especially the 727 star chime and quijada,
  the CR-78 guiro and the 909 ride, which are the loosest matches.
- **Cache:** `thunderplus-v159`.

## 16 Oct (late night): the Arrange mockups built — and how to take each back out

The owner approved the "not built yet" boards on the Arrange design canvas and asked for them with an easy way back.
Three commits, each one `git revert`-able on its own (checked: each reverts cleanly with the others in place), and
every feature behind its own Settings row (on by default unless noted). Unticking a setting gives exactly the old
behaviour. Paging and Pattern view were built in parallel by helper agents on worktree branches and cherry-picked.

1. **Song screen extras** — `b40576d`. Settings rows right after CLASSIC SONG: SONG MOUSE (`thunderplus-songmouse-v1`),
   SECTIONS (`thunderplus-songsecs-v1`), LOOP DRAG (`thunderplus-loopdrag-v1`), SONG PEEK (`thunderplus-songpeek-v1`,
   **off** by default: the dogs stay). Code: the JS block `SONG EXTRAS` right after the `arrGrid` handlers, the CSS block
   of the same name after `.tarr .tagrid b.sl`, and one-line hooks marked `SONG EXTRAS` (arrGrid pointerdown → `sxDown`,
   end of `drawArr` → `sxAfterArr`, `stageKind` / `stageLayout` / `stageCanvas` / draw's peek refresh, the `#tStagePk` /
   `#tStageSw` markup, `startScroll`'s skip list, `freshSong` / `normSong`'s `secs`). `info()` is wrapped inside the block.
   - Mouse (mouse/pen only; touch unchanged): `sxDown` starts a drag record; past 6 px `sxStart` picks *select* (from an
     empty cell: `selA` + cursor, so every Song command sees the selection) or *move* (from a block or the selection);
     `sxAim` / `sxShowDrop` outline the drop (`b.dt`) with a tag; `sxApply` moves or copies filled blocks and their mute
     bits (empty cells don't wipe), via `saveS()` so undo has it. Alt+press defers the old Alt-click mute to release.
     Right-click → `sxMenu` (a small popup, arrow keys + Esc) reusing `copyHere`, `paste`, `cloneHere`, `cloneDown`.
   - Sections: `S.secs = [{y, n}]` in the song file (old versions ignore it). `songSecAt(y)`, `songSecName(y)` (Pattern view
     uses it). The lane (`.sxlane`, absolutely placed in `#tArr`, `body.sxsecs` pads Arrange) draws only the visible rows
     and needs Arrange ≥ 560 px wide (`sxLaneOn`), so not on the folded phone; the info line names the section anyway.
   - Loop drag: `sxLoopDown` on a row number defers the old tap to pointerup; a drag sets `loopSel` + `loopArm`
     (`sxLoopRows`), and `sxAfterArr` lights those row numbers (`.rn.lp`).
   - Peek: `stageKind()` returns `'peek'` on the Song screen when SONG PEEK is on; `stagePeek()` writes the rows as text.
2. **Paging into Arrange + the folded song strip** — `b10761d`. Settings after SIDE PANEL: ARRANGE FOCUS
   (`thunderplus-arrfocus-v1`), SONG STRIP (`thunderplus-songstrip-v1`). Code: a CSS `<style>` block right after the main
   stylesheet and the JS block `ARRANGE PAGING` before "switching between the synth and the tracker"; hooks: top of
   `draw()` (`apHold`), end of `draw()` (`apAfterDraw()`), first line of `arrGo()` (side panel only). The block
   **wraps** `press`, `release`, `dirPress`, `globalKeys`, `keysDown`, `cmdAction`, `kpAction`, `nav` and `go` (listed in
   its banner): while Arrange (or the strip) has focus they run with `tv = 'song'` and drawing held, then `tv` goes back to
   'phrase', the phrase follows the block (unless PIN) and it draws once; a handler that changes screen drops the focus.
   Esc / a phrase tap / SHIFT+▶ give the keys back. The strip shows on Phrase when no Arrange panel is visible on the phone
   or under 600 px, and shrinks the phrase text (≈9.3 px) to keep 16 rows.
3. **Pattern view** — `1ce5cc3`. Setting PATTERN VIEW after INST LIST (`thunderplus-patternview-v1`; its own UI state in
   `thunderplus-patternview-ui-v1`). A mode of the Phrase screen (PHRASE | PATTERN on the title line, ≥ 600 px wide): the
   open track's column is the real `cPhrase` editor, the others are drawn read-only and a click or an arrow past the edge
   moves to them (`pvMoveX`, `pvTab`), Ctrl+↑↓ / [ ] / ‹ › step song rows (`pvRow`). Blocks `PATTERN VIEW` (CSS and JS)
   plus one-line hooks marked `PATTERN VIEW` in `followPlay`, `move`, `nav`, `optDir`, `fit`, `draw` (×4) and `keysDown`.
   Changed on merge: SHIFT+◀ from Phrase no longer opens the pattern (it keeps its job: Arrange focus, or the Song screen);
   SHIFT+▶ from the pattern still returns to the plain phrase. Not done: playing "this pattern", other tracks' playheads,
   Enter×2 on a header to make a phrase unique; with SECT shown, other tracks show the same sect row as the open one.

Tests (Playwright, headless): `scratchpad/t2/sx1.py`, `sx2.py` (Song extras at 1440/707/373 and settings off),
`scratchpad/merged/t2_desk.py`, `t4_unfold.py`, `t5_fold.py`, `t6_off.py`, `t7_misc.py` (paging and strip, incl. a
settings-off comparison with the old build), `scratchpad/mergedB/inter.py`, `mob.py` (Pattern view). All pass on `main`.

## 16 Oct (late night): the safety limiter is a real limiter now

Measured: `mkLimiter` (a DynamicsCompressorNode at −2 dB, 20:1) let a two-note Organ chord through at 1.06 and an
eight-note one at 1.33, so chords still clipped, on the Synth page and in the song. `safeLim(ac, dest)` now runs the
mastering worklet's brickwall `Limiter` alone (`thunder-master` with `SAFE_LIM` = lim on, −1 dBFS, 3 ms, true-peak,
120 ms release, meters off). Until `mwReady(ac)` resolves (the first notes of a session) a stand-in carries the sound:
the old compressor into `softClip()` (a WaveShaper that can't pass −1 dBFS). Used by `rtInit` (`rt.lim`, `_drop()` on
audio reset), `padOut()` (Synth page) and `pass()` (renders: the worklet's 3 ms lookahead is trimmed with the mastering
latency; the old compressor's own 6 ms delay used to be left in, so renders started 288 samples late). After: chords of
1–8 notes on Piano, Tine EP, Organ and Strings peak at exactly 0.891 with no clipped samples. `mkLimiter` remains only
inside the stand-in and as the fallback where AudioWorklet is missing.

## 16 Oct (late night): gentler Arrange zoom on a computer

`arrDeskZ()` (not `body.mob` and not a coarse pointer): `arrZMax()` = blocks about 84 px wide at most (`84 / arrFitW`,
1.25…3; `arrFitW` is the 1× column width from `arrSize`, which also clamps a saved zoom), − / + step ×1.25, Ctrl+wheel
`exp(−dy·0.0014)` (≈1.15× a notch; `deltaMode` lines converted), zoomed row height capped at 34 (Song screen) / 40.
`arrSetZoom` keeps the song cursor's block (`b.c`) still on screen (or centres it if it was out of view) by measuring it
after `arrSize()`. Touch (pinch, AZ_STEPS) is unchanged.

## 16 Oct (late night): Synth page reset, meter, live-note limiter

- Engine ◀ ▶ (`.enggrp` around `#vEngBtn`, `engineStep(d)`): cycles `ENGINE_GROUPS` flattened, wrapping, via `setEngine`.
- `#vEngBtn` is one width for every engine (`--engw`, measured from the longest name in the current font by `engFit`,
  again on skin change). `setEngine` calls `hushPad(cur)` first: `padTrack()` records every live source per pad
  (`padSrcs`), and `hushPad` fades them in ~10 ms and clears `synHeld`/`playing`.
- **Pad effects on engines:** `renderVoice` runs `padFx()` (the same fx loop + Tail + trim as Layers, with the per-effect
  meter stages) after Blip and every non-live XENG engine (`engFx(V)`); Bass, Voice and live engines (Acid, Machine) stay
  dry because the song plays them live. `bassUI` shows `#fxWrap` for those engines (below the engine card) and the
  card's note (`#fxAfter`) says "after the engine". A pad's existing fx list now applies when it switches to such an engine.
- Desktop synth layout: `synDesk()` also moves `bassWrap/blipWrap/xWrap/voxWrap` into `#synRight` (back before
  `#layersBand` when off), so a non-Layers engine's card fills the right column and scrolls there; the middle's `.wave`
  grows (`body.sdesk:has(#synRight > :is(...):not([hidden])) .wave`, ResizeObserver redraws it). A right column with
  nothing visible folds away (`grid-template-columns:… 0`), as a fallback.
- `makeRow()` sliders reset to `def.d` on double-click of the slider itself (not only the label); mod-sequence steps
  reset to 0.
- **Live notes bus:** `playNote`, `holdNote` and `play` now go `g → padOut()` instead of straight to `master`. `padOut()`
  builds (per audio context) `in → mkLimiter → master` (or straight to master when `S.limiter` is off, `padRoute()`), plus
  an analyser on `in`. Cause of the distortion: every rendered note is normalised to 0.8 peak (`KeysUtil.finish` and the
  others), so three or more keys summed past 1.0 and clipped at the output; the song path already had the limiter.
- **Meter:** `#vMeter` in the pad header (`.vmeter`, −48…+6 dB, 0 dB line, red past 0 dB before the limiter), drawn by
  `synMeterFrame()` only while something sounds (`synMeterOn()` from the three play functions).

## 16 Oct (late night): …chains/…waves mixers, Alt-drag copies, jack meters, module list and colours

- **Mixers:** `mixer` (both editors) and `smixer` (…chains) have **Inputs** (`chans`, 1–16, default 4 so old patches
  load unchanged). Defs are built by `mixDef()` in each editor and each worklet (params/inputs must stay identical:
  `chans, l1(,p1)…l16(,p16), level`). The worklet only sums `k < chans` and only patched inputs. Editor: `mixShown(m, k)`
  hides ports/sliders above Inputs; changing it runs `mixResize()` (drops cables into hidden inputs and their
  bottom-panel controls, then `rebuildNode()`). `ui.fixed` = a choice without a modulation jack and kept out of the panel.
- **Stereo mixer** (`smixer`, …chains only): pairs In N / In N R with Level and Pan each (balance: centre is unity on
  both sides), a lone side feeds both. **CHN follows the patch:** `chainsAutoOn(before)` in the app compares
  `chainsTracks()` before and after an edit (save, `param('track')`, `mod`) once things settle (`setTimeout 0`) and turns
  `S.chainOn[t]` on for any track that newly has a Track In + Chain Out, with a toast.
- **Alt/Option-drag a title bar** copies the module (or the selection) once the mouse moves (`dupForDrag` → `pasteGraph`
  at the same spot, then the drag carries the copies). `pasteGraph` now keeps every module field, and in …chains
  `pasteTracks()` moves pasted Track In / Chain Out to free tracks (a pasted pair stays a pair; the app then turns CHN on).
- **Palette drag fix:** the drag listens on `window` (pointerId-filtered) and a mouse move with no button down finishes it
  (`paletteDrag`), so a lost release can't leave the ghost stuck on the patch.
- **Jack meters (…chains):** the engine keeps per-jack peaks while telemetry is on (`_jackPeaks`, `io.pko/pki`) and
  sends `jacks:[id,'in'|'out',port,peak]`; the app merges them across track engines (max) into `api.meters({jacks})`;
  `paintJacks()` draws the thin `.pm` bar beside every port. Standalone uses the editor's own telemetry.
- **Jack meters in …waves too:** the waves engine keeps one peak record per module (`_jp`, across all its voices) in
  `_jackPeaks` while telemetry is on, and sends `jacks` the same way; the editor's `portEl`/`paintJacks` match …chains.
  The meters show the editor's own engine (what you play in …waves). `waves-worklet.js?v=22`.
- **Sidechain In:** new **Duck** output (1 − envelope: into a VCA's CV to pump), **Sensitivity** (dB, detector only) and
  **Release**; the envelope is now smoothed (2 ms attack) instead of following the waveform. Level only sets Audio out.
  The Env output was never inverted (it rises with the hit, like any envelope follower); Sidechain Comp ducks as before.
- **Module list and colours (shared block):** `PAL_LAYOUT` (in the SHARED EDITOR block, synced to …waves with
  `tools/sync-shared.py`) sets the groups, their order and a colour each; `organizePalette()` rebuilds the merged palette
  after `mergeShared()`, anything not named lands in "More". `catStyle(el, type)` gives list items, group headers and
  modules `--cat`: border + light tint (`.node.cat`), so groups read at a glance zoomed out (term skins keep their
  border colour; the tint and the list dots still show). Add a new module type to a `PAL_LAYOUT` group.
- Cache-busters: `chains-worklet.js?v=26`, `waves-worklet.js?v=21`; cache `thunderplus-v149`.

## 16 Oct (night): bottom panel parts fold instead of hiding

The owner didn't want hide-and-restore at all: INST, …CHAINS and FX now **fold** (`DK_FOLD`; `deck.inst/chn/fx` false =
folded) to a 26 px strip (`#tDeck .dks.col`, title vertical) on a title click, and a click anywhere on the strip opens
it. SCOPE and TRACK can't fold or hide (`d.scope = d.strip = true` on load; their titles get `.fixed`); TRACK keeps to
the right edge (`margin-left:auto`), so folding leaves empty space in the middle, to fill later. Gone: the hide toast,
the + buttons (`#dkOffs` stays in the markup, hidden), the bare-header case, and Settings → SHOW ALL PANEL PARTS.
This replaces the "+ buttons" item in the earlier bottom-panel note below.
Rules: a folded strip has a rule on both sides (the open part after it hides its own left rule so lines don't double);
with INST folded the last open part gets a right rule over the empty space; with INST open a strip next to TRACK drops
its right rule (TRACK has one). All in CSS after `#tDeck .dks.col` (uses `:has`).

## 16 Oct (night): Song-screen Arrange polish, …chains in/out meters and track picking, boxed sections

- **Song screen (Arrange):** one header line (`.thd` hidden; Arrange's own header reads SONG · name · rows with − ○ +);
  spacers only at the MONO | POLY break and before MST (12 px) so `arrBoxes()` can draw a box per group (`.agboxes`
  overlay, re-measured only when sizes/scroll change); the grid is sized to whole rows (`arrGrid.style.height` in
  `drawArr`) and snaps to rows, so it never ends on half a row; row-number column 30 px.
- **…chains meters were silent:** `routeTrack` disconnects `b.chn` before rewiring, which also dropped the analyser
  tap (`n.an`), so the Track In / Chain Out meters read 0 after the first rewire. Now both taps are re-made there:
  `n.anIn` (what the track sends in, from `b.inp`) and `n.an` (what comes out), 2048-sample windows so short hits
  register. The host sends `peaksIn` beside `peaks`; in the editor Track In shows IN and Chain Out shows OUT (`.iom`).
- **Picking a track that already has a chain** (Track In's list) no longer jumps to the other chain: the change is
  refused with a toast, and the list marks and disables used tracks (refreshed as it opens). Opening …chains from the
  tracker now centres and selects the chain of the track you were on (`chainFocusTry`, which retries until the frame
  has its patch and shows it again at +0.5 s and +1.3 s, since a fresh frame rebuilds its modules).
- **Bottom panel "+" buttons sometimes ignored a click:** `deckLayout()` runs on every redraw and re-appended them each
  time, so a redraw between press and release lost the click. It now moves them only when they are out of place.
- **Layer / voice / engine sections:** each `.sec` has its own 1px box with 10 px gaps (the shared hairlines left a
  section alone on its row half-boxed).
- Checked headless: Song screen at 373 / 707 / 1440 wide, two chains playing (IN/OUT move), focus on entry, the taken
  track case, six slow clicks on + FX while playing, the classic toggle, menus. Not heard on the Fold yet.

## 16 Oct (late evening): View and Audio & MIDI menus on Desktop

`#bView` and `#bAud` (class `deskmenu`, hidden on Mobile by `body.mob .deskmenu`) share the one `#gset` dropdown:
`gsetToggle(open, mode, btn)` sets `gsetMode` ('view' | 'aud' | 'set', always 'all' on Mobile), moves `#gset` into the
pressed button's `.setwrap` and `gsetSecs()` picks the sections. `PANEL_ROWS` (keypad, bottom panel, show-all, stage)
leave Project → Input for View → Panels on Desktop. Outside-tap close ignores all three buttons so one can switch to
another.

## 16 Oct (late evening): bottom panel parts, …chains colour, typing instrument numbers

- **Hidden bottom-panel parts were easy to lose:** their "+" buttons were faint dashed chips after the first title.
  Now solid `.dkoff` buttons at the right of the INST header (beside ✕ if INST is hidden too), a toast says where
  on hiding, and **Settings → › SHOW ALL PANEL PARTS** brings everything back (after BOTTOM PANEL in the rows).
- **…chains part ignored the Simple skins' colour** (kept its lavender): `#dkChn` added to the term-skin `--dkc`
  rule, `--dkp:9%` between INST and FX.
- **Typing "03" on an instrument cell wrote 00 here and 03 on the next row:** `typeHex` finished an instrument
  entry on its first digit. Now a first 0 waits for the second digit; 1–F still finish at once.

## 16 Oct (late evening): desktop synth page in three columns

`body.sdesk` (CSS by `.dock{padding-block:10px 14px}`) and `synDesk()` (beside `setView`, called from `setView`,
`applyPlatform` and on resize). On Desktop with a window ≥ 1100 px and the synth view: body becomes a grid
(top bar across; `#dock` left as a vertical list with Drums / Synths / Track pads headings from CSS `::before`;
`#main` centre; `#synRight` right). `synDesk()` moves `#fxWrap` and `#voiceWrap` into `#synRight` and back into
`#main` otherwise, and redraws the waveform when the layout flips. Pad clicks, keys and everything else are
unchanged. Checked headless at 1280, 1440 and 1920 wide, 1000 wide (falls back), and Tracker ↔ Synth switches.
To revert: `git revert` that commit (CSS block + `synDesk` + its three calls), or raise the 1100 in `synDesk()`.

## 16 Oct (late evening): Arrange is the Song screen (how to revert is below)

Commit `a4841c0` on `main`, cache `thunderplus-v142`. The owner wanted Song and Arrange to stop being two views of
the same thing, and Arrange to work on small screens too.

- **What it does:** with **Settings → Show classic Song screen** unticked (the default), the Song screen draws the
  song as Arrange's blocks, full width, at every size (cover screen included), with the amber MST column after the
  poly tracks. On every other screen Arrange goes back to its side panel as before (AUTO / ON / OFF still apply there).
- **Why nothing else changed:** the screen is still `tv === 'song'` with `cs.song` as its cursor, so keys, PAD
  buttons, the command row, hex typing, EDIT×2, CLN / CLN↓, selection, copy/paste, M mute, follow playhead and
  SHIFT+◀▶ screen changes all run the same code. Only the drawing differs. The song file format is untouched.
- **Code (all in `index.html`):**
  - `songGrid`, `asongOn()`, `setSongGrid()` beside `arrMode` (key `thunderplus-songgrid-v1`, `1` = classic).
  - `arrPlace()` (beside `arrShown`) moves `#tArr` into `#tScr` before `.tinfo`, and back to its home in `.tbody`.
    draw() calls it first; body class `asong` drives the CSS (`body.asong …` rules next to `body.arrtiny`).
  - draw()'s Song branch: when `asongOn()`, no text lines are built; it only keeps `cs.song.top` in view using
    `arrRows()` instead of `songRows()`. `fitCheck()` skips the Song screen then (it used to shrink the title to 7px).
  - `drawArr()`: the master column (`ms`), and the cursor (`c`) and selection (`sl`) are now marked in place
    (`arrMark`), so moving the cursor no longer rebuilds the panel's HTML (in the side panel too).
  - `arrSize()`: one more column for MST; row height capped so about 12 rows show; width capped (~44 px blocks)
    when STAGE isn't off, so `stageLayout()` (which now measures `#tArr` instead of the text grid) keeps the dancing
    dogs beside it on wide screens. Narrow screens give Arrange the full width and have no dogs on Song, as before.
  - Touch: `scr`'s swipe handler ignores `#tArr` (Arrange scrolls itself); Ctrl+wheel over it zooms instead of moving
    the cursor. Hint line adds "tap twice opens · hold mutes".
- **Checked headless** at 373×830 (folded, PAD), 707×823 (unfolded, PAD) and 1440×900 (desktop, KEYS): taps,
  double-tap to open, hold to mute, PAD SHIFT+◀▶, Ctrl+Shift+arrows to Mixer and back, hex typing, line select,
  copy/paste, EDIT×2 on an empty cell and on the MST column, the checkbox both ways (and kept after a reload), no
  page errors. **Not yet tried on the Fold by hand.**

**How to revert**

1. **Instantly, no code:** tick **Settings → Show classic Song screen**. The text grid and the side panel are back
   exactly as before (saved per device).
2. **Make classic the default for everyone:** in `index.html` change `localStorage.getItem(SGK) === '1'` to
   `localStorage.getItem(SGK) !== '0'` in the `songGrid` line. Arrange-as-Song then becomes the opt-in.
3. **Take it out completely:** `git revert a4841c0`, then bump the `sw.js` cache once more so phones fetch the
   reverted page. Only that commit carries code; this note can stay or go. If later commits touch the same lines,
   the conflicts are all in the places listed above.

## 16 Oct (evening): 15 new effects, 4 engines, Marbles/Tides, …chains controls in the bottom panel, song IRs

Commits on `main`: 71e94c0 (effects), dff828c (engines), c6fa6b7 (bottom panel + song IRs), then docs. Cache
`thunderplus-v141`; `chains-worklet.js?v=25`, `waves-worklet.js?v=20`. All checked headless (Node and Chromium); **none
of it has been heard by ear yet.**

### One effect, three places: shared modules as pad and track effects (MODFX)

- **The DSP lives once**, as a shared module in `chains/chains-worklet.js` (SHARED MODULES block → `tools/sync-shared.py`
  copies it to …waves). New ones: `spectral`, `stretch`, `convolve` (+ the `FFTR` real-FFT helper), `autotune`, `vowel`,
  `rotary`, `gdelay`, `reverse`, `envfilt`, `bytebeat`, `chaos`, `marbles`, `tides`; and module wrappers `flex`, `sgate`,
  `retrig`, `pdelay`, `preverb` around copies of index.html's FlexCore/StepGateCore/RetrigCore/clkSpb/PitchFxCore (keep
  the copies identical; the wrappers count beats from `restarted()` at the song tempo).
- **Pads** run the module through `WavesDSP` (the …waves engine the page loads with a script tag and the Worker imports):
  `modFxRun()` in the ENGINE block feeds the one-shot 128 samples at a time, both sides the same, and averages the output;
  a module's `latency` (Spectral: 2048) is pulled back when Mix is 100%. `rwCan` keeps such pads off a Worker that
  couldn't load …waves.
- **Track FX** run it in the `thunder-tfx` worklet through `ChainsDSP`, which chains-worklet.js now publishes on
  `globalThis` inside the audio thread too; `tfxReady()` awaits `chainsReady()` first so it is there. A module slot is
  `{key, mod, mp, bpm, on, slot}` in the chain (`tfxChain`), `modRun()` in the worklet; its transport comes from the beat
  clock (`bp`: playing while ≥ 0, tempo from `clkSpb`), its sidechain from the node's **input 1** (track FX nodes now have
  two inputs; `routeTrack` connects `scBus` to it), for the Envelope filter.
- **The tables**: `tools/modfx-spec.mjs` (names, rows, formats, groups) + `node tools/modfx.mjs` → writes `MODFX` into
  index.html between the ===MODFX=== markers, with each module's own ranges/defaults, and checks every param has a row.
  At load, `MODFX` becomes `FX[k]` (pad: choices are segments, menus when there are more than 6; `log` → the new `logf`
  slider curve, 4 significant figures, so small times never round to 0) and `TFX[k]` (bytes: a choice is its index, a
  small-range int itself, anything else 00–FF along its curve). `FXT_LIST`/`TAILS` include them.
- **Gotcha (again): a module param named `type` or `on`** would clobber the effect object's own `type`/`on`. `fxKey()`
  stores them as `type_`/`on_` (Convolution reverb's Type is `type_` in pad and track effect objects).
- The Synth page's **Add effect** menu is grouped (`FX_GROUP` for the old ones, `MODFX.group` for the new).
- Agents' notes worth keeping: Spectral's wet path is 43 ms late; Convolution reverb uses ~17.6 MB per instance (two 6 s
  stereo IRs for the crossfade; lower `cap` in its create() if phones struggle), its IR level `CNV_E` was lowered to .09
  so its tail sits with Plate/Spring; Extreme stretch ~5.7 MB; Pitch correct is mono, 65–1100 Hz; Rotary and Granular
  delay don't idle on silence; Feedback "100%" is 0.9 a pass in Granular delay and Reverse echo.

### Engines (ENGINE block, XENG/XUI/renderX/ENGINE_GROUPS, factory voices)

- `VectorCore` (tempo in `P.bpm` from `songBpm()`; `padBpm` re-renders a note-value wave sequence when the tempo changes),
  `ScannedCore`, `ResynthCore` (+ `SpecUtil`: FFT and base64), `BytebeatCore` (+ `BBExpr`, the safe formula compiler: never
  `eval`; limits 512 chars / 400 parts / 40 brackets), `ChaosCore` (also the layer type `chaos`, `CHAOS_OSC`/`chaosOscName`
  above TYPES). `BBExpr` and `ChaosCore` are also in the shared block (keep identical).
- Non-number engine data lives on the voice: `V.rsData` (Resynth's analysis, ≤ ~46 KB, made by `ResynthCore.analyse` on
  the page from **Load sample…** in `rsPanel()`; `demoRs()` is the built-in pluck, used by presets and on first pick) and
  `V.bbExpr` (the formula; `bbPanel()` is its box with a live error line). XUI presets for Bytebeat are
  `[name, params, extra, formula]`.
- Analysis of a 10 s sample takes ~0.5 s in Node, so several seconds on a phone, on the UI thread (could move to the
  Worker later).

### Random generator and Function generator (Marbles, Tides 2; MIT)

`dsp/mods/` (wrappers + `build.sh`, wasi-sdk 25). The 185 KB wasm is base64 in the shared block (`MODS_WASM_B64`), so
each worklet file grew ~250 KB; one instance per thread holds up to 64 modules. The page's main thread won't compile it
synchronously, so `WavesDSP.modsPrepare()` is called after `wasmInit` there. Marbles' X outputs are centred on C4
(0.40625 in the pitch-CV convention; A4 is still 0.5).

### …chains controls in the bottom panel

- **Editor** (chains/index.html, outside the shared block): right-click a control or a module (only when embedded) →
  Add / Remove … the bottom panel. The list is `doc.deck` in the patch: `{id, p, ml, l, r:[min,max,step], dv, o?|fk?|ft?}`
  (choices, a named format, or the text at 33 points for a format of the module's own), so the app can draw the controls
  without the editor. `markDeck()` dots the mapped rows; `removeModule` drops its entries. `chainsEmbed` gained
  `setParam`, `deckSet`, `fmt` (exact text while the editor is open) and `reveal`. `thunderChains.deck()` tells the app.
- **App**: section `#dkChn` (`chn` in `DK_SECS`, between INST and FX = the signal flow: instrument → …chains → track FX →
  fader). `deckChains()` shows the entries whose module leads to the followed track's Chain Out (`chainMods(t)`: the same
  walk back from Chain Out as the engine's `_active()`), grouped under the module's name (click = show it in …chains),
  laid out in columns (`grid-auto-flow:column`) that widen the section. Moves go through `dkcSet` → `thunderChains.param`
  (live) + the editor's `setParam` (so its next save keeps them) + a debounced `saveS`. Right-click a row: `dkcMenu`.
- The panel's sections no longer shrink (`flex-shrink:0`, INST still takes the spare room): the panel scrolls sideways.

### Song impulse responses (Convolution reverb → Song IR 1–4)

`S.irs` = 4 × `{name, sr, len, L:{ref}, R:{ref}|null}`; the sound is two mono samples in the samples IndexedDB (the
`"ref"` strings keep `smpTidy` off them) and in `IRDATA` (id → Float32Array). Settings → Song → Load…/Clear
(`irsUI`, `irsLoad`). `irsPublish()` sets `globalThis.__thunderIR`, posts `{op:'irs'}` to the Worker and `{irs}` to the
live audio thread; new `thunder-tfx`/`thunder-chains` nodes get them in `processorOptions.irs` (there before the first
block, so offline renders have them). Project files embed them (`irsFull`), and loading converts them back (`irsAdopt`
in `setSong`, `irsResolve` after).

### Checked

Every new module in Node through both worklets (no NaN, bounded, CPU per block); both editors load a patch holding all
of them; every new pad effect and track effect renders in Chromium (track effects in an OfflineAudioContext); every new
engine preset renders on the page and in the Worker; the mapping flow end to end (right-click in the embedded editor →
the panel section → a slider moves the patch, the engine and the editor's value; remove; hide/show); song IRs on a pad
and a track effect; the demo song playing live with four new effects on each of two tracks (no errors, 10–12% load
each in the headless VM).

## Licensing rule (the owner may sell the app later)

Only **MIT / BSD / ISC / zlib / Apache** code goes in. **Never GPL/LGPL**: no Surge, Vital, Dexed, Helm,
OB-Xd, Odin2, ZynAddSubFX, Dragonfly or Calf, and not the DaisySP-LGPL add-on (DaisySP's main library is MIT;
its comb filter and ReverbSc are LGPL, which is why the comb resonator is our own code). Mutable Instruments'
STM32 modules are MIT; its AVR ones are GPL. Every addition gets:

1. a credit in `THIRD_PARTY_NOTICES.md`;
2. a **generic name** in the UI, with "based on X" in its credit (FX_CREDIT in `index.html`, help text
   in `chains/index.html`). Product names are trademarks even when the code is MIT.

Verified this session: Beads is MIT in upstream `pichenettes/eurorack`, and so is Cloud Seed Core. The VST 3
SDK is MIT since 3.8 (Oct 2025), for a future VST port; iPlug2 or DPF are permissive frameworks; JUCE needs
a licence.

## What was added this session

### Effects (pad FX, track FX and …chains, all on one wasm module)

`dsp/tfx/tfx.wasm` (also base64 in `index.html`'s `tfxWasm` tag, and copied to `chains/dsp/` and
`waves/dsp/`). Effect ids, in `dsp/tfx/tfx.cc`:

| id | key (app) | UI name | source |
|---|---|---|---|
| 0–10 | rings … beads | (existing) | unchanged, bit-identical output |
| 11–18 | flanger, phaser, tremolo, odrive, folder, autowah, pshift, ladder | Flanger … Ladder filter | DaisySP (`fx_ds2.cc`) |
| 19 | xmod | Cross-mod | Mutable Warps, run at 96 kHz (`fx_warps.cc`) |
| 20 | space | Space reverb | Cloud Seed (`fx_cloudseed.cc`) |
| 21–24 | lofi, drive, air, room | Lo-fi, Drive, Air, Room reverb | Airwindows DeRez2, PurestDrive, Air3, Verbity2 |
| 25–29 | comb, transient, tgate, ott, fshift | Comb resonator … Frequency shifter | **our own code** (`fx_extra.cc`) |
| 30–33 | vinyl, dub, cab, tube | Vinyl, Dub tape, Speaker cab, Tube | Airwindows ToVinyl4, Dubly, Cabs, Tube2 |

- **…chains** shows all of them except the DaisySP phaser (…chains already had its own JS phaser). They're in
  the palette group "More effects".
- **Cloud Seed's buffers** are shrunk at build time (`build.sh` seds `DelayBufferSize`) from ~67 MB to ~5 MB
  an instance. `fx_cloudseed.cc` caps size/pre-delay so lines stay inside those buffers.
- **Trance gate** needs the tempo and a restart:
  - In track FX, `fxRestartN` is bumped in `tStart()` and the effect lists are resent; the gate waits 80 ms,
    the sequencer's lead time.
  - In …chains it uses `ctx.transport.startN`, which the engine counts in `setTransport` when playing goes true.
  - On pads it uses `songBpm()`.

**How the three tables are made:** one spec, [`tools/fxgen.py`](tools/fxgen.py), generated the pad-FX
rows (`FX`), track-FX rows (`TFX`), the …chains worklet `effect()` defs and the editor `MODULES` entries, so
their ranges match. It prints JS to paste and edits nothing. To add effects, add a batch to it (as `FX2` was)
and paste each part. If you edit by hand, keep the worklet `params` and editor `params` identical (the check
below catches it). The Overdrive's default output was trimmed after generating (to 0.2), so the spec
differs from the app there.

**Gotcha found:** never call an effect parameter `type`. Pad FX objects keep their effect key in `f.type`,
and `freshFx()` merges `init` over it (the Speaker cab's model is `model` for that reason).

### Synth

- **Oscillators** (`newOsc()` just above `makeOsc()`): `supersaw`, `pd` (8 CZ-style waves), `chip`
  (4-bit, chip clock), `formant`, `vosim`, `grains`. Grains plays the layer's sample; Load sample keeps a Grains
  (or PWM) layer's type.
- **Mod matrix:**
  - LFO shapes 4 = S&H and 5 = smooth random (`hashRnd`, deterministic per render).
  - `lfoNSync` / `seqSync` lock to the song tempo (`SYNC_NAMES`/`SYNC_BEATS`).
  - Synced pads re-render when the tempo changes: `ensure()` caches the bpm used.

### …chains modules (`chains/chains-worklet.js`, JS)

- **`steps`:** 8-step CV with gate, synced or clocked.
- **`clock`:** gates with swing, plus `ramp` and `bar` ramp outputs.
- **`repeat`:** beat repeat, reverse and tape stop. It keeps 4 s of history; tape stop keeps recording and
  reads behind it.
- **`DIV_BEATS`** is shared by all three. All three restart on `transport.startN`.

### Break chopping (instrument level, `index.html`)

- **Instrument fields:**
  - `slc`: 0 off, 2–64 slices.
  - `smode`: 0 grid, 1 hits.
  - `sync`: 0 off, 1 REPITCH, 2 STRETCH, 3 LOOP.
  - `beats`: 1–64.
  - `grain`: 0–4, which is 1/64–1/4 beat.

  They are saved in songs (`normSong` reads them).
- **Code** (above `rtBuf`):
  - `loopSecs()`: the sample layer's length at its pitch, else the pad's length.
  - `findHits()`: onset detection.
  - `slicePoints()`: memoised.
  - `stretchData()`: STRETCH is Hann OLA; LOOP is hard loops with 1.5 ms crossfades. Both sync the grain to
    the beat.
  - `applyChop()`: rewrites a note event (`ofsSec`, `durSec`, `rate`, `st`, `cut`).
- **The player** `startNote` takes `getBuf(pad, rev, st)` and plays from `ofsSec`. `rtBuf` and the render's
  `getB` build stretched buffers keyed by `stKey(st)`.
- **Notes pick slices:** `(note − ROOT) mod slc`. The typed note's pitch is ignored; TRANSPOSE and pitch
  commands still apply.
- **INST rows:** SLICES, SLICE BY, SYNC, BEATS, GRAIN, › GUESS BEATS, › PLAY SLICES.

### Bug fix: song phrase numbers with sects hidden

With sects hidden, typing 14 on the Song screen made sect 14. Phrase 14 already had notes, so
`ensureSectPhrase` put phrase 03 in it: Song showed 14 and Arrange showed 03.

- Song cells now show and take **phrase** numbers (`songShown`, `sectForPhrase`, `setSongPhrase`; typed hex
  writes on the second digit).
- An empty sect uses its own-numbered phrase whenever no other sect plays it (`phraseInSect`).
- The owner's existing song still has one block pointing at phrase 03 (they were told to retype 14).

### Samples up to 30 s, stored in IndexedDB (and a start-up kit bug)

- **Limits raised to 30 s:** Load sample (`fileIn`), the sample layer's Hold (30000 ms), and the voice's Max len
  (0.1–30 s, now a squared slider). The voice-amp render cap is 32 s.
- **Why the limit existed:** the kit autosaved to localStorage (~5 MB) with samples inside as base64. A long
  sample filled it, and `save()` then fell back to saving without samples, so they vanished on reload.
- **Now** (`/* samples in IndexedDB */` block, near `save()`):
  - Each sample has an `id` and its 16-bit PCM is stored once in IndexedDB (`thunderplus-samples`; Thunder:
    `thunder-samples`).
  - The localStorage kit holds `{ref:id}` (`kitToJSON('ref')`).
  - On start-up and `setKit`, `resolveSamples()` fills them in, re-renders those pads and drops unused ones
    (`smpTidy`).
  - Old kits with base64 inside are moved over on start-up.
  - Kit and project FILES still embed the audio (`kitToJSON(false)`).
  - Those variables are `var`, because `save()` can run before that part of the script.
- **Bug fixed (from `5c9aa79`, earlier on 12 Oct):** `kitFromJSON` called `nslots()` (a `const` defined later) while
  the saved kit was read at start-up. It threw, so every reload started from the factory kit. It now uses
  `slotsNow()`. Verified: a renamed kit and a 25 s sample both survive a reload.

### Fix (13 Oct): Beads crash, and effects that crash are bypassed

- **Found by a stress test** of every effect through the real …chains param maps (defaults, min, max and
  random settings): Beads with **Freeze on and Size at exactly 1** divides by zero after ~1.5 s. A wasm trap
  stops the whole module, so on a Mixer track it silenced that track's effects until a reload, and in
  …chains it silenced the track's chain.
- **Fix:** Size is capped at 0.995 in all three Beads maps (`BEADS_SIZE_MAX` in the worklet, the `TFX` and
  pad-FX `beads` maps in `index.html`). The wasm is unchanged.
- **Guards** (so any future effect bug costs only that effect):
  - …chains `effect()`: a throwing `tfx_process` frees the handle and passes the sound through.
  - Mixer track FX (`TFX_SRC`): each slot's input is copied first; a throwing slot is bypassed, its handle
    freed and `{failed:key}` posted (the page toasts). `set()` remakes it when its settings next change.
- **Also seen, not changed:** Space reverb, Air, OTT and EQ reach the ±8 output clamp at extreme settings
  (Air also at some random mid settings). Loud, not broken.

### One app: Mobile (...Thunder) and Desktop (...Thunder Plus) (13 Oct)

The owner found keeping two apps in step hard (12 Oct's Plus work reached Thunder late), so they are one app
with a run-time setting.

- **Settings → App → Mobile** (`#pApp`, first in `GLOBAL_SECS`; `platform`, `setPlatform()`,
  `applyPlatform()` beside `let view`). Saved in `thunderplus-platform-v1`.
  - Mobile hides the Waves/Chains tabs (`applyPlatform` rebuilds `segView`), redirects `setView` away from
    them, makes `openWaves`/`openChains`/`focusChainHit` do nothing, drops Export/Import chains from File and
    the Project rows, swaps the waves layer's edit button for a note (`.wvEdit`/`.wvMobNote` CSS), and hides
    the brand's "+" (title `...Thunder`).
  - Switching to Mobile stores Desktop's input (`thunderplus-desktop-input-v1`) and goes to PAD; switching
    back restores it. A first run in Mobile starts in PAD.
  - …waves layers and …chains effects still **play** in Mobile, so files are the same in both.
  - `PF_DEFAULT`: Mobile on a coarse pointer, else Desktop.
- **...Thunder retired (13 Oct, the owner's choice):** one app, one repo.
  - `matthettich/Thunder-` now holds only a forwarding `index.html` (clears `thunder-*` caches, unregisters its
    service worker, then `location.replace('../thunder-plus/')`), a self-removing `sw.js`, README, manifest and
    icons. Tested locally with the old service worker installed: it was removed and the page forwarded.
  - **Saved work:** both apps lived at matthettich.github.io, so Plus reads Thunder's `thunder-kit-v1`,
    `thunder-song-v1` and IndexedDB `thunder-samples` (only reads). `thunderOffer()` (2.5 s after start-up)
    asks once, with `confirm()`, when Thunder's work differs from Plus's. Yes: it downloads Plus's song and
    kit as `<song> project (before Thunder import).json`, then opens Thunder's and copies its samples into
    `thunderplus-samples`. No: it keeps Plus's. The answer is stored in `thunderplus-thunder-import-v1`.
    Nothing is asked when Plus has no work of its own (it already opened Thunder's) or both are the same.
    **File → Open ...Thunder's saved work…** appears while Thunder's data exists.

### Performance: what costs what (13 Oct, measured)

Headless Chromium with a phone-sized screen and CPU throttled 4× (Playwright + CDP
`Emulation.setCPUThrottlingRate`):

- **Playback is fine:** median frame 16.7 ms while the demo song plays in the tracker.
- **Pad rendering is the cost**, all on the UI thread: drum pads 10–90 ms each, the 8 synth slots
  0.8–1.9 s each (they render their whole Max len, 6–8 s, at 8 layers). At full speed: 170–380 ms each.
- **First PLAY froze ~8 s** (2 s at full speed): pads rendered at start-up assumed 48 kHz, the first sound made
  the real context (44.1 kHz here), `audio()` marked every pad dirty and `tStart()` rendered them all at once.
- **Fixed:**
  - `srGuess` asks the browser for its rate at start-up (a context made and closed at once), and `ensure()`
    renders at that rate.
  - `audio()` no longer marks every pad dirty (`ensure()` already re-renders a pad whose rate differs).
  - `prerender()` renders one pad per idle moment (`requestIdleCallback`), after the wasm has loaded and
    again when audio starts.
  - Result at 4×: first PLAY takes 144 ms once the kit has rendered (about 10 s after opening at that
    speed), and 2.7 s if pressed straight away.
- **Then the Worker (below) took pad rendering off the UI thread altogether.**
- **Not worth a switch:** the …chains engine's draft quality only trims Vocoder bands (and turns on by itself
  under load), …waves' draft mutes modules, and the editors cost nothing until opened. So Mobile hides
  editors but doesn't change sound or quality.

### Pad rendering in a Worker (13 Oct)

- **What:** `rwStart()` builds a Worker from this page's own `/* ===ENGINE START=== */` … `/* ===ENGINE END=== */`
  block (the synthesis code: oscillators, `renderVoice`, pad FX), plus a small glue script. The block must
  stay free of DOM use at top level; the Worker has no `document`, `window` or `localStorage`.
  - **Wasm:** the loader compiles each wasm once (`plMod`, `txMod`) and shares the modules with the …waves
    engine and the Worker. This also fixed the double base64 decode.
  - **…waves:** the Worker `importScripts` `waves/waves-worklet.js`.
  - **Tempo:** the Worker's `S` only carries `bpm`.
- **Main-thread API** (next to `ensure`):
  - `ensure(i)`: up to date, now, rendered here if needed. Used by chops, slice info and anything that must
    be exact.
  - `ensureLive(i)`: for playing. Returns the last render while the Worker makes the new one. Used by
    `buffer()`, `rtBuf*` and `tStart()`.
  - `bgRender(i)`: one Worker job per pad. An edit made meanwhile re-sends the newest state when the job
    lands, and `padRev[]` stops an older result replacing a newer one.
  - `showCur()`: draws the current pad now and refreshes it in the background.
  - `renderAsync()`: the song render's pads also go to the Worker.
- **Fallbacks:**
  - No Worker, or a Worker that errors (`rwDown`): everything renders on the UI thread as before.
  - A voice that can't be posted: that pad renders on the UI thread.
  - A …waves voice when the Worker couldn't load …waves: that pad renders on the UI thread.
  - Both "no Worker" and "Worker crashes" were tested.
- **Same sound:** the Worker's output is bit-identical to the old main-thread render for the factory kit,
  every oscillator type (sample/grains/PWM with a sample), every pad FX (34) and a …waves patch.
- **Measured at 4× CPU throttle, phone screen:**

  | | Before | After |
  |---|---|---|
  | First 6 s after opening | 17 frames, 2 s freezes | 359 frames, no long tasks |
  | Dragging a synth-pad slider for 3 s | 32 frames over 100 ms, 2 s stalls | no frame over 33 ms |
  | PLAY | ~0.1–0.2 s | ~0.1–0.2 s |

  On a real phone, rendering still takes as long, but in the background. A pad tapped before its first
  render lands still renders on the UI thread (`ensureLive` with no cache).

## Modulation shown on sliders, as in ...Seeds (13 Oct, evening)

A cable into a slider's small purple jack (`p:<param>`) now always shows the ...Seeds treatment in **…waves and
…chains**: the value turns purple and bold, and a 3 px purple mark under the slider shows the live position.

- **…chains in …Thunder Plus was blind before:** the page runs no engine there, and the host never passed the track
  engines' `mods` telemetry on. Now each chains node keeps `n.mods`, and the …chains-screen meter timer in
  `index.html` merges them (first track engine wins per id/param) into `chainsEmbed.meters({peaks, load, mods})`.
- **…waves poly modules were blind with no note held:** telemetry read voice #0 only. `telemetry()` now rebuilds each
  modulated value from the cables: newest gated voice (else newest awake one), and mono sources (a free-running LFO)
  still count when every voice is asleep.
- **Cabled but no live data** (audio not started, no track routed through …chains): `paintMods` still marks the row,
  at the slider's own value; a 200 ms timer repaints when telemetry has been quiet for 400 ms (also clears unplugged).
- Telemetry now posts every 12 blocks (~31 Hz) instead of 24, so the mark moves smoothly.

## CPU meter, dropouts and clicks (13 Oct, evening)

- **The meter read 0% on browsers without `renderCapacity`** (headless Chromium 141; likely the phone too) unless
  track effects or …chains were on: pads, bass and the master never reported. Now the bass worklet and the master
  worklet report a Date.now()-averaged load like track FX do; `fxLoads()` lists `MST dyn` and `Bass` too.
- **Dropouts:** `glitchScan()` (every 100 ms) uses `ctx.playbackStats.underrunEvents` where it exists, else the slip
  between `getOutputTimestamp()`'s audio and page clocks (> 12 ms = one dropout; it follows slow drift). A dropout
  turns the meter red for 3 s; the tooltip and the click toast give the count. **Untested on a real device:**
  headless Chromium's fake output waits for a stalled audio thread, so it never underruns (no false positives seen).
- **Clicks:** a late `kill()` (UI thread stalled) fades from `currentTime`, not from a past time (which was an
  instant cut). Notes started part-way into a sample (slices, offsets) get a 1.5 ms fade-in.
- **Phone defaults:** latency is `latOf()`: the person's pick (`aset.latPick`), else Balanced in Mobile and Lowest on
  desktop. The sequencer looks 0.25 s ahead in Mobile (0.15 s on desktop).
- **Track FX sleep:** a `thunder-tfx` node whose input is silent (< 1e-7) and whose output has been below 1e-6 for
  1 s outputs silence without running its effects; any input wakes it; settings changes reset it. Long reverbs
  take ~10 s to get there. Measured: two reverb tracks 5–8% → 0% once tails die.

## Shared modules: …chains and …waves have the same effects and utilities (13 Oct, evening)

- **One source, two copies.** `chains/chains-worklet.js` holds a `/* ===SHARED MODULES START=== … END=== */` block (the
  DSP) and `chains/index.html` a `/* ===SHARED EDITOR START=== … END=== */` block (editor entries, help text, palette
  groups, displays). **Edit them in …chains, then run `python3 tools/sync-shared.py`**, which copies both into …waves.
  Each MODULES map spreads/assigns `SHARED_MODULES`; `mergeShared()` adds the palette groups (skipping a module the
  palette already lists); help falls back to `SHARED_ABOUT[t]` then `d.about`.
- **Moved into the blocks** (so …waves now has them): Beads, the 22 "More effects" (flanger, tremolo, overdrive,
  wavefolder, auto-wah, pitch shifter, ladder, cross-mod, space, lo-fi, drive, air, room, comb, transient, trance gate,
  multiband squash, frequency shifter, vinyl, dub, cab, tube), envelope/gate/pitch followers, slew (now labelled
  "Slew limiter"), sample & hold, quantizer, Blades, meta modulator, phaser, width, ping-pong delay.
- **New, original code:** Meter (VU + peak, pass-through, Trim), Limiter (2 ms look-ahead peak limiter, running-max
  detector, hard ceiling), Utility (gain/pan/mono/polarity/mute), Ring mod (own carrier sine/tri/square/noise or the
  Carrier input; Freq CV ±4 oct), Spring reverb (two springs: delay + 18 stretched z⁻² all-passes in feedback, drip
  pre-emphasis), Plate reverb (Dattorro's 1997 figure-eight tank, written from the paper), Ensemble (3-voice string
  ensemble or the two-speed 80s polysynth chorus), Math, Logic, Clock divider, Euclidean.
- **Displays:** a module def with `show(st)` returns numbers; both engines' `telemetry()` sends them as `shows`. Editor
  entries with `display:'meter'|'gr'` get `buildShow()`, painted by `paintShows()` from `onMeter` and, in …Thunder
  Plus, from the host (`index.html` keeps each chains node's `shows` and forwards them with the meters, like `mods`).
- **Checked:** every shared module run in Node on both worklets (no NaN, no runaway, tails decay; harness was a
  scratch script, not committed); spring RT60 1–3.5 s, plate 1.75 s at default; limiter holds −0.3 dB with 10 dB GR;
  plate costs ~4.6% of a desktop core (first version was 32%). Browser: all palette entries present in both editors,
  every new module adds, Meter/Limiter displays live standalone (…waves) and embedded (…chains in Thunder Plus).
- The Effects page's pad/track FX (`FX`/`TFX` in index.html) are separate and unchanged; the new reverbs aren't there
  yet (they're JS, not in the wasm).

## PAD mode: OPTION+arrows in Phrase move around the Song grid (13 Oct, evening)

- The owner expected OPTION+arrow on the phrase screen to go to the phrase above/below/left/right of it on the Song
  grid, not phrase number ±1. `optDir(dir, grid)`: the PAD d-pad (`op` held) and Alt+arrows in PAD mode pass
  `grid`; with PATTERN NAV on (default) `phraseGrid()` finds this phrase's block (`followPhraseBlock`), moves to the
  nearest filled block that way (same track for ▲▼, same row for ◀▶, skipping empties), opens its first phrase and
  sets `cTrack`/`cSect` as opening it from Song would. At an edge it toasts and stays. A phrase not in the song
  steps by number as before; `[`/`]` and KEYS-mode Ctrl/⌘+▲▼ still step by number. Help, the PATTERN NAV caption
  and the phrase hint say so. Shift+Option+arrows (`shiftAltArrow`) is unchanged.

## Mastering close on phones, master bus kept on FX, Dimension / BBD delay / Harmonizer (13 Oct, evening)

- **Mastering window:** its × sat in the wrapping header and got lost on phones. Now `.mwx` is a labelled CLOSE
  button pinned top-right of `.mwhead` (which reserves 120 px for it), and the phone's Back closes the window
  (`popstate` → `closeMst()` before the other handlers).
- **Master bus forgotten on FX:** `go()` pointed FX at `cTrack` whenever you came from any screen but the Mixer, so
  Mixer master → FX → Project → FX landed on a track. It was a bug (the master is FX target `NT`, no master track
  needed). Now only Song, Sect and Phrase (`fromTrack`) point FX/Mixer at the edited track; FX on the master → Mixer
  goes to the master row (y 7), FX on a track leaves the master row.
- **New shared modules (original code):** Dimension (two antiphase BBD lines cross-mixed for width, four modes),
  BBD delay (bandwidth from a 4096-stage chip at that time, saturating feedback that self-oscillates boundedly,
  wow/flutter, Age hiss, time glide = pitch bend, tempo sync, Time CV), Harmonizer (two voices, interval menus,
  level/pan each, detune, delay, window; splice-aligned delay-line shifter: each jump picks the lag that best
  correlates with the playing waveform, then crossfades). Checked: echo at 250.2 ms for 250 ms; harmonizer within
  0.3% by autocorrelation (the plain two-head version was ~2% out); CPU 2.5% / 4% / 4.5% of a desktop core.

## Voice engine: a singing robot (13 Oct, evening)

- **What:** synth slots can set `V.engine = 'vox'` (Synth → Engine: Voice). Settings in `V.vox` (`normVox`; `txt` is the
  lyrics, the rest numbers); `voxParams(V)` folds in the voice's Tune, Drive, Level. Original code, no third-party code.
- **`class VoxCore`** (ENGINE block, self-contained like BassCore; tables are static getters so `toString()` carries
  them into the worklet):
  - `parse(txt)` → syllables `{on:[…], nu, co:[…], w, word}`. Words: the dictionary (`dict()`, ~250 common words incl.
    contractions) else `rules()`: letter-to-sound rules written after the 1976 NRL rules (public domain), format
    `left|MATCH|right|PH` with `_` for the word edge. `[ARPAbet]` in brackets is used as is; `-` forces a syllable break.
    `syllabify`: one consonant between vowels starts the next syllable, of more the first closes this one; NG never
    starts one.
  - Synthesis: glottal-pulse table ↔ polyBLEP saw (Robot), tilt (Bright), breath noise (louder in the open phase;
    Voice 0 = whisper), five cascade resonators (Klatt form; F4/F5 fixed, all × Size), frication noise through its own
    resonator in parallel, DC block, envelope, glide, delayed vibrato. Controls glide to targets (~12 ms formants).
  - Notes: `on(leg, n, same)` plans syllable n: onset consonants (stops = closure + burst + aspiration; affricates =
    closure + burst + frication), then the vowel held (`hold`); `off()` plays a diphthong's offglide, the coda, then
    releases. `busy()` is true while the coda plays.
- **Live:** `thunder-vox` worklet (`VOX_PROC`, in the same Blob as the bass one) with AudioParams rate/gate/trig/leg plus
  `syl` and `same`. `makePlayer`'s `bassNode` makes a `vox` node per track+pad (key `t:pad:vox`); `bassNote` counts
  syllables per node (`sylN`, a slide keeps the current one) and `stopAll` resets them, so PLAY starts the lyrics over.
  `isLivePad` = bass or voice; offline renders await `bassReady` for either. Load shows in the CPU meter's Bass entry.
- **Pads/Worker/exports:** `renderVox` sings the whole line at the pad's pitch, one syllable per `sylLen` (Pad step).
- **UI:** `#voxWrap` card (lyrics textarea with syllable chips, Voice / Expression / Timing rows `VU`, presets
  `VOX_PRE`, Say it). `typing()` now counts textareas. Tracker side panel shows Level, Robot, Size, Vibrato, Glide.
- **Checked:** phonemes for test sentences; vowel spectra peak at their formants; 3.9% of a desktop core; a 12-note
  melody through `pass()` (the offline tracker path, same worklet) sings every note, no NaN; Worker pad render; live
  node outputs (peak 0.54) and reports load. Not yet judged by ear on the owner's phone.
- **Ideas next:** a tracker command to jump to syllable N; CMU-dictionary pronunciations; per-note vowel colour.

## Fix: tapping a pad didn't select it (13 Oct, evening)

- `vuHit()` called `slotAt(hits, i)`, which pads `hits` with `null`, then pushed into `hits[i]`: the first tap on any pad
  threw, so `padPress` never reached `select(i)` (and the VU loop's `hits[i].filter` could throw the same way). Now
  `hits[i]` is made an array first, and `padPress` selects the pad before sounding it, so a sound error can't stop it.

## Blip engine, Blip drum and Life (13 Oct, evening): Newscool-style generative drums

The owner asked for drums like Reaktor's Newscool (eight simple tone voices driven by a Game of Life sequencer).
All original code; the name stays generic.

- **`class BlipVoice`** (one copy in index.html's ENGINE block, one in the SHARED MODULES block; keep them identical):
  parabolic tone, kick (pitch drop ×2^(kick·5·env), Kick time), FM (sine at fmR × pitch, phase mod), ring mod
  (ringF Hz), noise, click, linear attack + exponential decay, drive, bipolar SVF (filt < 0 LP, > 0 HP, resonance
  level-compensated), grain stutter (re-hits every grainT, ×0.72 each). `render(out, a, b, g)` ADDS into out.
- **Blip engine on pads** (`V.engine = 'blip'`, settings `V.blip`, `normBlip`, `renderBlip`): any pad, drums too. The
  Engine row now shows on every pad (Layers / Bass / Voice / Blip); on drum pads Bass and Voice render one-shots.
  Card `#blipWrap` (`BLU` rows, `BLIP_PRE` presets, Random). Tracker side panel shows Level + six blip rows.
- **Blip drum module** (`blipdrum`, shared): eight voices on gate inputs t1–t8, per-voice params `p k f m n d a x` + voice
  number (pitch, kick, FM, ring, noise, decay, level, filter), hidden in the card and edited as bars (`display:'bars'`,
  `Edit` picks the row); globals FM/ring ratio, kick time, decay scale, drive (on the mix), LFO → pitch, width,
  feedback delay (tempo divisions), level. `show()` returns each voice's level, so the bars light as they play.
- **Life module** (`life`, shared): 16 × 8 torus, board in hidden params b0–b7 (row bitmasks), edited on a canvas
  (`display:'life'`; Clear / Random / Glider / Blinker). The playhead advances one column per Step (or per Clock pulse);
  live cells in that column fire their row's gate (Fire on Born: only new ones); after each sweep, one generation
  (rules Life, HighLife, Seeds, Maze, Day & Night). Loop N reloads the drawing every N generations (0 = evolve).
  Changing the drawing reloads it. Outputs g1–g8, Any, Count. `show()` = [x, gen, rows…] for the live grid.
- **Editors:** a param whose ui has `hide:true` gets no row (both editors' buildNode). `buildShow` routes 'bars' and
  'life' to `buildBars`/`buildLife`; `paintShows` feeds them; a 90 ms timer redraws them.
- **Checked:** the glider walks correctly; Life → Blip drum renders (no NaN), both editors add, cable, run live (grid
  advances, bars light), tapping the grid toggles cells, dragging bars sets values; Blip on drum pad 0 renders.
  CPU: Life ≈ 1–2%, Blip drum depends on how many voices ring (all eight constantly ≈ 20% of a desktop core worst case).

## Degrade (lo-fi) everywhere (13 Oct, evening)

- **`class DegradeCore`** (original; three identical copies: index.html ENGINE block, the track-FX worklet Blob is built
  as `DegradeCore.toString() + TFX_SRC`, and the SHARED MODULES block): bits (continuous, 24 = off), rate (sample and
  hold), noise floor (RMS = noise² × 0.25, −∞…−12 dBFS, constant across Colour), Colour (one-pole darkening,
  loudness-compensated), Follow (noise × input envelope, so silence stays clean), separate Dry/Wet. Stereo, in place.
- **Pad FX** `degrade` (`js:true`: `fxReady` doesn't wait for wasm; `applyFx` runs it first). **Track FX** `degrade` (TFX id
  99, `LOFI`): the worklet's `set()` makes a JS slot (`s.js`) instead of a wasm one and `process()` runs it; JS slots are
  never `tfx_free`d. It is NOT in `tools/fxgen.py` (hand-added). **Module** `degrade` in …chains/…waves (rate 0–1 there).
- **Checked:** noise RMS matches the formula at every Colour; Follow 100% drops the hiss to −47 dB 0.35 s after the
  sound; pad FX −24 dB floor at Noise 50%; track FX through the worklet −18 dB floor at NOISE 180 (vs −240 without).

## Resonator (chord) (13 Oct, evening)

- **`class ResonCore`** (original; three identical copies like DegradeCore; the track-FX Blob is now
  `DegradeCore + ResonCore + TFX_SRC`): up to 5 Karplus–Strong loops tuned to Note + a chord (`CHORDS`, 12 shapes),
  one-pole damping in the loop (Bright), feedback from a T60 (Decay: 0.05 × 160^decay s), Tube = negative feedback with
  the loop halved (odd harmonics, same pitch), loop length minus the damping filter's delay, DC block, tanh out,
  Spread pans the voices. Measured: in tune (110/220/440 Hz exact; String and Tube), < 1% of a core.
- **Pad FX** `reson` (js), **track FX** `reson` (`RESO`, id 98, in `TAILS`), **module** `reson` (Note CV: 0.5 = A4,
  0.125/oct, the knob transposes from C3). Rings and Comb resonator are still there; this is the chord one.
- Test gotcha: `pass(evs, …, getB)` needs a real `getB` (a buffer per pad), or the notes are silent.

## Karplus–Strong: Pluck string layer and …waves String (13 Oct, evening)

- **`class KSString`** (original; two identical copies: index.html ENGINE block and waves-worklet.js, outside the shared
  block because String is …waves-only): `pluck(P, bright, pos, amp, rnd)` adds a noise burst (one-pole darkened,
  circular comb for the pick position, zero-mean, normalised) to the loop; `tick(P, t60, a)` = linear-interpolated
  read at P − (1−a)/a (damping filter delay), one-pole damping, loss per pass from T60 (cached), DC-blocked out.
  Measured: within a few cents from 55 Hz to 1.76 kHz; 0.1% of a core per voice.
- **Layer type `karplus`** ("Pluck string", Bright / Decay = 0.08 × 100^b s) in `TYPES` + `newOsc`; plucks on its first
  sample with the layer's seeded random, so renders repeat. **…waves module `string`** (poly, Pitch + Gate in; Tune,
  Bright, Decay, Position, Mute = ring time cut while the gate is low, Level).

## 13–14 Oct (night): fixes, presets, four engines, pitch FX, Pitch Track

Commits on `main`: 9ababb0 (fixes), e21c605 (presets + song-load fix), e8f2970 (engines, poly voice, icon), e6b1f5c
(vintage sampler, pitch FX, Pitch Track). Cache is `thunderplus-v83`; `chains-worklet.js?v=16`, `waves-worklet.js?v=12`.

- **Big fix: the saved song was lost on every reload.** `normSong` called `nslots()` (a const declared later) while
  `let S = …` ran at start-up; the throw fell into the catch and loaded `demoSong()`. Now `nslotsEarly()`.
- **Layout:** `fitCheck()` also shrinks the text to the widest line (Song/Mixer have 16+ tracks, more than BASEW);
  MONO/POLY `.tgbox` boxes are positioned in em; `fonts.ready` / `loadingdone` redraw. Settings (`.gset`) is a fixed
  sheet on screens ≤ 600 px.
- **Phrases list** (File ▾ → Phrases…, `phlOpen`/`phlDraw`/`phlPlace`): phrases with notes, filter "not on the song".
  **`pendPlace`**: moving from an empty song block into Phrase opens a fresh phrase and `placePending()` (via `saveS`)
  puts it in that block on its first note.
- **Voice:** rests (`. , ; : ! ? _` → `{rest:true}` syllables, `on()` silences); `hiss` (Consonant noise) and `clar`
  (Clarity: narrower bandwidths + presence) params; softer aspiration/burst; Breath default 0.3. **Poly tracks:**
  `polyVox()` in `makePlayer` uses up to 4 vox nodes per track+pad (`bassNode(t, pad, j)`), chord notes share a syllable.
- **Presets:** `factoryVoicePresets()` / `factoryLayerPresets()` (layer presets carry `vt`, the pad tune they were
  written for, and are re-pitched), song presets in `S.presets` (`normPresets`; samples by ref; `smpTidy` keeps them),
  File → Export song presets / Import presets (exports embed samples).
- **Engines** (ENGINE block, table `XENG`, settings in `V[V.engine]`, `normX`/`xParams`/`renderX`):
  `AcidCore` and `MachCore` are live (worklets from `XPROC(name, core)` in the bass Blob; AudioParam `acc` = accent;
  the sequencer sets `ev.acc` when volume ≥ C0); `ToneDrum.render` and `ModalCore.render` are rendered. One shared card
  `#xWrap` built from `XUI[kind]` (rows, sections, presets). Deck instrument rows are generic.
- **Vintage sampler** (sample layers): `L.vint` / `L.vintF`, `VINT` table, `vintData` (cached per sample) + `vintOsc`.
- **Pitch delay / Pitch reverb:** `PitchFxCore` (two-head granular shifter in/after a delay or a small reverb; loop gains
  kept under 1 — the reverb is normalised by (1−fb) and the shifter by 0.75). Pad FX `pdelay`/`preverb` (js), track FX
  ids 97/96 in the TFX worklet Blob.
- **…chains Pitch Track** (`pitchtrk`, shared block, synced to …waves): YIN on a 4× decimated buffer, ~90 runs/s.
- **Icon:** the two dogs' faces split down the middle, `icons/plus-*` (`?v=2` in manifest, page and sw).
- **Not yet heard on the phone:** all of the above was checked headless (renders, no NaN, levels, live nodes through
  `pass()`). Wavetable import (Serum-style 2048-sample frames) is deferred at the owner's request.

## 14 Oct (midday): swing timing fix, four dog dances

- **Swing/row timing fixed** (song `v:6`). A row used to last `pair·line·(sw or 1−sw)` with `pair = 2·swB`, so every row
  lasted SWING NOTE's number of lines (16ths, the default: 2) and LPB 4 played 8th-note rows. Now `2·line·(…)`: a row
  averages one line whatever SWING NOTE says. **Old songs are converted in `normSong`** (v < 6): LPB ÷ swB (else BPM ÷
  swB when LPB doesn't divide), phrase LPBs ÷ swB, TICKS × swB, TIC values × swB, so DEL/RET/KIL/OFF/PSL/ARC keep
  their length. Verified against the previous build: identical note times for 50/66/58/60% swing, swB 1–3, LPB 3/4/6,
  with DEL and RET in the phrase. `swingNote()` toasts once ("now reads LPB 2, 12 ticks…"). New songs default to LPB 4
  (true 16ths; the owner's earlier "LPB 8 default" was compensating for this bug). Grooves with a 0 (= "ticks a
  step") digit in old songs would stretch with the new ticks; not converted.
- **Settings → App → Show …waves and …chains on Mobile** (`mobEd`, `thunderplus-mobile-editors-v1`, off by default; the
  row only shows in Mobile). `noEditors()` = Mobile and not ticked replaces `isMobile()` in every editor gate (tabs,
  setView, openWaves/openChains, chains focus, File/Project chains rows); CSS uses `body.noed` for the waves-layer button.
- **Master phrases are commands only** (`mstView()` = `cTrack === MT`): `pCols()` gives just the FX columns, the phrase draw
  skips notes/V/I and neighbours, the picker and EDIT+▲▼ use `FXLIST_MST` / `MST_FXC`, letters only match those, the hint
  and the stage (master FX chain) change. Same row format underneath, so a phrase can still be used anywhere. Also fixed:
  EDIT×2 on the LEN/LPB line (`c.y === -2`) threw in `editDouble`.
- **Dogs:** legs are now groups (`.leg.ll/.lr`, `--lL/--lR`). `DOG_MOVES` up / down / legs / both; `dogsFollow()` (on each
  beat) advances the dance when the followed track (cursor's, else the first playing) starts a phrase: another block, or
  its step wrapping back (a repeat). Verified live: a new dance every 16 rows.

## 14 Oct (late morning): Piano, Tine EP, Organ, SoundFont / General MIDI

Cache `thunderplus-v85` (sw.js no longer caches `sounds/`: the app keeps fonts in IndexedDB).

- **Keys engines** (ENGINE block, rendered like Modal; `XENG`/`XUI` `piano`, `ep`, `organ`): `KeysUtil` (rotating-phasor
  partial banks, `finish()` = the usual normalise/drive/trim), `PianoCore` (stiff-string partials, hammer/strike-point
  spectrum, two-stage decay, 1–3 detuned strings, thump, 3-mode soundboard, dampers/pedal; ≤12 s), `TineEPCore` (tine +
  tone bar + bell pings through a 1/(1−kx)² pickup; tremolo), `OrganCore` (9 drawbars with foldback, percussion, click,
  rotary with horn Doppler, Pluck = the M1 "Organ 2"-style house organ preset). Checked in Node: no NaN, 10–400 ms each.
- **SoundFont engine** (`V.engine = 'sf'`, numbers in `V.sf` via XENG, font in `V.sfFont` / `V.sfFontName`). Code is in
  the UI part of the script (before `XUI`): `parseSF` (SF2.04: zones with preset/instrument generator merging, modulators
  merged by identity over `SF_DEFMODS`), `sfVoice` (generators + velocity/key-number modulators; velocity→attenuation is
  skipped because the tracker volume already sets loudness), IndexedDB `thunderplus-fonts`, `sfLoad`, `sfSample` (SF3
  Ogg via `decodeAudioData` on an OfflineAudioContext; SF3 loop points are relative to the sample start, SF2 absolute),
  `sfPick` (linked stereo pairs → one stereo buffer; FluidR3's pairs are unlinked, so each half plays with its own pan).
  **Player:** `startNote` hands SoundFont pads to `sfNote` (per-zone buffer source + volume-envelope gain + low-pass with
  the mod envelope + zone pan, one voice with `srcs` for kill). `pass()` awaits `sfPrepare(evs)`; `tStart` starts it in
  the background. **Pad render** (`renderX` → `globalThis.__sfRender`, because the ENGINE block can't see the UI code):
  C-4 at velocity 100. `rwCan` keeps these pads off the Worker. **UI:** `sfPanel()` (font select, preset list, Load
  .sf2/.sf3, Remove) at the top of the engine card; XUI presets are GM shortcuts.
- **GM sounds:** `sounds/FluidR3Mono_GM.sf3` (MuseScore's MS_General v0.1 fork of FluidR3Mono, MIT, 23.7 MB) with its
  licence beside it; fetched by `sfFetchGM()` on first use. Tested headless: loads in ~0.6 s, piano + drum kit decode in
  ~1.9 s, offline and live playback, OFF releases, kits.
- **Not done:** SoundFont LFOs (vibrato/tremolo), chorus/reverb sends, linked modulators, CC modulators.

## 14 Oct (morning): master column, tempo-locked FX, swarm, skins, Settings

Cache `thunderplus-v84`. Worklet versions unchanged.

- **Master column (MST):** song column `MT` (= `NT`, index 32: `S.song` rows are now `NT + 1` long, `normSong` reads it).
  Song screen only (after the poly tracks, amber `.tgbox.gx`, `--tk-mst`); not in Arrange. The song cursor sits on it
  as `cs.song.x === MT` (`move()` steps from the last track to it; `draw()` doesn't clamp it); `cTrack = MT` when its
  phrase is open (sub label MASTER, no neighbours). `trackWin(c, lab, extra)` reserves its room only when the tracks
  overflow. `makeSeq` runs `tr[MT]` like a track but plays no notes; `toggleBlockMute` refuses it (smute is a 32-bit mask).
- **Commands** (end of `FXC`): `FXO XY` (slot X 1–4, 0 = all; Y 0 off, 1 on, 2–F on for Y rows then back),
  `FLX` (Flex pattern 0–16), `GAT` (Step gate pattern 0–15), `RTG XY` (X rate 1–8, Y rows). `fxEvs()` in `makeSeq`
  turns them into `{type:'fxc', track, p, v}` events; in the MST column `VOL` becomes `{type:'mvol'}`. Rows for momentary
  lengths are the average played row (`pair * line / 2`, see the swing note below).
- **How they reach the audio, sample-accurately live and offline:** `thunder-tfx` now has AudioParams `bp` (a-rate:
  the song position in beats) and k-rate `sw0`–`sw3`, `flx`, `gat`, `rtg` (−1 = as set). The player owns a
  ConstantSource **beat clock** (`clock`, ramped by `{type:'beat'}` events that `makeSeq.step` emits every step, wraps
  at 16 beats, −1 stopped) connected to every effect node's `bp` (`clockTo`, `setMfx` for the master node). `fxc`
  events are `setValueAtTime` on the node's param; `stopAll` → `resetSeq` puts them all back to −1 and the master gain
  to `S.master`. `tfxChain` now sends **off slots too** (`on:false, slot:k`) so FXO can switch them on; nodes exist
  when `fxAny()`. Slot switching fades 5 ms (`s.g`); an off Flex is still fed (`feed`).
- **Effects** (original, JS, in the TFX Blob: `clkSpb`, `FlexCore` id 95, `StepGateCore` 94, `RetrigCore` 93; TFX keys
  `flex`, `sgate`, `retrig`; not in `tools/fxgen.py`, hand-added like `degrade`). Not on pads or in …chains yet.
- **Swarm engine** (`SwarmCore`, ENGINE block, rendered like Modal; `XENG.swarm`, `XUI.swarm`, 12 presets): ≤48
  polyBLEP voices, drift paths at control rate (32 samples), scatter intervals, bloom, shimmer, TPT low-pass. ~0.5 s for
  a 48-voice 6 s note in Node.
- **Pitch reverb fix:** shimmer loop gain was > 1 at large Size (the reverb is ~2.4× at its low resonances), so it
  self-oscillated; now Size tops at fb 0.94 and the shimmer loop is scaled 0.38 (measured to decay at every setting),
  plus a slow limiter on the loop.
- **Skins:** terminal skins' cursor is `--mxcur` (default the accent `--mxpoly`), never the playhead's `--mx`; cool
  cursors on the single-colour ones, lavender `#9a63ff` on Simple grey, which is now the first-run default. Monotone blue.
- **Settings:** Audio (+ **Reset audio engine**, `resetAudio()` → `pl.dispose()` and a fresh graph), Project (Song,
  Input, Render & export with the `pExport` panel, Mastering with `pMaster` + LIMITER/MASTER rows), Screen
  (`buildScreen`: the Screen rows + `pSkin`), MIDI, Kit, App. `INNER_SECS` are re-parented on each open.
- **FX target follows** the edited track (`draw()` sets `cFxT` on Song/Sect/Phrase). **LPB default 8** (new songs).
- **Mutable:** `git clone --depth 1 https://github.com/pichenettes/eurorack`, then
  `git -C eurorack submodule update --init stmlib`. Use the current upstream: older checkouts lack Beads.
- **DaisySP:** `git clone https://github.com/electro-smith/DaisySP`.
- **Airwindows:** a sparse checkout of `airwindows/airwindows` with the plugin folders listed in `build.sh`.
- **Cloud Seed:** `git clone https://github.com/GhostNoteAudio/CloudSeedCore`.
- **Toolchain:** wasi-sdk 25.

```
E=eurorack D=DaisySP A=airwindows/plugins/LinuxVST/src C=CloudSeedCore WASI_SDK=wasi-sdk-25.0-x86_64-linux sh dsp/tfx/build.sh
```

Then copy `dsp/tfx/tfx.wasm` to `chains/dsp/` and `waves/dsp/`, put its base64 into the `tfxWasm` script tag
in `index.html`, and delete the build output from `dsp/tfx/`. **Check the old effects still match**: render
ids 0–10 with the old and new wasm and compare (this session they were bit-identical). Then bump the
caches.

## 14 Oct (afternoon): the sequencer never renders a pad mid-song

- **Risk found:** `pump()` books notes from a 25 ms timer on the UI thread. A note on a pad with no usable
  render (new pad, first moments after opening, sample-rate change) made `ensureLive()` fall back to `ensure()`,
  rendering on the UI thread: measured 588 ms for a synth slot at 4× throttle, longer than the look-ahead, so
  notes came late. Chopped/synced instruments did the same through `ensure()` inside `makeSeq`.
- **Fix:** `ensurePlay(i)` (next to `ensureLive`): while `tp.on`, a pad the Worker can render returns `null` and
  goes to `bgRender`; the note is silent until the render lands (the player already skips a null buffer).
  `rtBuf`/`rtBufPlain` and the live `makeSeq` (`o.live`, set by `tStart`) use it. Offline renders, exports,
  `tStart`'s own pass and previews while stopped still use `ensure`/`ensureLive`. SoundFont voices, …waves without
  the Worker's copy, or no Worker at all: render here as before.
- **Look-ahead** is now 250 ms on every device (was 150 ms on a computer). Tracker edits and mutes reach notes
  up to ~100 ms later; pads, keys, previews and STOP are immediate.
- **Verified headless (4× throttle):** `ensurePlay` on an unrendered synth slot returns at once (0 ms) and the
  Worker's render lands in the background; the old path took 588 ms. Not yet heard on the PC or the Fold.
- **Later (the proper fix):** move the sequencer and note playback into an AudioWorklet, so nothing on the UI
  thread can delay a note. That is also the shape a native (C++) engine needs.
- Cache `thunderplus-v90`.

## 14 Oct (late afternoon): MIDI out, clock out, clock in

- **Where:** the `/* ---------- MIDI out and MIDI clock ---------- */` block right after the MIDI input code.
- **Instruments:** `I.mo` (0 PAD, 1 MIDI, 2 PAD+MIDI) and `I.mch` (0–15), on INST as OUTPUT and MIDI CH. Note events
  from `makeSeq` now carry `ii`, `mn` (tracker note + 12, so C-4 = 60) and `vel` (volume column / 255; retrigs fade it).
- **Sending:** `pump()` passes every event through `moEv()` first (it returns true for MIDI-only notes, which the
  player then skips). Mono tracks end the previous MIDI note at the next note; poly tracks keep 4; OFF, KIL and
  muted blocks end notes; a note with no end is held until one of those. `msend()` timestamps with `a2p()`: the audio
  clock mapped to `performance.now()` at the moment of hearing (`getOutputTimestamp`, offset smoothed in `amap`,
  since raw calls jitter ±2 ms). Gear delay (`midi.lat`) sends notes earlier, not the clock.
- **Stop / changes:** `moStop()` (in `tStop`) = `moRelease()` (note-offs for everything, including notes booked
  ahead; `clear()` where the browser has it; CC 123 on used channels) + MIDI Stop. Changing OUTPUT/MIDI CH while
  playing only releases notes, so the clock keeps running.
- **Clock out:** `moClock()` at the end of each `pump()` sends ticks up to `seq.time` from the last `'beat'` event,
  at the current tempo (straight; swing moves notes only). Start goes out on the beat-0 event.
- **Clock in:** `clkIn()`: Start/Continue restart PLAY via `playCtx()`, Stop stops it. Tempo is the mean over the
  last 48 clocks; every sixteenth `setTempoLive()` sets `tp.seq.bpm` (exact) and `S.bpm` (rounded). Phase: the
  device's beat (clocks since Start / 24) against ours at the moment that clock is heard; tempo is nudged by err/2,
  at most ±8% while |err| > 0.05 beat, ±3% after. Song Position Pointer is ignored (Continue starts like Start).
- **Exports:** `pass()` skips MIDI-only notes. Settings (port id, clock on/off, delay) live in `thunderplus-midi-v1`
  on the device, not in the song; OUTPUT and MIDI CH are in the song.
- **Verified headless with a fake Web MIDI device:** notes on the right channel and velocity, booked 100–390 ms ahead;
  clock intervals 20.83–21.36 ms at 120 BPM (20.83 expected); Start first, Stop and All Notes Off at STOP. Clock in
  at 100 BPM: tempo 100, phase error 0.17 beat at Start, under 0.01 beat after about 8 beats. Not yet tried with real
  gear (MPC One, Force).
- **Not sent yet:** pitch bend for slides/PBN, arps, CCs; Song Position Pointer.

## 14 Oct (evening): instrument list

- Settings → INST LIST (`ck: 'Show instrument list'`), **off by default**, stored in `thunderplus-instlist-v1`
  (`inlOn`, `setInl`). `instList()` runs at the end of the tracker `draw()` and only rebuilds when the names change.
- One row per instrument: its number and its pad's full name (`MIDI ch` for MIDI-only, `+MIDI` for PAD+MIDI),
  current instrument highlighted, long names cut with an ellipsis. A tap sets `cInst` and opens INST.
- Where: `#tInlSideW` in the side panel when the panel is showing; otherwise `#tInlPad` between the d-pad and the
  keys in PAD mode (hidden under 560 px wide, where there's no room). Never both at once.

## 14 Oct (evening): Simple rainbow skin

- `rainbow` in `SKINS`, base `term`, so it is Simple grey's look (white, outlined keys, solid PAD keys).
- Colour by position: `--ci` = an element's place among its siblings (nth-child, 12 steps) for tracker spans,
  buttons, pads, meters, keypad keys and Arrange blocks; `--ri` = the tracker line (12 steps). Hue
  `(ci + ri) × 30°` in OKLCH at one lightness. Buttons get their own `--mx`/`--mxdim`/`--mxsoft`, which the term
  rules already use, so outlines, text and solid fills all follow. Cursor and playhead cells keep their colours.
- **Simple rainbow dynamic** (`rainbowdyn`): the same CSS, plus `rbWheel()` (called from `applySkin`) setting
  `filter: hue-rotate()` on `<html>` ten times a second, 0.3° a step, once round every 120 s. Not a CSS animation:
  on the root that recalculated and repainted every frame (60 recalcs/s); this is 10/s, about 8 ms of main thread a
  second on a desktop. Paused while hidden, off with reduced motion. Fixed overlays (Help, toasts) are unaffected.

## 15 Oct (afternoon): review risk fixes, one commit each on main

After the eight bugs below were merged (PR #1), the review's risk items went in as separate commits. Cache is now
`thunderplus-v118`; `chains-worklet.js?v=20`, `waves-worklet.js?v=15`, `master-worklet.js?v=3`.

- **Effects can't be hung or silenced by a bad sample** (93d3ae3): the wasm only checks its output, and a NaN/∞ on the
  way in made the tape delay's `tfx_process` never return (frozen audio thread) and left twenty effects silent. The
  three JS call paths (track-FX worklet, …chains/…waves `effect()`, pad-FX `applyFx`) zero non-finite or |x| ≥ 8 input
  samples, and when an effect's output is no longer finite it is freed and made again with its settings (`s.t`/`s.p`
  kept on the slot). Checked in Node against the real wasm.
- **Replaced worklet nodes are released** (6ee130b): a processor that keeps returning `true` can never be collected, so
  each EQ-band/mastering change leaked a master chain (its stale meters flickered over the new one's), each chains
  on/off an engine node, each engine switch a bass/vox/acid/mach node. `{dispose:true}` / `op:'dispose'` make
  `process()` return `false`; `mstDrop()`, the chains rewire and `bassDrop()` disconnect the old nodes.
- **Transport** (837caca): the 25 ms pump ticks from a Worker (`pumpTimer`), so a MIDI-only song with the screen off or
  a muted master no longer stalls when the page timer is throttled; `audioInterrupted()` resumes a suspended/interrupted
  context on `statechange` and when the app returns, and stops the transport with a toast if it stays stopped 2.5 s.
  `pump()` already re-anchors the timeline when more than 0.5 s behind (from the dialog fix).
- **MIDI clock** (020b76b): ticks are booked ≤ 80 ms ahead (not the 250 ms look-ahead) and Stop is timestamped after the
  last booked tick (`mx.clk.last`), since Chrome can't take booked messages back. Fake-port test: none after Stop.
- **Samples and tabs** (67be903): `smpTidy` counts refs in the saved kit/song and skips entirely while another tab answers
  on `BroadcastChannel('thunderplus-tabs')`; a `storage` event from another tab toasts once. `smpGet` marks `own`, so
  samples borrowed from ...Thunder's store are copied into ours by `save()`; `thunderTidy()` removes `thunder-kit-v1` /
  `thunder-song-v1` once the import answer is yes/auto and everything is saved here with every sample in our store.
- **First PLAY and Space** (646904e): `tStart` sends un-cached Worker-capable pads to `bgRender` (3.7 s → 82 ms at 4×);
  `ensureLive` keeps a render at another sample rate while the Worker re-renders. Space reverb's Size is capped at 0.99
  (`SPACE_SIZE_MAX`, in the ENGINE block): at exactly 1 it never decays (5× the input, every Decay); 0.99 is fine.
- **Desktop/CI** (2ce6a3b): `will-navigate` blocks all navigation (dropped files), single-instance `return`, no `media`
  permission, page ignores drag/drop; workflow `contents: read` with `write` only on `release`; release notes explain
  SmartScreen / Gatekeeper; exact dependency versions. **Still to do for a real release:** commit a `package-lock.json`
  from the first real `npm install` and switch the workflow to `npm ci`; sign/notarise so electron-updater verifies.
- **Performance pass (15 Oct, afternoon), one commit each:** `draw()` measures layout only when a signature changed
  (bd93010: Song 19.7 → 4.6 ms a draw on the phone at 4×); Sect/Phrase light the playhead in place (09fecf1: Phrase
  22 → 5.2 ms); one `scheduleRefit()` for every shape change, grip drag ≤ 8 fps, frame loop at 10 Hz when idle
  (ddd1354); the two wasm modules are fetched from `waves/dsp/` instead of base64 in the page (index.html 3 MB → 1 MB;
  `tfxLoad` promise gates `tfxReady`/`chainsReady`; the service worker precaches with `cache:'reload'`).
- **Performance pass, part 2 (15 Oct, late afternoon), one commit each:** synth ↔ tracker is one layout pass with a
  cached character width and a per-render waveform envelope (5887b1d: 807 → 559 ms of main thread per round trip at
  4×, the rest is the browser laying out the shown page); no per-block garbage in the modular engines, the track-FX
  worklet and the Plaits/Elements voices (6020c92: a Plaits patch 123 → 28 scavenges / 40 000 blocks); released …waves
  voices sleep once their exit outputs (`m.exits`, set in `compile()`) have been under −100 dB for 0.25 s, and track-FX
  nodes sleep during playback unless the chain has a tempo-locked slot (cc5e5d4); the dynamic rainbow turns `--rbo`
  instead of filtering `<html>`, and ±1e-18 goes into every wasm effect against denormals (05cb255: Ladder on silence
  710 → 40 µs a block on x86; Clouds keeps a 3× idle cost from its own buffers).
- **The last of the review (15 Oct, evening):** Suppressor frame staged over the hop's four blocks, latency 2560
  (a9b4fc6: output bit-identical, p90 block cost 631 → 203 µs); load shedding steps back up after 8 s of calm and
  …waves' all-notes-off fires at most once per 10 s (1a3c7c4); …chains full telemetry only while its screen shows,
  'load' mode otherwise (12806cc, 28.5 → 3.5 msgs/s), which also found and fixed a crash when leaving …chains/…waves
  before the editor had started; cover-screen top bar on one row, Settings sheet under the keyboard, overscroll off
  (847a1bb). Not done: creating module states "off the audio thread" — a worklet's message handler runs on the
  rendering thread, so there is nowhere else to do it.
- **Hint line wraps** (15 Oct, evening): the tracker hint was one line with an ellipsis, cut off on every phone screen
  (it is how the PAD gestures are discovered). Now two lines (`.thint`, `-webkit-line-clamp:2`); a longer one gets
  `.more` and opens fully on a tap (`.open`, with a refit). Phone grid font and row count unchanged; desktop ~5 %
  smaller text on a 900 px window.
- **Owner's take on the UI suggestions (15 Oct):** the COMMANDS row and the two bottom play keys stay as they are — on
  touch, single-tap CLN↓/CUT/PST and pattern-vs-song play under the thumb are the point. The top "▷ Song" button stays
  too (the only play control when the key block is hidden; useful on desktop). Later, maybe: a PAD KEYS toggle in
  Settings → Screen (COMMANDS/KEYPAD/BOTTOM/SIDE already have one; the d-pad block in PAD mode doesn't), and a
  swipe-down to hide the lower block — with the caveat that it removes no-look navigation, so not a priority.
- **Preset browser (15 Oct, evening):** `presetSheet(kind)` — kinds `voice`, `layer` (the song's presets + factory
  groups, Recent, search, ▶ preview via `renderAsync` on a copy, ✕ delete, Save/Import) and `bass` / `blip` / `vox` /
  `x` (the engine cards' own lists; preview runs the card's preset function on a throwaway copy of the pad,
  `onVoiceCopy`). **To put the engine cards back on their native dropdowns set `ENGINE_PRESET_SHEET = false`** (one
  line next to `PRK`): the `<select>`s are still filled and wired; while it is true each one is hidden behind a
  `#…PreBtn` button (`.engpre`) that opens the sheet on click. `vPre` / `lPre` are buttons too; `fillPresetSel` is
  unused and can go once the sheet is settled.
- **Engine preset menus closed at once on touch (16 Oct; cache v136):** they opened on pointerdown over the select, so
  the tap's click landed on whichever preset row of the new sheet was under the finger (Piano / EP / Organ / Table
  lists reach the card header): it took that preset and closed. Now buttons that open on click, and both sheets
  ignore taps in their first 350 ms (`early()`). Checked with real taps at 412 and 884 wide. Cover-screen (≤480 px)
  engine card headers are a grid: name + full description, Random / Init beside them, Presets full width below.
  The engine card's footnote (`#xSecs>.note`) spans the grid with the sections' 12 px inset and a hairline above.
- **Tracker strip on the cover screen (16 Oct; cache v137):** the tempo / ROWS / LOOP / TRANSP strip under the tabs was
  419 px on a 369 px screen (TRANSP + off the edge). `patFit()` (ResizeObserver on `.ttabbar`, and on input-mode
  change) measures it: if it doesn't fit, PAD mode hides `#tSelBar` (the LOOP and SEMI −/+ keys do the same) and KEYS
  mode hides TRANSP −/+ (top bar's TRANSPOSE ▲▼). ≤600 px the strip is its own line and scrolls if still too wide.
- **ARRANGE zoom (16 Oct; cache v138):** blocks were ~12×16 px on the unfolded Fold (25×22 at the grip's maximum),
  too small to hold on purpose. `arrZoom` 1–3 (`thunderplus-arrange-zoom-v1`); `arrSize()` (called by `drawArr`) sets
  `--arcols` to fixed px columns when zoomed, so `.ar` rows run wide and `#tArrGrid` scrolls sideways; `arrHSync()`
  keeps `#tArrTh` / `#tArrGp` (overflow hidden) at the same scrollLeft; row numbers and the header corner are sticky
  (in the self-scrolling header rows sticky sits inside their padding, hence `left:0` there). Pinch = touch events
  (2 touches; a second finger cancels the hold-to-mute press, `arrPtrs`), Ctrl+wheel on desktop, and the header's
  − ○ + (`ARR_ZOOM_BTNS = true`; false removes them, the owner may not keep them). `arrFollowX()` keeps the cursor's
  block in view when the cursor changes track while zoomed; a pinch leaves the view where it was.
  Header (cache v140): a 3-column grid — `.tahl` (− ARRANGE), ○, `.tahr` (rows / muted / zoom line, +). ○ is centred
  on `--azfx`, the MONO | POLY break at fit size (`arrSize`), so it stays put while zoomed; it has a 44×32 target.
  The − ○ + are boxless 24 px dim glyphs (`#tArr .tahd .azb`, the ID beats the skins' button rules).
- **Three more rendered engines (15 Oct, night; cache v130):** `TableCore` (wavetable: four 8-frame tables drawn in
  the time domain, taken apart once with a DFT and rebuilt band-limited per note; Position/Warp, position envelope +
  LFO, unison, sub, noise, SVF low-pass with envelope, ADSR), `BreathCore` (coloured noise → grains → a bank of up to
  8 resonant band-passes with Spacing/Metal/Ring → HP/LP with envelope, Sweep for risers, Wind random walk; hats,
  shakers, cymbals, risers, wind, static) and `StringsCore` (plucked-string loops with a damping low-pass and a
  fractional tap — the tap is pulled in by the low-pass's delay so tuning holds —, pick/strike-point excitation,
  chord voicings strummed up or down, 4-mode body sized mandolin→cello, release damping). Same shape as the keys
  engines: `XENG` / `renderX` / `XUI` (`table`, `breath`, `strings`), engine grid is now an exact 4×4, factory
  voice groups "Table" / "Breath" / "Strings". All original code. Checked in Node (no NaN, pitch within ~1 %,
  20–230 ms a render) and headless (cards build, every preset renders, sheet lists them). Not heard yet.
- **Six more engines (15 Oct, late; cache v131):** `FmDrumCore` (3-op FM percussion, three algorithms, pitch/index
  envelopes, feedback, noise, click, curve), `WindCore` (blown/bowed waveguides: Reed = single reed on a half-length
  bore with the reed curve 0.7 − slope·Δp; Jet = jet delay + bore with the x(x²−1) jet curve, bore at the full period
  and a dark bore so it sits on its fundamental; Bow = bridge/neck delays with the (|Δv·slope|+0.75)⁻⁴ friction curve.
  Each model's controls are mapped into the narrow region where it oscillates (reed: pressure·slope ≈ 0.22–0.29;
  jet: pressure·gain ≈ 1.05–1.15 with jet ratio 0.38–0.48; bow: velocity·slope ≈ 0.48–0.58) — found by sweeping in
  Node, see the matrix test — and a self-trim runs the loop for 0.5 s, autocorrelates near the asked period (±16 %
  only, so an octave jump is left alone) and scales the delays), `PhaseCore` (CZ-style phase distortion, 8 waves incl.
  three windowed-resonance ones, DCW ADSR, two oscillators, ring, 2× oversampled), `PartialsCore` (up to 256
  harmonics on the KeysUtil bank; tilt/odd-even/formant/stretch, spectrum B morph, per-partial decay tilt, brightness
  envelope, drift), `SkinCore` (circular-membrane modes at the Bessel zeros, weighted by J_m(z·r) at the strike point,
  mallet hardness roll-off, tension pitch bend, damping/muffle, a shell resonance), `ChipCore` (frame-rate 60 fps
  8-bit voice: pulse duties + sweep, 4-bit triangle, 15-bit LFSR long/short, arp chords, slide, stepped vibrato,
  NES-style period quantisation). Engine grid is 22 cells now (room for two more makes 6×4). Same checks as the
  previous three. Original code throughout (the physical models follow the textbook waveguide structures, written
  from scratch).
- **Engine picker is a sheet (15 Oct, late; cache v133):** `engineSheet()` — groups (`ENGINE_GROUPS`: Layers, Drums,
  Keys, Synths, Strings & wind, Voice & bass, Fonts) with a line about each engine, search, the current one marked;
  the pad's engine row shows one button (`#vEngBtn`, "Strings ▾"). **Set `ENGINE_SHEET = false` to get the 4-column
  grid of engine buttons back** (the `makeSeg` code is still there). A new engine goes in `ENGINE_GROUPS` as well as
  `XENG`/`XUI`. The preset and engine sheets are now centred on every screen size (same gap above and below) instead
  of a bottom sheet on phones.
- **Voice card in groups (15 Oct, late; cache v135):** `#voiceCard` has four `.secgrp` blocks with a small caps
  heading — Voice (pitch, low-pass, amp, play), Envelopes (Envelope 1/2/3, each its own section), LFOs & modulation
  (LFOs, Sequence, Matrix), Tuning (song, two columns C–F / F♯–B) — hosts `voiceSecs`, `voiceEnvSecs`,
  `voiceModSecs`, `voiceTuneSecs`. Hairlines rule the sections off inside each group. Tuning is song-wide, not per
  pad; it lives at the bottom for now, Settings → Song would be its proper home if the card gets crowded.
- **Nothing from the review is left.** Next: hear it on the Fold and the PC (nothing has been checked by ear), then the
  feature list below.

## 15 Oct: review fixes (branch fix/review-bugs, merged as PR #1)

A code review (audio, storage, UI, DSP) found these; all fixed and checked headless. Cache `thunderplus-v98`;
`chains-worklet.js?v=17`, `waves-worklet.js?v=13`.

- **SoundFont pads were silent:** in `sfNote` the `f.connect(pn); pn.connect(bus)` had been swallowed by the "+6 dB"
  comment appended to the same line, so no SoundFont voice reached a track. One-line fix.
- **VCF (…chains and …waves, outside the shared block, so edited in both worklets):** the old core mixed a Chamberlin
  `hp` with the TPT `2·v − ic` update, doubling the integrator step; it ran away above cutoff ≈ 0.45 at the default
  resonance (5 000× full scale, resetting 200×/s). Now the TPT SVF Blades uses, same `q = 1 − res·0.95` mapping; checked
  at default, min and max cutoff/res in all three modes: peak ≤ 0.93 on a 0.5 input, no NaN.
- **Storage full was silent:** `storeWrite()` (before `save()`) is the one path for the song and kit writes. A failed
  write shows a red banner (`#storeB`, `.updb.storeb`) that exports the project on tap; it clears when a write succeeds.
  `navigator.storage.persist()` is requested at start-up (next to `hadSW`) so Android doesn't evict IndexedDB.
- **Imported presets kept base64 audio inside the song** (3.9 MB per 30 s sample in every autosave and undo step; two
  imports filled localStorage). `presetsByRef()` stores the audio in IndexedDB and keeps `{ref}`, like in-app presets;
  used by Import presets, by project files that carry preset audio, and once at start-up for songs saved before this.
  `smpHeld` (Map id → sample, every sample seen this session) backs `smpById`, `fullSmp` and undo. `presetSmpFill()`
  (awaited by Save/Share project and Export presets) fetches preset samples no longer on a pad, and `projBlob()` now
  embeds preset audio (`fullPresets()`), so presets travel between devices.
- **Kit undo by reference:** `vSnap(v)` = `voiceToJSON(v, 'ref')` with the sample objects parked in `smpHeld`; restore
  refills from there (else `resolveSamples()`). Was 3.7 MB and ~100 ms per settled tweak with a 30 s sample.
- **No more `prompt()`/`confirm()`:** `askText(msg, default, max)` and `askYes(msg, okLabel)` (`askSheet`, after
  `toast()`, CSS `.ask`) are promise-based in-page sheets: Electron has no `prompt()` (six features did nothing in the
  desktop build), and a native dialog froze the 25 ms pump so every missed note played at once when it closed. All ten
  call sites are now async. `pump()` also re-anchors the sequencer (`s.base`, `s.time`, `s.btT`) when it finds itself
  more than 0.5 s behind the audio clock (a frozen tab, a system dialog), so the song continues instead of bursting.
- **…waves pads rendered cut off on slow devices:** `wavesPre` now passes `costCeiling: Infinity`; the engine's wall-clock
  load shedding (cut voices → draft → all notes off) belongs to the live editor, not an offline render.
- **A tap that closed Settings / File / Phrases also pressed what was under it** (d-pad, pads) on wide screens:
  `swallowTap(e)` in the three capture-phase closers stops the pointerdown and the pointerup/click of that tap
  (propagation only — cancelling touchend would cancel the click). Checked with real taps at 884×1104.

## How to validate (there is no test suite)

1. **Syntax:** `node --check` the worklets; for the single-file apps, extract each inline `<script>` and
   `node --check` it (`type="module"` ones as `.mjs`).
2. **…chains params match:** eval the worklet (`(0,eval)(src)` exposes `globalThis.ChainsDSP`) and the
   editor's `const MODULES = {…}` block, then compare every module's `params`. The only expected
   "mismatch" is `track`'s max, which the editor writes as `CHAN_COUNT`.
3. **Effects in Node:** instantiate `tfx.wasm` with a stub WASI, `tfx_new(id, 48000)`, write 12 params at
   `tfx_params(h)`, fill `tfx_l()`/`tfx_r()` and call `tfx_process(h, 128)`. Check for sound, no NaNs, CPU
   and `memory.buffer` growth. Drive them through the worklet's own maps (`ChainsDSP.MODULES[k].process`
   after `wasmInit({tfxBytes})`) at default, min, max and random settings: raw 0–1 params give false alarms
   (Rings' voices, for one, must be 1/2/4).
4. **In the page:** headless Chromium (Playwright is installed) with a copy of `index.html` that adds
   `window.__T = {…}` before the main script's closing `})();`. That gives you `S`, `FX`, `freshFx`,
   `applyFx`, `pass`, `makeSeq`, `applyChop` and so on. Never commit that copy.
5. **Worker matches:** in that copy, compare `renderVoice(v, sr, null, hold)` with
   `(await renderAsync(v, sr, hold)).data` for every TYPES oscillator and FX key (they should be identical).
   For responsiveness, use CDP `Emulation.setCPUThrottlingRate` 4 and a `longtask` PerformanceObserver.

## Open items / where to pick up

1. **Hear it on the Fold.** Everything above is verified headless, not by ear on a phone. Worth checking:
   - CPU with Space reverb and Cross-mod on several tracks.
   - The trance gate landing on the downbeat.
   - STRETCH and LOOP on a real break.
   - Touch on the new INST rows.
2. **Long synth renders:** a synth slot renders its whole Max len (6–8 s, up to 30 s) each time. It's now in
   the background, but a phone still takes a second or more before an edit is heard. Rendering in pieces
   (the first second first) would shorten that.
3. **…waves editor** doesn't list the new effects (the shared wasm already has them). Adding them means
   mirroring the …chains editor entries in `waves/index.html` and `waves/waves-worklet.js`, which come from
   the separate `matthettich/waves` repo.
4. **Naming:** the older effects/oscillators still use product names (Plaits, Clouds, Rings, Elements, Beads,
   FM Genesis, SoundBlaster). Rename them before any commercial release, keeping "based on" credits.
5. **More effect ideas** (permissive only): more Airwindows (hundreds, all MIT). (Spectral freeze, granular delay and the
   sidechain envelope filter are in since 16 Oct.) A Load IR… right on the Convolution reverb's card (today: Settings →
   Song) and running Resynth's analysis in the Worker are the obvious next steps for the 16 Oct additions.
6. **Still left from the earlier mobile pass:** incremental playhead painting in `draw()`/`drawArr()`
   (`draw()` costs ~13 ms at 4× even when nothing changed, because it rebuilds the grid string to compare).
7. **On the phone:** the owner should install ...Thunder Plus from its page and remove the old ...Thunder icon
   (the old one now just forwards). Check that the offer to bring Thunder's work over appears there.

### Standard DAW features still missing (suggested 14 Oct, in rough priority order)

Already there, for reference: undo/redo, stem export, sidechain, FX automation from tracker commands, sample
recording, resampling, MIDI in/out and clock.

1. **Metronome and count-in.** No click exists yet. Also useful for checking clock-in lock against the MPC.
   About half a day.
2. **MIDI file export and import.** Export a song as `.mid` (to finish in a DAW or load on the MPC/Force);
   import MIDI drum patterns or melodies into phrases. About a day each way.
3. **The rest of MIDI out:** pitch bend, arps as notes, CC commands, program change and ports A–D are in (18 Oct).
   The LFO → CC and Song Position Pointer (both ways) followed the same evening. Ideas after real-gear tests: recording
   MIDI in into phrases, live transpose from a keyboard, force-to-scale, play direction, Euclidean fill, MIDI echo.
4. **Humanize.** Small random timing and velocity changes per track (GRV and swing exist; random variation
   doesn't). A few hours.
5. **Named song markers/sections**, to jump around a long arrangement. A few hours.

Deliberately left for later:
- **Audio tracks** (recording vocals or guitar across the song): turns Thunder into a different kind of app;
  better with the native port.
- **Time signatures other than 4/4:** a deep change for a tracker; odd meters already work through phrase length.
- **Plugin delay compensation and an automation-lane editor:** only matter once VSTs exist.

## Conventions worth knowing

- **Ports/cables:** audio = teal, `cv`/modulation = purple dashed, `gate` = pink dashed. Any slider
  has a purple modulation jack; a cable into `p:<param>` modulates it.
- **Adding a …chains module:**
  1. Define it in the worklet and register it in `MODULES`.
  2. Mirror it in the editor's `MODULES` table with identical `params`, plus `ui`.
  3. Add a `PALETTE` entry and help text in `ABOUT`.
  4. Bump `chains-worklet.js?v=` and the `sw.js` cache.
  5. Use `fx:true` only for `tfx.wasm` effects.
- **`README.md`:** the top part is Thunder Plus's; the rest is …Thunder's (the base app).
- **Two engines:** `waves/waves-worklet.js` (from the waves repo) and `chains/chains-worklet.js`.
- **Mobile vs Desktop:** anything that should differ uses `isMobile()` in the page; there is no second build.
- **Engine block:** code that renders pads goes between the `===ENGINE===` markers, with no DOM, `window` or
  `localStorage` at top level, because the render Worker runs that block as-is.

## Bass engine (13 Oct, afternoon)

- **What:** synth slots can set `V.engine = 'bass'` (Synth → Engine). Settings are in `V.bass` (`normBass`); the
  voice's `tune`, `drive` and `level` are folded in by `bassParams(V)`.
- **DSP:** `class BassCore` in the ENGINE block: additive (≤64 partials, sine table), spectral 24 dB resonant
  low-pass, two vowel formants, comb, Reese second set, sub, tempo-synced wobble (`BASS_DIV`), ADSR, glide.
  Self-contained on purpose: the live AudioWorklet is a Blob built from `BassCore.toString() + BASS_PROC`.
- **Live/offline:** `makePlayer` makes one `thunder-bass` node per track + pad (`bassNode`), and every note is
  AudioParam automation (`rate`, `gate`, `trig` counter, `leg`, `bpm`, `wob`), so offline renders are exact.
  `pass()` awaits `bassReady(oc)` when the song uses a bass pad. Pads, the waveform and the Worker use
  `renderBass()` (one held note).
- **Tracker:** `WOB` command (last in `FXC`): 01–0D pick `BASS_DIV`, 00 = the voice's rate. Mixer's master row
  has a MASTERING cell (`cs.mixer.mx === 1`).

## Desktop (Electron) build (13 Oct, afternoon)

- `electron/`: `main.js` (app:// protocol, permissions, single instance), `copy-app.js` (copies the app and
  fails if a file index.html loads is missing; add new runtime files to `PARTS`), `package.json` (Electron 44,
  electron-builder 26, unsigned, `--publish never`; product name "Thunder Plus" without the dots so the .app
  isn't a hidden file on macOS).
- `.github/workflows/desktop.yml` builds Windows/Mac/Linux on demand or on a `v*` tag.
- In the page, `DESKTOP_APP` (protocol `app:`) turns off the web update check and shows a note under App.
- Not built or run in the cloud session (no npm registry access there): first real run is the owner's.
- Auto-update: `electron/updater.js` (electron-updater, GitHub provider `matthettich/thunder-plus`). The workflow's
  `release` job (tags `vX.Y.Z` only) runs `gh release create` with every platform's files incl. `latest*.yml`.
