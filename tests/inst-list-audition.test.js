// INST LIST CLICK AUDITIONS (15 Nov, owner: "make the instrument panel on the sidebar audition an instrument when clicked").
// Source-level pins plus the helper run in a vm: a click still selects the instrument and opens INST, then plays it once with
// the note keys' own preview (the octave's C, through keyPlayTrack, normal velocity, not held).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const fn = (name) => { const i = src.indexOf('function ' + name + '('); assert.ok(i > 0, name); let d = 0, j = src.indexOf('{', i);
  for (let k = j; k < src.length; k++) { if (src[k] === '{') d++; else if (src[k] === '}' && --d === 0) return src.slice(i, k + 1); } };

test('the INST LIST click keeps selecting the instrument and opening INST, then auditions it', () => {
  const m = src.match(/for \(const id of \['tInlSide', 'tInlPad'\]\) \$\(id\)\.addEventListener\('click', e => \{([\s\S]*?)\n\}\);/);
  assert.ok(m);
  assert.match(m[1], /const ii = \+b\.dataset\.i; cInst = ii; go\('inst'\);\s*if \(cInst !== ii\)\{ cInst = ii; draw\(\); \}[^\n]*\n\s*inlAudition\(ii\);/);
});
test('inlAudition plays the instrument once at the current octave\'s C, normal velocity, through the screen\'s track', () => {
  const calls = [], ctx = { kOct: 3, clamp: (v, a, b) => Math.min(b, Math.max(a, v)), keyPlayTrack: () => 5, preview: (...a) => { calls.push(a); } };
  vm.createContext(ctx); vm.runInContext(fn('inlAudition'), ctx);
  ctx.inlAudition(7);
  assert.deepEqual(calls, [[7, 36, -1, true, null, false, 5]]);
  ctx.preview = () => { throw new Error('no audio'); }; assert.doesNotThrow(() => ctx.inlAudition(1));
});
