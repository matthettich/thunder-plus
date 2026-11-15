// FX SCREEN: with a long effect the wheel goes on past the top of the controls onto the slot list, and the open slot's ACTIVE line stays pinned (15 Nov, owner, build 609).
// Source pins always; the real-page run (computer, opt-in TP_BROWSER_TESTS=1) wheels up and down on a long effect.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
test('source: the wheel steps from the first control onto the open slot, from the last slot onto the controls; ACTIVE is a sticky line', () => {
  const m = src.match(/WHEEL REACHES THE SLOT LIST[\s\S]*?c\.y = ny != null \? ny : clamp\(c\.y \+ n, lo, hi\);/); assert.ok(m, 'wheel block');
  assert.match(m[0], /c\.y \+ n <= FXSLOTS\) ny = clamp\(cFxS \+ 1, 0, FXSLOTS\)/); assert.match(m[0], /c\.y \+ n > FXSLOTS\) ny = Math\.min\(FXSLOTS \+ 1, full\)/);
  assert.match(src, /y === FXSLOTS \+ 1\) L\[L\.length - 1\]\.fxact = true/); assert.match(src, /\.ln\.fxact\{position:sticky; top:0;/);
});
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page (computer 800x500): wheel up from the first control reaches the slot list; the ACTIVE line is pinned when scrolled', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot, app } = await import('./sweep/ui-lib.mjs');
  const B = await boot({ width: 1000, height: 520 }), P = B.page;
  try {
    await app(P, `(() => { go('fx'); cFxT = 2; cFxS = 1; cs.fx.y = FXSLOTS + 1; let best = 0, bn = 0; for (const k of Object.keys(TFX)){ try { S.tfx[2][1] = freshTfx(k); draw(); const n = rowsOf('fx').length; if (n > bn){ bn = n; best = k; } } catch(e){} } S.tfx[2][1] = freshTfx(best); cs.fx.y = FXSLOTS + 1; draw(); return bn; })()`); await P.waitForTimeout(500);
    const info = JSON.parse(await app(P, `JSON.stringify({y: cs.fx.y, rows: rowsOf('fx').length})`)); assert.ok(info.rows > 8, 'a long effect');
    const box = await P.locator('#tGrid').boundingBox(); await P.mouse.move(box.x + 100, box.y + box.height - 50);
    for (let i = 0; i < 25; i++){ await P.mouse.wheel(0, 100); await P.waitForTimeout(60); }
    const down = await app(P, `cs.fx.y`); const FS = +(await app(P, `FXSLOTS`)); assert.ok(down > FS + 2, 'moved down in the controls: y=' + down);
    const pos = await P.evaluate(() => { const a = document.querySelector('#tGrid .ln.fxact'), g = document.querySelector('#tGrid'); return a ? getComputedStyle(a).position + ':' + Math.round(a.getBoundingClientRect().top - g.getBoundingClientRect().top) : null; });
    assert.match(pos, /^sticky:/); assert.ok(+pos.split(':')[1] < 30, 'pinned near the top: ' + pos);
    for (let i = 0; i < 30; i++){ await P.mouse.wheel(0, -100); await P.waitForTimeout(40); }
    const y = +(await app(P, `cs.fx.y`)); assert.ok(y <= FS, 'the cursor reached the slot list, y=' + y);
  } finally { await B.close(); }
});
