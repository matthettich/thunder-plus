// SEQUENCER GOLDEN TEST (docs/NATIVE-DESKTOP-PLAN.md, phase 0).
// Plays each test song through the app's own sequencer (makeSeq, sim mode: no audio, no renders) in headless Chrome and
// compares every event it produces with tests/golden/seq-<song>.json. The point: when the sequencer moves onto the audio
// thread (phase 2) and into C++ (phase 3), the event stream must not change.
//
//   node tools/seq-golden.mjs            compare (exit 1 on any difference; the first differing row is printed)
//   node tools/seq-golden.mjs --update   write the goldens (only after checking why they changed)
//
// Needs Chrome (or Chromium) and the playwright package: CHROME=/path/to/chrome, PLAYWRIGHT=/path/to/playwright/index.mjs
// if they aren't found. The full event dumps go to tests/golden/out/ (not committed) for diffing.
import http from 'http'; import fs from 'fs'; import path from 'path'; import crypto from 'crypto'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const UPDATE = process.argv.includes('--update');
const SONGS = [{ id: 'electro', file: 'sounds/ELECTRO.thunder', mode: 'song' }];   // add songs here (files under the repo)
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', 'C:/Program Files/Google/Chrome/Application/chrome.exe', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: npm i -g playwright, or set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff': 'font/woff', '.woff2': 'font/woff2', '.png': 'image/png', '.webmanifest': 'application/manifest+json' };
const srv = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const port = srv.address().port;
const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
let failed = false;
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  page.on('pageerror', e => console.error('page error:', e.message));
  await page.goto(`http://localhost:${port}/index.html?seqtest=1`);
  await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
  await page.waitForTimeout(1500);   // let the app's own first-run work settle
  fs.mkdirSync(path.join(ROOT, 'tests/golden/out'), { recursive: true });
  for (const S of SONGS) {
    const info = await page.evaluate(([t, n]) => window.__seqTest.load(t, n), [fs.readFileSync(path.join(ROOT, S.file), 'utf8'), path.basename(S.file)]);
    const rows = await page.evaluate(m => window.__seqTest.run(m), S.mode);
    const text = rows.map(r => JSON.stringify(r)).join('\n') + '\n';
    fs.writeFileSync(path.join(ROOT, `tests/golden/out/seq-${S.id}.jsonl`), text);
    const counts = {}; for (const r of rows) for (const e of r.evs) counts[e.type] = (counts[e.type] || 0) + 1;
    const now = { song: S.file, mode: S.mode, bpm: info.bpm, tracks: info.nt, rows: rows.length, events: Object.values(counts).reduce((a, b) => a + b, 0), counts,
      sha256: crypto.createHash('sha256').update(text).digest('hex'), rowHashes: rows.map(r => crypto.createHash('sha256').update(JSON.stringify(r)).digest('hex').slice(0, 12)) };
    const gf = path.join(ROOT, `tests/golden/seq-${S.id}.json`);
    if (UPDATE || !fs.existsSync(gf)) { fs.writeFileSync(gf, JSON.stringify(now, null, 1) + '\n'); console.log(`wrote ${path.relative(ROOT, gf)}: ${now.rows} rows, ${now.events} events`); continue; }
    const was = JSON.parse(fs.readFileSync(gf, 'utf8'));
    if (was.sha256 === now.sha256) { console.log(`ok   ${S.id}: ${now.rows} rows, ${now.events} events`); continue; }
    failed = true;
    const i = now.rowHashes.findIndex((h, k) => h !== was.rowHashes[k]);
    console.log(`FAIL ${S.id}: rows ${was.rows} → ${now.rows}, events ${was.events} → ${now.events}; first difference at step ${i < 0 ? Math.min(was.rows, now.rows) : i}`);
    if (i >= 0) console.log('  now: ' + JSON.stringify(rows[i]).slice(0, 600));
    console.log(`  full stream: tests/golden/out/seq-${S.id}.jsonl (compare with a run of the last good commit)`);
  }
} finally { await browser.close(); srv.close(); }
process.exit(failed ? 1 : 0);
