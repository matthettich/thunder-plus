// CHORD PICK-UP (14 Nov, owner: "On a poly track, if Enter is pressed while the cursor is over a line, store all of the notes on that line"). The functions are cut out of
// index.html and run in a vm over a small phrase: pickUp / pickUpChord / chordDrop / insertAt. On a poly track Enter / EDIT on a filled note picks up every note of the row (a note or OFF
// in each of the four note columns) and the next Enter / EDIT on an empty note drops them in the same columns, as one saveS (one undo step). One note on the row, mono tracks,
// effect columns and OFF under the cursor are as before; a chord dropped on a mono track writes only the note that was under the cursor and says so. Opt-in (TP_BROWSER_TESTS=1): the same in a real page.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const chordCode = cut(/let lastChord = null;[\s\S]*?\nfunction pickUp\(x, y, say\)\{[\s\S]*?\n\}\n/, 'the chord pick-up and pickUp');
const insertAtSrc = cut(/function insertAt\(x, y, k, chordOk\)\{[\s\S]*?\n\}\n/, 'insertAt');
const clipQuiet = cut(/function clipCellQuiet\(x, y\)\{[\s\S]*?\n\}\n/, 'clipCellQuiet');
const J = v => JSON.parse(JSON.stringify(v));   // out of the vm: its arrays are not this realm's
const OFF = 200, NCOL = 4, VC = 4, IC = 5, FX0 = 6;
function rig({ poly = true, bus = false } = {}) {
  const log = { toast: [], saves: 0, draws: 0 };
  const blank = () => Array.from({ length: 32 }, () => new Array(12).fill(-1));
  const c = { OFF, NCOL, VC, IC, FX0, ROOT: 48, tv: 'phrase', cPhrase: 0, cTrack: poly ? 9 : 1, MT: 32, cInst: 0, instPicked: false, last: { note: 48, vol: 255, inst: 0, fxc: 0, fxv: 0 }, clip: null,
    S: { phrases: [blank()], insts: Array.from({ length: 4 }, (_, i) => ({ pad: i })) }, kit: { voices: [] }, PK: ['note', 'note', 'note', 'note', 'vol', 'inst', 'fxc', 'fxv', 'fxc', 'fxv', 'fxc', 'fxv'],
    FXC: ['---', 'VOL', 'PAN'], polyView: () => poly, busTrack: () => bus, noteS: n => n === OFF ? 'OFF' : ['C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-'][n % 12] + Math.floor(n / 12),
    hx: n => n.toString(16).toUpperCase().padStart(2, '0'), pasteKey: () => 'Ctrl+V', defInst: () => 2, pnoeOn: false, aud: () => { log.aud = (log.aud || 0) + 1; }, mstShareWarn: () => {},
    toast: t => log.toast.push(t), saveS: () => { log.saves++; }, draw: () => { log.draws++; },
    setCell: (v, x, y, val) => { c.S.phrases[c.cPhrase][y][x] = val; c.saveS(); }, after: (k, v, x, y, val) => { c.setChord(null); if (val >= 0 && val !== OFF && k in c.last) c.last[k] = val; } };
  vm.createContext(c);
  vm.runInContext(clipQuiet + '\n' + chordCode + '\n' + insertAtSrc + '\nthis.pickUp = pickUp; this.insertAt = insertAt; this.getChord = () => lastChord; this.setChord = v => { lastChord = v; }; this.rowChord = rowChord;', c);
  c.last.note = 48;
  const row = y => c.S.phrases[0][y];
  return { c, log, row, pick: (x, y) => c.pickUp(x, y, true), drop: (x, y) => c.insertAt(x, y, 'note', true) };
}
const chordRow = r => { r[0] = 48; r[1] = 52; r[2] = 55; r[IC] = 1; r[VC] = 100; };

