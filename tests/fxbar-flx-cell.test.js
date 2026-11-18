// FX BAR → COMMAND CELL (18 Nov, owner: "changing the flx effect in the fx bar changes the number in the fx column").
// flxCellAt / flxCellVal are cut out of index.html and run against a stand-in phrase; the bar's setter is checked as source.
// The end-to-end run (drag on the bar in headless Chrome, undo, drawn text, both sequencers' events) was measured by hand:
// see the commit message. This is what the node harness can hold.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, `index.html: could not find ${what}`); return m[0]; };
const code = cut(/function flxCellAt\(t\)\{[\s\S]*?\nconst flxCellVal = [^\n]*\n/, 'flxCellAt / flxCellVal');
const FXC = ['VOL', 'FLX', 'FLY', 'GAT'], FX0 = 6, RW = 12, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function make(state) {
  const f = new Function('st', 'FXC', 'FX0', 'RW', 'clamp', 'plen', `const S = st.S, cs = st.cs; let tv = st.tv, cTrack = st.cTrack, cPhrase = st.cPhrase; ${code}; return {flxCellAt, flxCellVal};`);
  return f(state, FXC, FX0, RW, clamp, () => 16);
}
const row = () => new Array(RW).fill(-1);
const mk = () => { const p = Array.from({ length: 32 }, row); p[2][FX0] = 1; p[2][FX0 + 1] = 0x10; p[3][FX0] = 2; p[3][FX0 + 1] = 5; p[4][FX0] = 0; p[4][FX0 + 1] = 0x80; p[5][FX0] = 1; p[5][FX0 + 1] = 0x20; p[5][FX0 + 2] = 1; p[5][FX0 + 3] = 0x30; p[0][0] = 48;
  return { S: { phrases: [p] }, cs: { phrase: { x: FX0, y: 2 } }, tv: 'phrase', cTrack: 0, cPhrase: 0 }; };

test('the FLX cell under the cursor: its command half and its value half, in the track whose Flex the bar shows', () => {
  for (const x of [FX0, FX0 + 1]) { const st = mk(); st.cs.phrase.x = x; const k = make(st).flxCellAt(0); assert.ok(k, 'x ' + x); assert.equal(k.m, FX0); assert.equal(k.r[k.m + 1], 0x10); }
  const st = mk(); st.cs.phrase.x = FX0 + 3; st.cs.phrase.y = 5; const k = make(st).flxCellAt(0); assert.equal(k.m, FX0 + 2, 'the second command column of the row'); assert.equal(k.r[k.m + 1], 0x30);
  const mt = mk(); mt.cTrack = 32; assert.ok(make(mt).flxCellAt(32), 'master (t = NT = 32) is the same');
});

test('nothing else is a FLX cell: a note, FLY, another command, an empty row, another track or screen, past the phrase length', () => {
  const none = (mut, t = 0) => { const st = mk(); mut(st); assert.equal(make(st).flxCellAt(t), null); };
  none(st => { st.cs.phrase.x = 0; st.cs.phrase.y = 0; });          // a note
  none(st => { st.cs.phrase.y = 3; });                              // FLY (the bar's pattern list is FLX 00–FF)
  none(st => { st.cs.phrase.y = 4; });                              // VOL
  none(st => { st.cs.phrase.y = 9; });                              // empty row
  none(st => { st.cs.phrase.y = 20; });                             // past LEN
  none(st => { st.cs.phrase.y = -1; });
  none(st => { st.cTrack = 3; });                                   // the cursor is in another track's phrase
  none(st => { st.tv = 'song'; });                                  // not the phrase screen
  assert.equal(make(mk()).flxCellVal(0, { type: 'flex' }), 0x10);
  assert.equal(make(mk()).flxCellVal(0, { type: 'delay' }), null, 'only a Flex slot');
});

test('the bar row for a Flex PATTERN writes the cell with the effect, and nothing is written when there is no cell', () => {
  const set = cut(/const set = x => \{ const g = getF\(\); if \(!g\) return; x = clamp\(Math\.round\(x\), q\.min, q\.max\);[^\n]*\n[^\n]*\n/, 'the FX bar setter');
  assert.match(set, /q\.k === 'pat' && g\.type === 'flex' \? flxCellAt\(mst \? NT : dkTrack\(\)\) : null/, 'only the Flex PATTERN row looks for a cell');
  assert.match(set, /g\[q\.k\] = x; if \(cell\) cell\.r\[cell\.m \+ 1\] = x;/, 'the effect as before, the cell as well, in the same step as fxChanged');
  assert.ok(set.indexOf('cell.r[cell.m + 1] = x') < set.indexOf('fxChanged('), 'the cell is written before the one save');
  assert.match(src, /const cur = g => \(q\.k === 'pat' && flxCellVal\(mst \? NT : dkTrack\(\), g\)\) \?\? g\[q\.k\]/, 'the row shows the cell under the cursor');
  for (const re of [/r\._fbSet = d => \{ const g = getF\(\); if \(g\) set\(cur\(g\) \+ d\)/, /let x = g \? cur\(g\) : q\.d/, /set\(cur\(g\) \+ d \* wstep\(e\)\)/, /fbPop\(v, q, getF, show, set, cur\)/])
    assert.match(src, re, String(re));
});
