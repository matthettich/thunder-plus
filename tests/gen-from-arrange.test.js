// GEN FROM ARRANGE (12 Nov, owner: "pressing GEN from the arrangement window should kick us over into the phrase under the cursor, not tell us to navigate
// there"; an empty cell: "Make a new pattern"). genToPhrase() is cut out of index.html and run in a vm with stand-ins for the song and the app's own paths.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function genToPhrase\(\)\{[\s\S]*?\n\}\nfunction genRefuse\(t\)\{[^\n]*\n/);
assert.ok(m, 'could not find genToPhrase(): the code moved, this test needs updating');

function rig({ x = 2, y = 5, cell = 7, sects = false, free = true, refuse = '' } = {}) {
  const log = { toast: [], arrNew: [], go: [] };
  const song = []; for (let r = 0; r < 256; r++) song.push(new Array(9).fill(-1)); song[y][x < 8 ? x : 0] = x < 8 ? cell : -1;
  const c = { NT: 16, busTrack: t => t === 3, d2: n => String(n).padStart(2, '0'), S: { song }, cs: { song: { x, y } }, MT: 8, sectsOn: sects, tv: 'song', toast: (t) => log.toast.push(t),
    arrNewAt: (yy, t) => { log.arrNew.push([yy, t]); if (free) song[yy][t] = 40; }, go: (v) => { log.go.push([v, c.tv]); c.tv = v; } };
  vm.createContext(c); vm.runInContext(m[0] + (refuse ? `\ngenRefuse = () => ${JSON.stringify(refuse)};` : '') + '\nthis.genToPhrase = genToPhrase;', c);
  return { c, log };
}
test('GEN on a block: its phrase opens (go phrase from the song), no toast, no new pattern', () => {
  const r = rig(); assert.equal(r.c.genToPhrase(), false); assert.deepEqual(r.log.go, [['phrase', 'song']]); assert.equal(r.c.tv, 'phrase'); assert.deepEqual(r.log.toast, []); assert.deepEqual(r.log.arrNew, []);
});
test('GEN on an empty cell: a new empty pattern there (arrNewAt, the double-click path), then it opens', () => {
  const r = rig({ cell: -1 }); assert.equal(r.c.genToPhrase(), false); assert.deepEqual(r.log.arrNew, [[5, 2]]); assert.deepEqual(r.log.go, [['phrase', 'song']]);
});
test('GEN on an empty cell with no free pattern: arrNewAt said why, nothing opens', () => {
  const r = rig({ cell: -1, free: false }); assert.equal(r.c.genToPhrase(), true); assert.deepEqual(r.log.go, []);
});
test('GEN on the master column, with sections on (empty cell), or on a track that takes no notes: a short toast, nothing made, nothing opened', () => {
  const a = rig({ x: 8 }); assert.equal(a.c.genToPhrase(), true); assert.match(a.log.toast[0], /master track takes commands/); assert.deepEqual(a.log.arrNew.concat(a.log.go), []);
  const b = rig({ cell: -1, sects: true }); assert.equal(b.c.genToPhrase(), true); assert.match(b.log.toast[0], /Sections are on/); assert.deepEqual(b.log.arrNew.concat(b.log.go), []);
  const d = rig({ cell: -1, refuse: 'A bus track takes commands, not notes' }); assert.equal(d.c.genToPhrase(), true); assert.equal(d.log.toast[0], 'A bus track takes commands, not notes'); assert.deepEqual(d.log.arrNew.concat(d.log.go), []);
  const e = rig({ sects: true }); assert.equal(e.c.genToPhrase(), false, 'a block with sections on still opens its phrase');
});
test('wiring: the GEN menu (pad GEN and Ctrl/⌘+G) and Alt+B go through it on the Song screen; the menu opens on the phrase two frames later', () => {
  assert.match(src, /function genMenu\(fromKeys\)\{\n  \/\/[^\n]*\n  if \(tv === 'song' && view === 'tracker'\)\{ if \(!genToPhrase\(\)\) requestAnimationFrame\(\(\) => requestAnimationFrame\(\(\) => \{ if \(tv === 'phrase'\) genMenuOpen\(fromKeys\); \}\)\); return; \}\n  genMenuOpen\(fromKeys\);/);
  assert.match(src, /function acidLine\(\)\{\n  if \(tv === 'song' && view === 'tracker' && genToPhrase\(\)\) return;/, 'Alt+B (acidLine) does the same');
  assert.match(src, /if \(b === 'gen'\)\{ clearTimeout\(rep\); genMenu\(\); return; \}/);
  assert.match(src, /e\.code === 'KeyG'\)\{ e\.preventDefault\(\); if \(!e\.repeat\) genMenu\(true\); return true; \}/);
});
test('GEN on a bus track\'s block (item 8): refused with a toast, nothing opened or made', () => {
  const r = rig({ x: 3, cell: -1 }); assert.equal(r.c.genToPhrase(), true); assert.match(r.log.toast[0], /Track 04 is a bus/); assert.equal(r.log.arrNew.length, 0); assert.equal(r.log.go.length, 0);
});
