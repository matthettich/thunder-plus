// LOOP SEL survives a trip into a phrase (build 675, 24 Nov): a selection made in the phrase must not replace Arrange's armed rows when you leave the phrase.
const test = require('node:test'); const assert = require('node:assert'); const H = require('./helpers.js');
const src = H.read('index.html');
const i = src.indexOf('function go(v){'); assert.ok(i >= 0, 'go');
const go = src.slice(i, src.indexOf('\n}', i));
test('go() does not re-arm from another screen\'s selection over Arrange\'s armed rows', () => {
  assert.match(go, /if \(loopSel && !tp\.on && !\(tv !== 'song' && loopArm && loopArm\.v === 'song'\)\) loopFor\(tv\);/);
});
test('Arrange\'s own selection is still armed on leaving the Song screen, and loopFor / syncLoop are unchanged', () => {
  assert.ok(go.includes("loopFor(tv)"));
  assert.match(src, /function loopFor\(view\)\{\n  if \(!loopSel \|\| !GRIDV\.has\(view\)\) return null;/);
  assert.match(src, /if \(!\(lsLock && \(tp\.on \|\| warmTok\)\) && tv === tp\.loopV && selA/);
});
