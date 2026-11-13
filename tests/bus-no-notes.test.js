// NO NOTES INTO A BUS (12 Nov, owner). The guard (busNo / busNoteNo / busRefFlush), setCell's head, paste, pasteMix, pastePicks and mselApply are cut out of index.html and run in a vm
// over a small song: a bus track takes commands only. A pattern with notes is refused on a bus (and said once), a notes-only paste changes nothing, a mixed paste leaves the notes out, and a
// refusal writes nothing (no saveS, so no undo step). The Song's Alt-drag, clones and typing go through the same guard; those hooks are checked on the source.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const guard = cut(/let busRef = null, busRefT = 0, busRefNotes = 0;[\s\S]*?\nconst pCols/, 'the bus guard').replace(/\nconst pCols$/, '');
const setCellHead = cut(/function setCell\(v, x, y, val\)\{\n  if \(v === 'song' && x < NT[^\n]*\n[^\n]*\n/, 'setCell head') + "  if (v === 'song') S.song[y][x] = val; else S.phrases[cPhrase][y][x] = val;\n  saveS();\n}\n";
const paste = cut(/function paste\(\)\{[\s\S]*?\n\}\n/, 'paste'), pasteMix = cut(/function pasteMix\(\)\{[\s\S]*?\n\}\n/, 'pasteMix'), pastePicks = cut(/function pastePicks\(\)\{[\s\S]*?\n\}\n/, 'pastePicks');
const mselApply = cut(/function mselApply\(m, copy\)\{[\s\S]*?\n\}\n/, 'mselApply');
const NT = 16, MT = 16, FX0 = 6, IC = 5, BUS = 3;
function rig({ tv = 'phrase', clip = null, cursor = { x: 0, y: 0 } } = {}) {
  const log = { toast: [], saves: 0 };
  const song = Array.from({ length: 256 }, () => new Array(MT + 1).fill(-1));
  const blank = () => Array.from({ length: 32 }, () => new Array(12).fill(-1));
  const c = { setTimeout, clearTimeout, NT, MT, FX0, IC, S: { song, chains: {}, phrases: {} }, tv, clip, cTrack: BUS, cPhrase: 0, selA: null, uniqVac: [], cs: { song: cursor, phrase: cursor }, sxPicks: [], NCOL: 4,
    busPlan: () => ({ feeds: new Set([BUS]) }), isBusT: t => t === BUS, busTrack: t => t >= 0 && t < NT && t === BUS, sectsOn: false, firstPhrase: id => (c.S.chains[id] || [[-1]])[0][0], hp: n => n.toString(16).toUpperCase().padStart(2, '0'),
    songShown: v => c.firstPhrase(v), toast: t => log.toast.push(t), saveS: () => { log.saves++; }, draw: () => {}, edBlock: () => false, inMode: 'keys', d2: n => String(n).padStart(2, '0'),
    dims: v => v === 'song' ? { w: 17, h: 256 } : { w: 12, h: 32 }, kindAt: () => 'cell', getCell: (v, x, y) => v === 'song' ? c.S.song[y][x] : c.S.phrases[c.cPhrase][y][x], sxU: x => x, sxT: u => u, ntr: () => NT,
    setBlockMute: () => {}, blockMuted: () => false, selRect: () => ({ y0: 0 }), clamp: (v, a, b) => Math.max(a, Math.min(b, v)), VNAME: { phrase: 'Phrase', song: 'Song' }, MODK: '', mstUniqIn: false, mstShareWarn: () => {}, rS: n => n, mselN: () => 1, keyHint: s => s, uniqVac: [] };
  c.S.phrases[0] = blank();
  vm.createContext(c);
  vm.runInContext('var hasNotes = p => !!p && p.some(r => { for (let x = 0; x <= IC; x++) if (r[x] >= 0) return true; return false; });\n' + guard + '\n' + setCellHead + '\n' + paste + '\n' + pasteMix + '\n' + pastePicks + '\n' + mselApply +
    '\nthis.paste = paste; this.pasteMix = pasteMix; this.pastePicks = pastePicks; this.mselApply = mselApply; this.setCell = setCell; this.busNo = busNo; this.busRefFlush = busRefFlush; this.dupe = 0;', c);
  const chain = (id, ph, rows) => { c.S.chains[id] = [[ph, 0]]; c.S.phrases[ph] = Array.from({ length: 32 }, () => new Array(12).fill(-1)); (rows || []).forEach(([y, x, v]) => { c.S.phrases[ph][y][x] = v; }); };
  return { c, log, chain, flush: () => c.busRefFlush(), cell: (y, x) => c.S.phrases[0][y][x] };
}
test('a notes-only paste into a bus phrase changes nothing and says so', () => {
  const r = rig({ clip: { v: 'phrase', x0: 0, d: [[48, -1, -1, -1, 5, 0]] } }); r.c.paste();
  assert.equal(r.cell(0, 0), -1); assert.equal(r.cell(0, 4), -1); assert.equal(r.log.saves, 0);
  assert.equal(r.log.toast.at(-1), 'A bus track takes commands, not notes · nothing pasted');
});
test('a mixed paste keeps the command columns, leaves the notes out, says so', () => {
  const r = rig({ clip: { v: 'phrase', x0: 0, d: [[48, -1, -1, -1, 5, 0, 7, 3, -1]] } }); r.c.paste();
  assert.equal(r.cell(0, 0), -1); assert.equal(r.cell(0, 6), 7); assert.equal(r.cell(0, 7), 3); assert.equal(r.log.toast.at(-1), 'Notes left out · this is a bus track');
});
test('a commands-only paste is as it was', () => {
  const r = rig({ clip: { v: 'phrase', x0: 6, d: [[7, 3]] } }); r.c.paste(); assert.equal(r.cell(0, 6), 7); assert.match(r.log.toast.at(-1), /^Pasted/);
});
test('mix-paste: notes only changes nothing, mixed leaves the notes out', () => {
  let r = rig({ clip: { v: 'phrase', x0: 0, d: [[48, -1, -1, -1, 5]] } }); r.c.pasteMix(); assert.equal(r.cell(0, 0), -1); assert.equal(r.log.saves, 0); assert.equal(r.log.toast.at(-1), 'A bus track takes commands, not notes · nothing pasted');
  r = rig({ clip: { v: 'phrase', x0: 0, d: [[48, -1, -1, -1, -1, -1, 9]] } }); r.c.pasteMix(); assert.equal(r.cell(0, 0), -1); assert.equal(r.cell(0, 6), 9); assert.equal(r.log.toast.at(-1), 'Notes left out · this is a bus track');
});
test('setCell: a note, volume or instrument into a bus phrase is refused with no save; a clear is allowed', () => {
  const r = rig(); for (const x of [0, 4, 5]) assert.equal(r.c.setCell('phrase', x, 1, 10), false); assert.equal(r.log.saves, 0);
  r.c.S.phrases[0][2][0] = 40; r.c.setCell('phrase', 0, 2, -1); assert.equal(r.cell(2, 0), -1); assert.equal(r.log.saves, 1);
});
test('Song: a pattern with notes is refused on a bus (named once), a commands-only or empty one goes in; no undo step for the refusal', () => {
  const r = rig({ tv: 'song' }); r.chain(1, 1, [[0, 0, 48]]); r.chain(2, 2, [[0, 6, 7]]); r.chain(3, 3, []);
  assert.equal(r.c.setCell('song', BUS, 0, 1), false); r.flush(); assert.equal(r.log.saves, 0); assert.equal(r.c.S.song[0][BUS], -1); assert.equal(r.log.toast.at(-1), 'Pattern 01 has notes · a bus track takes commands only');
  r.c.setCell('song', BUS, 1, 2); r.c.setCell('song', BUS, 2, 3); assert.equal(r.c.S.song[1][BUS], 2); assert.equal(r.c.S.song[2][BUS], 3); assert.equal(r.log.saves, 2);
  r.c.setCell('song', 0, 0, 1); assert.equal(r.c.S.song[0][0], 1);   // a normal track still takes it
});
test('Song paste across columns: the normal tracks still paste, the bus column leaves the notes out, and the toast names the pattern', () => {
  const r = rig({ tv: 'song', clip: { v: 'song', x0: 2, d: [[1, 1]], mu: [[0, 0]] }, cursor: { x: 2, y: 0 } }); r.chain(1, 1, [[0, 0, 48]]); r.c.paste(); r.flush();
  assert.equal(r.c.S.song[0][2], 1); assert.equal(r.c.S.song[0][3], -1); assert.match(r.log.toast.at(-1), /^Pattern 01 has notes · a bus track takes commands only/);
});
test('picked cells: only the refused one is left out and not counted', () => {
  const r = rig({ tv: 'song', clip: { picks: [{ id: 1, dy: 0, du: 0, mu: 0 }, { id: 1, dy: 1, du: 1, mu: 0 }] }, cursor: { x: 2, y: 0 } }); r.chain(1, 1, [[0, 0, 48]]); r.c.pastePicks(); r.flush();
  assert.equal(r.c.S.song[0][2], 1); assert.equal(r.c.S.song[1][3], -1); assert.match(r.log.toast.at(-1), /^Pattern 01 has notes/);
});
test('drag a selection (move): a block refused by the bus stays where it was', () => {
  const r = rig({ tv: 'song' }); r.chain(1, 1, [[0, 0, 48]]); r.c.S.song[0][2] = 1; r.c.S.song[0][0] = 1;
  r.c.mselApply({ r: { x0: 2, x1: 2, y0: 0, y1: 0 }, dx: 1, dy: 0 }, false); r.flush();
  assert.equal(r.c.S.song[0][2], 1); assert.equal(r.c.S.song[0][3], -1);
});
test('hooks: Arrange drag/Alt-drag, clones (CLN, duplicate, paste clone) and Duplicate Track go through the guard; generators refuse a bus', () => {
  assert.match(src, /busNo\(sxT\(sxU\(t\) \+ d\.du\), v\)/);
  assert.match(src, /function newBlock\(c, src\)\{\n  if \(src >= 0 && c && c\.x < NT && busNo\(c\.x, src\)\) return;/);
  assert.match(src, /!\(notes && busTrack\(u\)\)/);
  assert.match(src, /function genRefuse\(t\)\{ return t >= 0 && t < NT && busTrack\(t\)/);
});
