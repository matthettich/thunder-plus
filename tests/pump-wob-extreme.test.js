// WOB at extreme pitch (pump 5468d8c, d4c4987, 8d851d0; in the app since build 690): on pump deb07d3 (builds 642 to 689) the sub
// oscillator read its table past the end and pump-synth.wasm trapped with "memory access out of bounds" at 22050 Hz, MIDI note 127,
// Octave +2, Tune +12. Now it must run finite and bounded there and at other rates. The pad path clamps the note to 60 + Tune, so
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

for (const [sr, over] of [[22050, { octave: 2, tune: 12 }], [44100, { octave: 2, tune: 12 }], [48000, { octave: 2, tune: 12 }], [96000, { octave: 2, tune: 12 }], [22050, { octave: -3, sub: 100, sub_oct: 2 }]]) {
  test(`pump WOB at ${sr} Hz, note 127, ${JSON.stringify(over)}: no trap, finite, bounded`, () => {
    const X = wasm(), M = meta(); const r = wobExtreme(X, M, sr, 127, over);
    assert.equal(r.bad, 0); assert.ok(r.pk < 4, 'peak ' + r.pk);
  });
}
