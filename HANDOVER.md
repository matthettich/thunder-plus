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

## Desktop releases

- **v1.2.34** (31 Oct 2026, UTC; Run workflow through the API with version 1.2.34, from `3c992cb`) has build 294: the …pump
  creative effects, MASTER and the …chains pump modules ("30 Oct (26)" below). All three platforms built.
- **v1.2.26** (27 Oct 2026; Run workflow through the API with version 1.2.26) has the gain staging (TRIM, master OUT, PRE
  sends, the CHN crossfade) and its review fixes: "27 Oct (6)" below, build 265.
- **v1.2.22** (24 Oct 2026; Run workflow through the API with version 1.2.22) has everything in "24 Oct" below: the Windows
  Arrange-box and …pump-meter fixes, the reverb Mix curve, mouse on values, the mixer fader bars and the header meter.
- **v1.2.20** (24 Oct 2026, UTC; Run workflow through the API with version 1.2.20, from `0e73a86`) has everything up to
  that commit: the LXR-02 engines (`lxr/`), the …pump bus and master effects (`pump/`), the grouped FX column picker,
  ROWS → PATTERN LEN, LPB 8 for new songs and the phrase cursor wrap. All three platforms built; installed copies update
  themselves from it. Releasing from a session: `gh api -X POST repos/matthettich/thunder-plus/actions/workflows/desktop.yml/dispatches -f ref=main -f 'inputs[version]=X.Y.Z'`
  (sessions can push branches but not tags), next version after the newest on the Releases page.

## Before a commercial release (standing checklist: keep it current, read it before selling)

Owner (23 Oct): leaning away from selling Thunder Plus, but may change their mind. Keep a commercial build a short,
mechanical job: everything it would need is listed here, and new work must not add to it without a line below. This
is housekeeping, not legal advice: have a lawyer look before selling.

**Today:** Thunder Plus itself has no licence (no LICENSE file; `electron/package.json` says `"license": "UNLICENSED"`),
so it is all rights reserved, though the repo is public. All third-party code is MIT / BSD / ISC / zlib / Apache, fonts
are OFL, the GM SoundFont is MIT, **except `lxr/` (GPL-3.0)**.

1. **GPL: `lxr/` only.** To ship closed: `git rm -r lxr` (optionally also the `<script src="lxr/lxr-engine.js…">` line in
   `index.html`; every other LXR hook is guarded by `typeof LxrCore`, `sw.js` caches it as OPTIONAL, and
   `electron/copy-app.js` copies it only when present). Pads on a Firmware engine then open as Layers, so render any that
   matter to samples first (Render a part to a slot, or Export this pad). Check: no "LXR-02 firmware" engine group, no
   File ▾ → Import LXR kit, no LXR errors besides the script's 404. The other way is to release under GPL-3.0 (selling
   is allowed; the source must be offered; buyers may share it). The plugin / mobile plan
   (`docs/ENGINE-PLUGIN-PLAN.md`) must leave `lxr/` out of any closed build.
2. **The rule that keeps this list short.** Only permissive code goes into the app itself. Anything with limits (GPL,
   LGPL, non-commercial, attribution-only-with-conditions…) goes in its own folder the way `lxr/` does: its own LICENSE
   and NOTICE, loaded optionally, every hook in the app guarded, OPTIONAL in `sw.js`, copied-if-present by
   `copy-app.js`, a section in `THIRD_PARTY_NOTICES.md`, and a line in item 1. Never non-commercial code at all (the
   original Sonic Potions LXR firmware is non-commercial; the LXR-02 code in `lxr/` is Erica's GPL release).
3. **Names in the UI** (trademarks; a "based on X" credit can stay):
   - layer / oscillator names `Plaits`, `Elements`, `FM Genesis`, `FM SoundBlaster` (the oscillator table near the top
     of the engine block); pad effects `Clouds`, `Rings`, `Beads`; the `Plaits` section title. Later additions already
     use generic names (Cross-mod, Space reverb, Lo-fi…) with credits in `FX_CREDIT`.
   - drum kits and presets: "CR-78- / 808- / 606- / 909- / 707- / 727- / 505-style" (and the one "Roland" in the kit
     notes), "(808-ish)", "(Nord-ish)", "House organ (M1 style)", and the "MPC swing" labels.
   - help and README comparisons (Renoise, M8, SunVox, Koala, Ableton) only describe; reword if needed.
   - the …waves and …chains editors (`waves/`, `chains/`, also from `matthettich/waves`) carry the same Mutable names.
   - LXR names go with `lxr/`; if it stays, keep them truthful ("from the LXR-02 firmware"), as its NOTICE requires.
   - Find them: `grep -n -E "Plaits|Elements|Clouds|Rings|Beads|Genesis|SoundBlaster|Roland|CR-78|808|909|606|707|727|505|Nord|M1 style|MPC" index.html chains/index.html waves/index.html README.md`
   - check that "Thunder Plus" / "...Thunder" is free to use as a product name where it would be sold.
4. **Notices that must ship** (in place; keep them current): `THIRD_PARTY_NOTICES.md` (the MIT texts and credits, and pffft's
   BSD-style notice for `pump/pump.wasm`, which is otherwise the owner's own …pump code: nothing to remove for a closed build; it is
   copied into the desktop app), `fonts/OFL-*.txt`, `sounds/FluidR3Mono_License.md`, and Electron's own licence files
   (electron-builder adds them). The credits shown in the app (`FX_CREDIT`, the effect help) stay.
5. **Thunder Plus's own terms:** pick a licence or EULA, set `"license"` in `electron/package.json`, and decide whether
   the repo stays public. Code signing (Windows, Mac notarisation) is separate from licensing.
6. **Plugins and mobile, if they happen** (`docs/ENGINE-PLUGIN-PLAN.md`): iPlug2 is permissive; JUCE needs a paid licence
   (or the AGPL); the VST3 SDK is MIT since 3.8, but the VST name and logo have Steinberg's own terms; check the app
   stores' terms.

## 31 Oct (2): pop-up windows for pump MASTER, GATE, TUNE and CHOP; redo fix (build 296)

- Owner: "anything that needs a pop out should get one". Picked: MASTER (what it decided), GATE (step grid), TUNE (Graph
  mode is unusable without one), CHOP (pads). Not STUTTER (owner may drop it; its custom pattern is plugin state with no
  editor here). The other ten work from rows.
- **pump `7225807`:** `RunnerExtra<P>` in each `wasm/fx/<name>.cpp` gives the window's feeds (`kTel` floats after the common
  five; layouts in those files' comments), rings, commands (`pump_hold` kind 3) and momentary parameters (kind 4: CHOP's
  pad triggers, never saved). `pump_tel_len` per type from `pumpfx_tel_<name>()`. tel buffer 4096.
- **Slot state:** `f.pst` = the plugin's own text (MASTER `analysis`, TUNE `notes` + `curve`; `PUMP_STATES`). `PumpFX.getState /
  setState` (one undo step; `{quiet:true}` saves without one, for a capture's curve). Keys in `PUMP_STATE_UI` (curve) stay out
  of the worklet; `PUMP_STATE_OUT` (analysis) are polled back. Build 294's `f.ana` still loads into `pst.analysis`.
- **Windows** `pump/ui/master.js`, `gate.js`, `tune.js`, `chop.js` (ports of each `UI<Name>.cpp`, with narrow layouts);
  accents added to `kit.js` (v5). OPEN shows for `PUMP_WIN` + MASTER. Each file works around a few kit gaps inside itself
  (MASTER: per-control rainbow tint, peak meters; GATE: long-press-then-drag sets `ui.bodyActive`; CHOP: its own capture
  pointer listener for multi-touch pads; TUNE: note finder and text formats ported from TuneNotes.hpp). Checked headless,
  desktop and phone, light and dark: screenshots in the session scratchpad `win-*/`. **Not tried by ear or on the Fold.**
- **Redo fix:** after an undo, `undo.cur` is now `JSON.stringify(S)` (normSong added `fxPre: {}` the stored text lacked, so the
  next step looked like a new edit and cleared the redo history). Checked: set, set, undo, undo, redo, redo round-trips.
- Known: TUNE's capture of a synth shows short blips at note attacks (the pitch detector); with NOTES FROM holding a CHOP pad
  from PLAY, the frozen bar can be the silent first one (the plugin does the same).

## 31 Oct: pump VOX trimmed (build 295)

- Owner: trim VOX (keep it); STUTTER may be dropped later (not yet). `PUMP_BOX.vox` is a hand list of 23 rows (+ SC FROM,
  NOTES FROM); `ROWS.vox` in `tools/pump-chains.mjs` the same for the …chains module, in that order. Hidden parameters stay in
  `f.pump` at their values and presets still set them; in …chains they sit at the plugin's defaults. `chains-worklet.js?v=41`;
  two pvox golden hashes moved (the hidden knobs now take the plugin's exact defaults instead of a curve round trip).

## 30 Oct (26): …pump creative effects, MASTER and …chains modules (build 294)

Owner: port pump's effects Thunder Plus didn't have, each its own thing (keep the old reverbs and delays), into …chains
where they fit, MASTER last; hold off on AMP (note below); ROOM and the converter not wanted. Worked unattended: calls
marked *(guess)* are for the owner to confirm.

- **pump side (matthettich/pump `ab079e4`, `2b86cfe`):** the creative plugins needed no DSP split. `wasm/shim/DistrhoPlugin.hpp`
  stands in for DPF so each plugin's own `Plugin<Name>.hpp` compiles unchanged; one translation unit per plugin
  (`wasm/fx/<name>.cpp`: the plugins' `using namespace` lines would clash in one), `wasm/fx/Runner.hpp` the interface,
  `HostShim` in `pump_wasm.cpp` hosts them. Types 9–23 after the nine: tube snap gate echo spring verb grain vox tune
  crush swirl stutter chop twang master. New exports `pump_midi`, `pump_transport`, `pump_text`, `pump_state_get/set`;
  MASTER's LEARN is `pump_hold` kind 3 (1 start, 2 finish, 3 cancel); telemetry for these is the common five values
  (MASTER adds learning / learned / LUFS). wasi has no threads: STUTTER's mutex is a no-op stand-in; MASTER's one
  try/catch reads as plain blocks (parseAnalysis never throws). pump.wasm 1.5 MB (was 0.58). CPU per instance (node,
  48 kHz): 0.4–5 % of a core. Not checked against the native plugins sample for sample (there's no parity test for the
  shim path yet: `tests/parity.cpp` covers the nine); the smoke tests (`wsmoke.mjs` in the session scratchpad) run all 24
  finite.
- **Track effects:** `PUMP_CREATIVE` (14) go on **every track** (`pumpAnyTrack`), MASTER on buses / master only. Each in the
  picker group of its kind (`PUMP_GROUP`) *(guess: not one …pump group)*. FX rows: every parameter a preset may set, in
  the plugin's order (`PUMP_BOX` filled in automatically for types without a hand list; STUTTER has ~80 rows, VOX ~60).
  No **› OPEN** for them (no windows yet). Transport: the worklet works out the song position from the beat clock
  (`this.ppq`) and calls `pump_transport` per block; GATE / STUTTER / CHOP keep the node awake on the clock (`clk`).
- **NOTES FROM** (`f.nin`: 0 off, 1 + t = track t) on TUNE, VOX, TWANG, STUTTER, CHOP *(guess: off by default, any track's
  notes; STUTTER has no MIDI switch of its own and plays moves on C–G)*. `pmNotes` in the player sends note on / off as
  `{pmidi:[[slot, time, status, d1, d2]…]}`; the worklet holds them until their block (sample-timed). **Offline renders
  don't deliver worklet messages until the end**, so there `pmPost` suspends the render at the note's block (sharing
  `tempoAt` with the tempo changes, suspend at 0 allowed). Notes keep Thunder's numbering (C-4 = MIDI 60), so CHOP's pad 1
  (MIDI 48) is Thunder's C-3.
- **MASTER:** rows › LEARN (shows the learned LUFS), › FINISH LEARN, › CANCEL LEARN, › FORGET. The analysis text lives in
  the slot (`f.ana`, saved with the song), goes to the plugin with the chain (`ana`), and the worklet polls it about
  twice a second (`pumpStateGet`, ASCII by hand: no TextDecoder in a worklet) and posts `{pana}` when it changes;
  `PumpFX._ana` stores it (no undo step of its own). Checked live: LEARN 5 s of the demo → `-15.3 LUFS`, renders use it.
- **…chains:** `tools/pump-chains.mjs` writes `===PUMP MODULES===` (chains-worklet.js) and `===PUMP EDITOR===` /
  `===PUMP PALETTE===` (chains/index.html) from pump-meta.js: keys `ptube` … `ptwang` (13; **CHOP left out**: its pads need
  notes). Curved knobs (Hz, ms) are 0–1 on the plugin's curve; choices and whole numbers are the plugin's values. **pump
  VOX starts on Split L/R** (In L carrier, In R voice: the owner's ask). The bytes reach the chains worklet as
  processorOptions `pump`, or later by `op:'pump'` (`pumpToChains`); the standalone page fetches `../pump/pump.wasm`.
  Not in …waves (no shared block). Big modules are tall (VOX shows all ~60 rows; fold it by its title).
  `chains-worklet.js?v=40`; tests feed pump.wasm to the chains tests (39 new golden hashes, none changed).
- **Next if wanted:** pop-up windows for the fifteen (one `pump/ui/<type>.js` each, as the nine; the shim host would
  need their feeds: rings, meters); a curated, shorter row list for STUTTER / VOX / CHOP; FXV for pump slots; a parity
  test for the shim types; CHOP in …chains with a gate / pitch input.
- **AMP (held, owner 30 Oct):** pump AMP loads .nam models and cab IRs from files (`ModelGuard`, `NamJson`,
  `nam_unity.cpp`: the NAM runtime). For Thunder Plus it needs: the NAM code built for wasm (no threads; check its
  licence goes in THIRD_PARTY_NOTICES), the model and IR files carried in the song or the kit (as the convolution
  reverb's IRs are: `globalThis.__thunderIR`), and a file picker on the FX screen. Probably a …chains module too.

## 30 Oct (25): THREE DIGITS: sects and phrases 000–FFF (build 293)

- Owner ran out of unique sects/phrases in Arrange (one 256 pool shared by every track). `NPH = 4096` (next to NT):
  `S.chains`, `S.phrases`, `S.plen`, `S.plpb` are NPH long; the loader (`normSong`) reads up to NPH and clamps song
  cells and sect-row phrases to NPH − 1; `freeSect` / `freePhrase` / `freePair` / `sectForPhrase` / break placement /
  the Phrases list / `pvUses` / the ±16 steps and `cSect` / `cPhrase` clamps all use NPH. Song ROWS are still 256
  (every remaining `256` is a row count).
- Display: `hx(v, 3)` now masks 4095; `hp(v)` = 3-digit id, `h3(v)` = `---` for empty. The Song text grid shows 3 digits
  (headers padded, `trackWin(c, lab, extra, cw)` takes 4 chars a column there), Sect's PH column, Arrange (main and side,
  label font now also fits the block width: `--arf` ≤ width / 2, `.arnolab` hides labels below 7 px), the strip, Pattern
  view headers, titles, status lines and toasts.
- Typing: `typeHex` takes three digits for kinds `sect` (song cells) and `phrase` (sect column), mask 4095.
- Compatibility: older builds (and Thunder Perform, if it slices to 256) drop blocks numbered above 0FF from a song saved
  by 293+. Songs from older builds open unchanged.
- Undo snapshots grew a little (plen/plpb 4096 long, ~2 ms per JSON at 220 blocks).

## 30 Oct (24): first Shift-click anchors on the clicked cell (build 292)

- `sxEnd` Shift-click: with no selection, the anchor is now the clicked cell (it was `cs.song`, the cursor; beside Phrase
  that's the edited phrase's block, so Shift-clicking rows 0 and 1 selected 0–3). The next Shift-click extends as before.

## 30 Oct (23): Ctrl/⌘+D with picked blocks (build 291)

- Owner: "Cmd+D on a selection didn't work". Shift-click, Shift-drag and Shift+arrow selections all duplicated fine
  (checked headless); Cmd/Ctrl-click picks (`sxPicks`) were ignored, so only the cursor's row went in. `dupPattern` now
  takes the rows from the first pick to the last (whole rows, every track + master) and moves the picks onto the copy.

## 30 Oct (22): Shift+Option from the Mixer, PAD-mode keyboard selections (build 290)

- Mixer Shift+Alt+▲▼ now go to `shiftAltArrow` like every other non-Phrase screen; `mixMaster` / `mixBack` are gone
  (the master row is reached with ▼ past the VOL fader, build 289).
- PAD input mode with a computer keyboard: plain arrow keys clear `selA` when Shift isn't held and no OPTION / EDIT is held
  or latched (the keydown path before `press(b, true)`), so a selection only grows while Shift is down. KEYS mode already
  did this (SELECT RELEASE, 24 Oct). The on-screen pad buttons keep their one-finger selections.
- Ctrl/⌘+D with a Song / Arrange selection already duplicates the selected rows (every track + master); checked headless.
- Help: the Shift+Alt row describes the one-cell phrase stepping and the Mixer; the mixing help mentions the VOL fader keys.

## 30 Oct (21): Mixer VOL fader from the keyboard (build 289)

- Since GAIN STAGING split TRIM (row 0) from VOL (the `.mfbar` on each meter), VOL had no keyboard route. Now the
  Mixer cursor has a sub-state `cs.mixer.fv` on row 6 (`mixFv()` = fv && y === 6): ▼ from SC sets it, ▼ again goes to
  the master row, ▲ from the master row comes back to it, ▲ from it goes to SC. Kept as a flag rather than a new row so
  every `c.y === 7` master check stays as it was.
- On the fader: EDIT/Ctrl/⌘+◀▶ ±1, ▲▼ ±16 (the mixer value branch), hex typing (`typeHex`), Delete → E0 (TRK_UNITY),
  Enter toasts the value, the status line shows VOL. The bar gets `.cur`; the SC cell isn't lit. Clicking a fader
  selects it; any other grid click clears fv.

## 30 Oct (20): pad FX paging, stuck slider tip (build 288)

- Pad / synth effect slots (`renderFx`) get ‹ › buttons either side of the name (`.fxpg`): they swap that slot for the
  previous / next effect in the + Add effect menu's order (`fxOrder()`), fresh defaults, On state kept. The name is
  focusable; ← → on it or on the arrows page too.
- The ONE SLIDER drag's `end` now also listens on the window (capture) and for `lostpointercapture`: a slider redrawn
  mid-drag never got its pointerup, leaving the value tip ("0%") stuck. `sliderTip` hides for a detached slider, and on
  window blur.
- Owner asked whether Shift+Option+arrows could drive Arrange from the Mixer / FX screens: they already do (the key
  routing sends every screen but Phrase to `shiftAltArrow`), except Mixer ▲▼, which is the master-row jump (`mixMaster`).

## 30 Oct (19): Shift+Option+arrows into empty cells (build 287)

- `phraseGrid` (phrase editor, PATTERN NAV on, the Arrange panel) now steps one song cell per press, empty cells included,
  like Pattern view's `pvGo`: a filled cell opens its first phrase; an empty one takes `freePhrase()` and sets `pendPlace`,
  so the phrase lands on the song with its first note (`placePending`). It also runs while you sit on such a pending cell.
  It no longer toasts "No phrase above on this track." etc.; at the grid's edge it stays put.
- `shiftAltArrow` and `songGridDir` (Song screen) still jump to the nearest block, but with none that way they step one
  cell into the empty one instead of doing nothing / toasting.

## 30 Oct (18): master column copy / paste (build 286)

- `copySel` on the Song copied columns x0..x1 by number, so a selection reaching the master (column 32) also took the unused
  columns between the last track and it, and `paste` / `pasteMix` dropped anything at or past `dims('song').w` (= ntr()),
  the master included. Song clips are now by screen column (`sxCols` / `sxU`: the tracks in use, then the master) and
  paste maps back the same way. v1.2.32 (desktop) has builds up to 285.

## 30 Oct (17): PLAY keeps the cursor's track (build 285)

- `followPlay` took the first playing track when the cursor's track wasn't playing on the row, and `pvFollow` moved
  `cTrack` to it (a bus with no block there jumped to track 1). Now the cursor keeps its track and column: the phrase
  editor stays put, Pattern view only rides the rows (and takes the cursor track's phrase on a new song row if it has one).

## 30 Oct (16): cursor on the playhead (build 284)

- With FOLLOW the cursor rides the playing row; `.ln.plr span.cur` used the screen's background colour, so in Pattern view
  (no column box) it vanished into the bar. It now keeps the cursor colour with a ring. Checked headless: the cursor
  stays on its track and column when Space starts playback.

## 30 Oct (15): Arrange pages with playback (build 283)

- `followPlay` (FOLLOW on, song mode): when the playing row leaves Arrange's rows (`arrRows()`), `cs.song.top` jumps so
  the playing row is first (a page turn), for Arrange beside a screen and as the Song screen; not while Arrange has the
  keys. Checked headless on Phrase and Song at 240 BPM: the playing row stayed in view throughout.

## 30 Oct (14): Help and README audit (build 282)

- Checked both against the code and fixed: effect counts (72 a track, 81 bus / master, 65 pad) and groups, the
  drum-type preset groups, …pump EQ on any track, the master's command list (`MST_FXC`), the key legend (Tab, Enter,
  Backspace, Ctrl+Space, Alt+A, letter cycling), Shift+Alt on Arrange / the Mixer, README's instrument (64) and slot (8)
  counts, `FXI.GRV` (this track, not all). When adding commands or effects, update Help's `fxg` list, the key legend
  (`tLegKeys`) and README together.

## Keep doing with every build: What's new

- `CHANGES` (beside `whatsNew`, after `welcome`) lists what changed, newest first: `[build, date, [lines]]`. Add a line
  for every build someone would notice, in plain words, when bumping `BUILD`. After an update the app shows the entries
  newer than the build this device last ran (`thunderplus-seen-build`) once; a first run shows the welcome instead (a
  device that had run the app before the list existed sees the 30 Oct entries); Help's header has What's new.

## 30 Oct (13): Renoise-style commands (build 280)

- `FXC` += FDI, TRM, APN, TVL, TPN, STP, LPB, PDL, ONE, MPB, MAT (master may use LPB, PDL, STP).
- Per note (`ev.fdi`, `ev.trm`, `ev.apn`): `startNote` ramps the voice gain in, and hangs an OscillatorNode (X cycles a
  beat at `bpmNow`, depth Y/15) on its gain / pan, stopped when the source ends.
- TVL / TPN events → `trackTo` in the player: `tvlOv` / `tpnOv` (also read by `applyMix`, so mute and solo still work),
  set with a 4 ms setTargetAtTime; cleared and `applyMix()`ed in `resetSeq`.
- STP kills the voices of the track (00) or every track (01). LPB sets `api.lpb` (every track's line length from the
  next row; 00 back to the phrases'), PDL adds XX rows to every playing track's next step (`pdl`). ONE keeps one random
  note (and its column) of a chord row. MPB / MAT are MIDI-only events (`moCmd`, and the MIDI export).
- Checked headless: sequencer times for LPB 8 then 0, PDL 2 and STP were exact; TVL 40 / TPN 20 reached the bus while
  playing and the mixer's came back at stop. Not done (yet): Renoise's Zxx (a phrase from a note), Exx, Jxx, pan slides.

## 30 Oct (12): FXS / FXV, SCA / SCG, CUT / RES / DEC (build 279), after an audit against the M8's commands

- New `FXC` entries at the end: FXS, FXV, SCA, SCG, CUT, RES, DEC (master phrases may use FXS, FXV, SCG, PRB).
- FXS XY sets `T.fxs` (slot X, parameter Y, 1-based); FXV makes an `fxp` event; the player's `fxpTo` keeps per-slot
  overrides (`fxOv`), builds the slot's values with `TFX[type].map` and posts `{pset:{slot, p | mp}}` to the track's
  effect node at the note's time (`atTime`: timer live, `ac.suspend` offline). The worklet merges it into its last chain
  and runs `set()`. `resetSeq` posts the slots' own values back. …pump slots are skipped.
- SCA / SCG: `T.scl` / `gscl` replace `S` in the KEY FOLLOW `scaleSnap`.
- CUT / RES / DEC: per-track state (`T.cut`, `T.res`, `T.dec`, FF turns CUT and DEC off) → `ev.tw`; `varBuf` low-passes
  (`shapeData`, the voice filter's 2-pole) and fades the pad's render or its MUT / GEN variation, cached in `VAR`.
  Not for sliced / synced instruments.
- Checked headless: an FXV sweep on a Filter slot sent the cutoffs on time and the slot's own came back at stop; CUT and
  DEC notes were shaped and cached.

## 30 Oct (11): Tab in a phrase (build 278)

- KEYS-mode Tab in the phrase editor: note → instrument → FX1 → FX2 → FX3 → first note (Shift+Tab back); the other
  note columns and VOL are on the arrows (VOL: Tab goes on to the instrument). Master / bus-FX phrases: FX1–3.
  Pattern view keeps Tab = next track (`pvTab`).

## 30 Oct (10): MUT, GEN and PRB (build 277)

- New commands at the end of `FXC` (saved songs keep their numbers): `MUT`, `GEN` (group Sound), `PRB` (Chance and random).
- `PRB XX`: in the row's command loop, like RND: removes the command to its left unless `rand() * 255 < XX` (the
  song's seeded random, so SED repeats it); with none to its left it acts as the note's chance.
- `MUT` / `GEN` set `ev.vk` ('m'/'g' + value) on the note; `startNote` plays `varBuf(ev, ac)` (else the pad).
  `varVoice`: GEN → `genVoice(type, FAMILY, n, RANGE)` on `GEN_SPACE` (12 types on Tone drum, Breath, Skin, FM Drum,
  Blip; FAMILY picks a centre per control by `bkHash`, RANGE spreads around it); MUT → `mutVoice(pad, n, RANGE)` (engine
  controls by their XUI rows in lin/log space; both move each control by `varNoise` (smooth value noise over the number, a new
  point every 16, so neighbouring numbers sound alike and RNL drifts), whole-number controls only past half range; layers by `MUT_LAYER`).
  MUT variations are scaled to the pad's peak (`varLevel`). Sliced / synced instruments ignore both.
- Cache `VAR` (key: pad, vk, RANGE, and the pad's render revision for MUT or type+FAMILY for GEN), ~48 MB LRU. Live:
  `varWant` renders in the Worker when the sequencer makes the event (ahead of the note) and `varPrefetch` at every
  tStart renders the outright values; until ready the pad plays. Offline renders (`ac !== ctx`) render synchronously,
  so exports are exact.
- INST rows (not on MIDI instruments): RANGE (`mrng`, default 40 hex), GEN TYPE (`gtype`: AUTO by the pad's name via
  `drumTypeOf`, or one of `GEN_TYPES`), FAMILY (`gcen`).
- Checked headless: all 12 GEN types sound, GEN is deterministic, low RANGE keeps a kick's pitch within ~3 Hz and full
  within its whole box; MUT on layer kits and engine presets; a playing phrase with GEN+RND and MUT+PRB filled the cache
  in the Worker with no errors; the offline path renders.

## 30 Oct (9): Enter on effect cells, as on notes (build 276)

- Keyboard Enter on a filled command cell picks it up (`pickUp`: command and value become current); on an empty one it
  writes the current command (only one the master allows, in a master phrase); a quick second Enter opens the picker
  (`editDouble`). The pad's EDIT still opens the picker at once. v1.2.30 (desktop) has builds up to 275.

## 30 Oct (8): IDM drum effects (build 275)

- Three plain-JS track / master effects on the beat clock, ids 100–102 in the track-FX worklet (`GrainsCore`,
  `ScrambleCore`, `GlitchCore`, with `beatLock` = Flex's clock lock and `bkHash`; all added to the worklet Blob and to
  `clocked`), TFX keys `grains`, `scramble`, `glitch` (group Rhythmic), 21 factory presets.
- Beat grains free-runs at the last tempo when the song is stopped (Flex-style effects pass through instead); FREEZE
  holds once two beats are in the buffer. It counts as a tail (TAILS) for renders.
- Checked offline on a synthetic drum loop (finite, no silence, ~80× realtime for all three together) and in the real
  worklet (fxOk, levels on the track while playing). Track / master only: not pad effects yet (the pad menu is FX + MODFX).

## 30 Oct (7): Backspace steps down, Arrange follows Shift+Alt moves, Ctrl+Space loop off (build 274)

- Backspace in a phrase clears the cell and steps down by the edit step (KEYS mode handler); Delete still clears in place.
- `songViewRows()`: the rows actually shown where the song cursor is (Arrange's `arrRows()` when the panel is beside the
  screen or is the Song screen). `phraseGrid`, `shiftAltArrow`, the Song jump and the pattern-view move used the Song
  grid's taller count, so Shift+Alt+▼ could put the block below Arrange's edge (headless: 32 of 90 moves out of view
  before, 0 after).
- Ctrl+Space while playing stops and turns LOOP off.

## 30 Oct (6): FX screen fixes, FX presets, EQ window, Alt+A (build 273)

- **FX screen stage:** it sat right of the widest text line, so it moved as the picked effect's controls and description
  changed. `stageRoom()` now starts it at 72 characters at least on the FX screen.
- **Alt+A (`arrKeyToggle`):** `apFocus('arr', true)` from any tracker screen (the Arrange-beside-any-screen machinery
  already existed); again (or Esc) gives the keys back. In `globalKeys`, so it goes through `apRun`.
- **FX presets:** `FX_FACTORY` (214 presets, 66 effect types; envfilt, autotune and utility have none yet), `fxPresetOpen`
  in the fxp picker (`fxp.tpre`, headings DEFAULTS / FACTORY / THIS SONG), `fxPreApply` (named values, the rest to
  defaults, the sidechain source kept), `fxPresetSave` into `S.fxPre[type]` (`normFxPre` on load). Any parameter edit
  clears the slot's `pname`. Checked headless: every factory value is a real parameter within its range.
- **EQ window:** …pump EQ (`peq`) allowed on every track (`pumpAnyTrack` in `fxtL` / `fxtOk`); the other pump effects stay
  bus / master only. `eqWindow(t)` (› EQ WINDOW, the last FX screen row) finds the track's pump EQ or adds one to the first
  free slot, and calls `PumpFX.open`. LINEAR mode adds latency, which a normal track has no compensation for.

## 30 Oct (5): drum presets by type, 93 new ones (build 272)

- The pad Presets list now starts with one group per drum type (`DRUM_TYPES`: Kicks, Snares, Claps & snaps, Rims &
  sticks, Closed hats, Open hats, Cymbals, Toms, Hand drums, Shakers & tambourines, Cowbells, blocks & metal).
  `organizeDrums()` wraps `factoryVoicePresets()`: it files the 93 new ones (`moreDrumPresets()`, on Tone drum, FM
  Drum, Skin, Breath, Blip and layers), the old hand-made drum groups, the drum engines' presets (tagged "(tone drum)",
  "(FM drum)", "(skin)"; from Breath, Chip, Bytebeat and Modal only the drums, `DRUM_SOURCES`) and every classic and
  acoustic kit sound (already prefixed "808 …", "Funk …"), by name (`drumTypeOf`). Kits go last in each type. The old
  drum, kit, Tone drums, FM Drum and Skin groups are gone from this list; engine cards keep their own lists and
  File ▾ → Drum kits… still loads whole kits. LXR and IDM & glitch stay as they were.
- Recent picks are keyed by group and item number, so `PRK` moved to v2 (old recents are dropped once).
- Every drum preset was rendered headless: none silent, the new ones within the existing ones' level range.

## 30 Oct (4): Acid line generator and Breaks to chop (build 271)

- **Acid line… (Edit ▾ / right-click on a phrase, `acidLine`):** pump ACID's `randomizeSteps` / `mutateSteps`
  (plugins/acid/AcidParams.hpp) ported as `acidRoll` / `acidMutate`. It reads the phrase rows back into steps first
  (`acidRead`: note = note, empty after a sounding step = tie, OFF or empty after a rest = rest, VOL ≥ C0 = accent, a
  PSL on the next row = slide), rolls the lit lanes and writes them back (`acidWrite`): accent VOL FF, plain no volume,
  slide PSL 03 on the note slid into (the player plays a PSL note legato), rest OFF after a sounding step. Rows: the
  selection, else the phrase's LEN. The panel stays open (R roll, M mutate, Space play, Esc done); each roll is an undo
  step. Checked headless: the write/read round trip is exact, lanes left alone keep their steps, accents never run past
  3, the first step is always the root.
- **Breaks to chop (File ▾, `pickChopBreak`):** pump BREAK's 14 built-in breaks (`CHOP_BREAKS` = kBreaks) and its kit
  renderer (`renderChopBreak` = BreakSounds.hpp renderBreak, with pump's Rng / OnePole / Svf / FracDelay as small
  closures). Matches the C++ to 2e-7 on all 14 at 48 kHz (built the plugin's renderBreak and compared). Summed to mono,
  into a slot with `assignSample`; instruments on that slot get SLICES 16, GRID, SYNC REPITCH (as the plugin's FIT),
  BEATS 4. Renders on the UI thread in 0.5–1 s on a computer; move it to the pad Worker if phones stall.
- Not ported: BREAK's own 16-step slice sequencer (the tracker is the sequencer) and its momentary effects (Flex /
  Retrigger with FLX / RTG cover them); ACID's own sequencer modes (Song / Keys / Free).

## Possible future work: automation lanes, like Renoise (owner, 30 Oct: wanted; a big build, plan agreed in outline)

What exists already: `FXV` moves any effect slot parameter in time (`fxpTo` → `{pset}` into the slot's worklet, timer live,
`ac.suspend` offline, restored at stop); `TVL` / `TPN` move a track's level and pan (`trackTo`, AudioParams); the master's
VOL (`mvol`); CUT / MUT for pads through `varBuf`. So the playback half of automation is mostly built.
1. **Lanes per pattern (phase 1, ~a day).** Data: per song row / phrase, a list of lanes `{target:{kind:'fx'|'mix'|'mst',
   track, slot, param}, pts:[[pos, value, curve]]}` saved in the song (normalise on load). Playback: at each row the
   sequencer emits the lane's value (or a ramp over the row) as `fxp` / `tvl` / `tpn` / `mvol` events, so live and renders
   share one path. Editor: desktop Pattern view, a lane under a track's column (click to add points, drag, right-click
   delete, a lane picker listing the track's slots and their parameters, like FXS).
2. **Recording:** with REC on, moving a mixer or FX-screen control writes points into the playing pattern's lane.
3. **Touch editing** on the Fold (bigger handles, a lane sheet).
4. **Pad / instrument parameters:** only through renders (`varBuf`: CUT / RES / DEC / MUT), so stepped per note, not smooth.
Also audit candidates the owner can pick from: Elektron (trig conditions, parameter locks), Polyend Tracker, SunVox,
LSDJ, Furnace / OpenMPT.

## Possible future work: search and tidier groups in the sound and effect pickers (owner, 30 Oct: nice to have, not started)

Surveyed 30 Oct (line numbers as of build 270).
- **Already searchable:** the engine sheet (`engineSheet`), pad and layer presets (`presetSheet`), and quick-add in
  …chains / …waves. Each is a plain substring match, and each has its own copy of the code.
- **No search:**
  - the fxp pop-up (`fxsOpen` FX slot picker, 69 effects or 78 on buses and the master; `fxpOpen` command picker,
    `FXC_GROUPS`)
  - the pad "Add effect" `<select id="fxAdd">` (65 effects)
  - the SoundFont preset `<select>` (`sfPanel`, a flat list of 128+)
- **Suggested order, smallest first:**
  1. Type-to-filter in the fxp pop-up. It already has the best keys (▲▼, PgUp/PgDn, Enter, Esc, pad buttons); today a
     letter only jumps to the next match.
  2. The engine and preset sheets: focus the search box on open, add ▲▼ and Enter.
  3. Swap `#fxAdd` and the SoundFont preset menu for the same searchable sheet. Pull the sheet's search out once
     instead of a fourth copy.
  4. Tidy the groups. Get the owner to approve the list first, because it touches three apps.
