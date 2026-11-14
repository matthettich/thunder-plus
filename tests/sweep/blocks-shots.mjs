// Screenshots and checks for the audio building blocks in both editors (14 Nov 2026): 1440x900 and 344x780 (touch), Open / Compact / Mini.
//   SHOTS=/dir node tests/sweep/blocks-shots.mjs
// …chains gets One-pole filter, Allpass, DC blocker, Mid / side, Oscillator, Noise; …waves the first four (and no Oscillator / Noise). Also reads the module list.
import { boot } from './ui-lib.mjs'; import fs from 'fs';
const OUT = process.env.SHOTS || '/home/claude/work/progress/g4-shots'; fs.mkdirSync(OUT, { recursive: true });
const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp) => ({ from: [a, ap], to: [b, bp], bend: 0 });
const four = [P('op', 'onepole', 0, 200), P('ap', 'allpass', 250, 200, { mode: 1 }), P('dc', 'dcblock', 500, 200), P('ms', 'midside', 750, 200)];
const SPEC = {
  chains: { key: 'chains.patch', sk: 'chains.settings', ids: ['op', 'ap', 'dc', 'ms', 'os', 'nz'],
    mods: [P('i', 'trackIn', 0, 0, { track: 1 }), P('o', 'chainOut', 300, 0, { track: 1 }), ...four, P('os', 'osc', 0, 560, { freq: 3.5 }), P('nz', 'wnoise', 250, 560, { color: 1 })],
    cab: [C('i', 'out', 'op', 'in'), C('op', 'lp', 'ap', 'in'), C('ap', 'out', 'dc', 'in'), C('dc', 'out', 'ms', 'in'), C('i', 'outR', 'ms', 'inR'), C('ms', 'out', 'o', 'in'), C('ms', 'outR', 'o', 'inR'), C('os', 'out', 'ap', 'time')] },
  waves: { key: 'waves.patch', sk: 'waves.settings', ids: ['op', 'ap', 'dc', 'ms'],
    mods: [P('h', 'hostIn', 0, 0), P('o', 'out', 300, 0, { slot: 1, level: 1 }), ...four],
    cab: [C('h', 'out', 'op', 'in'), C('op', 'lp', 'ap', 'in'), C('ap', 'out', 'dc', 'in'), C('dc', 'out', 'ms', 'in'), C('ms', 'out', 'o', 'in')] },
};
const report = {};
for (const [vw, vh, mob, tag] of [[1440, 900, false, 'd'], [344, 780, true, 'm']]) {
  const B = await boot({ width: vw, height: vh, mobile: mob }); const { page } = B, base = 'http://127.0.0.1:' + B.port;
  for (const ed of ['chains', 'waves']) {
    const S = SPEC[ed]; const doc = { meta: { name: 'blocks' }, view: { x: mob ? 8 : 20, y: mob ? -150 : -120, zoom: mob ? .8 : .9, ui: 2, nw: 1 }, modules: S.mods, cables: S.cab, groups: [], custom: [], transport: { tempo: 120 } };
    const url = base + '/' + ed + '/index.html'; await page.goto(url);
    await page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({ tips: false })); }, [S.key, S.sk, doc]);
    await page.goto(url); await page.waitForSelector('.node[data-id="' + S.ids[0] + '"]', { timeout: 20000 });
    await page.waitForTimeout(1200);
    // the module list: groups and their items
    const pal = await page.evaluate(() => [...document.querySelectorAll('.pgroup')].map(g => ({ g: (g.querySelector('h3, .pname, header') || g).textContent.trim().replace(/\s+/g, ' ').slice(0, 40), items: [...g.querySelectorAll('.pitem')].map(i => i.textContent.trim().replace(/\s+/g, ' ')) })));
    report[ed + '-' + tag] = { palette: pal.filter(x => /Building|Sound sources|Filters and EQ/.test(x.g)), nodes: await page.evaluate(ids => ids.map(id => { const n = document.querySelector('.node[data-id="' + id + '"]'); return n ? { id, w: Math.round(n.getBoundingClientRect().width / (window.visualViewport ? 1 : 1)), ports: [...n.querySelectorAll('.pl')].map(x => x.textContent.trim()).join('|'), ctrls: [...n.querySelectorAll('.row .lab, .row label')].map(x => x.textContent.trim()).join('|') } : { id, missing: true }; }), S.ids),
      oscInPalette: await page.evaluate(() => [...document.querySelectorAll('.pitem')].some(i => /^Oscillator\b/.test(i.textContent.trim()))), errors: B.errors.slice() };
    for (const st of ['open', 'compact', 'mini']) {
      if (st === 'compact') { await page.mouse.move(vw / 2, vh - 20); await page.keyboard.press('Alt+c'); }
      if (st === 'mini') { await page.mouse.move(vw / 2, vh - 20); await page.keyboard.press('Alt+Shift+C'); }
      await page.waitForTimeout(700);
      await page.screenshot({ path: `${OUT}/${ed}-${tag}-${st}.png` });
      for (const id of S.ids) await page.locator('.node[data-id="' + id + '"]').screenshot({ path: `${OUT}/${ed}-${tag}-${st}-${id}.png` }).catch(() => {});
    }
  }
  await B.close();
}
fs.writeFileSync(OUT + '/report.json', JSON.stringify(report, null, 1)); console.log(JSON.stringify(report, null, 1).slice(0, 6000));
