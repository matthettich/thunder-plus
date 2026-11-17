// WAVETABLE layer with a negative Bend (build 635, 16 Nov 2026). warp(p) = 1 - (1 - p)^(1 - 3 * bend) rounds to exactly 1 for a phase just below 1,
// so x = TN, i = TN and the read of tab[i + 1] ran one past the 1025-entry table: undefined, so NaN in the voice. The table has one spare entry now.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
function load(){
  const s = H.read('index.html'), a = s.indexOf('function wavetableOsc('); assert.ok(a >= 0);
  let d = 0, j = s.indexOf('{', a);
  for (; j < s.length; j++){ if (s[j] === '{') d++; else if (s[j] === '}'){ d--; if (!d) break; } }
  return new Function('sinT', s.slice(a, j + 1) + '; return wavetableOsc;')(x => Math.sin(2 * Math.PI * x));
}
for (const bend of [-1, -0.5, 0.5]){
  test('wavetable layer: Bend ' + bend + ' renders only finite samples, even at phases just below 1', () => {
    const wt = load(), sr = 48000;
    let seed = 1; const R = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
    for (const uni of [1, 8]) for (const f of [110, 440, 3000]){
      const o = wt({wtBend: bend, wtUni: uni, wtSpread: 0.3, wtSub: 0.2, wtNoise: 0.1}, sr, 0.5, 0.2, R), inc = f / sr;
      let bad = 0; o.blk(0, 1, inc);
      for (let n = 0; n < sr; n++){ if (n % 128 === 0) o.blk(n / sr, 1, inc); if (!Number.isFinite(o.s(inc))) bad++; }
      assert.equal(bad, 0, 'bend ' + bend + ', ' + uni + ' voices, ' + f + ' Hz');
    }
    // the phase that used to read past the table: a start phase of 1 - 1e-9 (R() returns it for every unison voice)
    const o = wt({wtBend: bend}, sr, 0.5, 0.2, () => 1 - 1e-9); o.blk(0, 1, 100 / sr);
    for (let n = 0; n < 4; n++) assert.ok(Number.isFinite(o.s(1e-9)), 'phase just below 1');
  });
}
