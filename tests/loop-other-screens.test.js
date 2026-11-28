// LOOP SEL armed on Arrange, PLAY pressed on a screen with no rows of its own (owner, 15 Nov: "switching to the mixer panel caused the
// sequencer to ignore the loop selection I had turned on"). loopFor() and playCtx() are cut out of index.html and run with stubs.
// Also: pads whose render was dropped start rendering at once (the new poly track that was silent on its first pass).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const lf = src.match(/\nfunction loopFor\(view\)\{[\s\S]*?\n\}\n/), pc = src.match(/\nfunction playCtx\(\)\{[\s\S]*?\n\}\n/);
assert.ok(lf && pc, 'could not find loopFor / playCtx: the code moved, this test needs updating');
function rig({ tv, arm = null, loopSel = true, sel = null }) {
  const log = [], c = { tv, loopSel, loopArm: arm, selA: sel, lsLock: false, tp: { on: false }, GRIDV: new Set(['song', 'sect', 'phrase']), S: { plen: [] }, cPhrase: 0, cSect: 0, cTrack: 0, NT: 32, PROWS: 64,
    onPset: () => false, selRect: () => sel && sel.r, clamp: (v, a, b) => Math.max(a, Math.min(b, v)), isBusT: () => false, busPlay: () => log.push('bus'), tStopUser: () => log.push('stop'),
    toast: m => log.push('toast'), songRow: () => 5, tStart: (m, o) => log.push(m + ':' + (o.loop ? o.loop.y0 + '-' + o.loop.y1 : 'row' + o.row)), log };
  vm.createContext(c); vm.runInContext(lf[0] + pc[0] + '\nthis.go = playCtx; this.arm = () => loopArm;', c); return c;
}
test('LOOP SEL armed on Arrange: PLAY on Mixer, FX, MOD and INST plays the armed rows as a loop', () => {
  for (const tv of ['mixer', 'fx', 'mod', 'inst', 'project']) { const c = rig({ tv, arm: { v: 'song', y0: 8, y1: 15 } }); c.go(); assert.deepEqual(c.log, ['song:8-15'], tv); }
});
test('LOOP SEL off, or nothing armed: those screens play the song from its row, as before', () => {
  let c = rig({ tv: 'mixer', arm: { v: 'song', y0: 8, y1: 15 }, loopSel: false }); c.go(); assert.deepEqual(c.log, ['song:row5']);
  c = rig({ tv: 'mixer', arm: null }); c.go(); assert.deepEqual(c.log, ['song:row5']);
});
test('a loop armed in Phrase view does not start from the Mixer (the Mixer plays the song)', () => {
  const c = rig({ tv: 'mixer', arm: { v: 'phrase', y0: 0, y1: 7 } }); c.go(); assert.deepEqual(c.log, ['song:row5']);
});
test('on Arrange itself nothing changed: the selection arms and plays', () => {
  const c = rig({ tv: 'song', sel: { v: 'song', r: { y0: 2, y1: 5 } } }); c.go(); assert.deepEqual(c.log, ['song:2-5']);
});
test('leaving a screen keeps the selection as the armed loop', () => {
  assert.match(src, /if \(loopSel && !tp\.on && !\(tv !== 'song' && loopArm && loopArm\.v === 'song'\)\) loopFor\(tv\);[^\n]*\n  tv = v; if \(MID\.includes\(v\)\) midV = v; selA = null;/);
});
test('dropped pad renders are started again: after the track counts change, and when PLAY is pressed', () => {
  assert.match(src, /if \(vtrk\) growKit\(\);\n[\s\S]{0,500}if \(vtrk && \(n1 !== n0 \|\| m1 !== m0\)\) try \{ prerender\(\); \} catch\(e\)\{(?: tpLog\('[^']*', e\); )?\}/);
  assert.match(src, /if \(!tp\.on && !\(o && o\._warm\) && PW\.ready\) try \{ prerender\(\); \} catch\(e\)\{(?: tpLog\('[^']*', e\); )?\}/);
});
test('LP.SEL lock (23 Nov): a locked loop keeps its rows while another selection is made; a stale lock (not playing) does not', () => {
  const sel = { r: { y0: 0x20, y1: 0x27 } };
  let c = rig({ tv: 'song', arm: { v: 'song', y0: 8, y1: 15 }, sel: Object.assign({ v: 'song' }, sel) }); c.lsLock = true; c.tp.on = true; c.warmTok = 0;
  c.tp.on = false; c.lsLock = true; c.warmTok = 1; c.go(); assert.deepEqual(c.log, ['song:8-15'], 'locked during warm-up: the armed rows, not the selection');
  c = rig({ tv: 'song', arm: { v: 'song', y0: 8, y1: 15 }, sel: Object.assign({ v: 'song' }, sel) }); c.lsLock = true; c.warmTok = 0; c.go(); assert.deepEqual(c.log, ['song:32-39'], 'stale lock: the selection again');
});
