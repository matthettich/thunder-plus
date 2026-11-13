// CLEAR ALL SOLO AND MUTE (12 Nov, owner: "a quick double tap on the button"). clearSoloMute() is cut out of index.html and run in a vm: it clears every track's solo and mute,
// leaves the per-block mutes (S.smute) alone, toasts the count, and does nothing (no save) when nothing was set. The double-tap rule in press() is checked on the source.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function clearSoloMute\(pre\)\{[\s\S]*?\n\}\n/);
assert.ok(m, 'could not find clearSoloMute: the code moved, this test needs updating');
function rig(mix, smute = { '2:3': true }) {
  const log = { toast: [], saves: 0, draws: 0 };
  const c = { S: { mix, smute }, mixChanged: () => { log.saves++; }, draw: () => { log.draws++; }, toast: t => log.toast.push(t) };
  vm.createContext(c); vm.runInContext(m[0] + '\nthis.f = clearSoloMute;', c); return { c, log };
}
test('clears every solo and mute, counts the tracks, one save', () => {
  const { c, log } = rig([{ solo: true }, { mute: true }, {}, { solo: true, mute: true }]);
  c.f(); assert.ok(c.S.mix.every(x => !x.solo && !x.mute)); assert.equal(log.saves, 1);
  assert.match(log.toast[0], /Solo and mute cleared · 3 tracks/);
});
test('per-block mutes (S.smute) are untouched', () => { const { c } = rig([{ mute: true }]); c.f(); assert.deepEqual({ ...c.S.smute }, { '2:3': true }); });
test('nothing set: no save, says so', () => { const { c, log } = rig([{}, {}]); c.f(); assert.equal(log.saves, 0); assert.match(log.toast[0], /nothing was set/); });
test('the count given by the first tap is used (the first tap may have set one)', () => { const { c, log } = rig([{}, {}]); c.f(1); assert.match(log.toast[0], /· 1 track$/); });
test('press(): same button within 350 ms clears; another pad key or button resets', () => {
  assert.match(src, /if \(b !== 'mute' && b !== 'solo'\) msTap\.b = '';/);
  assert.match(src, /if \(msTap\.b === b && now - msTap\.t < 350\)\{[\s\S]{0,120}clearSoloMute\(pre\)/);
});
