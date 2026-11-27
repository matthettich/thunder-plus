// songBpm() / songLpb() (build 699): they sit in the ENGINE block, which runs both on the page (outside the app's IIFE, where the
// song `S` is a let) and in the render worker (whose glue declares its own S). On the page they could not see S and always said
// 120 BPM / 4 lines a beat, so line-synced envelopes rendered for 120 BPM and never re-rendered on a tempo change.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers'), src = H.read('index.html');
const line = a => { const i = src.indexOf(a); assert.ok(i >= 0, 'moved: ' + a); return src.slice(i, src.indexOf('\n', i)); };
const fns = line('function songBpm(){') + '\n' + line('function songLpb(){') + '\nthis.bpm = songBpm; this.lpb = songLpb;';

test('on the page (no S in scope) they read the song through tpSongNow', () => {
  const c = vm.createContext({}); c.tpSongNow = () => ({ bpm: 90, lpb: 6 }); vm.runInContext(fns, c);
  assert.equal(c.bpm(), 90); assert.equal(c.lpb(), 6);
});
test('in the render worker (its glue declares S) they read that S', () => {
  const c = vm.createContext({}); vm.runInContext('var S = {bpm: 133, lpb: 3};\n' + fns, c);
  assert.equal(c.bpm(), 133); assert.equal(c.lpb(), 3);
});
test('nothing to read (before the song exists): 120 and 4, no throw', () => {
  const c = vm.createContext({}); c.tpSongNow = () => { throw new ReferenceError('S before init'); }; vm.runInContext(fns, c);
  assert.equal(c.bpm(), 120); assert.equal(c.lpb(), 4);
});
test('the app sets tpSongNow to its own S, just after the song and undo exist', () => {
  assert.match(src, /const undo = \{past:\[\], future:\[\], cur:JSON\.stringify\(S\)[^\n]*\nglobalThis\.tpSongNow = \(\) => S;/);
});
