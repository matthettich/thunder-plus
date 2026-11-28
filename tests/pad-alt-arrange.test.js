// PAD ALT = THE ARRANGEMENT (build 705; owner, 28 Nov: "I'd love alt + arrows to navigate around the arrangement always, just as it does when
// phrase is in focus"; "step through empty cells"; "I don't want to change the behavior on a computer"). PAD mode with PATTERN NAV on: ALT + any
// arrow on every screen but Phrase moves the song cursor one cell (padArrStep) and the screen follows; Phrase keeps phraseGrid; KEYS mode and
// PATTERN NAV off are as before. padArrStep is cut out of index.html and run with stand-ins; the wiring is pinned as text. Undo: HANDOVER 28 Nov.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what); return m[0]; };
const stepSrc = cut(/\nfunction padArrStep\(dir\)\{[\s\S]*?\n\}\n/, 'padArrStep');
function rig(tv, o = {}) {
  const song = []; for (let y = 0; y < 256; y++) song.push(Array(33).fill(-1));
  song[4][2] = 7; song[9][2] = 8;
  const log = [];
  const c = Object.assign({ tv, NT: 31, MT: 32, navT: 0, performance: { now: () => 5 }, ntr: () => 8, S: { song }, selA: { v: 'x' }, apHost: 'phrase', apLastKey: 'k', apF: null, cTrack: 2, cFxT: 2, cSect: 0,
    PATSTEP_V: new Set(['mixer', 'fx', 'mod', 'inst']), cs: { song: { x: 2, y: 4, top: 0, pg: true }, mixer: { x: 2, y: 0 } }, clamp: (v, a, b) => Math.max(a, Math.min(b, v)), log,
    songViewRows: () => 8, arrSideMst: () => false, draw: () => log.push('draw'), apArrOk: () => true, apTop: () => log.push('apTop'), apFollow: () => { log.push('follow:' + c.apHost); return true; } }, o);
  vm.createContext(c); vm.runInContext(stepSrc, c); return c;
}
test('one cell at a time, empty cells included, on every screen; the screen follows (apFollow as that screen); at the edge it stays', () => {
  for (const v of ['song', 'mixer', 'fx', 'mod', 'inst', 'sect', 'project']) {
    const c = rig(v); c.padArrStep('down');
    assert.deepEqual([c.cs.song.x, c.cs.song.y], [2, 5], v + ': row 5 is empty and is where it goes (it used to skip to row 9)');
    assert.deepEqual(c.log, ['apTop', 'follow:' + v, 'draw'], v); assert.equal(c.apHost, 'phrase'); assert.equal(c.selA, null); assert.equal(c.cs.song.pg, false);
    c.padArrStep('right'); assert.equal(c.cs.song.x, 3, v + ': the next track, empty or not'); c.padArrStep('left'); c.padArrStep('left'); assert.equal(c.cs.song.x, 1);
  }
  const e = rig('song', { cs: { song: { x: 0, y: 0, top: 0 }, mixer: { x: 0, y: 0 } } }); e.padArrStep('up'); e.padArrStep('left'); assert.deepEqual([e.cs.song.x, e.cs.song.y], [0, 0], 'top-left edge: stays');
  const r = rig('song', { cs: { song: { x: 7, y: 0, top: 0 }, mixer: { x: 0, y: 0 } } }); r.padArrStep('right'); assert.equal(r.cs.song.x, 32, 'the master column on the Song screen');
  const a = rig('song', { apF: 'arr', cs: { song: { x: 7, y: 0, top: 0 }, mixer: { x: 0, y: 0 } } }); a.padArrStep('right'); assert.equal(a.cs.song.x, 7, 'Arrange without its master column: stays');
});
test('Mixer / FX / MOD / INST start from their own track; Sect shows the cell’s sect; the view keeps the row in sight', () => {
  const m = rig('mixer', { cs: { song: { x: 0, y: 4, top: 0 }, mixer: { x: 6, y: 0 } } }); m.padArrStep('up'); assert.deepEqual([m.cs.song.x, m.cs.song.y], [6, 3]);
  const f = rig('fx', { cFxT: 31 }); f.padArrStep('left'); assert.equal(f.cs.song.x, 7, 'from the master effects, ◀ is the last track');
  const s = rig('sect'); s.padArrStep('down'); s.padArrStep('down'); s.padArrStep('down'); s.padArrStep('down'); s.padArrStep('down'); assert.equal(s.cSect, 8, 'row 9 holds sect 08');
  const v = rig('song', { cs: { song: { x: 2, y: 7, top: 0 }, mixer: { x: 0, y: 0 } } }); v.padArrStep('down'); assert.equal(v.cs.song.top, 1, 'scrolls one row to keep the cursor in view');
});
test('wiring: PAD ALT (optDir with grid) off Phrase runs padArrStep when PATTERN NAV is on; KEYS mode still calls optDir only on Phrase; hints', () => {
  assert.match(src, /if \(tv === 'phrase' && !\(grid && patNavOn\) && pvActive\(\)\)[^\n]*\n[\s\S]{0,900}?if \(grid && patNavOn && tv !== 'phrase'\) return padArrStep\(dir\);\n  \/\/ Mixer FX row/);
  assert.match(src, /else if \(e\.altKey && \(arrow === 'up' \|\| arrow === 'down'\) && \(tv === 'phrase' \|\| tv === 'song'\)\)\{ if \(tv === 'phrase'\) optDir\(arrow, true\); else shiftAltArrow\(arrow\); \}/, 'KEYS mode ⌥▲▼ unchanged');
  assert.match(src, /else if \(e\.altKey\) nav\(arrow\);/, 'KEYS mode ⌥◀▶ still change screen');
  assert.match(src, /if \(inMode !== 'keys' && !patNavOn && HINT_OLD\[tv\]\) hintTxt = HINT_OLD\[tv\];/);
  assert.match(src, /else if \(c\.y === 5\) return fxActiveStep\(c\.x, d > 0 \? 1 : -1\);/, 'EDIT + arrows still turn the Mixer FX row\'s effects on and off');
});
