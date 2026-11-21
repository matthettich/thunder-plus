# verify-276: independent review of PR #276 (U37 P1: pads-and-chop snapshot with deferred free)

Reviewed: branch `n1/u37-p1-pads`, head e67ea4fd, base `native` 63f82d0. Reviewer did not write the code. Nothing on that branch was changed.

**VERDICT: MERGE AFTER FIXES.** I found no interleaving that frees a buffer under a live or booked voice (item 1: the free rule is sound as written, under three preconditions that are only partly documented). Nothing is blocking. Two should-fix items before merge: the central safety property (scan BEFORE adopt) is not protected by any test that can fail deterministically (F1; I made the unsafe change and 165 runs of the suite passed), and the variation path, which carries its own epoch, has no test at all (F2). The rest are should-fix / note items for P2.

Method: read all 13 changed files; built Release `test_live_pads_epoch` only (`-j1`, own build dir, deleted) and ran it (2072 checks, 0 failed, 4 runs); applied 8 source mutations of `pads_snap.cpp` one at a time, rebuilt that one object, ran the test, restored the file (`git status` clean on the PR worktree afterwards). No sanitizer build was run.

---

## Findings

### F1 (should-fix): the order "scan, then adopt" is the whole safety argument and no test fails if it is swapped
`pads_snap.cpp:187-193` (`beginQuantum`). Mutation M1: move `slot_->adopt()` above the scan and the board stores. This is genuinely unsafe. Sequence: Q-1: a note starts on B after Q-1's scan (board says NONE for B's slot). Control publishes S2 (without B). Q: `adopt()` pushes S1 to the ring (`snap.h` adopt, `out_.push(cur_)`), and BEFORE the scan/board stores run, control pops S1 in `collect()`, B's refs go to 0, `board_[slot]` still reads NONE (unchanged since Q-1's top), `epoch < NONE`, B is freed while the voice reads it.
Result: `live_pads_epoch` passed 165 of 165 runs with M1 (15 + 150). E1-E4 are single-threaded (control never runs inside a quantum), so they cannot see it. E5 does not hit it: 1000 replacements over about 14.9k quanta (about 15 quanta per replacement, about 20 us per quantum), the window is the scan loop (about 1 us), and a voice must have started on B in the previous quantum after the scan.
Suggested fix: a deterministic seam. For example a `#ifdef THUNDER_TEST` hook (or a `std::function`-free function pointer set by tests) called inside `beginQuantum` between each stage, so a test can run `publish()+collect()` at exactly: (a) after the board stores and before `adopt()`, (b) after `adopt()`, and assert nothing is freed in either place while a voice started in the previous quantum is alive. Alternatively a compile-time `static_assert`-free structural guard is not possible; the hook is the way. Also make E5 pace the audio thread (sleep to 128 frames / 48 kHz scale) and loop the control thread on a shorter cycle, but treat that as a supplement.

### F2 (should-fix): the variation path (`PadBuf::vars`, `RenderVar::buf.epoch/slot`) has no test and is the easiest one to break silently
`pads_snap.cpp:152-157` set `rv.buf.epoch = b->epoch; rv.buf.slot = p` for each variation. `Players::note` (`players.cpp:70-72`) hands `v->buf` to `VoicePool::note`, which records `v.ep = c.epoch; v.eslot = c.slot` (`voice.cpp:172`). No test calls `setPad` with variations (every call passes `nullptr, 0`), no test plays a note with `SH_VK`.
Mutation M2: `rv.buf.epoch = 0; rv.buf.slot = -1;` (variation voices become "untracked"): suite passes (0 failing checks). Sequence it would produce: a variation voice sounds on B's `varMem`, the pad is replaced, B retires, the scan ignores the voice (`eslot < 0`), `collect()` frees `varMem` under it: use after free.
Mutation M3: the identical-data check treating changed variations as identical (`same = true`): suite passes. A host re-sending a pad with new variations would silently keep the old ones.
Suggested fix: an E7 with a pad that has two variations, a MUT note (`SH_VK`) playing one, the pad replaced, assert retired-not-freed while it sounds and freed after; and a setPad with same audio, different variation, asserts a new epoch. Same for a stereo pad (see F9).

