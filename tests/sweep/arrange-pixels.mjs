// ARRANGE PIXELS (13 Nov 2026): did a CSS / drawing change move a pixel? Screenshots of the Arrange panel (stopped, fixed marks: playing blocks, cursor on a bus
// track, scrolled and not) on the Song screen, beside the Phrase screen and beside Pattern view, at Fold unfolded / folded and a desktop window, in nine skins.
//   node tests/sweep/arrange-pixels.mjs --out=/tmp/px-before          (run it on the old build, then on the new one with --out=/tmp/px-after)
//   node tests/sweep/arrange-pixels.mjs --compare=/tmp/px-before,/tmp/px-after   lists the files whose bytes differ (PNG encoding is deterministic: same bytes = same pixels)
//   [--css=".tagrid{...}"] adds a style sheet to every page, to try a change without editing index.html.
// The *-full.png files include the top bar's meters and clock, which move: ignore them. Text anti-aliasing in the headers can differ by a few dozen pixels when layers change.
import fs from 'fs'; import path from 'path';
import { boot, app, ROOT } from './ui-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const cmp = arg('compare', '');
if (cmp) { const [a, b] = cmp.split(','), list = d => fs.existsSync(d) ? fs.readdirSync(d).filter(f => f.endsWith('.png') && !f.endsWith('-full.png')).sort() : [];
  const A = list(a), B = list(b); let n = 0;
  if (!A.length || !B.length) { console.log(`FAIL: need screenshots in both directories (${a}: ${A.length}, ${b}: ${B.length})`); process.exit(2); }
  for (const f of A) if (!B.includes(f)) { console.log('MISSING in ' + b + ': ' + f); n++; }
  for (const f of B) if (!A.includes(f)) { console.log('EXTRA in ' + b + ': ' + f); n++; }
  if (n) { console.log(`FAIL: the file sets differ (${n}); not comparing bytes`); process.exit(1); }
  for (const f of A) if (!fs.readFileSync(path.join(a, f)).equals(fs.readFileSync(path.join(b, f)))) { console.log('DIFF ' + f); n++; }
  console.log(`${A.length} files compared, ${n} differ`); process.exit(n ? 1 : 0); }
const ELECTRO = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
const outd = arg('out', '/tmp/arrange-pixels'), CSS = arg('css', ''); fs.mkdirSync(outd, { recursive: true });
const SKINS = ['idm', 'idmpink', 'idmpg', 'tr909', 'lowpower', 'lowpowermustard', 'lowpowerpink', 'rainbow', 'mpc'];
const LAYS = [{ n: 'song', w: 884, h: 1020, screen: 'song' }, { n: 'phrase', w: 884, h: 1020, screen: 'phrase' }, { n: 'pv', w: 884, h: 1020, screen: 'pv' }, { n: 'fold-song', w: 412, h: 915, screen: 'song' },
  { n: 'desk-song', w: 1600, h: 1000, screen: 'song', desk: true }, { n: 'desk-pv', w: 1600, h: 1000, screen: 'pv', desk: true }];
for (const L of LAYS) {
  const B = await boot({ width: L.w, height: L.h, storage: { 'thunderplus-platform-v1': L.desk ? 'desktop' : 'mobile' }, mobile: !L.desk }); const { page } = B;
  await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(400);
  await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`); if (CSS) await page.addStyleTag({ content: CSS });
  for (const sk of SKINS) {
    await app(page, `(applySkin('${sk}'), 1)`);
    await app(page, L.screen === 'pv' ? `(go('phrase'), pvSetMode(true), cs.song.y = 0, cs.song.top = 0, draw(), 1)` : `(go('${L.screen}'), cs.song.y = 0, cs.song.top = 0, draw(), 1)`); await page.waitForTimeout(500);
    await app(page, `(cs.song.x = 3, cs.song.y = 5, draw(), (() => { const g = $('tArrGrid'); for (const y of [2, 3]) { const r = g.children[y]; if (!r) continue; r.firstElementChild.classList.add('pl'); for (const b of r.querySelectorAll('b.f')) b.classList.add('p'); } })(), 1)`);
    await page.waitForTimeout(250); await app(page, `($('toast').hidden = true, 1)`);
    const el = await page.$('#tArr'); if (el) await el.screenshot({ path: path.join(outd, `${L.n}-${sk}-arr.png`) });
    if (L.screen === 'pv') await page.screenshot({ path: path.join(outd, `${L.n}-${sk}-full.png`) });
    await app(page, `(cs.song.top = 20, cs.song.y = 24, draw(), 1)`); await page.waitForTimeout(250);
    if (el) await el.screenshot({ path: path.join(outd, `${L.n}-${sk}-arr-scrolled.png`) });
  }
  await B.close();
}
console.log('done ' + outd);
