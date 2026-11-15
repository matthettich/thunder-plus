// USED ON (build 609): the Synth page names the tracks that play the selected pad / synth slot (instrument column or INS, through the instruments on that slot); 'unused' when none.
// padTracks / padUsedText are cut out of index.html and run in a vm with a small song.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const src = require('./helpers').read('index.html');
const cut = re => { const m = src.match(re); assert.ok(m, String(re)); return m[0]; };
const code = cut(/let padUseM = null;\nfunction padTracks\(\)[\s\S]*?\nfunction padUsedText\(i\)[^\n]*/);
const mkW = () => { const S = { insts: [{ pad: 0 }, { pad: 8 }, { pad: 8 }, { pad: 3 }], phrases: [], mix: [] };
  const rowE = () => [-1, -1, -1, -1, -1, -1, -1, 0, -1, 0, -1, 0];
  const ph = (inst, ins) => Array.from({ length: 4 }, (_, y) => { const r = rowE(); if (y === 0 && inst != null) r[5] = inst; if (y === 1 && ins != null){ r[6] = 9; r[7] = ins; } return r; });
  S.phrases[0] = ph(1); S.phrases[1] = ph(null, 2); S.phrases[2] = ph(0);
  const m = new Map([[0, new Set([0])], [1, new Set([2, 5])], [2, new Set([1])]]);
  const ctx = vm.createContext({ S, songVer: 1, IC: 5, FX0: 6, RW: 12, FXC: { 9: 'INS' }, Map, Set, Array, d2: n => String(n).padStart(2, '0'), plen: () => 4, phraseTracks: () => m, tName: t => t === 0 ? 'KICK' : '' });
  vm.runInContext(code, ctx); return { S, ctx, T: i => vm.runInContext('padUsedText(' + i + ')', ctx) }; };
test('tracks per slot: instrument column and INS, several instruments on one slot merge, names shown, unused otherwise', () => {
  const { T } = mkW();
  assert.equal(T(8), 'Tracks 01 KICK · 03 · 06', 'inst 1 (column, phrase 0: track 1) + inst 2 (INS, phrase 1: tracks 3, 6), both on slot 8');
  assert.equal(T(0), 'Track 02'); assert.equal(T(3), 'unused', 'inst 3 is in no phrase'); assert.equal(T(5), 'unused');
});

test('memoised on songVer; recomputed after an edit', () => {
  const { S, ctx, T } = mkW(); assert.equal(T(0), 'Track 02'); S.insts[0].pad = 4; assert.equal(T(4), 'Track 02', 'the instruments pads are part of the key');
  S.phrases[2][0][5] = 3; assert.equal(T(3), 'unused', 'same songVer: still the old answer'); ctx.songVer = 2; assert.equal(T(3), 'Track 02');
});
test('source: the read-out is in the pad header, hidden on the Mobile setup, set on selection and on the Synth page opening; the pad tooltip has it', () => {
  assert.match(src, /id="vUsed"/); assert.match(src, /body\.mob \.vused\{display:none\}/);
  assert.match(src, /renderPads\(\); renderFx\(\); try \{ preLabels\(\); \} catch\(e\)\{\}\n  usedUI\(\);/);
  assert.match(src, /if \(view === 'synth'\) try \{ usedUI\(\); renderPads\(\); \}/);
  assert.match(src, /'unused in the song' : 'used on '/);
});
