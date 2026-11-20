// IN USE OUTLINE (build 654): which pads / instruments a note plays, on the song or only off it. padUseMap and friends are cut out of
// index.html and run in a vm on small songs; the class / marker wiring is checked on the source. Notes: docs/INUSE-OUTLINE-NOTES.md.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const src = require('./helpers').read('index.html');
const m = src.match(/let padUseC = null;[\s\S]*?\n(?=function usedUI)/); assert.ok(m, 'padUseMap block');
const rowE = () => [-1, -1, -1, -1, -1, -1, -1, 0, -1, 0, -1, 0];
const note = (inst, ins) => { const r = rowE(); r[0] = 48; if (inst != null) r[5] = inst; if (ins != null) { r[6] = 7; r[7] = ins; } return r; };
const ph = (...rows) => Array.from({ length: 4 }, (_, y) => rows[y] || rowE());
function world(o = {}) {
  const S = { insts: Array.from({ length: 8 }, (_, i) => ({ pad: i })), phrases: [], parts: [] };
  Object.assign(S, o.S || {});
  const onSong = o.onSong || new Map();   // phrase id -> Set of tracks (what phraseTracks answers)
  const ctx = vm.createContext({ S, songVer: 1, vtrk: !!o.vtrk, NCOL: 4, IC: 5, FX0: 6, RW: 12, NI: 8, FXC: { 7: 'INS' }, Map, Set, Array, Math,
    isNoteV: v => v >= 0 && v < 120, plen: () => 4, phraseTracks: () => onSong, voiceForTrack: t => 8 + t, hx: n => n.toString(16).toUpperCase().padStart(2, '0') });
  vm.runInContext(m[0], ctx);
  return { S, ctx, st: i => vm.runInContext(`padUseState(${i})`, ctx), ist: i => vm.runInContext(`instUseState(${i})`, ctx), txt: i => vm.runInContext(`padUseText(${i})`, ctx), short: i => vm.runInContext(`padUseShort(${i})`, ctx) };
}
test('a pad played by a note in a phrase on the song is 1; only off the song 2; instrument that points at a pad but is never played is free', () => {
  const w = world({ onSong: new Map([[0, new Set([0])]]) });
  w.S.phrases[0] = ph(note(1)); w.S.phrases[1] = ph(note(2)); w.S.insts[3].pad = 5;   // inst 3 -> pad 5, never in a note
  assert.equal(w.st(1), 1); assert.equal(w.st(2), 2, 'phrase 1 is on no song cell'); assert.equal(w.st(5), 0, 'pointed at, never played'); assert.equal(w.st(4), 0);
  assert.equal(w.ist(1), 1); assert.equal(w.ist(2), 2); assert.equal(w.ist(3), 0);
});
test('a note-less row (instrument column only) does not use an instrument; INS on a note row does; the phrase carries its last instrument', () => {
  const w = world({ onSong: new Map([[0, new Set([0])]]) });
  const bare = rowE(); bare[5] = 4;   // I column, no note
  const carried = rowE(); carried[0] = 50;   // note without an instrument: the one before it
  w.S.phrases[0] = ph(bare, note(null, 6), carried);
  assert.equal(w.st(4), 0, 'bare I column'); assert.equal(w.st(6), 1, 'INS 6 on a note row, and the next note carries it'); assert.match(w.txt(6), /instrument 06 · 2 notes on the song/);
  assert.equal(w.st(0), 0);
});
test('a chord counts every note; a note-off is not a note', () => {
  const w = world({ onSong: new Map([[0, new Set([0])]]) }); const r = note(2); r[1] = 52; r[2] = 200;
  w.S.phrases[0] = ph(r); assert.match(w.txt(2), /2 notes on the song/);
});
test('re-pointing an instrument frees the old pad at once (the key includes the pads); an edit needs a new songVer', () => {
  const w = world({ onSong: new Map([[0, new Set([0])]]) }); w.S.phrases[0] = ph(note(1));
  assert.equal(w.st(1), 1); w.S.insts[1].pad = 6; assert.equal(w.st(1), 0, 'old pad free'); assert.equal(w.st(6), 1);
  w.S.phrases[0][0][5] = 2; assert.equal(w.st(2), 0, 'same songVer: the cached answer'); w.ctx.songVer = 2; assert.equal(w.st(2), 1); assert.equal(w.st(6), 0, 'the note was moved to inst 2');
});
test('invalidation: a deleted phrase, a phrase taken off the song, a new S, vtrk, a part re-pointed', () => {
  const on = new Map([[0, new Set([0])]]), w = world({ onSong: on }); w.S.phrases[0] = ph(note(1)); assert.equal(w.st(1), 1);
  on.delete(0); w.ctx.songVer = 2; assert.equal(w.st(1), 2, 'taken off the song: dotted'); w.S.phrases[0] = null; w.ctx.songVer = 3; assert.equal(w.st(1), 0, 'deleted');
  const w2 = world({ onSong: new Map([[0, new Set([0])]]) }); w2.S.phrases[0] = ph(note(1)); assert.equal(w2.st(1), 1);
  w2.ctx.S = { insts: w2.S.insts, phrases: [], parts: [] }; assert.equal(w2.st(1), 0, 'a loaded song is a new S');
  w2.ctx.S.parts[0] = { insts: Array.from({ length: 8 }, (_, i) => ({ pad: i })) }; w2.ctx.S.phrases[0] = ph(note(1)); w2.ctx.songVer = 4; assert.equal(w2.st(1), 1);
  w2.ctx.S.parts[0].insts[1].pad = 7; assert.equal(w2.st(7), 1, 'a part re-pointed (no songVer change)');
  w2.ctx.vtrk = true; assert.equal(w2.st(7), 0, 'vtrk is in the key');
});
test('PERFORM part: the pad a part sends the song\'s notes to is in use too; a part used only off the song is not', () => {
  const parts = [{ insts: Array.from({ length: 8 }, (_, i) => ({ pad: i === 1 ? 6 : i })) }];
  const w = world({ onSong: new Map([[0, new Set([0])]]), S: { parts } }); w.S.phrases[0] = ph(note(1));
  assert.equal(w.st(1), 1); assert.equal(w.st(6), 1, 'via part 1'); assert.match(w.txt(6), /through a PERFORM part by instrument 01/); assert.match(w.short(6), /PERFORM part/);
  const w2 = world({ S: { parts } }); w2.S.phrases[0] = ph(note(1)); assert.equal(w2.st(6), 0); assert.equal(w2.st(1), 2);
});
test('a kit slot per track: the track picks the pad; an off-song phrase has no track', () => {
  const w = world({ vtrk: true, onSong: new Map([[0, new Set([0, 2])]]) }); w.S.phrases[0] = ph(note(1)); w.S.phrases[1] = ph(note(3));
  assert.equal(w.st(8), 1); assert.equal(w.st(10), 1); assert.equal(w.st(9), 0); assert.equal(w.st(1), 0); assert.equal(w.ist(3), 2);
});
test('info text: on-song notes, and what only unplaced phrases do', () => {
  const w = world({ onSong: new Map([[0, new Set([0])]]) }); w.S.insts[2].pad = 1;
  w.S.phrases[0] = ph(note(1), note(1), note(2)); w.S.phrases[1] = ph(note(1));
  assert.equal(w.txt(1), 'used by instrument 01, 02 · 3 notes on the song · also in phrases not on the song: instrument 01 · 1 note');
  assert.equal(w.short(1), 'in use: inst 01, 02 · 3 notes');
  const w2 = world(); w2.S.phrases[0] = ph(note(1)); assert.equal(w2.txt(1), 'only in phrases not on the song: instrument 01 · 1 note'); assert.equal(w2.short(1), 'only in phrases not on the song'); assert.equal(w2.txt(5), ''); assert.equal(w2.short(5), 'not in the song');
});
test('source: classes on the right tiles, the legend, the instrument list, a thin outline that moves nothing, and the cursor and drop styles still win', () => {
  assert.match(src, /p\.classList\.toggle\('uOn', us === 1\); p\.classList\.toggle\('uOff', us === 2\)/);
  assert.match(src, /id="padKey" hidden/); assert.match(src, /k\.hidden = !anyUse/);
  assert.match(src, /\.pad\.uOn, \.pad\.uOff\{outline:1\.5px solid color-mix\(in srgb, currentColor 72%, transparent\); outline-offset:-3px\}/);
  assert.match(src, /\.pad\.uOff\{outline-style:dotted\}/); assert.match(src, /\.pad\.sel\.uOn, \.pad\.sel\.uOff\{outline-offset:-5px\}/);
  const css = src.match(/\.pad\.uOn, \.pad\.uOff\{[^\n]*/)[0]; assert.ok(!/border|margin|padding|box-shadow/.test(css), 'no box-model change');
  assert.ok(src.indexOf('.pad.uOn, .pad.uOff{') < src.indexOf('.pad.droptgt{outline:3px dashed'), 'the file-drop outline comes later and wins');
  assert.match(src, /sig \+= nm \+ '\/' \+ I\.pad \+ '\/' \+ us/); assert.match(src, /\.tinl button\.uOn, \.tinl button\.uOff\{/);
  assert.match(src, /usedUI\(\);\n  bassUI\(\);/);
});
