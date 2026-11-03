// ABLETON LINK (desktop app): the main process's session (electron/link-session.js) against a fake addon, the optional
// loading in main.js, and, when it has been built here (node link/build.js), the real addon. The page side is
// tools/link-test.mjs (headless Chrome with a fake peer).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const { linkSession } = require(path.join(H.ROOT, 'electron/link-session.js'));

function fakeAddon() {
  const A = { on: false, sync: false, tempo: 120, peers: 0, playing: false, playAt: 0, calls: [], t: 1e9 };
  Object.assign(A, {
    enable(v) { A.calls.push(['enable', v]); A.on = v; return v; },
    startStopSync(v) { A.sync = v; return v; },
    now() { return A.t; },
    setTempo(b) { A.calls.push(['tempo', b]); A.tempo = b; return true; },
    start(at, q, play) { A.calls.push(['start', at, q, play]); if (play) { A.playing = true; A.playAt = at; } return true; },
    stop() { A.calls.push(['stop']); A.playing = false; return true; },
    snapshot(q) { A.t += 20000; const b = (A.t - 1e9) * A.tempo / 60e6; return { enabled: A.on, sync: A.sync, peers: A.peers, tempo: A.tempo, beat: b, phase: b % q, playing: A.playing, playAt: A.playAt, now: A.t }; },
  });
  return A;
}
const tick = ms => new Promise(r => setTimeout(r, ms));

test('link session: nothing is sent while off; on, a state message and timing messages every ~20 ms', async () => {
  const A = fakeAddon(), got = [];
  const L = linkSession(A, (ch, m) => got.push([ch, m]));
  await tick(60); assert.equal(got.length, 0, 'off: silent');
  L.enable(true, 133);
  assert.deepEqual(A.calls.slice(0, 2), [['tempo', 133], ['enable', true]], 'our tempo goes in before joining');
  await tick(110);
  const times = got.filter(g => g[0] === 'thunder:link-time'), states = got.filter(g => g[0] === 'thunder:link-state');
  assert.ok(times.length >= 4, 'timing messages: ' + times.length);
  for (const k of ['beat', 'phase', 'tempo', 'at', 'hostNow']) assert.equal(typeof times[0][1][k], 'number', k);
  assert.equal(states.length, 1, 'one state message (nothing changed after it)');
  A.peers = 2; await tick(40);
  assert.equal(got.filter(g => g[0] === 'thunder:link-state').pop()[1].peers, 2, 'a peer count change is a new state');
  L.enable(false); const n = got.length; await tick(60);
  assert.equal(got.length, n, 'off again: silent'); assert.equal(got[n - 1][1].enabled, false, 'with an "off" state last');
});

test('link session: start, stop, tempo and start/stop sync reach the addon', () => {
  const A = fakeAddon(), L = linkSession(A, () => {});
  L.enable(true, 120); L.sync(true); assert.equal(A.sync, true);
  const at = L.start(1.5e9, true); assert.equal(at, 1.5e9);
  assert.deepEqual(A.calls.find(c => c[0] === 'start'), ['start', 1.5e9, 4, true], 'quantum 4, sync on');
  assert.ok(L.start(0, false) > A.t, 'no time given: shortly after now');
  L.stop(); assert.equal(A.playing, false);
  L.setTempo(140.5); assert.equal(A.tempo, 140.5); assert.equal(L.setTempo('x'), false);
  L.off(); assert.equal(A.on, false);
});

test('link: main.js loads the addon optionally, and every handler answers without it', () => {
  const src = H.read('electron/main.js');
  assert.match(src, /try \{ LINK_ADDON = require\(/, 'the addon is required inside a try');
  for (const ch of ['link-enable', 'link-sync', 'link-tempo', 'link-start', 'link-stop', 'link-now'])
    assert.match(src, new RegExp(`'thunder:${ch}'[^\\n]*=> (link|LINK_ADDON) \\?`), ch + ' is guarded');
  const pkg = JSON.parse(H.read('electron/package.json'));
  assert.ok(pkg.build.files.includes('link/**/*') && pkg.build.files.includes('link-session.js'), 'packaged when present');
  assert.ok((pkg.build.asarUnpack || []).some(p => /link/.test(p)), 'the .node file is unpacked from the asar');
  // index.html: no Link without the desktop bridge (the website), and every hook outside the block is guarded
  const html = H.read('index.html');
  assert.match(html, /const LINKAPI = \(window\.thunderDesktop && window\.thunderDesktop\.link\) \|\| null;/);
  assert.match(html, /const lkp = lk \? lkPlan\(/); assert.match(html, /if \(tp\.on && lk && !lk\.restart\) lkStopped\(\)/);
});

test('link: the real addon, when built here (node link/build.js), loads and answers without going on the network', { skip: !fs.existsSync(path.join(H.ROOT, 'electron/link/thunder_link.node')) && 'not built here' }, async () => {
  const L = require(path.join(H.ROOT, 'electron/link/thunder_link.node'));
  const a = L.now(), s = L.snapshot(4);
  assert.ok(L.now() >= a); assert.equal(s.enabled, false); assert.equal(s.peers, 0);
  assert.ok(L.setTempo(128)); assert.equal(L.snapshot(4).tempo, 128);
  assert.equal(L.setTempo(5), false, 'out of range');
  // Link applies an app-thread commit on its own thread, so reading it straight back can see the old timeline under load
  const at = L.now() + 500000; L.start(at, 4, false);
  let d = Infinity; for (let i = 0; i < 50 && Math.abs(d) >= 2; i++) { d = L.timeAtBeat(0, 4) - at; if (Math.abs(d) >= 2) await tick(10); }
  assert.ok(Math.abs(d) < 2, 'alone, beat 0 moves to our start (off by ' + d + ' µs)');
});
