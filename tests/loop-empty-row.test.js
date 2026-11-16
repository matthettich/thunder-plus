// LOOP PLAY DOES NOT PAGE THE SONG AWAY (build 626, owner: "Pressing loop paged down the bottom of the song on song mode"). From an empty song row the loop used to start at the next
// row with a block (or row 0) and the view paged there. Now, on the Song screen, it starts only if that row is in view; else a toast and nothing starts. And arrWrapRow ends at the
// held row as soon as LOOP is on (before syncLoop sets tp.loopV), so the look-ahead does not turn the page for a loop that stays put.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const pl = src.match(/let lpW = null;[^\n]*\nfunction lpRestore\(\)[^\n]*\nfunction playPatLoop\(\)\{[\s\S]*?\n\}\n/); assert.ok(pl, 'playPatLoop moved');
function rig(y, top, next, tv = 'song') {
  const song = Array.from({ length: 256 }, () => []); song[next] = [3];
  const c = { loopPat: false, patRow: -1, patCur: -1, tv, cs: { song: { y, top } }, tp: { on: false }, warmTok: null, S: { song }, log: [], toasts: [], songRow: () => next, asongOn: () => true, arrRows: () => 14, songRows: () => 20,
    rS: n => String(n).padStart(3, '0'), toast: t => c.toasts.push(t), tStopUser() {}, tStart: (m, o) => { c.tp.on = true; c.log.push(o.row); }, draw() {}, toggleLoopPat: () => { c.loopPat = !c.loopPat; } };
  vm.createContext(c); vm.runInContext(pl[0] + '\nthis.f = playPatLoop;', c); return c;
}
test('an empty cursor row whose next block is out of view: nothing starts, nothing changes, a toast says what to do', () => {
  const c = rig(60, 55, 120); c.f(); assert.deepEqual(c.log, []); assert.equal(c.loopPat, false, 'LOOP stays as it was'); assert.equal(c.tp.on, false); assert.match(c.toasts[0], /Nothing on row 060/);
  const d = rig(60, 55, 0); d.f(); assert.deepEqual(d.log, [], 'row 0 above the view too');
});
test('an empty cursor row whose next block is in view still starts there; a filled row starts where it is; other screens are as before', () => {
  const c = rig(60, 55, 64); c.f(); assert.deepEqual(c.log, [64]);
  const d = rig(60, 55, 60); d.S.song[60] = [1]; d.f(); assert.deepEqual(d.log, [60]);
  const e = rig(60, 55, 120, 'phrase'); e.f(); assert.deepEqual(e.log, [120], 'not the Song screen: the old rule');
});
test('arrWrapRow: LOOP on while playing the song ends at the row it holds, before loopV is set', () => {
  const m = src.match(/function arrWrapRow\(\)\{[\s\S]*?\n\}\n/); assert.ok(m);
  const c = { tp: { loopV: '', mode: 'song' }, loopPat: true, patRow: 28, loopArm: null, S: { song: Array.from({ length: 256 }, (_, y) => (y <= 55 ? [1] : [])) } };
  vm.createContext(c); vm.runInContext(m[0] + '\nthis.f = arrWrapRow;', c);
  assert.equal(c.f(), 28); c.loopPat = false; assert.equal(c.f(), 55, 'no LOOP: the song\'s last row'); c.loopPat = true; c.patRow = -1; assert.equal(c.f(), 55, 'no held row yet');
});
