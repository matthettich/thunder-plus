// FX CLEAR EMPTIES THE SLOT UNDER THE CURSOR (14 Nov, owner: right-click the Retrigger in slot 1, Clear: the Compressor below it went).
// Source pins always; the real-page run (desktop and 344x780) is opt-in: TP_BROWSER_TESTS=1.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
test('source: del() on the slot rows empties that slot (never the last one); the mixer chain row keeps its whole-chain meaning', () => {
  assert.match(src, /if \(tv === 'fx' && cs\.fx\.y >= 1 && cs\.fx\.y <= FXSLOTS\) return fxDelSlot\(cFxT, cs\.fx\.y - 1\);/);
  assert.doesNotMatch(src, /if \(tv === 'fx' && cs\.fx\.y >= 1 && cs\.fx\.y <= FXSLOTS\) return fxDelLast/);
  assert.match(src, /function fxDelSlot\(t, k\)\{[\s\S]*?fxSwap\(t, k, A, null\); fxChanged\(t\);/);
});
test('source: a right-click on the FX screen or the Mixer moves the cursor to the clicked cell before the menu is built', () => {
  const m = src.match(/if \(tv === 'fx' \|\| tv === 'mixer'\)\{ const q = e\.target\.closest\('\[data-x\]\[data-y\]'\)[\s\S]*?draw\(\); \} \}\n  const sel = /); assert.ok(m, 'cursor move in cmdCtx');
  assert.ok(src.indexOf(m[0]) < src.indexOf("sxMenu(e.clientX, e.clientY, VNAME[tv]"), 'before the menu');
  assert.match(m[0], /!\(selA && selA\.v === tv\)/, 'a selection stays the target');
});
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const opt = { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 };
for (const [name, o] of [['computer 1440x900', { width: 1440, height: 900 }], ['phone 344x780', { width: 344, height: 780, mobile: true, storage: { 'thunderplus-platform-v1': 'mobile' } }]]) {
  test('in a real page (' + name + '): right-click slot 1 > Clear empties slot 1 only; the keyboard CLEAR on slot 1 does too; the master the same', opt, async () => {
    process.env.PLAYWRIGHT = PW;
    const { boot, app } = await import('./sweep/ui-lib.mjs');
    const B = await boot(o);
    const slots = t => app(B.page, `JSON.stringify((${t} >= NT ? S.mfx : S.tfx[${t}]).map(f => f && f.type).slice(0, 3))`).then(JSON.parse);
    try {
      for (const t of [6, 32]) {
        const setup = async () => { await app(B.page, `(() => { const t = ${t}, A = t >= NT ? S.mfx : S.tfx[t]; A[0] = freshTfx('retrig'); A[1] = freshTfx('comp'); A[2] = freshTfx('eq'); go('fx'); cFxT = t; cFxS = 2; cs.fx.y = 3; draw(); return 1; })()`); await B.page.waitForTimeout(300); };
        await setup();
        const pos = await B.page.evaluate(() => { const s = [...document.querySelectorAll('#tGrid .lb[data-x][data-y="1"]')].find(e => e.offsetWidth); if (!s) return null; const r = s.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
        assert.ok(pos, 'slot 1 row on screen');
        await B.page.mouse.click(pos[0], pos[1], { button: 'right' }); await B.page.waitForTimeout(250);
        assert.equal(await app(B.page, 'cs.fx.y'), 1, 'the cursor went to the clicked slot');
        const item = B.page.locator('.sxmenu button, .sxmenu [role=menuitem], .sxmenu .sxi').filter({ hasText: /^Clear/ }).first();
        await item.click({ timeout: 4000 }); await B.page.waitForTimeout(250);
        assert.deepEqual(await slots(t), [null, 'comp', 'eq'], 'track ' + t + ': only slot 1 cleared');
        await setup(); await app(B.page, 'cs.fx.y = 1, draw(), cmdAction("clr")'); await B.page.waitForTimeout(150);
        assert.deepEqual(await slots(t), [null, 'comp', 'eq'], 'keyboard CLEAR on slot 1');
      }
    } finally { await B.close(); }
  });
}