- **Organisation problems found:**
  - Effect group names differ between Thunder and `PAL_LAYOUT` in …chains / …waves: Modulation vs Modulation
    effects, Spectral and granular vs Granular and spectral, Rhythmic vs Rhythmic effects, EQ under Dynamics vs under
    Filters.
  - The pad menu (FX + MODFX `pad`, A–Z in each group) and the FX screen (`FXT_LIST`, `FXT_FIRST` order) differ in
    count and order.
  - "Mastering (…pump)" isn't in `FX_GROUPS`, so it sorts after More.
  - Engines: "Synths" is a grab-bag of 13; Bass is under "Voice & bass" but Blip under Drums; the …pump live twins read
    as duplicates.
  - Plaits groups are named "Synth models 1/2".
  - Help line 2431 still says "37 effects".

## 30 Oct (3): Alt-click mutes the selection (build 270)

- Song / Arrange: an Alt-click (no drag) on a block inside the selection mutes or unmutes every filled block in it
  (`sxEnd` → `sxMuteRect`, all muted → unmute); on a block among the Ctrl/⌘ picks it does the picks. The text Song grid
  does the same (`mselEnd`, `msel.alt`). Outside a selection it still mutes the one block; Alt-drag still copies.
- v1.2.27 released (builds 268–269 plus the 8-slot help fix).

## 30 Oct (2): FLX / GAT / RTG add their effect (build 269)

- These commands only drive a Flex, Step gate or Retrigger already in a slot; without one they did nothing, and the owner
  thought RTG on the master was broken. Now, once a command cell has held FLX, GAT or RTG for 0.9 s (so scrolling or
  letter-cycling past doesn't add anything), `fxNeed()` (beside `after()`) puts the effect in the first free slot of
  `cTrack` (the master on the master column) with a toast. Already there: left alone; there but off: a toast says so;
  no free slot: a toast says so. The slot change isn't part of the cell's undo step.

## 30 Oct: master cut flash (build 268)

- The owner lost an hour thinking the U-24 interface was dropping out; it was a VOL command on the master track cutting
  the song. Now, while the song plays and the master is more than 3 dB under its MASTER setting because of a command,
  the header meter and the Mixer's master meter flash amber (`mCut()`, beside `HM`). It reads `rt.out.gain.value`, so it
  flashes exactly while the cut is heard and stops on STOP (`resetSeq` puts MASTER back). Header meter tooltip says why.
- Also from that session (not a code change): the Mac desktop app showed ~8 macOS microphone prompts per launch after
  the owner picked the U-24 by name in Settings → Audio → Output. Likely cause: `setSinkId` with a device id makes
  Chromium enumerate Core Audio devices, the U-24 has inputs, and an unsigned build can't keep the macOS decision.
  Workaround: Output back to System default. A real fix needs a signed mac build.

## 27 Oct (6): gain staging: TRIM, VOL, master OUT, PRE sends, CHN crossfade (build 264)

Owner: "look at the handover document for information about gain staging changes, let me know if you have questions, and
then implement the changes". The plan was `docs/GAIN-STAGING.md`; its five questions went to the owner, who took every
recommendation: the master bar is an **OUT fader after the limiter** (MASTER stays the drive, the master phrase's VOL keeps
moving MASTER); **PRE-fader bus sends** as an option, **mute and solo still silence them**; no dry (pre-chain) send; a
**~20 ms crossfade** on CHN. Question 4 was answered from Thunder Perform's code (it reads nothing of the mix; it runs this
app in frames from its pinned submodule, this repo's `deck` branch from 13 Oct, so it gets this only once `main` is merged
into `deck`; `deck` sends `master` to the host, and OUT sits before `master`, so that still fits).
- **What changed, where:** `docs/GAIN-STAGING.md` → "Built" has the whole list with the names in the code. In short: the
  Mixer's first row is **TRIM** (`S.mix[t].trim`, 80 = 0 dB, ±18 dB, on `inp`, before …chains and FX; a light when the
  input runs hot); the meter's bar and the bottom panel's slider stay **VOL**; the master row is MASTER → FX → MASTERING →
  **OUT** (`S.mout`, `rt.mo`, after the limiter, in renders too, with a bar on the L / R meter; the bottom panel's master
  strip is OUT now); a Track In's **Bus send** (`pre`) in …chains; `routeTrack` crossfades `dry` / `wet` (`chnIn`,
  `chnOut`, `ramp`) and no longer pulls the track's input off its taps to rewire.
- **Old songs** open with TRIM and OUT at 0 dB and every Track In after its fader, so they sound as before.
- **Also fixed on the way:** a removed track-FX node stayed wired to the fader (silent); a click on the Mixer's master row
  moved the track cursor to track 1 (now it keeps it, and Shift+Option+▲ goes back to it); a new …chains pair is placed by
  the editor's real box heights (`CHN_BOX`, Track In grew a row).
- **Tests:** `tests/gain-staging.test.js` (6 tests: TRIM's scale, `applyMix`, the crossfade's ramps and node lifetimes,
  `busPlan().pre`, source guards for the graph, OUT, the Mixer and the song file); `npm test` 96/96. Headless Chromium
  checks (offline renders of TRIM, PRE / post sends with VOL, pan, mute; a live CHN on / off recorded on the audio thread;
  the OUT bar at 1400, 882 and 384 px; mouse drags of the OUT bar, a VOL bar and a TRIM value) are described in the doc.
  To repeat them: serve the repo, make a copy of index.html with `window.__ev = src => eval(src);` before the app's last
  `})();` (the app is one closure), and evaluate through it.
- Cache `thunderplus-v264`, `chains/chains-worklet.js?v=39` (index.html, chains/index.html, sw.js).
- **Not done:** the bus's sources switching back to the speakers when a bus goes away (not crossfaded); a first native
  track FX added or removed is still a rewire. Not yet played by the owner.
- **Review fixes (build 265),** from a separate review of 264 (it found no audio faults: old songs render bit-identical or
  within 1 LSB, ~−138 dBFS, from the extra gain nodes): the master row is hex only with two-space gaps (with OUT and the
  dB labels it was the Mixer's widest line, and a phone's fit shrank all its text: now as big as before or bigger at 360,
  384 and 412 px); `fitCheck` measures where lines end in the content, not on screen (scrolled to the OUT cursor it never
  converged and ran 2 px over); a VOL bar drag keeps the cursor's row (it went to row 0, now TRIM); typed hex and double
  EDIT tell MASTER / MASTERING / OUT apart; `onMstCell` is any master cell, so / Ctrl+/ Ctrl+D there no longer hit the last
  track; Bus send is `fixed` (no modulation jack); Help and README say NORMALISE undoes OUT in renders. Two older faults the
  review noticed: a removed track or master effect node let go of its taps, sends and beat clock (`fxDrop`), and Reset
  audio re-attaches the bottom panel's strip meter (`dkSt = null`). `npm test` 97/97. Desktop **v1.2.26** from here.

## 27 Oct (5): usability Phase 2 ("one feel"), built to the owner's calls in "27 Oct (3)"

Builds 257–261. Owner: "implement the rest of phase two" (and the editors' gestures "match Max"). Each change has a
comment marked Phase 2 / 27 Oct at the code; `tests/usability.test.js` has a test per part; each part was checked headless.
- **(1) Space, Shift, drag, hold (257).** `playKeySwap()` returns true: Space plays / stops the song in every mode and on
  every screen; Shift+Space this screen (the block's phrase on the Song; the selected pad on the Synth page, which had
  Space = pad). The on-screen PLAY button keeps the pad's meaning. Tracker value drags: Shift slows (48 px a step), Ctrl/⌘
  big steps (`drag.pos` accumulates, so Shift mid-drag never jumps); the wheel over a value no longer takes big steps, and
  Ctrl+wheel there is left to zoom. With a mouse a drag always selects, from the cursor's own cell too (`mselDown`
  `filled`; a click on it still opens / edits, `mselEnd`); **Alt-drag edits** (`altDrag`, gridDown and Pattern view's
  open track), and an Alt-click on the Song still mutes (`drag.altMute`); Alt-press inside a selection still copies it.
  Arrange hold-to-mute is touch and pen only (`holdOk`).
- **(2) One slider (258).** Document capture pointerdown for every host range input (`sliderOk`, not #gset / .rlock):
  relative drag, Shift a tenth (re-anchored per move), value tip (`.sldtip`), Alt / Ctrl / ⌘-click resets (`inp._reset`
  from makeRow), touch moves only after a 6 px sideways slide (on `pointer:coarse` ranges have pointer-events:none and
  `sliderAt()` finds them, or Chrome jumps them to the finger first). WHEEL FOCUS: the slider the pointer rests on for
  350 ms (outlined, `.wheelon`) takes the wheel; until then the page scrolls. …waves / …chains: wheel pans, Shift+wheel
  sideways, Ctrl/⌘+wheel or pinch zooms. …pump windows already matched (Shift fine, Cmd/Alt reset): untouched.
- **(3) Editors (259).** APP KEYS: the editors pass Space (…waves; …chains already did), F2–F9 and Ctrl/⌘+S / Shift+S /
  O / N up (`TW.appKey` → `editorKey`), so they act on the project; on those screens File ▾ shows the project's items first
  and the patch's as "New / Open a … patch file / Export the … patch". ONE UNDO: inside Thunder the editors' Undo / Redo are
  the song's (`TW.undo` → `undoStep`); …waves edits are `{k:'W'}` steps (`thunderWaves.save`), …chains edits are song
  steps (S.chainPatch); `PatchStore.snapshot()` hands off a pending edit first, so edits stay separate steps; undo shows the
  patch in the open editor (`chainsEmbed.setDoc` / `wavesEmbed.setDoc`) instead of dropping the frame (only when the patch
  actually changed, `chWas`).
- **(4) Max gestures (260)** in both editors: click a module (title or non-control part) selects just it; Shift-click
  toggles; mouse drag on the background = selection box (Shift adds); middle-drag / wheel / two fingers pan (touch drag
  still pans); Alt-drag copies (as before); Alt-click opens its help (`maxHelpPend`); Ctrl/⌘-click (⌘ on a Mac) works it
  without selecting; Delete / Backspace removes the selection (queue item 4 done). Module controls still work with a
  plain click (the patch plays live), the one deliberate difference from Max's locked/unlocked modes.
- **(5) The rest (261).** `sxMenu` picks on release when opened by a held press (`ptrHeld`); `ctxBind(el, fn)` = right-click
  + long-press menus, now on the Synth page's pads (right-click only: a long press holds the note) and layers. ONE CONFIRM:
  the inline Yes / Cancel row (`ask()`), File ▾'s "Tap again" and the Project rows' "Tap EDIT twice" are `askYes` (File
  items skip it when the project is dirty, since "Save changes?" asks). NOTE KEYS BY POSITION: `PIANO_CODE` / `pianoOf`
  (tracker, Synth page), `K_CODE` (…waves). ONE STEP PER GESTURE: `snap()` and `kitSettle()` wait while a pointer is down
  (`undoHeld`), one step on release (a Mixer fader drag made a step per move). TOUCH SIZING on `pointer:coarse`: 40 px
  hit areas (faders' `::before`, deck grip and header buttons, rows, ranges, menu items, the Arrange resize handle).
  PINCH-ZOOM: viewport no longer blocks it, `html{touch-action:manipulation}`; pads `pan-x pan-y`, keypad / PAD buttons
  `none`, Arrange keeps its own pinch.
- **Review fixes (263),** from a separate review of 257–261: a right-click held without moving no longer runs the item
  under the pointer (a slide is needed); opening, panning or the editors' first hand-off are no undo steps (`patchKey`
  leaves out view and save time; `WV.born` / `CH.born`, `undoQuiet`); a double EDIT on a confirm row still goes through
  (`askSheet` `dblOk`, early backdrop taps ignored); a click on the cursor's empty cell adds again on the classic Song and
  Sect; a lost pointerup can't leave undo held (a key or a buttonless move lets go; undoStep takes a waiting step
  first); …waves Space on a focused item works the item; the Synth page's octave keys and top E go by position.
- **Not done / left:** a full sweep giving every menu item its key hint and one item order; long-press menus on track
  numbers (their right-click solos, the owner's choice) and Mixer strips / FX slots on touch (mouse right-click there is
  the Edit menu already). Ctrl+N still opens a browser window in a browser tab.
