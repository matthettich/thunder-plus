# P3b notes: a private random state per effects plan; the FX-param snapshot (the L1 window)

**Status (top, updated last):** BUILT, Release ctest green (241 tests, 1 skipped by design). ASan+UBSan and TSan green on the touched tests (Gates, bottom). Branch `n1/p3b-fxparams`, draft PR into `native`. Only `native/` and `docs/port/` changed.

## What changed (read these lines, in this order)

1. `native/core/src/live/live_host.cpp`, `quantum()` step 1 and `applyFxParams`: `fxParams_.avail()` is read at the top of step 1 (next to the chains snapshot) and `applyFxParams(qStart, n)` pops at most `n` records (`while (n-- && fxParams_.pop(fp))`), as `applyChainParams(n)`. A record pushed after a commit that the command loop did not see waits for the next quantum, where it meets the new chain. One TEST-only hook line (`testFxSeam_`, null in a product) sits between `applyCommands` and the drain. `live_host.h`: the `applyFxParams` signature, `setTestFxSeam`, two members.
2. `native/core/src/fx/fxhost.{h,cpp}`:
   - `FxPlan` gets `Rs rs_` (a copy of the three statics: wasi `rand()`, `stmlib::Random`, DaisySP `myrand()`) and `rsUsed_`.
   - `plan()` (control thread) runs the effect constructors on `pl->rs_` (`ModuleState ms(&pl->rs_...)`), never on the host's fields. `seedPlan(pl)` (control thread, private) seeds `rs_`: the FIRST plan of a host that makes an effect with statics starts from the fresh-wasm-instance values (rand 0, stm 0x21, daisy 1), so a render's one plan is exactly as before; the k-th later one starts from splitmix64(k * golden) (`planSeq_`, control side only).
   - `commit()` (audio thread, between blocks): `if (pl->rsUsed_) { rand_ = pl->rs_.rand; stmRng_ = ...; daisySeed_ = ...; }`: three scalar copies, no allocation, no lock. A plan that made no such effect leaves the audio state alone (state continues as today).
   - `process()` is unchanged: it still wraps its work in `ModuleState ms(&rand_, &stmRng_, &daisySeed_)`, and `rand_ / stmRng_ / daisySeed_` are now touched only by the audio thread (process, commit) and the constructor's initialisers. The control thread writes `pl->rs_` only before the hand-over (the ring is the release / acquire) and never again; the plan may be deleted right after `commit` (the state was copied).
3. `native/tests/test_live_fxparams.cpp` (ctest `live_fxparams`) and its lines in `native/tests/CMakeLists.txt`.

## Decisions taken (no questions overnight)

- **Offline renders stay bit-identical**, by construction: one plan per host = the old initial values; and the audio state after `commit` equals the old shared state after the constructors ran (same stream, same order). Proof: every existing fx / render golden passes unchanged (full ctest). What DOES change, deliberately: a plan made after the first one that creates NEW Beads / Clouds / Cloud Seed / Airwindows / pitch shifter objects used to continue the shared stream; it now starts a deterministic stream seeded from its own counter (the old behaviour was a data race and not repeatable in live use; in offline renders with several plans per host this changes the noise only, and no golden does that, see the run).
- The state is copied on commit only when the plan made an effect with statics (`rsUsed_`), so a plan that reuses or adds only non-noise effects does not restart the audio stream (as before).
- A plan that is never committed (stale / dropped) loses its seed counter value; later plans get later counters: still deterministic for a given sequence of plan() calls.
- Not done, listed for the lead: while a PLAY is stalled (`stalledPlay_`), chains params are not drained (`applyChainParams` is skipped) but FX params are, as before: a stalled play's fx plans are committed later (`tryStartPlay`), so a param drained meanwhile is applied to the old chain. The brief asked for the snapshot only; mirroring the chains' "not while a play waits" rule would also hold records in the ring (the 64-slot ring could fill: E_BUSY), so I left it. If the owner wants it, it is one `if (!stalledPlay_)` plus a decision about ring pressure.

## Mutation table (each seen to FAIL, then reverted)

| # | Mutation | Seen to fail |
|---|---|---|
| M1 | `applyFxParams(qStart, 0xFFFFFFFFu)` (drain everything) | `live_fxparams`: "a param pushed after a commit the loop did not see meets the NEW chain": 24559 samples differ |
| M2 | `plan()` runs its constructors on the host's `rand_ / stmRng_ / daisySeed_` again (the old code) | `live_fxparams`: "plan() leaves the audio side's random state alone (rand 0 -> 4743923304649127710 ...)" |

(An earlier version of the seam test, with an empty track-0 host, did NOT fail M1: the queued pset waited in `q_` and landed on the new chain anyway. The test now commits a delay in slot 0 first, so the old chain exists and would take the param.)

## What the lead must read

- `FxHost::commit` last lines and `FxHost::plan` / `seedPlan` (about 12 lines), `quantum()` step 1 (3 lines), `applyFxParams` (1 line changed). Nothing on the audio path allocates or locks (the allocation guards in `rt_harness` and `live_fx` pass).
- That `process()` never reads `pl->rs_` and `commit()` is the only audio-side read of a plan's state, after the ring hand-over.
- The `stalledPlay_` note above.

## An honest note on the TSan case

With the effects of the test chains (Beads, Pitch shifter, Clouds, Reverb, Space) TSan did NOT report the old code (M2 run under TSan: clean). Reason, measured: the constructors only draw `rand()` (Airwindows / Cloud Seed seeding); `stmlib::Random` and `myrand()` are drawn only by `process()`, and `process()` never drew `rand()` in these chains, so the control thread's writes and the audio thread's accesses did not touch the same field in the same run. The race is real by construction (same fields, two threads, no ordering) but latent for these effects. So the deterministic probe `FxHost::audioRandState` (a read-only getter for tests, used from the owning thread) is what fails M2; the two-thread test stays as the guard for any effect that starts drawing the same statics in both places, and it ran clean under TSan with the fix.

## Gates (final head)

- Release, full ctest at the commit before the last one: 241 tests, 0 failed, 1 skipped (`pumpsynth_refused`, by design). After the last commit (the getter and its test) every `fx*`, `live_*` and rt test again: 49 of 49 passed. The allocation guards (`rt_harness`, `live_fx`) pass.
- TSan (`-fsanitize=thread`, build-p3b-tsan): `fx_ref`, `fx`, `live_fx`, `live_fxparams` passed, no report.
- ASan + UBSan (`THUNDER_SANITIZE=ON`, build-p3b-asan): `fxzero`, `fx_ref`, `fx`, `live_fx`, `live_fxparams` passed.
- No `lxr` under native/core; no new allocation or lock on the audio path.
