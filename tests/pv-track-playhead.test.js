// PER-TRACK PLAYHEAD IN PATTERN VIEW (12 Nov, owner: "Hop command in pattern view cannot show playhead for that pattern").
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const fn = src.match(/var pvLay = null[^\n]*\nfunction pvPlayMark\(P\)\{[\s\S]*?\n\}\n/); assert.ok(fn, 'pvPlayMark not found');
function rig() {
  const mk = () => { const s = new Set(); return { classList: { add: c => s.add(c), remove: c => s.delete(c), has: c => s.has(c) } }; };
  const lines = Array.from({ length: 3 + 16 }, (_, i) => { const sp = { 0: [mk(), mk()], 1: [mk(), mk()], 2: [mk()] }; return { sp, querySelectorAll: sel => sp[+sel.replace('.pvt', '')] || [] }; });
  const c = { grid: { querySelectorAll: () => lines }, lines }; vm.createContext(c); vm.runInContext(fn[0] + '\nthis.pvPlayMark = pvPlayMark; this.set = l => { pvLay = l; }; this.size = () => pvLit.size;', c); return c;
}
const lit = (c, t, r) => c.lines[3 + r].sp[t].some(e => e.classList.has('pvpl'));
const lay = { top: 0, n: 16, cols: [{ t: 0, ph: 5, len: 16 }, { t: 1, ph: 6, len: 16 }] };
test('a track lights its own row when its column shows the phrase it plays, wherever the cursor is', () => {
  const c = rig(); c.set(lay); c.pvPlayMark([{ phrase: 5, step: 10 }, { phrase: 6, step: 2 }]);
  assert.ok(lit(c, 0, 10) && lit(c, 1, 2)); assert.ok(!lit(c, 0, 2) && !lit(c, 1, 10));
});
test('a track playing another phrase than the one its column shows is not marked; nor is a row outside the view or past the phrase end', () => {
  const c = rig(); c.set({ top: 4, n: 8, cols: [{ t: 0, ph: 5, len: 16 }, { t: 1, ph: 6, len: 6 }] }); c.pvPlayMark([{ phrase: 9, step: 6 }, { phrase: 6, step: 7 }]); assert.equal(c.size(), 0);
  c.pvPlayMark([{ phrase: 5, step: 2 }, null]); assert.equal(c.size(), 0);
});
test('a HOP moves the band with the track; STOP (P null) clears every band; an unchanged step touches nothing', () => {
  const c = rig(); c.set(lay); c.pvPlayMark([{ phrase: 5, step: 3 }]); assert.ok(lit(c, 0, 3));
  c.pvPlayMark([{ phrase: 5, step: 10 }]); assert.ok(!lit(c, 0, 3) && lit(c, 0, 10));
  const before = c.lines[3 + 10].sp[0][0]; let n = 0; const add = before.classList.add; before.classList.add = x => { n++; add(x); }; c.pvPlayMark([{ phrase: 5, step: 10 }]); assert.equal(n, 0);
  c.pvPlayMark(null); assert.ok(!lit(c, 0, 10)); assert.equal(c.size(), 0);
});
test('wiring: spans carry pvtN, draw marks in place after the grid is built (only while playing), no rebuild or layout read', () => {
  assert.match(src, /const tint = ' pvt' \+ g\.t \+/);
  assert.match(src, /pvPlayMark\(tp\.on \? tp\.pos : null\); else if \(pvLit\.size\) pvPlayMark\(null\);/);
  const body = fn[0]; assert.doesNotMatch(body, /innerHTML|offsetWidth|offsetHeight|getBoundingClientRect|clientHeight/);
});
test('look: ink band only (the skin\'s own --ts-ink, so Low power stays black and grey), no animation, weaker than the cursor', () => {
  const m = src.match(/\.tgrid\.pvon span\.pvpl:not\(\.cur\):not\(\.sel\)\{([^}]*)\}/); assert.ok(m);
  assert.match(m[1], /var\(--ts-ink\) 24%/); assert.doesNotMatch(m[1], /animation|transition|#[0-9a-f]{3,6}|blur/i);
});
