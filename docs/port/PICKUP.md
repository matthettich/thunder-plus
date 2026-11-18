# Pick up here (written 17 Nov 2026, 22:00 Mountain, by Fable, the lead, at the owner's request to move to a new chat)

To continue the native port in a new chat, the owner says: **"pull thunder-plus, branch `native`, and continue from docs/port/PICKUP.md"**.
This file is the brief. Background only when needed: `docs/port/TODAY.md` (the full log of 16 and 17 Nov, newest at the end), `docs/port/WORKPLAN.md` (the
plan; sections 1 to 4 and 9), `HANDOVER.md` on `beta` (the web app, the owner's punch list).

## How the owner wants this run (his words, 16 and 17 Nov)
- The lead manages, decides, reviews and merges; **everything that can be is built by low-cost agents** (Sonnet builds a unit in its own `git worktree` under
  `/home/claude/wt/`, Haiku does document chores); an INDEPENDENT Sonnet verifier checks a unit before or right after merge (it has caught a real crash twice).
- Questions: make the best decision and tell him; weigh solutions that port easily. He wants to keep making music on the web app meanwhile: fixes go to
  `beta` by PR, then `main` by PR after each (he said so); a desktop release only when he asks.
- Only what is pushed exists: the session's server restarts every hour or so and stops the workers (they can be resumed with a message to their id; a new chat
  has none of them). Tell every worker to push work in progress every 30 minutes with its `Uxx-NOTES.md` current.
- Machine: 2 cores, about 7 GB free disk. At most two compiling workers; delete build directories of merged work. One git network operation at a time per
  worker; `gh` through REST only (GraphQL is blocked); no CI runs on a push to `native`, only on PRs.

## Where the port stands: 31 of 37 units merged; about half of the whole port by the lead's estimate
- **Stage N1 (playback core):** every worker-buildable unit is merged, pump effect hosting (U18) and Marbles / Tides included. **Left: steps 10 and 11, the
  lead's own: U34 (offline render API and pad renders through the core), U35 (MIDI scheduling), U37 (bridge and host switch: `core.wasm` behind the Settings
  switch).** Before wiring, the lead still has to read line by line: U07 (sequencer), U08 (voices), U09 (mixer), `fxpump.cpp`, `fxhost.cpp`.
- **Stage N3 (engines), merged:** U22 to U29 (foundation, all layer types, Plaits / Elements, Bass / Vox, Acid / Machine / Blip, both sets of rendered-only
  engines), U30 (…waves engine), U33 (pump synths, rendered path), U04 (LXR as a separable GPL module).
- **Not started:** **U31 …chains engine** (next for a Sonnet worker: it reuses U30's `Graph` and `waves` runner; brief it like U30 was, see the U30 entry in
  TODAY.md, and it needs the lead's review of its live part); **U32 SoundFont player** (blocked: the owner must decide the Vorbis decoder licence question in
  WORKPLAN section 12); **U36 Link / recording / inputs** (lead reviews); **Stage N2 (the Android shell)** and **N4 (Windows and Mac shells)**.
- **Fastest route to something the owner can install:** N1 steps 10 and 11, then N2. U31 can run in parallel with a worker.

## Rules the lead set for gates (they hold for every remaining unit)
- Ported JS: bit-exact against the web app's goldens and a Node reference; committed Linux hashes gate every platform. Never loosen a gate, never hand-edit a
  golden, no `-ffast-math`.
- A unit whose reference is a fast-math wasm (pump, Marbles / Tides, Plaits / Elements, pump synths, LXR, library modules inside …waves): the native library is
  built STRICT; each case within -100 dB of the reference's peak, exact cases listed and kept exact; anything else left out BY NAME with its numbers and a
  structural check (finite, RMS within 1 dB, same length); no native hashes where the system libm is involved (pump). Class rules, each written in its test:
  Plaits raw int16 output within 1 step; **LXR within -60 dB (weakest parity in the port)**.
- Live paths: sample-exact events, live output equals the rendered note bit for bit at every host block size; a fixed small latency instead of block-quantised
  timing (31 samples bass, 15 other live cores, 127 …waves); no allocation or locks after preparation.
- Memory safety: every unit fuzzed with several extreme fields at once under ASan + UBSan; a read outside a table in third-party code is patched narrowly in
  `prepare.sh` / `prepare-pump.sh` for every host build (never a sanitizer-only workaround) and reported.

## Conditions recorded for the wiring (N1 step 10 / U34), from the reviews
- U26 live shell: its latency goes into the PDC plan; a NoteOff must never be dropped when the 256-event queue overflows.
- U30: the Expression module must be compiled at patch preparation, not on the audio thread; Marbles' re-make at arm must not allocate there.
- U25: a thread that renders Plaits / Elements must call `pe_prepare()` off the audio thread first (see "Conditions before this is wired" in U25-NOTES.md).
- U33: pump synths are not yet wired into `xeng_render_voice`; LXR host glue and live lanes are not wired.
- Each unit's `Uxx-NOTES.md` has a "Choices for the lead" or "Conditions" section: read them before wiring that unit.

