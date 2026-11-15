// LOOP BESIDE PLAY (build 609): on the desktop layout #tLoop moves to sit right after #tSong in the top bar; the Mobile setup puts it back. Alt+L is the key (unchanged).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const src = require('./helpers').read('index.html');
test('source: applyPlatform moves #tLoop after #tSong (desktop) and back before #tLoopSel (mobile); Alt+L is still its key', () => {
  assert.match(src, /if \(!m && lp\.previousElementSibling !== ps\)\{ ps\.after\(lp\); lp\.classList\.add\('tsong'\); \} else if \(m && lp\.parentNode !== sb\.parentNode\)\{ sb\.before\(lp\); lp\.classList\.remove\('tsong'\); \}/);
  assert.match(src, /\.top #tLoop\.tsong\{/);
  assert.match(src, /e\.altKey && !mod && !e\.shiftKey && \(e\.code === 'KeyL'/);
});
test('the move is a pure DOM re-parent: a tiny model of it', () => {
  const mk = () => { const n = { parentNode: null, previousElementSibling: null, cl: new Set(), classList: { add: c => n.cl.add(c), remove: c => n.cl.delete(c) } }; return n; };
  const top = { id: 'top' }, bar = { id: 'bar' }, ps = mk(), lp = mk(), sb = mk(); ps.parentNode = top; sb.parentNode = bar; lp.parentNode = bar;
  ps.after = x => { x.parentNode = top; x.previousElementSibling = ps; }; sb.before = x => { x.parentNode = bar; x.previousElementSibling = null; };
  const body = src.match(/if \(!m && lp\.previousElementSibling[^\n]*?lp\.classList\.remove\('tsong'\); \}/)[0];
  const apply = m => new Function('m', 'lp', 'ps', 'sb', body)(m, lp, ps, sb);
  apply(false); assert.equal(lp.parentNode, top); assert.ok(lp.cl.has('tsong'));
  apply(true); assert.equal(lp.parentNode, bar); assert.ok(!lp.cl.has('tsong'));
});
