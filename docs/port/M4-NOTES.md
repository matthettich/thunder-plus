# M4 notes: the Android shell for the effect meters and `fx.values` (branch `n1/m4-shell-meters`)

Built on `native` d75762f (M1 #329 and PP #328 merged). Touches only `native/` and `docs/port/`. Nothing on the audio thread or in the core changed.

## What was done
- `live_events.cpp`: FXLVL -> `{t:'fx.lvl', seq, f, tr, a:[8]}` (linear peaks; NaN / negative / huge -> 0). FXDYN -> `{t:'fx.dyn', seq, f, tr, d:[[slot, 'type', red, in, out], ...]}`; type string from `thunder_fx_type_name`, dB with 3 decimals as the core gives them (`-180` = silence; a non-finite value becomes 0 for `red`, -180 for in / out). Field names checked against the page's `onFxLvl` / `onFxDyn` in `thunder-core-client.js` on `beta` (M5, build 680). `seq` is the core record's own `seq`, `f` the host frame. A track outside 0..32 or a short record is not forwarded; an FXDYN entry whose type the core has no name for, or slot outside 0..7, is left out (the page would drop it anyway). Both types are non-critical (dropped first when the queue is full).
- `BridgeTranslator`: `meters.want` with any of `fx` / `dyn` / `chains` / `master` / `trim` -> `LiveCall.MetersSet(hz, kinds, fxTracks)` -> `nativeMetersSet` -> `thunder_live_meters_set`. One `fx_tracks` mask = union of the `fx` and `dyn` lists (0..31, 32 or `"m"` = the master's effects); kinds FXLVL if `fx` is non-empty, FXDYN if `dyn` is non-empty. Rate: same clamp as before (0..30). A rate-only `meters.want` stays `MetersWant` -> `thunder_live_meters_want` (the old behaviour and tests unchanged). The spec is a replacement state, so "all off" from the page (`fx:[], dyn:[]`) is `MetersSet(rate, 0, 0)`.
- `hello.caps.meters2` = the core's `THUNDER_LIVE_CAP_METERS2` bit (0x400; set in every core build, so `true`). Also `caps.fxValues` from bit 0x800 (not asked for; harmless extra, the page ignores it).
- JNI `nativeMetersSet`, `nativeFxValues`; `fx.values {tr, slot, kind:'pump'|'mod', v:[...], at?}` -> `LiveCall.FxValues` -> `thunder_live_fx_values` (kind 1 pump, 2 mod; `tr` 0..31, 32 or `"m"`; slot 0..7; 1..256 finite numbers). A core refusal (E_ARG for the EQ's phase / lin_quality, a linear-phase EQ, a wrong count, an unhosted slot; E_STATE; E_BUSY; E_UNSUPPORTED) comes back as an `err` event `{where:'fx.values', msg:'rc -1 (refused: ...)'}` through the usual `check` path; no crash, audio untouched. The call goes through `LiveEngine::call` like `fx.param`.

## Decisions
- **No cap on watched tracks.** The task said to respect the cap; live.h (M1, METERS-DESIGN ruling 3) has none (E_ARG only for bits above 32), so the shell adds none; a track outside 0..32 refuses the message in the translator. If the owner later wants a cap, it is one line in `BridgeTranslator`.
- **Union mask.** The core has one mask and one `kinds`, so with `fx:[3], dyn:[5]` the core posts FXLVL and FXDYN for both tracks 3 and 5. The page keeps `fx.lvl` only for tracks that have a view and drops an `fx.dyn` whose slot type differs, so the extra records are harmless; the cost is a few extra records per second. M5 sends the same track in both lists today.
- `chains` / `master` / `trim` are read and ignored (MK_CHLVL / MK_MASTERM stay unset) until M3 / M2 / M7.
- An `E_BUSY` from `meters_set` or `fx_values` is reported, not retried: the page re-sends the whole meters state on its next change (and on `engine up`), and `fx.values` is followed by the next knob move. (If PC4 turns out to need a retry on busy, add it in `LiveBridge`.)
- The `fx.values` message is NOT an `ask`; a refusal has no reply id, only the `err` event.

## Follow-up (coordinator, 26 Nov): PC6 page side (#333)
- **fx.values is an ASK.** With an id the shell replies `{ok:true}` on rc 0, `{ok:false, busy:true, error:'busy'}` on E_BUSY, `{ok:false, error:'rc N (...)'}` otherwise (also when the translator refuses it, when the engine is down). Without an id: the err event as before. Every translator refusal reply now also carries `ok:false, error` (additive). `FXV_MAX` is 512 (a pump slot carries up to 280).
- **`recovery.put {project}`** (no reply, on the recovery thread, never the main or audio thread): written at once, atomically (temp file then rename, sidecar last) to `recovery.put.thunder` + `recovery.put.json` in filesDir, beside the `song.load` copy (`recovery.thunder`, written only on swipe-away as before). Not stored when not a string, empty, or above 24,000,000 chars (the page's `RCV_MAX`). Title / file in the sidecar come from the tracker.
- **`recovery.get`** returns the put text when a put exists, else the `song.load` copy, with `source:'put'|'song.load'` (the page decides by the text itself: `app:'thunder-project'` = full restore; the field is informational). `recovery.announce` / the `recovery` event use the put's meta when there is one.
- **`recovery.drop`** deletes both copies. **A successful `file.save` / `file.saveAs`** (`Recovery.onSaved`) also deletes the put: it is older than the save. The swipe-away writer is unchanged and cannot clobber a put (separate files). Known gap: if the person undoes back to a clean state without saving, an old put stays and is offered at the next start (the page does not tell the shell).
- `hello.caps.recoveryPut: true`.
- Tests: `BridgeTranslatorTest` (`fxValuesAskCarriesItsId`, `fxValuesReplies`), `SaveSafetyTest` (put stored and preferred, atomic, drop / saved, half-written put ignored, message rules and cap, tracker info). Kotlin: CI only.

## Tests
- Kotlin `BridgeTranslatorTest`: `metersWantEffectLists` (the page's exact messages, all-off, master as 32 and `"m"`, union mask, clamp, dedupe, refusals, `id` in the refusal) and `fxValues` (pump / mod, master, `at`, nine refusals, 257 values). **Kotlin does not run here: first compile is CI.**
- C++ `test_android_live_events` (new ctest `android_live_events`, no engine): fx.lvl and fx.dyn JSON exact strings, sanitising, bad track, short record, unknown type, n clamp, 8-entry line.

## Run here
- cmake host build (Release, `-j1`, LXR off): `android_live_events` passes. `android_live_engine` fails two checks (ELECTRO fit and fit json: "no-lxr") only because this build had `THUNDER_WITH_LXR=OFF`; the events part of that test (pos / rows / state decode) passed. Not related to this change; CI builds with LXR.

## Untested (CI or the device)
- The Kotlin compile and unit tests; the Android compile of `live_jni.cpp` (`nativeMetersSet`, `nativeFxValues`, added `<vector>` include); a real page + shell round trip (`fx.lvl` on the FX bar, `fx.dyn` on the stage); the `fx.values` rejection path on a device.
