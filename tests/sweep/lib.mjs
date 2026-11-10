// Shared harness for the sweep scripts: a static server on the repo root, headless Chrome on /index.html?seqtest=1,
// page errors and console errors collected. Usage: const A = await openApp(); ... await A.close();
// Needs Chrome and playwright (CHROME=..., PLAYWRIGHT=.../playwright/index.mjs if they aren't found).
import http from 'http'; import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';
export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', 'C:/Program Files/Google/Chrome/Application/chrome.exe'].find(f => fs.existsSync(f));
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff2': 'font/woff2', '.png': 'image/png', '.svg': 'image/svg+xml', '.webmanifest': 'application/manifest+json' };
export async function openApp(opts = {}){
  if (!PW){ console.error('playwright not found: set PLAYWRIGHT'); process.exit(2); }
  const { chromium } = await import(pathToFileURL(PW).href);
  const srv = http.createServer((q, r) => {
    let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)){ r.writeHead(404); return r.end(); }
    fs.readFile(f, (e, d) => { if (e){ r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
  });
  await new Promise(ok => srv.listen(0, ok));
  const port = srv.address().port;
  const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required', ...(opts.args || [])] });
  const page = await browser.newPage({ viewport: { width: opts.w || 1400, height: opts.h || 900 }, ...(opts.page || {}) });   // opts.page: more newPage options (a phone's user agent...)
  const errors = [];
  page.on('pageerror', e => errors.push('pageerror: ' + e.message));
  page.on('console', m => { if (m.type() === 'error') errors.push('console.error: ' + m.text()); });
  await page.goto(`http://localhost:${port}/${opts.path || 'index.html?seqtest=1'}`);
  if (!opts.path){
    await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 60000 });
    await page.waitForTimeout(1500);
    const g = page.getByRole('button', { name: 'Got it' }); if (await g.count()) await g.first().click().catch(() => {});
  }
  // run(src): an async function body inside the app's scope (S, kit, TFX… are in reach); returns its result
  const run = src => page.evaluate(s => window.__seqTest.ev('(async () => {' + s + '\n})()'), src);
  const takeErrors = () => errors.splice(0);   // the errors since the last call
  return { page, run, port, takeErrors, close: async () => { await browser.close(); srv.close(); } };
}
// A row as it happens (so a run that is stopped part way still shows what it found).
export function live(r){ console.log(r[1].padEnd(5) + '  ' + r[0] + (r[2] ? '  ·  ' + r[2] : '')); }
// The PASS/FAIL table at the end: every row that isn't PASS, then the count; returns the number of failures.
export function table(rows){
  const w = [0, 0]; for (const r of rows){ w[0] = Math.max(w[0], r[0].length); w[1] = Math.max(w[1], r[1].length); }
  let bad = 0; console.log('Rows that are not PASS:');
  for (const r of rows){ if (r[1] === 'FAIL') bad++; if (r[1] !== 'PASS') console.log(r[0].padEnd(w[0]) + '  ' + r[1].padEnd(5) + '  ' + (r[2] || '')); }
  console.log(`\n${rows.length} checks, ${rows.filter(r => r[1] === 'PASS').length} PASS, ${bad} FAIL, ${rows.filter(r => r[1] === 'NOTE').length} NOTE, ${rows.filter(r => r[1] === 'SKIP').length} SKIP`); return bad;
}
