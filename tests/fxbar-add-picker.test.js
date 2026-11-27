// + ADD FX opens the effect picker (14 Nov, owner: "If you click add FX from the FX bar, can we have it automatically open the FX picker after it pages to the Mixer?").
// Source pins, and (opt-in, TP_BROWSER_TESTS=1) a real page on a computer and a Mobile-sized one: the click goes to the FX screen with the list for the first empty slot open; Esc closes it and stays.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
test('source: the + ADD FX button opens fxsOpen for the first empty slot over the open screen (no goFx / go(fx)); a full track says so in a toast', () => {
  const m = src.match(/const addFx = \(\) => \{[\s\S]*?fxsOpen\(k, null, t\); \};/); assert.ok(m, 'addFx moved');
  assert.match(m[0], /findIndex\(x => !x\)/, 'the first empty slot, read when it is clicked');
  assert.match(m[0], /if \(k < 0\)\{ toast\(/); assert.ok(m[0].indexOf('goFx(') < 0, 'the screen is not changed');
  assert.match(src, /ad\.addEventListener\('click', \(\) => addFx\(\)\)/);
  const c = src.match(/function fxsChoose\(s, k\)\{[\s\S]*?\n\}\n/) || src.match(/function fxsChoose\(s, k\)\{?[\s\S]*?\n\}/); assert.ok(c);
  assert.match(c[0], /const bt = fxp\.bt;/); assert.match(c[0], /if \(bt == null\)\{ cFxS = s;/, 'the FX screen cursor is left alone when picking from the bar');
  assert.match(src, /function fxsOpen\(k, keepType, bt\)/);
});
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const DECK = JSON.stringify({ on: true, scope: true, inst: true, chn: true, fx: true, strip: true, h: 0, ws: 300, wf: 320, fxbar: true, perf: true, perfOn: false });
for (const [name, opts] of [['computer 1440x900', { width: 1440, height: 900 }],
  ['Mobile 344x780', { width: 344, height: 780, mobile: true, storage: { 'thunderplus-platform-v1': 'mobile', 'thunderplus-input-v1': 'keys', 'thunderplus-keyspad-v1': '0', 'thunderplus-deck-v1': DECK } }]]) {
  test('in a real page (' + name + '): ADD FX opens the picker on slot 1 over the open screen; choosing adds the effect and stays; Esc closes it and stays', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 }, async () => {
    process.env.PLAYWRIGHT = PW;
    const { boot, app } = await import('./sweep/ui-lib.mjs');
    const B = await boot(opts);
    try {
      await app(B.page, `go('song'); draw(); 0`); await B.page.waitForTimeout(500);
      const tv0 = await app(B.page, `tv`);
      await B.page.locator('#dkFxBar .dkadd').first().click({ timeout: 5000 });
      await B.page.waitForTimeout(300);
      const a = JSON.parse(await app(B.page, `JSON.stringify({tv, open: fxp.open, slot: fxp.slot, shown: !$('fxPick').hidden, items: $('fxPickList').querySelectorAll('.fpi').length, heads: $('fxPickList').querySelectorAll('.fph').length})`));
      assert.equal(a.tv, tv0); assert.equal(a.open, true); assert.equal(a.slot, 0); assert.equal(a.shown, true); assert.ok(a.items > 20 && a.heads > 2, 'the grouped effect list');
      await B.page.keyboard.press('Escape'); await B.page.waitForTimeout(150);
      const z = JSON.parse(await app(B.page, `JSON.stringify({tv, open: fxp.open, shown: !$('fxPick').hidden})`));
      assert.equal(z.tv, tv0); assert.equal(z.open, false); assert.equal(z.shown, false);
      await B.page.locator('#dkFxBar .dkadd').first().click(); await B.page.waitForTimeout(200);
      await B.page.keyboard.press('ArrowDown'); await B.page.keyboard.press('Enter'); await B.page.waitForTimeout(300);
      const y = JSON.parse(await app(B.page, `JSON.stringify({tv, n: S.tfx[dkTrack()].filter(Boolean).length})`));
      assert.equal(y.tv, tv0); assert.equal(y.n, 1);
    } finally { await B.close(); }
  });
}
