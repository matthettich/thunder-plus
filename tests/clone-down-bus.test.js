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

// CLN↓ x4 ON AN 8-ROW SELECTION (build 609, owner: "i selected 8 phrases and cloned down twice, on the third it inserted empty phrases in the song despite the fact that there were phrases in the song").
// The real page (headless, ELECTRO: 8 rows x 1 and x 4 tracks, with the song continuing below; also from Arrange beside Phrase) did not reproduce it, so this pins the model: every pass
// clones the 8 selected rows into 8 new rows below (never an empty row), the song below moves down intact, the selection and cursor follow, however far the view is from them.
function rigSel({ rows = 8, below = 20 } = {}) {
  const NT = 8, song = Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)); let next = 1000;
  for (let y = 0; y < rows + below; y++) song[y][0] = y + 1;
  const c = { NT, tv: 'song', selA: { x: 0, y: 0, v: 'song' }, cs: { song: { x: 0, y: rows - 1, top: 0 } }, song, log: [], rS: n => n, hp: n => n, toast() {}, saveS() {}, draw() {}, firstPhrase: id => id,
    selRect: () => ({ x0: 0, x1: 0, y0: Math.min(c.selA.y, c.cs.song.y), y1: Math.max(c.selA.y, c.cs.song.y) }), getCell: (v, x, y) => song[y][x], busNo: () => false,
    songRowsInsert: (at, h) => { for (let y = 256 - h; y < 256; y++) if (song[y].some(v => v >= 0)) return false; song.splice(at, 0, ...Array.from({ length: h }, () => new Array(NT + 1).fill(-1))); song.length = 256; return true; },
    songRowsRemove: (at, h) => { song.splice(at, h); while (song.length < 256) song.push(new Array(NT + 1).fill(-1)); },
    newBlock: (p, v) => { song[p.y][p.x] = next++; c.cloneOf = (c.cloneOf || {}); c.cloneOf[song[p.y][p.x]] = v; } };
  vm.createContext(c); vm.runInContext(m[0] + '\nthis.cloneDown = cloneDown;', c); return c;
}
test('an 8-row selection cloned down 6 times with the song below it: each pass is 8 new filled rows, the song below keeps its order, the selection follows', () => {
  const c = rigSel(), tail = []; for (let y = 8; y < 28; y++) tail.push(y + 1);
  for (let n = 1; n <= 6; n++) {
    c.cloneDown();
    const d0 = 8 * n; assert.equal(c.selA.y, d0, 'pass ' + n + ': the selection moved down 8'); assert.equal(c.cs.song.y, d0 + 7);
    for (let y = d0; y < d0 + 8; y++) { assert.ok(c.song[y][0] >= 1000, `pass ${n}: row ${y} holds a clone, not an empty cell`); assert.equal(c.cloneOf[c.song[y][0]], c.song[y - 8][0], 'pass ' + n + ': a clone of the rows just above'); }
    assert.deepEqual(c.song.slice(8 * (n + 1), 8 * (n + 1) + 20).map(r => r[0]), tail, 'pass ' + n + ': the song that was below moved down intact');
  }
  assert.ok(!c.song.slice(0, 8 * 7 + 20).some(r => r[0] < 0), 'no empty row anywhere in the song');
});
test('with nothing below the selection the clones go straight under it (no row inserted), 4 passes', () => {
  const c = rigSel({ below: 0 }); for (let n = 1; n <= 4; n++) { c.cloneDown(); for (let y = 8 * n; y < 8 * n + 8; y++) assert.ok(c.song[y][0] >= 1000, 'pass ' + n + ' row ' + y); }
  assert.ok(c.song[40][0] < 0, 'and nothing past the last clone');
});
