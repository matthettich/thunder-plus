// A newly loaded …chains patch must not inherit the previous patch's run list or bypass numbers. They are cached against the
// graph's change count and size; a fresh graph used to count from 0 again, so a patch with as many modules and cables as the
// one before looked unchanged: a Track In that was unpatched in the old patch and patched in the new one stayed switched off,
// and the chain was silent (found 14 Nov while adding the Delay line: its golden render came straight after a clock module's).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const N = 128, SR = 48000;
const D = (() => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx.ChainsDSP; })();
const m = (id, type, params) => ({ id, type, scope: 'mono', params });
const eng = () => new D.ChainsEngine(SR, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
const A = { modules: [m('a', 'trackIn', { track: 1 }), m('b', 'clock', { div: 3, swing: 0, width: .5, bars: 1 }), m('c', 'chainOut', { track: 1, level: 1 })], cables: [{ from: ['b', 'gate'], to: ['c', 'in'] }, { from: ['b', 'ramp'], to: ['c', 'inR'] }] };
const B = { modules: [m('a', 'trackIn', { track: 1 }), m('b', 'atten', { amount: 1, offset: 0 }), m('c', 'chainOut', { track: 1, level: 1 })], cables: [{ from: ['a', 'out'], to: ['b', 'in'] }, { from: ['b', 'out'], to: ['c', 'in'] }] };
const play = E => { const x = new Float32Array(N).fill(.5); E.setTrackInputs(x, x); E.process(N); return E.bus(1)[5]; };

test('a patch of the same size as the one before it runs its own modules, not the old patch\'s', () => {
  const E = eng(); E.loadPatch(A); play(E); assert.ok(!E._active().has('a'), 'in the first patch the Track In is not cabled, so it does not run');
  E.loadPatch(B); assert.equal(play(E), .5, 'the second patch (as many modules and cables) hears its Track In'); assert.ok(E._active().has('a'));
  const fresh = eng(); fresh.loadPatch(B); assert.equal(play(fresh), .5);
  E.loadPatch(A); play(E); E.loadPatch(B); assert.equal(play(E), .5, 'and again, back and forth');
});
test('the change count only ever goes up across loads and dispose, so nothing kept against it can look current', () => {
  const E = eng(); let last = E.graph.ver;
  for (const doc of [A, B, A, { modules: [], cables: [] }, B]) { E.loadPatch(doc); assert.ok(E.graph.ver > last, 'after a load: ' + E.graph.ver + ' > ' + last); last = E.graph.ver; }
  E.dispose(); assert.ok(E.graph.ver > last); assert.equal(E._actKey, -1); assert.equal(E._bnKey, -1);
});
test('bypass numbers are worked out again for the new patch', () => {
  const E = eng(), fx = (id, n) => m(id, 'utility', Object.assign({ gain: 0, pan: 0, mono: 0, flip: 0, mute: 0 }, n ? { _bn: n } : {}));
  E.loadPatch({ modules: [m('a', 'trackIn', { track: 1 }), fx('b', 3), m('c', 'chainOut', { track: 1, level: 1 })], cables: [{ from: ['a', 'out'], to: ['b', 'in'] }, { from: ['b', 'out'], to: ['c', 'in'] }] }); play(E);
  assert.equal(E._bnMap().get('b'), 3);
  E.loadPatch({ modules: [m('a', 'trackIn', { track: 1 }), fx('b', 7), m('c', 'chainOut', { track: 1, level: 1 })], cables: [{ from: ['a', 'out'], to: ['b', 'in'] }, { from: ['b', 'out'], to: ['c', 'in'] }] }); play(E);
  assert.equal(E._bnMap().get('b'), 7, 'the same shape with another number: the new number');
});
