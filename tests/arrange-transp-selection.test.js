// TRANSP. ON ARRANGE'S SELECTION (14 Nov, owner: "with a selection of blocks in Arrange and Arrange in focus, TRANSP. (next to LP. SEL) transposes every phrase in the selection").
// On the Song screen the buttons already worked (build 569); beside Phrase (Arrange holding the keys, apF === 'arr') the click handlers bypassed apRun and said "Select rows first".
// Now they go through apRun, and the buttons' title says what they will do. xposeScope / transposeSel / xposeBlocks / xposeTitles are cut out of index.html and run in a vm.
// Convention pinned: a phrase also played by a block OUTSIDE the selection is edited in place (the outside block changes too) and ONE toast says how many are shared.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function xposeScope\(\)\{[\s\S]*?\n\}\n(?:\/\*[\s\S]*?\*\/\n)?function xposeBlocks\(\)\{[\s\S]*?\n\}\nfunction xposeTitles\(\)\{[\s\S]*?\n\}\nfunction transposeSel\(d, needSel, songOk\)\{[\s\S]*?\n\}\n/);
assert.ok(m, 'could not find the transpose functions: the code moved, this test needs updating');
const OFF = 120, MT = 16, NCOL = 4;
const P = (...rows) => rows.map(r => [...r, -1, -1, -1, -1].slice(0, 4).concat([-1, -1]));
const chain = (...ps) => ps.map(p => [p, 0]);

// side: Arrange holds the keys beside Phrase (tv 'phrase', selection in apSel); else the Song screen (tv 'song', selection in selA)
function rig({ cells = [], chains = {}, phrases = {}, sel = null, picks = [], cursor = [0, 0], side = false, poly = () => false, tvv } = {}) {
  const song = []; for (let r = 0; r < 256; r++) song.push(new Array(MT + 1).fill(-1)); for (const [y, t, c] of cells) song[y][t] = c;
  const log = { toast: [], saves: 0, draws: 0, undo: [] }, els = { tXposeDn: { attrs: {} }, tXposeUp: { attrs: {} } };
  for (const e of Object.values(els)) e.setAttribute = (k, v) => { e.attrs[k] = v; };
  const c = { tv: tvv || (side ? 'phrase' : 'song'), S: { song, chains: JSON.parse(JSON.stringify(chains)), phrases: JSON.parse(JSON.stringify(phrases)) },
    cs: { song: { x: cursor[1], y: cursor[0] } }, selA: null, apSel: null, apReady: true, apF: side ? 'arr' : null, sxPicks: picks, MT, NCOL, NOTE_COLS: [0, 1, 2, 3], isPolyT: poly, plen: p => (c.S.phrases[p] || []).length,
    clamp: (v, a, b) => Math.max(a, Math.min(b, v)), hp: n => n.toString(16).toUpperCase().padStart(2, '0'), keyHint: s => s, onPset: () => false, mstView: () => false, $: id => els[id],
    selRect: () => ({ x0: Math.min(sel.x, cursor[1]), x1: Math.max(sel.x, cursor[1]), y0: Math.min(sel.y, cursor[0]), y1: Math.max(sel.y, cursor[0]) }),
    songCells: () => { const r = c.selRect(), o = []; for (let y = r.y0; y <= r.y1; y++) for (let t = r.x0; t <= r.x1; t++) o.push([y, t]); return o; },
    toast: t => log.toast.push(t), draw: () => { log.draws++; },
    // one undo step = one snapshot of the phrases per saveS
    saveS: () => { log.saves++; log.undo.push(JSON.stringify(c.S.phrases)); },
    // a stand-in for apRun: with Arrange holding the keys the handler runs as on the Song screen, the selection being Arrange's
    apAsSong: fn => { const t0 = c.tv, s0 = c.selA; c.tv = 'song'; c.selA = c.apSel; try { return fn(); } finally { c.tv = t0; c.selA = s0; } },
    apRun: (fn, a) => (c.apF && c.tv !== 'song') ? c.apAsSong(() => fn(...a)) : fn(...a) };
  if (sel) { const s = Object.assign({ v: 'song' }, sel); if (side) c.apSel = s; else c.selA = s; }
  vm.createContext(c); vm.runInContext(m[0] + '\nthis.transposeSel = transposeSel; this.xposeTitles = xposeTitles; this.xposeBlocks = xposeBlocks;', c);
  return { c, log, els, notes: p => c.S.phrases[p].map(r => r.slice(0, 4)), click: (d) => c.apRun(c.transposeSel, [d, true]) };
}

