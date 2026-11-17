# U18: the pump effects as slot kinds of the native track-effect host

State at the last commit of n1/u18. Source of truth for behaviour: the ThunderFx worklet (TFX_SRC in index.html). The JS is the specification, quirks included.

## Decision
Pump effects are slot kinds of `FxHost` (kind `kPumpT0 + type`, 200..224), not a sibling host: in the JS the pump slots live in the same worklet node and chain as the tfx slots and share
sleep, FXO switching, levels/telemetry and the clkSpb transport. Code: `core/src/fx/fxpump.cpp`, `pumpfx.h`, the pump branches in `fxhost.cpp`; the 25-type table is generated
(`pumptable.inc` from `tools/pump-table.mjs`, checked by `pump_table_current`).

## The gate: decided by the lead (Fable, 16 Nov evening) and applied here
The web app's `pump.wasm` and `mods.wasm` are compiled with -ffast-math, which lets the compiler reorder float sums; no native build can reproduce that order. Rebuilding the web wasm with
`-fno-associative-math` would make them bit-identical, but it was measured on 16 Nov evening (same machine, Node 22, pin-built pump, minimum of 7 runs) to cost CPU in the web app: LIMIT +40 %,
MASTER +40 %, METER +58 %, TUNE +108 %, HEAT +27 %, STRIP +19 to +34 %, VOX +22 %, SWIRL +22 %, TUBE +7 to +16 %, the others within 2 %; Marbles / Tides within about +-8 %. The owner's standing
concern is CPU on the phone, so the web app's wasm flags are NOT changed. Instead:
- (a) The native build of these libraries stays STRICT (no fast-math), so the native app is deterministic across macOS, Windows, Android and Linux.
- (b) Against the fast-math wasm reference the gate for these units (pump effects U18, Marbles / Tides, later Plaits / Elements) is a tolerance: each case within -100 dB of the reference's peak
  (max abs difference / reference peak), the worst case printed. It was -80 dB before: this TIGHTENS it. Every case that is bit-exact today stays bit-exact (`kPumpGateExact` in the generated
  `golden/pump_gate.inc`, 40 cases): a case that goes from exact to "within" fails (enforced in the pin-built run, `--pin`).
- (c) Cross-platform determinism is gated separately: `golden/pump_gate.inc` also holds the SHA-256 of the NATIVE STRICT render of every case (made on Linux x86-64, GCC 13) and of the reference's
  input, checked on every platform by every test_pump run. (Not yet seen on macOS / Android in CI: pump uses the system libm; if a hash differs there, that is the finding.)
- (d) A case where a quantiser or other discontinuity follows a nonlinear stage gets a named rule in `kRules` of test_pump.cpp: at most N samples above the -100 dB line (N = twice the measured
  count), every other sample within it. Today exactly one: `x_two_hosts_same` (TUBE into CRUSH): measured 20 samples above the line in all (-68.4 dB worst), so N = 40. A new such case must be listed by name
  with its measured numbers; there is no wildcard.
- (e) Wake allocations in the pump slot (pump_free + pump_new in the audio thread, as the JS does): accepted for now; spare instances are deferred to the real-time unit (U31).
- (f) The `pump` ctest runs against the SHIPPED `pump/pump.wasm`, an older pump than the pin (MASTER 75 parameters against 77, latency 358 against 4454). Whether the web app moves to the pin's MASTER is the
  owner's decision (it changes how his mastering sounds) and is being asked. Until then the reference for the gate is the PIN-BUILT wasm (parity-u18.sh's second run) and the shipped-wasm run skips by name,
  with the reason printed, `p_master_def`, `p_master_alt` and the 2 MASTER latency pairs (`kSkipShipped`). Each was verified (`test_pump <dir> --no-skip`): the only failures of the shipped run without
  the skip are those two cases (first differing sample 358 = the latency difference, posts differ) and they are within the gate against the pin-built wasm. Nothing else is skipped.
The gate logic is `native/tests/test_pump.cpp` (`kGateDb`, `kRules`, `kSkipShipped`, the exact list and hash check); the golden is written by `GATE_WRITE=<file> native/tools/parity-u18.sh` (never hand-edited; rebuild test_pump after).

Result (GCC 13.3, Release, pin-built reference from wasi-sdk 25): 80 cases, 40 bit-exact, 39 within -100 dB (worst: v_hot -115.6 dB, then v_hot_low -115.7 is the same level; VOX -119.4 / -121.8), 1 by rule
(x_two_hosts_same, 20 samples above the line of 163840, limit 40), 0 failed, 0 native-hash differences, latency 0 of 50 differ. Every one of the 39 "within" cases is below -100 dB: none needed loosening.
The older figures and "-80 dB" statements below are from before this decision and are history.

