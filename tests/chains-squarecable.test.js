// Right-angle cables (15 Nov): a dragged bend point snaps square to its neighbours within 8 px (Alt: no snap), and "Square up" makes every segment horizontal or vertical.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
for (const ed of ['chains', 'waves']){
  const src = H.read(ed + '/index.html');
  const cut = re => { const m = src.match(re); assert.ok(m, ed + ': not found ' + re); return m[0]; };
  const c = vm.createContext({ Math, Array });
  vm.runInContext(cut(/const SNAP_PX = 8;\n/) + cut(/function snapBendPt\([\s\S]*?\n\}\n/) + cut(/function squareUpPts\([\s\S]*?\n\}\n/), c);
  const square = (ends, pts) => { const all = [ends[0], ...pts, ends[1]]; return all.slice(1).every((q, i) => q[0] === all[i][0] || q[1] === all[i][1]); };
  test(ed + ': a dragged bend snaps to the x or y of the points either side of it, within 8 px', () => {
    const ends = [[0, 0], [300, 100]], pts = [[100, 50], [200, 50]];
    assert.deepEqual([...c.snapBendPt(ends, pts, 0, [103, 46], 8)], [103, 50], 'y of the next bend (50) is within 8; x is far from both neighbours');
    assert.deepEqual([...c.snapBendPt(ends, pts, 0, [5, 7], 8)], [0, 0], 'near the source jack on both axes');
    assert.deepEqual([...c.snapBendPt(ends, pts, 0, [120, 70], 8)], [120, 70], 'nothing near: it stays where it was put');
    assert.deepEqual([...c.snapBendPt(ends, pts, 1, [205, 98], 8)], [205, 100], 'the last bend takes the target jack\'s y (98 is 2 away)');
  });
  test(ed + ': the snap tolerance is in screen pixels (the caller divides by the zoom)', () => {
    assert.deepEqual([...c.snapBendPt([[0, 0], [100, 0]], [[50, 20]], 0, [55, 20], 4)], [55, 20], '5 away with a 4 tolerance: no snap');
    assert.deepEqual([...c.snapBendPt([[0, 0], [100, 0]], [[50, 20]], 0, [55, 3], 4)], [55, 0], 'y within 4 of both jacks');
  });
  test(ed + ': Square up makes every segment horizontal or vertical', () => {
    for (const [ends, pts] of [[[[0, 0], [300, 120]], [[90, 10], [160, 90]]], [[[0, 0], [300, 120]], []], [[[0, 0], [300, 120]], [[150, 60]]], [[[10, 10], [10, 90]], [[12, 40]]], [[[0, 0], [100, 0]], []], [[[0, 0], [300, 120]], [[100, 3], [101, 100], [300, 118]]]]){
      const out = c.squareUpPts(ends, pts).map(p => [...p]); assert.ok(square(ends, out), JSON.stringify([ends, pts, out]));
    }
    assert.deepEqual(JSON.parse(JSON.stringify(c.squareUpPts([[0, 0], [300, 120]], []))), [[150, 0], [150, 120]], 'no bends: a Z');
    assert.deepEqual(JSON.parse(JSON.stringify(c.squareUpPts([[0, 0], [100, 0]], []))), [], 'already straight: nothing added');
  });
  test(ed + ': the menus offer it and a bend drag with Alt skips the snap', () => {
    assert.match(src, /its\.splice\(its\.length - 1, 0, \['Square up', \(\) => squareUpCable\(cab\)\]\);/);
    assert.match(src, /\['Square up this cable', \(\) => squareUpCable\(c\)\]/);
    assert.match(src, /if \(!e\.altKey\)\{ const en = cableEndPts\(c\); if \(en\) P = snapBendPt\(en, c\.pts, i, P, SNAP_PX \/ view\(\)\.zoom\); \}/);
  });
}
