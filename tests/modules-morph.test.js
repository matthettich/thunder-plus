// MORPH (the owner's module queue, …chains): two stereo inputs combined in the frequency domain. Convolve (only what both
// share) and Shape (B's spectral outline on A, always audible); Morph A → combined → B; Sharpness, Smoothing, Frame, Freeze;
// one frame late; level kept; a limiter on the way out; nothing made per block.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const N = 128, SR = 48000;
const D = (() => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx.ChainsDSP; })();
const M = D.MODULES.morph, DEF = {}; for (const [k, d] of Object.entries(M.params)) DEF[k] = d[0];
function run(over, A, B, blocks, opt = {}) {
  const p = Object.assign({}, DEF, over), st = M.create(SR), io = { in: { in: new Float32Array(N), inR: new Float32Array(N), inB: new Float32Array(N), inBR: new Float32Array(N) }, out: { out: new Float32Array(N), outR: new Float32Array(N) }, p, linked: new Set(opt.links || ['in', 'inB']) };
  const L = new Float32Array(N * blocks), R = new Float32Array(N * blocks);
  for (let b = 0; b < blocks; b++) { if (opt.live) opt.live(p, b); for (let i = 0; i < N; i++) { const n = b * N + i; io.in.in[i] = A(n); io.in.inB[i] = B(n); io.in.inR[i] = opt.AR ? opt.AR(n) : 0; io.in.inBR[i] = opt.BR ? opt.BR(n) : 0; } M.process(st, io, { block: N, sr: SR }); L.set(io.out.out, b * N); R.set(io.out.outR, b * N); }
  return { L, R, st, show: [...M.show(st)] };
}
const sine = (hz, a = 1) => i => a * Math.sin(2 * Math.PI * hz * i / SR), zero = () => 0;
const rms = (x, f = 0, t = x.length) => { let e = 0; for (let i = f; i < t; i++) e += x[i] * x[i]; return Math.sqrt(e / (t - f)); };
const sig = H.testSignal(N * 800), S = i => sig[i] * .5; let s2 = 7; const NZ = new Float32Array(N * 800).map(() => { s2 ^= s2 << 13; s2 >>>= 0; s2 ^= s2 >>> 17; s2 ^= s2 << 5; s2 >>>= 0; return (s2 / 2147483648 - 1) * .3; }), Nf = i => NZ[i];
const band = (x, hz, from) => { let re = 0, im = 0; for (let i = from; i < x.length; i++) { re += x[i] * Math.cos(2 * Math.PI * hz * i / SR); im += x[i] * Math.sin(2 * Math.PI * hz * i / SR); } return 2 * Math.sqrt(re * re + im * im) / (x.length - from); };

