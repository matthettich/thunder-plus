// WHEEL ON A SCROLLBAR (3 Nov): in headless Chrome with real (classic) scrollbars, the wheel over each of the app's bars
// scrolls that bar and changes no value; off the bars the wheel does what it did. See sbHit / sbWheel in index.html.
//
//   node tools/wheel-scrollbar.mjs          run the checks (exit 1 on a failure)
//   BASE=1 node tools/wheel-scrollbar.mjs   the same with sbWheel taken out: shows which checks the fix is for
//
// Needs Chrome (or Chromium) and playwright: CHROME=/path/to/chrome, PLAYWRIGHT=/path/to/playwright/index.mjs if not found.
// Playwright's headless default hides scrollbars (--hide-scrollbars), and Linux Chrome may overlay them: both are undone.
import http from 'http'; import fs from 'fs'; import path from 'path'; import { pathToFileURL } from 'url';
import { fileURLToPath } from 'url';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PW = process.env.PLAYWRIGHT || ['/opt/node-tools/node_modules/playwright/index.mjs', '/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', 'C:/Program Files/Google/Chrome/Application/chrome.exe', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: npm i -g playwright, or set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  fs.readFile(path.join(ROOT, p), (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, {'content-type': p.endsWith('.html') ? 'text/html' : /\.m?js$/.test(p) ? 'text/javascript' : p.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream'}); r.end(d); }); });
await new Promise(ok => srv.listen(0, ok));
const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), ignoreDefaultArgs: ['--hide-scrollbars'], args: ['--autoplay-policy=no-user-gesture-required', '--disable-features=OverlayScrollbar'] });
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
page.on('pageerror', e => console.error('page error:', e.message));
await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`);
await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 }); await page.waitForTimeout(1500);
await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'));
await page.waitForTimeout(500); await page.evaluate(() => document.querySelectorAll('.welcome').forEach(w => w.remove()));
if (process.env.BASE) await page.evaluate(() => window.__seqTest.ev("removeEventListener('wheel', sbWheel, {capture:true}); 1"));
const scan = () => page.evaluate(() => [...document.querySelectorAll('*')].filter(el => { if (!el.getClientRects().length) return false; const s = getComputedStyle(el);
  return (/auto|scroll/.test(s.overflowY) && el.scrollHeight > el.clientHeight + 1) || (/auto|scroll/.test(s.overflowX) && el.scrollWidth > el.clientWidth + 1); })
  .map(el => ({id: el.id, tag: el.tagName, cls: typeof el.className === 'string' ? el.className : '', vw: el.offsetWidth - el.clientWidth, hh: el.offsetHeight - el.clientHeight, sh: el.scrollHeight - el.clientHeight, sw: el.scrollWidth - el.clientWidth})));
let fail = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) fail++; };
try { await checks({ page, scan, ok }); } finally { await browser.close(); srv.close(); }
process.exit(fail ? 1 : 0);

async function checks({ page, scan, ok }) {
  const box = sel => page.evaluate(sel => { const el = document.querySelector(sel), r = el.getBoundingClientRect(); return {l: r.left, t: r.top, r: r.right, b: r.bottom, cw: el.clientWidth, ch: el.clientHeight, st: el.scrollTop, sl: el.scrollLeft, vw: el.offsetWidth - el.clientWidth, hh: el.offsetHeight - el.clientHeight}; }, sel);
  const ui = () => page.evaluate(() => window.__seqTest.ui());
  const wheel = async (x, y, dy, shift) => { await page.mouse.move(x, y); await page.waitForTimeout(50); if (shift) await page.keyboard.down('Shift'); await page.mouse.wheel(0, dy); if (shift) await page.keyboard.up('Shift'); await page.waitForTimeout(150); };

  // ── ARRANGE (Song screen): vertical bar (the cursor sits on row 6, still in view after one notch of scroll; a scroll by
  // any means keeps the cursor in view, so a row that stays on screen must not move)
  await page.evaluate(() => window.__seqTest.nav({ tv: 'song', songY: 6, songX: 0 })); await page.waitForTimeout(400);
  let b = await box('#tArrGrid'), u0 = await ui();
  ok(b.vw > 0, 'arrange has a real vertical bar (' + b.vw + ' px)');
  await wheel(b.r - b.vw / 2 - 1, (b.t + b.b) / 2, 100);
  let b1 = await box('#tArrGrid'), u1 = await ui();
  ok(b1.st > b.st, `arrange: wheel on the bar scrolls it (${b.st} → ${b1.st})`);
  ok(u1.songY === u0.songY, `arrange: wheel on the bar leaves the song cursor (${u0.songY} → ${u1.songY})`);
  await wheel(b.r - b.vw / 2 - 1, (b.t + b.b) / 2, -100);
  ok((await box('#tArrGrid')).st < b1.st, 'arrange: wheel up on the bar scrolls back up');
  await page.evaluate(() => window.__seqTest.nav({ tv: 'song', songY: 0, songX: 0 })); await page.waitForTimeout(300);
  await wheel(b.l + 40, b.t + 30, 100);
  ok((await ui()).songY === 1, 'arrange: wheel off the bar still moves the song cursor a row (' + (await ui()).songY + ')');

  // ── PATTERN VIEW: horizontal bar
  await page.evaluate(() => window.__seqTest.nav({ tv: 'phrase', pv: true })); await page.waitForTimeout(400);
  b = await box('#tGrid'); u0 = await ui();
  ok(b.hh > 0, 'pattern view has a real horizontal bar (' + b.hh + ' px)');
  await wheel((b.l + b.r) / 2, b.t + b.ch + b.hh / 2, 100);
  b1 = await box('#tGrid'); u1 = await ui();
  ok(b1.sl > b.sl, `pattern: wheel on the bar scrolls sideways (${b.sl} → ${b1.sl})`);
  ok(u1.y === u0.y && u1.x === u0.x && !u1.sel, `pattern: cursor unchanged, no selection (${u0.y} → ${u1.y})`);
  await wheel((b.l + b.r) / 2, b.t + b.ch + b.hh / 2, 100, true);
  const b2 = await box('#tGrid'), u2 = await ui();
  ok(b2.sl > b1.sl, `pattern: Shift+wheel on the bar scrolls sideways too (${b1.sl} → ${b2.sl})`);
  ok(!u2.sel && u2.y === u0.y, 'pattern: Shift+wheel on the bar makes no selection');
  await wheel(b.l + 60, b.t + 80, 100);
  const u3 = await ui(), b3 = await box('#tGrid');
  ok(u3.y > u0.y && b3.sl === b2.sl, `pattern: wheel off the bar still moves the cursor a row (${u0.y} → ${u3.y}), no sideways scroll`);

  // ── FX screen's pane: vertical bar
  await page.evaluate(() => window.__seqTest.nav({ tv: 'fx', pv: false })); await page.waitForTimeout(400);
  b = await box('#tGrid .fxpane'); u0 = await ui();
  if (b.vw > 0 && b.st + 1 < 1e9) {
    await wheel(b.r - b.vw / 2 - 1, (b.t + b.b) / 2, 100);
    b1 = await box('#tGrid .fxpane'); u1 = await ui();
    ok(b1.st > b.st, `fx pane: wheel on the bar scrolls it (${b.st} → ${b1.st})`);
    ok(u1.y === u0.y && u1.x === u0.x, `fx pane: cursor unchanged (${u0.y} → ${u1.y})`);
  } else ok(false, 'fx pane has no real bar');

  // ── BOTTOM PANEL (classic, short): the INST list's vertical bar; its sliders don't move
  const ev = s => page.evaluate(s => window.__seqTest.ev(s), s);
  await page.evaluate(() => window.__seqTest.nav({ tv: 'phrase', pv: false }));
  await ev("deck.h = 110; deck.fxbar = false; deckLayout(); fit(); draw(); 1"); await page.waitForTimeout(400);
  b = await box('#dkInstRows');
  const vals = () => page.evaluate(() => [...document.querySelectorAll('#dkInstRows input[type=range]')].map(r => r.value).join(','));
  if (b.vw > 0) {
    const v0 = await vals();
    // rest on a slider first (arms the wheel), then go to the bar and wheel
    const sr = await page.evaluate(() => { const r = document.querySelector('#dkInstRows input[type=range]').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
    await page.mouse.move(sr[0], sr[1]); await page.waitForTimeout(500);
    await wheel(b.l + (b.r - b.l) - b.vw / 2 - 1, b.t + Math.min(30, b.ch / 2), 100);
    b1 = await box('#dkInstRows');
    ok(b1.st > b.st, `inst rows: wheel on the bar scrolls it (${b.st} → ${b1.st})`);
    ok(await vals() === v0, 'inst rows: no slider moved');
  } else ok(false, 'inst rows have no real bar');
  await ev("deck.h = 0; deck.fxbar = true; deckLayout(); fit(); draw(); 1"); await page.waitForTimeout(300);

  // ── BOTTOM PANEL on a narrow window: the panel body's horizontal bar
  await page.setViewportSize({ width: 900, height: 1000 }); await page.waitForTimeout(600);
  b = await box('#dkBody');
  if (b.hh > 0) {
    await wheel((b.l + b.r) / 2, b.t + b.ch + b.hh / 2, 100);
    b1 = await box('#dkBody'); ok(b1.sl > b.sl, `panel body: wheel on the horizontal bar scrolls it sideways (${b.sl} → ${b1.sl})`);
  } else console.log('note: panel body has no horizontal bar at 900 px', JSON.stringify(b));
  await page.setViewportSize({ width: 1600, height: 1000 }); await page.waitForTimeout(600);

  // ── HELP sheet: native scroll on its bar
  await ev("showHelp(); 1"); await page.waitForTimeout(400);
  const hs = (await scan()).filter(x => x.vw > 0 && x.sh > 0 && x.id !== 'tArrGrid');
  console.log('help bars:', JSON.stringify(hs));
  if (hs.length) {
    const sel = hs[0].id ? '#' + hs[0].id : '.' + hs[0].cls.trim().split(/\s+/).join('.');
    b = await box(sel); await wheel(b.r - b.vw / 2 - 1, (b.t + b.b) / 2, 100);
    ok((await box(sel)).st > b.st, `help (${sel}): wheel on the bar scrolls it`);
  }
  await page.keyboard.press('Escape'); await page.waitForTimeout(200);

  // ── any other bar found now
  console.log('bars now:', JSON.stringify(await scan()));
};
