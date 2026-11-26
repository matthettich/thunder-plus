// FOLLOW sits directly above PHRASE ON STOP and ARRANGE ON STOP (19 Nov, owner), in every list that has all three:
// the Project screen's rows (projRows, also the phone's Settings when the sheets are off) and the View / Settings "Screen" part (SHEET_QUICK, buildScreen).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
const WANT = ['FOLLOW', 'PHRASE ON STOP', 'ARRANGE ON STOP'];
const adjacent = (list, what) => {
  const i = list.indexOf('FOLLOW'); assert.ok(i >= 0, what + ': FOLLOW is there');
  assert.deepEqual(list.slice(i, i + 3), WANT, what + ': FOLLOW, PHRASE ON STOP, ARRANGE ON STOP, adjacent and in that order');
  for (const l of WANT) assert.equal(list.filter(x => x === l).length, 1, what + ': ' + l + ' once');
};
test('projRows (Project screen, phone Settings): the three rows are adjacent, FOLLOW first; the row set is unchanged', () => {
  const a = src.indexOf('function projRows(){'), b = src.indexOf("{l:'NAME', sec:'Song'", a); assert.ok(a > 0 && b > a);
  const labels = [...src.slice(a, b).matchAll(/^ {4}\{l:'([^']+)'/gm)].map(m => m[1]);
  adjacent(labels, 'projRows');
  const sec = src.slice(a, b);
  assert.match(sec, /\{l:'RESUME'[\s\S]*\{l:'FOLLOW', segs:true[\s\S]*\{l:'PHRASE ON STOP'[\s\S]*\{l:'ARRANGE ON STOP'[\s\S]*\{l:'NEIGHBORS'/, 'FOLLOW moved down to the ON STOP rows; no section header or gap row sits between them');
  for (const l of WANT) assert.doesNotMatch(src.slice(a, b).match(new RegExp("^ {4}\\{l:'" + l + "'.*$", 'm'))[0], /sec:|gap:/, l + ' has no header or gap');
});
test('View / Settings Screen part: SHEET_QUICK has the three adjacent, and the rest of the Screen rows skip them (no row twice)', () => {
  const m = src.match(/const SHEET_QUICK = (\[[^\]]*\]);/); assert.ok(m, 'SHEET_QUICK');
  const q = eval(m[1]);
  adjacent(q, 'SHEET_QUICK');
  assert.ok(q.indexOf('SIMPLE PHRASE') < q.indexOf('FOLLOW') && q.indexOf('FOLLOW') < q.indexOf('COMMANDS'), 'the quick rows keep their order around them');
  assert.match(src, /g2 === 'Screen' && r\.l !== 'SKIN' && !inSheet\(r\) && !SHEET_QUICK\.includes\(rowKey\(r\)\)/, 'the later Screen loop leaves the quick rows alone');
});
