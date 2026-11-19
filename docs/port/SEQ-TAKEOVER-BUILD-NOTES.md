IN THE MIDDLE OF: step 1 done (this plan); starting step 2 (registries + SeqSession in native/core/src/seq/session.h|cpp, Seq built through a session).

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
(filled in as they come)

## 3. Parked
(none yet)

## 4. Measurements, mutation checks
- verify-243 F3 tests added to test_seq_live (runner): (a) unstepped Seq at now-1 does not skip on the first pump; (b) one row + 1-step phrase looping with hold armed is never held.
  Mutation (a): `hasBase()` dropped from the skip condition in runner.h -> test_seq_live FAILS ("skipped 1, first t -1"); restored.
  Mutation (b): `sp < ls_` -> `sp <= ls_` -> FAILS ("never held (1 steps played)"); restored. Both runs Release, then reverted with git checkout.
- seq.h: one comment line at `Seq::setTime` (after the first step it moves only time_).
