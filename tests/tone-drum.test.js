// The Tone drum's level (owner's report, 25 Oct: "clipping" on the tone drum). Its raw synthesis peaks around 1.18 and the
// class trims it by a fixed factor; at 0.8 a single hit sat at -0.5 dBFS with no room to stack, and a 16th-note pattern held
// the safety limiter down ~6 dB. These tests pin the trim so it cannot drift back up.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');

function toneDrum() {
  const src = H.read('index.html'), at = src.indexOf('class ToneDrum');
  assert.ok(at > 0, 'class ToneDrum moved or was renamed; this test needs updating');
  let depth = 0, end = -1;
  for (let j = src.indexOf('{', at); j < src.length; j++) { if (src[j] === '{') depth++; else if (src[j] === '}') { depth--; if (!depth) { end = j + 1; break; } } }
  const ctx = H.workletScope(); ctx.clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  require('vm').runInContext(src.slice(at, end) + '\nthis.ToneDrum=ToneDrum;', ctx);
  return ctx.ToneDrum;
}
const SR = 48000, TD = toneDrum();
const peak = a => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); return m; };
const db = x => 20 * Math.log10(Math.max(x, 1e-9));

test('tone drum: one hit has headroom (no longer at full scale)', () => {
  const out = TD.render(TD.defaults(), SR);
  for (const v of out) assert.ok(Number.isFinite(v), 'no NaN/Infinity');
  const p = peak(out);
  assert.ok(p > .45 && p < .65, 'a single hit peaks at ' + db(p).toFixed(2) + ' dBFS (expected about -4.6; 0.8 gave -0.5)');
  assert.equal(out.length, Math.ceil(2.26 * SR) + 1, 'still about 2.26 s long');
  assert.ok(Math.abs(out[out.length - 1]) < 1e-3, 'the tail is faded to silence, so no click at the end');
});

test('tone drum: a dense 16th-note pattern stays near the ceiling instead of driving the limiter 6 dB', () => {
  const hit = TD.render(TD.defaults(), SR), step = Math.round(.125 * SR), n = SR * 2, sum = new Float32Array(n);
  for (let k = 0; k * step < n; k++) for (let i = 0; i < hit.length && k * step + i < n; i++) sum[k * step + i] += hit[i];
  const p = peak(sum);
  assert.ok(p < 1.25, 'peak of 125 ms retriggers is ' + db(p).toFixed(2) + ' dBFS (was +5.4; 0.8 gave 1.85)');
  assert.ok(p > .8, 'and it is still a loud drum: ' + db(p).toFixed(2) + ' dBFS');
});

test('tone drum: its own Level control still scales the output in dB', () => {
  const base = peak(TD.render({ level: 0 }, SR)), quieter = peak(TD.render({ level: -6 }, SR));
  assert.ok(Math.abs(db(quieter) - (db(base) - 6)) < .1, '-6 dB is -6 dB (' + db(base).toFixed(2) + ' -> ' + db(quieter).toFixed(2) + ')');
});
