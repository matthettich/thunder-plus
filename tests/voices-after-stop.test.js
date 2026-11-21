// VOICES AFTER STOP (13 Nov, owner's Balanced report: "1 s after STOP … voices 1", then 2). A live engine's note has no end of its own, so it stayed in
// the player's `alive` set until that engine's next note and the report counted it for good. stopAll now forgets a live voice once it has let it go.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
test('stopAll lets every voice go and forgets the live-engine ones', () => {
  assert.match(src, /function stopAll\(at\)\{ resetSeq\(at\); for \(const v of alive\)\{ kill\(v, at\); if \(v\.bass\) alive\.delete\(v\); \} for \(const T of vs\) T\.length = 0;/);
});
test('the logic, on a stand-in: sample voices leave by their own onended, live ones at stopAll', () => {
  const alive = new Set(), killed = [], kill = v => killed.push(v), a = { bass: {} }, b = {}, c = { bass: {} };
  alive.add(a); alive.add(b); alive.add(c);
  for (const v of alive){ kill(v, 0); if (v.bass) alive.delete(v); }
  assert.equal(killed.length, 3); assert.deepEqual([...alive], [b]);
});
