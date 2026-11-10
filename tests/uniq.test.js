// UNIQUE PER TRACK (blockMixed in index.html): a block gets a copy only the first time it comes onto a track.
// The function is cut out of the real file and run in a vm with small stand-ins for S, MT and SX.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const m = H.read('index.html').match(/function blockMixed\(cid, x, y\)\{[\s\S]*?\n\}\n/);
assert.ok(m, 'index.html: could not find blockMixed; the code moved, update this test');
const MT = 8;
function mk(uniq, cells) {   // cells: [row, track, cid]
  const S = { song: [] }; for (let r = 0; r < 256; r++) S.song.push(new Array(MT + 1).fill(-1));
  for (const [r, t, c] of cells) S.song[r][t] = c;
  const c = { S, MT, SX: { uniq } }; vm.createContext(c); vm.runInContext(m[0], c); return c;
}
test('another track holds it, this track does not: mixed (copy)', () => assert.equal(mk(true, [[0, 1, 5]]).blockMixed(5, 2, 3), true));
test('this track already holds it elsewhere: not mixed, even if another track holds it too', () => {
  assert.equal(mk(true, [[0, 2, 5], [1, 1, 5]]).blockMixed(5, 2, 3), false);
  assert.equal(mk(true, [[0, 2, 5]]).blockMixed(5, 2, 3), false);
});
test('only this track holds it: not mixed; rewriting its only cell while another track holds it: mixed', () => {
  assert.equal(mk(true, [[0, 2, 5]]).blockMixed(5, 2, 4), false);
  assert.equal(mk(true, [[0, 2, 5], [1, 1, 5]]).blockMixed(5, 2, 0), true, 'the only copy on this track is the cell itself');
});
test('master column cases unchanged', () => {
  assert.equal(mk(true, [[0, MT, 5]]).blockMixed(5, 2, 3), true, 'track cell, block in master column of another row');
  assert.equal(mk(true, [[0, MT, 5], [1, 2, 5]]).blockMixed(5, 2, 3), true, 'master guard beats "already on this track"');
  assert.equal(mk(true, [[0, 1, 5]]).blockMixed(5, MT, 3), true, 'master cell, block on a track');
  assert.equal(mk(true, [[0, MT, 5]]).blockMixed(5, MT, 3), false);
});
test('setting off: only the master cases are mixed', () => {
  assert.equal(mk(false, [[0, 1, 5]]).blockMixed(5, 2, 3), false);
  assert.equal(mk(false, [[0, MT, 5]]).blockMixed(5, 2, 3), true);
  assert.equal(mk(false, [[0, 1, 5]]).blockMixed(5, MT, 3), true);
});
