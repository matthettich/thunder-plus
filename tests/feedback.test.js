// FEEDBACK (audit finding M2): a cable marked fb closes a loop in …chains and …waves. It is left out of the run order, so
// the module it feeds hears the module it comes from one block (128 samples) late, whatever order the patch stores its
// modules in; what arrives through it is guarded so a loop with gain above 1 cannot grow without end.
// A patch with no fb cable must run exactly as before (the golden tests cover that; one check here pins the run order).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const N = 128;

function chains(opts) {
  const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js');
  const E = new ctx.ChainsDSP.ChainsEngine(48000, Object.assign({ focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } }, opts));
  return { ctx, E, D: ctx.ChainsDSP };
}
function waves() {
  const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  const TFX = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))), DSP = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm')));
  ctx.WavesDSP.wasmInit({ dspBytes: DSP, tfxBytes: TFX });
  return { ctx, D: ctx.WavesDSP };
}
const mod = (id, type, params) => ({ id, type, scope: 'mono', params });
const shuffles = a => [a, [...a].reverse(), [a[2], a[0], a[3], a[1]], [a[3], a[1], a[0], a[2]], [a[1], a[3], a[2], a[0]]];

/* in -> x -> out, and x -> y -(fb)-> x: an impulse comes round the loop once a block, scaled by y's amount each time. */
function loopPatch(order, amount, fb = true) {
  const all = { a: mod('a', 'trackIn', { track: 1 }), x: mod('x', 'atten', { amount: 1, offset: 0 }), y: mod('y', 'atten', { amount, offset: 0 }), c: mod('c', 'chainOut', { track: 1, level: 1 }) };
  const back = { from: ['y', 'out'], to: ['x', 'in'] }; if (fb) back.fb = true;
  return { modules: order.map(k => all[k]), cables: [{ from: ['a', 'out'], to: ['x', 'in'] }, { from: ['x', 'out'], to: ['c', 'in'] }, { from: ['x', 'out'], to: ['y', 'in'] }, back] };
}
function renderChains(E, blocks, input) {
  const out = new Float32Array(N * blocks), z = new Float32Array(N);
  for (let b = 0; b < blocks; b++) {
    const s = input ? input.subarray(b * N, (b + 1) * N) : z;
    E.setTrackInputs(s, s); E.process(N); out.set(E.bus(1), b * N);
  }
  return out;
}

test('chains: an impulse through a loop comes back every 128 samples, whatever order the modules are stored in', () => {
  const imp = new Float32Array(N * 8); imp[5] = 1;
  let first = null;
  for (const order of shuffles(['a', 'x', 'y', 'c'])) {
    const { E } = chains(); E.loadPatch(loopPatch(order, .5));
    const out = renderChains(E, 8, imp);
    for (let k = 0; k < 8; k++) assert.equal(out[5 + k * N], Math.fround(Math.pow(.5, k)), 'pass ' + k + ' with order ' + order.join(''));
    let other = 0; for (let i = 0; i < out.length; i++) if ((i - 5) % N) other += Math.abs(out[i]);
    assert.equal(other, 0, 'nothing between the passes');
    if (first) assert.deepEqual(out, first, 'same audio for order ' + order.join('')); else first = out;
  }
});

test('chains: the run order ignores a feedback cable, and the modules round the loop stay in signal order', () => {
  for (const order of shuffles(['a', 'x', 'y', 'c'])) {
    const { E } = chains(); E.loadPatch(loopPatch(order, .5));
    const o = E.graph.order, at = k => o.indexOf(k);
    assert.ok(at('a') < at('x') && at('x') < at('y') && at('x') < at('c'), 'a, x, then y and c: got ' + o.join(''));
  }
});

test('chains: a loop stored without the mark still runs as it always did (left over, in patch order)', () => {
  const { E } = chains(); E.loadPatch(loopPatch(['c', 'y', 'x', 'a'], .5, false));
  assert.deepEqual([...E.graph.order], ['a', 'c', 'y', 'x'], 'the source first, then the loop and what follows it in patch order');
  assert.ok(E.graph.cables.every(c => !c.fb));
});

