// KNOWN TRAP on the pinned pump wasm (pump deb07d3, build 645, same as 642): WOB's sub oscillator reads its table past the end
// at extreme pitch (its phase wraps once, but inc / 2 can exceed 1). pump-synth.wasm traps with "memory access out of bounds"
// at 22050 Hz, MIDI note 127, Octave +2, Tune +12. Fixed in pump commit 5468d8c (WOB), d4c4987 (BandWaves::read) and 8d851d0
// (GLOSS DigiWaves::read), which build 643 / 644 carried and build 645 backed out. THIS TEST FLIPS when pump is re-pinned to a
// commit with the fix: then turn it back into "no trap, NaN-free, bounded output". The pad path clamps the note to 60 + Tune, so
// it cannot reach note 127: the case is driven through the wasm's own C interface.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), vm = require('vm');
const H = require('./helpers');

function wasm() {
  const z = () => 0;
  const inst = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(path.join(H.ROOT, 'pump/synth/pump-synth.wasm'))), { wasi_snapshot_preview1: { fd_close: z, fd_seek: z, fd_write: z, proc_exit: z } });
  inst.exports._initialize(); return inst.exports;
}
function meta() {
  const c = {}; c.globalThis = c; vm.createContext(c); vm.runInContext(H.read('pump/synth/pump-synth-meta.js'), c); return c.PUMP_SYNTH_META;
}
function wobExtreme(X, M, sr, note, over) {
  const type = M.types.indexOf('wob'), specs = M.synth.wob.params, f32 = (p, n) => new Float32Array(X.memory.buffer, p, n);
  const st = f32(X.psyn_stage(), 512);
  specs.forEach((p, i) => { st[i] = over[p.s] != null ? over[p.s] : p.def; });
  const h = X.psyn_new(type, sr); assert.ok(h >= 0, 'psyn_new');
  X.psyn_midi(h, 0, 0x90, note, 127);
  let pk = 0, bad = 0;
  for (let b = 0; b < Math.ceil(sr * 0.5 / 128); b++) {   // half a second
    X.psyn_process(h, 128);
    for (const ch of [0, 1]) for (const v of f32(X.psyn_out(ch), 128)) { if (!Number.isFinite(v)) bad++; else pk = Math.max(pk, Math.abs(v)); }
  }
  X.psyn_free(h); return { pk, bad };
}

test('pump WOB: KNOWN trap on this wasm (22050 Hz, note 127, Octave +2, Tune +12) still throws out of bounds; flips when pump is re-pinned', () => {
  const X = wasm(), M = meta();
  assert.throws(() => wobExtreme(X, M, 22050, 127, { octave: 2, tune: 12 }), /memory access out of bounds/);
});
