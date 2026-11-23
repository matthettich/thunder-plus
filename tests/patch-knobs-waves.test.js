// Patch knobs, step 2 (22 Nov): …waves knobs. k1-k8 on the thunder-wlive node (WavesLiveProc), the page player's knSt, and the core's chc handler for __wl nodes.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path');
const { spawnSync } = require('node:child_process');
const H = require('./helpers');

const src = H.read('index.html'), coreSrc = H.read('core/seq-core.js');
const DSP = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm'))), TFX = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm')));
const wlCtx = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'waves/waves-worklet.js'); return ctx; };
const approx = (a, b, e = 1e-6) => assert.ok(Math.abs(a - b) < e, `${a} vs ${b}`);

test('thunder-wlive: parameterDescriptors keep the lane params and bpm, and add k1-k8 (k-rate, default -1, -1..1)', () => {
  const ctx = wlCtx(), C = ctx.__registered['thunder-wlive']; assert.ok(C, 'registered');
  const d = JSON.parse(JSON.stringify(C.parameterDescriptors)), by = Object.fromEntries(d.map(x => [x.name, x]));
  for (let j = 0; j < 4; j++) {
    assert.deepEqual(by['r' + j], { name: 'r' + j, defaultValue: 1, minValue: 0, maxValue: 256, automationRate: 'a-rate' });
    assert.deepEqual(by['g' + j], { name: 'g' + j, defaultValue: 0, minValue: 0, maxValue: 1, automationRate: 'a-rate' });
    assert.deepEqual(by['t' + j], { name: 't' + j, defaultValue: 0, automationRate: 'a-rate' });
    assert.deepEqual(by['l' + j], { name: 'l' + j, defaultValue: 0, minValue: 0, maxValue: 1, automationRate: 'a-rate' });
    assert.deepEqual(by['v' + j], { name: 'v' + j, defaultValue: 100, minValue: 1, maxValue: 127, automationRate: 'a-rate' });
  }
  assert.deepEqual(by.bpm, { name: 'bpm', defaultValue: 120, minValue: 10, maxValue: 999, automationRate: 'k-rate' });
  for (let k = 1; k <= 8; k++) assert.deepEqual(by['k' + k], { name: 'k' + k, defaultValue: -1, minValue: -1, maxValue: 1, automationRate: 'k-rate' });
  assert.equal(d.length, 4 * 5 + 1 + 8);
  assert.equal(new Set(d.map(x => x.name)).size, d.length, 'no duplicate names');
});

test('WavesLiveProc: e.knobs is Float32Array(9) of -1, __wl is set, and a patched Knob follows the k params on the output bus', () => {
  const ctx = wlCtx(); ctx.WavesDSP; const C = ctx.__registered['thunder-wlive'];
  const mod = (id, type, params) => ({ id, type, scope: 'mono', params: params || {} });
  const doc = { format: 'waves.patch', version: 1, modules: [mod('k', 'knob', { n: 2, value: .5, min: 0, max: 1 }), mod('o', 'out', { slot: 1, level: 1 })], cables: [{ from: ['k', 'out'], to: ['o', 'in'] }] };
  const p = new C({ processorOptions: { dspBytes: DSP, tfxBytes: TFX, doc, p: { bus: 1, level: 0, drive: 0 } } });
  assert.equal(p.__wl, true);
  assert.ok(p.e.knobs instanceof Float32Array && p.e.knobs.length === 9 && p.e.knobs.every(v => v === -1));
  const P = o => { const q = { bpm: [120] }; for (let k = 1; k <= 8; k++) q['k' + k] = [k in o ? o[k] : -1]; for (let j = 0; j < 4; j++) { q['t' + j] = [0]; q['g' + j] = [0]; q['l' + j] = [0]; q['r' + j] = [1]; q['v' + j] = [100]; } return q; };
  const mean = o => { let m = 0; for (let b = 0; b < 20; b++) { p.quiet = 0; const out = [[new Float32Array(128), new Float32Array(128)]]; p.process([[]], out, P(o)); m = out[0][0].reduce((a, v) => a + v, 0) / 128; } return m; };
  const base = mean({});
  assert.ok(base > .1, 'Value .5 gives a steady level (' + base + ')');
  const hi = mean({ 2: 1 }); assert.ok(hi > base * 1.5, `k2 = 1 is louder (${base} -> ${hi})`);
  approx(p.e.knobs[2], 1); approx(p.e.knobs[1], -1);
  const other = mean({ 5: 0 }); approx(other, base, 1e-4);   // another knob does not move it
  const lo = mean({ 2: 0 }); assert.ok(Math.abs(lo) < 1e-4, 'k2 = 0 is silent (0 is "set")');
  approx(mean({}), base, 1e-4);   // back to -1: the Value again
  const wmean = mean({ 2: 1 }); approx(wmean, hi, 1e-4);
});

