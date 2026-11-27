# PC8: four small native-client items (build 689, branch `pc8/native-small`, PR into `beta`)

Protocol: `docs/port/CORE-BRIDGE.md` 7.2 on `origin/native`; code wins (shell: `native/desktop/shared/bridge_translator.cpp`, `native/android/.../Recovery.kt`, `BridgeTranslator.kt`).

1. **fx.dyn age.** `FXD_AGE = 600` in `index.html`; the FX stage's slot-box bar and `dkDynMeters` (FX bar) read nothing older; the curve's history already ran quiet after 300 ms.
2. **recovery.drop.** `rcvClean()` in `thunder-core-client.js` (`NC.dirtyOff`, `setDirty(false)`, and the 10 s tick). Needs `caps.recoveryPut`, a put made this session, no copy awaiting an answer. `recovery.drop` takes no cap in the shells (it is an ask; it deletes the put and the song.load copy).
3. **master.resetPeak.** Not supported by either shell today. `NC.mstResetPeak()` sends it only with `caps.masterResetPeak === true` during a native PLAY. Open: the shell side (core `Master::resetPeak()` exists) and the cap name.
4. **Send / Receive lab.** `CHAIN_STRUCT(ty, name)` in `chainParam`: no chain.edit, the song publish carries it (the core swaps the song). Open: native `cdiff` agreement.

Tests: `tests/native-client-pc6.test.js` (4 new).
