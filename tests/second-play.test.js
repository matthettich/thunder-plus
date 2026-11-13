// SECOND PLAY JUMPS TO THE TOP (12 Nov, owner: Arrange had the keys, cursor on track 9 at row 8-12, PLAY pressed again and the cursor stood on row 000).
// Cause: the pad's "STOP tapped twice" (31 Oct) = a tap within 450 ms of a stop went to the top, whatever you had chosen. Now only for a cursor not touched since the play began.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const line = src.match(/if \(!tp\.on && performance\.now\(\) - stopTapT < 450\)\{[^\n]*\n/)[0];
function rig({ now, stopTap, apReady, apF, workT, arrActT, arrHandT, playWallT }) {
  const log = [], c = { tp: { on: false }, performance: { now: () => now }, stopTapT: stopTap, apReady, apF, workT, arrActT, arrHandT, playWallT, toTop: () => log.push('top'), log };
  vm.createContext(c);
  vm.runInContext('function t(){ ' + line + ' return "play"; }', c);
  c.t(); return log.join() || 'plain';
}
const base = { now: 1000, stopTap: 800, apReady: true, apF: null, workT: -1e9, arrActT: -1e9, arrHandT: -1e9, playWallT: 500 };
test('STOP twice: an untouched cursor, Arrange without the keys, goes to the top (31 Oct)', () => assert.equal(rig(base), 'top'));
test('Arrange with the keys: the second tap is a plain PLAY, the cursor stands', () => assert.equal(rig({ ...base, apF: 'arr' }), 'plain'));
test('a key / pad press / click since the play began: plain PLAY', () => {
  assert.equal(rig({ ...base, workT: 600 }), 'plain'); assert.equal(rig({ ...base, arrActT: 900 }), 'plain'); assert.equal(rig({ ...base, arrHandT: 700 }), 'plain');
});
test('a press from before the play began does not count; a slow second tap is a plain PLAY', () => {
  assert.equal(rig({ ...base, workT: 400 }), 'top'); assert.equal(rig({ ...base, stopTap: 100 }), 'plain');
});
test('playWallT is set when a play begins (songBtn)', () => assert.match(src, /if \(tp\.on && !playWasOn\) playWallT = performance\.now\(\); playWasOn = !!tp\.on;/));
