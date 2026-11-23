// LP.SEL lock (23 Nov, build 667): a loop LP.SEL starts keeps its rows while the selection moves; LP.SEL again moves it; STOP releases it.
const test = require('node:test'); const assert = require('node:assert'); const H = require('./helpers.js');
const src = H.read('index.html');
const fn = name => { const i = src.indexOf('function ' + name + '('); assert.ok(i >= 0, name); return src.slice(i, src.indexOf('\n}', i)); };
test('lpSelPlay locks the selected rows and moves the lock on a new selection', () => {
  const f = fn('lpSelPlay');
  assert.match(f, /loopArm = \{v:'song', y0:r\.y0, y1:r\.y1\}; playCtx\(\); if \(tp\.on \|\| warmTok\)\{ lsLock = true;/);
  assert.match(f, /if \(lsLock && lsW && tv === 'song'\)/);
  assert.match(f, /tp\.seq\.setLoop\(loopArm\)/);
  assert.ok(f.indexOf('tStopUser()') > f.indexOf('lsLock && lsW'), 'otherwise it still stops');
});
test('STOP releases the lock', () => { assert.match(fn('lpSelRestore'), /^function lpSelRestore\(\)\{ lsLock = false;/); });
test('syncLoop and loopFor do not re-arm from the selection while locked', () => {
  assert.match(fn('syncLoop'), /if \(!lsLock && tv === tp\.loopV && selA/);
  assert.match(fn('loopFor'), /if \(!lsLock && selA && selA\.v === view/);
});
test('LOOP SEL alone still follows the selection (lsLock only set by LP.SEL)', () => {
  const sets = src.match(/lsLock = true/g) || []; assert.strictEqual(sets.length, 1);
});
