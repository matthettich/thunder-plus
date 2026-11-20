# verify-246: PR #246 "U34b stems through the offline render API" (head 21e1cfb)

Independent verifier. Status: COMPLETE. Verdict: MERGE (with the notes below).

## 1. The JS claims: CONFIRMED (read index.html myself)
- Line refs in U34B-NOTES.md are exact: `audible` 20669, `applyMix(filter)` 22154, `pass` 24225, `pl.applyMix(t => keep.has(t))` 24255, `keepT` 24304, `inBus` 24310, mix pass 24329, stem pass 24351.
- A stem = TRIM, track FX (`setFx` for `keep` only), VOL, pan, PDC, MASTER VOL (`out`), OUT (`mo`). No master effects (`masterFx` false: no `mNode`), no master chain (`useM` false), no limiter (`lim` false): CONFIRMED.
- Every track still plays: `pl.play(e)` runs for all `evs` (MIDI-only instruments excepted); only the gain filter differs: CONFIRMED.
- Mute/solo: inside a pass `applyMix(t => keep.has(t))` REPLACES `audible`; the list uses `audible(t)` (solo > mute > nobody soloed; bus sources / bus of a soloed track stay audible): CONFIRMED.
- Which tracks: `audible(t)` and a note on it; plus a bus with an audible source in keepT; minus a pure source whose bus is in keepT (`inBus`): CONFIRMED. `pass` adds the audible sources of a kept bus (`k2`): CONFIRMED.
- Latency: `lat` = mastering (if useM) + master pump (if masterFx) + PDC T; a stem has PDC T only: CONFIRMED.
- Note (small difference, F5): the JS adds buses by iterating `BP.feeds` (Map order) while keepT grows; C++ iterates ascending track index with `keep` growing. Same result unless a bus feeds a bus in non-ascending order.

## 2. The C++ (read the whole diff)
- ABI: CONFIRMED safe. `opts.size` is the size field; `TR_COPY(stem)` / `TR_COPY(reserved2)` copy only when `opts->size >= offsetof+sizeof`; default is zero (= mix). Old struct ends where `stem` starts, no padding overlap. Stats: `memcpy` of `min(out->size, sizeof s)`. Test S6 runs an older-size struct (size = offsetof(stem)) and gets the mix.
- E_ARG paths: CONFIRMED by reading and S6: stem < 0 or > 32 -> create returns NULL with text (checked -1, 33, 1000, INT_MIN, INT_MAX); reserved2 != 0 -> NULL; `stem_tracks` NULL handle, cap < 0, out != NULL with cap 0 -> E_ARG; cap smaller than count returns count, writes cap entries; out NULL returns count; dead handle E_STATE.
- Stem 32 (track index 31): `1u << 31` is fine, `bit32()` is not involved (stem uses `forcedMask_` in the 32-bit audible mask; the 33rd mixer track (preview bus) is excluded by `t >= kNT` in `want`). CONFIRMED by S6 (stem 32 creates) and S3 (silent render).
- Allocation on the render path: none added (`setStem`, `setHeard` and `stem_tracks` run on the control thread; `stem_tracks` allocates two scratch arrays and a Seq, as `length_estimate` does). CONFIRMED.
- Bus / feeds-a-bus: `setStem` adds `feedMask[track] & audible` to the heard set; matches `k2`. The bus is only a plan today (no chains host in a render), so a bus's stem holds its sources heard directly; S7 tests it with a pass-through hook. CONFIRMED by reading and S7.
- `setHeard` and the MIX when stem == 0: the only change is `audible_ = forced_ ? forcedMask_ : mix_audible(...)` with `forced_` false: identical expression for the mix. Proof by hash: CONFIRMED. A harness (not committed) renders ELECTRO mix (240000 frames, 4 variants: plain / effects / master chain / effects + PDC auto) and FNV-hashes both channels. With the PR core files and with the core files of origin/native (6fd16ea, whose core diff to the PR is exactly this PR's 6 files) the four hashes are identical: 8ed1324beb51868f, 9fbcec926ece1df9, c4076a3b4dd6023b, 5c3e9aa8081d6727.

## 3. Tests
- Release ctest, full: 183 tests, 182 passed, 1 skipped (`pumpsynth_refused`), 0 failed (run in two parts after a restart: tests 1-103 then 104-183; no failure in either log). `render_stems` measured: max |mix - sum of stems| 7.3e-08 to 7.8e-08 (about 1e-7 of the peak), synthetic 4e-08; with master chain 0.72.
- ASan + UBSan (`-DTHUNDER_SANITIZE=ON`): render_stems, render_stems_bus, render_c PASS. render_api, render_api_hostile, render_chop PASS too (6 of 6, exit 0).
- Mutants (each applied by hand, test_render_stems + test_render_stems_bus run, restored with git checkout): 
  - (a) stem pass drops notes of non-heard tracks: KILLED, but only by `S5 the same length` (voices of the other tracks end sooner); the sound and the sum still pass, so a lost choke is NOT caught by any sound check (no cross-track choke in the synthetic song, S1 sum unaffected on ELECTRO). Weak kill.
  - (b) master chain applied to a stem: KILLED (S2, 4 checks).
  - (c) mute/solo honoured in a pass: KILLED (S4 direct-mute check and stats). Solo-only variant (c2): KILLED only by the `heard_mask` stats check; no audio check of a non-soloed stem during a solo. Weak kill.
  - (d) list includes tracks with no notes: KILLED (S1, S3, bus test).
  - (e) off-by-one in the 1-based index: KILLED (S1 sum, S3, more).
  - (f) stem trims the mix's latency (with master chain): KILLED (S2 equality and latency check).
  No mutant survives. Two kills are weak (a, c2).

## 4. Finding 8 (U34a header "always longer"): CONFIRMED, reproduced
`test_render_stems` prints `S5 synthetic: estimate 42000, real 36096`. Cause: `length_estimate` = the sequencer's time when it stops (start of the last row; here 0.875 s). The renderer ends when `songOver() && voices() == 0`, and `songOver` turns true when the sequencer has been stepped to the end, which the renderer does `lead` = 0.25 s AHEAD of the audio clock (`render.h` `lead = 0.25`). With short notes the voices are gone before the audio clock reaches the sequencer end, so the render stops at the last voice end (0.752 s) and the trailing empty rows (and up to `lead` of silence) are not rendered. So the header sentence is wrong (a wrong comment), and ALSO the render's unknown-length end differs from the JS (`N0 = ceil((max(seq.time, last note end) + 0.05) * sr)`: 44400 here vs 36096): the native render can be up to 0.25 s + 0.05 s shorter than the page's. A host that allocates by the estimate: over-allocates here (harmless); with long tails (reverb, ELECTRO) the real length is longer and an allocation by the estimate overflows if the host trusts it; it must size by `thunder_render_length` after the end or grow. Not harmful to stems when the host passes `use_frames`.

## Findings (so far)
- F1 (should fix, U34a header): "always longer, never shorter" is false (section 4); say "usually longer, can be shorter by up to the sequencer lead (0.25 s)". Separate point (note): unknown-length renders can end earlier than the page's (trailing silence), compare with the JS `N0` rule.
- F2 (should fix, test): add a direct stem of a non-soloed track while another is soloed, comparing audio (mutant c2 is caught only by the stats field).
- F3 (should fix, test): add a cross-track choke song (two tracks, pads in one choke group): the stem of the choked track must equal the mix with the others muted; mutant (a) is caught only by a length check today.
- F5 (note): bus-add order in `thunder_render_stem_tracks` vs the JS Map order (section 1).
