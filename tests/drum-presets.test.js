// The drum presets added in build 615+ (moreDrumPresets2) and the kits made from them (MORE_KITS): every type has its minimum,
// names are unique and non-empty, every parameter is a finite number, every kit pad resolves, and a sample of the sounds is
// rendered and measured against the window of its type (tools/drum-measure.js: finite, not silent, level, length, spectrum).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const G = require('../tools/engine-golden'), M = require('../tools/drum-measure');

const MIN = { 'Kicks': 28, 'Snares': 28, 'Claps & snaps': 20, 'Rims & sticks': 14, 'Closed hats': 22, 'Open hats': 12, 'Cymbals': 12,
  'Toms': 22, 'Hand drums': 12, 'Shakers & tambourines': 12, 'Cowbells, blocks & metal': 22, 'IDM & glitch': 28 };
const E = G.engine(), list = E.newDrumPresets();   // [type, name, make]
const byType = new Map(); for (const [t, n] of list) (byType.get(t) || byType.set(t, []).get(t)).push(n);

test('every drum type has its minimum of new presets', () => {
  for (const t of Object.keys(MIN)) assert.ok((byType.get(t) || []).length >= MIN[t], t + ': ' + (byType.get(t) || []).length + ' < ' + MIN[t]);
  for (const t of byType.keys()) assert.ok(E.DRUM_TYPES.includes(t), 'unknown drum type ' + t);
  assert.ok(list.length >= 250, 'at least 250 new drum presets, got ' + list.length);
});

test('names are non-empty and unique (within the type, and across all new presets)', () => {
  const seen = new Set();
  for (const [t, n] of list) { assert.ok(typeof n === 'string' && n.trim().length > 1 && n.length <= 40, 'bad name ' + JSON.stringify(n)); assert.ok(!seen.has(n), 'duplicate name ' + n); seen.add(n); }
});

test('every parameter of every voice is finite and in range', () => {
  const bad = [], walk = (o, p, id) => { if (typeof o === 'number') { if (!Number.isFinite(o)) bad.push(id + ' ' + p); } else if (o && typeof o === 'object') for (const k of Object.keys(o)) walk(o[k], p + '.' + k, id); };
  for (const [t, n, make] of list) {
    const v = make(); walk(v, '', t + '|' + n);
    assert.ok(v.len >= 0.05 && v.len <= 12, n + ' len ' + v.len);
    if (typeof v.level === 'number') assert.ok(v.level >= -48 && v.level <= 48, n + ' level ' + v.level);
    for (const l of v.layers || []) { assert.ok(l.tune == null || (l.tune >= -48 && l.tune <= 48), n + ' tune ' + l.tune); for (const k of ['a', 'b']) if (typeof l[k] === 'number') assert.ok(l[k] >= 0 && l[k] <= 1, n + ' layer ' + k + ' ' + l[k]); if (typeof l.level === 'number') assert.ok(l.level >= 0 && l.level <= 4, n + ' layer level ' + l.level); }
  }
  assert.deepEqual(bad, []);
});

test('MORE_KITS: eight pads each, every pad resolves to a new preset of its type', () => {
  const h = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8'), a = h.indexOf('const MORE_KITS = '), b = h.indexOf('\n];', a);
  assert.ok(a > 0 && b > a, 'MORE_KITS not found');
  const kits = new Function('return ' + h.slice(a + 'const MORE_KITS = '.length, b + 3))();
  assert.ok(kits.length >= 8 && kits.length <= 14, 'kits: ' + kits.length);
  const names = new Set();
  for (const [name, pads] of kits) {
    assert.ok(name && !names.has(name), 'kit name ' + name); names.add(name);
    assert.equal(pads.length, 8, name + ' pads');
    for (const [t, n] of pads) assert.ok(list.some(e => e[0] === t && e[1] === n), name + ': no ' + t + ' / ' + n);
  }
  assert.ok(h.includes('...MORE_KITS.map('), 'drumKits() adds the new kits');
});

test('measured windows: a sample of the new sounds (every 5th, and the first of each type) is finite, audible and in its window', () => {
  const firsts = new Set(), probs = [];
  list.forEach(([t, n, make], i) => {
    if (i % 5 && firsts.has(t)) return; firsts.add(t);
    const r = M.renderEntry({ type: t, name: n, make }), m = M.measure(r.data), v = M.violations(t, m, r.v.engine || 'layers');
    if (v.length) probs.push(t + ' / ' + n + ': ' + v.join('; '));
  });
  assert.deepEqual(probs, []);
});
