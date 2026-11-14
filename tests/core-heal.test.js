// THE CORE COMES BACK (12 Nov, owner's Balanced report): after the watchdog lets go of the song, the audio thread sequencer is allowed again
// once the audio clock has been steady (CORE_OK_N readings within 3 %), and stays off after CORE_BAD_MAX failures. coreHealStep is cut out of
// index.html and run alone.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), vm = require('vm');
const src = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
const m = src.match(/const CORE_BAD_MAX = 3, CORE_OK_N = 5;\nfunction coreHealStep\(c, w, a\)\{[\s\S]*?\n  return c\.okN >= CORE_OK_N; \}/);
assert.ok(m, 'coreHealStep found');
const c = vm.createContext({}); vm.runInContext(m[0] + '\nthis.step = coreHealStep; this.N = CORE_OK_N; this.MAX = CORE_BAD_MAX;', c);
test('steady clock: allowed again after CORE_OK_N readings, not before', () => {
  const s = { hk: null, okN: 0 }; let w = 100, a = 50, got = [];
  for (let i = 0; i <= c.N; i++){ got.push(c.step(s, w, a)); w += 2; a += 2; }
  assert.deepEqual(got, [false, false, false, false, false, true]);   // the first call only takes a sample
});
test('a fast or slow reading starts the count again', () => {
  const s = { hk: null, okN: 0 }; let w = 0, a = 0; c.step(s, w, a);
  for (let i = 0; i < 4; i++){ w += 2; a += 2; assert.equal(c.step(s, w, a), false); }
  w += 2; a += 2.8; assert.equal(c.step(s, w, a), false); assert.equal(s.okN, 0);   // 1.4x
  w += 2; a += 1.66; c.step(s, w, a); assert.equal(s.okN, 0);   // 0.83x
  for (let i = 0; i < c.N - 1; i++){ w += 2; a += 2; assert.equal(c.step(s, w, a), false); }
  w += 2; a += 2; assert.equal(c.step(s, w, a), true);
});
test('readings closer than 1.5 s apart are not judged', () => {
  const s = { hk: null, okN: 0 }; c.step(s, 0, 0); assert.equal(c.step(s, 1, 5), false); assert.equal(s.okN, 0);
});
test('wiring: coreFail counts, the third failure stays off, the heal runs beside the clock watch and never mid-song switches', () => {
  assert.match(src, /core\.bad = true; core\.badN = \(core\.badN \|\| 0\) \+ 1; core\.hk = null; core\.okN = 0;/);
  assert.match(src, /function coreHeal\(\)\{ if \(!core\.bad \|\| \(core\.badN \|\| 0\) >= CORE_BAD_MAX\) return;/);
  assert.match(src, /try \{ pbClock\(\); \} catch\(e\)\{\} try \{ coreHeal\(\); \} catch\(e\)\{\}/);
  assert.match(src, /if \(!tp\.on && coreWanted\(\) && !core\.ready\) try \{ coreLoad\(\); \}/);
});
