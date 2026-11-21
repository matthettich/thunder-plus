// ARRANGE ON STOP beside another screen (14 Nov, owner: ARRANGE ON STOP = PLAYHEAD, but the Arrange cursor went back to where it was).
// Phrase holding the keys read PHRASE ON STOP (STAYS), which restored Arrange's cursor too. arrCurToPlay is cut out and run in a vm; the STOP wiring is pinned as text;
// the real-page run is opt-in (TP_BROWSER_TESTS=1).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
const fn = src.match(/function arrCurToPlay\(P\)\{[\s\S]*?\n\}\n/); assert.ok(fn, 'arrCurToPlay moved');
function rig(o = {}) {
  const c = Object.assign({ cs: { song: { x: 3, y: 45, top: 40, pg: true } }, MT: 32, arrRows: () => 20, clamp: (v, a, b) => Math.max(a, Math.min(b, v)) }, o);
  vm.createContext(c); vm.runInContext(fn[0] + '\nthis.f = arrCurToPlay;', c); return c;
}
test('the cursor goes to the row playing on its own track; the column stays; the row is brought into view', () => {
  const c = rig(), P = []; P[3] = { row: 25 }; P[5] = { row: 9 };
  assert.equal(c.f(P), true); assert.equal(c.cs.song.y, 25); assert.equal(c.cs.song.x, 3); assert.equal(c.cs.song.top, 23); assert.equal(c.cs.song.pg, false);
});
test('a row already in view leaves the view alone', () => {
  const c = rig({ cs: { song: { x: 3, y: 0, top: 20, pg: true } } }), P = []; P[3] = { row: 30 }; c.f(P); assert.equal(c.cs.song.y, 30); assert.equal(c.cs.song.top, 20);
});
test('a cursor on an empty track takes the row from a track that played (never the master)', () => {
  const c = rig(), P = []; P[32] = { row: 99 }; P[7] = { row: 12 }; assert.equal(c.f(P), true); assert.equal(c.cs.song.y, 12); assert.equal(c.cs.song.x, 3);
  assert.equal(rig().f([]), false);
});
test('tStop: the Arrange-beside case needs ARRANGE ON STOP = PLAYHEAD, a user stop that was playing the song, another screen holding the keys, and Arrange showing; it runs after the restore', () => {
  const m = src.match(/arrP = (!quiet && stopArrOn[^;]*?) : null; \}\);/); assert.ok(m, 'arrP line');
  for (const w of ['!quiet', 'stopArrOn', '!stopToPlayhead()', "tv !== 'song'", 'tp.on', "tp.mode === 'song'", 'apPanel()']) assert.ok(m[1].includes(w), w);
  const a = src.indexOf("else if (home) try {"), b = src.indexOf("if (arrP) G('arrange cursor'"); assert.ok(a > 0 && b > a, 'the Arrange step comes after the home restore');
  assert.match(src, /if \(stopP\) try \{ if \(curToPlay\(stopP\.P, stopP\.mode\)/, 'STOP with the setting at the playhead on the keys-holding screen is unchanged');
});
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page: Phrase holds the keys, Arrange beside, FOLLOW on or off, PHRASE ON STOP at STAYS: STOP puts the Arrange cursor on the playing row', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot, app, ROOT } = await import('./sweep/ui-lib.mjs');
  const ELECTRO = fs.readFileSync(require('path').join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
  const B = await boot({ width: 1600, height: 1000, storage: { 'thunderplus-platform-v1': 'desktop', 'thunderplus-stoparr-v1': '1', 'thunderplus-stopcur-v2': '0' } });
  try {
    await B.page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await B.page.waitForTimeout(500);
    await app(B.page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, $('toast').hidden = true, 1)`);
    for (const follow of [true, false]) {
      await app(B.page, `tStop(true), go('phrase'), (typeof setFollowMode === 'function' ? setFollowMode(${follow}) : (followMode = ${follow})), apFocus('phrase'), (cs.song.y = 45, cs.song.top = 40, workT = arrActT = arrHandT = arrTouchT = navT = -1e9), draw(), 1`);
      await app(B.page, `tStart('song', {row: 10}), 1`); await B.page.waitForTimeout(3500);
      const row = await app(B.page, `tp.pos.find(Boolean).row`); await app(B.page, `tStop(), 1`); await B.page.waitForTimeout(400);
      const r = JSON.parse(await app(B.page, `JSON.stringify({y: cs.song.y, top: cs.song.top, vr: arrRows(), x: cs.song.x})`));
      assert.ok(Math.abs(r.y - row) <= 1, `follow ${follow}: cursor ${r.y} vs playing row ${row}`);
      assert.ok(r.y >= r.top && r.y <= r.top + r.vr - 1, `follow ${follow}: the row is in view`);
    }
  } finally { await B.close(); }
});

// PHRASE ON STOP = STAYS in Pattern view (build 609, owner: "following playhead"): the Arrange-beside step must not move the phrase cursor's track or step.
test('Pattern view: the Arrange step goes through pvArrToPlay (row only, own track kept), not curToPlay', () => {
  assert.match(src, /if \(tv === 'phrase' && pvActive\(\)\)\{ pvArrToPlay\(arrP\);/, 'the arrP step');
  const f = src.match(/function pvArrToPlay\(P\)\{[\s\S]*?\n\}\n/); assert.ok(f, 'pvArrToPlay moved');
  const c = { cTrack: 4, MT: 32, calls: [], pvGo: (...a) => (c.calls.push(a), true) }; vm.createContext(c); vm.runInContext(f[0] + '\nthis.f = pvArrToPlay;', c);
  const P = []; P[4] = { row: 12, step: 7, phrase: 3 }; P[9] = { row: 30, step: 1, phrase: 8 };
  assert.equal(c.f(P), true); assert.deepEqual(c.calls[0], [4, 12, null, null], 'own track, the playing row, no step');
  c.calls.length = 0; const Q = []; Q[9] = { row: 30 }; Q[32] = { row: 99 }; c.f(Q); assert.deepEqual(c.calls[0], [4, 30, null, null], 'an empty own track: the row of one that played, never the master');
  assert.equal(c.f([]), false);
});
