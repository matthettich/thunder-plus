// NATIVE INPUT ENGINE, the page's side against a FAKE helper (no Electron, no native addon): a stand-in for
// window.thunderDesktop.io whose start() hands the page a MessagePort that plays the helper (hello, devices, now, open,
// a stream of 28-channel chunks where channel c is the constant 0.01*(c+1), close).
//   - Settings › Audio has no 'Input engine' text any more (desktop and website)
//   - desktop, Record a sample: Engine row (Native selected), Input lists "500R8 (native, 28 inputs)", Inputs lists every
//     pair and channel, no "(all the app is given)"
//   - IN 17+18 recorded FREE: the take's left / right are channels 17 and 18 (0.17 / 0.18)
//   - Engine: Browser lists the browser's inputs again and closes the helper's input
//   - a helper that says available:false: the browser's list and one line saying why
//   - the website (no thunderDesktop): no Engine row, no 'Input engine' text, nothing throws
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
  window.__ioStarts = 0; window.__ioOps = [];
  window.thunderDesktop = { io: { available: true, start() {
    window.__ioStarts++;
    if (mode === 'silent') return Promise.resolve({ ok: true });
    setTimeout(() => {
      const ch = new MessageChannel(), h = ch.port1; let timer = null, frame0 = 0;
      const stop = () => { if (timer) clearInterval(timer); timer = null; };
      h.onmessage = e => { const m = e.data; if (m && m.op) window.__ioOps.push(m.op);
        if (m && m.op === 'devices') h.postMessage({ op: 'devices', id: m.id, devices: [
          { name: 'Built-in', inputs: 2, outputs: 2, backend: 'Core Audio', inId: 'in:bb', outId: 'out:bb', id: 'in:bb', rates: [48000], isDefaultIn: false, isDefaultOut: false },
          { name: '500R8', inputs: 28, outputs: 28, backend: 'Core Audio', inId: 'in:aa', outId: 'out:aa', id: 'in:aa', rates: [44100, 48000], isDefaultIn: true, isDefaultOut: true }] });
        else if (m && m.op === 'now') h.postMessage({ op: 'now', id: m.id, ns: performance.now() * 1e6 });
        else if (m && m.op === 'open') {
          const rate = m.rate; stop(); frame0 = 0;
          h.postMessage({ op: 'opened', id: m.id, ok: true, rate, channels: 28, deviceRate: rate, block: 256, latencyFrames: 256, name: '500R8' });
          timer = setInterval(() => {
            const frames = 256, data = new Float32Array(frames * 28);
            for (let f = 0; f < frames; f++) for (let c = 0; c < 28; c++) data[f * 28 + c] = 0.01 * (c + 1);
            h.postMessage({ op: 'in', frame0, hostNs: performance.now() * 1e6 - (frames / rate) * 1e9, channels: 28, frames, data, overruns: 0 }, [data.buffer]);
            frame0 += frames;
          }, 5);
        } else if (m && m.op === 'close') { stop(); h.postMessage({ op: 'closed', id: m.id }); } };
      h.postMessage(mode === 'ok' ? { op: 'hello', available: true, version: 'thunder_io 1 / miniaudio 0.11.26' } : { op: 'hello', available: false, why: 'addon missing' });
      window.postMessage({ thunderIo: true }, '*', [ch.port2]);
    }, 20);
    return Promise.resolve({ ok: true });
  } } };
};

