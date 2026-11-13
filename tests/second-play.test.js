// SECOND PLAY (13 Nov, owner: "Remove that shortcut!"): the 31 Oct "STOP tapped twice goes to the top" gesture (narrowed in build 573) is gone. A second PLAY tap after a stop is a plain PLAY from
// the cursor whatever the timing; nothing moves the cursor.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const press = src.match(/  if \(b === 'play'\)\{[^\n]*\n(?:    [^\n]*\n)+/)[0];
function rig(tpOn, dt) {
  const log = []; let now = 1000;
  const c = { tp: { on: tpOn }, performance: { now: () => now }, mod: () => false, used: {}, takeLatch() {}, playKey: s => log.push('playKey' + (s ? '+shift' : '')), toTop: () => log.push('top'), log };
  vm.createContext(c); vm.runInContext('function t(b){ if (b === "x") return; ' + press.replace(/^  if \(b === 'play'\)\{/, '{') + ' }', c);
  c.t('play'); return log;
}
test('a quick second PLAY tap after a stop is a plain PLAY from the cursor: playKey, never toTop', () => {
  assert.deepEqual(rig(false), ['playKey']);
  assert.deepEqual(rig(true), ['playKey']);
});
test('the gesture and everything that only served it are gone', () => {
  for (const w of ['toTop', 'playWallT', 'playWasOn', 'stopTapT']) assert.equal(src.includes(w), false, w + ' must not exist');
  assert.match(src, /\(13 Nov, owner: "Remove that shortcut!"; supersedes 31 Oct and build 573's narrowing\)/);
});
test('STOP while stopped keeps its other job (panic) and never touches the cursor', () => {
  const f = src.match(/function stopKey\(\)\{[^\n]*/)[0]; assert.match(f, /panic\(\)/); assert.doesNotMatch(f, /cs\.song|toTop/);
});
