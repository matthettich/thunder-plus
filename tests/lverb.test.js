// LITE REVERB (lverb): the very cheap reverb in the shared chains / waves modules (chains/chains-worklet.js, copied to waves).
// Module-level tests (finite and bounded at every extreme, silence, determinism, no allocation, block lengths, sample rate),
// the same module inside the track-effect worklet (thunder-tfx: any block length, same sound as the module on its own),
// and that the app's tables know it (MODFX, FX_FACTORY presets, the waves copy is identical).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const SR = 48000;
const TFXWASM = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'chains/dsp/tfx.wasm')));

function load(Counter) {
  const ctx = H.freezeClock(H.workletScope(Counter ? { Float32Array: Counter } : {})); H.load(ctx, 'chains/chains-worklet.js');
  return ctx.ChainsDSP;
}
const D = load(), M = D.MODULES.lverb;
const defaults = () => { const p = {}; for (const [k, d] of Object.entries(M.params)) p[k] = d[0]; return p; };
function mkIo(p, N) { return { in: { in: new Float32Array(N), inR: new Float32Array(N) }, out: { out: new Float32Array(N), outR: new Float32Array(N) }, p, linked: new Set(['inR']) }; }
/* runs `blocks` blocks of `N` frames; sig(i) is the input sample i (both channels); returns {L, R, st}; `each(b, p)` may change parameters */
function run(over, sig, blocks, { N = 128, sr = SR, mod = M, each, st } = {}) {
  const p = Object.assign(defaults(), over), io = mkIo(p, N), ctx = { sr, block: N };
  st = st || mod.create(sr); const L = new Float32Array(blocks * N), R = new Float32Array(blocks * N);
  for (let b = 0; b < blocks; b++) {
    if (each) each(b, p);
    for (let i = 0; i < N; i++) { const v = sig(b * N + i); io.in.in[i] = v; io.in.inR[i] = v; }
    mod.process(st, io, ctx); L.set(io.out.out, b * N); R.set(io.out.outR, b * N);
  }
  return { L, R, st };
}
let seed = 99; const noise = () => ((seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296) * 2 - 1;
const peak = a => { let m = 0; for (const v of a) { const x = Math.abs(v); if (x > m) m = x; } return m; };
const finite = a => { for (const v of a) if (!Number.isFinite(v)) return false; return true; };
const bits = (a, b) => { const x = new Uint32Array(a.buffer, a.byteOffset, a.length), y = new Uint32Array(b.buffer, b.byteOffset, b.length); for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return false; return true; };

test('lverb: is a module with the five controls, and its waves copy is the same code', () => {
  assert.deepEqual(Object.keys(M.params), ['size', 'decay', 'tone', 'pre', 'mix']);
  const a = fs.readFileSync(path.join(H.ROOT, 'chains/chains-worklet.js'), 'utf8'), b = fs.readFileSync(path.join(H.ROOT, 'waves/waves-worklet.js'), 'utf8');
  const cut = s => s.slice(s.indexOf('/* Lite reverb:'), s.indexOf('/* Ensemble: a lush chorus'));
  assert.ok(cut(a).length > 3000); assert.equal(cut(a), cut(b));
});

test('lverb: finite and bounded at every parameter extreme, with full-scale noise, a full-scale square, DC, silence, and NaN / Infinity / huge input', () => {
  const sigs = { noise: () => noise(), square: i => (Math.floor(i / 436) & 1 ? 1 : -1), dc: () => 1, loud: () => noise() * 50, bad: i => (i % 97 === 0 ? NaN : i % 89 === 0 ? Infinity : i % 83 === 0 ? -Infinity : noise()) };
  const vals = Object.entries(M.params).map(([k, d]) => [k, [d[1], d[2]]]);
  for (let c = 0; c < 32; c++) {
    const over = {}; vals.forEach(([k, e], j) => { over[k] = e[(c >> j) & 1]; });
    for (const [name, f] of Object.entries(sigs)) {
      const r = run(over, f, 300), tag = JSON.stringify(over) + ' ' + name;
      assert.ok(finite(r.L) && finite(r.R), 'finite ' + tag);
      // dry (≤ 50 for 'loud', 8 for the guarded) + wet (clamped at ±4 before the mix gain)
      assert.ok(peak(r.L) <= (name === 'loud' ? 50 : 8) + 4 && peak(r.R) <= (name === 'loud' ? 50 : 8) + 4, 'bounded ' + tag + ' ' + peak(r.L));
    }
  }
  // full-scale noise at the maximum settings: the wet signal stays within a few times full scale
  const mx = run({ size: 1, decay: 1, tone: 1, pre: .1, mix: 1 }, () => noise(), 1500);
  assert.ok(peak(mx.L) < 4.01 && peak(mx.R) < 4.01, 'wet peak at max settings ' + peak(mx.L));
});

test('lverb: silence in gives silence out after the tail, and then it only copies the dry sound (asleep)', () => {
  // a click, then silence: at the default settings the tail is gone (exactly zero) well inside 30 s
  const r = run({ mix: 1 }, i => (i === 0 ? 1 : 0), Math.ceil(30 * SR / 128));
  assert.ok(peak(r.L.subarray(0, 4800)) > .05, 'it makes a tail');
  assert.equal(peak(r.L.subarray(r.L.length - 128 * 100)), 0); assert.equal(peak(r.R.subarray(r.R.length - 128 * 100)), 0);
  assert.equal(r.st.asleep, true);
  // asleep: the dry sound still passes (a sound far below −140 dB: no wake), and a real sound wakes it in the same block
  const p = Object.assign(defaults(), { mix: .3 }), io = mkIo(p, 128), ctx = { sr: SR, block: 128 };
  for (let k = 0; k < 40; k++) { for (let i = 0; i < 128; i++) io.in.in[i] = io.in.inR[i] = 1e-9; M.process(r.st, io, ctx); }   // (the dry gain glides for a few blocks)
  const dg = Math.cos(.3 * Math.PI / 2); assert.ok(Math.abs(io.out.out[100] - 1e-9 * dg) < 1e-12, 'dry passes while asleep: ' + io.out.out[100]); assert.equal(r.st.asleep, true);
  for (let i = 0; i < 128; i++) io.in.in[i] = io.in.inR[i] = .5; M.process(r.st, io, ctx);
  assert.equal(r.st.asleep, false); assert.ok(Math.abs(io.out.out[10] - .5 * dg) < .1);
});

test('lverb: a very long tail is not cut off early, and the shortcut stays off while sound keeps coming', () => {
  const r = run({ mix: 1, decay: 1, size: 1 }, i => (i === 0 ? 1 : 0), Math.ceil(4 * SR / 128));   // 4 s into a 12 s RT60
  assert.equal(r.st.asleep, false); assert.ok(peak(r.L.subarray(r.L.length - 128 * 40)) > 1e-4, 'still ringing at 4 s');
  const q = run({ mix: .3 }, () => noise() * 1e-3, Math.ceil(5 * SR / 128)); assert.equal(q.st.asleep, false);
});

test('lverb: deterministic: the same input and settings give the same samples', () => {
  const sig = H.testSignal(128 * 400), f = i => sig[i], over = { size: .7, decay: .6, tone: .4, pre: .02, mix: .5 };
  const a = run(over, f, 400), b = run(over, f, 400); assert.ok(bits(a.L, b.L) && bits(a.R, b.R));
  const c2 = run(Object.assign({}, over, { size: .71 }), f, 400); assert.notEqual(H.hashFloats([a.L, a.R]), H.hashFloats([c2.L, c2.R]), 'Size changes the sound (both channels compared)');
});

test('lverb: moving every knob while it plays stays finite and click-free (no jump bigger than the signal moves anyway)', () => {
  const sig = H.testSignal(128 * 600), r = run({ mix: .4 }, i => sig[i] * .3, 600, { each: (b, p) => { if (b > 50) { p.size = .5 + .5 * Math.sin(b * .3); p.pre = .05 + .05 * Math.sin(b * .21); p.tone = .5 + .5 * Math.sin(b * .17); p.decay = .5 + .4 * Math.sin(b * .11); p.mix = .5 + .5 * Math.sin(b * .07); } } });
  assert.ok(finite(r.L)); let worst = 0; for (let i = 1; i < r.L.length; i++) worst = Math.max(worst, Math.abs(r.L[i] - r.L[i - 1]));
  assert.ok(worst < .35, 'largest sample-to-sample step ' + worst);
  // a SIZE jump alone, in a steady tail: the step stays small against the tail's own level
  const j = run({ mix: .6, decay: .7 }, () => noise() * .3, 400, { each: (b, p) => { p.size = b < 200 ? .3 : .9; } });
  let w2 = 0, lvl = 0; for (let i = 128 * 150; i < 128 * 190; i++) lvl = Math.max(lvl, Math.abs(j.L[i]));
  for (let i = 128 * 199; i < 128 * 203; i++) w2 = Math.max(w2, Math.abs(j.L[i] - j.L[i - 1]));
  assert.ok(w2 < lvl * 1.5, 'size jump step ' + w2 + ' vs level ' + lvl);
});

test('lverb: any block length (1, 7, 64, 128, 384, 4096) and a sample-rate change keep it finite, with a tail of about the same length', () => {
  const tail = (N, sr) => { const r = run({ mix: 1, decay: .5 }, i => (i === 0 ? 1 : 0), Math.ceil(3 * sr / N), { N, sr }); assert.ok(finite(r.L) && finite(r.R), 'finite N ' + N); let e = 0; for (const v of r.L) e += v * v; return e; };
  const e128 = tail(128, SR); for (const N of [1, 7, 64, 384, 4096]) { const e = tail(N, SR); assert.ok(Math.abs(Math.log(e / e128)) < .4, 'N ' + N + ' energy ' + e + ' vs ' + e128); }
  for (const sr of [22050, 44100, 88200, 96000, 192000]) tail(128, sr);
  // the same instance moved to another rate (created at 48 k, then run at 96 k and back)
  const st = M.create(SR), a = run({ mix: .5 }, () => noise() * .3, 50, { st }), b = run({ mix: .5 }, () => noise() * .3, 50, { st, sr: 96000 }), c = run({ mix: .5 }, () => noise() * .3, 50, { st, sr: 44100 });
  assert.ok(finite(a.L) && finite(b.L) && finite(c.L) && peak(b.L) > 0);
});

test('lverb: decay times (broadband T20 × 3 from the impulse response; the knob is the low-frequency RT60, damping shortens the highs)', () => {
  const rt = (decay) => { const secs = decay === 1 ? 24 : 8, r = run({ mix: 1, decay }, i => (i === 0 ? 1 : 0), Math.ceil(secs * SR / 128)), x = r.L, n = x.length, e = new Float64Array(n); let acc = 0;
    for (let i = n - 1; i >= 0; i--) { acc += x[i] * x[i]; e[i] = acc; } const db = i => 10 * Math.log10(e[i] / e[0] + 1e-30); let a = 0; while (db(a) > -5) a++; let b = a; while (db(b) > -25) b++; return 3 * (b - a) / SR; };
  const lo = rt(0), mid = rt(.5), hi = rt(1);
  assert.ok(lo > .05 && lo < .4, 'min ' + lo); assert.ok(mid > .8 && mid < 2.2, 'default ' + mid); assert.ok(hi > 7 && hi < 14, 'max ' + hi); assert.ok(lo < mid && mid < hi);
});

test('lverb: after warm-up a block makes no typed array and no subarray view', () => {
  let made = 0, sub = 0; const F = new Proxy(Float32Array, { construct(t, a, nt) { made++; return Reflect.construct(t, a, nt); } });
  const sp = Float32Array.prototype.subarray, d = load(F), m = d.MODULES.lverb, st = m.create(SR), p = defaults(), io = mkIo(p, 128), ctx = { sr: SR, block: 128 };
  const go = b => { for (let i = 0; i < 128; i++) io.in.in[i] = io.in.inR[i] = Math.sin(b * 128 + i) * .3; p.size = .3 + .6 * Math.abs(Math.sin(b * .05)); p.pre = (b % 40) * .001; p.decay = .3 + .4 * Math.abs(Math.sin(b * .03)); m.process(st, io, ctx); };
  for (let b = 0; b < 60; b++) go(b);
  made = 0; Float32Array.prototype.subarray = function (...a) { sub++; return sp.apply(this, a); };
  try { for (let b = 60; b < 400; b++) go(b); } finally { Float32Array.prototype.subarray = sp; }
  assert.equal(made, 0, 'typed arrays made'); assert.equal(sub, 0, 'subarray views made');
});

test('lverb: the track-effect worklet (thunder-tfx) runs it at 128 and at longer blocks, the same sound as the module on its own', () => {
  const html = H.read('index.html');
  const a = html.indexOf('const TFX_SRC = `') + 'const TFX_SRC = `'.length, b = html.indexOf('`;\nlet tfxBytes', a); assert.ok(a > 20 && b > a);
  const src = new Function('return `' + html.slice(a, b) + '`')();
  const ACORN = ['/opt/npm-tools/node_modules', path.join(H.ROOT, 'node_modules'), '/usr/lib/node_modules', '/usr/local/lib/node_modules'].find(d => fs.existsSync(path.join(d, 'acorn')));
  if (!ACORN) return;   // (the worklet declarations are cut out with acorn, as block-size.test.js does)
  const acorn = require(path.join(ACORN, 'acorn')), i0 = html.indexOf('<script>\n/* ===ENGINE START=== */'), s0 = i0 + 9, page = html.slice(s0, html.indexOf('\n</script>', s0));
  const ast = acorn.parse(page, { ecmaVersion: 'latest', sourceType: 'script', allowReturnOutsideFunction: true }), body = [...ast.body];
  for (const st of ast.body) { const c = st.type === 'ExpressionStatement' && st.expression.type === 'CallExpression' && st.expression.callee; if (c && c.body && c.body.body && c.end - c.start > 200000) body.push(...c.body.body); }
  const by = new Map(); for (const st of body) if ((st.type === 'FunctionDeclaration' || st.type === 'ClassDeclaration') && !by.has(st.id.name)) by.set(st.id.name, page.slice(st.start, st.end));
  const WK = ['rvMix', 'DegradeCore', 'VSampCore', 'ResonCore', 'PitchFxCore', 'clkSpb', 'FlexCore', 'StepGateCore', 'RetrigCore', 'beatLock', 'bkHash', 'GrainsCore', 'ScrambleCore', 'GlitchCore'];
  const full = WK.map(n => { assert.ok(by.has(n), n); return by.get(n); }).join('\n') + '\n' + src;
  const mp = { size: .6, decay: .55, tone: .5, pre: .01, mix: .45 };
  const sig = H.testSignal(128 * 240), N0 = 128 * 240;
  const viaTfx = B => {
    const reg = {}, g = globalThis, old = g.ChainsDSP; g.ChainsDSP = D;
    try {
      new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', 'currentTime', full)(class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, (n, c) => { reg[n] = c; }, SR, 0);
      const node = new reg['thunder-tfx']({ processorOptions: { bytes: TFXWASM, chain: [{ key: 'v', slot: 0, mod: 'lverb', mp, on: true }] } });
      assert.ok(node.slots.length === 1 && node.slots[0].def, 'the module slot exists');
      const out = new Float32Array(N0);
      for (let o = 0; o < N0; o += B) {
        const s = sig.subarray(o, o + B), P = { bp: new Float32Array(B).fill(-1), flx: new Float32Array([-1]), gat: new Float32Array([-1]), rtg: new Float32Array([-1]) }; for (let k = 0; k < 8; k++) P['sw' + k] = new Float32Array([-1]);
        const outs = [[new Float32Array(B), new Float32Array(B)]]; node.process([[s, s], [], []], outs, P); out.set(outs[0][0], o);
      }
      return out;
    } finally { g.ChainsDSP = old; }
  };
  const direct = run(mp, i => sig[i], 240).L, t128 = viaTfx(128), t512 = viaTfx(512);
  let worst = 0; for (let i = 0; i < N0; i++) worst = Math.max(worst, Math.abs(direct[i] - t128[i])); assert.ok(worst < 1e-3, 'tfx vs module, 128: ' + worst);
  let w2 = 0; for (let i = 0; i < N0; i++) w2 = Math.max(w2, Math.abs(t128[i] - t512[i])); assert.ok(w2 < 1e-6, 'a 512 block = four 128 blocks: ' + w2);
  assert.ok(peak(t128) > .1);
});

test('lverb: the app knows it: the effect table, the factory presets (all in range, all load) and the old-song key', () => {
  const html = H.read('index.html');
  const row = html.match(/\n  lverb:\{name:'Lite reverb', short:'LITE'[^\n]*tail:true, pad:true, track:true, group:'Reverbs'/); assert.ok(row, 'MODFX row');
  const pre = html.match(/\n  lverb:\[(\[.*\])\],\n/); assert.ok(pre, 'factory presets');
  const list = (0, eval)('[' + pre[1] + ']'); assert.ok(list.length >= 4 && list.length <= 6, 'presets ' + list.length);
  const names = new Set(); for (const [name, v] of list) { assert.ok(!names.has(name)); names.add(name); for (const [k, b] of Object.entries(v)) { assert.ok(k in M.params, k); assert.ok(Number.isInteger(b) && b >= 0 && b <= 255, name + ' ' + k); } }
  // every preset, as the module sees it (the app's byte → value: min + (max − min) · b / 255), sounds and stays finite
  for (const [name, v] of list) {
    const over = {}; for (const [k, b] of Object.entries(v)) { const [, mn, mx] = M.params[k]; over[k] = mn + (mx - mn) * b / 255; }
    const r = run(over, () => noise() * .3, 200); assert.ok(finite(r.L) && peak(r.L) > .01 && peak(r.L) < 3, name + ' ' + peak(r.L));
  }
  assert.match(html, /'sdelay', 'delay', 'room', 'plate', 'lverb', 'reverb'/);
});
