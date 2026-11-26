# M2 notes: the master meters record (MASTERM) (METERS-DESIGN 4.1, 5a ruling 5)

Branch `n1/m2-master-meters`, PR into `native`. Built ON TOP of M3 (#334), which itself contains M4 (#332): both unmerged when this was built, so their diffs show here until they merge; when they do, merge `origin/native` into this branch (never rebase) and they drop out. The lead reads the audio-side diff on the top model.

## What the page reads today (origin/beta `index.html`, 25 Nov)
`rt.mst.wk.port.onmessage` keeps `e.data.meter` as `mstMeter`; the master panel and CPU panel read: `sup` (48 numbers or null, dB reduction per band, drawn as bars; the GR cell uses `-max(sup)`), `comp` (GR dB), `compIn` (detector dB, -200 when off), `lim` (GR dB; also the page's limiter history), `inLu`, `outLu` (shown only if finite and > -70), `tp` (LINEAR, shown as 20 log10 when > 0: "PEAK"), `load` (the CPU panel's "Master dynamics"). `inPk` / `outPk` are posted by the worklet but no page code reads them. All of these are what `MasterProcessor` already computes (`MasterMeter`), so MASTERM is a copy: no new maths, nothing added to the processor's arithmetic.

## Built
- `live.h`: `THUNDER_LIVE_EV_MASTERM` 14, 248 bytes (`float in_pk, out_pk, tp, in_lu, out_lu, comp, comp_in, lim, load; uint32 flags (bit 0: sup present); float sup[48]`); `THUNDER_LIVE_MK_MASTERM` (8, reserved by M1) now works. **The spec struct did NOT grow**: the kind bit is the whole subscription (the master is one; no track mask), so the ABI size rule needs no new field (a caller with the 32- or 40-byte struct can ask for MASTERM). No new cap bit (`CAP_METERS2` covers it).
- `master.h/.cpp`: `MasterProcessor::setMeters(bool)` / `metersOn()`. The loudness filters, peaks and queue were already built with every processor (the ctor's `meters` flag only gated their use); now the flag is runtime. `setMeters` flips it and, on an EDGE only, clears scalars (the running peaks, compressor / limiter read-outs, `nq_`, the queue indexes). No allocation, no filter reset (ruling 5).
- `live_host.h/.cpp`: `applyMetersSet` (end of it): `mchain_->mastering().setMeters(wm)` on an edge of the MASTERM bit; `postMasterMeters` (step 9 after `postChainsMeters`): drains `popMeter` (bounded by the queue depth, 8), builds the record on the stack, `post()`. Member `mstWatch_`. A `static_assert` for the size.
- Shell: `live_events.cpp` MASTERM -> `{t:"master.lvl", seq, f, inPk, outPk, tp, inLu, outLu, comp, compIn, lim, load, sup:[48]|null}` (the page's `mstMeter` field names; the page names no event, so the task's `master.lvl`); `BridgeTranslator` `meters.want {master:true}` -> kind 8 (false / null: off; a non-boolean refuses the message); Kotlin test cases added (CI compiles and runs them; not run here).
- Tests: `live_masterm`, `live_masterm_alloc`, `android_live_events` (master.lvl JSON), `bench_master_meters` (built, not registered).

## Behaviour
- Watched = kind MASTERM AND the host has F_MASTER. Records come at the processor's own cadence (one message per `round(sr / 128 / 20)` = 19 quanta at 48 kHz, 20.2 a second; the METERS `rate_hz` does not pace it). Nothing is posted while the mastering worklet is not in the path (song `mst.on` false, or no stage on: `MasterChain::process` does not call the processor then): the page's own master worklet does not exist then either. The SAFETY limiter's processor is never metered (ruling 5, no `lim2`).
- `f` = frame at the end of the quantum that posted it. Order in a quantum: METERS, FXLVL / FXDYN, CHLVL, MASTERM.
- Unwatched cost: one bool test in `postMasterMeters`, plus the existing `if (meters_)` tests inside the processor (they already existed; they are now a runtime flag instead of a constructor constant).
- Values are the worklet's own. Replaced so the JSON never needs an infinity: loudness `-inf` -> -180 (also below -180 -> -180; the page shows nothing below -70 anyway), any other non-finite -> 0 (compIn -> -200). `compIn` stays -200 when the compressor is off (the worklet's number, not -180).
- The tests prove the numbers: the record sequence equals, field by field (bit-equal floats), a lock-step reference `MasterProcessor` fed the host's own master input (the same song with mastering off), including the 48 suppressor bands, and the reference reproduces the host's output exactly.

## Cost (ns per 128-frame quantum, `bench_master_meters`, min of 5 runs of 20 s of loud audio, this machine: 2 cores, Release, Linux)
| stages | meters off | meters on | delta |
|---|---|---|---|
| compressor + limiter | 12586 | 13899 | +1313 |
| suppressor + comp + limiter | 44237 | 45830 | +1593 |
| limiter only | 5501 | 6740 | +1239 |

A quantum is 2 666 667 ns of real time: the meters add about 0.05 % of a core (0.06 % with the suppressor), the BS.1770 filters being most of it. Not measured on the Fold; the CI Linux numbers are what the task asked for, rerun `bench_master_meters [seconds]` on any build.

## Lines the lead must read (audio side)
- `master.cpp`: `MasterProcessor::setMeters` (new, 5 lines; scalar writes only). No existing line of `quantum()` / `process()` changed.
- `master.h`: `setMeters`, `metersOn` declared.
- `live_host.cpp`: `applyMetersSet` (the new `if (mchain_) {...}` block at its end), `postMasterMeters` (new), `afterBlock` (one added call after `postChainsMeters`).
- Nothing existing was moved; no existing record or ABI field changed.

## Decisions for the lead
1. **Loudness is not reset on `on`.** Ruling 5 forbids resetting filters on the audio thread, so while off the K-weighting filters and the 3 s history simply are not run; after an `on` they continue from what they held (stale state decays in the filter's own time; the 3 s loudness window still holds old blocks until 30 new 100 ms blocks replace them). So IN / OUT LUFS can show stale values for up to 3 s after the master panel opens. The peaks, reductions and queue ARE cleared on the edge (scalars), so those are fresh from the first record. If the owner minds the 3 s: a `reset()` of the two `Loudness` objects (a few doubles and a 30-element array, no allocation) at the edge is trivial and RT-safe; it was left out only because the ruling said not to reset filters.
2. **-180 for loudness, -200 for `compIn`** (see Behaviour). The page treats both as silent.
3. **No event rate control**: MASTERM is paced by the processor (20 a second), not by `rate_hz` / `chains_hz`. No `master_hz` field.
4. **`load` is wall-clock**, not reproducible, and still measured by the processor with `steady_clock` around each quantum (existing code, also when unwatched: `quantum()` has called `clock::now()` twice per quantum since U11; a vDSO read on Linux / Android, not a syscall, but it is on the audio thread). Not changed here; the lead may want it gated by the meters flag (but then `load` would be 0 when unwatched, which is what the page shows without the worklet's meters anyway).
5. **Event name `master.lvl`** (the page names none); fields as `mstMeter`. M5's second half sends `master:true` (the page already does: `meters.want.master`) and sets `mstMeter` from the event; `sup` null maps to undefined as the design says.
6. **For M6 (CORE-BRIDGE 7.2):** MASTERM is under `CAP_METERS2` (no new cap bit); document type 14, the record, `master:true`, the -180 / -200 rule, and that `hello.caps.meters2` covers FXLVL, FXDYN, CHLVL, MASTERM.
7. `tp` is the processor's hold (`tpMax_`, "since resetPeak"); live nothing calls `resetPeak`, so it only grows within one watching (it is zeroed on each `on` edge). The page's worklet behaves the same unless the page posts `resetPeak`; if the page has a reset button, a `master.resetPeak` command is a small follow-up.
8. Not run: Kotlin unit tests (no Android SDK here; CI), a TSan build (single-threaded tests), the whole ctest (only `^live_|^fx|^android_live|^meter|^mix|^master|^chains`). `android_live_engine` fails in THIS build because it is configured with `THUNDER_WITH_LXR=OFF` (the ELECTRO fit test expects the drum module); unrelated to M2.

## Lead's read (25 Nov, 22:20, Fable): ACCEPTED, one change
- Audio side clean: `setMeters` touches scalars; `postMasterMeters` is one bool test unwatched and a bounded copy watched; `mst_` is never replaced (a `master.config` calls `set()` on the same processor, so the flag survives it).
- **Q1 decided: the two loudness windows are reset at meters-on** (`Loudness::reset`, about 50 doubles, no allocation). Ruling 5's "no filter reset" meant filters in the signal path; these only measure. The audio reads them in one place, the silence shortcut's `quiet()`, where on or off gives the same samples (the tests' bit-equal runs pass with the reset). LUFS now reads none (-180) for the first 100 ms after the panel opens instead of up to 3 s of an older part of the song.
- Q5: the two `steady_clock::now()` per quantum stay (older code, a vDSO read).
- Known, accepted: with the meters on, the silence shortcut starts later (it waits for the loudness filters to ring out); the limiter's envelope may then differ by less than 1e-9 from the unwatched run, below a float's step.
