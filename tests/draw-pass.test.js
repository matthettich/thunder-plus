// DRAW PASS (13 Nov 2026): the playhead's redraws. Source guards for the rules plus a small run of playLite's decisions in a sandbox.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = re => { const m = src.match(re); assert.ok(m, 'not found: ' + re); return m[0]; };

test('busPlan is asked once per redraw: draw() and drawArr() turn the memo on, the core copy of busPlan is untouched', () => {
  assert.match(src, /busPlan = \(\(\) => \{ const raw = busPlan, m = \{on:false, v:null\}/);
  assert.match(src, /if \(!busPlan\.m\.on\)\{ busPlan\.m\.on = true; busPlan\.m\.v = null; const t0 = performance\.now\(\); try \{ return draw\(\); \} finally/);
  assert.match(src, /const was = busPlan\.m\.on; busPlan\.m\.on = true; try \{ drawArr0\(PH\); \} finally \{ if \(!was\)/);
  const f = cut(/function busPlan\(\)\{[\s\S]*?\n\}\n/); assert.doesNotMatch(f, /\.m\b/, 'busPlan itself (copied into core/seq-core.js) must not know the memo');
});
test('instTracks / padUses: memo only while a redraw runs, keyed on songVer and the song; deciding callers always scan', () => {
  assert.match(src, /if \(PT === undefined && busPlan\.m\.on\)\{ const M = useM\(\)/);
  assert.match(src, /useMemo && useMemo\.v === songVer && useMemo\.S === S/);
  assert.match(src, /function padUses\(pad\)\{ if \(!busPlan\.m\.on\) return padUses0\(pad\);/);
});
test('frame(): the playhead redraw goes through playTick, one per frame at most, and an owed redraw (plOwed) is made on the next frame', () => {
  assert.match(src, /else if \(ch \|\| lpPend \|\| plOwed\)\{ if \(lpSkip\) lpPend = true; else \{ lpPend = false; followPlay\(\); playTick\(\); \} \}\n  \} else \{ plOwed = false; plBase = null; \}/);
  assert.match(src, /if \(COARSE && now - plLast < PL_MIN\)\{ plOwed = true; return; \}/);
});
// playLite in a sandbox
function rig(o = {}) {
  const log = [];
  const c = { performance: { now: () => c.t }, t: 1000, view: 'tracker', apHold: false, apF: null, selA: null, sdrag: null, drag: null, arrFast: false, arrSoon: 0, tv: 'mixer', COARSE: true,
    cs: { a: 1 }, tp: { on: true, pos: [], mode: 'song', bpm: 120, tks: 6 }, cTrack: 0, cPhrase: 0, cSect: 0, patRow: -1, loopPat: false, followMode: 2, scr: { style: { fontSize: '14px' } }, tScale: 100, vpW: 900, arrW: 0, sideOn: true, inlOn: true, stage: 'off', arrMode: 1, spOn: false, pvMode: true, pvFx: false, pvFold: true, inMode: 'pad', ntr: () => 8,
    playDraw: false, scrAdd() { log.push('count'); }, apReady: false, apStrip() { log.push('strip'); }, apStripOk: () => false, drawArr() { log.push('arr'); }, playhead: () => null, pvActive: () => false, pvPlayMark() { log.push('pvmark'); }, pvLit: new Set(), litRows(r) { log.push('lit'); }, draw() { log.push('draw'); }, log, ...o };
  vm.createContext(c);
  vm.runInContext(cut(/const PL_BEAT = 500[\s\S]*?\nfunction frameWake\(\)/).replace(/function frameWake\(\)$/, '') + '\nthis.tick = playTick; this.base = () => plBase; this.owed = () => plOwed;', c);
  return c;
}
test('first tick is a full draw and records the baseline; the next on a screen that shows nothing of the playhead draws nothing', () => {
  const c = rig(); c.tick(); assert.deepEqual(c.log, ['draw']); c.t += 60; c.log.length = 0; c.tick(); assert.deepEqual(c.log, ['arr', 'count']);
});
test('a moved cursor, a tempo change, a selection or Arrange focus take the full draw; so does a baseline older than PL_BEAT', () => {
  const c = rig(); c.tick(); c.log.length = 0;
  c.t += 60; c.cs.a = 2; c.tick(); assert.deepEqual(c.log, ['draw']); c.log.length = 0;
  c.t += 60; c.tp.bpm = 130; c.tick(); assert.deepEqual(c.log, ['draw']); c.log.length = 0;
  c.t += 60; c.selA = {}; c.tick(); assert.deepEqual(c.log, ['draw']); c.selA = null; c.log.length = 0;
  c.t += 60; c.apF = 'arr'; c.tick(); assert.deepEqual(c.log, ['draw']); c.apF = null; c.log.length = 0;
  c.t += 60; c.tick(); assert.deepEqual(c.log, ['draw'], 'the stamp changed back: one more full draw'); c.log.length = 0;
  c.t += 60; c.tick(); assert.deepEqual(c.log, ['arr', 'count']); c.log.length = 0;
  c.t += 600; c.tick(); assert.deepEqual(c.log, ['draw'], 'heartbeat');
});
test('Song / Arrange (their text carries the playing row) always take the full draw', () => {
  const c = rig({ tv: 'song' }); c.tick(); c.t += 60; c.log.length = 0; c.tick(); assert.deepEqual(c.log, ['draw']);
});
test('Phrase and Sect move the playing-row marks in place; Pattern view also its per-track bands', () => {
  const c = rig({ tv: 'phrase', pvActive: () => true }); c.tick(); c.t += 60; c.log.length = 0; c.tick(); assert.deepEqual(c.log, ['lit', 'pvmark', 'arr', 'count']);
  const d = rig({ tv: 'sect' }); d.tick(); d.t += 60; d.log.length = 0; d.tick(); assert.deepEqual(d.log, ['lit', 'arr', 'count']);
});
test('touch screens: no playhead drawing closer than PL_MIN; the frame owes it and pays on the next one past the gap; a desktop is not capped', () => {
  const c = rig(); c.tick(); c.log.length = 0; c.t += 10; c.tick(); assert.deepEqual(c.log, []); assert.equal(c.owed(), true);
  c.t += 45; c.tick(); assert.deepEqual(c.log, ['arr', 'count']); assert.equal(c.owed(), false);
  const d = rig({ COARSE: false }); d.tick(); d.log.length = 0; d.t += 5; d.tick(); assert.deepEqual(d.log, ['arr', 'count']);
});

test('the playhead-only update is timed into the CPU panel\'s screen figure; a full redraw is counted once (by draw() itself)', () => {
  assert.match(src, /if \(playLite\(now\)\) scrAdd\('draw', performance\.now\(\) - now\);/);
  const body = cut(/function playTick\(\)\{[\s\S]*?\n\}\n/); assert.equal((body.match(/scrAdd\(/g) || []).length, 1, 'only the playLite branch adds; draw() adds its own');
});