### CI result of the first push (b9c53b9f): the tolerance gate holds everywhere, the native hashes (c) do NOT
Same counts and the same worst case (-115.6 dB, v_hot) on every platform; 0 exact-list regressions. The committed SHA-256 of the native strict render (made with GCC 13.3 / glibc 2.39) differs on the CI runners:
GCC 15.2 / glibc 2.42: 7 of 80 (p_calm_def / alt, p_tone_def / alt, h_series_clk, h_series_master, b_blk_mixed); clang (runner): 4 (p_calm_def / alt, h_series_clk, b_blk_mixed); macOS (arm64, Apple libm): 22. Not
proven why (no such compiler / libm here); the cases are the ones with transcendental calls (CALM, TONE, the chains with them) and the count depends on the compiler and the libm, so the strict pump build is NOT bit-identical across
compilers / libms: it uses the system libm (as tfx did before U14's measured same-machine comparison; tfx has the same exposure). Marbles / Tides' native hashes (U21 PR) held on macOS and in the wasm job. For the lead: either
the pump TUs get a libm of their own (fdlibm / the core's rt/ maths) or the hash check (c) for pump becomes per-platform-class. Not loosened here: the check stays and CI stays red on it until decided.
Also fixed on the way: rt_core aborted under the sanitizers ("free(): invalid size": rt_harness wraps malloc / free for the guard, pump's AlignedBuffer is posix_memalign + free; under ASan the pair split between
glibc and ASan's heap): the malloc / free wrappers are off under AddressSanitizer (operator new / delete guards stay).

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

## x_two_hosts_same: the first differing bit is in TUBE's halfband FIR dot product (D1, 16 Nov; evaluation order, nothing else)

Question left open in the section above (point 5): WHICH operation in TUBE differs from the pin-built `pump.wasm`. Answer, measured: **evaluation order of a float dot product
(reassociation), allowed by the wasm's `-ffast-math`, not libm, not FMA / contraction, not x87.** Sure about it: the bit-exact fix below is run, not argued.

### Method
A driver that calls pump's own C API (`pump_stage` / `pump_new` / `pump_io` / `pump_process`, 128-frame blocks) for type 9 (tube), preset 1 ("Warm glow": mode 0, drive 8, sag 15, mix 100; auto gain on, oversampling 2 = two halfband stages),
on the `mix` signal of `pump-dump.mjs` (81920 frames at 48 kHz, the `x_tube_only` scratch case). Native = the strict host build of `core/pump/CMakeLists.txt` (GCC 13.3, `-O3 -fno-fast-math -ffp-contract=off -mno-mmx`),
wasm = the pin-built `pump.wasm` (wasi-sdk 25, `-O3 -ffast-math -fno-finite-math-only -msimd128`) run in Node 22. The drivers reproduce the gate's figure: first differing output sample 2, `0xa7d98116` against `0xa7d98117`
(1 ulp), 56135 of 81920 L samples differ. Then a temporary copy of the pump checkout (never committed) records a double / float at six points of `Engine::processChunk` and in `HalfbandStage::up` / `down` for the first block, from both builds, and the traces are compared point by point.

### Bisection (first block, first differing record at each point)
| point | what | first difference |
|---|---|---|
| input of pre-emphasis, pre-emphasis output (float), low cut | base-rate path before the oversampler | none (64 of 64 equal, bits) |
| `HalfbandStage::up`, accumulator `acc`, call 3 | 1st halfband stage, 4th input sample | equal (`0x36cdd983`) |
| **`HalfbandStage::up`, accumulator `acc`, call 4** | **5th input sample** | **native `0xb755edb3`, wasm `0xb755edb2`** |
| upsampled buffer (two stages) | | index 14 |
| tube stage output (`tanh`, `exp`, `sqrt`, ... in double) | | index 14, a consequence |
| after `downsample` | | index 2 (`-0x1.112d3ep-46` against `-0x1.112d40p-46`) |
| final L | | index 2, the gate's sample |

So the first bit that differs is `acc` of the first halfband upsampler, before any nonlinear function runs. Everything after it (the curve, `std::tanh`, the 2nd stage, the down filter) differs only because its input does.
At call 4 the input history is x[0..4] (all earlier history is zero), so the sum has five non-zero terms (`common/dsp/Oversampler.hpp` of the pump pin, line 55: `for (int j = 0; j < nEven; ++j) acc += evenTaps[j] * h[j];`; `down()` line 75 is the same loop):

| term j | `evenTaps[j] * h[j]` (float) |
|---|---|
| 0 | `0xb648fd79` |
| 1 | `0x369e4e18` |
| 2 | `0xb6f0ba49` |
| 3 | `0x3724d2ca` |
| 4 | `0xb78fa583` |

* Sequential, as the strict native build adds: partial sums `0xb648fd79, 0x35e73d6e, 0xb6b6eaee, 0x3692baa6, 0xb755edb3`. Native result `0xb755edb3`. (Native equals this sequential sum in 64 of 64 calls checked.)
* 4 lanes, as `-msimd128` with `-ffast-math` vectorises it (lane k adds terms j = k mod 4; term 4 lands on lane 0; then `(l0+l1)+(l2+l3)`): lane partials `0xb7a8c532, 0x369e4e18, 0xb6f0ba49, 0x3724d2ca`, total `0xb755edb2`.
  Wasm result `0xb755edb2`. (The wasm equals the 4-lane sum in 64 of 64 calls; the plain sequential sum in only 25 of 64; a halving lane reduction in 39 of 64.)
The product terms are identical in both; only the order of the additions differs. The wasm contains `f32x4.add` / `f32x4.mul` for this loop (`-S` output of the tube unit: 12 of them). The reduction is allowed to be re-ordered only because of `-ffast-math` (`-fassociative-math`).

### What it is NOT (each tested)
* libm: not involved at the first differing bit (it is upstream of every `tanh` / `exp`). And with the reduction made strict the two builds agree on the whole run, `std::tanh` (glibc) against wasi-libc included, for this preset.
* FMA / contraction: native is built with `-ffp-contract=off`; wasm SIMD has no FMA (no relaxed-simd). Neither contracts.
* x87: x86-64 GCC uses SSE for float and double; the x87 `fprem` problem in `CMakeLists.txt` is a different case.
* Native with `-ffast-math` is NOT the fix: GCC 13 with `-O3 -ffast-math -fno-finite-math-only` differs from the wasm in 52313 of 163840 samples (its vector width / unrolling differ from wasm's). Same compiler flags do not give the same reduction order on two targets.

### Which change makes them equal (measured; nothing applied; no code or flag on this branch changed)
Rebuilt the tube unit and `pump_wasm.cpp` of the pin with wasm flags changed, the native build unchanged, compared over 81920 frames, L and R (163840 samples):
| wasm flags for the pump units | samples that differ from native |
|---|---|
| as now (`-ffast-math -fno-finite-math-only`) | 112298 |
| `-ffast-math -fno-finite-math-only -fno-associative-math` | **0 (bit-exact)** |
| `-fno-fast-math -ffp-contract=off` (strict, simd128 kept) | **0 (bit-exact)** |
The smallest flag change is therefore `-fno-associative-math` on the wasm build of pump (`pump/wasm/build.sh` `FLAGS`, and the CMake wasm branch of `core/pump/CMakeLists.txt`); that keeps the rest of fast-math. The other route, a source change: write the two `Oversampler.hpp` loops with an explicit accumulator order that both builds keep (for example four named accumulators combined as `(a0+a1)+(a2+a3)`), which also pins the native result to the wasm's order; that changes the plugin's own output too, so it is the owner's decision. Which one to take is Fable's; the gate and goldens are unchanged here.

### What this means for the failing case and the others
* `x_two_hosts_same` (-68.4 dB): TUBE's output differs by about 1e-7 relative from the wasm, CRUSH (alone: exact) quantises it with `std::round(x * q) / q`; 14 samples land on the other side of a step. With TUBE bit-exact (rows above) CRUSH gets the same input in both builds, so those 14 flips cannot happen. This last step is an inference (TUBE and CRUSH measured separately, the two-stage chain not re-run with the changed wasm).
* Likely, not measured: the same loop (`HalfbandStage`) is in every oversampled pump effect, so a good part of the many "-130 dB, 1 ulp" rows in the table above (TONE, CALM, LIMIT, HEAT, ...) would go with it. Other reductions (`for ... acc +=` in the true-peak interpolator, filters) are other candidates for the rest. Not run.
* Nothing here says the native result is the wrong one: strict sequential order is what the source says.

### Reproduce (the instrumentation is not in the repo)
Copy the pinned pump checkout; add a `dbg.h` recording `(tag, index, double)` into a static array; call `dbg(...)` at the six points in `TubeEngine.hpp` and in `HalfbandStage::up/down`; export the array from `pump_wasm.cpp` (`__attribute__((export_name(...)))` for the wasm); build with the flags above; run the driver for 128 frames on both; diff the traces.
Scratch driver, harness and traces were under `/home/claude/d1work` on the build machine and are not kept.
