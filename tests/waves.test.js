// Golden tests for the waves engine (the polyphonic synth): every module is rendered through the real engine, in the voice chain it
// would really sit in, and the audio is hashed. A refactor that changes any module's output, even by one bit, fails here.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const TFX = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))), DSP = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm')));
const BLOCKS = 120, TRANSPORT = { tempo: 120, lpb: 4, ticks: 6, playing: true };

function engine({ wasm = true } = {}) {
  const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');   // pageScope: that is the only way waves publishes WavesDSP
  if (wasm) ctx.WavesDSP.wasmInit({ dspBytes: DSP, tfxBytes: TFX });
  return { ctx, D: ctx.WavesDSP };
}
const defaults = m => { const p = {}; for (const [k, d] of Object.entries(m.params || {})) p[k] = d[0]; return p; };
const SW = new Set(['mute']);   // the one switch whose purpose is silence
/* `flip` is not a switch (it ranges 0..3) but it matters here: Utility flips the LEFT channel's polarity (flip & 1), and `mono` then sums
   the sides, so on a centred source (-x + x) / 2 is exactly 0. That is correct DSP, not a bug, but a pass that lands on it hashes
   silence. The switches-on pass therefore leaves `flip` at its default so `mono` can be tested on a signal that is not cancelled. */
const KEEP_DEFAULT = new Set(['flip']);
const stress = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (mx - mn === 1 && st === 1) { o[k] = mn; continue; } let v = mn + (mx - mn) * .7; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
const switchesOn = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (KEEP_DEFAULT.has(k)) { o[k] = d[0]; continue; } if (mx - mn === 1 && st === 1) { o[k] = SW.has(k) ? mn : mx; continue; } let v = mn + (mx - mn) * .3; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };

/* A patch that puts `type` where it would really sit. Voice (poly) modules run once per voice, so they get the voice chain
   hostIn -> [module] -> vca(adsr) -> out; a mono effect is fed by a playing voice. Control and gain modules (adsr, vca, atten,
   mixer) get a vco underneath so there is something to shape. (Forcing a poly module to mono scope, as a first attempt did, makes
   the engine skip it entirely: 10 of the 12 voice modules never ran, and a hash of nothing proves nothing.) */
function patchFor(M, type, params) {
  const m = M[type], ins = Object.keys(m.inputs || {}), outs = Object.keys(m.outputs || {}), poly = m.scope === 'poly', aIn = ins.filter(n => m.inputs[n] === 'audio');
  const mods = [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }], cab = [];
  const vco = () => { mods.push({ id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } }); cab.push({ from: ['h', 'pitch'], to: ['v', 'pitch'] }); };
  const env = () => { mods.push({ id: 'e', type: 'adsr', scope: 'poly', params: { a: .005, d: .2, s: .7, r: .3 } }); cab.push({ from: ['h', 'gate'], to: ['e', 'gate'] }); };
  const amp = () => mods.push({ id: 'g', type: 'vca', scope: 'poly', params: { gain: 1 } }, { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } });
  if (poly) {
    env(); amp();
    if (['adsr', 'vca', 'atten', 'mixer'].includes(type)) {
      vco(); mods.push({ id: 'b', type, scope: 'poly', params });
      if (type === 'adsr') cab.push({ from: ['h', 'gate'], to: ['b', 'gate'] }, { from: ['v', 'out'], to: ['g', 'in'] }, { from: ['b', 'out'], to: ['g', 'cv'] });
      else if (type === 'vca') cab.push({ from: ['v', 'out'], to: ['b', 'in'] }, { from: ['e', 'out'], to: ['b', 'cv'] }, { from: ['b', 'out'], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] });
      else cab.push({ from: ['v', 'out'], to: ['b', 'in'] }, { from: ['b', 'out'], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] });
    } else {
      mods.push({ id: 'b', type, scope: 'poly', params });
      if (ins.includes('pitch')) cab.push({ from: ['h', 'pitch'], to: ['b', 'pitch'] }); if (ins.includes('gate')) cab.push({ from: ['h', 'gate'], to: ['b', 'gate'] });
      if (aIn.length) { vco(); cab.push({ from: ['v', 'out'], to: ['b', aIn[0]] }); }
      cab.push({ from: ['b', outs.find(n => m.outputs[n] === 'audio') || outs[0]], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] });
    }
    cab.push({ from: ['g', 'out'], to: ['o', 'in'] });
  } else {
    vco(); env(); amp(); mods.push({ id: 'b', type, scope: 'mono', params });
    cab.push({ from: ['v', 'out'], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] });
    if (aIn.includes('in')) cab.push({ from: ['g', 'out'], to: ['b', 'in'] }); if (aIn.includes('inR')) cab.push({ from: ['g', 'out'], to: ['b', 'inR'] });
    outs.slice(0, 2).forEach((p, i) => cab.push({ from: ['b', p], to: ['o', ['in', 'inR'][i]] }));
    if (!aIn.length) cab.push({ from: ['g', 'out'], to: ['o', 'in'] });
  }
  return { modules: mods, cables: cab };
}
function renderModule(D, type, over) {
  const m = D.MODULES[type], params = Object.assign(defaults(m), over);
  const E = new D.WavesEngine(48000, { maxVoices: 4, quality: 'full', seed: 7, transport: TRANSPORT }); E.loadPatch(patchFor(D.MODULES, type, params));
  const L = new Float32Array(128 * BLOCKS), R = new Float32Array(128 * BLOCKS);
  for (let b = 0; b < BLOCKS; b++) { if (b === 0) { E.noteOn(60, 1); E.noteOn(67, .7); } if (b === 70) E.noteOff(60); E.process(128); L.set(E.bus(1), b * 128); R.set(E.busR(1), b * 128); }
  // Under load the engine sheds to 'draft' quality, which changes the DSP; a golden hash taken then would be wrong.
  if (E.quality !== 'full') throw new Error('engine shed to draft quality while rendering ' + type + ' (machine too slow for a stable hash)');
  return { L, R };
}
const finite = a => { for (const v of a) if (!Number.isFinite(v)) return false; return true; };
const energy = a => { let e = 0; for (const v of a) e += v * v; return e; };
const SKIP = new Set(['hostIn', 'out']);
/* Modules that are silent in this harness, and why. A silent hash proves "unchanged", not "working", so the list is explicit. */
const NEEDS_OWN_INPUT = new Set(['beads', 'mixer']);   // beads: a texture source that needs its own trigger/input; mixer: needs in1..in16, not `in`

