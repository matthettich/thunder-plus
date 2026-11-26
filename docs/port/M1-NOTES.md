# M1 notes: the effect level and dynamics records (METERS-DESIGN 4.1, 4.2, 5a)

Branch `n1/m1-fx-meters`, draft PR into `native`. Built by a builder; the lead reads the audio-side diff on the top model.

## Built
- `live.h` (blocks marked `M1`): `THUNDER_LIVE_EV_FXLVL` 11 (56 bytes), `THUNDER_LIVE_EV_FXDYN` 12 (152 bytes, entries `{int16 slot, int16 type, float red_db, in_db, out_db}`), `thunder_live_meters_spec`
  (`size, kinds, rate_hz, fx_tracks, chains_hz`; `THUNDER_LIVE_MK_FXLVL / FXDYN / CHLVL / MASTERM`, the last two reserved), `thunder_live_meters_set` (CONTROL), `THUNDER_LIVE_CAP_METERS2` (0x400), `thunder_fx_type_name(int)`.
- `live_host.h/.cpp`: new `LiveCmdType::MetersSet` (x = rate, y = chains_hz, i0 = kinds, i64a = fx_tracks); `LiveHost::metersSet` (control: E_STATE without F_METERS, E_ARG, E_BUSY from `push`),
  `applyMetersSet` (step 1, from `applyOne`), `postFxMeters` (step 9, after `postMeters`), members `fxLvlWatch_`, `fxDynWatch_`.
- `live_api.cpp`: `thunder_live_meters_set` (ABI `size` rule), `thunder_fx_type_name` (reverse of the plan reader: `fx_key_of`, `"p" + fx_pump_key`, `fx_mod_key`), caps bit.
- Tests: `test_live_meters2` (ctest `live_meters2`, sanitizer variant uses fewer quanta), `test_live_meters2_alloc` (ctest `live_meters2_alloc`, glibc without sanitizer, else exit 77), two size asserts in `test_live_h.c`.

## Decisions and deviations (read these)
- **`rate_hz` of the spec drives METERS** like `thunder_live_meters_want` (0 = off): the spec is a replacement state, so a spec with `rate_hz` 0 switches METERS off. The later of `meters_want` / `meters_set` wins. The shell must put the page's rate in the spec.
- `setLevels()` restarts the 12-block window (`lvlN_ = 0`, clears the partial peak), so `applyMetersSet` calls `setLevels` / `setTelemetry` ONLY on a host whose watching changes (edge), never for a re-sent identical spec. A host that becomes watched also has any old unpopped post dropped (one `popLevels` / `popDyn` into a stack scratch), so the first record is fresh.
- `FXLVL.slots`: bit k set when the host's chain has a slot k (`chainEntry`); `lvl[k]` is 0 for absent slots. A slot that was off all the time is in the mask with level 0.
- `FXDYN.type` = `FxEntry::t` of the chain entry with that slot number (looked up in the audio thread's `chain_` via `chainEntry`; same thread as the commit, so no race); -1 if not found (cannot happen in practice).
- `FXDYN.red_db` is 0 for the level-only slots (types 7, 26, 28: that is the FxHost / page behaviour); a real reduction comes from pump COMP / LIMIT / BAND (types 200+).
- `f` of both records = engine frame at the END of the quantum (`qStart + 128`; `post()` adds the host offset). METERS records are posted before them in the same step 9.
- CAP bit 0x400 chosen as the next free one; the other builder (`pp`) may want a bit too: renumber on merge if so.
- `thunder_fx_type_name` builds the "p..." strings once in a function-local static (thread-safe); do not call it on the audio thread. Types 200..224 -> "pcomp" etc., 300+ MODFX keys.
- CHLVL / MASTERM kinds and `chains_hz` are validated and stored in the command but nothing uses them yet (M2, M3).
- No cap on watched tracks in the core (ruling 3).

## Lines the lead must read (audio side, `live_host.cpp`)
- `applyOne`: the `case LiveCmdType::MetersSet` line; `applyMetersSet` (setLevels / setTelemetry edge flips, two stack scratch buffers: 32 bytes and 8 x 40 bytes).
- `afterBlock`: the one added call `postFxMeters(qStart + kLiveQuantum);` after `postMeters();`.
- `postFxMeters`: stack records, `popLevels` / `popDyn` only for watched hosts, `post()` (drop newest + count on a full ring: unchanged).
- No existing line was changed or moved; no existing record changed.

## Tests run (Release, Linux, 2 cores)
- `live_meters2`: 615 checks, 0 failed: sizes and offsets; E_STATE / E_ARG / size rule / E_BUSY at exactly 256; FXLVL and FXDYN bit-equal to a lock-step reference `FxHost` (popLevels / popDyn) on a fixed signal through tape + level comp + pump COMP (deepest reduction -30.5 dB; types 7 and 201); track 1 identical chain and signal but unwatched posts nothing; the audio is bit-equal with and without watching; master (bit 32); kinds apart; off stops; replacement drops a track; re-send is a no-op; `meters_want` alone = METERS only and METERS records bit-equal with the spec run; one gapless `seq` order; a consumer that never polls drops and counts without changing a sample; type-name round trip.
- `live_meters2_alloc`: 400 quanta, 246 records (204 FX), 0 allocator calls on the audio path.
- ASan + UBSan (THUNDER_SANITIZE=ON): `live_meters2` 285 checks, 0 failed, no leak (the TSan variant was not built: the test is single-threaded).
- ctest `^live_|^fx|^android_live|^meter|^mix`: 54 of 54 passed.
