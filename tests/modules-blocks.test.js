// AUDIO BUILDING BLOCKS (the owner's module queue): One-pole filter, Allpass (Phase / Echo), DC blocker, Mid / side in both
// editors' engines; an Oscillator and a Noise source in …chains (…waves has its own).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const N = 128, SR = 48000;
const chainsD = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx.ChainsDSP; };
const wavesD = () => { const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  ctx.WavesDSP.wasmInit({ dspBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm'))), tfxBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))) }); return ctx.WavesDSP; };
const C = chainsD(), BOTH = [['chains', C], ['waves', wavesD()]];
function run(D, type, over, ins, blocks = 1, links) {
  const m = D.MODULES[type], p = {}; for (const [k, d] of Object.entries(m.params)) p[k] = d[0]; Object.assign(p, over);
  const st = m.create(SR), io = { in: {}, out: {}, p, linked: new Set(links || Object.keys(ins || {})) }, rec = {};
  for (const k in m.inputs) io.in[k] = new Float32Array(N); for (const k in m.outputs) { io.out[k] = new Float32Array(N); rec[k] = new Float32Array(N * blocks); }
  for (let b = 0; b < blocks; b++) { for (const k in io.in) { const s = ins && ins[k]; for (let i = 0; i < N; i++) io.in[k][i] = s ? s(b * N + i) : 0; } m.process(st, io, { block: N, sr: SR, transport: { tempo: 120 } }); for (const k in rec) rec[k].set(io.out[k], b * N); }
  return rec;
}
const sine = (hz, a = 1) => i => a * Math.sin(2 * Math.PI * hz * i / SR);
const rms = (x, from = 0) => { let e = 0; for (let i = from; i < x.length; i++) e += x[i] * x[i]; return Math.sqrt(e / (x.length - from)); };
const db = v => 20 * Math.log10(v);
const noG = (D, types) => { for (const t of types) assert.doesNotMatch(D.MODULES[t].process.toString(), /new |\[\s*\.\.\.|\.map\(|\.slice\(|=>\s*\{/, t + ' makes garbage per block'); };

for (const [name, D] of BOTH) {
  test(name + ': the four are registered, per-voice capable, and make nothing per block', () => {
    for (const t of ['onepole', 'allpass', 'dcblock', 'midside']) { assert.ok(D.MODULES[t], t); assert.equal(D.MODULES[t].scope, 'poly'); }
    noG(D, ['onepole', 'allpass', 'dcblock', 'midside']);
  });
  test(name + ': One-pole: -3 dB at Freq, 6 dB an octave, and Low + High is the input', () => {
    const at = hz => { const r = run(D, 'onepole', { freq: 1000 }, { in: sine(hz) }, 80); return { lp: db(rms(r.lp, 4800) / Math.SQRT1_2), hp: db(rms(r.hp, 4800) / Math.SQRT1_2) }; };
    assert.ok(Math.abs(at(1000).lp + 3) < .4, 'low at the corner: ' + at(1000).lp); assert.ok(Math.abs(at(1000).hp + 3) < .8, 'high at the corner: ' + at(1000).hp);
    assert.ok(Math.abs(at(100).lp) < .2); assert.ok(Math.abs((at(4000).lp - at(8000).lp) - 6) < 1.2, 'an octave up, about 6 dB down');
    assert.ok(Math.abs((at(125).hp - at(62.5).hp) - 6) < .6);
    const r = run(D, 'onepole', { freq: 700 }, { in: H.testSignal ? (s => i => s[i])(H.testSignal(N * 20)) : sine(333) }, 20); for (let i = 0; i < r.lp.length; i++) assert.ok(Math.abs(r.lp[i] + r.hp[i] - (H.testSignal(N * 20)[i])) < 1e-6);
    const bad = run(D, 'onepole', {}, { in: i => i === 5 ? NaN : 0 }, 3); assert.ok(Number.isFinite(bad.lp[N * 2 + 5]), 'a not-a-number does not stay in it');
  });
  test(name + ': Allpass, Phase: the same level at every frequency, a quarter turn at Freq', () => {
    for (const hz of [100, 1000, 5000]) { const r = run(D, 'allpass', { mode: 0, freq: 1000 }, { in: sine(hz) }, 60); assert.ok(Math.abs(db(rms(r.out, 4800) / Math.SQRT1_2)) < .05, hz + ' Hz passes whole'); }
    const r = run(D, 'allpass', { mode: 0, freq: 1000 }, { in: sine(1000) }, 60); let dot = 0; for (let i = 4800; i < r.out.length; i++) dot += r.out[i] * Math.sin(2 * Math.PI * 1000 * i / SR);
    assert.ok(Math.abs(dot / (r.out.length - 4800)) < .01, 'at Freq it is a quarter turn away from the input');
  });
  test(name + ': Allpass, Echo: an impulse comes out as -Gain, then echoes Time apart; a tone keeps its level', () => {
    const r = run(D, 'allpass', { mode: 1, time: .005, gain: .5 }, { in: i => i === 0 ? 1 : 0 }, 12);   // 240 samples
    assert.equal(r.out[0], -.5); assert.equal(r.out[240], Math.fround(.75)); assert.equal(r.out[480], Math.fround(.375)); assert.equal(r.out[100], 0);
    let e = 0; for (const v of run(D, 'allpass', { mode: 1, time: .005, gain: .5 }, { in: i => i === 0 ? 1 : 0 }, 200).out) e += v * v; assert.ok(Math.abs(e - 1) < 1e-3, 'all of the impulse comes out: ' + e);
    const t = run(D, 'allpass', { mode: 1, time: .0037, gain: -.7 }, { in: sine(440) }, 300); assert.ok(Math.abs(db(rms(t.out, 24000) / Math.SQRT1_2)) < .05);
    const hot = run(D, 'allpass', { mode: 1, time: .001, gain: .95 }, { in: i => i < 10 ? 1e9 : i === 20 ? NaN : 0 }, 40); for (const v of hot.out) assert.ok(Number.isFinite(v));
  });
  test(name + ': DC blocker: an offset goes, the sound stays', () => {
    const r = run(D, 'dcblock', { freq: 10 }, { in: i => .5 + .25 * Math.sin(2 * Math.PI * 440 * i / SR) }, 400);
    let mean = 0; const from = N * 300; for (let i = from; i < r.out.length; i++) mean += r.out[i]; mean /= r.out.length - from;
    assert.ok(Math.abs(mean) < .002, 'offset left: ' + mean); assert.ok(Math.abs(db(rms(r.out, from) / (.25 * Math.SQRT1_2))) < .05);
  });
  test(name + ': Mid / side: exact there and back; a mono input has no side', () => {
    const L = sine(300, .7), R = sine(470, .4), e = run(D, 'midside', { mode: 0 }, { in: L, inR: R }, 2);
    for (let i = 0; i < N * 2; i++) { assert.equal(e.out[i], Math.fround((Math.fround(L(i)) + Math.fround(R(i))) * .5)); assert.equal(e.outR[i], Math.fround((Math.fround(L(i)) - Math.fround(R(i))) * .5)); }
    const d = run(D, 'midside', { mode: 1 }, { in: i => e.out[i], inR: i => e.outR[i] }, 2); for (let i = 0; i < N * 2; i++) { assert.ok(Math.abs(d.out[i] - L(i)) < 1e-6); assert.ok(Math.abs(d.outR[i] - R(i)) < 1e-6); }
    const mono = run(D, 'midside', { mode: 0 }, { in: L }, 1, ['in']); for (let i = 0; i < N; i++) { assert.equal(mono.outR[i], 0); assert.equal(mono.out[i], Math.fround(L(i))); }
  });
}

test('chains: Oscillator: the pitch it says, four waves, FM in octaves, Sync, a slow wave for control', () => {
  assert.ok(C.MODULES.osc && C.MODULES.wnoise); noG(C, ['osc', 'wnoise']);
  const cross = x => { let n = 0; for (let i = 1; i < x.length; i++) if (x[i - 1] < 0 && x[i] >= 0) n++; return n; };
  for (const w of [0, 1, 2, 3]) { const r = run(C, 'osc', { wave: w, freq: 440, level: 1 }, {}, 375); assert.ok(Math.abs(cross(r.out) - 440) <= 1, 'wave ' + w + ': ' + cross(r.out) + ' cycles in a second'); let pk = 0; for (const v of r.out) { assert.ok(Number.isFinite(v)); pk = Math.max(pk, Math.abs(v)); } assert.ok(pk > .95 && pk < 1.3, 'wave ' + w + ' peak ' + pk); }
  assert.ok(Math.abs(rms(run(C, 'osc', { wave: 0, freq: 440, level: .5 }, {}, 375).out) - .5 * Math.SQRT1_2) < .002, 'Level scales it');
  const up = run(C, 'osc', { wave: 0, freq: 220, fm: .2, level: 1 }, { fm: () => 1 }, 375); assert.ok(Math.abs(cross(up.out) - 440) <= 1, 'FM depth 20% and 1 at the input: one octave up');
  const off = run(C, 'osc', { wave: 0, freq: 220, fm: 0, level: 1 }, { fm: () => 1 }, 375); assert.ok(Math.abs(cross(off.out) - 220) <= 1);
  const sy = run(C, 'osc', { wave: 2, freq: 100, level: 1 }, { sync: i => i === 300 ? 1 : 0 }, 4); assert.ok(sy.out[301] < -.9 && sy.out[299] > -.4, 'Sync starts the saw again (the edge itself is rounded off, one sample)');
  const lfo = run(C, 'osc', { wave: 1, freq: .5, level: 1 }, {}, 750); assert.ok(Math.abs(lfo.out[0] + 1) < 1e-3 && Math.abs(lfo.out[48000] - 1) < 1e-2, 'half a hertz: bottom to top in a second');
  // a saw at 5 kHz does not fold much back below it (band-limited edges)
  const saw = run(C, 'osc', { wave: 2, freq: 5000, level: 1 }, {}, 375).out; let lowE = 0; { let y = 0; const k = 1 - Math.exp(-2 * Math.PI * 1000 / SR); for (const v of saw) { y += (v - y) * k; lowE += y * y; } } assert.ok(Math.sqrt(lowE / saw.length) < .12, 'little below 1 kHz: ' + Math.sqrt(lowE / saw.length));
});
test('chains: Noise: three colours, each its own run of numbers, level kept in bounds', () => {
  const slope = c => { const x = run(C, 'wnoise', { color: c, level: 1 }, {}, 400).out; let lo = 0, y = 0; const k = 1 - Math.exp(-2 * Math.PI * 200 / SR); for (const v of x) { assert.ok(Number.isFinite(v) && Math.abs(v) <= 4); y += (v - y) * k; lo += y * y; } return { all: rms(x), low: Math.sqrt(lo / x.length) }; };
  const w = slope(0), p = slope(1), b = slope(2);
  assert.ok(Math.abs(w.all - 1 / Math.sqrt(3)) < .02, 'white: even between -1 and 1'); assert.ok(p.low / p.all > w.low / w.all * 2, 'pink has more low end than white'); assert.ok(b.low / b.all > p.low / p.all * 1.03, 'brown more still: ' + b.low / b.all + ' against ' + p.low / p.all);
  const a1 = run(C, 'wnoise', { level: 1 }, {}, 2).out, a2 = run(C, 'wnoise', { level: 1 }, {}, 2).out; assert.notDeepEqual(a1, a2, 'two Noise modules are not copies of each other');
  const q = run(C, 'wnoise', { level: 0 }, {}, 2).out; for (const v of q) assert.equal(v, 0);
});
test('chains engine: Oscillator → One-pole → Chain Out sounds without a Track In; waves has no osc / wnoise', () => {
  const E = new C.ChainsEngine(SR, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } }), m = (id, type, params) => ({ id, type, scope: 'mono', params });
  E.loadPatch({ modules: [m('o', 'osc', { wave: 2, freq: 110, fm: 0, level: .5 }), m('f', 'onepole', { freq: 800 }), m('c', 'chainOut', { track: 1, level: 1 })], cables: [{ from: ['o', 'out'], to: ['f', 'in'] }, { from: ['f', 'lp'], to: ['c', 'in'] }] });
  const z = new Float32Array(N); let e = 0; for (let b = 0; b < 50; b++) { E.setTrackInputs(z, z); E.process(N); for (const v of E.bus(1)) { assert.ok(Number.isFinite(v)); e += v * v; } } assert.ok(e > 1);
  const W = BOTH[1][1]; assert.equal(W.MODULES.osc, undefined); assert.equal(W.MODULES.wnoise, undefined);
});
