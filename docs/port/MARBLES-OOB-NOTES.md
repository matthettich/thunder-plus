# Marbles LagProcessor out-of-bounds read (sanitizers job, PR #344 head c8867b27)

## 1. The read
`LagProcessor::Process(value, smoothness, phase)` does `Interpolate(lut_raised_cosine, phase, 256.0f)`: `i = int(phase*256)`, reads `table[i]` and
`table[i+1]` (frac = phase*256 - i). The table has 257 entries, so a `phase` outside 0..1 reads outside it. prepare.sh already patches `phase >= 1`
(returns `table[256]`); the negative side is still open. The ASan hit is 20 bytes before the table: `phase` about -0.022 gives index -5, frac -0.59.

Where the bad phase comes from (instrumented run, build-san copy only): `phase` is a slave/master ramp from `TGenerator::Process`, fed by the
ramp extractor. The wrapper (`dsp/mods/mod_marbles.cc`) always calls `t.Process(use_external_clock=true, ...)`. When the clock input has an edge
every 1-2 samples, the extractor's per-sample increment `frequency` approaches 1 (seen: 0.95, 0.60). Then
- `master_phase_ += frequency*jitter` reaches 1.76 / 1.27 (only one `-= 1` per sample): phase > 1, and
- the Bernoulli `SlaveRamp` (slave_ramp.h) lets `phase_` run past 1 before `Init()` sets `ratio_ = (1 - phase_) * expected`: ratio negative, the
  slave ramp output is negative (-0.0128, -0.0255, ... -0.0218 seen, falling linearly). Index -5 is the one ASan caught.
Inputs are clamped by the wrapper (`clampf`/`clampi`), so parameters are not the cause. The trigger is the clock input signal: run_modfx feeds
uniform noise (-0.8..0.8) to in[0], port linked, and the gate threshold 0.5 toggles at near sample rate.

## 2. New or pre-existing
Pre-existing, newly reached. `knob` was added in `addCvMods`, which runs before `addModsMods`, so `marbles` moved from index m to m+1 in
`modfx::at()`. run_modfx seeds both the module (`12345+m`) and the input noise (`99+m`) by m, so Marbles got a different noise stream and a different
random draw. The Knob does not touch Marbles. Native/Native desktop runs on `native` (ffa2c691, 34845b29, cf3d460b, 65e97ef8 ...) were all green with
the sanitizers job; the mirror itself changed nothing in Marbles code or in prepare.sh. (The seed-shift is inferred from the code, not bisected.)

## 3. Reproduced locally
`cmake -S native -B build-san -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTHUNDER_SANITIZE=ON`, `--target rt_harness -j2` (8.5 min), `./build-san/tests/rt_harness`:
same stack as CI (LagProcessor::Process <- OutputChannel <- XYGenerator <- MarblesMod::process <- MarblesM::process <- run_modfx).
With the fix in 5 the whole harness passes (rt_modfx ok: 77 modules x 3 rates). build-san deleted.

## 4. Web
mods.wasm (dsp/mods/build.sh, no prepare.sh patch) does not trap. I decoded the embedded wasm in `chains/chains-worklet.js`: the 12 floats before
`lut_raised_cosine` are `0,0,0,-0.395,-5.0e29,2,5,10,0,0,-5,0`, so for phase -0.0218 it reads t[-5] = 10.0 and t[-4] = 0.0, giving
warped = 10*1.59 = 15.9 (should be near 0). Native (non-ASan) reads whatever the linker put before the table (probably the tail of the previous
constant; under ASan a redzone). So native and web differ in this case, and both are wrong; the web's 15.9 feeds `Crossfade(warped, phase, linearity)`
(linearity (1-smoothness)*5, so only with smoothness < 1), and a bad output is caught by `ModsWrap::run` (outputs outside +-16 clear and the object is made again).
Reachable from a real patch? Only with a very fast clock into Marbles `clock` (audio-rate or noise into the gate input, or a tempo high enough that
the ramp step exceeds roughly 0.004 per sample: index below -1 needs phase < -1/256). Musical clocks (a few Hz to tens of Hz) cannot. Not reachable
through the parameter ranges (all clamped); only through the clock signal. The harness's noise clock is the unusual input.

## 5. Fix (recommended: a, prepare.sh)
One extra clause in the existing patch (also covers NaN, which fails `phase > 0`; `phase >= 1` was already handled). Tested in the scratch build:
rt_harness passes under ASan+UBSan. Phases in 0..1 are untouched, so goldens do not move; only out-of-range phases change (from garbage reads to
`table[0]` = 0). Web parity: the web keeps reading garbage in that one case until `dsp/mods/build.sh` gets the same patch and mods.wasm is rebuilt;
web builds are out of scope unless the lead wants bit parity for out-of-range clocks. Not applied to the repo.

```diff
--- native/core/third_party/prepare.sh
 new="""  // Thunder: phase == 1 would read lut_raised_cosine[257], past the 257-entry table, times a fraction of 0 (prepare.sh)
   float warped_phase = phase >= 1.0f
       ? lut_raised_cosine[256]
+      : !(phase > 0.0f) ? lut_raised_cosine[0]   // phase <= 0 or NaN (a fast external clock makes the Bernoulli slave ramp negative): table[-n] is out of bounds
       : Interpolate(lut_raised_cosine, phase, 256.0f);
 """
```
(also update the comment block above it.) Options b/c rejected: (b) would need the clamp in the extractor/slave ramps, which are inside upstream, not
the wrapper (clamping the wrapper's clock input would change audible behaviour); (c) hides a real, patch-reachable out-of-bounds read.