test('waves: the engine loads and exposes its public surface', () => {
  const { D } = engine({ wasm: false });
  for (const k of ['WavesEngine', 'wasmInit', 'modsPrepare', 'MODULES', 'WASM', 'BUS_COUNT']) assert.ok(k in D, 'WavesDSP.' + k);
  assert.ok(Object.keys(D.MODULES).length >= 100, 'module count ' + Object.keys(D.MODULES).length);
});

test('waves: the real wasm (Plaits/Elements and the effects) loads, and records no error', () => {
  const { D } = engine(); assert.ok(D.WASM.dsp, 'dsp module'); assert.ok(D.WASM.tfxX, 'tfx exports'); assert.equal(D.WASM.err, undefined);
});

test('waves: every module renders finite audio and matches its golden hash (defaults, stressed, switches on)', () => {
  const { D } = engine(), actual = {}, bad = [];
  for (const type of Object.keys(D.MODULES).sort()) {
    const m = D.MODULES[type]; if (!m || !m.process || SKIP.has(type)) continue;
    const d = renderModule(D, type), s = renderModule(D, type, stress(m)), w = renderModule(D, type, switchesOn(m));
    for (const r of [d, s, w]) if (!finite(r.L) || !finite(r.R)) bad.push(type);
    actual[type + ' @default'] = H.hashFloats([d.L, d.R]); actual[type + ' @stressed'] = H.hashFloats([s.L, s.R]); actual[type + ' @switches-on'] = H.hashFloats([w.L, w.R]);
  }
  assert.deepEqual([...new Set(bad)], [], 'modules producing NaN/Infinity');
  const g = H.golden('waves-modules', actual);
  assert.ok(g.updated || g.diff.length === 0, 'audio changed in ' + (g.diff || []).length + ' render(s):\n  ' + (g.diff || []).slice(0, 20).join('\n  '));
});

test('waves: the same patch rendered twice is bit-identical (no clock, seed or Math.random dependence)', () => {
  const a = engine(), b = engine();
  for (const type of ['vco', 'noise', 'plaits', 'elements', 'string', 'fm', 'additive', 'reverb', 'chorus', 'delay']) if (a.D.MODULES[type]) {
    const x = renderModule(a.D, type), y = renderModule(b.D, type);
    assert.equal(H.hashFloats([x.L, x.R]), H.hashFloats([y.L, y.R]), type);
  }
});

test('waves: modules that make or process sound are not silent', () => {
  const { D } = engine(), dead = [];
  for (const type of Object.keys(D.MODULES).sort()) {
    const m = D.MODULES[type]; if (!m || !m.process || SKIP.has(type) || NEEDS_OWN_INPUT.has(type) || !Object.keys(m.outputs || {}).length) continue;
    const r = renderModule(D, type, stress(m)); if (energy(r.L) + energy(r.R) === 0) dead.push(type);
  }
  assert.deepEqual(dead, [], 'silent module(s): ' + dead.join(', '));
});

test('waves: a corrupt wasm does not throw, leaves the wasm modules out, and says why', () => {
  const ctx = H.freezeClock(H.pageScope()), warned = []; ctx.console = { warn: (...a) => warned.push(a.join(' ')) }; H.load(ctx, 'waves/waves-worklet.js');
  assert.doesNotThrow(() => ctx.WavesDSP.wasmInit({ dspBytes: new Uint8Array([1, 2, 3, 4]), tfxBytes: new Uint8Array([5, 6, 7, 8]) }));
  assert.equal(ctx.WavesDSP.WASM.dsp, null); assert.equal(ctx.WavesDSP.WASM.tfxX, null);
  assert.ok((ctx.WavesDSP.WASM.err || []).length >= 2, 'both failures recorded'); assert.ok(warned.some(w => /\[thunder\]/.test(w)));
});

