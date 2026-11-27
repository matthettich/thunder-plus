// P2-2: undo history has a byte budget as well as the 200-step count cap. undoTrim is cut out of index.html and run in a vm.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
function load() {
  const src = H.read('index.html');
  const b = src.match(/const UNDO_MAX = 200, UNDO_BYTES = [^\n]*\nfunction undoTrim\(\)\{[^\n]*\n/); assert.ok(b, 'could not find undoTrim in index.html');
  const u = src.match(/const undo = \{past:\[\], future:\[\][^\n]*\n/); assert.ok(u, 'could not find the undo object in index.html');
  const c = { JSON, Math }; vm.createContext(c);
  vm.runInContext('const S = {};\n' + u[0] + b[0] + '\nthis.undo = undo; this.undoTrim = undoTrim; this.BUDGET = UNDO_BYTES;', c);
  return c;
}
const total = u => u.past.reduce((n, e) => n + (e && e.j ? e.j.length : 0), 0);
const push = (c, e) => { c.undo.past.push(e); c.undoTrim(); };
test('500 x 1 MB entries stay under the byte budget and the newest survives', () => {
  const c = load(), big = 'x'.repeat(1e6);
  for (let i = 0; i < 500; i++) push(c, { k: i % 3 ? 'S' : 'K', j: big + i });
  assert.ok(total(c.undo) <= c.BUDGET, 'total ' + total(c.undo));
  assert.ok(c.undo.past.length >= 20, 'still a deep history (' + c.undo.past.length + ')');
  assert.equal(c.undo.past[c.undo.past.length - 1].j, big + 499, 'newest kept');
});
test('mixed shapes (S, K, W, V with a null j) are handled', () => {
  const c = load(); push(c, { k: 'V', i: 0, j: null }); push(c, { k: 'W', j: 'y'.repeat(1e6) }); push(c, { k: 'S', j: 'z'.repeat(40e6) });
  assert.equal(c.undo.past.length, 1, 'only the newest (over budget alone) is kept'); assert.equal(c.undo.past[0].k, 'S');
});
test('small entries still keep up to 200', () => {
  const c = load(); for (let i = 0; i < 500; i++) push(c, { k: 'S', j: '{"n":' + i + '}' });
  assert.equal(c.undo.past.length, 200); assert.equal(c.undo.past[0].j, '{"n":300}'); assert.equal(c.undo.past[199].j, '{"n":499}');
});
