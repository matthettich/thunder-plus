// EXPRESSION (the owner's module queue): typed maths as a module. Its own parser: the text is never run as code; output held
// between -4 and 4; length limited. Parser, evaluator, the module, and the text's way into both engines.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const N = 128, SR = 48000;
const chainsCtx = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx; };
const wavesCtx = () => { const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  ctx.WavesDSP.wasmInit({ dspBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm'))), tfxBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))) }); return ctx; };
const CC = chainsCtx(), WC = wavesCtx(), BOTH = [['chains', CC.ChainsDSP], ['waves', WC.WavesDSP]];
const VARS = (o = {}) => Float64Array.from([o.a || 0, o.b || 0, o.c || 0, o.d || 0, o.k1 || 0, o.k2 || 0, o.t || 0, o.y || 0, Math.PI, SR, o.rnd || 0]);
const ev = (D, s, o) => { const P = D.MathExpr.compile(s); return P.err ? 'E' + P.err + '@' + P.pos : D.mxRun(P, new Float64Array(40), VARS(o)); };
function run(D, text, over, ins, blocks = 1, tr) {
  const m = D.MODULES.expr, p = {}; for (const [k, d] of Object.entries(m.params)) p[k] = d[0]; Object.assign(p, over);
  const st = m.create(SR), io = { in: {}, out: { out: new Float32Array(N) }, p, inst: { text }, linked: new Set() }, rec = new Float32Array(N * blocks); for (const k in m.inputs) io.in[k] = new Float32Array(N);
  for (let b = 0; b < blocks; b++) { for (const k in io.in) { const s = ins && ins[k]; for (let i = 0; i < N; i++) io.in[k][i] = s ? s(b * N + i) : 0; } m.process(st, io, { block: N, sr: SR, transport: tr || { startN: 0 } }); rec.set(io.out.out, b * N); }
  return { out: rec, st, show: m.show(st) };
}

for (const [name, D] of BOTH) {
  test(name + ': the parser: precedence, operators, functions, names', () => {
    const T = [['1+2*3', 7], ['(1+2)*3', 9], ['-2^2', -4], ['2^3^2', 512], ['2^-1', .5], ['10%3', 1], ['-1%3', 2], ['1/0', 0], ['5%0', 0], ['1--1', 2], ['+3', 3], ['1e3', 1000], ['.5+.5', 1], ['2.5e-1*4', 1],
      ['1<2', 1], ['2<=2', 1], ['3>4', 0], ['4>=5', 0], ['2==2', 1], ['2!=2', 0], ['1<2&&2<3', 1], ['0||0', 0], ['0||2', 1], ['!0+!5', 1], ['3>4?10:20', 20], ['1?2?3:4:5', 3], ['0?1:0?2:3', 3],
      ['sin(pi/2)', 1], ['cos(0)', 1], ['abs(-3)', 3], ['sqrt(16)', 4], ['sqrt(-4)', 0], ['log(0)', 0], ['exp(0)', 1], ['floor(-1.5)', -2], ['ceil(1.2)', 2], ['round(2.5)', 3], ['sign(-9)', -1], ['fract(2.75)', .75], ['tanh(0)', 0],
      ['min(3,max(1,2))', 2], ['pow(2,10)', 1024], ['mod(7,4)', 3], ['clip(5,-1,1)', 1], ['clip(-5,-1,1)', -1], ['mix(0,10,.25)', 2.5], ['tri(0)', -1], ['tri(.5)', 1], ['tri(.25)', 0], ['saw(.75)', .5], ['sq(.2)', 1], ['sq(.6)', -1],
      ['SIN(PI/2)', 1], ['  1 +\n 2\t', 3], ['((((1))))', 1]];
    for (const [s, want] of T) { const got = ev(D, s); assert.ok(typeof got === 'number' && Math.abs(got - want) < 1e-9, s + ' = ' + got + ', wanted ' + want); }
    assert.equal(ev(D, 'a*k1+b*k2', { a: 2, b: 3, k1: 4, k2: 5 }), 23); assert.equal(ev(D, 'c-d', { c: 1, d: .25 }), .75); assert.equal(ev(D, 't*sr', { t: 2 }), 96000); assert.equal(ev(D, 'y+rnd', { y: 1, rnd: -.5 }), .5);
  });
  test(name + ': what is not maths is refused, with where; nothing typed is ever looked up as code', () => {
    const E = [['', 1], ['   ', 1], ['x'.repeat(201), 2], ['2 $ 3', 4], ['foo', 5], ['alert(1)', 5], ['constructor', 5], ['__proto__', 5], ['toString(1)', 5], ['hasOwnProperty(1)', 5], ['valueOf', 5], ['sin(', 8], ['(1+2', 6], ['sin(1,2)', 7], ['min(1)', 7], ['clip(1,2)', 7],
      ['1 2', 9], ['1+', 8], ['*2', 8], ['a.b', 9], ['1?2', 8], ['a[0]', 4], ['a;b', 4], ['"x"', 4], ['`x`', 4], ['a=1', 9], ['{}', 4], ['1,2', 9], ['this', 5], ['globalThis', 5], ['Math.sin(1)', 5]];
    for (const [s, code] of E) { const P = D.MathExpr.compile(s); assert.equal(P.err, code, JSON.stringify(s.slice(0, 30)) + ' gave ' + P.err + ' at ' + P.pos); assert.ok(P.pos >= 0 && P.pos <= 201); assert.equal(P.ops, undefined, 'no program for a bad formula'); }
    assert.equal(D.MathExpr.compile(42).err, 10); assert.equal(D.MathExpr.compile(null).err, 10); assert.equal(D.MathExpr.compile({ toString: () => '1' }).err, 10);
    assert.equal(D.MathExpr.compile('1+'.repeat(60) + '1').err, 0); assert.equal(D.MathExpr.compile('1+'.repeat(70) + 'sin('.repeat(10) + '1' + ')'.repeat(10)).err, 3, 'too many steps');
    assert.equal(D.MathExpr.compile('('.repeat(30) + '1' + ')'.repeat(30)).err, 3, 'too deep'); assert.equal(D.MathExpr.compile('-'.repeat(60) + '1').err, 3);
    assert.equal(D.MathExpr.compile('(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+(1+1))))))))))))))))))))').err, 0);
    const src = D.MathExpr.compile.toString() + D.mxRun.toString(); assert.doesNotMatch(src, /\beval\b|Function\(|new Function|import\(|setTimeout|\[name\]\(/, 'no way to run text');
    assert.doesNotMatch(D.mxRun.toString(), /new |\[\s*\.\.\.|\.map\(|\.slice\(|=>\s*\{/, 'the evaluator makes nothing');
  });
  test(name + ': random text never throws, never leaves a program that misbehaves', () => {
    const toks = ['a', 'b', 'k1', 't', 'y', 'rnd', '1', '2.5', '.1', '+', '-', '*', '/', '%', '^', '(', ')', 'sin(', 'min(', 'clip(', ',', '?', ':', '<', '>=', '==', '&&', '||', '!', ' ', '$', 'foo', ';', '[', '"'], S = new Float64Array(40); let seed = 99, ok = 0;
    const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296;
    for (let k = 0; k < 6000; k++) { let s = ''; const L = 1 + (rnd() * 12 | 0); for (let i = 0; i < L; i++) s += toks[rnd() * toks.length | 0];
      const P = D.MathExpr.compile(s); if (P.err) continue; ok++; assert.ok(P.n <= 128 && P.depth <= 32, s); S.fill(0); const v = D.mxRun(P, S, VARS({ a: .3, b: -.2, k1: 1, t: .01, y: .1 })); assert.equal(typeof v, 'number', s); }
    assert.ok(ok > 5, 'some of them were formulas (' + ok + ')');
  });
  test(name + ': the module: default passes A through; sliders, time, the previous sample, held between -4 and 4', () => {
    assert.equal(D.MODULES.expr.text, true); assert.equal(D.MODULES.expr.scope, 'poly');
    const x = i => Math.sin(i * .1) * .5; let r = run(D, undefined, {}, { a: x }, 2); for (let i = 0; i < N * 2; i++) assert.equal(r.out[i], Math.fround(x(i))); assert.deepEqual([...r.show], [0, 0]);
    r = run(D, 'a * k1 + b * k2', { k1: 2, k2: -1 }, { a: () => .25, b: () => .1 }); assert.ok(Math.abs(r.out[5] - .4) < 1e-6);
    r = run(D, 'sin(2 * pi * 440 * t)', {}, {}, 375); let cross = 0; for (let i = 1; i < r.out.length; i++) if (r.out[i - 1] < 0 && r.out[i] >= 0) cross++; assert.ok(Math.abs(cross - 440) <= 1, '440 Hz from t: ' + cross);
    r = run(D, 'y + (a - y) * 0.1', {}, { a: () => 1 }, 2); assert.ok(Math.abs(r.out[0] - .1) < 1e-6 && Math.abs(r.out[1] - .19) < 1e-6 && r.out[200] > .99, 'y is the sample before: a one-pole filter in one line');
    r = run(D, 'y + 1', {}, {}, 2); assert.equal(r.out[10], 4, 'a counter stops at 4'); r = run(D, 'a * 1000', {}, { a: () => -1 }); assert.equal(r.out[0], -4);
    r = run(D, '0 / 0 + sqrt(-1) + log(-1) + a ^ 0.5', {}, { a: () => -1 }); for (const v of r.out) assert.equal(v, 0, 'not-a-number is silence');
    r = run(D, 'exp(999)', {}, {}); assert.equal(r.out[0], 4); r = run(D, '-exp(999)', {}, {}); assert.equal(r.out[0], -4);
    r = run(D, 'rnd', {}, {}, 40); let m = 0, e = 0; for (const v of r.out) { assert.ok(v >= -1 && v <= 1); m += v; e += v * v; } assert.ok(Math.abs(m / r.out.length) < .05 && Math.abs(e / r.out.length - 1 / 3) < .03, 'rnd is noise');
    r = run(D, 'c > 0.5 ? a : b', {}, { a: () => .7, b: () => -.7, c: i => i < 64 ? 1 : 0 }); assert.equal(r.out[0], Math.fround(.7)); assert.equal(r.out[100], Math.fround(-.7));
  });
  test(name + ': a bad formula is silence and says where; t starts again with the song', () => {
    let r = run(D, 'sin(', {}, { a: () => 1 }); for (const v of r.out) assert.equal(v, 0); assert.deepEqual([...r.show], [8, 4]);
    r = run(D, 'a +* 2', {}, { a: () => 1 }); assert.equal(r.show[0], 8); assert.equal(r.show[1], 3);
    const m = D.MODULES.expr, st = m.create(SR), io = { in: { a: new Float32Array(N), b: new Float32Array(N), c: new Float32Array(N), d: new Float32Array(N) }, out: { out: new Float32Array(N) }, p: { k1: 1, k2: 0 }, inst: { text: 't' }, linked: new Set() };
    for (let b = 0; b < 10; b++) m.process(st, io, { block: N, sr: SR, transport: { startN: 1 } }); assert.ok(Math.abs(io.out.out[0] - 9 * N / SR) < 1e-6);
    m.process(st, io, { block: N, sr: SR, transport: { startN: 2 } }); assert.equal(io.out.out[0], 0, 'a new play: t is 0 again');
    const prog = st.prog; m.process(st, io, { block: N, sr: SR, transport: { startN: 2 } }); assert.equal(st.prog, prog, 'the same text is not read again');
    io.inst.text = 't * 0 + 0.5'; m.process(st, io, { block: N, sr: SR, transport: { startN: 2 } }); assert.equal(io.out.out[3], .5); assert.notEqual(st.prog, prog);
  });
}

test('chains engine: the text arrives by patch, by message and by setText, is cut at 200 characters, and only an Expression takes it', () => {
  const D = CC.ChainsDSP, E = new D.ChainsEngine(SR, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } }), m = (id, type, params, text) => ({ id, type, scope: 'mono', params, text });
  E.loadPatch({ modules: [m('a', 'trackIn', { track: 1 }), m('x', 'expr', { k1: 1, k2: 0 }, 'a * 0 + 0.25'), m('g', 'atten', { amount: 1, offset: 0 }, 'not for me'), m('c', 'chainOut', { track: 1, level: 1 })], cables: [{ from: ['a', 'out'], to: ['x', 'a'] }, { from: ['x', 'out'], to: ['c', 'in'] }] });
  const z = new Float32Array(N), play = () => { E.setTrackInputs(z, z); E.process(N); return E.bus(1)[7]; };
  assert.equal(play(), .25); assert.equal(E.graph.mods.get('g').inst.text, undefined);
  E.handleMessage({ op: 'text', id: 'x', value: '0.5' }); assert.equal(play(), .5);
  E.setText('x', 'k1 / 4'); assert.equal(play(), .25); E.setText('x', 42); assert.equal(play(), .25, 'not text: ignored'); E.setText('g', '1'); assert.equal(E.graph.mods.get('g').inst.text, undefined); E.setText('nobody', '1');
  E.setText('x', '1 +' + ' '.repeat(300) + '1'); assert.equal(E.graph.mods.get('x').inst.text.length, 200); assert.equal(play(), 0, 'cut short, it is a bad formula: silence, not a crash');
  E.handleMessage({ op: 'telemetry', value: true }); play(); const sh = E.telemetry().shows.find(s => s[0] === 'x'); assert.ok(sh && sh[1][0] === 8, 'the editor is told what is wrong');
  E.loadPatch({ modules: [m('x', 'expr', { k1: 1, k2: 0 }), m('c', 'chainOut', { track: 1, level: 1 })], cables: [{ from: ['x', 'out'], to: ['c', 'in'] }] }); assert.equal(play(), 0, 'no text: the default (A times the first slider)');
});
test('waves engine: the same, and per voice', () => {
  const D = WC.WavesDSP, E = new D.WavesEngine(SR, { voices: 4, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch({ modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } }, { id: 'x', type: 'expr', scope: 'poly', params: { k1: 1, k2: 0 }, text: 'abs(a)' }, { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }],
    cables: [{ from: ['h', 'pitch'], to: ['v', 'pitch'] }, { from: ['v', 'out'], to: ['x', 'a'] }, { from: ['x', 'out'], to: ['o', 'in'] }] });
  assert.equal(E.graph.mods.get('x').poly, true); assert.equal(E.graph.mods.get('x').inst.text, 'abs(a)'); E.noteOn(60, 1); E.noteOn(65, 1);
  let e = 0, neg = 0; for (let b = 0; b < 40; b++) { E.process(N); for (const v of E.bus(1)) { e += v * v; if (v < -1e-6) neg++; } } assert.ok(e > 0); assert.equal(neg, 0);
  E.handleMessage({ op: 'text', id: 'x', value: '-abs(a)' }); let pos = 0; for (let b = 0; b < 10; b++) { E.process(N); for (const v of E.bus(1)) if (v > 1e-6) pos++; } assert.equal(pos, 0);
});
test('both worklets carry the same parser', () => {
  const cut = s => s.slice(s.indexOf('const MX_VARS'), s.indexOf('const Expression = {')); const c = cut(H.read('chains/chains-worklet.js')); assert.ok(c.length > 3000); assert.equal(c, cut(H.read('waves/waves-worklet.js')));
});
