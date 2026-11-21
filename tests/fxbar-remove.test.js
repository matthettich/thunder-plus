// FX BAR: remove an effect (14 Nov, owner). Right-click / long press on a card opens a menu with "Remove effect"; the on light, the name click and the header drag stay.
// Source pins always; the real-page run (computer and 344x780) is opt-in: TP_BROWSER_TESTS=1.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
test('source: the menu, the removal through fxSwap + fxChanged (one saveS = one undo step), the toast, long press, and the unchanged gestures', () => {
  assert.match(src, /const rmFx = \(\) => \{[\s\S]*?fxSwap\(tt, k, AA, null\);[\s\S]*?fxChanged\(tt\);[\s\S]*?toast\(`Removed \$\{nmx\} from /);
  assert.match(src, /\{l:'Remove effect', f:rmFx\}/);
  assert.match(src, /col\.addEventListener\('contextmenu', e => \{ if \(e\._valMenu/);
  assert.match(src, /e\.pointerType !== 'touch'[\s\S]*?setTimeout\(\(\) => \{ done = true;/, 'long press');
  assert.match(src, /on\.addEventListener\('click', \(\) => \{ const g = FA\(\)\[k\]; if \(!g\) return; g\.on = !g\.on;/, 'on / off unchanged');
  assert.match(src, /nm\.addEventListener\('click', \(\) => \{ if \(!nm\._noClick\) goFx\(k\); \}\);/, 'name click unchanged');
  assert.match(src, /h\.addEventListener\('pointerdown', e => \{ if \(e\.button \|\| e\.target === on\) return;/, 'drag unchanged');
});
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const DECK = JSON.stringify({ on: true, scope: true, inst: true, chn: true, fx: true, strip: true, h: 0, ws: 300, wf: 320, fxbar: true, perf: true, perfOn: false });
const opt = { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 };
for (const [name, o] of [['computer 1440x900', { width: 1440, height: 900 }],
  ['phone 344x780', { width: 344, height: 780, mobile: true, storage: { 'thunderplus-platform-v1': 'mobile', 'thunderplus-input-v1': 'keys', 'thunderplus-keyspad-v1': '0', 'thunderplus-deck-v1': DECK } }]]) {
  test('in a real page (' + name + '): right-click the middle card > Remove effect takes out that one only; Undo brings it back', opt, async () => {
    process.env.PLAYWRIGHT = PW;
    const { boot, app } = await import('./sweep/ui-lib.mjs');
    const B = await boot(o);
    const slots = () => app(B.page, `JSON.stringify(S.tfx[6].map(f => f && f.type).slice(0, 4))`).then(JSON.parse);
    try {
      await app(B.page, `(() => { const A = S.tfx[6]; A[0] = freshTfx('retrig'); A[1] = freshTfx('comp'); A[2] = freshTfx('eq'); cTrack = 6; cs.song.x = 6; go('song'); saveS(); draw(); return 1; })()`); await B.page.waitForTimeout(700);
      const card = B.page.locator('#dkFxBar .dkfc[data-k="1"] .fch, #dkFxBar .dkfc:nth-child(2) .fch').first();
      await card.scrollIntoViewIfNeeded({ timeout: 4000 }).catch(() => {});
      await card.click({ button: 'right', position: { x: 40, y: 8 }, timeout: 5000 }); await B.page.waitForTimeout(250);
      await B.page.locator('.sxmenu button').filter({ hasText: /^Remove effect/ }).first().click({ timeout: 4000 }); await B.page.waitForTimeout(300);
      assert.deepEqual(await slots(), ['retrig', null, 'eq', null]);
      assert.match(await app(B.page, `$('toast').textContent`), /Removed .* from track 07/);
      await app(B.page, 'undoStep(-1), 1'); await B.page.waitForTimeout(300);
      assert.deepEqual(await slots(), ['retrig', 'comp', 'eq', null], 'one undo brings it back');
    } finally { await B.close(); }
  });
}
