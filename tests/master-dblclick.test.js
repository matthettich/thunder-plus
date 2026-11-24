// MASTER CELL DOUBLE-CLICK (build 657, owner 19 Nov): a double-click / double-tap on an empty MASTER cell of Arrange or the Song drops an empty block; on a row
// whose visible tracks are all empty it fills the whole row (every visible track and the master) with new empty blocks. masterDrop, freePair and the
// emptiness tests are cut out of index.html and run in a vm with stand-ins; the wiring (Arrange's double-click, the Song's Enter / EDIT) is checked in the source.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = re => { const m = src.match(re); assert.ok(m, 'moved: ' + re); return m[0]; };
const code = [
  cut(/const isEmptyPhrase = [^\n]*\n/), cut(/const isEmptySect = [^\n]*\n/),
  cut(/function freePair\(\)\{[\s\S]*?\n\}\n/), cut(/function masterDrop\(y\)\{[\s\S]*?\n\}\n/),
].join('\n');
const NT = 32, MT = NT, NPH = 64;
function rig(o) {
  o = o || {};
  const S = { song: Array.from({ length: 12 }, () => new Array(MT + 1).fill(-1)), chains: new Array(NPH).fill(null), phrases: new Array(NPH).fill(null), plen: new Array(NPH).fill(0), plpb: new Array(NPH).fill(0), dlen: 16 };
  const c = { S, NPH, MT, NA: o.NA || 19, cs: { song: { x: 0, y: 0 } }, pendPlace: null, sectPicked: 5, MODK: 'Ctrl', toasts: [], saves: 0, draws: 0, newBlocks: [],
    ntr: () => c.NA, rS: (y, n) => String(y).padStart(n, '0'), freshSect: () => Array.from({ length: 16 }, () => [-1, 0]), freshPhrase: () => [[-1, -1], [-1, -1]],
    toast: t => c.toasts.push(t), saveS: () => { c.saves++; }, draw: () => { c.draws++; },
    newBlock: (p) => { c.newBlocks.push(p); const [cid, pid] = c.freePair(); c.S.chains[cid] = c.freshSect(); c.S.chains[cid][0][0] = pid; c.S.phrases[pid] = c.freshPhrase(); c.S.song[p.y][p.x] = cid; } };
  vm.createContext(c); vm.runInContext(code + '\nthis.f = masterDrop; this.freePair = freePair;', c); return c;
}
const filledBlock = (c, id) => { c.S.chains[id] = c.freshSect(); c.S.chains[id][0][0] = id; c.S.phrases[id] = [[60, 0], [-1, -1]]; };

