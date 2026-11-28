// Song sections lane (owner, 27 Nov: "impossible to add more than one"): the + shows on the cursor's row unless that row already
// starts a section. It used to show only on rows in no section, and a section runs to the next one or the song's end, so after the
// first one every row below it was "in a section" and no + ever showed again.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers'), src = H.read('index.html');
const line = a => { const i = src.indexOf(a); assert.ok(i >= 0, 'moved: ' + a); return src.slice(i, src.indexOf('\n', i)); };
const rule = line('const L = sxSecs(), cy = cs.song.y, cur = songSecAt(cy), add = ');
function addAt(secs, cy, top = 0, VR = 32){
  const c = vm.createContext({ S: { secs }, cs: { song: { y: cy } }, top, VR });
  vm.runInContext(line('const sxSecs = () =>') + '\n' + line('function songSecAt(y){') + '\n' + rule.replace(/^\s*const /, 'var ').replace(/, st = arrGrid[\s\S]*$/, ';'), c);
  return c.add;
}
test('the + shows inside a section (it splits it there)', () => assert.equal(addAt([{ y: 4, n: 'VERSE' }], 10), 10));
test('a second, third… section can be started below the first', () => { assert.equal(addAt([{ y: 4, n: 'A' }, { y: 12, n: 'B' }], 20), 20); });
test('not on a row that already starts a section', () => assert.equal(addAt([{ y: 4, n: 'VERSE' }], 4), -1));
test('rows before any section, and a song with none', () => { assert.equal(addAt([{ y: 4, n: 'VERSE' }], 1), 1); assert.equal(addAt([], 0), 0); });
test('not when the cursor row is off screen', () => assert.equal(addAt([], 40, 0, 32), -1));
test('the lane redraws on every scroll step of the grid, not only on a new top row', () => {
  assert.match(src, /arrGrid\.addEventListener\('scroll', \(\) => \{ if \(!raf\) raf = requestAnimationFrame\(\(\) => \{ raf = 0; if \(sxLane && !sxLane\.hidden\) sxLaneDraw\(\);/);
  assert.match(line('const gy0 = g.top - a.top'), /arrRowTop\(y\) - st/, 'a row\'s place includes the grid\'s scroll position');
});
