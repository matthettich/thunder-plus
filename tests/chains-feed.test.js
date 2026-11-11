// Chains rail, feed view (9 Nov): chainReach / chainFeed cut out of index.html and run in a vm.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
function cut(re, what){ const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }

function ctx(){
  const blk = cut(/function chainMods\(t\)\{[\s\S]*?\n(?=\/\/ The editor's value formats)/, 'chainMods, chainReach and chainFeed');
  const c = vm.createContext({ Math, Set, Map, Array, Number, NT: 12,
    clamp: (v, a, b) => Math.min(b, Math.max(a, v)), int: (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d });
  vm.runInContext(blk, c); return c;
}
const mod = (id, type, track) => ({ id, type, params: track ? { track } : {} });
const cab = (a, b) => ({ from: [a, 'out'], to: [b, 'in'] });
const ids = s => [...s].sort();

function patch(){   // Track Ins 04 03 01 -> mixer -> reverb -> Out 08, plus a delay branch off mixer that goes nowhere, and a stray comp
  return { modules: [mod('i4', 'trackIn', 4), mod('i3', 'trackIn', 3), mod('i1', 'trackIn', 1), mod('mx', 'smixer'), mod('rv', 'reverb'), mod('dl', 'delay'), mod('cp', 'comp'), mod('o8', 'chainOut', 8)],
    cables: [cab('i4', 'mx'), cab('i3', 'mx'), cab('i1', 'mx'), cab('mx', 'rv'), cab('rv', 'o8'), cab('mx', 'dl'), cab('cp', 'rv')] };
}

test('chain feed: the path runs from this track\'s Track In to the Chain Out, branches that do not reach it are off the path', () => {
  const c = ctx(); c.S = { chainPatch: patch() };
  const f = c.chainFeed(3);
  assert.equal(f.outT, 7);
  assert.deepEqual(ids(f.act), ['cp', 'i1', 'i3', 'i4', 'mx', 'o8', 'rv'], 'the module set is the output track\'s, a dead-end delay is not in it');
  assert.deepEqual(ids(f.path), ['i4', 'mx', 'o8', 'rv'], 'other tracks\' inputs and the modulating comp are off the path');
  assert.deepEqual(ids(c.chainFeed(0).path), ['i1', 'mx', 'o8', 'rv']);
});
test('chain feed: nothing to show when the track feeds nothing', () => {
  const c = ctx(); const p = patch(); p.modules.push(mod('i5', 'trackIn', 5), mod('i6', 'trackIn', 6)); p.cables.push(cab('i6', 'dl'));
  c.S = { chainPatch: p };
  assert.equal(c.chainFeed(4), null, 'a Track In wired to nothing');
  assert.equal(c.chainFeed(5), null, 'wired to a branch that never reaches a Chain Out');
  assert.equal(c.chainFeed(1), null, 'no Track In for the track');
  assert.equal(c.chainFeed(7), null, 'the output track has a chain of its own (the rail does not ask)... and no Track In');
  assert.equal(c.chainFeed(12), null, 'master / out of range');
  assert.equal(c.chainFeed(-1), null);
  c.S = { chainPatch: null }; assert.equal(c.chainFeed(3), null);
  c.S = { chainPatch: { modules: [], cables: [] } }; assert.equal(c.chainFeed(3), null);
});
test('chain feed: two separate chains, the first on the bench is shown and both are listed', () => {
  const c = ctx(); const p = patch(); p.modules.push(mod('o9', 'chainOut', 9), mod('ec', 'eq')); p.cables.push(cab('i4', 'ec'), cab('ec', 'o9'));
  c.S = { chainPatch: p };
  const f = c.chainFeed(3);
  assert.deepEqual([...f.outs], [7, 8]); assert.equal(f.outT, 7);
  assert.deepEqual(ids(f.path), ['i4', 'mx', 'o8', 'rv'], 'the second chain is not on the shown path');
});
test('chain feed: cycles in the cables end (feedback loops), and cables to missing modules are harmless', () => {
  const c = ctx(); const p = patch(); p.cables.push(cab('rv', 'mx'), cab('rv', 'rv'), cab('mx', 'ghost'), { from: null, to: ['x', 'in'] }, null);
  c.S = { chainPatch: p };
  const f = c.chainFeed(3);
  assert.deepEqual(ids(f.path), ['i4', 'mx', 'o8', 'rv']);
  assert.deepEqual(ids(c.chainReach(p, ['mx'])), ['dl', 'ghost', 'mx', 'o8', 'rv']);
});
