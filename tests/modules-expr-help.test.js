// EXPRESSION help examples (MODULE_EXAMPLES.expr in the editors' shared block): every formula shown in a module's ? help is compiled and run
// in both engines at 48000 and 44100, and the claims its text makes (pitch, level, glide, stable filter) are measured.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const N = 128;
const chainsCtx = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx; };
const wavesCtx = () => { const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  ctx.WavesDSP.wasmInit({ dspBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm'))), tfxBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))) }); return ctx; };
const BOTH = [['chains', chainsCtx().ChainsDSP], ['waves', wavesCtx().WavesDSP]];
const listOf = src => { const b = /\/\* ===SHARED EDITOR START===[\s\S]*?===SHARED EDITOR END=== \*\//.exec(src)[0]; const i = b.indexOf('const MODULE_EXAMPLES = ');
  assert.ok(i >= 0); const o = b.indexOf('{', i); let d = 0, q = null, j = o; for (; j < b.length; j++) { const c = b[j]; if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; } if (c === "'") q = c; else if (c === '{') d++; else if (c === '}' && --d === 0) break; }
  return vm.runInNewContext('(' + b.slice(o, j + 1) + ')').expr.map(a => [...a]); };
const LIST = { chains: listOf(ED.chains), waves: listOf(ED.waves) };
const F = Object.fromEntries(LIST.chains.map(([w, f]) => [f, w]));
// one Expression run: seconds of audio; ins = per-input function of the sample number; returns the output
function run(D, SR, text, over, secs, ins) {
  const m = D.MODULES.expr, p = {}; for (const [k, d] of Object.entries(m.params)) p[k] = d[0]; Object.assign(p, over);
  const st = m.create(SR), io = { in: {}, out: { out: new Float32Array(N) }, p, inst: { text }, linked: new Set() }, blocks = Math.ceil(secs * SR / N), rec = new Float32Array(blocks * N);
  for (const k in m.inputs) io.in[k] = new Float32Array(N);
  for (let b = 0; b < blocks; b++) { for (const k in io.in) { const s = ins && ins[k]; for (let i = 0; i < N; i++) io.in[k][i] = s ? s(b * N + i) : 0; } m.process(st, io, { block: N, sr: SR, transport: { startN: 0 } }); rec.set(io.out.out, b * N); }
  return rec.subarray(0, Math.round(secs * SR));
}
const crossings = x => { let c = 0; for (let i = 1; i < x.length; i++) if ((x[i - 1] < 0) !== (x[i] < 0)) c++; return c; };
const peak = x => { let m = 0; for (const v of x) m = Math.max(m, Math.abs(v)); return m; };
const rms = x => Math.sqrt(x.reduce((s, v) => s + v * v, 0) / x.length);
const finite = x => x.every(Number.isFinite);
const SINE = n => i => Math.sin(2 * Math.PI * n * i / 48000);

test('the help list is the same text in both editors, plain, and every line is one short formula', () => {
  assert.equal(JSON.stringify(LIST.chains), JSON.stringify(LIST.waves)); assert.ok(LIST.chains.length >= 12);
  for (const [what, f] of LIST.chains) {
    assert.ok(f.length <= 200 && what.length > 10); assert.doesNotMatch(what + f, /["`\u2028\u2029\n\\]|\bCV\b/); assert.doesNotMatch(what, /'/); assert.doesNotMatch(f, /'/);
  }
  for (const ed of ['chains', 'waves']) { assert.match(ED[ed], /const exs = modExamples\(type\);/); assert.match(ED[ed], /Worked examples/); }
});

for (const [name, D] of BOTH) for (const SR of [48000, 44100]) {
  const tag = name + ' ' + SR + ': ';
  test(tag + 'every example compiles (within 128 steps) and runs finite for a second at a sensible level', () => {
    for (const [, f] of LIST.chains) {
      const P = D.MathExpr.compile(f); assert.equal(P.err, 0, f + ' err ' + P.err + ' at ' + P.pos); assert.ok(P.n <= 128 && f.length <= 200);
      const y = run(D, SR, f, {}, 1, { a: SINE(220), b: SINE(333), c: () => 0, d: () => 0 }); assert.ok(finite(y), f);
      for (const k1 of [0, 1, 4]) assert.ok(peak(run(D, SR, f, { k1 }, .3, { a: SINE(220), b: SINE(333) })) <= 4 + 1e-9, f + ' k1=' + k1);
    }
  });
  test(tag + 'oscillators: 220 Hz is 440 zero crossings a second (within 1), level 1', () => {
    for (const f of ['sin(2 * pi * 220 * t)', 'saw(220 * t)', 'tri(220 * t)', 'sq(220 * t)']) {
      const y = run(D, SR, f, {}, 1); assert.ok(Math.abs(crossings(y) - 440) <= 1, f + ' crossings ' + crossings(y)); assert.ok(peak(y) <= 1 + 1e-6 && peak(y) > .98, f + ' peak ' + peak(y));
    }
  });
  test(tag + 'K1 as the pitch: 220 Hz at 1, 110 Hz at 0, 550 Hz at 4', () => {
    const f = 'sin(2 * pi * (110 + k1 * 110) * t)';
    for (const [k1, hz] of [[1, 220], [0, 110], [4, 550]]) { const y = run(D, SR, f, { k1 }, 1); assert.ok(Math.abs(crossings(y) - 2 * hz) <= 1, k1 + ' gave ' + crossings(y) / 2); assert.ok(peak(y) <= 1 + 1e-6); }
  });
  test(tag + 'the glide pair: ramp into a sine gives the stated pitch, and moving K1 mid-run has no jump', () => {
    const ramp = 'fract(y + (110 + k1 * 110) / sr)', sine = 'sin(2 * pi * a)', seconds = 1, len = Math.round(seconds * SR);
    for (const [k1, hz] of [[1, 220], [0, 110], [4, 550]]) {
      const r = run(D, SR, ramp, { k1 }, seconds), y = run(D, SR, sine, {}, seconds, { a: i => r[i] });
      assert.ok(peak(r) <= 1 && Math.min(...r) >= 0, 'ramp is 0 to 1'); assert.ok(Math.abs(crossings(y) - 2 * hz) <= 2, 'k1 ' + k1 + ' crossings ' + crossings(y)); assert.ok(peak(y) <= 1 + 1e-6);
    }
    // K1 moved from 1 to 3 half way (run as two halves, state kept): the biggest step between neighbouring samples stays that of a 440 Hz sine, no jump
    const m = D.MODULES.expr, st = m.create(SR), st2 = m.create(SR), out = new Float32Array(N), io = { in: { a: new Float32Array(N), b: new Float32Array(N), c: new Float32Array(N), d: new Float32Array(N) }, out: { out }, p: { k1: 1, k2: 0 }, inst: { text: ramp }, linked: new Set() };
    const io2 = { in: { a: new Float32Array(N), b: new Float32Array(N), c: new Float32Array(N), d: new Float32Array(N) }, out: { out: new Float32Array(N) }, p: { k1: 1, k2: 0 }, inst: { text: sine }, linked: new Set() };
    let prev = null, worst = 0; const blocks = Math.ceil(len / N);
    for (let b = 0; b < blocks; b++) { io.p.k1 = b < blocks / 2 ? 1 : 3; m.process(st, io, { block: N, sr: SR, transport: { startN: 0 } }); io2.in.a.set(out); m.process(st2, io2, { block: N, sr: SR, transport: { startN: 0 } });
      for (const v of io2.out.out) { if (prev !== null) worst = Math.max(worst, Math.abs(v - prev)); prev = v; } }
    assert.ok(worst < 2 * Math.PI * 440 / SR * 1.05, 'largest step ' + worst);
    // and the one-line version does jump when K1 moves (the help says so)
    const jump = (() => { const P = D.MathExpr.compile('sin(2 * pi * (110 + k1 * 110) * t)'), S = new Float64Array(40), V = Float64Array.from([0, 0, 0, 0, 1, 0, 1.0013, 0, Math.PI, SR, 0]); const a = D.mxRun(P, S, V); V[4] = 3; return Math.abs(D.mxRun(P, S, V) - a); })();
    assert.ok(jump > .5, 'direct K1 pitch jumps: ' + jump);
  });
  test(tag + 'FM: K1 = 0 is the plain 220 Hz sine, K1 = 2 stays inside -1..1 and is brighter', () => {
    const f = 'sin(2 * pi * 220 * t + k1 * sin(2 * pi * 110 * t))', y0 = run(D, SR, f, { k1: 0 }, 1), y1 = run(D, SR, f, { k1: 1 }, 1), y4 = run(D, SR, f, { k1: 4 }, 1);
    const bright = y => { let e = 0, e0 = 0; for (let i = 2; i < y.length; i++) { const d2 = y[i] - 2 * y[i - 1] + y[i - 2]; e += d2 * d2; e0 += y[i] * y[i]; } return Math.sqrt(e / e0); };   // second difference over level: more highs, bigger number
    assert.ok(Math.abs(crossings(y0) - 440) <= 1); assert.ok(peak(y1) <= 1 + 1e-6 && peak(y4) <= 1 + 1e-6); assert.ok(bright(y1) > bright(y0) && bright(y4) > bright(y1) * 1.3, [bright(y0), bright(y1), bright(y4)].join(' '));
  });
  test(tag + 'chord: three sines over 3, peak under 1; fade-in reaches full level at 2 s', () => {
    const c = run(D, SR, '(sin(2 * pi * 220 * t) + sin(2 * pi * 277.2 * t) + sin(2 * pi * 329.6 * t)) / 3', {}, 1); assert.ok(peak(c) <= 1 && peak(c) > .8, 'peak ' + peak(c));
    const fade = run(D, SR, 'sin(2 * pi * 220 * t) * min(t / 2, 1)', {}, 3), at = s => peak(fade.subarray(Math.round(s * SR), Math.round(s * SR + SR / 100)));
    assert.ok(at(.5) < .3 && at(1) > .45 && at(1) < .55 && at(2.5) > .98 && peak(fade) <= 1 + 1e-6, [at(.5), at(1), at(2.5)].join(' '));
  });
  test(tag + 'a * b, tremolo, wavefolder, bit crusher, noise', () => {
    const ins = { a: SINE(1000), b: SINE(100) }, rm = run(D, SR, 'a * b', {}, 1, ins); assert.ok(peak(rm) <= 1 && peak(rm) > .9);
    const tr = run(D, SR, 'a * (0.5 + 0.5 * sin(2 * pi * 4 * t))', {}, 1, ins), q = (s, w) => peak(tr.subarray(Math.round(s * SR), Math.round((s + w) * SR)));
    assert.ok(peak(tr) <= 1 && q(.0625 - .005, .01) > .95 && q(.1875 - .005, .01) < .02, 'level ' + q(.0625 - .005, .01) + ' ' + q(.1875 - .005, .01));   // 4 Hz: full level at 1/16 s, nearly silent at 3/16 s
    const a1 = run(D, SR, 'sin(a * (1 + k1 * 8))', { k1: 1 }, 1, ins), a0 = run(D, SR, 'sin(a * (1 + k1 * 8))', { k1: 0 }, 1, ins); assert.ok(peak(a1) <= 1 && peak(a0) < .85 && peak(a0) > .8, 'fold K1=0 peak ' + peak(a0));
    const bc = run(D, SR, 'round(a * 8) / 8', {}, 1, ins), lv = new Set(Array.from(bc, v => Math.round(v * 8))); assert.ok(lv.size <= 17 && lv.size >= 15 && peak(bc) <= 1, 'levels ' + lv.size);
    const nz = run(D, SR, 'rnd * k1', { k1: .5 }, 1); assert.ok(peak(nz) <= .5 && peak(nz) > .49 && Math.abs(nz.reduce((s, v) => s + v, 0) / nz.length) < .01);
  });
  test(tag + 'low-pass: stable for K1 0 to 1 (and clipped above), cutoff near the stated figures, blows up past 2 without the clip', () => {
    const lp = 'y + (a - y) * clip(k1, 0, 1)', noise = i => Math.sin(i * 12.9898) * 43758.5453 % 1;
    for (const k1 of [0, .01, .1, .5, 1, 1.5, 4]) { const y = run(D, SR, lp, { k1 }, 1, { a: noise }); assert.ok(finite(y) && peak(y) <= 1 + 1e-6, 'k1 ' + k1 + ' peak ' + peak(y)); }
    const gain = (k1, hz) => { const y = run(D, SR, lp, { k1 }, 1, { a: SINE(hz) }); return rms(y.subarray(SR >> 1)) / rms(Float32Array.from({ length: SR >> 1 }, (_, i) => Math.sin(2 * Math.PI * hz * (i + (SR >> 1)) / SR))); };
    assert.ok(Math.abs(gain(.1, 800) - Math.SQRT1_2) < .06, '0.1 at 800 Hz ' + gain(.1, 800)); assert.ok(gain(.01, 76) > .6 && gain(.01, 76) < .8 && gain(.01, 2000) < .1, 'k1 .01: ' + gain(.01, 76) + ' ' + gain(.01, 2000)); assert.ok(gain(1, 5000) > .99);
    const bare = run(D, SR, 'y + (a - y) * k1', { k1: 3 }, .2, { a: SINE(220) }); assert.ok(peak(bare) > 3.9, 'unclipped K1 = 3 runs away to the ±4 hold: ' + peak(bare));
    const edge = run(D, SR, 'y + (a - y) * k1', { k1: 1.9 }, .2, { a: SINE(220) }); assert.ok(peak(edge) <= 1.5);
  });
}
