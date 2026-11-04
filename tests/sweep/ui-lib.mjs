// Shared helpers for the UI sweep scripts (tests/sweep/*.mjs): a static server for the repo root, headless Chrome through
// the preinstalled playwright, the first-run dialog dismissed, page errors / console errors collected, and a PASS/FAIL table.
import http from 'http'; import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';
export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const OUT = path.join(ROOT, 'tests/sweep/out');
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', 'C:/Program Files/Google/Chrome/Application/chrome.exe'].find(f => fs.existsSync(f));
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff': 'font/woff', '.woff2': 'font/woff2', '.png': 'image/png', '.svg': 'image/svg+xml', '.css': 'text/css', '.webmanifest': 'application/manifest+json', '.wav': 'audio/wav' };

export async function boot({ width = 1600, height = 1000, query = '?seqtest=1', storage = null, mobile = false } = {}) {
  if (!PW) throw new Error('playwright not found (set PLAYWRIGHT)');
  const { chromium } = await import(pathToFileURL(PW).href);
  const srv = http.createServer((q, r) => {
    let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
    fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
  });
  await new Promise(ok => srv.listen(0, ok));
  const port = srv.address().port;
  const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
  const ctx = await browser.newContext({ viewport: { width, height }, ...(mobile ? { hasTouch: true, isMobile: true, deviceScaleFactor: 2 } : {}) });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push('pageerror: ' + e.message));
  page.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') errors.push(m.type() + ': ' + m.text()); });
  page.on('dialog', d => d.dismiss().catch(() => {}));
  const url = `http://localhost:${port}/index.html${query}`;
  if (storage) { await page.goto(url); await page.evaluate(s => { for (const k in s) localStorage.setItem(k, s[k]); }, storage); }
  await page.goto(url);
  await page.waitForFunction(() => document.readyState === 'complete', null, { timeout: 30000 });
  await page.waitForTimeout(1500);
  await dismissTour(page);
  const close = async () => { await browser.close(); srv.close(); };
  return { page, ctx, browser, errors, close, url, port };
}

// An expression evaluated inside the app's own script scope (view, tv, S, aset… are in reach): needs ?seqtest=1.
export const app = (page, src) => page.evaluate(s => window.__seqTest.ev(s), src);

export async function dismissTour(page) {
  for (let i = 0; i < 3; i++) {
    const b = page.locator('button', { hasText: /^Got it/ }).first();
    if (await b.isVisible().catch(() => false)) { await page.waitForTimeout(600); await b.click({ timeout: 5000 }).catch(() => b.evaluate(x => x.click())); await page.waitForTimeout(200); } else break;   // (it slides in: on a phone-sized window a click while it moves lands on its text)
  }
}

// Errors raised since the last call, minus the ones that are the environment, not the app (no audio device, no MIDI).
export function takeErrors(errors) {
  return errors.splice(0).filter(e => !/MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource/i.test(e));
}

export class Table {
  constructor(name) { this.name = name; this.rows = []; }
  add(item, ok, note = '') {
    const r = [String(item), ok === true ? 'PASS' : ok === false ? 'FAIL' : String(ok), note]; this.rows.push(r);
    if (process.env.SWEEP_LIVE) console.log(`  .. ${r[1].padEnd(5)} ${r[0].slice(0, 70)}  ${String(r[2]).slice(0, 160)}`);
  }
  print() {
    const w0 = Math.min(60, Math.max(4, ...this.rows.map(r => r[0].length)));
    console.log(`\n== ${this.name} ==`);
    for (const [a, b, c] of this.rows) console.log(`${b.padEnd(5)} ${a.slice(0, 60).padEnd(w0)}  ${String(c).slice(0, 220)}`);
    const f = this.rows.filter(r => r[1] === 'FAIL').length;
    console.log(`-- ${this.rows.length} checks, ${f} FAIL`);
    return f;
  }
}
