// 19 Nov: "1 s / 3 s after STOP" lines are cancelled by the next PLAY; the "tracks" line exists and is called on PLAY and after a mix change.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), H = require('./helpers');
const src = H.read('index.html');
test('a PLAY cancels the pending after-STOP reports', () => {
  const m = src.match(/function pbStopCancel\(\)\{[^\n]*\n/); assert.ok(m);
  const tm = []; const c = vm.createContext({ PB: { stopTm: [1, 2] }, clearTimeout: h => tm.push(h) }); vm.runInContext(m[0] + 'pbStopCancel();', c);
  assert.deepEqual(tm, [1, 2]); assert.equal(c.PB.stopTm.length, 0);
  assert.match(src, /rkReset\(\); pbStopCancel\(\); pbLog\('PLAY '/); assert.match(src, /pbStopCancel\(\); for \(const ms of \[1000, 3000\]\) \(PB\.stopTm = PB\.stopTm \|\| \[\]\)\.push\(setTimeout/);
});
test('the per-track state line is written on PLAY and after a mix change', () => {
  assert.match(src, /pbTracks\('PLAY'\)/); assert.match(src, /function mixChanged\(\)\{[^\n]*pbTracks\('after a mix change'\)/);
  assert.match(src, /STUCK-REST/);
});
