# C2-NOTES: the chains host LIVE (unit C2b, branch `n1/c2-live`)

Spec: `docs/port/C2-DESIGN.md` (R1..R15, "C2b: done when"). Status is at the top; it is updated with every push.

## STATUS (update me)
- STAGE 2 PART 1 DONE: C2a merged (origin/n1/c2-cdiff, a merge), stub deleted, `chainsDecide` is R3 in full (a..e), `live_chains` all green. TODO: OOM test, rt_harness pass 4, TSan test (R13), mutation table, final ctest.
- STAGE 1 (no dependency on C2a): code written; `native/tests/test_live_chains.cpp` (ctest `live_chains`) written: 204/206 checks pass; the 2 failing are the param-publish checks that need stage 2 (stub makes a structural plan). `test_chains_song` level 3 passes (300 LiveHost runs bit-equal). OOM test, rt_harness pass 4, TSan test NOT yet written.
  - DONE (code): `ChainsHost` (`chost.{h,cpp}`): `keepPatch`, `live` flag, `play` / `stop` / `param`, `playing_`, `audioStats` / `ctlStats` / `audioNodes`.
    `live.h`: `F_CHAINS`, `CAP_CHAINS`, `chains_bytes`, stats fields, `thunder_live_chain_edit`, text. `live_api.cpp`: caps + `chain_edit` wrapper.
    `LiveHost` (`live_host.{h,cpp}`): `chains_`, `ChainsCommit`, `chBack_`, `chainParams_`, `PlayPlan::chains`, `chainsDecide / chainsFinish / chainsAbandon` (control, R3 / R5 / R6), `chainEdit` (R8, written already),
    `tryStartPlay` / `dropPlay` / `applyOne` / `adopt` / `render` / `dispatch` / `songEnded` / `collect` / `~LiveHost` / `publish` / `statsGet`.
  - STAGE 1 STUB: `live_host.cpp` top, anonymous namespace `chains_diff` / `bus_plan_equal` (Same or Structural by text; field-wise bus plan compare) with the C2a signatures; `chainsDecide` forces a full plan at every PLAY (`forPlay`).
  - NEXT: tests (stage 3 list), then stage 2 (merge `origin/n1/c2-cdiff`, delete the stub, remove the `forPlay` line).

## For the lead
1. **C2a `chains_diff` leaks on `std::bad_alloc`** (`native/core/src/chains/cdiff.cpp`, `modules_map` / `OMap::put`, line ~201-207 and ~228). Found by `native/tests/test_live_chains_oom.cpp` (`live_chains_oom`): with the n-th allocation failing inside `chains_diff(prev, next)` (two 4-module patches), every failing n leaves 1..30 blocks behind (2438 blocks over the whole sweep, still there after both JsonDocs are destroyed). The leaked blocks are the hash-table nodes / key strings made by `OMap::idx.emplace(...)` from `modules_map`. A control with the same `unordered_map<string,size_t>` + `vector<string>` pattern does not leak, so the cause is not libstdc++ nor the harness; I did not find it by reading (all members are RAII). Not patched (not my unit); the host calls it inside `try { } catch (...)` and returns E_MEMORY with the host state unchanged, so the effect is a leak per out-of-memory publish, nothing else. `live_chains_oom` therefore lets the leak count of a failing call into an allowance (printed at the end), as `chains_host` does for the engine's `preparePatch`.
