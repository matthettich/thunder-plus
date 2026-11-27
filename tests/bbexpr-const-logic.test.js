// BYTEBEAT: `x && constant` and `x || constant` (build 633, 16 Nov 2026). The one-step forms (77 / 78) skipped reading their constant when && / ||
// short-circuited, so the constant then ran as a step: most constants ended the formula early (a wrong value), and 50 or 51 jumped, which could loop
// for ever on the audio thread (`(t && 51) + a` at t = 0). The constant is now always read. All three copies of BBExpr must agree.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
function load(file){
  const s = H.read(file), a = s.indexOf('class BBExpr {'); assert.ok(a >= 0, file);
  let d = 0, j = s.indexOf('{', a);
  for (; j < s.length; j++){ if (s[j] === '{') d++; else if (s[j] === '}'){ d--; if (!d) break; } }
  return new Function(s.slice(a, j + 1) + '; return BBExpr;')();
}
const CASES = [['(t && 51) + a', 0, 7, 7], ['(t && 51) + a', 3, 7, 8], ['(t || 51) + a', 3, 7, 8], ['(t || 51) + a', 0, 7, 8], ['(t || 50) + a', 2, 7, 8],
  ['(t && 5) + a', 0, 7, 7], ['(t || 0) + a', 0, 7, 7], ['(t && 50) * 3 + a', 0, 7, 7], ['t && 10', 0, 7, 0], ['(t || 0) + (t && 51) + a', 0, 1, 1]];
for (const file of ['index.html', 'chains/chains-worklet.js', 'waves/waves-worklet.js']){
  test('BBExpr && / || with a constant: right value, and it ends (' + file + ')', () => {
    const BB = load(file);
    for (const [src, t, a, want] of CASES) assert.equal(BB.compile(src).run(t, a, 0, 0), want, src + ' at t=' + t);
    for (const [src] of CASES){ const p = BB.compile(src); for (let t = -4; t < 600; t++) p.run(t, 3, 5, 9); }   // every t returns
  });
}
