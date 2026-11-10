// LEVEL OVER A PLAY (10 Nov): the accumulator behind "level over this play: average X dB (RMS), peak Y dB, N s measured" in the playback report.
// pbLvReset / pbLvAdd / pbLvText / pbLvRead / pbLvStop are cut out of index.html and run against fake analysers. The browser is not run here.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/const PB_LV_MS = 250;[\s\S]*?\/\/ END LEVEL OVER A PLAY\n/);
assert.ok(m, 'could not find the level accumulator: the code moved, this test needs updating');

function rig(blocks) {   // blocks: per reading, [leftBlock, rightBlock] of numbers
  const log = [], st = { i: 0 }, mkAn = ch => ({ fftSize: 1024, getFloatTimeDomainData(b) { const d = blocks[st.i][ch]; for (let k = 0; k < Math.min(b.length, d.length); k++) b[k] = d[k]; } });
  const c = vm.createContext({ Math, Float32Array, PB: {}, performance: { now: () => 0 }, setInterval: () => 1, clearInterval() {}, tp: { on: true }, ctx: { state: 'running' }, rt: { man: [mkAn(0), mkAn(1)] }, pbLog: s => log.push(s) });
  vm.runInContext(m[0] + '\nthis.api = {pbLvReset, pbLvAdd, pbLvText, pbLvRead, pbLvStop};', c);
  return { api: c.api, log, st, c };
}
const fill = (v, n = 1024) => new Array(n).fill(v);

test('average of known blocks is their RMS over time; peak is the highest sample of either channel', () => {
  const R = rig([[fill(0.5), fill(0.5)], [fill(0.1), fill(-0.1)], [fill(0), fill(0)]]);
  R.api.pbLvReset();
  for (R.st.i = 0; R.st.i < 3; R.st.i++) R.api.pbLvRead();
  const ms = (0.25 + 0.01 + 0) / 3, db = x => (10 * Math.log10(x)).toFixed(1);
  assert.equal(R.api.pbLvText(R.c.PB.lv), `average ${db(ms)} dB (RMS), peak ${(20 * Math.log10(0.5)).toFixed(1)} dB, 3 readings in 0 s`);
});

test('peak uses the absolute value, and the right channel counts', () => {
  const R = rig([[fill(0.1), fill(-0.8, 10).concat(fill(0, 1014))]]);
  R.api.pbLvReset(); R.api.pbLvRead();
  assert.match(R.api.pbLvText(R.c.PB.lv), /peak -1\.9 dB/);
});

test('PLAY resets it; a new play does not inherit the last one', () => {
  const R = rig([[fill(1), fill(1)], [fill(0.1), fill(0.1)]]);
  R.api.pbLvReset(); R.api.pbLvRead(); R.api.pbLvStop();
  assert.match(R.log[0], /^level over this play: average 0\.0 dB \(RMS\), peak 0\.0 dB, 1 readings in 0 s$/);
  R.st.i = 1; R.api.pbLvReset(); R.api.pbLvRead();
  assert.match(R.api.pbLvText(R.c.PB.lv), /average -20\.0 dB \(RMS\), peak -20\.0 dB, 1 readings in 0 s/);
});

test('nothing is read when not playing, the engine is not running, or after STOP; a play with no readings says so; silence says silent', () => {
  const R = rig([[fill(0.5), fill(0.5)], [fill(0), fill(0)]]);
  R.api.pbLvReset(); R.c.tp.on = false; R.api.pbLvRead(); R.c.tp.on = true; R.c.ctx.state = 'suspended'; R.api.pbLvRead(); R.c.ctx.state = 'running';
  R.api.pbLvStop(); assert.match(R.log[0], /not measured/);
  R.api.pbLvRead(); assert.equal(R.c.PB.lv.n, 0, 'after STOP the accumulator is closed');
  R.api.pbLvReset(); R.st.i = 1; R.api.pbLvRead(); R.api.pbLvStop(); assert.match(R.log[1], /average silent \(RMS\), peak silent, 1 readings in 0 s/);
  assert.equal(R.c.PB.lvDone, 'average silent (RMS), peak silent, 1 readings in 0 s');
});

test('it hooks into the PLAY line, its own 4 Hz timer, STOP and the report header, and keeps the 4 s line', () => {
  assert.match(src, /rkReset\(\); pbLog\('PLAY ' \+ mode \+ ' · ' \+ pbState\(\)\); pbLvReset\(\);/);
  assert.match(src, /T:setInterval\(pbLvRead, PB_LV_MS\)/, 'its own timer, only while playing (lead, 10 Nov)'); assert.match(src, /clearInterval\(L\.T\)/);
  assert.match(src, /function pbAfterStop\(\)\{ PB\.stopT = performance\.now\(\); pbLvStop\(\);/);
  assert.match(src, /level over the last play: /); assert.match(src, /4 s into PLAY: level \$\{pbLevel\(\)\}/);
});
