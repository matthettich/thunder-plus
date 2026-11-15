// SYNTH SLIDER ENTRY (14 Nov, owner): double-click types a number into a Synth page slider; reset is Alt-click and right-click > Reset to
// default; a clicked slider takes the arrow keys (1% of the travel; Shift: a tenth of that) and keeps them from the tracker / pad list until Escape lets go.
// The code under test is cut out of index.html and run in a vm with a tiny fake DOM (no browser), as the other UI tests do.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, 'moved: ' + a); return src.slice(i, j); };

class El {
  constructor(tag) { this.tag = tag; this.L = {}; this.children = []; this.style = {}; this.attrs = {}; this.className = ''; this.value = ''; this.min = ''; this.max = ''; this.step = ''; this.textContent = ''; this.isConnected = true; this.focused = false;
    this.classList = { contains: c => this.className.split(' ').includes(c), add: c => { this.className += ' ' + c; } }; }
  get firstChild() { return null; } get lastChild() { return null; }
  addEventListener(t, f) { (this.L[t] = this.L[t] || []).push(f); }
  fire(t, e = {}) { e = Object.assign({ target: this, stopPropagation() { this.stopped = true; }, preventDefault() { this.prevented = true; }, altKey: false, shiftKey: false, ctrlKey: false, metaKey: false }, e); (this.L[t] || []).forEach(f => f(e)); return e; }
  append(...c) { this.children.push(...c); c.forEach(x => { if (x) x.parent = this; }); } replaceChildren(...c) { this.children = c; c.forEach(x => { x.parent = this; }); this.textContent = ''; }
  insertBefore(c) { this.children.push(c); }
  querySelector(sel) { return sel === 'input' ? this.children.find(c => c.tag === 'input') || null : null; }
  setAttribute(k, v) { this.attrs[k] = v; } getAttribute(k) { return this.attrs[k]; } hasAttribute(k) { return k in this.attrs; }
  focus() { this.focused = true; } blur() { this.focused = false; this.fire('blur'); } select() { this.selected = true; }
  matches(s) { return s === 'input[type=range]' && this.tag === 'input' && this.type === 'range'; }
  closest(s) { return this.matches(s) ? this : null; }
  dispatchEvent(ev) { return this.fire(ev.type, ev); }
}
function world(def = { l: 'Cutoff', min: 20, max: 20000, d: 1000, c: 'log', f: v => Math.round(v) + ' Hz' }, extra = '') {
  const obj = { f: 1000 }, log = { changed: 0, menu: null, focusKeys: [] };
  const docL = {};
  const c = { Math, JSON, String, Number, Array, Object, Event: class { constructor(t) { this.type = t; } }, MouseEvent: class { constructor(t) { this.type = t; } },
    console, parseFloat, isNaN, performance: { now: () => 0 }, clearTimeout() {}, setTimeout: f => 0, MODK: 'Ctrl', MACK: false,
    el: (tag, cls, txt) => { const e = new El(tag); if (cls) e.className = cls; if (txt != null) e.textContent = txt; return e; },
    setText: (e, s) => { e.textContent = String(s); }, pct: v => Math.round(v * 100) + '%', clamp: (v, a, b) => Math.max(a, Math.min(b, v)),
    changed: () => { log.changed++; }, cur: 0, rows: [], sxMenu: (x, y, t, items) => { log.menu = items; }, rowLabel: () => '', setLearn() {}, midi: {}, renderPads() {}, ccBadges() {}, midiStatus() {},
    document: { addEventListener: (t, f) => { (docL[t] = docL[t] || []).push(f); }, createElement: t => new El(t), activeElement: null },
    ctxPress: e => e.button === 2, resetClick: () => false };
  vm.createContext(c);
  const code = [cut('const toPos = (d, v)', '/* ---------- state ---------- */'), cut('// rowParse(d, txt)', "const rows = [];"), cut('const rows = [];', 'function makeSeg('),
    cut('const sliderOk =', '/* ONE RULE FOR VALUE CONTROLS'), cut('function sliderSet(r, v){', '/* WHEEL (27 Oct'), cut('/* SLIDER KEYS', '/* WHEEL (27 Oct')].join('\n');
  vm.runInContext(code + '\nthis.api = {makeRow, rowParse, snapDef, fromPos, toPos, sliderNudge, sliderKey, sliderSet, rowTypeBox};', c);
  const row = c.api.makeRow(def, 'f', () => obj, { idp: 't', on: () => { log.changed++; } });
  return { c, row, obj, log, docL };
}
const R = (def, txt) => world(def).c.api.rowParse(def, txt);
const HZ = { l: 'Cutoff', min: 20, max: 20000, d: 1000, c: 'log' }, DB = { l: 'Level', min: -60, max: 6, d: 0 }, PC = { l: 'Mix', min: 0, max: 1, d: 0.5 }, INT = { l: 'Octave', min: -4, max: 4, d: 0, c: 'int' };