### F3 (should-fix): `Players` with `pads_ == nullptr` dereferences null on the first note; the test's comment says otherwise
`players.cpp:31` `const RenderPads& pads_ = *this->pads_;` then `players.cpp:40` `pads_.route[pad]` (reached for any pad 0..255 after the `track`/`ii` checks). `beginQuantum` leaves `Players::pads_` untouched until the first snapshot is adopted (`pads_snap.cpp:190`: `if (changed) pl.setPads(...)`). The tests build `Players(nullptr, ...)` and the Rig comment says "no pads until the first snapshot is adopted (every note is then 'no pad')" (`test_live_pads_epoch.cpp`, Rig constructor): that is false, it is a null dereference (reading by inspection; I did not run it, no test sends a note before the first adopt).
Sequence: P2 LiveHost starts, the Seq is playing (or a MIDI note-on arrives), no `publish()` has happened yet; first quantum: `s == nullptr`, `changed == false`, `dispatch` calls `Players::note`: crash on the audio thread.
Suggested fix: `PadsHost::create()` publishes an empty snapshot that the first `beginQuantum` adopts (so `current()` is never null after the first quantum), and/or `Players::note` returns early on `!pads_`. Add a test that sends a note before any publish.

### F4 (should-fix, P2 contract): "seq_adopt, then beginQuantum, then steps" is documentation only; `beginQuantum(pl, nullptr)` leaves a dangling chop pointer
No code outside tests calls `beginQuantum` (grep), so nothing enforces the order; the contract lives in `pads_snap.h` (the comment above `beginQuantum`), `seq.h:173-177` and the design draft 3.5 step 2.
Sequences:
(a) A Seq made by `SeqSession::make` / a successor copies `session.opts()` (`seq.cpp:382`, `o_(session.opts())`; `takeOver` does not copy `o_`, `seq.cpp:419-440`). If the session was created with `opts.chop` pointing at an old snapshot's table (the obvious thing for a P2 author who builds the first session from the first snapshot) and the new Seq steps once before `setChop`, `applyChop` (`seq.cpp:556`) reads a freed table. The notes say "create the session with `opts.chop = nullptr`"; that is a convention.
(b) `beginQuantum(pl, nullptr)` (the signature allows it, "may be null") in a quantum that adopts S2 while a Seq exists: S1 goes back to the ring, `seq` keeps `o_.chop = &S1->pads.chop`, control deletes S1, the Seq steps later: use after free (`seq.cpp:556-569` reads `ch->has/loop/hits`). `if (s && seq)` at `pads_snap.cpp:192` hides it.
Suggested fix: make the null `seq` case an error in debug (assert) and in P2 hand the host the Seq only through one function that does `setChop` itself; add a test that adopts a Seq made through a session (successor) and checks its first step uses the current table (mutation M4 "setChop only when `changed`": 0 failing checks, so nothing tests the every-quantum store that the header relies on for a newly adopted Seq).

### F5 (should-fix or note, ruled): min-based retention is unbounded in the number of buffers while one voice lives
`pads_snap.cpp:179-181` frees `epoch < board_[slot]`. A voice on epoch e0 of slot s keeps EVERY later-retired buffer of slot s. Concrete: a looping or held voice on pad A (a long drone; `SfPool` pads with `Max len` do not stop it, `sf_live.cpp:157`) for 5 minutes while the UI re-edits pad A once a second (a sample editor, a MUT/GEN variation tweak: see F10, every variation change is a new full copy) keeps 300 retired copies; a 4 s stereo 48 kHz pad is 1.5 MB: 450 MB. Ruled by the lead (D5, "min-based") and listed by the builder (open question 2), so not blocking; it needs a cap or a visible counter (`retired()` exists) and a plan.
Suggested fix: publish per slot a small sorted set of live epochs instead of the minimum (at most 96 voices: a 96-entry array + count per slot is too large for 512 slots, so use one global array of (slot, epoch) pairs, sorted, of at most `voices` entries, published with a seqlock), and free exactly. Alternative: stop a voice (fast release) when `retired()` passes a byte budget.

