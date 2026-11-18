# U31 notes: the ...chains engine in the core

Branch `n3/u31` (based on `native`). Source of truth: `chains/chains-worklet.js` outside the shared module block (ChainsEngine, Graph, ChainsProcessor, ThunderChains, the chains-only modules). THE JS IS THE SPECIFICATION, quirks included; what is decided differently is listed under "Deviations". Not in scope and not done: wiring into the mixer, `xeng_render_voice` or the bridge (see "Conditions for the wiring").

## What is here

| Piece | File |
|---|---|
| API, ChainsEngine, ChainsNode, ChainsLive declarations | `native/core/src/chains/chains.h` |
| Generated module tables (145 types) and the 13 pump modules | `chains/ctable.inc`, `chains/cpump.inc` (from `native/tools/chains-gen.mjs`, `--check` for drift) |
| Chains-only modules (VCF, VCA, Mixer, Stereo mixer, Osc, Noise, Repeat, Track In, Chain Out, Sidechain In / Comp / Out, Vocoder) | `chains/cmods.cpp` |
| Morph, Resample, the pump modules with spare instances | `chains/cmods2.cpp` |
| Engine: run list, bypass, buses, sends, sources, mix-down, per-track node | `chains/cengine.cpp` |
| Live runner | `chains/clive.cpp` |
| Graph (moved from U30's engine.cpp) | `native/core/src/waves/graph.h` |

The graph is U30's `waves::PatchImpl` (no second runner), built from a `ModTable {info, def}`: `wavesTable()` for U30, `chainsTable()` here. Modules are the MODFX ones, the 33 track-effect wrappers and control modules U30 ported, plus the chains-only ones. All chains modules are mono-scope (one slot, no voices).

## Results

- Golden rows (Node reference `native/tools/chains-dump.mjs` on the real JS; 462 cases): every non-library row, every scenario and every processor / node row is bit-exact (committed Linux FNV-1a hashes gate every platform). Library effect rows (`chains_effects.json`, 144 entries): 91 exact (kept exact), 37 within the -100 dB rule (peak of the difference against the reference), 16 left out BY NAME (below) with their numbers and a structural check (finite, RMS within 1 dB, same length). No native hashes for the pump modules (system libm). Scope's `show()` is not ported: 3 rows are audio-exact and left out by name.
- Live equals offline bit for bit at host block sizes 128, 64, 17, 1 and uneven (`test_chains_live`, `chains_live`), with events cut sample-exactly (13 cut blocks of 52 in the test).
- Latency: 127 samples, fixed. The engine renders blocks of 128 (the JS BLOCK); an output sample needs its whole block, and a host chunk ending at any sample can only have been served up to the last full block, so 127 is the smallest delay with which the output never depends on the host's block size. A fixed 127 (not 128) because the block that ends at host sample h is complete at h + 1 - 128 at the earliest.
- `rt_harness` (`run_chains`): the engine, a track node and the live runner under the allocation guard (odd sizes, parameter / bypass / transport changes, taps, sends, patch swaps, events, pump modules that go bad and take their spare): passes. The patch is prepared outside the guard; `serviceSpares()` / `service()` run outside it. (Resample takes: the guarded run reaches none in its time; the take path is tested in `test_chains`, `test_chains_live`.)
- Fuzz (`test_chains_fuzz`): hostile patches, several extreme fields at once, events, live runner with patch swaps. Under ASan + UBSan (`-DTHUNDER_SANITIZE=ON`, RelWithDebInfo, DSP and pump on): 1,840 runs (seeds 1 to 4), no finding. No third-party table over-read was found by it, so `prepare.sh` / `prepare-pump.sh` are NOT patched by this unit.
- Mutations: `native/tools/mutate-u31.sh <build dir>`: 22 seeded faults (bypass rule, crossfade, switch numbers, bypass numbers, source keys, send delay, written-bus reset, Chain Out clamp, mix-down clip, live latency / cut / event landing / mono fallback / right channel, pump bad limit, compressor ratio and slope, vocoder talk and spectral gain, Osc level, Resample slice table); all 22 killed. A mutation that makes the test hang counts as killed (timeout). The first runs found four gaps that were then closed: no test of Resample takes, of the live mono fallback, of the pump's +-8 bound, of Chain Out's clamp; and no golden row reached the Vocoder's spectral mode or a compressor ratio below 2 (three scenarios were added to `chains-dump.mjs`: `mod2/vocoder-spectral`, `mod2/vocoder-spectral-unlinked`, `mod2/comp-low-ratio`; every existing hash is unchanged, 465 cases now). Guard constants far below the test's resolution (for example Morph's / Vocoder's `1e-6` magnitude guard) are not mutation-tested.
- Test totals: Release GCC, LXR + DSP + pump: `ctest` 151 tests, 0 failed, 1 skipped (`pumpsynth_refused`, not this unit's). Closed build (no LXR, no DSP library, no pump): 107 tests; the run was cut by a server restart at test 77 (all passed up to there), then tests 78 to 107 and a re-run of every chains / waves / rt / modfx / fuzz test after the last source change: all passed (`chains`: 282 non-library module rows, 31 scenarios and 4 processor rows bit-exact; the 144 library rows are skipped by name in a closed build). The closed build found one bug, fixed: Marbles / Tides are absent there, and the JS draws one seed for Marbles at its first block; a silent stand-in now makes that draw so later rows get the same seeds.

## What I changed in U30's code

- `PatchImpl`, `ModNode`, `Slot` moved from `waves/engine.cpp` to the new `waves/graph.h`, taking a `ModTable` where they called `winfo` / `wdef`. `ctest waves` passes unchanged. New fields: `bypOk`, `pIn`, `pInR`, `pOut`, `pOutR`, `act`, `actTel`, `bn`, `seen`, `stk`, sized in `compile()`; the chains fixups in `addNode`.
- `engine.cpp`: `readPatchDoc(Json, PatchImpl&, bool chains)` (chains adds the Track In stereo cable step).
- `wmods.cpp`: `wavesTable()`; Expression has `ensureSrc` and `prepareText` (compiled at patch preparation).
- `modfx/modfx.h`: `kMaxPorts` 20 to 32 (Stereo mixer: 32 inputs); new virtuals `prepare(const double*)`, `service()`, `prepareText(const uint16_t*, int)` (default no-ops).
- `rt/fdlibm.cpp`, `rt/jsmath.h`: `js_log10` (V8 ieee754::log10, needed by Sidechain Comp).
- `fx/fxpump.cpp`, `fx/pumpfx.h`: `pump_make_staged`, `pump_free_ctl` (an instance made with staged parameters, NaN = default).
- `tests/rt_harness.cpp`: `run_chains` added; `run_modfx` keeps drawing for the first 20 ports (its random draws, not its coverage, would otherwise change with kMaxPorts and move Marbles into its known first-block remake, which the harness comment excludes); all other lines untouched, none of the lines PR #225 changes.

No pump source or third-party script is patched (PR #225's files untouched).

## Design points

- Run list (focus): the Chain Out of this track, the Sidechain Out at the lowest-track Chain Out of its connected component, with telemetry `scin` and probes, and every ancestor through `inc`. Computed on the control thread by `prepareDerived`; allocation-free after `compile()`.
- Bypass: numbers 1..15 (own `_bn`, else the lowest free, in patch order); switch rule `want = sw < -.5 ? own : sw >= 1.5 ? !own : sw >= .5`; a crossfade over a full 128 block; a module settled in bypass runs on silence. Dry copies live in `ModNode.dry`.
- Sends (Sidechain Out / In): standalone engines copy `sendOut` to `sendPrev` at `endStandaloneBlock`, one block of delay as the JS; with host sends (`setSendIn`) the host's are used. Source keys: 0 Self, 1..8 sends, 9 and up taps (through `tapMap`).
- Spare pump instances (decision (e)): a pump module's instance and its spare are made in `prepare()` on the control thread. A bad block (output outside +-8) is silenced on the audio thread, which swaps in the spare by atomic exchange and puts the dead one into a `dead_` slot; `service()` (control thread) frees dead handles and makes the next spare. Offline `process()` services inline (`setInlineService(true)`, the default) so offline matches the JS's new-instance-at-next-block. Counters `chains_pump_swaps/bad/dry`. Tested: with every parameter at maximum and a +-7.9 input, ptube, pgate, pvox, ptune, pcrush, pswirl, pstutter go bad; 634 of 637 bad blocks were followed by a spare and none ran dry.
- Resample: two preallocated buffer pairs; finished takes go through a 16-slot SPSC ring (`popTake` / `releaseTake`); a full ring releases the take at once.
- Expression: its text is compiled at `prepareText` (patch preparation), never at the first audio block.
- Live runner: events in an SPSC queue (4096) plus a pending array; blocks are cut at events; input and output rings of 512; patch swap by mailbox (8 slots) and retired slots; offline render = the same loop with no queue and no latency (`ChainsLive::renderOffline`).

## Deviations from the JS (decided)

1. Track In with a track out of range gives NaN in the JS; here silence.
2. Scope's `show()` (display) is not ported; its audio is.
3. Telemetry data output and the quality-shed load meter are not ported (the run list for telemetry is).
4. The JS's tapmap / busmap accept fractional entries (JS index quirks); fractions are dropped or truncated here.
5. A live Param event's module id is limited to 39 characters (name 23); a longer one is refused by `post()`.
6. The live runner has no text event: an Expression's text changes arrive by a patch swap.
7. A patch swap does not carry module state over (the JS `loadPatch` resets state too).

## Left out BY NAME (library effects, beyond -100 dB; numbers are the peak of the difference against the reference in dB)

Strict native builds of fast-math wasm effects differ from the reference. Structural check applies (finite, RMS within 1 dB, same length):
autowah default -87.6, stressed -89.8; beads default -66.5, stressed -77.4, switches-on -75.5; clouds stressed -70.7, switches-on -76.4; ladder stressed -47.0; pshift default -86.1, stressed -74.9, switches-on -90.9; pverb stressed -85.1; rings stressed -68.1; xmod default -90.2, stressed -91.3, switches-on -94.5. Also left out: the 3 Scope rows (display).

## Conditions for the wiring

1. One `ChainsLive` (or `ChainsNode`) per track; the host feeds exactly what `ChainsLiveConfig` declares (taps, sc, sends, feeds) every block, each input `n` frames long, pointer arrays at least that long.
2. A control thread must call `ChainsLive::service()` regularly (frees retired patches, keeps pump spares current). Without it the pump slot falls back to silence for a bad block.
3. Patches are built with `preparePatch` on the control thread; `post()` / `postParam()` from one producer thread only (SPSC).
4. The host compensates 127 samples of latency (report it to the delay compensation of the mixer) and sums the node outputs with `chains_mixdown`-compatible soft clipping if it wants the JS's ChainsProcessor sum.
5. Sidechain sends to other tracks arrive one host block late, as in the JS.
6. Pump and tfx effect modules exist only where the libraries are in the build (`chains_have_lib`); the closed build refuses those patches' modules (silent module), the rest runs.
7. Not wired here: mixer, `xeng_render_voice`, bridge, state persistence.

## Choices for the lead

- State across a patch swap: reset (as the JS) or carried over for modules whose type and id are unchanged.
- Offline inline pump servicing (one thread, JS-like) versus the live runner's control-thread servicing: both implemented; confirm the live one for the app.
- Limit of 39 characters for a Param event id: raise it (costs a bigger event) or keep it.
- Telemetry data output and the shed load meter: port with the bridge or leave to the UI.
- The 16 effect cases left out by name: accept, or have a pump/tfx owner look at the ones near -47 dB (ladder stressed is the largest).

## Not verified

- Windows / macOS / Android builds of this unit; only Linux GCC. The goldens are Linux hashes.
- The rt_harness run does not reach a Resample take.
