// MOD FOLLOWS ARRANGE (14 Nov, owner: "neither track shows any modulation until I navigate to the song mode and back to the MOD screen").
// With Arrange holding the keys beside MOD (Alt+A), moving its cursor across tracks left MOD on the old track (apFollow moved the
// FX screen's track but not MOD's, and did not ask for a redraw). Also: an LFO whose target no longer resolves is still labelled plainly.
// apFollow / tgtFx / tgtLabel are cut out of index.html and run in a vm with stand-ins for the rest of the app.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, 'moved: ' + a); return src.slice(i, j); };
const follow = cut('function apFollow(', 'function apAsSong(');
const lab = cut('function tgtFx(', 'function tgtLabel(') + cut('function tgtLabel(', '// The song\'s own value');

function rig(host) {
  const c = { NT: 22, MT: 22, tv: 'mod', apHost: host, apPin: false, apLastKey: '', cSect: 0, cPhrase: 0, cTrack: 0, cInst: 0, cFxT: 0,
    cs: { song: { x: 0, y: 0 } }, S: { song: [[-1, -1, -1]] }, apKey: () => 'k' + c.cs.song.x, ntr: () => 22, firstPhrase: () => -1, isBusT: () => false,
    pvActive: () => false, phraseToCursor() {}, phraseInfo: () => ({ inst: -1 }) };
  vm.createContext(c); vm.runInContext(follow + '\nthis.apFollow = apFollow;', c);
  return c;
}
test('Arrange beside MOD: moving its cursor to another track moves MOD there, and asks for a redraw', () => {
  const c = rig('mod'); c.cs.song.x = 12;
  assert.equal(c.apFollow(), true, 'apFollow reports a change so apBack redraws');
  assert.equal(c.cFxT, 12, 'MOD (modT reads cFxT) is on the track Arrange is on');
  c.cs.song.x = 22; c.apFollow(); assert.equal(c.cFxT, 22, 'the master column is the master (NT)');
});
test('the owner\'s case: Arrange holding the keys beside MOD, cursor on track 13 (index 12): MOD lists track 13\'s LFOs, not track 12\'s', () => {
  const c = rig('mod'); c.cFxT = 11;   /* MOD was showing track 12 */
  c.S.perf = { lfo: { 11: [{ id: 'fx:11:0:0' }, { id: 'fx:11:1:8' }, { id: 'fx:11:3:7' }], 12: [{ id: 'fx:12:3:7' }] } };
  vm.runInContext(cut('const modT = ', 'const modLab'), c); vm.runInContext('this.modT = modT; this.modArr = modArr;', c);
  c.cs.song.x = 12; c.apFollow();
  assert.equal(c.modT(), 12); assert.equal(c.modArr().length, 1); assert.equal(c.modArr()[0].id, 'fx:12:3:7');
  assert.equal(c.modT() + 1, 13, 'the header prints t + 1: TRACK 13');
});
test('Arrange beside FX still follows (unchanged)', () => {
  const c = rig('fx'); c.cs.song.x = 5; assert.equal(c.apFollow(), true); assert.equal(c.cFxT, 5);
});
test('an LFO target that does not resolve is labelled plainly, not "(empty)"', () => {
  const c = { MT: 32, TFX: { dly: { n: 'DLAY', ps: [{ l: 'TIME' }] } }, isPumpT: () => false, tgtTrk: t => 'TRACK ' + (t + 1),
    tgtParse: id => { const p = id.split(':'); return { k: p[0], t: +p[1], s: +p[2], p: +p[3], b: undefined }; },
    S: { tfx: Array.from({ length: 32 }, () => []), mfx: [] } };
  c.S.tfx[11] = [{ type: 'dly' }, null, null, null];
  vm.createContext(c); vm.runInContext(lab + '\nthis.tgtLabel = tgtLabel;', c);
  assert.equal(c.tgtLabel('fx:11:0:0', true), 'FX1 DLAY TIME');
  assert.equal(c.tgtLabel('fx:11:0:8', true), 'FX1 DLAY: no such setting');
  assert.equal(c.tgtLabel('fx:11:3:7', true), 'FX4: no effect in this slot');
});
