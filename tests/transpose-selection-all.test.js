// THE COMMAND BAR'S TRANSP. + / - TRANSPOSE EVERYTHING IN A SELECTION (21 Nov, owner: "Transposing a selection with the + / - buttons in the command bar transposes everything in the selection").
// The buttons run transposeSel, as the keys do. Left out before: a selection on the Sect screen (it said "Transpose works on a phrase, or on blocks on the Song" and moved nothing),
// and bus tracks were not skipped (a block on a bus track, the other tracks of a Pattern view selection). xposeScope / transposeSel are cut out of index.html and run in a vm.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function xposeScope\(\)\{[\s\S]*?\n\}\nfunction transposeSel\(d, needSel, songOk\)\{[\s\S]*?\n\}\n/);
assert.ok(m, 'could not find xposeScope / transposeSel: the code moved, this test needs updating');
const MT = 16, NCOL = 4, BUS = 3;
const P = (...rows) => rows.map(r => [...r, -1, -1, -1, -1].slice(0, 4).concat([-1, -1]));
const chain = (...ps) => ps.map(p => [p, 0]);
// tv 'sect': the block's chain rows are the selection (sel y..cursor y); tv 'phrase': the open phrase, optionally a pattern selection with other tracks (parts)
function rig({ tv = 'sect', chains = {}, phrases = {}, cSect = 1, sel = null, cursorY = 0, cTrack = 0, poly = () => false, cells = [], parts = [], pat = false, selX = 0, curX = 0 } = {}) {
  const song = []; for (let r = 0; r < 256; r++) song.push(new Array(MT + 1).fill(-1)); for (const [y, t, c] of cells) song[y][t] = c;
  const log = { toast: [], saves: 0, bus: 0 };
  const c = { tv, S: { song, chains: JSON.parse(JSON.stringify(chains)), phrases: JSON.parse(JSON.stringify(phrases)) }, cSect, cTrack, cPhrase: 10, cs: { song: { x: 0, y: 0 }, sect: { x: 0, y: cursorY }, phrase: { x: curX, y: cursorY } },
    selA: sel == null ? null : { v: tv, x: selX, y: sel, pat }, sxPicks: [], MT, NCOL, NOTE_COLS: [0, 1, 2, 3], isPolyT: poly, plen: p => (c.S.phrases[p] || []).length, polyView: () => poly(cTrack),
    clamp: (v, a, b) => Math.max(a, Math.min(b, v)), hp: n => n.toString(16).toUpperCase().padStart(2, '0'), keyHint: s => s, onPset: () => false, mstView: () => false, freshPhrase: () => P([-1]),
    busTrack: t => t === BUS, busSay: () => { log.bus++; return true; }, patParts: () => parts, songCells: () => [[0, 0]],
    selRect: () => ({ x0: Math.min(selX, curX), x1: Math.max(selX, curX), y0: Math.min(sel, cursorY), y1: Math.max(sel, cursorY) }),
    toast: t => log.toast.push(t), saveS: () => { log.saves++; }, draw: () => {} };
  vm.createContext(c); vm.runInContext(m[0] + '\nthis.transposeSel = transposeSel;', c);
  return { c, log, notes: p => c.S.phrases[p].map(r => r[0]), click: d => c.transposeSel(d, true) };   // the TRANSP. buttons: transposeSel(±1, true)
}

