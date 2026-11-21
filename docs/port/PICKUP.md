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

## Where the port stands: 35 of 37 units merged (20 Nov); about 65% of the whole port by the lead's estimate (the table below is of 19 Nov: see START HERE)
- **Stage N1 (playback core):** every worker-buildable unit is merged, pump effect hosting (U18) and Marbles / Tides included. **Left: steps 10 and 11, the
  lead's own: U34 (offline render API and pad renders through the core), U35 (MIDI scheduling), U37 (bridge and host switch: `core.wasm` behind the Settings
  switch).** Before wiring, the lead still has to read line by line: U07 (sequencer), U08 (voices), U09 (mixer), `fxpump.cpp`, `fxhost.cpp`.
- **Stage N3 (engines), merged:** U22 to U29 (foundation, all layer types, Plaits / Elements, Bass / Vox, Acid / Machine / Blip, both sets of rendered-only
  engines), U30 (…waves engine), U33 (pump synths, rendered path), U04 (LXR as a separable GPL module).
- **Not started:** **U31 …chains engine** (next for a Sonnet worker: it reuses U30's `Graph` and `waves` runner; brief it like U30 was, see the U30 entry in
  TODAY.md, and it needs the lead's review of its live part); **U32 SoundFont player** (UNBLOCKED 17 Nov 23:13: the owner chose **stb_vorbis** (public domain / MIT), pinned and fuzzed like
  every other upstream; start it when a compiling slot is free); **U36 Link / recording / inputs** (lead reviews); **Stage N2 (the Android shell)** and **N4 (Windows and Mac shells)**.
- **Fastest route to something the owner can install:** N1 steps 10 and 11, then N2. U31 can run in parallel with a worker.

## START HERE (20 Nov, 21:00): the state for a new chat. Everything below this section is history, newest first.
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (20 Nov, 21:00)."

**TWO CHATS ARE RUNNING. Read this first.** The OLD chat (the lead of 19-20 Nov) stays open ONLY to finish the live-engine route: PR #265 (`n1/live-route`: bass / vox / acid / mach as live engines; verifier's verdict MERGE AFTER FIXES, report `docs/port/reviews/verify-265.md` on `review/verify-265`; its builder is applying F1-F6) and its follow-on `n1/live-eng` (ENGINE TARGETS + the idle skip). **The new chat must NOT touch `n1/live-route`, `n1/live-eng`, `review/verify-265`, or merge / close #265.** When #265 and the eng PR are merged the old chat writes one line here ("live route step 1 merged, date") and stops. If that line is missing and those branches have not moved for a day, ask the owner whether the old chat is dead before taking them over (the design and every ruling are in `docs/port/LIVE-ROUTE-DESIGN.md`, the builder's status at the top of `docs/port/LIVE-ROUTE-NOTES.md`, both on `n1/live-route`).

**21 Nov, 00:55: PR #265 (the live-engine route, step 1: bass / vox / acid / mach) is MERGED into `native`** after the verifier's re-check (MERGE; `docs/port/reviews/verify-265.md` on `review/verify-265`) and green CI on every job. `n1/live-route` is finished. **`n1/live-eng` (ENGINE TARGETS + the idle skip) is STILL with the old chat**: do not touch it until a line here says it is merged or handed over. Follow-ups from #265's verifier: `native/tools/live-route-dump.mjs` emulates the page's "hold" by hand (not the app's own code: the class of flaw that produced the false B11 in U35: replace it with the app's code or prove it equivalent; the old chat's eng PR is to do it); the two `*_oom` tests and `live_u26` were not run under the sanitizers locally (CI's sanitizers job runs the whole suite); a vox pad on 33 tracks costs about 235 KB a track and renders silence at about 3x real time until the idle skip lands (required before U37). The "Android shell v0" CI job can fail on a Maven Central HTTP 429: re-run it, it is not the code.

**Usage limits and timers (the owner's instruction, 20 Nov):** a 5-hour usage limit stops the lead AND every worker; twice on 19-20 Nov a check-in that fired DURING a limit stayed queued until the owner next wrote (7 hours lost each time), and a chained check-in (each schedules the next) breaks at the first one that cannot run. So: when a worker fails with "You've hit your 5-hour limit · resets HH:MM", or whenever work is left running unattended, schedule ONE check-in for a few minutes AFTER the stated reset time (not a chain during the limit), and tell the owner the reset time. Workers stopped by a limit or by a server restart resume with SendMessage to their agent id; ids die with the chat. Server restarts happen about hourly: only what is pushed exists. The machine has 2 cores: thirteen workers at once gave load 25 and everything crawled; keep to about four, at most two compiling. Workers must never `pkill` (two did, killing others' test runs).

### Where things stand
**Native port: 35 of 37 units merged, about 65% of the whole port** (the lead's estimate; units left: U36 and U37; plus the live route's steps 2 and 3, U04's LXR seam, the stretch port, and the shells' work behind them).
- Merged on 20 Nov into `native`: #247 follow-ups (the sanitizers CI job now has 120 minutes, takes about 42), #246 stems (U34b), #260 `Players` (dispatch and the pools behind one class; holds neither Song nor pads by reference), #264 U35 MIDI scheduling (`MidiSched`, `thunder_midi_*`; the web's MIDI bugs B1-B9 and B12 are FIXED natively, each difference from the web ledgers an exact rule in `native/tests/golden/midi-ledger-expect.json`; B11 was a harness artefact, not a web bug).
- Queued native follow-ups (one small PR, Sonnet): #246's verifier F1 (the U34a header says the length estimate is always below the real length: false; an unknown-length render can also end up to 0.3 s early), F2 / F3 (no audio test of a stem under solo; no cross-track choke test in a stem), F4 (the stem list's bus order vs the JS Map order: matters for bus into bus); #264's verifier N1 (a live tempo change just before a TPO row leaves the clock about 15 ms off) and its four test gaps (two TPO rows, `block_sec` forwarding, `block_sec` > 1 rejected, Start's priority in the ring); `Players`' take / capPoly order is guarded only by `render_sf` (the live route's tests are to pin it).
- The lead's pre-wiring read is `docs/port/reviews/prewire-19nov.md`: before U37 (the live host) F2 + F3 need ONE design (an immutable pads-and-chop snapshot with deferred free), pump must not run on two threads at once, and the live route needs its idle skip (a vox pad on 33 tracks renders silence at about 3x real time).
- The native side has NOT yet followed these web changes of 19-20 Nov (each has a spec written for a native worker): bus into bus (`docs/BUS-INTO-BUS-NATIVE.md` on `beta`: `bus_plan`, transitive solo, `setStem`), SYNC TO LINES (`insts[i].lines`: `docs/SAMPLER-NOTES.md`), envelope times in lines (the host converts to ms before the render: `docs/ENV-LINES-NOTES.md`), the stretch modes (`I.sx`, OLDSCHOOL: `docs/STRETCH-BUILD-NOTES.md`; the stretch port itself does not exist), reset track's `stp` message. Re-record the sequencer goldens against the current `beta` when doing these.
- Next units by the workplan: U36 (the shells' MIDI: port table, sending `MidiSched`'s records; clock in is phase 2) and the core glue that calls `MidiSched` each block; then the full wasm target and U37's design (the lead's own).

