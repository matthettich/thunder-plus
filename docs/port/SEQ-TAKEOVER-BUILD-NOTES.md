IN THE MIDDLE OF: step 4 (gate). test_seq_takeover passes (24 golden runs, 17 edit goldens x2). Next: rt_harness guard on takeOver, measure takeOver, TSan + ASan runs, mutation checks, full ctest, PR.

# SEQ TAKEOVER BUILD NOTES (Part B, `n1/seq-takeover`)

Spec order: `SEQ-LIVE-DESIGN.md` "Part B: the lead's rulings" (7), then `SEQ-TAKEOVER-NOTES.md`, then the JS.

## 1. The plan (one page)

### Classes, owners, threads
| Class | Made by | Owned by | Touched by |
|---|---|---|---|
| `SeqSession` (`seq/session.h`) | control thread, once per PLAY (`SeqSession::create(mode, opts)`) | `shared_ptr`, held by the host AND by every `SongSnap` of the session | registries: control thread only. Pages / chunks (the counter memory): audio thread only (through a Seq's pointer tables) |
| `SongSnap` { `shared_ptr<SeqSession> sess; shared_ptr<const Song> song; SeqVars vars; unique_ptr<Seq> seq;` } | `SeqSession::make(song, vars)` (control) | the `SnapSlot<SongSnap>` (audio side holds `cur_`, returns it through the out ring, the control thread frees it in `collect()`) | audio thread reads |
| `Seq` | in `make()` | its SongSnap | audio thread (`step`, `takeOver`), the control thread only constructs it |

One lifetime: members are declared session, song, vars, seq, so the Seq dies first; the session outlives every snapshot that holds it
(refcount moves only on the control thread: copy in `make`, drop in `collect` / slot destructor; the audio thread never touches it).
The non-session constructor `Seq(song, vars, mode, opts)` is KEPT: it makes a private one-Seq session (`ownSess_`), so SongRenderer,
`test_seq`, `test_seq_live`, `rt_harness` and the four + four goldens run unchanged; the new tests also build through a real session.

### The three registries (all in `SeqSession`, append-only for the whole PLAY, control thread only)
1. `rv_` PAGES: a table of 256 x 33 `uint32_t*` (song mode; 33 in sect / phrase mode, all allocated at once) to 2 KB pages (16 chain rows x 32
   steps). A page exists for every cell that held a chain at any time of the PLAY and is never freed before the session ends. A Seq keeps its OWN COPY of the
   table (67 KB) taken at its construction (+ pages for the cells that are newly non-empty), so the control thread's later writes to the session's
   table never race with the audio thread's reads. The counters themselves live in the pages, shared by every Seq of the session.
2. NTH / CND / REP rows: `(phrase id, step) -> slot` map, slots never reused; counters in chunks of 256 slots ({nth, gen, c1} x 33 tracks x 256), a Seq
   keeps a copy of the 512-entry chunk table. `rowSlot_` (per stored phrase row, song-derived) now holds the registry slot, so the stride no longer depends on the song.
3. `ids_`: append-only list of target-id strings (cap 4096; past the cap an id is not interned: counted in `SeqSession::idOverflow()`). Every Seq builds
   `ids_` as `registry prefix + new ids` (parse + `own` from ITS song), so indices never change in a PLAY: `Tr::lk[]`, `sent*`, `pno` carry over by index.
Commit: `make()` stages registry growth; `SeqSession::publish(slot, ...)` commits it only when `SnapSlot::publish` returned true, else rolls
back (frees the staged pages, truncates the id list / slot map / chunk count) and deletes the snapshot.
No allocation on the audio thread: it never reads a registry; new pages / chunks are allocated in `make()`, reach the audio thread inside the Seq's tables when the snapshot is adopted.

### `takeOver(const Seq& old)` (audio thread; no allocation, no lock; refuses, counted, if `old` is from another session or has more ids)
Copied member by member (all plain data or preallocated vectors of the successor, sized at its construction):
- the 33 `Tr`; `time_ base_ bt_ btT_ hasBase_ hasBtT_`; `bpm_ tks_ hasGrv_ grv_ hasLpb_ lpb_` (the song's `bpm` is NOT applied: ruling 1)
- `mrand_ seeded_ isSeeded_ hasSseed_ sseed_ hasPseed_ pseed_ mtch_ hasGscl_ gsclScale_ gsclRoot_`
- `hasLoop_ loopY0_ loopY1_`; `pfXf_ pfFill_ pfPart_ perfFirst_`; `pnoHas_ pno_`; `trGen_`; `badSets_ dropped_ maxEvs_ maxArp_`
- `busT_ hearT_ hasHear_ NIs_` (D1, D2: stale until PLAY)
- `pfMac_ / pfMacSet_`: for i < old macro count: effective value (`set ? pfMac_ : macros_[i].v`), set = 1 (D3); beyond: copied as is
- `sentHas_ sentVal_ sentOrd_ sentN_` (index = registry id, same in both) ; `want*` stay the successor's own scratch
NOT copied: every song-derived table (rebuilt by the successor's constructor from the new Song), the arp pool (scratch).
Counters are not copied at all: the pages and chunks are the session's and the successor already points at them (ruling 3: valid from ANY earlier Seq of the session).

### Adoption (audio thread)
`SnapSlot::adopt(handover)` (new overload; the old `adopt()` is unchanged): pops every pending snapshot, retires the ones that never ran at once, and calls
`handover(prev, next)` ONCE with the snapshot that was current before the call (the last one that ran) and the newest, BEFORE `prev` goes back
to the control thread. `seq_adopt(slot, runner)` = that handover is `next.seq->takeOver(*prev.seq)`, then `runner.rebind(newSeq)`.
`SeqRunner::rebind(Seq*)`: re-points the pointer only if the runner is bound; hold / lr / ls / ended / skips kept.

### Successor mode
`Seq(SeqSession&, song, vars, successor=true)`: no start-up (no `on`, no `settle`, no hand-over `at`, no `chaseChance`): `takeOver` writes all 33 `Tr`. Table builds as for the first Seq.

## 2. Choices for the lead
1. **`Song::load(..., bool pageS = false)`** (song.h). Two normSong rules are applied at LOAD only in the page, but `Song::load` re-applied them on every bare S, so a rebuilt Song differed from the JS's in-place edit:
   an empty phrase's length (`plen` of a null / all-empty phrase was reset to `dlen`: found by the edit-plen golden at step 112) and a missing instrument. With `pageS = true` (the host passes it for the page's own S: the `song` /
   `patch` ops and any snapshot made while a sequencer runs) neither is applied; the default `false` keeps every existing caller and `test_song` as they were. **core.cpp's wiring must pass `pageS = true`** for song / patch snapshots.
2. **Ruling 6 (removed instrument), answer to "is it reachable without an edit?": NO.** The page's `normSong` starts from 64 `freshInst` and fills them, so every `S.insts[ii]` exists; a file or raw song gets the same defaults in C++ (`readInst`).
   So there is no existing difference to fix with a golden of its own; the case is `edit-instrument-removed` (the JS golden: `insts[3] = null`, `insts.length = 12` under the playhead plays instrument 0). Implementation: `Song::instMissing[i]` (pageS only) and `instOf` falls back to instrument 0.
3. **`takeOver` copies no counters.** With the session's pages / chunks shared by every Seq of the PLAY (ruling 2, 3), the successor already points at them; a "counter move" does not exist. takeOver's cost is the 33 `Tr` (728 B each) + the sent arrays + a few hundred bytes.
4. **`SeqRunner::rebind(Seq*)` does nothing on a runner that has no sequencer** (stopped, held, never started): an adoption must not bring a stopped sequencer back. Say if you want it to bind regardless.
5. **`SeqSession::publish` deletes the snapshot when the ring is full** (and rolls the registry back); it does not hand it back as `SnapSlot::publish` does: the staged registry growth belongs to that Seq, so a retry builds a new one.
6. **`SnapSlot::adopt(handover)`** (new overload; `adopt()` unchanged) calls `handover(prev, next)` once, with the snapshot that RAN (or, with none running yet, the oldest popped) and the newest, before `prev` is returned. The snapshots in between are retired unrun.
   The plain `adopt()` must not be used for a `SnapSlot<SongSnap>` after the first one: use `seq_adopt(slot, runner)` (session.h).
7. **Registry cap 4096 ids**: past it a new target id is not interned (counted in `SeqSession::idOverflow()`), its scene entry / lock / LFO is inert. No fallback "fresh state".
8. **`lx` (live transpose, D14) is not carried**: a successor is built with the session's opts (the PLAY's `lx`). Out of Part B; wiring would need a `SeqSession::setLx`.
9. **The edit goldens compare events and the clock, not `pos`**: the plain goldens do not carry `pos` either (the notes asked for it; adding it means a new serialisation on both sides).
10. **The TSan run is local** (no CI job builds with `-fsanitize=thread`); result below. ASan test runs `--quick` (a takeOver every 7th step) under the sanitizer job, to fit its time; the Release run does every step.

## 3. Parked
- Nothing parked over 45 minutes.

## 4. Measurements, mutation checks
- **takeOver cost** (Release, this 2-core container, steady_clock, warm): with the registry FULL (4096 ids, the largest `sent*` arrays) and 33 tracks: mean 1.7 us, worst of 300 runs 5.6 us (21.6 us under ASan+UBSan). `seq_adopt` as the audio side runs it (slot pop / push, takeOver, rebind) inside rt_harness's guard: worst 39 us over 300 adoptions (cold first one included). A 128-frame block at 48 kHz is 2667 us.
  Memory per Seq: the page table 67 KB (song mode) + the chunk table 4 KB + `rowSlot_` (phrase rows x 4 B, 0.5 MB at the format's maximum) + the tables of the old Seq as before; ELECTRO's counters are 616 pages = 1.26 MB, as before, shared by every Seq of the PLAY.
- **No allocation**: rt_harness `run_seq_takeover` (ELECTRO live, a publish every 40 blocks, three at once every 200; 300 adoptions): zero allocations inside the guard.
- **Constructor cost of a successor** (control thread; the 24-run identity test makes ~1800 of them per golden in ~14 s on this machine, ELECTRO): about 5 ms each; `buildChase` is the part that scales with the song (D11).
- **Verify-243 F3 tests** (added to test_seq_live): (a) unstepped Seq at now-1 does not skip; (b) 1-row, 1-step-phrase loop with hold armed is never held. Mutations: `hasBase()` dropped from the skip condition: FAILS; `sp < ls_` -> `sp <= ls_`: FAILS. Both restored.
- **Mutation checks, one per gate** (each applied, built, run, then `git checkout`):
  | gate | mutation | result |
  |---|---|---|
  | identity (T0) | takeOver does not copy `btT_` | FAILS (every golden at row 2) |
  | edit goldens, D1 bus plan | takeOver does not copy `busT_` | FAILS (edit-bus, first row 32) |
  | edit goldens, D3 macros | takeOver does not carry the effective macro values | FAILS (edit-perf, row 73) |
  | edit goldens, counters registry | `ensurePage` zeroes an existing page (counters not carried) | FAILS (edit-seed-loop, row 300) |
  | skipped snapshots | `handover(*ret[nr-1], ...)` (from the immediate predecessor, which never ran) | FAILS (3 of the 17 cases, 3 snapshots) |
  | allocation guard | `new std::vector<int>` inside takeOver | FAILS ("operator new inside the audio callback") |
  | ASan | takeOver copies 64 doubles too many into `sentVal_` | FAILS (heap-buffer-overflow) |
  | TSan | the control thread writes the Seq (`setBpm`) right after `slot.publish` succeeded | FAILS (data race reported in `Seq::takeOver`, exit 66) |
  Two first attempts survived and were replaced by better mutations / gates: the `bus` edit changed a track that has no notes (now it moves the bus to a track that has), and the macro edit hit a macro that a MAC command overrides (now `mac[1].v`).
  The first M6 attempt (a fresh page for every successor, leaking) was killed by the OOM killer: a failure, but not a clean one; the zeroing mutation above replaced it.
- **Thread test** (`test_seq_takeover --threads`, ThreadSanitizer build, `-fsanitize=thread`, Release): 1000 publishes (every fifth a vars-only message) while the audio thread runs `seq_adopt` + `step` in a loop: 1000 published, 994 adoptions, 66618 audio steps, no race, exit 0.
- **ASan + UBSan** (`-DTHUNDER_SANITIZE=ON`): `test_seq`, `test_seq_live` PASS; `test_seq_takeover --quick` PASS (24 golden runs, 17 + 17 edit runs, the session cases, 300 random hostile edits, the all-at-once dangling positions); `rt_harness` PASS. UBSan's vptr check is off for the new test (the `-fno-rtti` core makes the shared_ptr control blocks; same as test_modfx / rt_harness / test_hardening).
