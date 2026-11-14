// CONTROL MATHS (the owner's module queue, group 1): Invert / rectify, Scale / map, Comparator, Switch, Crossfade, Constant.
// Each is run directly (its own process on known input) for exact values, in both worklets, then once through each engine.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const N = 128, SR = 48000;
const chainsD = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx.ChainsDSP; };
const wavesD = () => { const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  ctx.WavesDSP.wasmInit({ dspBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm'))), tfxBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))) }); return ctx.WavesDSP; };
const BOTH = [['chains', chainsD()], ['waves', wavesD()]];
const TYPES = ['rect', 'scale', 'compare', 'switch', 'xfade', 'const'];
/* Run a module's process over `blocks` blocks. ins: { port: fn(i) or Float32Array }. Returns { port: Float32Array }. */
function run(D, type, over, ins, blocks = 1) {
  const m = D.MODULES[type], p = {}; for (const [k, d] of Object.entries(m.params)) p[k] = d[0]; Object.assign(p, over);
  const st = m.create(SR), io = { in: {}, out: {}, p }, rec = {};
  for (const k in m.inputs) io.in[k] = new Float32Array(N); for (const k in m.outputs) { io.out[k] = new Float32Array(N); rec[k] = new Float32Array(N * blocks); }
  for (let b = 0; b < blocks; b++) {
    for (const k in io.in) { const s = ins && ins[k]; for (let i = 0; i < N; i++) io.in[k][i] = !s ? 0 : typeof s === 'function' ? s(b * N + i) : s[b * N + i]; }
    m.process(st, io, { block: N, sr: SR });
    for (const k in rec) rec[k].set(io.out[k], b * N);
  }
  return rec;
}
const ramp = i => (i / (N - 1)) * 2 - 1, f = Math.fround;

for (const [name, D] of BOTH) {
  test(name + ': the six modules are registered, per-voice capable, with the ports the editor shows', () => {
    for (const t of TYPES) { assert.ok(D.MODULES[t], t); assert.equal(D.MODULES[t].type, t); assert.equal(D.MODULES[t].scope, 'poly'); }
    assert.deepEqual(Object.keys(D.MODULES.rect.outputs), ['inv', 'abs', 'pos', 'neg']);
    assert.deepEqual(Object.keys(D.MODULES.compare.outputs), ['gt', 'lt']);
    assert.deepEqual(Object.keys(D.MODULES.switch.inputs), ['a', 'b', 'sel']);
    assert.deepEqual(Object.keys(D.MODULES.xfade.inputs), ['a', 'b', 'cv']);
  });
  test(name + ': Invert / rectify', () => {
    const r = run(D, 'rect', {}, { in: ramp });
    for (let i = 0; i < N; i++) { const x = f(ramp(i)); assert.equal(r.inv[i], -x); assert.equal(r.abs[i], Math.abs(x)); assert.equal(r.pos[i], Math.max(x, 0)); assert.equal(r.neg[i], Math.max(-x, 0)); assert.equal(f(r.pos[i] - r.neg[i]), x); }
  });
  test(name + ': Scale / map', () => {
    let r = run(D, 'scale', {}, { in: ramp });   // default: -1..1 becomes 0..1
    assert.equal(r.out[0], 0); assert.equal(r.out[N - 1], 1); assert.ok(Math.abs(r.out[64] - (f(ramp(64)) + 1) / 2) < 1e-6);
    r = run(D, 'scale', { ilo: 0, ihi: 1, olo: 1, ohi: -1 }, { in: i => i / (N - 1) });   // turned over
    assert.equal(r.out[0], 1); assert.equal(r.out[N - 1], -1);
    r = run(D, 'scale', { ilo: 0, ihi: .5, olo: 0, ohi: .5, clip: 1 }, { in: ramp }); assert.equal(r.out[0], 0); assert.equal(r.out[N - 1], .5);   // held in the Out range
    r = run(D, 'scale', { ilo: 0, ihi: .5, olo: 0, ohi: .5, clip: 0 }, { in: ramp }); assert.equal(r.out[0], -1); assert.equal(r.out[N - 1], 1);   // free
    r = run(D, 'scale', { ilo: .3, ihi: .3, olo: .25, ohi: 1 }, { in: ramp }); for (const v of r.out) assert.equal(v, .25);   // no input range: Out low, never a division by nothing
  });
  test(name + ': Comparator, with a dead band', () => {
    let r = run(D, 'compare', { hyst: 0 }, { a: ramp });
    for (let i = 0; i < N; i++) { assert.equal(r.gt[i], ramp(i) > 0 ? 1 : 0); assert.equal(r.lt[i], 1 - r.gt[i]); }
    r = run(D, 'compare', { hyst: 0, thr: .5 }, { a: ramp, b: () => -.25 }); for (let i = 0; i < N; i++) assert.equal(r.gt[i], f(ramp(i)) - f(-.25) - .5 > 0 ? 1 : 0, 'A against B plus Level');
    // a signal wobbling by 0.05 around the level: no chatter with a 0.2 band, plenty without
    const wob = i => .05 * Math.sin(i * 1.3), flips = g => { let n = 0; for (let i = 1; i < g.length; i++) if (g[i] !== g[i - 1]) n++; return n; };
    assert.ok(flips(run(D, 'compare', { hyst: 0 }, { a: wob }, 4).gt) > 50);
    assert.equal(flips(run(D, 'compare', { hyst: .2 }, { a: wob }, 4).gt), 0);
    // and it does go high and low across the band
    const big = run(D, 'compare', { hyst: .2 }, { a: i => Math.sin(i * .05) }, 4).gt; assert.ok(flips(big) >= 6);
    const nan = run(D, 'compare', {}, { a: () => NaN }); for (const v of nan.gt) assert.equal(v, 0);
  });
  test(name + ': Switch, click-free', () => {
    const a = () => .25, b = () => -.5;
    let r = run(D, 'switch', { pick: 0 }, { a, b }); for (const v of r.out) assert.equal(v, .25);
    r = run(D, 'switch', { pick: 1 }, { a, b }); for (const v of r.out) assert.equal(v, -.5);   // no fade in at the start
    r = run(D, 'switch', { pick: 0 }, { a, b, sel: () => 1 }); for (const v of r.out) assert.equal(v, -.5);   // the gate picks the other one
    r = run(D, 'switch', { pick: 1 }, { a, b, sel: () => 1 }); for (const v of r.out) assert.equal(v, .25);
    // a gate half way through: with Fade 0 the change is one sample; with 5 ms it takes 240 samples and never jumps
    const sel = i => i >= 300 ? 1 : 0;
    r = run(D, 'switch', { fade: 0 }, { a, b, sel }, 5); assert.equal(r.out[299], .25); assert.equal(r.out[300], -.5);
    r = run(D, 'switch', { fade: .005 }, { a, b, sel }, 5);
    assert.equal(r.out[299], .25); assert.equal(r.out[300 + 240], -.5); assert.ok(r.out[300 + 120] < 0 && r.out[300 + 120] > -.3);
    let jump = 0; for (let i = 1; i < r.out.length; i++) jump = Math.max(jump, Math.abs(r.out[i] - r.out[i - 1])); assert.ok(jump <= .75 / 240 + 1e-6, 'largest step ' + jump);
  });
  test(name + ': Crossfade, straight and equal power, with the Mix input', () => {
    const a = () => 1, b = () => -1;
    let r = run(D, 'xfade', { mix: 0 }, { a, b }); for (const v of r.out) assert.equal(v, 1);
    r = run(D, 'xfade', { mix: 1 }, { a, b }); for (const v of r.out) assert.equal(v, -1);
    r = run(D, 'xfade', { mix: .5 }, { a, b }); for (const v of r.out) assert.equal(v, 0);
    r = run(D, 'xfade', { mix: 0 }, { a, b, cv: i => i / (N - 1) }); assert.equal(r.out[0], 1); assert.equal(r.out[N - 1], -1); for (let i = 1; i < N; i++) assert.ok(r.out[i] < r.out[i - 1], 'the input moves it sample by sample');
    r = run(D, 'xfade', { mix: .5 }, { a, b, cv: i => i < 64 ? -9 : 9 }); assert.equal(r.out[0], 1); assert.equal(r.out[N - 1], -1);   // held at the ends
    r = run(D, 'xfade', { mix: .5, curve: 1 }, { a: () => 1, b: () => 1 }); assert.ok(Math.abs(r.out[0] - Math.SQRT2) < 1e-6, 'equal power: both at 0.707');
    r = run(D, 'xfade', { mix: 0, curve: 1 }, { a, b }); assert.ok(Math.abs(r.out[0] - 1) < 1e-6); r = run(D, 'xfade', { mix: 1, curve: 1 }, { a, b }); assert.ok(Math.abs(r.out[0] + 1) < 1e-6);
    r = run(D, 'xfade', { mix: .5 }, { a, b, cv: () => NaN }); for (const v of r.out) assert.equal(v, 1);   // a broken Mix input falls to A, it does not poison the output
  });
  test(name + ': Constant', () => {
    let r = run(D, 'const', { value: .25 }); for (const v of r.out) assert.equal(v, .25);
    r = run(D, 'const', { value: -1, fine: .005 }); for (const v of r.out) assert.equal(v, f(-.995));
  });
  test(name + ': none of the six makes garbage per block', () => {
    for (const t of TYPES) { const src = D.MODULES[t].process.toString(); assert.doesNotMatch(src, /new |\[\s*\.\.\.|\.map\(|\.slice\(|=>\s*\{/, t); }
  });
}

test('chains engine: a Constant through Scale / map into Chain Out, and a Comparator switching a Switch', () => {
  const D = BOTH[0][1], E = new D.ChainsEngine(SR, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  const m = (id, type, params) => ({ id, type, scope: 'mono', params });
  E.loadPatch({ modules: [m('k', 'const', { value: .5, fine: 0 }), m('s', 'scale', { ilo: 0, ihi: 1, olo: 0, ohi: -1, clip: 1 }), m('c', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['k', 'out'], to: ['s', 'in'] }, { from: ['s', 'out'], to: ['c', 'in'] }] });
  E.setTrackInputs(new Float32Array(N), new Float32Array(N)); E.process(N); for (const v of E.bus(1)) assert.equal(v, -.5);
  E.loadPatch({ modules: [m('a', 'trackIn', { track: 1 }), m('k', 'const', { value: .75, fine: 0 }), m('q', 'compare', { thr: 0, hyst: 0 }), m('w', 'switch', { pick: 0, fade: 0 }), m('c', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['a', 'out'], to: ['q', 'a'] }, { from: ['a', 'out'], to: ['w', 'a'] }, { from: ['k', 'out'], to: ['w', 'b'] }, { from: ['q', 'gt'], to: ['w', 'sel'] }, { from: ['w', 'out'], to: ['c', 'in'] }] });
  const x = new Float32Array(N); for (let i = 0; i < N; i++) x[i] = Math.sin(i * .2) * .5;
  E.setTrackInputs(x, x); E.process(N); const o = E.bus(1);
  for (let i = 0; i < N; i++) assert.equal(o[i], x[i] > 0 ? .75 : x[i], 'the positive half replaced by the constant, at sample ' + i);
});

test('waves engine: fed by a voice the maths run per voice; a Constant alone is shared', () => {
  const D = wavesD(), E = new D.WavesEngine(SR, { voices: 4, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch({ modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } },
    { id: 'r', type: 'rect', scope: 'poly', params: {} }, { id: 'k', type: 'const', scope: 'poly', params: { value: .5, fine: 0 } }, { id: 'x', type: 'xfade', scope: 'poly', params: { mix: 0, curve: 0 } },
    { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }],
    cables: [{ from: ['h', 'pitch'], to: ['v', 'pitch'] }, { from: ['v', 'out'], to: ['r', 'in'] }, { from: ['r', 'abs'], to: ['x', 'a'] }, { from: ['k', 'out'], to: ['x', 'cv'] }, { from: ['x', 'out'], to: ['o', 'in'] }] });
  const g = E.graph.mods; assert.equal(g.get('r').poly, true); assert.equal(g.get('x').poly, true); assert.equal(g.get('k').poly, false);
  E.noteOn(60, 1); E.noteOn(67, 1);
  let e = 0, neg = 0; for (let b = 0; b < 40; b++) { E.process(N); for (const v of E.bus(1)) { assert.ok(Number.isFinite(v)); e += v * v; if (v < -1e-6) neg++; } }
  assert.ok(e > 0, 'sound comes out'); assert.equal(neg, 0, 'rectified per voice (half of it, the Constant holding the mix at 0.5 against silence): never negative');
});
