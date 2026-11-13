// ARRANGE'S OWN STACKING CONTEXT (13 Nov 2026, docs/audits/2026-11-13-draw-pass.md, "What stalls Pattern view"): the block grid (.tagrid) is isolated, so its position:relative / z-index:1
// blocks (cursor, playing) no longer share the page's stacking context. Measured with the Arrange panel beside Pattern view, 4x CPU, ELECTRO playing: 54 to 70 long tasks per 10 s -> 10 to 16
// (Phrase 21 to 41 -> 1 to 8, Song 10 -> 0 to 2). The rule is a one-liner that a later "tidy-up" could drop, so it is pinned here.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
test('.tagrid keeps isolation:isolate (its blocks\' z-index stays inside the grid)', () => {
  const m = src.match(/\n\.tagrid\{[^\n]*\}/); assert.ok(m, 'the .tagrid rule moved');
  assert.match(m[0], /isolation:isolate/); assert.match(m[0], /overflow-y:auto/, 'and it is still the scroller');
});
test('the blocks that use z-index (cursor, playing) are still inside the grid, positioned', () => {
  assert.match(src, /\.tagrid b\.p\{outline:2px solid var\(--ar-pl-ring, var\(--ink\)\); outline-offset:-1px; position:relative; z-index:1\}/);
  assert.match(src, /\.tagrid b\.c\{outline:2px solid var\(--ar-cur\); outline-offset:0; position:relative; z-index:1\}/);
});
