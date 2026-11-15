// ARRANGE PAGING WITH LOOK-AHEAD (14 Nov, owner: "I think it's the way it pages - it's difficult to see whole phrases"). The page turns when the playhead comes within ARR_AHEAD rows of the
// bottom of the view (a quarter of the view under 16 rows), once, so the playhead lands ARR_KEEP rows from the top (an eighth of the view if smaller). arrPageTop is pure: it is cut out of
// index.html and run in a vm, with arrFollowPage (the rules about hands on Arrange, FOLLOW off, the minute) around it. Source pins: followPlay's page() uses the same rule.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const rule = cut(/const ARR_AHEAD = 8, ARR_KEEP = 4;[\s\S]*?\nfunction arrPageTop\(top, VR, row, len, keep = -1, end = len - 1\)\{[\s\S]*?\n\}\n/, 'arrPageTop');
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const P = (() => { const c = { clamp }; vm.createContext(c); vm.runInContext(rule + '\nthis.f = arrPageTop; this.AH = ARR_AHEAD; this.KP = ARR_KEEP;', c); return c; })();
const pt = (top, VR, row, len = 256, keep = -1, end = len - 1) => P.f(top, VR, row, len, keep, end);

test('the constants: 8 rows look-ahead, 4 rows kept', () => { assert.equal(P.AH, 8); assert.equal(P.KP, 4); });
test('32 rows in view: nothing turns until the playhead is within 8 rows of the bottom, then once, to 4 rows from the top', () => {
  for (let r = 0; r <= 23; r++) assert.equal(pt(0, 32, r), 0, 'row ' + r);
  assert.equal(pt(0, 32, 24), 20); assert.equal(pt(20, 32, 24), 20, 'after the turn the playhead sits at row 4 of the view: no more turns');
  for (let r = 20; r < 20 + 32 - 8; r++) assert.equal(pt(20, 32, r), 20);
  assert.equal(pt(20, 32, 44), 40);
});
test('a turn happens once per page: walking the playhead down a 256-row song turns about every (view - 8 - 4) rows', () => {
  let top = 0, turns = 0; for (let r = 0; r < 200; r++){ const n = pt(top, 32, r); if (n !== top){ turns++; top = n; assert.ok(r - top === 4, 'the playhead lands 4 rows from the top (' + (r - top) + ')'); } }
  assert.ok(turns >= 8 && turns <= 10, 'turns ' + turns);   // 200 rows / 20 per page
});
test('a view of under 16 rows uses a quarter of it as the look-ahead and an eighth (at least 1) as the rows kept', () => {
  assert.equal(pt(0, 12, 8), 0); assert.equal(pt(0, 12, 9), 8, '12 rows: 3 ahead, 1 kept');
  assert.equal(pt(0, 8, 5), 0); assert.equal(pt(0, 8, 6), 5, '8 rows: 2 ahead, 1 kept');
  assert.equal(pt(0, 15, 11), 0); assert.equal(pt(0, 15, 12), 11, '15 rows: 3 ahead, 1 kept');
  assert.equal(pt(0, 16, 7), 0); assert.equal(pt(0, 16, 8), 6, '16 rows: the full 8 ahead, 2 kept');
  assert.equal(pt(0, 2, 1), 0, 'the smallest view: no thrash');
});
test('the last page: one turn goes straight to the page that shows the end (no second turn a few rows on); never past the end', () => {
  assert.equal(pt(0, 20, 11, 32), 0); assert.equal(pt(0, 20, 12, 32), 12, 'a 32-row phrase in a 20-row view: row 12 turns to the final page'); for (let r = 12; r < 32; r++) assert.equal(pt(12, 20, r, 32), 12, 'row ' + r);
  assert.equal(pt(77, 23, 92, 256, -1, 103), 81, 'a 104-row song: the last page ends on its last row');
  assert.equal(pt(58, 23, 73, 256, -1, 103), 71, 'far from the end: the usual 2 rows kept');
});
test('turns per pass of a 32-row phrase through a view of 12 to 31 rows: no more than the old rule from 20 rows up (the last page goes straight to the end), at most about twice below that (the look-ahead is half a small view)', () => {
  for (let VR = 12; VR <= 31; VR++){
    const run = f => { let top = 0, n = 0; for (let cyc = 0; cyc < 3; cyc++) for (let r = 0; r < 32; r++){ const t = f(top, r); if (t !== top){ n++; top = t; } } return n; };
    const old = run((top, r) => (r < top || r > top + VR - 1 ? clamp(r, 0, 32 - VR) : top)), now = run((top, r) => pt(top, VR, r, 32));
    assert.ok(VR >= 20 ? now <= old : now <= old * 2.2, `VR ${VR}: ${now} turns, the old rule ${old}`); }
});
test('the playhead above the view (a loop, LP.SEL or song wrap, "Top of the song") puts its row first, as before', () => {
  assert.equal(pt(40, 32, 0), 0); assert.equal(pt(40, 32, 12), 12, 'a loop that starts at row 12: it is the first row'); assert.equal(pt(40, 32, 39), 39);
});
test('beyond the view (a jump forward): the playhead lands 4 rows from the top, and the last page is clamped to the end', () => {
  assert.equal(pt(0, 32, 100), 96); assert.equal(pt(0, 32, 255), 224); assert.equal(pt(0, 32, 250, 64), 32, 'a 64-row pattern: the last page');
});
test('nothing more to come before the wrap: a view that already shows the loop\'s / song\'s last row never turns early', () => {
  assert.equal(pt(0, 32, 28, 256, -1, 20), 0); assert.equal(pt(0, 32, 28, 256, -1, 31), 0, 'the end row is in view');
  assert.equal(pt(0, 32, 28, 256, -1, 32), 1, 'one row more than the view shows: it turns, once, to the page that shows it');
  assert.equal(pt(0, 23, 15, 256, -1, 15), 0, 'a 16-row loop in a 23-row view');
  assert.equal(pt(0, 32, 40, 256, -1, 20), 36, 'off the view it turns whatever the end says');
});
test('a 32-row phrase in a 32-row view, a 64-row pattern in a 32-row view', () => {
  for (let r = 0; r < 32; r++) assert.equal(pt(0, 32, r, 32), 0, 'the whole phrase is in view: it never turns');
  assert.equal(pt(0, 32, 23, 64, -1, 63), 0); assert.equal(pt(0, 32, 24, 64, -1, 63), 20, 'a 64-row pattern pages at row 24');
  assert.equal(pt(20, 32, 44, 64, -1, 63), 32, 'its last page is clamped to 32');
  assert.equal(pt(32, 32, 62, 64, -1, 63), 32, 'and stays there until the wrap, which puts row 0 first'); assert.equal(pt(32, 32, 0, 64, -1, 63), 0);
});
test('the cursor\'s row stays in view when both fit and the playhead has left the view (12 Nov); an early turn inside the view is the playhead\'s', () => {
  assert.equal(pt(0, 32, 40, 256, 20), 20, 'playhead off the bottom, cursor at 20: the page starts at the cursor'); assert.equal(pt(0, 32, 40, 256, 38), 36, 'cursor below where the playhead rule starts: the playhead rule');
  assert.equal(pt(40, 32, 5, 256, 2), 2, 'playhead above the view');
  assert.equal(pt(0, 32, 30, 256, 10), 26, 'still in view, in the look-ahead zone: it turns even though the cursor is at row 10');
});
test('a row of -1 (nothing playing there) turns nothing', () => { assert.equal(pt(5, 32, -1), 5); });

