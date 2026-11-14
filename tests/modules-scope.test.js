// SCOPE (the owner's module queue): a probe that draws its inputs. Engine side: the sweep, the trigger, Freeze, what show() sends,
// and that it runs in …chains while the editor watches although nothing comes out of it.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const N = 128, SR = 48000, W = 128;
const chainsD = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx.ChainsDSP; };
const wavesD = () => { const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  ctx.WavesDSP.wasmInit({ dspBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm'))), tfxBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))) }); return ctx.WavesDSP; };
function scope(D, over, linkB) {
  const m = D.MODULES.scope, p = {}; for (const [k, d] of Object.entries(m.params)) p[k] = d[0]; Object.assign(p, over);
  const st = m.create(SR), io = { in: { a: new Float32Array(N), b: new Float32Array(N), trig: new Float32Array(N) }, out: {}, p, linked: new Set(linkB ? ['a', 'b'] : ['a']) }; let at = 0;
  return { m, st, p, run(ins, blocks) { for (let b = 0; b < blocks; b++) { for (const k in io.in) { const s = ins[k]; for (let i = 0; i < N; i++) io.in[k][i] = s ? s(at + i) : 0; } m.process(st, io, { block: N, sr: SR }); at += N; } return m.show(st); } };
}
const sine = hz => i => Math.sin(2 * Math.PI * hz * i / SR);
const cols = v => { const a = []; for (let c = 0; c < W; c++) a.push({ lo: v[2 + c * 4], hi: v[3 + c * 4], lob: v[4 + c * 4], hib: v[5 + c * 4] }); return a; };

for (const [name, D] of [['chains', chainsD()], ['waves', wavesD()]]) {
  test(name + ': Scope is a probe with no outputs; show() is one reused list of plain numbers', () => {
    const m = D.MODULES.scope; assert.equal(m.probe, true); assert.deepEqual(Object.keys(m.outputs), []); assert.deepEqual(Object.keys(m.inputs), ['a', 'b', 'trig']);
    const S = scope(D, {}), v1 = S.run({ a: sine(100) }, 20), v2 = S.run({ a: sine(100) }, 20);
    assert.equal(v1, v2, 'the same list each time: nothing made per telemetry'); assert.equal(v1.length, 2 + W * 4); assert.ok(Array.isArray(v1)); for (const x of v1) assert.equal(typeof x, 'number');
    assert.doesNotThrow(() => JSON.stringify(v1)); assert.doesNotMatch(m.process.toString(), /new |\.slice\(|\.map\(/);
  });
  test(name + ': a sweep is Time long and each column holds the lowest and highest sample of its slice', () => {
    // 20 ms window, a 100 Hz sine: exactly two cycles across the 128 columns
    const S = scope(D, { time: .02, trig: 1, level: 0 }), v = S.run({ a: sine(100) }, 40), c = cols(v);
    assert.ok(v[0] >= 1, 'at least one sweep done'); assert.equal(v[1], 0, 'B not patched');
    let hi = -9, lo = 9; for (const k of c) { assert.ok(k.lo <= k.hi); hi = Math.max(hi, k.hi); lo = Math.min(lo, k.lo); } assert.ok(hi > .99 && lo < -.99);
    assert.ok(c[0].lo > -.1 && c[0].hi < .2, 'Rising through 0: the picture starts at the crossing (' + c[0].lo + ' .. ' + c[0].hi + ')');
    assert.ok(c[16].hi > .99, 'a quarter of a cycle in: the top'); assert.ok(c[48].lo < -.99, 'three quarters: the bottom'); assert.ok(c[64].lo > -.1 && c[64].hi < .2, 'one cycle: the crossing again');
    // a 10 kHz sine in a 100 ms window: every column is a full band, it does not alias into a slow wave
    const f = cols(scope(D, { time: .1, trig: 0 }).run({ a: sine(10000) }, 120)); for (const k of f) assert.ok(k.hi > .9 && k.lo < -.9);
  });
  test(name + ': Rising holds a steady wave still; Free does not; no trigger still sweeps after two windows', () => {
    const S = scope(D, { time: .01, trig: 1, level: .5 }), a = [...S.run({ a: sine(130) }, 30)], b = [...S.run({ a: sine(130) }, 37)];
    assert.ok(b[0] > a[0]); assert.ok(Math.abs(a[2] - b[2]) < .06 && Math.abs(a[3] - b[3]) < .06, 'both sweeps start where the wave climbs through 0.5'); assert.ok(a[3] >= .5 - .03 && a[2] <= .5 + .03);
    const F = scope(D, { time: .01, trig: 0 }), f1 = [...F.run({ a: sine(130) }, 30)], f2 = [...F.run({ a: sine(130) }, 37)]; assert.ok(Math.abs(f1[3] - f2[3]) > .06 || Math.abs(f1[2 + 200] - f2[2 + 200]) > .06, 'free: it wanders');
    const q = scope(D, { time: .01, trig: 1, level: .9 }).run({ a: () => .25 }, 40); assert.ok(q[0] >= 1, 'a level it never crosses: swept anyway'); assert.equal(q[2], .25); assert.equal(q[3], .25);
    const g = scope(D, { time: .01, trig: 2 }), v = g.run({ a: i => i / 48000, trig: i => i === 1000 ? 1 : 0 }, 12); assert.ok(Math.abs(v[2] - 1000 / 48000) < .002, 'Gate: the sweep starts at the pulse on Trig');
  });
  test(name + ': B is a second trace on the same time base; Freeze keeps the picture', () => {
    const S = scope(D, { time: .02, trig: 0 }, true), v = S.run({ a: () => .5, b: () => -.25 }, 40), c = cols(v);
    assert.equal(v[1], 1); for (const k of c) { assert.equal(k.lo, .5); assert.equal(k.hi, .5); assert.equal(k.lob, -.25); assert.equal(k.hib, -.25); }
    S.p.freeze = 1; const seq = v[0], w = S.run({ a: () => -1, b: () => 1 }, 40); assert.equal(w[0], seq); assert.equal(w[2], .5);
    S.p.freeze = 0; const x = S.run({ a: () => -1, b: () => 1 }, 40); assert.ok(x[0] > seq); assert.equal(x[2], -1);
    const bad = scope(D, { time: .005, trig: 0 }).run({ a: () => NaN }, 20); for (const y of bad) assert.ok(Number.isFinite(y), 'never a not-a-number to the page');
  });
}

test('chains engine: an unpatched-output Scope runs with what feeds it while the editor watches, and is left out when it does not', () => {
  const D = chainsD(), E = new D.ChainsEngine(SR, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  const m = (id, type, params) => ({ id, type, scope: 'mono', params });
  E.loadPatch({ modules: [m('a', 'trackIn', { track: 1 }), m('k', 'const', { value: .5, fine: 0 }), m('s', 'scope', { time: .005, trig: 0, level: 0, freeze: 0, zoom: 1 }), m('c', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['a', 'out'], to: ['c', 'in'] }, { from: ['k', 'out'], to: ['s', 'a'] }] });
  const z = new Float32Array(N);
  assert.ok(!E._active().has('s') && !E._active().has('k'), 'editor closed: the Scope and its Constant cost nothing');
  E.handleMessage({ op: 'telemetry', value: true });
  assert.ok(E._active().has('s') && E._active().has('k'), 'editor open: both run');
  for (let b = 0; b < 20; b++) { E.setTrackInputs(z, z); E.process(N); }
  const t = E.telemetry(), sh = t.shows.find(x => x[0] === 's'); assert.ok(sh, 'the Scope reports'); assert.ok(sh[1][0] >= 1); assert.equal(sh[1][2], .5); assert.equal(sh[1][3], .5);
});

test('waves engine: the Scope reports through telemetry there too', () => {
  const D = wavesD(), E = new D.WavesEngine(SR, { voices: 4, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch({ modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } },
    { id: 's', type: 'scope', scope: 'mono', params: { time: .01, trig: 1, level: 0, freeze: 0, zoom: 1 } }, { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }],
    cables: [{ from: ['h', 'pitch'], to: ['v', 'pitch'] }, { from: ['v', 'out'], to: ['s', 'a'] }, { from: ['v', 'out'], to: ['o', 'in'] }] });
  E.handleMessage({ op: 'telemetry', value: true }); E.noteOn(60, 1);
  for (let b = 0; b < 60; b++) E.process(N);
  const sh = E.telemetry().shows.find(x => x[0] === 's'); assert.ok(sh && sh[1][0] >= 1); let hi = 0; for (let i = 2; i < sh[1].length; i++) hi = Math.max(hi, Math.abs(sh[1][i])); assert.ok(hi > .3, 'the oscillator shows (' + hi + ')');
});
