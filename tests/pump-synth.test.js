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
  assert.deepEqual(Object.keys(ui), ['pmono', 'pmonol', 'ppoly', 'ppolyl', 'pcoil', 'pcoill'], 'each instrument and its live twin');
  for (const k of P.rendered) {   // (a live twin renders through its rendered engine, with the same presets)
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

/* ───────── live (26 Oct): pump-synth-worklet.js, the Mono live / Poly live / Coil live engines ───────── */
function liveNode(kind, over) {
  const P = load(), c = H.workletScope({ Date }); H.load(c, 'pump/synth/pump-synth-worklet.js');
  const Proc = c.__registered['thunder-psyn'], bytes = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'pump/synth/pump-synth.wasm')));
  const node = new Proc({ processorOptions: Object.assign({ p: Object.assign(P.xeng()[kind].d(), { tune: 0, level: 0, drive: 0 }, over || {}), bytes }, P.liveSpec(kind)) });
  return { P, c, node };
}
const lanes = over => { const o = {}; for (let j = 0; j < 4; j++) { o['r' + j] = [1]; o['g' + j] = [0]; o['t' + j] = [0]; o['l' + j] = [0]; o['v' + j] = [100]; } for (const k in over) o[k] = over[k]; for (const k in o) o[k] = Float32Array.from(o[k]); return o; };
const stepAt = (at, a, b) => Array.from({ length: 128 }, (_, i) => (i >= at ? b : a));
const block = (node, P) => { const o = [[new Float32Array(128), new Float32Array(128)]]; node.process([], o, lanes(P)); return o[0]; };
function spy(c, name, log) { const X = c.__psynX; c.__psynX = Object.assign({}, X, { [name]: (...a) => { log.push(a); return X[name](...a); } }); }

test('pump synths live: notes become MIDI at their sample: retrigger, legato (note on before the old note off), bend, note off, velocity', () => {
  const { c, node } = liveNode('pmonol');
  block(node, {}); const log = []; spy(c, 'psyn_midi', log);
  const ev = () => log.splice(0).map(([, f, s, a, b]) => [f, s, a, b]);
  block(node, { t0: stepAt(10, 0, 1), g0: stepAt(10, 0, 1), v0: [90] });
  assert.deepEqual(ev(), [[10, 0x90, 60, 90]], 'note on: C-4 is the plugin\'s 60, at its sample, with its velocity');
  block(node, { t0: stepAt(5, 1, 2), g0: [1], r0: [Math.pow(2, 7 / 12)] });
  assert.deepEqual(ev(), [[5, 0x80, 60, 0], [5, 0x90, 67, 100]], 'a new note retriggers: note off, then note on');
  block(node, { t0: stepAt(5, 2, 3), g0: [1], l0: [1], r0: [2] });
  assert.deepEqual(ev(), [[5, 0x90, 72, 100], [5, 0x80, 67, 0]], 'a slide is legato: the new note on before the old note off');
  block(node, { t0: [3], g0: [1], r0: Array.from({ length: 128 }, (_, i) => Math.pow(2, (12 + 2 * i / 127) / 12)) });
  const b = ev(); assert.ok(b.length >= 3 && b.every(([, s]) => s === 0xE0), 'a pitch ramp is pitch bend');
  const last = b[b.length - 1], st = ((last[3] << 7 | last[2]) - 8192) / 8191 * 24; assert.ok(st > 1.4 && st < 2.05, 'bend reaches the ramp (' + st.toFixed(2) + ' st)');
  block(node, { t0: [3], g0: stepAt(64, 1, 0), r0: [Math.pow(2, 14 / 12)] });
  assert.deepEqual(ev(), [[64, 0x80, 72, 0]], 'note off when the gate drops');
});

test('pump synths live: a poly track\'s chord takes the lanes, one note each', () => {
  const { c, node } = liveNode('ppolyl');
  block(node, {}); const log = []; spy(c, 'psyn_midi', log);
  block(node, { t0: stepAt(0, 0, 1), g0: [1], t1: stepAt(0, 0, 1), g1: [1], r1: [Math.pow(2, 4 / 12)], t2: stepAt(0, 0, 1), g2: [1], r2: [Math.pow(2, 7 / 12)] });
  assert.deepEqual(log.filter(a => a[2] === 0x90).map(a => a[3]).sort(), [60, 64, 67]);
});

test('pump synths live: plays exactly what the pad renders (same values, same note), and sleeps in silence', () => {
  for (const [k, kl] of [['pmono', 'pmonol'], ['ppoly', 'ppolyl'], ['pcoil', 'pcoill']]) {
    const { P, c, node } = liveNode(kl), sr = 48000, offB = 188, N = 2 * sr, L = new Float32Array(N + 128);
    for (let b = 0; b * 128 < N; b++) { const o = block(node, { t0: [1], g0: [b < offB ? 1 : 0] }); for (let i = 0; i < 128; i++) L[b * 128 + i] = 0.5 * (o[0][i] + o[1][i]); }
    const r = P.render(k, Object.assign(P.xeng()[k].d(), { tune: 0, level: 0, drive: 0 }), sr, offB * 128 / sr), lat = k === 'pmono' ? 32 : 0;
    let md = 0; for (let i = 0; i < r.length - 200; i++) md = Math.max(md, Math.abs(L[i + lat] - r[i]));
    assert.ok(md < 1e-5, kl + ': live and rendered differ by ' + md);
    let calls = 0; spy(c, 'psyn_process', { push: () => calls++ });
    for (let b = 0; b < 400; b++) block(node, { t0: [1], g0: [0] });
    assert.ok(calls < 400, kl + ': still running after a second of silence');
  }
});