### F6 (should-fix, test quality): E5's poison check is defeated by malloc reuse; only a sanitizer can catch a use after free
`test_live_pads_epoch.cpp` e5: the freed buffer is poisoned with 1000.0 in the free hook, then `delete[]`d; the control thread immediately allocates the next 4800-float pad (`set(0, ..., LEN)`), the same size class, so the allocator hands the same chunk back and the voice reads valid-looking 0.1..0.3 data: `bad` stays 0. With M1-style bugs the check can pass. With "free always" (builder's mutation 1) it fires because the free precedes the reallocation. The ctest registration (`tests/CMakeLists.txt` hunk) does not require a sanitized build.
Suggested fix: do not return the memory to the allocator in the hook in tests (keep poisoned buffers on a list until the test ends), so the voice always reads poison; keep the ASan/TSan runs as a second line.

### F7 (note): exceptions in a file built with exceptions, rest of the core without
`pads_snap.cpp` is added to `set_source_files_properties(... -fexceptions)` (`core/CMakeLists.txt:10`). The two throwing operations are `s->pads = stage_` (vector copy of `chop` hits, `var`) and `reserve` (`pads_snap.cpp:131-137`); after `reserve(nv)` no `push_back` reallocates. The implicit `RenderPads` copy assignment and `std::vector<RenderVar>` members are inline/COMDAT functions also instantiated in no-exceptions objects; the linker keeps one copy. If the kept copy came from a `-fno-exceptions` TU, an exception thrown through it crosses frames with no cleanup pads: on x86-64/aarch64 unwind tables are still emitted by default so `catch (...)` at `pads_snap.cpp:142` is reached, but the partially built `RenderPads` members in those frames are not destroyed (a leak, not a crash). Not reproducible here (would need a failing allocator); the wasm build (`__cpp_exceptions` off, `PS_TRY if (true)` / `else`) compiles to an abort on exhaustion, as the builder says. I did not find an error path that leaks a PadBuf: `setPad` frees `mem/vmem/vv` when `newBuf` fails (`pads_snap.cpp:98`), `publish` failure paths `delete s` whose destructor releases exactly the references taken.
Suggested fix: nothing now; P6 decides. Add one sentence to the header that a `bad_alloc` in `publish()` may leak the partial copy.

### F8 (note): `board_` shares cache lines with control-written fields and with `mins_`
`pads_snap.h:127-129`: `board_` follows `made_/freed_/hook_` (control-written) and precedes `mins_/last_` (audio-written every quantum, `mins_` is reset every quantum). Without `alignas(64)`, board_[0..] shares a line with `freed_/hookUser_` and board_[~511] with `mins_[0]`. Cost: a few cache-line transfers per `collect()`; harmless today. Suggested: `alignas(64)` on `board_` and on `mins_`.

### F9 (note): untested and unbounded details
- No `static_assert(kEpochSlots == 2 * kRenderPads)`; `newBuf(kRenderPads + pad)` relies on it (`pads_snap.cpp:123`).
- No stereo pad in any test (`setPad(..., r != nullptr)`, `R()`, `bytes()`, the `memcmp` of R, `d.data2`).
- `setPad` allows `frames` up to 2^29 per channel (`pads_snap.cpp:64`): a stereo pad of 4 GiB, and `nVars` is unbounded with an O(nVars^2) duplicate check on the control thread.
- `setPad(frames == 0)`, `clearSf`, `setKit`, `setSfParams`: no test.
- Epoch is `uint64_t` from 1: wraps never. Slot bounds: pad is range-checked in every setter; `epochMins` re-checks `eslot` against `kEpochSlots` in both pools.

### F10 (note, item 8): the reported departures
- Per-slot board instead of one atomic: correct and required by D5 (E3 kills the all-slot version). Fine.
- Over-retention within a slot: see F5.
- A variation change copies the pad audio: every MUT/GEN edit of a pad that has 4 s of audio allocates and `memcmp`s the whole pad on the control thread and doubles memory while a voice sounds on the old one; with F5 it multiplies. A live host that regenerates variations while playing will feel it. Suggested split: `VarBuf` refcounted separately (the builder's option).
- `setPad` clears the chop data only when the pad changes (`pads_snap.cpp:110`); an identical `setPad` keeps it (`pads_snap.cpp:94-95`). The C API (`render_api.cpp:192`) clears it on every `set_pad`. So the web (which sends the pad then the loop/hits) and the native path differ if a host re-sends the same pad and not its chop. Also: if the host publishes between `setPad` and `chop().setLoop`, the new pad plays once unchopped (`seqChopSkipped`). Not a real problem if the host stages then publishes once; say so in the header. Mutation M5 (do not clear the chop on change): suite passes (nothing tests it).

---

## Item answers

### Item 1: the free rule (answer: sound; the window in the question cannot occur)
Case in the task: a note starts on B in quantum Q after the scan; control publishes S2 without B (builder's reference released) and calls `collect()` before Q+1's scan. Nothing is freed because B is not even retired: S1 (which holds a reference to B) is still `cur_` on the audio side. It leaves `cur_` only inside `adopt()` of Q+1, which runs AFTER Q+1's scan and board stores (`pads_snap.cpp:187-190`, `snap.h` adopt: `out_.push(cur_)`). Only then can `collect()` pop S1 (`Spsc::pop` acquire, `spsc.h:22-26`), drop B's last reference (`pads_snap.cpp:52-54`), and read `board_` (acquire, `pads_snap.cpp:180`). The voice is alive at Q+1's scan (min stored, release, `pads_snap.cpp:189`, before the ring push, release, `spsc.h:19`), or it ended in Q: its reads precede the push in program order. Argument in `pads_snap.h` is complete provided the three preconditions hold, and I would add them to it: (1) `beginQuantum` runs on the audio thread before any note of the quantum is dispatched from the snapshot's pads (a note dispatched BEFORE `beginQuantum` is also fine: it is in the alive list at the scan); (2) `Players::pads_` is only ever set by `beginQuantum` (one `Players`, no other `setPads`); (3) a voice only starts on the buffers named by the snapshot `Players` currently points at. Everything below holds under them.
- Snapshot adopted then replaced twice in one control burst: `adopt()` (`snap.h:36-40`) pops S2 and S3 in one call, pushes S1 then S2 back (the loop pushes `cur_` each time), S2 never started a voice; B in S1 only: S1 popped, B retired, scan was earlier: fine. If the ring has no room, `adopt` stops (`out_.space() > 0`): S1 stays current, S2/S3 stay pending: fine.
- `collect()` between the board store and the adopt: S1 is not in the return ring yet, B refs > 0: nothing freed.
- Return ring full: `adopt` refuses to adopt (`snap.h:36`); nothing is dropped, `publish` returns `kFull` when `in_` is full, caller retries after `collect()`. If the audio thread is stopped, publishes stop at 16 pending; each `setPad` still allocates (unbounded if a UI keeps calling `setPad` without `publish`: a buffer made and replaced before any snapshot names it is retired at once with no voice, freed by the next `collect()`: fine).
- Booked voice (not yet sounding): it is in `alive_` from `note()` (`voice.cpp:159`, `sf_live.cpp:156`) and `ep/eslot` are written in the same call: scan sees it (E2 kills the "sounding only" mutation).
- Release tail, killed, `stopAll`: `kill()` leaves the voice in `alive_` until it ends; `aliveErase` removes it only when it no longer reads (`voice.cpp:86`, `sf_live.cpp:51`). OK.
- Voice stolen: pools drop the NEW note when full (`dropped_++`), no steal-in-place. OK.
- `last_` dedup: a store happens whenever the slot's value changes, a missing store means the board already holds the current value; any voice that ended in between had its reads ordered before the later ring push. OK.
- Memory orders walked: board store release; ring push release; ring pop acquire; board load acquire. TSan would be the check; the builder reports one clean run; I did not run it.
Residual risk is only F1 (nothing checks the order) and F4 (nothing enforces the preconditions).

### Item 2: other holders of pad data
Checked and OK: `Players` caches only `pads_` (`players.h:70`) and a by-value `VoiceBuf` copy during one `note()` call; `VoicePool::note` and `SfPool::note` copy `kit.syn/rel/choke` by value; `takeAll`/`take` read `kit` during the call. SoundFont: zones hold `L/R` pointers into `PresetPcm` (`sf_live.cpp:123-132`), kept alive by `PadBuf::pcm` and `font` (both `shared_ptr`); `font`/`pr` are read only in `note()`/`covers()` (`sf_live.cpp:91-106`), not in render. Live pool: `LivePadConfig` has no sample pointer (`live_pool.h:30-34`), nodes own their parameters, `Tap` is a test callback; so `epochMins` excluding it is right. Chop: `ch->hits()` pointer is used inside `applyChop` only (`seq.cpp:569-580`). MIDI path and meters read no pad data. Variations: covered by the `PadBuf` (but untested: F2).

### Item 3: `Seq::setChop`
The claim holds: `o_.chop` is read only at `seq.cpp:556` (`applyChop`, called only from `Seq::step`, `seq.cpp:914`); the constructor chain (`seq.cpp:380-414`, `chaseChance` included) and `takeOver` never touch it; `SeqSession::publish` copies `opts_` (it names no chop). Events already stepped ahead (`lead`) are cut with the old table (documented). `setChop(nullptr)`: safe, every chopped note counts `chopSkipped_` and stays unchopped (`seq.cpp:557`). Order enforcement: documentation only: F4.

### Item 4: audio-thread rules
`beginQuantum`, `Players::epochMins`, `VoicePool::epochMins`, `SfPool::epochMins`: no allocation, lock, free or throw; loops bounded: 512 stores (reset) + at most 96 voice reads + 512 compares against `last_` + one release store per changed slot, plus one pointer store for the chop. `rt_harness` `run_pads_snap` runs it under the allocation guard. False sharing: F8. Cost is microseconds per 2.7 ms quantum.

### Item 5: control side
Single control thread is documented, not enforced (`refs` are plain `uint32_t`, `retired_` plain pointer; two threads calling `publish`/`collect`/`setPad` would corrupt them). Epoch wrap: not reachable. Slot bounds: OK. Leaks: none found on error paths (F7).

### Item 6: offline renderer bit-identity
Proved by inspection: the new fields (`VoiceBuf::epoch/slot`, `SfPad::epoch/slot`, `Voice::ep/eslot`) default to 0 / -1 and are WRITTEN in two places per note (`voice.cpp:172`, `sf_live.cpp:154`, plain stores, no branch) and READ only in `epochMins`, which `SongRenderer`/the C API never call. `render.cpp:12` still passes by-reference pads (`withChop`); no `setChop`, no `PadsHost`. No per-sample path changed. No positional aggregate initialisation or `sizeof`/`memcmp` on the changed structs exists (grep of `core` and `tests`). Did not re-run the 213-test ctest (builder reports 212 pass, 1 skipped as before).

### Item 7: the tests
- E1 kills: free regardless of board; `<=`; epoch never growing; the dedup-never-increasing store; "setPad identical makes a new buffer". E2 kills: scan ignoring booked voices. E3 kills: min over all slots. E4 kills: scan loop limited to 256 slots; SoundFont voices not in `epochMins` (it also kills `sp.slot = p` but only because it asserts the field value `slot == kRenderPads`, not by behaviour: no audio pad 0 exists in E4, so a real collision is not exercised). E5 kills only "free regardless" (builder: 161233 poisoned samples) and, under ASan/TSan, real races. E6 kills: no `setChop`; stale table.
- Mutations NO test kills (run: M1-M5; reasoned: the rest):
  1. M1 adopt before scan/board stores (ran: 165/165 pass).
  2. M2 variation voices untracked (`rv.buf.epoch = 0`) (ran: pass).
  3. M3 variations ignored in the identical-data test (ran: pass).
  4. M4 `setChop` only when a snapshot changed, i.e. a newly adopted Seq never gets the table (ran: pass).
  5. M5 `setPad` does not clear the chop on change (ran: pass).
  6. Weaken the board store/load or the ring push to relaxed (x86 hides it; only TSan, not part of ctest).
  7. Stereo: `mem` allocated with `frames` instead of `2*frames`, or `R()` wrong (no stereo test; ASan only).
  8. `setPad(frames == 0)` not releasing the buffer; `clearSf` not releasing (no test).
  9. `epochMins` without the `eslot >= 0` guard (untracked offline voices write `mins[-1]`; no test scans an untracked voice).
  10. Dropping `delete s` on the `kFull` path of `publish` (leaks a snapshot and its references; E5's no-leak check would only catch it if `kFull` happens, which the single-core timing makes rare).
- E5 and the window of item 1: the window that matters is "between the ring push in `adopt()` and the board stores" in a mutated order (F1). Real code has no such window, so E5 cannot hit one; for the mutated order it is too coarse (about 15 quanta per replacement, about 1 us window, 165 runs: zero failures). E5 also uses voices on pad 0 of 4800 frames only, so a pad longer than the retire cycle (the E1 case) is not under threads.
- Checks that cannot fail: E5 poison check under reuse (F6); `CHECK(notes.load() > 100)` only checks the generator; the test's `CHECK(rc == kOk)` loop is guarded by `tries++ < 100000` so a stuck ring would show as a failure, fine. E6 `differ >= 2` can fail (good).

### Item 8: see F5, F10.

---

## Checked and found OK
Free rule (all listed interleavings, item 1); lifetime of SoundFont data (shared_ptr in `PadBuf`, freed on control); `PadsSnap` destructor on both the `kFull` and `bad_alloc` paths (references taken equals references released; builder's reference keeps refs above 0, so nothing retires); teardown order in `~PadsHost` (`slot_` deleted first, then `cur_`, then the retired list); `std::sort` comparator on `RenderVar`; per-pad (not per-snapshot) freeing (E3); shared pointer identity of unchanged pads between snapshots (E1); offline path unchanged (item 6); `static_assert` that the atomic is lock-free; `PS_TRY`/`PS_CATCH_NOMEM` macros compile in both modes.

## Could not verify
- ThreadSanitizer and ASan runs (not built: more than 30 min here); the builder's TSan/ASan claims are unchecked. The memory-order argument above is by reading.
- The 213-test ctest, the wasm and Android/iOS builds (build scripts outside the repo tree I could see; `pads_snap.cpp` is added only in `core/CMakeLists.txt`, I found no other source list in the repo, but a script elsewhere that enumerates core sources would need it).
- F3 was not run (read from `players.cpp:31,40`).
- Behaviour on a weakly ordered CPU (aarch64): by reading only.
