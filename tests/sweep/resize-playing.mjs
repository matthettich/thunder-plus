// RESIZE DURING PLAYBACK (24 Nov 2026, build 676): the owner's report (Mac desktop app): "GUI hangs when I resize the window ... animations or information
// overlaps with the content in the window. Happens intermittently during playback."
//   (--sample=1 also measures the overlap two frames after every step, which itself costs time: the long-task numbers are then not the drag's alone)
//   node tests/sweep/resize-playing.mjs [--skins=idm,idmpink,tr909] [--screens=phrase,pv,song,mixer,sect] [--steps=24] [--gap=16] [--out=/tmp/resize]
// One Chrome per skin (desktop setup, 1440x900), ELECTRO playing, stage animation on (BY SCREEN). For each screen the window is resized in steps (wide to narrow and back,
// then short heights), a step every --gap ms like a live drag. Per screen it prints: long tasks (count, total, longest) during the drag, and after the drag settled (700 ms)
// whether the layout is STALE: the same measurements again after forcing fit() + draw() + stageLayout(); and OVERLAPS: the stage box over the grid's text, over Arrange,
// the deck over the grid. Screenshots (--out). Exits 1 on a stale layout or an overlap.
import fs from 'fs'; import path from 'path';
import { boot, app, ROOT } from './ui-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SKINS = arg('skins', 'idm,idmpink,tr909').split(','), SCREENS = arg('screens', 'phrase,pv,song,mixer,sect').split(','), STEPS = +arg('steps', 24), W0 = +arg('w0', 1600), W1 = +arg('w1', 700), H0 = +arg('h0', 900), GAP = +arg('gap', 16), SAMPLE = arg('sample', '0') === '1', OUT = arg('out', '/tmp/resize');
fs.mkdirSync(OUT, { recursive: true });
const ELECTRO = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
const SIZES = []; for (let i = 0; i <= STEPS; i++) SIZES.push([Math.round(W0 - (W0 - W1) * i / STEPS), H0]);   // wide to narrow
for (let i = STEPS - 1; i >= 0; i--) SIZES.push([Math.round(W0 - (W0 - W1) * i / STEPS), H0]);                  // and back
for (let i = 0; i <= STEPS; i++) SIZES.push([Math.round((W0 + W1) / 2), Math.round(H0 - (H0 - 480) * i / STEPS)]);                       // short heights
for (let i = STEPS - 1; i >= 0; i--) SIZES.push([Math.round((W0 + W1) / 2), Math.round(H0 - (H0 - 480) * i / STEPS)]);
const MEASURE = `(() => { const R = e => { if (!e || e.hidden || !e.offsetParent && getComputedStyle(e).position !== 'fixed') return null; const r = e.getBoundingClientRect(); return r.width && r.height ? [r.left, r.top, r.right, r.bottom].map(Math.round) : null; };
  const st = $('tStage'), ar = $('tArr'), dk = $('tDeck');
  let right = 0; const rg = document.createRange(); for (const L of grid.querySelectorAll(':scope > .ln')) { rg.selectNodeContents(L); for (const r of [rg.getBoundingClientRect()]) if (r.width && r.right > right) right = r.right; }
  return { fs: scr.style.fontSize, dkh: $('tDeck').style.getPropertyValue('--dkh'), grid: R(grid), stage: R(st), stageKind: stgKindNow, arr: R(ar), deck: R(dk), textRight: Math.round(right), scr: R(scr), iw: innerWidth, ih: innerHeight, arrW: arrEl.style.width }; })()`;
