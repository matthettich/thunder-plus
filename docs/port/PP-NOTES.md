# PP notes: pump and modular effect knobs at once (thunder_live_fx_values)

Branch `n1/pp-pump-params`, draft PR into `native`. Only `native/` and `docs/port/` change.

## What the page sends today (index.html on beta)

- Worklet `onmessage {pset:{slot, p | mp | pv}}` (index.html ~20866): the chain entry of that slot gets the new list, then `set(chain)` again. `fxpMsg` (~23589) builds the three kinds:
  - tfx: `p` = 12 mapped values (`D.map(...)`), already covered by `thunder_live_fx_param` / `FxHost::psetAt`.
  - **pump slot: `pv`** = `pumpPv(D, f, ov)`: the slot's WHOLE value list in the plugin's own parameter order, length = the plugin's parameter count (`D.pd.length`; 32 for WIDE, 277 for EQ; native cap `kPumpMaxParams` = 280). Missing values at the slot's own / the default. The PAGE maps its controls to this list (`pumpUnbyte` / `pumpClamp`); it is stored into a Float32Array, so each value is a float32. The plugin's own bypass entry is overwritten by the slot's ON / OFF (`pumpRun`: `v[u.bi] = u.byp`).
  - **modular slot: `mp`** = `TFX[k].map(f2, sr, bpm)`: the module's values in the table's order (`fx/modfxtable.inc`), count = the effect's control count (`kModMaxParams` = 16 cap; `FxEntry::nmp`), doubles. Native's `fx_mod_map` is the same conversion; the caller (page / shell) does it, the core does not.

## What FxHost can already do

- `psetPump(at, slot, pv, n)` / `psetMod(at, slot, mp, n)` queue (`qp_` 32, `q_` 256, sorted by `at`, applied by the first block that starts within 64 samples of `at`, inside p1 after the pump gate is taken) and apply with `applyPumpPset` / `applyPset`: copy into the chain entry, then `applyEntry` for EVERY slot (the JS `set(chain)` again) and `pumpUp()`.
- Allocation, read from the code: `applyPset`, `applyEntry` (pump: memcpy + `dirty`; modular: `fx_mod_set` = a copy into a double array), `pumpUp` (`pump::latency`) allocate nothing; `pumpRun` commits with `pump::commit` (documented in fxpump.cpp REAL-TIME RULES and run under the allocation guard by `rt_harness` with `psetPump`). **The one case that allocates:** `applyEntry` on a pump slot that is RESTING (`rs == 1`: OFF by its own setting, its output is the rest delay line) with changed values sets `pch`; `pumpRest` then wakes it with a fresh plugin (`renew` = `pump_free` + `pump_new`, the accepted FX1 rule "a pump slot waking from rest may allocate"). A plugin whose output went bad is remade the same way (existing, not caused by the call).
- Another hazard in `applyPset`: a modular list handed to a slot that is not modular would be read as `p12` (writes `p[0..11]` of a tfx entry). Both are closed below.

## Decision: the C call

```
int thunder_live_fx_values(thunder_live* L, int track, int slot, int kind /* THUNDER_LIVE_FXV_PUMP 1 | THUNDER_LIVE_FXV_MOD 2 */, const double* v, int n, int64_t at);
```
Differences from the first sketch: `v` is `const double*`, not `float*` (a modular value is a double in `FxEntry::mp`; a float would break "equals the plan with that value"; pump values are cast to float32 on the control side, exactly what the page's Float32Array does). The record is fixed-size (float[280] + double[16] + header, about 1.2 KB, copied) on a ring of 16 (`THUNDER_LIVE_FXVALUES_RING`; a knob drag is drained every quantum, E_BUSY is the caller's retry). Capability `THUNDER_LIVE_CAP_FXVALUES` = 0x800 (bit 11; 0x400, bit 10, is M1's `CAP_METERS2`).

- Control side (`LiveHost::fxValues`): judged against `ppKind_[track][slot]`, filled by `ppNote` from the plan's entries (fx_plan, and the plan play() makes for the tracks and the master): E_ARG if the slot is not a hosted pump / modular slot of that kind in the LAST PLANNED chain, n is not the planned count (`npv` / `nmp`), a value is NaN / infinite (pump: or not a float32), bad track / slot / kind / `at` / NULL; E_STATE without F_EFFECTS; E_BUSY when the ring is full (counted in cmd_busy).
- Audio side: `quantum()` step 1 reads `fxValues_.avail()` before the command loop (the P3b rule), `applyFxValues` runs right after `applyFxParams` and pops at most that many records into the host's queues with `psetPumpRt` / `psetModRt` (same `at` conversion as fx_param).
- FxHost: `Q` / `Qp` get an `rt` flag; the consumption lines call `applyPumpRt` / `applyModRt` for flagged records. These write only the one slot, and only if the chain in force holds a pump / modular slot there (else nothing is written: covers a later-timed commit that changed the slot). `applyPumpRt` on a resting slot stores the values (chain entry and `u.pv`) and returns: no `pch`, no fresh instance. Nothing is audible from a resting slot, and when it is switched on it is made with the stored values.

## Covered / rides in the song

Covered live: every pump type's whole value list on a running (or warming) slot; every modular effect's value list; a resting pump slot stores without waking. Rides in the song (not changed): the slot's ON / OFF (plan change: fx_plan + fx_commit), COMP / VOX external sidechain and NOTES FROM (refused at planning), a slot's pump state texts (MASTER / TUNE), a song's own FXV lanes for the slot (`FxBank::event` still computes from the planned bytes `mb`, so an FXV event after a live change moves the slot back to the planned values for the rows it touches; pump FXV is still refused), and values for a slot whose plan is staged but not committed when the record is drained at an earlier quantum (they meet the chain in force; a later-timed commit loses them: send them after the commit).
Not caught on the audio side: `FxHost` queues are 32 (pump) / 256 (modular) records by `at`; records far in the future beyond that are dropped and counted (`queueDropped()`; the pump counter has no getter).
