// BassCore (index.html; the pad-render Worker and the bass AudioWorklet are both built from its source text). After a big upward
// pitch jump a partial that was still fading out could sit above Nyquist: its phase increment reached 1 or more, the phase ran off
// the 4096-entry sine table, the lookup read undefined, and the voice produced NaN, then kept a huge phase so it came back.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const vm = require('node:vm');
const H = require('./helpers');

const html = H.read('index.html'), a = html.indexOf('\nclass BassCore {') + 1;
let d = 0, i = a, started = false;
for (; i < html.length; i++) { const c = html[i]; if (c === '{') { d++; started = true; } else if (c === '}') { d--; if (started && d === 0) break; } }
const BassCore = vm.runInNewContext('(' + html.slice(a, i + 1) + ')', { Math, Float32Array, Float64Array, Int32Array, Object, Array });

const finiteBlock = (L, R) => { for (let k = 0; k < L.length; k++) if (!Number.isFinite(L[k]) || !Number.isFinite(R[k])) return false; return true; };

test('BassCore: a big upward rate jump with many partials never produces NaN, and the voice stays healthy afterwards', () => {
  let seed = 7; const rnd = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
  const rates = [0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16];
  let bad = 0;
  for (let t = 0; t < 120; t++) {
    const c = new BassCore(48000); c.set({ parts: 64, det: rnd() < .5 ? 30 : 0 });
    const L = new Float32Array(128), R = new Float32Array(128);
    let ok = true;
    for (let blk = 0; blk < 60 && ok; blk++) {
      if (blk % 10 === 0) c.on(rnd() < .3);
      if (blk % 10 === 6 && rnd() < .5) c.off();
      const r = rates[Math.floor(rnd() * rates.length)], s = Math.floor(rnd() * 128);
      if (s > 0) c.render(L, R, 0, s, r);
      c.render(L, R, s, 128, r);
      ok = finiteBlock(L, R);
    }
    if (!ok) bad++;
  }
  assert.strictEqual(bad, 0, bad + ' of 120 random rate sequences produced non-finite output');
});

test('BassCore: rate 1 -> 16 with 64 partials, then back to rate 1, is finite throughout (the NaN used to stay)', () => {
  for (const det of [0, 30]) {
    const c = new BassCore(48000); c.set({ parts: 64, det });
    const L = new Float32Array(128), R = new Float32Array(128);
    c.on(false); for (let k = 0; k < 20; k++) c.render(L, R, 0, 128, 1);
    c.off(); c.on(false);
    for (let k = 0; k < 5; k++) { c.render(L, R, 0, 128, 16); assert.ok(finiteBlock(L, R), 'det ' + det + ': non-finite after the jump to rate 16, block ' + k); }
    for (let k = 0; k < 300; k++) { c.render(L, R, 0, 128, 1); assert.ok(finiteBlock(L, R), 'det ' + det + ': the voice stayed dead, block ' + k); }
  }
});