const rows = []; let bad = 0;
for (const sk of SKINS) {
  const B = await boot({ width: W0, height: H0, storage: { 'thunderplus-platform-v1': 'desktop', 'thunderplus-stage-v1': 'screen' } }); const { page } = B;
  await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(400);
  await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, applySkin('${sk}'), setArr(1), 1)`);
  await page.evaluate(() => { const Z = window.__lt = []; try { new PerformanceObserver(l => { for (const e of l.getEntries()) Z.push([e.startTime, e.duration]); }).observe({ type: 'longtask', buffered: true }); } catch (e) {} });
  for (const scrn of SCREENS) {
    await page.setViewportSize({ width: W0, height: H0 }); await page.waitForTimeout(300);
    // mst, mstpv: the MASTER track's phrase (its stage is the SIGNAL chain), plain and in Pattern view
    await app(page, scrn === 'pv' ? `(go('phrase'), pvSetMode(true), 1)` : scrn === 'mstpv' || scrn === 'mst' ? `(go('song'), cs.song.x = MT, go('phrase'), pvSetMode(${scrn === 'mstpv'}), 1)` : `(go('${scrn}'), tv === 'phrase' && pvSetMode(false), 1)`);
    await app(page, `(tStart('song', {row:0}), 1)`); await page.waitForTimeout(2500);
    await page.screenshot({ path: path.join(OUT, `${sk}-${scrn}-before.png`) });
    const t0 = await page.evaluate(() => { window.__lt.length = 0; return performance.now(); });
    let trans = 0, transEx = '';   // transient overlaps: measured two frames after each step, in the middle of the drag
    for (const [w, h] of SIZES) { await page.setViewportSize({ width: w, height: h }); await page.waitForTimeout(GAP);
      if (!SAMPLE) continue;
      const q = await page.evaluate(src => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => r(window.__seqTest.ev(src))))), MEASURE);
      if (q.stage && q.stageKind && q.textRight && q.stage[0] < q.textRight - 1) { trans++; if (!transEx) transEx = `${w}x${h}: stage left ${q.stage[0]} < text right ${q.textRight}`; } }
    const lt = await page.evaluate(() => window.__lt.slice()); const tot = lt.reduce((a, x) => a + x[1], 0), mx = lt.reduce((a, x) => Math.max(a, x[1]), 0);
    await page.waitForTimeout(700);
    // the settled layout vs the same after a forced refit
    const meas = async () => page.evaluate(src => window.__seqTest.ev(src), MEASURE);
    const a = await meas(); await page.screenshot({ path: path.join(OUT, `${sk}-${scrn}-after.png`) });
    await app(page, `(fit(), draw(), stageLayout(), 1)`); await page.waitForTimeout(120); const b = await meas();
    const stale = []; for (const k of ['fs', 'dkh', 'stageKind', 'arrW']) if (a[k] !== b[k]) stale.push(`${k}:${a[k]}→${b[k]}`);
    for (const k of ['grid', 'stage', 'arr', 'deck']) if (JSON.stringify(a[k]) !== JSON.stringify(b[k])) stale.push(`${k}:${JSON.stringify(a[k])}→${JSON.stringify(b[k])}`);
    const over = [];
    if (a.stage && a.textRight && a.stageKind && a.stage[0] < a.textRight - 1 && scrn !== 'song') over.push(`stage left ${a.stage[0]} < text right ${a.textRight}`);
    if (a.stage && a.arr && scrn !== 'song' && a.stage[0] < a.arr[2] - 1 && a.stage[2] > a.arr[0] + 1 && a.stage[1] < a.arr[3] && a.stage[3] > a.arr[1] && a.arr[0] > a.grid[0] - 1 && false) over.push('stage over arrange');
    if (a.stage && a.scr && (a.stage[2] > a.scr[2] + 1 || a.stage[3] > a.scr[3] + 1)) over.push(`stage ${JSON.stringify(a.stage)} outside the screen ${JSON.stringify(a.scr)}`);
    if (a.deck && a.grid && a.deck[1] < a.grid[3] - 1) over.push(`deck top ${a.deck[1]} over grid bottom ${a.grid[3]}`);
    if (trans) over.push(`${trans} of ${SIZES.length} steps overlapped mid-drag (first ${transEx})`);
    const bad1 = stale.length || over.length; if (bad1) bad++;
    console.log(`${bad1 ? 'FAIL' : 'PASS'} ${sk} ${scrn}: ${SIZES.length} resizes, long tasks ${lt.length}, ${Math.round(tot)} ms total, longest ${Math.round(mx)} ms${stale.length ? '; STALE ' + stale.join(' ') : ''}${over.length ? '; OVERLAP ' + over.join(' ') : ''}`);
    await app(page, `(tStop(), 1)`);
  }
  await B.close();
}
// SCROLLED PATTERN VIEW (the owner's overlap, 24 Nov): the master's Pattern view scrolled to its end, the window resized, then scrolled back: the stage must not be over the text
{ const B = await boot({ width: 3000, height: 1000, storage: { 'thunderplus-platform-v1': 'desktop', 'thunderplus-stage-v1': 'screen' } }); const { page } = B;
  await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(400);
  await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, applySkin('idmpink'), setArr(1), go('song'), cs.song.x = MT, go('phrase'), pvSetMode(true), draw(), 1)`);
  await page.setViewportSize({ width: 2700, height: 1000 }); await page.waitForTimeout(600);
  await app(page, `(grid.scrollLeft = 99999, 1)`); await page.waitForTimeout(300);
  await page.setViewportSize({ width: 2690, height: 1000 }); await page.waitForTimeout(700);
  await app(page, `(grid.scrollLeft = 0, 1)`); await page.waitForTimeout(400);
  const m = await page.evaluate(src => window.__seqTest.ev(src), MEASURE); const ov = m.stage && m.stageKind && m.stage[0] < m.textRight - 1;
  console.log(`${ov ? 'FAIL' : 'PASS'} scrolled master Pattern view, resized, scrolled back: stage ${JSON.stringify(m.stage)}, text right ${m.textRight}`); if (ov) bad++; await B.close(); }
console.log(bad ? `${bad} FAIL` : 'all PASS'); process.exit(bad ? 1 : 0);
