// CLONE DOWN LEFT AN EMPTY ROW (12 Nov, owner: "Pressing clone down multiple times eventually inserts an empty row"). Cause: a block with notes on a bus is refused by
// newBlock (build 569), after cloneDown had already inserted the row for it. Reproduced in Chrome: bus on track 06, notes in its block, a block below it, CLN↓: a whole empty
// song row, every track, and the cursor on it.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function cloneDown\(\)\{[\s\S]*?\n\}\n/); assert.ok(m, 'cloneDown not found');
function rig({ bus = [], free = true } = {}) {
  const NT = 8, log = [], song = Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1));
  song[0][0] = 1; song[1][0] = 2; song[0][5] = 3; song[1][5] = 4;   // a block under the cursor, one below it (a clash), and the same on track 05
  const c = { NT, tv: 'song', selA: null, cs: { song: { x: 0, y: 0 } }, song, log, rS: n => n, hp: n => n, toast: t => log.push('toast ' + t), saveS() {}, draw() {}, firstPhrase: id => id,
    selRect: () => ({ x0: 0, x1: 5, y0: 0, y1: 0 }), getCell: (v, x, y) => song[y][x],
    busNo: (x, id) => bus.includes(x), songRowsInsert: (at, h) => { log.push('insert ' + at); song.splice(at, 0, new Array(NT + 1).fill(-1)); song.length = 256; return true; },
    songRowsRemove: (at, h) => { log.push('remove ' + at); song.splice(at, h); song.push(new Array(NT + 1).fill(-1)); },
    newBlock: (p, v) => { if (free) song[p.y][p.x] = 100 + v; } };
  vm.createContext(c); vm.runInContext(m[0] + '\nthis.cloneDown = cloneDown;', c); return c;
}
test('a refused bus block inserts no row and does not move the cursor', () => {
  const c = rig({ bus: [0] }); c.cloneDown();
  assert.ok(!c.log.some(l => l.startsWith('insert')), c.log.join()); assert.equal(c.cs.song.y, 0); assert.equal(c.song[1][0], 2);
});
test('with other tracks selected too, the refused ones are left out and the rest are cloned (the old loop stopped at the first refusal)', () => {
  const c = rig({ bus: [0] }); c.selA = { v: 'song' }; c.cloneDown();
  assert.deepEqual(c.log.filter(l => l.startsWith('insert')), ['insert 1']); assert.equal(c.song[1][0], -1, 'the bus cell stays empty'); assert.equal(c.song[1][5], 103);
});
test('nothing written (no free sects): the rows that were inserted come out again', () => {
  const c = rig({ free: false }); c.cloneDown();
  assert.deepEqual(c.log.filter(l => /^(insert|remove)/.test(l)), ['insert 1', 'remove 1']); assert.equal(c.song[1][0], 2); assert.equal(c.cs.song.y, 0);
});
test('the normal case still inserts one row and clones into it', () => {
  const c = rig(); c.cloneDown(); assert.equal(c.song[1][0], 101); assert.equal(c.song[2][0], 2); assert.equal(c.cs.song.y, 1);
});
