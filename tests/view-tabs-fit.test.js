// VIEW TABS (14 Nov, owner, folded Fold 6 cover screen ~344 px: the tabs read "Tracker | Synth | Chai", Chains cut, with Waves hidden):
// three tabs fell into the "scroll inside itself" rule, and four tabs made the whole top bar wider than the screen (build 578 measured the tabs, not the bar).
// Three or more tabs now take their own full row at 460 px and under. Static pins, then (opt-in, TP_BROWSER_TESTS=1) a real page at 320-412 px with 3 and 4 tabs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');

test('view tabs: with 3 or more tabs on a phone (460 px and under) the switch is a full row of its own, never scrolls, and shares the row out', () => {
  const m = src.match(/@media \(max-width:460px\)\{ body\.mob \.top\{flex-wrap:wrap\}\n\s*body\.mob \.top #segView:has\(button:nth-child\(3\)\)\{([^}]*)\}\n\s*body\.mob \.top #segView:has\(button:nth-child\(3\)\) button\{([^}]*)\} \}/);
  assert.ok(m, 'the full-row rule for 3+ tabs is there');
  assert.match(m[1], /flex:1 1 100%/); assert.match(m[1], /overflow:visible/); assert.match(m[2], /flex:1 1 auto/); assert.match(m[2], /padding-inline:6px!important/);
  // it comes after the older rules it must win over (same or lower specificity), and the two-tab switch is still left beside the bar
  const at = re => { const i = src.search(re); assert.ok(i > 0, 'not found ' + re); return i; };
  const mine = at(/body\.mob \.top #segView:has\(button:nth-child\(3\)\)\{flex:1 1 100%/);
  for (const re of [/body\.mob \.top #segView\{flex:0 1 auto; min-width:0; overflow-x:auto/, /body\.mob \.top #segView:has\(button:nth-child\(4\)\) button\{padding-inline:3px!important/, /body\.mob \.top #segView:has\(button:nth-child\(4\)\) button\{font-size:10\.5px/, /body\.mob \.top #segView button\{padding-inline:3px!important; letter-spacing:0\}/])
    assert.ok(at(re) < mine, 'after ' + re);
  assert.match(src, /body\.mob \.top #segView:not\(:has\(button:nth-child\(3\)\)\)\{flex:none; overflow:visible\}/, 'the two-tab switch is as before');
});

let PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('view tabs: in a real page every label reads in full and the whole top bar stays on screen at 320-412 px with 3 and 4 tabs, in several skins', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1 (boots Chrome several times, about a minute; kept out of npm test)' : !PW ? 'playwright not installed' : false, timeout: 300000 }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot } = await import('./sweep/ui-lib.mjs');
  const MCK = src.match(/MCK = '([^']+)'/)[1], MWK = src.match(/MWK = '([^']+)'/)[1];
  const skins = ['', 'matrix', 'wes', 'idm', 'lowpower', 'swiss', 'neon'], bad = [];
  for (const w of [320, 344, 360, 390, 412]) for (const [wv, ch] of [[0, 1], [1, 1]]) {   // 3 tabs (Waves hidden) and 4
    const B = await boot({ width: w, height: 800, mobile: true, storage: { 'thunderplus-platform-v1': 'mobile', [MCK]: '1', [MWK]: wv ? '1' : '0' } });
    try {
      for (const sk of skins) {
        const r = await B.page.evaluate(async sk => { if (sk) document.documentElement.dataset.skin = sk; await new Promise(r => setTimeout(r, 80));
          const s = document.getElementById('segView'), bs = [...s.querySelectorAll('button')];
          return { n: bs.length, words: bs.map(b => b.textContent).join(' '), clip: bs.filter(b => b.scrollWidth > b.clientWidth + 0.5 || b.getBoundingClientRect().right > innerWidth + 0.5).map(b => b.textContent),
            scroll: s.scrollWidth > s.clientWidth + 0.5, bar: Math.max(...[...document.querySelectorAll('.top > *')].filter(e => e.offsetParent).map(e => e.getBoundingClientRect().right)), vw: innerWidth }; }, sk);
        const want = wv ? 'Tracker Synth Waves Chains' : 'Tracker Synth Chains';
        if (r.words !== want || r.clip.length || r.scroll || r.bar > w + 0.5 || r.vw !== w) bad.push(`${w}px ${wv ? 4 : 3} tabs skin "${sk}": ${JSON.stringify(r)}`);
      }
    } finally { await B.close(); }
  }
  assert.deepEqual(bad, []);
});
