// Screenshots of the Expression module in both editors (14 Nov 2026): 1440x900 and 344x780 (touch); a valid formula, an error, Compact.
//   SHOTS=/dir node tests/sweep/expr-shots.mjs
import { boot } from './ui-lib.mjs'; import fs from 'fs';
const OUT = process.env.SHOTS || '/home/claude/work/progress/g6-shots'; fs.mkdirSync(OUT, { recursive: true });
const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp) => ({ from: [a, ap], to: [b, bp], bend: 0 });
const X = { ...P('x', 'expr', 20, 260, { k1: 1.5 }), text: 'tanh(a * (1 + k1 * 4))' };
const SPEC = {
  chains: { key: 'chains.patch', sk: 'chains.settings', mods: [P('i', 'trackIn', 20, 0, { track: 1 }), P('o', 'chainOut', 330, 0, { track: 1 }), X], cab: [C('i', 'out', 'o', 'in'), C('i', 'out', 'x', 'a')] },
  waves: { key: 'waves.patch', sk: 'waves.settings', mods: [P('h', 'hostIn', 20, 0), P('o', 'out', 330, 0, { slot: 1, level: 1 }), X], cab: [C('h', 'pitch', 'x', 'a')] },
};
for (const [vw, vh, mob, tag] of [[1440, 900, false, 'd'], [344, 780, true, 'm']]) {
  const B = await boot({ width: vw, height: vh, mobile: mob }); const { page } = B, base = 'http://127.0.0.1:' + B.port;
  for (const ed of ['chains', 'waves']) {
    const S = SPEC[ed], doc = { meta: { name: 'expr' }, view: { x: mob ? 8 : 20, y: mob ? -200 : -150, zoom: mob ? .9 : 1, ui: 2, nw: 1 }, modules: S.mods, cables: S.cab, groups: [], custom: [], transport: { tempo: 120 } };
    const url = base + '/' + ed + '/index.html'; await page.goto(url);
    await page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({ tips: false })); }, [S.key, S.sk, doc]);
    await page.goto(url); await page.waitForSelector('.node[data-id="x"] textarea.extext', { timeout: 20000 });
    if (ed === 'waves') { await page.mouse.click(vw / 2, vh - 30); await page.keyboard.press('z'); }
    await page.waitForTimeout(1200);
    await page.screenshot({ path: `${OUT}/${ed}-${tag}-valid.png` });
    const ta = page.locator('.node[data-id="x"] textarea.extext'); await ta.click(); await page.keyboard.press('Control+a'); await page.keyboard.type('sin(1 + '); await page.keyboard.press('Enter');
    await page.waitForTimeout(1500); await page.screenshot({ path: `${OUT}/${ed}-${tag}-error.png` });
    await page.locator('.node[data-id="x"]').screenshot({ path: `${OUT}/${ed}-${tag}-error-module.png` }).catch(() => {});
    await ta.click(); await page.keyboard.press('Control+a'); await page.keyboard.type('tanh(a * (1 + k1 * 4))'); await page.keyboard.press('Enter'); await page.waitForTimeout(900);
    await page.mouse.move(vw / 2, vh - 20); await page.keyboard.press('Alt+c'); await page.waitForTimeout(900);
    await page.screenshot({ path: `${OUT}/${ed}-${tag}-compact.png` });
  }
  await B.close();
}
console.log('done');
