// UNDO KEEPS ARRANGE'S VIEW (build 609, owner: "pressing undo while a section of arrange was selected kept jumping the arrange screen to the selected area"):
// undoStep's redraw let arrKeepRow bring the selection's cursor row back into view. Now the view stays, unless every row the undo changed is off screen.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
test('undoStep: the redraw runs with arrKeepOff set (1 keep the scroll, 2 the top it chose for an off-screen change)', () => {
  const i = src.indexOf('function undoStep('), j = src.indexOf('setTimeout(kitUndoReset, 0);', i); assert.ok(i > 0 && j > i);
  const f = src.slice(i, j);
  assert.match(f, /const rowsWas = S\.song\.map\(r => r\.join\(\)\);/); assert.match(f, /arrKeepOff = 1;/); assert.match(f, /ch\.some\(y => y >= top && y < top \+ VR\)/);
  assert.match(f, /try \{ draw\(\); \} finally \{ arrKeepOff = 0; \}/);
});
test('arrKeepRow: skips the keep-in-view step while arrKeepOff is set, and reads the real scroll unless the top was chosen (2)', () => {
  const m = src.match(/function arrKeepRow\(c, VR\)\{[\s\S]*?\n\}\n/); assert.ok(m);
  assert.match(m[0], /arrKeepOff !== 2 && arrLookY !== c\.y && rh > 0/); assert.match(m[0], /if \(arrKeepOff\) return;\n  const top = c\.top \|\| 0;/);
  assert.ok(src.indexOf('let arrKeepOff') < src.indexOf('function undoStep('), 'declared before use');
});
test('behaviour: a vm of arrKeepRow, the cursor far below the view: normal draw pages to it, an undo redraw leaves the view', () => {
  const vm = require('vm'), m = src.match(/function arrKeepRow\(c, VR\)\{[\s\S]*?\n\}\n/)[0];
  const c = { arrKeepOff: 0, arrRowH: () => 20, arrGrid: { offsetParent: 1, scrollTop: 400 }, arrRowAt: st => st / 20, arrIgnore: -1, arrRows: () => 14, Math, clamp: (v, a, b) => Math.max(a, Math.min(b, v)), performance: { now: () => 1e9 }, arrTouchT: -1e9 };
  vm.createContext(c); vm.runInContext('var arrKeepOff = 0, arrLastY = -1, arrLookY = -1, arrAhead = 16, ARR_KEEP = 4;' + m + '\nthis.f = arrKeepRow;', c);
  const cur = { y: 90, top: 5 }; c.f(cur, 14); assert.equal(cur.top, 84, 'a normal draw brings the cursor row into view');
  const cur2 = { y: 90, top: 5 }; vm.runInContext('arrKeepOff = 1', c); c.f(cur2, 14); assert.equal(cur2.top, 20, 'the undo redraw keeps the scroll the user left (row 20)');
});
