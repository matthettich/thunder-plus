// RENOISE TRANSPOSE KEYS (build 609): Alt/Option+F1 / F2 = selection down / up a semitone, Alt+F11 / F12 = an octave, through the same transposeSel as Alt+, / Alt+.
// F1 alone is still Help (Alt+F1 must not open it).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const src = require('./helpers').read('index.html');
const line = src.match(/  if \(e\.altKey && !mod && !e\.shiftKey && \/\^F\(1\|2\|11\|12\)\$\/[^\n]*/)[0];
const run = (key, o = {}) => { const got = []; const e = Object.assign({ altKey: true, shiftKey: false, key, repeat: false, preventDefault() { got.push('pd'); } }, o);
  const r = vm.runInNewContext('(function(e, mod, transposeSel){ ' + line + ' return false; })', {})(e, false, d => got.push(d)); return { r, got }; };
test('Alt+F1 / F2 / F11 / F12 transpose -1 / +1 / -12 / +12 and are handled', () => {
  for (const [k, d] of [['F1', -1], ['F2', 1], ['F11', -12], ['F12', 12]]) { const { r, got } = run(k); assert.equal(r, true); assert.ok(got.includes(d), k); }
});
test('no Alt, Shift or a held key repeat: nothing / no second transpose; F3 is untouched', () => {
  assert.equal(run('F2', { altKey: false }).r, false); assert.equal(run('F2', { shiftKey: true }).r, false); assert.equal(run('F3').r, false);
  assert.ok(!run('F2', { repeat: true }).got.some(x => typeof x === 'number'));
});
test('source: Alt+F1 does not toggle Help; the Help lists the keys; no desktop accelerator takes them', () => {
  assert.match(src, /const tog = \(e\.key === 'F1' && !e\.altKey\)/);
  assert.match(src, /<dt>Alt\+F1 Alt\+F2<\/dt>/); assert.match(src, /<kbd>F11<\/kbd> <kbd>F12<\/kbd>/);
  assert.doesNotMatch(require('./helpers').read('electron/main.js'), /accelerator|F11|F12/);
});