- **The owner's queue ("27 Oct (4)") is done too (build 262):** (1) …chains draws the modules of a track whose CHN is off
  dashed and faded (`paintChainOff`, from `TW.chainOn()`; the Mixer's CHN and undo tell the editor, `chainOnChanged`);
  (3) "Make this track a normal track (not a bus)" in the track menus (Song / Arrange right-click, Edit ▾) when the track
  is a bus: `unBus(t)` takes its Chain Out(s) out, gives it a straight pair, gives each source without a Chain Out of its
  own one, and leaves the bus's modules in the patch unplugged; (5) CLN↓ inserts new song rows below (`songRowsInsert`,
  shared with Ctrl+D) instead of needing the rows below empty; (6) "Remove row" in the Song / Arrange menu
  (`songRowRemove`, `songRowsRemove`: every track and the master, mutes, sections, an armed loop). (2) and (4) are Phase 2's
  Max gestures above. Then the gain staging talk: `docs/GAIN-STAGING.md`
  (the VOL field and the meter fader are one post-chain value; a proposed TRIM / fader / master OUT split and five
  questions; the owner doesn't mind old songs changing sound). Built in "27 Oct (6)".

## 27 Oct (4): owner's queue (done in builds 260 and 262: see "27 Oct (5)")

Owner asked for these next, in their words:
1. **…chains: show which modules are live.** When a chain is turned off in the Mixer, its modules look different (a dashed
   outline, a drop shadow or similar) so it's easy to see which are active.
2. **…waves and …chains: modules behave like Max/MSP's edit mode (owner: "make it all match Max").** Click selects one
   (drops the rest); Shift-click adds / removes a module; drag on empty space = a box selection (Shift-drag adds to it);
   Alt/⌥-drag duplicates and drags the copy; Ctrl/⌘-click works the module's controls without selecting or moving it;
   Alt/⌥-click opens its help; Delete / Backspace removes the selection (item 4). Check what the editors use these
   modifiers for today, and move anything that clashes.
3. **Bus back to a normal track:** a track made into a bus can be changed back.
4. **Delete / Backspace on a selected module deletes it** (…waves and …chains).
5. **Clone down (CLN↓) inserts a new row in the arrangement** instead of overwriting the phrases below it.
6. **Arrange right-click → "Remove row":** removes the whole song row (the rows below move up).

## 27 Oct (3): the owner's calls for Phase 2 (read before starting Phase 2)

Owner went through the gate table in the plan doc (https://claude.ai/artifact/8yDBMKuKciLpnVqYRHVrTH, "Your calls before
Phase 2", now with a "Your call" column). Phase 2 can start; build it to these:
- **Space:** plays / stops the song everywhere, every mode, the Synth page included (the pad auditions from its note keys
  and a click); **Shift+Space** plays this screen. Replaces `playKeySwap()`'s split and the Synth page's Space = pad.
- **Shift:** fine everywhere (sliders, faders, pump, tracker drags and nudges); **Ctrl/⌘ = big steps** (octave, 16).
- **Ctrl/⌘+arrows (values) and Alt/⌥+arrows (screens):** keep. A Renoise key option can come later (Phase 4).
- **Drag from the cursor's filled cell:** always selects, as Renoise; **Alt/⌥-drag edits** the value. On the Song grid
  Alt-click mutes on press today (`tv === 'song' && e.altKey` in the grid pointerdown): make it wait to tell a click
  (mute) from a drag (edit), as Arrange's `sxDown` already does.
- **Hold-to-mute in Arrange:** touch and pen only. With a mouse, Alt-click mutes (already does) and middle-click too; add
  Alt-click to Help's Arrange row.
- **Wheel on sliders:** the slider under the pointer, after the pointer rests on it a moment, highlighted while it takes
  the wheel (scrolling past doesn't change it).
- **Wheel in …waves / …chains:** scrolls; Ctrl+wheel or a pinch zooms.
- **Ctrl/⌘+S, N, O in the editors:** act on the project; patch export moves to a menu item in the editor.
- **Undo in the editors:** one shared history with the song, the DAW way (the bigger job; plan it first).
- **Skins:** keep all of them for now. The owner will list which to keep and which to revise. Done now (build 256): a
  first visit starts on **Simple rainbow** (`'rainbow'`; was Simple grey). Saved picks are kept.
- **Pinch-zoom:** allow it, except on the pads, the keypad and the Arrange (which keeps its own pinch): `touch-action` on
  those, and drop `maximum-scale=1, user-scalable=no` from the viewport.

## 27 Oct (2): usability Phase 1 (the audit's bugs; no design changes)

Build 255. Owner said "go for it" to the plan in "27 Oct: usability audit" below. Phase 1 is done; every owner decision
in the doc's gate table is untouched (Space order, Shift fine/big, drag-from-cursor, Arrange hold-to-mute, wheel focus,
editor keys, skins, pinch-zoom: all still as the owner set them). Ids are `docs/USABILITY-AUDIT.md`'s; each fix has a
`FIX (27 Oct, audit …)` comment at the change.
- **K1** KEYS mode returns before the PAD key table, and keyup lets go of a held PAD key in either mode (Shift then X no
  longer pastes; Z then X no longer clears). Ctrl/⌘+B toggles the block selection (no repeat).
- **K2** Insert / Ctrl/⌘+Backspace move rows only inside the phrase's LEN (a sect's 16); the row pushed past the end goes,
  rows kept past the LEN stay (Renoise). It used to cut every phrase to 16 rows and throw.
- **K4** Delete (and Shift+Delete / row ops) on a selection clears it (`clearSel`, toast says Undo brings it back); the
  clipboard is no longer replaced.
- **K6** AltGr symbols (`{ } [ ] \ | @` on DE/FR/Nordic layouts) reach the tracker without Ctrl/Alt (`noAltGr`); Ctrl+Alt+
  letters and digits are still commands.
- **K7** A `<select>` blurs after a pick made with the pointer, so piano keys and arrows go back to the tracker (keyboard
  use of the select keeps focus; dialogs excluded).
- **K8** The Synth page's keys act only on the Synth page (not on …waves / …chains).
- **K3** Help ("Main keys", Alt/⌥+arrows, CLN on Ctrl/⌘+Alt+C · Ctrl/⌘+Shift+C, Ctrl/⌘+D, Ctrl/⌘+T, Ctrl+Space, / \ mute,
  Ctrl/⌘+/ solo, F8 twice, the lower note row ending at ;), the key legend's Space line (follows `playKeySwap()`), the
  first-run tour's Space line (same), README (Space order, Delete vs Shift+Delete, F8 twice), Help's MIDI location.
- **M2** Wheel steps add up movement, one step per 100 px (lines × 40, pages × 400), reset after 250 ms or a turn, at most
  8 a event: host sliders (`wheelSteps`) and pump windows (`Win.wheelN`, kit.js v=4). Pump menus scroll by pixels.
- **M3** A …chains slider row in the bottom panel opens one menu (its own, with the panel items: `makeRow` `opts.menu`);
  list rows get a non-modal `sxMenu` instead of the `askPick` modal.
- **M17** Mixer bars drag on the left button only.
- **B1** "Save changes?" now notices `setKit` (Load kit, Factory drums, Clear slots), …waves edits (`wavesChanged(true)`,
  `thunderWaves.save` only when the patch changed), pad/kit names, choke and SLOTS edits (`markKit`).
- **B2** Open project starts a fresh undo history (`undoReset`), so undo can't bring the old song back over the new file.
  Funk breaks and other in-song loads keep their undo (owner's decision, not touched).
- **B3** `toast(msg, ms)` honours ms (1–12 s); errors with no time stay 4–8 s; a long toast (≥ 4 s) isn't wiped by a
  short one (the short one shows after; up to 3 long ones queue); tap dismisses; `role=status aria-live=polite`.
- **B8** The browser asks before closing a tab with unsaved changes (not on the app's own update reload, not in the
  desktop app, which asks itself).
- **B12** A dialog opened from Settings sits above the menu (`.ask` z 125 > `.setmenu` 120) and the menu's key handler
  leaves it the keys (`if (askOpen) return;`).
- **B13** List dialogs ("Save changes?") focus the first choice (Save); arrows move, Enter picks.
- **B18** F8 / Stop when already stopped = panic: synths let go, held MIDI released, CC 64/120/123 on every channel of
  every open port; toast says so.
- **V1** On desktop windows wider than 600 px where the tabs don't fit (Fold unfolded 882, 1024), the transport strip
  goes on its own row under the tabs (`.ttabbar.wrap2`, `patFit`); checked at 882/1024/1180/1400.
- **Tests:** `tests/usability.test.js` (8 tests); `npm test` 83/83. Headless (Chromium) checks of each fix above passed,
  except M3, which is covered by the source test only (needs a …chains patch mapped to the panel).
- **Left for the review:** everything in the doc's gate table and Phases 2–4. Also seen while doing this (not fixed):
  Ctrl+N on the Synth page in a browser tab still opens a browser window (browsers keep it; desktop app gets it).

## 27 Oct: usability audit (read this first when reviewing it)

Owner asked for an audit of key commands, mouse gestures, behaviours and anything that would make Thunder Plus feel
streamlined, familiar and professional, plus a plan, to review with another agent. Four audits ran at build 254 (keys,
mouse/trackpad/touch, behaviours, and a screen-by-screen walkthrough at desktop, Fold unfolded and Fold cover sizes).
- **Owner-facing write-up and plan:** the doc "Thunder Plus: usability audit and plan",
  https://claude.ai/artifact/8yDBMKuKciLpnVqYRHVrTH (summary, a section per area with severity and effort, the plan as
  four phases with checklists, and a table of the owner's own past decisions to settle before Phase 2).
- **Agent-facing detail with code locations:** `docs/USABILITY-AUDIT.md` (ids K1…, M1…, B1…, V1…).
- **Summary.** Close to pro already (rich Renoise-like keys; the engine picker and pump windows are polished). What
  holds it back: trust bugs (Shift/Z/X stuck in KEYS mode silently pasting or clearing; Insert wiping half a phrase;
  "Save changes?" missing kit and …waves edits; undo after Open saving the old song over the new file; warning toasts
  gone in 2 s), controls that behave differently per surface (sliders jump to the click, wheel overshoots on
  trackpads, Shift fine here and big steps there, Space per screen, four menu and four confirm styles), a look with no
  type system (36 styles on one screen, header covering the tabs on the unfolded Fold, faint muted text, dark skins with
  a light grid), and Help/legend/README describing keys from before 25 Oct.
- **Plan.** Phase 1: the bugs, no design changes (below). Gate: the owner's calls on Space, Shift, drag-from-cursor,
  Arrange hold-to-mute, wheel focus, editor wheel/keys/undo, skins, pinch-zoom (table in the doc, with a recommendation
  each). Phase 2: one feel (shared slider and wheel behaviour, modifier map, one menu and confirm component, editor keys
  pass up, physical note keys, one undo step per gesture, touch sizing). Phase 3: one look (type system, responsive
  header, tabular numbers, contrast, skins, less on-screen text, Synth page, pump windows, Mixer, cover screen). Phase 4:
  pro extras (one key table for Help/legend/README/tooltips, command palette, named undo, render progress, system
  clipboard, missing Renoise commands, glossary, two-tab lock).
- **Phase 1 status:** done in build 255, see "27 Oct (2)" above. The doc's Phase 1 checklist is ticked.

## 26 Oct (4): Settings → Audio → …pump live (Auto / Rendered)

Build 254. Owner asked for the "force renders" idea as an option. A per-device setting (`aset.psLive`, saved with the
other audio settings, so the Fold can stay Rendered while the desktop plays live from the same song): **Auto** (default)
plays the live engines live with the safety net; **Rendered** always plays their renders (lighter on the battery).
`psNode` returns null when it is Rendered; switching to it drops the live nodes at once. Picking either also undoes a
safety-net switch (`psLive.off`), so live can be retried without a reload (the safety-net toast now says so). The row and
its note only show when `pump/synth/` is there. `psLive` moved up next to `aset` (the Audio panel reads it at start-up).

## 26 Oct (3): …pump synths live: Mono live, Poly live, Coil live, with a safety net

Build 253 (`index.html`, `sw.js` → `thunderplus-v253`; `pump-synth-engine.js?v=2`, new `pump-synth-worklet.js?v=1`). Owner:
keep the rendered engines and add live ones as separate engines; liked the safety net. Nothing in pump changed (the wasm
already took MIDI at any frame).
- **Engines:** `pmonol`, `ppolyl`, `pcoill` (picker: Mono, Mono live, Poly, Poly live, Coil, Coil live). A live engine and its
  rendered twin share one set of values: `setEngine` copies them over (and keeps Tune and Level, no -6 dB restart). XENG
  entries carry `twin`. Live cards add the rows a played line needs (MONO: Glide & legato; POLY: Glide); `Hit` reads
  *Velocity* there. Presets: the rendered engines' (the live ones share them).
- **Playing:** `pump/synth/pump-synth-worklet.js` (processor `thunder-psyn`), loaded by `bassReady` with the other live
  worklets (`PSYN_WK`, `psOK`). One node per track and pad (`psNode` in `makePlayer`), four note lanes of AudioParams
  (`r` rate, `g` gate, `t` trig, `l` legato, `v` velocity), driven by the existing `bassNote` (lane objects look like a bass
  node), so slides, arps, bends, vibrato, KIL/OFF and exports behave as for Acid. Mono tracks use lane 0; poly tracks
  `polyPs` (a free lane, else the oldest). In the worklet: note on = nearest note to 60 + Tune + 12 log2(rate), a slide is
  legato (new note on before the old note off), every other pitch is bend over ±24 st (COIL ±12, retuned at most once a
  block). Bend is one per instrument: a poly track's chord bends together. The wasm's bytes go with the first node on each
  audio thread (`psSent`); one instance per thread. The node sleeps after a second of silence with nothing held.
- **Velocity:** the VOL column is the note's MIDI velocity (`vc` added to note events), the pad's Velocity when there is
  none; the level keeps only the instrument's volume, so a soft note isn't turned down twice. Level, Drive and POLY's
  +8 dB trim are applied in the worklet as the render applies them.
- **Safety net (`psLive`, `psFallback`):** once a second while the song plays, if live …pump nodes take over 12 % of the
  audio thread and the audio is dropping out or the thread is over 92 % (or they alone are over 75 %), twice in a row:
  their nodes are dropped and `psNode` returns null for the rest of the visit, so `startNote` plays the pad's render (the
  twin's sound of the same values). Toast once; reload to try again. Also null (render) with no worklet or wasm. Exports
  always play live.
- **Checked:** live output equals the render for the same note (max difference 1.5e-8, all three); in the page, an offline
  player through the real `makePlayer` plays them (MONO live peak -17.1 dB = its render's), a VOL 30 note is 2.5 dB softer
  (velocity only), a C-major chord on Poly live, Coil live; the safety net drops the nodes and the next note renders.
  CPU, desktop, one note: MONO 2.9 %, POLY 5.4 %, COIL 2.6 % of a core (expect several times that on the Fold).
- **Not yet:** RET's fade applies to both
  velocity and level on live notes; a poly track's per-column slide bends the whole chord.

## 26 Oct (2): …pump synths: MONO, POLY and COIL as synth engines

Build 252 (`index.html`, `sw.js` → `thunderplus-v252`). Owner: diversify the synth section by porting the …pump
instruments, TABLE skipped. WOB, SING, KIT and MACHINE were left out on purpose: WOB is a C++ port of Bass, SING of
Voice, MACHINE is the LXR firmware already in `lxr/`, KIT overlaps the drum engines.
- **What:** engine group *…pump* with **Mono** (`pmono`), **Poly** (`ppoly`), **Coil** (`pcoil`). The sound is the plugins'
  own DSP: `pump/synth/pump-synth.wasm` (242 kB), built in the pump repo by `wasm/build.sh` from `wasm/pump_synth.cpp`
  (each plugin's `sync()` and `run()` without DPF; MIDI in, audio out). `pump-synth-meta.js` (generated there too) has
  the parameters and every factory preset. `pump/synth/pump-synth-engine.js` is the glue (hand-written, here).
- **Rendered, like LXR:** a fresh instance per render, note on (MIDI 60 + Tune's semitones; Tune's fraction as a slightly
  different render rate), note off after the hold (a held slot: its length), then until it has died away (max 12 s);
  stereo folded to mono, MONO's oversampler latency (32 samples) taken off. The page compiles the wasm and hands the
  Module to the render Worker (`PSYN_JS`, `psMod`, `PW.ps`), as `lxr/` does.
- **Pad values** are the plugin's own symbols and values (so a pad = the plugin with the same values), plus `vel` (Hit:
  the velocity the note renders with). Mod wheel, bend, glide, priority and legato are kept in the pad but not shown
  (nothing for one rendered note). Held notes fade over the engine's release: `XENG[k].rel` (new, read by `relOf`):
  MONO and POLY `a_r`, COIL `release`.
- **Level:** no normalising, the plugin's Volume is the level, except POLY renders x2.5 (+8 dB): its level is set for
  8-voice chords and one note sat 8-10 dB under the others. Factory presets peak about -4 to -20 dBFS before the pad's
  -6 dB start.
- **Card:** the plugins' panels as sections; `noRnd` rows (Volume, Hit) are skipped by Random (new flag in `xRandom`).
- **Tests:** `tests/pump-synth.test.js`: every factory preset renders finite and under full scale, hold / release / Tune,
  same render twice, golden hashes (`tests/golden/pump-synth.json`). 71 pass.
- **Rebuilding:** in pump, `WASI_SDK=… sh wasm/build.sh OUT`, copy `OUT/pump-synth.wasm` and `OUT/pump-synth-meta.js` here,
  bump their `?v=` (index.html script tag, `PSYN_JS`, the `PsynCore.load` URL, sw.js), then `UPDATE_GOLDEN=1 npm test` if the
  sound changed on purpose. `pump/pump.wasm` (the FX) was not rebuilt.
- **Known:** MONO's factory preset *Self-oscillating filter* is silent: nothing excites the filter on a fresh instance (a
  pump issue; the plugin does the same). **Next steps if wanted:** play them live (one instance per track and pad in a
  worklet, chords through the plugin's own voices; the wasm interface already takes MIDI at any frame), and the rest of
  the pump instruments (PHASE and LIFE are new there since).
## 26 Oct: SING sibilants (build 251)

S, SH, Z, ZH (and CH, JH through them) were as loud as the vowels and gated on and off in 4 ms, so they came out as
bursts of noise. Their frication levels (fa) are about a third of what they were (S 0.22 -> 0.07, SH 0.48 -> 0.15,
Z 0.14 -> 0.045, ZH 0.28 -> 0.09: 8-10 dB under the vowel), and frication now fades in over 8 ms and out over 12 ms
(P T K bursts keep the 4 ms). The same change went into pump SING (the C++ port).

## 26 Oct: review of the 25 Oct changes (four fixes)

Build 250 (`index.html`, `sw.js` → `thunderplus-v250`; the 27 Oct battery work, further down, is 249). Everything from 25 Oct was read through and the app was loaded headless
(no console errors; FX pane, phrase / pattern, song length, engines and play keys exercised). Four things were wrong:
- **Song length (LEFT / TO) read 41:40 on every song** and cost ~150 ms per read: `songRemain` ran the looping song sequencer,
  which never ends, so it always hit its 20000-step guard. It now uses the render's one-pass sequencer
  (`{once:true, sim:true}`), so the end is the last filled row of the cursor's block (where PLAY goes round again) and a read
  is a few ms. Stopped, it is worked out 400 ms after edits pause (`lenSoon`), not inside every draw; playing, once at PLAY
  (`tp.lenTot`). TO now counts from the block's top row, not row 000.
- **PHRASE ↔ PATTERN kept the other's text size.** The held phrase size (25 Oct (6)) was keyed on TEXT SIZE alone, so
  PATTERN's smaller fit stuck on PHRASE and the other way round, until a Mixer visit. The key is now TEXT SIZE + view mode +
  character width (a late web font refits too). Still held through window resizes, as intended.
- **Swapped play keys (25 Oct (7)) lost LOOP SEL and BUS PLAY.** Shift+Space on the Phrase / Sect screens is now the old Space
  (`playCtx`), and Space on the Song screen is `playCtx` too (the song from the cursor, or LOOP SEL's rows). On the Song
  screen Shift+Space plays the block's phrase on the block's own track (it used `cTrack`, which can be another track).
- **-6 dB engine start overwrote the layers' level.** Level is the pad's, so going back to layers after trying an engine left
  them 6 dB off. The layers' level is kept in `v.levelL` (as `tuneL` is) and comes back with them.
- Tests: 67 pass; the two new ones fail on the 25 Oct code.
- Left alone, worth a look: Delete on an FX slot removes the *last* effect, not the slot under the cursor (as written on
  25 Oct); the held phrase size also survives folding / unfolding the Fold (by design, but the cover screen gets the unfolded size).

## 25 Oct (8): Android project transfer (correction: what actually works)

Build 248 (`index.html`, `manifest.webmanifest` → `?v=8`, `sw.js` → `thunderplus-v248`).
- Owner's report: a `.thunder` emailed from the Mac to the Android phone was not offered to the webapp (a `.json` was). Cause (confirmed): **Chrome Android has no `launchQueue`** (BCD: Chrome Android false; `file_handlers` is desktop/ChromeOS only), so Android cannot offer a PWA in "Open with" for a custom extension. Separately, Chrome Android 132+ now has `showOpenFilePicker`, and its `types` filter is MIME-based: the old `PJT` list showed `.json` but hid `.thunder`.
- **The reliable Android routes are the share target and LOAD PROJECT.** `manifest.webmanifest` now has a `share_target` (`./share-target`, POST, `multipart/form-data`, accepts `.thunder`/`.json` as `application/json`, `application/x-thunder` and `application/octet-stream`). Gmail → Share on the attachment → ...Thunder Plus loads it. `file_handlers` + `launchQueue` are kept for desktop/ChromeOS and are harmless on Android.
- `index.html`: on Android, LOAD PROJECT skips `showOpenFilePicker` and uses the hidden file input with **no `accept` list at all**, so every file is selectable and `loadProjText` validates it; SAVE likewise omits the `types` filter on Android. `openSharedProject()` reads the file the service worker saved.
- `sw.js`: the POST handler parks the shared `File` in the **`thunder-share`** cache (deliberately not a `thunderplus-*` name, which the activate clean-up deletes) and 303-redirects to `./?shared=1`.
- The share target needs the installed WebAPK to pick up the new manifest: Chrome checks about once a day. Force it with `chrome://webapks` → Update, or remove and reinstall the app (Settings → App). Downloading the attachment and using LOAD PROJECT works even without that.
- Tests: `npm test` passes (65), including checks for the manifest, the Android picker, the share target and the cache naming.

## 25 Oct (7): playback keys swapped on desktop KEYS mode (to swap back, read this first)

Build 245 (`index.html`, `sw.js` → `thunderplus-v245`).
- Desktop KEYS mode (the keyboard, `inMode === 'keys'`, and `matchMedia('(pointer:fine)')`): **Space plays the whole song**
  (from the cursor's row; Space again stops it) and **Shift+Space plays the current phrase** (the cursor's block's phrase on
  the Song screen, else the open phrase; Shift+Space again stops it).
- Before: Space = play this screen (phrase, pattern, track), Shift+Space = the song.
- Pad mode and touch are unchanged (Space = play this screen, Shift+Space = the song).
- Code: `playKeySwap()` and `playPhraseNow()` sit just above `playCtx()`; the tracker's Space branch in the KEYS handler
  (`if (k === ' ')`) reads `playKeySwap()`. To swap back, make `playKeySwap()` return `false`: `playCtx()` and the old
  Shift+Space branch are still there.
- Non-tracker screens (Synth, Waves, Chains) keep their own handling: Shift+Space = the song, and Space still plays the pad.
  The swap doesn't reach them, so Shift+Space there doesn't play a phrase.

## 25 Oct (6): phrase/pattern space and stable text size

Cache `thunderplus-v233` (`index.html` and `sw.js`).
- Pattern row navigation, row count and LPB, plus FX2/3 and EMPTY TRACKS toggles, share the PHRASE | PATTERN title line; the duplicate phrase LEN/LPB title data is removed in Pattern mode.
- The KEYS-mode command bar stays below the tracker beside the lower controls, rather than occupying a line inside the grid. The root note's info text no longer repeats “the pad’s own pitch.”
- Phrase and Pattern text size is fitted on entry (and when TEXT SIZE changes), then held through viewport resizes. The grids calculate visible row count from their current height and can reveal all 32 phrase steps when enough vertical space is available.
- `npm test` passes, including checks for fixed text sizing and the control-bar layout.

## 25 Oct (5): a build number, and MIXER SIZE in the View menu

Cache `thunderplus-v232` (`index.html` and `sw.js`). The owner asked how to tell a build apart without DevTools.
- **BUILD** (`const BUILD` in `index.html`) is the number shown as "build 232" at the foot of **both** the View → Screen and
  the Project menus (`buildStamp()`), and it must equal the number in `sw.js`'s `CACHE` — a test fails if they disagree, so
  bumping one without the other cannot ship a build that lies about itself. The same line reads "v1.2.22 · build 232" in the
  desktop app: the packaged version comes from the updater bridge (`window.thunderDesktop.updates.state()`, already fetched on
  load), so a test build is identifiable by both numbers.
- **Desktop window title:** when the bridge reports a version, the page sets `document.title` to "...Thunder Plus 1.2.22" and
  Electron follows it, so the packaged version is readable from the title bar / taskbar. The website keeps "...Thunder Plus"
  (no bridge there).
- **MIXER SIZE is now in the View menu**, directly under TEXT SIZE (`SHEET_QUICK`), as well as in the Screen sheet's "Text &
  colour" group. Like TEXT SIZE it is a sheet row, so it is not repeated in the Project section.
- Style: `.gset .setbuild` (small, muted, tabular figures, selectable text so the number can be copied).

## 25 Oct (4): the Tone drum's level (the "clipping" the owner heard)

Cache `thunderplus-v231` (only `index.html` changed).
- **What it was:** not dropouts. `ToneDrum.render` synthesises a peak of about 1.18 and applied a fixed `* 0.8`, so a single hit
  came out at 0.939 (-0.5 dBFS) with no headroom to stack. With its 320 ms decay, a 16th-note pattern at 120 BPM reached
  1.851 (+5.4 dBFS), so the safety limiter (ceiling -1 dBFS, which the song output does pass through: `rt.out` → `safeLim`)
  was held down ~6 dB for as long as it played. That pumping/distortion is what sounded like clipping. The render is 70–110 ms
  for a 2.26 s buffer, but pads render in the worker, so it was not a main-thread dropout.
- **Fix (owner's choice):** the trim is now `* 0.5`, 4 dB less. One hit peaks at 0.587 (-4.6 dBFS), a dense 16th-note pattern
  at 1.157 (+1.3 dBFS) so the limiter is only touched ~2.3 dB at the densest moments, and sparse patterns do not touch it.
  The voice's own Level control still scales in dB. **Tone drums are 4 dB quieter in every song that uses them**, which was
  accepted; raise the pad's LEVEL where one needs to sit louder.
- **Tests:** `tests/tone-drum.test.js` pins a single hit's peak (-4.6 dBFS band), the 16th-note pattern peak, and the Level
  control. Reverting the trim to 0.8 makes them fail, so the level cannot drift back up unnoticed. `ToneDrum` itself is
  unchanged otherwise and is not covered by the other golden tests (those are …chains, …waves and the mastering chain).

## 25 Oct (3): the Mixer has its own size

Cache `thunderplus-v230` (only `index.html` changed). Looked at in Chrome on Desktop by the owner; not measured from here.
- **MIXER SIZE** (Project, beside TEXT SIZE): 60–200 %, saved per device (`thunderplus-mixsize-v1`, default 100). The Mixer no
  longer takes TEXT SIZE and TEXT SIZE no longer moves it, so sizing the tracker leaves the Mixer alone and the other way
  round. Mobile was already at 100 % and does not change.
- **Why 100 % is bigger than before:** the Mixer is fitted to 20 rows (VOL to MASTER), and on Desktop TEXT SIZE defaulted to
  80 %, so the Mixer used about two-thirds of the available height. At its own 100 % it fills the grid: roughly 25 % larger in
  each direction. Above 100 % it takes whatever room is left, never past the screen: `fitCheck` is never bypassed for the
  Mixer (it must keep every row visible), where TEXT SIZE is allowed to scroll or run past on purpose.
- **Not the font cap:** the size is height-bound by those 20 rows, not by the 30 px font cap. If it still reads small, the next
  lever is giving the Mixer more vertical room (drop the title / info line on that screen, or shorten the meter row) — a layout
  change, not a size change.
- Implementing it: `mixScale` / `mixScaleDef` / `setMixScale` / `MSK` beside `tScale`; the effective scale is chosen in `fit()`
  (`tv === 'mixer' ? mixScale : tScale`) and used in `fitCheck`, the cursor's sideways-scroll key (`hk`) and the title-line
  fit (`thdFit`). A test in `tests/app-logic.test.js` covers the default (100), a stored value, both ends of the range, garbled
  or out-of-range values, and blocked storage.

## 25 Oct (2): …chains buses and sends, and CHN no longer adds I/O

Cache `thunderplus-v229` (only `index.html` changed; no worklet or editor file did). Not played by ear: the routing plan, the
toggle and the engine's bus mixing are covered by tests, but the page's audio wiring in a browser is not.
- **BUG: switching CHN on from the Mixer rebuilt the patch.** `setChainOn` → `ensureChainPair` added a Track In **and** a
  Chain Out for the track. On a pure bus source (a Track In wired into a Chain Out on another track) that added back an
  output — turning the bus into a send — and a second Track In besides, which is why "the inputs did not work". It now adds
  nothing when the track already has any Track In / Chain Out in the patch; a track with no I/O of its own still gets the
  straight-through pair, so one switch from the Mixer is still all it takes. If the CHN comes on by itself (adding I/O in
  the editor, `chainsAutoOn`), nothing is added either way.
- **BUG: a Chain Out for a track that brings its own sound in counted as a bus.** With tracks 4 and 5 keeping their outs and
  also feeding track 6, the plan read "4 feeds bus 5" and "5 feeds bus 4" — a feedback pair the loop check then skipped, so
  the send only half worked and the bus column was asymmetric. `busPlan` now treats a Chain Out on a track that has its own
  Track In as that track's own output, never a bus (owner, 25 Oct). **A bus is a Chain Out with no Track In beside it.**
  Consequence, accepted with the owner: you cannot send into a track that also has its own input. The alternative was a
  per-Chain-Out "Bus" switch and a patch-format change.
- **A track still needs CHN on to feed a bus** (unchanged; confirmed with the owner), and the bus track needs CHN on because
  its node is what hears the feeds. Both ends are what the Mixer's BS/ON show.
- **MISS said the wrong thing.** When the Chain Out existed but its target track's CHN was off, the Mixer said "patch needs a
  Chain Out set to <this track>", which sent the owner looking for an output already in the patch. `chainOutTargetsOf(t)`
  walks the patch without the CHN test, so the cell now says "its Chain Out feeds track N, whose CHN is off". The other case
  keeps a version of the old wording.
- **Engine check:** a `thunder-chains` node given two tracks on inputs 4/5 with `busmap` mixes them (peak 0.38 for 0.3 +
  0.2); with no map it is silent. So the DSP and the host node were never at fault. **If a bus shows BS but is still not
  heard, the next place to look is `busSync()` (the splitter / 32-channel merger wiring), not `busPlan`.**
- **Tests:** five in `tests/app-logic.test.js` — the bus patch, the send patch, the both-ends-need-CHN rule, and the CHN
  toggle adding nothing / adding a pair for an untouched track. Each of the three rules was reverted in turn and a test
  failed (mutation-checked), so they cannot quietly regress.

## 25 Oct: a test suite, the Function generator hang, and failures that used to be silent

Cache `thunderplus-v228`, `chains/chains-worklet.js?v=38`, `waves/waves-worklet.js?v=31`, `pump/ui/kit.js?v=3`. **Not yet played
by ear or released** (no desktop build): the changes were checked by `npm test` (below) and in Chrome's console on a local server,
nothing more. Play a pattern, an engine switch, a pump window, and the Function generator in Audio range with Sync on before
releasing.
- **BUG FIXED: the Function generator (`tides`) hung the audio thread** in its Audio range with a clock, either Sync to tempo
  or a cable on Clock (every division and tempo; the first block never returned). Nothing in the editor stopped a user choosing
  it. The loop is inside the Mutable Instruments Tides ramp extractor in `mods.wasm`, which cannot be rebuilt without wasi-sdk
  (not installed here; see `dsp/mods/build.sh`), so `TidesM.process` now does not hand the wasm a clock in the Audio range
  (`const audio = p.range >= .5, clk = !audio && ...`) and Frequency decides, as with no clock. Control range is unchanged.
  This is a guard, not a cure: if the wasm is rebuilt, the extractor's audio path is the thing to look at. Regression tests in
  `tests/hangs.test.js` run the render in a child process with a time limit (a hang cannot be asserted in-process).
- **`npm test`** (root `package.json`, no dependencies, Node 20+, ~10 s, 52 tests; `tests/README.md` explains it). Loads the
  worklets and page scripts into a Node `vm`: no browser, no audio device. Every chains and waves module is rendered in three
  parameter sets and the float bits are hashed against `tests/golden/*.json` (657 hashes); the processor wrappers
  (`ChainsProcessor`, `ThunderChains`, `WavesProcessor`) are driven with a signal loud enough to reach the `soft()` output
  limiter; the mastering chain is tested for what each stage does as well as for its hash. `npm run test:golden:update`
  accepts an intended change to the sound; review the golden diff first. **A golden hash says "unchanged", never "correct"**,
  and modules that need a gate or clock are rendered without one (see the comment above `SKIP` in `chains.test.js`).
  Mutation-tested: 20 deliberate regressions injected, each must fail a test; the ones that first slipped through (the soft
  limiter knee, Bus Comp's `clip`, the limiter's default ceiling, the waves output limiter) are covered now.
- **Failures that used to be invisible are logged.** `tpLog(where, err)` (top of `index.html`; `window.tpLogs` holds what was
  printed) is console-only, prints each distinct message once and stops at 200, so a failure in a render loop cannot flood
  it. Global `error` / `unhandledrejection` handlers use it. Wired to: every WASM load / compile / instantiate failure (page,
  render Worker via its `ready` message's `errs`, both worklets, the Random / Function generators' wasm, a crashed effect once
  per instance), a failed kv write (`kvSet` / `kvDel` also log from the transaction's error / abort, which never reached the
  `catch`), service-worker registration, the song / IR load and the converted-song save, and the editors' save hand-off.
  **About 390 empty `catch`es remain on purpose** (audio-node teardown, `localStorage`, focus): logging those would flood the
  console during playback and hide the real ones. Leave them silent, and say why in a comment if you touch one.
- **Editors' unsaved flag** (`PatchStore.save`, waves and chains): inside Thunder Plus the hand-off to the page (`TW.save`)
  decides `dirty`, so a failed hand-off stays dirty and the pause / pagehide / leave paths retry it. It used to clear
  `dirty` regardless and the edit was forgotten until the next one. On its own, the `localStorage` write decides.
- **Memory:** `IRDATA` (decoded impulse responses) was never pruned, so every project opened with IRs added a few MB until a
  reload. The existing 60 s prune of `smpHeld` now drops `IRDATA` entries that nothing refers to (the song's IRs, undo history
  and presets keep theirs; `irsResolve()` reloads from IndexedDB if one is needed again). It still only runs with 8+ samples
  held and not while playing: that was the original gate and was left alone.
- **Pump windows:** a draw or tick that throws is reported once per message (`warnOnce` in `pump/ui/kit.js`), not ~60 times a
  second for as long as the window is open.
- **Desktop updater** (`electron/updater.js`): the direct download (Mac .dmg, portable .exe) is hashed as it streams and checked
  against the SHA-256 GitHub lists for the asset (`digest: sha256:...`); a mismatch is deleted and never opened. A release
  with no digest behaves as before. This catches corruption and a swapped file, **not** a compromised release (the digest is
  from the same account).
- **Content Security Policy, report-only, desktop only** (`electron/main.js`, a `Content-Security-Policy-Report-Only` header on
  the `app://` pages). It lists what it would block and blocks nothing; read the console of the desktop app, fix what it
  reports, and only then make it enforcing. The website has none: a `<meta>` tag cannot carry Report-Only, so on GitHub Pages
  it would enforce at once. The app is inline `<script>`, so `'unsafe-inline'` has to stay until those move out or get
  nonces; today it stops loading anything from another site and locks object / base / form, not injected inline script.
- **Section names** in the Arrange lane use `esc()` rather than stripping `<>&"` (a section called `R&B` now shows as `R&B`).
- **`modsWhy` is in the SHARED MODULES block**, so its text is one neutral label. `chains/` is the source of truth: edit there
  and run `python3 tools/sync-shared.py` (a test fails if the two blocks differ). An earlier hand edit gave the two files
  different labels, which the next sync would have silently undone.
- **Left alone on purpose.** (a) `inAI` / `window.claude` (about a dozen branches): not dead code, the in-app text says
  "Inside AI, files come out as .zip" and "MIDI is blocked inside AI"; remove it only if running inside a Claude artifact is
  no longer supported. (b) The IPC file read / save accept any `.thunder` / `.json` path: the page legitimately passes paths the
  main process has never seen (a dropped file, the stored current-file path, the recent list), so an allow-list would need to be
  remembered across restarts and a mistake makes Save fail on a user's project. (c) The waves oscillator starts every voice at
  `Math.random()`, which the engine's `seed` does not control; a musical choice, and the tests give their scope a deterministic
  `Math.random` instead (`freezeClock` in `tests/helpers.js`). (d) `localStorage` helpers repeated in `pump/ui/*.js` differ on
  purpose (number-checked, JSON, raw string); each file is its own function scope, and a shared module would be one more thing
  every pop-up depends on.
- **Corrections to the 25 Oct code review.** The "duplicated worklet / editor code" is mostly deliberate: `tools/sync-shared.py`
  copies the shared blocks from chains into waves (what differs, `mixDef`, `normalize`, `blankDoc`, differs for real waves-versus-
  chains reasons). `innerHTML` is not a glaring XSS risk (dynamic sites use `esc()` or numbers and constants). The one Electron
  sandbox setting I doubted (`contextIsolation`) is on.
- **Chrome's console on `file://`** shows ~28 CORS errors (fonts, manifest, every `.wasm`): the page was opened by double-click.
  It needs a server (`python3 -m http.server`) or the desktop app; `file://` was never supported.

## 24 Oct: Windows fixes, reverb Mix curve, mouse on values, header meter, LXR presets — desktop v1.2.22

Cache `thunderplus-v225`, `chains/chains-worklet.js?v=36`, `waves/waves-worklet.js?v=29`, `lxr/lxr-engine.js?v=2`. Each
change is its own commit, tested in headless Chromium (and the DSP in Node) before it went in.
- **Arrange boxes on Windows.** `.tagrid` scrolls with a real scrollbar there (17 px+, none on Mac / phones), so the blocks
  were narrower than the MONO / POLY / MST tabs and track numbers above them, and the boxes (measured from the header) drifted
  up to ~16 px by MST. `arrSbw()` sets `--arsb` on `#tArr` and `.tath` / `.tagp` take it as `margin-right`.
- **…pump meters on the FX screen.** `stageRoom()` measured each line with a Range over the whole `.ln`; the description line's
  `.fxdesc` is `display:block` up to 68ch, so a 21-character "pump COMP: compressor" reserved ~540 px and at 150 % scaling the
  stage fell under its 150 px minimum and hid. Empty slots have no description, which is why the meter only showed there. It
  now measures `.fxdesc`'s client rects (the text). Built-in effects benefit too.
- **Reverb Mix curve** (`rvMix(m)` → `[cos(πm/2), sin²(πm/2)]`): 10 % −32 dB, 35 % −11 dB, 50 % a real middle (dry −3, tail −6),
  100 % tail only. Before: Room / Space held the dry full while the tail reached full by 50 % (Room 35 % = −3 dB), Galactic was
  1−(1−m)³, Spring / Pitch reverb crossfaded with the tail ×1.4. In …chains / …waves the shared block has `rvMix` and
  `effect(…, wetAt)` runs Galactic / Space / Room fully wet and blends in JS; Spring, Plate and Pitch reverb's reverb branch use
  it (Pitch **delay** keeps `1 − mix/2`). In Thunder: `rvMix` is a function declaration in the ENGINE block (shipped to the
  track-FX worklet by `toString()`), pad Space / Room are `blend:true` (Room `blendKey:'wet'`) and blend in `txRun`, track slots
  t 8 / 20 / 24 set `s.wi` and `slotRun` blends against its dry copy, and Thunder's own `PitchFxCore` reverb branch uses it.
  All measured on a noise burst: every reverb, pad and track, within 0.3 dB of the curve. Songs sound drier at the same setting.
- **Mouse on values** (VALUE MOUSE, `valCell`): Mixer VOL / PAN / MASTER and the FX, INST, PROJECT rows' values change on a drag
  (16 px a step, `bump`) or the wheel from the first press, instead of moving between lines; a click only selects (`drag.tap`),
  a click on the selected value is EDIT as before.
- **Mixer fader bars** (MIXER FADERS, `mixFaders`): a bar over each track's meter at its VOL on the meter's scale (0 dB at the
  top, 6 dB a line, half a line above = +6 dB / FF). Drag, Shift fine, double-click = E0; one undo step per drag.
- **Header meter** (HEADER METER, `#hMeter`, `hmFrame`): master L / R between the screen buttons and File, −48…0 dB, amber over
  −6, a peak tick held 1 s, a red end after a clip (click clears). Own analysers on `master`, re-attached when `master` changes.
  Hidden under 700 px.
- **Keys / defaults:** `\` mutes the cursor's track (as `/`); Desktop starts Phrase in pattern mode at 80 % text
  (`tScaleDef`, `pvMode`) unless the user already chose.
- **Engine cards:** ◀ ▶ around each card's Presets button step its presets (PRESET STEP, `preAt` per pad + engine); the button
  shows the preset's name.
- **LXR presets:** 41 (`PRESETS` in `lxr/lxr-engine.js`, written by knob name through `alias()` / `named()`), each rendered and
  checked for pitch, length and peak < 1.0. Also in `lxr/src/lxr-engine.src.js`, so a rebuild keeps them.
- **…chains:** a mixer's Pan sliders draw at half opacity (`.row.sl.pan`), so level and pan are easy to tell apart.
- **Not a bug:** engine switching doesn't play the voice; what the owner heard was a pattern sending it a note.

## 23 Oct (7): …pump pop-up windows for CALM, TONE, BAND, WIDE and HEAT (stage 3 of 3)

Cache `thunderplus-v222`, `pump/ui/multiband.js?v=1`, `calm.js?v=1`, `tone.js?v=1`, `band.js?v=1`, `wide.js?v=1`, `heat.js?v=1`
(new; index.html's `…PUMP WINDOWS` block, multiband.js before band / wide / heat; `sw.js` FILES). All nine pump effects
now open their own window from the FX screen's **› OPEN** (and the bottom panel's OPEN). Same approach as stage 2: each
window is its plugin's `UI<Name>.cpp` on the kit's canvas, in the plugin's coordinates. **Read `docs/PUMP-FX.md`**,
"Multiband windows" and "What stage 3's windows do".
- **`pump/ui/multiband.js`** ports pump's `common/ui/MultibandUI.hpp` as `PumpUI.Multiband(ui, cfg)`: band regions between
  draggable crossover handles (drag, Shift fine, wheel 1/12 octave, double-click to type with Tab to the next), the S / M / B
  strip (click, hold = momentary, Ctrl/Cmd-click = only this band), Alt + hold a band = hear only it (`PumpFX.hold` kind 0,
  slide across to switch, released on up / Alt up / blur / close), drag / wheel / double-click in a band = its main amount,
  Ctrl/Cmd + drag = its second, the band menu (solo, mute, bypass, copy / paste, reset), keys 1–4, ←/→, S / M / B, Esc (clears
  every solo; with none, closes). Selecting a band moves the FX screen's BAND (`setSel`) and back.
- **What each window has:** CALM: spectra, the live reduction from the top (two detectors in L/R, M/S), the sensitivity curve
  and its six nodes, focus edges, Alt-listen and Alt-hold node solo (kind 1 with region), DELTA, keys. TONE: spectra, the
  balance curve, auto level line, target curve, Alt-listen, DELTA, A / D / T. BAND: thresholds on the level scale with
  detector bars, the GAIN lane (drag = makeup) with the combined response, band panel with transfer curve. WIDE: mid / side
  spectra, width bars with stereoize hatch, mono-bass line (draggable), phosphor vectorscope, correlation overall (lowest
  hold) and per band, the band image. HEAT: band glow, drive bars, curve picker, into-curve / heat / auto gain readouts,
  transfer curve, oversampling, latency. Narrow layouts (480 wide) stack the same parts for a folded phone.
- **No interface changes** to `PumpFX`, the worklet or pump.wasm: every feed the windows need was already in the telemetry.
  The kit is unchanged; `PumpUI` gains `Multiband`, `mbEffective`, `mbBandMag`, `MB_STRIP`. pump repo untouched.
- **WIDE's vectorscope** is the plugin's phosphor image, but sized 1× or 2× the scope (nearest to the screen's pixels: window
  scale × device pixel ratio) instead of always 2×, with the splat and margins scaled to match, and decay + repaint in one
  pass over a 32-bit view: paint 82 → 41 ms/s on a 1× desktop (a 2× phone draws as many pixels as the plugin).
- **Left out / different** (as stage 2): display settings the plugins keep in their UI state are kept on this device (BAND's
  GAIN range, CALM's GR scale and listen width, TONE's curve scale, target on/off, listen width; localStorage
  `thunderplus-pump-band-lane-v1`, `…-calm-gr-v1`, `…-calm-lq-v1`, `…-tone-range-v1`, `…-tone-target-v1`, `…-tone-lq-v1`); copied
  band settings are per page; CALM's and the multiband windows' Esc does the plugin's thing first, then closes.
- Checked headless (Playwright; 1600 × 1000 mouse, 884 × 1100 and 412 × 915 touch at DPR 2), demo song playing, bus = tracks
  1 + 2 → …chains Chain Out 4: each of the five opened from slot 1 of the bus and of the master; dragging a dial moves `f.pump`,
  the FX box and the sound (BAND / WIDE / HEAT output −24 dB: RMS −22 → −46 dB, TONE −12 dB: −17 → −29); an FX box moved → the
  window follows; a crossover dragged (120 → 177 Hz, box `50 177 Hz`) and typed (250); Alt-hold a band = hold kind 0 with the
  band, low band −18 dB vs top band −39 … −44 dB against −15 dB full, released → −15 dB, nothing saved; touch: the SOLO / DELTA
  keys held = on while held, off after; CALM node drag, Alt-listen (−18 → −48 dB around 1 kHz) and node solo (regions below /
  band / above for nodes 1 / 3 / 6); TONE Alt-listen (−17 → −55 dB); strip M, Ctrl-click S exclusive and again clears, keys
  2 + B; the FX screen's BAND row moves the window's band; one drag = one undo step (BAND threshold 0 → −19.3, Ctrl+Z → 0); a
  preset from the window's menu applies exactly, Ctrl+Z / UNDO removes it; Esc / ✕ close, no telemetry frames after, reopen
  shows the slot; five windows at once (desktop), one at a time (phone); save → reload identical; WIDE correlation with
  stereoize 1 → 0.2 per band, scope spreads. CPU (headless software rendering, paint / frame handling ms per s at ~20 fps):
  CALM 33 / 21, TONE 22 / 16, BAND 29 / 14, WIDE 41 / 17, HEAT 28 / 18. Stage 1 checks (t1–t7) and stage 2 checks (p2–p7) give
  the same results as before. No page errors. **Not tried on the Fold or by ear.**
- Screenshots (session scratchpad `pop3/`): `cmp_calm.png`, `cmp_tone.png`, `cmp_band.png`, `cmp_wide.png`, `cmp_heat.png`
  (plugin above, pop-up below, same preset and theme), `desk_*_bus.png`, `desk_*_master.png`, `fold_*`, `unfold_*`,
  `desk_five_windows.png`, `desk_wide_stereoize.png`.
- **Next:** delay compensation for buses (none in Thunder yet), a try on the Fold, and by ear.

## 23 Oct (6): …pump pop-up windows for EQ, COMP, LIMIT and METER (stage 2 of 3)

Cache `thunderplus-v221`, `pump/ui/kit.js?v=1`, `eq.js?v=1`, `comp.js?v=1`, `limit.js?v=1`, `meter.js?v=1` (new; index.html's
`…PUMP WINDOWS` script block after the main script, `sw.js` FILES; `electron/copy-app.js` already copies all of `pump/` and
checks the refs). Owner: "a pop up window that you can open from the FX screen" for the pump FX on buses and the master.
**Read `docs/PUMP-FX.md`, "The pop-up kit"**: the kit's API and how stage 3 adds CALM, TONE, BAND, WIDE, HEAT.
- **Design: the plugin's own window on a canvas.** Each window draws in the plugin's logical coordinates (EQ 1040 × 660,
  the rest 1000 × 620) scaled to the window, so the layouts, drawing and mouse code are line-by-line ports of
  `plugins/<p>/UI<Name>.cpp`, and `kit.js` ports `common/ui/PumpUI.hpp` (header with brand / tag / kind, preset menu with
  sections and Save preset…, UNDO / REDO, A / B / COPY, ?, DARK, BYPASS, plus ✕; Dial, SliderRow, Button, Segmented,
  MenuButton, value fields; typed entry (1k, A4, C#3+13, 2x, 50 %, inf; a real `<input>`, so a phone shows its
  keyboard) with Tab rings; menus; status line; help overlay) and `Theme.hpp` (all nine accents, light / dark). Text sizes
  are converted from NanoVG's (ascent-to-descent) to CSS em: labels × 0.887, mono × 0.758; the narrow layouts read 12 %
  larger. `SpectrumView` (Blackman-Harris, 320 log points, +4.5 dB/oct, ballistics) is ported too; EQ's pre and post
  analyzers share one complex FFT.
- **Light / dark:** `THEME = 'skin'` in kit.js (a dark Thunder skin gives pump's dark look); DARK overrides it on this device
  for every window; Ctrl/⌘-click DARK follows the skin again. One constant to change.
- **Windows:** desktop: several at once (one per slot, cascaded, OPEN again raises it), moved by the header's brand,
  resized from the corner (uniform scale, remembered per type), fitted to the screen. Below 640 css px: the narrow layout
  (480 wide; each plugin file has one), full screen under the top bar, one at a time. Esc, ✕ and Back close (Back:
  `PumpFX.backHook` in the popstate handler). Keys typed in a window stay in it (`typing()` asks `PumpFX.ownsKey`), except
  Space (Thunder plays / stops) and Ctrl/⌘ shortcuts the window doesn't use (save, open…).
- **Thunder mappings:** BYPASS = the slot's ACTIVE (METER has none, as the plugin); UNDO / REDO / Ctrl/⌘+Z = Thunder's undo
  (the song's last change; a drag or a gesture is one step: live sets while dragging, one plain set at the end); A / B per
  slot for the session; presets through `PumpFX.applyPreset` (Factory / Sources / Chains) plus your own on this device
  (`localStorage thunderplus-pump-user-presets-v1`; `pname` = its name via the new `set(…, {preset})`); COMP's External
  sidechain menu lists Thunder's sends and tracks (new `PumpFX.setSck` / `sckNames`, the FX screen's SC FROM).
- **What each does:** EQ: the curve display with pre / post analyzers, per-band fills, dynamic range shading and the live
  dynamic movement, nodes (drag = freq + gain of the selection, Shift fine, Alt one axis, Ctrl/⌘-drag Q, wheel Q / slope,
  Ctrl/⌘-wheel gain, Alt-wheel dynamic range), double-click or Ctrl/⌘-click to add (Alt: dynamic), drag the curve to add,
  Alt-hold a node = solo (hold kind 0), Alt-hold / drag empty space = listen (kind 1, wheel / up-down width), box select,
  Shift-click range, Ctrl/⌘-click toggle, right-click band / display menus, Delete, B, Esc, ←/→, Ctrl/⌘+A/C/V/D, SOLO latch
  (a hold, never saved), the band panel and globals, ±dB range, OUT meter. The selected band follows the FX screen's BAND
  and back (`setSel` is now a view change: no undo step). COMP: history (in / out / GR, 4.8 s), threshold line drag /
  wheel / type, transfer inset (drag threshold, Ctrl/⌘-drag ratio, wheel knee, typed ratio → knee → threshold), In / Out /
  GR meters with holds (click resets), detector bar, sidechain (HP / LP / SC GAIN, LISTEN latch / hold), styles, all dials
  with the plugin's value text, ↑↓ threshold, ←→ style. LIMIT: 5 s history (in, out, GR, short-term LUFS), gain drag,
  ceiling line drag, target line, -n s chips, meter scale (top 12 dB = half the height) with holds, loudness screen (click / R resets: hold kind 2),
  styles with their descriptions, timing, channel link, options (true peak, DC, OS, UNITY latch / hold, dither menu), keys.
  METER: integrated with target colours and delta, loudness bar, S / M, the eight statistics, history since the reset
  (1 / 3 / 10 / 30 min, momentary band, short-term and integrated lines, target band), spectrum, correlation (lowest
  hold) and balance, L/R peak + RMS meters with true-peak holds, target / custom fields / history / scale, RESET and R.
- **Left out** (in the plugins): user presets are on this device, not files; the plugins' own undo history (Thunder's undo
  instead); "remember DARK for new instances" is this device's setting. Telemetry only while a window is open.
- **Fixes to stage 1:** unsubscribing one telemetry subscriber stopped the feed for every subscriber of that slot (the
  worklet keys feeds by slot): now it stops only when the last one leaves (`shared()` in PumpFX). Undo / redo and loading a
  song now send `onChange` (`'undo'`, `'load'`) as the doc said; `set` / `applyPreset` / `setOn` redraw the FX screen and
  the bottom panel (`drawSoon`, 80 ms).
- Checked headless (Playwright; 1600 × 1000 mouse, 884 × 1100, 412 × 915 touch at DPR 2), demo song, bus = tracks 1 + 2 →
  …chains Chain Out 4, playing: each of the four opened from slot 1 of the bus and of the master (screenshots below);
  dragging an EQ node moves `f.pump`, the FX box (byte and text) and the sound (post RMS −16.4 → −15.6 dB; typed +12 dB →
  −10.8 dB); an FX box moved → the window follows; undo (Ctrl+Z in the window) restores; double-click / double-tap adds a
  band at 1 kHz +6 dB, drag moves it (1 → 2 kHz), Alt-hold / SOLO solos it (−17 → −39 dB RMS, released on up), Delete /
  DELETE removes it; preset "Master: tilt brighter" from the window's menu applies exactly (FX row shows it), Ctrl+Z
  undoes it; LIMIT gain dial dragged 0 → 13.5 dB: GR 2.9 → 17.1 dB, FX box +14 dB; two windows at once (desktop), one at a
  time (phone); save → reload: identical slots, window reopens with them; A/B, Save preset… / load, COMP External from
  track 01 (GR −29.9 dB, SC FROM box TRACK 01), LIMIT and METER resets, ↑↓ stay in the window (tracker cursor unmoved),
  Space plays / stops, Esc and Back close, a dark skin gives dark windows, DARK and Ctrl-click DARK, wheel Q, Alt-drag
  listen, window move / resize, help, touch long-press (band menu, typed entry), the window closes when its slot is
  emptied or changes type, a stage-3 type gets the toast, no paints 5 s after stopping, no frames after closing. CPU
  (headless software rendering, main thread): Thunder playing ~290–310 ms/s; a window adds ~80–180 (paint 20–38 ms/s at
  20–25 fps, EQ / METER spectra ~16 ms/s; the rest is canvas rasterising, cheaper with a GPU). The stage-1 checks (t1–t7 in
  the session scratchpad) give the same results as before. No page errors. **Not tried on the Fold or by ear.**
- Screenshots (session scratchpad `pop/`): `desk_*_bus.png`, `desk_*_master.png`, `dark_*.png` and `cmp_*.png` (plugin
  above, pop-up below), `fold_all_bus.png`, `fold_all_master.png`, `desk_two_windows.png`, `fold_eq_presetmenu.png`,
  `listen_help.png`, `fold_eq_longpress.png`.
- **Next (stage 3):** CALM, TONE, BAND, WIDE, HEAT: port `common/ui/MultibandUI.hpp` into the kit first (BAND, WIDE, HEAT),
  WIDE's vectorscope from rings 0/1; one file each plus the script line, sw.js entry and cache bump.

## 23 Oct (5): the nine …pump plugins as bus and master effects (stage 1 of 3: sound, FX boxes, the pop-up interface)

Cache `thunderplus-v220`, `pump/pump.wasm?v=1`, `pump/pump-meta.js?v=1` (both new; `sw.js` FILES and `electron/copy-app.js`
PARTS / its refs check have `pump`). Owner: "add these to thunder plus as FX I could use on Bus and the master track", with
"parameters in that screen that look like all of the other plugins"; later stages add "a pop up window that you can open
from the FX screen" with each plugin's whole interface. The mastering window (`master-worklet.js`, `S.mst`) is untouched.
**Read `docs/PUMP-FX.md`**: the slot format, `window.PumpFX` (the interface the pop-ups build on), telemetry layouts,
latency, CPU. Search `…PUMP` in index.html.
- **DSP = the plugins' own code.** matthettich/pump (local commit, not pushed): `wasm/pump_wasm.cpp` (one C interface;
  `Host<Name>` = each `Plugin<Name>.hpp` without DPF: same prepare/activate/sync/run, chunking, BypassMixer, sanitising,
  ScopeRings / MeterValues), `wasm/pump_meta.cpp` (Params.hpp tables → `pump-meta.js`, so presets and ranges stay in
  parity), `wasm/build.sh` (wasi-sdk 25, `-O3 -ffast-math -fno-finite-math-only -msimd128`, pffft's wasm SIMD; 582 kB, imports
  fd_close/fd_seek/fd_write only), `wasm/compare.cpp` + `compare.mjs` (wasm vs native), `tests/wasm_wrapper.cpp`. No threads
  in a worklet: new compile option **`PUMP_SYNC_KERNELS`** (Util.hpp, FFT.hpp, Multiband.hpp, EQEngine.hpp) designs the
  worker-thread kernels on the calling thread (EQ linear phase: at most one kernel build per 80 ms of audio while values
  move; it allocates on the audio thread, so expect a crackle when dragging EQ values in linear phase on a phone). Plugins
  unchanged (`make`, `make tests` pass; the tests now also run EQ/BAND/WIDE/HEAT built with the option, and the wrapper test).
- **Slots** (`TFX` keys `peq pcomp plimit pcalm ptone pband pwide pheat pmeter`, short names pEQ pCOMP pLIM pCALM pTONE pBAND
  pWIDE pHEAT pMETR, group "Mastering (…pump)" last in the picker): `f.pump` holds **every** plugin parameter by symbol (plugin
  units); `f.on` is the plugin's bypass (latency-matched; `pump.bypass` ignored); `f.sel` the band the band rows show; `f.sck`
  COMP's External sidechain feed (Thunder's Source list: sends, tracks; `sckUses` / `slotScks` include it, so taps connect);
  `f.pname` last preset. The FX boxes are bytes `f.p_*` mirroring `f.pump` (`pumpBytes` writes them from `f.pump`, `pumpSync`
  writes a moved box into `f.pump`; `fxChanged` runs it first). `freshTfx` / `normTfx` / `tfxChain` branch on `TFX[k].pump`.
  Box display: the exact value while the byte still matches it (a preset or pop-up can set values between byte steps).
- **FX-screen boxes** (`PUMP_BOX`; a choice is its name, on/off a toggle, small whole numbers themselves, the rest 00–FF on
  the plugin's own knob curve): EQ out_gain, auto_gain, phase, BAND (1–24) + that band's used, shape, freq, gain, Q, slope,
  place, dyn, dyn range, dyn thresh · COMP style, threshold, ratio, knee, attack, release, auto release, range, output, auto
  makeup, mix, SC source, SC FROM (Thunder's), SC HPF · LIMIT gain, ceiling, style, lookahead, attack, release, true peak,
  oversampling · CALM depth, sharpness, selectivity, attack, release, mode, focus low/high, mix, quality · TONE recover, tame,
  bias, brighten, body, speed, auto level, output, mix, quality · BAND bands, xover 1–3, crossover, output, mix, auto makeup,
  BAND (1–4) + mode, thresh, ratio, attack, release, range, makeup · WIDE output, balance, mono bass, mono freq, bands, xover
  1–3, BAND + width, stereoize · HEAT output, mix, oversampling, auto gain, bands, xover 1–3, BAND + mode, drive, band mix,
  level · METER target, custom LUFS, custom TP. Then **› PRESET** (the factory list in the FX picker, FACTORY / SOURCES /
  CHAINS headings: `pumpPresetOpen`, `fxp.pre`), **› OPEN** (`PumpFX.open` → `PumpFX.openHook`, a toast until the pop-ups
  exist) and METER's **› RESET**. The bottom panel's FX part shows the same rows plus PRESET ▾ / OPEN (and now shows choice
  names for every effect's choice rows; it printed a % before).
- **Only buses and the master**: `fxtL(t)` (the picker, SLOT stepping, `fxsChoose`) hides pump types unless `pumpAllowed(t)`
  (`t >= NT || isBusT(t)`); `fxActiveStep` / ADD TO SLOT fall back to EQ. A slot keeps its effect if the bus stops being one.
- **Worklet** (`TFX_SRC`): pump.wasm goes to each audio thread once, in the first effect node's `processorOptions.pump`
  (`pumpSent` per context), else a node asks (`{needPump}` → `{pumpBytes}`; `pumpWait` holds askers until the fetch
  lands). One module instance per audio thread (`globalThis.__pumpX`), a handle per slot; values go by index (`pv`, with
  `bi` = bypass index), `pump_commit` diffs them like a host. Telemetry: `pump_tel` once per report (it exchanges the meters);
  with `{tel:true}` (FX stage) COMP / LIMIT / BAND feed the dynamics display, METER / LIMIT their numbers (`{pump:[…]}` →
  `node.pmeter`); a pop-up's `{ptel:{key,on,back}}` adds the full frame with each ScopeRing's new samples. `{phold}` =
  Alt-listen. `{plat}` reports latencies when they change. NaN / crash: restart or bypass, as the other wasm slots.
- **FX stage** (1920-wide screens; at 1600 the stage has no room, as for every effect): pCOMP draws its curve + GR
  history, pLIM the limiter curve, pBAND the GR lane (no single curve); a METER slot shows short-term / integrated /
  momentary / LRA / true peak / PLR (`pumpMeterPaint`).
- **Latency**: no compensation in Thunder's FX chains. Master: harmless live; **renders now trim it** (`pass()` adds
  `pumpChainLatency(S.mfx)` to the mastering's latency; `pumpLatencyOf` asks a main-thread copy of pump.wasm, `pumpMain`).
  Bus: plays late against the other tracks; the ACTIVE line / OPEN help / a toast for CALM, TONE, LIMIT on a bus say so.
  Pump types are in `TAILS` so renders run past their latency (the silence after is trimmed).
- **CPU / defaults** (node, this machine, ms per s of audio): EQ 1–5 (linear 10–14), COMP 8–16, LIMIT 35 (oversampling off;
  2x 45, 8x 76), CALM 10–12, TONE 11–16, BAND 12–22, WIDE 6–8, HEAT 11 (1x; 4x 43), METER 17; in the page each node's
  load was 0.7–4.7 % of real time. New slots start at the plugin defaults except LIMIT and HEAT oversampling off
  (`PUMP_START`); presets that don't set oversampling keep it (`PUMP_CPU`; `applyPreset(…, {keepCpu:false})` = exactly the
  plugin).
- Checked: wasm vs native build of the same wrapper on pump's test mix, 10 s, 49 effect/preset runs incl. a mid-run value
  change: identical for 13, worst difference −135.5 dB relative to the output (EQ linear phase), typically −140…−150 dB, same
  latencies. Headless (1920 × 1080 and 412 × 915), demo song, bus = tracks 1 + 2 → …chains Chain Out 4: each of the nine on
  the bus and on the master, put in through the FX picker; sound passes (telemetry out peaks), each acts (COMP GR −26 dB at
  −40 / 4:1; LIMIT +12 dB holds −1.0 dBTP, GR 15 dB; CALM 7.5 dB reduction; TONE band gains; BAND GR −24/−17/−8 dB, RMS −7 dB;
  WIDE correlation 1 → 0.6 with stereoize; HEAT level match; METER −15 LUFS); a box moves `f.pump` and undo/redo restores
  it; preset 1 of each applies exactly; COMP SC FROM track 1 keys it (GR 0 → −30 dB); a live drag is one undo step;
  `subscribe` gone on a type change; save → reload round-trips every slot (both rounds identical JSON); renders line the
  master up (onset sample identical with LIMIT, CALM + TONE on the master); the mastering window's limiter still limits
  (peak −1.9 → −3.9 dBFS, also with a pump COMP on the master); plain-track pickers list no pump effect; no page errors.
  **Not heard by ear; not tried on the Fold.**
- **Open / next stages**: the pop-up windows (stage 2–3; build on `PumpFX`, see docs/PUMP-FX.md "What the pop-up stage
  should do"); delay compensation for buses (none anywhere in Thunder yet); the phone FX screen has no dynamics display (as
  before); the bottom panel's FX part follows its own slot buttons, not the FX cursor (as before).

## 23 Oct (4): the LXR-02's voices as engines (lxr/, GPL-3.0, optional)

Cache `thunderplus-v219`, `lxr/lxr-engine.js?v=1`, `lxr/lxr.wasm?v=1`. Owner: Erica Synths open-sourced the LXR-02
firmware (https://github.com/erica-synths/lxr-desktop-app, GPL-3.0 with trademark terms); include it as an engine.
Owner's standing wish (licensing): anything with licence limits kept removable, in case Thunder Plus is ever sold
(see **Before a commercial release** above).
- **Everything is in `lxr/`** (read `lxr/README.md` and `lxr/NOTICE.md`): `src/firmware/` the firmware files the build
  reads, unchanged (commit a43cd4f), with its LICENSE and NOTICE.md; `src/lxr_wasm.c` the interface; `src/stub/` HAL,
  Cortex-M intrinsics (__SSAT, __QADD16… in C) and ST's USB header (empty, nothing used); `src/tools/lxr_meta.py` (host
  clang program over the firmware's menu tables → the knob pages, names, dtypes, option lists, modTargets, init kit);
  `src/lxr-engine.src.js` + `build.sh` → `lxr.wasm` (151 kB, no imports) and `lxr-engine.js` (META inlined). Toolchain as
  dsp/build.sh: clang 18 + wasi-sdk 25 sysroot/builtins (both downloadable here from GitHub releases), `-O3 -ffast-math
  -funsigned-char` (ARM's char is unsigned).
- **The wasm** (`lxr_*` exports): init (mixer, parameterArray, velocity mod nodes, the four voice inits), init_kit,
  load (= preset_sendDrumsetParametersImmediate: VEL_DEST / TARGET_LFO through modTargets, then parameterOrder through
  midiParser_ccHandler), trigger (= voiceControl_noteOn; 5 closed hat, 6 open), render (one voice, as
  mixer_calcNextSampleBlock does it before pan and FX bus; 64-sample blocks at REAL_FS 44140). Defined in the wrapper:
  activeParameters, the RNG (xorshift, seeded per render), adc_getFaderVolume = 1, seq_getBpm (lxr_set_bpm), seq_setMute
  / menu_* no-ops, menu_activePage 0.
- **Rendering** (`LxrCore.render`): the module's memory is restored to a copy taken after start-up before every render
  (the firmware keeps filter/osc/counter state its init functions leave, so the 2nd render of the same pad differed);
  init kit + the pad's bytes; other voices' VEL_DEST/TARGET_LFO cleared; 320 blocks to settle; trigger with note 60 +
  Tune; blocks until 0.3 s below 3e-5 or Max len (≤ 12 s); windowed-sinc to the app's rate (32 taps, 1024-phase table);
  Level/Drive as KeysUtil.finish but **not normalised** (a kit's balance carries over).
- **Engines** `lxdrum`, `lxsnare`, `lxcym`, `lxhat` (XENG via `LxrCore.xeng()`, XUI via `LxrCore.xui()`, ENGINE_GROUPS
  group "LXR-02 firmware" after Drums). Pad values: `p<param number>` (drum = drum 1's numbers), `vdst` / `ldst` (0 or a
  position in this engine's target list: its voice's modTargets range, only its own knobs), `vel` (Hit), `open` (hat).
  Sections follow the menu pages; the mod page is split into Pitch envelope and Velocity; rows use short names in
  their section (SHORT), target lists the full ones. Pan, audio out and the LFO's destination voice are left out.
- **index.html** (search `LXR ENGINES`): script tag before the engine block; XENG spread; `isLxV`; renderX calls
  `XENG[k].r(P, sr, hold, V, songBpm())` when an entry renders itself; the Worker blob starts with
  `importScripts(lxr-engine.js)` (when the page has LxrCore), init gets the page's compiled module (`lx`), `PW.lx`;
  `rwCan` needs PW.lx for LXR pads; the wasm loader compiles `lxr/lxr.wasm` alongside dsp/tfx and marks LXR pads dirty
  when ready; padBpm re-renders a pad whose LFO is synced and routed; pad Presets group (init kit, seven sounds);
  File ▾ → **Import LXR kit (.snd)…** (`pickLxrKit` / `importLxrKit`: 8 name bytes + params, onto pads 1–7 or from
  this pad). sw.js caches lxr files as OPTIONAL (a missing folder doesn't break install); electron/copy-app.js copies
  `lxr/` when present.
- **Removal** (before selling): delete `lxr/` (and optionally its script tag). Checked headless: no engines, no import
  item, a saved LXR pad falls back to Layers, only the script's 404 in the console.
- Checked: Node renders of all seven init-kit sounds (no NaN, kick/tom/click/snare/cymbal/hats look right on
  spectrograms), knobs act (decay, coarse, Tune = semitone, cutoff, velocity → decay), identical repeat renders; in the
  page (1600 and 412): engines in the list, cards, page and Worker renders, presets group, kit import through the
  dialog (edited values and drum 2's velocity target carried over). **Not heard by ear, not compared with a real LXR.**
- Open: compare against the owner's machine (same kit file, same note); LXR-02 user samples (SD card waveforms) aren't
  there; no export back to .snd yet.
## 23 Oct (3): PLAN — …sample, a wave editor (not started)

Status: **plan only, nothing built.** The owner will start this later; this entry is the brief.

**How to start (owner):** open a session on this repo and say *"Build …sample from HANDOVER.md, 23 Oct (3), starting with
step 1."* Mock-up: the design canvas "Sample editor mockup" (https://claude.ai/artifact/4mJaZERazXoijdPHmAEGq1, private to
the owner; read it with the Artifact tool's `read`). The layout is also written out below in case the link is gone.

**What the owner wants (23 Oct):** a wave editor like BIAS Peak with features from the Renoise sample editor. It should
look like Thunder Plus and be easy to bring into it as another screen. Later it should be able to process audio through
VST plug-ins. "…sample" is a working name; ask the owner before it shows anywhere.

**Where it goes:** `sample/index.html` (and `sample/core.js`, see below), a page of its own like `waves/` and `chains/`:
- Minimal skin, white.
- Works standalone first: open or drop audio files, export WAV.
- Then it becomes a fifth screen in a frame: Tracker · Synth · Waves · Chains · Sample.
  - Copy how `WV.frame` / `CH.frame` are made, around line 12374 of `index.html`.
  - `skinFrame()` carries the skins into the frame.
- When it ships, add its files to:
  - `sw.js` `FILES`, and bump the cache;
  - `electron/copy-app.js` `PARTS`, plus `sample` in its `refs` regex.

**Three layers.** Keep these apart so the core can later be ported to C++/wasm (see `docs/ENGINE-PLUGIN-PLAN.md`):
1. **Core** (`sample/core.js`, no DOM, no Web Audio):
   - The sample model: `sr`, channels as `Float32Array[]`, `loop {mode, start, end, xfade, release}`, `slices[]`, cue
     `markers[]`, and playback props `{note, fine, vol, pan, syncLines, interp}`.
   - Every edit is a processor `(chans, sr, sel, params) → chans`.
   - An undo stack that keeps only the changed range.
   - WAV read/write (16/24-bit, 32-bit float).
2. **UI:**
   - Canvas waveform with an overview strip and a zoomed view.
   - Min/max peak levels cached per zoom, so long files scroll smoothly.
   - Exposed as `mount(container, host)` rather than owning the page.
3. **Host** (standalone page or Thunder Plus) supplies:
   - the AudioContext;
   - file open and save;
   - the skin's CSS variables;
   - the key-command registry, so shortcuts go through Thunder's Renoise-style keys;
   - sending a sample to, and taking one from, a Thunder layer or INST.

**Thunder's side of the data** (check these before wiring):
- Layer samples are **mono**, up to 30 s: `L.sample = {sr, name, id, data: Float32Array}`.
  - Kept in IndexedDB as 16-bit PCM: the "samples in IndexedDB" block, `smpDb()`, around line 10855.
  - Files carry them as base64: `smpToJSON` / `smpFromJSON`.
  - The editor is stereo. Either mix to mono when it hands a sample to a layer, or widen Thunder's samples to stereo
    first (a bigger job: the player, the renders and the JSON). Ask the owner which.
- Break chopping already has the pieces:
  - INST fields `slc`, `smode`, `sync`, `beats`;
  - `findHits()`, `slicePoints()`, `stretchData()`.
- The editor's slices should match them:
  - "Transients" uses `findHits()`;
  - hand-placed slice markers need a new INST field (say `slcAt`, a list of seconds) that `slicePoints()` uses when it
    is there;
  - Beat sync maps to `sync` / `beats`.

**Layout (the mock-up), desktop:**
- **Top bar:** as in Thunder.
- **Grey band:**
  - Sample number, name, rate, bit depth, channels and length.
  - Play · Play loop · Stop.
  - Tool: Select / Draw / Zoom.
  - Zero crossings and Snap to slices checkboxes.
  - Zoom: − + Fit Selection.
- **Black monitor screen:**
  - Overview strip with the view window outlined.
  - Beat ruler with the loop shown as a gold bar.
  - L and R lanes with the selection in light grey, and pink numbered slice flags.
  - Gold LOOP / END lines, a teal cue marker and a red playhead.
- **Readout line (mono):** SEL, LEN, CURSOR, PEAK, RMS, VIEW.
- **Three Seeds-style cards:**
  - **Loop** (`--pl`): Off / Forward / Backward / Ping-pong; start and end; crossfade; leave the loop at note-off;
    Loop the selection; Crossfade loop; Loop tuner.
  - **Slices** (`--smp`): detect by Transients / Beats / Equal; sensitivity; slice chips; Slices to pads; Render to
    samples; Clear.
  - **Playback** (`--std`): base note; fine tune; volume; pan; beat sync (lines); interpolation None / Linear / Cubic /
    Sinc.
- **Right column:**
  - **Process** in groups:
    - Level: Normalize, Gain, Fade in/out, Remove DC.
    - Edit: Trim, Silence, Reverse, Invert phase.
    - Rate & pitch: Resample, Pitch shift, Time stretch, Bit depth.
    - Channels: To mono, To stereo, Swap L / R.
    - Plugins: Apply plugin…, dashed and marked DESKTOP.
  - **History:** click a step to go back; Undo / Redo.
- **Bottom dock:** the song's samples, in pad style.
- **On the Fold** (884 unfolded): the cards keep three across and the right column drops below. Make Process a
  `Process ▾` menu there instead. At 412 (folded) the cards stack.

**Build order:**
1. `core.js`: model, WAV in and out, undo, and the Level / Edit / Channels processors. Test it in node like
   `tools/drums/`.
2. The page:
   - top bar, grey band, monitor screen;
   - select, zoom, scroll;
   - play the selection and the loop. Forward loops use `AudioBufferSourceNode` `loopStart` / `loopEnd`. Backward and
     ping-pong play a preview buffer built with the loop reversed, or forward then reversed.
3. Process column and history; export WAV; keep the session in IndexedDB so a reload loses nothing.
4. Loop card (crossfade loop, loop tuner); Slices card (detection, render slices to new samples).
5. Rate & pitch:
   - Resample (windowed sinc) and Bit depth (with dither) in JS.
   - Pitch shift and Time stretch: start from `stretchData()`, or a permissive library compiled with the `dsp/` wasm
     toolchain:
     - Signalsmith Stretch (MIT);
     - r8brain (MIT) or libsamplerate (BSD) for resampling.
   - **Not Rubber Band** (GPL or paid). The licensing rule below applies.
6. The frame in Thunder Plus: Sample tab, skins, sending to and from layers and INST, key commands.
7. Later, desktop only: **Apply plugin…**
   - An Electron native module (C++) loads a VST3 (SDK MIT since 3.8).
   - It renders the selection offline and hands the buffer back; the plug-in's own window opens natively.
   - Offline rendering avoids the browser's real-time limits.
   - The web and Android builds hide the item.

**Checks for each step:** as in other entries, headless at 412, 884 and 1600, with no page errors. Exported WAVs must
read back identical in node.

## 23 Oct (2): the FX column's command picker in groups

Cache `thunderplus-v218`. Owner: organise the tracker FX for the FX column menu into categories, like the track FX.
- `FXC_GROUPS` (before `FXLIST_ALL`): Volume and pan · Pitch and arpeggio · Note timing · Sound · Chance and random ·
  Effects · Tempo and groove · Play order · MIDI, the everyday ones first in each. `fxcGroup(name)` (else 'More', for a
  command added later and not grouped), `fxcByGroup(names)` (groups in first-appearance order). `FXLIST_ALL` is now in
  that order (was alphabetical); `FXLIST_MST` is `MST_FXC` grouped, groups in the order the master list had them
  (volume, effects, tempo, play order, timing, chance). `fxpOpen` passes `heads` to `fxpFill` as `fxsOpen` does
  (sticky `.fph` headings; keys and clicks skip them). Only the picker's order changed: `FXC` numbers, `MST_FXC` (EDIT+▲▼
  on the master) and letter typing are as before.
- Checked headless (1600, 412): 41 commands + --- under nine headings, NOW on the cell's command, ▼▼ / a letter / Enter,
  a click chooses, the master list grouped; no page errors.

## 23 Oct: ROWS sets PATTERN LEN; new songs LPB 8; the phrase cursor wraps

Cache `thunderplus-v217`. Owner: when the rows of a phrase are changed, that number should become the default for new
phrases; default lines per beat 8; up past the top of a phrase or pattern (or down past the bottom) should wrap round,
with the view following (on the phone the cursor was getting lost).
- ROWS = DEFAULT: `setRows` (tab bar ROWS −/+/typed) and `psetSet` x 0 (the LEN line) call `setDefLen(n)`, so `S.dlen`
  (PATTERN LEN) follows and every empty phrase takes the new length too. Delete on LEN (back to 16) does the same. The
  PATTERN LEN info line and the ROWS dialog say so.
- LPB 8: `freshSong` has `lpb:8` and the Settings LPB row `d:8` (old files without `lpb` still load as 4). Note this
  reverses the 14 Oct "new songs LPB 4" (owner's choice now, after the swing fix). Things written in 16ths are pinned:
  `demoSong` sets `S.lpb = 4`, `breakSong` `lpb:4`, and `placeBreak` into a song that isn't LPB 4 gives the break's
  phrases their own LPB 4 (`plpb`) and works its DEL ticks out at 4.
- WRAP: in `move()`, a vertical step past either end of the phrase (`dims('phrase').h` = the open phrase's LEN, Pattern
  view included) goes to the other end; a longer jump (PgUp/PgDn) stops at the end first, then wraps. `draw()`'s follow
  scrolls `c.top`. ▲ on the top row no longer goes to the LEN / LPB line: the folded phone hides that line (`thdFit` f6),
  so the cursor vanished there. Tap LEN / LPB to reach it (wide screens); ▼ from it still returns to row 0. Shift+▲▼
  selections stay clamped (`shiftSel`).
- Checked headless (412 folded, 884 unfolded, 1600): ▲ at row 0 → last row visible, ▼ at the last → row 0, 16 and 32
  rows, Pattern view, ArrowUp, PgUp twice, Shift+▲ at the top, LEN line ▼; no page errors.

## 22 Oct (7): a bus is FX commands only; buses outlined in Pattern view

Cache `thunderplus-v216`. Desktop **v1.2.19** (22 Oct, started with Run workflow through the API: this session can push
branches but not tags) has everything up to 22 Oct (6). Owner: on a bus, no note entry, FX only; notes it already had keep
playing until playback is stopped and started again; a bus should look different in Pattern view (Arrange already shows
it darker), e.g. a cyan border in the plum skin.
- BUS FX ONLY (after `mstView`): `busTrack(t)`, `hasNotes(p)` (cols 0..IC), `busFxView()`, `busSay()`. `pCols`, `move`'s
  step off the LEN line, the phrase draw's cursor clamp and `pvColsOf` give a bus with no notes the FX columns only (as
  the master). Refused with a toast on a bus: `typeNote` (not REC while playing, which `recLive` refuses), `typeOff`,
  `typeHex` on V / I, `editTap` and `bump` on note / V / I, `recNote`, `recLive`, `euclidFill`, `volSel`, `instSel`.
  `paste` and `pasteMix` keep only the FX columns there (a toast says so). `busHint`'s one-time note now says FX only. The
  hint line on a bus says so (and that old notes go silent at the next PLAY).
- `makeSeq` takes `busT` (the buses when it starts) and plays no notes or note-offs on them (`nbus`); commands still run.
  So a track that becomes a bus mid-song keeps its notes until PLAY starts again. Note: a bus whose chain doesn't take its
  own Track In was already silent through the routing (its Chain Out replaces its sound); only a chain that mixes its own
  Track In in hears its notes until the restart.
- Look (Pattern view only): `--tk-bus` (`.tscr` #1f9bb0 cyan; term skins `var(--mxbus, …)`, overridden where cyan is one of
  the skin's own track colours: matrix / bwterm / espresso a brighter cyan, night pink, midnight amber, ocean violet, miami
  indigo, tomato / desert blue, winter magenta). `pvLines` groups carry `bus`; their bands use it (`span.bus` in `.pvon`, no
  darkening there), their cells get `pvbus` (a 12% tint) and the first / last span of each line `pvbl` / `pvbr` (a 2 px
  line each side), the ENDS HERE rows too.
- Checked headless (plum): a normal track 4 sounds, becomes a bus mid-play and keeps sounding, silent after a restart;
  old notes show and typing a note is refused; with them cleared FX columns only and the cursor on FX1; an FX command
  types; a pasted line keeps only its FX; the band and side lines are rgb(31,155,176); earlier suites.

## 22 Oct (6): PLAY on a bus plays it with its sources

Cache `thunderplus-v215`. Owner (in Pattern view on a bus): Space on a bus should make every track connected to it sound.
- BUS PLAY (before `playCtx`): on Phrase (Pattern view or not) or Sect, with no LOOP SEL selection loop, when `cTrack` is a
  bus, `busPlay()` starts `tStart('song', {row, loop:{v:'bus', y0:row, y1:row}, only})`: `only` = `busSources(b)`, the
  bus and every track feeding it, transitively (`busPlan().feeds`). `makeSeq`'s song start skips tracks not in `o.only`.
  The row: `busRowOf` (Pattern view's row, the row the phrase sits on, or the sect's row; else the song cursor's). A toast
  says which tracks; a row where nothing feeding it plays says so instead. `syncLoop` leaves a `'bus'` loop alone (it
  would have dropped it on the next redraw), and `followPlay` doesn't jump to another track when the bus has no block of
  its own (it fell back to the first playing track and moved the cursor off the bus).
- Checked headless: Pattern view on an empty bus (track 4 fed by track 1 through a Glue): Space plays only track 1, the
  row loops, the cursor stays, the bus track's meter carries the sound and tracks 2 / 3 stay silent; Space stops; a
  plain track still plays its phrase; an empty row toasts; earlier suites.

## 22 Oct (5): view keys; Pattern view reaches buses and the master; no INST part on a bus

Cache `thunderplus-v214`. Owner: keys to switch between Tracker, Synth, Waves and Chains (took Alt+8 / Alt+9 / Alt+`);
in Pattern view the arrows didn't move onto the bus or the master; on a bus, hide the bottom panel's instrument part so
…chains gets the room.
- VIEW KEYS (index.html, before `setView`): `viewKey(code)`: Alt+0 synth, Alt+1–7 tracker screens, Alt+8 …waves, Alt+9
  …chains (a toast when Mobile hides it), Alt+` `lastView` (set in `setView` whenever the view changes). A capture
  keydown on the page takes Alt+8 / 9 / ` everywhere and Alt+0 off the tracker (the tracker's `globalKeys` and the synth
  page keep their own Alt+0 / Alt+1–7). Both editors (after `const EMBED`, outside the shared block) catch Alt+digit /
  Alt+` in the capture phase and call `TW.viewKey` (`thunderChains.viewKey` / `thunderWaves.viewKey`). Leaving an editor
  blurs its iframe so the tracker gets the keys back. Help (KEYS legend, the synth page table) and README updated.
- Pattern view: `pvNextTrack` (◀▶ past a track's edge, Tab / Shift+Tab) never steps over a bus (`isBusT`) or the master
  column, folded or not; landing there opens it like any track (an empty cell: a pending phrase, `pendPlace`, so nothing
  is added until a note is typed). Other empty tracks are still stepped over while EMPTY TRACKS FOLDED.
- Bottom panel: `deckLayout` hides `#dkInst` while `dkTrack()` is a bus; `deckInst` skips it then.
- Checked headless: every view key from the tracker, synth and inside both editors (focus in the frame), the tracker's
  arrows after Alt+3 from …chains; Pattern view ▶ from the last filled track onto the bus, on to the master, Shift+Tab /
  Tab back, nothing added to the song; the panel on track 1 / the bus (Mixer and Phrase) / track 2; the earlier suites.

## 22 Oct (4): dynamics displays in …chains / …waves and on the FX screen

Cache `thunderplus-v213`, `chains-worklet.js?v=35`, `waves-worklet.js?v=28`. Owner: compressors (and the limiter, anything
that needs it) should show the standard display (threshold, gain reduction…); "we have a little metering, but it could use
a better gui"; double-clicking the title bar should fold to a reasonable size.
- Engine (shared block of chains-worklet.js, synced): `show()` of Compressor (`ClassicComp`), Bus Comp, Glue Comp, Noise
  gate (new) and Limiter now sends `[reduction dB ≤ 0, level the detector hears dB, peak out dB]` (`st.dd`, `st.op`, the
  gate's `gm`/`ep`, the limiter's pre-Input `ai`). Sidechain Comp (…chains only) too. `withLevels(def)` (after `linDb`)
  wraps a wasm effect's create/process/show to send `[0, peak in, peak out]`: Transient shaper and Multiband squash in the
  shared block, Pressure (`Comp`) after the block in both worklets.
- Editor (shared block of chains/index.html, synced): display kinds `dyn` (compressor, buscomp, glue, ngate, limiter,
  sccomp) and `dynlv` (comp, ott, transient). DYNAMICS DISPLAY: `DYN` (type → curve params from the module's own params),
  `dynOut` (the engine's soft-knee / Range / limiter / gate maths), `buildDyn` (canvas `.dyncv` 116 px, plus `n.mini`, a one-
  line GR / OUT meter shown only when folded: `.node.collapsed > .dmini`), `dynPush` (80-sample history ≈ 2.5 s at the
  ~31 Hz telemetry; numbers hold their peak 0.4 s), `drawDyn` (curve square with threshold, unity line and a live dot;
  history with the GR lane on top (0…−24 dB) and levels below (−60…+6, input filled, output teal, threshold dashed); fixed
  screen colours on the skin's `--glass`, because a monochrome skin made GR and output the same grey). Redrawn on each
  telemetry (rAF) and every 150 ms when the settings' signature changes. Both editors' `buildNode` put `n.mini` after the
  ports. The old `gr` kind is kept for anything else.
- FX screen (index.html): the track-effect worklet (`TFX_SRC`, a template string: no backticks) takes `{tel:true|false}`
  and, every 12 blocks, posts `{dyn:[[slot, key, v]]}` from the shared modules' `show()` and, for wasm Pressure / OTT /
  Transient (ids 7, 28, 26), peaks measured around `slotRun` (`pk(n)`). FX DYNAMICS: `fxDynIn` keeps a history per slot on
  the node (`n.dyn`), `fxTelSet` turns one node's reports on (the stage's track, from `stageTick`; off when the stage
  doesn't show the chain and in `setView` away from the tracker). `stageChain` draws a thin GR / Δ bar under each dynamics
  box and, below the chain, the display (`fxDynPaint`, the same design) for the cursor's slot (`cFxS`) or the first
  dynamics slot; 30 fps while the track has one. Curves from `TFX[type].map(f)` (the module params). Only on the stage
  (desktop widths); the phone FX screen has no display yet. The mastering window keeps its own meters.
- Checked headless: the demo song through Track In → Compressor → Glue → Bus Comp → Sidechain Comp → Gate → Pressure →
  Squash → Transient → Limiter → Chain Out: all nine draw, reductions read (−18 dB at −30 / 6:1), level-only ones show Δ,
  folding keeps a one-line meter and unfolding redraws; FX screen with Glue, Pressure, Limiter on track 1: reports for
  all three, display follows the cursor's slot, reports off away from FX; earlier suites; no page errors.

## 22 Oct (3): CHN on stays in the Mixer with tidy pairs; stereo jacks read L / R

Cache `thunderplus-v212`. Owner: turning CHN on in the Mixer shouldn't jump to …chains; the pairs should be waiting there,
and they were spaced so far apart you couldn't see them all. Then: stereo outputs read "Out L" / "Out R", inputs too.
- `setChainOn()` (CHN FROM THE MIXER): never changes screen (`focusChainHit` is gone). A song with no patch yet
  (`chainPatch:null`, which made `ensureChainPair` fail and the old code jump to the editor, whose starter patch was for
  track 1 only) gets `chainsBlank()` (the editor's `blankDoc` shape) and a pair for every track whose CHN is on. A track
  that already has a Chain Out just runs through it again. A toast says which. `CH.fitNext` → the next `setView('chains')`
  calls `chainFitTry()` → the editor's new `chainsEmbed.fit()` (`fitAll(1)`: whole patch, no closer than 100%) instead of
  focusing the cursor track's chain.
- TIDY PAIRS: `ensureChainPair` puts a pair 40 px below `chainsBottom(P)` (box tops + `CHN_BOX` heights: Track In 260,
  Chain Out 290, others 400, folded 80; group bounds), first one at y 40, so fresh pairs are 330 px apart. The old
  `maxY + 220 + idx * 160` grew the gap with every Track In (220, 600, 1140, 1840 …). Measured in the editor: Track In
  255 px, Chain Out 288 px, Tape delay 369 px tall, 250 wide. Existing patches keep their layout.
- STEREO LABELS: `portName(type, port, dir)` in both editors (outside the shared block, next to `makeJack`): a port whose
  `port + 'R'` twin exists on the same side reads "<label> L" (Out L, In L, In 3 L); mono ports unchanged. Used by the jack
  label, its tooltip and Help's module cards. Help / tips text that named the left jack updated (shared block, then
  `tools/sync-shared.py`; the worklet blocks were already equal and are untouched).
- Shift+drag box selection already exists in both editors (`ws` pointerdown with Shift on the empty background; additive;
  any box it touches). Checked in the app: it works. It does not start on a box, a cable (`.hit`) or a group (`.decor`).
- Checked headless (1600 wide): CHN on with Enter on the Mixer for 4 tracks in a new song (stays on the Mixer, 4 pairs at
  y 40/370/700/1030, 8 cables, each track feeds its chain), off/on again (no new pair, no jump), …chains opens fitted with
  no overlaps and every box in view, an 8th track after the editor saved (y 1360, refits), below the starter patch
  (y 500); labels in both editors (Track In Out L/R, Chain Out In L/R, waves Out In L/R, mono VCO/ADSR/VCA stay Out);
  the earlier selection and FX suites again; no page errors.

## 22 Oct (2): Shift+▲▼ selects cells, Ctrl/⌘+Shift+▲▼ lines; FX and the Mixer follow the Song / Arrange track

Cache `thunderplus-v211`. Owner: Shift+▲▼ should select what is straight up and down from the cursor, keep a way to take
whole lines; PAD mode stays as it is. Then: going to FX from a phrase should land on the track selected in Arrange / Song.
- `shiftSel()` (KEYS mode Shift+arrows): with no selection, any Shift+arrow starts a block at the cursor and grows it
  (spreadsheet style). On a line selection Shift+▲▼ still grows the lines (`lineSel`), Shift+◀▶ turns it into a block over
  the same rows. ▲▼ set `c.y` directly (clamped to the grid) instead of `move()`, which took ▲ on a phrase's top row to the
  title row (y −2).
- `keysDown`'s `mod` branch: Ctrl/⌘+Shift+▲▼ → `lineSel` in both input modes; where it returns false (off the grid) it falls
  through to `bump`, which is all Ctrl/⌘+Shift+▲▼ did before. PAD mode's own SHIFT+▲▼ (pad and keyboard Shift) unchanged.
- Help (KEYS tables, `#tLegKeys`, the PAD paragraph), `info()` hint (Shift+arrows grow for a block), the empty-paste toast,
  the master-phrase hint and the README's keyboard section updated (it still said Alt+arrows change values and Ctrl/⌘+Shift
  changes screen).
- FX FOLLOWS ARRANGE in `go()`: from Song, Sect or Phrase (and from Inst when `instViaTrk`, i.e. Inst was reached from one
  of those, so walking Phrase → Inst → Mixer → FX counts), FX opens on `cs.song.x` (the master column → `NT`) and the Mixer
  on that track (master → row 7), instead of `cTrack`, which stays on the phrase's track when Arrange's cursor moves to an
  empty block. `nav()`'s Phrase → Inst (sets `tv` without `go`) sets `instViaTrk` too. Mixer → Inst → Mixer and Project ↔
  FX keep what they had.
- Checked headless (1400 and 1600 wide) with a test hook injected into a served copy: cell blocks (grow, widen, top row),
  lines (first press, grow, Shift+▲▼ grows lines, block ↔ lines, Ctrl+C copies lines, plain arrow drops them) on Phrase,
  Song and Sect; Mixer Ctrl+Shift+▲ still changes the value; PAD keyboard and on-screen SHIFT+▲▼ lines, PAD Ctrl+Shift+▼;
  hints and help; FX / Mixer from a phrase after the song cursor moved, the master column, the Alt+▶ walk, Arrange holding
  the keys on an empty block then Esc and walk to FX; no page errors.

## 22 Oct: the Renoise audit batch — track names, selection tools, render a part, file safety, the rest of the mouse

Cache `thunderplus-v210`. Owner asked for everything from the usability audit except automation (Open items 10, later),
custom key bindings and live DEL-timing recording. Each block is marked in `index.html` and can come out on its own.
- **TRACK NAMES**: `S.mix[t].nm` (≤ 12) and `.col` (index into `TCOL`, 0 none), read in `normSong`, so they save and undo
  with the song. `tName`, `tCol`, `trkLab` ("TRACK 02 KICK"), `trkTip`. Shown: Phrase / Pattern titles (`tSub` regex takes the
  name; `#tSub.tcl` colour mark), Pattern bands, Arrange's `#tArrTh i` (`.tcl`, `--tc`), Song / Mixer headers (grid spans now
  take `st`, an inline style), meters, `dkStripT`, stem file names. `nameTrack(t)` is an `askSheet` with the new `swatch`
  option. Double-clicks: the headers are redrawn by the first click so the browser never fires dblclick: `hdDbl(t)` (two
  presses on the same track in 400 ms) in `gridDown`'s header branch, Arrange's header click and the Pattern bands; the meters
  and `dkStripT` use dblclick; the Phrase title is caught on `scr` (it holds pointer capture for drag-scroll).
- **SELECTION TOOLS** (before MOUSE SELECT): `selScope`, `interpSel`, `volSel`, `instSel`, `stretchSel(expand)`, `pasteMix`
  (Ctrl/⌘+Shift+V in `globalKeys`). Items in `cmdItems` (so Edit ▾ and right-click). One undo step each.
- **RENDER PART** (before `shareLast`): `renderScope()` builds a stand-in song (a copy of the lists, the part at row 0, sect and
  phrase 255 for a phrase part; song rows copied down with their block mutes), `makeSeq(S2, 'song', {row:0, once:true})`, then
  the same `pass()` as RENDER SONG (no master / limiter for a phrase). To a slot: `freeSlot()` (no note in the song plays it,
  factory name first, drums first), layers off, `assignSample`, an instrument set to play it. Or a WAV via `deliver`.
- **FILE SAFETY** (replaces the old `saveProj` / `openProj` / `loadProjFile` block): `markChange` (from `saveS` and `changed`,
  armed 2.5 s after start) → `projDirty` (localStorage `thunderplus-dirty-v1`), `dirtyShow` (title "• Song — ...Thunder Plus",
  `#bFile.dirty`). `okToLeave(what)` / `guardLeave` before New, Open, Recent, Restore, demo, a break's new song, …Thunder import
  and the desktop close; "Don't save" makes a backup first. `projSet(path, handle)`: the desktop app remembers a path
  (`thunderplus-projpath-v1`), a browser the FileSystemFileHandle (IndexedDB `thunderplus-files` / kv `cur`; `fsPerm` asks again
  after a restart). `recentL` (kv `recent`, 8) → File ▾ → Open recent…. Backups: kv `bkIndex` + `bk<time>`, every 3 min when
  `bkDirty` (not while playing or rendering), 10 kept, File ▾ → Restore a backup…. `.thunder` is the extension (JSON;
  `.json` opens; the file input and pickers accept both). `loadProjText(text, name, where)`.
- **Desktop shell** (`electron/`): preload exposes `thunderDesktop.files` (open, read, save, saveAs, pathOf via
  `webUtils.getPathForFile`) and `thunderDesktop.app` (ready, onCloseAsk, closeAck, closeReply, onOpenPath). `main.js`: only
  `.thunder` / `.json` paths are read or written; writes go to `<file>.saving` then rename. The window's close is held until the
  page answers (it acks at once; no ack in 3 s, a crash or a reload → it closes); a quit carries on after the reply. A
  `.thunder` path from argv, a second launch or the Mac's `open-file` waits for `thunder:page-ready`. `package.json` has
  `fileAssociations` for `.thunder` (installer, Mac, AppImage; not the portable .exe). Checked with a fake-Electron harness
  (close held / Cancel / OK / silent page / quit, read, atomic save, save as, open, moved file, argv, second instance,
  open-file); not run in a real Electron here (no npm registry), so check a close and a double-click on the next build.
- **SLIDER MOUSE** (after `loadAudioInto`): wheel on the focused slider (2% a notch, Shift 0.2%), Shift-drag at a tenth, via
  `sliderSet` (fires input, then change). Not `#gset` / `.rlock` / dialogs. `makeRow` rows get a right-click menu (`sxMenu`).
- **FILE DROP**: `dragenter/over/leave/drop` on the window (`body.dropping`, `.pad.droptgt`; pads carry `data-pad`). Audio →
  `loadAudioInto(f, pad)` (factored out of `fileIn`); projects → `okToLeave` then `loadProjText` (desktop: the dropped path);
  kits, `.mid`, …chains patches and preset files to their existing importers.
- **Arrange**: middle-click (a capture listener before the press handler; `mousedown` stops autoscroll) mutes the block or
  `sxMuteRect` the selection; `sxAim` copies with Alt, ⌘ (Mac) or Ctrl; `arrGo` makes a phrase on a double-tap / click of an
  empty cell on the Song screen (`newBlock(…, -1)`, then opens it).
- **Pattern view bands**: `pvBandT`; Alt-click mutes, right-click solos (capture listeners on `grid`, before the band's open
  handler), double-press names; `.pvso` underline.
- **QUICK ADD** in `chains/index.html` and `waves/index.html` (after `hideMenu`, outside the shared blocks): dblclick on empty
  canvas → the context menu holds a search box and up to 14 matches (custom modules too); Enter adds the first at the spot.
- Checked headless (1400–1600 wide, the Fold folded 412 and unfolded 884 with touch): names (dialog, every place it shows,
  save/load, undo, double-clicks leave mute as it was), each tool's numbers, render to a slot / WAV (phrase, selection, song
  row), dirty dot, Save changes? (Don't save → backup → restore), Save / Ctrl+S to a browser handle and to a desktop path, path
  and recent across a reload, close ask (Cancel / Save / clean), open by path, open recent, slider wheel / fine wheel /
  Shift-drag / menu, a WAV dropped on pad 03, middle-click, double-click new phrase, Ctrl-drag copy, Pattern bands, quick add in
  both editors, the earlier mouse suites again, no page errors. (Opening chains/ or waves/ on their own logs a 404 for their
  sw.js: that was so before.)

## 21 Oct (7): the tracker grid with a mouse works like Renoise; right-click solos; …chains and …waves on Mobile apart

Cache `thunderplus-v209`. Owner asked for a review of the mouse against Renoise / SunVox and picked the first four fixes, plus
…chains shown on the Fold without …waves (chains hidden by default).
- **MOUSE SELECT** block (before `let drag = null;`, with its CSS by `.tgrid span[data-x]`; to remove: the block, the CSS, the
  `mselDown` line in `gridDown` and in the Pattern view's pointerdown). Mouse only (`pointerType === 'mouse'`, left button), on
  GRIDV screens drawn in the grid (Phrase, Pattern view's open track, Sect, the classic Song grid; not Arrange):
  - `mselDown()`: Shift-click anchors at the cursor (or keeps the selection's anchor) and moves the cursor end, then a drag goes
    on; a press inside the selection is `grab`; a press elsewhere drops the selection (mouse only: touch still moves the corner)
    and is `pend`, which becomes `sel` after 5 px. The cursor's own FILLED cell returns false, so `gridDown`'s value drag runs
    as before; the cursor's EMPTY phrase cell selects (the old mouse drag there did nothing, 19 Oct). A click on the cursor's
    effect cell still opens the picker (`mselEnd` → `editTap`).
  - `mselFrame()` (one per animation frame): `mselCell()` finds the cell under the pointer (data-x, or in Pattern view
    `pvSpanAt()` through `pvMap`, open track only); past the grid's top or bottom an interval steps the cursor a row each 60 ms.
    `grab` moves `selA` and the cursor with the drag as the preview; `mselDxOk()` allows sideways only onto visible columns of
    the same `PK` kind (Phrase) or between tracks (Song, not the master), else rows only. Line selections move rows only.
  - `mselApply()`: reads the original rectangle, clears it unless copying (Alt, Ctrl or ⌘ held on release), writes it at the
    offset (overwrites, as paste; Song carries block mutes). One undo step (all in one pointerup). Esc while dragging:
    `mselBack()`. Toasts count visible cells (`mselN`).
  - `pvSpanAt(s, clientX)` was pulled out of the Pattern view's pointerdown (same logic) so `mselCell` can use it.
- **Shift while dragging a value**: `drag.big` (read on each pointermove) → `bump(d, true)`.
- **Right-click solo**: `soloTrack(t)`. `gridDown`'s right-click on a y = −1 track number (Song, Mixer) solos and sets
  `mselNoMenu` so `cmdCtx` skips the menu once; the meters (`#tMeters .mtr`, class `so` when soloed) and Arrange's header
  (`#tArrTh i[data-t]`, click mutes, right-click solos, mouse only; `.so` styled with `--ar-pl`).
- **Mobile editors split**: `thunderplus-mobile-chains-v1` / `thunderplus-mobile-waves-v1` (`mobCh`, `mobWv`; unset → the
  old `thunderplus-mobile-editors-v1`, so a ticked old box starts both on). `noChains()`, `noWaves()`, `noEdFor(view)`
  replace `noEditors()` (every remaining use was a …chains one). Body classes `nowv` (was `noed`, hides `.wvEdit`) and
  `noch`. Settings → App: Show …chains on Mobile, Show …waves on Mobile, both off by default.
- Checked headless (1400 and 1600 wide, Fold width 884 for Settings): drag select, move, one undo step, Alt-drag copy with
  the copy cursor, click-away, Shift-click, sideways onto another kind stays put, Esc puts it back, value drag +1 then
  Shift +12, empty cursor cell selects, effect cell opens the picker, wheel, drag past the bottom scrolls on, Pattern view
  select stays in the open track, a touch press starts nothing new, right-click solo on meter / Mixer header / Arrange
  header with no menu, right-click on a cell still opens the menu, Mobile ticks (Chains only, both, neither, old key
  migrates), no page errors.

## 21 Oct (6): Shift-click and Shift-drag select in Arrange; the selection in its own colour

Cache `thunderplus-v208`. Owner: "in arrange mode, can we make shift + click select phrases? Shift+click+drag could as
well", then: Shift-click 1 then Shift-click 5 should select everything between, and the selection was hard to see.
- `sxDown()` (SONG EXTRAS mouse, Arrange as the Song screen and the side Arrange with the keys): a Shift press captures
  the pointer and returns true, so it is never a tap, hold-to-mute or block move. `sxEnd()`: a Shift-click without moving
  starts a selection on that cell when there is none (`selA` there, toast "Selecting from here…"), else moves the
  cursor end, so the rectangle runs from the first Shift-click to this one. A Shift-drag goes through `sxStart()`'s select
  branch (`d.sel`/`d.filled` false), so it draws a new selection even when it starts on a block. `sxSelToast()` is the
  shared "n cells selected" toast.
- Colour: `.tarr{--ar-sel}` amber, sky blue on the amber/orange skins (the same list as PATTERN VIEW's `--pv-sel`);
  `b.sl` (and the drop target `b.dt`) use it instead of `--ar-cur`. On the Simple / Matrix skins `--ar-cur` is the
  blocks' colour, and their more specific `b.f` rule (background shorthand, glow box-shadow) wiped the tint and outline
  off selected blocks entirely, so `b.sl` now uses `!important`.
- Checked headless on mustard: Shift-click 0:0 → 1 cell, Shift-click 0:2 → 3, then 2:4 → 15; Escape clears; a Shift-drag
  from a block to 3:1 → 8 cells and nothing moved; no errors.

## 21 Oct (5): one line between CLICK and OCT

Cache `thunderplus-v207`. Owner: the line between CLICK and OCT on the tracker's top strip looked thick. CLICK's right
border and `.toct`'s left border sat side by side (2 px); `#tRecBar + .toct{border-left:0}` leaves one. `#tRecBar` is
never hidden and always ends in a bordered `.rv` (REC when CLICK is dropped by `fitsel`/`fitxp`), so the line stays.

## 21 Oct (4): a ? on every …waves and …chains module

Cache `thunderplus-v206`. Owner: "a small ? on every module in waves and chains that displays a tooltip about what the
module does and possibly a short use case example".
- Each module's title bar has a **?** before the × (`mhelpBtn(type, title)` in both pages' `buildNode()`). A mouse hover
  (250 ms) shows a popover; a click or tap pins it; a second click, a click elsewhere, Escape, a wheel (pan / zoom) or
  dragging a module closes it. Buttons in the title bar never start a drag, so nothing else changed there.
- The popover (`#mhelp`, `.mhelp`, position:fixed above the context menu, the module's palette colour as its border):
  the name, the description the Help page shows (`modAbout`: the page's `PAGE_ABOUT`, then `SHARED_ABOUT`, then the
  module's `about`) and a **Try** line (`modUse`: `PAGE_USE` if a page defines one, else `MODULE_USE`).
- `ABOUT` moved out of `helpDoc()` to top level as `PAGE_ABOUT` in both pages (helpDoc uses it as before).
- `MODULE_USE` (one concrete patch or setting per type, all 118 types across both pages) and the popover code and CSS
  live in the SHARED EDITOR block: edit them in `chains/index.html`, then run `python3 tools/sync-shared.py`.
- In …Thunder Plus's frame skin the ? is bordered like the ×, so long effect names with BYP cut off a little sooner
  ("Tape del…"); the popover always shows the full name.
- Checked headless: both editors standalone (hover, pin, switch module, Escape, drag still moves a module) and inside
  …Thunder Plus on a touch screen (tap opens, tap again closes); no errors.

## 21 Oct (3): engine controls in the middle; the sound view in the right column

Cache `thunderplus-v205`. Owner (screenshot of Tone drum on the Desktop synth layout): the space under the Engine row
"looks awkward on any synth engine aside from layers". It was empty because a non-Layers engine's card sat in the right
column, and the waveform no longer grows into the room (19 Oct, so ◀ ▶ stay put).
- `synDesk()` no longer moves `bassWrap/blipWrap/xWrap/voxWrap` into `#synRight`: they stay in `#main` before
  `#layersBand` (where Layers has its strip and card), so the engine's sections spread across the middle (3 to 4 a row).
  The Engine row doesn't move between engines (checked: same top for Tone drum, Blip, FM Drum, Skin, Breath, Table).
- **Sound view** (`#sndWrap` / `.sndcard`, the engine cards' `--smp` colour): Desktop synth layout only, every engine but
  Layers, in `#synRight` after `#fxWrap` (synDesk moves it; `sndVis()` from bassUI and synDesk shows it). Two screens in
  the waveform's `--screen` colours:
  - Spectrogram of the pad's render: `spgOf(c, cols, rows)` (2048-point Hann FFT with `SpecUtil.fft`, ≤ 480 × 260,
    20 Hz–20 kHz log, 84 dB below the render's loudest point fades to the screen colour, gamma 1.3), kept on the render as
    `c.spg` like `waveEnv`; `spgImg` paints it in the skin's colours (kept until those change). Lines at 100 / 1k / 10k
    like the tracker SCOPE. Tap plays the pad.
  - A playhead per sounding note (`sndPH`): from `vuHit` (taps, keys, MIDI: `sndHit(i, now, rate, mono)`) and `padBlink`
    (the song with the synth open: `sndHit(pad, e.t, e.rate, false, e)`, following sample start / slices / reverse / cut).
  - **Output** scope: an analyser tapped off `master` (rebuilt with the context), drawn from a rising zero crossing.
  - Paints once per render / pad / resize (`drawWave` calls `sndSoon`); the rAF loop (`sndFrame`) runs only while
    something sounds (`tp.on`, `audioWake` < 1.5 s, or a playhead), started by `synMeterOn` and `padBlink`.
  - Live engines (Acid, Machine, Bass, Voice) have no pad effects, so the sound view is the right column's only card.
  - State is `var` / function declarations on purpose: drawWave and bassUI can reach it during start-up.
- Checked headless at 1000 (narrow: unchanged, sound view hidden), 1440 and 1920: Tone drum, Table, Acid on a synth slot,
  Layers (unchanged: effects and voice cards on the right, no sound view), tap and song playback, no console errors.
- To revert: put back the `ew` move in `synDesk()` (`r.prepend(...ew)` / `$('layersBand').before(...ew)`), and delete
  `#sndWrap`, the `.snd*` CSS, the sound-view block after `drawWave`, and its calls (drawWave, bassUI, synDesk,
  synMeterOn, vuHit, padBlink).

## 21 Oct (2): the Song screen's sections lane is only as wide as its names

Cache `thunderplus-v204`. Owner (on the unfolded Fold): "the song window on mobile looks a little awkward because of the
sections label and I couldn't figure out how to use sections", shorten it so the grid is more centred.
- SECTIONS LANE WIDTH: `sxLaneSize()` (called at the top of drawArr, before arrSize measures the columns) sets `--sxw` on
  body: the label (now **SECT**) + 8 px while there are no sections, else the widest name measured with a hidden
  `.sxsec.sxm` (`width:max-content`, `--sxh:0` so its 4 px border counts) + 6, capped 88 px (60 under 900 px). `.tarr`
  padding, `.sxlane` and `.sxsec` widths follow the variable (the old fixed 92/88/84 and 64/60/56 are gone). Keyed on the
  names, arrsmall, tScale and font status.
- Discoverability: a dashed **+** (`.sxadd`) sits in the lane on the cursor's row when that row isn't in a section; it
  opens the naming prompt. Touch: a short tap on the empty lane names a section at that row, done on pointerup (the page's
  touch handling keeps a click from reaching the bare div); a long press still opens the menu. Mouse double-click as before.
- Checked headless at 884 and 960 px wide (touch, DPR 2) and 1440: empty lane 30 px (grid from x 76 → 46 at 884), + and
  tap name sections, widths fit INTRO / VERSE / BREAKDOWN (cut with … at 60 px), double-click / right-click on desktop;
  mute and duplicate regression tests identical.

## 21 Oct: one Source for every sidechain (no mixer sidechain bus); bus tracks look darker

Cache `thunderplus-v203`; desktop **v1.2.16** (21 Oct) has everything up to here; `chains-worklet.js?v=34`, `waves-worklet.js?v=27` (shared blocks
synced, MODFX regenerated). Owner: the single mixer sidechain bus was confusing ("what use cases does it solve?"); drop it,
let each sidechain user pick its own source, keep the mixer's SC row as a light that shows a track is used as a sidechain;
and make a track acting as a bus look a little darker on every screen so it isn't programmed by mistake.
- **`sck`, one param** on Sidechain In, Sidechain Comp, Classic Comp, Noise Gate, Envelope Filter, Bus Comp, Glue Comp
  (modules and MODFX slots): 0 = Self (Off on Sidechain In), 1–8 = Send n, 9 + t = track t (0-based; Track 1 = 9), max
  136 (`SCK_MAX` = NSEND + CHAN_COUNT in the chains engine; the SHARED block has its own `SCK_TOP = 136` because …waves has
  no CHAN_COUNT). Engine `ctx.key(sck)` → Float32Array or null (sends → `sendIn`, tracks → `ctx.tap`). Modules: patched SC
  input wins, else `K = ctx.key(p.sck)`, else their own input. Editor: `SCK_OPTS(self)` / `SCK_P` in the SHARED EDITOR
  block (…waves gets just Self). `addModule` (engine) and `normalize` (editor) convert old `src`/`snd`/`source` (the editor
  can't know the song: the old bus becomes Self there).
- **Host**: the mixer `scBus` is unconnected (`scv = 0`). `sckUses()` lists every user (chains modules plus `S.tfx`/`S.mfx`
  slots with sck > 0); `keyTrackList()` = their distinct tracks (≤ 32) is what `tapNode()` merges (`chnTapList` is it now).
  Track / master effects: the TFX worklet has 3 inputs (1 = the tap merger, 2 = `sendDly`), `tapMap` in processorOptions
  and `{tapmap}` on its port; `fxKeySync(node, slots)` wires them (routeTrack, setMfx, after a tap rebuild).
- **Old songs**: `scMigrate(j)` at the top of `normSong` (flag `sc2`; `freshSong` has it): Sidechain In `src`/`snd` →
  sck; anything on the old bus (Sidechain Comp always, others with `source ≥ .5`, MODFX slots too) → the **lowest track
  with SC on** (none: Self); then `m.sc = false`. `scConv` → `scNote()` toast once, at load / project open.
- **Mixer SC row**: ON when `sckUsesOf(t)` isn't empty (title and info line list them); Enter / EDIT → `scSay(t)`; no more
  toggling. CHN row: a source heard only through its bus shows **BS** (was MISS) with the bus track in the info line.
- **Bus look**: `isBusT(t)` (= `busPlan().feeds.has(t)`), `trkKind(t)` → ' BUS' / ' POLY' / ' MONO' in every TRACK nn
  label (info lines, Phrase / Pattern title, Pattern band, strip). Class `bus` on Arrange cells, the side strip, Song
  classic grid and Pattern view cells (`filter:brightness(.78) saturate(1.15)`), Arrange's header number (bold underline,
  title "bus of tracks …"), the mixer header; mixer cells get `mbus` (a 22 % track-colour tint, they're bare text).
  `busHint(t)` once per track per session from `newBlock` and `typeNote`: `busNote` rides on the next toast ("New phrase 03
  · … · track 04 is a bus of tracks 01, 02: notes here play through its chain too") or shows alone; toast() lets it stay
  4.5 s.
- Checked: node dsp_test (Bus Comp Self −2.5 dB, keyed by loud track 3 −20.1 dB; old `src` converts); headless app
  with audio: a snare's Bus Comp keyed by a muted kick ducks, master Glue keyed by track 3, Send 1 into a track-FX Bus Comp
  (0.036 → 0.011), migration (track-keyed → 11 with SC on tracks 3 and 4, sends kept, `sc` cleared, note once; no SC
  tracks → Self; new songs untouched), SC light + Enter toast, bus look screenshots in plum and night (Arrange, Song,
  Pattern, Mixer), bus 1 → 4 / Send 1 / tap / Glue bus 5 / renders + stems, editor (Source lists 41 with 32 tracks,
  migration), …waves loads; mute and duplicate regression tests identical. Not heard by ear yet.

## 20 Oct (10): …chains buses, Sidechain Out (sends 1–8), sidechain from any track, Bus Comp and Glue Comp, 128 chain tracks

Cache `thunderplus-v202`; desktop **v1.2.15** (20 Oct) has everything up to here; `chains-worklet.js?v=33`, `waves-worklet.js?v=26` (the shared blocks were synced:
`python3 tools/sync-shared.py`; MODFX regenerated: `node tools/modfx.mjs`). Owner: do the two more-tracks fixes for
…chains, a Sidechain Out module, buses (several tracks summed through a chain to another track, "like a DAW": each
source after its own fader), and stereo Bus Comp + Glue Comp (two modules).
- **128 chain tracks** (`CHAN_COUNT = 128` in the worklet and editor; `NSEND = 8`). The engine clears only the buses
  written (`_bw`/`_bwl`, `busL`/`busR` mark them) and ThunderChains only the track slots it filled (`_tw`), so the size
  costs nothing per block. The editor's pickers list `TRK_N` = the host's `thunderChains.tracks` (a getter for NT: the
  object is made before NT exists) else 32. `chainsTracks()` ignores a Chain Out past NT instead of clamping it to NT.
- **Sidechain from any track**: `chnTapList(patch)` = the distinct `src` tracks of Sidechain Ins with no Send, ≤ 32; the
  player's `tapNode()` builds a merger of just those (rebuilt when the list changes, reconnecting every node) and sends
  `tapmap` (engine `tapMap[track] = channel`; `ctx.tap` reads through it; null = identity for an old host). `inpTap`
  uses the list. Also in `processorOptions.tapMap` (an offline render can end before a message arrives).
- **Sidechain Out** (`scout`, chains-only, after Sidechain Comp): writes (L+R)/2 × Level into `ctx.sendOut(n)`. Sidechain
  In got `snd` (0 off, 1–8; wins over `src`) reading `ctx.sendIn(n)`. In ThunderChains the sends leave on output 1 (8
  channels) and come back on input 3: player `sendBus` (8 ch, discrete) → `sendDly` (DelayNode, 128 samples: a loop needs
  a delay) → every node; `sendSync(b)` after each rewire (a rewire drops the node's outputs). A Sidechain Out runs in one
  node only: `_active()` adds it to the node of the lowest Chain Out track in its connected chain (undirected). Standalone
  (ChainsProcessor) the engine loops its own sends back a block later.
- **Buses**: `busPlan()` (host, beside `chnTaps`; cached on a fingerprint of Track In / Chain Out tracks, cables and
  CHN): per Track In t with CHN on, BFS over cables to the Chain Out tracks it reaches; b ≠ t with `chainsFeed(b)` makes t
  a source of bus b (`feeds` b → [t], `targets` t → [b]); `pure` when t's own Chain Out isn't reached. Loop-closing edges
  are dropped (`P.loop`). Player `busSync()` (end of `routeTrack`, also on its no-chain early return): per bus node a
  splitter per source on `p` (after FX, fader, pan) into 32-channel mergers → inputs 4 / 5 (16 stereo sources each),
  `busmap` (also in `processorOptions.busMap`); pure sources' `p → out` is cut (`_routed`), restored when not. ThunderChains
  copies each mapped source into its track slot, so the bus chain's Track In t reads it. Solo: `audible()` keeps a
  soloed bus's sources and a soloed source's bus. Renders: `pass()` adds the sources of kept buses; `renderOut` adds a bus
  track fed by kept sources (even with no notes, stem named "Bus") and skips stems of pure sources (they're in the bus).
  `chainsAutoOn` now also turns CHN on for new Track In tracks (`chainsAutoTracks()`).
- **Editor**: Chain Out set to a track no Track In is on is a bus (`m.bus = 1`, kept with the patch, toast); back on a
  Track In's track it's an insert. A Track In's change no longer drags a bus Chain Out; `alignChainTracks` leaves bus outs
  alone. Help texts (Track In, Chain Out, Sidechain Out, Sidechain In's Send), port tips, palette (Sidechain group),
  categories, a SEND meter for Sidechain Out (`display:'scout'`), meters start empty (`scaleX(0)`).
- **Bus Comp** (`buscomp`) and **Glue Comp** (`glue`) in the SHARED MODULES block (so …waves has them) with editor entries,
  presets and `display:'gr'`; also MODFX (track, master and pad effects; `tools/modfx-spec.mjs`). Both stereo-linked, SC
  input wins, else `source` (Self / the mixer's sidechain bus), SC high-pass (TPT SVF `bcHp`), auto release `bcRel`
  (0.1 → 1.2 s as reduction holds). Bus Comp: peak, 2 dB knee, stepped ratio / attack / release. Glue: RMS (~6 ms), knee,
  Range cap, Soft clip.
- Checked: node (`scratchpad/dsp_test.js`: both comps' gain reduction on a −6 dBFS sine at −20 dB threshold ≈ theory,
  send loopback one block late, Sidechain Out runs only in its node, tap map, Chain Out to track 120, buses cleared).
  Headless app with audio: bus 1 → 4 (track 4 mute / fader 0 silences it, solo either side keeps both, CHN off on 1 plays
  it direct again), Send 1 → Bus Comp SC on another track (GR ~ −17 dB; Send 2 empty ~ 0), Source track 1 tap (GR −14 dB),
  tracks 1–3 → stereo mixer → Glue → Chain Out 5 (all three routed, GR −9 dB), render + stems (mix, the Bus stem, no
  stems for bused tracks), editor (bus flag, Track In change leaves it, back to insert), Glue / Bus Comp as track FX,
  …waves loads; mute and duplicate regression tests identical. Not heard by ear yet.

## 20 Oct (9): groundwork for more than 32 tracks (mutes and the song file), nothing changes at 32

Cache `thunderplus-v201`. Owner: do steps 1 and 2 of "More tracks than 32" (Open items 9) now; the GUI and the mono / poly
split come later. `NT` is still 32, so the app behaves and saves exactly as before.
- **Block mutes** go through `blockMuted(Sx, y, t)` / `setBlockMute(y, t, on)` / `smuteInsertRows(at, h)` /
  `mutedCount()` (beside `blockMuted`); nothing else touches `S.smute`. Tracks 1–32 stay in `smute[y]`; tracks 33+ (only
  when NT > 32) in `S.smuteX[y]`, 32-bit words (word k bit b = track 33 + 32k + b), made on the first such mute and
  removed when empty, so with 32 tracks it never exists. normSong reads `smuteX` only when NT > 32.
- **Song file**: `songOut(Sx)` is what storage (`storeWrite(TK…)`, 3 places) and project files (`projBlob`) write. With
  NT ≤ 32 it returns the song itself. Above, each row is tracks 1–32 + the master (33 numbers, as every version reads) and
  tracks 33+ go in `songX[row]` (trailing empties trimmed, null when empty). normSong: a row of NT + 1 numbers (in
  memory: undo steps, or anything while NT is 32) is read as it is; a row of 33 with NT > 32 takes the master from slot
  32 and tracks 33+ from `songX`. Undo snapshots keep the in-memory shape.
- To raise the limit later: `NT` (and TRACKS' clamp, 8–NT already), then the screens, the mono / poly split and sidechain
  by track (Open items 9). Opening a bigger song in an older version keeps tracks 1–32 and the master; saving it there
  drops the rest.
- Checked headless: every mute path (hold, Alt-click, M, the menu over a selection, Insert / Ctrl+Backspace / Shift+Del in
  a column, Delete, undo, Ctrl+D pattern and track with notes, drag move, Alt-drag copy, the muted count) gives identical
  results in the old and new builds; storage and the project file are byte-for-byte the song as before; the duplicate
  tests identical. A debug copy with `NT = 128`: mutes on tracks 33 and 100 (no aliasing onto 1), save → 33-number rows
  + songX, reopen, undo, an old 33-number file (the master stays the master); that 128-track song opened in this build and
  in the previous one keeps tracks 1–32, their mutes and the master.

## 20 Oct (8): Renoise's Ctrl+D; clones on Ctrl+Alt+C / Ctrl+Shift+C

Cache `thunderplus-v200`. Desktop **v1.2.14** (20 Oct) has everything up to here. Owner: has used Renoise's Ctrl+D (both: sequencer and pattern editor, by context); keep Thunder's
clone on Ctrl+Alt+C and clone down on Ctrl+Shift+C ("to match").
- `globalKeys` (by `e.code`, since ⌥ changes `e.key` on a Mac): Ctrl/⌘+Shift+C → `cloneDown`, Ctrl/⌘+Alt+C → `cloneHere`,
  Ctrl/⌘+D / Ctrl/⌘+Alt+D → `dupKey(alt)`. The old Ctrl/⌘+D (clone) and Ctrl/⌘+Shift+D (clone down) are gone.
- `dupKey` (before `cloneDown`): on the Song (and Arrange holding the keys, which runs as the Song) → `dupPattern()`, the
  sequencer's duplicate: the cursor's song row (or the selection's rows), every track and the master, inserted again just
  below as unique copies (`newBlock`), block mutes kept; rows below move down (`S.song`, `S.smute`, `S.secs`, a song
  `loopArm` below); cursor and selection go onto the copy. Refused when the song's last rows aren't empty.
  On Phrase / Pattern / Sect / Mixer / FX → `dupTrack(notes)`, Renoise's Duplicate Track (Ctrl+D columns + effects, no
  notes; Ctrl+Alt+D "incl. Patterns"): Thunder's tracks are fixed, so it goes into the next track right of `dkTrack()`
  of the same kind (mono / poly) with nothing on the song: `S.mix` vol / pan / sc (not mute / solo), `S.tfx` (cloned),
  `rtFx`; with notes every block as a unique copy on the same row, block mutes kept. …chains isn't copied (one patch per
  song). The cursor follows (Mixer x, FX `cFxT`, pattern view `pvGo`, plain Phrase opens the copy on this row if any).
- Edit ▾ / right-click (`cmdItems`): Duplicate the pattern on the Song; Duplicate the track / with its notes elsewhere.
  Clone / Clone down labels, CMDS tooltips, the Song right-click menu (`sxCtx`), help KEYS table, Arrange footer, README.
- On a Mac ⌥⌘D is the system's Dock hiding toggle, so Ctrl+Alt+D is the one to use there (mod takes Ctrl too).
- Checked headless: Song one row and three selected rows (sections and mutes move, undo), Arrange, clone + paste, clone
  down, Phrase (skips a non-empty track), Pattern view with notes twice, Mixer poly track, no room, Edit ▾ lists.

## 20 Oct (7): / mutes the track (Renoise's key)

Cache `thunderplus-v199`. Desktop **v1.2.13** (20 Oct) has everything up to here. Owner: add Renoise's mute key.
- `/` (no modifiers, not numpad: `e.code !== 'NumpadDivide'`) in `globalKeys` → `muteKey()` (before `soloKey`): toggles
  `S.mix[t].mute` for `dkTrack()` (the whole track, as the mixer's M; M on the Song still mutes one block); the master
  can't be muted. globalKeys runs through `apRun`, so with Arrange holding the keys it is Arrange's cursor's track.
- Lost: the tracker's lower piano row no longer has `/` (E an octave up); `E` on the upper row plays the same note.
  PIANO still has `/`, so the Synth page plays it as before. Numpad / is still octave down, Shift+/ (?) still help.
  Help (KEYS table, the notes row), the Phrase hint, the welcome lines and README say Z–; now.
- Checked headless: Song, a Phrase note cell (nothing typed), Arrange on an empty cell, master, numpad /, Shift+/, E.

## 20 Oct (6): Arrange beside Phrase IS the Song screen; pattern selection colour; Ctrl+/ solo

Cache `thunderplus-v198`. Owner: CLN↓ in the command bar said it only works on the Song screen; "the arrange window is
basically a nested song mode": everything the Song screen does should work there. Desktop **v1.2.12** (20 Oct) has everything up to here.
- **Audit:** a differential headless test (session scratchpad `diff.py`) runs 47 Song-screen key sequences on the Song screen
  and with Arrange holding the keys beside Phrase (pattern view off and on) and compares song, mutes, cursor, selection,
  loop, play state and clipboard. All keyboard paths already matched (they run through `apRun`) except Alt+→ / SHIFT+▶.
- **Alt+→ / SHIFT+▶** with Arrange beside Phrase now does what it does on the Song screen (`nav` wrapper: `f(dir)`): opens
  the cursor's block (an empty cell gets a fresh phrase that goes on the song at its first note; SECT with sects shown),
  and the keys go back to the screen. Beside other screens it still goes back to that screen. Hint and footer say so.
- **Command bar** (KEYS + COMMAND BAR, it sits in `#tScr`): a click on it no longer hands the keys back to the phrase
  (`scr`'s capture pointerdown skips `#tCmd`, `.tinfo`, `#tStage`, `#tStagePs`), so its buttons act on Arrange as its
  ARRANGE tag says (that was the CLN↓ error: the click gave the keys back first, then CLN↓ ran on Phrase).
- **Edit ▾**: `editItems()` → `apSongItems()` (the Song screen's `cmdItems`, each run through `apRun`) while Arrange or
  the strip has the keys; the command bar's tooltips (`cmdTips`) likewise.
- **Right-click on the side panel** = the Song screen's menu: its builder is now `sxCtx(y, t)` → `{title, items}` (used
  by both; run through `apRun` for the side panel), so selection-aware Copy / Cut / Paste here / Clone / Clone down / Mute
  the selection / Loop these rows / Open / Name a section / Remove / Delete, then the rest of `cmdItems`.
- **Song mouse in the side panel** (SONG EXTRAS, when SONG MOUSE / LOOP DRAG are on): drag on empty cells selects, drag
  a block or the selection moves it, Alt-drag copies, drag the row numbers to loop rows (marked on the row numbers too,
  `sxAfterArr`). `sxSide()` (Arrange beside a screen holding the keys) makes `sxDown`, `sxStart`, `sxFrame` and `sxEnd`
  re-run themselves through `apRun`. The section lane still needs the Song screen's width.
- **Top bar**: LOOP SEL (`toggleLoop` wrapped by `apRun`) loops Arrange's song selection; ▶ SONG plays from `songRow()`
  as the Song screen does.
- **PATTERN view selection colour** (owner, plum skin: a selection looked like the open track's tint): `.tgrid.pvon
  span.sel` uses `--pv-sel`, amber mixed with the screen colour; the warm skins (night, mustard, citrus, contrast,
  sunset, desert, complement, espresso) get sky blue. The plain phrase view keeps `--ts-sel`.
- **Ctrl/⌘+/** solos the track you're on (Renoise's key; `soloKey` in `globalKeys`): `dkTrack()` (song cursor's track
  on Song and with Arrange holding the keys, the mixer's, FX's, else the phrase's); again un-solos it; solos add up as
  the mixer's S does. **Ctrl/⌘+Shift+/** clears every solo. The master can't be soloed. Help KEYS table and README.
- Checked headless: the differential test (all same), the command bar CLN↓, Edit ▾ items and Clone down, the side
  panel menu, drag select / move / undo, row-number loop drag, LOOP SEL, Alt+→ on an empty cell, solo on Song / Phrase /
  Arrange / Mixer / master, pattern selection in plum, night and the default skin.

## 20 Oct (5): Arrange's arrows reach empty cells with PATTERN view on

Cache `thunderplus-v197`. Owner (desktop, Phrase in PATTERN view, Arrange beside it holding the keys, KEYS mode): the
arrows only reached filled blocks (mono tracks), never an empty cell (the poly tracks, one filled), so no new pattern could
be started from Arrange. Arrange is meant to work exactly as the Song screen did.
- Cause: `pvLines` (every draw) pulled `cs.song` back onto the open phrase's block (`pvAlign`, `cs.song.x = cTrack`). A
  filled block made the phrase follow (`apFollow`), so the cursor landed; on an empty cell the phrase stays, so the
  pattern snapped the cursor back. Playing, `followPlay`'s `pvFollow` did the same every step.
- Fix: with Arrange or the strip holding the keys (`apReady && apF`), `pvLines` runs as before (the pattern still shows the
  open phrase's row) and then puts `cs.song` x / y / top back where Arrange had it (`pvLinesIn` guards the re-entry);
  `followPlay` skips `pvFollow` then. Keys back to the phrase (Esc, a click on it) re-align as before.
- Checked headless (pattern view, Arrange focus, stopped and playing): arrows across empty mono/poly cells and rows,
  Enter / Enter×2 on an empty cell, hex, M, Del, Shift+arrows select, copy/paste, Ctrl+D clone; without pattern view
  unchanged. The pattern's OWN arrows (keys on the phrase) still skip folded empty tracks, by design (EMPTY TRACKS FOLDED).

## 20 Oct (4): Ctrl+Space plays just this pattern, looped

Cache `thunderplus-v196`. Owner: a key to quickly play only the active pattern.
- `playPatLoop()` (after `toggleLoopPat`), from `globalKeys` on Ctrl/⌘+Space (no Alt/Shift): stopped → LOOP on (if off)
  and `tStart('song', {row})` from the cursor's row when anything on it is filled (else `songRow()`), so every track of
  that song row repeats; playing → `tStop()`, as Space. LOOP stays on afterwards; Alt+L turns it off. ⌘+Space is
  Spotlight on a Mac (the app never sees it), so Ctrl+Space is the documented key there too. Help KEYS table, the LOOP
  button's tooltip and README say so.
- Checked headless: Song screen (empty cell on a filled row loops that row), Phrase, Alt+L off while playing, Space stops.

## 20 Oct (3): the Phrase title line never overlaps

Cache `thunderplus-v195`. Desktop **v1.2.10** (20 Oct) has everything up to here. Owner: with the side panel open (and bigger text), PHRASE | PATTERN ran over the track name.
- `.thl` (title, LEN / LPB, PHRASE | PATTERN) no longer shrinks under its contents; `.tsub` (the track name) takes what is
  left. `thdFit()` (after `draw()` sets the title, and from a ResizeObserver on `.thd`; measured only when its signature
  changes) then steps through classes on `.thd` while the line overflows or the name is cut: f1 drops the "1/16 song"
  note (`.pstail`), f2 PHR | PAT (owner's suggestion; the buttons hold `.lfull` / `.lshort`) with tighter spacing, f3
  TRACK 02 (owner: MONO / POLY can go, the note columns show it), f4 T02, f5 no track name, f6 no LEN / LPB, f7 wrap.
- Checked at 1000 px with the side panel, text 100–170%, and the other screens' title lines (unchanged).

## 20 Oct (2): …chains starter patch takes both sides of the track

Cache `thunderplus-v194`; `chains-worklet.js?v=32`. Desktop **v1.2.9** (20 Oct) has everything up to here.
- The starter patch (`starterDoc`, chains/index.html) had only Track In Out → Tape delay In, so the track reached the chain
  as its left channel, played as mono (an effect with no In R takes In for both sides). It now has Out R → In R too.
- Existing patches (no `ti2`): a Track In whose Out R goes nowhere gets Out R → In R of every module its Out feeds where
  that In R is free, once. Editor: `trackInStereoUp` from `normalize` (outside the shared editor block; …waves has no
  Track In), saved with `ti2`. Engine: `trackInStereoCables` in `ChainsEngine.loadPatch` (after the shared modules block),
  for songs never opened in the editor. The host's `chnDiff` reloads the whole patch when `ti2` changes, as for `st2`.
- Checked: node (engine: the old starter gains the cable; with ti2, or Out R already used, nothing) and the app (a new
  song's starter has both; an old song's one-cable patch gains it on opening, once).

## 20 Oct: Thunder Plus as a MIDI sequencer (owner: "Thunder Plus could take it" instead of a separate sequencer app) — desktop v1.2.8

Cache `thunderplus-v193`. Desktop **v1.2.8** (20 Oct) has everything up to here. (Releasing from a Claude session: tag pushes
are refused, so start Actions → Desktop app with the version through the REST API: `gh api -X POST
repos/matthettich/thunder-plus/actions/workflows/desktop.yml/dispatches -f ref=main -f 'inputs[version]=X.Y.Z'`.) One commit; everything below is in `index.html` unless named. Checked headless (session
scratchpad: a debug-hook copy of the page, `window.__T(code)` evaluated inside the app's scope, and a fake Web MIDI with
two inputs and two outputs); not tried with real gear yet.

- **Poly tracks and MIDI (moNote):** each note column is its own voice: a new note in a column ends the note that column
  was holding (`h.col`, carried through ARP returns: `pend` entries and `pendPlay`). Before, a chord rang until an OFF,
  the same key or the 4-note cap, so two chords in a row sounded as six notes on a hardware poly synth. The pad side is
  unchanged (pads still ring over each other, as README says).
- **MIDI in routing** (Settings → MIDI; `midi` settings `inId`, `inCh`, `clkId`, `keys`, `lats`, all in
  `thunderplus-midi-v1`): **Notes in** (input + channel; `onMidi(e, src)`, hooked per input), **Clock from**
  (`clkFrom`: the picked input, or the first that sends clock, kept until 2 s quiet; two clock sources used to interleave
  and double the tempo), **Delay** per output port (`mLatOf(p)`, used by `msend`; Clock in's phase lock uses `clkLat()`:
  the port whose cable has the clock input's name, else A). Old settings: `lat` fills every port, and `lat` is still
  written (= port A). Active sensing (FE) and system messages are ignored.
- **KEYS PLAY** (Settings → MIDI → Keys play, default the current instrument): in the tracker a keyboard plays
  `kbInst()` (INST screen: cInst; a phrase: the cursor row's instrument, else `defInst()`; else `last.inst`).
  `preview(ii, note, vol, poly, when, hold)` has a held mode: its own column on the preview bus (`'h' + n`), MIDI
  instruments through `moNote` with `cap:16` (more keys than a column's 4); `previewOff(h)` ends it (MIDI note-off, a
  synth pad's release; drum pads ring). Bend (kept in `mx.bend` so a later song bend isn't skipped), aftertouch, poly
  aftertouch (+TRANSPOSE), program change and unlearned CCs go to a MIDI instrument's port/channel (`kbPass`, `kbCC`);
  CC 64 also holds pad notes (`kb.sus`, `kb.susd`). `releaseKeys()` on view change, window blur and the input settings.
  Computer keys typing a note hold its preview until keyup (`audKey` set by keysDown, `aud`, `audHeld`).
- **REC, metronome, count-in** (block "REC, METRONOME AND COUNT-IN" after `tStop`; settings `thunderplus-rec-v1`:
  `met` 0 off / 1 while recording / 2 always, `lvl`, `cnt` bars, `q` 1 STEP / 0 TICKS, `vel`, `offs`). REC/CLICK buttons
  in the top strip (`tRecBar`; CLICK hides with the TRANSP buttons when the strip doesn't fit), F9 / Alt+R, Alt+M.
  Step record: `recNote` (stopped) writes at the cursor; keys held together are one row (a poly track's columns, a mono
  track keeps the first); `recOff` moves the cursor by EDIT STEP once every key is up. Live record: the pump records
  when each step of `recTrack()` is heard (`recHist` → `rec.hist`); `recStep(ta, tr)` finds the nearest step (or, TICKS,
  the step it was in plus a DEL), `ta = p2a(MIDI timestamp)`. Note-offs write OFF only for MIDI instruments and synth
  slots, only on a later step whose cell is empty. The computer keys and the keypad record at the playhead while
  playing (`typeNote` → `recKey`). A take is one undo step: `var recTake` stops `snap()` until `recEndTake()` (STOP, REC
  off). Clicks: `click(t, acc)` into `master` (never renders), booked by `metRun` from `mx.beat` up to 100 ms ahead
  (`met.next` counts on while off, so switching on mid-song stays on the beat); `metStop` stops booked ones. Count-in:
  `tStart` puts the song's start `recS.cnt` bars later and `countIn` clicks them (not when Clock in starts us).
- **MIDI files** (block "MIDI FILES" before "bringing over ...Thunder's saved work"): `songMidiEvents()` runs `makeSeq`
  from RENDER FROM like a render (`once`, `sim`), times → beats through the beat events, note ends from OFF/KIL/the
  next note/the column/NOTE LEN, drum pads (one-shot slots, not MIDI) as `drumNote(pad)` on channel 10, a 16th long;
  `smfBytes` writes type 1 at 480 PPQ (offs before CCs before ons at a tick). `parseSmf` reads type 0/1 (running status,
  meta, sysex; a note with no note-off lasts a 16th); `importMidiFile` writes phrases of PATTERN LEN at the song LPB from
  the cursor's track and song row (asks before replacing blocks), sharing identical phrases and one empty one. File ▾,
  Project → Render & export (both in `inFile`), hidden input `#midIn`.
- **Key** (block "KEY" in the MIDI section): `lxp` (`thunderplus-livexp-v1`: 0 off, 1 whole keyboard from C-4,
  2 keys below C-4 from C-3; latched `lxp.st`), applied in `makeSeq` only when `o.live`; `SCALES`/`scaleSnap(S, n)`
  (nearest, ties down) with `S.scale`/`S.sroot` (song, normSong). Both only for instruments with `I.kf` (KEY FOLLOW).
- **makeSeq**: `nOf(x)` is the note with every transpose, live transpose and the scale (before PIT/FIN), also used for
  the piano's SOUNDING NOTES. HUMAN TIME/VEL (`I.hut` ms, `I.huv` %) move each note's time (`nat`) and gain/vel with
  `rand()` (so SED repeats them). Note events carry `tick` (NOTE LEN). `mcc` events carry `ramp` (CSL). **DIR**: `T.dir`
  (0–3), `T.pp` (ping-pong heading), `T.cnt` (steps played this pass, reset when a pass ends); with a direction a pass
  still lasts the phrase's LEN steps, and `dirGo` says where the next pass starts (bottom for backwards, ping-pong goes
  on from where it was, random anywhere). A selection loop plays forward.
- **MIDI out extras:** NOTE LEN (`I.mlen` ticks) in `moNote`'s `end`; ECHO (`I.eco` repeats, `I.ecr` beats,
  `I.ecf` %, `I.ecp` semitones) queued in `mx.echo` and booked by `moEcho` ≤ 100 ms ahead; CSL glides in `mx.ccr`
  (from `mx.ccv`, the last value per port/channel/CC; a CC never sent jumps), booked by `moRamp` ≤ 80 ms ahead, a new
  value on the same CC cuts a glide. `moRelease` clears both queues.
- **New commands** (appended to `FXC`, so saved songs keep their numbers): `CSL` (index 39), `DIR` (40); FXI, FXB,
  RSTEP and the help's command list have them.
- **Instruments:** `NI = 64` (one constant; the I column, INS, INST, the list and song files follow it). New fields in
  `freshInst`/`normSong`: `kf` (older songs: on for MIDI instruments and pads ≥ 08), `hut`, `huv`, `mlen`, `eco`, `ecr`,
  `ecf`, `ecp`. INST rows: KEY FOLLOW, HUMAN TIME, HUMAN VEL; for MIDI instruments NOTE LEN and ECHO (+ TIME, FADE,
  PITCH). The instrument list shows 00–0F plus instruments used in a phrase or changed (`instShown`, cached on songVer).
- **SLOTS** (top of the Synth page, `#vKit`): `NSLOT` = `kit.slots` (16–`MAXSLOT` 64, saved in the kit; kits without it
  are 16). `setSlots(n)`: more slots grow the kit with `slotVoice(i)` (a simple synth, or a drum if the slot's kind
  says so); fewer only hide the last ones (their voices stay in `kit.voices` and come back). **THIS SLOT** ONE-SHOT /
  HELD: `kit.held[i]` (saved; null = by position as before). `isSyn` lives in the ENGINE block, which can't see `kit`,
  so it reads `slotHeld`, kept in step by `syncHeld()` (boot, `setKit`, `setSlotKind`). Both are kit undo steps
  (`kitStep`). `instFit()` points instruments 10+ that nothing uses and nobody changed at slot k (or k % slots) when the
  count changes, without an undo step. A voice per track still overrides SLOTS (the control is disabled). MIDI learn
  works for slots past 16 (`midi.notes` grows; saved up to 64). The desktop pad column's third heading reads MORE SLOTS
  without a voice per track.
- **Euclidean fill** (Edit ▾ and the right-click menu, phrase only): `euclid(k, n, r)` is Bjorklund's algorithm;
  `euclidFill()` writes the column under the cursor over the selection or from the cursor down.
- **Settings sheets:** a new **Record & play** sheet (`SHEETS.seq`, button in Settings → Song & input) with Recording,
  Metronome and Live transpose; SCALE and SCALE ROOT are in the Song sheet's new Key group.
- **After an independent review (same day, before merging):**
  - `S.ni` (song file): the instruments a song was made with. Older songs load as 16, and `makeSeq` clamps `INS` to
    `ni − 1` (`NIs`), so an INS above 0F (typed, RND, RNL, REP) still plays 0F there. `niUp` makes it 64 once an
    instrument past 0F goes into a phrase: `niScan` (the I column, from `saveS`) or `after()` (a typed INS value).
  - `normSong` keeps any slot up to `MAXSLOT − 1` (it used to clamp to the slot count at load, 32 at start-up, which
    rewrote instruments on bigger kits). A note on a slot the kit doesn't have is silent (`startNote`, `renderOut`).
  - KEYS PLAY: held MIDI previews go out now (`p2a(performance.now() + delay)`), not after the audio output's latency;
    the sustain pedal's port/channel is kept (`kb.susTo`) and `susUp()` lets it up on pedal-up anywhere, `releaseKeys`
    (view change, blur, input settings) and `moRelease` (STOP). `releaseKeys` also lets go of REC's computer keys
    (`compHeld`) and the pads' held notes (`midiHeld`). Keys play defaults to the pads for someone with learned notes.
  - Live transpose keys only in the tracker, and note-offs are handled before them. `lxp`'s initializer can't use
    `clamp` (declared later): it was silently always OFF after a reload.
  - REC: count-in only when PLAY starts from stopped (`wasOn` in `tStart`); `recEndTake` settles the take at once,
    and `undoStep`/`importMidiFile` (and `setSong` via `tStop`) end it first, so undo mid-take undoes the take.
  - DIR: with a direction the swing pair counts `T.cnt` (steps played), so a pass lasts as a forward one; it works
    with song and sect loops (only a phrase-row loop plays forward).
  - ECHO: note-offs booked lazily; an echo is left out while a real note holds its key and ends before one starts.
    `moRelease` ends echoes already sounding.
  - MIDI files: ARP steps after KIL/OFF/NOTE LEN are left out, every off after its on; drums go by the pad that plays
    (`e.pad`, so a voice per track works); `smfBytes` writes into a growing Uint8Array (spreading a big body into
    push() threw); the reader skips system common bytes and rejects a zero division.
  - `instFit` recognises an instrument a slot count put anywhere (k % m for any m); `syncHeld` ignores `kit.held`
    with a voice per track (and THIS SLOT is disabled there).
  - Tests for every finding: session scratchpad (`t1`–`t10`), all passing.

## 19 Oct (17): desktop updates download directly on the Mac (and the portable .exe)

Cache `thunderplus-v192`. Not in a desktop build yet (v1.2.6 still opens the download page; the next build has this).
- `electron/updater.js`: where the app can't replace itself (Mac, portable .exe: `notifyOnly`), **Download** (the box,
  the top-bar Update button via `act()`) now fetches the release's `.dmg` / `-portable.exe` (found with GitHub's API,
  `releases/tags/vX`) into Downloads with `net.fetch`, streaming to `name.part` and renaming when complete; progress
  goes to the page (`state.dl` 0–1) and the Dock / taskbar (`setProgressBar`). Done: the Mac opens the .dmg (its
  drag-to-Applications window), the portable .exe is shown in its folder, and a box offers **Quit …Thunder Plus** (the
  Finder won't replace an open app). Any failure: a box with **Open download page**. Clicking again after it's
  downloaded opens it again. Files the app writes itself should carry no quarantine flag (it doesn't set
  LSFileQuarantineEnabled), so Gatekeeper may not stop the new copy: not yet checked on a real Mac.
- The page (`deskUpdShow`): the Update button reads **Update 42%** while downloading, then **Install**.
- Tested in node with Electron stubbed and a local server (progress, rename, reopen, and a failed download).

## 19 Oct (16): presets for …chains modules; Blades/Phaser choice fix — desktop v1.2.6

Cache `thunderplus-v191`. Desktop **v1.2.6** (19 Oct) has everything up to here.
- **Presets** (chains/index.html, outside the shared block, so …waves has none yet): `PRESETS` (263 factory presets for
  72 modules, `{type: [[name, params]]}`; choice values are the parameter's own numbers) and a **Preset** row
  (`presetRow`, first in the body) on every module with factory presets, every effect (`bypOk`), the sound sources and
  any type with a saved preset. The menu (`preMenu`, `showMenu` with `§` section headings): Default, Factory, Yours,
  Save as a preset… (`askDialog`; localStorage `chains.presets`, per device, for every patch), Delete one of yours….
  `applyPreset` snapshots (one undo), sets every param but `PRE_SKIP` (track, chans, _byp, _bn, src, edit): named ones
  clamped, the rest to defaults. `m.pre` (saved with the patch) names it; `setParam` sets `m.preEd` ("edited").
  A table test checks every preset's params exist, are in range and are real choices. Help (both) describes it.
- **Fix**: Blades' Slope and Phaser's Stages lists had values 0–3 for parameters that take 2/4 and 2–8, so the list
  showed blank and 4-pole never took; the choices now carry the real values (shared block, both editors). A check over
  every choice list in both editors finds no other mismatch.
- Sidechain In's Source has no modulation jack (`fixed`).

## 19 Oct (15): Sidechain In picks its source; its meter; SC lights

Cache `thunderplus-v190`; `chains-worklet.js?v=31`.
- **Source** on Sidechain In (`src`, first control): 0 the sidechain bus (the Mixer's SC row, as before), 1–32 one
  track's own sound: mono, taken at the track's input node (before its …chains, FX and fader), so a muted ghost kick
  still drives it and nothing can loop back into a chain. Host (`makePlayer`): a 32-input `ChannelMerger` (`taps`,
  made when first needed) fed by every `buses[t].inp`; `tapSync(b)` connects it to input 2 of a track's …chains node
  while `chnTaps(S.chainPatch)` (a Sidechain In with src > 0); called from `routeTrack` and, via the player's
  `tapsSync`, from the bridge's `param('src')` and `mod`. `routeTrack`'s `b.inp.disconnect()` drops the tap, so
  `inpTap(t)` reconnects it. `chainsNode` now has 3 inputs with `channelCountMode:'max'` (input 2 arrives with 32
  channels; a mono track input reads as both sides, as before). Engine: `ThunderChains.process` puts `ins[2]` in
  `e.taps`; `ctx.tap(k)` is a channel or silence.
- **Meter** on Sidechain In (`display:'scin'`, `show`: peak dB and envelope): IN with a 1.5 s peak tick, ENV 0–100%,
  falling back when telemetry stops. `_active` runs an unpatched Sidechain In while the editor watches
  (`telemetryOn === true`), so it can be checked before it's patched.
- **SC lights**: every SC input (Sidechain Comp, Compressor, Noise gate, Envelope filter) has an LED (`.scled`) that
  flashes with the sidechain it hears: the SC cable's jack meter, or `scbus` (`ChainsEngine._scPeak`: the bus's peak,
  reported when the module listens to the bus: Sidechain Comp always, the others with Source on Sidechain).
- Tested in the app (headless, playing): Track 1 source unpatched −45 dB / env .17; bus without SC Out silent; bus with
  SC Out −38 dB; a silent track silent; Track 1 muted still drives it; the LED flickers with the hits.

## 19 Oct (14): a taller bottom panel by default; DANCE/DRUM only when the dogs fit

Cache `thunderplus-v189`.
- **Bottom panel height** (owner: dragging it taller looked better and made room for the dogs, since the phrase's text
  sizes to the height left): `deck.h` is now 0 = follow the window (`deckH()`: a quarter of `innerHeight`, 168–240 px),
  a number once the top edge is dragged; a double-click on the edge goes back to following. Saved `168` (the old fixed
  default, so never dragged) reads as 0. A resize listener updates `--dkh` before the refit measures.
- **DANCE · DRUM · KEYS · HIDE**: `stageRoom()` (split out of `stageLayout`) measures the room beside the grid on every
  layout, HIDE included; `stgRoomPh` 2 the dogs fit (150 px), 1 only the keys (90 px), 0 nothing (or under 150 px tall).
  `phStageSwitch` hides DANCE and DRUM at 1 (KEYS shows lit, since the keys stand in; the choice is kept, so the dogs
  come back when there's room) and the whole switch at 0. Tested 1100×700 to 1680×1000, phrase and pattern view.

## 19 Oct (13): every …chains effect is stereo

Cache `thunderplus-v188`; `chains-worklet.js?v=30`, `waves-worklet.js?v=25`.
- **VCF, VCA, Sidechain Comp** (chains only) and **Blades, Meta, Phaser** (shared, so …waves too) got **In R / Out R**
  with their own right-side state (`inR(io)`: In R falls back to In, so a mono feed gives L = R). Everything else that
  passes audio was already stereo.
- **Older patches** (no `st2`) get their right sides patched once, wherever the left side is: a cable X.Out → one of the
  six adds X.Out R → In R; one from their Out → a module with an In R adds Out R → In R (only if that In R is free).
  The editors' `normalize` runs `chainsStereoUp` (shared editor block) and saves it with `st2:1`; the engines'
  `loadPatch` runs `stereoUpCables` (shared modules block) so a song that's never reopened in the editor still plays in
  stereo. The host's `chnDiff` sends a full reload when `st2` changes, so a right-side cable deleted in the editor goes.
- Tested: node (both engines: cables added without st2, none with it) and both editors (normalize adds them once).

## 19 Oct (12): …chains module bypass, sequenced with BYP

Cache `thunderplus-v187`; `chains-worklet.js?v=29`.
- **Engine** (chains-worklet.js, `ChainsEngine`, not the shared block): an effect module (`bypOk`: audio `in` and `out`)
  is bypassed when its `_byp` param is set, or when the tracker says so: `bsw[n]` for its number n (−1 the patch's
  own, 0 in, 1 bypassed, 2 the opposite of the patch's own). `_bypRun` passes `in`/`inR` to `out`/`outR`, crossfading
  over the switching block; once settled it runs the module on silence (gates and LFOs stay on the beat, delays and
  reverbs come back empty). Numbers: `_bn` (1–15), else the lowest free one in patch order (`_bnMap`, cached on the
  graph version; `setParam('_bn')` clears it). `addModule` keeps `_byp`/`_bn`. Telemetry reports `byp` (ids bypassed
  now). `ThunderChains` has k-rate AudioParams `b1`–`b15` copied into `bsw` each block. Node test: 0.0 dB through vs
  −52 dB filtered, every switch value, no step larger than the signal's own slope at a switch.
- **Editor** (chains/index.html, outside the shared block, so …waves has none): `buildNode` puts a **BYP n** button on
  the title bar of bypassable modules (`setParam(n, '_byp')`, undoable), `bnMap` mirrors the engine's numbering,
  `bnFree` gives new and pasted modules a free number, `paintByp` (renderAll, add, remove, paste, telemetry) labels
  and lights them; `.seq` (dashed outline) when the tracker bypassed it, `.node.bypassed` dims the module.
  The host forwards `byp` from the track engines in `meters()`.
- **Tracker: BYP XY** (appended to `FXC`, index 38; FXB/FXI/help): X the module number (0 = all numbered), Y 0 in,
  1 bypassed, 2–F the other way from the patch for Y rows then back. The sequencer emits `chc` events (`b1`–`b15` on
  that track's …chains node), the player sets them sample-timed like FXO's `fxc`; `resetSeq` puts them back to −1.
- **Shift+arrows never change screen** (owner: walking the screens with Shift got stuck on Phrase, where Shift selects):
  on screens with nothing to select (Inst, Mixer, FX, Project, a phrase's settings line) they move like plain arrows.

## 19 Oct (11): Option moves, Command edits

Cache `thunderplus-v185`.
- **KEYS mode modifiers swapped** (owner, the same afternoon as the (4) remap): **⌥/Alt + arrows change screen** (`nav`:
  ◀▶ tab order, ▲ Project / back, ▼ Mixer; from Phrase ◀ gives Arrange the keys), **Ctrl/⌘ + arrows change the value**
  (`bump`: ◀▶ ±1, ▲▼ more), in PAD mode with a keyboard too (Ctrl/⌘ + arrows = EDIT + arrows; Alt + arrows still page
  there, OPTION + arrows). Ctrl/⌘ + Shift + arrows no longer change screen (globalKeys); they fall through to the value.
  Shift + ⌥ + arrows (other phrase / block) and Shift + arrows (select) unchanged. Menu key labels (`cmdItems` val →
  'mod+'), HINTK, Arrange's lines, the Pattern hint, the legend and the KEYS help tables all say so.
- **A phrase playing on its own lights its block** (owner): `playhead()` now also answers in phrase / sect playback:
  the playing track's block on the song cursor's row when it holds that phrase (sect), else the first block on that
  track that does. Arrange (side panel and Song screen), the Song grid and the strip light it as in song playback.
- **FX screen: short names on the slot lines, the description above the controls** (owner: long descriptions ran off
  the screen): slot lines are `n + fxNameOf(type)` (the pad list's name, the module's, or the description up to its
  colon: "FILT Filter", "DLY Tape delay"); the ACTIVE row carries `desc` and draw() puts it, wrapped (`.ln.wrap`,
  `.fxdesc`, max 68ch), in the gap above ACTIVE, not as a row, so the cursor never lands on it. The stage's signal
  boxes use the short names too.

## 19 Oct (10): the COMMAND BAR is the editor's toolbar

Cache `thunderplus-v182`.
- **Mock-up A** (owner): in KEYS mode with the COMMAND BAR on, `#tCmd` lives in `#tScr` just above `.tinfo` (the middle
  column, under the phrase, the pattern, Arrange as the Song screen, the mixer…), in PHRASE | PATTERN's look: `.7em` mono,
  joined groups in `--ts-ink` (UNDO|REDO · CUT|COPY|PASTE|CLEAR · SELECT(|CLONE|CLONE↓ on Song) · −−|−|+|++ · ‹PREV|NEXT›,
  ▶ SONG at the right). `cmdBarState()` (end of draw(), and applyCmdOrder) re-places it before `.tinfo` (Arrange as Song
  is inserted there too), sets the tag `.cmtag` (PHRASE / PATTERN / ARRANGE when Arrange has the keys or is the Song
  screen / SONG / MIXER…) and greys (`.off`, pointer-events none) what can't act: cut/copy/select off the grid screens,
  paste without a clip for that screen, undo/redo with nothing to undo (`undo.pend` counts). ±16 are shown on every screen
  now. PAD mode or the bar off: it goes home (`cmdHome`). The stage (piano/dogs) is laid out from the grid, so it ends at
  the toolbar. The (7) column alignment (`cmdAlign`, `CMD_GROUPS`, `CMD_ANCHOR`) is gone.

## 19 Oct (9): Delete on the song deletes the phrase; Remove from song keeps it

Cache `thunderplus-v181`. Desktop **v1.2.5** (19 Oct, from 542183e) has everything up to here.
- **Delete / Clear on the song** (Delete or Backspace, Edit ▾ "Delete (the phrase too)", right-click, PAD OPT+EDIT; with
  Arrange holding the keys it runs as the Song screen): `songDelete(cells)` takes the blocks off and then, for each sect
  no longer on the song anywhere, empties it (`freshSect`) and erases its phrases (`freshPhrase`, plen = dlen, plpb 0)
  unless another sect still plays them. A phrase shared with another block (a clone that shares) is kept and the toast
  says how many copies remain. One undo step (the whole-song snapshot) brings it back. With blocks selected on the song,
  Delete now deletes them (it used to cut them to the clipboard); Ctrl/⌘+X still cuts and keeps the phrases.
- **Remove from song (keep the phrase)** (`cmdAction('rmv')`, in `cmdItems` on the Song screen, so Edit ▾, the Song grid's
  right-click and the side Arrange panel's menu; Arrange-as-Song's own menu via `sxClearRect(r, true)`): the old
  behaviour, the block goes and its phrase stays under its number (Phrases… lists it as not on the song). Shift+Delete
  (clear the cell and step down) also keeps the phrase.
- Tested headless: shared block kept, last use erased, undo, Remove keeps, selection delete (3 blocks, 3 phrases, the
  clipboard untouched), both right-click menus.

## 19 Oct (8): LOOP = the pattern, LOOP SEL = the selection

Cache `thunderplus-v180`.
- **LOOP** (tab bar `#tLoop`, the keypad's LOOP `#kLoop`, Alt+L, plain L off a note cell): `loopPat`. Any song play
  (`tStart` adds `{v:'pat', y0:row, y1:row}` when no other loop is given) repeats its row, i.e. the pattern (each track
  goes round its block there). `syncLoop` keeps it on `patLoopRow()`: the row it started on, or the row the song cursor
  moves to (`patCur` vs `cs.song.y`: the Song cursor, or opening another phrase / pattern), so the tracks move there as
  their phrases end; turned on mid-play it takes the playing row (`patPlayRow`), off releases it (`loopV` ''). A phrase
  played alone repeats anyway, so on the Phrase screen LOOP matters for Shift+Space (and PATTERN view's Shift+Space). The
  button shows ON, or the looped row while it plays.
- **LOOP SEL** (`#tLoopSel` beside it, Shift+Alt+L, Shift+L off a note cell): the old selection loop (`loopSel`,
  `loopArm`, drag-to-loop on the Song row numbers); where it applies it wins. Hidden on the phone setup (`body.mob`).
- Tested headless at 480 BPM: holds row 0, follows the cursor to row 1, releases when off; pattern view Shift+Space
  loops its row; Space still plays the phrase alone.

## 19 Oct (7): top bar mock-up D, DANCE · DRUM · KEYS · HIDE on the title line, command bar in columns

Cache `thunderplus-v178`.
- **Top bar = mock-up D** (owner: the inverted bar of (5) felt too heavy): light Simple skins give `.top` and its own
  buttons `--mxpanel2`; the screen tabs (`.ttabs button[data-v]`) are borderless words, the open one bold with a 4px
  underline (`inset 0 -4px 0 var(--mx)`). The (5) inverted-bar rules are gone.
- **Phrase stage switch moved to the title line** (`#tStagePs`, now `.pvseg.phsw` inside `.thd`, after `#tSub`): the same
  look and line as PHRASE | PATTERN, at the right; `.tsub` has `margin-left:auto`. **HIDE** added (`PHS` has 'hide',
  stageKind gives '' so the stage hides). `phStageSwitch()` (no args) runs from draw() and stageLayout(): shown on Phrase
  when Settings → STAGE is BY SCREEN, not on the master's phrase, not in SIMPLE PHRASE, ≥600px wide. Its pointerdown
  stops propagation (the screen's drag-scroll took the click, as for PHRASE | PATTERN).
- **Pattern bar buttons the same size** as PHRASE | PATTERN and the stage switch (`.tscr .pvbar button`: .7em of the
  screen like `.pvseg`, line-height 1): ▲▼, FX2/3 and EMPTY TRACKS; the bar's right edge already meets the switch's.
- **COMMAND BAR (desktop) like the panel header under it**: 22px strip, `.dkt`-style chips (17px, fs-2, bold, flat; the
  term keypad shadow is beaten with `:not(#zz)`), padding 6px. `cmdAlign()` starts each group (`CMD_GROUPS`) above a part's
  title chip (`CMD_ANCHOR`: undo → SCOPE, cut → INST, select → …CHAINS, the value steps → FX; prev/next follow; SONG at the
  right) by adding margin-left; a hidden or folded part leaves its group in the flow; if the result would overflow, all
  margins are dropped. Re-run (rAF, skipped when its signature is unchanged) from a ResizeObserver on the bar and the deck
  parts, draw() (song/phrase changes the visible buttons), applyCmdOrder and font loads; PAD mode clears the margins.
- **Caps Lock fixed again** (see the Caps Lock line in (4)); tested with synthetic Mac and Windows event patterns.

## 19 Oct (6): basic effects (simple delay, filter, cut, compressor, gate, auto-pan, pump) and grouped FX picker

Cache `thunderplus-v176`; `chains-worklet.js?v=28`, `waves-worklet.js?v=24`.
- **Seven new shared modules** (chains-worklet.js SHARED MODULES block, "Basic studio effects"; synced to …waves):
  `sdelay` Simple delay (sync divisions with dotted/triplets `SD_DIVS`, ms, feedback ×.95, Stereo/Ping-pong, one-pole low
  and high cut on the repeats, mix keeps dry at 100% to 50%; a new time glides 60 ms), `filter` Filter (TPT SVF LP/HP/BP/
  notch, 12/24 dB, resonance, drive, tempo-synced LFO ±4 oct in six shapes, cutoff smoothed 5 ms), `cut` Low and high cut
  (Butterworth 6/12/24 dB, each off at its end), `compressor` (threshold/ratio/attack/release/soft knee/makeup, stereo-linked
  peak, Self or Sidechain bus or SC cable, parallel mix), `ngate` Noise gate (hysteresis, hold, range, sidechain), `autopan`
  (sync/rate, depth, sine/triangle/rounded square, sides only turned down), `pump` (LFO-tool duck every 1/8–1 bar, depth,
  release share, curve). Helpers `bfSvf/bfStep/bfTidy/bfK/bfTempo`. Editor entries + help + palette ('Basic effects';
  PAL_LAYOUT places them in Dynamics / Filters and EQ / Modulation / Delays / Rhythmic) in the SHARED EDITOR block.
- **Also exposed as pad/track effects** (modules that already existed): `limiter` LIMT, `ringmod` RMOD, `width` WIDE
  ("Stereo width"), `utility` UTIL. Pads render mono (L+R averaged), so autopan, pump, width and utility are track-only
  (`pad:false` in tools/modfx-spec.mjs). Old pad "Compressor" (Pressure4) keeps its name; the new one is "Compressor
  (classic)" on pads, CMPR on tracks.
- Checked in Node (scratch test, 30 checks): delay lands on 24000 samples at 1/4 120 BPM, ping-pong alternates, 100%
  feedback stays bounded over 20 s; LP12 −3.0/−12.4 dB and LP24 −3.0/−24.2 at fc/2fc, HP mirrored, BP unity, notch −150
  dB; cut −3 dB at the corner; compressor −6 dBFS sine at −18/4:1 → −8.7 dB; sidechain ducks; gate passes/silences and
  Range 20 dB; auto-pan never louder; pump silent on the beat; 280 random settings finite; 120–690× real time each.
  In the app: every one builds a track chain, a song plays with four chained, the pad ones render. **Not heard by ear.**
- **FX slot picker grouped** (owner): `FXT_LIST` is now sorted by group (`FX_GROUPS`, shared with the pad Add effect
  list: Dynamics and EQ, Filters, Drive and lo-fi, Modulation, Delays, Reverbs, Pitch and voice, Resonators, Spectral and
  granular, Rhythmic, Utility, More), the everyday ones first (`FXT_FIRST`), and `fxsOpen` passes `heads` so `fxpFill`
  shows sticky `.fph` headings (not items: keys and clicks skip them; `.fpi` has scroll-margin for them). Slots keep their
  effect by name, so reordering is safe. Frequency shifter moved from More to Modulation (pads too).

## 19 Oct (5): inverted top bar, bottom panel scroll arrows, pattern view scrolling and play keys

Cache `thunderplus-v175`. Desktop **v1.2.4** has everything above the meter fix.
- ~~Top bar inverted~~ (replaced by mock-up D in (7)) (owner chose mock-up B): on the light Simple (term) skins `.top` takes `--mx` as its background,
  its own buttons (`.top >`, `.top > .seg >`, `.top > .setwrap >`) go transparent with `--mxon` text and a 40% mix
  border (`--tb-ln`); `.on` is filled `--mxon` with `--mx` text. The menus inside `.top` (#gfile, #gedit, #gset, #phl)
  keep the skin's colours (`.top`'s own colour is untouched). #bUpd stays filled. Dark Simple skins (matrix, bwterm,
  night, espresso, midnight), Pastel and Minimal are unchanged. Specificity: `:not(#zz)` beats the term `.on` list.
- **Bottom panel ▼ / ▲** (`DKMORE`, `dkMore`, `dkMoreDue`): INST and FX lists (`#dkInstRows`, `#dkFxRows`; the …chains
  list wraps into columns and doesn't scroll) show a small ▼ at the list's bottom right when more is below and a ▲ at
  its top right when scrolled down; a click scrolls a screenful, a wheel over them scrolls the list. While a list
  overflows it has a 24px right gutter (`.mov`) so the arrows never cover a value, and the cut edge fades (mask).
  Updated on scroll, ResizeObserver, a MutationObserver on the rows, and window resize.
- **Pattern view SONG ROW ▲ ▼**: the bar's row stepper is "SONG ROW 000 ▲▼" (`.pvud`), not ◀ ▶ around the label
  (song rows run down the song; the owner found sideways arrows for that awkward).
- **Pattern view scrolls sideways with the mouse**: `.tgrid.pvon{overflow-x:auto}` with a thin skin-coloured scrollbar
  (::-webkit-scrollbar; Firefox scrollbar-color), and the screen's wheel handler sends a sideways swipe / tilt wheel /
  Shift+wheel to `grid.scrollLeft`. draw()'s generic cursor follow is skipped in the pattern view (pvScroll alone, from
  where the view is), so a click on a track scrolled into view only snaps to a track edge; a plain wheel (which moves
  the cursor's row) sets `pvHoldX` so the view stays where it was scrolled. Arrow keys still bring the cursor back.
  Headless Chromium hides scrollbars: launch with `ignore_default_args=['--hide-scrollbars']` to see it.
- **Mixer meters torn on the Simple skins** (owner's screenshot): the meters are block characters (`BLK`, the master's
  `HB`); Share Tech Mono has none, so they came from JetBrains Mono, ~10% wider, and a lit cell pushed the rest of its
  line right. `.mt` and the master's `.mm.m1–m3` are inline-blocks `calc(N * var(--chw))` wide, clipped at the sides only
  (`clip-path`, so rows still overlap without seams). `--chw` is the measured advance of a character in the grid
  (`chwSet`, per text size / skin / font load): `1ch` alone misses the body's inherited −.01em letter-spacing.
- **Pattern view play back to before** (owner): Space plays the open phrase (as in Phrase); Shift+Space plays every
  track from the song row on. `songRow()` in the pattern view takes the pattern's row when any track on it is filled.
  The 'pattern' loop kind (`loopV`) is gone.

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
- ~~Pattern view PLAY = the whole pattern, looping~~: undone in 19 Oct (5), the owner preferred Space = this phrase.
- **Arrange beside every screen** (`apArrOk`: any screen but Song; `apHost` remembers it; `apBack` returns to it):
  a click gives Arrange the keys (Shift+◀ and the SONG tab only on Phrase, so screen navigation is unchanged), the
  screen follows the block (`apFollow`: FX → cFxT, Mixer → its track, Inst → the block's instrument), a double click
  opens the phrase, Esc / a click on the screen gives the keys back. Right-click in the side panel: Open the phrase,
  Mute, then `cmdItems()` run through `apRun`. Right-clicks no longer count as taps or hold-to-mute there. A click on
  an empty cell (with ARRANGE FOCUS off) just moves the cursor. Checked beside FX, Mixer and Inst.
- **POLY cells per skin**: light term skins mix `--tk-poly` 35% with white (plum went grey mixing green into pink);
  the dark ones keep mixing with their background.
- **Caps Lock**: on a Mac a keyup types an OFF too (`MACK`; Chromium there sends keydown only when it turns on and keyup
  only when it turns off). Fixed again in (7): the first version (`capsDn`, a keyup with no keydown before it) still lost every
  other press because the turning-on keydown was always there. A keydown/keyup pair (<400 ms, same `getModifierState`) counts once.
- **Poly OFF per column**: a poly row's OFF becomes `{type:'off', cols}`; voices carry `col` (all three voice makers);
  the player and MIDI out (`moOffCols`, held notes tagged with `col`) stop only those columns; the piano drops only
  those columns (`pos[t].offc`). Mono tracks unchanged.
- **CLONE = copy a unique clone** (Ctrl/⌘+D, CLN, Edit ▾, right-click): `cloneHere` puts the block (or selection) on
  the clipboard with `uniq`; `paste` → `pasteClone` makes a fresh unique copy (newBlock) at the cursor each time; the
  original stays. EDIT×2 / Enter twice still makes a unique copy in place.
- **⌥/Alt (or EDIT) + arrows on an EMPTY song block** start a new pattern on the next free number (`bump` → `newBlock(c,
  -1)`, sects hidden), not the last number used (00 on a fresh song). With Arrange holding the keys it opens in the editor.
- **KEYS mode: Ctrl/⌘+arrows change screen, Shift+arrows select** (owner; the modifiers were swapped in (11): ⌥ moves, ⌘ edits): keysDown's mod branch calls `nav(dir)` in
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
8. **The synth engine as a VST / AU / CLAP plugin, then Android and iOS apps** (owner, 19 Oct: for making an album
   in Live while Thunder Plus is still being tuned; to start once Thunder Plus is in good shape). The plan, with
   the parity approach (one C++ engine compiled to wasm for the app and natively for the plugin, held in step by a
   render-comparison test in CI), the inventory of what to port, milestones, stores and licensing:
   [`docs/ENGINE-PLUGIN-PLAN.md`](docs/ENGINE-PLUGIN-PLAN.md).

9. **More tracks than 32 (up to 128): planned, not started** (owner, 20 Oct: likely wanted later; the GUI and the
   mono / poly split need deciding first; few old songs to worry about). TRACKS is 8–32 now (24 with a voice per track,
   a kit-voice limit that stays). What has to change, checked 20 Oct:
   - **Block mutes** and **song files**: done 20 Oct (9) (`setBlockMute` / `smuteX`, `songOut` / `songX`), tested with a
     `NT = 128` debug copy. Older versions and Thunder Perform's deck open such songs with tracks 1–32 and the master.
   - **…chains' track limit** and **sidechain by track**: done 20 Oct (10) (`CHAN_COUNT = 128`, pickers follow the
     app's NT; `chnTapList` / `tapmap`).
   - **Screens**: Arrange with 129 columns needs more zoom / sideways scroll (side panel especially) and may need to draw
     only the visible cells (~33,000 at 256 rows); 3-digit track numbers in labels. Medium.
   - **Thunder Perform** (`matthettich/thunder-perform-`) runs this app as its decks (submodule on the `deck` branch):
     merge `main` into `deck` and bump its pin afterwards; its README has the same note.

10. **Automation, Renoise-style: later, after the app is stable** (owner, 21 Oct: likes how Renoise does it, but
    there's a lot to work on, so stability comes first). Renoise draws an envelope per parameter per pattern in an
    automation lane under the pattern, with points, curves and lines, and parameters can also be recorded live. Thunder
    Plus today has tracker commands (CCA–CCD, FXO, VOL, PAN, PIT…) and, from 21 Oct, Interpolate for ramps. Not started;
    decide the scope with the owner first (which parameters, per phrase or per song, how it shows on the phone).

11. **Idle dogs settle after a while (battery): later** (owner, 27 Oct: "we will look later"). With the song stopped, the
    deck's dancing dogs breathe and wag forever (`.dogs.idle .bob` / `.tail`, `dkBreath` 3.4 s and `dkWag` 2.6 s, both
    `infinite`). Running CSS animations keep the Fold's screen redrawing at its full rate whenever Thunder Plus is open
    and stopped, the last idle cost left after the 27 Oct battery work. Idea: after about a minute without a touch, a key
    or a sound, drop the `idle` class (or pause the animations) so the dogs sit still, and bring it back on the next
    touch, key or sound. Check with the owner how long before they settle, and whether they should settle at all.

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
  3. Add a `PALETTE` entry and help text in `PAGE_ABOUT` (top level since 21 Oct; shared modules use `SHARED_ABOUT` or
     their `about`), and a one-line example in `MODULE_USE` (the shared block, then `python3 tools/sync-shared.py`).
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

## Track focus, live song edits, Ctrl/⌘+N and Ctrl/⌘+T (24 Oct)

- **FOCUS TRACK** (`focusT()`, beside `dkTrack()`): one answer to "which track am I on" for the bottom panel (INST,
  …CHAINS, FX, TRACK) and the FX target in `draw()`. Arrange (or the song strip) holding the keys → its cursor's track;
  Mixer → its cursor (MASTER row = master); FX → its track; else the open track. `MT` means the master: the panel then
  shows FX · MASTER and a MASTER strip (S.master level, master-out meter; pan, mute, solo, INST and …CHAINS hidden).
  Fixed with it: Arrange's master click left the panel on the phrase's track; the Mixer's master row showed track 01;
  Arrange beside Mixer/FX only moved them on blocks with a phrase (now empty blocks and the master too); clicking a
  screen tab while Arrange had the keys bounced the next key back to Phrase (go() now drops the focus); ▲▼ on the
  Song/Arrange master column jumped to the last track.
- **LIVE SONG EDITS** (in `makeSeq`, `wake()`): with the song playing live, a track that had no block when PLAY started
  joins as soon as its cell on the lead track's song row gets a block, lined up with that row's start (`T.r0`). Renders
  and BUS PLAY are unchanged. Notes in blocks already playing were always read live.
- **Ctrl/⌘+N** new song (`newSong()`, shared with the File menu and Project's › NEW SONG). **Ctrl/⌘+T**
  (`newPhraseRight()`): a new empty phrase on the nearest empty non-bus track to the right on the current song row,
  opened there. Both are browser-reserved in a tab; they work in the desktop app.
- **SKIN THEME** (24 Oct): the …pump windows (`themeFor()` / `skinTheme()` in pump/ui/kit.js) and the FX screen's
  dynamics viewer (`fxDynPal()`) take their colours from the tracker skin's `--ts-*` / `--tk-*` variables. The windows'
  DARK key still picks pump's own palettes on this device; Ctrl/⌘-click DARK goes back to the skin. `applySkin()` calls
  `PumpFX.skinChanged()` so open windows repaint. Also: Ctrl/⌘+\ solos (same as Ctrl/⌘+/); a plain arrow key drops a
  selection.

## Battery: idle audio engine and display loops (27 Oct)

Owner's phone (the Fold) was running out of battery quickly. Measured headless (whole browser, idle, tracker screen at
373×830; numbers vary run to run): before the first sound 6–7.5% CPU and 120 rAF callbacks a second; after one pad tap the
AudioContext kept running for good, 12–21% CPU and 180 rAF/s, in the background and with the screen off too. Changes, all
marked `BATTERY (27 Oct)`:
- **Hidden + idle suspends the engine** (`bgArm()` / `bgIdle()`, after `audioInterrupted()`): hidden, song stopped, not
  rendering, nothing held and nothing heard for 20 s → `ctx.suspend()`. Visible again resumes at once; any sound resumes
  it through `audio()` (which now calls `bgArm(); hmWake();`). Not while following MIDI clock (`midi.clkIn`) or with a
  MIDI input connected, so the MPC can start a song or play notes with the screen off without a late first beat.
  On screen it is never suspended, so a pad tap stays instant. Measured with it: ~3% CPU.
- **Header meter** (`hmFrame` → `hmDraw`): silent, it looks 6 times a second on a timer instead of every display frame;
  `hmWake()` brings it back at once. It now reads levels even when the meter isn't shown (phone width), because
  `frameIdle()` uses them.
- **Tracker loop** (`frameIdle()`): no longer kept at full rate just because the deck is open (on a phone it always is);
  stays at full rate while the master has level (`HM.lv`), so keys and MIDI notes still animate the deck.
- **Synth meter** (`synMeterFrame`): bug, it never stopped after the first sound (in silence `a >= smPk`, 0 >= 0,
  restamped the peak time every frame). Now ends when level and peak are gone; `smOnT` gives a new note 1.5 s.
- Result: idle 6 rAF/s instead of 120–180; before the first sound ~4% CPU instead of 6–7.5%. Tests: `tests/*.test.js`
  all pass. Not yet tried on the Fold: check a pad tap after coming back to the app sounds at once, and that the
  battery use in Android's settings drops.
- Left for later: the idle dogs' endless breathe/wag animation (open item 11).