test('chains: a loop with gain 2 stays finite and under the ceiling', () => {
  const { E } = chains();
  // x -> y and x -> z, both back into x: twice the signal each time round
  E.loadPatch({ modules: [mod('a', 'trackIn', { track: 1 }), mod('x', 'atten', { amount: 1, offset: 0 }), mod('y', 'atten', { amount: 1, offset: 0 }), mod('z', 'atten', { amount: 1, offset: 0 }), mod('c', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['a', 'out'], to: ['x', 'in'] }, { from: ['x', 'out'], to: ['c', 'in'] }, { from: ['x', 'out'], to: ['y', 'in'] }, { from: ['x', 'out'], to: ['z', 'in'] },
      { from: ['y', 'out'], to: ['x', 'in'], fb: true }, { from: ['z', 'out'], to: ['x', 'in'], fb: true }] });
  const sig = H.testSignal(N * 2000), out = renderChains(E, 2000, sig);
  let peak = 0; for (const v of out) { assert.ok(Number.isFinite(v)); peak = Math.max(peak, Math.abs(v)); }
  assert.ok(peak > 1, 'the loop did build up (peak ' + peak + ')');
  assert.ok(peak <= 2 * 2 + 1.0001, 'two guarded cables (each at most 2) plus the input: peak ' + peak);
  // and with the input gone it neither blows up nor turns into something that is not a number
  const tail = renderChains(E, 2000, null); for (const v of tail) assert.ok(Number.isFinite(v) && Math.abs(v) <= 4.0001);
});

test('the guard: untouched inside -1..1, smooth and rising above it, never past 2, not-a-number becomes silence', () => {
  for (const D of [chains().D, waves().D]) {
    const f = D.fbAdd, src = new Float32Array(N), dst = new Float32Array(N);
    for (let i = 0; i < N; i++) src[i] = (i / (N - 1)) * 2 - 1;   // -1 .. 1
    f(dst, src, 1); assert.deepEqual(dst, src, 'bit for bit inside the range');
    dst.fill(.25); f(dst, src, 1); for (let i = 0; i < N; i++) assert.equal(dst[i], Math.fround(.25 + src[i]), 'it adds to what is there');
    const hot = new Float32Array(N), o = new Float32Array(N);
    for (let i = 0; i < N; i++) hot[i] = 1 + i * .25;   // 1 .. 32.75
    f(o, hot, 1);
    for (let i = 1; i < N; i++) { assert.ok(o[i] >= o[i - 1], 'rising'); assert.ok(o[i] <= 2); }
    assert.ok(Math.abs(o[1] - 1.2449) < 1e-3, 'just over 1 it is nearly straight (1.25 gives ' + o[1] + ')');
    const neg = new Float32Array(N), on = new Float32Array(N); for (let i = 0; i < N; i++) neg[i] = -hot[i];
    f(on, neg, 1); for (let i = 0; i < N; i++) assert.equal(on[i], -o[i], 'the same both ways');
    const bad = new Float32Array(N), ob = new Float32Array(N); bad[0] = NaN; bad[1] = Infinity; bad[2] = -Infinity; bad[3] = .5;
    f(ob, bad, 1); assert.deepEqual([...ob.subarray(0, 4)], [0, 0, 0, .5]);
    const og = new Float32Array(N); f(og, src, .5); for (let i = 0; i < N; i++) assert.equal(og[i], Math.fround(src[i] * .5), 'the gain comes first');
  }
});

test('chains: a feedback cable into a parameter is guarded too, and a not-a-number never reaches the module', () => {
  const { E } = chains();
  E.loadPatch({ modules: [mod('a', 'trackIn', { track: 1 }), mod('x', 'atten', { amount: 0, offset: 0 }), mod('c', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['a', 'out'], to: ['x', 'in'] }, { from: ['x', 'out'], to: ['c', 'in'] }, { from: ['x', 'out'], to: ['x', 'p:offset'], fb: true }] });
  const io = E._bufs(E.graph.mods.get('x')); io.out.out.fill(NaN);
  const out = renderChains(E, 4, null); for (const v of out) assert.equal(v, 0);
});

