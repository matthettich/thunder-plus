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

Compilers here (15 Nov, strict pump build, Release): GCC 13.3 and clang 18. GCC 15 and clang 20 are NOT installable on this machine (`apt-get install g++-15 clang-20`: "Unable to locate package"); CI covers them.
The reference is a `pump.wasm` BUILT FROM THE PIN with wasi-sdk 25 (`build-wasm.sh`, downloaded fine here), meta from `pump-table.mjs --meta`; 80 cases, gate -80 dB below each reference's peak.
GCC 13 and clang 18 give IDENTICAL figures for every case (the two printed tables are the same text except the gate stress counters). One table therefore:

**80 cases: 40 bit-exact, 39 within -80 dB (worst of those: v_hot -115.6 dB), 1 failing: x_two_hosts_same -68.4 dB (14 of 81920 samples per host; 28 of 163840).** Latency: 50 type x values pairs, 0 differ. Reverse-order pass: 0 changed. Gate stress: ok.

| case | figure | case | figure | case | figure | case | figure |
|---|---|---|---|---|---|---|---|
| p_eq_def | exact | p_snap_def | exact | p_stutter_def | exact | r_sleep | -138.6 dB |
| p_eq_alt | -148.8 dB | p_snap_alt | exact | p_stutter_alt | exact | r_rest_node | exact |
| p_comp_def | exact | p_gate_def | exact | p_chop_def | exact | b_blk1 | exact |
| p_comp_alt | exact | p_gate_alt | exact | p_chop_alt | exact | b_blk37 | -149.3 dB |
| p_limit_def | -142.7 dB | p_echo_def | exact | p_twang_def | exact | b_blk128 | -149.3 dB |
| p_limit_alt | -131.5 dB | p_echo_alt | exact | p_twang_alt | exact | b_blk480 | -149.3 dB |
| p_calm_def | -132.4 dB | p_spring_def | exact | p_master_def | exact | b_blk1024 | -149.3 dB |
| p_calm_alt | -133.1 dB | p_spring_alt | exact | p_master_alt | exact | b_blk_mixed | -133.1 dB |
| p_tone_def | -133.1 dB | p_verb_def | -142.5 dB | p_strip_def | -131.0 dB | x_two_hosts | -134.0 dB |
| p_tone_alt | -133.1 dB | p_verb_alt | -140.3 dB | p_strip_alt | -129.6 dB | x_two_hosts_same | -68.4 dB |
| p_band_def | exact | p_grain_def | exact | h_series3 | -137.5 dB | s_send | exact |
| p_band_alt | exact | p_grain_alt | -148.9 dB | h_series_clk | -130.0 dB | s_tap | exact |
| p_wide_def | exact | p_vox_def | -119.4 dB | h_series_master | -128.4 dB | s_tap_chunked | exact |
| p_wide_alt | exact | p_vox_alt | -121.8 dB | h_onoff | -137.5 dB | s_none | exact |
| p_heat_def | exact | p_tune_def | -136.0 dB | h_fxo | -131.4 dB | s_send2_missing | exact |
| p_heat_alt | -148.0 dB | p_tune_alt | -142.0 dB | r_wake_on | -137.4 dB | v_pset | -131.4 dB |
| p_meter_def | exact | p_crush_def | exact | r_wake_values | exact | v_hold_eq | -154.9 dB |
| p_meter_alt | exact | p_crush_alt | exact | r_wake_fxo | -127.9 dB | v_hot | -115.6 dB |
| p_tube_def | -130.2 dB | p_swirl_def | exact | r_comp_eq | -157.2 dB | v_hot_low | -115.7 dB |
| p_tube_alt | -128.6 dB | p_swirl_alt | exact | r_burst | exact | v_tel | -136.0 dB |

VOX, which failed at -71.8 / -75.1 dB with the fast-math host build the previous table was made with, is -119.4 / -121.8 dB now (strict build). The earlier per-compiler table in this file (GCC 13 / clang 20 / GCC 15
with p_vox_*) predates the strict pump build and is withdrawn.

