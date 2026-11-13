// LP.SEL (12 Nov, owner: "add a lp.sel button to the command bar that loops the selection in arrange mode. Can be next to the loop play button"; "It can disappear on folded").
// lpSelPlay() is cut out of index.html and run in a vm with stubs for LOOP SEL's own functions; the CSS and menu wiring are checked in the source.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const fn = src.match(/let lsW = null;\nfunction lpSelRestore\(\)\{[^\n]*\nfunction lpSelPlay\(\)\{[\s\S]*?\n\}\n/);
assert.ok(fn, 'could not find lpSelPlay: the code moved, this test needs updating');
function rig({ tvv = 'song', sel = null, on = false, loopSel = false } = {}) {
  const log = [], c = { tp: { on }, tv: tvv, loopSel, loopArm: null, warmTok: 0, cs: { song: { y: 9 } }, rS: n => String(n).padStart(3, '0'), log,
    sxRect: () => sel, tStopUser: () => { log.push('stop'); c.tp.on = false; }, toast: m => log.push('toast:' + m), draw: () => log.push('draw'),
    toggleLoop: () => { log.push('toggleLoop'); c.loopSel = !c.loopSel; if (c.loopSel && sel) c.loopArm = { v: 'song', y0: sel.y0, y1: sel.y1 }; },
    playCtx: () => { log.push('playCtx:' + (c.loopSel && c.loopArm ? c.loopArm.y0 + '-' + c.loopArm.y1 : 'plain')); c.tp.on = true; }, playPatLoop: () => { log.push('playPatLoop:' + c.cs.song.y); c.tp.on = true; } };
  vm.createContext(c); vm.runInContext(fn[0].replace(/^let lsW/, 'var lsW') + '\nthis.go = lpSelPlay; this.get = () => lsW; this.restore = lpSelRestore;', c); return c;
}
test('a selection of rows: LOOP SEL turns on from it and playCtx plays those rows (no second loop path)', () => {
  const c = rig({ sel: { y0: 4, y1: 7 } }); c.go();
  assert.deepEqual(c.log.filter(x => !x.startsWith('toast')), ['toggleLoop', 'playCtx:4-7']); assert.ok(c.get(), 'remembers LOOP SEL as it was');
});
test('LOOP SEL already on: nothing is toggled, playCtx plays the rows', () => {
  const c = rig({ sel: { y0: 2, y1: 3 }, loopSel: true }); c.loopArm = { v: 'song', y0: 2, y1: 3 }; c.go(); assert.deepEqual(c.log, ['playCtx:2-3']);
});
test('no selection (or one row): loop play on the cursor row, and a toast says to select rows for a longer loop', () => {
  for (const sel of [null, { y0: 5, y1: 5 }]) { const c = rig({ sel }); c.go(); assert.deepEqual(c.log.filter(x => x !== 'draw').map(x => x.replace(/ .*/, '')), ['playPatLoop:9', 'toast:Looping']); assert.match(c.log.join(), /select 2 or more rows for a longer loop/); }
});
test('pressed again while playing: it stops, and LOOP SEL goes back to what it was when the song has stopped', () => {
  const c = rig({ sel: { y0: 4, y1: 7 } }); c.go(); assert.equal(c.loopSel, true); c.log.length = 0; c.go(); assert.deepEqual(c.log, ['stop']);
  c.get().seen = true; c.restore(); assert.equal(c.loopSel, false, 'LOOP SEL as before'); assert.equal(c.loopArm, null); assert.equal(c.get(), null);
});
test('not on Arrange: it says so and starts nothing', () => { const c = rig({ tvv: 'phrase', sel: { y0: 1, y1: 4 } }); c.go(); assert.ok(!c.tp.on); assert.match(c.log[0], /^toast:LOOP SEL loops the selected rows on Arrange/); });
test('wiring: the button, its action, the bar orders, draw()', () => {
  assert.match(src, /\['lpsel', 'LOOP SEL', 'Loops the selected rows of the song/);
  assert.match(src, /if \(c === 'lpsel'\) return lpSelPlay\(\);/);
  assert.match(src, /'redo', 'lpsel', 'song'\];/, 'pad order: right beside loop play'); assert.match(src, /'prev', 'next', 'lpsel', 'song'\];/, 'COMMAND BAR order');
  assert.match(src, /if \(lsW\)\{ if \(tp\.on\) lsW\.seen = true; else if \(lsW\.seen\) lpSelRestore\(\); \}/);
  assert.match(src, /lb\.hidden = !vis; lb\.classList\.toggle\('on', !!lsW && tp\.on\)/);
  assert.match(src, /it\('lpsel', 'LOOP SEL: loop the selected rows[^\n]*\{off:!onSong\}/, 'Edit ▾');
  assert.match(src, /new Set\(\['mixp', 'dup', 'tname', 'unbus', 'lpsel'\]\)/, 'Arrange block right-click');
});
test('folded: not on the row under 700 px; wider windows get a column more where it shows', () => {
  assert.match(src, /@media \(max-width:699px\)\{ \.tcmd:not\(\.cmini\) button\[data-c=lpsel\]\{display:none\} \}/);
  assert.match(src, /@media \(min-width:700px\)\{ \.tcmd\.lps\{grid-template-columns:repeat\(16,/); assert.match(src, /\.tcmd\.song\.lps\{grid-template-columns:repeat\(18,/);
});
