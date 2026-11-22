// EVERY PITCHED GENERATOR FOLLOWS THE SONG'S KEY (21 Nov, owner: "every generator follows the song's key by default"; with SCALE off, the key found from the song's notes).
// The real arpGen / chordGen / polyGen / markovGen / mutatePhrase, genScope, keySnap and songKey are cut out of index.html and run in a vm over a small song with genPanel
// stubbed to hand back its buttons: the "song key" option is on at first, and with it on every note a generator writes is in the key; off, the generator writes as before.
// Not pitched (no option, checked on the source): Ratchet, Probability fill, Euclidean fill, Drum pattern; Acid line has its own KEY select.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const code = [
  cut(/const SCALES = \[[\s\S]*?\['DIMINISHED', [^\n]*\]\];/, 'SCALES'), cut(/const ROOTS = [^\n]*\n/, 'ROOTS'), cut(/const ACID_SCALES = [\s\S]*?\['Octaves', \[0\]\]\];/, 'ACID_SCALES'),
  cut(/function euclid\(k, n, r\)\{[\s\S]*?\n\}\n/, 'euclid'), cut(/function songKey\(skipPh\)\{[\s\S]*?\n\}\nfunction acidLine/, 'songKey').replace(/function acidLine$/, ''),
  cut(/function genScope\(what\)\{[\s\S]*?\/\* ===== end MORE GENERATORS ===== \*\//, 'the generators'), cut(/const isNoteV = [\s\S]*?\/\* DRUM PATTERN:/, 'Mutate').replace(/\/\* DRUM PATTERN:$/, ''),
  'this.gens = {arpGen, chordGen, polyGen, markovGen, mutatePhrase, probFill, ratchetGen}; this.sets = {arpSet, chordSet, polySet, markSet, mutSet}; this.keyIv = keyIv; this.keySnap = keySnap; this.songKey = songKey;'].join('\n');
const MIN = [0, 2, 3, 5, 7, 8, 10], MAJ = [0, 2, 4, 5, 7, 9, 11];
const inKey = (n, root, iv) => iv.includes(((n - root) % 12 + 12) % 12);
const blank = (L = 32) => Array.from({ length: L }, () => new Array(12).fill(-1));
// song: a melodic phrase 1 on track 0 (notes of C minor unless `notes` says otherwise), the one open for writing is phrase 2
function rig({ scale = 0, sroot = 0, notes = null, poly = false, cursorNote = null } = {}) {
  const NT = 32, MT = 16, buttons = [], log = { toast: [], saves: 0 };
  const song = Array.from({ length: 256 }, () => new Array(MT + 1).fill(-1)); song[0][0] = 1; song[1][0] = 2;
  const ph1 = blank(); (notes || [60, 63, 67, 70, 72, 67, 63, 60, 62, 65, 67, 68, 70, 67, 65, 63]).forEach((n, y) => { ph1[y][0] = n; ph1[y][5] = 0; });
  const ph2 = blank(); if (cursorNote != null){ ph2[0][0] = cursorNote; ph2[0][5] = 0; }
  const c = { NT, MT, NCOL: 4, IC: 5, VC: 4, ROOT: 48, OFF: 200, FX0: 6, RW: 12, FXC: [], view: 'tracker', tv: 'phrase', cTrack: 0, cPhrase: 2, selA: null, cs: { phrase: { y: 0, x: 0 } }, mstView: () => false, busTrack: () => false, busSay: () => true,
    S: { scale, sroot, song, chains: { 1: [[1, 0]], 2: [[2, 0]] }, phrases: [null, ph1, ph2], insts: [{ kf: true, tsp: 0 }], plpb: {}, lpb: 4 }, plen: () => 16, plpbOf: () => 4, freshPhrase: () => blank(), isEmptyPhrase: P => !P.some(r => r.slice(0, 6).some(v => v >= 0)), defInst: () => 0,
    isPolyT: () => poly, polyFrom: () => 0, d2: n => String(n).padStart(2, '0'), hx: n => n.toString(16), rS: n => String(n), noteS: n => 'n' + n, clamp: (v, a, b) => Math.max(a, Math.min(b, v)),
    toast: t => log.toast.push(t), saveS: () => { log.saves++; }, draw: () => {}, playCtx: () => {}, askOpen: null, preview: () => null, previewOff: () => {}, instLabel: j => 'i' + j, selOf: null, el: () => ({ append() {}, classList: { toggle() {} }, setAttribute() {}, addEventListener() {} }),
    genPanel: (title, msg, build, btns) => { buttons.length = 0; buttons.push(...btns); const fields = []; build({ field: (l, ctl) => fields.push([l, ctl]), slider: () => {} }); c.fields = fields; return () => {}; } };
  // selOf / el need no DOM: genKeyField is given a recording `field`, and selOf (a DOM <select>) is replaced by a recorder that keeps the options and the setter
  c.document = {}; vm.createContext(c);
  vm.runInContext(code.replace(/const selOf = [^\n]*\n/, ''), c);
  vm.runInContext('var selOf = (opts, val, set) => ({opts, val, set});', c);
  const roll = () => { const ok = buttons.find(b => b[2] === 'ok'); ok[1](); };
  const notesOf = () => c.S.phrases[2].map(r => r.slice(0, 4)).flat().filter(n => n >= 0 && n < 120);
  const keyField = () => c.fields.find(f => f[0] === 'Song key')[1];
  return { c, roll, notesOf, keyField, log };
}
const run = (r, g) => { r.c.gens[g](); };

for (const [name, g, ok] of [['Arp', 'arpGen'], ['Chords', 'chordGen'], ['Polymeter', 'polyGen'], ['Markov melody', 'markovGen'], ['Mutate', 'mutatePhrase']]) {
  const pre = (r) => { if (g === 'mutatePhrase') { r.c.S.phrases[2] = r.c.S.phrases[1].map(x => x.slice()); } };
  test(`${name}: the song key option is there and on at first (the song's SCALE: D dorian)`, () => {
    const r = rig({ scale: 5, sroot: 2, poly: true }); pre(r); run(r, g);
    assert.ok(r.keyField(), 'a "Song key" field'); assert.equal(r.keyField().val, 1, 'on by default');
    assert.deepEqual(Array.from(r.keyField().opts.map(o => o[0])), [1, 0]); assert.match(r.keyField().opts[0][1], /D dorian/);
  });
  test(`${name}: with it on, every note written is in the key (SCALE set: D dorian; and SCALE off: the key found from the song, C minor)`, () => {
    for (const [o, root, iv] of [[{ scale: 5, sroot: 2 }, 2, [0, 2, 3, 5, 7, 9, 10]], [{ scale: 0 }, 0, MIN]]) {
      for (let k = 0; k < 6; k++) {
        const r = rig({ ...o, poly: true, cursorNote: g === 'polyGen' || g === 'arpGen' ? 61 : null }); pre(r); run(r, g);
        if (g === 'arpGen') r.c.sets.arpSet.root = 61 + k;   // a root off the key, and the major shape: the third must land in the key too
        if (g === 'chordGen') { r.c.sets.chordSet.root = 1 + k; r.c.sets.chordSet.mood = 0; }
        if (g === 'mutatePhrase') r.c.sets.mutSet.amt = 100;
        const before = g === 'mutatePhrase' ? r.c.S.phrases[2].map(x => x.slice()) : null;
        r.roll(); let n = r.notesOf(); assert.ok(n.length, 'wrote notes');
        if (before) { n = []; r.c.S.phrases[2].forEach((row, y) => [0, 1, 2, 3].forEach(c => { const v = row[c]; if (v >= 0 && v < 120 && v !== before[y][c]) n.push(v); })); assert.ok(n.length, 'Mutate changed some notes'); }   // Mutate leaves the notes it does not touch as they are
        for (const x of n) assert.ok(inKey(x, root, iv), `${name}: note ${x} is outside root ${root} ${iv}`);
      }
    }
  });
}

test('SCALE off: the key is the one songKey() works out from the song’s notes, and a Project scale gives its own exact notes (Lydian is not squeezed to Major)', () => {
  const a = rig({ scale: 0 }); const k = a.c.songKey(2); assert.equal(k.root, 0); assert.equal(k.scale, 0); assert.deepEqual(Array.from(a.c.keyIv(k)), MIN);
  const b = rig({ scale: 7, sroot: 5 }); const kb = b.c.songKey(2); assert.deepEqual(Array.from(b.c.keyIv(kb)), [0, 2, 4, 6, 7, 9, 11], 'LYDIAN as written in Project');
  assert.equal(b.c.songKey(2).root, 5);
});

test('keySnap: the nearest note of the key, ties go down; notes already in the key stay', () => {
  const r = rig(); const G = { kiv: MAJ, root: 0 };
  assert.equal(r.c.keySnap(G, 61), 60, 'C♯ between C and D goes down'); assert.equal(r.c.keySnap(G, 64), 64); assert.equal(r.c.keySnap(G, 70), 69);
  assert.equal(r.c.keySnap({ kiv: [0, 3, 7], root: 2 }, 61), 62, 'D minor triad: C♯ is a semitone from D');
});

test('Arp with the option on: a major shape in a minor key becomes its minor (the third snaps to the key); off: the shape as written', () => {
  const on = rig({ scale: 2, sroot: 0, cursorNote: 60 }); run(on, 'arpGen'); on.c.sets.arpSet.root = 60; on.c.sets.arpSet.shape = 0; on.c.sets.arpSet.oct = 1; on.roll();
  assert.ok(on.notesOf().every(n => [60, 63, 67].includes(n)), 'C minor notes: ' + [...new Set(on.notesOf())]);
  const off = rig({ scale: 2, sroot: 0, cursorNote: 60 }); run(off, 'arpGen'); off.keyField().set(0); off.c.sets.arpSet.root = 60; off.c.sets.arpSet.shape = 0; off.c.sets.arpSet.oct = 1; off.roll();
  assert.ok(off.notesOf().includes(64), 'E natural written as before'); assert.equal(off.c.sets.arpSet.key, false);
});

test('the option is kept like the others (in the generator’s settings object, as its other options are); off, Chords, Polymeter and Arp write what they wrote before (out of the key)', () => {
  const ch = rig({ scale: 2, sroot: 0, poly: true }); run(ch, 'chordGen'); ch.keyField().set(0); assert.equal(ch.c.sets.chordSet.key, false, 'the setting is in chordSet');
  ch.c.sets.chordSet.root = 1; ch.c.sets.chordSet.mood = 0; ch.roll(); assert.ok(ch.notesOf().some(n => !inKey(n, 0, MIN)), 'C♯ major chords are not squeezed into C minor');
  ch.c.gens.chordGen(); assert.equal(ch.keyField().val, 0, 'and a reopened panel still has it off');
  const po = rig({ scale: 2, sroot: 0, cursorNote: 61 }); run(po, 'polyGen'); po.keyField().set(0); po.c.sets.polySet.pitch = 1; po.c.sets.polySet.hits = 5; po.roll();
  assert.ok(po.notesOf().length && po.notesOf().every(n => n === 61), 'the cursor’s C♯ is the one note, as written');
});

test('no key found yet (a song with too few notes, SCALE off): the option says so and the generators keep their notes', () => {
  const r = rig({ scale: 0, notes: [60] }); r.c.S.phrases[1] = blank(); r.c.S.phrases[1][15][0] = 60; r.c.S.phrases[1][15][5] = 0;   // one short note: under 8 weighted notes, no key
  run(r, 'arpGen'); assert.match(r.keyField().opts[0][1], /none found/); r.c.sets.arpSet.root = 61; r.c.sets.arpSet.shape = 0; r.c.sets.arpSet.oct = 1; r.roll();
  assert.ok(r.notesOf().includes(61), 'C♯ stays: there is no key to follow');
});

test('source guards: the four rhythm / copy generators have no option, every pitched one has `key:true`, the panel row is "Song key"', () => {
  for (const s of ['arpSet', 'chordSet', 'polySet', 'markSet', 'mutSet']) assert.match(src, new RegExp('const ' + s + ' = \\{[^\\n]*key:true'), s + ' has key:true');
  for (const s of ['ratSet', 'fillSet', 'drumSet']) assert.doesNotMatch(src, new RegExp('const ' + s + ' = \\{[^\\n]*key:'), s + ' has no key option');
  for (const g of ['arpGen', 'chordGen', 'polyGen', 'markovGen']) assert.match(cut(new RegExp('function ' + g + '\\(\\)\\{[\\s\\S]*?\\n\\}\\n'), g), /genKeyField\(G, \w+, field\)/, g + ' shows it');
  assert.match(src, /genKeyField\(\{key:found\}, mutSet, field\)/);
});