test('waves: notes make sound, release ends it, and a voice limit is respected', () => {
  const { D } = engine({ wasm: false });
  const E = new D.WavesEngine(48000, { maxVoices: 2, quality: 'full', seed: 7, transport: TRANSPORT }); E.loadPatch(patchFor(D.MODULES, 'vco', defaults(D.MODULES.vco)));
  const run = n => { let e = 0; for (let b = 0; b < n; b++) { E.process(128); for (const v of E.bus(1)) e += v * v; } return e; };
  assert.equal(run(10), 0, 'silent before any note');
  E.noteOn(60, 1); assert.ok(run(40) > 0, 'a held note sounds');
  E.noteOn(64, 1); E.noteOn(67, 1); E.noteOn(71, 1);   // more notes than voices: must not throw or go non-finite
  assert.ok(Number.isFinite(run(20)), 'finite with more notes than voices');
  E.allNotesOff(); run(900); assert.ok(run(10) < 1e-6, 'silent again after all notes off and the release has died');
});

/* THE PROCESSOR WRAPPER. Everything above drives WavesEngine directly, through the page-style load (the only way waves publishes
   WavesDSP). WavesProcessor, which mixes the buses down and applies the soft() output limiter, only exists when the file is loaded
   inside an AudioWorklet, so it is loaded a second time in a worklet-style scope. The test notes are loud enough to reach the
   limiter's 0.8 knee; without that a missing or changed limiter is invisible. */
function driveProcessor(gain, blocks = 160) {
  const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'waves/waves-worklet.js'); assert.ok(ctx.__registered.waves, 'waves registers a processor in a worklet scope');
  const p = new ctx.__registered.waves({ processorOptions: { dspBytes: DSP, tfxBytes: TFX, maxVoices: 4, quality: 'full', transport: TRANSPORT } });
  const M = ctx.__registered.waves; assert.ok(M);
  // the processor's engine only exists inside the worklet's closure, so the patch and notes go in the way the host sends them: by message
  const doc = { modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } }, { id: 'e', type: 'adsr', scope: 'poly', params: { a: .005, d: .2, s: .7, r: .3 } }, { id: 'g', type: 'vca', scope: 'poly', params: { gain } }, { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }],
    cables: [{ from: ['h', 'pitch'], to: ['v', 'pitch'] }, { from: ['h', 'gate'], to: ['e', 'gate'] }, { from: ['v', 'out'], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] }, { from: ['g', 'out'], to: ['o', 'in'] }] };
  p.engine.loadPatch(doc);
  const L = new Float32Array(128 * blocks), R = new Float32Array(128 * blocks);
  for (let b = 0; b < blocks; b++) {
    if (b === 0) { p.engine.noteOn(60, 1); p.engine.noteOn(64, 1); p.engine.noteOn(67, 1); }
    const out = [[new Float32Array(128), new Float32Array(128)]]; p.process([[]], out, {}); L.set(out[0][0], b * 128); R.set(out[0][1], b * 128);
  }
  if (p.engine.quality !== 'full') throw new Error('engine shed to draft quality (machine too slow for a stable hash)');
  return { L, R };
}
const peakOf = a => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); return m; };

test('waves: WavesProcessor limits a hot mix below full scale, leaves a quiet one alone, and its output is golden', () => {
  const quiet = driveProcessor(.05), hot = driveProcessor(1);
  assert.ok(peakOf(quiet.L) > 0 && peakOf(quiet.L) < .8, 'a quiet mix stays under the knee (peak ' + peakOf(quiet.L).toFixed(3) + ')');
  assert.ok(peakOf(hot.L) <= 1 && peakOf(hot.L) > .9, 'a hot mix is held at, not above, full scale (peak ' + peakOf(hot.L).toFixed(4) + ')');
  const g = H.golden('waves-processor', { quiet: H.hashFloats([quiet.L, quiet.R]), hot: H.hashFloats([hot.L, hot.R]) });
  assert.ok(g.updated || g.diff.length === 0, 'processor output changed:\n  ' + (g.diff || []).join('\n  '));
});

test('waves: soft() is exact where the app depends on it (identity below the knee, bounded above, odd, continuous)', () => {
  const ctx = H.workletScope(); H.load(ctx, 'waves/waves-worklet.js', ['soft']); const soft = ctx.soft;
  for (const x of [0, .1, -.5, .79, -.8, .8]) assert.equal(soft(x), x);
  for (const x of [.81, 1, 2, 10, 1e6]) assert.ok(soft(x) > .8 && soft(x) < 1.0000001);
  assert.ok(Math.abs(soft(.8) - soft(.8 + 1e-9)) < 1e-6); for (const x of [.3, .9, 1.7]) assert.equal(soft(-x), -soft(x));
  assert.ok(Math.abs(soft(1) - 0.9523) < 1e-3, 'pinned value at 1.0 (' + soft(1).toFixed(5) + ')');
});
