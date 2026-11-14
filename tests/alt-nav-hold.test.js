// ALT + ▲▼ IN THE PHRASE EDITOR vs THE PLAYHEAD (12 Nov, owner: "when editing a phrase with phrase in focus and using the alt button to navigate up between editing patterns, the playhead and cursor fight each
// other for visibility. Cursor should be able to move past the playhead this way, and stay in focus."). Arrange beside the phrase WITHOUT the keys was held off the playhead only by arrHandT / arrTouchT, which a pad press never
// sets: every ALT+▲ put Arrange's rows where the cursor was and the next redraw paged them back to the playing row (cs.song.top alternating, the cursor out of view).
// followPlay + arrFollowPage are cut out of index.html and run in a vm with a fake clock and a moving playing position.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const code = [cut(/const handsOn = [^\n]*\n/, 'handsOn'), cut(/let navT = -1e9;\n/, 'navT'), cut(/function arrFollowPage\(own\)\{[\s\S]*?\n\}\n/, 'arrFollowPage'),
  cut(/function followPlay\(\)\{[\s\S]*?\n\}\n\/\/ ARRANGE PAGES/, 'followPlay').replace(/\n\/\/ ARRANGE PAGES$/, '')].join('\n');
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function rig({ follow = 2, row = 2, y = 30, top = 15 } = {}) {
  let now = 100000;
  const c = { performance: { now: () => now }, clamp, console, FOLLOW_HOLD: 4000, ARR_IDLE: 60000, followMode: follow, followOn: follow === 2, selA: null, sdrag: null, drag: null, tv: 'phrase', apF: null, apReady: true, cTrack: 0, cPhrase: 5, cSect: 0,
    cs: { song: { x: 0, y, top, pg: false }, phrase: { y: 0, top: 0, pg: false }, sect: { y: 0 } }, tp: { on: true, mode: 'song', pos: [], loopV: '' }, fpHome: null, fpMem: null, workT: -1e9, arrHandT: -1e9, arrActT: -1e9, arrTouchT: -1e9,
    asongOn: () => false, apPanel: () => true, arrRows: () => 27, songRows: () => 20, pvActive: () => false, pvFollow() {}, plen: () => 32, FOLLOW: 0 };
  vm.createContext(c); vm.runInContext(code.replace(/^let navT/m, 'var navT').replace(/^let /gm, 'var ') + '\nthis.followPlay = followPlay; this.setNav = t => { navT = t; };', c);
  c.tp.pos = [{ row, phrase: 9, step: 0, sect: 1 }];
  return { c, at: t => { now = t; }, now: () => now, play(r, ph) { c.tp.pos = [{ row: r, phrase: ph == null ? 9 : ph, step: 0, sect: 1 }]; }, nav() { c.setNav(now); c.workT = now; c.arrActT = now; } };
}
for (const follow of [0, 1, 2]) {
  test(`FOLLOW mode ${follow}: while the user is navigating with ALT, a moving playhead (above, then below, then wrapped) changes nothing; after the hold it pages once`, () => {
    const r = rig({ follow }), c = r.c; r.nav();
    const before = [c.cPhrase, c.cs.song.y, c.cs.song.top];
    for (let i = 0; i < 30; i++) { r.at(r.now() + 100); r.play([2, 40, 3, 1][i % 4], 9 + (i % 2)); c.followPlay(); }   // 3 s of redraws, playhead jumping around the cursor's row 30
    assert.deepEqual([c.cPhrase, c.cs.song.y, c.cs.song.top], before, 'cPhrase / song row / Arrange top stand while hands-on');
    const hold = follow ? 4000 : 60000;
    r.at(r.now() + hold + 500); r.play(2); c.followPlay(); const t1 = c.cs.song.top;
    assert.notEqual(t1, before[2], 'after the hold the playhead pages Arrange'); assert.ok(c.cs.song.top <= 2 || (c.cs.song.y >= c.cs.song.top && c.cs.song.y < c.cs.song.top + 27), 'to the playing row, or keeping the cursor if both fit');
    for (let i = 0; i < 20; i++) { r.at(r.now() + 16); c.followPlay(); }
    assert.equal(c.cs.song.top, t1, 'once: no flip-flop afterwards');
  });
}
test('after the hold the page keeps the cursor row in view when the playhead and the cursor both fit (the playhead does not hide the cursor the user moved past it)', () => {
  const r = rig({ follow: 0, y: 12, top: 30 }); r.at(r.now() + 61000); r.play(2); r.c.followPlay();
  assert.equal(r.c.cs.song.top, 2, 'cursor 12 and playhead 2 fit in 27 rows: the page starts at the higher row'); assert.ok(r.c.cs.song.y >= 2 && r.c.cs.song.y < 29);
  const q = rig({ follow: 0, y: 40, top: 30 }); q.at(q.now() + 61000); q.play(2); q.c.followPlay(); assert.equal(q.c.cs.song.top, 2, 'too far apart: the playing row becomes the first (owner, 30 Oct)');
});
test('the hold does not outlast its time: FOLLOW ON 4 s, FOLLOW OFF a minute (3 Nov)', () => {
  const a = rig({ follow: 2 }); a.nav(); a.at(a.now() + 3900); a.play(2); a.c.followPlay(); assert.equal(a.c.cs.song.top, 15, '3.9 s: held');
  const b = rig({ follow: 0 }); b.nav(); b.at(b.now() + 59000); b.play(2); b.c.followPlay(); assert.equal(b.c.cs.song.top, 15, '59 s: held'); b.at(b.now() + 2000); b.c.followPlay(); assert.equal(b.c.cs.song.top, 2, '61 s: pages');
});
test('FOLLOW PATTERN with Arrange holding the keys, the cursor track HAS a block on song row 0 and the song wraps: the cursor is not pulled to row 000 while you work', () => {
  const r = rig({ follow: 1, y: 30, top: 15 }); r.c.apF = 'arr'; r.c.tv = 'phrase'; r.play(40); r.nav();
  for (let i = 0; i < 20; i++) { r.at(r.now() + 150); r.play(i < 10 ? 41 + i : 0 + (i - 10)); r.c.followPlay(); }
  assert.equal(r.c.cs.song.y, 30, 'row stays'); assert.equal(r.c.cs.song.top, 15);
});
test('the wiring: ALT + arrows set navT, and the Song-grid moves clear the playhead-page flag', () => {
  assert.match(src, /function optDir\(dir, grid\)\{\n  if \(grid\) navT = performance\.now\(\);/);
  assert.match(src, /cTrack = x; cPhrase = p; selA = null; cs\.song\.pg = cs\.phrase\.pg = false;/);
  assert.match(src, /cTrack = t; cPhrase = p; cs\.song\.x = t; cs\.song\.y = y; selA = null; cs\.song\.pg = false;/);
});
