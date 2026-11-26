# M5 (master meters): the master panel's meters from the native engine (build 683, branch `m5c/master-meters`)

Shell side: PR #336 (`n1/m2-master-meters`): `meters.want {master:true}` and `{t:'master.lvl', seq, f, inPk, outPk, tp, inLu, outLu, comp, compIn, lim, load, sup:[48]|null}` (~20 a second, the master worklet's `e.data.meter` names), under `caps.meters2`.

## What it does
- `thunder-core-client.js` (`?v=7`): `meters.want.master` was never set by anything (the `wanted.master` flag stayed false), so it now comes from `host.mstWant()` (a missing or throwing hook means false), asked at every look as `chains` is, and goes only while a native PLAY runs, METERS is on and `caps.meters2`. `master.lvl` listener (only with meters2, ignored when no native PLAY runs) cleans the record (bad number: 0; loudness -180; compIn -200; comp / lim clamped to <= 0; sup not exactly 48 numbers: null) and calls `host.mstIn(obj)`.
- `index.html`: `mstNativeWant` (NCON() && tp.nc && the master panel open && page visible), `mstNativeIn` (sets `mstMeter`, stamps the time), `mstM()` (what the panel's draws and the CPU panel's "MST dyn" read: with the switch off it is `mstMeter` itself; native: a record older than 600 ms is a silent record, so numbers show 0 / dashes and the limiter history keeps scrolling). The web worklet's `onmessage` line is unchanged.

## Decisions
1. The page did NOT send `master:true` before (nothing set it), so the hook `mstWant` is new; panel open is polled (100 ms debounce + the 500 ms tick), as chains.
2. sup null stays null (what the worklet posts with the suppressor off; the bars simply are not drawn).
3. Silent record rather than null when stale: the panel's `live` stays true while playing, so the GR numbers read 0.0 and LUFS dashes instead of freezing; after STOP `live` is false as today.
4. Stale LUFS for a few seconds after opening is the shell's (M2-NOTES decision 1: filters are not reset); noted in the punch list. A `master.resetPeak` command does not exist yet: the PEAK readout click (`resetPeak` to the web worklet) does nothing on native; `tp` only grows within one watching.
5. Inert counts: NCON() 17 to 19, tp.nc 12 to 14 (lines 10 to 12).

## Tests
`tests/native-client-m5c.test.js` (5 tests).
