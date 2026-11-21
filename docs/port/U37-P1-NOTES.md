# U37 P1 notes: the pads-and-chop snapshot with deferred free

Branch `n1/u37-p1-pads` (base `native` 63f82d0). Design: `U37-DESIGN-DRAFT.md` 3.4, 9 (row P1), 12 (D5); prewire F2 + F3.

## Status (update at every push)
- DONE (code written): `render/pads_snap.{h,cpp}` (PadBuf, PadsSnap, PadsHost), epoch fields in `VoiceBuf` / `SfPad`, `epochMins` in `VoicePool`, `SfPool`, `Players`,
  `Seq::setChop`, CMake, `tests/test_live_pads_epoch.cpp`.
- NEXT: build + run `live_pads_epoch`; rt_harness section; full ctest Release; ASan+UBSan subset; TSan; three mutations; PR.

## Design as built
- `RenderPads` is the snapshot's payload unchanged in layout (`PadsSnap::pads`). `Players::setPads` and `Seq::setChop` point into it. `SongRenderer` and the C API keep their by-reference `RenderPads`
  (voices there record epoch 0 / slot -1 and are ignored by the scan): the offline path is untouched, so its output cannot change.
- `RenderVar` entries in a snapshot carry no data of their own (`data` empty, `buf` points into the owning `PadBuf`).
- `PadBuf` (control-side refcounted; epoch = a counter that only grows; slot p = pad p's audio and variations, slot 256 + p = pad p's SoundFont font + decoded PCM). Snapshots and the builder's current
  table hold references; refs move on the control thread only; the last release puts the buffer on an intrusive retired list (no allocation).
- A voice stores `(epoch, slot)` of the buffer it started on (`VoicePool::Voice::ep/eslot`, `SfPool::Voice::ep/eslot`), set in `note()`.
- `PadsHost::beginQuantum(Players&, Seq*)` (audio): scan voices (alive list = sounding AND booked) into `mins_[512]`, store changed slots to `board_[]` (release), THEN `slot.adopt()`, `setPads`, `seq->setChop`.
- `PadsHost::collect()` (control): delete returned snapshots, then free each retired buffer with `epoch < board_[slot]` (acquire).

## The free rule (exact)
Freed iff `refs == 0 && epoch < minLive[slot]`; `minLive[slot] = 2^64-1` when no voice of the slot is alive. See the header comment for the safety argument (scan before adopt before ring push).
- No active voice: every retired buffer of the slot goes (`e < max`).
- Voice on the buffer itself: its epoch equals `e`, so `e < min` fails. Voice on a newer buffer of the slot: the older retired one goes. Voice on an OLDER buffer: it keeps newer retired ones of the SAME slot
  (minimum cannot tell): bounded over-retention, never unsafe. It does not hold other slots (per-slot board). See open question 2.
- Retired and re-published with the same data: `setPad` with identical data is a no-op (same object, same epoch). A replaced pad with identical content is a new object with a larger epoch (the old one is
  freed by the rule when its voices end). Nothing revives a retired buffer (a retired buffer has refs 0, and only the builder's current table can add a reference).

## What `Seq::setChop` touches
`o_.chop` only (a pointer store). The Seq derives nothing from the table at construction or in `takeOver`: `applyChop` (seq.cpp:556) reads `o_.chop` afresh per chopped note; the constructor and
`takeOver` never dereference it (checked: the only reads are seq.cpp:556 and the opts copy). Not redone: events already stepped (up to `lead` ahead) were cut with the old table. A Seq made by
`SeqSession::make` starts with the session opts' chop pointer: create the session with `opts.chop = nullptr` and call `beginQuantum` (which calls `setChop` every quantum) after `seq_adopt` and before the first `pump`.

## Audio-thread code (for the line-by-line read)
- `render/pads_snap.cpp` `PadsHost::beginQuantum` (the last function).
- `voice/voice.h` `VoicePool::epochMins`, the `v.ep = ...` assignment in `voice.cpp` `note()`.
- `sf/sf_live.h` `SfPool::epochMins`, the `v.ep = ...` assignment in `sf_live.cpp` `note()`.
- `render/players.h` `Players::epochMins`; `seq/seq.h` `Seq::setChop`.

## Open questions
1. Under `-fno-exceptions` (the wasm build) a failed `std::vector` allocation in `PadsHost::publish` (the `RenderPads` copy: chop table vector, `var`) aborts; `pads_snap.cpp` is built WITH exceptions
   in the native build like `render.cpp` and catches `bad_alloc` there. Option A: keep (wasm P6 decides on exceptions for this file). Option B: give `SeqChop` / `RenderPads::var` fixed storage (bigger diff).
2. Over-retention within a slot (above): a 5-minute sample on pad A keeps every buffer of pad A replaced during those 5 minutes. Option A: accept (ruled min-based). Option B: publish the SET of live
   epochs per slot (small array + seqlock) and free exact; costs a larger scan.
3. `setPad` copies the pad again when only its variations change (PadBuf is immutable). Option: split variations into their own refcounted object.

## Mutations
(to be filled in)
