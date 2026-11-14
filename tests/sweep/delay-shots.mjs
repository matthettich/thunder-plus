// Screenshots and checks for the Delay line in both editors (14 Nov 2026): 1440x900 and 344x780 (touch), Open / Compact / Mini.
//   SHOTS=/dir node tests/sweep/delay-shots.mjs
import { boot } from './ui-lib.mjs'; import fs from 'fs';
const OUT = process.env.SHOTS || '/home/claude/work/progress/g5-shots'; fs.mkdirSync(OUT, { recursive: true });
const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp) => ({ from: [a, ap], to: [b, bp], bend: 0 });
const SPEC = {
  chains: { key: 'chains.patch', sk: 'chains.settings', mods: [P('i', 'trackIn', 0, 0, { track: 1 }), P('o', 'chainOut', 600, 0, { track: 1 }), P('dl', 'dline', 260, 120, { time: 2, fb: .9, damp: .2 }), P('dm', 'dline', 260, 520, { time: 350, fb: .4, depth: 3 }), P('lf', 'lfo', 0, 520)],
    cab: [C('i', 'out', 'dl', 'in'), C('dl', 'out', 'o', 'in'), C('lf', 'out', 'dm', 'tmod')] },
  waves: { key: 'waves.patch', sk: 'waves.settings', mods: [P('h', 'hostIn', 0, 0), P('o', 'out', 600, 0, { slot: 1, level: 1 }), P('dl', 'dline', 260, 120, { time: 2, fb: .9, damp: .2 }), P('dm', 'dline', 260, 520, { time: 350, fb: .4, depth: 3 }), P('lf', 'lfo', 0, 520)],
    cab: [C('h', 'out', 'dl', 'in'), C('dl', 'out', 'o', 'in'), C('lf', 'out', 'dm', 'tmod')] },
};
const report = {};
for (const [vw, vh, mob, tag] of [[1440, 900, false, 'd'], [344, 780, true, 'm']]) {
  const B = await boot({ width: vw, height: vh, mobile: mob }); const { page } = B, base = 'http://127.0.0.1:' + B.port;
  for (const ed of ['chains', 'waves']) {
    const S = SPEC[ed]; const doc = { meta: { name: 'delay' }, view: { x: mob ? 8 : 20, y: mob ? -100 : -60, zoom: mob ? .8 : .9, ui: 2, nw: 1 }, modules: S.mods, cables: S.cab, groups: [], custom: [], transport: { tempo: 120 } };
    const url = base + '/' + ed + '/index.html'; await page.goto(url);
    await page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({ tips: false })); }, [S.key, S.sk, doc]);
    await page.goto(url); await page.waitForSelector('.node[data-id="dl"]', { timeout: 20000 }); await page.waitForTimeout(1200);
    const pal = await page.evaluate(() => [...document.querySelectorAll('.pgroup')].map(g => ({ g: (g.querySelector('h3, .pname, header') || g).textContent.trim().replace(/\s+/g, ' ').slice(0, 40), items: [...g.querySelectorAll('.pitem')].map(i => i.textContent.trim().replace(/\s+/g, ' ')) })));
    report[ed + '-' + tag] = { palette: pal.filter(x => /Building/.test(x.g)), nodes: await page.evaluate(() => ['dl', 'dm'].map(id => { const n = document.querySelector('.node[data-id="' + id + '"]'); const r = n.getBoundingClientRect(); return { id, w: Math.round(r.width), h: Math.round(r.height), ports: [...n.querySelectorAll('.pl')].map(x => x.textContent.trim()).join('|'), text: n.innerText.replace(/\s+/g, ' ').slice(0, 220), overflowX: n.scrollWidth > n.clientWidth + 1 }; })), errors: B.errors.slice() };
    for (const st of ['open', 'compact', 'mini']) {
      if (st === 'compact') { await page.mouse.move(vw / 2, vh - 20); await page.keyboard.press('Alt+c'); }
      if (st === 'mini') { await page.mouse.move(vw / 2, vh - 20); await page.keyboard.press('Alt+Shift+C'); }
      await page.waitForTimeout(700);
      await page.screenshot({ path: `${OUT}/${ed}-${tag}-${st}.png` });
      for (const id of ['dl', 'dm']) await page.locator('.node[data-id="' + id + '"]').screenshot({ path: `${OUT}/${ed}-${tag}-${st}-${id}.png` }).catch(() => {});
      report[ed + '-' + tag][st] = await page.evaluate(() => ['dl', 'dm'].map(id => document.querySelector('.node[data-id="' + id + '"]').innerText.replace(/\s+/g, ' ').slice(0, 160)));
    }
  }
  await B.close();
}
fs.writeFileSync(OUT + '/report.json', JSON.stringify(report, null, 1)); console.log(JSON.stringify(report, null, 1).slice(0, 7000));