test('rowChord: every note column with a note or an OFF, in its own column', () => {
  const R = rig(); const r = new Array(12).fill(-1); r[0] = 60; r[2] = OFF; r[3] = 64; r[VC] = 9;
  assert.deepEqual(J(R.c.rowChord(r)), [[0, 60], [2, OFF], [3, 64]]);
});
test('poly: Enter on any filled note of a 3-note row picks up all three, says how many, and copies them as a clip', () => {
  const R = rig(); chordRow(R.row(2));
  R.pick(1, 2);
  assert.deepEqual(J(R.c.getChord().cols), [0, 1, 2]); assert.deepEqual(J(R.c.getChord().notes), [48, 52, 55]);
  assert.match(R.log.toast.at(-1), /^3 notes picked up: C-4 E-4 G-4/); assert.match(R.log.toast.at(-1), /instrument 01/);
  assert.equal(R.c.last.note, 52, 'the note under the cursor is the current note'); assert.equal(R.c.last.inst, 1);
  assert.deepEqual(J(R.c.clip.d[0].slice(0, 4)), [48, 52, 55, null]); assert.equal(R.c.clip.d[0][IC], 1); assert.equal(R.c.clip.x0, 0);
  assert.deepEqual([...R.row(2)], [48, 52, 55, -1, -1, 1, -1, -1, -1, -1, -1, -1].map((v, i) => i === VC ? 100 : v), 'nothing is removed from the source');
});
test('poly: an OFF in the row is carried with the notes', () => {
  const R = rig(); const r = R.row(4); r[0] = 50; r[1] = OFF; r[IC] = 0; R.pick(0, 4);
  assert.deepEqual(J(R.c.getChord().notes), [50, OFF]); assert.match(R.log.toast.at(-1), /^2 notes picked up: D-4 OFF/);
});
test('drop: Enter on an empty note cell of a poly row puts every note in the same columns, one undo step', () => {
  const R = rig(); chordRow(R.row(2)); R.pick(0, 2); const s0 = R.log.saves;
  R.drop(3, 10);   // the cursor is on an empty N4: the chord still goes in its own columns
  assert.deepEqual([...R.row(10).slice(0, 4)], [48, 52, 55, -1]); assert.equal(R.row(10)[IC], 2, 'a row with no instrument gets the default, as a single note does');
  assert.equal(R.log.saves - s0, 1, 'one saveS = one undo step'); assert.match(R.log.toast.at(-1), /^3 notes dropped: C-4 E-4 G-4/);
  R.drop(1, 14); assert.deepEqual([...R.row(14).slice(0, 3)], [48, 52, 55], 'the chord stays current for the next drop');
});
test('drop: a filled column of the target is replaced, the others are left', () => {
  const R = rig(); chordRow(R.row(2)); R.pick(0, 2); R.row(6)[1] = 70; R.row(6)[3] = 71; R.drop(0, 6);
  assert.deepEqual([...R.row(6).slice(0, 4)], [48, 52, 55, 71]);
});
test('poly: a row with ONE note is the single-note pick-up, as before', () => {
  const R = rig(); R.row(3)[2] = 60; R.row(3)[IC] = 1; R.pick(2, 3);
  assert.equal(R.c.getChord(), null); assert.equal(R.c.last.note, 60); assert.match(R.log.toast.at(-1), /^Picked up: instrument 01.* · note C-5/);
  assert.equal(R.c.clip.d[0][0], 60); assert.equal(R.c.clip.x0, 2);
  R.drop(0, 8); assert.equal(R.row(8)[0], 60, 'a single note drops as one note'); assert.equal(R.row(8)[1], -1);
});
test('a single note picked up after a chord forgets the chord', () => {
  const R = rig(); chordRow(R.row(2)); R.pick(0, 2); R.row(5)[0] = 70; R.pick(0, 5); assert.equal(R.c.getChord(), null);
  R.drop(0, 9); assert.deepEqual([...R.row(9).slice(0, 3)], [70, -1, -1]);
});
test('a note typed after the pick-up (after()) or one that changed last.note is not the chord any more', () => {
  const R = rig(); chordRow(R.row(2)); R.pick(0, 2); R.c.last.note = 70;   // e.g. recorded: last.note changed without after()
  R.drop(0, 9); assert.deepEqual([...R.row(9).slice(0, 3)], [70, -1, -1]);
  R.pick(0, 2); R.c.after('note', 'phrase', 0, 9, 65); assert.equal(R.c.getChord(), null);
});
test('mono track: the pick-up is the single note, exactly as before', () => {
  const R = rig({ poly: false }); R.row(2)[0] = 50; R.row(2)[1] = 52; R.row(2)[IC] = 1; R.pick(0, 2);
  assert.equal(R.c.getChord(), null); assert.match(R.log.toast.at(-1), /^Picked up: instrument 01.* · note D-4/);
});
test('a chord dropped on a mono track writes only the note that was under the cursor, says so, and stays current for a poly track', () => {
  const P = rig(); chordRow(P.row(2)); P.pick(1, 2); const chord = P.c.getChord();
  const M = rig({ poly: false }); M.c.setChord(chord); M.c.last.note = 52; M.drop(0, 7);
  assert.equal(M.row(7)[0], 52); assert.equal(M.row(7)[1], -1); assert.match(M.log.toast.at(-1), /^Dropped a note \(E-4\): 3-note chords need a poly track/);
  assert.equal(M.c.getChord(), chord, 'still current');
});
test('a bus or the master takes no chord (a bus refuses notes before this is reached; the drop guards it too)', () => {
  const P = rig(); chordRow(P.row(2)); P.pick(0, 2);
  const B = rig({ bus: true }); B.c.setChord(P.c.getChord()); B.c.last.note = 48; B.drop(0, 7); assert.equal(B.row(7)[1], -1); assert.match(B.log.toast.at(-1), /^Dropped a note/);
});
test('effect and volume columns pick up as before, and leave a chord current', () => {
  const R = rig(); chordRow(R.row(2)); R.row(2)[FX0] = 1; R.row(2)[FX0 + 1] = 0x20; R.pick(0, 2); const ch = R.c.getChord(), n = R.log.toast.length; R.pick(FX0, 2);
  assert.equal(R.c.last.fxc, 1); assert.equal(R.c.last.fxv, 0x20); assert.equal(R.log.toast.length, n, 'build 686: an effect pick-up is silent: ' + R.log.toast.at(-1)); assert.equal(R.c.getChord(), ch);
});
test('EDIT+arrows on an empty note cell (bump) does not drop the chord: insertAt only drops it for Enter / EDIT', () => {
  const R = rig(); chordRow(R.row(2)); R.pick(0, 2); R.c.insertAt(0, 12, 'note'); assert.deepEqual([...R.row(12).slice(0, 3)], [52 - 4, -1, -1].map((v, i) => i ? v : R.c.last.note));
});