test('index.html makePlayer: knSt is kept per track, applied to live nodes on chc, to new nodes in wlNode, and cleared on stop', () => {
  assert.ok(src.includes('const knSt = new Map();'));
  assert.ok(/if \(ev\.type === 'chc'\)\{[^\n]*\n\s*if \(ev\.p\[0\] === 'k'\)\{ const K = knSt\.get\(ev\.track\) \|\| new Float32Array\(9\)\.fill\(-1\); K\[\+ev\.p\.slice\(1\)\] = ev\.v; knSt\.set\(ev\.track, K\);/.test(src), 'chc records knSt');
  assert.ok(/for \(const \[k, o\] of bassN\) if \(o\.wl && k\.startsWith\(ev\.track \+ ':'\)\)\{ const p = o\.node\.parameters\.get\(ev\.p\); if \(p\) try \{ p\.setValueAtTime\(ev\.v, at\)/.test(src), 'chc sets wl nodes of the track');
  assert.ok(/const K = knSt\.get\(t\); if \(K\) for \(let k = 1; k <= 8; k\+\+\) if \(K\[k\] >= 0\)\{ const q = P\('k' \+ k\); if \(q\) q\.value = K\[k\]; \}/.test(src), 'wlNode applies knSt');
  assert.ok(/knSt\.clear\(\); for \(const o of bassN\.values\(\)\) if \(o\.wl && o\.node && o\.node\.parameters\) for \(let k = 1; k <= 8; k\+\+\)\{ const q = o\.node\.parameters\.get\('k' \+ k\); if \(q\) try \{ q\.cancelScheduledValues\(at\); q\.setValueAtTime\(-1, at\)/.test(src), 'stop resets to -1');
});

test('core: source has knSt/knTouched, the chc k handler, lnode catch-up and the stopAll reset', () => {
  for (const re of [/this\.knSt = new Array\(NT\)\.fill\(null\); this\.knTouched = new Set\(\)/, /p\.__wl && key\.startsWith\(pre\)\)\{ this\.knPut\(p, key, ev\.p, ev\.v, at\)/,
    /const K = proc\.__wl && this\.knSt\[t\]; if \(K\) for \(let k = 1; k <= 8; k\+\+\) if \(K\[k\] >= 0\) this\.knPut\(/, /this\.knTouched\.clear\(\); this\.knSt\.fill\(null\)/]) assert.match(coreSrc, re);
});

function coreWith() {
  const C = H.workletScope(); vm.runInContext(coreSrc, C, { filename: 'seq-core.js' });
  C.__tcNodes = new Map(); vm.runInContext('TFX = {};', C);
  const core = new C.__registered['thunder-core'](); core.kv[5] = { lk: { kind: 'bass', vox: false, ps: false } };
  return { C, core };
}
test('core chc: k params go to the chains node and the track\'s __wl nodes only; b params only the chains node; unhandled is false', () => {
  const { C, core } = coreWith(), at = (A, t) => A.at(t);
  const chn = {}, w1 = { __wl: true }, w2 = { __wl: true }, otherTrack = { __wl: true }, notWl = {};
  C.__tcNodes.set('chn:1', chn); C.__tcNodes.set('1:5:bass', w1); C.__tcNodes.set('1:6:bass', w2); C.__tcNodes.set('2:5:bass', otherTrack); C.__tcNodes.set('1:7:bass', notWl);
  C.currentTime = 1;
  assert.equal(core.ev({ type: 'chc', track: 1, t: 2, p: 'k3', v: .6 }), true);
  for (const n of [chn, w1, w2]) approx(at(n.__ov.p.get('k3'), 2.1), .6);
  assert.equal(otherTrack.__ov, undefined, 'another track untouched'); assert.equal(notWl.__ov, undefined, 'a non-waves node untouched');
  approx(core.knSt[1][3], .6); assert.equal(core.knSt[1][1], -1);
  assert.equal(core.ev({ type: 'chc', track: 1, t: 2, p: 'b4', v: 1 }), true);
  assert.equal(w1.__ov.p.get('b4'), undefined, 'b params never go to waves nodes'); approx(at(chn.__ov.p.get('b4'), 2.1), 1);
  // no chains node, no waves nodes: nothing handled, but a k still remembered
  const e = coreWith(); assert.equal(e.core.ev({ type: 'chc', track: 3, t: 2, p: 'k2', v: .25 }), false); approx(e.core.knSt[3][2], .25);
  assert.equal(e.core.ev({ type: 'chc', track: 3, t: 2, p: 'b2', v: 1 }), false);
  assert.equal(e.core.knSt[3][1], -1);
});
test('core: a waves node met after a KN gets it (lnode); stopAll resets k1-k8 to -1 and forgets', () => {
  const { C, core } = coreWith(), at = (A, t) => A.at(t), w = { __wl: true };
  C.currentTime = 1;
  core.ev({ type: 'chc', track: 1, t: 1, p: 'k2', v: .8 });
  C.__tcNodes.set('1:5:bass', w); assert.equal(w.__ov, undefined);
  assert.ok(core.lnode(1, 5, 0));
  approx(at(w.__ov.p.get('k2'), 1.5), .8); assert.equal(w.__ov.p.get('k1'), undefined, 'only the set knobs');
  assert.ok(core.knTouched.has('1:5:bass'));
  core.stopAll(3);
  for (const k of [2]) assert.equal(at(w.__ov.p.get('k' + k), 3.01), -1);
  assert.equal(core.knTouched.size, 0); assert.equal(core.knSt[1], null);
  assert.ok(w.__ov.until <= 3 + 4 + 1e-9, 'the page has the node back (not held open forever)');
  // a node that never got a knob is never woken
  const f = coreWith(), v = { __wl: true }; f.C.__tcNodes.set('1:5:bass', v); f.core.lnode(1, 5, 0); f.core.stopAll(2); assert.equal(v.__ov, undefined);
});

test('core/seq-core.js is current (tools/make-core.mjs --check)', () => {
  const r = spawnSync(process.execPath, ['tools/make-core.mjs', '--check'], { cwd: H.ROOT, encoding: 'utf8' });
  assert.equal(r.status, 0, r.stdout + r.stderr);
});