## Waiting on the owner
- **His ears:** Elements (33 cases differ from the first block), LXR (-60 dB class), Plaits' 45 chaotic cases, and the N1 exit listen (three songs with the
  native core switched on in the desktop web copy, once U37 exists).
- **The pump repo: CONFIRMED by the owner at 21:36 on 17 Nov ("pump is committed"). Checked: pump HEAD is e43ee10** (PR #6 `fix/synth-table-reads`, merged
  17 Nov 18:51): WOB sub phase wrap (5468d8c), `BandWaves::read` (d4c4987), GLOSS `DigiWaves::read` (8d851d0), FREEZE spectra made at creation, web build
  (24a24e1), `SynthBase::run` insertion sort (458b88d), the oversampler's halfband FIR summed with four named accumulators in a fixed order (8d95efc).
  **TO DO FIRST IN THE NEW CHAT (a Sonnet worker each side, the lead reviews):** re-pin from deb07d3 to e43ee10. Things to know before starting:
  (1) between the old pin and e43ee10 there is also e7c55dc, **a new pump plugin, FLEX** (Thunder Plus's FLX / FLY as a plugin): a 26th effect type in
  `pump.wasm` and its meta, so the web's lists and the native `kPumpTypes` / `pumptable.inc` change, and **the owner decided (17 Nov 22:17): "FLX / FLY can stay hidden"**: the type is in the wasm and the tables, but the web app's effect picker and lists do not offer it;
  (2) the oversampler change alters TUBE, LIMIT, HEAT, STRIP and anything oversampled by ULPs ON PURPOSE (it removes the fast-math reassociation): the web's
  pump goldens for those move (regenerate only those rows, list them), and natively the TUBE / `x_two_hosts_same` rule and several "within -100 dB" pump
  cases should become bit-exact: re-measure, tighten the exact list, and drop rules that are no longer needed; (3) drop the native patches the fixes
  replace (`prepare-pump.sh`: the three table reads and the insertion sort) and confirm the gates still pass without them; (4) web: rebuild `pump/pump.wasm`,
  `pump/synth/pump-synth.wasm` and both metas from e43ee10 as a new web build on `beta`, prove the unchanged types are bit-identical as build 638 did, and
  re-check the WOB crash case (22050 Hz, note 127, Octave +2, Tune +12) no longer traps; (5) `PINS.md` and `native`'s copies of the web files follow.
- (Settled 17 Nov: the …chains editor keeps accepting any cable; see below.)
- U32's licence decision.

## Web app, state at 22:00 on 17 Nov (no web branch is open; no worker is running)
`main` = `beta` = build 642; desktop v1.3.12 is build 639 (the owner has not asked for another). Today's web builds: 639 (Chains / Waves editors start
again: a raw U+2028 / U+2029 in a regex had stopped both editor scripts since about build 620; the owner's song is the first-open default), 640 (a module
dragged to the edge stays under the pointer; Undo of a long drag is one step), 641 (299 pop-up messages reworded from the audit sheet
https://claude.ai/artifact/WSbgsqqBk5oU6QbpWTKMPy; 53 not applied, listed in HANDOVER; the owner reviews from the sheet later), 642 (one playhead rule in
Pattern view: a bar per track on its own row, the active track included, playing and stopped; the FX bar's ...CHAINS section collapses to its tab; FIT rows
and A- / A+ for the pattern grid: on the owner's screen 32 rows fit at 10 px rows, about 7.7 px text; the floor is 9 px rows). His tests for all of these are in
"Owner's punch list" in HANDOVER.md.

## Decided by the owner on 17 Nov
- The ...chains editor keeps accepting any cable into any input ("leave the cables as is, as long as they do something"): no port-kind rule.
- Effect slots stay at 8 for now; he uses bus tracks for more. **Future feature: 12 slots** (scoped: FXO's first digit is the slot 1 to 8 and 9 means "all",
  so slots 9 to 12 need a new command; FXS reaches 15; older builds drop slots above 8 on load; new songs' mastering slots would move; the native core has 8
  as a fixed size in about 15 places; 16 breaks both digit fields).
- "The dual sound bug can wait" (both instruments sound when a note's instrument number changes during playback).

## Future features he asked for (web app; the lead's, audio path)
- **FIRST: bus into bus.** He wants a bus's output, with its FX, to feed another bus through ...chains ("the reverb I put on the first bus doesn't play when
  things were on the second bus"). Today a bus is "a track with a Chain Out and no Track In of its own" (`chainsTracks`, `chainsFeed`, `chainOutTargetsOf`
  near `index.html` 20935; the bus plan in `core/seq-core.js`), so a track that both receives (Chain Out) and sends on (Track In) is not a bus and its source
  shows MISS. Needs: tracks processed in dependency order, cycles refused with a clear message, the latency plan following the chain, the Mixer's CHN row
  saying what is happening, and the native core's bus plan kept in step.
- 12 effect slots (above).

## The combined `native` head was built and tested after the last two merges
dd879896 (#217 + #214): Release GCC build clean, full ctest 143 of 144 passed with 1 intended skip, the closed build (no LXR, no DSP library, no pump) 3 of 3.