test('typed values: the unit shown or the bare number, both work', () => {
  assert.equal(R(HZ, '440'), 440); assert.equal(R(HZ, '440 Hz'), 440); assert.equal(R(HZ, '440hz'), 440); assert.equal(R(HZ, '2 kHz'), 2000); assert.equal(R(HZ, '2,5 kHz'), 2500);
  assert.equal(R(DB, '-6.0 dB'), -6); assert.equal(R(DB, '−6'), -6); assert.equal(R(DB, '+3 dB'), 3);
  assert.equal(R(PC, '35%'), 0.35); assert.equal(R(PC, '35'), 0.35); assert.equal(R(PC, '0.35'), 0.35);
  assert.equal(R({ l: 'Time', min: 0, max: 5, d: 0.1 }, '250 ms'), 0.25);
  assert.equal(R(HZ, 'abc'), null); assert.equal(R(HZ, ''), null);
});
test('typed values are clamped to the range and snapped to the step a drag lands on', () => {
  assert.equal(R(HZ, '5'), 20); assert.equal(R(HZ, '99999'), 20000); assert.equal(R(DB, '-200'), -60); assert.equal(R(DB, '12 dB'), 6); assert.equal(R(PC, '250%'), 1);
  assert.equal(R(INT, '2.6'), 3, 'a whole-number row rounds'); assert.equal(R(INT, '9'), 4); assert.equal(R(INT, '-9'), -4);
  assert.equal(R(HZ, '440.4 Hz'), 440, 'the log curve lands on whole Hz above 10'); assert.equal(R(HZ, '55.5'), 56);
  assert.equal(R({ l: 'x', min: 0, max: 4, d: 0, c: 'half' }, '1.3'), 1.5);
});
test('number entry opens on double-click of the slider, its name and its value; it is pre-filled and selected; Enter applies through the row\'s own handler', () => {
  const w = world(), { row, obj, log } = w, d = row.def; row.refresh();
  assert.equal(row.out.textContent, '1000 Hz');
  row.inp.fire('dblclick'); assert.equal(log.changed, 0, 'a double-click no longer resets or changes anything');
  const box = row.out.children[0]; assert.ok(box && box.tag === 'input', 'the field replaces the readout'); assert.equal(box.value, '1000 Hz'); assert.ok(box.selected && box.focused);
  box.value = '2 kHz'; const ev = box.fire('keydown', { key: 'Enter' }); assert.ok(ev.stopped, 'keys typed there never reach the tracker');
  assert.equal(obj.f, 2000); assert.equal(log.changed, 1, 'one change = the same on() call a drag makes (undo, MOD, learn hang off it)'); assert.equal(row.out.textContent, '2000 Hz');
  assert.ok(row.inp.focused, 'the slider keeps the keys afterwards');
  for (const t of ['lab', 'out']) { const x = world(); x.row.refresh(); x.row[t].fire('dblclick'); assert.ok(x.row.out.children[0], t + ' double-click opens it too'); assert.equal(x.obj.f, 1000); }
});
test('Escape cancels; leaving the field unchanged cancels (no change, no undo step); an unparseable entry is dropped', () => {
  let w = world(); w.row.refresh(); w.row.inp.fire('dblclick'); let box = w.row.out.children[0]; box.value = '3000'; box.fire('keydown', { key: 'Escape' });
  assert.equal(w.obj.f, 1000); assert.equal(w.log.changed, 0); assert.equal(w.row.out.textContent, '1000 Hz');
  w = world(); w.row.refresh(); w.row.inp.fire('dblclick'); box = w.row.out.children[0]; box.blur(); assert.equal(w.log.changed, 0, 'blur without a change');
  w = world(); w.row.refresh(); w.row.inp.fire('dblclick'); box = w.row.out.children[0]; box.value = 'xyz'; box.blur(); assert.equal(w.log.changed, 0); assert.equal(w.obj.f, 1000);
  w = world(); w.row.refresh(); w.row.inp.fire('dblclick'); box = w.row.out.children[0]; box.value = '1500'; box.blur(); assert.equal(w.obj.f, 1500, 'blur with a new value applies it (as before)');
});
test('reset: a double-click no longer resets; the right-click menu\'s Reset to default and Alt-click do', () => {
  const w = world(); const { row, obj, log } = w; obj.f = 4000; row.refresh();
  row.inp.fire('dblclick'); row.lab.fire('dblclick'); row.out.fire('dblclick'); assert.equal(obj.f, 4000);
  row.r.fire('contextmenu', { clientX: 1, clientY: 1 }); const it = log.menu.find(x => x && x.l === 'Reset to default'); assert.ok(it && !it.off);
  assert.ok(log.menu.find(x => x && x.l === 'Type a value…'));
  it.f(); assert.equal(obj.f, 1000); assert.equal(log.changed, 1);
  /* Alt-click: the document's pointerdown handler on a slider */
  const x = world(); x.obj.f = 4000;
  const blk = cut("document.addEventListener('pointerdown', e => {\n  const r = sliderAt(e);", "/* ===== FILE DROP");
  const ctx = Object.assign({}, x.c, { sldReset: r => r._reset(), sliderAt: e => e.target, sliderOk: () => true, sliderTip() {}, dragMul: () => 1, addEventListener() {}, removeEventListener() {} });
  vm.createContext(ctx); vm.runInContext(blk, ctx);
  const h = x.docL.pointerdown[0]; x.row.inp._reset = x.row.inp._reset;
  h({ target: x.row.inp, button: 0, pointerType: 'mouse', altKey: true, preventDefault() {}, stopPropagation() {}, clientX: 0, clientY: 0, pointerId: 1 });
  assert.equal(x.obj.f, 1000, 'Alt-click resets'); assert.equal(x.log.changed, 1);
});
test('two-state rows (nothing to type) keep double-click on the name as reset', () => {
  const w = world({ l: 'Reverse', min: 0, max: 1, d: 0, c: 'int', f: v => v ? 'on' : 'off' }); w.obj.f = 1; w.row.r.classList.add('two'); w.row.lab.fire('dblclick'); assert.equal(w.obj.f, 0);
});
test('arrows on a focused slider: 1% of the travel, Shift a tenth of that; whole-number rows still change by one; Up / Down match; bounds hold', () => {
  const w = world(); const { row, obj, log } = w; row.refresh(); const p0 = +row.inp.value;
  let e = row.inp.fire('keydown', { key: 'ArrowRight' }); assert.ok(e.prevented && e.stopped, 'the arrow stays with the slider');
  assert.equal(+row.inp.value, p0 + 10); assert.equal(log.changed, 1, 'through the same input handler a drag uses');
  row.inp.fire('keydown', { key: 'ArrowLeft', shiftKey: true }); assert.equal(+row.inp.value, p0 + 10 - 1);
  row.inp.fire('keydown', { key: 'ArrowUp' }); assert.equal(+row.inp.value, p0 + 9 + 10);
  const wi = world(INT); wi.obj.f = 0; wi.row.refresh(); wi.row.inp.fire('keydown', { key: 'ArrowRight' }); assert.equal(wi.obj.f, 1, 'a whole-number row moves by one, not by a no-op slider step');
  wi.row.inp.fire('keydown', { key: 'ArrowLeft', shiftKey: true }); assert.equal(wi.obj.f, 0, 'Shift on a whole-number row: at least one unit back');
  const wb = world(PC); wb.obj.f = 1; wb.row.refresh(); wb.row.inp.fire('keydown', { key: 'ArrowRight' }); assert.equal(wb.obj.f, 1);
});
test('Escape on a focused slider lets go of it; Ctrl / Alt arrows are not taken', () => {
  const w = world(); w.row.inp.focused = true; const e = w.row.inp.fire('keydown', { key: 'Escape' }); assert.equal(w.row.inp.focused, false); assert.ok(e.stopped);
  const p = +w.row.inp.value; const e2 = w.row.inp.fire('keydown', { key: 'ArrowRight', ctrlKey: true }); assert.ok(!e2.prevented); assert.equal(+w.row.inp.value, p);
});
test('source pins: arrows are stopped on the slider and skipped by the Synth page\'s pad stepping; focus ring on a clicked slider; docs say the new gestures', () => {
  assert.match(src, /inp\.addEventListener\('keydown', e => sliderKey\(e, inp\)\)/);
  assert.match(src, /if \(!e\.target\.matches\('input\[type=range\]'\)\)\{ select\(\(cur \+ step/, 'pad stepping leaves a focused slider alone');
  assert.match(src, /input\[type=range\]:focus\{outline:2px solid var\(--acc\)/);
  assert.match(src, /inp\.addEventListener\('dblclick', dbl\)/); assert.doesNotMatch(src, /inp\.addEventListener\('dblclick', reset\)/);
  assert.doesNotMatch(src.slice(0, src.indexOf('const CHANGES')), /double-click or Alt-click to reset/);
  const tracker = cut("document.addEventListener('keydown', e0 => {\n  const e = noAltGr(e0);", 'if (fileKeys(e))');
  assert.match(tracker, /typing\(e\)/);
  const rd = H.read('README.md'); assert.doesNotMatch(rd, /Double-click any slider \(or its name\) to put it back/);
});