for (const side of [false, true]) {
  const where = side ? 'Arrange beside Phrase' : 'the Song screen';
  test(`${where}: several selected blocks move every distinct phrase once, even when blocks share one`, () => {
    const r = rig({ side, cells: [[0, 0, 1], [1, 0, 2], [0, 1, 3]], chains: { 1: chain(10, 11), 2: chain(10), 3: chain(12) }, phrases: { 10: P([60], [64]), 11: P([48]), 12: P([50]) }, sel: { x: 0, y: 0 }, cursor: [1, 1] });
    r.click(1);
    assert.deepEqual(r.notes(10).map(x => x[0]), [61, 65], 'phrase 10 is in blocks 1 and 2: +1 once, not +2');
    assert.deepEqual([r.notes(11)[0][0], r.notes(12)[0][0]], [49, 51]);
    assert.equal(r.log.saves, 1, 'one undo step'); assert.equal(r.log.toast.length, 1, 'one notice'); assert.equal(r.log.toast[0], 'Transposed 3 patterns +1');
  });
  test(`${where}: a phrase that a block outside the selection plays too changes there as well (existing convention), and the one notice says so`, () => {
    const r = rig({ side, cells: [[0, 0, 1], [9, 0, 1], [1, 0, 2], [9, 1, 3]], chains: { 1: chain(10), 2: chain(11), 3: chain(12) }, phrases: { 10: P([60]), 11: P([50]), 12: P([70]) }, sel: { x: 0, y: 0 }, cursor: [1, 0] });
    r.click(-12);
    assert.deepEqual([r.notes(10)[0][0], r.notes(11)[0][0], r.notes(12)[0][0]], [48, 38, 70], 'the unselected block on 12 is not in the selection and keeps its own phrase');
    assert.equal(r.log.toast.length, 1); assert.equal(r.log.toast[0], 'Transposed 2 patterns −12 · 1 is also used elsewhere');
  });
  test(`${where}: only notes move; OFF and empty cells stay; clamps at 0 and 119; mono tracks use column 0 only`, () => {
    const r = rig({ side, cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: [[119, 30, -1, -1, -1, -1], [OFF, 30, -1, -1, -1, -1], [0, -1, -1, -1, -1, -1], [-1, -1, -1, -1, -1, -1]] }, sel: { x: 0, y: 0 }, cursor: [0, 0] });
    r.click(1); assert.deepEqual(r.notes(10), [[119, 30, -1, -1], [OFF, 30, -1, -1], [1, -1, -1, -1], [-1, -1, -1, -1]]);
    const d = rig({ side, cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: [[0, -1, -1, -1, -1, -1], [5, -1, -1, -1, -1, -1]] }, sel: { x: 0, y: 0 }, cursor: [0, 0] });
    d.click(-12); assert.deepEqual(d.notes(10).map(x => x[0]), [0, 0]);
  });
  test(`${where}: the whole operation is one undo step (one snapshot taken after, the one before is the starting state)`, () => {
    const r = rig({ side, cells: [[0, 0, 1], [1, 0, 2], [2, 0, 3]], chains: { 1: chain(10), 2: chain(11), 3: chain(12) }, phrases: { 10: P([60]), 11: P([61]), 12: P([62]) }, sel: { x: 0, y: 0 }, cursor: [2, 0] });
    r.click(2); assert.equal(r.log.saves, 1); assert.deepEqual([10, 11, 12].map(p => r.notes(p)[0][0]), [62, 63, 64]);
  });
}