**Web app: `beta` is at build 660** (head 1ae8d41, CI green). Merged 19-20 Nov, all by PR, each punch-listed in HANDOVER.md: #248 Expression examples, #252 beta icon, #249 drag on the Settings sliders, #253 FOLLOW order, #250 FX preset keys (mod + Left / Right; the value nudge moved to mod + Shift), #256 master double-click, #255 in-use outline, #251 reset track, #254 bus into bus (+ solo follows the chain, CHN labels MS / LP, and the fix: the mix is re-applied when the bus plan changes), #257 sampler pitch + SYNC TO LINES, #258 playback-report diagnostics, #259 envelope times in bars + lines, #262 the FlexCore golden, #261 the stretch mode picker (659), #263 OLDSCHOOL stretch (660). **Desktop betas published: v1.3.14-beta.1 (build 658) and v1.3.14-beta.2 (build 660). NOT merged to `main`: the owner has not asked.** A desktop beta is: `desktop.yml` from `beta` through the API with `inputs[version]=1.3.14-beta.N`, `inputs[prerelease]=true` (it replaces the stable app: same appId; stable installs ignore pre-releases).
- Almost none of this has been run by a person. Never run in a real page at all: the FX preset keys, the envelope rows, a long swell on both sequencers. The owner's tests are "Owner's punch list" in HANDOVER.md ("give me my test checklist" makes the doc).
- The stretch plan (`docs/STRETCH-DESIGN.md` on `beta-stretch-design`; "Time stretch: what is left" in HANDOVER.md): steps 1 and 2 of 10 are merged. Next: step 3 (stretching in a Worker + a byte-capped cache), step 4 (chop and stretch on the audio-thread sequencer: today `applyChop` is a stub in `core/seq-core.js`, so slices and synced samples play unchopped there: the owner's open "slices" bug), then CLEAN (Signalsmith Stretch, MIT: a listening spike on the owner's loops FIRST), SLICED, the character modes, live OLDSCHOOL, the native port.
- Queued small web jobs: convert the five other history-based tests that silently skip in CI (`core-memory`, `block-size`, `eng-targets`, `eng-targets-pump`, `idle-shortcut`) to committed references (waiting for the owner's yes); the stems double-count (a send's direct sound is in its own stem and in the bus's: on `beta` before these PRs; write-up at the end of `docs/BUS-INTO-BUS-NATIVE.md`) and "a pure source whose only bus is muted still gets a stem".

### Decisions made for the owner on 19-20 Nov (he was told each; he can reverse any)
Enter on an empty master cell in an empty row fills the row (as double-click). Replacing the sample in an EXISTING Sample layer keeps that layer's tuning (a toast says how to fix the pitch); only a new layer gets the corrected tune. Envelope and SYNC lengths in lines follow the song's LPB. A reset never changes a track's bus role (a bus or a feeder keeps CHN on). A loop of …chains cables is accepted and the closing cable is left out (label LP). A middle bus is heard only through the next bus. OLDSCHOOL is rendered first; live in the voice later. The tempo settle (300 ms) applies to OLDSCHOOL only. Natively: live engines run ahead by their latency (no PDC entry); a note-off always wins on a full queue; the MIDI clock is realigned after a TPO command; late notes for gear-only instruments are sent, not dropped. New web FEATURES are merged into `beta` only with his yes ("merge everything" covered the 19 Nov batch only).

### Waiting on the owner
Two or three of his loops (a break, something tonal, a full mix) for the CLEAN stretch listening test. Rubber Band yes / no (GPL: only as a separable module; the lead recommends no). Whether to convert the five skipped tests. Whether he wants a side-by-side beta app (own name, appId and settings folder). Anything he finds in beta.2.

### Suspected web bugs told to the owner, NOT confirmed by ear and NOT fixed in the web app
"Audio thread" sequencer: slices / sync do not chop (the stub above); CUT / RES ignored on nine live engines (COIL, FOLD, TRACT, STRIKE, BOING, FREEZE, LIFE, X0X, …waves); a WOB command on an empty row ignored; a slide cut by the next note stops gliding. MIDI out (all fixed natively only): vol 00 leaves the previous mono note ringing; clock jitter and a permanent 42-50 ms offset after a tempo command; Start not Gear-delayed; two tracks on one port + channel cut each other; stale CC / channel tables; a tick burst after a stall; late pad notes dropped but their MIDI sent; a stray note-off on release; on a browser port without `clear()`, a tick and a note-off go out after Stop; no RPN bend range / bank select is ever sent. The quiet-mix report of 19 Nov: the likely cause (gains not re-applied after a bus-plan change with a solo set) is fixed in build 651+; the owner's test is on the punch list.

## START HERE of 19 Nov, 17:00 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (19 Nov, 17:00)."

**UPDATE 20 Nov, 08:55. Only what is pushed exists.**
WEB: all twelve PRs of 19 Nov are MERGED into `beta` (build 658, head 1dfbf7ad, CI green): #248-#259 (Expression examples, beta icon, drag sliders, FOLLOW order, FX preset keys, master double-click, in-use outline, reset track, bus into bus + the applyMix-after-plan-change fix, sampler pitch + SYNC TO LINES, playback-report diagnostics, envelope times in bars + lines). **Desktop beta 1.3.14-beta.1 was started ONCE** (run 44766538470 from `beta`, prerelease): do not start it again; check the release page. Building now on `beta`: stretch steps 1 and 2 (`beta-stretch-picker` 659, `beta-stretch-oldschool` 660; design `docs/STRETCH-DESIGN.md` on `beta-stretch-design`), and `beta-flex-golden` (the FlexCore test's reference is stale since 647 and skipped in CI). Not merged to `main`: the owner has not asked.
NATIVE: #247 follow-ups MERGED (the sanitizers CI job now has 120 minutes; it takes about 42). #246 stems: verdict MERGE (`review/verify-246`), branch updated onto native, merge when CI is green; its three follow-ups (F1 the length-estimate comment is wrong, F2 / F3 two missing audio tests) go into the next follow-ups PR. #260 `n1/players` (PR A): verifier report on `review/verify-260`; merge on a MERGE verdict + green CI. Live routes PR B on `n1/live-route` in stages B1-B6 (B1 done: reset / seek; status at the top of `docs/port/LIVE-ROUTE-NOTES.md`). U35 `n1/u35-spec`: MidiSched building. After bus into bus on the web, the native mixer needs `docs/BUS-INTO-BUS-NATIVE.md` (on `beta`) applied; after SYNC TO LINES and envelope lines, the native host must read `insts[i].lines` and the envelope unit fields (`docs/SAMPLER-NOTES.md`, `docs/ENV-LINES-NOTES.md` on `beta`).

**UPDATE 19 Nov, 22:45 (the owner went to bed; "merge everything", then "push a new desktop beta"). Only what is pushed exists.**
NATIVE (branch `native`):
- #246 `n1/u34b` stems: open, CI green except sanitizers (was running); verifier report expected on `review/verify-246`. Merge when both are clean.
- #247 `n1/followups-19nov`: open (beats 0 -> 4 fixed); verifier report expected on `review/verify-247`. Merge when clean.
- Live routes: design + rulings L1-L10, C1-C11, P1-P6 in `docs/port/LIVE-ROUTE-DESIGN.md` on `n1/live-route`; worker building the decision-log golden, PR A `n1/players`, PR B on `n1/live-route`.
- U35 MIDI: `n1/u35-spec` (notes, `tools/midi-ledger.mjs`, 28 ledgers, rulings: the web's MIDI bugs B1-B9 are FIXED natively, each with a listed expected difference); worker building `MidiSched`.
- The lead's pre-wiring read: `docs/port/reviews/prewire-19nov.md` (F1-F3 must be fixed before U37).
WEB (branch `beta`; each PR holds a build number; every merge makes the next need a rebase of BUILD / sw.js / changelog / HANDOVER; merge one at a time when CI is green on the rebased head):
- MERGED: #248 Expression examples (648).
- Ready after rebase: #252 beta icon (`beta-icon-swap`), #249 drag sliders (649), #253 FOLLOW order (656).
- #250 FX preset keys (653): the owner has not answered whether moving the value nudge to mod+Shift+arrows is right; the lead's call if he stays silent: merge as built (his own example needs it) and tell him.
- #251 reset track (650): verifier report expected on `review/verify-251`.
- Building, no PR yet at 22:45: `beta-bus-into-bus` (651; rulings in `docs/BUS-INTO-BUS-DESIGN.md`; CHN labels two letters: MS, LP), `beta-solo-fx-quiet` (652, bug hunt), `beta-inuse-outline` (654), `beta-sampler-pitch-sync` (655; bars + lines), `beta-master-dblclick` (657; a NEW master block), `beta-env-lines` (658; 4 bars must work, 8 if cheap; bars + lines).
- Design only: `beta-stretch-design` (`docs/STRETCH-DESIGN.md`): build steps 1 and 2 after 655 merges. Waiting on the owner: his test loops, Rubber Band yes / no, OLDSCHOOL rendered first (the lead's default: yes).
- DESKTOP BETA when the web PRs are in: run `desktop.yml` from `beta` through the API with `inputs[version]=1.3.14-beta.1`, `inputs[prerelease]=true` (the beta icon is picked by `THUNDER_BETA=1`, set by the workflow for a prerelease). Beta and stable share one appId: the beta replaces the stable app.

**UPDATE 19 Nov, 20:25 (a later chat; if that chat is gone, its workers are gone too: only what is pushed exists):**
- **PR #246 `n1/u34b` (U34b stems) is OPEN, not merged.** The lead read the core diff (sound). An independent verifier was running; its report, if it
  got that far, is `docs/port/reviews/verify-246.md` on `review/verify-246`. No report there: send a new verifier, then merge when it and CI are clean.
- **`n1/followups-19nov` (queue item 1) was being built** (notes: `docs/port/FOLLOWUPS-19NOV-NOTES.md` on that branch). No PR yet at 20:25.
- **The live routes (queue item 2) are designed and ruled:** `docs/port/LIVE-ROUTE-DESIGN.md` and `LIVE-ROUTE-NOTES.md` on `n1/live-route`. A worker was
  building step 1: the decision-log golden, PR A `n1/players` (the `Players` move, no behaviour change), PR B on `n1/live-route` (LivePool: bass, vox,
  acid, mach). Continue from the checklist at the top of LIVE-ROUTE-NOTES.md with a new Sonnet worker briefed from the design's last section.
- Told to the owner as suspected "Audio thread" web bugs (from the code, not by ear): CUT / RES ignored on nine live engines, a WOB command on an
  empty row ignored, a slide cut by the next note stops gliding. The native port follows the page's player for all three.

**Nothing is running and no PR of ours is open** (#225 `n1/repin-pump` stays open, waiting on pump). Weekly usage was at 90% at 12:45.

**Merged into `native` on 19 Nov (each verified by an independent Sonnet verifier; reports on the `review/verify-*` branches):**
- **#243** the sequencer on a live clock: `Seq::setTime / setBpm / shift`, `SeqRunner` (catch-up skip, 3 steps a block, EXACT STOP).
- **#242** the SoundFont route into the offline renderer: `thunder_render_add_font / set_pad_sf`, the poly cap shared by both pools.
- **#244** the Song's lifetime under a running sequencer: `SeqSession`, `SongSnap`, `Seq::takeOver`, `seq_adopt`, `SeqRunner::rebind`.
- **#245** chop: sliced / synced instruments chopped as the page's `applyChop` (`seq/chop.h` `SeqChop`, `thunder_render_set_pad_chop / _hits`).
- Design and rulings: `docs/port/SEQ-LIVE-DESIGN.md`. Notes: `SEQ-LIVE-NOTES.md`, `SEQ-TAKEOVER-NOTES.md`, `SEQ-TAKEOVER-BUILD-NOTES.md`,
  `RENDER-SF-NOTES.md`, `CHOP-NOTES.md` (all in `docs/port/`).

**Estimates (the lead's): 33 of 37 units (89%); about 60% of the whole port; about 72% of the way to an Android install.**

**The queue, in order (nothing started):**
1. **A small follow-ups PR (Sonnet):** a reversed-chop render test (verify-chop F1: `voice.cpp` ~:178 `max(0, dur - ofsSec - durSec)` is untested);
   the PERFORM-part path clamps slc / beats as the main path (verify-chop F4); U31's round-3 follow-ups (F1 to F7, `reviews/verify-227-round3.md`).
2. **The live routes:** the live-engine route and the SoundFont LIVE route into the mixer (the renderer has the SoundFont route; `notes_live`
   notes are still silent and counted). Conditions: PICKUP's "Conditions recorded for the wiring", R11 (the U26 shell must never drop a NoteOff).
3. **U34b stems** (Sonnet).
4. **U36** (Link, recording, inputs), **the full wasm target and U37** (the core behind a Settings switch in the web app), **U35** MIDI
   scheduling, then **N2**, the Android shell.
- **Conditions for U37 (the host), from today:** the host sets the start time on a PLAY's first Seq before publishing it and calls
  `runner.start` after `seq_adopt` on a new session; past `sessionFull()` (8 MB) it stops growing the registries and warns (no restart at a
  pattern edge); `SeqChop` is read-only while a Seq runs, so a pad change during PLAY publishes a new table with the snapshot; the host
  calls `set_pad_chop` (and `set_pad_hits` for SMODE instruments) after every `set_pad`, with `loopSecs` of the pad AS RENDERED.
- **Known and audible until the stretch port exists:** sync modes STRETCH and LOOP play unstretched (counted in `notes_stretched`), and their
  offsets and slice lengths are those of the stretched timeline, so slices land in the wrong place.
- Still open from before: can a saved song's JSON exceed the render API's 16 MiB limit; the 1.5 dB RMS difference at 384 kHz (master chain).

**For the owner:**
- Reproduce the suspected web bug: Settings, sequencer "Audio thread" vs "Page (old)", an instrument with SLICES 4, four notes a semitone
  apart: on Audio thread every note seems to start at the top of the sample. The chop builder adds: the worklet already has the pad data
  (`core/seq-core.js` :912), only `applyChop` is a stub there, so the web fix is to stop stubbing it (a `beta` job when he confirms).

**Lessons of 19 Nov:** every brief says "add files by path, never `git add -A`" (two branches had build directories committed;
`native/build*/` is in `.gitignore` now) and "every pushed commit compiles; a checklist in the notes updated in every commit"; check a PR's
file count before merging; the sanitizers CI job takes about 50 minutes; start the verifier while CI runs; the server restarted at 12:19
and 12:36 and stopped the workers (a stopped agent of the same chat is resumed by its id).

## START HERE of 19 Nov, 12:50 (now history)
**Say to a new chat:** "Pull thunder-plus, branch native, and continue from docs/port/PICKUP.md. Start at START HERE (19 Nov, 12:50)."
(Written because weekly usage reached 90%: the owner may stop here. Nothing new is started until he says so.)

- **Merged today:** #243, the sequencer on a live clock (setters, `SeqRunner`); verified after the merge (`review/verify-243`), no defect.
- **#242, the SoundFont route into the renderer: MERGED at 12:50** (every CI job green on the clean commit 730d787; verified, the five findings
  fixed: leak, budget hole, the poly cap shared by both pools, test gaps, stats). `native/build*/` is now in `.gitignore`.
- **#244, Part B, the Song's lifetime: MERGED at 15:07** (`SeqSession`, `SongSnap`, `Seq::takeOver`, `seq_adopt`, `SeqRunner::rebind`). Verified
  (`review/verify-244`), its findings fixed (the dead sequencer when a PLAY and an edit are adopted in one call; test gaps; the plain `adopt()`
  is not callable on `SnapSlot<SongSnap>`), every CI job green, the lead read `adopt(handover)`, `seq_adopt` and `takeOver`.
  - **The lead's ruling on the session's size (F5b): no restart at a pattern edge.** Every PLAY is a new session already. Past 8 MB
    (`sessionFull()`) the host stops growing the registries: new places get no counter and are counted in `dropped`, and the host warns.
    The wiring step (U37) implements and tests that.
  - **Conditions for the wiring:** the host sets the start time on a PLAY's first Seq before publishing it, and calls `runner.start` after
    `seq_adopt` on a new session (`rebind` keeps hold / lr / ls / ended).
- Estimates at 15:07: 33 of 37 units (89%); about 58% of the whole port; about 70% of the way to an Android install (the lead's estimates:
  of the wiring toward live playback, the live clock, the Song lifetime and the SoundFont render route are done; chop, the live-engine and
  SoundFont live routes, U36, the wasm target + U37 and U35 remain before N2).
- **Next, not started:** the chop unit (ruled: `docs/port/SEQ-LIVE-DESIGN.md`, last-but-one section; Sonnet). Then the old queue from item 4.
- **For the owner:** reproduce the suspected web bug (Settings, sequencer "Audio thread" vs "Page (old)", an instrument with SLICES 4: on Audio
  thread every note seems to start at the top of the sample). Steps were given in the chat of 19 Nov.
- **Lessons of today:** every brief must say "add files by path, never `git add -A`; never commit a build directory" (`native/build*/` goes
  into `.gitignore` with #242); check a PR's file count before merging; the server restarted at 12:19 and 12:36 and stopped the workers
  (a stopped agent of the same chat can be resumed by its id).

## START HERE of 19 Nov, 07:30 (now history): the state for a new chat. The dated sections below are the history, newest first after this one.

**Merged into `native` (33 of 37 units; about 55% of the port, about 65% of the way to an Android install):**
- U31, the ...chains engine (#227), after three verification rounds; U32, the SoundFont player (#226).
- Wiring step 1, the offline song render C API (U34a: #237, its verification fixes #238): `native/core/include/thunder/render.h`.
- Wiring step 3, `dispatch` routing and `noShape` (#239).
- Web: `main` = `beta` = build 647; desktop v1.3.13 = build 647.

**Open pull requests into `native`: none of ours (08:20 on 19 Nov).** Both follow-ups merged with every CI job green: **#240** (render API: variation
replace within the budget, four test gaps, `set_pad` drops a pad's variations, header corrections; CI ran on the tree with U31 merged in) and **#241**
(U32: a work budget for decodes, a stream cache, the granule read first, parse memory bounded). They overlap nowhere (render vs sf), but the head of
`native` with both has had no CI run of its own: the next PR's run covers it.
- (#225 `n1/repin-pump` stays open and unmerged: it waits for pump-side changes, see the 11:30 section of 18 Nov.)
- **Lesson of the night:** a green PR is green against the `native` it was tested with. When `native` has moved, merge `native` into the branch and let
  CI run again before merging (U31's first combined run failed on a test whose premise another PR had changed).

**Started 08:40 on 19 Nov (a new chat; this chat's workers are Sonnet, each in `/home/claude/wt/`):**
- Item 1, Part A (clock setters + `SeqRunner`): **MERGED, #243** (10:50; CI green, the lead read the audio-thread diff). Its verifier runs after the
  merge: report `docs/port/reviews/verify-243.md` on branch `review/verify-243`. Part B (Song lifetime, design in `docs/port/SEQ-LIVE-DESIGN.md`):
  the read-only first step (make-time table, run-state inventory, counter sizes) is `docs/port/SEQ-TAKEOVER-NOTES.md` on branch
  `n1/seq-takeover-notes`; the lead rules on it, then a builder starts.
- Item 2: **PR #242** (`n1/render-sf`), CI green, NOT merged: its verifier's report is `docs/port/reviews/verify-242.md` on `review/verify-242`.
  Before merging: merge `native` into the branch and let CI run again. The lead still rules on its "Choices for the lead" (RENDER-SF-NOTES.md):
  the poly cap across the two pools, PCM shared per preset before Android.
- Item 3 is RULED: the page's sequencer is the specification, sliced / synced notes are chopped (same design file, last section). New unit "chop"
  after Part A. To tell the owner: with Settings on the audio core the web app seems to play sliced / synced instruments unchopped.

**The queue, in order (as of 07:30; see the lines above for what has started):**
1. **Seq setters and the Song lifetime** (WIRING-DOSSIER D1: `time_`, `base_`, `bpm_` setters, RESUME shift, the catch-up skip, EXACT STOP hold; R1).
   The lead's design; gate `seq-golden`. This is the next step on the path to live playback, and it is the top model's work.
2. **The SoundFont route into the renderer** (`thunder_render_set_pad_sf`, an `SfPool` rendered into the mixer), then the live-engine route. Sonnet,
   the lead reviews. Until then such notes are silent and counted (`notes_sf`, `notes_live`).
3. **The lead must rule on chop:** the C++ sequencer follows `core/seq-core.js`, where `applyChop` is empty, while the page's own sequencer chops
   sliced / synced notes (`docs/port/DISPATCH-NOTES.md`, choice 1). Which is the specification?
4. **U34b stems** (Sonnet). 5. **U31 follow-ups PR** (round 3's F1 to F7; round 2's notes-only D6, D8, D10; `docs/port/reviews/verify-227-round3.md`).
6. **U36** (Link, recording, inputs), **the full wasm target and U37** (the core behind a Settings switch in the web app), **U35** MIDI scheduling,
   then **N2**, the Android shell.
- To check before the host relies on it: can a real saved song's JSON exceed the render API's 16 MiB song limit (embedded samples)?
- Noted for the master-chain unit: 1.5 dB lower RMS at 384 kHz than at 48 kHz (verify-238-239).

**How to run it (what worked, 18 to 19 Nov):**
- One Sonnet worker per job, in its own `git worktree` under `/home/claude/wt/`, at most two compiling at once (2 cores). Every brief: push every 20
  minutes, a top line in the notes saying what it is in the middle of, park any problem after 45 minutes, CI green on every job before reporting,
  never merge. A fresh worker with a tight brief beat resuming one with a very long transcript.
- **An independent Sonnet verifier for every unit, before or right after merge.** It found real defects every single time (crashes on NaN, a thread
  race, build breaks on five platforms, leaks, an out-of-bounds read in the reverb). The lead reads audio-thread and API-shape diffs himself.
- **The server restarts about every hour and stops every worker.** An hourly check (a scheduled check-in in the chat) that resumes them is required:
  without one, two workers once sat dead for two hours. A new chat cannot resume this chat's workers: it starts new ones from the branch and notes.
- CI does not run on pushes to `native`, only on PRs; `gh` works through REST only. Gate rules: the section "Rules the lead set for gates" below.
- The owner (22:59 on 18 Nov): keep working without him, keep the documentation current, watch for loops and rabbit holes, be economical with usage
  (weekly usage was at 76%). He switches chats at a clean point; close the old chat afterwards (a chat left open kept pushing to the same branches).

## State at 00:10 on 18 Nov (the lead, same chat; check-ins are set for after each usage reset)
- **Pump re-pin to e43ee10:** web = PR #224 into `beta` (build 643; CI green; verified SOUND WITH CONDITIONS, report `/home/claude/progress/verify-224.md`;
  the condition, a 0.85 s audio-thread stall when a live FREEZE node is made, is fixed in 7582f8b: warm-up while stopped, render plays while playing;
  an independent check of that fix with real playback is running; merge to `beta` then `main` when it is clean). Native = PR #225 into `native`
  (four `prepare-pump.sh` patches dropped; exact list unchanged at 40 of 80: the fast-math wasm still reassociates `dot4`; merge after #224, mirror the
  web files, one full ctest on the combined head). **FLEX is NOT in pump's web build: 25 types, nothing to hide** (the brief above was wrong).
  For the owner's pump agent: `#pragma clang fp reassociate(off)` in the oversampler's `dot4`; FREEZE spectra built lazily or in chunks.
- **Running (Sonnet):** U31 on `n3/u31`, U32 on `n3/u32` (stb_vorbis). Notes: `docs/port/U31-NOTES.md`, `U32-NOTES.md` on their branches.
- **Wiring:** `docs/port/WIRING-DOSSIER.md` (a Sonnet read of seq / voice / mix / fxhost / fxpump / core.cpp; the lead checked its top findings against
  the source): Seq has no time / bpm setters; Seq holds `const Song&` across a song swap; the U26 live shell drops NoteOff on a full queue
  (`live_shell.h` enqueue); `SongRenderer::dispatch` plays sampler pads only; `core.cpp` is still the N0 stand-in; no MIDI scheduler. Its part 5 is the
  step order: U34a offline song render C API, U34b stems, then dispatch routing + Seq setters + Song lifetime (the lead reviews).
- `docs/port/IF-SOLD.md`: what would change if the app were sold (the owner asked).

## State at 11:30 on 18 Nov (the lead; supersedes the 00:10 pump paragraph above)
- **The owner wants percentage-complete estimates whenever he asks for progress.** At this point: about 50 % of the whole port, 31 of 37 units merged
  (84 %), about 60 % of the way to an Android install (the lead's estimates).
- **The pump re-pin is BACKED OUT of the web app.** Builds 643 (#224) and 644 (#228) reached `beta`, never `main`. pump e43ee10 builds FREEZE's spectra
  at creation (about 0.9 s on the web's audio thread); four verified rounds of web-side warm-up only moved the stall, and 644 lost the first notes when
  PLAY followed a kit load or engine switch within about a second. Build 645 (branch `web/pump-backout`, a Sonnet worker) returns `beta` to 642's pump
  files and removes the warm-up code; verify it is byte-identical to `main` for the wasm files, then merge to `beta` and `main`.
  **PR #225 (native re-pin) is HELD**: web and native re-pin together once pump has the changes in `docs/port/PUMP-REQUESTS.md` (the owner's pump agent).
- **U31 = PR #227, U32 = PR #226**: built; independent verifiers were running (`/home/claude/progress/verify-227.md`, `verify-226.md`; if the session is
  new, rerun a verifier from the PR). U32: Chrome's `decodeAudioData` drops trailing frames of single-page Ogg streams, so short looped SoundFont samples
  stop early in the web app; native decodes correctly by default (the lead's decision, told to the owner) and has a Chrome-parity mode for the gate only.
  The Android host must pass a key list (`prepare_preset(..., keys)`: full-keyboard piano is 191 MB). U31: the lead still owes the line-by-line review of
  its live part and a decision on the 16 library effect rows left out by name.
- **Next:** merge U31 / U32 when verified; then the wiring per `docs/port/WIRING-DOSSIER.md` part 5 (U34a offline render C API first), U36, then N2.

## State at 13:00 on 18 Nov (the START HERE of 18 Nov, now history; supersedes the pump and U31 / U32 lines above)
- **Web:** `main` = `beta` = build 645 (#229, #230): pump files byte-identical to 642, all FREEZE warm-up code removed. The pump re-pin waits on
  `docs/port/PUMP-REQUESTS.md`; PR #225 (native re-pin) stays open and HELD.
- **U31 (PR #227, branch `n3/u31`) and U32 (PR #226, branch `n3/u32`) are each in a second round** after independent verification (both SOUND WITH
  CONDITIONS; full reports in `docs/port/reviews/verify-227.md` and `verify-226.md`). A Sonnet worker was fixing each when this was written; a new chat
  has no worker: start one per branch from the branch's notes and the list below, then merge when the list is done and the full ctest passes in both
  configurations (give `engine_xeng_*` a 3600 s timeout on a loaded machine).
  - **U31 list:** (1) native clamps parameters to their range at load and on `setParam` (`waves/graph.h:65`), the JS chains engine uses raw values
    (`chains-worklet.js:5831`, `:5843`): port the JS, add out-of-range golden cases, check whether waves has the same fault; (2) audio-thread allocation:
    Marbles `arm` -> `make()` (`modfx/mod_mods.cpp:63`) and `space` (CloudSeed `RandomBuffer::Generate`): move to preparation, and put an allocation
    audit over every module type into ctest; (3) no memory budget and `bad_alloc` uncaught (1024 Resample modules abort): budget at preparation;
    (4) live queue: accepted events dropped at `clive.cpp:58`, a dropped Patch event keeps its mailbox slot (`clive.cpp:73`: after 8, every swap is
    refused), `preparePatch` returns true when it cannot deliver: Playing=0 / Bypass release are never dropped, parameter events coalesce;
    (5) pump take-over leaks a handle when four modules fail between services (`cmods2.cpp:234`), and the host must call `service()` at a stated rate;
    (6) the 16 rows beyond -100 dB are ACCEPTED as left out by name, but their recorded dB becomes a bound (3 dB) in `test_chains.cpp`;
    (7) tests for the verifier's six surviving mutations; (8) `mutate-u30.sh` re-targeted at `graph.h`; (9) notes: 17 modules' `show()` not ported,
    the closed build's 5 skips, and malformed cables (native refuses the patch: if the JS plays it, native must load it); (10) ctest timeouts.
    The lead still owes the line-by-line review of the live part (`chains/clive.cpp`, `cengine.cpp`).
  - **U32 list:** (1) stb keeps frames past the last-page granule for samples 234, 235, 704: trim to the granule in `decodeOgg` so the default equals
    libvorbis exactly; (2) clamp SF2 sample sizes to the data present and add a per-preset / per-font memory budget (a 5.6 KB font allocated 1536 MB);
    (3) lower `kMaxWork` in `sf_parse.cpp` (a hostile font spun 8 s); (4) refuse absurd sample rates; (5) committed reference data for the fitted
    resampler and a behavioural test for a loop end beyond the data; (6) parity mode stays off by default; (7) the PINS.md licence quote.
  - **For the owner (told 18 Nov):** in the web app on Chrome, `decodeAudioData` drops trailing frames of one-page Ogg streams, so looped SoundFont
    zones whose loop end lies past the decode lose their loop: 159 samples, 65 of 197 GM presets (square lead stops after 0.06 to 0.08 s). Native
    sustains them by default. A web-side fix (decode SF3 with a wasm Vorbis decoder) is a possible future item: not started, not asked for.
- **Then:** the wiring per `docs/port/WIRING-DOSSIER.md` part 5 (U34a first), U36, N2. Estimates: about 50 % of the port, 31 of 37 units merged.

## State at 12:55 on 18 Nov (the lead, NEW chat; read after the 13:00 section, which was written just before the move)
- New session: `native` cloned at `/home/claude/thunder-plus`, worktrees `/home/claude/wt/u31` and `/home/claude/wt/u32`. A Sonnet worker per branch was
  started at 12:50 from heads ee359e3 (U31) and 89ad779 (U32) with the second-round lists above; each pushes every 30 minutes, fetches before pushing
  and never force-pushes. Neither PR is merged. The lead reads `chains/clive.cpp` and `cengine.cpp` line by line once U31's worker reports.
- Check-ins (this chat): a restart check at 14:05, then after each usage reset: 15:02, 20:07 on 18 Nov; 01:12, 06:17 on 19 Nov (Mountain).

## State at 06:25 on 19 Nov (the lead)
- **U31, the ...chains engine, is MERGED (#227): 33 of 37 units.** Three verification rounds (reports in `docs/port/reviews/verify-227*.md`); the last
  CI run was on the branch with `native` merged in (308d2f1), nine jobs green. The first combined run had failed on one test, `render_api_oom`, whose
  premise (a read allocates) stopped holding because U31 moved the effects' allocations to preparation: the lead changed the test to pass when a read
  allocates nothing. Follow-ups F1 to F7 of round 3 and the notes-only items of round 2 (D6, D8, D10) are still to do in a small PR.
- **Verification of #238 + #239: SOUND WITH CONDITIONS** (`docs/port/reviews/verify-238-239.md` arrives with the follow-up PR): D1 to D8 confirmed
  fixed; one defect V1 (replacing a variation at the 512 MiB limit holds two copies), four test gaps, two header nits. Worker a0c0a0461610425b2 on
  `n1/render-v1` (worktree `/home/claude/wt/renderv1`) is fixing them, plus `set_pad` dropping a pad's variations as the page does, and checking the
  track-takeover rule against the JS. Noted for the master-chain unit: 1.5 dB lower RMS at 384 kHz than at 48 kHz.
- **U32 follow-up:** worker afe6a6893a1478d8e on `n3/u32-d1`; last push c1aadff at 04:20; it was in its full ctest at 06:18, no PR yet.
- **Owner's rule for this morning:** from 07:15 no new unit is started; running work finishes and is pushed; a fresh START HERE goes at the top.

## State at 04:50 on 19 Nov (the lead)
- **Dispatch routing is MERGED (#239, native 9efae76)**, CI green on nine jobs; the lead read the `render.cpp` diff line by line. MUT / GEN notes play
  a variation buffer the HOST supplies (`thunder_render_set_variation`; missing: silent + `notes_var_missing`, never the base pad); live-engine and
  SoundFont pads are silent with a stat through `thunder_render_set_pad_route` (the live shell and `SfPool` are not wired into the renderer yet);
  `noShape` set per event for sliced / synced instruments (D3); other events counted in `events_ignored`. ELECTRO unchanged (six WAVs byte-identical).
  **Choices left for the lead (docs/port/DISPATCH-NOTES.md):** (1) the C++ sequencer follows `seq-core.js`, where `applyChop` is empty, while the
  page's own sequencer chops sliced / synced notes: a real difference from the page, not fixed: decide which is the specification; (2) SoundFont needs
  a font input (`set_pad_sf`) and an `SfPool` rendered into the mixer: the next routing step; (3) `set_pad` does not drop a pad's variations as the
  page does; (4) `findVar` is a linear search (fine offline; index it before live use).
- **Verifier a42bba9f5eb481d31** (worktree `/home/claude/wt/vrender`, report `/home/claude/progress/verify-238-239.md`) is checking #238 and #239
  together (neither had an independent check).
- **U31 (#227, head 444cad9 = the branch with native merged in):** CI on the combined tree was still running at 04:50 (two Android jobs, wasm and
  `test` green; the Linux, macOS and Windows jobs in progress). **Merge it the moment every job is green** (round 3 was SOUND, nothing blocking).
- **U32 follow-up:** worker afe6a6893a1478d8e on `n3/u32-d1` (decode work budget, stream cache, up-front granule check, parse memory bound).
- Queue: Seq setters + Song lifetime (dossier D1, the lead's), SoundFont route into the renderer, U34b stems, the U31 follow-ups PR (F1 to F7), U36,
  full wasm / U37, N2.

## State at 03:50 on 19 Nov (the lead)
- **U31 (#227): round-3 independent verification = SOUND WITH CONDITIONS, nothing blocking** (`docs/port/reviews/verify-227-round3.md`): D1, D2, D3, D4,
  D5, D7, D9 verified fixed on 2515ab0; full ASan + UBSan ctest 165 of 168 passed, 0 failed, 3 intended skips; CI green on nine jobs; 561 chains and 383
  waves NaN cases against Node with no difference; goldens gained only `nan/*` rows. The lead read the pump spare hand-over (`cmods2.cpp`) and accepts it.
  The lead merged the current `native` into the branch (clean, no conflicts) and pushed it at 03:50 so CI runs on the COMBINED tree (U31 + the U34a
  fixes, which changed `-fexceptions` on the DSP libraries and `wmods.cpp`). **Merge #227 as soon as that CI run is all green: then 33 of 37.**
  Follow-ups for a small PR afterwards (F1 to F7 of the report): `mutate-u31.sh:32` no longer applies after the D9 edit; the TSan test catches the
  reverted race in only 1 of 10 runs (longer run or random service gaps); the sweep skips the I/O modules; nothing checks that `mods.wasm` stays the
  web's bytes; plus round 2's notes-only items D6 (quadratic document load), D8 (memory budget is per patch, six can be in flight), D10.
- **Dispatch routing (D2 / D3):** worker a98d8ec030262b0d9 on `n1/dispatch`, resumed after the 02:40 server restart; it had pushed routes, tests and
  notes (47a9c18) and was in its full ctest. Then PR, CI, the lead's line-by-line review.
- Queue: Seq setters + Song lifetime (dossier D1, the lead's), U34b stems, the U31 follow-ups PR, U32's D1 follow-up, U36, full wasm / U37, N2.

## State at 02:05 on 19 Nov (the lead)
- **U34a's verification fixes are MERGED (#238, native 33d136d)**, CI green on all nine jobs. The verifier's final verdict was SOUND WITH CONDITIONS
  (`docs/port/reviews/verify-237.md`, D1 to D8); all eight fixed with a test each, plus `thunder_render_length_estimate` (song end without voice
  tails: ELECTRO 4,921,832 against a real 4,983,424). Things that changed outside the API: the tfx `Fx::operator new` and two `layer_pe.cpp`
  allocations now throw / are checked; `-fexceptions` on the core files that build a handle and on the tfx effect libraries (host builds only; goldens
  and the six ELECTRO WAVs unchanged); CloudSeed's rate is capped at 96 kHz in `fx/fxhost.cpp` and `waves/wmods.cpp` (its buffers are sized for 96
  kHz: above that the reverb runs with 96 kHz timing); song limit 64 -> 16 MiB, vars 16 -> 4 MiB. **To check before the host (U37) relies on it: can a
  real saved song's JSON exceed 16 MiB (embedded samples)?** No song can reach a pump effect slot through the render API, so two handles share nothing.
  Not yet done: an independent look at #238 itself (the lead read its report only).
- **Wiring step 3 started:** Sonnet worker a98d8ec030262b0d9, worktree `/home/claude/wt/dispatch`, branch `n1/dispatch`: dossier D2 (`dispatch` routing:
  MUT / GEN variations, live engines, SoundFont, null kit entries) and D3 (`noShape`). Rule given: never the wrong sound; silence plus a counted stat
  where a piece is missing. Notes: `docs/port/DISPATCH-NOTES.md`. The lead reviews its diff line by line before merge.
- **U31 (#227):** worker a64ac5611cf5e1607 found the cause of each of the six CI failures (a fifth NaN crash site in the vowel filter; a NaN-to-int
  cast that differs between arm64 and x86-64; Unix-only calls in two tests on MSVC; a wasm link error that predates round 2), merged native in
  (2515ab0) and is waiting for CI. After its report: an independent verifier again (round 3, short), then merge.
- Queue after these: Seq setters + Song lifetime (dossier D1, the lead's), U34b stems, U32's D1 follow-up, U36, the full wasm target / U37, N2.

## State at 23:05 on 18 Nov (the lead)
- **U34a is MERGED (#237, native 8e909ad)**, CI green on every platform. Its independent verifier (a0a273a4180a74c30; report so far
  `docs/port/reviews/verify-237.md`) has found defects D1 to D5 and is wrapping up: D1 `max_frames` rounds the output up to a 128-frame quantum; D2
  `drain` does not mark the handle started (set_pad still accepted afterwards); D3 the header's claim about builds without exceptions is false
  (`render_api.cpp` does not compile with `-fno-exceptions`); D4 `set_pad` allocates a temporary copy beyond the stated pad budget; D5 `create` leaks on
  an allocation failure part-way through building the renderer. **Next job when a slot is free:** a Sonnet worker fixes these on a branch from native
  (with the length ESTIMATE for unknown-length renders), then U34b (stems).
- **U31 (#227):** a server restart at about 21:10 stopped both workers for almost two hours (nothing noticed it: hourly checks are now set). Remote
  head e240c43 FAILED CI on six jobs (Linux GCC, sanitizers, clang, macOS, MSVC, wasm); the old worker's later commit 353cf6a was local only. A FRESH
  Sonnet worker (a64ac5611cf5e1607) took over at 23:00 with a tight brief: push first, read the six CI logs, finish D1 / D4 / D5 / D7 / D9 of
  `docs/port/reviews/verify-227-round2.md`, CI green on every job. The old worker (a1971f2533efd1b7b) is retired: do not resume it.
- **Owner, 22:59:** keep working through the night; set check-ins so nothing stalls; keep the documentation current; watch for workers looping or
  down a rabbit hole (each brief now has a 45-minute park rule and a 20-minute push rule); weekly usage was at 76% (he will reset if it runs out);
  "If you come to a stopping point within 1 hour of 8 am, wait for me. If not, keep going." He wants to switch chats at a clean point.
- **Checks set (Mountain, 19 Nov):** 00:05, 01:12, 02:15, 03:15, 04:15, 05:15, 06:17, 07:15 (the 07:15 one applies the 8 am rule).
- Still queued after the above: U32's D1 follow-up (decode work budget), dossier D2 / D3 (`dispatch` routing, `noShape`), Seq setters, U36, N2.

## State at 20:20 on 18 Nov (the lead)
- **U31 (#227, head 9549599) is NOT mergeable yet.** The round-2 independent verifier (still running at 20:20; its log so far is
  `docs/port/reviews/verify-227-round2.md`, the final one replaces it) found: **D1** `setParam(.., "type", NaN)` on `convolve` segfaults on the audio
  thread in chains and waves (the raw-parameter change hands NaN to an index; every defect found is a NaN through setParam, never from JSON);
  **D2** `waves/graph.h:89` `"\x01btrue"` is a greedy hex escape: clang / Apple clang / NDK / wasi-sdk refuse it (5 CI jobs red) and GCC builds a
  wrong key; **D3** `tests/CMakeLists.txt:746` links `Threads::Threads` without `find_package(Threads)` (MSVC configure fails). Confirmed good by the
  verifier's own tests: the bypass fade across cuts (80 of 80 bit-equal), `setParam(track)` cost max 0.37 ms, mutations (33 of 34 killed, 1 equivalent).
  The builder (a1971f2533efd1b7b) was sent D1 to D3 at 20:15 with the lead's ruling: native never crashes on any double and does what the JS does
  for NaN; a NaN sweep over every parameter of every module goes into ctest; CI must be green on every platform before it reports.
- **U34a = PR #237 (head cfea0bf), built, the lead accepted the API shape** (`native/core/include/thunder/render.h`: create / set_pad / set_pad_kit /
  length / read / position / latency / stats_get / drain / free). Merge when CI is green; an independent verifier right after. To add in U34b: a length
  ESTIMATE for unknown-length renders (progress bars), and note that an out-of-memory aborts in a build without exceptions (wasm).
- Still queued: U32's D1 follow-up (work budget for decodes that are discarded), U34b (stems), then D2 / D3 of the dossier, Seq setters, U36, N2.
- Check-ins set: 01:12 and 06:17 on 19 Nov.

## State at 18:10 on 18 Nov (the lead)
- **U32 is MERGED (#226, native 4982eed): 32 of 37 units.** Round-2 independent verification: SOUND WITH CONDITIONS (`docs/port/reviews/verify-226-round2.md`):
  all seven conditions hold; ASan + UBSan full ctest 150 of 151 passed, 1 intended skip; CI green on 9 jobs. **One new defect, D1 (follow-up PR, not yet
  started):** `prepare_preset` does unbounded WORK on samples that decode in full and are then discarded (last granule 0; aliased headers): a 1.7 MB
  hostile font takes 17.7 s on the control thread. Fix: charge every decode to a work budget and remember a failed stream by its offset; also give
  parse memory a budget (a 191 KB font at the `kMaxWork` cap takes 205 MB). Fonts: `/home/claude/v2tmp/fonts/` (lost on a new session: the report
  describes how to make them). Start it with a Sonnet worker when U31's worker frees a compiling slot.
- **U34a started** (Sonnet, worktree `/home/claude/wt/u34a`, branch `n1/u34a`): the offline song render C API as a PULL model (create from song bytes,
  set pad buffers, read blocks, position / length for progress, free to cancel), the lead's decision: no callbacks across the wasm / JNI boundary.
- **U31:** the worker is on the lead's review items A to K (resumed after the 16:35 server restart).

## State at 15:30 on 18 Nov (the lead)
- **Web:** `main` = `beta` = build 647. 646 (#231): the side panel's instrument list above the PLAYING box. 647 (#233): two FlexCore NaN fixes (FLX E8 just
  under a beat; `rd` at buffer index 0) and the FX bar's Flex PATTERN row writing the FLX command under the cursor (the lead fixed a `false ?? x` bug in
  it at review). FLX 76 on the master was measured as working on both sequencers. Open: page-sequencer FLY 0D differs in headless Chrome (cause not
  isolated); `ScrambleCore.rd` has the index pattern fixed in `FlexCore.rd` (the lead's to-do). **Desktop v1.3.13 = build 647**, released at the owner's
  request (run 44405171015, 3 platforms, 11 assets); recorded in HANDOVER (#235, #236).
- **U31:** the lead's line-by-line review of `clive.cpp` / `cengine.cpp` is done; 11 items (A to K) were sent to the worker at 15:05: stats data race,
  single-producer rule, `cur_` fallback race, in-flight count one short during a swap, honest drop counts, Playing flood, queue size, the bypass
  crossfade truncated on a cut block (a click the JS never has), `prepareDerived` cost on the audio thread, `resample_release` on the audio thread, one
  TSan test. The worker records them in U31-NOTES.md under "Lead review, 18 Nov". Merge #227 only after those and both ctest configurations.
- **U32:** the worker was still running its second round (last push 2168e9a, macOS reference fix). Merge #226 when its report is in and checked.
- Workers of this chat: U31 a1971f2533efd1b7b, U32 ad2f95e69f7ff1111 (resume with a message; a new chat starts new ones from the branch notes).
- **Next when a compiling slot is free:** the U34a worker (WIRING-DOSSIER.md part 5).

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
- (Settled 17 Nov 23:13: U32's decoder is stb_vorbis.) He also asked for a note of what would have to change if the app were ever sold
  (unlikely): `docs/port/IF-SOLD.md`.

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
