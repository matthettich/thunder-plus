// Audio-thread hardening of the master worklet (master-worklet.js): no allocation in the limiter's per-sample true-peak estimate,
// and a bad (NaN / Infinity) input sample or setting must not leave the chain dead. The goldens in master.test.js pin that
// finite output is unchanged; these pin what the fixes are for.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const vm = require('node:vm');
const H = require('./helpers');
const SR = 48000;

const ctxFor = () => { const ctx = H.workletScope({ sampleRate: SR }); H.load(ctx, 'master-worklet.js'); return ctx; };

test('limiter: the true-peak estimate makes no array per call, and gives the same values as the array version', () => {
  const ctx = ctxFor(), Lim = vm.runInContext('Limiter', ctx), lim = new Lim(SR);
  const src = H.read('master-worklet.js'), a = src.indexOf('  tpPeak(h){'), body = src.slice(a, src.indexOf('\n  }\n', a));
  assert.ok(!/\bof\s*\[/.test(body), 'tpPeak loops over an array literal (one array per call, twice a sample)');
  const ref = h => {   // the previous version, verbatim
    let m = Math.max(Math.abs(h[1]), Math.abs(h[2]));
    for (const t of [.25, .5, .75]) {
      const t2 = t * t, t3 = t2 * t;
      const v = 0.5 * ((2 * h[1]) + (-h[0] + h[2]) * t + (2 * h[0] - 5 * h[1] + 4 * h[2] - h[3]) * t2 + (-h[0] + 3 * h[1] - 3 * h[2] + h[3]) * t3);
      if (Math.abs(v) > m) m = Math.abs(v);
    }
    return m;
  };
  let s = 7; const rnd = () => { s = (s * 16807) % 2147483647; return s / 2147483647; };
  for (let k = 0; k < 20000; k++) {
    const h = [rnd() * 4 - 2, rnd() * 4 - 2, rnd() * 4 - 2, rnd() * 4 - 2];
    assert.ok(Object.is(lim.tpPeak(h), ref(h)), 'tpPeak differs for ' + h);
  }
});

/* ───────── a bad input sample, or a bad setting, must not kill a stage ───────── */
const BLOCK = 128;
const SUP = { on: true, depth: .5, sharp: .5, sel: 3, atk: 8, rel: 90, lo: 150, hi: 12000, mix: 1, delta: false };
const COMP = { on: true, thr: -14, ratio: 2, atk: 10, rel: 150, auto: true, knee: 6, makeup: 0, automk: true, mix: 1, hpf: 80 };
const LIM = { on: true, drive: 0, ceil: -1, rel: 120, look: 3, tp: true };
const CONFIGS = { 'limiter only': { lim: LIM }, 'compressor only': { comp: COMP }, 'suppressor only': { sup: SUP }, 'all three': { sup: SUP, comp: COMP, lim: LIM } };
const tone = (k, i, g) => g * Math.sin(2 * Math.PI * 220 * (k * BLOCK + i) / SR);
function make(set) { const ctx = ctxFor(); return new (ctx.__registered['thunder-master'])({ processorOptions: { meters: false, set } }); }
function feed(P, k, g, bad) {
  const a = new Float32Array(BLOCK), out = [new Float32Array(BLOCK), new Float32Array(BLOCK)];
  for (let i = 0; i < BLOCK; i++) a[i] = tone(k, i, g);
  if (bad !== undefined) a[5] = bad;
  P.process([[a, a]], [out]); return out;
}
const finite = a => { for (const v of a) if (!Number.isFinite(v)) return false; return true; };
const rmsOf = a => { let e = 0; for (const v of a) e += v * v; return Math.sqrt(e / a.length); };

for (const [name, set] of Object.entries(CONFIGS)) for (const bad of [NaN, Infinity, -Infinity]) {
  test('master (' + name + '): one ' + bad + ' input sample leaves the output finite, and the stages still work afterwards', () => {
    const P = make(set), F = make(set);
    for (let k = 0; k < 150; k++) { feed(P, k, .3); feed(F, k, .3); }
    for (let k = 150; k < 200; k++) {
      const o = feed(P, k, .3, k === 160 ? bad : undefined); feed(F, k, .3);
      assert.ok(finite(o[0]) && finite(o[1]), 'non-finite output in block ' + k);
    }
    // loud signal now: the compressor / suppressor must reduce it as an undisturbed instance does (after settling)
    let a, b;
    for (let k = 200; k < 700; k++) { a = feed(P, k, .9); b = feed(F, k, .9); assert.ok(finite(a[0]) && finite(a[1]), 'non-finite output in block ' + k); }
    const dA = 20 * Math.log10(rmsOf(a[0])), dB = 20 * Math.log10(rmsOf(b[0]));
    assert.ok(Math.abs(dA - dB) < .25, 'level after the bad sample ' + dA.toFixed(2) + ' dB vs ' + dB.toFixed(2) + ' dB for an undisturbed instance');
    if (set.comp) { assert.ok(Number.isFinite(P.comp.gr), 'compressor gain is finite'); assert.ok(P.comp.gr < -.5, 'compressor still reduces (gr ' + P.comp.gr + ')'); }
    if (set.sup) { let nan = 0; for (const v of P.sup.pw) if (v !== v) nan++; assert.strictEqual(nan, 0, 'suppressor spectrum has NaN bins'); }
  });
}

test('master: an absurdly large (but finite) input sample is replaced too', () => {
  const P = make({ lim: LIM }), out = [new Float32Array(BLOCK), new Float32Array(BLOCK)], a = new Float32Array(BLOCK);
  a[3] = 1e30; a[4] = -1e30; a[5] = 63.9;
  P.process([[a, a]], [out]);
  assert.ok(finite(out[0]) && finite(out[1]));
});

test('master: a NaN / Infinity setting (or a compressor ratio of 0) is ignored and the chain keeps rendering finite audio', () => {
  const bad = [['comp', 'ratio', 0], ['comp', 'ratio', NaN], ['comp', 'atk', NaN], ['comp', 'rel', Infinity], ['comp', 'mix', NaN], ['comp', 'thr', NaN], ['comp', 'knee', NaN], ['comp', 'hpf', NaN],
    ['lim', 'look', NaN], ['lim', 'drive', NaN], ['lim', 'rel', NaN], ['lim', 'ceil', NaN], ['sup', 'mix', NaN], ['sup', 'depth', NaN], ['sup', 'lo', NaN]];
  for (const [stage, key, v] of bad) {
    const P = make(CONFIGS['all three']); P.apply({ [stage]: { [key]: v } });
    for (let k = 0; k < 300; k++) { const o = feed(P, k, .7); assert.ok(finite(o[0]) && finite(o[1]), stage + '.' + key + ' = ' + v + ': non-finite output in block ' + k); }
  }
  // a finite setting still takes effect
  const P = make(CONFIGS['all three']); P.apply({ comp: { ratio: 8 } }); assert.strictEqual(P.comp.p.ratio, 8);
  P.apply({ comp: { ratio: NaN } }); assert.strictEqual(P.comp.p.ratio, 8, 'a NaN ratio keeps the previous one');
});
