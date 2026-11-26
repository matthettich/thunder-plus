# M6 notes: CORE-BRIDGE 7.2 (meters2, fx.values, recovery.put)

Branch `n1/m6-bridge-72`, PR into `native`. Docs only (`CORE-BRIDGE.md`, this file); no code. **Stacked:** built on `n1/m2-master-meters` (M2 #336), which holds M3 (#334) and M4 (#332); all three were unmerged when this was written, so their diffs show in this PR until they merge. When they do, merge `origin/native` into this branch (never rebase).

## What changed in CORE-BRIDGE.md
- New sub-section **7.2** (10 numbered items plus known gaps). BRIDGE_V stays 1.
- Section 4 (events table): rows for `fx.lvl`, `fx.dyn`, `ch.lvl`, `master.lvl` with their rates.
- Section 3: the `meters.want` row now shows the fields as built (`{rate, fx, dyn, chains, master}`) and points to 7.2; the draft's `tracks` and `spec` fields never existed. This is the only existing line edited (section 7 and 7.1 untouched).

## Where the code and the notes disagree (the code wins in 7.2)
1. **CHLVL shape.** METERS-DESIGN 4.1: 280 bytes, all 32 tracks, `valid` mask, mono `in[32]` / `out[32]`, event `chains.lvl {i, o}`. Built (M3, live.h): 40 bytes per track, `in[2]` / `out[2]` (L, R), event `ch.lvl {tr, in:[l,r], out:[l,r]}`.
2. **Event names.** Design 4.4: `chains.lvl`, `master.meters`. Built: `ch.lvl`, `master.lvl`.
3. **`meters.want` fields.** Design 4.4: `chains:bool`, `trim:bool`. Built: `chains:[tracks]` (a boolean is accepted and ignored), `trim` only selects the whole-state form. CORE-BRIDGE section 3 had `tracks` and `spec`; neither exists.
4. **Spec size.** M1-NOTES lists the spec with five fields (32 bytes) and CHLVL / MASTERM "reserved, unused". Built: 40 bytes (`chains_tracks` added last by M3), both kinds work. M2-NOTES says "a caller with the 32- or 40-byte struct can ask for MASTERM": true.
5. **MASTERM rate.** M2-NOTES: "19 quanta at 48 kHz, 20.2 a second". 375 quanta a second / 19 = 19.7 a second (the processor's `nq_ >= js_round(sr/128/20)`). 7.2 says 19.7.
6. **CHLVL rate.** Design and M3-NOTES: 16 a second. The core waits `llround(sr / 16)` = 3000 frames and posts at the next quantum end, which is 24 quanta = 3072 frames, so 15.6 a second at 48 kHz (my arithmetic from `postChainsMeters`; not measured). 7.2 says "16 by default, 15.6 in whole quanta".
7. **Does the page send `master:true`?** M2-NOTES decision 5: "the page already does". PC6 / M5C-NOTES: it never did (nothing set it) until build 683, which adds `host.mstWant()`.
8. **fx.values in M4-NOTES:** the first section says 1..256 numbers, no ask, refusals as `err` events, and `caps.fxValues` "not asked for". Superseded by its own follow-up and the code: FXV_MAX 512, an ask with replies, the page needs `caps.fxValues`. 7.2 follows the code. (M4's notes still say "257 values" in the test list; the test itself uses 513, over FXV_MAX.)
9. **PP-NOTES** says the shells have no binding yet; M4 added it. Core errors reach the page as `{ok:false, error:'rc N ...'}`; the core itself answers E_STATE / E_ARG (not E_UNSUPPORTED) without the pump library; the shell's `-9` wording exists but the core does not return it from `fx_values`.
10. **`master.lvl` `sup`:** M2 / METERS-DESIGN say null becomes undefined on the page; M5C keeps it null. Page-side only; 7.2 says null.
11. **Age limit of `fx.dyn`:** none found on the page (the 400 / 600 ms limits are `fx.lvl` and `ch.lvl` 400, `master.lvl` 600). 7.2 says so.

## Not verified here
- Kotlin and Android code were read, not compiled or run. The page's `index.html` age limits (400, 600) were confirmed by reading `origin/beta` (`fxSlotLevel`, `chNativeFeed`, `mstM`).
- The 15.6 a second figure and the 19.7 a second figure are my arithmetic from the code, not measurements.
- The 7.2 text assumes #332, #334, #336 merge as they are. If their review changes a rule, 7.2 changes with it.
