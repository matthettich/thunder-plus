// TO TOP (13 Nov, owner: "Let's do the corner button"; build 576 had taken the second-PLAY shortcut away): a button in the empty corner of Arrange's header
// (above the row numbers) and of the classic Song grid, and Edit ▾ → Top of the song. One tap: the song cursor to row 000 on its own track, Arrange's view to
// the top (cs.song.top = 0, pg cleared), the user's hand on the screen for every follow / paging hold, and never a play or a stop.
// songToTop, markHands and arrCursorMoved are cut out of index.html and run in a vm; the markup, CSS and wiring are checked as text.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const fnSong = cut(/function songToTop\(\)\{[\s\S]*?\n\}\n/, 'songToTop'), fnHands = cut(/const markHands = \(\) => \{[^\n]*\};/, 'markHands'), fnMoved = cut(/function arrCursorMoved\(c, y\)\{[\s\S]*?\n\}\n/, 'arrCursorMoved');

function rig(o = {}) {
  let now = 5000; const calls = [];
  const c = Object.assign({ tv: 'song', cs: { song: { x: 5, y: 30, top: 24, pg: true }, phrase: { x: 2 } }, workT: -1e9, arrActT: -1e9, arrHandT: -1e9, arrTouchT: -1e9, navT: -1e9, arrUserT: 77, apReady: false, apF: null, cTrack: 5,
    performance: { now: () => now }, tStart: () => calls.push('tStart'), tStop: () => calls.push('tStop'), tp: { on: false },
    songEndKey: last => calls.push('songEndKey:' + last), draw: () => calls.push('draw'), pvActive: () => false, pvGo: (...a) => calls.push('pvGo:' + a.join(',')),
    apFollow: () => calls.push('apFollow'), apTop: () => calls.push('apTop'), arrKeepRow: () => calls.push('arrKeepRow'), arrRows: () => 20, apLastKey: 'x', toast: () => {} }, o);
  c.calls = calls; vm.createContext(c);
  vm.runInContext(fnHands.replace(/^const /, 'var '), c);
  vm.runInContext(fnMoved + '\n' + fnSong, c);
  return { c, calls, at: t => { now = t; } };
}
const clocks = c => [c.workT, c.arrActT, c.arrHandT, c.arrTouchT, c.navT];

test('on the Song screen it is Ctrl/⌘+Home\'s songEndKey(false), after clearing top and the page flag; the hold clocks are all now', () => {
  const r = rig(); r.c.songToTop();
  assert.deepEqual(r.calls, ['songEndKey:false'], 'only the Home function (it redraws itself)');
  assert.equal(r.c.cs.song.top, 0); assert.equal(r.c.cs.song.pg, false); assert.equal(r.c.cs.song.x, 5, 'the track is kept');
  assert.deepEqual(clocks(r.c), [5000, 5000, 5000, 5000, 5000]); assert.equal(r.c.arrUserT, -1e9, 'Arrange\'s scroll-drag guard is off, so the grid scrolls to the top');
});
test('beside another screen: the cursor to row 0 on the same track, the view to 0, the phrase follows as for the wheel, one draw', () => {
  const r = rig({ tv: 'mixer' }); r.c.songToTop();
  assert.deepEqual(r.c.cs.song, { x: 5, y: 0, top: 0, pg: false });
  assert.deepEqual(r.calls, ['arrKeepRow', 'draw'], 'nothing else on a screen with no phrase');
  const p = rig({ tv: 'phrase', pvActive: () => true }); p.c.songToTop();
  assert.equal(p.calls[0], 'pvGo:5,0,2,', 'Pattern view opens this track\'s pattern on row 0 (pvGo, as the wheel does)'); assert.equal(p.calls[p.calls.length - 1], 'draw');
  assert.equal(p.c.cs.song.top, 0); assert.deepEqual(clocks(p.c), [5000, 5000, 5000, 5000, 5000]);
  const k = rig({ tv: 'phrase', apReady: true, apF: 'arr' }); k.c.songToTop();
  assert.ok(k.calls.includes('apFollow') && k.calls.includes('apTop') && !k.calls.includes('pvGo:5,0,undefined,null'), 'Arrange holding the keys: apFollow + apTop');
  assert.equal(k.c.cs.song.top, 0); assert.equal(k.c.cs.song.pg, false);
});
test('it never starts or stops playback, parked or playing', () => {
  for (const on of [false, true]) for (const tv of ['song', 'phrase', 'mixer', 'inst']) { const r = rig({ tv, tp: { on } }); r.c.songToTop(); assert.ok(!r.calls.some(x => /^t(Start|Stop)$/.test(x)), `${tv} playing=${on}`); assert.equal(r.c.tp.on, on); }
  assert.ok(!/function songToTop\(\)\{[\s\S]*?\n\}\n/.exec(src)[0].match(/tStart|tStop|tStopUser|playSong|togglePlay/), 'the function does not mention play or stop at all');
});
test('the button, its label and its wiring are in place', () => {
  assert.match(src, /<div class="tatop"><button type="button" id="tArrTop" aria-label="Top of the song" title="Top of the song"><svg class="ic" aria-hidden="true"><use href="#ic-totop"\/><\/svg><\/button><\/div>\s*<div class="tath" id="tArrTh">/, 'between the MONO / POLY row and the track numbers, in the sticky header');
  assert.match(src, /<symbol id="ic-totop" viewBox="0 0 24 24"><path d="[^"]+"\/><\/symbol>/, 'an outline icon in the sprite, no emoji');
  assert.match(src, /\$\('tArrTop'\)\.addEventListener\('click', e => \{ e\.preventDefault\(\); songToTop\(\); \}\);/);
  assert.match(src, /b\.addEventListener\('click', e => \{ e\.preventDefault\(\); songToTop\(\); \}\); scr\.append\(b\);/, 'the classic Song grid\'s corner');
  assert.match(src, /\{a:'top', l:'Top of the song', k:keyHint\('mod\+Home'\), f:songToTop\}/, 'Edit ▾ → Move shows Ctrl/⌘+Home');
  assert.match(src, /,#tGridTop'\)\) return;/, 'the classic grid\'s swipe handler leaves the button\'s own click alone');
});
test('the corner button takes no room: zero-height row, 36 px target, the header\'s ink, no new colours', () => {
  const css = cut(/\.tatop\{flex:none; height:0;[\s\S]*?body\.asong #tArr \.tatop button\{width:36px\}/, 'the .tatop CSS');
  assert.match(css, /\.tatop\{flex:none; height:0; position:relative/); assert.match(css, /width:32px; height:36px/); assert.match(css, /color:var\(--ar-dim\)/); assert.match(css, /:active\{background:var\(--ar-line\); color:var\(--ar-ink\)/);
  assert.ok(!/#[0-9a-f]{3,8}\b|rgb|hsl/i.test(css.replace(/#tArr\b/g, '')), 'only the header\'s own variables');
});
test('markHands sets every clock the follow / paging holds read', () => {
  for (const n of ['workT', 'arrActT', 'arrHandT', 'arrTouchT', 'navT']) assert.ok(fnHands.includes(n), n);
  assert.match(src, /const handsOn = \(ms, \.\.\.ts\) => performance\.now\(\) - Math\.max\(\.\.\.ts\) < ms;/);
});
test('the wheel still uses the same cursor-moved code (one place for "the phrase follows")', () => {
  assert.match(src, /c\.y = y; arrUserT = -1e9; c\.pg = false; arrHandT = performance\.now\(\);\n  arrCursorMoved\(c, y\);\n  draw\(\);/);
});
