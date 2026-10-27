// The …pump synth engines (pump/synth/): the plugins' instruments in pump-synth.wasm, rendered as Thunder Plus pads by
// pump-synth-engine.js. Loaded exactly as the page and the render Worker load them (meta, glue, then the compiled module).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path');
const H = require('./helpers');

function load() {
  const c = { console: { warn() {}, log() {} }, Math, Float32Array, Uint8Array, WebAssembly, Object, Array, Set, Map, JSON, String, Number, isFinite };
  c.globalThis = c; c.self = c; vm.createContext(c);
  vm.runInContext(H.read('pump/synth/pump-synth-meta.js'), c, { filename: 'pump-synth-meta.js' });
  vm.runInContext(H.read('pump/synth/pump-synth-engine.js'), c, { filename: 'pump-synth-engine.js' });
  c.PsynCore.attachModule(new WebAssembly.Module(fs.readFileSync(path.join(H.ROOT, 'pump/synth/pump-synth.wasm'))));
  return c.PsynCore;
}
const stats = d => { let pk = 0, bad = 0; for (const v of d) { if (!Number.isFinite(v)) bad++; else pk = Math.max(pk, Math.abs(v)); } return { pk, bad }; };
// Factory presets that are silent in the plugin too, on a fresh instance: nothing excites the filter (no oscillator, no
// noise), so it never starts to self-oscillate. A pump issue, not a port one; listed so a fix there shows up here.
const SILENT_IN_PLUGIN = new Set(['pmono:Self-oscillating filter']);

test('pump synths: every factory preset of MONO, POLY and COIL renders, finite and below full scale', () => {
  const P = load(), ui = P.xui(), xe = P.xeng();
  assert.deepEqual(Object.keys(ui), ['pmono', 'ppoly', 'pcoil']);
  for (const k in ui) {
    for (const [name, o] of ui[k].pre) {
      const d = P.render(k, Object.assign(xe[k].d(), o, { tune: 0, level: 0, drive: 0 }), 48000, 1, { len: P.len(k) }), { pk, bad } = stats(d);
      assert.equal(bad, 0, k + ' ' + name + ': NaN or infinity in the render');
      assert.ok(pk < 1, k + ' ' + name + ': peaks at ' + pk.toFixed(3) + ' (full scale is 1)');
      if (SILENT_IN_PLUGIN.has(k + ':' + name)) continue;
      assert.ok(pk > 0.01, k + ' ' + name + ': silent (peak ' + pk + ')');
    }
  }
});

test('pump synths: a note is held for the hold, released, then rendered until it dies away; Tune moves the pitch', () => {
  const P = load(), xe = P.xeng(), sr = 48000;
  const p = Object.assign(xe.pmono.d(), { a_r: 50, a_s: 100, tune: 0, level: 0, drive: 0 });
  const short = P.render('pmono', p, sr, 0.25), long = P.render('pmono', p, sr, 1);
  assert.ok(short.length > 0.25 * sr && short.length < 0.6 * sr, 'held 0.25 s: ' + (short.length / sr).toFixed(2) + ' s');
  assert.ok(long.length > 1 * sr && long.length < 1.4 * sr, 'held 1 s: ' + (long.length / sr).toFixed(2) + ' s');
  // zero crossings over the sustained part: an octave up doubles them
  const zc = d => { let n = 0; for (let i = 0.2 * sr; i < 0.2 * sr + 9600; i++) if ((d[i - 1] < 0) !== (d[i] < 0)) n++; return n; };
  const up = P.render('pmono', Object.assign({}, p, { tune: 12 }), sr, 1);
  const r = zc(up) / zc(long);
  assert.ok(r > 1.8 && r < 2.2, 'Tune +12 doubles the pitch (crossings ratio ' + r.toFixed(2) + ')');
});

test('pump synths: a pad renders the same every time (a fresh instance per render), and the output is golden', () => {
  const P = load(), xe = P.xeng(), got = {};
  for (const k of ['pmono', 'ppoly', 'pcoil']) {
    const p = Object.assign(xe[k].d(), { tune: 0, level: 0, drive: 0 });
    const a = P.render(k, p, 48000, 0.5), b = P.render(k, p, 48000, 0.5);
    assert.equal(H.hashFloats(a), H.hashFloats(b), k + ': two renders of the same pad differ');
    got[k] = H.hashFloats(a);
  }
  const g = H.golden('pump-synth', got);
  if (!g.updated) assert.deepEqual(g.diff, [], 'the …pump synths sound different (a rebuilt pump-synth.wasm? run UPDATE_GOLDEN=1 npm test if intended):\n' + g.diff.join('\n'));
});

test('pump synths: every row and section has a value in the pad, and Hit and Volume are there', () => {
  const P = load(), xe = P.xeng(), ui = P.xui();
  for (const k in ui) {
    const d = xe[k].d(), rows = ui[k].rows;
    for (const key of Object.keys(rows)) assert.ok(key in d, k + ': a row with no value (' + key + ')');
    for (const [, keys] of ui[k].secs) for (const key of keys) assert.ok(rows[key], k + ': a section lists ' + key + ' but there is no row for it');
    assert.ok('vel' in d && 'volume' in d, k + ': Hit and Volume');
  }
});
