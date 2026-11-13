// TRANSPOSE ON THE SONG / ARRANGE (12 Nov, owner: "If in arrangement mode, it would be nice to select patterns and be able to use the transpose keys on every pattern
// selected"). xposeScope() and transposeSel() are cut out of index.html and run in a vm over a small song: the selected blocks, the picked ones, else the block under the
// cursor; each pattern once; the pattern itself changes (so other blocks playing it do too, and the toast says so); one saveS (= one undo step); the selection stays.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function xposeScope\(\)\{[\s\S]*?\n\}\nfunction transposeSel\(d, needSel, songOk\)\{[\s\S]*?\n\}\n/);
assert.ok(m, 'could not find xposeScope / transposeSel: the code moved, this test needs updating');
const OFF = 120, MT = 16, NCOL = 4;

// cells: [row, track, blockId]; chains: id -> [[phrase, tp], ...]; phrases: id -> [[n1, n2, n3, n4, ...], ...]
function rig({ cells = [], chains = {}, phrases = {}, sel = null, picks = [], cursor = [0, 0], poly = t => t >= 8 } = {}) {
  const song = []; for (let r = 0; r < 256; r++) song.push(new Array(MT + 1).fill(-1)); for (const [y, t, c] of cells) song[y][t] = c;
  const log = { toast: [], saves: 0, draws: 0 };
  const c = { tv: 'song', S: { song, chains: Object.fromEntries(Object.entries(chains).map(([k, v]) => [k, v.map(r => r.slice())])), phrases: JSON.parse(JSON.stringify(phrases)) },
    cs: { song: { x: cursor[1], y: cursor[0] } }, selA: sel ? Object.assign({ v: 'song' }, sel) : null, sxPicks: picks, MT, NCOL, NOTE_COLS: [0, 1, 2, 3], isPolyT: poly, plen: p => (c.S.phrases[p] || []).length,
    clamp: (v, a, b) => Math.max(a, Math.min(b, v)), hp: n => n.toString(16).toUpperCase().padStart(2, '0'), keyHint: s => s, onPset: () => false, mstView: () => false,
    songCells: () => { const r = { x0: Math.min(sel.x, cursor[1]), x1: Math.max(sel.x, cursor[1]), y0: Math.min(sel.y, cursor[0]), y1: Math.max(sel.y, cursor[0]) }, o = []; for (let y = r.y0; y <= r.y1; y++) for (let t = r.x0; t <= r.x1; t++) o.push([y, t]); return o; },
    toast: t => log.toast.push(t), saveS: () => { log.saves++; }, draw: () => { log.draws++; } };
  vm.createContext(c); vm.runInContext(m[0] + '\nthis.transposeSel = transposeSel;', c);
  return { c, log, notes: p => c.S.phrases[p].map(r => r.slice(0, 4)) };
}
const P = (...rows) => rows.map(r => [...r, -1, -1, -1, -1].slice(0, 4).concat([-1, -1]));   // a phrase of rows with up to 4 note columns
const chain = (...ps) => ps.map(p => [p, 0]);