test('case B: an empty row fills every visible track and the master with consecutive new empty blocks, in track order', () => {
  const c = rig(); c.f(3); const row = c.S.song[3];
  for (let t = 0; t < 19; t++) assert.equal(row[t], t, 'track ' + t);
  assert.equal(row[MT], 19, 'the master takes the next number');
  for (let t = 19; t < MT; t++) assert.equal(row[t], -1, 'hidden track ' + t + ' untouched');
  for (let i = 0; i <= 19; i++) { assert.equal(c.S.chains[i][0][0], i); assert.ok(c.S.phrases[i].every(r => r.every(v => v < 0)), 'empty phrase'); assert.equal(c.S.plen[i], 16); }
  assert.equal(c.saves, 1, 'one saveS: one undo step'); assert.deepEqual({ ...c.cs.song }, { x: MT, y: 3 }); assert.equal(c.newBlocks.length, 0, 'not through newBlock');
  assert.match(c.toasts[0], /Row 003: an empty phrase on 19 tracks and the master · Ctrl\+Z undoes/);
});
test('case B takes the next free numbers, skipping used ones, still consecutive in track order', () => {
  const c = rig(); c.S.song[0][0] = 0; filledBlock(c, 0); c.S.song[0][1] = 1; filledBlock(c, 1); c.f(5);
  assert.deepEqual(Array.from({ length: 19 }, (_, t) => c.S.song[5][t]), Array.from({ length: 19 }, (_, t) => t + 2)); assert.equal(c.S.song[5][MT], 21);
});
test('case B: tracks past MONO + POLY (hidden) keep their data and get nothing; 3 visible tracks give 4 blocks', () => {
  const c = rig({ NA: 3 }); c.S.song[2][10] = 40; filledBlock(c, 40); c.f(2);
  assert.deepEqual([c.S.song[2][0], c.S.song[2][1], c.S.song[2][2], c.S.song[2][MT]], [0, 1, 2, 3]); assert.equal(c.S.song[2][10], 40); assert.equal(c.S.song[2][3], -1);
  assert.match(c.toasts[0], /3 tracks and the master/);
});
test('case B ignores a hidden track holding data (only visible cells count)', () => {
  const c = rig({ NA: 4 }); c.S.song[1][20] = 9; filledBlock(c, 9); c.f(1); assert.equal(c.S.song[1][MT], 4); assert.equal(c.newBlocks.length, 0);
});
test('not enough free numbers: nothing changes, one toast with needed / free and CLEAR UNUSED PHRASES, never a half row', () => {
  const c = rig(); for (let i = 0; i < NPH - 7; i++) { c.S.song[6][i % MT] = -1; filledBlock(c, i); c.S.song[7 + (i >> 5)][i % 32] = i; }
  const before = JSON.stringify(c.S); c.f(4);
  assert.equal(JSON.stringify(c.S), before, 'the song is identical'); assert.equal(c.saves, 0);
  assert.equal(c.toasts.length, 1); assert.match(c.toasts[0], /needs 20 new phrases .*only 7 are free/); assert.match(c.toasts[0], /CLEAR UNUSED PHRASES/);
});
test('exactly enough numbers still fills the row', () => {
  const c = rig(); for (let i = 20; i < NPH; i++) filledBlock(c, i); c.f(0); assert.equal(c.S.song[0][MT], 19); assert.equal(c.saves, 1);
});
test('one undo step: a snapshot of the song before is restored exactly (one saveS for the whole row)', () => {
  const c = rig(); c.S.song[0][2] = 2; filledBlock(c, 2); const before = JSON.stringify(c.S); c.f(8);
  assert.notEqual(JSON.stringify(c.S), before); assert.equal(c.saves, 1);
  const back = JSON.parse(before); assert.equal(JSON.stringify(back), before);
});
test('case A: one filled track cell in the row gives only the master block, through newBlock', () => {
  const c = rig(); c.S.song[4][7] = 3; filledBlock(c, 3); c.f(4);
  assert.equal(c.newBlocks.length, 1); assert.deepEqual({ ...c.newBlocks[0] }, { x: MT, y: 4 }); assert.notEqual(c.S.song[4][MT], -1);
  for (let t = 0; t < MT; t++) if (t !== 7) assert.equal(c.S.song[4][t], -1); assert.equal(c.saves, 0, 'newBlock saves itself');
});
test('a filled master cell: nothing happens here (the caller opens it)', () => {
  const c = rig(); c.S.song[4][MT] = 9; filledBlock(c, 9); const before = JSON.stringify(c.S); assert.equal(c.f(4), false);
  assert.equal(JSON.stringify(c.S), before); assert.equal(c.newBlocks.length, 0); assert.equal(c.toasts.length, 0);
});
test('wiring: Arrange (on the Song and beside other screens) and the Song screen share masterDrop', () => {
  const arr = cut(/function arrGo\(y, t, sysDbl\)\{[\s\S]*?\n\}\n/), tap = cut(/function editTap\(enter, stamp\)\{[\s\S]*?\n\}\nfunction editDouble/);
  assert.match(arr, /t === MT && asongOn\(\) && tv === 'song' && !sectsOn\)\{ masterDrop\(y\); return; \}/, 'Arrange as the Song screen');
  assert.match(arr, /if \(t === MT\) masterDrop\(y\); else arrNewAt\(y, t\)/, 'Arrange beside another screen');
  assert.match(arr, /sysDbl/); assert.match(arr, /now - arrEmptyTap\.t < 380/, 'the existing double-click timing is reused');
  assert.match(tap, /c\.x === MT \? masterDrop\(c\.y\) : newBlock\(c, -1\)/, 'the Song: Enter / EDIT on an empty master cell');
  assert.match(arr, /filled && \(dbl \|\| tv === 'phrase'\)\)\{ tv = 'song'; go\('phrase'\)/, 'a filled cell still opens its phrase');
});
test('the new code carries no raw U+2028 / U+2029', () => { assert.ok(!new RegExp('[' + String.fromCharCode(0x2028, 0x2029) + ']').test(code)); });