test('Sect screen: the buttons transpose the phrase of every selected chain row, each once, in one undo step', () => {
  const r = rig({ chains: { 1: chain(10, 11, 10, 12) }, phrases: { 10: P([60], [64]), 11: P([48]), 12: P([50]) }, sel: 0, cursorY: 2 });
  r.click(1);
  assert.deepEqual(r.notes(10), [61, 65], 'phrase 10 is on rows 0 and 2: +1 once'); assert.deepEqual(r.notes(11), [49]); assert.deepEqual(r.notes(12), [50], 'row 3 is outside the selection');
  assert.equal(r.log.saves, 1, 'one undo step'); assert.equal(r.log.toast.length, 1); assert.equal(r.log.toast[0], 'Transposed 2 patterns +1');
});
test('Sect screen: a poly track moves all four note columns; a mono track column 0 only; the minus button goes down and clamps', () => {
  const ph = { 10: [[1, 40, 41, 42, -1, -1], [119, -1, 5, -1, -1, -1]] };
  const poly = rig({ chains: { 1: chain(10) }, phrases: ph, sel: 0, cursorY: 0, poly: () => true }); poly.click(-1);
  assert.deepEqual(poly.c.S.phrases[10].map(x => x.slice(0, 4)), [[0, 39, 40, 41], [118, -1, 4, -1]]);
  const mono = rig({ chains: { 1: chain(10) }, phrases: ph, sel: 0, cursorY: 0 }); mono.click(1);
  assert.deepEqual(mono.c.S.phrases[10].map(x => x.slice(0, 4)), [[2, 40, 41, 42], [119, -1, 5, -1]]);
});
test('Sect screen: a phrase another block on the song plays too changes there as well, and the one notice says so', () => {
  const r = rig({ chains: { 1: chain(10), 2: chain(10) }, cells: [[0, 0, 1], [5, 1, 2]], phrases: { 10: P([60]) }, sel: 0, cursorY: 0 }); r.click(2);
  assert.equal(r.notes(10)[0], 62); assert.equal(r.log.toast[0], 'Transposed 1 pattern +2 · 1 is also used elsewhere');
});
test('Sect screen: rows with no phrase, or a bus track, change nothing; with no selection the old message stays', () => {
  const e = rig({ chains: { 1: [[-1, 0], [-1, 0]] }, phrases: { 10: P([60]) }, sel: 0, cursorY: 1 }); e.click(1); assert.equal(e.log.saves, 0); assert.match(e.log.toast[0], /No phrase in the selected rows/);
  const b = rig({ chains: { 1: chain(10) }, phrases: { 10: P([60]) }, sel: 0, cursorY: 0, cTrack: BUS }); b.click(1); assert.equal(b.notes(10)[0], 60); assert.equal(b.log.saves, 0); assert.equal(b.log.bus, 1);
  const n = rig({ chains: { 1: chain(10) }, phrases: { 10: P([60]) }, cursorY: 0 }); n.c.transposeSel(1, false); assert.equal(n.notes(10)[0], 60); assert.equal(n.log.toast[0], 'Transpose works on a phrase, or on blocks on the Song.');
});
test('Song: a block on a bus track is left alone (notes never go into a bus), the others move', () => {
  const r = rig({ tv: 'song', cells: [[0, 0, 1], [0, BUS, 2]], chains: { 1: chain(10), 2: chain(11) }, phrases: { 10: P([60]), 11: P([70]) } });
  r.c.selA = { v: 'song', x: 0, y: 0 }; r.c.cs.song = { x: BUS, y: 0 }; r.c.songCells = () => [[0, 0], [0, BUS]]; r.click(1);
  assert.equal(r.notes(10)[0], 61); assert.equal(r.notes(11)[0], 70); assert.equal(r.log.saves, 1);
});
test('Phrase: a pattern selection (every track on the row) moves each track once but not a bus track’s phrase; a selection on a bus track says so', () => {
  const ph = { 10: P([60], [62]), 11: P([48], [50]), 12: P([70], [72]) };
  const r = rig({ tv: 'phrase', phrases: ph, sel: 0, cursorY: 1, pat: true, parts: [{ t: 1, p: 11 }, { t: BUS, p: 12 }] }); r.click(1);
  assert.deepEqual(r.notes(10), [61, 63]); assert.deepEqual(r.notes(11), [49, 51]); assert.deepEqual(r.notes(12), [70, 72], 'the bus track’s phrase is untouched'); assert.equal(r.log.saves, 1);
  const b = rig({ tv: 'phrase', phrases: ph, sel: 0, cursorY: 1, cTrack: BUS }); b.click(1); assert.equal(b.notes(10)[0], 60); assert.equal(b.log.saves, 0); assert.equal(b.log.bus, 1);
});
test('source: the command bar buttons run transposeSel with a selection required (the keys’ path), and the Sect branch is the Sect screen only', () => {
  assert.match(src, /\$\('tXposeDn'\)\.addEventListener\('click', \(\) => apRun\(transposeSel, \[-1, true\]\)\)/);
  assert.match(src, /\$\('tXposeUp'\)\.addEventListener\('click', \(\) => apRun\(transposeSel, \[1, true\]\)\)/);
  assert.match(src, /if \(tv === 'sect' && selA && selA\.v === 'sect'\)\{/);
});
