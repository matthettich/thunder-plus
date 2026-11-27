// Patch knobs S4: a Knob added from the palette goes on the bottom panel as K<n>; nothing else adds one.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('chains/index.html');
const pick = re => { const m = src.match(re); assert.ok(m, 'not found: ' + re); return m[0]; };
function world(){
  const code = pick(/function knobToDeck\(m\)\{[\s\S]*?\n\}\n/);
  const calls = { touch: 0, mark: 0, tw: 0 };
  const doc = { modules: [], deck: undefined };
  const c = vm.createContext({ doc, Math, Array, calls, EMBED: true, TW: { deck(){ calls.tw++; } },
    store: { touch(){ calls.touch++; } }, markDeck(){ calls.mark++; },
    deckHas: (id, k) => (doc.deck || []).some(d => d.id === id && d.p === k),
    deckDesc: (m, k) => ({ id: m.id, p: k, ml: 'Knob', l: 'Value' }) });
  vm.runInContext(code, c); return { c, doc, calls };
}
test('knobToDeck puts Value on the panel labelled K<n>, once', () => {
  const { c, doc, calls } = world(), m = { id: 'a', type: 'knob', params: { n: 3 } };
  c.knobToDeck(m); c.knobToDeck(m);
  assert.equal(doc.deck.length, 1); assert.equal(doc.deck[0].l, 'K3'); assert.equal(doc.deck[0].p, 'value');
  assert.equal(calls.touch, 1); assert.equal(calls.mark, 1); assert.equal(calls.tw, 1);
});
test('two Knobs get two entries; existing deck entries are kept', () => {
  const { c, doc } = world(); doc.deck = [{ id: 'z', p: 'x' }];
  c.knobToDeck({ id: 'a', params: { n: 1 } }); c.knobToDeck({ id: 'b', params: { n: 2 } });
  assert.deepEqual(doc.deck.map(d => d.l || d.p), ['x', 'K1', 'K2']);
});
test('the hook is in addItem only (not addModule, load or paste), for the knob type; removal already drops deck entries', () => {
  const add = pick(/function addItem\(type, x, y\)\{[\s\S]*?\n\}\n/);
  assert.match(add, /type === 'knob'\) knobToDeck\(m\)/);
  assert.doesNotMatch(pick(/function addModule\(type, x, y, params\)\{[\s\S]*?\n\}\n/), /knobToDeck/);
  assert.doesNotMatch(pick(/function pasteGraph\(graph, pos\)\{[\s\S]*?\n\}\n/), /knobToDeck/);
  assert.match(pick(/function removeModule\(id\)\{[\s\S]*?\n/ ) + src.match(/function removeModule[\s\S]{0,300}/)[0], /doc\.deck = doc\.deck\.filter\(d => d\.id !== id\)/);
});
test('waves has no bottom-panel mapping and no hook', () => {
  assert.doesNotMatch(H.read('waves/index.html'), /knobToDeck/);
});