test('Morph is a chains module with two stereo inputs, a declared latency, and makes nothing per block', () => {
  assert.deepEqual(Object.keys(M.inputs), ['in', 'inR', 'inB', 'inBR']); assert.deepEqual(Object.keys(M.outputs), ['out', 'outR']); assert.equal(M.latency, 1024);
  const src = M.process.toString() + require('fs').readFileSync(require('path').join(H.ROOT, 'chains/chains-worklet.js'), 'utf8').match(/function morphFrame[\s\S]*?\n}\n/)[0];
  assert.doesNotMatch(src.replace(/\/\/[^\n]*/g, ''), /new Float|\[\s*\.\.\.|\.map\(|\.slice\(|Math\.atan2|Math\.sin\(|Math\.cos\(/, 'no arrays made and no angles worked out per frame');
  const W = (() => { const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js'); return ctx.WavesDSP; })(); assert.equal(W.MODULES.morph, undefined, 'not in waves');
});
test('Morph at 0 is A and at 1 is B, exactly, one frame late, for each frame size', () => {
  for (const [fr, n] of [[0, 512], [1, 1024], [2, 2048]]) {
    const a = run({ morph: 0, frame: fr }, S, Nf, 120), b = run({ morph: 1, frame: fr }, S, Nf, 120); let ea = 0, eb = 0;
    for (let i = 4096; i < a.L.length; i++) { ea = Math.max(ea, Math.abs(a.L[i] - Math.fround(S(i - n)))); eb = Math.max(eb, Math.abs(b.L[i] - Math.fround(Nf(i - n)))); }
    assert.ok(ea < 1e-5 && eb < 1e-5, 'frame ' + n + ': ' + ea + ' ' + eb); assert.equal(a.show[0], n, 'the module says its real latency');
  }
});
test('Convolve: what both share is heard about as loud as they are; what only one has is not; one side silent is silence', () => {
  for (const sh of [0, .5, 1]) {
    assert.ok(Math.abs(rms(run({ mode: 0, sharp: sh }, sine(440, .5), sine(440, .5), 200).L, 8000) - .3536) < .06, 'the same tone on both, Sharpness ' + sh);
    assert.ok(rms(run({ mode: 0, sharp: sh }, sine(440, .5), sine(3000, .5), 200).L, 8000) < .002, 'two different tones, Sharpness ' + sh);
    assert.equal(rms(run({ mode: 0, sharp: sh }, sine(440, .5), zero, 100).L, 4000), 0); assert.equal(rms(run({ mode: 0, sharp: sh }, zero, sine(440, .5), 100).L, 4000), 0);
  }
  // a chord on A, one of its notes on B: that note comes through, the others fall away
  const chord = i => .25 * (Math.sin(2 * Math.PI * 300 * i / SR) + Math.sin(2 * Math.PI * 800 * i / SR) + Math.sin(2 * Math.PI * 2100 * i / SR)), r = run({ mode: 0, sharp: .5 }, chord, sine(800, .5), 300).L;
  assert.ok(band(r, 800, 9600) > .1); assert.ok(band(r, 300, 9600) < band(r, 800, 9600) * .01 && band(r, 2100, 9600) < band(r, 800, 9600) * .01);
  const lo = rms(run({ mode: 0, sharp: 0 }, S, Nf, 400).L, 9600), hi = rms(run({ mode: 0, sharp: 1 }, S, Nf, 400).L, 9600); assert.ok(lo > hi && hi > .005, 'Sharpness narrows it: ' + lo + ' > ' + hi);
  assert.ok(lo <= Math.max(rms(sig) * .5, rms(NZ)) + .01, 'never louder than the inputs');
});
test('Shape: A always sounds, at its own loudness, with B\'s outline; B silent leaves A as it is', () => {
  const a = run({ mode: 1, smooth: 0 }, Nf, zero, 300).L; let e = 0; for (let i = 8192; i < a.length; i++) e = Math.max(e, Math.abs(a[i] - Math.fround(Nf(i - 1024)))); assert.ok(e < 1e-4, 'B silent, no Smoothing: A exactly (' + e + ')'); assert.ok(Math.abs(rms(run({ mode: 1 }, Nf, zero, 300).L, 8192) / rms(NZ) - 1) < .5, 'with Smoothing: A at about its level');
  for (const sh of [0, .5, 1]) assert.ok(Math.abs(rms(run({ mode: 1, sharp: sh }, Nf, S, 400).L, 9600) / rms(NZ) - 1) < .25, 'noise shaped by the test signal keeps the noise\'s loudness, Sharpness ' + sh);
  // noise (flat) as A, a low-passed sound as B: the result is darker than A
  let y = 0; const dark = new Float32Array(N * 800); let s3 = 99; for (let i = 0; i < dark.length; i++) { s3 ^= s3 << 13; s3 >>>= 0; s3 ^= s3 >>> 17; s3 ^= s3 << 5; s3 >>>= 0; y += ((s3 / 2147483648 - 1) - y) * .03; dark[i] = y; }
  const r = run({ mode: 1, sharp: .5 }, Nf, i => dark[i], 400).L, hf = x => { let d = 0; for (let i = 9601; i < x.length; i++) d += Math.abs(x[i] - x[i - 1]); return d / rms(x, 9600); };
  assert.ok(hf(r) < hf(NZ) * .5, 'the bright noise took the dark sound\'s outline');
  assert.ok(rms(run({ mode: 1 }, sine(440, .5), sine(3000, .5), 200).L, 8000) > .005, 'even two pure tones that share nothing: A is still there');
});
test('the Morph slider crosses A → combined → B without a hole; Smoothing slows changes; Freeze holds a spectrum that goes on sounding', () => {
  for (const mo of [0, .25, .5, .75, 1]) assert.ok(rms(run({ mode: 1, morph: mo }, S, Nf, 300).L, 9600) > .08, 'Morph ' + mo);
  // A stops: with no Smoothing the combined sound stops within a few frames, with Smoothing it rings on
  const cut = i => i < 24000 ? NZ[i] : 0, quick = run({ mode: 0, smooth: .02 }, cut, Nf, 400).L, slow = run({ mode: 0, smooth: 1 }, cut, Nf, 400).L;
  assert.ok(rms(quick, 30000, 34000) < 1e-3); assert.ok(rms(slow, 30000, 34000) > rms(quick, 30000, 34000) * 50 && rms(slow, 30000, 34000) > .002);
  // Freeze A on a 440 Hz tone, then the tone stops: it is still there, at 440 Hz and its level
  const tone = i => i < 20000 ? .5 * Math.sin(2 * Math.PI * 440 * i / SR) : 0, f = run({ morph: 0, freeze: 0 }, tone, zero, 600, { live: (p, b) => { if (b === 120) p.freeze = 1; } }).L;
  let cr = 0; for (let i = 40000; i < 76800; i++) if (f[i - 1] < 0 && f[i] >= 0) cr++; assert.ok(Math.abs(cr / (36800 / SR) - 440) < 3, 'held at its pitch: ' + cr / (36800 / SR)); assert.ok(Math.abs(rms(f, 40000) - .3536) < .03);
  const un = run({ morph: 0, freeze: 1 }, tone, zero, 600, { live: (p, b) => { if (b === 300) p.freeze = 0; } }).L; assert.ok(rms(un, 60000) < 1e-4, 'let go: it follows the input again');
  const fb = run({ mode: 1, freeze: 0 }, Nf, i => i < 20000 ? S(i) : 0, 500, { live: (p, b) => { if (b === 120) p.freeze = 2; } }).L, nb = run({ mode: 1, freeze: 0 }, Nf, i => i < 20000 ? S(i) : 0, 500).L;
  let diff = 0; for (let i = 40000; i < fb.length; i++) diff = Math.max(diff, Math.abs(fb[i] - nb[i])); assert.ok(diff > .01, 'B frozen keeps its outline on A after B has stopped');
});
test('stereo: each side on its own; with no right inputs patched the left is copied and only one channel is worked', () => {
  const r = run({ morph: 0 }, sine(300, .5), zero, 120, { links: ['in', 'inR', 'inB'], AR: sine(700, .4) }); let el = 0, er = 0;
  for (let i = 4096; i < r.L.length; i++) { el = Math.max(el, Math.abs(r.L[i] - .5 * Math.sin(2 * Math.PI * 300 * (i - 1024) / SR))); er = Math.max(er, Math.abs(r.R[i] - .4 * Math.sin(2 * Math.PI * 700 * (i - 1024) / SR))); } assert.ok(el < 1e-5 && er < 1e-5);
  const m = run({ morph: 0 }, S, zero, 60); assert.deepEqual(m.R, m.L); assert.equal(m.st.c[1].has, 0, 'the second channel never ran');
});
test('it cannot blow up: hot, broken or silent inputs, Level at the top, a frame size changed while playing', () => {
  for (const mode of [0, 1]) {
    let r = run({ mode, level: 24 }, sine(440, 1), sine(440, 1), 200).L; let pk = 0; for (const v of r) { assert.ok(Number.isFinite(v)); pk = Math.max(pk, Math.abs(v)); } assert.ok(pk <= 2.0001 && pk > 1, 'the limiter holds 2: ' + pk);
    r = run({ mode }, i => i % 97 === 0 ? NaN : i % 89 === 0 ? Infinity : 1e9, i => i % 5 ? -1e9 : NaN, 100).L; for (const v of r) assert.ok(Number.isFinite(v) && Math.abs(v) <= 2.0001);
    r = run({ mode }, zero, zero, 60).L; for (const v of r) assert.equal(v, 0);
    r = run({ mode }, S, Nf, 400, { live: (p, b) => { p.frame = (b / 50 | 0) % 3; p.sharp = (b % 7) / 6; p.smooth = (b % 5) / 4; p.freeze = (b / 30 | 0) % 3; p.morph = (b % 11) / 10; } }).L; for (const v of r) assert.ok(Number.isFinite(v) && Math.abs(v) <= 2.0001);
  }
  const q = run({ mode: 1 }, sine(500, .2), Nf, 200).L, lin = run({ mode: 1, level: 6 }, sine(500, .2), Nf, 200).L; let d = 0; for (let i = 0; i < q.length; i++) d = Math.max(d, Math.abs(lin[i] - q[i] * Math.pow(10, 6 / 20))); assert.ok(d < 1e-5, 'inside -1..1 Level is a plain gain');
});
test('chains engine: a Morph in a patch, A from one Track In and B from another', () => {
  const E = new D.ChainsEngine(SR, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } }), m = (id, type, params) => ({ id, type, scope: 'mono', params });
  E.loadPatch({ modules: [m('a', 'trackIn', { track: 1 }), m('n', 'wnoise', { color: 0, level: .5 }), m('x', 'morph', Object.assign({}, DEF, { mode: 1 })), m('c', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['n', 'out'], to: ['x', 'in'] }, { from: ['a', 'out'], to: ['x', 'inB'] }, { from: ['x', 'out'], to: ['c', 'in'] }, { from: ['x', 'outR'], to: ['c', 'inR'] }] });
  let e = 0; for (let b = 0; b < 200; b++) { const s = sig.subarray(b * N, (b + 1) * N); E.setTrackInputs(s, s); E.process(N); for (const v of E.bus(1)) { assert.ok(Number.isFinite(v)); e += v * v; } } assert.ok(e > 1);
});
