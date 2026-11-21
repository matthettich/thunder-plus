// MAKE UNIQUE KEEPS THE SELECTION (build 625, owner: "Pressing make unique on command bar removes selection"). makeUnique is cut out of index.html and run
// in a vm with stand-ins; the selection, the picks and the cursor must stand after it runs, and the blocks still get their new numbers.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const m = H.read('index.html').match(/function makeUnique\(\)\{[\s\S]*?\n\}\n/); assert.ok(m, 'makeUnique moved');
function rig(o) {
  const song = Array.from({ length: 8 }, () => new Array(9).fill(-1)); for (let y = 0; y < 8; y++) song[y][1] = 10 + y;
  const c = Object.assign({ tv: 'song', S: { song }, cs: { song: { x: 1, y: 2, top: 0 } }, selA: { x: 1, y: 5, v: 'song' }, sxPicks: [], toasts: [], toast: t => c.toasts.push(t), saveS() {}, draw() { c.drew = true; },
    selRect: () => ({ x0: 1, x1: 1, y0: 2, y1: 5 }), sxCols: r => [1], getCell: (v, t, y) => c.S.song[y][t], newBlock: (p, v) => { c.S.song[p.y][p.x] = 100 + p.y; } }, o || {});
  vm.createContext(c); vm.runInContext(m[0] + '\nthis.f = makeUnique;', c); return c;
}
test('a selection (upward: cursor above the anchor) stays, with the cursor, and its blocks are made unique', () => {
  const c = rig(); c.f();
  assert.deepEqual({ ...c.selA }, { x: 1, y: 5, v: 'song' }); assert.deepEqual({ ...c.cs.song }, { x: 1, y: 2, top: 0 });
  for (let y = 2; y <= 5; y++) assert.equal(c.S.song[y][1], 100 + y); assert.equal(c.S.song[6][1], 16, 'outside the selection: untouched'); assert.match(c.toasts[0], /Made 4 blocks unique/);
});
test('picked cells stay picked', () => {
  const picks = [{ y: 1, t: 1 }, { y: 3, t: 1 }], c = rig({ selA: null, sxPicks: picks }); c.f(); assert.equal(c.sxPicks.length, 2); assert.equal(c.S.song[3][1], 103);
});
test('no selection: the cursor block only, nothing invented', () => { const c = rig({ selA: null }); c.f(); assert.equal(c.selA, null); assert.equal(c.S.song[2][1], 102); assert.equal(c.S.song[3][1], 13); });
