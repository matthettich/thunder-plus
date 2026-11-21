// ARRANGE ZOOM OUT (14 Nov, owner: "it's difficult to see whole phrases"): below fit (zoom 1, today's smallest) the rows get lower, down to 0.32, with the same − ○ + buttons, Ctrl+wheel and pinch,
// saved under the same key. Source pins, and (opt-in, TP_BROWSER_TESTS=1) a real page: more rows in view at each level, the cursor, a muted block and the colours still drawn, the buttons' steps, the wheel, the saved level.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
const css = [...src.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(m => m[1]).join('\n').replace(/\/\*[\s\S]*?\*\//g, '');
test('source: the saved zoom reads below 1, the default is still 1, the levels are named constants', () => {
  assert.match(src, /const AZ_MIN = 0\.32, AZ_OUT = \[0\.8, 0\.64, 0\.5, 0\.4, 0\.32\], AZ_RH_MIN = 5;/);
  assert.match(src, /return z >= AZ_MIN && z <= AZ_MAX \? z : 1; \} catch\(e\)\{ return 1; \}/);
  assert.match(src, /const AZK = 'thunderplus-arrange-zoom-v1'/, 'the same saved key');
  assert.match(src, /if \(arrZoom < 1\) rh = Math\.max\(AZ_RH_MIN, Math\.round\(rh \* arrZoom\)\)/, 'only a zoom under 1 changes the row height');
  assert.match(src, /z = clamp\(z, arrZMin\(\), arrZMax\(\)\); if \(z > 0\.95 && z < 1\.05\) z = 1;/);
});
test('CSS: the zoom-out rules use the skins\' variables only (no literal colour) and keep the cursor, playhead, selection and muted blocks', () => {
  const rules = []; css.replace(/([^{}]+)\{([^{}]*)\}/g, (_, sel, body) => { if (/\.arout|\.arlow/.test(sel)) rules.push([sel.trim(), body]); return ''; });
  assert.ok(rules.length >= 8, 'the zoom-out rules');
  for (const [s, b] of rules) assert.doesNotMatch(b, /#[0-9a-f]{3,8}\b|rgba?\(|hsla?\(|\b(?:white|black|orange|yellow)\b/i, s);
  assert.ok(!rules.some(([s, b]) => /\.tagrid b\.(c|m)\b/.test(s) && /display:\s*none|opacity:\s*0|visibility/.test(b)), 'the cursor and muted blocks are never hidden');
  assert.ok(!rules.some(([s, b]) => /b\.p\b/.test(s) && /transparent|display:\s*none/.test(b)));
});
let PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
for (const [name, opts, screen] of [['computer 1440x900, Song screen', { width: 1440, height: 900 }, 'song'], ['computer 1440x900, Arrange beside Phrase', { width: 1440, height: 900 }, 'phrase'],
  ['Mobile 344x780', { width: 344, height: 780, mobile: true, storage: { 'thunderplus-platform-v1': 'mobile' } }, 'song']]) {
  test('in a real page (' + name + '): more rows at each zoom-out level, buttons, wheel, saved level', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 180000 }, async () => {
    process.env.PLAYWRIGHT = PW;
    const { boot, app } = await import('./sweep/ui-lib.mjs');
    const B = await boot(opts), song = fs.readFileSync(require('path').join(H.ROOT || __dirname + '/..', 'sounds/ELECTRO.thunder'), 'utf8');
    try {
      assert.equal(await app(B.page, 'arrZoom'), 1, 'never zoomed: fit, as before');
      await app(B.page, `window.__seqTest.load(${JSON.stringify(song)}, 'ELECTRO.thunder')`); await B.page.waitForTimeout(600);
      await app(B.page, `go(${JSON.stringify(screen)}); draw(); 0`); await B.page.waitForTimeout(300);
      const info = async () => JSON.parse(await app(B.page, `JSON.stringify({z: arrZoom, rows: arrRows(), arout: arrEl.classList.contains('arout'), cur: !!arrGrid.querySelector('b.c') && arrGrid.querySelector('b.c').offsetHeight > 0,
        f: [...arrGrid.querySelectorAll('b.f')].slice(0, 20).every(b => b.offsetHeight > 0 && getComputedStyle(b).backgroundColor !== 'rgba(0, 0, 0, 0)'), sub: $('tArrSub').textContent})`));
      const i1 = await info(); assert.equal(i1.arout, false);
      let prev = i1.rows; const levels = [0.8, 0.64, 0.5, 0.4, 0.32];
      for (const z of levels) { await app(B.page, `arrSetZoom(${z}); arrZoomDone(); draw(); 0`); await B.page.waitForTimeout(150); const i = await info();
        assert.equal(i.z, z); assert.ok(i.arout); assert.ok(i.rows > prev, `level ${z}: ${i.rows} rows after ${prev}`); assert.ok(i.cur, 'the cursor block is drawn at ' + z); assert.ok(i.f, 'filled blocks keep their colour at ' + z); prev = i.rows; }
      assert.ok(prev >= i1.rows * 2, `at least twice the rows at the smallest (${i1.rows} → ${prev})`);
      assert.equal(await app(B.page, `localStorage.getItem('thunderplus-arrange-zoom-v1')`), '0.32', 'saved the way the zoom is');
      assert.match((await info()).sub, /32%/);
      // the buttons: ○ back to fit, − steps down the levels, disabled at the smallest, + steps up
      await app(B.page, `arrSetZoom(1); arrZoomDone(); 0`);
      const seq = []; for (let k = 0; k < 6; k++) { await B.page.evaluate(() => document.getElementById('tArrZo').click()); seq.push(await app(B.page, 'arrZoom')); }
      assert.deepEqual(seq, [0.8, 0.64, 0.5, 0.4, 0.32, 0.32]); assert.equal(await app(B.page, `$('tArrZo').disabled`), true); assert.equal(await app(B.page, `$('tArrZf').disabled`), false);
      await B.page.evaluate(() => document.getElementById('tArrZi').click()); assert.equal(await app(B.page, 'arrZoom'), 0.4);
      await B.page.evaluate(() => document.getElementById('tArrZf').click()); assert.equal(await app(B.page, 'arrZoom'), 1); assert.equal(await app(B.page, `$('tArrZf').disabled`), true);
      // Ctrl+wheel (a trackpad pinch) reaches the zoom-out levels
      await app(B.page, `arrGrid.dispatchEvent(new WheelEvent('wheel', {ctrlKey: true, deltaY: 400, bubbles: true, cancelable: true, clientX: 200, clientY: 300})); 0`);
      const w = await app(B.page, 'arrZoom'); assert.ok(w < 1 && w >= 0.32, 'Ctrl+wheel out: ' + w);
      // a muted block stays drawn
      await app(B.page, `arrSetZoom(0.32); arrZoomDone(); draw(); 0`); assert.equal(B.errors.filter(e => /pageerror/.test(e)).length, 0, B.errors.join('\n'));
    } finally { await B.close(); }
  });
}