let fails = 0;
const ok = (c, msg) => { console.log((c ? 'ok   ' : 'FAIL ') + msg); if (!c) fails++; };
const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required', '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'] });
async function open(mode) {
  const page = await (await browser.newContext({ permissions: ['microphone'], viewport: { width: 1400, height: 900 } })).newPage();
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
const openRec = async page => { await page.evaluate(() => window.__seqTest.ev('rcdPanel(0)')); await page.waitForSelector('.recask .recset', { timeout: 5000 }); };
// the window's rows: label -> {text, options[], value (select), on (seg)}
const rows = page => page.evaluate(() => { const o = {}; document.querySelectorAll('.recset > label').forEach(f => { const k = f.children[0].textContent, c = f.children[1];
  o[k] = c.tagName === 'SELECT' ? { opts: [...c.options].map(x => x.text), val: c.options[c.selectedIndex] && c.options[c.selectedIndex].text, dis: c.disabled } : { on: [...c.querySelectorAll('button.on')].map(b => b.textContent) }; }); return o; });
const until = async (page, f) => { for (let i = 0; i < 50; i++) { const r = await rows(page); if (f(r)) return r; await page.waitForTimeout(100); } return rows(page); };
const noEngine = page => page.evaluate(() => /input engine/i.test(document.getElementById('pAudio').textContent + document.getElementById('pAudio').innerHTML));

try {
  // desktop, native
  { const page = await open('ok');
    ok(await page.evaluate(() => window.__ioStarts) === 0, 'the helper is not started at page load');
    await openAudio(page); await page.waitForTimeout(500);
    ok(!(await noEngine(page)), "desktop: no 'Input engine' text in Settings › Audio");
    ok(await page.evaluate(() => window.__ioStarts) === 0, 'desktop: opening Settings › Audio does not start the helper');
    await page.click('#bAud'); await page.waitForFunction(() => document.getElementById('pAudio').hidden || !document.getElementById('pAudio').offsetParent);
    await openRec(page);
    const r = await until(page, r => r.Input && r.Input.opts.some(t => /native/.test(t)));
    ok(r.Engine && r.Engine.on.join() === 'Native', 'Engine row shows Native selected: ' + JSON.stringify(r.Engine));
    ok(r.Input && r.Input.val === '500R8 (native, 28 inputs)', 'Input: ' + (r.Input && r.Input.opts.join(' | ')));
    ok(r.Inputs && r.Inputs.opts.includes('IN 27+28') && r.Inputs.opts.includes('IN 28 (mono)') && r.Inputs.opts.length === 14 + 28, 'Inputs lists every pair and channel (' + (r.Inputs && r.Inputs.opts.length) + ')');
    ok(!/all the app is given/.test(JSON.stringify(r)), "no '(all the app is given)' on the native path");
    ok(await page.evaluate(() => window.__ioStarts) === 1, 'the helper was started once, by the window');
    // IN 17+18, a FREE take of ~0.5 s
    await page.selectOption('.recset label:has(> span:text-is("Inputs")) select', 'p16');
    const take = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E("rcdSet.mode = 'free'; rcdSet.ch = 'stereo'; rcdSet.lat = 0; RCD.take = null;");
      await E('rcdStart()'); await wait(500); E('rcdStop()');
      for (let i = 0; i < 80 && !E('!!RCD.take'); i++) await wait(50);
      const lat = document.querySelector('.recset b') && [...document.querySelectorAll('.recset b')].map(b => b.textContent).join('|');
      return E('RCD.take ? (() => { const t = RCD.take, n = t.data.length; let l = 0, r = 0; for (let i = 0; i < n; i++){ l = Math.max(l, t.data[i]); r = Math.max(r, t.data2 ? t.data2[i] : NaN); } return {n, sr:t.sr, l, r, io:!!RCD.io, lat:' + JSON.stringify(lat) + '}; })() : null'); });
    ok(take && take.n > take.sr * 0.3 && Math.abs(take.l - 0.17) < 0.005 && Math.abs(take.r - 0.18) < 0.005, 'IN 17+18 FREE take: ' + JSON.stringify(take));
    ok(take && take.io && /native input, placed by its timestamps/.test(take.lat), 'latency line says native once the input is open: ' + (take && take.lat));
    // Engine: Browser
    await page.evaluate(() => { window.__ioOps.length = 0; });
    await page.locator('.recset .seg').first().getByText('Browser', { exact: true }).click();
    const b = await until(page, r => r.Input && r.Input.opts[0] === 'Default input');
    ok(b.Input.opts[0] === 'Default input' && !b.Input.opts.some(t => /native/.test(t)), 'Browser: the Input list is the browser’s: ' + b.Input.opts.join(' | '));
    ok(b.Engine.on.join() === 'Browser', 'Browser selected');
    ok((await page.evaluate(() => window.__ioOps)).includes('close'), "Browser: 'close' sent to the helper");
    ok(await page.evaluate(() => window.__seqTest.ev('aset.inEngine')) === 'browser', 'Browser saved in the settings');
    await page.close(); }
  // unavailable
  { const page = await open('off'); await openRec(page);
    const r = await until(page, r => r.Input && r.Input.opts.length);
    ok(r.Input.opts[0] === 'Default input', 'helper unavailable: the browser’s Input list');
    const st = await page.evaluate(() => document.querySelector('.recstate').textContent);
    ok(st === 'Native input not available (addon missing): using the browser’s input.', 'reason shown: "' + st + '"'); await page.close(); }
  // the website
  { const page = await open(null); await openAudio(page); await page.waitForTimeout(300);
    ok(!(await noEngine(page)), "website: no 'Input engine' text in Settings › Audio");
    await page.click('#bAud'); await page.waitForFunction(() => document.getElementById('pAudio').hidden || !document.getElementById('pAudio').offsetParent);
    await openRec(page); const r = await rows(page);
    ok(!r.Engine && r.Input && r.Inputs, 'website: no Engine row in Record a sample (' + Object.keys(r).join(', ') + ')');
    const txt = await page.evaluate(() => ({ t: /input engine/i.test(document.body.innerText), io: window.thunderDesktop || null }));
    ok(!txt.t, "website: no 'Input engine' text on the page"); ok(txt.io === null, 'website: no thunderDesktop'); await page.close(); }
} catch (e) { console.error(e); fails++; }
await browser.close(); srv.close();
console.log(fails ? fails + ' FAILED' : 'all ok'); process.exit(fails ? 1 : 0);
