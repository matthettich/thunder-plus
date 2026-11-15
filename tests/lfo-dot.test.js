// LFO DOT (build 609, owner: "show what parameter the mod is connected to"): lfoNames / lfoMark put a dot class and the LFO's name on every element that
// carries data-lfo=<target id>; no browser: the two functions are cut out of index.html and run in a vm with stub elements.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = re => { const m = src.match(re); assert.ok(m, 'index.html: ' + re); return m[0]; };
const el = (id, title) => { const e = { dataset: { lfo: id }, title: title || '', classList: { s: new Set(), toggle(c, on) { on ? this.s.add(c) : this.s.delete(c); } }, removeAttribute(a) { if (a === 'title') e.title = ''; } }; return e; };
const mk = els => { const S = { perf: { lfo: {} } }; const ctx = vm.createContext({ S, document: { querySelectorAll: () => els }, Map, Array });
  vm.runInContext(cut(/function lfoNames\(\)[\s\S]*?\nfunction lfoMark\(root\)[\s\S]*?\n(?=const |function |\/\*)/), ctx); return { S, ctx, run: c => vm.runInContext(c, ctx) }; };
test('dot and the LFO name follow the target; removed target clears it; the old title stays', () => {
  const a = el('fx:0:1:2', 'DRIVE: drag'), b = el('pan:3'), c = el('vol:3'), W = mk([a, b, c]);
  W.S.perf.lfo[0] = [{ id: 'pan:3' }, { id: 'fx:0:1:2' }]; W.S.perf.lfo[3] = [{ id: 'fx:0:1:2' }];
  W.run('lfoMark()');
  assert.equal(a.title, 'DRIVE: drag · LFO 2, LFO 1'); assert.ok(a.classList.s.has('lfod'));
  assert.equal(b.title, 'LFO 1'); assert.ok(!c.classList.s.has('lfod')); assert.equal(c.title, '');
  W.S.perf.lfo[0] = [{ id: 'vol:3' }]; delete W.S.perf.lfo[3]; W.run('lfoMark()');
  assert.ok(!a.classList.s.has('lfod')); assert.equal(a.title, 'DRIVE: drag'); assert.ok(!b.classList.s.has('lfod')); assert.equal(c.title, 'LFO 1');
});
test('source: FX rows, FX bar labels and Mixer PAN / VOL / OUT carry data-lfo, and draw() marks', () => {
  assert.match(src, /lfo:tv === 'fx' && r\.lfo \? r\.lfo : null/);
  assert.match(src, /lb\.dataset\.lfo = li/);
  assert.match(src, /lfo:y === 1 \? 'pan:' \+ t : null/);
  assert.match(src, /lfo:k === MH - 1 \? 'vol:' \+ t : null/);
  assert.match(src, /lfo:'vol:' \+ MT, title:/);
  assert.match(src, /modScSync\(rebuilt\);[^\n]*\n\s*lfoMark\(\);/);
});
