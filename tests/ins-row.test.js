// INS (12 Nov, owner: "Add an 'ins' button to the control bar that inserts an empty row below it ... Add menu command for the same on desktop").
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const eqj = (a, b) => assert.equal(JSON.stringify(a), JSON.stringify(b));
const grab = re => { const m = src.match(re); assert.ok(m, 'not found: ' + re); return m[0]; };
const code = grab(/function smuteInsertRows\(at, h\)\{[\s\S]*?\n\}\n/) + grab(/function songRowsInsert\(at, h\)\{[\s\S]*?\n\}\n/) + grab(/function insRowBelow\(\)\{[\s\S]*?\n\}\n/);
function rig(tv) {
  const log = [], NT = 4;
  const S = { song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), smute: new Array(256).fill(0), secs: [{ y: 1, n: 'B' }, { y: 5, n: 'C' }], rcol: { 2: 7 } };
  S.song[0][0] = 1; S.song[1][0] = 2; S.song[2][1] = 3;
  const c = { S, NT, tv, cs: { song: { x: 0, y: 1 } }, loopArm: null, log, onPset: () => false, saveS: () => log.push('save'), draw() {}, toast: m => log.push(m), rS: n => 'r' + n, hx: n => n.toString(16), selA: null,
    edBlock: () => false, rowOp: o => log.push('rowOp ' + o + ' at ' + c.cs.phrase.y), rowsArr: () => new Array(32).fill(0), plen: () => 16, cPhrase: 0, cs2: 0 };
  c.cs.phrase = { x: 0, y: 3 }; c.cs.sect = { x: 0, y: 0 };
  vm.createContext(c); vm.runInContext(code + '\nthis.insRowBelow = insRowBelow;', c); return c;
}
test('Song: an empty row goes in BELOW the cursor row, every track; the rows below, the sections and the row colours move down; the cursor stays', () => {
  const c = rig('song'); c.insRowBelow();
  eqj(c.S.song[1], [2, -1, -1, -1, -1]); eqj(c.S.song[2], new Array(5).fill(-1)); assert.equal(c.S.song[3][1], 3);
  assert.equal(c.S.song.length, 256); assert.equal(c.cs.song.y, 1); eqj(c.S.secs, [{ y: 1, n: 'B' }, { y: 6, n: 'C' }]); eqj(c.S.rcol, { 3: 7 });
  assert.equal(c.log[0], 'save', 'one saveS = one undo step');
});
test('Song: the last row with a block refuses, and writes nothing', () => {
  const c = rig('song'); c.S.song[255][2] = 9; c.insRowBelow();
  assert.match(c.log.join('|'), /song is full/); assert.ok(!c.log.includes('save')); assert.equal(c.S.song[1][0], 2);
});
test('Phrase: rowOp(insert) one row down (its rules), the cursor back on its own row; a selection or the last row refuses', () => {
  const c = rig('phrase'); c.tv = 'phrase'; c.insRowBelow();
  assert.equal(c.log[0], 'rowOp insert at 4'); assert.equal(c.cs.phrase.y, 3);
  const d = rig('phrase'); d.tv = 'phrase'; d.selA = { v: 'phrase' }; d.insRowBelow(); assert.match(d.log[0], /drop the selection/);
  const e = rig('phrase'); e.tv = 'phrase'; e.cs.phrase.y = 15; e.insRowBelow(); assert.match(e.log[0], /last row/);
});
test('wiring: bar button, menu item, key, right-click', () => {
  assert.match(src, /if \(c === 'ins'\) return insRowBelow\(\);/);
  assert.match(src, /\['ins', 'INS', 'Insert an empty row below/);
  assert.match(src, /it\('ins', 'Insert row below \(rows below move down\)', 'mod\+alt\+Enter'/);
  assert.match(src, /if \(k === 'Enter' && e\.altKey && !e\.shiftKey\)\{ if \(!e\.repeat\) insRowBelow\(\); return true; \}/);
  assert.match(src, /l:'Insert row below ' \+ rS\(r\.y1, 3\)/);
});