test('chains: a module patched into itself runs, one block late', () => {
  const { E } = chains();
  E.loadPatch({ modules: [mod('a', 'trackIn', { track: 1 }), mod('x', 'atten', { amount: .5, offset: 0 }), mod('c', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['a', 'out'], to: ['x', 'in'] }, { from: ['x', 'out'], to: ['c', 'in'] }, { from: ['x', 'out'], to: ['x', 'in'], fb: true }] });
  assert.deepEqual([...E.graph.order], ['a', 'x', 'c']);
  const imp = new Float32Array(N * 4); imp[0] = 1;
  const out = renderChains(E, 4, imp);
  assert.deepEqual([out[0], out[N], out[2 * N], out[3 * N]], [.5, .25, .125, .0625]);
});

test('chains: the mark travels by loadPatch, by connect and by message; connecting the same cable again only changes the mark', () => {
  const { E } = chains(); E.loadPatch(loopPatch(['a', 'x', 'y', 'c'], .5));
  const back = () => E.graph.cables.find(c => c.a[0] === 'y');
  assert.equal(back().fb, true); const n = E.graph.cables.length;
  E.connect(['y', 'out'], ['x', 'in']); assert.equal(!!back().fb, false, 'connected again without the mark'); assert.equal(E.graph.cables.length, n);
  E.connect(['y', 'out'], ['x', 'in'], true); assert.equal(back().fb, true); assert.equal(E.graph.cables.length, n);
  const v = E.graph.ver; E.connect(['y', 'out'], ['x', 'in'], true); assert.equal(E.graph.ver, v, 'nothing changed: no recompile');
  E.disconnect(['y', 'out'], ['x', 'in']); assert.equal(back(), undefined);
  E.handleMessage({ op: 'connect', a: ['y', 'out'], b: ['x', 'in'], fb: true }); assert.equal(back().fb, true);
  E.handleMessage({ op: 'disconnect', a: ['y', 'out'], b: ['x', 'in'] }); E.handleMessage({ op: 'connect', a: ['y', 'out'], b: ['x', 'in'] }); assert.equal(!!back().fb, false);
  // anything but true is not a mark (a patch file is not trusted to hold a boolean)
  E.loadPatch(Object.assign(loopPatch(['a', 'x', 'y', 'c'], .5, false), {})); E.connect(['y', 'out'], ['x', 'in'], 1); assert.equal(!!back().fb, false);
});

test('chains: a node that plays one track still runs the module a feedback cable comes from', () => {
  const { E } = chains({ focus: 0 }); E.loadPatch(loopPatch(['a', 'x', 'y', 'c'], .5));
  const act = E._active(); for (const k of ['a', 'x', 'y', 'c']) assert.ok(act.has(k), k);
});

