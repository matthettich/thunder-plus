// LOOP PLAY (12 Nov, owner: "cmd bar should be loop Play"; the pad's PLAY stays plain). The control bar's ▶ (data-c=song) turns LOOP on and plays; LOOP back to
// what it was after the stop (lpW, checked at the top of draw()).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const pl = src.match(/let lpW = null;[^\n]*\nfunction lpRestore\(\)[^\n]*\nfunction playPatLoop\(\)\{[\s\S]*?\n\}\n/);
assert.ok(pl, 'could not find playPatLoop / lpRestore');
function rig(loopPat0) {
  const log = [];
  const c = { loopPat: loopPat0, patRow: 5, patCur: 5, cs: { song: { y: 3 } }, tp: { on: false }, warmTok: null, S: { song: [[], [], [], [-1, 2]] }, log,
    tStopUser: () => { c.tp.on = false; log.push('stop'); }, tStart: (m, o) => { c.tp.on = true; log.push(['start', m, o.row]); }, songRow: () => 9, draw() {},
    toggleLoopPat: () => { c.loopPat = !c.loopPat; log.push('loop ' + c.loopPat); } };
  vm.createContext(c); vm.runInContext(pl[0] + '\nthis.playPatLoop = playPatLoop; this.lpRestore = lpRestore; this.lpW = () => lpW; this.lpSeen = () => { if (lpW && c.tp.on) lpW.seen = true; };', c);
  return c;
}
test('loop play turns LOOP on, starts the song from the cursor row, and restores LOOP when it ends (LOOP was off)', () => {
  const c = rig(false); c.playPatLoop();
  assert.equal(c.loopPat, true); assert.deepEqual(c.log, ['loop true', ['start', 'song', 3]]); assert.equal(c.lpW().was, false);
  c.playPatLoop(); assert.equal(c.log.at(-1), 'stop');   // pressed again: stops
  c.lpRestore(); assert.equal(c.loopPat, false, 'LOOP is back to off: the pad PLAY is plain afterwards'); assert.equal(c.lpW(), null);
});
test('LOOP that was on before the loop play stays on after it; a LOOP press during it (off) is put back too', () => {
  const c = rig(true); c.playPatLoop(); assert.ok(!c.log.includes('loop false'), 'already on: not toggled'); c.loopPat = false; c.lpRestore(); assert.equal(c.loopPat, true);
});
test('wiring: the control bar button runs it, the pad PLAY and Space do not, draw() ends it, the labels say loop', () => {
  assert.match(src, /if \(c === 'song'\) return playPatLoop\(\);/);
  assert.match(src, /if \(lpW\)\{ if \(tp\.on\) lpW\.seen = true; else if \(lpW\.seen\) lpRestore\(\); \}/);
  assert.match(src, /function playKey\(shift\)\{[^\n]*tStart\('song', \{row:songRow\(\)\}\)/);
  assert.doesNotMatch(src.match(/function playKey\(shift\)\{[^\n]*/)[0], /playPatLoop/);
  assert.match(src, /'▶ LOOP'/); assert.match(src, /'Loop play the song, or stop'/); assert.match(src, /\['song', '▶↻', 'Loop play/);
});
