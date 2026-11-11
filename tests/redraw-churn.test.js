// REDRAW CHURN (10 Nov soak): the redraws that run while a song plays must not assign text that would not change anything
// (every textContent assignment replaces the node's text node: garbage for the GC, worst on a phone). setText is cut out of
// index.html and run against a small fake node that counts assignments; source guards check that the per-redraw readouts go
// through it. A browser measurement of the same thing: node tests/sweep/redraw-churn.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function setText\(e, s\)\{[^\n]*\n/);
assert.ok(m, 'could not find setText: the code moved, this test needs updating');
const setText = vm.runInContext('(' + m[0].replace(/^function setText/, 'function') + ')', vm.createContext({ String }));

// A node with at most text children, enough like the DOM for setText: a textContent setter that replaces the children and counts.
function node(...kids) {
  const n = { writes: 0, childNodes: kids.map(k => typeof k === 'string' ? { nodeType: 3, data: k } : k),
    get firstChild() { return this.childNodes[0] || null; }, get lastChild() { return this.childNodes[this.childNodes.length - 1] || null; },
    get textContent() { return this.childNodes.map(c => c.nodeType === 3 ? c.data : '').join(''); },
    set textContent(v) { this.writes++; this.childNodes = v === '' ? [] : [{ nodeType: 3, data: String(v) }]; } };
  return n;
}
test('setText: same text writes nothing, new text writes once', () => {
  const e = node('120 BPM'); setText(e, '120 BPM'); setText(e, '120 BPM'); assert.equal(e.writes, 0);
  setText(e, '121 BPM'); assert.equal(e.writes, 1); assert.equal(e.textContent, '121 BPM'); setText(e, '121 BPM'); assert.equal(e.writes, 1);
});
test('setText: numbers and empty strings', () => {
  const e = node('4'); setText(e, 4); assert.equal(e.writes, 0); setText(e, 5); assert.equal(e.writes, 1);
  const z = node(); setText(z, ''); assert.equal(z.writes, 0, 'empty stays empty'); setText(z, 'x'); assert.equal(z.writes, 1); setText(z, ''); assert.equal(z.writes, 2); assert.equal(z.firstChild, null);
});
test('setText compares the node, not a cache: a write from elsewhere is noticed', () => {
  const e = node('A'); setText(e, 'A'); e.textContent = 'B'; e.writes = 0;   // some other writer changed it
  setText(e, 'A'); assert.equal(e.writes, 1); assert.equal(e.textContent, 'A');
});
test('setText overwrites a node that holds something other than one text node (a value box\'s input)', () => {
  const box = node({ nodeType: 1, tagName: 'INPUT' }); setText(box, ''); assert.equal(box.writes, 1, 'an input child is replaced even by ""');
  const two = node('a', 'b'); setText(two, 'ab'); assert.equal(two.writes, 1, 'two text nodes: rewritten as one, as before');
});

const idx = src.indexOf('function draw(){');
const drawTxt = src.slice(idx, src.indexOf('\n}\n', idx));
const guarded = [
  [drawTxt, /\$\('tTitle'\)/, 'tTitle'], [drawTxt, /\$\('tInfo'\)/, 'tInfo'], [drawTxt, /\$\('tState'\)/, 'tState'],
  [drawTxt, /setText\(\$\('tBpm'\)/, 'tBpm'], [drawTxt, /setText\(\$\('tOctN'\)/, 'tOctN'], [drawTxt, /setText\(n, loopPat/, 'tLoopN'], [drawTxt, /setText\(n, loopSel/, 'tLoopSelN'],
  [drawTxt, /setText\(sb, cmdSongLab/, 'the SONG command button'],
];
for (const [t, re, what] of guarded) test('draw(): ' + what + ' is not written when unchanged', () => assert.match(t, re));
test('draw(): tTitle, tInfo and tState still compare before they write (build 562)', () => {
  assert.match(drawTxt, /e\.textContent !== title\) e\.textContent = title/); assert.match(drawTxt, /e\.textContent !== s\) e\.textContent = s/); assert.match(drawTxt, /e\._h !== h/);
});
test('the readouts the redraws rewrite go through setText', () => {
  for (const re of [
    /setText\(out, \(this\.fmt \|\| this\.def\.f \|\| pct\)\(v\)\)/,   // makeRow's refresh: every <output> of the deck's rows
    /setText\(\$\('tRowsN'\), plen\(id\)\)/, /setText\(\$\('tClkB'\)/, /setText\(\$\('tClkT'\)/,   // drawPat
    /setText\(\$\('dkStripT'\)/, /setText\(\$\('stVolV'\)/, /setText\(\$\('stPanV'\)/,   // deckStrip
    /setText\(\$\('dkInstT'\)/, /setText\(\$\('dkFxT'\)/, /setText\(\$\('dkChnT'\)/, /setText\(\$\('dkChnI'\)/, /setText\(\$\('tArrSub'\)/, /setText\(cue, /,
  ]) assert.match(src, re, String(re));
  assert.doesNotMatch(src, /\$\('(tBpm|tRowsN|stVolV|stPanV|dkStripT|dkInstT|dkFxT|dkChnT|tArrSub)'\)\.textContent =/, 'a per-redraw readout is written directly again');
});
test('the Mixer meters still write only a changed string (paintMeters)', () => {
  assert.match(src, /if \(e\.textContent !== tx\) e\.textContent = tx;/); assert.match(src, /if \(e\.textContent !== t\)\{ e\.textContent = t; e\.style\.width/);
});
