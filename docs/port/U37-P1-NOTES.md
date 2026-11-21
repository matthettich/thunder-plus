# U37 P1 notes: the pads-and-chop snapshot with deferred free

Branch `n1/u37-p1-pads` (base `native` 63f82d0), PR #276. Design: `U37-DESIGN-DRAFT.md` 3.4, 9 (row P1), 12 (D5); prewire F2 + F3. Round 2 follows the lead's rulings on `reviews/verify-276.md` (F1 to F10).

## Status
- Round 1 (e67ea4f): per-slot MINIMUM board. Round 2: replaced (lead's ruling D5 refined) by a PER-VOICE board and an EXACT free rule.
- Verified after round 2 (numbers in the PR body): full ctest Release; ASan + UBSan on render|players|voice|seq|sf|live|chop|rt_; `live_pads_epoch` under TSan; `rt_harness` section `run_pads_snap`.
- Nothing of P2 onward (no LiveHost, no C API).

## Design as built
- `RenderPads` is the snapshot's payload, layout unchanged (`PadsSnap::pads`). `SongRenderer` and the C API keep their by-reference pads (voices record epoch 0, board entry 0): the offline path is untouched.
- `RenderVar` entries in a snapshot carry no data of their own (`data` empty, `buf` points into the owning `PadBuf`).
- `PadBuf`: control-side refcount (snapshots + the builder's current table); epoch unique, > 0, never reused; the last release puts it on an intrusive retired list. One struct for a pad's audio + variations
  or a pad's SoundFont data (font + decoded PCM by shared_ptr).
- A voice stores only the epoch of the buffer it started on (`VoicePool::Voice::ep`, `SfPool::Voice::ep`; set in `note()`). The slot / `eslot` fields and `kEpochSlots` of round 1 are gone.
- THE BOARD: `board_[kBoardMax = 512]` atomics, `alignas(64)`, apart from the control-written fields and from `scan_` / `last_` (audio-only, own lines). Entry i = the epoch of the buffer voice slot i is on, 0 = none. The
  slots are the sampler pool's capacity then the SoundFont pool's (`Players::epochSlots()`, default 64 + 32 = 96). `create(const Players&)` checks the sum fits (else nullptr) and binds the host to that Players;
  `beginQuantum` refuses another one.
- `beginQuantum(Players&, Seq*)` (audio): stage 0; zero `scan_`, `Players::epochVoices(scan_)` (stage 1); store the changed entries release (stage 2); `slot.adopt()` (stage 3); `setPads` if changed; `seq->setChop`
  every call when a snapshot exists (stage 4).
- `collect()` (control): delete returned snapshots; load the board once (acquire); free each retired buffer with refs 0 and no entry equal to its epoch. Cost: nBoard loads + retired x nBoard compares, no allocation.
- `create()` publishes an EMPTY snapshot (F3): the first quantum adopts it. `Players::note` has a null guard (`pads_ == nullptr`: counted as no-pad). The guard is a branch on a pointer that is never null in any
  existing render (SongRenderer, the C API and every test pass real pads), so no render changes; the full ctest (goldens) passes unchanged.
- TEST SEAM (F1): `setSeam(fn, user)`, a function pointer, null in a product build, called between the stages (0..4). Chosen over a compile-time define because the tests then exercise the binary a product links;
  cost: five loads and never-taken branches per quantum. E8 runs `collect()` at every stage.
- F4: `beginQuantum` re-points the passed Seq every call; `seqGone()` and `orphanAdopts()` plus a debug `assert` flag a snapshot adopted with a null `seq` after a Seq was seen (a host that holds a Seq it does not pass is the host's bug).
- F6: the free hook may take `mem` / `varMem`; the tests keep freed audio poisoned (1000.0) and quarantined until the end, so a late read is audible without a sanitizer.
- F9: limits as the C API (`THUNDER_RENDER_MAX_PAD_FRAMES`, `MAX_VARIATIONS`, `MAX_PAD_BYTES` for a pad plus its variations); static_assert on the atomic size / lock-freedom.

## The free rule (exact) and why it is safe
See the header comment of `render/pads_snap.h` (facts F1 to F4, and one bullet each for: a voice started after the scan, a later board value, a voice slot reused between two scans, a voice that ends,
a torn read of the board, identical data). Short form: a voice only starts on a buffer of the current snapshot (refs > 0); that snapshot goes back only inside `adopt()`, after the scan and the board stores of the same call
(release), and the control side pops it (acquire) before it loads the board; so a voice alive at that scan is on the board, and one that ended had finished reading before the push.

## What `Seq::setChop` touches
`o_.chop` only. The Seq derives nothing from the table at construction or in `takeOver`: `applyChop` (seq.cpp:556) reads `o_.chop` afresh per chopped note; the constructor and `takeOver` never dereference it. Not redone:
events already stepped (up to `lead` ahead) were cut with the old table. Create the session with `opts.chop = nullptr` and pass the runner's Seq to `beginQuantum` every quantum, after `seq_adopt`.

## Audio-thread code (for the line-by-line read)
- `render/pads_snap.cpp` `PadsHost::beginQuantum` (the last function) and the inline `seam()` / `seqGone()` in the header.
- `voice/voice.h` `VoicePool::epochVoices` and the `v.ep = c.epoch;` store in `voice.cpp` `note()`.
- `sf/sf_live.h` `SfPool::epochVoices` and the `v.ep = pad.epoch;` store in `sf_live.cpp` `note()`.
- `render/players.h` `Players::epochSlots` / `epochVoices`; `render/players.cpp` the null guard at the top of `note()`; `seq/seq.h` `Seq::setChop`.

## Open questions
1. Under `-fno-exceptions` (the wasm build) a failed `std::vector` allocation in `PadsHost::publish` aborts; native builds `pads_snap.cpp` with exceptions and catch `bad_alloc` (a failure may leak the partial copy: F7).
2. A variation change copies the pad again (PadBuf is immutable): split variations into their own refcounted object if a host regenerates them while playing.
3. An identical `setPad` is a no-op and keeps the pad's chop data; a changed pad loses it (the C API clears it on every call). Stage, then publish once.

## Mutations (each made on `pads_snap.cpp`, the test seen to fail, then undone)
Round 2 (the current rule):
1. `adopt()` moved above the scan and the board stores (the seam's stage 3 runs first): E8 fails (the buffer is freed at stage 3, the voice then reads 1000.0).
2. A variation's `buf.epoch` set to 0 (untracked): E7 fails (the variation voice's buffer is freed under it).
3. `setChop` only when a snapshot changed: E6(b) fails (a Seq made after the adopt keeps the stale table: 4 events differ).
4. The exact rule replaced by "epoch below the smallest board entry": E3 fails (pad 2's newer retired buffers are held by pad 0's voice).
Round 1 (the minimum board, now gone): ignore the board; global minimum; no `setChop`; `<=`.
