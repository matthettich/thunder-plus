// Screenshots of the Scope in both editors (14 Nov 2026): 1440x900 and 344x780 (touch), white and IDM pink & green, Open / Compact / Mini.
//   SHOTS=/dir node tests/sweep/scope-shots.mjs      (a sine on A, a square LFO on B)
import { boot } from './ui-lib.mjs'; import fs from 'fs';
const OUT = process.env.SHOTS || '/home/claude/work/progress/g3-shots'; fs.mkdirSync(OUT, { recursive: true });
const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp) => ({ from: [a, ap], to: [b, bp], bend: 0 });
const lf = [P('l', 'lfo', 0, 360, { rate: 8, shape: 0, depth: 1 }), P('q', 'lfo', 0, 620, { rate: 4, shape: 1, depth: 1 }), P('s', 'scope', 330, 300, { time: .5, trig: 1, level: 0 })];
const SPEC = {
  chains: { key: 'chains.patch', sk: 'chains.settings', mods: [P('i', 'trackIn', 0, 0, { track: 1 }), P('o', 'chainOut', 300, 0, { track: 1 }), ...lf], cab: [C('i', 'out', 'o', 'in'), C('l', 'out', 's', 'a'), C('q', 'out', 's', 'b')] },
  waves: { key: 'waves.patch', sk: 'waves.settings', mods: [P('h', 'hostIn', 0, 0), P('o', 'out', 300, 0, { slot: 1, level: 1 }), ...lf], cab: [C('l', 'out', 's', 'a'), C('q', 'out', 's', 'b')] },
};
const skinCss = `:root{--bench:#0b0710; --panel:#0b0710; --panel-2:#1c1224; --edge:#ff2d8a; --ink:#ff2d8a; --muted:#c070a0; --track:#1c1224; --side:#140c1b; --frame:#140c1b; --video:#39ff88; --audio:#39ff88; --audio-dark:#11a85a; --mod:#ff2d8a; --mod-dark:#a01058; --gate:#ff2d8a; --gate-dark:#a01058; --selc:#fff200}
  :is(.tb,.pgroup,.pitem,.numsmall,select,.btn,button,input[type=text],input[type=search],input[type=number],textarea){background:#0b0710!important; color:#ff2d8a; border-color:#ff2d8a!important}
  :is(.tb.on,button.on){background:#ff2d8a!important; color:#0b0710!important}`;
for (const [vw, vh, mob, tag] of [[1440, 900, false, 'd'], [344, 780, true, 'm']]) {
  const B = await boot({ width: vw, height: vh, mobile: mob }); const { page } = B, base = 'http://127.0.0.1:' + B.port;
  for (const ed of ['chains', 'waves']) for (const skin of ['white', 'idmpg']) {
    const S = SPEC[ed]; const doc = { meta: { name: 'scope' }, view: { x: mob ? 8 : 20, y: mob ? -250 : -240, zoom: mob ? .9 : 1, ui: 2, nw: 1 }, modules: S.mods, cables: S.cab, groups: [], custom: [], transport: { tempo: 120 } };
    const url = base + '/' + ed + '/index.html'; await page.goto(url);
    await page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({ tips: false })); }, [S.key, S.sk, doc]);
    await page.goto(url); await page.waitForSelector('.node[data-id="s"]', { timeout: 20000 });
    if (ed === 'waves') { await page.mouse.click(vw / 2, vh - 30); await page.keyboard.press('z'); }
    if (skin === 'idmpg') await page.addStyleTag({ content: skinCss });
    await page.waitForTimeout(1800);
    for (const st of ['open', 'compact', 'mini']) {
      if (st === 'compact') { await page.mouse.move(vw / 2, vh - 20); await page.keyboard.press('Alt+c'); }
      if (st === 'mini') { await page.mouse.move(vw / 2, vh - 20); await page.keyboard.press('Alt+Shift+C'); }
      await page.waitForTimeout(900);
      await page.screenshot({ path: `${OUT}/${ed}-${tag}-${skin}-${st}.png` });
      if (st === 'open' || st === 'compact') { const n = page.locator('.node[data-id="s"]'); await n.screenshot({ path: `${OUT}/${ed}-${tag}-${skin}-${st}-module.png` }).catch(() => {}); }
    }
  }
  await B.close();
}
console.log('done');
