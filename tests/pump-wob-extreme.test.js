// pump WOB's sub oscillator read its table past the end at extreme pitch (its phase wrapped once, but inc / 2 can exceed 1):
// pump-synth.wasm trapped (out of bounds) at 22050 Hz, MIDI note 127, Octave +2, Tune +12. Fixed in pump commit 5468d8c
// (WOB), with the same wrap in BandWaves::read (d4c4987) and GLOSS's DigiWaves::read (8d851d0). The case, driven through the
// wasm's own C interface (the pad path clamps the note to 60 + Tune, so it cannot reach note 127).
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

test('pump WOB: the extreme case that trapped (22050 Hz, note 127, Octave +2, Tune +12) no longer traps', () => {
  const X = wasm(), M = meta();
  const r = wobExtreme(X, M, 22050, 127, { octave: 2, tune: 12 });
  assert.equal(r.bad, 0, 'NaN or infinity in the output');
  assert.ok(r.pk < 4, 'output stays bounded (peak ' + r.pk + ')');
});

test('pump WOB: the same note with the Sub on its upper octave setting, and other rates, run without a trap', () => {
  const X = wasm(), M = meta();
  for (const sr of [22050, 44100, 48000, 96000]) for (const over of [{ octave: 2, tune: 12 }, { octave: 2, tune: 12, sub: 100, sub_oct: 1 }, { octave: -3, tune: -12, sub: 100 }]) {
    const r = wobExtreme(X, M, sr, 127, over);
    assert.equal(r.bad, 0, sr + ' Hz ' + JSON.stringify(over) + ': NaN or infinity');
  }
});
