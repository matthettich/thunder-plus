// Shared harness for the tests/sweep scripts: a static server on the repo root, headless Chrome with the app at
// /index.html?seqtest=1 (window.__seqTest), the first-run dialog dismissed, page errors and console errors collected,
// and a PASS/FAIL table printer. Needs Chrome and playwright as tools/seq-golden.mjs does (CHROME=, PLAYWRIGHT= override).
import http from 'http'; import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';
export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', 'C:/Program Files/Google/Chrome/Application/chrome.exe', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].find(f => fs.existsSync(f));
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff': 'font/woff', '.woff2': 'font/woff2', '.png': 'image/png', '.webmanifest': 'application/manifest+json', '.svg': 'image/svg+xml' };

export async function open(opts = {}) {
  if (!PW) { console.error('playwright not found: set PLAYWRIGHT'); process.exit(2); }
  const { chromium } = await import(pathToFileURL(PW).href);
  const srv = http.createServer((q, r) => {
    let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
    fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
  });
  await new Promise(ok => srv.listen(0, ok));
  const port = srv.address().port;
  const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
  const context = await browser.newContext({ viewport: { width: 1400, height: 900 }, acceptDownloads: true });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push('pageerror: ' + e.message));
  page.on('console', m => { if ((m.type() === 'error' || (opts.warn && m.type() === 'warning')) && !/favicon|404|Failed to load resource/.test(m.text())) errors.push('console.' + m.type() + ': ' + m.text().slice(0, 300)); });
  page.on('dialog', d => d.accept().catch(() => {}));
  page.setDefaultTimeout(180000);   // generous: the machine may be busy
  await page.goto(`http://localhost:${port}/index.html?seqtest=1${opts.query || ''}`, { timeout: 180000 });
  await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 180000 });
  await page.waitForTimeout(1500);
  await dismiss(page);
  const close = async () => { await browser.close(); srv.close(); };
  return { page, errors, close, port, context, browser };
}
// "Three things to know" and any other first-run "Got it"
export async function dismiss(page) {
  for (let i = 0; i < 3; i++) { const b = page.locator('button:has-text("Got it")'); if (await b.count()) { await b.first().click({ timeout: 2000 }).catch(() => {}); await page.waitForTimeout(200); } else break; }
}
// Run code inside the app's scope (where S, makeSeq, FXC… are). The result must be JSON-able.
export const ev = (page, src) => page.evaluate(s => window.__seqTest.ev(s), src);

export function table(rows, title) {
  const w = Math.max(6, ...rows.map(r => r[0].length));
  console.log(`\n${title}\n` + '-'.repeat(100));
  for (const [name, res, note] of rows) console.log(name.padEnd(w) + '  ' + res.padEnd(5) + '  ' + (note || ''));
  const n = rows.filter(r => r[1] === 'FAIL').length;
  console.log('-'.repeat(100) + `\n${rows.length - n} pass, ${n} fail`);
  return n;
}
