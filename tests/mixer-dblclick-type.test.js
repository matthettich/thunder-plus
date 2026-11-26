// MIXER: a double-click on a value types it instead of resetting (15 Nov, owner: "double click on a mixer parameter (like pan) should allow for manual entry").
// Source pins always; the real-page run (computer, opt-in TP_BROWSER_TESTS=1) double-clicks the strip's PAN and the strip's VOL.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
test('source: double-click types (fader, OUT bar, grid cells, strip VOL / PAN); Reset stays on Alt-click and the right-click menu', () => {
  assert.match(src, /e\.stopPropagation\(\);\n  mfType\(\+b\.dataset\.t\); \}, true\);/, 'fader dblclick types');
  assert.match(src, /if \(resetClick\(e\)\)\{ moReset\(\); return; \}[\s\S]{0,120}if \(two\)\{[^}]*moType\(\); return; \}/, 'OUT bar');
  assert.match(src, /if \(dbl\)\{ keyType\(\); return; \}/, 'grid cell');
  assert.match(src, /stVol\.addEventListener\('dblclick', stVolType\);/); assert.match(src, /stPan\.addEventListener\('dblclick', panType\);/);
  assert.match(src, /valMenu\(e, 'Master OUT', \{reset:moReset, type:moType\}\)/); assert.match(src, /\{reset:stReset, type:/); assert.match(src, /\{reset:dkMst\(\) \? null : panReset, type:dkMst\(\) \? null : panType\}/);
  assert.match(src, /if \(resetClick\(e\)\)\{ mfReset\(t\); return; \}/, 'Alt-click reset on the fader stays');
});
test('panType parses L50 / R30 / C / hex the way the readout reads', () => {
  const m = src.match(/async function panType\(\)\{[\s\S]*?\n  if \(v == null\)/); assert.ok(m);
  const body = m[0].match(/const x = a\.trim\(\)[\s\S]*?\n  if \(v == null\)/)[0].replace(/\n  if \(v == null\)$/, '');
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const f = new Function('a', 'clamp', body + '; return v;');
  assert.equal(f('c', clamp), 128); assert.equal(f('L100', clamp), 0); assert.equal(f('r100', clamp), 255); assert.equal(f('L50', clamp), 64); assert.equal(f('A0', clamp), 160); assert.equal(f('zz', clamp), null);
});
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page (computer 1440x900): double-clicking the strip PAN and VOL opens a typed entry, Enter sets the pan', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot, app } = await import('./sweep/ui-lib.mjs');
  const DECK = JSON.stringify({ on: true, scope: true, inst: true, chn: true, fx: true, strip: true, h: 0, ws: 300, wf: 320, fxbar: true, perf: true, perfOn: false });
  const B = await boot({ width: 1440, height: 900, storage: { 'thunderplus-deck-v1': DECK } }), P = B.page;
  try {
    await app(P, `go('song'); draw(); 0`); await P.waitForTimeout(500);
    await P.locator('#stPan').dblclick({ timeout: 5000 }); await P.waitForTimeout(250);
    assert.equal(await P.locator('.ask').count(), 1, 'typed entry opened');
    await P.locator('.ask input').fill('L50'); await P.keyboard.press('Enter'); await P.waitForTimeout(250);
    assert.equal(await app(P, `S.mix[dkTrack()].pan`), 64);
    await P.locator('#stVol').dblclick({ position: { x: 5, y: 40 }, timeout: 5000 }); await P.waitForTimeout(250);
    assert.equal(await P.locator('.ask').count(), 1, 'VOL typed entry opened'); await P.keyboard.press('Escape'); await P.waitForTimeout(150);
    await app(P, `go('mixer'); draw(); 0`); await P.waitForTimeout(500);
    const mf = P.locator('.mfbar:not(.mobar)').first(); await mf.dblclick({ timeout: 5000 }); await P.waitForTimeout(250);
    assert.equal(await P.locator('.ask').count(), 1, 'fader typed entry opened'); await P.keyboard.press('Escape'); await P.waitForTimeout(150);
    await P.locator('#tGrid [data-x="2"][data-y="1"], #grid [data-x="2"][data-y="1"]').first().dblclick({ timeout: 5000 }); await P.waitForTimeout(250);
    assert.equal(await P.locator('.ask').count(), 1, 'PAN cell typed entry opened');
  } finally { await B.close(); }
});
