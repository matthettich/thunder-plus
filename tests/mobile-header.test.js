// MOBILE HEADER (12 Nov, owner): in the Mobile setup with PAD input the top TRANSP − / + are hidden (the pad's SEMI − / + do the same move) and
// BPM, LPB, ticks, ROWS and REC share the tab row. Static pins on index.html, then (when Playwright and Chrome are here) a real page.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');

test('mobile header: TRANSP hides only in body.mob.inpad, and the pad SEMI keys call the same transpose', () => {
  assert.match(src, /body\.mob\.inpad #tXposeDn, body\.mob\.inpad #tXposeUp\{display:none\}/, 'TRANSP − / + hidden in Mobile + PAD');
  assert.match(src, /function padHdr\(\)\{ document\.body\.classList\.toggle\('inpad', inMode === 'pad'\); \}/, 'body.inpad follows the input mode');
  assert.match(src, /function setInput\(m\)\{[^\n]*\n[^\n]*padHdr\(\);/, 'and it is kept in step when the input changes');
  assert.match(src, /\$\('tXposeDn'\)\.addEventListener\('click', \(\) => transposeSel\(-1, true\)\)/, 'the hidden button is the same move (selection needed)');
  assert.match(src, /\.ttabbar\.tcompact \.ttabs button\[data-v=project\]::before\{content:'PROJ'/, 'PROJ when the row is tight');
  assert.match(src, /b\.setAttribute\('aria-label', 'Project'\)/, 'PROJECT keeps its name for readers');
});

let PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('mobile header: in a real page TRANSP is hidden in Mobile PAD only, the group sits in the tab row, nothing scrolls', { skip: !PW || !fs.existsSync('/usr/bin/google-chrome') && 'no Playwright / Chrome here' }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot, app } = await import('./sweep/ui-lib.mjs');
  const vis = id => app0.page.evaluate(i => { const e = document.getElementById(i); return !!(e && e.offsetParent); }, id);
  let app0;
  async function at(w, h, platform, input) {
    const B = await boot({ width: w, height: h, mobile: platform === 'mobile', storage: { 'thunderplus-platform-v1': platform, 'thunderplus-input-v1': input } });
    app0 = B; await app(B.page, `(setInput('${input}'), setView('tracker'), go('phrase'), 1)`); await B.page.waitForTimeout(500); return B;
  }
  const IK = await (async () => { const m = src.match(/const IK = '([^']+)'/); return m ? m[1] : null; })();
  assert.ok(IK, 'input storage key found');
  for (const [w, h] of [[884, 1020], [360, 800]]) {
    let B = await at(w, h, 'mobile', 'pad');
    try {
      assert.equal(await vis('tXposeDn'), false, `${w}: TRANSP − hidden in Mobile PAD`); assert.equal(await vis('tXposeUp'), false, `${w}: TRANSP + hidden`);
      const g = await B.page.evaluate(() => { const r = id => document.getElementById(id).getBoundingClientRect(), t = document.getElementById('tTabs').getBoundingClientRect();
        const ids = ['tClk', 'tRowsDn', 'tRowsV', 'tRowsUp', 'tRec'], rs = ids.map(r);
        return { order: rs.every((x, i) => !i || x.left >= rs[i - 1].left - 1), tabsTop: t.top, tabsRight: t.right, grpTop: rs[0].top, grpLeft: rs[0].left, h: Math.min(...rs.map(x => x.height)),
          scroll: document.documentElement.scrollWidth > innerWidth, tabsScroll: document.getElementById('tTabs').scrollWidth > document.getElementById('tTabs').clientWidth + 1 }; });
      assert.ok(g.order, `${w}: BPM, ROWS and REC in that order`); assert.ok(g.h >= 39, `${w}: tap targets ${g.h}px`); assert.ok(!g.scroll && !g.tabsScroll, `${w}: nothing scrolls sideways`);
      if (w > 800) assert.ok(g.grpTop < g.tabsTop + 40 && g.grpLeft >= g.tabsRight - 1, '884: the group is in the tab row, right of the tabs');
    } finally { await B.close(); }
  }
  for (const [platform, input, w, h] of [['mobile', 'keys', 884, 1020], ['desktop', 'pad', 1280, 800], ['desktop', 'keys', 1600, 1000]]) {
    const B = await at(w, h, platform, input);
    try { assert.equal(await vis('tXposeDn'), true, `${platform} ${input}: TRANSP is on screen`); } finally { await B.close(); }
  }
});
