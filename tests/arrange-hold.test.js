// HANDS ON WHILE A PAD ARROW IS HELD (12 Nov, owner: "Scrolling down in Arrange causes the cursor to get stuck at the bottom of the screen when the playhead is at
// the top, and the cursor cannot move past the bottom of the screen"). A tap's pointerdown starts FOLLOW's and Arrange's 4 s hold (workT, arrActT); a HELD arrow
// only did on its first press, so 4 s into the hold the playhead paged Arrange back (FOLLOW ON) or FOLLOW PATTERN pulled the cursor to the playhead.
// press() is cut out of index.html and run in a vm with a fake clock and timers.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function press\(b, fromKey\)\{[\s\S]*?\n\}\nfunction release/);
assert.ok(m, 'could not find press(): the code moved, this test needs updating');

function rig() {
  const timers = [], log = { dir: [], hold: [] }; let now = 1000;
  const c = { fxp: { open: false }, held: { shift: false, opt: false, edit: false }, used: {}, latch: { shift: false, opt: false }, workT: -1e9, arrActT: -1e9, rep: 0,
    performance: { now: () => now }, setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; }, clearTimeout() {}, takeLatch: () => ({ shift: false, opt: false }),
    dirPress: (b) => { log.dir.push(b); log.hold.push([c.workT, c.arrActT]); }, stopTapT: 0, tp: { on: false }, mod: () => false };
  vm.createContext(c); vm.runInContext(m[0].replace(/\nfunction release$/, '') + '\nthis.press = press;', c);
  return { c, timers, log, at(t) { now = t; } };
}

test('a held pad arrow: every auto-repeat refreshes the hands-on clocks, so the playhead cannot page Arrange or pull the cursor mid-hold', () => {
  const r = rig(); r.c.press('down');
  assert.equal(r.log.dir.length, 1, 'the first press'); assert.equal(r.timers[0].ms, 330, 'the repeat starts after 330 ms');
  r.at(5000); r.timers.pop().f();   // 4 s into the hold
  assert.deepEqual(r.log.dir, ['down', 'down']); assert.deepEqual(r.log.hold[1], [5000, 5000], 'workT and arrActT are now');
  assert.equal(r.timers[r.timers.length - 1].ms, 70, 'and it goes on every 70 ms');
  r.at(9000); r.timers.pop().f(); assert.deepEqual(r.log.hold[2], [9000, 9000], 'still hands on 8 s in');
});
test('a key held on the computer (fromKey) books no repeat of its own: the browser repeats it, and keydown already refreshes the clocks', () => {
  const r = rig(); r.c.press('down', true); assert.equal(r.timers.length, 0);
});
test('the window listeners that start the hold on a tap and on a key are still there', () => {
  assert.match(src, /window\.addEventListener\('pointerdown', e => \{[^\n]*e\.target\.closest\('#tPad'\)\) arrActT = workT;/);
  assert.match(src, /window\.addEventListener\('keydown', e => \{ if \(e\.key !== ' ' [^\n]*arrActT = workT = performance\.now\(\); \}, true\);/);
});
