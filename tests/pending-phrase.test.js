// A PENDING PHRASE IS NEVER DROPPED WITH NOTES IN IT (12 Nov, owner's lost notes: new song, notes entered into a new phrase, PLAY, "they vanished while it was open").
// placePending used to forget a pending phrase when the cursor's phrase had changed or the cell had been filled, leaving the notes in a phrase on no block.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const fn = src.match(/function placePending\(\)\{[\s\S]*?\n\}\n/); assert.ok(fn, 'placePending not found');
function rig({ cPhrase = 0, pend = { x: 0, y: 0, p: 0 }, notes = true, taken = [], onSong = null } = {}) {
  const log = [], S = { song: Array.from({ length: 256 }, () => Array(17).fill(-1)), chains: [], phrases: [] };
  S.phrases[pend ? pend.p : 0] = Array.from({ length: 16 }, (_, r) => Array(8).fill(notes && r === 0 ? 60 : -1));
  for (const [x, y] of taken) { S.song[y][x] = 5; S.chains[5] = [[9]]; }
  if (onSong != null) { S.song[onSong.y][onSong.x] = 6; S.chains[6] = [[pend.p]]; }
  const c = { S, cPhrase, pendPlace: pend, log, isEmptyPhrase: p => !p || p.every(r => r.every(v => v < 0)), hp: n => 'P' + n, rS: n => 'R' + n, d2: n => 'T' + n,
    toast: t => log.push('toast ' + t), toastErr: t => log.push('err ' + t),
    setSongPhrase: (x, y, p) => { S.song[y][x] = 100 + p; S.chains[100 + p] = [[p]]; log.push(`set ${x},${y},${p}`); } };
  vm.createContext(c); vm.runInContext(fn[0] + '\nthis.placePending = placePending;', c); return c;
}
test('the ordinary case: a note in the open pending phrase puts it on its cell', () => {
  const c = rig(); c.placePending(); assert.deepEqual(c.log.slice(0, 1), ['set 0,0,0']); assert.equal(c.pendPlace, null); assert.match(c.log[1], /is on the song: row R0, track T1/);
});
test('the cursor moved to another phrase before the microtask ran: the notes still go on the song (they used to be dropped)', () => {
  const c = rig({ cPhrase: 3 }); c.placePending(); assert.equal(c.log[0], 'set 0,0,0'); assert.equal(c.pendPlace, null);
});
test('the cell was filled meanwhile: first free row of the same track, and the toast says which row was taken', () => {
  const c = rig({ taken: [[0, 0], [0, 1]] }); c.placePending(); assert.equal(c.log[0], 'set 0,2,0'); assert.match(c.log[1], /row R2, track T1: row R0 was taken/);
});
test('a taken cell at the bottom wraps round to the top of the track', () => {
  const c = rig({ pend: { x: 2, y: 255, p: 4 }, taken: [[2, 255]] }); c.placePending(); assert.equal(c.log[0], 'set 2,0,4');
});
test('already on the song through another path: nothing placed twice, the waiting phrase is cleared', () => {
  const c = rig({ onSong: { x: 3, y: 4 } }); c.placePending(); assert.deepEqual(c.log, []); assert.equal(c.pendPlace, null);
});
test('an untouched pending phrase: kept while it is the open one on a free cell, forgotten when it is not or the cell is taken', () => {
  let c = rig({ notes: false }); c.placePending(); assert.deepEqual(c.log, []); assert.ok(c.pendPlace);
  c = rig({ notes: false, cPhrase: 2 }); c.placePending(); assert.equal(c.pendPlace, null); assert.deepEqual(c.log, []);
  c = rig({ notes: false, taken: [[0, 0]] }); c.placePending(); assert.equal(c.pendPlace, null);
});
test('no free row at all: a visible error, not a silent drop', () => {
  const c = rig({ taken: Array.from({ length: 256 }, (_, y) => [0, y]) }); c.placePending(); assert.equal(c.log.length, 1); assert.match(c.log[0], /^err Phrase P0 has notes but track T1 has no free song row/);
});
test('wiring: setSong forgets a phrase waiting in the old song; the three silent drops are gone', () => {
  assert.match(src, /function setSong\(s, msg\)\{ tStop\(true\); tp\.paused = null; pendPlace = null;/);
  const body = fn[0]; assert.doesNotMatch(body, /q\.p !== cPhrase\)\{ pendPlace = null; return; \}\n/);
});
test('nav(): SHIFT+▶ from an empty Arrange / Song cell does not let go() fill the cell before the pending phrase is made for it (reproduced: notes in a phrase on no block)', () => {
  const m = src.match(/if \(tv === 'song' && !sectsOn\)\{ cTrack = cs\.song\.x; const x = cs\.song\.x, y = cs\.song\.y, e = S\.song\[y\]\[x\] < 0; ([^\n]*?)if \(!e\) return null;/);
  assert.ok(m, 'the empty-block path of nav is there');
  assert.match(m[1], /^goNoNew = e; try \{ go\('phrase'\); \} finally \{ goNoNew = false; \} $/, 'go() runs with goNoNew set for an empty cell');
  assert.match(src, /&& !sectsOn && !goNoNew\) newBlock\(/, 'go() honours goNoNew');
});
