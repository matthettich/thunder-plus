// MORPH, editor side (14 Nov 2026, module queue; …chains only): the definition has the engine's params, ports and latency,
// plain-English labels, a Help paragraph that says what the user needs, and its places in the module list.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') }, WK = H.read('chains/chains-worklet.js');
function literal(src, i) {
  const open = src[i], close = open === '{' ? '}' : ']'; let d = 0, q = null;
  for (let j = i; j < src.length; j++) {
    const c = src[j];
    if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; }
    if (c === "'" || c === '"' || c === '`') q = c; else if (c === open) d++; else if (c === close && --d === 0) return src.slice(i, j + 1);
  }
  throw new Error('unbalanced');
}
const ctx = { opts: names => names.map((t, i) => [i, t]), pct: v => Math.round(v * 100) + '%', db: v => (v > 0 ? '+' : '') + v.toFixed(1) + ' dB' };
const ev = s => vm.runInNewContext('(' + s + ')', ctx);
const m = /\n  morph:\{ label:/.exec(ED.chains); assert.ok(m, 'editor entry for morph');
const e = literal(ED.chains, m.index + 1 + '  morph:'.length);
const w = WK.indexOf("type:'morph', scope:'mono'"), wp = ev(literal(WK, WK.indexOf('{', WK.indexOf('params:', w))));

test('Morph editor def matches the engine: params, ports, latency', () => {
  assert.equal(JSON.stringify(ev(literal(e, e.indexOf('params:') + 7))), JSON.stringify(wp));
  assert.deepEqual(Object.keys(ev(literal(e, e.indexOf('inputs:') + 7))), ['in', 'inR', 'inB', 'inBR']);
  assert.deepEqual(Object.keys(ev(literal(e, e.indexOf('outputs:') + 8))), ['out', 'outR']);
  assert.match(e, /latency:1024,/); assert.match(WK, /type:'morph', scope:'mono', cost:[\d.]+, latency:1024/);
  const ui = ev(literal(e, e.indexOf('ui:') + 3));
  assert.deepEqual(Object.fromEntries(Object.entries(ui).map(([k, v]) => [k, v.l])), { mode: 'Mode', morph: 'Morph', sharp: 'Sharpness', smooth: 'Smoothing', frame: 'Frame', freeze: 'Freeze', level: 'Level' });
  assert.equal(JSON.stringify(ui.mode.c), '[[0,"Convolve"],[1,"Shape"]]'); assert.equal(ui.freeze.c.length, 3); assert.equal(ui.level.f(24), '+24.0 dB');
  assert.equal(wp.level[1], -24); assert.equal(wp.level[2], 24);
});
test('Morph Help says what the user needs to know, with no em dashes', () => {
  const about = /about:'([^']+)'/.exec(e)[1];
  for (const w of ['In B', 'Convolve', 'Shape', 'Morph', 'Sharpness', 'Smoothing', 'Frame', 'Freeze', '21 ms', '48 kHz', 'quieter', 'Level up', '+24 dB']) assert.ok(about.includes(w), w);
  assert.ok(!/[—–]/.test(about));
});
test('Morph is in the chains list, hint, use line and Compact summary, and not in the shared module block of the worklet', () => {
  assert.match(/const PAL_LAYOUT = \[[\s\S]*?\n\];/.exec(ED.chains)[0], /'Granular and spectral'[^\n]*'morph'/);
  assert.match(ED.chains, /\['morph','Morph'\]/); assert.match(/const PAL_HINT = \{[^}]*\}/.exec(ED.chains)[0], /morph:'[^']+'/);
  assert.match(ED.chains, /\n  morph:'[^'\n]{30,}'/); assert.match(/const SUMMARY_KEYS = \{[\s\S]*?\n\};/.exec(ED.chains)[0], /morph:\['mode'/);
  assert.equal(/\n  morph:\{ label:/.test(ED.waves), false, 'no def in waves');
});
