// FlexCore (patterns 1..511) and StepGateCore (0..15) against a COMMITTED reference (tests/fixtures/flex-core-golden.json), for
// index.html and for the generated copies in chains/ and waves/. No git history is needed, so this runs in CI (the old test in
// worklet-memory.test.js compared with build 562 from `git show`, which skips in a shallow checkout and went stale at build 647).
// An intended change to these cores: node tools/make-flex-golden.js, review the diff, say why in HANDOVER.
'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const H = require('./helpers'), L = require('./flex-golden-lib');
const G = require('./fixtures/flex-core-golden.json');

for (const f of ['index.html', 'chains/chains-worklet.js', 'waves/waves-worklet.js']) {
  test('FlexCore (all 511 patterns) and StepGateCore (all 16) match the committed golden: ' + f, () => {
    const C = L.cores(H.read(f)), bad = []; let sounding = 0;
    assert.equal(Object.keys(G.flex).length, 511); assert.equal(Object.keys(G.gate).length, 16);
    for (let k = 1; k < 512; k++) { const o = L.render(C, 'flex', k); if (L.hash(o) !== G.flex[k]) bad.push('flex ' + k); if (o.some(a => a.some(v => v !== 0))) sounding++; }
    for (let k = 0; k < 16; k++) if (L.hash(L.render(C, 'gate', k)) !== G.gate[k]) bad.push('step gate ' + k);
    assert.deepEqual(bad, [], bad.length + ' cases differ from tests/fixtures/flex-core-golden.json (intended? node tools/make-flex-golden.js)');
    assert.ok(sounding > 500, 'the patterns must sound (' + sounding + ')');
  });
}
