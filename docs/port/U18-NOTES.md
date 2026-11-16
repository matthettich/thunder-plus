# U18: the pump effects as slot kinds of the native track-effect host

State at the last commit of n1/u18. Source of truth for behaviour: the ThunderFx worklet (TFX_SRC in index.html). The JS is the specification, quirks included.

## Decision
Pump effects are slot kinds of `FxHost` (kind `kPumpT0 + type`, 200..224), not a sibling host: in the JS the pump slots live in the same worklet node and chain as the tfx slots and share
sleep, FXO switching, levels/telemetry and the clkSpb transport. Code: `core/src/fx/fxpump.cpp`, `pumpfx.h`, the pump branches in `fxhost.cpp`; the 25-type table is generated
(`pumptable.inc` from `tools/pump-table.mjs`, checked by `pump_table_current`).

## What works
Plan on the control thread (instance made with the staged values; latency measured with a temporary instance, no bypass override: `pumpLatencyOf`), values and commit, per-block process at
any block size (chunks of kFxBlock), latency into U10's PDC path (`FxBank::pdcSlots` passes bypass and measured latency; `up` lags creative-effect transport, build 616), latency-matched
bypass, the OFF rest and its three wakes (values changed, ON, FXO), hotBad / hotClip, transport (playing / tempo / ppq incl. the NaN-ppq quirk), notes (pmidi), FXV (psetPump), pump_hold,
COMP's external sidechain (sources / tap map), `{lvl}`, `{dyn}`, `{pump}` telemetry and `{plat}`.

## Gate (tools/pump-dump.mjs against tests/test_pump.cpp; -80 dB below the reference peak per case)
80 cases: every type at defaults and one preset, ON / OFF / switching, rest and wake, block sizes 1/37/128/480/1024, two hosts side by side, several slots in a chain, sidechain, notes, FXV,
runaway plugin; plus `pumpLatencyOf` of 50 type x values pairs; an order check (all cases again in reverse order must give the same bits), a control-thread / audio-thread gate stress test.
Reference = real worklet JS in Node with a pump.wasm BUILT FROM THE PIN with wasi-sdk 25 (`parity-u18.sh`); the shipped `pump/pump.wasm` is the second run.

| compiler (native) | vs pin-built wasm: exact / within -80 / failing | failing figures |
|---|---|---|
| GCC 13 | 38 / 40 / 2 | p_vox_def -71.8 dB, p_vox_alt -75.1 dB |
| clang 20 | 44 / 33 / 3 | p_vox_def -71.8, p_vox_alt -75.6, x_two_hosts_same -68.4 (14 of 163840 samples) |
| GCC 15 | 38 / 40 / 2 | p_vox_def -71.77, p_vox_alt -75.6 |

Against the SHIPPED wasm (GCC 13): 36 / 40 / 4: the same two VOX cases plus MASTER (+2.8 dB, posts differ; latency 4454 against 358, 77 against 75 parameters): version skew, the pin's MASTER is
newer than the shipped wasm. MASTER passes against the pin-built wasm. Report the pin's newer MASTER to the lead: the web app's pump.wasm should be rebuilt from the pin before cut-over.

### Why VOX / x_two_hosts_same are left failing (not state, not order, not uninitialised memory)
- Reverse-order second pass gives identical bits; a single case alone gives the same figure; valgrind (Debug build, p_vox_def) reports no error; the figure is the same on GCC 13 and clang 20
  and GCC 15 within 0.01 dB, so the native result is stable and the wasm differs.
- The difference starts at sample 4 at float epsilon (got 0.00162137672, want 0.00162137789) and stays at -75 to -85 dB for the whole case (no onset): arithmetic spread. VOX uses
  `std::exp`, `exp2`, `pow`, `sin` in doubles (VoxFx.hpp) with -ffast-math: wasm (wasi-libc) and glibc libm differ in the last bit and the feedback keeps it. pump's flags were not changed.
- x_two_hosts_same (tube + crush, clang only): 14 samples differ by up to 3e-4 around a quantiser step in CRUSH: a rounding edge flips under a different fast-math contraction. Exact on GCC.
Gate not loosened. `ctest pump` is therefore red by design until the lead decides (loosen for VOX, or build pump with strict flags).

## Real-time findings (the pump C API; full text in the header of fxpump.cpp)
- pump_process / values / commit / latency / tel / midi / transport / hold: no allocation that matters (all 25 types run under the allocation guard in rt_core: 2400 blocks x 4 hosts).
- pump_new / pump_free allocate and write the process-global handle table and stage: control thread only. A control-thread pump_new races with the audio thread's pump_process on the handle
  table (vector reallocation). The host's gate keeps them apart: the audio thread never waits, a block in which the control thread is making an instance leaves the pump slots alone
  (`pumpGated()`; measured: 15 433 of 15 498 blocks untouched while the control thread looped on creation: the others passed the sound = a click when an effect is added mid-song).
- pump_io, stage, tel, text buffers are process-global: one audio thread for all hosts (not enforced). Two audio threads would overwrite each other's block.
- The JS itself makes a fresh instance in the audio thread (rest wake; runaway output): pump_free + pump_new = allocation + malloc lock in the audio thread. Kept (the output depends on it).
  rt_core shows the wake DOES allocate (measured outside the guard) and `audioRemakes()` counts them. Allocation-free needs a spare instance per slot: lead's decision.
- Found and fixed on the way: a caller's FxEntry with more values than the plugin has parameters wrote past pump_values (heap corruption); the host clamps to pump_nparams.

## Not done / not hosted
- The pop-up window's telemetry feed (`ptel`): pumpRest refuses while a pop-up is fed in the JS; here no pop-up exists, so the rest is never refused for it.
- Enforcing the single-audio-thread rule; a spare-instance scheme for the wake.
- pump-synth (U03's instrument) is not touched.
- TSan on the gate test (the gate is two seq_cst atomics + a mutex on the control side; stress test passes, ASan build not run on this unit in CI beyond the existing job).

## Open questions for the lead
1. Is `ctest pump` allowed to stay red for VOX (-71.8 dB)? Options: build pump strict, or a per-case gate.
2. Wake allocations: accept the JS behaviour, or add spare instances.
3. Rebuild the web app's pump.wasm from the pin (MASTER 77 params, latency 4454).
4. CI: the u18 step is in the wasm job (needs the pin-built wasm); it will fail on VOX until 1 is decided.