// arrFollowPage around it: it does NOT page while he is working in Arrange and starts again after the hold / the minute; FOLLOW off still pages (10 Nov)
const FP = cut(/function arrFollowPage\(own\)\{[\s\S]*?\n\}\n/, "arrFollowPage").replace("catch(e){}", "catch(e){ globalThis.__err = e; }");
function rigFollow(o = {}) {
  let now = 1e6;
  const c = { clamp, performance: { now: () => now }, FOLLOW_HOLD: 4000, ARR_IDLE: 60000, arrHandT: -1e9, arrActT: -1e9, arrTouchT: -1e9, navT: -1e9, followMode: 2, apF: '', tv: 'phrase', tp: { mode: 'song' },
    cs: { song: { top: 0, y: 0, pg: false } }, asongOn: () => !!o.asong, apPanel: () => true, arrRows: () => 32, arrWrapRow: () => 255, ...o };
  vm.createContext(c);
  vm.runInContext('const handsOn = (ms, ...ts) => performance.now() - Math.max(...ts) < ms;\n' + rule + '\n' + FP + '\nthis.go = own => arrFollowPage(own);', c); c.__dbg = () => c.__err;
  return { c, at: ms => { now += ms; }, go: row => c.go({ row }) };
}
test('arrFollowPage: pages early with FOLLOW ON, FOLLOW PATTERN and FOLLOW OFF, and sets pg', () => {
  for (const fm of [0, 1, 2]) { const R = rigFollow({ followMode: fm }); R.go(23); assert.equal(R.c.cs.song.top, 0); R.go(24); assert.equal(R.c.cs.song.top, 20, 'followMode ' + fm); assert.equal(R.c.cs.song.pg, true); }
});
test('arrFollowPage: not while he works in Arrange (keys there, or the Song screen), again after 4 s (FOLLOW on) or a minute (FOLLOW off)', () => {
  const R = rigFollow({ followMode: 2, apF: 'arr' }); R.c.arrActT = 1e6 - 500; R.go(30); assert.equal(R.c.cs.song.top, 0, 'working');
  R.at(4500); R.go(30); assert.equal(R.c.cs.song.top, 26, 'FOLLOW on: 4 s later');
  const O = rigFollow({ followMode: 0, apF: 'arr' }); O.c.arrActT = 1e6 - 500; O.go(30); assert.equal(O.c.cs.song.top, 0);
  O.at(30000); O.go(30); assert.equal(O.c.cs.song.top, 0, 'FOLLOW off: still working after 30 s'); O.at(31000); O.go(30); assert.equal(O.c.cs.song.top, 26, 'a minute with no key, click or wheel');
  const S = rigFollow({ followMode: 0, asong: true, tv: 'song' }); S.c.arrHandT = 1e6 - 1000; S.go(40); assert.equal(S.c.cs.song.top, 0, 'a scroll of it holds paging for 4 s');
  const N = rigFollow({ followMode: 2 }); N.c.navT = 1e6 - 100; N.go(30); assert.equal(N.c.cs.song.top, 0, 'an ALT move holds it'); 
});
test('arrFollowPage: a wrap (loop, song) puts the playhead row first; the cursor\'s row is kept in view when both fit', () => {
  const R = rigFollow(); R.c.cs.song.top = 100; R.go(0); assert.equal(R.c.cs.song.top, 0, 'song wrap / Top of the song'); R.go(12); assert.equal(R.c.cs.song.top, 0 + 0, 'inside: nothing');
  R.c.cs.song.y = 200; R.c.cs.song.top = 40; R.go(12); assert.equal(R.c.cs.song.top, 12, 'a loop at row 12');
  const K = rigFollow(); K.c.cs.song.y = 5; K.c.cs.song.top = 0; K.go(26); assert.equal(K.c.cs.song.top, 22, 'cursor at 5, playhead in the look-ahead zone: it turns early'); K.c.cs.song.top = 0; K.go(36); assert.equal(K.c.cs.song.top, 5, 'cursor at 5 and playhead at 36 (off the view) fit one view: the page starts at the cursor');
  const L = rigFollow({ arrWrapRow: () => 20 }); L.go(26); assert.equal(L.c.cs.song.top, 0, 'a loop that ends in view does not turn a page');
});
test('source: followPlay\'s page() (Song grid, Phrase, Pattern view) and Arrange use the one rule; the loop / song end is read, not asked of the sequencer', () => {
  assert.match(src, /const page = \(c, row, VR, len, end\) => \{ if \(row < 0 \|\| view !== 'tracker'\) return; const top = c\.top \|\| 0, nt = arrPageTop\(top, VR, row, len, -1, end\);/);
  assert.match(src, /page\(cs\.song, own\.row, songRows\(\), 256, arrWrapRow\(\)\)/); assert.match(src, /page\(cs\.phrase, mine\.step, pvr, pvActive\(\) \? 256 : plen\(cPhrase\), plen\(cPhrase\) - 1\)/);
  const w = cut(/function arrWrapRow\(\)\{[\s\S]*?\n\}\n/, 'arrWrapRow'); assert.doesNotMatch(w, /tp\.seq|setLoop|patLoopRow|postMessage/);
});

test('followPlay\'s page() does nothing off the tracker (build 609: the Synth page round trip with LOOP on left Arrange paged on the loop\'s row)', () => {
  assert.match(src, /const page = \(c, row, VR, len, end\) => \{ if \(row < 0 \|\| view !== 'tracker'\) return;/);
});