/* waves: hostIn -> vco -> x(atten) -> out, and x -> y -(fb)-> x inside the voice. */
function wavesPatch(order, amount, fb = true) {
  const all = { h: { id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, v: { id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } },
    x: { id: 'x', type: 'atten', scope: 'poly', params: { amount: 1, offset: 0 } }, y: { id: 'y', type: 'atten', scope: 'poly', params: { amount, offset: 0 } },
    z: { id: 'z', type: 'atten', scope: 'poly', params: { amount, offset: 0 } }, o: { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } } };
  const cables = [{ from: ['h', 'pitch'], to: ['v', 'pitch'] }, { from: ['v', 'out'], to: ['x', 'in'] }, { from: ['x', 'out'], to: ['o', 'in'] }, { from: ['x', 'out'], to: ['y', 'in'] }, { from: ['x', 'out'], to: ['z', 'in'] }];
  for (const k of ['y', 'z']) { const c = { from: [k, 'out'], to: ['x', 'in'] }; if (fb) c.fb = true; cables.push(c); }
  return { modules: order.map(k => all[k]), cables };
}
function renderWaves(D, doc, blocks) {
  D = waves().D;   // a fresh engine scope each time: an oscillator's starting phase comes from a counter that runs on between engines
  const E = new D.WavesEngine(48000, { voices: 4, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch(doc); E.noteOn(60, 1);
  const out = new Float32Array(N * blocks);
  for (let b = 0; b < blocks; b++) { E.process(N); out.set(E.bus(1), b * N); }
  return { E, out };
}

test('waves: a loop in a voice sounds the same whatever order the modules are stored in, and stays finite at gain 2', () => {
  const { D } = waves(), base = ['h', 'v', 'x', 'y', 'z', 'o'];
  let first = null;
  for (const order of [base, [...base].reverse(), ['y', 'o', 'x', 'h', 'z', 'v'], ['z', 'x', 'y', 'v', 'o', 'h']]) {
    const { out } = renderWaves(D, wavesPatch(order, .4), 60);
    let e = 0; for (const v of out) { assert.ok(Number.isFinite(v)); e += v * v; }
    assert.ok(e > 0, 'it makes sound');
    if (first) assert.deepEqual(out, first, 'same audio for order ' + order.join('')); else first = out;
  }
  const { out } = renderWaves(D, wavesPatch(base, 1), 1500);
  let peak = 0; for (const v of out) { assert.ok(Number.isFinite(v)); peak = Math.max(peak, Math.abs(v)); }
  assert.ok(peak > 1 && peak <= 5.0001, 'built up and held: peak ' + peak);
});

test('waves: a feedback cable does not make its target run per voice', () => {
  const { D } = waves();
  const E = new D.WavesEngine(48000, { voices: 4, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  // m (an attenuverter with nothing polyphonic feeding it forwards) takes the voices' x only through a feedback cable
  const doc = wavesPatch(['h', 'v', 'x', 'y', 'z', 'o'], .4);
  doc.modules.push({ id: 'm', type: 'atten', scope: 'poly', params: { amount: 1, offset: 0 } });
  doc.cables.push({ from: ['x', 'out'], to: ['m', 'in'], fb: true });
  E.loadPatch(doc);
  assert.equal(E.graph.mods.get('m').poly, false, 'not per voice through a feedback cable');
  assert.equal(E.graph.mods.get('y').poly, true, 'the loop itself is per voice (x feeds y forwards)');
  E.loadPatch(Object.assign(doc, { cables: doc.cables.map(c => c.to[0] === 'm' ? { from: c.from, to: c.to } : c) }));
  assert.equal(E.graph.mods.get('m').poly, true, 'the same cable unmarked does');
});

test('both worklets carry the same guard and the same ordering rule', () => {
  const c = H.read('chains/chains-worklet.js'), w = H.read('waves/waves-worklet.js');
  const cut = s => s.slice(s.indexOf('function fbAdd('), s.indexOf('class Graph {'));
  assert.ok(cut(c).length > 100); assert.equal(cut(c), cut(w));
  for (const s of [c, w]) assert.ok(s.includes('if (c.fb) continue;'), 'compile leaves feedback cables out of the sort');
});

test('release: build 589 is announced, the cache and both worklet versions moved on', () => {
  const root = H.read('index.html'), sw = H.read('sw.js');
  assert.match(root, /\[589, '14 Nov', \[/);
  const b = +/const BUILD = (\d+);/.exec(root)[1]; assert.ok(b >= 589); assert.ok(sw.includes('thunderplus-v' + b));
  const cv = /chains-worklet\.js\?v=(\d+)/.exec(sw)[1], wv = /waves-worklet\.js\?v=(\d+)/.exec(sw)[1];
  assert.ok(+cv >= 49 && +wv >= 39);
  for (const m of root.matchAll(/chains-worklet\.js\?v=(\d+)/g)) assert.equal(m[1], cv);
  for (const m of root.matchAll(/waves-worklet\.js\?v=(\d+)/g)) assert.equal(m[1], wv);
  assert.ok(H.read('chains/index.html').includes('chains-worklet.js?v=' + cv)); assert.ok(H.read('waves/index.html').includes('waves-worklet.js?v=' + wv));
});
