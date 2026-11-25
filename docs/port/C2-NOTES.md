# C2-NOTES: the chains host LIVE (unit C2b, branch `n1/c2-live`)

Spec: `docs/port/C2-DESIGN.md` (R1..R15, "C2b: done when"). Status is at the top; it is updated with every push.

## STATUS (update me)
- STAGE 1 (no dependency on C2a): code written, compiles; tests NOT yet written.
  - DONE (code): `ChainsHost` (`chost.{h,cpp}`): `keepPatch`, `live` flag, `play` / `stop` / `param`, `playing_`, `audioStats` / `ctlStats` / `audioNodes`.
    `live.h`: `F_CHAINS`, `CAP_CHAINS`, `chains_bytes`, stats fields, `thunder_live_chain_edit`, text. `live_api.cpp`: caps + `chain_edit` wrapper.
    `LiveHost` (`live_host.{h,cpp}`): `chains_`, `ChainsCommit`, `chBack_`, `chainParams_`, `PlayPlan::chains`, `chainsDecide / chainsFinish / chainsAbandon` (control, R3 / R5 / R6), `chainEdit` (R8, written already),
    `tryStartPlay` / `dropPlay` / `applyOne` / `adopt` / `render` / `dispatch` / `songEnded` / `collect` / `~LiveHost` / `publish` / `statsGet`.
  - STAGE 1 STUB: `live_host.cpp` top, anonymous namespace `chains_diff` / `bus_plan_equal` (Same or Structural by text; field-wise bus plan compare) with the C2a signatures; `chainsDecide` forces a full plan at every PLAY (`forPlay`).
  - NEXT: tests (stage 3 list), then stage 2 (merge `origin/n1/c2-cdiff`, delete the stub, remove the `forPlay` line).