ctest, whole suite, on each of GCC 13 and clang 18: 81 tests, 80 pass; the one red is `pump` (it runs against the SHIPPED `pump/pump.wasm`: p_master_def / p_master_alt +2.8 dB with posts differing, latency 4454 against 358, 77 against 75
parameters = the pin's MASTER is newer than the shipped wasm's; and x_two_hosts_same as below). `pump_ref`, `pump_cases_current`, `pump_table_current`, `rt_core` pass. So `ctest` is red until the web app's pump.wasm is rebuilt from the pin
(or the `pump` ctest is pointed at the pin-built wasm, which is what parity-u18.sh's second run does).

### x_two_hosts_same: NOT a two-host effect (proof)
The premise that a second host changes those samples is false. Measured on the pin-built reference, GCC 13 (clang 18 the same):
1. Native host 0 and host 1 of x_two_hosts_same are bit-identical (L and R), and so are the two nodes in the JS reference. Both differ from the JS in the same 14 samples per host (L only; L index 8407..8413 and 27528..27534; R exact).
2. The same chain (tube preset 1 -> crush preset 1, play transport, 'mix' signal) as a ONE-host case (scratch case `x_one_host`, reference made with the same tool): native has the same 14 differing samples, same first sample
   (8407: got -0.5157712698, want -0.5158551335), same -68.4 dB. Native one host == native two hosts for host 0 (0 samples differ); JS one node == JS two nodes for node 0 (0 samples differ). A second host changes nothing.
3. Where the 14 come from: CRUSH alone on that signal (scratch `x_crush_only`) is bit-exact against the wasm; TUBE alone (scratch `x_tube_only`) is NOT: 55623 of 81920 samples differ, -128.6 dB (first at sample 2, 1 ulp, then 1 to a few ulp of float
   at most samples; the largest relative gap, 16103 ulp, is a tiny sample). CRUSH quantises with `std::round(x * q) / q` (CrushEngine.hpp `quantise`), so an input that differs by a few ulp lands on the other side of a quantiser step in a handful of samples; the 1-step flip
   (about 8e-5 here) is then smoothed by CRUSH's filters over 7 samples (8407..8413). Same input -> same output (crush-only exact); different input by 1e-7 relative -> a one-step flip. That is spread, not shared state.
4. The tube spread is arithmetic, not state: with the previous fast-math host build (diagnostic only, reverted) GCC 13 is also not exact for TUBE (p_tube_def -136.2, p_tube_alt -131.1 dB) and x_two_hosts_same happens to be exact there (a flip needs a tube sample that lands within the spread of a quantiser step: it did not with those particular bits; not investigated further). Per-instance state is not involved: CRUSH's RNG is seeded per instance (`rng.seed(0x2545F491u)` in Engine), the handle table, `stage`, `io`, `tel` of pump_wasm.cpp
   are process-global but are written before each read within a call (pump_new consumes `stage` and resets it to NaN; `io` is the block buffer), and the reverse-order pass gives the same bits for all 80 cases.
5. Not tested directly, so not claimed: WHICH operation in TUBE differs from the wasm (libm `exp` / `tanh` / `pow` in double against wasi-libc, or the wasm's `-ffast-math` reassociation that the strict native build does not do). For Fable: the wasm
   is built with -ffast-math, the host strict; they are not expected to match to the bit, and a quantiser downstream of a nonlinear stage will show 1-step flips in rare samples. The gate stays at -80 dB per case and stays red for this case; nothing loosened.

Scratch cases (x_one_host, x_tube_only, x_crush_only) were used for this and are not committed. Failing figure to keep an eye on in CI: x_two_hosts_same should read -68.4 dB on GCC 15 / clang 20 too if the strict builds agree as they did in U14.

### parity-u18.sh, run end to end here (BUILD_DIR=/home/claude/b/g13, GCC 13, PIN_WASM_DIR reusing the wasm built by build-wasm.sh from the pin)
rt_core, pump_ref, pump_cases_current, pump_table_current pass; `pump` (shipped wasm) fails as above; the second run against the pin-built wasm: 80 cases, 40 exact, 39 within, 1 failed (x_two_hosts_same -68.4 dB); the script ends `FAIL u18`.

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
1. `ctest pump` runs against the SHIPPED wasm and is red (MASTER skew, see above), and the pin-built run is red on x_two_hosts_same (-68.4 dB, cause above). Options: rebuild the web app's pump.wasm from the pin (clears the first); for the second, a per-case documented
   tolerance is NOT proposed here (not loosened): it needs Fable's decision.
2. Wake allocations: accept the JS behaviour, or add spare instances.
3. Rebuild the web app's pump.wasm from the pin (MASTER 77 params, latency 4454).
4. CI: the u18 step is in the wasm job (needs the pin-built wasm); it will fail on x_two_hosts_same until 1 is decided.
