// NATIVE INPUT ENGINE, the page's side against a FAKE helper (no Electron, no native addon): a stand-in for
// window.thunderDesktop.io whose start() hands the page a MessagePort that plays the helper (hello, devices).
//   - Settings › Audio shows "Input engine: native (500R8, 28 inputs)" when the helper answers
//   - choosing BROWSER shows the browser line and persists across a reload
//   - a helper that says available:false shows the unavailable line
//   - the website (no thunderDesktop): no INPUT ENGINE row, no 'Input engine' text, nothing throws
//
//   node tools/io-check.mjs        CHROME=… PLAYWRIGHT=… as for tools/seq-golden.mjs
import http from 'http'; import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PW = process.env.PLAYWRIGHT || ['/opt/node-tools/node_modules/playwright/index.mjs', '/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff': 'font/woff', '.woff2': 'font/woff2', '.png': 'image/png', '.webmanifest': 'application/manifest+json' };
const srv = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const port = srv.address().port;

// The fake, in the page before the app runs. mode: 'ok' | 'off' (hello available:false) | 'silent' (no port at all)
const FAKE = mode => {
  window.__ioStarts = 0;
  window.thunderDesktop = { io: { available: true, start() {
    window.__ioStarts++;
    if (mode === 'silent') return Promise.resolve({ ok: true });
    setTimeout(() => {
      const ch = new MessageChannel(), h = ch.port1;
      h.onmessage = e => { const m = e.data;
        if (m && m.op === 'devices') h.postMessage({ op: 'devices', id: m.id, devices: [
          { name: 'Built-in', inputs: 2, outputs: 2, backend: 'Core Audio', inId: 'in:bb', outId: 'out:bb', id: 'in:bb', rates: [48000], isDefaultIn: false, isDefaultOut: false },
          { name: '500R8', inputs: 28, outputs: 28, backend: 'Core Audio', inId: 'in:aa', outId: 'out:aa', id: 'in:aa', rates: [44100, 48000], isDefaultIn: true, isDefaultOut: true }] }); };
      h.postMessage(mode === 'ok' ? { op: 'hello', available: true, version: 'thunder_io 1 / miniaudio 0.11.26' } : { op: 'hello', available: false, why: 'addon missing' });
      window.postMessage({ thunderIo: true }, '*', [ch.port2]);
    }, 20);
    return Promise.resolve({ ok: true });
  } } };
};

let fails = 0;
const ok = (c, msg) => { console.log((c ? 'ok   ' : 'FAIL ') + msg); if (!c) fails++; };
const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
async function open(mode, ctxPage) {
  const page = ctxPage || await browser.newPage({ viewport: { width: 1400, height: 900 } });
  page.on('pageerror', e => { console.error('page error:', e.message); fails++; });
  page.on('console', m => { if (m.type() === 'error' && !/Failed to load resource/.test(m.text())) { console.error('console error:', m.text()); fails++; } });
  await page.addInitScript(() => { try { if (!localStorage.getItem('thunderplus-welcome-v1')) { localStorage.setItem('thunderplus-welcome-v1', '1'); localStorage.setItem('thunderplus-seen-build', '9999'); } } catch (e) {} });
  if (mode !== null) await page.addInitScript(FAKE, mode);
  await page.goto(`http://localhost:${port}/index.html?seqtest=1`);
  await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
  await page.waitForTimeout(800);
  return page;
}
const openAudio = async page => { await page.click('#bAud'); await page.waitForFunction(() => !document.getElementById('pAudio').hidden && document.getElementById('pAudio').offsetParent, null, { timeout: 5000 }); };
const stat = page => page.evaluate(() => { const s = document.getElementById('inEngStat'), u = document.getElementById('inEngSub'); return { st: s ? s.textContent : null, sub: u ? u.textContent : null }; });
const until = async (page, re) => { for (let i = 0; i < 40; i++) { const s = await stat(page); if (s.st && re.test(s.st)) return s; await page.waitForTimeout(100); } return stat(page); };

try {
  // not started at page load
  { const page = await open('ok'); const n = await page.evaluate(() => window.__ioStarts);
    ok(n === 0, 'the helper is not started at page load (starts: ' + n + ')');
    await openAudio(page); const s = await until(page, /native \(500R8/);
    ok(s.st === 'Input engine: native (500R8, 28 inputs)', 'ready: "' + s.st + '"');
    ok(/Core Audio · thunder_io 1 \/ miniaudio 0.11.26/.test(s.sub), 'detail line: "' + s.sub + '"');
    ok(await page.evaluate(() => window.__ioStarts) === 1, 'start() called once');
    await page.click('#segInEng button:nth-child(2)');
    const b = await stat(page); ok(b.st === 'Input engine: browser (inputs 1–2)', 'BROWSER: "' + b.st + '"');
    await page.reload(); await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 }); await page.waitForTimeout(500);
    ok(await page.evaluate(() => window.__ioStarts) === 0, 'BROWSER persisted: no helper start after reload');
    await openAudio(page); const r = await stat(page);
    ok(r.st === 'Input engine: browser (inputs 1–2)', 'BROWSER persists across reload: "' + r.st + '"');
    ok(await page.evaluate(() => window.__ioStarts) === 0, 'BROWSER: opening Audio does not start the helper');
    await page.close(); }
  // unavailable
  { const page = await open('off'); await openAudio(page); const s = await until(page, /not available/);
    ok(s.st === 'Input engine: browser — native helper not available: addon missing', 'unavailable: "' + s.st + '"'); await page.close(); }
  // no answer (3 s)
  { const page = await open('silent'); await openAudio(page); const c = await until(page, /checking/);
    ok(c.st === 'Input engine: checking…', 'while starting: "' + c.st + '"');
    await page.waitForTimeout(3300); const s = await stat(page);
    ok(s.st === 'Input engine: browser — native helper not available: no answer', 'no hello in 3 s: "' + s.st + '"'); await page.close(); }
  // the website
  { const page = await open(null); await openAudio(page); await page.waitForTimeout(500);
    const r = await page.evaluate(() => ({ row: !!document.getElementById('inEngRow'), seg: !!document.getElementById('segInEng'), txt: /input engine/i.test(document.body.innerText), io: (window.thunderDesktop || null) }));
    ok(!r.row && !r.seg, 'website: no INPUT ENGINE row');
    ok(!r.txt, "website: no 'Input engine' text on the page");
    const all = await page.evaluate(() => document.getElementById('pAudio').textContent + document.getElementById('pAudio').innerHTML);
    ok(!/input engine/i.test(all), "website: no 'Input engine' text in Settings › Audio");
    ok(r.io === null, 'website: no thunderDesktop'); await page.close(); }
} catch (e) { console.error(e); fails++; }
await browser.close(); srv.close();
console.log(fails ? fails + ' FAILED' : 'all ok'); process.exit(fails ? 1 : 0);
