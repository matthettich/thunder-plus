// SHIFT+▼ ON MOBILE (12 Nov, owner: "Make shift + down always go to phrase or pattern view."): from the Song / Arrange (and Sect) it opens the block's phrase.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const fn = src.match(/function navDownToPhrase\(\)\{[\s\S]*?\n\}\n/); assert.ok(fn, 'navDownToPhrase not found');
function rig({ cell = 5, pv = false, tv = 'song', free = 20 } = {}) {
  const log = [], c = { tv, cs: { song: { x: 2, y: 7 }, phrase: { y: 9 } }, S: { song: Array.from({ length: 16 }, (_, y) => y === 7 ? [0, 0, cell] : []) }, cTrack: 0, cPhrase: 3, pendPlace: null, log,
    go: v => { log.push('go ' + v + ' newBlockAllowed=' + !c.goNoNew); c.tv = v; }, pvActive: () => pv, pvGo: (...a) => log.push('pvGo ' + a.join()), freePhrase: () => free, hp: n => n, rS: n => n,
    toast: t => log.push('toast ' + t), draw() {} };
  vm.createContext(c); vm.runInContext('var goNoNew = false;' + fn[0] + '\nthis.navDownToPhrase = navDownToPhrase;', c); return c;
}
test('a filled block: go(phrase) on its track, without a block made first; nothing else', () => {
  const c = rig(); c.navDownToPhrase(); assert.equal(c.tv, 'phrase'); assert.equal(c.cTrack, 2); assert.deepEqual(c.log, ['go phrase newBlockAllowed=false']); assert.equal(c.goNoNew, false);
});
test('Pattern view: pvGo takes the song cell (it also handles an empty one)', () => {
  const c = rig({ pv: true, cell: -1 }); c.navDownToPhrase(); assert.deepEqual(c.log, ['go phrase newBlockAllowed=false', 'pvGo 2,7,,']);
});
test('an empty cell opens a fresh phrase that waits in pendPlace, and makes no pattern', () => {
  const c = rig({ cell: -1 }); c.navDownToPhrase(); assert.equal(c.cPhrase, 20); assert.equal(c.pendPlace.x, 2); assert.equal(c.pendPlace.y, 7); assert.equal(c.pendPlace.p, 20); assert.match(c.log[1], /new phrase 20/);
});
test('wiring: every setup (desktop too), Song and Sect, before the old Mixer line; go() honours goNoNew', () => {
  assert.match(src, /if \(dir === 'down' && \(tv === 'song' \|\| tv === 'sect'\)\) return navDownToPhrase\(\);[^\n]*\n  if \(dir === 'down'\) return tv === 'project'/);
  assert.match(src, /&& !sectsOn && !goNoNew\) newBlock\(/);
});
test('desktop: no isMobile() gate on the SHIFT+▼ line; Mobile ▲ keeps its own gate', () => {
  assert.doesNotMatch(src, /dir === 'down' && isMobile\(\)/);
  assert.match(src, /if \(dir === 'up' && isMobile\(\)\)/);
});
