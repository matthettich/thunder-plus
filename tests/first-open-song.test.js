// FIRST-OPEN SONG (owner, 17 Nov): a first-time visitor (no saved song) opens sounds/BETA-TESTING-ELECTRO.thunder; the older
// sounds/ELECTRO.thunder stays (File > Load ELECTRO, the native tests' fixture, the fallback if the new file can't be fetched).
// Someone who already has a song gets that song back. File and wiring are pinned as text; the real-page run (fresh profile, then a
// returning profile) is opt-in (TP_BROWSER_TESTS=1).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), http = require('http');
const H = require('./helpers');
const src = H.read('index.html'), sw = H.read('sw.js');
const NEW = 'sounds/BETA-TESTING-ELECTRO.thunder';

test('the owner\'s song is a project with a kit, titled bETA tESTING eLECTRO, and the old ELECTRO is untouched', () => {
  const j = JSON.parse(H.read(NEW));
  assert.equal(j.app, 'thunder-project'); assert.equal(j.song.name, 'bETA tESTING eLECTRO'); assert.ok(j.kit && Array.isArray(j.kit.voices || j.kit.slots) || j.kit.voices);
  assert.ok(j.song.chainPatch && j.song.chainPatch.format === 'chains.patch' && j.song.chainPatch.modules.length === 9);
  assert.equal(JSON.parse(H.read('sounds/ELECTRO.thunder')).song.name, 'ELECTRO');
});
test('first run fetches the new song first, falls back to ELECTRO; File > Load ELECTRO still loads the old one', () => {
  const m = src.match(/else if \(FIRST_SONG\) setTimeout\(async \(\) => \{[\s\S]*?\n\}, 0\);/); assert.ok(m, 'the first-run block moved');
  assert.ok(m[0].indexOf('BETA-TESTING-ELECTRO.thunder?v=1') > 0 && m[0].indexOf('BETA-TESTING-ELECTRO.thunder?v=1') < m[0].indexOf('sounds/ELECTRO.thunder?v=2'));
  assert.match(m[0], /FIRST_SONG && !projDirty/);
  assert.match(src, /fetch\('sounds\/ELECTRO\.thunder\?v=2'\)[^\n]*loadProjText\(await r\.text\(\), 'ELECTRO\.thunder'\)/);
  assert.match(src, /l:'ELECTRO \(demo project\)'/);
});
test('the service worker pre-caches both songs', () => {
  assert.ok(sw.includes("'./sounds/BETA-TESTING-ELECTRO.thunder?v=1'")); assert.ok(sw.includes("'./sounds/ELECTRO.thunder?v=2'"));
});

const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page: a fresh profile opens the new song; a profile with a saved song gets its own song back', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 }, async () => {
  const { chromium } = await import(PW);
  const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    fs.readFile(path.join(H.ROOT, path.normalize(p)), (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': p.endsWith('.html') ? 'text/html' : p.endsWith('.js') ? 'text/javascript' : p.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream' }); r.end(d); }); });
  await new Promise(ok => srv.listen(0, ok));
  const b = await chromium.launch({ executablePath: ['/usr/bin/google-chrome'].find(f => fs.existsSync(f)), args: ['--autoplay-policy=no-user-gesture-required'] });
  try {
    const c = await b.newContext({ viewport: { width: 1175, height: 760 } }), page = await c.newPage();
    await page.addInitScript(() => { localStorage.setItem('thunderplus-welcome-v1', '1'); localStorage.setItem('thunderplus-seen-build', '9999'); });
    const url = `http://localhost:${srv.address().port}/index.html?seqtest=1`, name = () => page.evaluate(() => window.__seqTest.ev('S.name'));
    await page.goto(url); await page.waitForFunction(() => window.__seqTest && window.__seqTest.ev('S.name') === 'bETA tESTING eLECTRO', null, { timeout: 30000 });
    assert.equal(await name(), 'bETA tESTING eLECTRO', 'a fresh profile opens the new default song');
    await page.waitForTimeout(1500);
    await page.evaluate(() => window.__seqTest.ev("(S.name = 'My own song', saveS(), 1)")); await page.waitForTimeout(1200);
    await page.reload(); await page.waitForFunction(() => !!window.__seqTest); await page.waitForTimeout(3000);
    assert.equal(await name(), 'My own song', 'a returning profile keeps its own last song');
  } finally { await b.close(); srv.close(); }
});
