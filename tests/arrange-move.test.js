// ARRANGE CURSOR MOVES WHILE A SONG PLAYS (12 Nov, owner: "Scrolling up in Arrange while playback is happening on the audio thread sequencer on Android
// causes lags in playback"). move() is cut out of index.html and run in a vm with stand-ins; the drawArr0 / CSS changes are guarded by source checks.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function move\(dx, dy\)\{[\s\S]*?\n\}\nfunction psetGet/);
assert.ok(m, 'could not find move(): the code moved, this test needs updating');
const mv = m[0].replace(/function psetGet$/, '');

function rig({ playing = false, arrange = true, hold = false, pg = false, x = 2, y = 5, h = 255 } = {}) {
  const log = { draws: [], raf: [], cancelled: [] };
  const c = { tv: 'song', cs: { song: { x, y, pg }, phrase: { x: 0, y: 0 } }, MT: 16, tp: { on: playing }, apHold: hold, apSong: false, arrFast: false, arrSoon: 0,
    clamp: (v, a, b) => Math.max(a, Math.min(b, v)), dims: () => ({ w: 16, h: h + 1 }), ntr: () => 16, asongOn: () => arrange,
    draw() { log.draws.push({ fast: c.arrFast }); }, requestAnimationFrame(f) { log.raf.push(f); return log.raf.length; }, cancelAnimationFrame(i) { log.cancelled.push(i); } };
  vm.createContext(c); vm.runInContext(mv + '\nthis.move = move;', c);
  return { c, log, flush() { const f = log.raf.splice(0); f.forEach(g => g()); } };
}

test('move: ▲ at the first row (and ▼ at the last) changes nothing, so it draws nothing', () => {
  const r = rig({ y: 0 }); r.c.move(0, -1); assert.equal(r.log.draws.length, 0);
  const e = rig({ y: 255 }); e.c.move(0, 1); assert.equal(e.log.draws.length, 0);
});
test('move: a move that changes the cursor redraws once, as a cursor-only draw (arrFast)', () => {
  const r = rig({ y: 5 }); r.c.move(0, -1); assert.equal(r.c.cs.song.y, 4);
  assert.deepEqual(r.log.draws, [{ fast: true }]); assert.equal(r.c.arrFast, false, 'arrFast is off again after the draw');
  r.c.move(1, 0); assert.equal(r.c.cs.song.x, 3); assert.equal(r.log.draws.length, 2);
});
test('move: while a song plays the redraw waits for the next frame, and a second press in that frame adds none', () => {
  const r = rig({ playing: true, y: 20 }); r.c.move(0, 1); r.c.move(0, 1);
  assert.equal(r.c.cs.song.y, 22, 'the cursor itself moves at once'); assert.equal(r.log.draws.length, 0); assert.equal(r.log.raf.length, 1);
  r.flush(); assert.deepEqual(r.log.draws, [{ fast: true }]); assert.equal(r.c.arrSoon, 0);
  r.c.move(0, 1); assert.equal(r.log.raf.length, 1, 'the next press books the next frame');
});
test('move: paged by the playhead (pg), a press that cannot move still draws: it brings the cursor back into view', () => {
  const r = rig({ y: 0, pg: true }); r.c.move(0, -1); assert.equal(r.log.draws.length, 1); assert.equal(r.c.cs.song.pg, false);
});
test('move: Arrange beside another screen (apHold) and the other screens keep the plain draw()', () => {
  const a = rig({ hold: true, y: 3 }); a.c.move(0, 1); assert.deepEqual(a.log.draws, [{ fast: false }]);
  const b = rig({ arrange: false, y: 3, playing: true }); b.c.move(0, 1); assert.deepEqual(b.log.draws, [{ fast: false }]); assert.equal(b.log.raf.length, 0);
  const n = rig({ arrange: false, y: 0 }); n.c.move(0, -1); assert.equal(n.log.draws.length, 1, 'the text grid redraws as before');
});

test('draw: a full draw cancels the frame booked by a move; drawArr0 keeps its blocks only for arrFast and a matching key', () => {
  assert.match(src, /function draw\(\)\{\n  if \(arrSoon && !arrFast\)\{ cancelAnimationFrame\(arrSoon\); arrSoon = 0; \}/);
  assert.match(src, /fast = arrFast && arrSig && fkey === arrFastKey && arrFastN >= Math\.max\(cs\.song\.y \+ 1, \(cs\.song\.top \|\| 0\) \+ 40\)/, 'a rebuild is forced when the cursor goes past the built rows, the sizes change or arrSig is cleared');
  assert.match(src, /if \(fast\)\{\} else if \(ms\)\{ const pitch/, 'the height is not read back on a cursor-only draw');
});
test('CSS: the blocks have no b::before (it was clipped by their own overflow:hidden and doubled the compositing cost of every repaint)', () => {
  assert.doesNotMatch(src, /\.tagrid b::before\{/);
  assert.match(src, /\.tagrid b\{overflow:hidden\}/, 'the clip that made the pseudo-element inert is still there');
});
