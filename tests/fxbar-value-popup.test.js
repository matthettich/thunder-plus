// FX BAR: a double-click on a parameter value opens a pop-up slider with the number box (15 Nov, owner, "the cut effect ... high cut").
// Source pins always; the real-page run (computer, opt-in TP_BROWSER_TESTS=1) double-clicks a value, drags the bar, types a value, Esc / outside close.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
test('source: dblclick opens fbPop (slider + number box, live set, Shift fine, Enter / Esc / outside), reset and the menu stay', () => {
  assert.match(src, /v\.addEventListener\('dblclick', e => \{ e\.preventDefault\(\); fbPop\(v, q, getF, show, set(?:, cur)?\); \}\);/);
  const m = src.match(/function fbPop\(v, q, getF, show, set(?:, cur)?\)\{[\s\S]*?\n\}\n/); assert.ok(m, 'fbPop');
  assert.match(m[0], /dragMul\(ev\)/, 'Shift fine'); assert.match(m[0], /fbParse\(a, q, show\)/, 'typed entry'); assert.match(m[0], /e\.key === 'Escape'/); assert.match(m[0], /addEventListener\('pointerdown', m\._out, true\)/);
  assert.match(src, /if \(resetClick\(e\)\)\{ set\(q\.d\); return; \}/, 'Alt-click reset stays');
  assert.match(src, /v\.addEventListener\('contextmenu', e => valMenu\(e, q\.l, \{reset:/, 'right-click menu stays');
  assert.ok(src.indexOf('let fbPopEl') < src.indexOf('function fbPop('), 'declared before use');
});
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const opt = { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 };
test('in a real page (computer 1440x900): double-click a value, drag the bar, type, Esc and outside close', opt, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot, app } = await import('./sweep/ui-lib.mjs');
  const DECK = JSON.stringify({ on: true, scope: true, inst: true, chn: true, fx: true, strip: true, h: 0, ws: 300, wf: 320, fxbar: true, perf: true, perfOn: false });
  const B = await boot({ width: 1440, height: 900, storage: { 'thunderplus-deck-v1': DECK } });
  const P = B.page, val = () => app(P, `S.tfx[6][0] ? JSON.stringify(Object.values(S.tfx[6][0]).filter(x => typeof x === 'number')) : '[]'`).then(JSON.parse);
  try {
    await app(P, `(() => { S.tfx[6][0] = freshTfx('cut'); cTrack = 6; cs.song.x = 6; go('song'); saveS(); draw(); return 1; })()`); await P.waitForTimeout(700);
    const v = P.locator('#dkFxBar .dkfc[data-k="0"] .fr span').first();
    const before = await val();
    await v.dblclick({ timeout: 5000 }); await P.waitForTimeout(200);
    assert.equal(await P.locator('.fbpop').count(), 1); assert.equal(await P.locator('.fbpop input.fbnum').count(), 1);
    const bar = await P.locator('.fbpop .fbbar').boundingBox();
    await P.mouse.move(bar.x + 2, bar.y + bar.height / 2); await P.mouse.down(); await P.mouse.move(bar.x + bar.width - 2, bar.y + bar.height / 2, { steps: 6 });
    assert.notDeepEqual(await val(), before, 'live while dragging'); await P.mouse.up();
    await P.locator('.fbpop input.fbnum').fill('0'); await P.keyboard.press('Enter'); await P.waitForTimeout(150);
    assert.equal(await P.locator('.fbpop').count(), 0, 'Enter applies and closes');
    await v.dblclick(); await P.waitForTimeout(150); await P.keyboard.press('Escape'); assert.equal(await P.locator('.fbpop').count(), 0);
    await v.dblclick(); await P.waitForTimeout(150); await P.mouse.click(5, 300); assert.equal(await P.locator('.fbpop').count(), 0, 'click outside closes');
  } finally { await B.close(); }
});
