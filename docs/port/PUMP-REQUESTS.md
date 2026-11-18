# Requests for the pump repo (matthettich/pump), from the Thunder Plus port (18 Nov 2026)

The web app and the native core both build pump from one pinned commit. The pin stays at **deb07d3** on the web (build 645 backs out builds 643 / 644);
the native re-pin to e43ee10 (PR #225) is held so both sides move together. Two changes in pump unblock one re-pin for both.

## 1. FREEZE: do not build all seven spectra inside `psyn_new` (blocks the re-pin)
- At e43ee10 the first FREEZE synth made in a wasm instance / process builds all seven built-in spectra at creation: about 0.95 s in Node, 0.75 to 1.5 s in
  Chrome's worklet, about 0.64 s natively (GCC), about 11 MiB. Before (deb07d3) each spectrum was built at its first note: 33 to 406 ms each, 941 ms in total.
- In the web app `psyn_new` runs in the worklet's first `process()` block, on the audio thread: every node in the context stops for the whole build. Four
  rounds of web-side warm-up could not hide it (`/home/claude/progress/verify-224.md`, `verify-224-fix.md`, `verify-224-fix2.md`, `verify-643.md`,
  `verify-644.md` in the session that did the work; the facts are in HANDOVER.md on `beta`, build 645's entry).
- The native side asked for the opposite of the old behaviour (no allocation or table building at the first note on the audio thread), which is why the
  change was made. Both needs are met by any ONE of these, in order of preference:
  a. **An explicit, chunked warm-up API**: e.g. `psyn_freeze_warm_step()` builds one spectrum (or a bounded slice of one, a few ms) per call and returns
     how many remain; `psyn_new` builds nothing; a note on a source whose spectrum is not ready yet is silent or uses a cheap fallback, never builds on
     the audio thread. The web worklet calls a step per `process()` block while idle; the native host calls it to completion off the audio thread.
  b. **A build option** (`PUMP_FREEZE_EAGER`, default on): the native build keeps today's behaviour; the web wasm is built with it off and gets the old
     lazy per-source build (accepting the old 33 to 406 ms per first note).
  c. Precomputed spectra as a data blob loaded at instantiation (no build at all), if their size is acceptable.
- Whichever is chosen: the spectra must be bit-identical to today's so the goldens do not move.

## 2. Oversampler `dot4`: keep the four accumulators in source order under fast-math
- 8d95efc sums the halfband FIR with four named accumulators in a fixed order, but the wasm is built with fast-math and clang still reassociates them, so
  the strict native build is not bit-exact with the wasm (native stays at 40 of 80 effect cases exact; `x_two_hosts_same` passes at -68.4 dB with 30
  samples over the -100 dB line against a limit of 40).
- In a scratch build `#pragma clang fp reassociate(off)` inside `dot4` made the wasm match the strict order in 4000 of 4000 trials. With that in pump,
  TUBE / LIMIT / HEAT / STRIP / oversampled COMP should become bit-exact natively and the `x_two_hosts_same` rule can be dropped.

## 3. Smaller, not blocking
- FLEX (e7c55dc) is a plugin only: it is not in `wasm/build.sh`, `pump_wasm.cpp` or `pump_meta.cpp`, so neither the web app nor the native table has it.
  The owner decided it stays hidden in Thunder Plus either way.
- POLY (about 190 ms) and GLOSS (about 380 ms) also do noticeable one-time work in `psyn_new` (measured in Chrome on deb07d3): the same chunked warm-up
  would cover them.
- The old prepare patch mapped a NaN WOB sub phase to 0; upstream's fix does not. The fuzz found nothing without it.