test('selected blocks: every pattern in them moves, a pattern in several selected blocks only once, and the message counts patterns', () => {
  const r = rig({ cells: [[0, 0, 1], [1, 0, 1], [2, 0, 2], [0, 1, 3]], chains: { 1: chain(10), 2: chain(11), 3: chain(10) }, phrases: { 10: P([60], [64]), 11: P([48]) }, sel: { x: 0, y: 0 }, cursor: [2, 1], poly: () => false });
  r.c.transposeSel(1, true, true);
  assert.deepEqual(r.notes(10).map(x => x[0]), [61, 65], 'moved once (+1, not +2) though blocks 1 and 3 both play it');
  assert.deepEqual(r.notes(11).map(x => x[0]), [49]); assert.equal(r.log.saves, 1, 'one saveS = one undo step'); assert.equal(r.log.toast.at(-1), 'Transposed 2 patterns +1');
  assert.ok(r.c.selA, 'the selection stays');
});
test('a pattern that other blocks outside the selection play: it changes there too, and the message says how many', () => {
  const r = rig({ cells: [[0, 0, 1], [9, 0, 1], [1, 0, 2], [9, 1, 2]], chains: { 1: chain(10), 2: chain(11) }, phrases: { 10: P([60]), 11: P([50]) }, sel: { x: 0, y: 0 }, cursor: [1, 0], poly: () => false });
  r.c.transposeSel(-12, true, true);
  assert.deepEqual([r.notes(10)[0][0], r.notes(11)[0][0]], [48, 38]); assert.equal(r.log.toast.at(-1), 'Transposed 2 patterns −12 · 2 are also used elsewhere');
  const one = rig({ cells: [[0, 0, 1], [9, 1, 1]], chains: { 1: chain(10) }, phrases: { 10: P([60]) }, cursor: [0, 0], poly: () => false }); one.c.transposeSel(1, false);
  assert.equal(one.log.toast.at(-1), 'Transposed 1 pattern +1 · 1 is also used elsewhere');
});
test('no selection: the block under the cursor (the pad works without a selection on the Song; the top-bar buttons and the lock still want one)', () => {
  const r = rig({ cells: [[3, 2, 1], [4, 2, 2]], chains: { 1: chain(10), 2: chain(11) }, phrases: { 10: P([60]), 11: P([60]) }, cursor: [3, 2], poly: () => false });
  r.c.transposeSel(1, true, true); assert.deepEqual([r.notes(10)[0][0], r.notes(11)[0][0]], [61, 60]); assert.equal(r.log.saves, 1);
  const no = rig({ cells: [[3, 2, 1]], chains: { 1: chain(10) }, phrases: { 10: P([60]) }, cursor: [3, 2], poly: () => false }); no.c.transposeSel(1, true);
  assert.deepEqual(no.log.toast, ['Select rows first (or alt+, / alt+. for the whole block).']); assert.equal(no.notes(10)[0][0], 60); assert.equal(no.log.saves, 0);
});
test('picked blocks (sxPicks) are included, alone or with a selection; picks take the place of the cursor block', () => {
  const r = rig({ cells: [[0, 0, 1], [5, 3, 2], [8, 4, 3]], chains: { 1: chain(10), 2: chain(11), 3: chain(12) }, phrases: { 10: P([60]), 11: P([60]), 12: P([60]) }, picks: [{ y: 5, t: 3 }], cursor: [0, 0], poly: () => false });
  r.c.transposeSel(1, true, true); assert.deepEqual([10, 11, 12].map(p => r.notes(p)[0][0]), [60, 61, 60], 'the picked block only (the cursor block is not added)');
  const s = rig({ cells: [[0, 0, 1], [5, 3, 2]], chains: { 1: chain(10), 2: chain(11) }, phrases: { 10: P([60]), 11: P([60]) }, picks: [{ y: 5, t: 3 }], sel: { x: 0, y: 0 }, cursor: [0, 0], poly: () => false });
  s.c.transposeSel(2, true, true); assert.deepEqual([10, 11].map(p => s.notes(p)[0][0]), [62, 62], 'selection and picks together');
});
test('empty cells and the master column are skipped; nothing to move says so and makes no undo step', () => {
  const r = rig({ cells: [[0, MT, 1]], chains: { 1: chain(10) }, phrases: { 10: P([60]) }, cursor: [0, MT] }); r.c.transposeSel(1, true, true);
  assert.deepEqual(r.log.toast, ['No block here to transpose.']); assert.equal(r.notes(10)[0][0], 60); assert.equal(r.log.saves, 0);
  const e = rig({ cursor: [7, 2] }); e.c.transposeSel(1, true, true); assert.deepEqual(e.log.toast, ['No block here to transpose.']);
  const none = rig({ cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: P([OFF], [-1]) }, cursor: [0, 0], poly: () => false }); none.c.transposeSel(1, true, true);
  assert.match(none.log.toast[0], /^Nothing to transpose/); assert.equal(none.log.saves, 0);
});
test('the phrase editor’s rules per phrase: only note values move, OFF stays, 0 and 119 clamp, a mono track moves its one note column, a poly track all four', () => {
  const mono = rig({ cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: [[119, 30, -1, -1, -1, -1], [OFF, 30, -1, -1, -1, -1], [0, -1, -1, -1, -1, -1]] }, cursor: [0, 0], poly: () => false });
  mono.c.transposeSel(1, true, true); assert.deepEqual(mono.notes(10), [[119, 30, -1, -1], [OFF, 30, -1, -1], [1, -1, -1, -1]], 'column 1 is not a note column on a mono track');
  const poly = rig({ cells: [[0, 8, 1]], chains: { 1: chain(10) }, phrases: { 10: [[60, 64, 67, -1, -1, -1]] }, cursor: [0, 8], poly: () => true });
  poly.c.transposeSel(-1, true, true); assert.deepEqual(poly.notes(10), [[59, 63, 66, -1]]);
});
test('a block with several phrases moves all of them', () => {
  const r = rig({ cells: [[0, 0, 1]], chains: { 1: chain(10, 11, 10) }, phrases: { 10: P([60]), 11: P([62]) }, cursor: [0, 0], poly: () => false }); r.c.transposeSel(1, false);
  assert.deepEqual([r.notes(10)[0][0], r.notes(11)[0][0]], [61, 63]); assert.equal(r.log.toast.at(-1), 'Transposed 2 patterns +1');
});
test('wiring: the pad’s SEMI keys pass songOk; the help lists the Song behaviour', () => {
  assert.match(src, /if \(b === 'xposeUp'\)\{ transposeSel\(1, true, true\);/); assert.match(src, /if \(b === 'xposeDn'\)\{ transposeSel\(-1, true, true\);/);
  assert.match(src, /<dt>Alt\+, Alt\+\.<\/dt><dd>[^<]*on the Song \/ Arrange the selected and picked blocks/);
  assert.match(src, /<dt>SEMI \+ −<\/dt><dd>[^<]*on the Song \/ Arrange, the selected blocks/);
});