for (const side of [false, true]) {
  const where = side ? 'Arrange beside Phrase' : 'the Song screen';
  test(`${where}: picked blocks (sxPicks) alone, and mixed with a selection; a pick whose phrase is also selected moves once`, () => {
    const base = { side, cells: [[0, 0, 1], [5, 3, 2], [8, 4, 3]], chains: { 1: chain(10), 2: chain(11), 3: chain(10) }, phrases: { 10: P([60]), 11: P([60]) } };
    const a = rig({ ...base, picks: [{ y: 5, t: 3 }], cursor: [0, 0] }); a.click(1);
    assert.deepEqual([10, 11].map(p => a.notes(p)[0][0]), [60, 61], 'picks alone: only the picked block, not the cursor block'); assert.equal(a.log.saves, 1);
    const b = rig({ ...base, picks: [{ y: 5, t: 3 }], sel: { x: 0, y: 0 }, cursor: [0, 0] }); b.click(2);
    assert.deepEqual([10, 11].map(p => b.notes(p)[0][0]), [62, 62], 'selection and pick together'); assert.equal(b.log.saves, 1);
    const c = rig({ ...base, picks: [{ y: 8, t: 4 }], sel: { x: 0, y: 0 }, cursor: [0, 0] }); c.click(1);
    assert.equal(c.notes(10)[0][0], 61, 'phrase 10 is in the selected block and the picked block: +1 once, not +2'); assert.equal(c.notes(11)[0][0], 60);
  });
}

test('Arrange beside Phrase with no selection: the buttons still ask for one and change nothing; another screen: unchanged too', () => {
  const r = rig({ side: true, cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: P([60]) }, cursor: [0, 0] }); r.click(1);
  assert.deepEqual(r.log.toast, ['Select rows first (or alt+, / alt+. for the whole block).']); assert.equal(r.notes(10)[0][0], 60); assert.equal(r.log.saves, 0);
  const o = rig({ tvv: 'mixer', cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: P([60]) }, cursor: [0, 0] }); o.click(1);
  assert.match(o.log.toast[0], /^Select rows first/); assert.equal(o.notes(10)[0][0], 60);
});

test('the buttons’ title says what they will do: the number of selected blocks, in the Arrange context only', () => {
  const r = rig({ side: true, cells: [[0, 0, 1], [1, 0, 2], [2, 1, 3]], chains: { 1: chain(10), 2: chain(11), 3: chain(12) }, phrases: { 10: P([60]), 11: P([60]), 12: P([60]) }, sel: { x: 0, y: 0 }, cursor: [2, 1] });
  r.c.xposeTitles(); assert.match(r.els.tXposeUp.title, /^Transposes the 3 selected blocks’ phrases up a semitone/); assert.match(r.els.tXposeDn.title, /down a semitone/);
  const one = rig({ cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: P([60]) }, sel: { x: 0, y: 0 }, cursor: [0, 0] }); one.c.xposeTitles();
  assert.match(one.els.tXposeUp.title, /^Transposes the 1 selected block’s phrases up/);
  const none = rig({ side: true, cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: P([60]) }, cursor: [0, 0] }); none.c.xposeTitles();
  assert.equal(none.els.tXposeUp.title, 'Transpose the selection up a semitone (Alt+.)');
  const mix = rig({ tvv: 'mixer', cells: [[0, 0, 1]], chains: { 1: chain(10) }, phrases: { 10: P([60]) }, sel: { x: 0, y: 0 }, cursor: [0, 0] }); mix.c.xposeTitles();
  assert.equal(mix.els.tXposeDn.title, 'Transpose the selection down a semitone (Alt+,)');
});

test('wiring: the TRANSP. buttons go through apRun, and draw refreshes their titles', () => {
  assert.match(src, /\$\('tXposeDn'\)\.addEventListener\('click', \(\) => apRun\(transposeSel, \[-1, true\]\)\)/);
  assert.match(src, /\$\('tXposeUp'\)\.addEventListener\('click', \(\) => apRun\(transposeSel, \[1, true\]\)\)/);
  assert.match(src, /const onSong = tv === 'song';\n  xposeTitles\(\);/);
});
