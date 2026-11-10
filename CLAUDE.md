# Thunder Plus: notes for Claude sessions

- Read `HANDOVER.md` first: where things stand, the owner's decisions, and the dated work log (newest first under the To-do).
- **If the owner says "I want to build the helper"** (or asks to start the native I/O helper / multi-channel inputs): follow
  the section "Build the native I/O helper — START HERE" near the top of `HANDOVER.md`, with
  `docs/NATIVE-IO-HELPER-PLAN.md`. It has the owner's answers, the task list (who does what, delegated vs. lead), and when
  each task counts as done.
- **If the owner says "continue the helper"** (or reports how the helper beta went): start at "PICK UP HERE" in the START HERE
  section of `HANDOVER.md` (PR #14, the helper, is merged into `beta` and `main` as of 8 Nov; the `io-helper` branch is history).
- **Native Mac / Windows / Android / iOS apps:** the plan is "Native port" in the To-do section of `HANDOVER.md` with
  `docs/NATIVE-PORT-STRATEGY.md`; the task-by-task plan is `docs/port/WORKPLAN.md` (port work goes on the `native` branch, in
  `native/` only, never in the app's own files). A question about it gets an answer from those, nothing built. Start stage N0 (and only N0)
  only when the owner says so in words, such as "start the native spike".
- Entries dated before 8 Nov 2026 are in `docs/HANDOVER-ARCHIVE.md`.
- **The owner's running test list is "Owner's punch list" in `HANDOVER.md`.** Every build adds its tests there as tickable items
  (which device, what a pass looks like); an item leaves only when the owner reports a result (then it is ticked or becomes a bug
  entry). "Give me my test checklist" means: read that section and make a Claude Doc grouped by phone, computer and hardware.
- Day-to-day work goes on the `beta` branch; `main` only by merging `beta` when the owner asks. Keep `HANDOVER.md` up to
  date at the end of a session.
