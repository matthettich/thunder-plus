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