test('source: Enter / EDIT reach pickUp and the chord drop (the pad and the keyboard share editTap); the song document only', () => {
  assert.match(src, /if \(k === 'note' && v !== OFF && !stamp\)\{ lastTap = \{t:0, key:'', empty:false\}; pickUp\(c\.x, c\.y, true\); aud\(r\); return; \}/, 'a filled note: pickUp');
  assert.match(src, /if \(k !== 'tsp' && v < 0\)\{ lastTap\.empty = true; insertAt\(c\.x, c\.y, k, true\); \}/, 'an empty note: the drop');
  assert.match(src, /if \(k === 'note'\) lastChord = null;   \/\/ CHORD PICK-UP/, 'typing a note drops the chord');
  const code = chordCode + insertAtSrc;
  assert.doesNotMatch(code, /seq|worklet|rt\.|ctx\./i, 'no audio or sequencer code in the chord code');
});

let PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('chord pick-up in a real page: Enter on a poly row, Enter on an empty row in one undo step', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot, app } = await import('./sweep/ui-lib.mjs');
  const B = await boot({ width: 1440, height: 900, storage: { 'thunderplus-input-v1': 'keys' } });
  try {
    const r = JSON.parse(await app(B.page, `(async () => { const nap = ms => new Promise(r => setTimeout(r, ms)); cs.song.x = 9; cs.song.y = 0; go('phrase'); draw(); const p = S.phrases[cPhrase]; for (const q of p) q.fill(-1);
      p[2][0] = 48; p[2][1] = 52; p[2][2] = 55; p[2][IC] = 3; await nap(50); const u0 = undo.past.length;
      cs.phrase.x = 1; cs.phrase.y = 2; editTap(true); const picked = JSON.stringify(lastChord && lastChord.notes);
      lastTap = {t:0, key:'', empty:false}; cs.phrase.x = 0; cs.phrase.y = 10; editTap(true); await nap(50);
      const dropped = p[10].slice(0, 3), u1 = undo.past.length;
      return JSON.stringify({ poly: polyView(), picked, dropped, steps: u1 - u0 }); })()`));
    assert.ok(r.poly); assert.equal(r.picked, '[48,52,55]'); assert.deepEqual(r.dropped, [48, 52, 55]); assert.equal(r.steps, 1);
  } finally { await B.close(); }
});
